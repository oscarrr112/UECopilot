// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/AssetDocumentGraphParser.h"

namespace
{
const TCHAR* GraphFields[] = {
	TEXT("Name"),
	TEXT("Schema"),
	TEXT("GraphGuid"),
	TEXT("Category"),
	TEXT("Description"),
	TEXT("Signature"),
	TEXT("Nodes"),
	TEXT("Links")
};

const TCHAR* NodeFields[] = {
	TEXT("Id"),
	TEXT("NodeGuid"),
	TEXT("Class"),
	TEXT("Capability"),
	TEXT("Member"),
	TEXT("PinOverrides"),
	TEXT("Position"),
	TEXT("Comment")
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

FString JoinPath(const FString& BasePath, const FString& Segment)
{
	if (BasePath.IsEmpty())
	{
		return FString::Printf(TEXT("/%s"), *Segment);
	}
	return FString::Printf(TEXT("%s/%s"), *BasePath, *Segment);
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

	Object->TryGetStringField(TEXT("Direction"), OutPinOverride.Direction);
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
	bool BoolValue = false;
	if (Object->TryGetBoolField(TEXT("Hidden"), BoolValue))
	{
		OutPinOverride.Hidden = BoolValue;
	}
	if (Object->TryGetBoolField(TEXT("AdvancedView"), BoolValue))
	{
		OutPinOverride.AdvancedView = BoolValue;
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
	Object->TryGetStringField(TEXT("Comment"), OutNode.Comment);
	OutNode.bHasComment = Object->HasField(TEXT("Comment"));

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

	const TArray<TSharedPtr<FJsonValue>>* PinOverrides = nullptr;
	if (TryGetArrayFieldIfPresent(Object, TEXT("PinOverrides"), Path, TEXT("InvalidGraphPin"), PinOverrides, Result))
	{
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
	TryGetStringField(GraphObject, TEXT("Name"), Options.Path, TEXT("MissingGraphName"), Graph.Name, Result);
	TryGetStringField(GraphObject, TEXT("Schema"), Options.Path, TEXT("MissingGraphSchema"), Graph.Schema, Result);
	GraphObject->TryGetStringField(TEXT("GraphGuid"), Graph.GraphGuid);
	GraphObject->TryGetStringField(TEXT("Category"), Graph.Category);
	GraphObject->TryGetStringField(TEXT("Description"), Graph.Description);

	TSharedPtr<FJsonObject> Signature;
	if (TryGetObjectFieldIfPresent(GraphObject, TEXT("Signature"), Options.Path, TEXT("InvalidGraphSignature"), Signature, Result))
	{
		Graph.Signature = CloneJsonObject(Signature);
	}

	TSet<FString> NodeIds;
	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (GraphObject->TryGetArrayField(TEXT("Nodes"), Nodes))
	{
		for (int32 Index = 0; Index < Nodes->Num(); ++Index)
		{
			const FString NodePath = IndexPath(JoinPath(Options.Path, TEXT("Nodes")), Index);
			const TSharedPtr<FJsonObject> NodeObject = (*Nodes)[Index].IsValid() ? (*Nodes)[Index]->AsObject() : nullptr;
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
	else
	{
		Result.AddDiagnostic(TEXT("InvalidGraphRegionType"), JoinPath(Options.Path, TEXT("Nodes")), TEXT("Nodes must be an array."));
	}

	TSet<FString> LinkKeys;
	const TArray<TSharedPtr<FJsonValue>>* Links = nullptr;
	if (GraphObject->TryGetArrayField(TEXT("Links"), Links))
	{
		for (int32 Index = 0; Index < Links->Num(); ++Index)
		{
			const FString LinkPath = IndexPath(JoinPath(Options.Path, TEXT("Links")), Index);
			const TSharedPtr<FJsonObject> LinkObject = (*Links)[Index].IsValid() ? (*Links)[Index]->AsObject() : nullptr;
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
	else
	{
		Result.AddDiagnostic(TEXT("InvalidGraphRegionType"), JoinPath(Options.Path, TEXT("Links")), TEXT("Links must be an array."));
	}

	Result.Graphs.Add(MoveTemp(Graph));
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
