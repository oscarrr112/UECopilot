// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentProfileRegistry.h"

void FAssetDocumentProfileRegistry::Register(TSharedRef<IAssetDocumentProfile> Profile)
{
	if (UClass* ExactClass = Profile->GetExactClass())
	{
		ProfilesByExactClass.Add(ExactClass, Profile);
	}
}

TSharedPtr<IAssetDocumentProfile> FAssetDocumentProfileRegistry::FindForClass(const UClass* AssetClass) const
{
	if (!AssetClass)
	{
		return nullptr;
	}

	if (const TSharedRef<IAssetDocumentProfile>* Profile = ProfilesByExactClass.Find(AssetClass))
	{
		return *Profile;
	}

	return nullptr;
}

TArray<TSharedRef<IAssetDocumentProfile>> FAssetDocumentProfileRegistry::GetAllProfiles() const
{
	TArray<TSharedRef<IAssetDocumentProfile>> Profiles;
	ProfilesByExactClass.GenerateValueArray(Profiles);
	return Profiles;
}
