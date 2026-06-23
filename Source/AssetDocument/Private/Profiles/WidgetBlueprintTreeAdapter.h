// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"

class UWidgetBlueprint;

class FWidgetBlueprintTreeAdapter
{
public:
	static TSharedRef<FJsonObject> MakeDefaultWidgetTree();
	static FAssetDocumentCapabilityResult Validate(const TSharedPtr<FJsonValue>& WidgetTreeJson);
	static FAssetDocumentCapabilityResult Preflight(const UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonValue>& WidgetTreeJson);
	static FAssetDocumentCapabilityResult Apply(UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonValue>& WidgetTreeJson, bool* bOutChanged = nullptr);
	static FAssetDocumentCapabilityResult Extract(const UWidgetBlueprint* WidgetBlueprint, TSharedRef<FJsonObject>& OutWidgetTreeJson);
	static FAssetDocumentCapabilityResult Diff(const UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonValue>& DesiredJson, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries);
	static FAssetDocumentCapabilityResult CollectVariableWidgetNames(const TSharedPtr<FJsonValue>& WidgetTreeJson, TSet<FName>& OutNames);
};
