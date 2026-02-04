// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/CurveVectorGenerator.h"
#include "AssetFactoryModule.h"
#include "Curves/CurveVector.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/CurveFactory.h"
#include "UObject/SavePackage.h"

FGenerationResult FCurveVectorGenerator::Generate(
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

	UCurveVector* Curve = nullptr;

	if (bExists)
	{
		Curve = Cast<UCurveVector>(LoadExistingAsset(Path, Name));
		if (!Curve)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to load existing curve"));
		}
		// Clear existing keys
		Curve->FloatCurves[0].Reset();
		Curve->FloatCurves[1].Reset();
		Curve->FloatCurves[2].Reset();
	}
	else
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		UCurveVectorFactory* Factory = NewObject<UCurveVectorFactory>();
		Curve = Cast<UCurveVector>(AssetTools.CreateAsset(Name, Path, UCurveVector::StaticClass(), Factory));

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
			(*KeyObj)->TryGetNumberField(TEXT("Time"), Time);

			FVector Value = ParseVector((*KeyObj)->TryGetField(TEXT("Value")));

			// Add keys to each channel
			FKeyHandle KeyHandleX = Curve->FloatCurves[0].AddKey(Time, Value.X);
			FKeyHandle KeyHandleY = Curve->FloatCurves[1].AddKey(Time, Value.Y);
			FKeyHandle KeyHandleZ = Curve->FloatCurves[2].AddKey(Time, Value.Z);

			// Set interpolation mode
			FString InterpModeStr;
			if ((*KeyObj)->TryGetStringField(TEXT("InterpMode"), InterpModeStr))
			{
				ERichCurveInterpMode InterpMode = ParseInterpMode(InterpModeStr);
				Curve->FloatCurves[0].SetKeyInterpMode(KeyHandleX, InterpMode);
				Curve->FloatCurves[1].SetKeyInterpMode(KeyHandleY, InterpMode);
				Curve->FloatCurves[2].SetKeyInterpMode(KeyHandleZ, InterpMode);
			}
		}
	}

	// Save
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

ERichCurveInterpMode FCurveVectorGenerator::ParseInterpMode(const FString& ModeString) const
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

FVector FCurveVectorGenerator::ParseVector(const TSharedPtr<FJsonValue>& JsonValue) const
{
	FVector Result = FVector::ZeroVector;

	if (!JsonValue.IsValid())
	{
		return Result;
	}

	const TArray<TSharedPtr<FJsonValue>>* ArrayValue;
	if (JsonValue->TryGetArray(ArrayValue) && ArrayValue->Num() >= 3)
	{
		(*ArrayValue)[0]->TryGetNumber(Result.X);
		(*ArrayValue)[1]->TryGetNumber(Result.Y);
		(*ArrayValue)[2]->TryGetNumber(Result.Z);
	}

	return Result;
}
