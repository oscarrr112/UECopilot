// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "GenerationTypes.h"
#include "Dom/JsonObject.h"
#include "AssetFactorySubsystem.generated.h"

/**
 * Editor Subsystem for asset generation
 * Main entry point for generating assets from JSON
 */
UCLASS()
class ASSETFACTORY_API UAssetFactorySubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	/** UEditorSubsystem interface */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Generate assets from a JSON file
	 * @param JsonFilePath - Path to the JSON configuration file
	 * @return Generation report with results for each asset
	 */
	UFUNCTION(BlueprintCallable, Category = "AssetFactory")
	FGenerationReport GenerateFromFile(const FString& JsonFilePath);

	/**
	 * Generate assets from a JSON string
	 * @param JsonString - JSON configuration string
	 * @return Generation report with results for each asset
	 */
	UFUNCTION(BlueprintCallable, Category = "AssetFactory")
	FGenerationReport GenerateFromString(const FString& JsonString);

	/**
	 * Generate assets from a parsed JSON object
	 * @param RootObject - Parsed JSON object
	 * @return Generation report with results for each asset
	 */
	FGenerationReport GenerateFromJson(TSharedPtr<FJsonObject> RootObject);

	/**
	 * Get list of supported asset types
	 */
	UFUNCTION(BlueprintCallable, Category = "AssetFactory")
	TArray<FString> GetSupportedAssetTypes() const;

	/**
	 * Extract asset configuration as JSON (reverse of Generate)
	 * @param AssetPath - Full path to the asset (e.g., "/Game/Test/BP_MyActor")
	 * @param bDiffOnly - If true, only extract values different from defaults
	 * @return JSON configuration that can be used with Generate(), or nullptr if extraction failed
	 */
	TSharedPtr<FJsonObject> ExtractAsset(const FString& AssetPath, bool bDiffOnly = false);

	/**
	 * Validate all asset configs upfront before any generation.
	 * Returns a report where Failed entries have error messages and
	 * Skipped entries passed validation. If HasFailures(), no generation should proceed.
	 */
	FGenerationReport ValidateAllConfigs(TSharedPtr<FJsonObject> RootObject) const;

private:
	/**
	 * Process a single asset configuration
	 */
	FGenerationResult ProcessAssetConfig(TSharedPtr<FJsonObject> AssetConfig);

	/**
	 * Parse action string to enum
	 */
	EGenerationAction ParseAction(const FString& ActionString) const;

	/**
	 * Sort asset configurations by generator priority
	 */
	void SortByPriority(TArray<TSharedPtr<FJsonObject>>& AssetConfigs);

	/**
	 * Get priority for an asset type
	 */
	int32 GetPriorityForAssetType(const FString& AssetType) const;
};
