// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/BlueprintGenerator.h"
#include "AssetFactoryModule.h"
#include "Utils/ClassFinderUtils.h"
#include "Utils/PropertySetterUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/BlueprintFactory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/SavePackage.h"
#include "GameFramework/Actor.h"

FGenerationResult FBlueprintGenerator::Generate(
	const FString& Name,
	const FString& Path,
	EGenerationAction Action,
	TSharedPtr<FJsonObject> Config)
{
	const bool bExists = DoesAssetExist(Path, Name);

	if (Action == EGenerationAction::Create && bExists)
	{
		return FGenerationResult::MakeSkipped(GetAssetType(), Name, Path, TEXT("Asset already exists"));
	}

	if (Action == EGenerationAction::Update && !bExists)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Asset does not exist for update"));
	}

	// Get parent class using utility
	FString ParentClassName = GetStringField(Config, TEXT("ParentClass"), TEXT("Actor"));
	UClass* ParentClass = FClassFinderUtils::FindClassByName(ParentClassName, UObject::StaticClass(), true);
	if (!ParentClass)
	{
		// Fallback to AActor if not found
		UE_LOG(LogAssetFactory, Warning, TEXT("Parent class '%s' not found, using AActor"), *ParentClassName);
		ParentClass = AActor::StaticClass();
	}

	UBlueprint* Blueprint = nullptr;

	if (bExists)
	{
		Blueprint = Cast<UBlueprint>(LoadExistingAsset(Path, Name));
		if (!Blueprint)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to load existing blueprint"));
		}
	}
	else
	{
		// Create new blueprint
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
		Factory->ParentClass = ParentClass;

		Blueprint = Cast<UBlueprint>(AssetTools.CreateAsset(Name, Path, UBlueprint::StaticClass(), Factory));
		if (!Blueprint)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create blueprint"));
		}
	}

	// Add interfaces using utility
	const TArray<TSharedPtr<FJsonValue>>* InterfacesArray = GetArrayField(Config, TEXT("Interfaces"));
	if (InterfacesArray)
	{
		for (const TSharedPtr<FJsonValue>& InterfaceValue : *InterfacesArray)
		{
			FString InterfaceName;
			if (InterfaceValue->TryGetString(InterfaceName))
			{
				UClass* InterfaceClass = FClassFinderUtils::FindInterfaceClass(InterfaceName);
				if (InterfaceClass)
				{
					// Check if interface is already implemented
					bool bAlreadyImplemented = false;
					for (const FBPInterfaceDescription& Desc : Blueprint->ImplementedInterfaces)
					{
						if (Desc.Interface == InterfaceClass)
						{
							bAlreadyImplemented = true;
							break;
						}
					}

					if (!bAlreadyImplemented)
					{
						FBlueprintEditorUtils::ImplementNewInterface(Blueprint, InterfaceClass->GetFName());
					}
				}
				else
				{
					UE_LOG(LogAssetFactory, Warning, TEXT("Interface class '%s' not found"), *InterfaceName);
				}
			}
		}
	}

	// Set default properties on CDO
	TSharedPtr<FJsonObject> DefaultProperties = GetObjectField(Config, TEXT("DefaultProperties"));
	if (DefaultProperties.IsValid())
	{
		SetDefaultProperties(Blueprint, DefaultProperties);
	}

	// Compile blueprint
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	// Save
	Blueprint->MarkPackageDirty();

	UPackage* Package = Blueprint->GetOutermost();
	FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, Blueprint, *PackageFileName, SaveArgs);

	if (bExists)
	{
		return FGenerationResult::MakeUpdated(GetAssetType(), Name, Path, Blueprint);
	}
	return FGenerationResult::MakeSuccess(GetAssetType(), Name, Path, Blueprint);
}

void FBlueprintGenerator::SetDefaultProperties(UBlueprint* Blueprint, TSharedPtr<FJsonObject> Properties)
{
	if (!Blueprint || !Properties.IsValid())
	{
		return;
	}

	UClass* GeneratedClass = Blueprint->GeneratedClass;
	if (!GeneratedClass)
	{
		return;
	}

	UObject* CDO = GeneratedClass->GetDefaultObject();
	if (!CDO)
	{
		return;
	}

	// Use PropertySetterUtils to set properties on CDO
	FPropertySetterUtils::SetPropertiesFromJson(CDO, Properties);

	// Mark CDO as modified
	CDO->Modify();
}

TOptional<FString> FBlueprintGenerator::ValidateConfig(TSharedPtr<FJsonObject> Config) const
{
	if (!Config.IsValid())
	{
		return FString(TEXT("Invalid configuration object"));
	}

	// ParentClass is optional (defaults to Actor), so no required fields
	// But if specified, we could validate it exists

	return TOptional<FString>();
}

TArray<FString> FBlueprintGenerator::GetRequiredFields() const
{
	// No strictly required fields - ParentClass defaults to "Actor"
	return {};
}
