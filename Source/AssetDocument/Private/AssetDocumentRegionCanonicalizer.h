// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentPolicy.h"

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

enum class EAssetDocumentRegionCanonicalizeSource : uint8
{
	SidecarAuthored,
	AssetEvidence
};

struct FAssetDocumentRegionCanonicalizeContext
{
	const FAssetDocumentRegionPolicy* Policy = nullptr;
	EAssetDocumentRegionCanonicalizeSource Source = EAssetDocumentRegionCanonicalizeSource::SidecarAuthored;
	UClass* AssetClass = nullptr;
};

class IAssetDocumentRegionCanonicalizationStrategy
{
public:
	virtual ~IAssetDocumentRegionCanonicalizationStrategy() = default;

	virtual TSharedPtr<FJsonValue> CanonicalizeForHash(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const = 0;

	virtual TSharedPtr<FJsonValue> CanonicalizeForSidecarWriteback(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const = 0;
};

class FAssetDocumentRegionCanonicalizer
{
public:
	static TSharedPtr<FJsonValue> CanonicalizeForHash(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue);

	static TSharedPtr<FJsonValue> CanonicalizeForSidecarWriteback(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue);

	static FString HashRegionValue(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue);
};
