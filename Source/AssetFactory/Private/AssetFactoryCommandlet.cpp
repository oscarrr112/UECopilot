// Copyright ProjectRPG. All Rights Reserved.

#include "AssetFactoryCommandlet.h"
#include "AssetFactoryModule.h"
#include "AssetFactorySubsystem.h"
#include "GenerationTypes.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

UAssetFactoryCommandlet::UAssetFactoryCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UAssetFactoryCommandlet::Main(const FString& Params)
{
	UE_LOG(LogAssetFactory, Display, TEXT("========================================"));
	UE_LOG(LogAssetFactory, Display, TEXT("AssetFactory Commandlet"));
	UE_LOG(LogAssetFactory, Display, TEXT("========================================"));

	// Parse parameters
	FString JsonPath;
	bool bVerbose = false;

	if (!ParseParameters(Params, JsonPath, bVerbose))
	{
		PrintHelp();
		return 1;
	}

	if (bVerbose)
	{
		UE_LOG(LogAssetFactory, Display, TEXT("JSON Path: %s"), *JsonPath);
	}

	// Process JSON file
	if (!ProcessJsonFile(JsonPath, bVerbose))
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Failed to process JSON file: %s"), *JsonPath);
		return 1;
	}

	UE_LOG(LogAssetFactory, Display, TEXT("========================================"));
	UE_LOG(LogAssetFactory, Display, TEXT("AssetFactory Commandlet completed successfully"));
	UE_LOG(LogAssetFactory, Display, TEXT("========================================"));

	return 0;
}

bool UAssetFactoryCommandlet::ParseParameters(const FString& Params, FString& OutJsonPath, bool& bOutVerbose)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamMap;

	// Parse command line
	const TCHAR* ParamStr = *Params;
	ParseCommandLine(ParamStr, Tokens, Switches, ParamMap);

	// Check for help
	if (Switches.Contains(TEXT("help")) || Switches.Contains(TEXT("h")) || Switches.Contains(TEXT("?")))
	{
		return false;
	}

	// Get JSON path
	if (ParamMap.Contains(TEXT("json")))
	{
		OutJsonPath = ParamMap[TEXT("json")];
	}
	else if (Tokens.Num() > 0)
	{
		// First token as JSON path
		OutJsonPath = Tokens[0];
	}
	else
	{
		UE_LOG(LogAssetFactory, Error, TEXT("No JSON file specified"));
		return false;
	}

	// Resolve relative paths
	if (FPaths::IsRelative(OutJsonPath))
	{
		OutJsonPath = FPaths::Combine(FPaths::ProjectDir(), OutJsonPath);
	}
	OutJsonPath = FPaths::ConvertRelativePathToFull(OutJsonPath);

	// Check if file exists
	if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*OutJsonPath))
	{
		UE_LOG(LogAssetFactory, Error, TEXT("JSON file not found: %s"), *OutJsonPath);
		return false;
	}

	// Check verbose
	bOutVerbose = Switches.Contains(TEXT("verbose")) || Switches.Contains(TEXT("v"));

	return true;
}

bool UAssetFactoryCommandlet::ProcessJsonFile(const FString& JsonPath, bool bVerbose)
{
	// Get the subsystem
	UAssetFactorySubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetFactorySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogAssetFactory, Error, TEXT("AssetFactorySubsystem not available"));
		return false;
	}

	// Use the subsystem's GenerateFromFile which handles all the parsing and processing
	FGenerationReport Report = Subsystem->GenerateFromFile(JsonPath);

	// Log individual results
	for (const FGenerationResult& Result : Report.Results)
	{
		switch (Result.Status)
		{
		case EGenerationStatus::Success:
			UE_LOG(LogAssetFactory, Display, TEXT("[SUCCESS] %s: %s"), *Result.AssetType, *Result.AssetName);
			break;

		case EGenerationStatus::Updated:
			UE_LOG(LogAssetFactory, Display, TEXT("[UPDATED] %s: %s"), *Result.AssetType, *Result.AssetName);
			break;

		case EGenerationStatus::Skipped:
			UE_LOG(LogAssetFactory, Display, TEXT("[SKIPPED] %s: %s - %s"), *Result.AssetType, *Result.AssetName, *Result.Message);
			break;

		case EGenerationStatus::Failed:
			UE_LOG(LogAssetFactory, Error, TEXT("[FAILED] %s: %s - %s"), *Result.AssetType, *Result.AssetName, *Result.Message);
			break;
		}
	}

	// Summary
	UE_LOG(LogAssetFactory, Display, TEXT("----------------------------------------"));
	UE_LOG(LogAssetFactory, Display, TEXT("%s"), *Report.GetSummary());

	return !Report.HasFailures();
}

void UAssetFactoryCommandlet::PrintHelp()
{
	UE_LOG(LogAssetFactory, Display, TEXT(""));
	UE_LOG(LogAssetFactory, Display, TEXT("AssetFactory Commandlet - Generate assets from JSON configuration"));
	UE_LOG(LogAssetFactory, Display, TEXT(""));
	UE_LOG(LogAssetFactory, Display, TEXT("Usage:"));
	UE_LOG(LogAssetFactory, Display, TEXT("  UnrealEditor.exe <Project> -run=AssetFactory -json=<path> [-verbose]"));
	UE_LOG(LogAssetFactory, Display, TEXT(""));
	UE_LOG(LogAssetFactory, Display, TEXT("Parameters:"));
	UE_LOG(LogAssetFactory, Display, TEXT("  -json=<path>      Path to JSON config file (relative to project or absolute)"));
	UE_LOG(LogAssetFactory, Display, TEXT("  -verbose, -v      Enable verbose logging"));
	UE_LOG(LogAssetFactory, Display, TEXT("  -help, -h         Show this help message"));
	UE_LOG(LogAssetFactory, Display, TEXT(""));
	UE_LOG(LogAssetFactory, Display, TEXT("Note: Action (Create/Update/CreateOrUpdate) is specified per-asset in JSON"));
	UE_LOG(LogAssetFactory, Display, TEXT(""));
	UE_LOG(LogAssetFactory, Display, TEXT("Examples:"));
	UE_LOG(LogAssetFactory, Display, TEXT("  -run=AssetFactory -json=Config/AssetDefinitions/party_hud.json"));
	UE_LOG(LogAssetFactory, Display, TEXT("  -run=AssetFactory -json=Config/WidgetAssets/CombatUI.json -verbose"));
	UE_LOG(LogAssetFactory, Display, TEXT(""));
}
