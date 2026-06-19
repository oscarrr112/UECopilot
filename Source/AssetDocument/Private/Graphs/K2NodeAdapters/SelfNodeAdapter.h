// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "Graphs/AssetDocumentNodeAdapter.h"

class UBlueprint;
class UK2Node_Self;

class FAssetDocumentK2SelfNodeAdapter final : public IAssetDocumentNodeAdapter
{
public:
	virtual FString GetClassPath() const override { return TEXT("/Script/BlueprintGraph.K2Node_Self"); }
	virtual FString GetCapability() const override { return TEXT("Self"); }

	bool ExtractNode(const UBlueprint* Blueprint, const UK2Node_Self* Node, FAssetDocumentNodeSpec& OutNode) const;
};
