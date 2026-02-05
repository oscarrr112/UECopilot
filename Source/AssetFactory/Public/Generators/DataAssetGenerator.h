// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetGenerator.h"

/**
 * Generator for UDataAsset subclasses
 * NOTE: Cannot create base UDataAsset directly, must specify a subclass
 *
 * JSON Config:
 * {
 *   "AssetType": "DataAsset",
 *   "Name": "DA_HeroStats",
 *   "Path": "/Game/Data/Heroes",
 *   "ClassName": "HeroDataAsset",     // REQUIRED: UDataAsset subclass name
 *   "Properties": {                   // Optional: property values to set
 *     "MaxHealth": 100.0,
 *     "MoveSpeed": 600.0
 *   }
 * }
 */
class ASSETFACTORY_API FDataAssetGenerator : public IAssetGenerator
{
public:
	virtual FString GetAssetType() const override { return TEXT("DataAsset"); }
	virtual int32 GetPriority() const override { return 50; }

	virtual FGenerationResult Generate(
		const FString& Name,
		const FString& Path,
		EGenerationAction Action,
		TSharedPtr<FJsonObject> Config
	) override;

	virtual TOptional<FString> ValidateConfig(TSharedPtr<FJsonObject> Config) const override;
	virtual TArray<FString> GetRequiredFields() const override;
};
