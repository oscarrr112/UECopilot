// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentPolicy.h"

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

class FAssetDocumentCanonicalJson
{
public:
	static FString WriteCanonicalJson(const TSharedPtr<FJsonValue>& Value, const FAssetDocumentRegionPolicy* Policy = nullptr);
	static FString HashJsonValue(const TSharedPtr<FJsonValue>& Value, const FAssetDocumentRegionPolicy* Policy = nullptr);
	static TSharedPtr<FJsonValue> CloneWithoutExtractOnlyFields(const TSharedPtr<FJsonValue>& Value, const FAssetDocumentRegionPolicy* Policy = nullptr);
};
