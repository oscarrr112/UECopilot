// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentRegion.h"

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;

struct FAssetDocumentEditorLayoutGraphState
{
	UEdGraph* Graph = nullptr;
	TMap<FString, UEdGraphNode*> NodesById;
};

struct FAssetDocumentEditorLayoutRegionAdapterConfig
{
	FName Name;
};

struct FAssetDocumentEditorLayoutRegionAdapterHooks
{
	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		TSet<FString>&)> CollectSemanticNodeIds;

	TFunction<FAssetDocumentCapabilityResult(
		FAssetDocumentRegionContext&,
		FAssetDocumentEditorLayoutGraphState&)> PrepareGraphForApply;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		FAssetDocumentEditorLayoutGraphState&)> CollectGraphForExtract;
};

class FAssetDocumentEditorLayoutRegionAdapter final : public IAssetDocumentRegionAdapter
{
public:
	FAssetDocumentEditorLayoutRegionAdapter(
		FAssetDocumentEditorLayoutRegionAdapterConfig InConfig,
		FAssetDocumentEditorLayoutRegionAdapterHooks InHooks);

	static FAssetDocumentCapabilityResult ValidateSemanticReferences(
		const TSharedPtr<FJsonValue>& DesiredValue,
		const FString& JsonPointer,
		const TSet<FString>& SemanticNodeIds);

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
	FAssetDocumentEditorLayoutRegionAdapterConfig Config;
	FAssetDocumentEditorLayoutRegionAdapterHooks Hooks;
};
