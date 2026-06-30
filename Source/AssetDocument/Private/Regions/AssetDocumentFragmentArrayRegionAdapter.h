// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"
#include "AssetDocumentRegion.h"

#include "CoreMinimal.h"

struct FAssetDocumentFragmentArrayRegionConfig
{
	FName AdapterName;
	FName RegionId;
	FString BodyPath;
	FString JsonPointer;
	FString SchemaLabel;
	bool bEnableDefaultDiff = true;
};

struct FAssetDocumentFragmentArrayEntry
{
	FAssetDocumentFragmentArrayEntry(
		int32 InIndex,
		FString InJsonPointer,
		TSharedRef<FJsonObject> InFragmentObject)
		: Index(InIndex)
		, JsonPointer(MoveTemp(InJsonPointer))
		, FragmentObject(MoveTemp(InFragmentObject))
	{
	}

	int32 Index = INDEX_NONE;
	FString JsonPointer;
	TSharedRef<FJsonObject> FragmentObject;
};

struct FAssetDocumentFragmentArrayHooks
{
	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		const TArray<FAssetDocumentFragmentArrayEntry>&)> Validate;

	TFunction<FAssetDocumentCapabilityResult(
		FAssetDocumentRegionContext&,
		const TArray<FAssetDocumentFragmentArrayEntry>&)> Preflight;

	TFunction<FAssetDocumentCapabilityResult(
		FAssetDocumentRegionContext&,
		const TArray<FAssetDocumentFragmentArrayEntry>&,
		bool&)> Apply;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		TArray<TSharedRef<FJsonObject>>&)> Extract;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		const TArray<FAssetDocumentFragmentArrayEntry>&,
		TArray<TSharedPtr<FJsonValue>>&)> Diff;
};

struct FAssetDocumentFragmentArrayUtils
{
	static FString MakeEntryPath(const FString& BasePath, int32 Index);
	static FAssetDocumentCapabilityResult ParseObjectEntries(
		const TSharedPtr<FJsonValue>& Value,
		const FString& BasePath,
		TArray<FAssetDocumentFragmentArrayEntry>& OutEntries);
	static TSharedRef<FJsonValue> MakeArrayValue(const TArray<TSharedRef<FJsonObject>>& Entries);
};

class FAssetDocumentFragmentArrayRegionAdapter final : public IAssetDocumentRegionAdapter
{
public:
	FAssetDocumentFragmentArrayRegionAdapter(
		FAssetDocumentFragmentArrayRegionConfig InConfig,
		FAssetDocumentFragmentArrayHooks InHooks);

	virtual FName GetName() const override;
	virtual bool SupportsRegion(const FAssetDocumentRegionContext& Context) const override;
	virtual TSharedRef<FJsonObject> GetSchemaHint(const FAssetDocumentRegionContext& Context) const override;
	virtual FAssetDocumentCapabilityResult ValidateRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue) const override;
	virtual FAssetDocumentCapabilityResult PreflightRegion(
		FAssetDocumentRegionContext& Context,
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
	FAssetDocumentFragmentArrayRegionConfig Config;
	FAssetDocumentFragmentArrayHooks Hooks;
};
