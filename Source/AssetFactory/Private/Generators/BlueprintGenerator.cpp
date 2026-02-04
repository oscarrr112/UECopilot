// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/BlueprintGenerator.h"
#include "AssetFactoryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/BlueprintFactory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/SavePackage.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"
#include "GameFramework/GameModeBase.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Blueprint/UserWidget.h"
#include "TestActorBase.h"

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

	// Get parent class
	FString ParentClassName = GetStringField(Config, TEXT("ParentClass"), TEXT("Actor"));
	UClass* ParentClass = FindParentClass(ParentClassName);
	if (!ParentClass)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path,
			FString::Printf(TEXT("Parent class '%s' not found"), *ParentClassName));
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

	// Add interfaces
	const TArray<TSharedPtr<FJsonValue>>* InterfacesArray = GetArrayField(Config, TEXT("Interfaces"));
	if (InterfacesArray)
	{
		for (const TSharedPtr<FJsonValue>& InterfaceValue : *InterfacesArray)
		{
			FString InterfaceName;
			if (InterfaceValue->TryGetString(InterfaceName))
			{
				UClass* InterfaceClass = FindInterfaceClass(InterfaceName);
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

UClass* FBlueprintGenerator::FindParentClass(const FString& ClassName) const
{
	// Common parent class mappings
	static TMap<FString, UClass*> ClassMap = {
		{TEXT("Actor"), AActor::StaticClass()},
		{TEXT("AActor"), AActor::StaticClass()},
		{TEXT("Pawn"), APawn::StaticClass()},
		{TEXT("APawn"), APawn::StaticClass()},
		{TEXT("Character"), ACharacter::StaticClass()},
		{TEXT("ACharacter"), ACharacter::StaticClass()},
		{TEXT("PlayerController"), APlayerController::StaticClass()},
		{TEXT("APlayerController"), APlayerController::StaticClass()},
		{TEXT("AIController"), AAIController::StaticClass()},
		{TEXT("AAIController"), AAIController::StaticClass()},
		{TEXT("GameModeBase"), AGameModeBase::StaticClass()},
		{TEXT("AGameModeBase"), AGameModeBase::StaticClass()},
		{TEXT("ActorComponent"), UActorComponent::StaticClass()},
		{TEXT("UActorComponent"), UActorComponent::StaticClass()},
		{TEXT("SceneComponent"), USceneComponent::StaticClass()},
		{TEXT("USceneComponent"), USceneComponent::StaticClass()},
		{TEXT("UserWidget"), UUserWidget::StaticClass()},
		{TEXT("UUserWidget"), UUserWidget::StaticClass()},
		{TEXT("Widget"), UUserWidget::StaticClass()},
	};

	if (UClass* const* Found = ClassMap.Find(ClassName))
	{
		return *Found;
	}

	// Normalize search name - remove prefix if present
	FString SearchName = ClassName;
	if (SearchName.StartsWith(TEXT("A")) || SearchName.StartsWith(TEXT("U")))
	{
		SearchName = SearchName.Mid(1);
	}

	// Search in multiple modules using StaticLoadClass for reliability
	const FString ModulesToSearch[] = {
		TEXT("/Script/AssetFactory"),                                    // This plugin
		FString::Printf(TEXT("/Script/%s"), FApp::GetProjectName()),     // Main project (dynamic)
		TEXT("/Script/Engine"),                                          // Engine
	};

	for (const FString& ModulePath : ModulesToSearch)
	{
		// UE class paths don't include A/U prefix
		FString DirectPath = FString::Printf(TEXT("%s.%s"), *ModulePath, *SearchName);
		UClass* FoundClass = StaticLoadClass(UObject::StaticClass(), nullptr, *DirectPath, nullptr, LOAD_None, nullptr);
		if (FoundClass)
		{
			return FoundClass;
		}
	}

	// Try loading as blueprint
	FString BlueprintPath = FString::Printf(TEXT("/Game/Blueprints/%s.%s_C"), *ClassName, *ClassName);
	UClass* LoadedClass = LoadClass<UObject>(nullptr, *BlueprintPath);
	if (LoadedClass)
	{
		return LoadedClass;
	}

	UE_LOG(LogAssetFactory, Warning, TEXT("Parent class '%s' not found, using AActor"), *ClassName);
	return AActor::StaticClass();
}

UClass* FBlueprintGenerator::FindInterfaceClass(const FString& InterfaceName) const
{
	FString SearchName = InterfaceName;

	// Remove leading 'I' if present for searching
	if (SearchName.StartsWith(TEXT("I")) && SearchName.Len() > 1)
	{
		SearchName = SearchName.Mid(1);
	}

	// Try finding with U prefix (UInterface classes)
	UClass* FoundClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/ProjectRPG.U%s"), *SearchName));
	if (FoundClass && FoundClass->IsChildOf(UInterface::StaticClass()))
	{
		return FoundClass;
	}

	// Try Engine
	FoundClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.U%s"), *SearchName));
	if (FoundClass && FoundClass->IsChildOf(UInterface::StaticClass()))
	{
		return FoundClass;
	}

	return nullptr;
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

	for (const auto& Pair : Properties->Values)
	{
		const FString& PropertyName = Pair.Key;
		const TSharedPtr<FJsonValue>& JsonValue = Pair.Value;

		FProperty* Property = GeneratedClass->FindPropertyByName(*PropertyName);
		if (!Property)
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Property '%s' not found on blueprint CDO"), *PropertyName);
			continue;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CDO);

		// Handle numeric
		if (FNumericProperty* NumericProp = CastField<FNumericProperty>(Property))
		{
			double Value = 0.0;
			if (JsonValue->TryGetNumber(Value))
			{
				if (NumericProp->IsFloatingPoint())
				{
					NumericProp->SetFloatingPointPropertyValue(ValuePtr, Value);
				}
				else
				{
					NumericProp->SetIntPropertyValue(ValuePtr, static_cast<int64>(Value));
				}
			}
		}
		// Handle bool
		else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
		{
			bool Value = false;
			if (JsonValue->TryGetBool(Value))
			{
				BoolProp->SetPropertyValue(ValuePtr, Value);
			}
		}
		// Handle string
		else if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
		{
			FString Value;
			if (JsonValue->TryGetString(Value))
			{
				StrProp->SetPropertyValue(ValuePtr, Value);
			}
		}
		// Handle TSubclassOf (class reference)
		else if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
		{
			FString ClassPath;
			if (JsonValue->TryGetString(ClassPath))
			{
				UClass* LoadedClass = LoadClass<UObject>(nullptr, *ClassPath);
				if (LoadedClass)
				{
					ClassProp->SetPropertyValue(ValuePtr, LoadedClass);
					UE_LOG(LogAssetFactory, Log, TEXT("Set class property '%s' to '%s'"), *PropertyName, *LoadedClass->GetName());
				}
				else
				{
					UE_LOG(LogAssetFactory, Warning, TEXT("Failed to load class '%s' for property '%s'"), *ClassPath, *PropertyName);
				}
			}
		}
		// Handle TSoftClassPtr (soft class reference)
		else if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Property))
		{
			FString ClassPath;
			if (JsonValue->TryGetString(ClassPath))
			{
				FSoftObjectPath SoftPath(ClassPath);
				*static_cast<FSoftObjectPtr*>(ValuePtr) = FSoftObjectPtr(SoftPath);
				UE_LOG(LogAssetFactory, Log, TEXT("Set soft class property '%s' to '%s'"), *PropertyName, *ClassPath);
			}
		}
	}

	// Mark CDO as modified
	CDO->Modify();
}
