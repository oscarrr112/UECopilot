// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "Graphs/AssetDocumentNodeAdapter.h"

class UBlueprint;
class UK2Node_Event;

class FAssetDocumentK2EventNodeAdapter final : public IAssetDocumentNodeAdapter
{
public:
	virtual FString GetClassPath() const override { return TEXT("/Script/BlueprintGraph.K2Node_Event"); }
	virtual FString GetCapability() const override { return TEXT("Event"); }

	bool ExtractNode(const UBlueprint* Blueprint, const UK2Node_Event* Node, FAssetDocumentNodeSpec& OutNode) const;
};
