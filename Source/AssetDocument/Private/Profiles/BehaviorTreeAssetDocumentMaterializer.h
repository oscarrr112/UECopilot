// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentRegion.h"

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FBehaviorTreeAssetDocumentMaterializer
{
public:
	static FAssetDocumentCapabilityResult ValidateTree(
		const FAssetDocumentRegionContext& Context,
		const TSharedRef<FJsonObject>& Tree);

	static FAssetDocumentCapabilityResult ValidateBodyCrossRegion(
		const FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& Body);

	static FAssetDocumentCapabilityResult ApplyTree(
		FAssetDocumentRegionContext& Context,
		const TSharedRef<FJsonObject>& Tree,
		bool& bOutChanged);

	static FAssetDocumentCapabilityResult ExtractTree(
		const FAssetDocumentRegionContext& Context,
		TSharedRef<FJsonObject>& OutTree);

	static FAssetDocumentCapabilityResult DiffTree(
		const FAssetDocumentRegionContext& Context,
		const TSharedRef<FJsonObject>& DesiredTree,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries);

	static FAssetDocumentCapabilityResult CollectSemanticNodeIdsFromTree(
		const FAssetDocumentRegionContext& Context,
		const TSharedRef<FJsonObject>& Tree,
		TSet<FString>& OutIds);

	static FAssetDocumentCapabilityResult CollectSemanticNodeIds(
		const FAssetDocumentRegionContext& Context,
		TSet<FString>& OutIds);

	static FAssetDocumentCapabilityResult RebuildEditorGraph(
		FAssetDocumentRegionContext& Context,
		bool bForceRebuild,
		bool& bOutChanged);

	/** Rebuild transient Blackboard selector caches from the authored key names without dirtying the asset. */
	static FAssetDocumentCapabilityResult RefreshDerivedSelectorCaches(class UBehaviorTree& BehaviorTree);

	static FAssetDocumentCapabilityResult CollectEditorGraphNodes(
		const FAssetDocumentRegionContext& Context,
		class UEdGraph*& OutGraph,
		TMap<FString, class UEdGraphNode*>& OutNodesById);

#if WITH_DEV_AUTOMATION_TESTS
	static void FailNextEditorGraphRebuildForTest();
	static void FailNextTreeGraphSwapForTest();
	static void FailNextPreviousTreeGraphCleanupForTest();
#endif
};
