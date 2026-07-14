// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"

class FJsonObject;

class FAssetDocumentManagedPropertyPartition
{
public:
	static TSet<FName> CollectTopLevelPropertyNames(const IAssetDocumentProfile* Profile);
	static FAssetDocumentCapabilityResult ValidateTopLevelProperties(
		const IAssetDocumentProfile* Profile,
		const TSharedPtr<FJsonObject>& Properties);
};
