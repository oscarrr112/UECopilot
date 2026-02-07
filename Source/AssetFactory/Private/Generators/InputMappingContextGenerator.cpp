// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/InputMappingContextGenerator.h"
#include "AssetFactoryModule.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputTriggers.h"
#include "InputModifiers.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

FGenerationResult FInputMappingContextGenerator::Generate(
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

	UInputMappingContext* IMC = nullptr;

	if (bExists)
	{
		IMC = Cast<UInputMappingContext>(LoadExistingAsset(Path, Name));
		if (!IMC)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to load existing IMC"));
		}
		// Clear existing mappings for update
		IMC->UnmapAll();
	}
	else
	{
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

		IMC = NewObject<UInputMappingContext>(Package, UInputMappingContext::StaticClass(), *Name, RF_Public | RF_Standalone);
		if (!IMC)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create IMC"));
		}

		FAssetRegistryModule::AssetCreated(IMC);
	}

	// Add mappings
	const TArray<TSharedPtr<FJsonValue>>* MappingsArray = GetArrayField(Config, TEXT("Mappings"));
	if (MappingsArray)
	{
		for (const TSharedPtr<FJsonValue>& MappingValue : *MappingsArray)
		{
			const TSharedPtr<FJsonObject>* MappingObj;
			if (!MappingValue->TryGetObject(MappingObj))
			{
				continue;
			}

			// Get action path
			FString ActionPath;
			if (!(*MappingObj)->TryGetStringField(TEXT("Action"), ActionPath))
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("Mapping missing 'Action' field"));
				continue;
			}

			// Load input action
			UInputAction* InputAction = LoadObject<UInputAction>(nullptr, *ActionPath);
			if (!InputAction)
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("Failed to load InputAction: %s"), *ActionPath);
				continue;
			}

			// Get key
			FString KeyName;
			if (!(*MappingObj)->TryGetStringField(TEXT("Key"), KeyName))
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("Mapping missing 'Key' field"));
				continue;
			}

			FKey Key = ParseKey(KeyName);
			if (!Key.IsValid())
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("Invalid key: %s"), *KeyName);
				continue;
			}

			// Map the key
			FEnhancedActionKeyMapping& Mapping = IMC->MapKey(InputAction, Key);

			// Add per-mapping triggers
			const TArray<TSharedPtr<FJsonValue>>* TriggersArray;
			if ((*MappingObj)->TryGetArrayField(TEXT("Triggers"), TriggersArray))
			{
				for (const TSharedPtr<FJsonValue>& TriggerValue : *TriggersArray)
				{
					FString TriggerName;
					if (TriggerValue->TryGetString(TriggerName))
					{
						if (UInputTrigger* Trigger = CreateTrigger(IMC, TriggerName))
						{
							Mapping.Triggers.Add(Trigger);
						}
					}
				}
			}

			// Add per-mapping modifiers
			const TArray<TSharedPtr<FJsonValue>>* ModifiersArray;
			if ((*MappingObj)->TryGetArrayField(TEXT("Modifiers"), ModifiersArray))
			{
				for (const TSharedPtr<FJsonValue>& ModifierValue : *ModifiersArray)
				{
					FString ModifierName;
					if (ModifierValue->TryGetString(ModifierName))
					{
						if (UInputModifier* Modifier = CreateModifier(IMC, ModifierName))
						{
							Mapping.Modifiers.Add(Modifier);
						}
					}
				}
			}
		}
	}

	// Save
	IMC->MarkPackageDirty();

	UPackage* Package = IMC->GetOutermost();
	FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, IMC, *PackageFileName, SaveArgs);

	if (bExists)
	{
		return FGenerationResult::MakeUpdated(GetAssetType(), Name, Path, IMC);
	}
	return FGenerationResult::MakeSuccess(GetAssetType(), Name, Path, IMC);
}

TOptional<FString> FInputMappingContextGenerator::ValidateConfig(TSharedPtr<FJsonObject> Config) const
{
	if (!Config.IsValid())
	{
		return FString(TEXT("Invalid configuration object"));
	}

	const TArray<TSharedPtr<FJsonValue>>* MappingsArray = nullptr;
	if (Config->TryGetArrayField(TEXT("Mappings"), MappingsArray))
	{
		for (int32 i = 0; i < MappingsArray->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* MappingObj;
			if (!(*MappingsArray)[i]->TryGetObject(MappingObj))
			{
				return FString::Printf(TEXT("Mappings[%d]: not a valid JSON object"), i);
			}

			FString ActionPath;
			if (!(*MappingObj)->TryGetStringField(TEXT("Action"), ActionPath) || ActionPath.IsEmpty())
			{
				return FString::Printf(TEXT("Mappings[%d]: missing 'Action' field"), i);
			}

			FString KeyName;
			if (!(*MappingObj)->TryGetStringField(TEXT("Key"), KeyName) || KeyName.IsEmpty())
			{
				return FString::Printf(TEXT("Mappings[%d]: missing 'Key' field"), i);
			}
		}
	}

	return TOptional<FString>();
}

FKey FInputMappingContextGenerator::ParseKey(const FString& KeyName) const
{
	// Common key mappings
	static TMap<FString, FKey> KeyMap = {
		// Letters
		{TEXT("A"), EKeys::A}, {TEXT("B"), EKeys::B}, {TEXT("C"), EKeys::C}, {TEXT("D"), EKeys::D},
		{TEXT("E"), EKeys::E}, {TEXT("F"), EKeys::F}, {TEXT("G"), EKeys::G}, {TEXT("H"), EKeys::H},
		{TEXT("I"), EKeys::I}, {TEXT("J"), EKeys::J}, {TEXT("K"), EKeys::K}, {TEXT("L"), EKeys::L},
		{TEXT("M"), EKeys::M}, {TEXT("N"), EKeys::N}, {TEXT("O"), EKeys::O}, {TEXT("P"), EKeys::P},
		{TEXT("Q"), EKeys::Q}, {TEXT("R"), EKeys::R}, {TEXT("S"), EKeys::S}, {TEXT("T"), EKeys::T},
		{TEXT("U"), EKeys::U}, {TEXT("V"), EKeys::V}, {TEXT("W"), EKeys::W}, {TEXT("X"), EKeys::X},
		{TEXT("Y"), EKeys::Y}, {TEXT("Z"), EKeys::Z},
		// Numbers
		{TEXT("0"), EKeys::Zero}, {TEXT("1"), EKeys::One}, {TEXT("2"), EKeys::Two},
		{TEXT("3"), EKeys::Three}, {TEXT("4"), EKeys::Four}, {TEXT("5"), EKeys::Five},
		{TEXT("6"), EKeys::Six}, {TEXT("7"), EKeys::Seven}, {TEXT("8"), EKeys::Eight},
		{TEXT("9"), EKeys::Nine},
		// Function keys
		{TEXT("F1"), EKeys::F1}, {TEXT("F2"), EKeys::F2}, {TEXT("F3"), EKeys::F3},
		{TEXT("F4"), EKeys::F4}, {TEXT("F5"), EKeys::F5}, {TEXT("F6"), EKeys::F6},
		{TEXT("F7"), EKeys::F7}, {TEXT("F8"), EKeys::F8}, {TEXT("F9"), EKeys::F9},
		{TEXT("F10"), EKeys::F10}, {TEXT("F11"), EKeys::F11}, {TEXT("F12"), EKeys::F12},
		// Special keys
		{TEXT("Space"), EKeys::SpaceBar}, {TEXT("SpaceBar"), EKeys::SpaceBar},
		{TEXT("Enter"), EKeys::Enter}, {TEXT("Return"), EKeys::Enter},
		{TEXT("Escape"), EKeys::Escape}, {TEXT("Esc"), EKeys::Escape},
		{TEXT("Tab"), EKeys::Tab},
		{TEXT("Backspace"), EKeys::BackSpace}, {TEXT("BackSpace"), EKeys::BackSpace},
		{TEXT("CapsLock"), EKeys::CapsLock},
		// Arrow keys
		{TEXT("Up"), EKeys::Up}, {TEXT("Down"), EKeys::Down},
		{TEXT("Left"), EKeys::Left}, {TEXT("Right"), EKeys::Right},
		// Modifiers
		{TEXT("LeftShift"), EKeys::LeftShift}, {TEXT("RightShift"), EKeys::RightShift},
		{TEXT("LeftControl"), EKeys::LeftControl}, {TEXT("RightControl"), EKeys::RightControl},
		{TEXT("LeftAlt"), EKeys::LeftAlt}, {TEXT("RightAlt"), EKeys::RightAlt},
		{TEXT("Shift"), EKeys::LeftShift}, {TEXT("Ctrl"), EKeys::LeftControl}, {TEXT("Alt"), EKeys::LeftAlt},
		// Mouse
		{TEXT("LeftMouseButton"), EKeys::LeftMouseButton},
		{TEXT("RightMouseButton"), EKeys::RightMouseButton},
		{TEXT("MiddleMouseButton"), EKeys::MiddleMouseButton},
		{TEXT("ThumbMouseButton"), EKeys::ThumbMouseButton},
		{TEXT("ThumbMouseButton2"), EKeys::ThumbMouseButton2},
		{TEXT("MouseScrollUp"), EKeys::MouseScrollUp},
		{TEXT("MouseScrollDown"), EKeys::MouseScrollDown},
		// Gamepad
		{TEXT("Gamepad_LeftX"), EKeys::Gamepad_LeftX},
		{TEXT("Gamepad_LeftY"), EKeys::Gamepad_LeftY},
		{TEXT("Gamepad_RightX"), EKeys::Gamepad_RightX},
		{TEXT("Gamepad_RightY"), EKeys::Gamepad_RightY},
		{TEXT("Gamepad_FaceButton_Bottom"), EKeys::Gamepad_FaceButton_Bottom},
		{TEXT("Gamepad_FaceButton_Right"), EKeys::Gamepad_FaceButton_Right},
		{TEXT("Gamepad_FaceButton_Left"), EKeys::Gamepad_FaceButton_Left},
		{TEXT("Gamepad_FaceButton_Top"), EKeys::Gamepad_FaceButton_Top},
	};

	if (const FKey* Found = KeyMap.Find(KeyName))
	{
		return *Found;
	}

	// Try constructing directly from name
	FKey DirectKey(*KeyName);
	if (DirectKey.IsValid())
	{
		return DirectKey;
	}

	return EKeys::Invalid;
}

UInputTrigger* FInputMappingContextGenerator::CreateTrigger(UObject* Outer, const FString& TriggerName) const
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

UInputModifier* FInputMappingContextGenerator::CreateModifier(UObject* Outer, const FString& ModifierName) const
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

FString FInputMappingContextGenerator::TriggerToString(UInputTrigger* Trigger) const
{
	if (!Trigger) return TEXT("");

	if (Trigger->IsA<UInputTriggerDown>()) return TEXT("Down");
	if (Trigger->IsA<UInputTriggerPressed>()) return TEXT("Pressed");
	if (Trigger->IsA<UInputTriggerReleased>()) return TEXT("Released");
	if (Trigger->IsA<UInputTriggerHold>()) return TEXT("Hold");
	if (Trigger->IsA<UInputTriggerTap>()) return TEXT("Tap");
	if (Trigger->IsA<UInputTriggerPulse>()) return TEXT("Pulse");

	return Trigger->GetClass()->GetName();
}

FString FInputMappingContextGenerator::ModifierToString(UInputModifier* Modifier) const
{
	if (!Modifier) return TEXT("");

	if (Modifier->IsA<UInputModifierNegate>()) return TEXT("Negate");
	if (Modifier->IsA<UInputModifierSwizzleAxis>()) return TEXT("Swizzle");
	if (Modifier->IsA<UInputModifierScalar>()) return TEXT("Scalar");
	if (Modifier->IsA<UInputModifierDeadZone>()) return TEXT("DeadZone");
	if (Modifier->IsA<UInputModifierSmooth>()) return TEXT("Smooth");

	return Modifier->GetClass()->GetName();
}

//~ Extract Implementation

bool FInputMappingContextGenerator::CanExtract(UObject* Asset) const
{
	return Asset && Asset->IsA<UInputMappingContext>();
}

TSharedPtr<FJsonObject> FInputMappingContextGenerator::Extract(UObject* Asset, bool bDiffOnly) const
{
	UInputMappingContext* IMC = Cast<UInputMappingContext>(Asset);
	if (!IMC)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();

	// Extract mappings
	const TArray<FEnhancedActionKeyMapping>& Mappings = IMC->GetMappings();
	if (Mappings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> MappingsArray;
		for (const FEnhancedActionKeyMapping& Mapping : Mappings)
		{
			TSharedPtr<FJsonObject> MappingObj = MakeShared<FJsonObject>();

			// Action path
			if (Mapping.Action)
			{
				MappingObj->SetStringField(TEXT("Action"), Mapping.Action->GetPathName());
			}

			// Key
			MappingObj->SetStringField(TEXT("Key"), Mapping.Key.GetFName().ToString());

			// Triggers
			if (Mapping.Triggers.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> TriggersArray;
				for (UInputTrigger* Trigger : Mapping.Triggers)
				{
					FString TriggerName = TriggerToString(Trigger);
					if (!TriggerName.IsEmpty())
					{
						TriggersArray.Add(MakeShared<FJsonValueString>(TriggerName));
					}
				}
				if (TriggersArray.Num() > 0)
				{
					MappingObj->SetArrayField(TEXT("Triggers"), TriggersArray);
				}
			}

			// Modifiers
			if (Mapping.Modifiers.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> ModifiersArray;
				for (UInputModifier* Modifier : Mapping.Modifiers)
				{
					FString ModifierName = ModifierToString(Modifier);
					if (!ModifierName.IsEmpty())
					{
						ModifiersArray.Add(MakeShared<FJsonValueString>(ModifierName));
					}
				}
				if (ModifiersArray.Num() > 0)
				{
					MappingObj->SetArrayField(TEXT("Modifiers"), ModifiersArray);
				}
			}

			MappingsArray.Add(MakeShared<FJsonValueObject>(MappingObj));
		}

		Config->SetArrayField(TEXT("Mappings"), MappingsArray);
	}

	return Config;
}
