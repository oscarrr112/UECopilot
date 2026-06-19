// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"

class FUBlueprintGraphRegionAdapter
{
public:
	FAssetDocumentCapabilityResult ValidateRegions(
		const FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& BodyObject) const;
};
