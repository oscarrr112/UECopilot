// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/AnimSequenceGenerator.h"

#include "AssetFactoryNamedAnimNotifyState.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimTypes.h"
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

bool ValidateFrameRateField(const TSharedPtr<FJsonObject>& Config, FString& OutError)
{
	if (!Config.IsValid() || !Config->HasField(TEXT("FrameRate")))
	{
		return true;
	}

	if (!Config->HasTypedField<EJson::Object>(TEXT("FrameRate")))
	{
		OutError = TEXT("'FrameRate' must be an object with positive Numerator and Denominator");
		return false;
	}

	const TSharedPtr<FJsonObject> FrameRateObject = Config->GetObjectField(TEXT("FrameRate"));
	double Numerator = 0.0;
	double Denominator = 0.0;
	if (!FrameRateObject->TryGetNumberField(TEXT("Numerator"), Numerator) || !IsPositiveIntegerNumber(Numerator))
	{
		OutError = TEXT("'FrameRate.Numerator' must be a positive integer");
		return false;
	}
	if (!FrameRateObject->TryGetNumberField(TEXT("Denominator"), Denominator) || !IsPositiveIntegerNumber(Denominator))
	{
		OutError = TEXT("'FrameRate.Denominator' must be a positive integer");
		return false;
	}

	return true;
}

bool ValidateNumberOfFramesField(const TSharedPtr<FJsonObject>& Config, FString& OutError)
{
	if (!Config.IsValid() || !Config->HasField(TEXT("NumberOfFrames")))
	{
		return true;
	}

	double NumberOfFrames = 0.0;
	if (!Config->TryGetNumberField(TEXT("NumberOfFrames"), NumberOfFrames) || !IsPositiveIntegerNumber(NumberOfFrames))
	{
		OutError = TEXT("'NumberOfFrames' must be a positive integer");
		return false;
	}

	return true;
}

TOptional<float> GetValidatedPlayLength(const UAnimSequence* AnimSequence)
{
	if (!AnimSequence)
	{
		return TOptional<float>();
	}

	const float PlayLength = AnimSequence->GetPlayLength();
	return FMath::IsFinite(PlayLength) && PlayLength >= 0.0f
		? TOptional<float>(PlayLength)
		: TOptional<float>();
}

FFrameRate GetEffectiveFrameRate(const UAnimSequence* AnimSequence, const TSharedPtr<FJsonObject>& Config)
{
	FFrameRate FrameRate(30, 1);
	if (const IAnimationDataModel* DataModel = AnimSequence ? AnimSequence->GetDataModel() : nullptr)
	{
		FrameRate = DataModel->GetFrameRate();
	}

	if (Config.IsValid())
	{
		if (Config->HasTypedField<EJson::Object>(TEXT("FrameRate")))
		{
			const TSharedPtr<FJsonObject> FrameRateObject = Config->GetObjectField(TEXT("FrameRate"));
			double Numerator = FrameRate.Numerator;
			double Denominator = FrameRate.Denominator;
			FrameRateObject->TryGetNumberField(TEXT("Numerator"), Numerator);
			FrameRateObject->TryGetNumberField(TEXT("Denominator"), Denominator);
			FrameRate = FFrameRate(static_cast<int32>(Numerator), static_cast<int32>(Denominator));
		}
	}

	return FrameRate;
}

int32 GetEffectiveNumberOfFrames(const UAnimSequence* AnimSequence, const TSharedPtr<FJsonObject>& Config)
{
	int32 NumberOfFrames = 1;
	if (const IAnimationDataModel* DataModel = AnimSequence ? AnimSequence->GetDataModel() : nullptr)
	{
		NumberOfFrames = DataModel->GetNumberOfFrames();
	}

	if (Config.IsValid())
	{
		double NumberOfFramesValue = NumberOfFrames;
		if (Config->TryGetNumberField(TEXT("NumberOfFrames"), NumberOfFramesValue))
		{
			NumberOfFrames = static_cast<int32>(NumberOfFramesValue);
		}
	}

	return NumberOfFrames;
}

TOptional<float> GetExpectedPlayLength(const UAnimSequence* AnimSequence, const TSharedPtr<FJsonObject>& Config)
{
	const FFrameRate FrameRate = GetEffectiveFrameRate(AnimSequence, Config);
	const int32 NumberOfFrames = GetEffectiveNumberOfFrames(AnimSequence, Config);
	const double FrameRateValue = FrameRate.AsDecimal();
	if (NumberOfFrames < 0 || !FMath::IsFinite(FrameRateValue) || FrameRateValue <= 0.0)
	{
		return TOptional<float>();
	}

	return static_cast<float>(static_cast<double>(NumberOfFrames) / FrameRateValue);
}

bool TryGetEntryObject(
	const TSharedPtr<FJsonValue>& EntryValue,
	const TCHAR* FieldName,
	TSharedPtr<FJsonObject>& OutObject,
	FString& OutError)
{
	const TSharedPtr<FJsonObject>* EntryObject = nullptr;
	if (!EntryValue.IsValid() || !EntryValue->TryGetObject(EntryObject) || !EntryObject || !EntryObject->IsValid())
	{
		OutError = FString::Printf(TEXT("%s entries must be objects"), FieldName);
		return false;
	}

	OutObject = *EntryObject;
	return true;
}

bool TryGetValidName(
	const TSharedPtr<FJsonObject>& EntryObject,
	const TCHAR* FieldName,
	FString& OutName,
	FName& OutFName,
	FString& OutError)
{
	if (!EntryObject->TryGetStringField(TEXT("Name"), OutName) || OutName.IsEmpty())
	{
		OutError = FString::Printf(TEXT("%s entries require non-empty Name"), FieldName);
		return false;
	}

	OutFName = FName(*OutName);
	if (OutFName.IsNone())
	{
		OutError = FString::Printf(TEXT("%s entries require a valid non-None Name"), FieldName);
		return false;
	}

	return true;
}

bool TryGetFiniteFloatField(
	const TSharedPtr<FJsonObject>& EntryObject,
	const TCHAR* FieldName,
	const FString& Context,
	float& OutValue,
	FString& OutError)
{
	double RawValue = 0.0;
	if (!EntryObject->TryGetNumberField(FieldName, RawValue))
	{
		OutError = FString::Printf(TEXT("%s requires numeric %s"), *Context, FieldName);
		return false;
	}

	const float FloatValue = static_cast<float>(RawValue);
	if (!FMath::IsFinite(FloatValue))
	{
		OutError = FString::Printf(TEXT("%s %s must be finite"), *Context, FieldName);
		return false;
	}

	OutValue = FloatValue;
	return true;
}

bool RejectUnsupportedNotifyClassFields(
	const TSharedPtr<FJsonObject>& EntryObject,
	const TCHAR* FieldName,
	const FString& Context,
	FString& OutError)
{
	for (const TCHAR* UnsupportedField : { TEXT("NotifyClass"), TEXT("NotifyStateClass"), TEXT("Class") })
	{
		if (EntryObject->HasField(UnsupportedField))
		{
			OutError = FString::Printf(TEXT("%s does not support class-based %s field '%s'"), *Context, FieldName, UnsupportedField);
			return false;
		}
	}

	return true;
}

bool TryGetTrackIndex(
	const TSharedPtr<FJsonObject>& EntryObject,
	const FString& Context,
	int32& OutTrackIndex,
	FString& OutError)
{
	if (!EntryObject->HasField(TEXT("TrackIndex")))
	{
		OutTrackIndex = 0;
		return true;
	}

	double RawTrackIndex = 0.0;
	if (!EntryObject->TryGetNumberField(TEXT("TrackIndex"), RawTrackIndex))
	{
		OutError = FString::Printf(TEXT("%s requires numeric TrackIndex"), *Context);
		return false;
	}

	const double RoundedTrackIndex = FMath::RoundToDouble(RawTrackIndex);
	if (!FMath::IsFinite(static_cast<float>(RawTrackIndex)) ||
		RawTrackIndex < 0.0 ||
		RawTrackIndex > static_cast<double>(MAX_int32) ||
		RawTrackIndex != RoundedTrackIndex)
	{
		OutError = FString::Printf(TEXT("%s TrackIndex must be a non-negative integer"), *Context);
		return false;
	}

	OutTrackIndex = static_cast<int32>(RoundedTrackIndex);
	if (OutTrackIndex != 0)
	{
		OutError = FString::Printf(TEXT("%s TrackIndex %d is not supported; AnimSequence Part 1 only supports TrackIndex 0"), *Context, OutTrackIndex);
		return false;
	}

	return true;
}

bool ValidateTimeWithinPlayLength(
	float Time,
	TOptional<float> PlayLength,
	const FString& Context,
	FString& OutError)
{
	if (Time < 0.0f)
	{
		OutError = FString::Printf(TEXT("%s Time must be non-negative"), *Context);
		return false;
	}
	if (PlayLength.IsSet() && Time > PlayLength.GetValue() + KINDA_SMALL_NUMBER)
	{
		OutError = FString::Printf(TEXT("%s Time is outside sequence length"), *Context);
		return false;
	}

	return true;
}

bool ValidateTimeRangeWithinPlayLength(
	float Time,
	float Duration,
	TOptional<float> PlayLength,
	const FString& Context,
	FString& OutError)
{
	if (Duration <= 0.0f)
	{
		OutError = FString::Printf(TEXT("%s requires positive Duration"), *Context);
		return false;
	}
	if (!ValidateTimeWithinPlayLength(Time, PlayLength, Context, OutError))
	{
		return false;
	}

	const float EndTime = Time + Duration;
	if (!FMath::IsFinite(EndTime))
	{
		OutError = FString::Printf(TEXT("%s end time must be finite"), *Context);
		return false;
	}
	if (PlayLength.IsSet() && EndTime > PlayLength.GetValue() + KINDA_SMALL_NUMBER)
	{
		OutError = FString::Printf(TEXT("%s time range is outside sequence length"), *Context);
		return false;
	}

	return true;
}

bool IsSupportedNamedNotify(const FAnimNotifyEvent& NotifyEvent)
{
	return !NotifyEvent.Notify &&
		!NotifyEvent.NotifyStateClass &&
		!NotifyEvent.NotifyName.IsNone() &&
		NotifyEvent.Duration <= 0.0f;
}

bool IsSupportedNamedNotifyState(const FAnimNotifyEvent& NotifyEvent)
{
	return NotifyEvent.NotifyStateClass &&
		NotifyEvent.NotifyStateClass->IsA<UAssetFactoryNamedAnimNotifyState>() &&
		!NotifyEvent.NotifyName.IsNone();
}

bool IsLegacyNamedNotifyState(const FAnimNotifyEvent& NotifyEvent)
{
	return !NotifyEvent.Notify &&
		!NotifyEvent.NotifyStateClass &&
		!NotifyEvent.NotifyName.IsNone() &&
		NotifyEvent.Duration > 0.0f;
}

void RefreshNotifyData(UAnimSequence* AnimSequence)
{
	AnimSequence->SortNotifies();
	AnimSequence->InitializeNotifyTrack();
	AnimSequence->RefreshCacheData();
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

	FString TimingFieldError;
	if (!ValidateFrameRateField(Config, TimingFieldError))
	{
		return TimingFieldError;
	}
	if (!ValidateNumberOfFramesField(Config, TimingFieldError))
	{
		return TimingFieldError;
	}

	if (Config->HasField(TEXT("RateScale")))
	{
		double RateScale = 0.0;
		if (!Config->TryGetNumberField(TEXT("RateScale"), RateScale))
		{
			return FString(TEXT("'RateScale' must be a number"));
		}
	}

	if (Config->HasField(TEXT("FloatCurves")))
	{
		const TArray<TSharedPtr<FJsonValue>>* FloatCurves = nullptr;
		if (!Config->TryGetArrayField(TEXT("FloatCurves"), FloatCurves) || !FloatCurves)
		{
			return FString(TEXT("'FloatCurves' must be an array"));
		}

		FString Error;
		if (!ValidateFloatCurves(*FloatCurves, Error))
		{
			return Error;
		}
	}

	const TOptional<float> EffectivePlayLength = GetExpectedPlayLength(ExistingAnimSequence, Config);
	if (Config->HasField(TEXT("Notifies")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Notifies = nullptr;
		if (!Config->TryGetArrayField(TEXT("Notifies"), Notifies) || !Notifies)
		{
			return FString(TEXT("'Notifies' must be an array"));
		}

		FString Error;
		if (!ValidateNotifies(*Notifies, EffectivePlayLength, Error))
		{
			return Error;
		}
	}

	if (Config->HasField(TEXT("NotifyStates")))
	{
		const TArray<TSharedPtr<FJsonValue>>* NotifyStates = nullptr;
		if (!Config->TryGetArrayField(TEXT("NotifyStates"), NotifyStates) || !NotifyStates)
		{
			return FString(TEXT("'NotifyStates' must be an array"));
		}

		FString Error;
		if (!ValidateNotifyStates(*NotifyStates, EffectivePlayLength, Error))
		{
			return Error;
		}
	}

	if (Config->HasField(TEXT("SyncMarkers")))
	{
		const TArray<TSharedPtr<FJsonValue>>* SyncMarkers = nullptr;
		if (!Config->TryGetArrayField(TEXT("SyncMarkers"), SyncMarkers) || !SyncMarkers)
		{
			return FString(TEXT("'SyncMarkers' must be an array"));
		}

		FString Error;
		if (!ValidateSyncMarkers(*SyncMarkers, EffectivePlayLength, Error))
		{
			return Error;
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

	TSharedPtr<FJsonObject> FrameRateJson = MakeShared<FJsonObject>();
	const IAnimationDataModel* DataModel = AnimSequence->GetDataModel();
	const FFrameRate FrameRate = DataModel ? DataModel->GetFrameRate() : AnimSequence->GetSamplingFrameRate();
	FrameRateJson->SetNumberField(TEXT("Numerator"), FrameRate.Numerator);
	FrameRateJson->SetNumberField(TEXT("Denominator"), FrameRate.Denominator);
	Config->SetObjectField(TEXT("FrameRate"), FrameRateJson);
	Config->SetNumberField(TEXT("RateScale"), AnimSequence->RateScale);

	if (DataModel)
	{
		Config->SetNumberField(TEXT("NumberOfFrames"), DataModel->GetNumberOfFrames());

		TArray<TSharedPtr<FJsonValue>> FloatCurveValues;
		for (const FFloatCurve& FloatCurve : DataModel->GetFloatCurves())
		{
			TSharedPtr<FJsonObject> CurveJson = MakeShared<FJsonObject>();
			CurveJson->SetStringField(TEXT("Name"), FloatCurve.GetName().ToString());

			TArray<TSharedPtr<FJsonValue>> KeyValues;
			for (const FRichCurveKey& Key : FloatCurve.FloatCurve.GetConstRefOfKeys())
			{
				TSharedPtr<FJsonObject> KeyJson = MakeShared<FJsonObject>();
				KeyJson->SetNumberField(TEXT("Time"), Key.Time);
				KeyJson->SetNumberField(TEXT("Value"), Key.Value);
				KeyJson->SetStringField(TEXT("InterpMode"), InterpModeToString(Key.InterpMode));
				KeyValues.Add(MakeShared<FJsonValueObject>(KeyJson));
			}

			CurveJson->SetArrayField(TEXT("Keys"), KeyValues);
			FloatCurveValues.Add(MakeShared<FJsonValueObject>(CurveJson));
		}

		if (FloatCurveValues.Num() > 0)
		{
			Config->SetArrayField(TEXT("FloatCurves"), FloatCurveValues);
		}
	}

	TArray<TSharedPtr<FJsonValue>> NotifyValues;
	TArray<TSharedPtr<FJsonValue>> NotifyStateValues;
	for (const FAnimNotifyEvent& NotifyEvent : AnimSequence->Notifies)
	{
		if (IsSupportedNamedNotify(NotifyEvent) || IsSupportedNamedNotifyState(NotifyEvent))
		{
			TSharedPtr<FJsonObject> NotifyJson = MakeShared<FJsonObject>();
			NotifyJson->SetStringField(TEXT("Name"), NotifyEvent.NotifyName.ToString());
			NotifyJson->SetNumberField(TEXT("Time"), NotifyEvent.GetTime());
			NotifyJson->SetNumberField(TEXT("TrackIndex"), NotifyEvent.TrackIndex);

			if (IsSupportedNamedNotifyState(NotifyEvent))
			{
				NotifyJson->SetNumberField(TEXT("Duration"), NotifyEvent.GetDuration());
				NotifyStateValues.Add(MakeShared<FJsonValueObject>(NotifyJson));
			}
			else
			{
				NotifyValues.Add(MakeShared<FJsonValueObject>(NotifyJson));
			}
		}
	}
	if (NotifyValues.Num() > 0)
	{
		Config->SetArrayField(TEXT("Notifies"), NotifyValues);
	}
	if (NotifyStateValues.Num() > 0)
	{
		Config->SetArrayField(TEXT("NotifyStates"), NotifyStateValues);
	}

	TArray<TSharedPtr<FJsonValue>> SyncMarkerValues;
	for (const FAnimSyncMarker& Marker : AnimSequence->AuthoredSyncMarkers)
	{
		if (Marker.MarkerName.IsNone())
		{
			continue;
		}

		TSharedPtr<FJsonObject> MarkerJson = MakeShared<FJsonObject>();
		MarkerJson->SetStringField(TEXT("Name"), Marker.MarkerName.ToString());
		MarkerJson->SetNumberField(TEXT("Time"), Marker.Time);
#if WITH_EDITORONLY_DATA
		MarkerJson->SetNumberField(TEXT("TrackIndex"), Marker.TrackIndex);
#else
		MarkerJson->SetNumberField(TEXT("TrackIndex"), 0);
#endif
		SyncMarkerValues.Add(MakeShared<FJsonValueObject>(MarkerJson));
	}
	if (SyncMarkerValues.Num() > 0)
	{
		Config->SetArrayField(TEXT("SyncMarkers"), SyncMarkerValues);
	}
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

bool FAnimSequenceGenerator::ValidateFloatCurves(
	const TArray<TSharedPtr<FJsonValue>>& FloatCurves,
	FString& OutError) const
{
	TSet<FName> CurveNames;
	for (const TSharedPtr<FJsonValue>& CurveValue : FloatCurves)
	{
		const TSharedPtr<FJsonObject>* CurveObject = nullptr;
		if (!CurveValue.IsValid() || !CurveValue->TryGetObject(CurveObject) || !CurveObject || !CurveObject->IsValid())
		{
			OutError = TEXT("FloatCurves entries must be objects");
			return false;
		}

		FString CurveName;
		if (!(*CurveObject)->TryGetStringField(TEXT("Name"), CurveName) || CurveName.IsEmpty())
		{
			OutError = TEXT("FloatCurves entries require non-empty Name");
			return false;
		}
		const FName CurveFName(*CurveName);
		if (CurveFName.IsNone())
		{
			OutError = TEXT("FloatCurves entries require a valid non-None Name");
			return false;
		}
		if (CurveNames.Contains(CurveFName))
		{
			OutError = FString::Printf(TEXT("FloatCurves contains duplicate Name '%s'"), *CurveName);
			return false;
		}
		CurveNames.Add(CurveFName);

		if (!(*CurveObject)->HasTypedField<EJson::Array>(TEXT("Keys")))
		{
			OutError = FString::Printf(TEXT("Float curve '%s' requires Keys array"), *CurveName);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>& Keys = (*CurveObject)->GetArrayField(TEXT("Keys"));
		bool bHasPreviousTime = false;
		float PreviousTime = 0.0f;
		for (const TSharedPtr<FJsonValue>& KeyValue : Keys)
		{
			const TSharedPtr<FJsonObject>* KeyObject = nullptr;
			if (!KeyValue.IsValid() || !KeyValue->TryGetObject(KeyObject) || !KeyObject || !KeyObject->IsValid())
			{
				OutError = FString::Printf(TEXT("Float curve '%s' contains a non-object key"), *CurveName);
				return false;
			}

			double Time = 0.0;
			if (!(*KeyObject)->TryGetNumberField(TEXT("Time"), Time))
			{
				OutError = FString::Printf(TEXT("Float curve '%s' key requires numeric Time"), *CurveName);
				return false;
			}
			const float TimeFloat = static_cast<float>(Time);
			if (!FMath::IsFinite(TimeFloat) || TimeFloat < 0.0f)
			{
				OutError = FString::Printf(TEXT("Float curve '%s' key Time must be finite and non-negative"), *CurveName);
				return false;
			}
			if (bHasPreviousTime && TimeFloat <= PreviousTime)
			{
				OutError = FString::Printf(TEXT("Float curve '%s' key Time values must be strictly increasing"), *CurveName);
				return false;
			}
			PreviousTime = TimeFloat;
			bHasPreviousTime = true;

			double Value = 0.0;
			if (!(*KeyObject)->TryGetNumberField(TEXT("Value"), Value))
			{
				OutError = FString::Printf(TEXT("Float curve '%s' key requires numeric Value"), *CurveName);
				return false;
			}
			const float ValueFloat = static_cast<float>(Value);
			if (!FMath::IsFinite(ValueFloat))
			{
				OutError = FString::Printf(TEXT("Float curve '%s' key Value must be finite"), *CurveName);
				return false;
			}

			FString InterpMode = TEXT("Linear");
			if ((*KeyObject)->HasField(TEXT("InterpMode")) && !(*KeyObject)->TryGetStringField(TEXT("InterpMode"), InterpMode))
			{
				OutError = FString::Printf(TEXT("Float curve '%s' key InterpMode must be a string"), *CurveName);
				return false;
			}
			if (!InterpMode.Equals(TEXT("Linear"), ESearchCase::IgnoreCase) &&
				!InterpMode.Equals(TEXT("Constant"), ESearchCase::IgnoreCase) &&
				!InterpMode.Equals(TEXT("Cubic"), ESearchCase::IgnoreCase))
			{
				OutError = FString::Printf(TEXT("Float curve '%s' key has unsupported InterpMode: %s"), *CurveName, *InterpMode);
				return false;
			}
		}
	}

	return true;
}

bool FAnimSequenceGenerator::ApplyFloatCurves(
	UAnimSequence* AnimSequence,
	const TArray<TSharedPtr<FJsonValue>>& FloatCurves,
	FString& OutError) const
{
	if (!AnimSequence)
	{
		OutError = TEXT("Invalid AnimSequence");
		return false;
	}
	if (!ValidateFloatCurves(FloatCurves, OutError))
	{
		return false;
	}

	IAnimationDataController& Controller = AnimSequence->GetController();
	TArray<FName> ExistingFloatCurveNames;
	if (const IAnimationDataModel* DataModel = AnimSequence->GetDataModel())
	{
		ExistingFloatCurveNames.Reserve(DataModel->GetFloatCurves().Num());
		for (const FFloatCurve& ExistingCurve : DataModel->GetFloatCurves())
		{
			ExistingFloatCurveNames.Add(ExistingCurve.GetName());
		}
	}

	for (const FName& ExistingCurveName : ExistingFloatCurveNames)
	{
		const FAnimationCurveIdentifier ExistingCurveId(ExistingCurveName, ERawCurveTrackTypes::RCT_Float);
		if (!Controller.RemoveCurve(ExistingCurveId, false))
		{
			OutError = FString::Printf(TEXT("Failed to remove existing float curve '%s'"), *ExistingCurveName.ToString());
			return false;
		}
	}

	for (const TSharedPtr<FJsonValue>& CurveValue : FloatCurves)
	{
		const TSharedPtr<FJsonObject>* CurveObject = nullptr;
		if (!CurveValue.IsValid() || !CurveValue->TryGetObject(CurveObject) || !CurveObject || !CurveObject->IsValid())
		{
			OutError = TEXT("FloatCurves entries must be objects");
			return false;
		}

		FString CurveName;
		if (!(*CurveObject)->TryGetStringField(TEXT("Name"), CurveName) || CurveName.IsEmpty())
		{
			OutError = TEXT("FloatCurves entries require non-empty Name");
			return false;
		}

		if (!(*CurveObject)->HasTypedField<EJson::Array>(TEXT("Keys")))
		{
			OutError = FString::Printf(TEXT("Float curve '%s' requires Keys array"), *CurveName);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>& Keys = (*CurveObject)->GetArrayField(TEXT("Keys"));
		TArray<FRichCurveKey> RichKeys;
		RichKeys.Reserve(Keys.Num());
		for (const TSharedPtr<FJsonValue>& KeyValue : Keys)
		{
			const TSharedPtr<FJsonObject>* KeyObject = nullptr;
			if (!KeyValue.IsValid() || !KeyValue->TryGetObject(KeyObject) || !KeyObject || !KeyObject->IsValid())
			{
				OutError = FString::Printf(TEXT("Float curve '%s' contains a non-object key"), *CurveName);
				return false;
			}

			double Time = 0.0;
			if (!(*KeyObject)->TryGetNumberField(TEXT("Time"), Time))
			{
				OutError = FString::Printf(TEXT("Float curve '%s' key requires numeric Time"), *CurveName);
				return false;
			}

			double Value = 0.0;
			if (!(*KeyObject)->TryGetNumberField(TEXT("Value"), Value))
			{
				OutError = FString::Printf(TEXT("Float curve '%s' key requires numeric Value"), *CurveName);
				return false;
			}

			FString InterpMode = TEXT("Linear");
			if ((*KeyObject)->HasField(TEXT("InterpMode")) && !(*KeyObject)->TryGetStringField(TEXT("InterpMode"), InterpMode))
			{
				OutError = FString::Printf(TEXT("Float curve '%s' key InterpMode must be a string"), *CurveName);
				return false;
			}
			if (!InterpMode.Equals(TEXT("Linear"), ESearchCase::IgnoreCase) &&
				!InterpMode.Equals(TEXT("Constant"), ESearchCase::IgnoreCase) &&
				!InterpMode.Equals(TEXT("Cubic"), ESearchCase::IgnoreCase))
			{
				OutError = FString::Printf(TEXT("Float curve '%s' key has unsupported InterpMode: %s"), *CurveName, *InterpMode);
				return false;
			}

			FRichCurveKey Key(static_cast<float>(Time), static_cast<float>(Value));
			Key.InterpMode = ParseInterpMode(InterpMode);
			RichKeys.Add(Key);
		}

		const FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Float);
		if (!Controller.AddCurve(CurveId, AACF_Editable, false))
		{
			OutError = FString::Printf(TEXT("Failed to add float curve '%s'"), *CurveName);
			return false;
		}
		if (!Controller.SetCurveKeys(CurveId, RichKeys, false))
		{
			OutError = FString::Printf(TEXT("Failed to set keys for float curve '%s'"), *CurveName);
			return false;
		}
	}

	return true;
}

ERichCurveInterpMode FAnimSequenceGenerator::ParseInterpMode(const FString& InterpMode) const
{
	if (InterpMode.Equals(TEXT("Constant"), ESearchCase::IgnoreCase))
	{
		return RCIM_Constant;
	}
	if (InterpMode.Equals(TEXT("Cubic"), ESearchCase::IgnoreCase))
	{
		return RCIM_Cubic;
	}
	return RCIM_Linear;
}

bool FAnimSequenceGenerator::ValidateNotifies(
	const TArray<TSharedPtr<FJsonValue>>& Notifies,
	TOptional<float> PlayLength,
	FString& OutError) const
{
	for (const TSharedPtr<FJsonValue>& NotifyValue : Notifies)
	{
		TSharedPtr<FJsonObject> NotifyObject;
		if (!TryGetEntryObject(NotifyValue, TEXT("Notifies"), NotifyObject, OutError))
		{
			return false;
		}

		FString NotifyName;
		FName NotifyFName;
		if (!TryGetValidName(NotifyObject, TEXT("Notifies"), NotifyName, NotifyFName, OutError))
		{
			return false;
		}

		const FString Context = FString::Printf(TEXT("Notify '%s'"), *NotifyName);
		if (!RejectUnsupportedNotifyClassFields(NotifyObject, TEXT("Notifies"), Context, OutError))
		{
			return false;
		}

		float Time = 0.0f;
		if (!TryGetFiniteFloatField(NotifyObject, TEXT("Time"), Context, Time, OutError) ||
			!ValidateTimeWithinPlayLength(Time, PlayLength, Context, OutError))
		{
			return false;
		}

		int32 TrackIndex = 0;
		if (!TryGetTrackIndex(NotifyObject, Context, TrackIndex, OutError))
		{
			return false;
		}
	}

	return true;
}

bool FAnimSequenceGenerator::ApplyNotifies(
	UAnimSequence* AnimSequence,
	const TArray<TSharedPtr<FJsonValue>>& Notifies,
	FString& OutError) const
{
	if (!AnimSequence)
	{
		OutError = TEXT("Invalid AnimSequence");
		return false;
	}
	if (!ValidateNotifies(Notifies, GetValidatedPlayLength(AnimSequence), OutError))
	{
		return false;
	}

	TArray<FAnimNotifyEvent> UpdatedNotifies;
	UpdatedNotifies.Reserve(AnimSequence->Notifies.Num() + Notifies.Num());
	for (const FAnimNotifyEvent& ExistingNotify : AnimSequence->Notifies)
	{
		if (!IsSupportedNamedNotify(ExistingNotify))
		{
			UpdatedNotifies.Add(ExistingNotify);
		}
	}

	for (const TSharedPtr<FJsonValue>& NotifyValue : Notifies)
	{
		TSharedPtr<FJsonObject> NotifyObject;
		if (!TryGetEntryObject(NotifyValue, TEXT("Notifies"), NotifyObject, OutError))
		{
			return false;
		}

		FString NotifyName;
		FName NotifyFName;
		if (!TryGetValidName(NotifyObject, TEXT("Notifies"), NotifyName, NotifyFName, OutError))
		{
			return false;
		}

		const FString Context = FString::Printf(TEXT("Notify '%s'"), *NotifyName);
		float Time = 0.0f;
		int32 TrackIndex = 0;
		if (!TryGetFiniteFloatField(NotifyObject, TEXT("Time"), Context, Time, OutError) ||
			!TryGetTrackIndex(NotifyObject, Context, TrackIndex, OutError))
		{
			return false;
		}

		FAnimNotifyEvent NotifyEvent;
		NotifyEvent.NotifyName = NotifyFName;
		NotifyEvent.TrackIndex = TrackIndex;
		NotifyEvent.SetTime(Time);
		NotifyEvent.RefreshTriggerOffset(AnimSequence->CalculateOffsetForNotify(Time));
#if WITH_EDITORONLY_DATA
		NotifyEvent.Guid = FGuid::NewGuid();
#endif
		UpdatedNotifies.Add(NotifyEvent);
	}

	AnimSequence->Notifies = MoveTemp(UpdatedNotifies);
	RefreshNotifyData(AnimSequence);
	return true;
}

bool FAnimSequenceGenerator::ValidateNotifyStates(
	const TArray<TSharedPtr<FJsonValue>>& NotifyStates,
	TOptional<float> PlayLength,
	FString& OutError) const
{
	for (const TSharedPtr<FJsonValue>& NotifyValue : NotifyStates)
	{
		TSharedPtr<FJsonObject> NotifyObject;
		if (!TryGetEntryObject(NotifyValue, TEXT("NotifyStates"), NotifyObject, OutError))
		{
			return false;
		}

		FString NotifyName;
		FName NotifyFName;
		if (!TryGetValidName(NotifyObject, TEXT("NotifyStates"), NotifyName, NotifyFName, OutError))
		{
			return false;
		}

		const FString Context = FString::Printf(TEXT("NotifyState '%s'"), *NotifyName);
		if (!RejectUnsupportedNotifyClassFields(NotifyObject, TEXT("NotifyStates"), Context, OutError))
		{
			return false;
		}

		float Time = 0.0f;
		float Duration = 0.0f;
		if (!TryGetFiniteFloatField(NotifyObject, TEXT("Time"), Context, Time, OutError) ||
			!TryGetFiniteFloatField(NotifyObject, TEXT("Duration"), Context, Duration, OutError) ||
			!ValidateTimeRangeWithinPlayLength(Time, Duration, PlayLength, Context, OutError))
		{
			return false;
		}

		int32 TrackIndex = 0;
		if (!TryGetTrackIndex(NotifyObject, Context, TrackIndex, OutError))
		{
			return false;
		}
	}

	return true;
}

bool FAnimSequenceGenerator::ApplyNotifyStates(
	UAnimSequence* AnimSequence,
	const TArray<TSharedPtr<FJsonValue>>& NotifyStates,
	FString& OutError) const
{
	if (!AnimSequence)
	{
		OutError = TEXT("Invalid AnimSequence");
		return false;
	}
	if (!ValidateNotifyStates(NotifyStates, GetValidatedPlayLength(AnimSequence), OutError))
	{
		return false;
	}

	TArray<FAnimNotifyEvent> UpdatedNotifies;
	UpdatedNotifies.Reserve(AnimSequence->Notifies.Num() + NotifyStates.Num());
	for (const FAnimNotifyEvent& ExistingNotify : AnimSequence->Notifies)
	{
		if (!IsSupportedNamedNotifyState(ExistingNotify) && !IsLegacyNamedNotifyState(ExistingNotify))
		{
			UpdatedNotifies.Add(ExistingNotify);
		}
	}

	for (const TSharedPtr<FJsonValue>& NotifyValue : NotifyStates)
	{
		TSharedPtr<FJsonObject> NotifyObject;
		if (!TryGetEntryObject(NotifyValue, TEXT("NotifyStates"), NotifyObject, OutError))
		{
			return false;
		}

		FString NotifyName;
		FName NotifyFName;
		if (!TryGetValidName(NotifyObject, TEXT("NotifyStates"), NotifyName, NotifyFName, OutError))
		{
			return false;
		}

		const FString Context = FString::Printf(TEXT("NotifyState '%s'"), *NotifyName);
		float Time = 0.0f;
		float Duration = 0.0f;
		int32 TrackIndex = 0;
		if (!TryGetFiniteFloatField(NotifyObject, TEXT("Time"), Context, Time, OutError) ||
			!TryGetFiniteFloatField(NotifyObject, TEXT("Duration"), Context, Duration, OutError) ||
			!TryGetTrackIndex(NotifyObject, Context, TrackIndex, OutError))
		{
			return false;
		}

		FAnimNotifyEvent NotifyEvent;
		NotifyEvent.NotifyName = NotifyFName;
		NotifyEvent.NotifyStateClass = NewObject<UAssetFactoryNamedAnimNotifyState>(AnimSequence, NAME_None, RF_Transactional);
		NotifyEvent.TrackIndex = TrackIndex;
		NotifyEvent.SetTime(Time);
		NotifyEvent.SetDuration(Duration);
		NotifyEvent.RefreshTriggerOffset(AnimSequence->CalculateOffsetForNotify(Time));
		NotifyEvent.RefreshEndTriggerOffset(AnimSequence->CalculateOffsetForNotify(Time + Duration));
#if WITH_EDITORONLY_DATA
		NotifyEvent.Guid = FGuid::NewGuid();
#endif
		UpdatedNotifies.Add(NotifyEvent);
	}

	AnimSequence->Notifies = MoveTemp(UpdatedNotifies);
	RefreshNotifyData(AnimSequence);
	return true;
}

bool FAnimSequenceGenerator::ValidateSyncMarkers(
	const TArray<TSharedPtr<FJsonValue>>& SyncMarkers,
	TOptional<float> PlayLength,
	FString& OutError) const
{
	for (const TSharedPtr<FJsonValue>& MarkerValue : SyncMarkers)
	{
		TSharedPtr<FJsonObject> MarkerObject;
		if (!TryGetEntryObject(MarkerValue, TEXT("SyncMarkers"), MarkerObject, OutError))
		{
			return false;
		}

		FString MarkerName;
		FName MarkerFName;
		if (!TryGetValidName(MarkerObject, TEXT("SyncMarkers"), MarkerName, MarkerFName, OutError))
		{
			return false;
		}

		const FString Context = FString::Printf(TEXT("Sync marker '%s'"), *MarkerName);
		float Time = 0.0f;
		if (!TryGetFiniteFloatField(MarkerObject, TEXT("Time"), Context, Time, OutError) ||
			!ValidateTimeWithinPlayLength(Time, PlayLength, Context, OutError))
		{
			return false;
		}

		int32 TrackIndex = 0;
		if (!TryGetTrackIndex(MarkerObject, Context, TrackIndex, OutError))
		{
			return false;
		}
	}

	return true;
}

bool FAnimSequenceGenerator::ApplySyncMarkers(
	UAnimSequence* AnimSequence,
	const TArray<TSharedPtr<FJsonValue>>& SyncMarkers,
	FString& OutError) const
{
	if (!AnimSequence)
	{
		OutError = TEXT("Invalid AnimSequence");
		return false;
	}
	if (!ValidateSyncMarkers(SyncMarkers, GetValidatedPlayLength(AnimSequence), OutError))
	{
		return false;
	}

	TArray<FAnimSyncMarker> UpdatedSyncMarkers;
	UpdatedSyncMarkers.Reserve(SyncMarkers.Num());
	for (const TSharedPtr<FJsonValue>& MarkerValue : SyncMarkers)
	{
		TSharedPtr<FJsonObject> MarkerObject;
		if (!TryGetEntryObject(MarkerValue, TEXT("SyncMarkers"), MarkerObject, OutError))
		{
			return false;
		}

		FString MarkerName;
		FName MarkerFName;
		if (!TryGetValidName(MarkerObject, TEXT("SyncMarkers"), MarkerName, MarkerFName, OutError))
		{
			return false;
		}

		const FString Context = FString::Printf(TEXT("Sync marker '%s'"), *MarkerName);
		float Time = 0.0f;
		int32 TrackIndex = 0;
		if (!TryGetFiniteFloatField(MarkerObject, TEXT("Time"), Context, Time, OutError) ||
			!TryGetTrackIndex(MarkerObject, Context, TrackIndex, OutError))
		{
			return false;
		}

		FAnimSyncMarker Marker;
		Marker.MarkerName = MarkerFName;
		Marker.Time = Time;
#if WITH_EDITORONLY_DATA
		Marker.TrackIndex = TrackIndex;
		Marker.Guid = FGuid::NewGuid();
#endif
		UpdatedSyncMarkers.Add(Marker);
	}

	AnimSequence->InitializeNotifyTrack();
	AnimSequence->AuthoredSyncMarkers = MoveTemp(UpdatedSyncMarkers);
	AnimSequence->SortSyncMarkers();
	AnimSequence->RefreshSyncMarkerDataFromAuthored();
	AnimSequence->RefreshCacheData();
	return true;
}

FString FAnimSequenceGenerator::InterpModeToString(ERichCurveInterpMode InterpMode) const
{
	switch (InterpMode)
	{
	case RCIM_Constant:
		return TEXT("Constant");
	case RCIM_Cubic:
		return TEXT("Cubic");
	case RCIM_Linear:
	default:
		return TEXT("Linear");
	}
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
	if (!ValidateFrameRateField(Config, OutError) || !ValidateNumberOfFramesField(Config, OutError))
	{
		return false;
	}

	const TOptional<float> EffectivePlayLength = GetExpectedPlayLength(AnimSequence, Config);
	if (Config->HasField(TEXT("Notifies")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Notifies = nullptr;
		if (!Config->TryGetArrayField(TEXT("Notifies"), Notifies) || !Notifies)
		{
			OutError = TEXT("'Notifies' must be an array");
			return false;
		}
		if (!ValidateNotifies(*Notifies, EffectivePlayLength, OutError))
		{
			return false;
		}
	}
	if (Config->HasField(TEXT("NotifyStates")))
	{
		const TArray<TSharedPtr<FJsonValue>>* NotifyStates = nullptr;
		if (!Config->TryGetArrayField(TEXT("NotifyStates"), NotifyStates) || !NotifyStates)
		{
			OutError = TEXT("'NotifyStates' must be an array");
			return false;
		}
		if (!ValidateNotifyStates(*NotifyStates, EffectivePlayLength, OutError))
		{
			return false;
		}
	}
	if (Config->HasField(TEXT("SyncMarkers")))
	{
		const TArray<TSharedPtr<FJsonValue>>* SyncMarkers = nullptr;
		if (!Config->TryGetArrayField(TEXT("SyncMarkers"), SyncMarkers) || !SyncMarkers)
		{
			OutError = TEXT("'SyncMarkers' must be an array");
			return false;
		}
		if (!ValidateSyncMarkers(*SyncMarkers, EffectivePlayLength, OutError))
		{
			return false;
		}
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
	if (Config->HasField(TEXT("Properties")))
	{
		if (!Config->HasTypedField<EJson::Object>(TEXT("Properties")))
		{
			OutError = TEXT("'Properties' must be an object");
			return false;
		}

		const TSharedPtr<FJsonObject> Properties = Config->GetObjectField(TEXT("Properties"));
		if (!FPropertySetterUtils::SetPropertiesFromJson(AnimSequence, Properties))
		{
			OutError = TEXT("Failed to apply AnimSequence Properties");
			return false;
		}
	}

	if (Config->HasField(TEXT("FloatCurves")))
	{
		const TArray<TSharedPtr<FJsonValue>>* FloatCurves = nullptr;
		if (!Config->TryGetArrayField(TEXT("FloatCurves"), FloatCurves) || !FloatCurves)
		{
			OutError = TEXT("'FloatCurves' must be an array");
			return false;
		}

		if (!ApplyFloatCurves(AnimSequence, *FloatCurves, OutError))
		{
			return false;
		}
	}

	if (Config->HasField(TEXT("Notifies")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Notifies = nullptr;
		if (!Config->TryGetArrayField(TEXT("Notifies"), Notifies) || !Notifies)
		{
			OutError = TEXT("'Notifies' must be an array");
			return false;
		}

		if (!ApplyNotifies(AnimSequence, *Notifies, OutError))
		{
			return false;
		}
	}

	if (Config->HasField(TEXT("NotifyStates")))
	{
		const TArray<TSharedPtr<FJsonValue>>* NotifyStates = nullptr;
		if (!Config->TryGetArrayField(TEXT("NotifyStates"), NotifyStates) || !NotifyStates)
		{
			OutError = TEXT("'NotifyStates' must be an array");
			return false;
		}

		if (!ApplyNotifyStates(AnimSequence, *NotifyStates, OutError))
		{
			return false;
		}
	}

	if (Config->HasField(TEXT("SyncMarkers")))
	{
		const TArray<TSharedPtr<FJsonValue>>* SyncMarkers = nullptr;
		if (!Config->TryGetArrayField(TEXT("SyncMarkers"), SyncMarkers) || !SyncMarkers)
		{
			OutError = TEXT("'SyncMarkers' must be an array");
			return false;
		}

		if (!ApplySyncMarkers(AnimSequence, *SyncMarkers, OutError))
		{
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
