// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"

class FUBlueprintGraphRegionAdapter
{
public:
	FAssetDocumentCapabilityResult ValidateRegions(
		const FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& BodyObject) const;

	FAssetDocumentCapabilityResult ExtractRegions(
		const FAssetDocumentCapabilityContext& Context,
		TSharedRef<FJsonObject>& OutBodyJson) const;

	FAssetDocumentCapabilityResult DiffRegions(
		const FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& DesiredBody,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const;
};
