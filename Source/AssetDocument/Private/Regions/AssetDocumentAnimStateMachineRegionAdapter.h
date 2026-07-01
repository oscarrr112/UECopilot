// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentRegion.h"

#include "CoreMinimal.h"

class FAssetDocumentAnimStateMachineRegionAdapter final : public IAssetDocumentRegionAdapter
{
public:
	explicit FAssetDocumentAnimStateMachineRegionAdapter(FName InAdapterName);

	virtual FName GetName() const override;
	virtual bool SupportsRegion(const FAssetDocumentRegionContext& Context) const override;
	virtual TSharedRef<FJsonObject> GetSchemaHint(const FAssetDocumentRegionContext& Context) const override;

	virtual FAssetDocumentCapabilityResult ValidateRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue) const override;

	virtual FAssetDocumentCapabilityResult ApplyRegion(
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		bool& bOutChanged) override;

	virtual FAssetDocumentCapabilityResult ExtractRegion(
		const FAssetDocumentRegionContext& Context,
		TSharedPtr<FJsonValue>& OutCurrentValue) const override;

	virtual FAssetDocumentCapabilityResult DiffRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const override;

private:
	FName AdapterName;
};
