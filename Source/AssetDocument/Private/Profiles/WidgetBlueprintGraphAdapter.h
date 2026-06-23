// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"

class UWidgetBlueprint;

class FWidgetBlueprintGraphAdapter
{
public:
	FAssetDocumentCapabilityResult ValidateRegions(
		const FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& BodyObject) const;

	FAssetDocumentCapabilityResult PreflightRegions(
		FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& BodyObject) const;

	FAssetDocumentCapabilityResult ApplyRegions(
		FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& DesiredBody,
		bool& bOutChanged) const;

	FAssetDocumentCapabilityResult ExtractRegions(
		const FAssetDocumentCapabilityContext& Context,
		TSharedRef<FJsonObject>& OutBodyJson) const;
};
