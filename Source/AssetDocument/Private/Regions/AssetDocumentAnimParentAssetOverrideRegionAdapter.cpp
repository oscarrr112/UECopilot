// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentAnimParentAssetOverrideRegionAdapter.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "Graphs/AssetDocumentAnimationGraphRuntime.h"
#include "Graphs/AssetDocumentGraphTypes.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimationAsset.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"

namespace
{
constexpr const TCHAR* ParentAssetOverridesRegionId = TEXT("Body.ParentAssetOverrides");
constexpr const TCHAR* RootAnimGraphId = TEXT("AnimGraph");
constexpr const TCHAR* RootAnimGraphKind = TEXT("AnimGraph");

FAssetDocumentCapabilityResult AnimParentOverrideFailure(const FString& Path, const FString& Code, const FString& Message)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FString Escape(const FString& Token)
{
	return FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Token);
}

FString GuidToIdentity(const FGuid& Guid)
{
	return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

bool ParseGuidIdentity(const FString& Text, FGuid& OutGuid)
{
	const FString Trimmed = Text.TrimStartAndEnd();
	return !Trimmed.IsEmpty() && FGuid::Parse(Trimmed, OutGuid);
}

FString OverridePath(const FString& GuidIdentity)
{
	return FString::Printf(TEXT("/Body/ParentAssetOverrides/%s"), *Escape(GuidIdentity));
}

struct FParsedParentAssetOverride
{
	FString NodeAlias;
	bool bHasNodeAlias = false;
	FAnimParentNodeAssetOverride Override;

	FString Identity() const
	{
		return bHasNodeAlias ? NodeAlias : GuidToIdentity(Override.ParentNodeGuid);
	}
};

FAssetDocumentGraphSpec MakeRootAnimGraphSpec()
{
	FAssetDocumentGraphSpec Graph;
	Graph.Id = RootAnimGraphId;
	Graph.Kind = RootAnimGraphKind;
	return Graph;
}

FAssetDocumentNodeSpec MakeNodeSpecForAlias(const FString& NodeAlias)
{
	FAssetDocumentNodeSpec Node;
	Node.Id = NodeAlias;
	return Node;
}

bool TryGetManagedNodeAlias(const UEdGraphNode* Node, FString& OutAlias)
{
	return Node && FAssetDocumentAnimationGraphRuntime::TryParseManagedNodeObjectName(Node->GetFName(), OutAlias);
}

bool IsAnimationGraph(const UEdGraph* Graph)
{
	if (!Graph)
	{
		return false;
	}

	UClass* AnimationGraphSchemaClass =
		StaticLoadClass(UObject::StaticClass(), nullptr, TEXT("/Script/AnimGraph.AnimationGraphSchema"));
	const UEdGraphSchema* Schema = Graph->GetSchema();
	return !AnimationGraphSchemaClass || (Schema && Schema->GetClass()->IsChildOf(AnimationGraphSchemaClass));
}

void GetAnimationGraphNodes(const UAnimBlueprint* AnimBlueprint, TArray<UEdGraphNode*>& OutNodes)
{
	OutNodes.Reset();
	if (!AnimBlueprint)
	{
		return;
	}

	TArray<UEdGraph*> Graphs;
	AnimBlueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (!IsAnimationGraph(Graph))
		{
			continue;
		}
		TArray<UEdGraphNode*> GraphNodes;
		Graph->GetNodesOfClass<UEdGraphNode>(GraphNodes);
		OutNodes.Append(GraphNodes);
	}
}

bool TryFindAliasForGuid(const UAnimBlueprint* AnimBlueprint, const FGuid& Guid, FString& OutAlias)
{
	TArray<UEdGraphNode*> Nodes;
	GetAnimationGraphNodes(AnimBlueprint, Nodes);
	for (const UEdGraphNode* Node : Nodes)
	{
		if (!Node || Node->NodeGuid != Guid)
		{
			continue;
		}
		if (TryGetManagedNodeAlias(Node, OutAlias))
		{
			return true;
		}
	}
	return false;
}

bool TryResolveNodeAlias(const UAnimBlueprint* AnimBlueprint, const FString& NodeAlias, FGuid& OutGuid)
{
	OutGuid.Invalidate();

	TArray<UEdGraphNode*> Nodes;
	GetAnimationGraphNodes(AnimBlueprint, Nodes);
	for (const UEdGraphNode* Node : Nodes)
	{
		FString ExistingAlias;
		if (!TryGetManagedNodeAlias(Node, ExistingAlias) || ExistingAlias != NodeAlias)
		{
			continue;
		}
		if (OutGuid.IsValid() && OutGuid != Node->NodeGuid)
		{
			OutGuid.Invalidate();
			return false;
		}
		OutGuid = Node->NodeGuid;
	}

	if (OutGuid.IsValid())
	{
		return true;
	}

	const FAssetDocumentGraphSpec RootGraph = MakeRootAnimGraphSpec();
	const FAssetDocumentNodeSpec NodeSpec = MakeNodeSpecForAlias(NodeAlias);
	const FGuid DeterministicGuid = FAssetDocumentAnimationGraphRuntime::MakeManagedNodeGuid(RootGraph, NodeSpec);
	for (const UEdGraphNode* Node : Nodes)
	{
		if (Node && Node->NodeGuid == DeterministicGuid)
		{
			OutGuid = DeterministicGuid;
			return true;
		}
	}

	return false;
}

TSharedPtr<FJsonValue> MakeAssetRefValue(const UObject* Object)
{
	if (!Object)
	{
		return MakeShared<FJsonValueNull>();
	}

	TSharedRef<FJsonObject> AssetRef = MakeShared<FJsonObject>();
	AssetRef->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	AssetRef->SetStringField(TEXT("Path"), Object->GetPathName());
	return MakeShared<FJsonValueObject>(AssetRef);
}

TSharedRef<FJsonObject> MakeEvidenceObject(const FGuid& ParentNodeGuid)
{
	TSharedRef<FJsonObject> Evidence = MakeShared<FJsonObject>();
	Evidence->SetStringField(TEXT("ParentNodeGuid"), GuidToIdentity(ParentNodeGuid));
	return Evidence;
}

TSharedRef<FJsonObject> MakeOverrideObject(const FParsedParentAssetOverride& Parsed)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	if (Parsed.bHasNodeAlias)
	{
		Object->SetStringField(TEXT("Node"), Parsed.NodeAlias);
		Object->SetObjectField(TEXT("Evidence"), MakeEvidenceObject(Parsed.Override.ParentNodeGuid));
	}
	else
	{
		Object->SetStringField(TEXT("ParentNodeGuid"), GuidToIdentity(Parsed.Override.ParentNodeGuid));
	}
	Object->SetField(TEXT("NewAsset"), MakeAssetRefValue(Parsed.Override.NewAsset));
	return Object;
}

TSharedRef<FJsonObject> MakeOverrideObject(const FAnimParentNodeAssetOverride& Override)
{
	FParsedParentAssetOverride Parsed;
	Parsed.Override = Override;
	return MakeOverrideObject(Parsed);
}

FAssetDocumentCapabilityResult ResolveAnimationAssetRef(
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	UAnimationAsset*& OutAsset)
{
	OutAsset = nullptr;
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		return AnimParentOverrideFailure(Path, TEXT("MissingParentAssetOverrideAsset"), TEXT("ParentAssetOverrides.NewAsset is required"));
	}
	if (Value->Type != EJson::Object)
	{
		return AnimParentOverrideFailure(Path, TEXT("InvalidParentAssetOverrideAsset"), TEXT("ParentAssetOverrides.NewAsset must be an AssetRef object"));
	}

	const TSharedPtr<FJsonObject> AssetRef = Value->AsObject();
	FString Kind;
	if (!AssetRef.IsValid() || !AssetRef->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("AssetRef"))
	{
		return AnimParentOverrideFailure(Path / TEXT("Kind"), TEXT("InvalidParentAssetOverrideAssetKind"), TEXT("ParentAssetOverrides.NewAsset.Kind must be AssetRef"));
	}

	FString AssetPath;
	if (!AssetRef->TryGetStringField(TEXT("Path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
	{
		if (!AssetRef->TryGetStringField(TEXT("Asset"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
		{
			return AnimParentOverrideFailure(Path / TEXT("Path"), TEXT("MissingParentAssetOverrideAssetPath"), TEXT("ParentAssetOverrides.NewAsset.Path is required"));
		}
	}

	UObject* LoadedObject = StaticLoadObject(UAnimationAsset::StaticClass(), nullptr, *AssetPath);
	OutAsset = Cast<UAnimationAsset>(LoadedObject);
	if (!OutAsset)
	{
		return AnimParentOverrideFailure(
			Path,
			TEXT("UnresolvedParentAssetOverrideAsset"),
			FString::Printf(TEXT("Failed to resolve ParentAssetOverrides.NewAsset '%s' as UAnimationAsset"), *AssetPath));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateOverrideObject(
	const TSharedPtr<FJsonObject>& Object,
	int32 Index,
	const UAnimBlueprint* AnimBlueprint,
	bool bAllowUnmaterializedNodeAlias,
	TSet<FGuid>& SeenGuids,
	TSet<FString>& SeenAliases,
	FParsedParentAssetOverride& OutOverride)
{
	OutOverride = FParsedParentAssetOverride();
	if (!Object.IsValid())
	{
		return AnimParentOverrideFailure(
			FString::Printf(TEXT("/Body/ParentAssetOverrides/%d"), Index),
			TEXT("InvalidParentAssetOverride"),
			TEXT("ParentAssetOverrides entries must be objects"));
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		if (Pair.Key != TEXT("Node") && Pair.Key != TEXT("ParentNodeGuid") && Pair.Key != TEXT("NewAsset") && Pair.Key != TEXT("Evidence"))
		{
			return AnimParentOverrideFailure(
				FString::Printf(TEXT("/Body/ParentAssetOverrides/%d/%s"), Index, *Escape(Pair.Key)),
				TEXT("UnknownParentAssetOverrideField"),
				FString::Printf(TEXT("Unknown ParentAssetOverrides field '%s'"), *Pair.Key));
		}
	}

	FString NodeAlias;
	if (Object->TryGetStringField(TEXT("Node"), NodeAlias) && !NodeAlias.TrimStartAndEnd().IsEmpty())
	{
		OutOverride.NodeAlias = NodeAlias.TrimStartAndEnd();
		OutOverride.bHasNodeAlias = true;
		if (!AnimBlueprint || !TryResolveNodeAlias(AnimBlueprint, OutOverride.NodeAlias, OutOverride.Override.ParentNodeGuid))
		{
			if (bAllowUnmaterializedNodeAlias || !AnimBlueprint)
			{
				const FAssetDocumentGraphSpec RootGraph = MakeRootAnimGraphSpec();
				const FAssetDocumentNodeSpec NodeSpec = MakeNodeSpecForAlias(OutOverride.NodeAlias);
				OutOverride.Override.ParentNodeGuid =
					FAssetDocumentAnimationGraphRuntime::MakeManagedNodeGuid(RootGraph, NodeSpec);
			}
			else
			{
				return AnimParentOverrideFailure(
					FString::Printf(TEXT("/Body/ParentAssetOverrides/%s/Node"), *Escape(OutOverride.NodeAlias)),
					TEXT("UnknownParentOverrideNode"),
					FString::Printf(TEXT("ParentAssetOverrides.Node '%s' does not resolve to an authored animation graph node."), *OutOverride.NodeAlias));
			}
		}
		if (SeenAliases.Contains(OutOverride.NodeAlias))
		{
			return AnimParentOverrideFailure(
				FString::Printf(TEXT("/Body/ParentAssetOverrides/%s/Node"), *Escape(OutOverride.NodeAlias)),
				TEXT("DuplicateParentAssetOverrideNode"),
				FString::Printf(TEXT("Duplicate ParentAssetOverrides node identity '%s'"), *OutOverride.NodeAlias));
		}
		SeenAliases.Add(OutOverride.NodeAlias);
	}
	else
	{
		FString GuidString;
		if (!Object->TryGetStringField(TEXT("ParentNodeGuid"), GuidString) || !ParseGuidIdentity(GuidString, OutOverride.Override.ParentNodeGuid))
		{
			const TSharedPtr<FJsonObject>* Evidence = nullptr;
			if (Object->TryGetObjectField(TEXT("Evidence"), Evidence) && Evidence && Evidence->IsValid())
			{
				(*Evidence)->TryGetStringField(TEXT("ParentNodeGuid"), GuidString);
			}
		}
		if (!OutOverride.Override.ParentNodeGuid.IsValid() && !ParseGuidIdentity(GuidString, OutOverride.Override.ParentNodeGuid))
		{
			return AnimParentOverrideFailure(
				FString::Printf(TEXT("/Body/ParentAssetOverrides/%d/ParentNodeGuid"), Index),
				TEXT("InvalidParentNodeGuid"),
				TEXT("ParentAssetOverrides.ParentNodeGuid must be a GUID string when Node is not authored"));
		}
	}

	if (SeenGuids.Contains(OutOverride.Override.ParentNodeGuid))
	{
		return AnimParentOverrideFailure(
			OutOverride.bHasNodeAlias
				? FString::Printf(TEXT("/Body/ParentAssetOverrides/%s/Node"), *Escape(OutOverride.NodeAlias))
				: FString::Printf(TEXT("/Body/ParentAssetOverrides/%d/ParentNodeGuid"), Index),
			TEXT("DuplicateParentAssetOverrideGuid"),
			FString::Printf(TEXT("Duplicate ParentAssetOverrides identity '%s'"), *GuidToIdentity(OutOverride.Override.ParentNodeGuid)));
	}
	SeenGuids.Add(OutOverride.Override.ParentNodeGuid);

	UAnimationAsset* Asset = nullptr;
	const FString Identity = OutOverride.Identity();
	const FAssetDocumentCapabilityResult AssetResult = ResolveAnimationAssetRef(
		Object->TryGetField(TEXT("NewAsset")),
		FString::Printf(TEXT("/Body/ParentAssetOverrides/%s/NewAsset"), *Escape(Identity)),
		Asset);
	if (!AssetResult.bSuccess)
	{
		return AssetResult;
	}
	OutOverride.Override.NewAsset = Asset;
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseDesiredOverrides(
	const TSharedPtr<FJsonValue>& DesiredValue,
	const UAnimBlueprint* AnimBlueprint,
	bool bAllowUnmaterializedNodeAlias,
	TArray<FParsedParentAssetOverride>& OutParsedOverrides,
	TArray<FAnimParentNodeAssetOverride>& OutOverrides)
{
	OutParsedOverrides.Reset();
	OutOverrides.Reset();
	if (!DesiredValue.IsValid() || DesiredValue->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (DesiredValue->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = DesiredValue->AsObject();
		if (Object.IsValid() && Object->Values.Num() == 0)
		{
			return FAssetDocumentCapabilityResult::Success();
		}
		return AnimParentOverrideFailure(TEXT("/Body/ParentAssetOverrides"), TEXT("InvalidParentAssetOverridesRegionType"), TEXT("Body.ParentAssetOverrides must be an array"));
	}
	if (DesiredValue->Type != EJson::Array)
	{
		return AnimParentOverrideFailure(TEXT("/Body/ParentAssetOverrides"), TEXT("InvalidParentAssetOverridesRegionType"), TEXT("Body.ParentAssetOverrides must be an array"));
	}

	TSet<FGuid> SeenGuids;
	TSet<FString> SeenAliases;
	const TArray<TSharedPtr<FJsonValue>>& Values = DesiredValue->AsArray();
	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		const TSharedPtr<FJsonObject> Object = Values[Index].IsValid() ? Values[Index]->AsObject() : nullptr;
		FParsedParentAssetOverride ParsedOverride;
		const FAssetDocumentCapabilityResult Result =
			ValidateOverrideObject(Object, Index, AnimBlueprint, bAllowUnmaterializedNodeAlias, SeenGuids, SeenAliases, ParsedOverride);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutParsedOverrides.Add(ParsedOverride);
		OutOverrides.Add(ParsedOverride.Override);
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Parsed AnimBlueprint ParentAssetOverrides"));
}

FAssetDocumentCapabilityResult ParseDesiredOverrides(
	const TSharedPtr<FJsonValue>& DesiredValue,
	const UAnimBlueprint* AnimBlueprint,
	bool bAllowUnmaterializedNodeAlias,
	TArray<FAnimParentNodeAssetOverride>& OutOverrides)
{
	TArray<FParsedParentAssetOverride> Ignored;
	return ParseDesiredOverrides(DesiredValue, AnimBlueprint, bAllowUnmaterializedNodeAlias, Ignored, OutOverrides);
}

FParsedParentAssetOverride MakeParsedCurrentOverride(
	const UAnimBlueprint* AnimBlueprint,
	const FAnimParentNodeAssetOverride& Override)
{
	FParsedParentAssetOverride Parsed;
	Parsed.Override = Override;
	FString Alias;
	if (TryFindAliasForGuid(AnimBlueprint, Override.ParentNodeGuid, Alias))
	{
		Parsed.NodeAlias = Alias;
		Parsed.bHasNodeAlias = true;
	}
	return Parsed;
}

TArray<TSharedPtr<FJsonValue>> MakeOverrideArray(
	const UAnimBlueprint* AnimBlueprint,
	const TArray<FAnimParentNodeAssetOverride>& Overrides)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Overrides.Num());
	for (const FAnimParentNodeAssetOverride& Override : Overrides)
	{
		Values.Add(MakeShared<FJsonValueObject>(MakeOverrideObject(MakeParsedCurrentOverride(AnimBlueprint, Override))));
	}
	return Values;
}

bool OverridesEqual(const TArray<FAnimParentNodeAssetOverride>& Left, const TArray<FAnimParentNodeAssetOverride>& Right)
{
	if (Left.Num() != Right.Num())
	{
		return false;
	}

	for (int32 Index = 0; Index < Left.Num(); ++Index)
	{
		if (Left[Index].ParentNodeGuid != Right[Index].ParentNodeGuid || Left[Index].NewAsset != Right[Index].NewAsset)
		{
			return false;
		}
	}
	return true;
}

TMap<FGuid, FAnimParentNodeAssetOverride> MakeOverrideMap(const TArray<FAnimParentNodeAssetOverride>& Overrides)
{
	TMap<FGuid, FAnimParentNodeAssetOverride> Map;
	for (const FAnimParentNodeAssetOverride& Override : Overrides)
	{
		Map.Add(Override.ParentNodeGuid, Override);
	}
	return Map;
}
}

FAssetDocumentAnimParentAssetOverrideRegionAdapter::FAssetDocumentAnimParentAssetOverrideRegionAdapter(FName InAdapterName)
	: AdapterName(InAdapterName)
{
}

FName FAssetDocumentAnimParentAssetOverrideRegionAdapter::GetName() const
{
	return AdapterName;
}

bool FAssetDocumentAnimParentAssetOverrideRegionAdapter::SupportsRegion(const FAssetDocumentRegionContext& Context) const
{
	return Context.RegionId == ParentAssetOverridesRegionId;
}

TSharedRef<FJsonObject> FAssetDocumentAnimParentAssetOverrideRegionAdapter::GetSchemaHint(const FAssetDocumentRegionContext&) const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Kind"), TEXT("AnimParentAssetOverrideIdentityArray"));
	Schema->SetStringField(TEXT("Shape"), TEXT("array<{Node?:graphNodeId, ParentNodeGuid?:guid, NewAsset:AssetRef<UAnimationAsset>, Evidence?:{ParentNodeGuid:guid}}>"));
	return Schema;
}

FAssetDocumentCapabilityResult FAssetDocumentAnimParentAssetOverrideRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	TArray<FAnimParentNodeAssetOverride> Overrides;
	return ParseDesiredOverrides(DesiredValue, Cast<UAnimBlueprint>(Context.Asset), !Context.Asset, Overrides);
}

FAssetDocumentCapabilityResult FAssetDocumentAnimParentAssetOverrideRegionAdapter::PreflightRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	TArray<FAnimParentNodeAssetOverride> Overrides;
	return ParseDesiredOverrides(DesiredValue, Cast<UAnimBlueprint>(Context.Asset), true, Overrides);
}

FAssetDocumentCapabilityResult FAssetDocumentAnimParentAssetOverrideRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	bOutChanged = false;
	TArray<FAnimParentNodeAssetOverride> DesiredOverrides;
	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Context.Asset);
	const FAssetDocumentCapabilityResult Result = ParseDesiredOverrides(DesiredValue, AnimBlueprint, false, DesiredOverrides);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (!AnimBlueprint)
	{
		return Result;
	}

	if (!OverridesEqual(AnimBlueprint->ParentAssetOverrides, DesiredOverrides))
	{
		AnimBlueprint->ParentAssetOverrides = MoveTemp(DesiredOverrides);
		bOutChanged = true;
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Applied AnimBlueprint ParentAssetOverrides"));
}

FAssetDocumentCapabilityResult FAssetDocumentAnimParentAssetOverrideRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	const UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Context.Asset);
	OutCurrentValue = MakeShared<FJsonValueArray>(MakeOverrideArray(
		AnimBlueprint,
		AnimBlueprint ? AnimBlueprint->ParentAssetOverrides : TArray<FAnimParentNodeAssetOverride>()));
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted AnimBlueprint ParentAssetOverrides"));
}

FAssetDocumentCapabilityResult FAssetDocumentAnimParentAssetOverrideRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	const UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Context.Asset);
	TArray<FParsedParentAssetOverride> ParsedDesiredOverrides;
	TArray<FAnimParentNodeAssetOverride> DesiredOverrides;
	const FAssetDocumentCapabilityResult Result = ParseDesiredOverrides(
		DesiredValue,
		AnimBlueprint,
		false,
		ParsedDesiredOverrides,
		DesiredOverrides);
	if (!Result.bSuccess)
	{
		return Result;
	}

	const TArray<FAnimParentNodeAssetOverride> CurrentOverrides = AnimBlueprint
		? AnimBlueprint->ParentAssetOverrides
		: TArray<FAnimParentNodeAssetOverride>();
	const TMap<FGuid, FAnimParentNodeAssetOverride> CurrentByGuid = MakeOverrideMap(CurrentOverrides);
	const TMap<FGuid, FAnimParentNodeAssetOverride> DesiredByGuid = MakeOverrideMap(DesiredOverrides);

	for (const FParsedParentAssetOverride& ParsedDesiredOverride : ParsedDesiredOverrides)
	{
		const FAnimParentNodeAssetOverride& DesiredOverride = ParsedDesiredOverride.Override;
		const FAnimParentNodeAssetOverride* CurrentOverride = CurrentByGuid.Find(DesiredOverride.ParentNodeGuid);
		const FString Path = OverridePath(ParsedDesiredOverride.Identity());
		if (!CurrentOverride)
		{
			FAssetDocumentJsonRegionUtils::AddDiffEntry(
				OutDiffEntries,
				Path,
				TEXT("added"),
				MakeShared<FJsonValueNull>(),
				MakeShared<FJsonValueObject>(MakeOverrideObject(ParsedDesiredOverride)));
			continue;
		}

		if (CurrentOverride->NewAsset != DesiredOverride.NewAsset)
		{
			FAssetDocumentJsonRegionUtils::AddDiffEntry(
				OutDiffEntries,
				FString::Printf(TEXT("%s/NewAsset"), *Path),
				TEXT("changed"),
				MakeAssetRefValue(CurrentOverride->NewAsset),
				MakeAssetRefValue(DesiredOverride.NewAsset));
		}
	}

	for (const FAnimParentNodeAssetOverride& CurrentOverride : CurrentOverrides)
	{
		if (DesiredByGuid.Contains(CurrentOverride.ParentNodeGuid))
		{
			continue;
		}

		FAssetDocumentJsonRegionUtils::AddDiffEntry(
			OutDiffEntries,
			OverridePath(MakeParsedCurrentOverride(AnimBlueprint, CurrentOverride).Identity()),
			TEXT("removed"),
			MakeShared<FJsonValueObject>(MakeOverrideObject(MakeParsedCurrentOverride(AnimBlueprint, CurrentOverride))),
			MakeShared<FJsonValueNull>());
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Diffed AnimBlueprint ParentAssetOverrides"));
}
