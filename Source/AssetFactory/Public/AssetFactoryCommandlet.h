// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "AssetFactoryCommandlet.generated.h"

/**
 * Commandlet for generating assets from JSON configuration files.
 *
 * Usage:
 *   UnrealEditor.exe ProjectName -run=AssetFactory -json=Path/To/Config.json [-verbose]
 *
 * Examples:
 *   UnrealEditor.exe ProjectRPG -run=AssetFactory -json=Config/AssetDefinitions/party_hud.json
 *   UnrealEditor.exe ProjectRPG -run=AssetFactory -json=Config/WidgetAssets/CombatUI.json -verbose
 *
 * Parameters:
 *   -json=<path> : Path to JSON config file (relative to project root or absolute)
 *   -verbose     : Enable verbose logging
 *   -help        : Show help message
 *
 * Note: Action (Create/Update/CreateOrUpdate) is specified per-asset in the JSON config.
 */
UCLASS()
class ASSETFACTORY_API UAssetFactoryCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UAssetFactoryCommandlet();

	//~ UCommandlet interface
	virtual int32 Main(const FString& Params) override;

private:
	/** Parse command line parameters */
	bool ParseParameters(const FString& Params, FString& OutJsonPath, bool& bOutVerbose);

	/** Process a single JSON file */
	bool ProcessJsonFile(const FString& JsonPath, bool bVerbose);

	/** Print usage help */
	void PrintHelp();
};
