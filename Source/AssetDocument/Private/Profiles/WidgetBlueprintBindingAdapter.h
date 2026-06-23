// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"

class UWidgetBlueprint;

class FWidgetBlueprintBindingAdapter
{
public:
	static FAssetDocumentCapabilityResult Validate(const TSharedPtr<FJsonValue>& BindingsJson);
	static FAssetDocumentCapabilityResult Apply(UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonValue>& BindingsJson, bool* bOutChanged = nullptr);
	static FAssetDocumentCapabilityResult Extract(const UWidgetBlueprint* WidgetBlueprint, TArray<TSharedPtr<FJsonValue>>& OutBindings);
	static FAssetDocumentCapabilityResult CanonicalizeDesired(const TSharedPtr<FJsonValue>& BindingsJson, TSharedPtr<FJsonValue>& OutCanonicalJson);
};
