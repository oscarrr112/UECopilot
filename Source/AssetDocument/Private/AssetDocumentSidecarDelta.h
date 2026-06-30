// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentPolicy.h"
#include "AssetDocumentRegionCanonicalizer.h"

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

enum class EAssetDocumentSidecarRegionState : uint8
{
	Unset,
	Present,
	ExplicitEmpty
};

struct FAssetDocumentSidecarRegionValue
{
	EAssetDocumentSidecarRegionState State = EAssetDocumentSidecarRegionState::Unset;
	TSharedPtr<FJsonValue> Value;
};

class FAssetDocumentSidecarDelta
{
public:
	static FAssetDocumentSidecarRegionValue FindRegionValue(const TSharedRef<FJsonObject>& DocumentJson, const FAssetDocumentRegionPolicy& Policy);
	static bool SetRegionValue(const TSharedRef<FJsonObject>& DocumentJson, const FAssetDocumentRegionPolicy& Policy, const TSharedPtr<FJsonValue>& Value, FString& OutError);

	// Unset regions hash to an empty string; present and explicit-empty regions hash their canonical JSON value.
	static FString HashSidecarRegion(const TSharedRef<FJsonObject>& DocumentJson, const FAssetDocumentRegionPolicy& Policy);
	static FString HashSidecarRegion(
		const TSharedRef<FJsonObject>& DocumentJson,
		const FAssetDocumentRegionPolicy& Policy,
		EAssetDocumentRegionCanonicalizeSource Source,
		UClass* AssetClass = nullptr);
	static bool IsExplicitEmptyRegion(const TSharedPtr<FJsonValue>& Value, const FAssetDocumentRegionPolicy& Policy);
};
