// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetGenerator.h"

/**
 * Generator for Blueprint classes
 *
 * JSON Config:
 * {
 *   "AssetType": "Blueprint",
 *   "Name": "BP_Enemy",
 *   "Path": "/Game/Blueprints",
 *   "ParentClass": "Character",      // Actor, Pawn, Character, PlayerController, etc.
 *   "Interfaces": ["IDamageable"],   // Optional: interfaces to implement
 *   "DefaultProperties": {           // Optional: CDO default values
 *     "MaxHealth": 100.0
 *   }
 * }
 */
class ASSETFACTORY_API FBlueprintGenerator : public IAssetGenerator
{
public:
	virtual FString GetAssetType() const override { return TEXT("Blueprint"); }
	virtual int32 GetPriority() const override { return 200; } // Lowest priority - processed last

	virtual FGenerationResult Generate(
		const FString& Name,
		const FString& Path,
		EGenerationAction Action,
		TSharedPtr<FJsonObject> Config
	) override;

	virtual TOptional<FString> ValidateConfig(TSharedPtr<FJsonObject> Config) const override;
	virtual TArray<FString> GetRequiredFields() const override;

protected:
	/** Set default properties on CDO using utility class */
	void SetDefaultProperties(UBlueprint* Blueprint, TSharedPtr<FJsonObject> Properties);
};
