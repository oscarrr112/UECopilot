// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"

class FAssetDocumentProfileRegistry
{
public:
	void Register(TSharedRef<IAssetDocumentProfile> Profile);
	TSharedPtr<IAssetDocumentProfile> FindForClass(const UClass* AssetClass) const;
	TArray<TSharedRef<IAssetDocumentProfile>> GetAllProfiles() const;

private:
	TMap<const UClass*, TSharedRef<IAssetDocumentProfile>> ProfilesByExactClass;
};
