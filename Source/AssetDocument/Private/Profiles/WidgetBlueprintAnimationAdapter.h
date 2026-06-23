// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"

class UWidgetBlueprint;

class FWidgetBlueprintAnimationAdapter
{
public:
	static FAssetDocumentCapabilityResult Validate(const TSharedPtr<FJsonValue>& AnimationsJson);
	static FAssetDocumentCapabilityResult Preflight(UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonValue>& AnimationsJson);
	static FAssetDocumentCapabilityResult Apply(UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonValue>& AnimationsJson, bool* bOutChanged = nullptr);
	static FAssetDocumentCapabilityResult Extract(const UWidgetBlueprint* WidgetBlueprint, TArray<TSharedPtr<FJsonValue>>& OutAnimations);
	static FAssetDocumentCapabilityResult Diff(const UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonValue>& DesiredJson, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries);
	static FAssetDocumentCapabilityResult CheckForUnsupportedCurrentTracks(const UWidgetBlueprint* WidgetBlueprint);
	static FAssetDocumentCapabilityResult CanonicalizeDesired(const TSharedPtr<FJsonValue>& AnimationsJson, TSharedPtr<FJsonValue>& OutCanonicalJson);
};
