// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentRegion.h"

#include "CoreMinimal.h"

struct FAssetDocumentNamedArrayRegionAdapterConfig
{
	FName Name = TEXT("AssetDocumentNamedArrayRegionAdapter");
	FString IdentityField = TEXT("Name");
	TArray<FString> IdentityAliases;
	FString MissingIdentityCode = TEXT("MissingNamedArrayIdentity");
	FString DuplicateIdentityCode = TEXT("DuplicateNamedArrayIdentity");
	TFunction<FString(const FString&)> NormalizeIdentity;
	bool bCanonicalizeByIdentity = true;
	bool bPreserveAuthoredApplyOrder = false;
};

struct FAssetDocumentNamedArrayRegionAdapterHooks
{
	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		const TSharedRef<FJsonObject>&,
		int32)> ValidateElement;

	TFunction<FAssetDocumentCapabilityResult(
		FAssetDocumentRegionContext&,
		const TArray<TSharedRef<FJsonObject>>&,
		bool&)> ApplyElements;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		TArray<TSharedRef<FJsonObject>>&)> ExtractElements;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		const TArray<TSharedRef<FJsonObject>>&,
		TArray<TSharedPtr<FJsonValue>>&)> DiffElements;
};

class FAssetDocumentNamedArrayRegionAdapter final : public IAssetDocumentRegionAdapter
{
public:
	FAssetDocumentNamedArrayRegionAdapter();
	FAssetDocumentNamedArrayRegionAdapter(
		FName InName,
		FString InIdentityField,
		FAssetDocumentNamedArrayRegionAdapterHooks InHooks = {});
	FAssetDocumentNamedArrayRegionAdapter(
		FAssetDocumentNamedArrayRegionAdapterConfig InConfig,
		FAssetDocumentNamedArrayRegionAdapterHooks InHooks = {});

	static FName DefaultAdapterName();

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
	FAssetDocumentCapabilityResult ParseElements(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& Value,
		TArray<TSharedRef<FJsonObject>>& OutElements) const;

	bool TryReadIdentity(
		const TSharedRef<FJsonObject>& Element,
		FString& OutIdentity,
		FString& OutIdentityField) const;

	FString NormalizeIdentity(const FString& Identity) const;
	TArray<TSharedRef<FJsonObject>> SortElementsForCanonicalOrder(TArray<TSharedRef<FJsonObject>> Elements) const;
	TSharedPtr<FJsonValue> MakeArrayValueFromElements(TArray<TSharedRef<FJsonObject>> Elements, bool bCanonicalize) const;

	FAssetDocumentNamedArrayRegionAdapterConfig Config;
	FAssetDocumentNamedArrayRegionAdapterHooks Hooks;
};
