// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentRegion.h"

#include "CoreMinimal.h"

struct FAssetDocumentBodyRegionDispatcherHooks
{
	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentCapabilityContext&,
		const TSharedRef<FJsonObject>&)> ValidateCrossRegion;

	TFunction<FAssetDocumentCapabilityResult(
		FAssetDocumentCapabilityContext&,
		const TSet<FName>&)> PostApplyRepair;
};

class FAssetDocumentBodyRegionDispatcher
{
public:
	FAssetDocumentBodyRegionDispatcher(
		TArray<FAssetDocumentRegionBinding> InRegionBindings,
		TArray<FAssetDocumentRegionPolicy> InRegionPolicies,
		TMap<FName, IAssetDocumentRegionAdapter*> InAdapters,
		FAssetDocumentBodyRegionDispatcherHooks InHooks = {});

	FAssetDocumentCapabilityResult ValidateBody(
		const FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonValue>& BodyJson) const;

	FAssetDocumentCapabilityResult PreflightBody(
		FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonValue>& BodyJson) const;

	FAssetDocumentCapabilityResult ApplyBody(
		FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonValue>& BodyJson,
		TSet<FName>& OutAppliedRegions) const;

	FAssetDocumentCapabilityResult ExtractBody(
		const FAssetDocumentCapabilityContext& Context,
		TSharedRef<FJsonObject>& OutBodyObject) const;

	FAssetDocumentCapabilityResult DiffBody(
		const FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonValue>& DesiredBodyJson,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const;

private:
	TArray<FAssetDocumentRegionBinding> RegionBindings;
	TMap<FName, FAssetDocumentRegionBinding> BindingsByBodyKey;
	TMap<FName, FAssetDocumentRegionPolicy> PoliciesByRegionId;
	TMap<FName, IAssetDocumentRegionAdapter*> AdaptersByName;
	FAssetDocumentBodyRegionDispatcherHooks Hooks;
};
