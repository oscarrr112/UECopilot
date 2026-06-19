// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/K2NodeAdapters/SelfNodeAdapter.h"

#include "K2Node_Self.h"

bool FAssetDocumentK2SelfNodeAdapter::ExtractNode(const UBlueprint*, const UK2Node_Self* Node, FAssetDocumentNodeSpec& OutNode) const
{
	if (!Node)
	{
		return false;
	}

	OutNode.Capability = GetCapability();
	return true;
}
