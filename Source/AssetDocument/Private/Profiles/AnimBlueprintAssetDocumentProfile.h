// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"
#include "AssetDocumentRegion.h"
#include "Profiles/AnimBlueprintAssetDocumentCapability.h"

class FAnimBlueprintAssetDocumentProfile final : public IAssetDocumentProfile
{
public:
	static FName ObjectRegionAdapterName();
	static FName TargetSkeletonRegionAdapterName();
	static FName SyncGroupsRegionAdapterName();
	static FName BlueprintCommonRegionAdapterName();
	static FName AnimGraphRegionAdapterName();
	static FName StateMachineRegionAdapterName();
	static FName AnimLayerRegionAdapterName();
	static FName ParentAssetOverrideRegionAdapterName();
	static FName DeferredRegionAdapterName();
	static TArray<FAssetDocumentRegionBinding> MakeRegionBindings();

	virtual UClass* GetExactClass() const override;
	virtual TSharedRef<FJsonObject> GetDocumentShape() const override;
	virtual TSharedRef<FJsonObject> CreateTemplate(const FAssetDocumentTemplateContext& Context) const override;
	virtual TArray<FName> GetBodyKeys() const override;
	virtual const IAssetDocumentCapability* ResolveBodyAdapter(FName BodyKey) const override;
	virtual TArray<FAssetDocumentRegionPolicy> GetRegionPolicies() const override;

private:
	FAnimBlueprintAssetDocumentCapability BodyCapability;
};
