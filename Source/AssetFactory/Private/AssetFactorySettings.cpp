// Copyright ProjectRPG. All Rights Reserved.

#include "AssetFactorySettings.h"

UAssetFactorySettings::UAssetFactorySettings()
{
}

UAssetFactorySettings* UAssetFactorySettings::Get()
{
	return GetMutableDefault<UAssetFactorySettings>();
}
