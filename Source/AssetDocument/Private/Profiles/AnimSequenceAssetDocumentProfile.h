// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentRegion.h"
#include "AssetDocumentProfile.h"
#include "Profiles/AnimSequenceAssetDocumentCapability.h"

class FAnimSequenceAssetDocumentProfile final : public IAssetDocumentProfile
{
public:
	static TArray<FAssetDocumentRegionBinding> MakePilotRegionBindings();
	static TArray<FAssetDocumentRegionPolicy> MakePilotRegionPolicies();

	virtual UClass* GetExactClass() const override;
	virtual TSharedRef<FJsonObject> GetDocumentShape() const override;
	virtual TSharedRef<FJsonObject> CreateTemplate(const FAssetDocumentTemplateContext& Context) const override;
	virtual TArray<FName> GetBodyKeys() const override;
	virtual const IAssetDocumentCapability* ResolveBodyAdapter(FName BodyKey) const override;
	virtual TArray<FAssetDocumentRegionPolicy> GetRegionPolicies() const override;

private:
	FAnimSequenceAssetDocumentCapability BodyCapability;
};
