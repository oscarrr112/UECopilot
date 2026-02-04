// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/InputActionGenerator.h"
#include "AssetFactoryModule.h"
#include "InputAction.h"
#include "InputTriggers.h"
#include "InputModifiers.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/DataAssetFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

FGenerationResult FInputActionGenerator::Generate(
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

	UInputAction* InputAction = nullptr;

	if (bExists)
	{
		InputAction = Cast<UInputAction>(LoadExistingAsset(Path, Name));
		if (!InputAction)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to load existing input action"));
		}
		// Clear existing triggers and modifiers for update
		InputAction->Triggers.Empty();
		InputAction->Modifiers.Empty();
	}
	else
	{
		// Create package and asset directly (InputAction doesn't need factory)
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

		InputAction = NewObject<UInputAction>(Package, UInputAction::StaticClass(), *Name, RF_Public | RF_Standalone);
		if (!InputAction)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create input action"));
		}

		FAssetRegistryModule::AssetCreated(InputAction);
	}

	// Set value type
	FString ValueTypeStr = GetStringField(Config, TEXT("ValueType"), TEXT("Boolean"));
	InputAction->ValueType = ParseValueType(ValueTypeStr);

	// Add triggers
	const TArray<TSharedPtr<FJsonValue>>* TriggersArray = GetArrayField(Config, TEXT("Triggers"));
	if (TriggersArray)
	{
		for (const TSharedPtr<FJsonValue>& TriggerValue : *TriggersArray)
		{
			FString TriggerName;
			if (TriggerValue->TryGetString(TriggerName))
			{
				if (UInputTrigger* Trigger = CreateTrigger(InputAction, TriggerName))
				{
					InputAction->Triggers.Add(Trigger);
				}
			}
		}
	}

	// Add modifiers
	const TArray<TSharedPtr<FJsonValue>>* ModifiersArray = GetArrayField(Config, TEXT("Modifiers"));
	if (ModifiersArray)
	{
		for (const TSharedPtr<FJsonValue>& ModifierValue : *ModifiersArray)
		{
			FString ModifierName;
			if (ModifierValue->TryGetString(ModifierName))
			{
				if (UInputModifier* Modifier = CreateModifier(InputAction, ModifierName))
				{
					InputAction->Modifiers.Add(Modifier);
				}
			}
		}
	}

	// Save
	InputAction->MarkPackageDirty();

	UPackage* Package = InputAction->GetOutermost();
	FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, InputAction, *PackageFileName, SaveArgs);

	if (bExists)
	{
		return FGenerationResult::MakeUpdated(GetAssetType(), Name, Path, InputAction);
	}
	return FGenerationResult::MakeSuccess(GetAssetType(), Name, Path, InputAction);
}

EInputActionValueType FInputActionGenerator::ParseValueType(const FString& TypeString) const
{
	if (TypeString.Equals(TEXT("Axis1D"), ESearchCase::IgnoreCase))
	{
		return EInputActionValueType::Axis1D;
	}
	else if (TypeString.Equals(TEXT("Axis2D"), ESearchCase::IgnoreCase))
	{
		return EInputActionValueType::Axis2D;
	}
	else if (TypeString.Equals(TEXT("Axis3D"), ESearchCase::IgnoreCase))
	{
		return EInputActionValueType::Axis3D;
	}
	return EInputActionValueType::Boolean;
}

UInputTrigger* FInputActionGenerator::CreateTrigger(UInputAction* Outer, const FString& TriggerName) const
{
	if (TriggerName.Equals(TEXT("Down"), ESearchCase::IgnoreCase))
	{
		return NewObject<UInputTriggerDown>(Outer);
	}
	else if (TriggerName.Equals(TEXT("Pressed"), ESearchCase::IgnoreCase))
	{
		return NewObject<UInputTriggerPressed>(Outer);
	}
	else if (TriggerName.Equals(TEXT("Released"), ESearchCase::IgnoreCase))
	{
		return NewObject<UInputTriggerReleased>(Outer);
	}
	else if (TriggerName.Equals(TEXT("Hold"), ESearchCase::IgnoreCase))
	{
		return NewObject<UInputTriggerHold>(Outer);
	}
	else if (TriggerName.Equals(TEXT("Tap"), ESearchCase::IgnoreCase))
	{
		return NewObject<UInputTriggerTap>(Outer);
	}
	else if (TriggerName.Equals(TEXT("Pulse"), ESearchCase::IgnoreCase))
	{
		return NewObject<UInputTriggerPulse>(Outer);
	}

	UE_LOG(LogAssetFactory, Warning, TEXT("Unknown trigger type: %s"), *TriggerName);
	return nullptr;
}

UInputModifier* FInputActionGenerator::CreateModifier(UInputAction* Outer, const FString& ModifierName) const
{
	if (ModifierName.Equals(TEXT("Negate"), ESearchCase::IgnoreCase))
	{
		return NewObject<UInputModifierNegate>(Outer);
	}
	else if (ModifierName.Equals(TEXT("Swizzle"), ESearchCase::IgnoreCase) ||
			 ModifierName.Equals(TEXT("SwizzleAxis"), ESearchCase::IgnoreCase))
	{
		return NewObject<UInputModifierSwizzleAxis>(Outer);
	}
	else if (ModifierName.Equals(TEXT("Scalar"), ESearchCase::IgnoreCase))
	{
		return NewObject<UInputModifierScalar>(Outer);
	}
	else if (ModifierName.Equals(TEXT("DeadZone"), ESearchCase::IgnoreCase))
	{
		return NewObject<UInputModifierDeadZone>(Outer);
	}
	else if (ModifierName.Equals(TEXT("Smooth"), ESearchCase::IgnoreCase))
	{
		return NewObject<UInputModifierSmooth>(Outer);
	}

	UE_LOG(LogAssetFactory, Warning, TEXT("Unknown modifier type: %s"), *ModifierName);
	return nullptr;
}
