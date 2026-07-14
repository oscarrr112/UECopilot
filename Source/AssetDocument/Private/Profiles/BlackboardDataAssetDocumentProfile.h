// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"
#include "AssetDocumentRegion.h"

class UBlackboardData;

class FBlackboardDataAssetDocumentCapability final : public IAssetDocumentCapability
{
public:
	static TArray<FName> GetCanonicalBodyKeys();

#if WITH_DEV_AUTOMATION_TESTS
	static void FailNextLiveApplyAfterMutationForTest(TFunction<void(UBlackboardData*)> BeforeFailure = {});
#endif

	virtual FName GetName() const override;
	virtual TArray<FName> GetInternalAdapterNames() const override;
	virtual int32 GetApplyOrder() const override;
	virtual bool SupportsAsset(const UObject* Asset) const override;
	virtual bool SupportsClass(const UClass* AssetClass) const override;
	virtual TSharedRef<FJsonObject> GetSchemaHint() const override;
	virtual FAssetDocumentCapabilityResult Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const override;
	virtual FAssetDocumentCapabilityResult Apply(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) override;
	virtual FAssetDocumentCapabilityResult Extract(const FAssetDocumentCapabilityContext& Context, TSharedRef<FJsonObject>& OutBodyJson) const override;
	virtual FAssetDocumentCapabilityResult Diff(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& DesiredJson, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const override;
};

class FBlackboardDataAssetDocumentProfile final : public IAssetDocumentProfile
{
public:
	static FName ParentRegionAdapterName();
	static FName KeysRegionAdapterName();
	static TArray<FAssetDocumentRegionBinding> MakeRegionBindings();

	virtual UClass* GetExactClass() const override;
	virtual TSharedRef<FJsonObject> GetDocumentShape() const override;
	virtual TSharedRef<FJsonObject> CreateTemplate(const FAssetDocumentTemplateContext& Context) const override;
	virtual TArray<FName> GetBodyKeys() const override;
	virtual const IAssetDocumentCapability* ResolveBodyAdapter(FName BodyKey) const override;
	virtual TArray<FAssetDocumentRegionPolicy> GetRegionPolicies() const override;

private:
	FBlackboardDataAssetDocumentCapability BodyCapability;
};
