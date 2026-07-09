// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/AssetDocumentGraphParser.h"

#include "AssetDocumentJsonRegionUtils.h"

namespace
{
const TCHAR* GraphFields[] = {
	TEXT("Id"),
	TEXT("Kind"),
	TEXT("Owner"),
	TEXT("OwnerNodeId"),
	TEXT("OwnerPin"),
	TEXT("Name"),
	TEXT("Schema"),
	TEXT("GraphGuid"),
	TEXT("Category"),
	TEXT("Description"),
	TEXT("Signature"),
	TEXT("EntryPins"),
	TEXT("ResultPins"),
	TEXT("Position"),
	TEXT("Metadata"),
	TEXT("Diagnostics"),
	TEXT("Skipped"),
	TEXT("_Skipped"),
	TEXT("Evidence"),
	TEXT("Nodes"),
	TEXT("Links"),
	TEXT("Subgraphs")
};

const TCHAR* NodeFields[] = {
	TEXT("Id"),
	TEXT("NodeGuid"),
	TEXT("Class"),
	TEXT("Capability"),
	TEXT("Kind"),
	TEXT("Spawner"),
	TEXT("Fields"),
	TEXT("Pins"),
	TEXT("Member"),
	TEXT("PinOverrides"),
	TEXT("Position"),
	TEXT("SubgraphRefs"),
	TEXT("Evidence"),
	TEXT("Comment")
};

const TCHAR* GraphRegionFields[] = {
	TEXT("Graphs")
};

const TCHAR* PinOverrideFields[] = {
	TEXT("Pin"),
	TEXT("Direction"),
	TEXT("Type"),
	TEXT("DefaultValue"),
	TEXT("DefaultObject"),
	TEXT("DefaultTextValue"),
	TEXT("Hidden"),
	TEXT("AdvancedView")
};

const TCHAR* LinkFields[] = {
	TEXT("From"),
	TEXT("To")
};

const TCHAR* LinkEndpointFields[] = {
	TEXT("Node"),
	TEXT("Pin")
};

const TCHAR* GraphOwnerFields[] = {
	TEXT("StateMachine"),
	TEXT("State"),
	TEXT("Transition"),
	TEXT("Layer"),
	TEXT("Function"),
	TEXT("Macro"),
	TEXT("ParentGraph"),
	TEXT("Graph"),
	TEXT("Node"),
	TEXT("OwnerNodeId"),
	TEXT("OwnerPin"),
	TEXT("ParentNode"),
	TEXT("Pin")
};

bool IsKnownField(const FString& Field, const TCHAR* const* KnownFields, int32 KnownFieldCount)
{
	for (int32 Index = 0; Index < KnownFieldCount; ++Index)
	{
		if (Field == KnownFields[Index])
		{
			return true;
		}
	}
	return false;
}

bool IsSidecarId(const FString& Value)
{
	if (Value.IsEmpty())
	{
		return false;
	}

	const TCHAR First = Value[0];
	if (!FChar::IsAlpha(First) && First != TEXT('_'))
	{
		return false;
	}

	for (int32 Index = 1; Index < Value.Len(); ++Index)
	{
		const TCHAR Character = Value[Index];
		if (!FChar::IsAlnum(Character) && Character != TEXT('_') && Character != TEXT('-'))
		{
			return false;
		}
	}
	return true;
}

bool IsKnownGraphKind(const FString& Kind)
{
	return Kind == TEXT("AnimGraph") ||
		Kind == TEXT("StateMachine") ||
		Kind == TEXT("StatePose") ||
		Kind == TEXT("TransitionRule") ||
		Kind == TEXT("TransitionBlend") ||
		Kind == TEXT("AnimLayer") ||
		Kind == TEXT("FunctionGraph") ||
		Kind == TEXT("MacroGraph");
}

FString JoinPath(const FString& BasePath, const FString& Segment)
{
	const FString EscapedSegment = FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Segment);
	if (BasePath.IsEmpty())
	{
		return FString::Printf(TEXT("/%s"), *EscapedSegment);
	}
	return FString::Printf(TEXT("%s/%s"), *BasePath, *EscapedSegment);
}

FString IndexPath(const FString& BasePath, int32 Index)
{
	return JoinPath(BasePath, FString::FromInt(Index));
}

TSharedPtr<FJsonValue> CloneJsonValue(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return nullptr;
	}

	switch (Value->Type)
	{
	case EJson::Object:
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		TSharedRef<FJsonObject> Clone = MakeShared<FJsonObject>();
		if (Object.IsValid())
		{
			TArray<FString> Keys;
			Object->Values.GetKeys(Keys);
			Keys.Sort();
			for (const FString& Key : Keys)
			{
				Clone->SetField(Key, CloneJsonValue(Object->Values[Key]));
			}
		}
		return MakeShared<FJsonValueObject>(Clone);
	}
	case EJson::Array:
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
		{
			Array.Add(CloneJsonValue(Item));
		}
		return MakeShared<FJsonValueArray>(MoveTemp(Array));
	}
	case EJson::String:
		return MakeShared<FJsonValueString>(Value->AsString());
	case EJson::Number:
		return MakeShared<FJsonValueNumber>(Value->AsNumber());
	case EJson::Boolean:
		return MakeShared<FJsonValueBoolean>(Value->AsBool());
	case EJson::Null:
	default:
		return MakeShared<FJsonValueNull>();
	}
}

TSharedPtr<FJsonObject> CloneJsonObject(const TSharedPtr<FJsonObject>& Object)
{
	if (!Object.IsValid())
	{
		return nullptr;
	}

	TSharedRef<FJsonObject> Clone = MakeShared<FJsonObject>();
	TArray<FString> Keys;
	Object->Values.GetKeys(Keys);
	Keys.Sort();
	for (const FString& Key : Keys)
	{
		Clone->SetField(Key, CloneJsonValue(Object->Values[Key]));
	}
	return Clone;
}

void ValidateUnknownFields(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	const TCHAR* const* KnownFields,
	int32 KnownFieldCount,
	const FString& Code,
	FAssetDocumentGraphParseResult& Result)
{
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		if (!IsKnownField(Pair.Key, KnownFields, KnownFieldCount))
		{
			Result.AddDiagnostic(
				Code,
				JoinPath(Path, Pair.Key),
				FString::Printf(TEXT("Unknown graph field '%s'."), *Pair.Key));
		}
	}
}

bool TryGetStringField(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* Field,
	const FString& Path,
	const FString& MissingCode,
	FString& OutValue,
	FAssetDocumentGraphParseResult& Result)
{
	if (!Object->TryGetStringField(Field, OutValue) || OutValue.IsEmpty())
	{
		Result.AddDiagnostic(
			MissingCode,
			JoinPath(Path, Field),
			FString::Printf(TEXT("Required string field '%s' is missing or empty."), Field));
		return false;
	}
	return true;
}

bool TryGetObjectFieldIfPresent(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* Field,
	const FString& Path,
	const FString& InvalidTypeCode,
	TSharedPtr<FJsonObject>& OutObject,
	FAssetDocumentGraphParseResult& Result)
{
	const TSharedPtr<FJsonValue>* Value = Object->Values.Find(Field);
	if (!Value)
	{
		return false;
	}

	if (!Value->IsValid() || (*Value)->Type != EJson::Object)
	{
		Result.AddDiagnostic(
			InvalidTypeCode,
			JoinPath(Path, Field),
			FString::Printf(TEXT("Field '%s' must be an object."), Field));
		return false;
	}

	OutObject = (*Value)->AsObject();
	return OutObject.IsValid();
}

bool TryGetNullableObjectFieldIfPresent(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* Field,
	const FString& Path,
	const FString& InvalidTypeCode,
	TSharedPtr<FJsonObject>& OutObject,
	FAssetDocumentGraphParseResult& Result)
{
	const TSharedPtr<FJsonValue>* Value = Object->Values.Find(Field);
	if (!Value)
	{
		return false;
	}

	if (!Value->IsValid() || (*Value)->Type == EJson::Null)
	{
		OutObject.Reset();
		return true;
	}

	if ((*Value)->Type != EJson::Object)
	{
		Result.AddDiagnostic(
			InvalidTypeCode,
			JoinPath(Path, Field),
			FString::Printf(TEXT("Field '%s' must be an object or null."), Field));
		return false;
	}

	OutObject = (*Value)->AsObject();
	return OutObject.IsValid();
}

bool TryGetArrayFieldIfPresent(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* Field,
	const FString& Path,
	const FString& InvalidTypeCode,
	const TArray<TSharedPtr<FJsonValue>>*& OutArray,
	FAssetDocumentGraphParseResult& Result)
{
	const TSharedPtr<FJsonValue>* Value = Object->Values.Find(Field);
	if (!Value)
	{
		return false;
	}

	if (!Value->IsValid() || (*Value)->Type != EJson::Array)
	{
		Result.AddDiagnostic(
			InvalidTypeCode,
			JoinPath(Path, Field),
			FString::Printf(TEXT("Field '%s' must be an array."), Field));
		return false;
	}

	OutArray = &(*Value)->AsArray();
	return true;
}

bool TryGetRequiredArrayField(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* Field,
	const FString& Path,
	const FString& InvalidTypeCode,
	const TArray<TSharedPtr<FJsonValue>>*& OutArray,
	FAssetDocumentGraphParseResult& Result)
{
	if (!TryGetArrayFieldIfPresent(Object, Field, Path, InvalidTypeCode, OutArray, Result))
	{
		Result.AddDiagnostic(
			InvalidTypeCode,
			JoinPath(Path, Field),
			FString::Printf(TEXT("%s must be an array."), Field));
		return false;
	}
	return true;
}

bool TryGetStringFieldIfPresent(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* Field,
	const FString& Path,
	const FString& InvalidTypeCode,
	FString& OutValue,
	FAssetDocumentGraphParseResult& Result)
{
	const TSharedPtr<FJsonValue>* Value = Object->Values.Find(Field);
	if (!Value)
	{
		return true;
	}

	if (!Value->IsValid() || (*Value)->Type != EJson::String)
	{
		Result.AddDiagnostic(
			InvalidTypeCode,
			JoinPath(Path, Field),
			FString::Printf(TEXT("Field '%s' must be a string."), Field));
		return false;
	}

	OutValue = (*Value)->AsString();
	return true;
}

bool TryGetBoolFieldIfPresent(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* Field,
	const FString& Path,
	const FString& InvalidTypeCode,
	TOptional<bool>& OutValue,
	FAssetDocumentGraphParseResult& Result)
{
	const TSharedPtr<FJsonValue>* Value = Object->Values.Find(Field);
	if (!Value)
	{
		return true;
	}

	if (!Value->IsValid() || (*Value)->Type != EJson::Boolean)
	{
		Result.AddDiagnostic(
			InvalidTypeCode,
			JoinPath(Path, Field),
			FString::Printf(TEXT("Field '%s' must be a boolean."), Field));
		return false;
	}

	OutValue = (*Value)->AsBool();
	return true;
}

bool TryCloneAnyFieldIfPresent(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* Field,
	TSharedPtr<FJsonValue>& OutValue)
{
	const TSharedPtr<FJsonValue>* Value = Object->Values.Find(Field);
	if (!Value)
	{
		return false;
	}

	OutValue = CloneJsonValue(*Value);
	return true;
}

struct FGraphParseContext
{
	TArray<TPair<FString, FString>> GraphStack;
};

bool GraphStackContainsKindAndId(
	const FGraphParseContext& Context,
	const FString& Kind,
	const FString& Id)
{
	for (const TPair<FString, FString>& GraphIdentity : Context.GraphStack)
	{
		if (GraphIdentity.Key == Kind && GraphIdentity.Value == Id)
		{
			return true;
		}
	}
	return false;
}

bool ValidateOwnerIdField(
	const TSharedRef<FJsonObject>& Owner,
	const FString& OwnerPath,
	const TCHAR* Field,
	FAssetDocumentGraphParseResult& Result,
	FString& OutValue)
{
	const TSharedPtr<FJsonValue>* Value = Owner->Values.Find(Field);
	if (!Value)
	{
		return true;
	}

	if (!Value->IsValid() || (*Value)->Type != EJson::String)
	{
		Result.AddDiagnostic(
			TEXT("InvalidGraphOwner"),
			JoinPath(OwnerPath, Field),
			FString::Printf(TEXT("Owner field '%s' must be a string stable id."), Field));
		return false;
	}

	OutValue = (*Value)->AsString();
	if (!IsSidecarId(OutValue))
	{
		Result.AddDiagnostic(
			TEXT("InvalidGraphOwner"),
			JoinPath(OwnerPath, Field),
			FString::Printf(TEXT("Owner field '%s' must match ^[A-Za-z_][A-Za-z0-9_-]*$."), Field));
		return false;
	}

	return true;
}

void ValidateGraphOwner(
	const TSharedPtr<FJsonObject>& Owner,
	const FString& GraphPath,
	const FGraphParseContext& Context,
	FAssetDocumentGraphParseResult& Result)
{
	if (!Owner.IsValid())
	{
		return;
	}

	const FString OwnerPath = JoinPath(GraphPath, TEXT("Owner"));
	ValidateUnknownFields(
		Owner.ToSharedRef(),
		OwnerPath,
		GraphOwnerFields,
		UE_ARRAY_COUNT(GraphOwnerFields),
		TEXT("UnknownGraphOwnerField"),
		Result);

	FString StateMachineId;
	if (ValidateOwnerIdField(Owner.ToSharedRef(), OwnerPath, TEXT("StateMachine"), Result, StateMachineId) &&
		!StateMachineId.IsEmpty() &&
		!GraphStackContainsKindAndId(Context, TEXT("StateMachine"), StateMachineId))
	{
		Result.AddDiagnostic(
			TEXT("UnknownGraphOwnerReference"),
			JoinPath(OwnerPath, TEXT("StateMachine")),
			FString::Printf(TEXT("Owner StateMachine '%s' does not refer to the current graph family."), *StateMachineId));
	}

	const TCHAR* IdOwnerFields[] = {
		TEXT("State"),
		TEXT("Transition"),
		TEXT("Layer"),
		TEXT("Function"),
		TEXT("Macro"),
		TEXT("ParentGraph"),
		TEXT("Graph"),
		TEXT("Node"),
		TEXT("OwnerNodeId"),
		TEXT("OwnerPin"),
		TEXT("ParentNode"),
		TEXT("Pin")
	};
	for (const TCHAR* Field : IdOwnerFields)
	{
		FString Ignored;
		ValidateOwnerIdField(Owner.ToSharedRef(), OwnerPath, Field, Result, Ignored);
	}
}

bool ParseSharedGraphFields(
	const TSharedRef<FJsonObject>& GraphObject,
	const FString& GraphPath,
	FAssetDocumentGraphSpec& OutGraph,
	FAssetDocumentGraphParseResult& Result)
{
	bool bParsed = true;
	if (!TryGetStringFieldIfPresent(
			GraphObject,
			TEXT("OwnerNodeId"),
			GraphPath,
			TEXT("InvalidGraphOwner"),
			OutGraph.OwnerNodeId,
			Result) ||
		(!OutGraph.OwnerNodeId.IsEmpty() && !IsSidecarId(OutGraph.OwnerNodeId)))
	{
		Result.AddDiagnostic(
			TEXT("InvalidGraphOwner"),
			JoinPath(GraphPath, TEXT("OwnerNodeId")),
			TEXT("OwnerNodeId must match ^[A-Za-z_][A-Za-z0-9_-]*$."));
		bParsed = false;
	}
	if (!TryGetStringFieldIfPresent(
			GraphObject,
			TEXT("OwnerPin"),
			GraphPath,
			TEXT("InvalidGraphOwner"),
			OutGraph.OwnerPin,
			Result) ||
		(!OutGraph.OwnerPin.IsEmpty() && !IsSidecarId(OutGraph.OwnerPin)))
	{
		Result.AddDiagnostic(
			TEXT("InvalidGraphOwner"),
			JoinPath(GraphPath, TEXT("OwnerPin")),
			TEXT("OwnerPin must match ^[A-Za-z_][A-Za-z0-9_-]*$."));
		bParsed = false;
	}

	TryCloneAnyFieldIfPresent(GraphObject, TEXT("EntryPins"), OutGraph.EntryPins);
	TryCloneAnyFieldIfPresent(GraphObject, TEXT("ResultPins"), OutGraph.ResultPins);
	TryCloneAnyFieldIfPresent(GraphObject, TEXT("Metadata"), OutGraph.Metadata);
	TryCloneAnyFieldIfPresent(GraphObject, TEXT("Diagnostics"), OutGraph.Diagnostics);
	TryCloneAnyFieldIfPresent(GraphObject, TEXT("Skipped"), OutGraph.Skipped);
	TryCloneAnyFieldIfPresent(GraphObject, TEXT("_Skipped"), OutGraph.UnderscoreSkipped);
	return bParsed;
}

bool TryParseEndpointObject(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	FAssetDocumentGraphEndpoint& OutEndpoint,
	FAssetDocumentGraphParseResult& Result)
{
	ValidateUnknownFields(
		Object,
		Path,
		LinkEndpointFields,
		UE_ARRAY_COUNT(LinkEndpointFields),
		TEXT("UnknownGraphLinkEndpointField"),
		Result);

	if (!TryGetStringField(Object, TEXT("Node"), Path, TEXT("InvalidGraphLinkEndpointSyntax"), OutEndpoint.Node, Result) ||
		!TryGetStringField(Object, TEXT("Pin"), Path, TEXT("InvalidGraphLinkEndpointSyntax"), OutEndpoint.Pin, Result))
	{
		return false;
	}

	if (!IsSidecarId(OutEndpoint.Node))
	{
		Result.AddDiagnostic(
			TEXT("InvalidGraphNodeId"),
			JoinPath(Path, TEXT("Node")),
			TEXT("Expanded link endpoint node id must match ^[A-Za-z_][A-Za-z0-9_-]*$."));
		return false;
	}

	if (!IsSidecarId(OutEndpoint.Pin))
	{
		Result.AddDiagnostic(
			TEXT("InvalidGraphPinId"),
			JoinPath(Path, TEXT("Pin")),
			TEXT("Expanded link endpoint pin id must match ^[A-Za-z_][A-Za-z0-9_-]*$."));
		return false;
	}
	return true;
}

bool TryParseCompactEndpoint(
	const FString& Endpoint,
	const FString& Path,
	FAssetDocumentGraphEndpoint& OutEndpoint,
	FAssetDocumentGraphParseResult& Result)
{
	TArray<FString> Parts;
	Endpoint.ParseIntoArray(Parts, TEXT("."), false);
	if (Parts.Num() != 2 || Parts[0].IsEmpty() || Parts[1].IsEmpty())
	{
		Result.AddDiagnostic(
			TEXT("InvalidGraphLinkEndpointSyntax"),
			Path,
			TEXT("Compact link endpoint must use exactly one dot in Node.Pin form."));
		return false;
	}

	OutEndpoint.Node = Parts[0];
	OutEndpoint.Pin = Parts[1];
	if (!IsSidecarId(OutEndpoint.Node))
	{
		Result.AddDiagnostic(
			TEXT("InvalidGraphNodeId"),
			Path,
			TEXT("Compact link endpoint node id must match ^[A-Za-z_][A-Za-z0-9_-]*$."));
		return false;
	}

	if (!IsSidecarId(OutEndpoint.Pin))
	{
		Result.AddDiagnostic(
			TEXT("InvalidGraphPinId"),
			Path,
			TEXT("Compact link endpoint pin id must match ^[A-Za-z_][A-Za-z0-9_-]*$."));
		return false;
	}
	return true;
}

bool TryParseEndpoint(
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	FAssetDocumentGraphEndpoint& OutEndpoint,
	FAssetDocumentGraphParseResult& Result)
{
	if (!Value.IsValid())
	{
		Result.AddDiagnostic(TEXT("InvalidGraphLinkEndpointSyntax"), Path, TEXT("Link endpoint is missing."));
		return false;
	}

	if (Value->Type == EJson::String)
	{
		return TryParseCompactEndpoint(Value->AsString(), Path, OutEndpoint, Result);
	}

	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		if (Object.IsValid())
		{
			return TryParseEndpointObject(Object.ToSharedRef(), Path, OutEndpoint, Result);
		}
	}

	Result.AddDiagnostic(
		TEXT("InvalidGraphLinkEndpointSyntax"),
		Path,
		TEXT("Link endpoint must be an expanded object or compact Node.Pin string."));
	return false;
}

bool ParsePinOverride(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	FAssetDocumentPinOverrideSpec& OutPinOverride,
	FAssetDocumentGraphParseResult& Result)
{
	ValidateUnknownFields(
		Object,
		Path,
		PinOverrideFields,
		UE_ARRAY_COUNT(PinOverrideFields),
		TEXT("UnknownGraphPinOverrideField"),
		Result);

	if (!TryGetStringField(Object, TEXT("Pin"), Path, TEXT("InvalidGraphPinId"), OutPinOverride.Pin, Result))
	{
		return false;
	}

	if (!IsSidecarId(OutPinOverride.Pin))
	{
		Result.AddDiagnostic(
			TEXT("InvalidGraphPinId"),
			JoinPath(Path, TEXT("Pin")),
			TEXT("Pin id must match ^[A-Za-z_][A-Za-z0-9_-]*$."));
		return false;
	}

	if (!TryGetStringFieldIfPresent(
			Object,
			TEXT("Direction"),
			Path,
			TEXT("InvalidGraphPin"),
			OutPinOverride.Direction,
			Result))
	{
		return false;
	}

	if (const TSharedPtr<FJsonValue>* Type = Object->Values.Find(TEXT("Type")))
	{
		if (!Type->IsValid() || (*Type)->Type != EJson::Object)
		{
			Result.AddDiagnostic(
				TEXT("InvalidGraphPin"),
				JoinPath(Path, TEXT("Type")),
				TEXT("Pin override Type must be an object."));
			return false;
		}
		OutPinOverride.Type = CloneJsonValue(*Type);
	}
	if (const TSharedPtr<FJsonValue>* DefaultValue = Object->Values.Find(TEXT("DefaultValue")))
	{
		OutPinOverride.DefaultValue = CloneJsonValue(*DefaultValue);
	}
	if (const TSharedPtr<FJsonValue>* DefaultObject = Object->Values.Find(TEXT("DefaultObject")))
	{
		OutPinOverride.DefaultObject = CloneJsonValue(*DefaultObject);
	}
	if (const TSharedPtr<FJsonValue>* DefaultTextValue = Object->Values.Find(TEXT("DefaultTextValue")))
	{
		OutPinOverride.DefaultTextValue = CloneJsonValue(*DefaultTextValue);
	}

	if (!TryGetBoolFieldIfPresent(
			Object,
			TEXT("Hidden"),
			Path,
			TEXT("InvalidGraphPin"),
			OutPinOverride.Hidden,
			Result))
	{
		return false;
	}

	if (!TryGetBoolFieldIfPresent(
			Object,
			TEXT("AdvancedView"),
			Path,
			TEXT("InvalidGraphPin"),
			OutPinOverride.AdvancedView,
			Result))
	{
		return false;
	}
	return true;
}

bool ParseNode(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	FAssetDocumentNodeSpec& OutNode,
	FAssetDocumentGraphParseResult& Result)
{
	ValidateUnknownFields(
		Object,
		Path,
		NodeFields,
		UE_ARRAY_COUNT(NodeFields),
		TEXT("UnknownGraphNodeField"),
		Result);

	if (!TryGetStringField(Object, TEXT("Id"), Path, TEXT("InvalidGraphNodeId"), OutNode.Id, Result))
	{
		return false;
	}

	if (!IsSidecarId(OutNode.Id))
	{
		Result.AddDiagnostic(
			TEXT("InvalidGraphNodeId"),
			JoinPath(Path, TEXT("Id")),
			TEXT("Node id must match ^[A-Za-z_][A-Za-z0-9_-]*$."));
		return false;
	}

	TryGetStringField(Object, TEXT("Class"), Path, TEXT("MissingGraphNodeClass"), OutNode.Class, Result);
	Object->TryGetStringField(TEXT("NodeGuid"), OutNode.NodeGuid);
	Object->TryGetStringField(TEXT("Capability"), OutNode.Capability);
	Object->TryGetStringField(TEXT("Kind"), OutNode.Kind);
	Object->TryGetStringField(TEXT("Comment"), OutNode.Comment);
	OutNode.bHasComment = Object->HasField(TEXT("Comment"));

	TSharedPtr<FJsonObject> Spawner;
	if (TryGetObjectFieldIfPresent(Object, TEXT("Spawner"), Path, TEXT("InvalidGraphNodeSpawner"), Spawner, Result))
	{
		OutNode.Spawner = CloneJsonObject(Spawner);
	}

	TSharedPtr<FJsonObject> Fields;
	if (TryGetObjectFieldIfPresent(Object, TEXT("Fields"), Path, TEXT("InvalidGraphNodeFields"), Fields, Result))
	{
		OutNode.Fields = CloneJsonObject(Fields);
	}

	TSharedPtr<FJsonObject> Pins;
	if (TryGetObjectFieldIfPresent(Object, TEXT("Pins"), Path, TEXT("InvalidGraphNodePins"), Pins, Result))
	{
		OutNode.Pins = CloneJsonObject(Pins);
	}

	TSharedPtr<FJsonObject> Member;
	if (TryGetObjectFieldIfPresent(Object, TEXT("Member"), Path, TEXT("InvalidGraphMemberReference"), Member, Result))
	{
		OutNode.Member = CloneJsonObject(Member);
	}

	TSharedPtr<FJsonObject> Position;
	if (TryGetObjectFieldIfPresent(Object, TEXT("Position"), Path, TEXT("InvalidGraphPosition"), Position, Result))
	{
		OutNode.Position = CloneJsonObject(Position);
	}

	TSharedPtr<FJsonObject> SubgraphRefs;
	if (TryGetObjectFieldIfPresent(Object, TEXT("SubgraphRefs"), Path, TEXT("InvalidGraphSubgraphRefs"), SubgraphRefs, Result))
	{
		OutNode.SubgraphRefs = CloneJsonObject(SubgraphRefs);
	}

	TSharedPtr<FJsonObject> Evidence;
	if (TryGetObjectFieldIfPresent(Object, TEXT("Evidence"), Path, TEXT("InvalidGraphEvidence"), Evidence, Result))
	{
		OutNode.Evidence = CloneJsonObject(Evidence);
	}

	const TArray<TSharedPtr<FJsonValue>>* PinOverrides = nullptr;
	if (TryGetArrayFieldIfPresent(Object, TEXT("PinOverrides"), Path, TEXT("InvalidGraphPin"), PinOverrides, Result))
	{
		TSet<FString> PinOverrideIds;
		for (int32 Index = 0; Index < PinOverrides->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> PinObject = (*PinOverrides)[Index].IsValid()
				? (*PinOverrides)[Index]->AsObject()
				: nullptr;
			if (!PinObject.IsValid())
			{
				Result.AddDiagnostic(
					TEXT("InvalidGraphPin"),
					IndexPath(JoinPath(Path, TEXT("PinOverrides")), Index),
					TEXT("Pin override must be an object."));
				continue;
			}

			FAssetDocumentPinOverrideSpec PinOverride;
			if (ParsePinOverride(PinObject.ToSharedRef(), IndexPath(JoinPath(Path, TEXT("PinOverrides")), Index), PinOverride, Result))
			{
				if (PinOverrideIds.Contains(PinOverride.Pin))
				{
					Result.AddDiagnostic(
						TEXT("DuplicateGraphPinId"),
						JoinPath(IndexPath(JoinPath(Path, TEXT("PinOverrides")), Index), TEXT("Pin")),
						FString::Printf(TEXT("Duplicate pin override id '%s'."), *PinOverride.Pin));
				}
				else
				{
					PinOverrideIds.Add(PinOverride.Pin);
				}
				OutNode.PinOverrides.Add(MoveTemp(PinOverride));
			}
		}
	}

	return true;
}

bool ParseLink(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	FAssetDocumentLinkSpec& OutLink,
	FAssetDocumentGraphParseResult& Result)
{
	ValidateUnknownFields(
		Object,
		Path,
		LinkFields,
		UE_ARRAY_COUNT(LinkFields),
		TEXT("UnknownGraphLinkField"),
		Result);

	const TSharedPtr<FJsonValue>* From = Object->Values.Find(TEXT("From"));
	const TSharedPtr<FJsonValue>* To = Object->Values.Find(TEXT("To"));
	const bool bParsedFrom = TryParseEndpoint(From ? *From : nullptr, JoinPath(Path, TEXT("From")), OutLink.From, Result);
	const bool bParsedTo = TryParseEndpoint(To ? *To : nullptr, JoinPath(Path, TEXT("To")), OutLink.To, Result);
	return bParsedFrom && bParsedTo;
}

void ParseNodeArray(
	const TArray<TSharedPtr<FJsonValue>>& Nodes,
	const FString& GraphPath,
	FAssetDocumentGraphSpec& Graph,
	FAssetDocumentGraphParseResult& Result)
{
	TSet<FString> NodeIds;
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		const FString NodePath = IndexPath(JoinPath(GraphPath, TEXT("Nodes")), Index);
		const TSharedPtr<FJsonObject> NodeObject = Nodes[Index].IsValid() ? Nodes[Index]->AsObject() : nullptr;
		if (!NodeObject.IsValid())
		{
			Result.AddDiagnostic(TEXT("InvalidGraphNode"), NodePath, TEXT("Node entry must be an object."));
			continue;
		}

		FAssetDocumentNodeSpec Node;
		if (!ParseNode(NodeObject.ToSharedRef(), NodePath, Node, Result))
		{
			continue;
		}

		if (NodeIds.Contains(Node.Id))
		{
			Result.AddDiagnostic(
				TEXT("DuplicateGraphNodeId"),
				JoinPath(NodePath, TEXT("Id")),
				FString::Printf(TEXT("Duplicate node id '%s'."), *Node.Id));
		}
		else
		{
			NodeIds.Add(Node.Id);
		}
		Graph.Nodes.Add(MoveTemp(Node));
	}
}

void ParseLinkArray(
	const TArray<TSharedPtr<FJsonValue>>& Links,
	const FString& GraphPath,
	FAssetDocumentGraphSpec& Graph,
	FAssetDocumentGraphParseResult& Result)
{
	TSet<FString> LinkKeys;
	for (int32 Index = 0; Index < Links.Num(); ++Index)
	{
		const FString LinkPath = IndexPath(JoinPath(GraphPath, TEXT("Links")), Index);
		const TSharedPtr<FJsonObject> LinkObject = Links[Index].IsValid() ? Links[Index]->AsObject() : nullptr;
		if (!LinkObject.IsValid())
		{
			Result.AddDiagnostic(TEXT("InvalidGraphLink"), LinkPath, TEXT("Link entry must be an object."));
			continue;
		}

		FAssetDocumentLinkSpec Link;
		if (!ParseLink(LinkObject.ToSharedRef(), LinkPath, Link, Result))
		{
			continue;
		}

		const FString LinkKey = Link.ToKey();
		if (LinkKeys.Contains(LinkKey))
		{
			Result.AddDiagnostic(
				TEXT("DuplicateGraphLink"),
				LinkPath,
				FString::Printf(TEXT("Duplicate graph link '%s'."), *LinkKey));
		}
		else
		{
			LinkKeys.Add(LinkKey);
		}
		Graph.Links.Add(MoveTemp(Link));
	}
}

bool ParseRecursiveGraph(
	const TSharedRef<FJsonObject>& GraphObject,
	const FString& GraphPath,
	bool bRejectUnknownGraphFields,
	const FGraphParseContext& Context,
	FAssetDocumentGraphSpec& OutGraph,
	FAssetDocumentGraphParseResult& Result);

void ParseSubgraphArray(
	const TArray<TSharedPtr<FJsonValue>>& Subgraphs,
	const FString& GraphPath,
	bool bRejectUnknownGraphFields,
	const FGraphParseContext& Context,
	FAssetDocumentGraphSpec& Graph,
	FAssetDocumentGraphParseResult& Result)
{
	TSet<FString> GraphIds;
	for (int32 Index = 0; Index < Subgraphs.Num(); ++Index)
	{
		const FString SubgraphPath = IndexPath(JoinPath(GraphPath, TEXT("Subgraphs")), Index);
		const TSharedPtr<FJsonObject> SubgraphObject = Subgraphs[Index].IsValid() ? Subgraphs[Index]->AsObject() : nullptr;
		if (!SubgraphObject.IsValid())
		{
			Result.AddDiagnostic(TEXT("InvalidGraphRegionType"), SubgraphPath, TEXT("Subgraph entry must be an object."));
			continue;
		}

		FAssetDocumentGraphSpec Subgraph;
		FGraphParseContext SubgraphContext = Context;
		SubgraphContext.GraphStack.Add(TPair<FString, FString>(Graph.Kind, Graph.Id));
		if (!ParseRecursiveGraph(SubgraphObject.ToSharedRef(), SubgraphPath, bRejectUnknownGraphFields, SubgraphContext, Subgraph, Result))
		{
			continue;
		}

		if (GraphIds.Contains(Subgraph.Id))
		{
			Result.AddDiagnostic(
				TEXT("DuplicateGraphId"),
				JoinPath(SubgraphPath, TEXT("Id")),
				FString::Printf(TEXT("Duplicate graph id '%s'."), *Subgraph.Id));
		}
		else
		{
			GraphIds.Add(Subgraph.Id);
		}
		Graph.Subgraphs.Add(MoveTemp(Subgraph));
	}
}

bool ParseRecursiveGraph(
	const TSharedRef<FJsonObject>& GraphObject,
	const FString& GraphPath,
	bool bRejectUnknownGraphFields,
	const FGraphParseContext& Context,
	FAssetDocumentGraphSpec& OutGraph,
	FAssetDocumentGraphParseResult& Result)
{
	if (bRejectUnknownGraphFields)
	{
		ValidateUnknownFields(
			GraphObject,
			GraphPath,
			GraphFields,
			UE_ARRAY_COUNT(GraphFields),
			TEXT("UnknownGraphField"),
			Result);
	}

	bool bParsed = true;
	bParsed &= TryGetStringField(GraphObject, TEXT("Id"), GraphPath, TEXT("MissingGraphId"), OutGraph.Id, Result);
	bParsed &= TryGetStringField(GraphObject, TEXT("Kind"), GraphPath, TEXT("MissingGraphKind"), OutGraph.Kind, Result);
	if (!OutGraph.Id.IsEmpty() && !IsSidecarId(OutGraph.Id))
	{
		Result.AddDiagnostic(
			TEXT("InvalidGraphId"),
			JoinPath(GraphPath, TEXT("Id")),
			TEXT("Graph id must match ^[A-Za-z_][A-Za-z0-9_-]*$."));
		bParsed = false;
	}
	if (!OutGraph.Kind.IsEmpty() && !IsKnownGraphKind(OutGraph.Kind))
	{
		Result.AddDiagnostic(
			TEXT("UnknownGraphKind"),
			JoinPath(GraphPath, TEXT("Kind")),
			FString::Printf(TEXT("Unknown graph kind '%s'."), *OutGraph.Kind));
		bParsed = false;
	}
	bParsed &= ParseSharedGraphFields(GraphObject, GraphPath, OutGraph, Result);
	OutGraph.Name = OutGraph.Id;
	GraphObject->TryGetStringField(TEXT("Name"), OutGraph.Name);
	GraphObject->TryGetStringField(TEXT("Schema"), OutGraph.Schema);
	GraphObject->TryGetStringField(TEXT("GraphGuid"), OutGraph.GraphGuid);
	GraphObject->TryGetStringField(TEXT("Category"), OutGraph.Category);
	GraphObject->TryGetStringField(TEXT("Description"), OutGraph.Description);

	TSharedPtr<FJsonObject> Owner;
	if (TryGetNullableObjectFieldIfPresent(GraphObject, TEXT("Owner"), GraphPath, TEXT("InvalidGraphOwner"), Owner, Result) && Owner.IsValid())
	{
		FGraphParseContext OwnerContext = Context;
		OwnerContext.GraphStack.Add(TPair<FString, FString>(OutGraph.Kind, OutGraph.Id));
		ValidateGraphOwner(Owner, GraphPath, OwnerContext, Result);
		OutGraph.Owner = CloneJsonObject(Owner);
	}

	TSharedPtr<FJsonObject> Signature;
	if (TryGetObjectFieldIfPresent(GraphObject, TEXT("Signature"), GraphPath, TEXT("InvalidGraphSignature"), Signature, Result))
	{
		OutGraph.Signature = CloneJsonObject(Signature);
	}

	TSharedPtr<FJsonObject> Position;
	if (TryGetObjectFieldIfPresent(GraphObject, TEXT("Position"), GraphPath, TEXT("InvalidGraphPosition"), Position, Result))
	{
		OutGraph.Position = CloneJsonObject(Position);
	}

	TSharedPtr<FJsonObject> Evidence;
	if (TryGetObjectFieldIfPresent(GraphObject, TEXT("Evidence"), GraphPath, TEXT("InvalidGraphEvidence"), Evidence, Result))
	{
		OutGraph.Evidence = CloneJsonObject(Evidence);
	}

	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (TryGetRequiredArrayField(GraphObject, TEXT("Nodes"), GraphPath, TEXT("InvalidGraphRegionType"), Nodes, Result))
	{
		ParseNodeArray(*Nodes, GraphPath, OutGraph, Result);
	}
	else
	{
		bParsed = false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Links = nullptr;
	if (TryGetRequiredArrayField(GraphObject, TEXT("Links"), GraphPath, TEXT("InvalidGraphRegionType"), Links, Result))
	{
		ParseLinkArray(*Links, GraphPath, OutGraph, Result);
	}
	else
	{
		bParsed = false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Subgraphs = nullptr;
	if (TryGetRequiredArrayField(GraphObject, TEXT("Subgraphs"), GraphPath, TEXT("InvalidGraphRegionType"), Subgraphs, Result))
	{
		ParseSubgraphArray(*Subgraphs, GraphPath, bRejectUnknownGraphFields, Context, OutGraph, Result);
	}
	else
	{
		bParsed = false;
	}

	return bParsed;
}
}

FAssetDocumentGraphParseResult FAssetDocumentGraphParser::ParseGraphArray(
	const TArray<TSharedPtr<FJsonValue>>& GraphValues,
	const FAssetDocumentGraphParseOptions& Options)
{
	FAssetDocumentGraphParseResult Result;
	TSet<FString> GraphNames;

	for (int32 Index = 0; Index < GraphValues.Num(); ++Index)
	{
		const FString GraphPath = IndexPath(Options.Path, Index);
		const TSharedPtr<FJsonObject> GraphObject = GraphValues[Index].IsValid() ? GraphValues[Index]->AsObject() : nullptr;
		if (!GraphObject.IsValid())
		{
			Result.AddDiagnostic(TEXT("InvalidGraphRegionType"), GraphPath, TEXT("Graph entry must be an object."));
			continue;
		}

		FAssetDocumentGraphParseOptions GraphOptions = Options;
		GraphOptions.Path = GraphPath;
		FAssetDocumentGraphParseResult GraphResult = ParseSingleGraph(GraphObject.ToSharedRef(), GraphOptions);
		Result.Diagnostics.Append(GraphResult.Diagnostics);
		if (GraphResult.Graphs.Num() != 1)
		{
			continue;
		}

		FAssetDocumentGraphSpec Graph = MoveTemp(GraphResult.Graphs[0]);
		if (GraphNames.Contains(Graph.Name))
		{
			Result.AddDiagnostic(
				TEXT("DuplicateGraphName"),
				JoinPath(GraphPath, TEXT("Name")),
				FString::Printf(TEXT("Duplicate graph name '%s'."), *Graph.Name));
		}
		else
		{
			GraphNames.Add(Graph.Name);
		}
		Result.Graphs.Add(MoveTemp(Graph));
	}

	return Result;
}

FAssetDocumentGraphParseResult FAssetDocumentGraphParser::ParseSingleGraph(
	const TSharedRef<FJsonObject>& GraphObject,
	const FAssetDocumentGraphParseOptions& Options)
{
	FAssetDocumentGraphParseResult Result;
	if (Options.bRejectUnknownGraphFields)
	{
		ValidateUnknownFields(
			GraphObject,
			Options.Path,
			GraphFields,
			UE_ARRAY_COUNT(GraphFields),
			TEXT("UnknownGraphField"),
			Result);
	}

	FAssetDocumentGraphSpec Graph;
	GraphObject->TryGetStringField(TEXT("Id"), Graph.Id);
	GraphObject->TryGetStringField(TEXT("Kind"), Graph.Kind);
	TryGetStringField(GraphObject, TEXT("Name"), Options.Path, TEXT("MissingGraphName"), Graph.Name, Result);
	TryGetStringField(GraphObject, TEXT("Schema"), Options.Path, TEXT("MissingGraphSchema"), Graph.Schema, Result);
	GraphObject->TryGetStringField(TEXT("GraphGuid"), Graph.GraphGuid);
	GraphObject->TryGetStringField(TEXT("Category"), Graph.Category);
	GraphObject->TryGetStringField(TEXT("Description"), Graph.Description);
	ParseSharedGraphFields(GraphObject, Options.Path, Graph, Result);

	TSharedPtr<FJsonObject> Owner;
	if (TryGetNullableObjectFieldIfPresent(GraphObject, TEXT("Owner"), Options.Path, TEXT("InvalidGraphOwner"), Owner, Result) && Owner.IsValid())
	{
		Graph.Owner = CloneJsonObject(Owner);
	}

	TSharedPtr<FJsonObject> Signature;
	if (TryGetObjectFieldIfPresent(GraphObject, TEXT("Signature"), Options.Path, TEXT("InvalidGraphSignature"), Signature, Result))
	{
		Graph.Signature = CloneJsonObject(Signature);
	}

	TSharedPtr<FJsonObject> Position;
	if (TryGetObjectFieldIfPresent(GraphObject, TEXT("Position"), Options.Path, TEXT("InvalidGraphPosition"), Position, Result))
	{
		Graph.Position = CloneJsonObject(Position);
	}

	TSharedPtr<FJsonObject> Evidence;
	if (TryGetObjectFieldIfPresent(GraphObject, TEXT("Evidence"), Options.Path, TEXT("InvalidGraphEvidence"), Evidence, Result))
	{
		Graph.Evidence = CloneJsonObject(Evidence);
	}

	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (GraphObject->TryGetArrayField(TEXT("Nodes"), Nodes))
	{
		ParseNodeArray(*Nodes, Options.Path, Graph, Result);
	}
	else
	{
		Result.AddDiagnostic(TEXT("InvalidGraphRegionType"), JoinPath(Options.Path, TEXT("Nodes")), TEXT("Nodes must be an array."));
	}

	const TArray<TSharedPtr<FJsonValue>>* Links = nullptr;
	if (GraphObject->TryGetArrayField(TEXT("Links"), Links))
	{
		ParseLinkArray(*Links, Options.Path, Graph, Result);
	}
	else
	{
		Result.AddDiagnostic(TEXT("InvalidGraphRegionType"), JoinPath(Options.Path, TEXT("Links")), TEXT("Links must be an array."));
	}

	const TArray<TSharedPtr<FJsonValue>>* Subgraphs = nullptr;
	if (TryGetArrayFieldIfPresent(GraphObject, TEXT("Subgraphs"), Options.Path, TEXT("InvalidGraphRegionType"), Subgraphs, Result))
	{
		FGraphParseContext Context;
		ParseSubgraphArray(*Subgraphs, Options.Path, Options.bRejectUnknownGraphFields, Context, Graph, Result);
	}

	Result.Graphs.Add(MoveTemp(Graph));
	return Result;
}

FAssetDocumentGraphParseResult FAssetDocumentGraphParser::ParseGraphRegion(
	const TSharedRef<FJsonObject>& RegionObject,
	const FAssetDocumentGraphParseOptions& Options)
{
	FAssetDocumentGraphParseResult Result;
	if (Options.bRejectUnknownGraphFields)
	{
		ValidateUnknownFields(
			RegionObject,
			Options.Path,
			GraphRegionFields,
			UE_ARRAY_COUNT(GraphRegionFields),
			TEXT("UnknownGraphRegionField"),
			Result);
	}

	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	if (!TryGetRequiredArrayField(RegionObject, TEXT("Graphs"), Options.Path, TEXT("InvalidGraphRegionType"), Graphs, Result))
	{
		return Result;
	}

	TSet<FString> GraphIds;
	for (int32 Index = 0; Index < Graphs->Num(); ++Index)
	{
		const FString GraphPath = IndexPath(JoinPath(Options.Path, TEXT("Graphs")), Index);
		const TSharedPtr<FJsonObject> GraphObject = (*Graphs)[Index].IsValid() ? (*Graphs)[Index]->AsObject() : nullptr;
		if (!GraphObject.IsValid())
		{
			Result.AddDiagnostic(TEXT("InvalidGraphRegionType"), GraphPath, TEXT("Graph entry must be an object."));
			continue;
		}

		FAssetDocumentGraphSpec Graph;
		FGraphParseContext Context;
		if (!ParseRecursiveGraph(
				GraphObject.ToSharedRef(),
				GraphPath,
				Options.bRejectUnknownGraphFields,
				Context,
				Graph,
				Result))
		{
			continue;
		}

		if (GraphIds.Contains(Graph.Id))
		{
			Result.AddDiagnostic(
				TEXT("DuplicateGraphId"),
				JoinPath(GraphPath, TEXT("Id")),
				FString::Printf(TEXT("Duplicate graph id '%s'."), *Graph.Id));
		}
		else
		{
			GraphIds.Add(Graph.Id);
		}
		Result.Graphs.Add(MoveTemp(Graph));
	}

	return Result;
}

TSharedRef<FJsonValue> FAssetDocumentGraphParser::WriteCanonicalGraphArray(const TArray<FAssetDocumentGraphSpec>& Graphs)
{
	TArray<FAssetDocumentGraphSpec> SortedGraphs = Graphs;
	SortedGraphs.Sort([](const FAssetDocumentGraphSpec& Left, const FAssetDocumentGraphSpec& Right)
	{
		return Left.Name < Right.Name;
	});

	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FAssetDocumentGraphSpec& Graph : SortedGraphs)
	{
		Values.Add(MakeShared<FJsonValueObject>(Graph.ToJsonObject()));
	}
	return MakeShared<FJsonValueArray>(MoveTemp(Values));
}

TSharedRef<FJsonObject> FAssetDocumentGraphParser::WriteCanonicalGraphRegion(const TArray<FAssetDocumentGraphSpec>& Graphs)
{
	TArray<FAssetDocumentGraphSpec> SortedGraphs = Graphs;
	SortedGraphs.Sort([](const FAssetDocumentGraphSpec& Left, const FAssetDocumentGraphSpec& Right)
	{
		return Left.Id < Right.Id;
	});

	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FAssetDocumentGraphSpec& Graph : SortedGraphs)
	{
		Values.Add(MakeShared<FJsonValueObject>(Graph.ToJsonObject()));
	}

	TSharedRef<FJsonObject> RegionObject = MakeShared<FJsonObject>();
	RegionObject->SetArrayField(TEXT("Graphs"), MoveTemp(Values));
	return RegionObject;
}
