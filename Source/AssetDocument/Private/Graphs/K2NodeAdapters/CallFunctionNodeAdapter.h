// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "Graphs/AssetDocumentNodeAdapter.h"

class UBlueprint;
class UK2Node_CallFunction;

class FAssetDocumentK2CallFunctionNodeAdapter final : public IAssetDocumentNodeAdapter
{
public:
	virtual FString GetClassPath() const override { return TEXT("/Script/BlueprintGraph.K2Node_CallFunction"); }
	virtual FString GetCapability() const override { return TEXT("CallFunction"); }

	bool ExtractNode(const UBlueprint* Blueprint, const UK2Node_CallFunction* Node, FAssetDocumentNodeSpec& OutNode) const;
};
