// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"
#include "Profiles/WidgetBlueprintAssetDocumentCapability.h"

class FWidgetBlueprintAssetDocumentProfile final : public IAssetDocumentProfile
{
public:
	virtual UClass* GetExactClass() const override;
	virtual TSharedRef<FJsonObject> GetDocumentShape() const override;
	virtual TSharedRef<FJsonObject> CreateTemplate(const FAssetDocumentTemplateContext& Context) const override;
	virtual TArray<FName> GetBodyKeys() const override;
	virtual const IAssetDocumentCapability* ResolveBodyAdapter(FName BodyKey) const override;
	virtual TArray<FAssetDocumentRegionPolicy> GetRegionPolicies() const override;

private:
	FWidgetBlueprintAssetDocumentCapability BodyCapability;
};
