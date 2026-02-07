// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/DataTableGenerator.h"
#include "AssetFactoryModule.h"
#include "Engine/DataTable.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/FileHelper.h"

FGenerationResult FDataTableGenerator::Generate(
	const FString& Name,
	const FString& Path,
	EGenerationAction Action,
	TSharedPtr<FJsonObject> Config)
{
	// Check if asset exists
	const bool bExists = DoesAssetExist(Path, Name);

	if (Action == EGenerationAction::Create && bExists)
	{
		return FGenerationResult::MakeSkipped(GetAssetType(), Name, Path, TEXT("Asset already exists"));
	}

	if (Action == EGenerationAction::Update && !bExists)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Asset does not exist for update"));
	}

	// Get RowStruct name
	FString RowStructName = GetStringField(Config, TEXT("RowStruct"), TEXT(""));
	if (RowStructName.IsEmpty())
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path,
			TEXT("DataTable requires 'RowStruct' field"));
	}

	// Find the row struct
	UScriptStruct* RowStruct = FindRowStruct(RowStructName);
	if (!RowStruct)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path,
			FString::Printf(TEXT("Row struct '%s' not found or not a FTableRowBase subclass"), *RowStructName));
	}

	// Get CSV file path
	FString CSVFilePath = GetStringField(Config, TEXT("CSVFilePath"), TEXT(""));
	if (CSVFilePath.IsEmpty())
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path,
			TEXT("DataTable requires 'CSVFilePath' field"));
	}

	// Read CSV file
	if (!FPaths::FileExists(CSVFilePath))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path,
			FString::Printf(TEXT("CSV file not found: %s"), *CSVFilePath));
	}

	FString CSVContent;
	if (!FFileHelper::LoadFileToString(CSVContent, *CSVFilePath))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path,
			FString::Printf(TEXT("Failed to read CSV file: %s"), *CSVFilePath));
	}

	UDataTable* DataTable = nullptr;

	if (bExists)
	{
		// Load existing asset for update
		DataTable = Cast<UDataTable>(LoadExistingAsset(Path, Name));
		if (!DataTable)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to load existing DataTable"));
		}

		// Clear existing data and re-import
		DataTable->EmptyTable();
	}
	else
	{
		// Create new DataTable
		FString FullPath = Path / Name;
		if (!FullPath.StartsWith(TEXT("/")))
		{
			FullPath = TEXT("/") + FullPath;
		}

		UPackage* Package = CreatePackage(*FullPath);
		if (!Package)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create package"));
		}

		DataTable = NewObject<UDataTable>(Package, UDataTable::StaticClass(), *Name, RF_Public | RF_Standalone);
		if (!DataTable)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create DataTable"));
		}

		DataTable->RowStruct = RowStruct;
		FAssetRegistryModule::AssetCreated(DataTable);
	}

	// Import CSV data
	TArray<FString> ImportErrors = DataTable->CreateTableFromCSVString(CSVContent);

	if (ImportErrors.Num() > 0)
	{
		FString ErrorMsg = FString::Join(ImportErrors, TEXT("\n"));
		UE_LOG(LogAssetFactory, Warning, TEXT("DataTable CSV import warnings for '%s':\n%s"), *Name, *ErrorMsg);

		// If table is completely empty after import, treat as failure
		if (DataTable->GetRowMap().Num() == 0)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path,
				FString::Printf(TEXT("CSV import failed with errors:\n%s"), *ErrorMsg));
		}
	}

	// Mark dirty and save
	DataTable->MarkPackageDirty();

	UPackage* Package = DataTable->GetOutermost();
	FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, DataTable, *PackageFileName, SaveArgs);

	if (bExists)
	{
		return FGenerationResult::MakeUpdated(GetAssetType(), Name, Path, DataTable);
	}
	return FGenerationResult::MakeSuccess(GetAssetType(), Name, Path, DataTable);
}

TOptional<FString> FDataTableGenerator::ValidateConfig(TSharedPtr<FJsonObject> Config) const
{
	if (!Config.IsValid())
	{
		return FString(TEXT("Invalid configuration object"));
	}

	// Validate RowStruct
	FString RowStructName;
	if (!Config->TryGetStringField(TEXT("RowStruct"), RowStructName) || RowStructName.IsEmpty())
	{
		return FString(TEXT("Missing or empty required field 'RowStruct'"));
	}

	UScriptStruct* RowStruct = FindRowStruct(RowStructName);
	if (!RowStruct)
	{
		return FString::Printf(TEXT("Row struct '%s' not found or not a FTableRowBase subclass"), *RowStructName);
	}

	// Validate CSVFilePath
	FString CSVFilePath;
	if (!Config->TryGetStringField(TEXT("CSVFilePath"), CSVFilePath) || CSVFilePath.IsEmpty())
	{
		return FString(TEXT("Missing or empty required field 'CSVFilePath'"));
	}

	if (!FPaths::FileExists(CSVFilePath))
	{
		return FString::Printf(TEXT("CSV file not found: %s"), *CSVFilePath);
	}

	return TOptional<FString>();
}

TArray<FString> FDataTableGenerator::GetRequiredFields() const
{
	return { TEXT("RowStruct"), TEXT("CSVFilePath") };
}

UScriptStruct* FDataTableGenerator::FindRowStruct(const FString& StructName) const
{
	// Normalize: support both "FHeroStats" and "HeroStats"
	FString SearchName = StructName;
	FString SearchNameWithF = StructName;
	FString SearchNameWithoutF = StructName;

	if (SearchName.StartsWith(TEXT("F")))
	{
		SearchNameWithoutF = SearchName.Mid(1);
	}
	else
	{
		SearchNameWithF = TEXT("F") + SearchName;
	}

	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;
		FString Name = Struct->GetName();

		if (Name.Equals(SearchName, ESearchCase::IgnoreCase) ||
			Name.Equals(SearchNameWithF, ESearchCase::IgnoreCase) ||
			Name.Equals(SearchNameWithoutF, ESearchCase::IgnoreCase))
		{
			// Verify it's a FTableRowBase subclass
			if (Struct->IsChildOf(FTableRowBase::StaticStruct()))
			{
				return Struct;
			}
		}
	}

	return nullptr;
}

//~ Extract Implementation

bool FDataTableGenerator::CanExtract(UObject* Asset) const
{
	return Asset && Asset->IsA<UDataTable>();
}

TSharedPtr<FJsonObject> FDataTableGenerator::Extract(UObject* Asset, bool bDiffOnly) const
{
	UDataTable* DataTable = Cast<UDataTable>(Asset);
	if (!DataTable)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();

	// RowStruct name
	if (DataTable->RowStruct)
	{
		Config->SetStringField(TEXT("RowStruct"), DataTable->RowStruct->GetName());
	}

	// Export CSV content
	FString CSVContent = DataTable->GetTableAsCSV();
	if (!CSVContent.IsEmpty())
	{
		Config->SetStringField(TEXT("CSVContent"), CSVContent);
	}

	return Config;
}
