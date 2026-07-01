// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentRegion.h"

#include "CoreMinimal.h"

class UBlueprint;

struct FWidgetBlueprintRegionAdapterHooks
{
	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		const TSharedPtr<FJsonValue>&)> Validate;

	TFunction<FAssetDocumentCapabilityResult(
		FAssetDocumentRegionContext&,
		const TSharedPtr<FJsonValue>&)> Preflight;

	TFunction<FAssetDocumentCapabilityResult(
		FAssetDocumentRegionContext&,
		const TSharedPtr<FJsonValue>&,
		bool&)> Apply;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		TSharedPtr<FJsonValue>&)> Extract;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		const TSharedPtr<FJsonValue>&,
		TArray<TSharedPtr<FJsonValue>>&)> Diff;
};

class FWidgetBlueprintTreeRegionAdapter final : public IAssetDocumentRegionAdapter
{
public:
	explicit FWidgetBlueprintTreeRegionAdapter(FWidgetBlueprintRegionAdapterHooks InHooks = {});

	static FName AdapterName();

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
	FWidgetBlueprintRegionAdapterHooks Hooks;
};

class FWidgetBlueprintBindingRegionAdapter final : public IAssetDocumentRegionAdapter
{
public:
	explicit FWidgetBlueprintBindingRegionAdapter(FWidgetBlueprintRegionAdapterHooks InHooks = {});

	static FName AdapterName();

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
	FWidgetBlueprintRegionAdapterHooks Hooks;
};

class FWidgetBlueprintAnimationRegionAdapter final : public IAssetDocumentRegionAdapter
{
public:
	explicit FWidgetBlueprintAnimationRegionAdapter(FWidgetBlueprintRegionAdapterHooks InHooks = {});

	static FName AdapterName();

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
	FWidgetBlueprintRegionAdapterHooks Hooks;
};

class FWidgetBlueprintGraphRegionAdapter final : public IAssetDocumentRegionAdapter
{
public:
	explicit FWidgetBlueprintGraphRegionAdapter(FWidgetBlueprintRegionAdapterHooks InHooks = {});
	explicit FWidgetBlueprintGraphRegionAdapter(UBlueprint* InDesiredStateBlueprint, FWidgetBlueprintRegionAdapterHooks InHooks = {});

	static FName AdapterName();

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
	FWidgetBlueprintRegionAdapterHooks Hooks;
	UBlueprint* DesiredStateBlueprint = nullptr;
};
