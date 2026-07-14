// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/AssetDocumentAnimationGraphRuntime.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "AssetDocumentPropertyAdapter.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr const TCHAR* ManagedNodeNamePrefix = TEXT("ADNode_");

FString AnimationRuntimeJoinPath(const FString& BasePath, const FString& Segment)
{
	const FString EscapedSegment = FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Segment);
	if (BasePath.IsEmpty())
	{
		return FString::Printf(TEXT("/%s"), *EscapedSegment);
	}
	return FString::Printf(TEXT("%s/%s"), *BasePath, *EscapedSegment);
}

FString AnimationRuntimeGraphPath(const FAssetDocumentGraphSpec& GraphSpec, const FAssetDocumentAnimationGraphContext& Context)
{
	if (!Context.GraphPath.IsEmpty())
	{
		return Context.GraphPath;
	}
	return FString::Printf(TEXT("/Graphs/%s"), *FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(GraphSpec.Id));
}

FString AnimationRuntimeNodePath(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentNodeSpec& NodeSpec,
	const FAssetDocumentAnimationGraphContext& Context)
{
	return AnimationRuntimeJoinPath(AnimationRuntimeJoinPath(AnimationRuntimeGraphPath(GraphSpec, Context), TEXT("Nodes")), NodeSpec.Id);
}

FString LinkPath(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentLinkSpec& Link,
	const FAssetDocumentAnimationGraphContext& Context)
{
	return AnimationRuntimeJoinPath(AnimationRuntimeJoinPath(AnimationRuntimeGraphPath(GraphSpec, Context), TEXT("Links")), Link.ToKey());
}

FAssetDocumentCapabilityResult RuntimeFailure(
	const FString& Path,
	const FString& Code,
	const FString& Message)
{
	return FAssetDocumentJsonRegionUtils::Failure(Path, Code, Message);
}

bool HasSpawnerDescriptor(const FAssetDocumentNodeSpec& NodeSpec)
{
	return NodeSpec.Spawner.IsValid() && !NodeSpec.Spawner->Values.IsEmpty();
}

bool SpawnerMatches(
	const TSharedPtr<FJsonObject>& DesiredSpawner,
	const TSharedPtr<FJsonObject>& CandidateSpawner)
{
	if (!DesiredSpawner.IsValid())
	{
		return true;
	}
	if (!CandidateSpawner.IsValid())
	{
		return false;
	}
	return AssetDocumentGraphJson::AreJsonObjectsEqual(DesiredSpawner, CandidateSpawner);
}

FAssetDocumentCapabilityResult PropertyApplyFailure(
	const FString& Path,
	const FAssetDocumentPropertyApplyResult& ApplyResult)
{
	FAssetDocumentCapabilityResult Result;
	Result.bSuccess = false;
	Result.Message = ApplyResult.Message;
	if (ApplyResult.Diagnostics.IsEmpty())
	{
		Result.Diagnostics.Add({Path, TEXT("GraphNodeFieldApplyFailed"), ApplyResult.Message});
		return Result;
	}

	for (const FAssetDocumentDiagnostic& Diagnostic : ApplyResult.Diagnostics)
	{
		FAssetDocumentDiagnostic Mapped;
		Mapped.Path = Diagnostic.Path.IsEmpty() ? Path : AnimationRuntimeJoinPath(Path, Diagnostic.Path);
		Mapped.Code = Diagnostic.Code.IsEmpty() ? TEXT("GraphNodeFieldApplyFailed") : Diagnostic.Code;
		Mapped.Message = Diagnostic.Message;
		Result.Diagnostics.Add(MoveTemp(Mapped));
	}
	return Result;
}

TSharedRef<FJsonObject> MakePositionObject(const UEdGraphNode* Node)
{
	TSharedRef<FJsonObject> Position = MakeShared<FJsonObject>();
	Position->SetNumberField(TEXT("X"), Node ? Node->NodePosX : 0);
	Position->SetNumberField(TEXT("Y"), Node ? Node->NodePosY : 0);
	return Position;
}

TSharedRef<FJsonObject> MakeNodeEvidenceObject(const UEdGraphNode* Node)
{
	TSharedRef<FJsonObject> Evidence = MakeShared<FJsonObject>();
	Evidence->SetStringField(TEXT("NodeGuid"), Node ? Node->NodeGuid.ToString(EGuidFormats::Digits) : FString());
	return Evidence;
}

bool IsOptionalPinMetadataObject(const TSharedPtr<FJsonObject>& Object)
{
	return Object.IsValid()
		&& Object->HasField(TEXT("PropertyName"))
		&& Object->HasField(TEXT("PropertyFriendlyName"))
		&& Object->HasField(TEXT("bShowPin"))
		&& Object->HasField(TEXT("bCanToggleVisibility"));
}

bool IsGeneratedGraphNodeFieldValue(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return true;
	}

	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		return Object.IsValid()
			&& Object->Values.Num() == 1
			&& Object->HasField(TEXT("Class"));
	}

	if (Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& Array = Value->AsArray();
		if (Array.IsEmpty())
		{
			return true;
		}

		for (const TSharedPtr<FJsonValue>& EntryValue : Array)
		{
			if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object || !IsOptionalPinMetadataObject(EntryValue->AsObject()))
			{
				return false;
			}
		}
		return true;
	}

	return false;
}

TSharedPtr<FJsonValue> ExtractPropertyDeltaToJson(FProperty* Property, const void* ValuePtr, const void* DefaultValuePtr)
{
	if (!Property || !ValuePtr || !DefaultValuePtr || Property->Identical(ValuePtr, DefaultValuePtr))
	{
		return nullptr;
	}

	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		TSharedPtr<FJsonObject> StructJson = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> FieldIt(StructProperty->Struct); FieldIt; ++FieldIt)
		{
			FProperty* Field = *FieldIt;
			const void* FieldValuePtr = Field->ContainerPtrToValuePtr<void>(ValuePtr);
			const void* DefaultFieldValuePtr = Field->ContainerPtrToValuePtr<void>(DefaultValuePtr);
			TSharedPtr<FJsonValue> FieldJson = ExtractPropertyDeltaToJson(Field, FieldValuePtr, DefaultFieldValuePtr);
			if (FieldJson.IsValid())
			{
				StructJson->SetField(Field->GetName(), FieldJson);
			}
		}
		if (StructJson->Values.IsEmpty())
		{
			return nullptr;
		}
		return MakeShared<FJsonValueObject>(StructJson);
	}

	return FAssetDocumentPropertyAdapter::ExtractPropertyValue(Property, ValuePtr);
}

TSharedPtr<FJsonObject> ExtractAuthoredNodeFields(UEdGraphNode* Node)
{
	if (!Node)
	{
		return nullptr;
	}

	UClass* NodeClass = Node->GetClass();
	UObject* DefaultNode = NodeClass ? NodeClass->GetDefaultObject() : nullptr;
	if (!NodeClass || !DefaultNode)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> AuthoredFields = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> PropertyIt(NodeClass); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		if (!FAssetDocumentPropertyAdapter::IsWritableProperty(Property))
		{
			continue;
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Node);
		const void* DefaultValuePtr = Property->ContainerPtrToValuePtr<void>(DefaultNode);
		TSharedPtr<FJsonValue> JsonValue = ExtractPropertyDeltaToJson(Property, ValuePtr, DefaultValuePtr);
		if (JsonValue.IsValid() && !IsGeneratedGraphNodeFieldValue(JsonValue))
		{
			AuthoredFields->SetField(Property->GetName(), JsonValue);
		}
	}
	return AuthoredFields->Values.IsEmpty() ? nullptr : AuthoredFields;
}

TSharedRef<FJsonObject> MakeSkippedNodeObject(const UEdGraphNode* Node)
{
	TSharedRef<FJsonObject> Skipped = MakeShared<FJsonObject>();
	Skipped->SetStringField(TEXT("Reason"), TEXT("UnsupportedAnimationGraphNodeIdentity"));
	Skipped->SetStringField(TEXT("Class"), Node && Node->GetClass() ? Node->GetClass()->GetPathName() : FString());
	Skipped->SetStringField(TEXT("NodeTitle"), Node ? Node->GetNodeTitle(ENodeTitleType::ListView).ToString() : FString());
	Skipped->SetStringField(TEXT("NodeGuid"), Node ? Node->NodeGuid.ToString(EGuidFormats::Digits) : FString());
	return Skipped;
}

bool IsPinNameRepresentable(const UEdGraphPin* Pin)
{
	if (!Pin || Pin->PinName.IsNone())
	{
		return false;
	}

	const FString PinName = Pin->PinName.ToString();
	if (PinName.IsEmpty())
	{
		return false;
	}

	const TCHAR First = PinName[0];
	if (!FChar::IsAlpha(First) && First != TEXT('_'))
	{
		return false;
	}

	for (int32 Index = 1; Index < PinName.Len(); ++Index)
	{
		const TCHAR Character = PinName[Index];
		if (!FChar::IsAlnum(Character) && Character != TEXT('_') && Character != TEXT('-'))
		{
			return false;
		}
	}
	return true;
}

UEdGraphNode* FindManagedNodeById(const FString& NodeId, const FAssetDocumentAnimationGraphContext& Context)
{
	if (!Context.Graph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : Context.Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		FString ParsedNodeId;
		if (FAssetDocumentAnimationGraphRuntime::TryParseManagedNodeObjectName(Node->GetFName(), ParsedNodeId)
			&& ParsedNodeId == NodeId)
		{
			return Node;
		}
	}
	return nullptr;
}

UEdGraphPin* FindUniquePinByName(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentLinkSpec& Link,
	const FAssetDocumentAnimationGraphContext& Context,
	const FAssetDocumentGraphEndpoint& Endpoint,
	UEdGraphNode* Node,
	EEdGraphPinDirection ExpectedDirection,
	FAssetDocumentCapabilityResult& OutResult)
{
	OutResult = FAssetDocumentCapabilityResult::Success();
	if (!Node)
	{
		OutResult = RuntimeFailure(
			LinkPath(GraphSpec, Link, Context),
			TEXT("UnresolvedGraphLinkEndpoint"),
			FString::Printf(TEXT("Graph link endpoint node '%s' could not be resolved."), *Endpoint.Node));
		return nullptr;
	}

	TArray<UEdGraphPin*> DirectionMatches;
	bool bHasOppositeDirectionMatch = false;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->PinName.ToString() != Endpoint.Pin)
		{
			continue;
		}
		if (Pin->Direction == ExpectedDirection)
		{
			DirectionMatches.Add(Pin);
		}
		else
		{
			bHasOppositeDirectionMatch = true;
		}
	}
	if (DirectionMatches.IsEmpty())
	{
		if (bHasOppositeDirectionMatch)
		{
			OutResult = RuntimeFailure(
				LinkPath(GraphSpec, Link, Context),
				TEXT("InvalidGraphLinkType"),
				FString::Printf(TEXT("Graph link endpoint '%s.%s' has the wrong pin direction."), *Endpoint.Node, *Endpoint.Pin));
			return nullptr;
		}
		OutResult = RuntimeFailure(
			LinkPath(GraphSpec, Link, Context),
			TEXT("UnresolvedGraphLinkEndpoint"),
			FString::Printf(TEXT("Graph link endpoint pin '%s.%s' could not be resolved after node reconstruction."), *Endpoint.Node, *Endpoint.Pin));
		return nullptr;
	}
	if (DirectionMatches.Num() > 1)
	{
		OutResult = RuntimeFailure(
			LinkPath(GraphSpec, Link, Context),
			TEXT("AmbiguousGraphLinkEndpointPin"),
			FString::Printf(TEXT("Graph link endpoint pin '%s.%s' is ambiguous after node reconstruction."), *Endpoint.Node, *Endpoint.Pin));
		return nullptr;
	}
	return DirectionMatches[0];
}

struct FResolvedAnimationGraphLink
{
	FAssetDocumentLinkSpec Link;
	UEdGraphPin* FromPin = nullptr;
	UEdGraphPin* ToPin = nullptr;
};

struct FManagedGraphLinkSnapshotEntry
{
	UEdGraphPin* FromPin = nullptr;
	UEdGraphPin* ToPin = nullptr;
};

TSet<UEdGraphNode*> MakeManagedNodeSet(const TMap<FString, UEdGraphNode*>& NodesById)
{
	TSet<UEdGraphNode*> ManagedNodes;
	for (const TPair<FString, UEdGraphNode*>& Pair : NodesById)
	{
		if (Pair.Value)
		{
			ManagedNodes.Add(Pair.Value);
		}
	}
	return ManagedNodes;
}

TArray<FManagedGraphLinkSnapshotEntry> SnapshotManagedManagedLinks(const TMap<FString, UEdGraphNode*>& NodesById)
{
	TArray<FManagedGraphLinkSnapshotEntry> Snapshot;
	TSet<FString> LinkKeys;
	const TSet<UEdGraphNode*> ManagedNodes = MakeManagedNodeSet(NodesById);
	for (UEdGraphNode* Node : ManagedNodes)
	{
		if (!Node)
		{
			continue;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output)
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !ManagedNodes.Contains(LinkedPin->GetOwningNode()))
				{
					continue;
				}

				const FString LinkKey = FString::Printf(
					TEXT("%p->%p"),
					static_cast<void*>(Pin),
					static_cast<void*>(LinkedPin));
				if (LinkKeys.Contains(LinkKey))
				{
					continue;
				}
				LinkKeys.Add(LinkKey);

				FManagedGraphLinkSnapshotEntry Entry;
				Entry.FromPin = Pin;
				Entry.ToPin = LinkedPin;
				Snapshot.Add(Entry);
			}
		}
	}
	return Snapshot;
}

void BreakManagedManagedLinks(const TMap<FString, UEdGraphNode*>& NodesById)
{
	TArray<FManagedGraphLinkSnapshotEntry> LinksToBreak = SnapshotManagedManagedLinks(NodesById);
	for (const FManagedGraphLinkSnapshotEntry& Link : LinksToBreak)
	{
		if (Link.FromPin && Link.ToPin)
		{
			Link.FromPin->BreakLinkTo(Link.ToPin);
		}
	}
}

void RestoreManagedManagedLinks(
	const TMap<FString, UEdGraphNode*>& NodesById,
	const TArray<FManagedGraphLinkSnapshotEntry>& Snapshot)
{
	BreakManagedManagedLinks(NodesById);
	for (const FManagedGraphLinkSnapshotEntry& Link : Snapshot)
	{
		if (Link.FromPin && Link.ToPin && !Link.FromPin->LinkedTo.Contains(Link.ToPin))
		{
			Link.FromPin->MakeLinkTo(Link.ToPin);
		}
	}
}

TArray<FManagedGraphLinkSnapshotEntry> SnapshotManagedUnmanagedLinks(const TMap<FString, UEdGraphNode*>& NodesById)
{
	TArray<FManagedGraphLinkSnapshotEntry> Snapshot;
	const TSet<UEdGraphNode*> ManagedNodes = MakeManagedNodeSet(NodesById);
	for (UEdGraphNode* Node : ManagedNodes)
	{
		if (!Node)
		{
			continue;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin)
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || ManagedNodes.Contains(LinkedPin->GetOwningNode()))
				{
					continue;
				}

				FManagedGraphLinkSnapshotEntry Entry;
				Entry.FromPin = Pin;
				Entry.ToPin = LinkedPin;
				Snapshot.Add(Entry);
			}
		}
	}
	return Snapshot;
}

void RestoreManagedUnmanagedLinks(const TArray<FManagedGraphLinkSnapshotEntry>& Snapshot)
{
	for (const FManagedGraphLinkSnapshotEntry& Link : Snapshot)
	{
		if (Link.FromPin && Link.ToPin && !Link.FromPin->LinkedTo.Contains(Link.ToPin))
		{
			Link.FromPin->MakeLinkTo(Link.ToPin);
		}
	}
}

bool AreSnapshotLinksPresent(const TArray<FManagedGraphLinkSnapshotEntry>& Snapshot)
{
	for (const FManagedGraphLinkSnapshotEntry& Link : Snapshot)
	{
		if (!Link.FromPin || !Link.ToPin || !Link.FromPin->LinkedTo.Contains(Link.ToPin) || !Link.ToPin->LinkedTo.Contains(Link.FromPin))
		{
			return false;
		}
	}
	return true;
}

bool ResponseBreaksFirstPin(ECanCreateConnectionResponse Response)
{
	return Response == CONNECT_RESPONSE_BREAK_OTHERS_A || Response == CONNECT_RESPONSE_BREAK_OTHERS_AB;
}

bool ResponseBreaksSecondPin(ECanCreateConnectionResponse Response)
{
	return Response == CONNECT_RESPONSE_BREAK_OTHERS_B || Response == CONNECT_RESPONSE_BREAK_OTHERS_AB;
}

bool HasUnmanagedLinkedPin(const UEdGraphPin* Pin, const UEdGraphPin* AuthoredOtherPin, const TSet<UEdGraphNode*>& ManagedNodes)
{
	if (!Pin)
	{
		return false;
	}

	for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
	{
		if (!LinkedPin || LinkedPin == AuthoredOtherPin)
		{
			continue;
		}

		if (!ManagedNodes.Contains(LinkedPin->GetOwningNode()))
		{
			return true;
		}
	}
	return false;
}

FAssetDocumentCapabilityResult LinkWouldBreakUnmanagedEndpointFailure(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentLinkSpec& Link,
	const FAssetDocumentAnimationGraphContext& Context)
{
	return RuntimeFailure(
		LinkPath(GraphSpec, Link, Context),
		TEXT("GraphLinkWouldBreakUnmanagedEndpoint"),
		FString::Printf(
			TEXT("Graph schema would break an unmanaged endpoint while creating link '%s'."),
			*Link.ToKey()));
}

struct FManagedPinNameCounts
{
	TMap<FString, int32> InputCounts;
	TMap<FString, int32> OutputCounts;
};

void IncrementPinCount(TMap<FString, int32>& Counts, const FString& PinName)
{
	int32& Count = Counts.FindOrAdd(PinName);
	++Count;
}

TMap<const UEdGraphNode*, FManagedPinNameCounts> BuildManagedPinNameCounts(const TMap<const UEdGraphNode*, FString>& NodeIds)
{
	TMap<const UEdGraphNode*, FManagedPinNameCounts> CountsByNode;
	for (const TPair<const UEdGraphNode*, FString>& Pair : NodeIds)
	{
		const UEdGraphNode* Node = Pair.Key;
		if (!Node)
		{
			continue;
		}

		FManagedPinNameCounts& Counts = CountsByNode.FindOrAdd(Node);
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->PinName.IsNone())
			{
				continue;
			}
			const FString PinName = Pin->PinName.ToString();
			if (Pin->Direction == EGPD_Input)
			{
				IncrementPinCount(Counts.InputCounts, PinName);
			}
			else if (Pin->Direction == EGPD_Output)
			{
				IncrementPinCount(Counts.OutputCounts, PinName);
			}
		}
	}
	return CountsByNode;
}

bool IsUniqueRepresentableManagedPin(
	const UEdGraphPin* Pin,
	const TMap<const UEdGraphNode*, FManagedPinNameCounts>& CountsByNode)
{
	if (!IsPinNameRepresentable(Pin))
	{
		return false;
	}

	const UEdGraphNode* Node = Pin->GetOwningNode();
	const FManagedPinNameCounts* Counts = CountsByNode.Find(Node);
	if (!Counts)
	{
		return false;
	}

	const FString PinName = Pin->PinName.ToString();
	const TMap<FString, int32>& DirectionCounts =
		Pin->Direction == EGPD_Input ? Counts->InputCounts : Counts->OutputCounts;
	const int32* Count = DirectionCounts.Find(PinName);
	return Count && *Count == 1;
}

FString PinDirectionString(const UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return FString();
	}
	return Pin->Direction == EGPD_Input ? TEXT("Input") : Pin->Direction == EGPD_Output ? TEXT("Output") : TEXT("Unknown");
}

void AddUnmanagedEndpointFields(TSharedRef<FJsonObject> Object, const FString& Prefix, const UEdGraphPin* Pin)
{
	const UEdGraphNode* Node = Pin ? Pin->GetOwningNode() : nullptr;
	Object->SetStringField(Prefix + TEXT("Class"), Node && Node->GetClass() ? Node->GetClass()->GetPathName() : FString());
	Object->SetStringField(Prefix + TEXT("Title"), Node ? Node->GetNodeTitle(ENodeTitleType::ListView).ToString() : FString());
	Object->SetStringField(Prefix + TEXT("NodeGuid"), Node ? Node->NodeGuid.ToString(EGuidFormats::Digits) : FString());
	Object->SetStringField(Prefix + TEXT("Pin"), Pin ? Pin->PinName.ToString() : FString());
	Object->SetStringField(Prefix + TEXT("PinDirection"), PinDirectionString(Pin));
}

TSharedRef<FJsonObject> MakeUnmanagedLinkSkippedObject(
	const FString& ManagedNodeId,
	const UEdGraphPin* ManagedPin,
	const UEdGraphPin* UnmanagedPin)
{
	TSharedRef<FJsonObject> Skipped = MakeShared<FJsonObject>();
	Skipped->SetStringField(TEXT("Reason"), TEXT("UnmanagedGraphLinkEndpoint"));
	Skipped->SetStringField(TEXT("ManagedNode"), ManagedNodeId);
	Skipped->SetStringField(TEXT("ManagedPin"), ManagedPin ? ManagedPin->PinName.ToString() : FString());
	Skipped->SetStringField(TEXT("ManagedPinDirection"), PinDirectionString(ManagedPin));
	AddUnmanagedEndpointFields(Skipped, TEXT("Unmanaged"), UnmanagedPin);
	return Skipped;
}

TSharedRef<FJsonObject> MakeManagedPinSkippedObject(
	const FString& Reason,
	const FString& ManagedNodeId,
	const UEdGraphPin* ManagedPin)
{
	TSharedRef<FJsonObject> Skipped = MakeShared<FJsonObject>();
	Skipped->SetStringField(TEXT("Reason"), Reason);
	Skipped->SetStringField(TEXT("ManagedNode"), ManagedNodeId);
	Skipped->SetStringField(TEXT("ManagedPin"), ManagedPin ? ManagedPin->PinName.ToString() : FString());
	Skipped->SetStringField(TEXT("ManagedPinDirection"), PinDirectionString(ManagedPin));
	return Skipped;
}

FString ManagedPinSkipReason(
	const UEdGraphPin* Pin,
	const TMap<const UEdGraphNode*, FManagedPinNameCounts>& CountsByNode)
{
	if (!IsPinNameRepresentable(Pin))
	{
		return TEXT("UnrepresentableManagedGraphPin");
	}

	const UEdGraphNode* Node = Pin ? Pin->GetOwningNode() : nullptr;
	const FManagedPinNameCounts* Counts = CountsByNode.Find(Node);
	const FString PinName = Pin ? Pin->PinName.ToString() : FString();
	const TMap<FString, int32>* DirectionCounts = nullptr;
	if (Counts && Pin)
	{
		DirectionCounts = Pin->Direction == EGPD_Input ? &Counts->InputCounts : &Counts->OutputCounts;
	}
	const int32* Count = DirectionCounts ? DirectionCounts->Find(PinName) : nullptr;
	return Count && *Count > 1 ? TEXT("AmbiguousManagedGraphPin") : TEXT("UnrepresentableManagedGraphPin");
}

void AddManagedPinSkippedLink(
	TArray<TSharedPtr<FJsonValue>>& SkippedLinks,
	const FString& ManagedNodeId,
	const UEdGraphPin* ManagedPin,
	const TMap<const UEdGraphNode*, FManagedPinNameCounts>& CountsByNode)
{
	SkippedLinks.Add(MakeShared<FJsonValueObject>(
		MakeManagedPinSkippedObject(ManagedPinSkipReason(ManagedPin, CountsByNode), ManagedNodeId, ManagedPin)));
}

void AddManagedUnmanagedSkippedLink(
	TArray<TSharedPtr<FJsonValue>>& SkippedLinks,
	const FString& ManagedNodeId,
	const UEdGraphPin* ManagedPin,
	const UEdGraphPin* UnmanagedPin)
{
	SkippedLinks.Add(MakeShared<FJsonValueObject>(
		MakeUnmanagedLinkSkippedObject(ManagedNodeId, ManagedPin, UnmanagedPin)));
}

void AddExtractedManagedLinks(
	const TArray<UEdGraphNode*>& GraphNodes,
	const TMap<const UEdGraphNode*, FString>& NodeIds,
	FAssetDocumentGraphSpec& OutGraph,
	TArray<TSharedPtr<FJsonValue>>& SkippedLinks)
{
	const TMap<const UEdGraphNode*, FManagedPinNameCounts> CountsByNode = BuildManagedPinNameCounts(NodeIds);
	TSet<FString> LinkKeys;
	for (UEdGraphNode* Node : GraphNodes)
	{
		const FString* FromNodeId = NodeIds.Find(Node);

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output)
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				const UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
				const FString* ToNodeId = NodeIds.Find(LinkedNode);
				if (!FromNodeId && !ToNodeId)
				{
					continue;
				}
				if (!FromNodeId)
				{
					AddManagedUnmanagedSkippedLink(SkippedLinks, *ToNodeId, LinkedPin, Pin);
					continue;
				}
				if (!ToNodeId)
				{
					AddManagedUnmanagedSkippedLink(SkippedLinks, *FromNodeId, Pin, LinkedPin);
					continue;
				}
				if (!IsUniqueRepresentableManagedPin(Pin, CountsByNode))
				{
					AddManagedPinSkippedLink(SkippedLinks, *FromNodeId, Pin, CountsByNode);
					continue;
				}
				if (!IsUniqueRepresentableManagedPin(LinkedPin, CountsByNode))
				{
					AddManagedPinSkippedLink(SkippedLinks, *ToNodeId, LinkedPin, CountsByNode);
					continue;
				}

				FAssetDocumentLinkSpec Link;
				Link.From.Node = *FromNodeId;
				Link.From.Pin = Pin->PinName.ToString();
				Link.To.Node = *ToNodeId;
				Link.To.Pin = LinkedPin->PinName.ToString();
				if (!LinkKeys.Contains(Link.ToKey()))
				{
					LinkKeys.Add(Link.ToKey());
					OutGraph.Links.Add(MoveTemp(Link));
				}
			}
		}
	}
}

TCHAR ToHexDigit(uint8 Value)
{
	return Value < 10 ? static_cast<TCHAR>(TEXT('0') + Value) : static_cast<TCHAR>(TEXT('A') + (Value - 10));
}

bool FromHexDigit(TCHAR Digit, uint8& OutValue)
{
	if (Digit >= TEXT('0') && Digit <= TEXT('9'))
	{
		OutValue = static_cast<uint8>(Digit - TEXT('0'));
		return true;
	}
	if (Digit >= TEXT('A') && Digit <= TEXT('F'))
	{
		OutValue = static_cast<uint8>(10 + Digit - TEXT('A'));
		return true;
	}
	if (Digit >= TEXT('a') && Digit <= TEXT('f'))
	{
		OutValue = static_cast<uint8>(10 + Digit - TEXT('a'));
		return true;
	}
	return false;
}

FString MakeManagedNodeGuidKey(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentNodeSpec& NodeSpec)
{
	return FString::Printf(
		TEXT("AssetDocument.AnimationGraph.Node|%s|%s|%s"),
		*GraphSpec.Kind,
		*GraphSpec.Id,
		*NodeSpec.Id);
}

FAssetDocumentCapabilityResult AssignManagedIdentity(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentNodeSpec& NodeSpec,
	const FAssetDocumentAnimationGraphContext& Context,
	UEdGraphNode& Node)
{
	FGuid DesiredGuid;
	if (!NodeSpec.NodeGuid.IsEmpty())
	{
		if (!FGuid::Parse(NodeSpec.NodeGuid, DesiredGuid))
		{
			return RuntimeFailure(
				AnimationRuntimeJoinPath(AnimationRuntimeNodePath(GraphSpec, NodeSpec, Context), TEXT("NodeGuid")),
				TEXT("InvalidGraphNodeGuid"),
				TEXT("Node.NodeGuid must be a GUID string."));
		}
	}
	else
	{
		DesiredGuid = FAssetDocumentAnimationGraphRuntime::MakeManagedNodeGuid(GraphSpec, NodeSpec);
	}
	Node.NodeGuid = DesiredGuid;

	FString DesiredName = FAssetDocumentAnimationGraphRuntime::MakeManagedNodeObjectName(NodeSpec.Id);
	if (!DesiredName.IsEmpty() && Node.GetName() != DesiredName)
	{
		if (UObject* ExistingObject = FindObject<UObject>(Node.GetOuter(), *DesiredName))
		{
			if (ExistingObject != &Node)
			{
				DesiredName = MakeUniqueObjectName(Node.GetOuter(), Node.GetClass(), FName(*DesiredName)).ToString();
			}
		}
		if (!Node.Rename(*DesiredName, Node.GetOuter(), REN_DontCreateRedirectors | REN_NonTransactional))
		{
			return RuntimeFailure(
				AnimationRuntimeNodePath(GraphSpec, NodeSpec, Context),
				TEXT("GraphNodeIdentityRenameFailed"),
				FString::Printf(TEXT("Graph node '%s' could not be assigned a stable object identity."), *NodeSpec.Id));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

}

FAssetDocumentAnimationGraphRuntime::FAssetDocumentAnimationGraphRuntime(
	TSharedPtr<IAssetDocumentAnimationGraphCandidateProvider> InCandidateProvider)
	: CandidateProvider(MoveTemp(InCandidateProvider))
{
}

FAssetDocumentCapabilityResult FAssetDocumentAnimationGraphRuntime::ResolveCandidate(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentNodeSpec& NodeSpec,
	const FAssetDocumentAnimationGraphContext& Context,
	FAssetDocumentAnimationGraphNodeSpawnCandidate& OutCandidate) const
{
	const FString CurrentNodePath = AnimationRuntimeNodePath(GraphSpec, NodeSpec, Context);
	if (!CandidateProvider.IsValid())
	{
		return RuntimeFailure(
			AnimationRuntimeJoinPath(CurrentNodePath, TEXT("Class")),
			TEXT("MissingGraphNodeCandidateProvider"),
			TEXT("Animation graph runtime has no node spawn candidate provider."));
	}

	TArray<FAssetDocumentAnimationGraphNodeSpawnCandidate> Candidates =
		CandidateProvider->FindCandidates(NodeSpec, Context);
	Candidates.RemoveAll(
		[&NodeSpec](const FAssetDocumentAnimationGraphNodeSpawnCandidate& Candidate)
		{
			return !NodeSpec.Class.IsEmpty() && Candidate.ClassPath != NodeSpec.Class;
		});

	if (HasSpawnerDescriptor(NodeSpec))
	{
		Candidates.RemoveAll(
			[&NodeSpec](const FAssetDocumentAnimationGraphNodeSpawnCandidate& Candidate)
			{
				return !SpawnerMatches(NodeSpec.Spawner, Candidate.Spawner);
			});
	}

	if (Candidates.IsEmpty())
	{
		return RuntimeFailure(
			AnimationRuntimeJoinPath(CurrentNodePath, TEXT("Class")),
			TEXT("UnspawnableGraphNodeClass"),
			FString::Printf(TEXT("No spawn candidate exists for graph node '%s'."), *NodeSpec.Id));
	}

	if (!HasSpawnerDescriptor(NodeSpec) && Candidates.Num() > 1)
	{
		return RuntimeFailure(
			AnimationRuntimeJoinPath(CurrentNodePath, TEXT("Spawner")),
			TEXT("AmbiguousGraphNodeSpawner"),
			FString::Printf(TEXT("Graph node '%s' requires Node.Spawner to disambiguate candidates."), *NodeSpec.Id));
	}

	OutCandidate = Candidates[0];
	if (!OutCandidate.bSpawnable)
	{
		return RuntimeFailure(
			AnimationRuntimeJoinPath(CurrentNodePath, TEXT("Class")),
			TEXT("UnspawnableGraphNodeClass"),
			FString::Printf(TEXT("Graph node '%s' is not spawnable in this graph context."), *NodeSpec.Id));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FAssetDocumentAnimationGraphRuntime::ValidateGraph(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentAnimationGraphContext& Context) const
{
	for (const FAssetDocumentNodeSpec& NodeSpec : GraphSpec.Nodes)
	{
		FAssetDocumentAnimationGraphNodeSpawnCandidate Candidate;
		const FAssetDocumentCapabilityResult CandidateResult =
			ResolveCandidate(GraphSpec, NodeSpec, Context, Candidate);
		if (!CandidateResult.bSuccess)
		{
			return CandidateResult;
		}
	}

	for (const FAssetDocumentGraphSpec& Subgraph : GraphSpec.Subgraphs)
	{
		FAssetDocumentAnimationGraphContext SubgraphContext = Context;
		SubgraphContext.GraphPath = AnimationRuntimeJoinPath(AnimationRuntimeJoinPath(AnimationRuntimeGraphPath(GraphSpec, Context), TEXT("Subgraphs")), Subgraph.Id);
		SubgraphContext.GraphKind = Subgraph.Kind;
		const FAssetDocumentCapabilityResult SubgraphResult = ValidateGraph(Subgraph, SubgraphContext);
		if (!SubgraphResult.bSuccess)
		{
			return SubgraphResult;
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FAssetDocumentAnimationGraphRuntime::MaterializeGraphNodes(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentAnimationGraphContext& Context,
	TMap<FString, UEdGraphNode*>& OutNodesById) const
{
	OutNodesById.Reset();
	for (const FAssetDocumentNodeSpec& NodeSpec : GraphSpec.Nodes)
	{
		UEdGraphNode* SpawnedNode = FindManagedNodeById(NodeSpec.Id, Context);
		if (!SpawnedNode)
		{
			FAssetDocumentAnimationGraphNodeSpawnCandidate Candidate;
			FAssetDocumentCapabilityResult CandidateResult = ResolveCandidate(GraphSpec, NodeSpec, Context, Candidate);
			if (!CandidateResult.bSuccess)
			{
				return CandidateResult;
			}

			CandidateResult = CandidateProvider->SpawnNode(GraphSpec, NodeSpec, Context, Candidate, SpawnedNode);
			if (!CandidateResult.bSuccess)
			{
				return CandidateResult;
			}
			if (!SpawnedNode)
			{
				return RuntimeFailure(
					AnimationRuntimeJoinPath(AnimationRuntimeNodePath(GraphSpec, NodeSpec, Context), TEXT("Class")),
					TEXT("GraphNodeSpawnFailed"),
					FString::Printf(TEXT("Graph node '%s' did not produce a UE graph node."), *NodeSpec.Id));
			}
			if (Context.Graph && !Context.Graph->Nodes.Contains(SpawnedNode))
			{
				Context.Graph->AddNode(SpawnedNode, false, false);
			}
		}
		if (!NodeSpec.Class.IsEmpty() && SpawnedNode->GetClass()->GetPathName() != NodeSpec.Class)
		{
			return RuntimeFailure(
				AnimationRuntimeJoinPath(AnimationRuntimeNodePath(GraphSpec, NodeSpec, Context), TEXT("Class")),
				TEXT("GraphNodeSpawnClassMismatch"),
				FString::Printf(
					TEXT("Graph node '%s' spawned '%s' instead of '%s'."),
					*NodeSpec.Id,
					*SpawnedNode->GetClass()->GetPathName(),
					*NodeSpec.Class));
		}

		const FAssetDocumentCapabilityResult IdentityResult =
			AssignManagedIdentity(GraphSpec, NodeSpec, Context, *SpawnedNode);
		if (!IdentityResult.bSuccess)
		{
			return IdentityResult;
		}

		if (NodeSpec.Fields.IsValid() && !NodeSpec.Fields->Values.IsEmpty())
		{
			const FString FieldsPath = AnimationRuntimeJoinPath(AnimationRuntimeNodePath(GraphSpec, NodeSpec, Context), TEXT("Fields"));
			const FAssetDocumentPropertyApplyResult FieldResult =
				FAssetDocumentPropertyAdapter::ApplyProperties(SpawnedNode, NodeSpec.Fields);
			if (!FieldResult.bSuccess)
			{
				return PropertyApplyFailure(FieldsPath, FieldResult);
			}
		}

		SpawnedNode->NodePosX = 0;
		SpawnedNode->NodePosY = 0;
		if (NodeSpec.Position.IsValid())
		{
			double X = 0.0;
			double Y = 0.0;
			if (NodeSpec.Position->TryGetNumberField(TEXT("X"), X))
			{
				SpawnedNode->NodePosX = static_cast<int32>(X);
			}
			if (NodeSpec.Position->TryGetNumberField(TEXT("Y"), Y))
			{
				SpawnedNode->NodePosY = static_cast<int32>(Y);
			}
		}
		SpawnedNode->ReconstructNode();
		OutNodesById.Add(NodeSpec.Id, SpawnedNode);
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FAssetDocumentAnimationGraphRuntime::MaterializeGraphLinks(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentAnimationGraphContext& Context,
	const TMap<FString, UEdGraphNode*>& NodesById) const
{
	const UEdGraphSchema* Schema = Context.Graph ? Context.Graph->GetSchema() : nullptr;
	TArray<FResolvedAnimationGraphLink> ResolvedLinks;
	const TSet<UEdGraphNode*> ManagedNodes = MakeManagedNodeSet(NodesById);

	for (const FAssetDocumentLinkSpec& Link : GraphSpec.Links)
	{
		UEdGraphNode* const* FromNode = NodesById.Find(Link.From.Node);
		UEdGraphNode* const* ToNode = NodesById.Find(Link.To.Node);

		FAssetDocumentCapabilityResult PinResult;
		UEdGraphPin* FromPin = FindUniquePinByName(
			GraphSpec,
			Link,
			Context,
			Link.From,
			FromNode ? *FromNode : nullptr,
			EGPD_Output,
			PinResult);
		if (!PinResult.bSuccess)
		{
			return PinResult;
		}

		UEdGraphPin* ToPin = FindUniquePinByName(
			GraphSpec,
			Link,
			Context,
			Link.To,
			ToNode ? *ToNode : nullptr,
			EGPD_Input,
			PinResult);
		if (!PinResult.bSuccess)
		{
			return PinResult;
		}

		if (!FromPin || !ToPin)
		{
			return RuntimeFailure(
				LinkPath(GraphSpec, Link, Context),
				TEXT("UnresolvedGraphLinkEndpoint"),
				FString::Printf(TEXT("Graph link endpoint '%s' could not be resolved after node reconstruction."), *Link.ToKey()));
		}

		if (Schema)
		{
			const bool bAlreadyLinked = FromPin->LinkedTo.Contains(ToPin);
			const FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
			if (!bAlreadyLinked && Response.Response == CONNECT_RESPONSE_DISALLOW)
			{
				return RuntimeFailure(
					LinkPath(GraphSpec, Link, Context),
					TEXT("InvalidGraphLinkType"),
					FString::Printf(TEXT("Graph schema rejected link '%s': %s"), *Link.ToKey(), *Response.Message.ToString()));
			}
			if (ResponseBreaksFirstPin(Response.Response) && HasUnmanagedLinkedPin(FromPin, ToPin, ManagedNodes))
			{
				return LinkWouldBreakUnmanagedEndpointFailure(GraphSpec, Link, Context);
			}
			if (ResponseBreaksSecondPin(Response.Response) && HasUnmanagedLinkedPin(ToPin, FromPin, ManagedNodes))
			{
				return LinkWouldBreakUnmanagedEndpointFailure(GraphSpec, Link, Context);
			}
		}

		FResolvedAnimationGraphLink ResolvedLink;
		ResolvedLink.Link = Link;
		ResolvedLink.FromPin = FromPin;
		ResolvedLink.ToPin = ToPin;
		ResolvedLinks.Add(MoveTemp(ResolvedLink));
	}

	const TArray<FManagedGraphLinkSnapshotEntry> LinkSnapshot = SnapshotManagedManagedLinks(NodesById);
	const TArray<FManagedGraphLinkSnapshotEntry> UnmanagedLinkSnapshot = SnapshotManagedUnmanagedLinks(NodesById);
	BreakManagedManagedLinks(NodesById);

	for (const FResolvedAnimationGraphLink& ResolvedLink : ResolvedLinks)
	{
		if (Schema)
		{
			if (!Schema->TryCreateConnection(ResolvedLink.FromPin, ResolvedLink.ToPin))
			{
				RestoreManagedManagedLinks(NodesById, LinkSnapshot);
				RestoreManagedUnmanagedLinks(UnmanagedLinkSnapshot);
				return RuntimeFailure(
					LinkPath(GraphSpec, ResolvedLink.Link, Context),
					TEXT("InvalidGraphLinkType"),
					FString::Printf(TEXT("Graph schema rejected link '%s'."), *ResolvedLink.Link.ToKey()));
			}
			if (!AreSnapshotLinksPresent(UnmanagedLinkSnapshot))
			{
				RestoreManagedManagedLinks(NodesById, LinkSnapshot);
				RestoreManagedUnmanagedLinks(UnmanagedLinkSnapshot);
				return LinkWouldBreakUnmanagedEndpointFailure(GraphSpec, ResolvedLink.Link, Context);
			}
		}
		else if (!ResolvedLink.FromPin->LinkedTo.Contains(ResolvedLink.ToPin))
		{
			ResolvedLink.FromPin->MakeLinkTo(ResolvedLink.ToPin);
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FAssetDocumentAnimationGraphRuntime::ApplyGraphAfterPreflight(
	const FAssetDocumentGraphSpec& GraphSpec,
	FAssetDocumentAnimationGraphContext& Context,
	IAssetDocumentAnimationGraphStructuralHook& Hook) const
{
	const FAssetDocumentCapabilityResult LocateResult = Hook.LocateOrCreateGraph(GraphSpec, Context);
	if (!LocateResult.bSuccess)
	{
		return LocateResult;
	}

	TMap<FString, UEdGraphNode*> NodesById;
	const FAssetDocumentCapabilityResult MaterializeResult = MaterializeGraphNodes(GraphSpec, Context, NodesById);
	if (!MaterializeResult.bSuccess)
	{
		return MaterializeResult;
	}

	const FAssetDocumentCapabilityResult LinkResult = MaterializeGraphLinks(GraphSpec, Context, NodesById);
	if (!LinkResult.bSuccess)
	{
		return LinkResult;
	}

	for (const FAssetDocumentGraphSpec& Subgraph : GraphSpec.Subgraphs)
	{
		FAssetDocumentAnimationGraphContext SubgraphContext = Context;
		SubgraphContext.GraphPath = AnimationRuntimeJoinPath(AnimationRuntimeJoinPath(AnimationRuntimeGraphPath(GraphSpec, Context), TEXT("Subgraphs")), Subgraph.Id);
		SubgraphContext.GraphKind = Subgraph.Kind;
		const FAssetDocumentCapabilityResult SubgraphResult =
			ApplyGraphAfterPreflight(Subgraph, SubgraphContext, Hook);
		if (!SubgraphResult.bSuccess)
		{
			return SubgraphResult;
		}
	}

	return Hook.RepairAfterApply(GraphSpec, Context);
}

FAssetDocumentCapabilityResult FAssetDocumentAnimationGraphRuntime::ApplyGraph(
	const FAssetDocumentGraphSpec& GraphSpec,
	FAssetDocumentAnimationGraphContext& Context,
	IAssetDocumentAnimationGraphStructuralHook& Hook) const
{
	const FAssetDocumentCapabilityResult ValidateResult = ValidateGraph(GraphSpec, Context);
	if (!ValidateResult.bSuccess)
	{
		return ValidateResult;
	}

	return ApplyGraphAfterPreflight(GraphSpec, Context, Hook);
}

FAssetDocumentCapabilityResult FAssetDocumentAnimationGraphRuntime::ExtractGraph(
	const FAssetDocumentAnimationGraphContext& Context,
	FAssetDocumentGraphSpec& OutGraph) const
{
	OutGraph.Id = Context.GraphKind.IsEmpty() ? TEXT("AnimGraph") : Context.GraphKind;
	OutGraph.Kind = Context.GraphKind;
	OutGraph.Nodes.Reset();
	OutGraph.Links.Reset();
	OutGraph.UnderscoreSkipped.Reset();

	if (!Context.Graph)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TArray<TSharedPtr<FJsonValue>> SkippedNodes;
	TMap<const UEdGraphNode*, FString> NodeIds;
	for (UEdGraphNode* Node : Context.Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		FString NodeId;
		if (!TryParseManagedNodeObjectName(Node->GetFName(), NodeId))
		{
			SkippedNodes.Add(MakeShared<FJsonValueObject>(MakeSkippedNodeObject(Node)));
			continue;
		}

		FAssetDocumentNodeSpec NodeSpec;
		NodeSpec.Id = NodeId;
		NodeSpec.Class = Node->GetClass() ? Node->GetClass()->GetPathName() : FString();
		NodeSpec.Position = MakePositionObject(Node);
		NodeSpec.Evidence = MakeNodeEvidenceObject(Node);
		if (TSharedPtr<FJsonObject> Fields = ExtractAuthoredNodeFields(Node))
		{
			NodeSpec.Fields = Fields;
		}
		NodeIds.Add(Node, NodeId);
		OutGraph.Nodes.Add(MoveTemp(NodeSpec));
	}

	TArray<TSharedPtr<FJsonValue>> SkippedLinks;
	AddExtractedManagedLinks(Context.Graph->Nodes, NodeIds, OutGraph, SkippedLinks);

	if (!SkippedNodes.IsEmpty() || !SkippedLinks.IsEmpty())
	{
		TSharedRef<FJsonObject> Skipped = MakeShared<FJsonObject>();
		if (!SkippedNodes.IsEmpty())
		{
			Skipped->SetArrayField(TEXT("Nodes"), MoveTemp(SkippedNodes));
		}
		if (!SkippedLinks.IsEmpty())
		{
			Skipped->SetArrayField(TEXT("Links"), MoveTemp(SkippedLinks));
		}
		OutGraph.UnderscoreSkipped = MakeShared<FJsonValueObject>(Skipped);
	}
	return FAssetDocumentCapabilityResult::Success();
}

FGuid FAssetDocumentAnimationGraphRuntime::MakeManagedNodeGuid(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentNodeSpec& NodeSpec)
{
	return FGuid::NewDeterministicGuid(MakeManagedNodeGuidKey(GraphSpec, NodeSpec));
}

FString FAssetDocumentAnimationGraphRuntime::MakeManagedNodeObjectName(const FString& NodeId)
{
	FString Encoded;
	Encoded.Reserve(FCString::Strlen(ManagedNodeNamePrefix) + NodeId.Len() * 2);
	Encoded += ManagedNodeNamePrefix;
	for (TCHAR Character : NodeId)
	{
		const uint8 Byte = static_cast<uint8>(Character);
		Encoded.AppendChar(ToHexDigit(Byte >> 4));
		Encoded.AppendChar(ToHexDigit(Byte & 0x0F));
	}
	return Encoded;
}

bool FAssetDocumentAnimationGraphRuntime::TryParseManagedNodeObjectName(
	const FName& ObjectName,
	FString& OutNodeId)
{
	const FString Name = ObjectName.ToString();
	if (!Name.StartsWith(ManagedNodeNamePrefix))
	{
		return false;
	}

	const FString EncodedWithOptionalSuffix = Name.Mid(FCString::Strlen(ManagedNodeNamePrefix));
	int32 EncodedLength = 0;
	while (EncodedLength < EncodedWithOptionalSuffix.Len())
	{
		uint8 Ignored = 0;
		if (!FromHexDigit(EncodedWithOptionalSuffix[EncodedLength], Ignored))
		{
			break;
		}
		++EncodedLength;
	}
	if (EncodedLength <= 0 || EncodedLength % 2 != 0)
	{
		return false;
	}
	const FString Encoded = EncodedWithOptionalSuffix.Left(EncodedLength);

	FString Decoded;
	Decoded.Reserve(Encoded.Len() / 2);
	for (int32 Index = 0; Index < Encoded.Len(); Index += 2)
	{
		uint8 High = 0;
		uint8 Low = 0;
		if (!FromHexDigit(Encoded[Index], High) || !FromHexDigit(Encoded[Index + 1], Low))
		{
			return false;
		}
		Decoded.AppendChar(static_cast<TCHAR>((High << 4) | Low));
	}

	OutNodeId = MoveTemp(Decoded);
	return !OutNodeId.IsEmpty();
}
