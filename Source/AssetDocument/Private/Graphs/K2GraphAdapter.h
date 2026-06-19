// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"
#include "Graphs/AssetDocumentGraphTypes.h"
#include "Graphs/AssetDocumentNodeAdapter.h"

class UBlueprint;
class UEdGraphNode;

struct FAssetDocumentK2GraphExtractResult
{
	TArray<FAssetDocumentGraphSpec> Graphs;
	TArray<TSharedPtr<FJsonValue>> SkippedNodes;
};

struct FAssetDocumentK2GraphApplyResult
{
	FAssetDocumentCapabilityResult Result;
	bool bChanged = false;
};

class FAssetDocumentK2GraphAdapter
{
public:
	static FAssetDocumentNodeAdapterRegistry CreateTier1NodeAdapterRegistry();

	FAssetDocumentK2GraphExtractResult ExtractUbergraphPages(const UBlueprint* Blueprint) const;
	FAssetDocumentK2GraphApplyResult ApplyUbergraphPages(UBlueprint* Blueprint, const TArray<FAssetDocumentGraphSpec>& DesiredGraphs) const;

private:
	bool ExtractNode(const UBlueprint* Blueprint, const UEdGraphNode* Node, FAssetDocumentNodeSpec& OutNode) const;
};
