// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/DataAssetGenerator.h"
#include "AssetFactoryModule.h"
#include "Engine/DataAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "JsonObjectConverter.h"
#include "TestDataAsset.h"

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

	// Find class
	UClass* DataAssetClass = FindDataAssetClass(ClassName);
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

	// Set properties
	TSharedPtr<FJsonObject> Properties = GetObjectField(Config, TEXT("Properties"));
	if (Properties.IsValid())
	{
		SetProperties(Asset, Properties);
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

UClass* FDataAssetGenerator::FindDataAssetClass(const FString& ClassName)
{
	// Try direct load
	FString FullClassName = ClassName;
	if (!FullClassName.StartsWith(TEXT("U")))
	{
		FullClassName = TEXT("U") + FullClassName;
	}

	// Normalize: remove U prefix if present (UE class paths don't include prefix)
	FString SearchName = ClassName;
	if (SearchName.StartsWith(TEXT("U")))
	{
		SearchName = SearchName.Mid(1);
	}

	// Search in multiple modules using StaticLoadClass for reliability
	const FString ModulesToSearch[] = {
		TEXT("/Script/AssetFactory"),                                    // This plugin
		FString::Printf(TEXT("/Script/%s"), FApp::GetProjectName()),     // Main project (dynamic)
		TEXT("/Script/Engine"),                                          // Engine
	};

	UClass* FoundClass = nullptr;

	for (const FString& ModulePath : ModulesToSearch)
	{
		FString ClassPath = FString::Printf(TEXT("%s.%s"), *ModulePath, *SearchName);
		FoundClass = StaticLoadClass(UDataAsset::StaticClass(), nullptr, *ClassPath, nullptr, LOAD_None, nullptr);
		if (FoundClass)
		{
			return FoundClass;
		}
	}

	// Try loading Blueprint class
	FString BlueprintPath = FString::Printf(TEXT("/Game/Blueprints/%s.%s_C"), *ClassName, *ClassName);
	FoundClass = LoadClass<UDataAsset>(nullptr, *BlueprintPath);
	if (FoundClass)
	{
		return FoundClass;
	}

	// Return base class only if explicitly requested (will be rejected by Generate())
	if (ClassName.Equals(TEXT("DataAsset"), ESearchCase::IgnoreCase) ||
		ClassName.Equals(TEXT("UDataAsset"), ESearchCase::IgnoreCase))
	{
		return UDataAsset::StaticClass();
	}

	UE_LOG(LogAssetFactory, Warning, TEXT("Could not find DataAsset subclass '%s'"), *ClassName);
	return nullptr;
}

void FDataAssetGenerator::SetProperties(UDataAsset* Asset, TSharedPtr<FJsonObject> Properties)
{
	if (!Asset || !Properties.IsValid())
	{
		return;
	}

	UClass* Class = Asset->GetClass();

	for (const auto& Pair : Properties->Values)
	{
		const FString& PropertyName = Pair.Key;
		const TSharedPtr<FJsonValue>& JsonValue = Pair.Value;

		FProperty* Property = Class->FindPropertyByName(*PropertyName);
		if (!Property)
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Property '%s' not found on class '%s'"),
				*PropertyName, *Class->GetName());
			continue;
		}

		if (!SetPropertyFromJson(Asset, Property, JsonValue))
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Failed to set property '%s'"), *PropertyName);
		}
	}
}

bool FDataAssetGenerator::SetPropertyFromJson(UObject* Object, FProperty* Property, const TSharedPtr<FJsonValue>& JsonValue)
{
	if (!Object || !Property || !JsonValue.IsValid())
	{
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);

	// Handle numeric properties
	if (FNumericProperty* NumericProp = CastField<FNumericProperty>(Property))
	{
		if (NumericProp->IsFloatingPoint())
		{
			double Value = 0.0;
			if (JsonValue->TryGetNumber(Value))
			{
				NumericProp->SetFloatingPointPropertyValue(ValuePtr, Value);
				return true;
			}
		}
		else if (NumericProp->IsInteger())
		{
			int64 Value = 0;
			if (JsonValue->TryGetNumber(Value))
			{
				NumericProp->SetIntPropertyValue(ValuePtr, Value);
				return true;
			}
		}
	}

	// Handle bool
	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		bool Value = false;
		if (JsonValue->TryGetBool(Value))
		{
			BoolProp->SetPropertyValue(ValuePtr, Value);
			return true;
		}
	}

	// Handle string
	if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		FString Value;
		if (JsonValue->TryGetString(Value))
		{
			StrProp->SetPropertyValue(ValuePtr, Value);
			return true;
		}
	}

	// Handle FName
	if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		FString Value;
		if (JsonValue->TryGetString(Value))
		{
			NameProp->SetPropertyValue(ValuePtr, FName(*Value));
			return true;
		}
	}

	// Handle FText
	if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		FString Value;
		if (JsonValue->TryGetString(Value))
		{
			TextProp->SetPropertyValue(ValuePtr, FText::FromString(Value));
			return true;
		}
	}

	// For complex types, try FJsonObjectConverter
	if (const TSharedPtr<FJsonObject>* ObjectValue = nullptr; JsonValue->TryGetObject(ObjectValue))
	{
		return FJsonObjectConverter::JsonObjectToUStruct(ObjectValue->ToSharedRef(), Property->GetOwnerStruct(), ValuePtr);
	}

	return false;
}
