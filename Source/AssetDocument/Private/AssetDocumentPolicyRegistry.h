// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentPolicy.h"

#include "CoreMinimal.h"

class FJsonObject;

class FAssetDocumentPolicyRegistry
{
public:
	static bool GetBuiltinPreset(FName PresetName, FAssetDocumentRegionPolicyPreset& OutPreset);
	static TArray<FAssetDocumentRegionPolicyPreset> GetBuiltinPresets();
	static bool ExpandPreset(
		const FAssetDocumentRegionPolicyPreset& Preset,
		const FAssetDocumentRegionPolicyOverride& Override,
		FAssetDocumentRegionPolicy& OutPolicy);

	static TSharedRef<FJsonObject> ExportPolicyToJson(const FAssetDocumentRegionPolicy& Policy);
	static TSharedRef<FJsonObject> ExportPresetToJson(const FAssetDocumentRegionPolicyPreset& Preset);
};
