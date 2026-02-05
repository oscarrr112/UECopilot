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

FGenerationReport UAssetFactorySubsystem::GenerateFromJson(TSharedPtr<FJsonObject> RootObject)
{
	FGenerationReport Report;

	if (!RootObject.IsValid())
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Invalid JSON object"));
		return Report;
	}

	// Get Assets array
	const TArray<TSharedPtr<FJsonValue>>* AssetsArray;
	if (!RootObject->TryGetArrayField(TEXT("Assets"), AssetsArray))
	{
		UE_LOG(LogAssetFactory, Error, TEXT("JSON missing 'Assets' array"));
		return Report;
	}

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

	UE_LOG(LogAssetFactory, Log, TEXT("Found %d asset configurations"), AssetConfigs.Num());

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
	TOptional<FString> ValidationError = Generator->ValidateConfig(AssetConfig);
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
