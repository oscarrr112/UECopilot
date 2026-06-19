// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "Graphs/AssetDocumentGraphTypes.h"
#include "Graphs/AssetDocumentNodeAdapter.h"

class UBlueprint;
class UEdGraphNode;

struct FAssetDocumentK2GraphExtractResult
{
	TArray<FAssetDocumentGraphSpec> Graphs;
	TArray<TSharedPtr<FJsonValue>> SkippedNodes;
};

class FAssetDocumentK2GraphAdapter
{
public:
	static FAssetDocumentNodeAdapterRegistry CreateTier1NodeAdapterRegistry();

	FAssetDocumentK2GraphExtractResult ExtractUbergraphPages(const UBlueprint* Blueprint) const;

private:
	bool ExtractNode(const UBlueprint* Blueprint, const UEdGraphNode* Node, FAssetDocumentNodeSpec& OutNode) const;
};
