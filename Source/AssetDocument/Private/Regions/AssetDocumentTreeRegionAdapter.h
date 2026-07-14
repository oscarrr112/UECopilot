// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentRegion.h"

#include "CoreMinimal.h"

struct FAssetDocumentTreeRegionAdapterConfig
{
	FName Name;
	FString RootField = TEXT("Root");
	FString IdField = TEXT("Id");
	FString ClassField = TEXT("Class");
	FString ChildrenField = TEXT("Children");
	FString ChildField = TEXT("Child");
	FString DecoratorsField = TEXT("Decorators");
	FString DecoratorLogicField = TEXT("DecoratorLogic");
	FString RootDecoratorsField = TEXT("RootDecorators");
	FString RootDecoratorLogicField = TEXT("RootDecoratorLogic");
	FString ServicesField = TEXT("Services");
	FString PropertiesField = TEXT("Properties");
	FString DecoratorLogicNumberField = TEXT("Number");
	FString DuplicateNodeIdCode = TEXT("DuplicateTreeNodeId");
	bool bDuplicateNodeIdUsesSemanticPath = false;
};

struct FAssetDocumentTreeRegionAdapterHooks
{
	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		const TSharedRef<FJsonObject>&)> ValidateTree;

	TFunction<FAssetDocumentCapabilityResult(
		FAssetDocumentRegionContext&,
		const TSharedRef<FJsonObject>&,
		bool&)> ApplyTree;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		TSharedRef<FJsonObject>&)> ExtractTree;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		const TSharedRef<FJsonObject>&,
		TArray<TSharedPtr<FJsonValue>>&)> DiffTree;
};

class FAssetDocumentTreeRegionAdapter final : public IAssetDocumentRegionAdapter
{
public:
	FAssetDocumentTreeRegionAdapter();
	FAssetDocumentTreeRegionAdapter(
		FAssetDocumentTreeRegionAdapterConfig InConfig,
		FAssetDocumentTreeRegionAdapterHooks InHooks = {});

	static FName DefaultAdapterName();
	static FString MakeNodePath(const FAssetDocumentRegionContext& Context, const FString& NodeId);
	FString MakeChildEdgePath(
		const FAssetDocumentRegionContext& Context,
		const FString& ParentId,
		const FString& ChildId) const;
	FString MakeDecoratorPath(
		const FAssetDocumentRegionContext& Context,
		const FString& ParentId,
		const FString& ChildId,
		const FString& DecoratorId) const;
	FString MakeServicePath(
		const FAssetDocumentRegionContext& Context,
		const FString& OwnerId,
		const FString& ServiceId) const;

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

	FAssetDocumentCapabilityResult CollectSemanticPaths(
		const FAssetDocumentRegionContext& Context,
		const TSharedRef<FJsonObject>& Tree,
		TMap<FString, FString>& OutSemanticPaths) const;

private:
	FAssetDocumentCapabilityResult ParseTree(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& Value,
		TSharedPtr<FJsonObject>& OutTree) const;

	FAssetDocumentTreeRegionAdapterConfig Config;
	FAssetDocumentTreeRegionAdapterHooks Hooks;
};
