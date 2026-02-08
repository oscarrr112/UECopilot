// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetGenerator.h"

/**
 * Generator for GameplayTags
 *
 * Registers GameplayTags in the project. Tags are persisted to
 * Config/DefaultGameplayTags.ini and registered at runtime for immediate use.
 *
 * JSON Config:
 * {
 *   "AssetType": "GameplayTag",
 *   "Name": "GAS_StatusTags",                // Tag set label (for logging)
 *   "Path": "/Config/Tags",                  // Ignored (tags go to DefaultGameplayTags.ini)
 *   "Tags": [
 *     "Status.Burning",                      // Simple string format
 *     "Status.Frozen",
 *     {"Tag": "Damage.Fire", "DevComment": "Fire damage type"}  // Object format with comment
 *   ]
 * }
 */
class ASSETFACTORY_API FGameplayTagGenerator : public IAssetGenerator
{
public:
	virtual FString GetAssetType() const override { return TEXT("GameplayTag"); }
	virtual int32 GetPriority() const override { return -10; } // Highest priority: tags must exist before other assets reference them

	virtual FGenerationResult Generate(
		const FString& Name,
		const FString& Path,
		EGenerationAction Action,
		TSharedPtr<FJsonObject> Config
	) override;

	virtual TOptional<FString> ValidateConfig(TSharedPtr<FJsonObject> Config) const override;
	virtual TArray<FString> GetRequiredFields() const override;

	// Tags are not UObject assets, so no extract support
	virtual bool CanExtract(UObject* Asset) const override { return false; }

private:
	struct FTagDefinition
	{
		FString Tag;
		FString DevComment;
	};

	/** Parse tag definitions from the "Tags" array in config */
	TArray<FTagDefinition> ParseTagDefinitions(const TArray<TSharedPtr<FJsonValue>>* TagsArray) const;
};
