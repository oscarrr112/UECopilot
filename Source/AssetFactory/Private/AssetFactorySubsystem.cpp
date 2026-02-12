// Copyright ProjectRPG. All Rights Reserved.

#include "AssetFactorySubsystem.h"
#include "AssetFactoryModule.h"
#include "AssetGeneratorRegistry.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UAssetFactorySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogAssetFactory, Log, TEXT("AssetFactorySubsystem initialized"));
}

void UAssetFactorySubsystem::Deinitialize()
{
	UE_LOG(LogAssetFactory, Log, TEXT("AssetFactorySubsystem deinitialized"));
	Super::Deinitialize();
}

FGenerationReport UAssetFactorySubsystem::GenerateFromFile(const FString& JsonFilePath)
{
	FGenerationReport Report;

	// Read file
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Failed to read file: %s"), *JsonFilePath);
		return Report;
	}

	UE_LOG(LogAssetFactory, Log, TEXT("Loaded JSON file: %s (%d bytes)"), *JsonFilePath, JsonString.Len());
	return GenerateFromString(JsonString);
}

FGenerationReport UAssetFactorySubsystem::GenerateFromString(const FString& JsonString)
{
	FGenerationReport Report;

	// Parse JSON
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Failed to parse JSON: %s"), *Reader->GetErrorMessage());
		return Report;
	}

	return GenerateFromJson(RootObject);
}

FGenerationReport UAssetFactorySubsystem::ValidateAllConfigs(TSharedPtr<FJsonObject> RootObject) const
{
	FGenerationReport Report;

	// Validate root structure
	const TArray<TSharedPtr<FJsonValue>>* AssetsArray;
	if (!RootObject.IsValid() || !RootObject->TryGetArrayField(TEXT("Assets"), AssetsArray))
	{
		Report.AddResult(FGenerationResult::MakeFailed(TEXT(""), TEXT(""), TEXT(""), TEXT("JSON missing 'Assets' array")));
		return Report;
	}

	for (int32 i = 0; i < AssetsArray->Num(); ++i)
	{
		TSharedPtr<FJsonObject> AssetObj = (*AssetsArray)[i]->AsObject();
		if (!AssetObj.IsValid())
		{
			Report.AddResult(FGenerationResult::MakeFailed(TEXT(""), TEXT(""), TEXT(""),
				FString::Printf(TEXT("Assets[%d]: not a valid JSON object"), i)));
			continue;
		}

		// Validate common fields
		FString AssetType;
		if (!AssetObj->TryGetStringField(TEXT("AssetType"), AssetType) || AssetType.IsEmpty())
		{
			Report.AddResult(FGenerationResult::MakeFailed(TEXT("Unknown"), TEXT(""), TEXT(""),
				FString::Printf(TEXT("Assets[%d]: missing 'AssetType'"), i)));
			continue;
		}

		FString Name;
		if (!AssetObj->TryGetStringField(TEXT("Name"), Name) || Name.IsEmpty())
		{
			Report.AddResult(FGenerationResult::MakeFailed(AssetType, TEXT(""), TEXT(""),
				FString::Printf(TEXT("Assets[%d]: missing 'Name'"), i)));
			continue;
		}

		FString Path;
		if (!AssetObj->TryGetStringField(TEXT("Path"), Path) || Path.IsEmpty())
		{
			Report.AddResult(FGenerationResult::MakeFailed(AssetType, Name, TEXT(""),
				FString::Printf(TEXT("Assets[%d]: missing 'Path'"), i)));
			continue;
		}

		// Find generator
		IAssetGenerator* Generator = FAssetGeneratorRegistry::Get().FindGenerator(AssetType);
		if (!Generator)
		{
			Report.AddResult(FGenerationResult::MakeFailed(AssetType, Name, Path,
				FString::Printf(TEXT("Assets[%d]: no generator for type '%s'"), i, *AssetType)));
			continue;
		}

		// Parse action for validation
		FString ActionStr;
		AssetObj->TryGetStringField(TEXT("Action"), ActionStr);
		EGenerationAction AssetAction = ParseAction(ActionStr);

		// Per-generator validation
		TOptional<FString> GenError = Generator->ValidateConfig(AssetObj, AssetAction);
		if (GenError.IsSet())
		{
			Report.AddResult(FGenerationResult::MakeFailed(AssetType, Name, Path,
				FString::Printf(TEXT("Assets[%d]: %s"), i, *GenError.GetValue())));
			continue;
		}

		// Passed validation — add a Skipped placeholder
		Report.AddResult(FGenerationResult::MakeSkipped(AssetType, Name, Path, TEXT("Validation passed")));
	}

	return Report;
}

FGenerationReport UAssetFactorySubsystem::GenerateFromJson(TSharedPtr<FJsonObject> RootObject)
{
	FGenerationReport Report;

	if (!RootObject.IsValid())
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Invalid JSON object"));
		return Report;
	}

	// Phase 1: Validate all configs upfront
	FGenerationReport ValidationReport = ValidateAllConfigs(RootObject);
	if (ValidationReport.HasFailures())
	{
		UE_LOG(LogAssetFactory, Warning, TEXT("Validation failed for %d of %d assets. No assets were generated."),
			ValidationReport.FailedCount, ValidationReport.TotalCount);

		for (const FGenerationResult& Result : ValidationReport.Results)
		{
			const TCHAR* StatusStr = (Result.Status == EGenerationStatus::Failed) ? TEXT("FAILED") : TEXT("SKIPPED");
			UE_LOG(LogAssetFactory, Log, TEXT("[%s] %s: %s - %s"),
				StatusStr, *Result.AssetType, *Result.GetFullPath(), *Result.Message);
		}

		return ValidationReport;
	}

	// Phase 2: Generate (only if all validation passed)
	// Get Assets array (already validated in Phase 1)
	const TArray<TSharedPtr<FJsonValue>>* AssetsArray;
	RootObject->TryGetArrayField(TEXT("Assets"), AssetsArray);

	// Convert to array of objects
	TArray<TSharedPtr<FJsonObject>> AssetConfigs;
	for (const TSharedPtr<FJsonValue>& Value : *AssetsArray)
	{
		TSharedPtr<FJsonObject> AssetObj = Value->AsObject();
		if (AssetObj.IsValid())
		{
			AssetConfigs.Add(AssetObj);
		}
	}

	UE_LOG(LogAssetFactory, Log, TEXT("Validation passed for %d assets, proceeding with generation"), AssetConfigs.Num());

	// Sort by priority
	SortByPriority(AssetConfigs);

	// Process each asset
	for (const TSharedPtr<FJsonObject>& Config : AssetConfigs)
	{
		FGenerationResult Result = ProcessAssetConfig(Config);
		Report.AddResult(Result);

		// Log result
		const TCHAR* StatusStr = TEXT("Unknown");
		switch (Result.Status)
		{
		case EGenerationStatus::Success: StatusStr = TEXT("SUCCESS"); break;
		case EGenerationStatus::Updated: StatusStr = TEXT("UPDATED"); break;
		case EGenerationStatus::Skipped: StatusStr = TEXT("SKIPPED"); break;
		case EGenerationStatus::Failed: StatusStr = TEXT("FAILED"); break;
		}

		UE_LOG(LogAssetFactory, Log, TEXT("[%s] %s: %s - %s"),
			StatusStr, *Result.AssetType, *Result.GetFullPath(), *Result.Message);
	}

	UE_LOG(LogAssetFactory, Log, TEXT("Generation complete: %s"), *Report.GetSummary());
	return Report;
}

FGenerationResult UAssetFactorySubsystem::ProcessAssetConfig(TSharedPtr<FJsonObject> AssetConfig)
{
	// Get common fields
	FString AssetType;
	if (!AssetConfig->TryGetStringField(TEXT("AssetType"), AssetType))
	{
		return FGenerationResult::MakeFailed(TEXT("Unknown"), TEXT(""), TEXT(""), TEXT("Missing 'AssetType' field"));
	}

	FString Name;
	if (!AssetConfig->TryGetStringField(TEXT("Name"), Name))
	{
		return FGenerationResult::MakeFailed(AssetType, TEXT(""), TEXT(""), TEXT("Missing 'Name' field"));
	}

	FString Path;
	if (!AssetConfig->TryGetStringField(TEXT("Path"), Path))
	{
		return FGenerationResult::MakeFailed(AssetType, Name, TEXT(""), TEXT("Missing 'Path' field"));
	}

	// Parse action
	FString ActionString;
	AssetConfig->TryGetStringField(TEXT("Action"), ActionString);
	EGenerationAction Action = ParseAction(ActionString);

	// Find generator
	IAssetGenerator* Generator = FAssetGeneratorRegistry::Get().FindGenerator(AssetType);
	if (!Generator)
	{
		return FGenerationResult::MakeFailed(AssetType, Name, Path,
			FString::Printf(TEXT("No generator found for asset type '%s'"), *AssetType));
	}

	// Check if asset exists and prompt user when creating
	if (Action == EGenerationAction::Create)
	{
		FString FullPath = Path / Name;
		if (!FullPath.StartsWith(TEXT("/")))
		{
			FullPath = TEXT("/") + FullPath;
		}

		if (FPackageName::DoesPackageExist(FullPath))
		{
			// Asset exists - ask user what to do
			const FText Title = FText::FromString(TEXT("Asset Already Exists"));
			const FText Message = FText::Format(
				FText::FromString(TEXT("{0} '{1}' already exists at '{2}'.\n\nDo you want to overwrite it?\n\n[Yes] = Overwrite\n[No] = Skip this asset")),
				FText::FromString(AssetType),
				FText::FromString(Name),
				FText::FromString(Path)
			);

			EAppReturnType::Type UserChoice = FMessageDialog::Open(EAppMsgType::YesNo, Message, Title);

			if (UserChoice != EAppReturnType::Yes)
			{
				// User chose to skip
				return FGenerationResult::MakeSkipped(AssetType, Name, Path, TEXT("User chose to skip existing asset"));
			}

			// User chose to overwrite - change action to CreateOrUpdate so generator will update existing asset
			Action = EGenerationAction::CreateOrUpdate;
		}
	}

	// Validate configuration before generation
	TOptional<FString> ValidationError = Generator->ValidateConfig(AssetConfig, Action);
	if (ValidationError.IsSet())
	{
		return FGenerationResult::MakeFailed(AssetType, Name, Path, ValidationError.GetValue());
	}

	// Generate asset
	return Generator->Generate(Name, Path, Action, AssetConfig);
}

EGenerationAction UAssetFactorySubsystem::ParseAction(const FString& ActionString) const
{
	if (ActionString.Equals(TEXT("Update"), ESearchCase::IgnoreCase))
	{
		return EGenerationAction::Update;
	}
	else if (ActionString.Equals(TEXT("CreateOrUpdate"), ESearchCase::IgnoreCase))
	{
		return EGenerationAction::CreateOrUpdate;
	}
	return EGenerationAction::Create;
}

void UAssetFactorySubsystem::SortByPriority(TArray<TSharedPtr<FJsonObject>>& AssetConfigs)
{
	AssetConfigs.Sort([this](const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B)
	{
		FString TypeA, TypeB;
		A->TryGetStringField(TEXT("AssetType"), TypeA);
		B->TryGetStringField(TEXT("AssetType"), TypeB);

		return GetPriorityForAssetType(TypeA) < GetPriorityForAssetType(TypeB);
	});
}

int32 UAssetFactorySubsystem::GetPriorityForAssetType(const FString& AssetType) const
{
	IAssetGenerator* Generator = FAssetGeneratorRegistry::Get().FindGenerator(AssetType);
	return Generator ? Generator->GetPriority() : 999;
}

TArray<FString> UAssetFactorySubsystem::GetSupportedAssetTypes() const
{
	return FAssetGeneratorRegistry::Get().GetRegisteredTypes();
}

TSharedPtr<FJsonObject> UAssetFactorySubsystem::ExtractAsset(const FString& AssetPath, bool bDiffOnly)
{
	// Try multiple ways to find/load the asset
	UObject* Asset = nullptr;
	FString AssetName = FPaths::GetBaseFilename(AssetPath);
	FString FullPathWithSuffix = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);

	// First, try with the full object path (e.g., /Game/Test/MyAsset.MyAsset)
	// This is the correct way to reference the actual asset object, not the package
	Asset = StaticFindObject(UObject::StaticClass(), nullptr, *FullPathWithSuffix);

	// If not found in memory, try LoadObject with suffix
	if (!Asset)
	{
		Asset = LoadObject<UObject>(nullptr, *FullPathWithSuffix);
	}

	// Fallback: try without suffix (might work for some asset types)
	if (!Asset)
	{
		Asset = StaticFindObject(UObject::StaticClass(), nullptr, *AssetPath);
		// If we got a package, try to find the actual asset inside it
		if (Asset && Asset->IsA<UPackage>())
		{
			UPackage* Package = Cast<UPackage>(Asset);
			Asset = StaticFindObject(UObject::StaticClass(), Package, *AssetName);
		}
	}

	// Last resort: LoadObject without suffix
	if (!Asset)
	{
		Asset = LoadObject<UObject>(nullptr, *AssetPath);
	}

	if (!Asset)
	{
		UE_LOG(LogAssetFactory, Warning, TEXT("Failed to load asset: %s"), *AssetPath);
		return nullptr;
	}

	// Find a generator that can extract this asset
	TArray<FString> AssetTypes = FAssetGeneratorRegistry::Get().GetRegisteredTypes();
	for (const FString& AssetType : AssetTypes)
	{
		IAssetGenerator* Generator = FAssetGeneratorRegistry::Get().FindGenerator(AssetType);
		if (Generator && Generator->CanExtract(Asset))
		{
			TSharedPtr<FJsonObject> Config = Generator->Extract(Asset, bDiffOnly);
			if (Config.IsValid())
			{
				// Add asset metadata
				Config->SetStringField(TEXT("AssetType"), Generator->GetAssetType());

				// Extract Name and Path from the asset path (AssetName already computed above)
				FString AssetDir = FPaths::GetPath(AssetPath);
				Config->SetStringField(TEXT("Name"), AssetName);
				Config->SetStringField(TEXT("Path"), AssetDir);

				UE_LOG(LogAssetFactory, Log, TEXT("Extracted asset: %s (type: %s)"), *AssetPath, *AssetType);
				return Config;
			}
		}
	}

	UE_LOG(LogAssetFactory, Warning, TEXT("No generator can extract asset: %s (class: %s)"),
		*AssetPath, *Asset->GetClass()->GetName());
	return nullptr;
}
