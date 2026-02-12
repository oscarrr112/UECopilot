// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/CurveFloatGenerator.h"
#include "AssetFactoryModule.h"
#include "Curves/CurveFloat.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/CurveFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

FGenerationResult FCurveFloatGenerator::Generate(
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

	UCurveFloat* Curve = nullptr;

	if (bExists)
	{
		Curve = Cast<UCurveFloat>(LoadExistingAsset(Path, Name));
		if (!Curve)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to load existing curve"));
		}
		// Only clear existing keys if JSON provides new Keys
		if (Config->HasField(TEXT("Keys")))
		{
			Curve->FloatCurve.Reset();
		}
	}
	else
	{
		// Create new curve
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		UCurveFloatFactory* Factory = NewObject<UCurveFloatFactory>();
		Curve = Cast<UCurveFloat>(AssetTools.CreateAsset(Name, Path, UCurveFloat::StaticClass(), Factory));

		if (!Curve)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create curve"));
		}
	}

	// Add keys
	const TArray<TSharedPtr<FJsonValue>>* KeysArray = GetArrayField(Config, TEXT("Keys"));
	if (KeysArray)
	{
		for (const TSharedPtr<FJsonValue>& KeyValue : *KeysArray)
		{
			const TSharedPtr<FJsonObject>* KeyObj;
			if (!KeyValue->TryGetObject(KeyObj))
			{
				continue;
			}

			double Time = 0.0;
			double Value = 0.0;
			(*KeyObj)->TryGetNumberField(TEXT("Time"), Time);
			(*KeyObj)->TryGetNumberField(TEXT("Value"), Value);

			FKeyHandle KeyHandle = Curve->FloatCurve.AddKey(Time, Value);

			// Set interpolation mode
			FString InterpModeStr;
			if ((*KeyObj)->TryGetStringField(TEXT("InterpMode"), InterpModeStr))
			{
				Curve->FloatCurve.SetKeyInterpMode(KeyHandle, ParseInterpMode(InterpModeStr));
			}
		}
	}

	// Mark dirty and save
	Curve->MarkPackageDirty();

	UPackage* Package = Curve->GetOutermost();
	FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, Curve, *PackageFileName, SaveArgs);

	if (bExists)
	{
		return FGenerationResult::MakeUpdated(GetAssetType(), Name, Path, Curve);
	}
	return FGenerationResult::MakeSuccess(GetAssetType(), Name, Path, Curve);
}

ERichCurveInterpMode FCurveFloatGenerator::ParseInterpMode(const FString& ModeString) const
{
	if (ModeString.Equals(TEXT("Constant"), ESearchCase::IgnoreCase))
	{
		return ERichCurveInterpMode::RCIM_Constant;
	}
	else if (ModeString.Equals(TEXT("Cubic"), ESearchCase::IgnoreCase))
	{
		return ERichCurveInterpMode::RCIM_Cubic;
	}
	return ERichCurveInterpMode::RCIM_Linear;
}

FString FCurveFloatGenerator::InterpModeToString(ERichCurveInterpMode Mode) const
{
	switch (Mode)
	{
	case ERichCurveInterpMode::RCIM_Constant: return TEXT("Constant");
	case ERichCurveInterpMode::RCIM_Cubic: return TEXT("Cubic");
	case ERichCurveInterpMode::RCIM_Linear:
	default: return TEXT("Linear");
	}
}

//~ Extract Implementation

bool FCurveFloatGenerator::CanExtract(UObject* Asset) const
{
	return Asset && Asset->IsA<UCurveFloat>();
}

TSharedPtr<FJsonObject> FCurveFloatGenerator::Extract(UObject* Asset, bool bDiffOnly) const
{
	UCurveFloat* Curve = Cast<UCurveFloat>(Asset);
	if (!Curve)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();

	// Extract keys
	TArray<TSharedPtr<FJsonValue>> KeysArray;
	for (auto It = Curve->FloatCurve.GetKeyIterator(); It; ++It)
	{
		const FRichCurveKey& Key = *It;

		TSharedPtr<FJsonObject> KeyObj = MakeShared<FJsonObject>();
		KeyObj->SetNumberField(TEXT("Time"), Key.Time);
		KeyObj->SetNumberField(TEXT("Value"), Key.Value);
		KeyObj->SetStringField(TEXT("InterpMode"), InterpModeToString(Key.InterpMode));

		KeysArray.Add(MakeShared<FJsonValueObject>(KeyObj));
	}

	if (KeysArray.Num() > 0)
	{
		Config->SetArrayField(TEXT("Keys"), KeysArray);
	}

	return Config;
}
