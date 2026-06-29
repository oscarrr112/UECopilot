// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentRegion.h"
#include "AssetDocumentProfile.h"
#include "Profiles/AnimSequenceAssetDocumentCapability.h"
#include "Regions/AssetDocumentNamedArrayRegionAdapter.h"

class FAnimSequenceAssetDocumentProfile final : public IAssetDocumentProfile
{
public:
	static FName PilotObjectRegionAdapterName();
	static FName PreviewObjectRegionAdapterName();
	static FName PlaybackObjectRegionAdapterName();
	static FName NotifyTracksNamedArrayRegionAdapterName();
	static FAssetDocumentNamedArrayRegionAdapterConfig MakeNotifyTracksNamedArrayConfig();
	static TArray<FString> MakeNotifyTracksIdentityFieldNames();
	static FString MakeNotifyTracksIdentityJsonPointer(int32 Index);
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
