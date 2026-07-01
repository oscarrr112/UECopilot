// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/AssetDocumentAnimationGraphRuntime.h"

#include "AssetDocumentJsonRegionUtils.h"

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

FAssetDocumentCapabilityResult ApplyGraphAfterPreflight(
	const FAssetDocumentGraphSpec& GraphSpec,
	FAssetDocumentAnimationGraphContext& Context,
	IAssetDocumentAnimationGraphStructuralHook& Hook)
{
	const FAssetDocumentCapabilityResult LocateResult = Hook.LocateOrCreateGraph(GraphSpec, Context);
	if (!LocateResult.bSuccess)
	{
		return LocateResult;
	}

	for (const FAssetDocumentGraphSpec& Subgraph : GraphSpec.Subgraphs)
	{
		FAssetDocumentAnimationGraphContext SubgraphContext = Context;
		SubgraphContext.GraphPath = JoinPath(JoinPath(GraphPath(GraphSpec, Context), TEXT("Subgraphs")), Subgraph.Id);
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
	const FString CurrentNodePath = NodePath(GraphSpec, NodeSpec, Context);
	if (!CandidateProvider.IsValid())
	{
		return RuntimeFailure(
			JoinPath(CurrentNodePath, TEXT("Class")),
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
			JoinPath(CurrentNodePath, TEXT("Class")),
			TEXT("UnspawnableGraphNodeClass"),
			FString::Printf(TEXT("No spawn candidate exists for graph node '%s'."), *NodeSpec.Id));
	}

	if (!HasSpawnerDescriptor(NodeSpec) && Candidates.Num() > 1)
	{
		return RuntimeFailure(
			JoinPath(CurrentNodePath, TEXT("Spawner")),
			TEXT("AmbiguousGraphNodeSpawner"),
			FString::Printf(TEXT("Graph node '%s' requires Node.Spawner to disambiguate candidates."), *NodeSpec.Id));
	}

	OutCandidate = Candidates[0];
	if (!OutCandidate.bSpawnable)
	{
		return RuntimeFailure(
			JoinPath(CurrentNodePath, TEXT("Class")),
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
		SubgraphContext.GraphPath = JoinPath(JoinPath(GraphPath(GraphSpec, Context), TEXT("Subgraphs")), Subgraph.Id);
		SubgraphContext.GraphKind = Subgraph.Kind;
		const FAssetDocumentCapabilityResult SubgraphResult = ValidateGraph(Subgraph, SubgraphContext);
		if (!SubgraphResult.bSuccess)
		{
			return SubgraphResult;
		}
	}

	return FAssetDocumentCapabilityResult::Success();
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
	TSharedRef<FJsonObject> Skipped = MakeShared<FJsonObject>();
	Skipped->SetStringField(TEXT("Reason"), TEXT("AnimationGraphExtractionNotMaterialized"));
	Skipped->SetStringField(TEXT("Message"), TEXT("Animation graph extraction is not materialized in the runtime shell."));
	OutGraph.UnderscoreSkipped = MakeShared<FJsonValueObject>(Skipped);
	return FAssetDocumentCapabilityResult::Success();
}
