// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/AnimSequenceGenerator.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FrameRate.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Utils/PropertySetterUtils.h"

namespace
{
bool IsPositiveIntegerNumber(double Value)
{
	return Value >= 1.0 && FMath::IsNearlyEqual(Value, FMath::RoundToDouble(Value));
}
}

FString FAnimSequenceGenerator::BuildLongPackageName(const FString& Path, const FString& Name) const
{
	FString FullPath = Path / Name;
	if (!FullPath.StartsWith(TEXT("/")))
	{
		FullPath = TEXT("/") + FullPath;
	}
	return FullPath;
}

USkeleton* FAnimSequenceGenerator::LoadSkeleton(const FString& SkeletonPath) const
{
	return SkeletonPath.IsEmpty() ? nullptr : LoadObject<USkeleton>(nullptr, *SkeletonPath);
}

USkeletalMesh* FAnimSequenceGenerator::LoadPreviewMesh(const FString& PreviewMeshPath) const
{
	return PreviewMeshPath.IsEmpty() ? nullptr : LoadObject<USkeletalMesh>(nullptr, *PreviewMeshPath);
}

bool FAnimSequenceGenerator::IsPreviewMeshCompatible(USkeletalMesh* PreviewMesh, USkeleton* Skeleton) const
{
	return !PreviewMesh || !Skeleton || Skeleton->IsCompatibleMesh(PreviewMesh);
}

FFrameRate FAnimSequenceGenerator::ParseFrameRate(TSharedPtr<FJsonObject> Config) const
{
	TSharedPtr<FJsonObject> FrameRateObject = GetObjectField(Config, TEXT("FrameRate"));
	if (!FrameRateObject.IsValid())
	{
		return FFrameRate(30, 1);
	}

	double Numerator = 30.0;
	double Denominator = 1.0;
	FrameRateObject->TryGetNumberField(TEXT("Numerator"), Numerator);
	FrameRateObject->TryGetNumberField(TEXT("Denominator"), Denominator);
	return FFrameRate(FMath::Max(1, static_cast<int32>(Numerator)), FMath::Max(1, static_cast<int32>(Denominator)));
}

int32 FAnimSequenceGenerator::ParseNumberOfFrames(TSharedPtr<FJsonObject> Config) const
{
	if (!Config.IsValid())
	{
		return 1;
	}

	double NumberOfFrames = 1.0;
	Config->TryGetNumberField(TEXT("NumberOfFrames"), NumberOfFrames);
	return FMath::Max(1, static_cast<int32>(NumberOfFrames));
}

FGenerationResult FAnimSequenceGenerator::Generate(
	const FString& Name,
	const FString& Path,
	EGenerationAction Action,
	TSharedPtr<FJsonObject> Config)
{
	if (!Config.IsValid())
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Invalid configuration object"));
	}

	const bool bExists = DoesAssetExist(Path, Name);
	if (Action == EGenerationAction::Create && bExists)
	{
		return FGenerationResult::MakeSkipped(GetAssetType(), Name, Path, TEXT("Asset already exists"));
	}
	if (Action == EGenerationAction::Update && !bExists)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Asset does not exist for update"));
	}

	FString Error;
	UAnimSequence* AnimSequence = nullptr;
	if (bExists)
	{
		UObject* ExistingAsset = LoadExistingAsset(Path, Name);
		AnimSequence = Cast<UAnimSequence>(ExistingAsset);
		if (!AnimSequence)
		{
			return FGenerationResult::MakeFailed(
				GetAssetType(),
				Name,
				Path,
				ExistingAsset
					? FString::Printf(TEXT("Existing asset is not a UAnimSequence: %s"), *BuildLongPackageName(Path, Name))
					: FString::Printf(TEXT("Failed to load existing AnimSequence: %s"), *BuildLongPackageName(Path, Name)));
		}
	}
	else
	{
		AnimSequence = CreateAnimSequence(Name, Path, Config, Error);
	}

	if (!AnimSequence)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Error.IsEmpty() ? TEXT("Failed to create or load AnimSequence") : Error);
	}

	if (!ApplyPatch(AnimSequence, Config, Error))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Error);
	}

	if (!SaveAnimSequence(AnimSequence, Error))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Error);
	}

	return bExists
		? FGenerationResult::MakeUpdated(GetAssetType(), Name, Path, AnimSequence)
		: FGenerationResult::MakeSuccess(GetAssetType(), Name, Path, AnimSequence);
}

TOptional<FString> FAnimSequenceGenerator::ValidateConfig(TSharedPtr<FJsonObject> Config, EGenerationAction Action) const
{
	if (!Config.IsValid())
	{
		return FString(TEXT("Invalid configuration object"));
	}

	static const TArray<FString> UnsupportedRawImportFields = {
		TEXT("RawTracks"),
		TEXT("BoneTracks"),
		TEXT("CompressedData"),
		TEXT("ImportFile"),
		TEXT("SourceFile")
	};

	for (const FString& Field : UnsupportedRawImportFields)
	{
		if (Config->HasField(Field))
		{
			return FString::Printf(TEXT("AnimSequence generator does not support raw animation import field '%s'"), *Field);
		}
	}

	FString Name;
	FString Path;
	const bool bHasAssetIdentity =
		Config->TryGetStringField(TEXT("Name"), Name) &&
		Config->TryGetStringField(TEXT("Path"), Path);

	UAnimSequence* ExistingAnimSequence = nullptr;
	if ((Action == EGenerationAction::Update || Action == EGenerationAction::CreateOrUpdate) && bHasAssetIdentity)
	{
		const bool bExistingAsset = DoesAssetExist(Path, Name);
		if (Action == EGenerationAction::Update && !bExistingAsset)
		{
			return FString::Printf(TEXT("Asset does not exist for AnimSequence update: %s"), *BuildLongPackageName(Path, Name));
		}

		if (bExistingAsset)
		{
			UObject* ExistingAsset = LoadExistingAsset(Path, Name);
			ExistingAnimSequence = Cast<UAnimSequence>(ExistingAsset);
			if (!ExistingAnimSequence)
			{
				return ExistingAsset
					? FString::Printf(TEXT("Existing asset is not a UAnimSequence: %s"), *BuildLongPackageName(Path, Name))
					: FString::Printf(TEXT("Failed to load existing AnimSequence: %s"), *BuildLongPackageName(Path, Name));
			}
		}
	}

	USkeleton* EffectiveSkeleton = ExistingAnimSequence ? ExistingAnimSequence->GetSkeleton() : nullptr;
	FString SkeletonPath;
	if (Config->HasField(TEXT("Skeleton")))
	{
		if (!Config->HasTypedField<EJson::String>(TEXT("Skeleton")))
		{
			return FString(TEXT("'Skeleton' must be a string asset path"));
		}

		Config->TryGetStringField(TEXT("Skeleton"), SkeletonPath);
		USkeleton* RequestedSkeleton = LoadSkeleton(SkeletonPath);
		if (!RequestedSkeleton)
		{
			return FString::Printf(TEXT("Skeleton not found or not a USkeleton: %s"), *SkeletonPath);
		}

		if (ExistingAnimSequence && RequestedSkeleton != EffectiveSkeleton)
		{
			return FString::Printf(
				TEXT("Skeleton does not match existing AnimSequence: requested %s, existing %s"),
				*SkeletonPath,
				EffectiveSkeleton ? *EffectiveSkeleton->GetPathName() : TEXT("<none>"));
		}

		EffectiveSkeleton = RequestedSkeleton;
	}
	else if (!ExistingAnimSequence && Action != EGenerationAction::Update)
	{
		return FString(TEXT("Missing required field 'Skeleton' for AnimSequence create"));
	}

	if (Config->HasField(TEXT("PreviewMesh")))
	{
		if (!Config->HasTypedField<EJson::String>(TEXT("PreviewMesh")))
		{
			return FString(TEXT("'PreviewMesh' must be a string asset path"));
		}

		FString PreviewMeshPath;
		Config->TryGetStringField(TEXT("PreviewMesh"), PreviewMeshPath);
		USkeletalMesh* PreviewMesh = LoadPreviewMesh(PreviewMeshPath);
		if (!PreviewMesh)
		{
			return FString::Printf(TEXT("PreviewMesh not found or not a USkeletalMesh: %s"), *PreviewMeshPath);
		}
		if (!EffectiveSkeleton)
		{
			return FString(TEXT("Cannot validate PreviewMesh because the AnimSequence has no Skeleton"));
		}
		if (!IsPreviewMeshCompatible(PreviewMesh, EffectiveSkeleton))
		{
			return FString::Printf(TEXT("PreviewMesh is not compatible with Skeleton: %s"), *PreviewMeshPath);
		}
	}

	if (Config->HasField(TEXT("FrameRate")))
	{
		if (!Config->HasTypedField<EJson::Object>(TEXT("FrameRate")))
		{
			return FString(TEXT("'FrameRate' must be an object with positive Numerator and Denominator"));
		}

		const TSharedPtr<FJsonObject> FrameRateObject = Config->GetObjectField(TEXT("FrameRate"));
		double Numerator = 0.0;
		double Denominator = 0.0;
		if (!FrameRateObject->TryGetNumberField(TEXT("Numerator"), Numerator) || !IsPositiveIntegerNumber(Numerator))
		{
			return FString(TEXT("'FrameRate.Numerator' must be a positive integer"));
		}
		if (!FrameRateObject->TryGetNumberField(TEXT("Denominator"), Denominator) || !IsPositiveIntegerNumber(Denominator))
		{
			return FString(TEXT("'FrameRate.Denominator' must be a positive integer"));
		}
	}

	if (Config->HasField(TEXT("NumberOfFrames")))
	{
		double NumberOfFrames = 0.0;
		if (!Config->TryGetNumberField(TEXT("NumberOfFrames"), NumberOfFrames) || !IsPositiveIntegerNumber(NumberOfFrames))
		{
			return FString(TEXT("'NumberOfFrames' must be a positive integer"));
		}
	}

	if (Config->HasField(TEXT("RateScale")))
	{
		double RateScale = 0.0;
		if (!Config->TryGetNumberField(TEXT("RateScale"), RateScale))
		{
			return FString(TEXT("'RateScale' must be a number"));
		}
	}

	return TOptional<FString>();
}

TArray<FString> FAnimSequenceGenerator::GetRequiredFields() const
{
	return { TEXT("Skeleton") };
}

bool FAnimSequenceGenerator::CanExtract(UObject* Asset) const
{
	return Asset && Asset->IsA<UAnimSequence>();
}

TSharedPtr<FJsonObject> FAnimSequenceGenerator::Extract(UObject* Asset, bool bDiffOnly) const
{
	(void)bDiffOnly;

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(Asset);
	if (!AnimSequence)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();
	Config->SetStringField(TEXT("Skeleton"), AnimSequence->GetSkeleton() ? AnimSequence->GetSkeleton()->GetPathName() : TEXT(""));
	if (USkeletalMesh* PreviewMesh = AnimSequence->GetPreviewMesh(false))
	{
		Config->SetStringField(TEXT("PreviewMesh"), PreviewMesh->GetPathName());
	}
	Config->SetNumberField(TEXT("NumberOfFrames"), AnimSequence->GetNumberOfSampledKeys());
	return Config;
}

UAnimSequence* FAnimSequenceGenerator::CreateAnimSequence(
	const FString& Name,
	const FString& Path,
	TSharedPtr<FJsonObject> Config,
	FString& OutError) const
{
	const FString SkeletonPath = GetStringField(Config, TEXT("Skeleton"));
	USkeleton* Skeleton = LoadSkeleton(SkeletonPath);
	if (!Skeleton)
	{
		OutError = FString::Printf(TEXT("Skeleton not found or not a USkeleton: %s"), *SkeletonPath);
		return nullptr;
	}

	USkeletalMesh* PreviewMesh = nullptr;
	const FString PreviewMeshPath = GetStringField(Config, TEXT("PreviewMesh"));
	if (!PreviewMeshPath.IsEmpty())
	{
		PreviewMesh = LoadPreviewMesh(PreviewMeshPath);
		if (!PreviewMesh)
		{
			OutError = FString::Printf(TEXT("PreviewMesh not found or not a USkeletalMesh: %s"), *PreviewMeshPath);
			return nullptr;
		}
		if (!IsPreviewMeshCompatible(PreviewMesh, Skeleton))
		{
			OutError = FString::Printf(TEXT("PreviewMesh is not compatible with Skeleton: %s"), *PreviewMeshPath);
			return nullptr;
		}
	}

	UPackage* Package = CreatePackage(*BuildLongPackageName(Path, Name));
	if (!Package)
	{
		OutError = TEXT("Failed to create AnimSequence package");
		return nullptr;
	}

	UAnimSequence* AnimSequence = NewObject<UAnimSequence>(Package, UAnimSequence::StaticClass(), *Name, RF_Public | RF_Standalone);
	if (!AnimSequence)
	{
		OutError = TEXT("Failed to create AnimSequence object");
		return nullptr;
	}

	AnimSequence->SetSkeleton(Skeleton);

	IAnimationDataController& Controller = AnimSequence->GetController();
	Controller.InitializeModel();
	Controller.SetFrameRate(ParseFrameRate(Config), false);
	Controller.SetNumberOfFrames(FFrameNumber(ParseNumberOfFrames(Config)), false);
	Controller.NotifyPopulated();

	if (PreviewMesh)
	{
		AnimSequence->SetPreviewMesh(PreviewMesh, false);
	}

	FAssetRegistryModule::AssetCreated(AnimSequence);
	AnimSequence->MarkPackageDirty();

	return AnimSequence;
}

bool FAnimSequenceGenerator::ApplyPatch(UAnimSequence* AnimSequence, TSharedPtr<FJsonObject> Config, FString& OutError) const
{
	if (!AnimSequence)
	{
		OutError = TEXT("Invalid AnimSequence");
		return false;
	}
	if (!Config.IsValid())
	{
		OutError = TEXT("Invalid configuration object");
		return false;
	}

	if (Config->HasField(TEXT("Skeleton")))
	{
		FString SkeletonPath;
		if (!Config->TryGetStringField(TEXT("Skeleton"), SkeletonPath))
		{
			OutError = TEXT("'Skeleton' must be a string asset path");
			return false;
		}

		USkeleton* RequestedSkeleton = LoadSkeleton(SkeletonPath);
		if (!RequestedSkeleton)
		{
			OutError = FString::Printf(TEXT("Skeleton not found or not a USkeleton: %s"), *SkeletonPath);
			return false;
		}

		if (RequestedSkeleton != AnimSequence->GetSkeleton())
		{
			OutError = FString::Printf(
				TEXT("Skeleton does not match existing AnimSequence: requested %s, existing %s"),
				*SkeletonPath,
				AnimSequence->GetSkeleton() ? *AnimSequence->GetSkeleton()->GetPathName() : TEXT("<none>"));
			return false;
		}
	}

	if (Config->HasField(TEXT("PreviewMesh")))
	{
		FString PreviewMeshPath;
		if (!Config->TryGetStringField(TEXT("PreviewMesh"), PreviewMeshPath))
		{
			OutError = TEXT("'PreviewMesh' must be a string asset path");
			return false;
		}

		USkeletalMesh* PreviewMesh = LoadPreviewMesh(PreviewMeshPath);
		if (!PreviewMesh)
		{
			OutError = FString::Printf(TEXT("PreviewMesh not found or not a USkeletalMesh: %s"), *PreviewMeshPath);
			return false;
		}
		if (!AnimSequence->GetSkeleton())
		{
			OutError = TEXT("Cannot validate PreviewMesh because the AnimSequence has no Skeleton");
			return false;
		}
		if (!IsPreviewMeshCompatible(PreviewMesh, AnimSequence->GetSkeleton()))
		{
			OutError = FString::Printf(TEXT("PreviewMesh is not compatible with Skeleton: %s"), *PreviewMeshPath);
			return false;
		}

		AnimSequence->SetPreviewMesh(PreviewMesh, false);
	}

	IAnimationDataController& Controller = AnimSequence->GetController();
	if (Config->HasField(TEXT("FrameRate")))
	{
		const FFrameRate RequestedFrameRate = ParseFrameRate(Config);
		Controller.SetFrameRate(RequestedFrameRate, false);

		const IAnimationDataModel* DataModel = AnimSequence->GetDataModel();
		if (!DataModel || DataModel->GetFrameRate() != RequestedFrameRate)
		{
			const FFrameRate ActualFrameRate = DataModel ? DataModel->GetFrameRate() : FFrameRate();
			OutError = FString::Printf(
				TEXT("Failed to set AnimSequence FrameRate to %d/%d; actual FrameRate is %d/%d"),
				RequestedFrameRate.Numerator,
				RequestedFrameRate.Denominator,
				ActualFrameRate.Numerator,
				ActualFrameRate.Denominator);
			return false;
		}
	}
	if (Config->HasField(TEXT("NumberOfFrames")))
	{
		Controller.SetNumberOfFrames(FFrameNumber(ParseNumberOfFrames(Config)), false);
	}
	if (Config->HasField(TEXT("RateScale")))
	{
		FProperty* RateScaleProperty = AnimSequence->GetClass()->FindPropertyByName(TEXT("RateScale"));
		if (!RateScaleProperty)
		{
			OutError = TEXT("AnimSequence RateScale property was not found");
			return false;
		}

		const TSharedPtr<FJsonValue>* RateScaleValue = Config->Values.Find(TEXT("RateScale"));
		if (!RateScaleValue || !FPropertySetterUtils::SetPropertyFromJson(AnimSequence, RateScaleProperty, *RateScaleValue))
		{
			OutError = TEXT("Failed to set AnimSequence RateScale");
			return false;
		}
	}

	AnimSequence->MarkPackageDirty();

	return true;
}

bool FAnimSequenceGenerator::SaveAnimSequence(UAnimSequence* AnimSequence, FString& OutError) const
{
	if (!AnimSequence)
	{
		OutError = TEXT("Invalid AnimSequence");
		return false;
	}

	UPackage* Package = AnimSequence->GetOutermost();
	if (!Package)
	{
		OutError = TEXT("AnimSequence package is invalid");
		return false;
	}

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, AnimSequence, *PackageFileName, SaveArgs))
	{
		OutError = FString::Printf(TEXT("Failed to save AnimSequence package: %s"), *Package->GetName());
		return false;
	}

	return true;
}
