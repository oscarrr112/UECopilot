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
};
