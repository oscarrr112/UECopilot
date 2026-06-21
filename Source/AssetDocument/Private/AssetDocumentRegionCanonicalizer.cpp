// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentRegionCanonicalizer.h"

#include "AssetDocumentCanonicalJson.h"

namespace
{
class FAssetDocumentIdentityRegionCanonicalizationStrategy final : public IAssetDocumentRegionCanonicalizationStrategy
{
public:
	virtual TSharedPtr<FJsonValue> CanonicalizeForHash(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		return FAssetDocumentCanonicalJson::CloneWithoutExtractOnlyFields(RegionValue, Context.Policy);
	}

	virtual TSharedPtr<FJsonValue> CanonicalizeForSidecarWriteback(
		const FAssetDocumentRegionCanonicalizeContext&,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		return FAssetDocumentCanonicalJson::CloneWithoutExtractOnlyFields(RegionValue);
	}
};

const IAssetDocumentRegionCanonicalizationStrategy& GetIdentityStrategy()
{
	static FAssetDocumentIdentityRegionCanonicalizationStrategy Strategy;
	return Strategy;
}
}

TSharedPtr<FJsonValue> FAssetDocumentRegionCanonicalizer::CanonicalizeForHash(
	const FAssetDocumentRegionCanonicalizeContext& Context,
	const TSharedPtr<FJsonValue>& RegionValue)
{
	return GetIdentityStrategy().CanonicalizeForHash(Context, RegionValue);
}

TSharedPtr<FJsonValue> FAssetDocumentRegionCanonicalizer::CanonicalizeForSidecarWriteback(
	const FAssetDocumentRegionCanonicalizeContext& Context,
	const TSharedPtr<FJsonValue>& RegionValue)
{
	return GetIdentityStrategy().CanonicalizeForSidecarWriteback(Context, RegionValue);
}

FString FAssetDocumentRegionCanonicalizer::HashRegionValue(
	const FAssetDocumentRegionCanonicalizeContext& Context,
	const TSharedPtr<FJsonValue>& RegionValue)
{
	return FAssetDocumentCanonicalJson::HashJsonValue(CanonicalizeForHash(Context, RegionValue), nullptr);
}
