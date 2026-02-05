// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/DataAssetGenerator.h"
#include "AssetFactoryModule.h"
#include "Utils/ClassFinderUtils.h"
#include "Utils/PropertySetterUtils.h"
#include "Engine/DataAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

FGenerationResult FDataAssetGenerator::Generate(
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

	// Get class name - ClassName is REQUIRED for DataAsset, must be a subclass
	FString ClassName = GetStringField(Config, TEXT("ClassName"), TEXT(""));
	if (ClassName.IsEmpty())
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path,
			TEXT("DataAsset requires 'ClassName' field specifying a UDataAsset subclass"));
	}

	// Find class using utility
	UClass* DataAssetClass = FClassFinderUtils::FindDataAssetClass(ClassName);
	if (!DataAssetClass)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path,
			FString::Printf(TEXT("Class '%s' not found"), *ClassName));
	}

	// Cannot create base UDataAsset directly
	if (DataAssetClass == UDataAsset::StaticClass())
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path,
			TEXT("Cannot create base UDataAsset. Please specify a UDataAsset subclass in 'ClassName'"));
	}

	UDataAsset* Asset = nullptr;

	if (bExists)
	{
		// Load existing asset for update
		Asset = Cast<UDataAsset>(LoadExistingAsset(Path, Name));
		if (!Asset)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to load existing asset"));
		}
	}
	else
	{
		// Create package and asset directly
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

		Asset = NewObject<UDataAsset>(Package, DataAssetClass, *Name, RF_Public | RF_Standalone);
		if (!Asset)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create asset"));
		}

		FAssetRegistryModule::AssetCreated(Asset);
	}

	// Set properties using utility class
	TSharedPtr<FJsonObject> Properties = GetObjectField(Config, TEXT("Properties"));
	if (Properties.IsValid())
	{
		// Detect format: check if first property has "type" field (new typed format)
		bool bUseTypedFormat = false;
		for (const auto& Pair : Properties->Values)
		{
			const TSharedPtr<FJsonObject>* PropObj;
			if (Pair.Value->TryGetObject(PropObj) && (*PropObj)->HasField(TEXT("type")))
			{
				bUseTypedFormat = true;
			}
			break; // Only check first property
		}

		if (bUseTypedFormat)
		{
			FPropertySetterUtils::SetTypedPropertiesFromJson(Asset, Properties);
		}
		else
		{
			FPropertySetterUtils::SetPropertiesFromJson(Asset, Properties);
		}
	}

	// Mark dirty and save
	Asset->MarkPackageDirty();

	// Save asset
	UPackage* Package = Asset->GetOutermost();
	FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs);

	if (bExists)
	{
		return FGenerationResult::MakeUpdated(GetAssetType(), Name, Path, Asset);
	}
	return FGenerationResult::MakeSuccess(GetAssetType(), Name, Path, Asset);
}

TOptional<FString> FDataAssetGenerator::ValidateConfig(TSharedPtr<FJsonObject> Config) const
{
	if (!Config.IsValid())
	{
		return FString(TEXT("Invalid configuration object"));
	}

	if (!Config->HasField(TEXT("ClassName")))
	{
		return FString(TEXT("Missing required field 'ClassName'"));
	}

	FString ClassName;
	if (!Config->TryGetStringField(TEXT("ClassName"), ClassName) || ClassName.IsEmpty())
	{
		return FString(TEXT("'ClassName' field must be a non-empty string"));
	}

	return TOptional<FString>();
}

TArray<FString> FDataAssetGenerator::GetRequiredFields() const
{
	return { TEXT("ClassName") };
}

//~ Extract Implementation

bool FDataAssetGenerator::CanExtract(UObject* Asset) const
{
	if (!Asset || !Asset->IsA<UDataAsset>())
	{
		return false;
	}

	// Exclude types that have their own specialized generators
	static const TArray<FName> ExcludedClassNames = {
		TEXT("InputAction"),
		TEXT("InputMappingContext")
	};

	FName ClassName = Asset->GetClass()->GetFName();
	return !ExcludedClassNames.Contains(ClassName);
}

TSharedPtr<FJsonObject> FDataAssetGenerator::Extract(UObject* Asset, bool bDiffOnly) const
{
	UDataAsset* DataAsset = Cast<UDataAsset>(Asset);
	if (!DataAsset)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();

	// ClassName
	FString ClassName = DataAsset->GetClass()->GetName();
	if (ClassName.StartsWith(TEXT("U")))
	{
		ClassName = ClassName.Mid(1);
	}
	Config->SetStringField(TEXT("ClassName"), ClassName);

	// Properties
	TSharedPtr<FJsonObject> PropertiesObj = ExtractProperties(DataAsset, bDiffOnly);
	if (PropertiesObj.IsValid() && PropertiesObj->Values.Num() > 0)
	{
		Config->SetObjectField(TEXT("Properties"), PropertiesObj);
	}

	return Config;
}

TSharedPtr<FJsonObject> FDataAssetGenerator::ExtractProperties(UDataAsset* DataAsset, bool bDiffOnly) const
{
	if (!DataAsset)
	{
		return nullptr;
	}

	// Use the generic property extraction utility
	TSharedPtr<FJsonObject> AllProperties = FPropertySetterUtils::ExtractPropertiesToJson(DataAsset, true, bDiffOnly);

	if (!AllProperties.IsValid())
	{
		return nullptr;
	}

	// Filter out properties from base classes (UDataAsset, UObject)
	TSharedPtr<FJsonObject> FilteredProperties = MakeShared<FJsonObject>();
	UClass* AssetClass = DataAsset->GetClass();

	for (const auto& Pair : AllProperties->Values)
	{
		FProperty* Property = AssetClass->FindPropertyByName(*Pair.Key);
		if (Property)
		{
			UClass* OwnerClass = Property->GetOwnerClass();
			if (OwnerClass != UDataAsset::StaticClass() && OwnerClass != UObject::StaticClass())
			{
				FilteredProperties->SetField(Pair.Key, Pair.Value);
			}
		}
	}

	return FilteredProperties;
}
