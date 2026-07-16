// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/AssetDocumentAnimationGraphNodeActionProvider.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "AnimGraphNode_Base.h"
#include "BlueprintActionFilter.h"
#include "BlueprintActionMenuBuilder.h"
#include "BlueprintActionMenuItem.h"
#include "BlueprintActionMenuUtils.h"
#include "BlueprintNodeSignature.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"

namespace
{
FString JoinPath(const FString& BasePath, const FString& Segment)
{
	const FString EscapedSegment = FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Segment);
	if (BasePath.IsEmpty())
	{
		return FString::Printf(TEXT("/%s"), *EscapedSegment);
	}
	return FString::Printf(TEXT("%s/%s"), *BasePath, *EscapedSegment);
}

FString GraphPath(const FAssetDocumentGraphSpec& GraphSpec, const FAssetDocumentAnimationGraphContext& Context)
{
	if (!Context.GraphPath.IsEmpty())
	{
		return Context.GraphPath;
	}
	return FString::Printf(TEXT("/Graphs/%s"), *FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(GraphSpec.Id));
}

FString NodePath(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentNodeSpec& NodeSpec,
	const FAssetDocumentAnimationGraphContext& Context)
{
	return JoinPath(JoinPath(GraphPath(GraphSpec, Context), TEXT("Nodes")), NodeSpec.Id);
}

FAssetDocumentCapabilityResult Failure(const FString& Path, const FString& Code, const FString& Message)
{
	return FAssetDocumentJsonRegionUtils::Failure(Path, Code, Message);
}

UBlueprint* ResolveBlueprint(const FAssetDocumentAnimationGraphContext& Context)
{
	if (Context.Blueprint)
	{
		return Context.Blueprint;
	}
	return Cast<UBlueprint>(Context.Asset);
}

FBlueprintActionContext MakeActionContext(const FAssetDocumentAnimationGraphContext& Context)
{
	FBlueprintActionContext ActionContext;
	if (UBlueprint* Blueprint = ResolveBlueprint(Context))
	{
		ActionContext.Blueprints.Add(Blueprint);
	}
	if (Context.Graph)
	{
		ActionContext.Graphs.Add(Context.Graph);
	}
	return ActionContext;
}

FString MakeActionKey(const FString& ClassPath, const FString& SpawnerSignature)
{
	if (!SpawnerSignature.IsEmpty())
	{
		return FString::Printf(TEXT("BlueprintActionMenu:%s:%s"), *ClassPath, *SpawnerSignature);
	}
	return FString::Printf(TEXT("BlueprintActionMenu:%s"), *ClassPath);
}

TSharedPtr<FJsonObject> MakeSpawnerDescriptor(
	const FString& Type,
	const FString& ClassPath,
	const FString& ActionKey,
	const FString& SpawnerSignature,
	const FString& MenuName,
	const FString& Category)
{
	TSharedPtr<FJsonObject> Spawner = MakeShared<FJsonObject>();
	Spawner->SetStringField(TEXT("Type"), Type);
	Spawner->SetStringField(TEXT("ClassPath"), ClassPath);
	if (!ActionKey.IsEmpty())
	{
		Spawner->SetStringField(TEXT("ActionKey"), ActionKey);
	}
	if (!SpawnerSignature.IsEmpty())
	{
		Spawner->SetStringField(TEXT("SpawnerSignature"), SpawnerSignature);
	}
	if (!MenuName.IsEmpty())
	{
		Spawner->SetStringField(TEXT("MenuName"), MenuName);
	}
	if (!Category.IsEmpty())
	{
		Spawner->SetStringField(TEXT("Category"), Category);
	}
	return Spawner;
}

FVector2D GetNodeLocation(const FAssetDocumentNodeSpec& NodeSpec)
{
	double X = 0.0;
	double Y = 0.0;
	if (NodeSpec.Position.IsValid())
	{
		NodeSpec.Position->TryGetNumberField(TEXT("X"), X);
		NodeSpec.Position->TryGetNumberField(TEXT("Y"), Y);
	}
	return FVector2D(X, Y);
}

bool CandidateMatches(
	const FAssetDocumentAnimationGraphNodeSpawnCandidate& Candidate,
	const FAssetDocumentAnimationGraphNodeSpawnCandidate& Desired)
{
	if (!Desired.ActionKey.IsEmpty() && Candidate.ActionKey == Desired.ActionKey)
	{
		return true;
	}
	if (!Desired.SpawnerSignature.IsEmpty() && Candidate.SpawnerSignature == Desired.SpawnerSignature)
	{
		return Candidate.ClassPath == Desired.ClassPath || Desired.ClassPath.IsEmpty();
	}
	return !Desired.ClassPath.IsEmpty() && Candidate.ClassPath == Desired.ClassPath;
}

bool IsAnimGraphNodeClass(const UClass* NodeClass)
{
	return NodeClass && NodeClass->IsChildOf(UAnimGraphNode_Base::StaticClass());
}

FAssetDocumentAnimationGraphNodeActionCandidate MakeCandidateFromAction(
	const FBlueprintActionMenuItem& MenuItem,
	const UBlueprintNodeSpawner& Spawner)
{
	FAssetDocumentAnimationGraphNodeActionCandidate Result;

	const UClass* NodeClass = Spawner.NodeClass.Get();
	const FString ClassPath = NodeClass ? NodeClass->GetPathName() : FString();
	const FBlueprintNodeSignature Signature = Spawner.GetSpawnerSignature();
	const FString SpawnerSignature = Signature.IsValid() ? Signature.ToString() : FString();
	const FString MenuName = MenuItem.GetMenuDescription().ToString();
	const FString Category = MenuItem.GetCategory().ToString();
	const FString ActionKey = MakeActionKey(ClassPath, SpawnerSignature);

	Result.Candidate.ClassPath = ClassPath;
	Result.Candidate.MenuName = MenuName;
	Result.Candidate.Category = Category;
	Result.Candidate.ActionKey = ActionKey;
	Result.Candidate.SpawnerSignature = SpawnerSignature;
	Result.Candidate.bSpawnable = IsAnimGraphNodeClass(NodeClass);
	Result.Candidate.Spawner = MakeSpawnerDescriptor(
		TEXT("BlueprintActionMenu"),
		ClassPath,
		ActionKey,
		SpawnerSignature,
		MenuName,
		Category);
	Result.Spawner = &Spawner;
	return Result;
}

FAssetDocumentAnimationGraphNodeActionCandidate MakeFallbackCandidate(
	const FString& ClassPath,
	UBlueprintNodeSpawner* Spawner)
{
	FAssetDocumentAnimationGraphNodeActionCandidate Result;
	Result.bFallback = true;
	Result.Spawner = Spawner;

	const UClass* NodeClass = Spawner ? Spawner->NodeClass.Get() : nullptr;
	const FBlueprintNodeSignature Signature = Spawner ? Spawner->GetSpawnerSignature() : FBlueprintNodeSignature();
	const FString SpawnerSignature = Signature.IsValid() ? Signature.ToString() : ClassPath;
	const FString ActionKey = FString::Printf(TEXT("BlueprintNodeSpawnerCreate:%s:%s"), *ClassPath, *SpawnerSignature);

	Result.Candidate.ClassPath = ClassPath;
	Result.Candidate.MenuName = NodeClass ? NodeClass->GetDisplayNameText().ToString() : ClassPath;
	Result.Candidate.Category = TEXT("Fallback");
	Result.Candidate.ActionKey = ActionKey;
	Result.Candidate.SpawnerSignature = SpawnerSignature;
	Result.Candidate.bSpawnable = IsAnimGraphNodeClass(NodeClass);
	Result.Candidate.Spawner = MakeSpawnerDescriptor(
		TEXT("BlueprintNodeSpawnerCreate"),
		ClassPath,
		ActionKey,
		SpawnerSignature,
		Result.Candidate.MenuName,
		Result.Candidate.Category);
	return Result;
}

void AddUniqueCandidate(
	TArray<FAssetDocumentAnimationGraphNodeActionCandidate>& Candidates,
	FAssetDocumentAnimationGraphNodeActionCandidate&& Candidate)
{
	if (Candidate.Candidate.ActionKey.IsEmpty())
	{
		return;
	}
	const bool bExists = Candidates.ContainsByPredicate(
		[&Candidate](const FAssetDocumentAnimationGraphNodeActionCandidate& Existing)
		{
			return Existing.Candidate.ActionKey == Candidate.Candidate.ActionKey;
		});
	if (!bExists)
	{
		Candidates.Add(MoveTemp(Candidate));
	}
}

}

FAssetDocumentAnimationGraphNodeActionProvider::FAssetDocumentAnimationGraphNodeActionProvider(
	bool bInContextSensitive,
	uint32 InContextTargetMask,
	bool bInAllowClassFallback)
	: bContextSensitive(bInContextSensitive)
	, ContextTargetMask(InContextTargetMask)
	, bAllowClassFallback(bInAllowClassFallback)
{
}

uint32 FAssetDocumentAnimationGraphNodeActionProvider::DefaultContextTargetMask()
{
	return EContextTargetFlags::TARGET_Blueprint |
		EContextTargetFlags::TARGET_SubComponents |
		EContextTargetFlags::TARGET_NodeTarget |
		EContextTargetFlags::TARGET_PinObject |
		EContextTargetFlags::TARGET_SiblingPinObjects |
		EContextTargetFlags::TARGET_BlueprintLibraries |
		EContextTargetFlags::TARGET_NonImportedTypes;
}

TArray<FAssetDocumentAnimationGraphNodeActionCandidate>
FAssetDocumentAnimationGraphNodeActionProvider::FindActionCandidates(
	const FAssetDocumentNodeSpec& NodeSpec,
	const FAssetDocumentAnimationGraphContext& Context) const
{
	TArray<FAssetDocumentAnimationGraphNodeActionCandidate> Candidates;

	const FBlueprintActionContext ActionContext = MakeActionContext(Context);
	const bool bCanUseEditorActionMenu =
		Context.Graph && !Context.Graph->HasAnyFlags(RF_Transient);
	if (bCanUseEditorActionMenu
		&& ActionContext.Blueprints.Num() > 0
		&& ActionContext.Graphs.Num() > 0
		&& !GIsSavingPackage
		&& !IsGarbageCollecting())
	{
		FBlueprintActionMenuBuilder MenuBuilder(FBlueprintActionMenuBuilder::DefaultConfig);
		FBlueprintActionMenuUtils::MakeContextMenu(ActionContext, bContextSensitive, ContextTargetMask, MenuBuilder);
		if (const UEdGraphSchema* Schema = Context.Graph ? Context.Graph->GetSchema() : nullptr)
		{
			Schema->InsertAdditionalActions(ActionContext.Blueprints, ActionContext.Graphs, ActionContext.Pins, MenuBuilder);
		}
		MenuBuilder.RebuildActionList();

		while (MenuBuilder.GetNumPendingActions() > 0 && MenuBuilder.ProcessPendingActions())
		{
		}

		for (int32 Index = 0; Index < MenuBuilder.GetNumActions(); ++Index)
		{
			const TSharedPtr<FEdGraphSchemaAction>& Action = MenuBuilder.GetSchemaAction(Index);
			if (!Action.IsValid() || Action->GetTypeId() != FBlueprintActionMenuItem::StaticGetTypeId())
			{
				continue;
			}

			const FBlueprintActionMenuItem* MenuItem = static_cast<const FBlueprintActionMenuItem*>(Action.Get());
			const UBlueprintNodeSpawner* Spawner = MenuItem ? MenuItem->GetRawAction() : nullptr;
			if (!Spawner || !Spawner->NodeClass.Get())
			{
				continue;
			}
			if (!NodeSpec.Class.IsEmpty() && Spawner->NodeClass->GetPathName() != NodeSpec.Class)
			{
				continue;
			}

			FAssetDocumentAnimationGraphNodeActionCandidate Candidate =
				MakeCandidateFromAction(*MenuItem, *Spawner);
			if (!Candidate.Candidate.ClassPath.IsEmpty() && Candidate.Candidate.bSpawnable)
			{
				AddUniqueCandidate(Candidates, MoveTemp(Candidate));
			}
		}
	}

	if (Candidates.IsEmpty() && bAllowClassFallback && !NodeSpec.Class.IsEmpty())
	{
		UClass* NodeClass = StaticLoadClass(
			UEdGraphNode::StaticClass(),
			nullptr,
			*NodeSpec.Class,
			{},
			LOAD_NoWarn);
		UBlueprintNodeSpawner* FallbackSpawner = NodeClass
			? UBlueprintNodeSpawner::Create(TSubclassOf<UEdGraphNode>(NodeClass), GetTransientPackage())
			: nullptr;
		AddUniqueCandidate(Candidates, MakeFallbackCandidate(NodeSpec.Class, FallbackSpawner));
	}

	Candidates.Sort(
		[](const FAssetDocumentAnimationGraphNodeActionCandidate& Left, const FAssetDocumentAnimationGraphNodeActionCandidate& Right)
		{
			if (Left.Candidate.ClassPath != Right.Candidate.ClassPath)
			{
				return Left.Candidate.ClassPath < Right.Candidate.ClassPath;
			}
			return Left.Candidate.ActionKey < Right.Candidate.ActionKey;
		});

	return Candidates;
}

TArray<FAssetDocumentAnimationGraphNodeSpawnCandidate> FAssetDocumentAnimationGraphNodeActionProvider::FindCandidates(
	const FAssetDocumentNodeSpec& NodeSpec,
	const FAssetDocumentAnimationGraphContext& Context) const
{
	TArray<FAssetDocumentAnimationGraphNodeSpawnCandidate> Result;
	for (const FAssetDocumentAnimationGraphNodeActionCandidate& Candidate : FindActionCandidates(NodeSpec, Context))
	{
		Result.Add(Candidate.Candidate);
	}
	return Result;
}

FAssetDocumentCapabilityResult FAssetDocumentAnimationGraphNodeActionProvider::SpawnNode(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentNodeSpec& NodeSpec,
	const FAssetDocumentAnimationGraphContext& Context,
	const FAssetDocumentAnimationGraphNodeSpawnCandidate& Candidate,
	UEdGraphNode*& OutNode) const
{
	OutNode = nullptr;
	const FString CurrentNodePath = NodePath(GraphSpec, NodeSpec, Context);
	if (!Context.Graph)
	{
		return Failure(
			JoinPath(CurrentNodePath, TEXT("Class")),
			TEXT("MissingAnimationGraph"),
			FString::Printf(TEXT("Graph node '%s' cannot be spawned without a target graph."), *NodeSpec.Id));
	}

	const TArray<FAssetDocumentAnimationGraphNodeActionCandidate> Candidates = FindActionCandidates(NodeSpec, Context);
	const FAssetDocumentAnimationGraphNodeActionCandidate* ActionCandidate = Candidates.FindByPredicate(
		[&Candidate](const FAssetDocumentAnimationGraphNodeActionCandidate& Existing)
		{
			return CandidateMatches(Existing.Candidate, Candidate);
		});
	if (!ActionCandidate || !ActionCandidate->Spawner)
	{
		return Failure(
			JoinPath(CurrentNodePath, TEXT("Spawner")),
			TEXT("UnresolvedGraphNodeSpawner"),
			FString::Printf(TEXT("Graph node '%s' spawn candidate could not be resolved."), *NodeSpec.Id));
	}
	if (!ActionCandidate->Candidate.bSpawnable)
	{
		return Failure(
			JoinPath(CurrentNodePath, TEXT("Class")),
			TEXT("UnspawnableGraphNodeClass"),
			FString::Printf(TEXT("Graph node '%s' is not spawnable in this graph context."), *NodeSpec.Id));
	}

	IBlueprintNodeBinder::FBindingSet EmptyBindings;
	OutNode = const_cast<UBlueprintNodeSpawner*>(ActionCandidate->Spawner)->Invoke(
		Context.Graph,
		EmptyBindings,
		GetNodeLocation(NodeSpec));
	if (!OutNode)
	{
		return Failure(
			JoinPath(CurrentNodePath, TEXT("Class")),
			TEXT("GraphNodeSpawnFailed"),
			FString::Printf(TEXT("Graph node '%s' did not produce a UE graph node."), *NodeSpec.Id));
	}

	return FAssetDocumentCapabilityResult::Success();
}
