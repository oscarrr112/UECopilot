// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/AnimMontageAssetDocumentCapability.h"

#include "AssetDocumentClassResolver.h"
#include "AssetDocumentFragmentCompiler.h"
#include "AssetDocumentPropertyAdapter.h"
#include "Profiles/AnimMontageNotifyPlacementAdapter.h"
#include "Regions/AssetDocumentFragmentArrayRegionAdapter.h"
#include "Regions/AssetDocumentPreviewApplyDiffAdapter.h"

#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimMetaData.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/TimeStretchCurve.h"
#include "Curves/RichCurve.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace
{
bool IsKnownBodyKey(const FString& BodyKey)
{
	for (const FName& KnownBodyKey : FAnimMontageAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (KnownBodyKey.ToString() == BodyKey)
		{
			return true;
		}
	}
	return false;
}

FString GetLegacyBodyKeyGuidance(const FString& BodyKey)
{
	if (BodyKey == TEXT("Slots"))
	{
		return TEXT("Use SlotAnimTracks");
	}
	if (BodyKey == TEXT("Segments"))
	{
		return TEXT("Use AnimSegments");
	}
	if (BodyKey == TEXT("Animation"))
	{
		return TEXT("Use AnimReference");
	}
	if (BodyKey == TEXT("Sections"))
	{
		return TEXT("Use CompositeSections");
	}
	return FString();
}

FAssetDocumentCapabilityResult BodyFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

bool IsNullOrObject(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && (Value->Type == EJson::Null || Value->Type == EJson::Object);
}

bool IsArray(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && Value->Type == EJson::Array;
}

bool IsObject(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && Value->Type == EJson::Object;
}

FAssetDocumentCapabilityResult FragmentFailure(const FAssetDocumentFragmentResult& FragmentResult)
{
	FAssetDocumentCapabilityResult Result = FAssetDocumentCapabilityResult::Failure(FragmentResult.Message);
	Result.Diagnostics = FragmentResult.Diagnostics;
	return Result;
}

FAssetDocumentCapabilityResult RequireObjectValue(const TSharedPtr<FJsonValue>& Value, const FString& Path, TSharedPtr<FJsonObject>& OutObject)
{
	if (!Value.IsValid() || Value->Type != EJson::Object)
	{
		return BodyFailure(TEXT("Expected a JSON object"), Path, TEXT("InvalidBodySectionType"));
	}

	OutObject = Value->AsObject();
	if (!OutObject.IsValid())
	{
		return BodyFailure(TEXT("Expected a JSON object"), Path, TEXT("InvalidBodySectionType"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult RequireArrayValue(const TSharedPtr<FJsonValue>& Value, const FString& Path, const TArray<TSharedPtr<FJsonValue>>*& OutArray)
{
	if (!Value.IsValid() || Value->Type != EJson::Array)
	{
		return BodyFailure(TEXT("Expected a JSON array"), Path, TEXT("InvalidBodySectionType"));
	}

	OutArray = &Value->AsArray();
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult CompileObjectFragment(
	const FAssetDocumentFragmentCompiler& Compiler,
	const FAssetDocumentCapabilityContext& CapabilityContext,
	UAnimMontage* Montage,
	const TSharedRef<FJsonObject>& Fragment,
	UClass* ExpectedBaseClass,
	const FString& JsonPath,
	FAssetDocumentFragmentResult& OutFragmentResult,
	UObject* FragmentOuter = nullptr)
{
	FAssetDocumentFragmentContext FragmentContext;
	FragmentContext.OwnerAsset = Montage;
	FragmentContext.Outer = FragmentOuter ? FragmentOuter : Montage;
	FragmentContext.ExpectedBaseClass = ExpectedBaseClass;
	FragmentContext.Definitions = CapabilityContext.Definitions;
	FragmentContext.JsonPath = JsonPath;

	OutFragmentResult = Compiler.Compile(Fragment, FragmentContext);
	return OutFragmentResult.bSuccess ? FAssetDocumentCapabilityResult::Success() : FragmentFailure(OutFragmentResult);
}

FAssetDocumentCapabilityResult CompileMetadataArray(
	const FAssetDocumentFragmentCompiler& Compiler,
	const FAssetDocumentCapabilityContext& CapabilityContext,
	UAnimMontage* Montage,
	UObject* FragmentOuter,
	const TArray<TSharedPtr<FJsonValue>>& Values,
	const FString& BasePath,
	TArray<TObjectPtr<UAnimMetaData>>& OutMetadata)
{
	OutMetadata.Reset();
	OutMetadata.Reserve(Values.Num());

	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		const FString Path = FString::Printf(TEXT("%s/%d"), *BasePath, Index);
		TSharedPtr<FJsonObject> FragmentObject;
		const FAssetDocumentCapabilityResult ObjectResult = RequireObjectValue(Values[Index], Path, FragmentObject);
		if (!ObjectResult.bSuccess)
		{
			return ObjectResult;
		}

		FAssetDocumentFragmentResult FragmentResult;
		const FAssetDocumentCapabilityResult CompileResult = CompileObjectFragment(
			Compiler,
			CapabilityContext,
			Montage,
			FragmentObject.ToSharedRef(),
			UAnimMetaData::StaticClass(),
			Path,
			FragmentResult,
			FragmentOuter);
		if (!CompileResult.bSuccess)
		{
			return CompileResult;
		}

		UAnimMetaData* MetadataObject = Cast<UAnimMetaData>(FragmentResult.Object);
		if (!MetadataObject)
		{
			return BodyFailure(TEXT("Metadata fragment did not resolve to UAnimMetaData"), Path, TEXT("InvalidMetadataObject"));
		}

		OutMetadata.Add(MetadataObject);
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult MoveMetadataArrayToMontage(
	TArray<TObjectPtr<UAnimMetaData>>& MetadataArray,
	UAnimMontage* Montage,
	const FString& BasePath)
{
	for (int32 Index = 0; Index < MetadataArray.Num(); ++Index)
	{
		UAnimMetaData* MetadataObject = MetadataArray[Index];
		const FString Path = FString::Printf(TEXT("%s/%d"), *BasePath, Index);
		if (!MetadataObject)
		{
			return BodyFailure(TEXT("Metadata fragment resolved to null"), Path, TEXT("InvalidMetadataObject"));
		}

		if (MetadataObject->GetOuter() == Montage)
		{
			continue;
		}

		const FName MetadataName = MakeUniqueObjectName(Montage, MetadataObject->GetClass(), TEXT("AssetDocumentManaged_Metadata_"));
		if (!MetadataObject->Rename(*MetadataName.ToString(), Montage, REN_DontCreateRedirectors | REN_NonTransactional))
		{
			return BodyFailure(TEXT("Failed to attach metadata object to AnimMontage"), Path, TEXT("MetadataOuterMoveFailed"));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

struct FParsedFloatCurveKey
{
	float Time = 0.0f;
	float Value = 0.0f;
};

struct FParsedFloatCurve
{
	FName Name = NAME_None;
	int32 Flags = AACF_DefaultCurve;
	TArray<FParsedFloatCurveKey> Keys;
};

struct FParsedAnimMontageBody
{
	bool bHasSkeleton = false;
	USkeleton* Skeleton = nullptr;
	bool bHasPreviewMesh = false;
	USkeletalMesh* PreviewMesh = nullptr;
	bool bHasPreviewBasePose = false;
	UAnimSequence* PreviewBasePose = nullptr;
	bool bHasSyncGroup = false;
	FName SyncGroup = NAME_None;
	bool bHasSyncSlotIndex = false;
	int32 SyncSlotIndex = 0;
	bool bHasRootMotionTranslation = false;
	bool bRootMotionTranslation = false;
	bool bHasRootMotionRotation = false;
	bool bRootMotionRotation = false;
	bool bHasRootMotionRootLock = false;
	ERootMotionRootLock::Type RootMotionRootLock = ERootMotionRootLock::RefPose;
	bool bHasSlotAnimTracks = false;
	TArray<FSlotAnimationTrack> SlotAnimTracks;
	float CompositeLength = 0.0f;
	bool bHasCompositeSections = false;
	TArray<FCompositeSection> CompositeSections;
	FAnimMontageNotifyPlacementResult NotifyPlacements;
	bool bHasBlendInTime = false;
	float BlendInTime = 0.0f;
	bool bHasBlendOutTime = false;
	float BlendOutTime = 0.0f;
	bool bHasBlendModeIn = false;
	EMontageBlendMode BlendModeIn = EMontageBlendMode::Standard;
	bool bHasBlendModeOut = false;
	EMontageBlendMode BlendModeOut = EMontageBlendMode::Standard;
	bool bHasBlendOutTriggerTime = false;
	float BlendOutTriggerTime = 0.0f;
	bool bHasEnableAutoBlendOut = false;
	bool bEnableAutoBlendOut = false;
	bool bHasMetadata = false;
	TArray<TObjectPtr<UAnimMetaData>> Metadata;
	bool bHasSectionMetadata = false;
	TMap<FName, TArray<TObjectPtr<UAnimMetaData>>> SectionMetadataByName;
	bool bHasCurves = false;
	TArray<FParsedFloatCurve> Curves;
	bool bHasTimeStretch = false;
	bool bHasTimeStretchCurveName = false;
	FName TimeStretchCurveName = NAME_None;
	bool bHasTimeStretchSamplingRate = false;
	float TimeStretchSamplingRate = 60.0f;
	bool bHasTimeStretchCurveValueMinPrecision = false;
	float TimeStretchCurveValueMinPrecision = 0.01f;
};

FAssetDocumentCapabilityResult ValidateBodyObjectShape(const TSharedRef<FJsonObject>& BodyObject)
{
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BodyObject->Values)
	{
		const FString Path = FString::Printf(TEXT("/Body/%s"), *Pair.Key);
		const FString Guidance = GetLegacyBodyKeyGuidance(Pair.Key);
		if (!Guidance.IsEmpty())
		{
			return BodyFailure(
				FString::Printf(TEXT("Body key '%s' is not supported. %s"), *Pair.Key, *Guidance),
				Path,
				TEXT("DeprecatedBodyKey"));
		}

		if (!IsKnownBodyKey(Pair.Key))
		{
			return BodyFailure(
				FString::Printf(TEXT("Unknown AnimMontage Body key '%s'"), *Pair.Key),
				Path,
				TEXT("UnknownBodyKey"));
		}
	}

	auto RequireNullOrObject = [&BodyObject](const TCHAR* FieldName) -> FAssetDocumentCapabilityResult
	{
		if (const TSharedPtr<FJsonValue>* Value = BodyObject->Values.Find(FieldName))
		{
			if (!IsNullOrObject(*Value))
			{
				const FString Path = FString::Printf(TEXT("/Body/%s"), FieldName);
				return BodyFailure(FString::Printf(TEXT("Body.%s must be null or an object fragment"), FieldName), Path, TEXT("InvalidBodySectionType"));
			}
		}
		return FAssetDocumentCapabilityResult::Success();
	};

	auto RequireArray = [&BodyObject](const TCHAR* FieldName) -> FAssetDocumentCapabilityResult
	{
		if (const TSharedPtr<FJsonValue>* Value = BodyObject->Values.Find(FieldName))
		{
			if (!IsArray(*Value))
			{
				const FString Path = FString::Printf(TEXT("/Body/%s"), FieldName);
				return BodyFailure(FString::Printf(TEXT("Body.%s must be an array"), FieldName), Path, TEXT("InvalidBodySectionType"));
			}
		}
		return FAssetDocumentCapabilityResult::Success();
	};

	auto RequireObject = [&BodyObject](const TCHAR* FieldName) -> FAssetDocumentCapabilityResult
	{
		if (const TSharedPtr<FJsonValue>* Value = BodyObject->Values.Find(FieldName))
		{
			if (!IsObject(*Value))
			{
				const FString Path = FString::Printf(TEXT("/Body/%s"), FieldName);
				return BodyFailure(FString::Printf(TEXT("Body.%s must be an object"), FieldName), Path, TEXT("InvalidBodySectionType"));
			}
		}
		return FAssetDocumentCapabilityResult::Success();
	};

	const TArray<FAssetDocumentCapabilityResult> Results = {
		RequireNullOrObject(TEXT("Skeleton")),
		RequireNullOrObject(TEXT("PreviewMesh")),
		RequireObject(TEXT("References")),
		RequireObject(TEXT("Preview")),
		RequireObject(TEXT("Sync")),
		RequireObject(TEXT("RootMotion")),
		RequireArray(TEXT("Metadata")),
		RequireObject(TEXT("SectionMetadata")),
		RequireObject(TEXT("TimeStretch")),
		RequireArray(TEXT("Curves")),
		RequireArray(TEXT("SlotAnimTracks")),
		RequireArray(TEXT("CompositeSections")),
		RequireArray(TEXT("Notifies")),
		RequireArray(TEXT("NotifyStates")),
		RequireObject(TEXT("Blend")),
	};

	for (const FAssetDocumentCapabilityResult& Result : Results)
	{
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadOptionalNumber(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* FieldName,
	const FString& Path,
	bool& bOutHasValue,
	double& OutValue)
{
	bOutHasValue = false;
	if (const TSharedPtr<FJsonValue>* Value = Object->Values.Find(FieldName))
	{
		if (!Value->IsValid() || (*Value)->Type != EJson::Number)
		{
			return BodyFailure(FString::Printf(TEXT("%s must be a number"), FieldName), Path, TEXT("InvalidNumericField"));
		}

		bOutHasValue = true;
		OutValue = (*Value)->AsNumber();
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadOptionalNonNegativeNumber(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* FieldName,
	const FString& Path,
	bool& bOutHasValue,
	double& OutValue)
{
	const FAssetDocumentCapabilityResult Result = ReadOptionalNumber(Object, FieldName, Path, bOutHasValue, OutValue);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (bOutHasValue && OutValue < 0.0)
	{
		return BodyFailure(FString::Printf(TEXT("%s must be non-negative"), FieldName), Path, TEXT("InvalidTime"));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadOptionalString(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* FieldName,
	const FString& Path,
	const TCHAR* ErrorCode,
	bool& bOutHasValue,
	FString& OutValue)
{
	bOutHasValue = false;
	OutValue.Reset();
	if (const TSharedPtr<FJsonValue>* Value = Object->Values.Find(FieldName))
	{
		if (!Value->IsValid() || (*Value)->Type != EJson::String)
		{
			return BodyFailure(FString::Printf(TEXT("%s must be a string"), FieldName), Path, ErrorCode);
		}

		bOutHasValue = true;
		OutValue = (*Value)->AsString();
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadOptionalBool(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* FieldName,
	const FString& Path,
	bool& bOutHasValue,
	bool& OutValue)
{
	bOutHasValue = false;
	OutValue = false;
	if (const TSharedPtr<FJsonValue>* Value = Object->Values.Find(FieldName))
	{
		if (!Value->IsValid() || (*Value)->Type != EJson::Boolean)
		{
			return BodyFailure(FString::Printf(TEXT("%s must be a boolean"), FieldName), Path, TEXT("InvalidBooleanField"));
		}

		bOutHasValue = true;
		OutValue = (*Value)->AsBool();
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadOptionalInteger(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* FieldName,
	const FString& Path,
	const TCHAR* ErrorCode,
	bool& bOutHasValue,
	int32& OutValue)
{
	double NumberValue = 0.0;
	const FAssetDocumentCapabilityResult Result = ReadOptionalNumber(Object, FieldName, Path, bOutHasValue, NumberValue);
	if (!Result.bSuccess)
	{
		return BodyFailure(FString::Printf(TEXT("%s must be an integer"), FieldName), Path, ErrorCode);
	}
	if (bOutHasValue)
	{
		const int32 IntegerValue = static_cast<int32>(NumberValue);
		if (NumberValue < 0.0 || !FMath::IsNearlyEqual(NumberValue, static_cast<double>(IntegerValue)))
		{
			return BodyFailure(FString::Printf(TEXT("%s must be a non-negative integer"), FieldName), Path, ErrorCode);
		}
		OutValue = IntegerValue;
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseObjectReferenceFromObject(
	const FAssetDocumentFragmentCompiler* Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimMontage* Montage,
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* FieldName,
	UClass* ExpectedBaseClass,
	const FString& Path,
	bool bResolveFragments,
	bool& bOutHasValue,
	UObject*& OutObject)
{
	bOutHasValue = false;
	OutObject = nullptr;

	const TSharedPtr<FJsonValue>* Value = Object->Values.Find(FieldName);
	if (!Value)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	if (!Value->IsValid() || (*Value)->Type == EJson::Null)
	{
		bOutHasValue = true;
		return FAssetDocumentCapabilityResult::Success();
	}

	TSharedPtr<FJsonObject> FragmentObject;
	const FAssetDocumentCapabilityResult ObjectResult = RequireObjectValue(*Value, Path, FragmentObject);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}

	bOutHasValue = true;
	if (!bResolveFragments)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	if (!Compiler || !Montage)
	{
		return BodyFailure(TEXT("Fragment resolution requires an AnimMontage asset"), Path, TEXT("UnsupportedAsset"));
	}

	FAssetDocumentFragmentResult FragmentResult;
	const FAssetDocumentCapabilityResult CompileResult = CompileObjectFragment(*Compiler, Context, Montage, FragmentObject.ToSharedRef(), ExpectedBaseClass, Path, FragmentResult);
	if (!CompileResult.bSuccess)
	{
		return CompileResult;
	}

	if (FragmentResult.Object && !FragmentResult.Object->IsA(ExpectedBaseClass))
	{
		return BodyFailure(FString::Printf(TEXT("%s did not resolve to %s"), FieldName, *ExpectedBaseClass->GetName()), Path, TEXT("InvalidObjectReference"));
	}

	OutObject = FragmentResult.Object;
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseObjectReference(
	const FAssetDocumentFragmentCompiler* Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimMontage* Montage,
	const TSharedRef<FJsonObject>& BodyObject,
	const TCHAR* FieldName,
	UClass* ExpectedBaseClass,
	bool bResolveFragments,
	bool& bOutHasValue,
	UObject*& OutObject)
{
	const FString Path = FString::Printf(TEXT("/Body/%s"), FieldName);
	return ParseObjectReferenceFromObject(Compiler, Context, Montage, BodyObject, FieldName, ExpectedBaseClass, Path, bResolveFragments, bOutHasValue, OutObject);
}

bool TryParseRootMotionRootLock(const FString& Value, ERootMotionRootLock::Type& OutRootLock)
{
	if (Value == TEXT("RefPose"))
	{
		OutRootLock = ERootMotionRootLock::RefPose;
		return true;
	}
	if (Value == TEXT("AnimFirstFrame"))
	{
		OutRootLock = ERootMotionRootLock::AnimFirstFrame;
		return true;
	}
	if (Value == TEXT("Zero"))
	{
		OutRootLock = ERootMotionRootLock::Zero;
		return true;
	}
	return false;
}

FString RootMotionRootLockToString(ERootMotionRootLock::Type RootLock)
{
	switch (RootLock)
	{
	case ERootMotionRootLock::AnimFirstFrame:
		return TEXT("AnimFirstFrame");
	case ERootMotionRootLock::Zero:
		return TEXT("Zero");
	case ERootMotionRootLock::RefPose:
	default:
		return TEXT("RefPose");
	}
}

bool TryParseMontageBlendMode(const FString& Value, EMontageBlendMode& OutBlendMode)
{
	if (Value == TEXT("Standard"))
	{
		OutBlendMode = EMontageBlendMode::Standard;
		return true;
	}
	if (Value == TEXT("Inertialization"))
	{
		OutBlendMode = EMontageBlendMode::Inertialization;
		return true;
	}
	return false;
}

FString MontageBlendModeToString(EMontageBlendMode BlendMode)
{
	switch (BlendMode)
	{
	case EMontageBlendMode::Inertialization:
		return TEXT("Inertialization");
	case EMontageBlendMode::Standard:
	default:
		return TEXT("Standard");
	}
}

void RepairDerivedMontageMarkerCacheAfterApply(UAnimMontage& Montage)
{
	// UAnimMontage::CollectMarkers() is not exported from UE 5.7's MinimalAPI class.
	// Keep this as a post-apply repair for UE's derived marker cache; AssetDoc does not author MarkerData.
	Montage.MarkerData.AuthoredSyncMarkers.Reset();
	Montage.MarkerData.UniqueMarkerNames.Empty();

	if (Montage.SyncGroup == NAME_None || !Montage.SlotAnimTracks.IsValidIndex(Montage.SyncSlotIndex))
	{
		return;
	}

	const FAnimTrack& AnimTrack = Montage.SlotAnimTracks[Montage.SyncSlotIndex].AnimTrack;
	for (const FAnimSegment& Segment : AnimTrack.AnimSegments)
	{
		const UAnimSequence* Sequence = Cast<UAnimSequence>(Segment.GetAnimReference());
		if (!Sequence || Sequence->AuthoredSyncMarkers.IsEmpty())
		{
			continue;
		}

		for (const FAnimSyncMarker& Marker : Sequence->AuthoredSyncMarkers)
		{
			if (Marker.Time < Segment.AnimStartTime || Marker.Time > Segment.AnimEndTime)
			{
				continue;
			}

			const float TotalSegmentLength = (Segment.AnimEndTime - Segment.AnimStartTime) * Segment.AnimPlayRate;
			for (int32 LoopCount = 0; LoopCount < Segment.LoopingCount; ++LoopCount)
			{
				FAnimSyncMarker NewMarker;
				NewMarker.Time = Segment.StartPos + (Marker.Time - Segment.AnimStartTime) * Segment.AnimPlayRate + TotalSegmentLength * LoopCount;
				NewMarker.MarkerName = Marker.MarkerName;
				Montage.MarkerData.AuthoredSyncMarkers.Add(NewMarker);
			}
		}
	}

	Montage.MarkerData.AuthoredSyncMarkers.Sort();
	Montage.MarkerData.UniqueMarkerNames.Reserve(Montage.MarkerData.AuthoredSyncMarkers.Num());
	for (const FAnimSyncMarker& Marker : Montage.MarkerData.AuthoredSyncMarkers)
	{
		Montage.MarkerData.UniqueMarkerNames.AddUnique(Marker.MarkerName);
	}
}

FAssetDocumentCapabilityResult ParseSlotAnimTracks(
	const FAssetDocumentFragmentCompiler* Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimMontage* Montage,
	const TSharedRef<FJsonObject>& BodyObject,
	bool bResolveFragments,
	FParsedAnimMontageBody& OutParsed)
{
	const TSharedPtr<FJsonValue>* SlotAnimTracksValue = BodyObject->Values.Find(TEXT("SlotAnimTracks"));
	if (!SlotAnimTracksValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const TArray<TSharedPtr<FJsonValue>>* SlotAnimTracksArray = nullptr;
	FAssetDocumentCapabilityResult ArrayResult = RequireArrayValue(*SlotAnimTracksValue, TEXT("/Body/SlotAnimTracks"), SlotAnimTracksArray);
	if (!ArrayResult.bSuccess)
	{
		return ArrayResult;
	}

	TArray<FSlotAnimationTrack> NewSlotAnimTracks;
	float CompositeLength = 0.0f;

	for (int32 SlotIndex = 0; SlotIndex < SlotAnimTracksArray->Num(); ++SlotIndex)
	{
		const FString SlotPath = FString::Printf(TEXT("/Body/SlotAnimTracks/%d"), SlotIndex);
		TSharedPtr<FJsonObject> SlotObject;
		const FAssetDocumentCapabilityResult SlotObjectResult = RequireObjectValue((*SlotAnimTracksArray)[SlotIndex], SlotPath, SlotObject);
		if (!SlotObjectResult.bSuccess)
		{
			return SlotObjectResult;
		}

		FString SlotNameString;
		if (!SlotObject->TryGetStringField(TEXT("SlotName"), SlotNameString) || SlotNameString.TrimStartAndEnd().IsEmpty())
		{
			return BodyFailure(TEXT("SlotAnimTrack requires SlotName"), SlotPath / TEXT("SlotName"), TEXT("MissingSlotName"));
		}

		const TSharedPtr<FJsonObject>* AnimTrackObject = nullptr;
		if (!SlotObject->TryGetObjectField(TEXT("AnimTrack"), AnimTrackObject) || !AnimTrackObject || !AnimTrackObject->IsValid())
		{
			return BodyFailure(TEXT("SlotAnimTrack requires AnimTrack object"), SlotPath / TEXT("AnimTrack"), TEXT("MissingAnimTrack"));
		}

		const TArray<TSharedPtr<FJsonValue>>* AnimSegmentsArray = nullptr;
		if (!(*AnimTrackObject)->TryGetArrayField(TEXT("AnimSegments"), AnimSegmentsArray) || !AnimSegmentsArray)
		{
			return BodyFailure(TEXT("AnimTrack requires AnimSegments array"), SlotPath / TEXT("AnimTrack/AnimSegments"), TEXT("MissingAnimSegments"));
		}

		FSlotAnimationTrack SlotAnimTrack;
		SlotAnimTrack.SlotName = FName(*SlotNameString);

		for (int32 SegmentIndex = 0; SegmentIndex < AnimSegmentsArray->Num(); ++SegmentIndex)
		{
			const FString SegmentPath = FString::Printf(TEXT("%s/AnimTrack/AnimSegments/%d"), *SlotPath, SegmentIndex);
			TSharedPtr<FJsonObject> SegmentObject;
			const FAssetDocumentCapabilityResult SegmentObjectResult = RequireObjectValue((*AnimSegmentsArray)[SegmentIndex], SegmentPath, SegmentObject);
			if (!SegmentObjectResult.bSuccess)
			{
				return SegmentObjectResult;
			}

			const TSharedPtr<FJsonObject>* AnimReferenceObject = nullptr;
			if (!SegmentObject->TryGetObjectField(TEXT("AnimReference"), AnimReferenceObject) || !AnimReferenceObject || !AnimReferenceObject->IsValid())
			{
				return BodyFailure(TEXT("AnimSegment requires AnimReference fragment"), SegmentPath / TEXT("AnimReference"), TEXT("MissingAnimReference"));
			}

			FAnimSegment Segment;

			double NumberValue = 0.0;
			bool bHasNumber = false;
			FAssetDocumentCapabilityResult NumberResult = ReadOptionalNonNegativeNumber(SegmentObject.ToSharedRef(), TEXT("StartPos"), SegmentPath / TEXT("StartPos"), bHasNumber, NumberValue);
			if (!NumberResult.bSuccess)
			{
				return NumberResult;
			}
			if (bHasNumber)
			{
				Segment.StartPos = static_cast<float>(NumberValue);
			}

			NumberResult = ReadOptionalNonNegativeNumber(SegmentObject.ToSharedRef(), TEXT("AnimStartTime"), SegmentPath / TEXT("AnimStartTime"), bHasNumber, NumberValue);
			if (!NumberResult.bSuccess)
			{
				return NumberResult;
			}
			if (bHasNumber)
			{
				Segment.AnimStartTime = static_cast<float>(NumberValue);
			}

			NumberResult = ReadOptionalNonNegativeNumber(SegmentObject.ToSharedRef(), TEXT("AnimEndTime"), SegmentPath / TEXT("AnimEndTime"), bHasNumber, NumberValue);
			if (!NumberResult.bSuccess)
			{
				return NumberResult;
			}
			if (bHasNumber)
			{
				Segment.AnimEndTime = static_cast<float>(NumberValue);
			}

			NumberResult = ReadOptionalNumber(SegmentObject.ToSharedRef(), TEXT("AnimPlayRate"), SegmentPath / TEXT("AnimPlayRate"), bHasNumber, NumberValue);
			if (!NumberResult.bSuccess)
			{
				return NumberResult;
			}
			if (bHasNumber)
			{
				if (NumberValue <= 0.0)
				{
					return BodyFailure(TEXT("AnimPlayRate must be greater than zero"), SegmentPath / TEXT("AnimPlayRate"), TEXT("InvalidAnimPlayRate"));
				}
				Segment.AnimPlayRate = static_cast<float>(NumberValue);
			}

			NumberResult = ReadOptionalNumber(SegmentObject.ToSharedRef(), TEXT("LoopingCount"), SegmentPath / TEXT("LoopingCount"), bHasNumber, NumberValue);
			if (!NumberResult.bSuccess)
			{
				return NumberResult;
			}
			if (bHasNumber)
			{
				const double RoundedLoopingCount = FMath::RoundToDouble(NumberValue);
				if (NumberValue <= 0.0 || !FMath::IsNearlyEqual(NumberValue, RoundedLoopingCount))
				{
					return BodyFailure(TEXT("LoopingCount must be a positive integer"), SegmentPath / TEXT("LoopingCount"), TEXT("InvalidLoopingCount"));
				}
				Segment.LoopingCount = static_cast<int32>(RoundedLoopingCount);
			}

			if (Segment.AnimPlayRate <= 0.0f)
			{
				return BodyFailure(TEXT("AnimPlayRate must be greater than zero"), SegmentPath / TEXT("AnimPlayRate"), TEXT("InvalidAnimPlayRate"));
			}
			if (Segment.LoopingCount <= 0)
			{
				return BodyFailure(TEXT("LoopingCount must be greater than zero"), SegmentPath / TEXT("LoopingCount"), TEXT("InvalidLoopingCount"));
			}
			if (Segment.AnimEndTime < Segment.AnimStartTime)
			{
				return BodyFailure(TEXT("AnimEndTime must be greater than or equal to AnimStartTime"), SegmentPath / TEXT("AnimEndTime"), TEXT("InvalidAnimEndTime"));
			}

			if (bResolveFragments)
			{
				if (!Compiler || !Montage)
				{
					return BodyFailure(TEXT("Fragment resolution requires an AnimMontage asset"), SegmentPath / TEXT("AnimReference"), TEXT("UnsupportedAsset"));
				}

				FAssetDocumentFragmentResult AnimReferenceResult;
				FAssetDocumentCapabilityResult CompileResult = CompileObjectFragment(
					*Compiler,
					Context,
					Montage,
					AnimReferenceObject->ToSharedRef(),
					UAnimSequenceBase::StaticClass(),
					SegmentPath / TEXT("AnimReference"),
					AnimReferenceResult);
				if (!CompileResult.bSuccess)
				{
					return CompileResult;
				}

				UAnimSequenceBase* AnimReference = Cast<UAnimSequenceBase>(AnimReferenceResult.Object);
				if (!AnimReference)
				{
					return BodyFailure(TEXT("AnimReference did not resolve to UAnimSequenceBase"), SegmentPath / TEXT("AnimReference"), TEXT("InvalidAnimReference"));
				}

				Segment.SetAnimReference(AnimReference);
			}

			CompositeLength = FMath::Max(CompositeLength, Segment.GetEndPos());
			SlotAnimTrack.AnimTrack.AnimSegments.Add(Segment);
		}

		NewSlotAnimTracks.Add(SlotAnimTrack);
	}

	OutParsed.bHasSlotAnimTracks = true;
	OutParsed.SlotAnimTracks = MoveTemp(NewSlotAnimTracks);
	OutParsed.CompositeLength = CompositeLength;
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseCompositeSections(const TSharedRef<FJsonObject>& BodyObject, FParsedAnimMontageBody& OutParsed)
{
	const TSharedPtr<FJsonValue>* CompositeSectionsValue = BodyObject->Values.Find(TEXT("CompositeSections"));
	if (!CompositeSectionsValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const TArray<TSharedPtr<FJsonValue>>* CompositeSectionsArray = nullptr;
	FAssetDocumentCapabilityResult ArrayResult = RequireArrayValue(*CompositeSectionsValue, TEXT("/Body/CompositeSections"), CompositeSectionsArray);
	if (!ArrayResult.bSuccess)
	{
		return ArrayResult;
	}

	TArray<FCompositeSection> NewCompositeSections;
	TSet<FName> SectionNames;
	TMap<FName, FString> NextSectionPaths;

	for (int32 SectionIndex = 0; SectionIndex < CompositeSectionsArray->Num(); ++SectionIndex)
	{
		const FString SectionPath = FString::Printf(TEXT("/Body/CompositeSections/%d"), SectionIndex);
		TSharedPtr<FJsonObject> SectionObject;
		const FAssetDocumentCapabilityResult SectionObjectResult = RequireObjectValue((*CompositeSectionsArray)[SectionIndex], SectionPath, SectionObject);
		if (!SectionObjectResult.bSuccess)
		{
			return SectionObjectResult;
		}

		FString SectionNameString;
		if (!SectionObject->TryGetStringField(TEXT("SectionName"), SectionNameString) || SectionNameString.TrimStartAndEnd().IsEmpty())
		{
			return BodyFailure(TEXT("CompositeSection requires SectionName"), SectionPath / TEXT("SectionName"), TEXT("MissingSectionName"));
		}
		SectionNameString.TrimStartAndEndInline();

		const FName SectionName(*SectionNameString);
		if (SectionNames.Contains(SectionName))
		{
			return BodyFailure(FString::Printf(TEXT("Duplicate CompositeSection '%s'"), *SectionNameString), SectionPath / TEXT("SectionName"), TEXT("DuplicateSectionName"));
		}
		SectionNames.Add(SectionName);

		FCompositeSection Section;
		Section.SectionName = SectionName;

		double LinkableTime = 0.0;
		bool bHasLinkableTime = false;
		const FAssetDocumentCapabilityResult LinkableTimeResult = ReadOptionalNonNegativeNumber(SectionObject.ToSharedRef(), TEXT("LinkableTime"), SectionPath / TEXT("LinkableTime"), bHasLinkableTime, LinkableTime);
		if (!LinkableTimeResult.bSuccess)
		{
			return LinkableTimeResult;
		}
		Section.SetTime(static_cast<float>(LinkableTime));

		if (const TSharedPtr<FJsonValue>* NextSectionNameValue = SectionObject->Values.Find(TEXT("NextSectionName")))
		{
			const FString NextSectionNamePath = SectionPath / TEXT("NextSectionName");
			if (!NextSectionNameValue->IsValid() || (*NextSectionNameValue)->Type != EJson::String)
			{
				return BodyFailure(TEXT("NextSectionName must be a non-empty string when present"), NextSectionNamePath, TEXT("InvalidNextSectionName"));
			}

			FString NextSectionNameString = (*NextSectionNameValue)->AsString();
			NextSectionNameString.TrimStartAndEndInline();
			if (NextSectionNameString.IsEmpty())
			{
				return BodyFailure(TEXT("NextSectionName must be a non-empty string when present"), NextSectionNamePath, TEXT("InvalidNextSectionName"));
			}

			Section.NextSectionName = FName(*NextSectionNameString);
			NextSectionPaths.Add(Section.NextSectionName, NextSectionNamePath);
		}

		NewCompositeSections.Add(Section);
	}

	for (const FCompositeSection& Section : NewCompositeSections)
	{
		if (!Section.NextSectionName.IsNone() && !SectionNames.Contains(Section.NextSectionName))
		{
			const FString* NextSectionPath = NextSectionPaths.Find(Section.NextSectionName);
			return BodyFailure(
				FString::Printf(TEXT("NextSectionName '%s' does not reference an existing CompositeSection"), *Section.NextSectionName.ToString()),
				NextSectionPath ? *NextSectionPath : TEXT("/Body/CompositeSections"),
				TEXT("InvalidNextSectionName"));
		}
	}

	OutParsed.bHasCompositeSections = true;
	OutParsed.CompositeSections = MoveTemp(NewCompositeSections);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseBlend(const TSharedRef<FJsonObject>& BodyObject, FParsedAnimMontageBody& OutParsed)
{
	const TSharedPtr<FJsonValue>* BlendValue = BodyObject->Values.Find(TEXT("Blend"));
	if (!BlendValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TSharedPtr<FJsonObject> BlendObject;
	const FAssetDocumentCapabilityResult BlendObjectResult = RequireObjectValue(*BlendValue, TEXT("/Body/Blend"), BlendObject);
	if (!BlendObjectResult.bSuccess)
	{
		return BlendObjectResult;
	}

	double BlendTime = 0.0;
	bool bHasBlendTime = false;
	FAssetDocumentCapabilityResult BlendTimeResult = ReadOptionalNonNegativeNumber(BlendObject.ToSharedRef(), TEXT("BlendInTime"), TEXT("/Body/Blend/BlendInTime"), bHasBlendTime, BlendTime);
	if (!BlendTimeResult.bSuccess)
	{
		return BlendTimeResult;
	}
	if (bHasBlendTime)
	{
		OutParsed.bHasBlendInTime = true;
		OutParsed.BlendInTime = static_cast<float>(BlendTime);
	}

	BlendTimeResult = ReadOptionalNonNegativeNumber(BlendObject.ToSharedRef(), TEXT("BlendOutTime"), TEXT("/Body/Blend/BlendOutTime"), bHasBlendTime, BlendTime);
	if (!BlendTimeResult.bSuccess)
	{
		return BlendTimeResult;
	}
	if (bHasBlendTime)
	{
		OutParsed.bHasBlendOutTime = true;
		OutParsed.BlendOutTime = static_cast<float>(BlendTime);
	}

	FString BlendModeString;
	FAssetDocumentCapabilityResult Result = ReadOptionalString(
		BlendObject.ToSharedRef(),
		TEXT("BlendModeIn"),
		TEXT("/Body/Blend/BlendModeIn"),
		TEXT("InvalidBlendMode"),
		OutParsed.bHasBlendModeIn,
		BlendModeString);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (OutParsed.bHasBlendModeIn && !TryParseMontageBlendMode(BlendModeString, OutParsed.BlendModeIn))
	{
		return BodyFailure(TEXT("BlendModeIn must be Standard or Inertialization"), TEXT("/Body/Blend/BlendModeIn"), TEXT("InvalidBlendMode"));
	}

	Result = ReadOptionalString(
		BlendObject.ToSharedRef(),
		TEXT("BlendModeOut"),
		TEXT("/Body/Blend/BlendModeOut"),
		TEXT("InvalidBlendMode"),
		OutParsed.bHasBlendModeOut,
		BlendModeString);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (OutParsed.bHasBlendModeOut && !TryParseMontageBlendMode(BlendModeString, OutParsed.BlendModeOut))
	{
		return BodyFailure(TEXT("BlendModeOut must be Standard or Inertialization"), TEXT("/Body/Blend/BlendModeOut"), TEXT("InvalidBlendMode"));
	}

	double BlendOutTriggerTime = 0.0;
	Result = ReadOptionalNumber(
		BlendObject.ToSharedRef(),
		TEXT("BlendOutTriggerTime"),
		TEXT("/Body/Blend/BlendOutTriggerTime"),
		OutParsed.bHasBlendOutTriggerTime,
		BlendOutTriggerTime);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (OutParsed.bHasBlendOutTriggerTime)
	{
		if (BlendOutTriggerTime < 0.0 && BlendOutTriggerTime != -1.0)
		{
			return BodyFailure(TEXT("BlendOutTriggerTime must be -1.0 or non-negative"), TEXT("/Body/Blend/BlendOutTriggerTime"), TEXT("InvalidBlendOutTriggerTime"));
		}
		OutParsed.BlendOutTriggerTime = static_cast<float>(BlendOutTriggerTime);
	}

	Result = ReadOptionalBool(
		BlendObject.ToSharedRef(),
		TEXT("bEnableAutoBlendOut"),
		TEXT("/Body/Blend/bEnableAutoBlendOut"),
		OutParsed.bHasEnableAutoBlendOut,
		OutParsed.bEnableAutoBlendOut);
	if (!Result.bSuccess)
	{
		return Result;
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseReferences(
	const FAssetDocumentFragmentCompiler* Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimMontage* Montage,
	const TSharedRef<FJsonObject>& BodyObject,
	bool bResolveFragments,
	FParsedAnimMontageBody& OutParsed)
{
	const TSharedPtr<FJsonValue>* ReferencesValue = BodyObject->Values.Find(TEXT("References"));
	if (!ReferencesValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TSharedPtr<FJsonObject> ReferencesObject;
	FAssetDocumentCapabilityResult Result = RequireObjectValue(*ReferencesValue, TEXT("/Body/References"), ReferencesObject);
	if (!Result.bSuccess)
	{
		return Result;
	}

	UObject* SkeletonObject = nullptr;
	bool bHasSkeleton = false;
	Result = ParseObjectReferenceFromObject(
		Compiler,
		Context,
		Montage,
		ReferencesObject.ToSharedRef(),
		TEXT("Skeleton"),
		USkeleton::StaticClass(),
		TEXT("/Body/References/Skeleton"),
		bResolveFragments,
		bHasSkeleton,
		SkeletonObject);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (bHasSkeleton)
	{
		OutParsed.bHasSkeleton = true;
		OutParsed.Skeleton = Cast<USkeleton>(SkeletonObject);
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParsePreview(
	const FAssetDocumentFragmentCompiler* Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimMontage* Montage,
	const TSharedRef<FJsonObject>& BodyObject,
	bool bResolveFragments,
	FParsedAnimMontageBody& OutParsed)
{
	const TSharedPtr<FJsonValue>* PreviewValue = BodyObject->Values.Find(TEXT("Preview"));
	if (!PreviewValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TSharedPtr<FJsonObject> PreviewObject;
	FAssetDocumentCapabilityResult Result = RequireObjectValue(*PreviewValue, TEXT("/Body/Preview"), PreviewObject);
	if (!Result.bSuccess)
	{
		return Result;
	}

	UObject* PreviewMeshObject = nullptr;
	bool bHasPreviewMesh = false;
	Result = ParseObjectReferenceFromObject(
		Compiler,
		Context,
		Montage,
		PreviewObject.ToSharedRef(),
		TEXT("PreviewMesh"),
		USkeletalMesh::StaticClass(),
		TEXT("/Body/Preview/PreviewMesh"),
		bResolveFragments,
		bHasPreviewMesh,
		PreviewMeshObject);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (bHasPreviewMesh)
	{
		OutParsed.bHasPreviewMesh = true;
		OutParsed.PreviewMesh = Cast<USkeletalMesh>(PreviewMeshObject);
	}

	UObject* PreviewBasePoseObject = nullptr;
	Result = ParseObjectReferenceFromObject(
		Compiler,
		Context,
		Montage,
		PreviewObject.ToSharedRef(),
		TEXT("PreviewBasePose"),
		UAnimSequence::StaticClass(),
		TEXT("/Body/Preview/PreviewBasePose"),
		bResolveFragments,
		OutParsed.bHasPreviewBasePose,
		PreviewBasePoseObject);
	if (!Result.bSuccess)
	{
		return Result;
	}
	OutParsed.PreviewBasePose = Cast<UAnimSequence>(PreviewBasePoseObject);

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseSync(
	const TSharedRef<FJsonObject>& BodyObject,
	int32 ExistingSlotTrackCount,
	FParsedAnimMontageBody& OutParsed)
{
	const TSharedPtr<FJsonValue>* SyncValue = BodyObject->Values.Find(TEXT("Sync"));
	if (!SyncValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TSharedPtr<FJsonObject> SyncObject;
	FAssetDocumentCapabilityResult Result = RequireObjectValue(*SyncValue, TEXT("/Body/Sync"), SyncObject);
	if (!Result.bSuccess)
	{
		return Result;
	}

	FString SyncGroup;
	Result = ReadOptionalString(SyncObject.ToSharedRef(), TEXT("SyncGroup"), TEXT("/Body/Sync/SyncGroup"), TEXT("InvalidSyncGroup"), OutParsed.bHasSyncGroup, SyncGroup);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (OutParsed.bHasSyncGroup)
	{
		OutParsed.SyncGroup = FName(*SyncGroup.TrimStartAndEnd());
	}

	Result = ReadOptionalInteger(SyncObject.ToSharedRef(), TEXT("SyncSlotIndex"), TEXT("/Body/Sync/SyncSlotIndex"), TEXT("InvalidSyncSlotIndex"), OutParsed.bHasSyncSlotIndex, OutParsed.SyncSlotIndex);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (OutParsed.bHasSyncSlotIndex)
	{
		int32 SlotTrackCount = INDEX_NONE;
		if (OutParsed.bHasSlotAnimTracks)
		{
			SlotTrackCount = OutParsed.SlotAnimTracks.Num();
		}
		else if (ExistingSlotTrackCount != INDEX_NONE)
		{
			SlotTrackCount = ExistingSlotTrackCount;
		}

		if (SlotTrackCount != INDEX_NONE && OutParsed.SyncSlotIndex >= SlotTrackCount)
		{
			return BodyFailure(TEXT("SyncSlotIndex must refer to an existing SlotAnimTracks entry"), TEXT("/Body/Sync/SyncSlotIndex"), TEXT("InvalidSyncSlotIndex"));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseRootMotion(const TSharedRef<FJsonObject>& BodyObject, FParsedAnimMontageBody& OutParsed)
{
	const TSharedPtr<FJsonValue>* RootMotionValue = BodyObject->Values.Find(TEXT("RootMotion"));
	if (!RootMotionValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TSharedPtr<FJsonObject> RootMotionObject;
	FAssetDocumentCapabilityResult Result = RequireObjectValue(*RootMotionValue, TEXT("/Body/RootMotion"), RootMotionObject);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ReadOptionalBool(
		RootMotionObject.ToSharedRef(),
		TEXT("bEnableRootMotionTranslation"),
		TEXT("/Body/RootMotion/bEnableRootMotionTranslation"),
		OutParsed.bHasRootMotionTranslation,
		OutParsed.bRootMotionTranslation);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ReadOptionalBool(
		RootMotionObject.ToSharedRef(),
		TEXT("bEnableRootMotionRotation"),
		TEXT("/Body/RootMotion/bEnableRootMotionRotation"),
		OutParsed.bHasRootMotionRotation,
		OutParsed.bRootMotionRotation);
	if (!Result.bSuccess)
	{
		return Result;
	}

	FString RootLockString;
	Result = ReadOptionalString(
		RootMotionObject.ToSharedRef(),
		TEXT("RootMotionRootLock"),
		TEXT("/Body/RootMotion/RootMotionRootLock"),
		TEXT("InvalidRootMotionRootLock"),
		OutParsed.bHasRootMotionRootLock,
		RootLockString);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (OutParsed.bHasRootMotionRootLock && !TryParseRootMotionRootLock(RootLockString, OutParsed.RootMotionRootLock))
	{
		return BodyFailure(TEXT("RootMotionRootLock must be RefPose, AnimFirstFrame, or Zero"), TEXT("/Body/RootMotion/RootMotionRootLock"), TEXT("InvalidRootMotionRootLock"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseCurveFlags(
	const TSharedRef<FJsonObject>& CurveObject,
	const FString& CurvePath,
	int32& OutFlags)
{
	OutFlags = AACF_DefaultCurve;

	const TSharedPtr<FJsonValue>* FlagsValue = CurveObject->Values.Find(TEXT("Flags"));
	if (!FlagsValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const TArray<TSharedPtr<FJsonValue>>* FlagsArray = nullptr;
	const FAssetDocumentCapabilityResult ArrayResult = RequireArrayValue(*FlagsValue, CurvePath / TEXT("Flags"), FlagsArray);
	if (!ArrayResult.bSuccess)
	{
		return BodyFailure(TEXT("Curve Flags must be an array"), CurvePath / TEXT("Flags"), TEXT("InvalidCurveFlags"));
	}

	OutFlags = 0;
	for (int32 FlagIndex = 0; FlagIndex < FlagsArray->Num(); ++FlagIndex)
	{
		const FString FlagPath = FString::Printf(TEXT("%s/Flags/%d"), *CurvePath, FlagIndex);
		const TSharedPtr<FJsonValue>& FlagValue = (*FlagsArray)[FlagIndex];
		if (!FlagValue.IsValid() || FlagValue->Type != EJson::String)
		{
			return BodyFailure(TEXT("Curve Flags entries must be strings"), FlagPath, TEXT("InvalidCurveFlags"));
		}

		const FString FlagText = FlagValue->AsString();
		if (FlagText == TEXT("Default"))
		{
			OutFlags |= AACF_DefaultCurve;
			continue;
		}

		return BodyFailure(TEXT("Unsupported curve flag"), FlagPath, TEXT("InvalidCurveFlags"));
	}

	if (OutFlags == 0)
	{
		OutFlags = AACF_DefaultCurve;
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseCurves(const TSharedRef<FJsonObject>& BodyObject, FParsedAnimMontageBody& OutParsed)
{
	const TSharedPtr<FJsonValue>* CurvesValue = BodyObject->Values.Find(TEXT("Curves"));
	if (!CurvesValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const TArray<TSharedPtr<FJsonValue>>* CurvesArray = nullptr;
	FAssetDocumentCapabilityResult Result = RequireArrayValue(*CurvesValue, TEXT("/Body/Curves"), CurvesArray);
	if (!Result.bSuccess)
	{
		return Result;
	}

	OutParsed.bHasCurves = true;
	OutParsed.Curves.Reset();
	OutParsed.Curves.Reserve(CurvesArray->Num());
	TSet<FName> CurveNames;

	for (int32 CurveIndex = 0; CurveIndex < CurvesArray->Num(); ++CurveIndex)
	{
		const FString CurvePath = FString::Printf(TEXT("/Body/Curves/%d"), CurveIndex);
		TSharedPtr<FJsonObject> CurveObject;
		Result = RequireObjectValue((*CurvesArray)[CurveIndex], CurvePath, CurveObject);
		if (!Result.bSuccess)
		{
			return Result;
		}

		const TSharedPtr<FJsonValue>* NameValue = CurveObject->Values.Find(TEXT("Name"));
		if (!NameValue || !NameValue->IsValid() || (*NameValue)->Type != EJson::String)
		{
			return BodyFailure(TEXT("Curve Name is required"), CurvePath / TEXT("Name"), TEXT("InvalidCurveName"));
		}

		FString CurveNameString = (*NameValue)->AsString();
		CurveNameString.TrimStartAndEndInline();
		if (CurveNameString.IsEmpty())
		{
			return BodyFailure(TEXT("Curve Name must be non-empty"), CurvePath / TEXT("Name"), TEXT("InvalidCurveName"));
		}

		const TSharedPtr<FJsonValue>* KeysValue = CurveObject->Values.Find(TEXT("Keys"));
		if (!KeysValue || !KeysValue->IsValid() || (*KeysValue)->Type != EJson::Array)
		{
			return BodyFailure(TEXT("Curve Keys are required"), CurvePath / TEXT("Keys"), TEXT("InvalidCurveKeys"));
		}

		const TArray<TSharedPtr<FJsonValue>>& KeysArray = (*KeysValue)->AsArray();
		if (KeysArray.IsEmpty())
		{
			return BodyFailure(TEXT("Curve Keys must be non-empty"), CurvePath / TEXT("Keys"), TEXT("InvalidCurveKeys"));
		}

		FParsedFloatCurve ParsedCurve;
		ParsedCurve.Name = FName(*CurveNameString);
		if (CurveNames.Contains(ParsedCurve.Name))
		{
			return BodyFailure(TEXT("Curve Name must be unique within Body.Curves"), CurvePath / TEXT("Name"), TEXT("DuplicateCurveName"));
		}
		CurveNames.Add(ParsedCurve.Name);

		Result = ParseCurveFlags(CurveObject.ToSharedRef(), CurvePath, ParsedCurve.Flags);
		if (!Result.bSuccess)
		{
			return Result;
		}

		ParsedCurve.Keys.Reserve(KeysArray.Num());
		for (int32 KeyIndex = 0; KeyIndex < KeysArray.Num(); ++KeyIndex)
		{
			const FString KeyPath = FString::Printf(TEXT("%s/Keys/%d"), *CurvePath, KeyIndex);
			TSharedPtr<FJsonObject> KeyObject;
			Result = RequireObjectValue(KeysArray[KeyIndex], KeyPath, KeyObject);
			if (!Result.bSuccess)
			{
				return BodyFailure(TEXT("Curve key must be an object"), KeyPath, TEXT("InvalidCurveKeys"));
			}

			const TSharedPtr<FJsonValue>* TimeValue = KeyObject->Values.Find(TEXT("Time"));
			if (!TimeValue || !TimeValue->IsValid() || (*TimeValue)->Type != EJson::Number)
			{
				return BodyFailure(TEXT("Curve key Time must be numeric"), KeyPath / TEXT("Time"), TEXT("InvalidCurveKeyTime"));
			}
			const double Time = (*TimeValue)->AsNumber();
			if (Time < 0.0)
			{
				return BodyFailure(TEXT("Curve key Time must be non-negative"), KeyPath / TEXT("Time"), TEXT("InvalidCurveKeyTime"));
			}

			const TSharedPtr<FJsonValue>* ValueValue = KeyObject->Values.Find(TEXT("Value"));
			if (!ValueValue || !ValueValue->IsValid() || (*ValueValue)->Type != EJson::Number)
			{
				return BodyFailure(TEXT("Curve key Value must be numeric"), KeyPath / TEXT("Value"), TEXT("InvalidCurveKeyValue"));
			}

			FParsedFloatCurveKey ParsedKey;
			ParsedKey.Time = static_cast<float>(Time);
			ParsedKey.Value = static_cast<float>((*ValueValue)->AsNumber());
			ParsedCurve.Keys.Add(ParsedKey);
		}

		OutParsed.Curves.Add(MoveTemp(ParsedCurve));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseTimeStretch(const TSharedRef<FJsonObject>& BodyObject, FParsedAnimMontageBody& OutParsed)
{
	const TSharedPtr<FJsonValue>* TimeStretchValue = BodyObject->Values.Find(TEXT("TimeStretch"));
	if (!TimeStretchValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TSharedPtr<FJsonObject> TimeStretchObject;
	FAssetDocumentCapabilityResult Result = RequireObjectValue(*TimeStretchValue, TEXT("/Body/TimeStretch"), TimeStretchObject);
	if (!Result.bSuccess)
	{
		return Result;
	}

	OutParsed.bHasTimeStretch = true;

	FString CurveNameString;
	Result = ReadOptionalString(
		TimeStretchObject.ToSharedRef(),
		TEXT("TimeStretchCurveName"),
		TEXT("/Body/TimeStretch/TimeStretchCurveName"),
		TEXT("InvalidTimeStretchCurveName"),
		OutParsed.bHasTimeStretchCurveName,
		CurveNameString);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (OutParsed.bHasTimeStretchCurveName)
	{
		CurveNameString.TrimStartAndEndInline();
		OutParsed.TimeStretchCurveName = FName(*CurveNameString);
		if (!OutParsed.TimeStretchCurveName.IsNone() && OutParsed.bHasCurves)
		{
			const bool bCurveProvided = OutParsed.Curves.ContainsByPredicate([&OutParsed](const FParsedFloatCurve& Curve)
			{
				return Curve.Name == OutParsed.TimeStretchCurveName;
			});
			if (!bCurveProvided)
			{
				return BodyFailure(TEXT("TimeStretchCurveName must reference a montage-owned float curve"), TEXT("/Body/TimeStretch/TimeStretchCurveName"), TEXT("MissingTimeStretchCurve"));
			}
		}
	}

	const TSharedPtr<FJsonValue>* SamplingRateValue = TimeStretchObject->Values.Find(TEXT("SamplingRate"));
	if (SamplingRateValue)
	{
		if (!SamplingRateValue->IsValid() || (*SamplingRateValue)->Type != EJson::Number)
		{
			return BodyFailure(TEXT("SamplingRate must be numeric"), TEXT("/Body/TimeStretch/SamplingRate"), TEXT("InvalidTimeStretchSamplingRate"));
		}
		const double SamplingRate = (*SamplingRateValue)->AsNumber();
		if (SamplingRate <= 0.0)
		{
			return BodyFailure(TEXT("SamplingRate must be greater than zero"), TEXT("/Body/TimeStretch/SamplingRate"), TEXT("InvalidTimeStretchSamplingRate"));
		}
		OutParsed.bHasTimeStretchSamplingRate = true;
		OutParsed.TimeStretchSamplingRate = static_cast<float>(SamplingRate);
	}

	const TSharedPtr<FJsonValue>* PrecisionValue = TimeStretchObject->Values.Find(TEXT("CurveValueMinPrecision"));
	if (PrecisionValue)
	{
		if (!PrecisionValue->IsValid() || (*PrecisionValue)->Type != EJson::Number)
		{
			return BodyFailure(TEXT("CurveValueMinPrecision must be numeric"), TEXT("/Body/TimeStretch/CurveValueMinPrecision"), TEXT("InvalidTimeStretchCurveValueMinPrecision"));
		}
		const double Precision = (*PrecisionValue)->AsNumber();
		if (Precision < 0.0)
		{
			return BodyFailure(TEXT("CurveValueMinPrecision must be non-negative"), TEXT("/Body/TimeStretch/CurveValueMinPrecision"), TEXT("InvalidTimeStretchCurveValueMinPrecision"));
		}
		OutParsed.bHasTimeStretchCurveValueMinPrecision = true;
		OutParsed.TimeStretchCurveValueMinPrecision = static_cast<float>(Precision);
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateMetadataArrayShape(
	const TArray<TSharedPtr<FJsonValue>>& Values,
	const FString& BasePath)
{
	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		const FString Path = FString::Printf(TEXT("%s/%d"), *BasePath, Index);
		TSharedPtr<FJsonObject> FragmentObject;
		const FAssetDocumentCapabilityResult ObjectResult = RequireObjectValue(Values[Index], Path, FragmentObject);
		if (!ObjectResult.bSuccess)
		{
			return ObjectResult;
		}

		FString Kind;
		if (!FragmentObject->TryGetStringField(TEXT("Kind"), Kind) || Kind.TrimStartAndEnd().IsEmpty())
		{
			return BodyFailure(TEXT("Metadata fragment requires Kind"), Path / TEXT("Kind"), TEXT("MissingMetadataObjectKind"));
		}
		Kind.TrimStartAndEndInline();
		if (Kind != TEXT("EmbeddedObject") && Kind != TEXT("DefinitionRef"))
		{
			return BodyFailure(TEXT("Metadata fragment must be EmbeddedObject or DefinitionRef"), Path / TEXT("Kind"), TEXT("InvalidMetadataObjectFragment"));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateMetadataObjectProducingFragment(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& ObjectFragment,
	const FString& Path,
	TSet<FString>& DefinitionStack)
{
	FString Kind;
	if (!ObjectFragment->TryGetStringField(TEXT("Kind"), Kind) || Kind.TrimStartAndEnd().IsEmpty())
	{
		return BodyFailure(TEXT("Metadata fragment requires Kind"), Path, TEXT("missing-fragment-kind"));
	}
	Kind.TrimStartAndEndInline();

	if (Kind == TEXT("EmbeddedObject"))
	{
		FString ClassName;
		if (!ObjectFragment->TryGetStringField(TEXT("Class"), ClassName) || ClassName.TrimStartAndEnd().IsEmpty())
		{
			return BodyFailure(TEXT("EmbeddedObject field 'Class' is required."), Path, TEXT("missing-embeddedobject-class"));
		}

		UClass* ResolvedClass = nullptr;
		FString Error;
		if (!FAssetDocumentClassResolver::ResolveClass(ClassName, ResolvedClass, Error))
		{
			return BodyFailure(Error, Path, TEXT("object-class-resolve-failed"));
		}

		if (!ResolvedClass->IsChildOf(UAnimMetaData::StaticClass()))
		{
			return BodyFailure(
				FString::Printf(TEXT("Class '%s' is not a child of '%s'."), *ResolvedClass->GetName(), *UAnimMetaData::StaticClass()->GetName()),
				Path,
				TEXT("embeddedobject-base-class-mismatch"));
		}

		const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
		if (ObjectFragment->TryGetObjectField(TEXT("Properties"), PropertiesPtr) && PropertiesPtr && PropertiesPtr->IsValid())
		{
			const FAssetDocumentPropertyApplyResult PreflightResult = FAssetDocumentPropertyAdapter::PreflightProperties(ResolvedClass, *PropertiesPtr);
			if (!PreflightResult.bSuccess)
			{
				FAssetDocumentCapabilityResult Failure = BodyFailure(PreflightResult.Message, Path, TEXT("embeddedobject-preflight-failed"));
				Failure.Diagnostics.Append(PreflightResult.Diagnostics);
				return Failure;
			}
		}

		return FAssetDocumentCapabilityResult::Success();
	}

	if (Kind == TEXT("DefinitionRef"))
	{
		FString Id;
		if (!ObjectFragment->TryGetStringField(TEXT("Id"), Id) || Id.TrimStartAndEnd().IsEmpty())
		{
			return BodyFailure(TEXT("DefinitionRef field 'Id' is required."), Path, TEXT("missing-definitionref-id"));
		}
		Id.TrimStartAndEndInline();

		if (!Context.Definitions || !Context.Definitions->IsValid())
		{
			return BodyFailure(TEXT("DefinitionRef requires Definitions."), Path, TEXT("definitionref-missing-definitions"));
		}

		if (DefinitionStack.Contains(Id))
		{
			return BodyFailure(FString::Printf(TEXT("DefinitionRef cycle detected at '%s'."), *Id), Path, TEXT("definitionref-cycle"));
		}

		const TSharedPtr<FJsonObject>* DefinitionJson = nullptr;
		if (!(*Context.Definitions)->TryGetObjectField(Id, DefinitionJson) || !DefinitionJson || !DefinitionJson->IsValid())
		{
			return BodyFailure(FString::Printf(TEXT("Definition '%s' was not found."), *Id), Path, TEXT("definitionref-missing-id"));
		}

		DefinitionStack.Add(Id);
		const FAssetDocumentCapabilityResult Result = ValidateMetadataObjectProducingFragment(Context, DefinitionJson->ToSharedRef(), Path, DefinitionStack);
		DefinitionStack.Remove(Id);
		return Result;
	}

	return BodyFailure(TEXT("Metadata fragment must be EmbeddedObject or DefinitionRef"), Path, TEXT("InvalidMetadataObjectFragment"));
}

FAssetDocumentCapabilityResult ValidateMetadataArray(
	const FAssetDocumentCapabilityContext& Context,
	const TArray<TSharedPtr<FJsonValue>>& Values,
	const FString& BasePath)
{
	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		const FString Path = FString::Printf(TEXT("%s/%d"), *BasePath, Index);
		TSharedPtr<FJsonObject> FragmentObject;
		const FAssetDocumentCapabilityResult ObjectResult = RequireObjectValue(Values[Index], Path, FragmentObject);
		if (!ObjectResult.bSuccess)
		{
			return ObjectResult;
		}

		TSet<FString> DefinitionStack;
		const FAssetDocumentCapabilityResult Result = ValidateMetadataObjectProducingFragment(Context, FragmentObject.ToSharedRef(), Path, DefinitionStack);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentFragmentArrayRegionConfig MakeAnimMontageMetadataFragmentArrayConfig()
{
	FAssetDocumentFragmentArrayRegionConfig Config;
	Config.AdapterName = TEXT("AnimMontageMetadataFragmentArray");
	Config.RegionId = TEXT("Body.Metadata");
	Config.BodyPath = TEXT("Body.Metadata");
	Config.JsonPointer = TEXT("/Body/Metadata");
	Config.SchemaLabel = TEXT("AnimMontage Metadata");
	return Config;
}

FAssetDocumentRegionContext MakeAnimMontageMetadataFragmentArrayRegionContext(
	const FAssetDocumentCapabilityContext& Context)
{
	FAssetDocumentRegionContext RegionContext;
	RegionContext.Asset = Context.Asset;
	RegionContext.AssetClass = Context.AssetClass;
	RegionContext.TargetAssetPath = Context.TargetAssetPath;
	RegionContext.SourceDocumentPath = Context.SourceDocumentPath;
	RegionContext.Definitions = Context.Definitions;
	RegionContext.bIsDryRun = Context.bIsDryRun;
	RegionContext.Result = Context.Result;
	RegionContext.RegionId = TEXT("Body.Metadata");
	RegionContext.BodyPath = TEXT("Body.Metadata");
	RegionContext.JsonPointer = TEXT("/Body/Metadata");
	return RegionContext;
}

TArray<TSharedPtr<FJsonValue>> FragmentArrayEntriesToValues(
	const TArray<FAssetDocumentFragmentArrayEntry>& Entries)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Entries.Num());
	for (const FAssetDocumentFragmentArrayEntry& Entry : Entries)
	{
		Values.Add(MakeShared<FJsonValueObject>(Entry.FragmentObject));
	}
	return Values;
}

void CollectSectionNames(const TArray<FCompositeSection>& Sections, TSet<FName>& OutSectionNames)
{
	for (const FCompositeSection& Section : Sections)
	{
		if (!Section.SectionName.IsNone())
		{
			OutSectionNames.Add(Section.SectionName);
		}
	}
}

FAssetDocumentCapabilityResult ParseMetadataRegions(
	const FAssetDocumentFragmentCompiler* Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimMontage* Montage,
	const TSharedRef<FJsonObject>& BodyObject,
	bool bResolveFragments,
	FParsedAnimMontageBody& OutParsed)
{
	const TSharedPtr<FJsonValue>* MetadataValue = BodyObject->Values.Find(TEXT("Metadata"));

	const TSharedPtr<FJsonValue>* SectionMetadataValue = BodyObject->Values.Find(TEXT("SectionMetadata"));
	TSharedPtr<FJsonObject> SectionMetadataObject;
	TMap<FName, const TArray<TSharedPtr<FJsonValue>>*> SectionMetadataArrays;
	if (SectionMetadataValue)
	{
		FAssetDocumentCapabilityResult Result = RequireObjectValue(*SectionMetadataValue, TEXT("/Body/SectionMetadata"), SectionMetadataObject);
		if (!Result.bSuccess)
		{
			return Result;
		}

		TSet<FName> SectionNames;
		if (OutParsed.bHasCompositeSections)
		{
			CollectSectionNames(OutParsed.CompositeSections, SectionNames);
		}
		else if (Montage)
		{
			CollectSectionNames(Montage->CompositeSections, SectionNames);
		}
		const bool bCanValidateSectionTargets = OutParsed.bHasCompositeSections || Montage != nullptr;

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : SectionMetadataObject->Values)
		{
			const FString SectionPath = FString::Printf(TEXT("/Body/SectionMetadata/%s"), *Pair.Key);
			const FName SectionName(*Pair.Key);
			if (bCanValidateSectionTargets && !SectionNames.Contains(SectionName))
			{
				return BodyFailure(
					FString::Printf(TEXT("SectionMetadata target '%s' does not match a CompositeSection"), *Pair.Key),
					SectionPath,
					TEXT("UnknownSectionMetadataTarget"));
			}

			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			Result = RequireArrayValue(Pair.Value, SectionPath, Values);
			if (!Result.bSuccess)
			{
				return Result;
			}

			Result = bResolveFragments
				? ValidateMetadataArrayShape(*Values, SectionPath)
				: ValidateMetadataArray(Context, *Values, SectionPath);
			if (!Result.bSuccess)
			{
				return Result;
			}

			SectionMetadataArrays.Add(SectionName, Values);
		}
	}

	if (!bResolveFragments)
	{
		if (MetadataValue)
		{
			FAssetDocumentFragmentArrayHooks MetadataHooks;
			MetadataHooks.Validate = [&Context](
				const FAssetDocumentRegionContext&,
				const TArray<FAssetDocumentFragmentArrayEntry>& Entries)
			{
				return ValidateMetadataArray(Context, FragmentArrayEntriesToValues(Entries), TEXT("/Body/Metadata"));
			};

			const FAssetDocumentFragmentArrayRegionAdapter MetadataAdapter(
				MakeAnimMontageMetadataFragmentArrayConfig(),
				MoveTemp(MetadataHooks));
			const FAssetDocumentCapabilityResult Result = MetadataAdapter.ValidateRegion(
				MakeAnimMontageMetadataFragmentArrayRegionContext(Context),
				*MetadataValue);
			if (!Result.bSuccess)
			{
				return Result;
			}
		}

		OutParsed.bHasMetadata = MetadataValue != nullptr;
		OutParsed.bHasSectionMetadata = SectionMetadataValue != nullptr;
		return FAssetDocumentCapabilityResult::Success();
	}

	if (!Compiler || !Montage)
	{
		return BodyFailure(TEXT("Metadata fragment resolution requires an AnimMontage asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	UObject* MetadataStagingOuter = nullptr;
	if (MetadataValue || SectionMetadataValue)
	{
		MetadataStagingOuter = NewObject<UAnimMontage>(GetTransientPackage(), UAnimMontage::StaticClass(), NAME_None, RF_Transient);
	}

	if (MetadataValue)
	{
		FAssetDocumentFragmentArrayHooks MetadataHooks;
		MetadataHooks.Validate = [Compiler, &Context, Montage, MetadataStagingOuter, &OutParsed](
			const FAssetDocumentRegionContext&,
			const TArray<FAssetDocumentFragmentArrayEntry>& Entries)
		{
			const TArray<TSharedPtr<FJsonValue>> MetadataValues = FragmentArrayEntriesToValues(Entries);
			FAssetDocumentCapabilityResult Result = ValidateMetadataArrayShape(MetadataValues, TEXT("/Body/Metadata"));
			if (!Result.bSuccess)
			{
				return Result;
			}

			Result = CompileMetadataArray(*Compiler, Context, Montage, MetadataStagingOuter, MetadataValues, TEXT("/Body/Metadata"), OutParsed.Metadata);
			if (Result.bSuccess)
			{
				OutParsed.bHasMetadata = true;
			}
			return Result;
		};

		const FAssetDocumentFragmentArrayRegionAdapter MetadataAdapter(
			MakeAnimMontageMetadataFragmentArrayConfig(),
			MoveTemp(MetadataHooks));
		const FAssetDocumentCapabilityResult Result = MetadataAdapter.ValidateRegion(
			MakeAnimMontageMetadataFragmentArrayRegionContext(Context),
			*MetadataValue);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	if (SectionMetadataValue)
	{
		OutParsed.bHasSectionMetadata = true;
		for (const TPair<FName, const TArray<TSharedPtr<FJsonValue>>*>& Pair : SectionMetadataArrays)
		{
			TArray<TObjectPtr<UAnimMetaData>> CompiledMetadata;
			FAssetDocumentCapabilityResult Result = CompileMetadataArray(
				*Compiler,
				Context,
				Montage,
				MetadataStagingOuter,
				*Pair.Value,
				FString::Printf(TEXT("/Body/SectionMetadata/%s"), *Pair.Key.ToString()),
				CompiledMetadata);
			if (!Result.bSuccess)
			{
				return Result;
			}
			OutParsed.SectionMetadataByName.Add(Pair.Key, MoveTemp(CompiledMetadata));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseAnimMontageBody(
	const FAssetDocumentFragmentCompiler* Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimMontage* Montage,
	const TSharedRef<FJsonObject>& BodyObject,
	bool bResolveFragments,
	int32 ExistingSlotTrackCount,
	FParsedAnimMontageBody& OutParsed)
{
	FAssetDocumentCapabilityResult Result = ValidateBodyObjectShape(BodyObject);
	if (!Result.bSuccess)
	{
		return Result;
	}

	UObject* SkeletonObject = nullptr;
	Result = ParseObjectReference(Compiler, Context, Montage, BodyObject, TEXT("Skeleton"), USkeleton::StaticClass(), bResolveFragments, OutParsed.bHasSkeleton, SkeletonObject);
	if (!Result.bSuccess)
	{
		return Result;
	}
	OutParsed.Skeleton = Cast<USkeleton>(SkeletonObject);

	UObject* PreviewMeshObject = nullptr;
	Result = ParseObjectReference(Compiler, Context, Montage, BodyObject, TEXT("PreviewMesh"), USkeletalMesh::StaticClass(), bResolveFragments, OutParsed.bHasPreviewMesh, PreviewMeshObject);
	if (!Result.bSuccess)
	{
		return Result;
	}
	OutParsed.PreviewMesh = Cast<USkeletalMesh>(PreviewMeshObject);

	Result = ParseReferences(Compiler, Context, Montage, BodyObject, bResolveFragments, OutParsed);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ParsePreview(Compiler, Context, Montage, BodyObject, bResolveFragments, OutParsed);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ParseSlotAnimTracks(Compiler, Context, Montage, BodyObject, bResolveFragments, OutParsed);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ParseCompositeSections(BodyObject, OutParsed);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ParseSync(BodyObject, ExistingSlotTrackCount, OutParsed);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ParseRootMotion(BodyObject, OutParsed);
	if (!Result.bSuccess)
	{
		return Result;
	}

	FAnimMontageNotifyPlacementAdapter NotifyPlacementAdapter;
	if (bResolveFragments)
	{
		Result = NotifyPlacementAdapter.Compile(*Compiler, Context, Montage, BodyObject, OutParsed.NotifyPlacements);
	}
	else
	{
		Result = NotifyPlacementAdapter.Validate(Context, BodyObject);
	}
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ParseBlend(BodyObject, OutParsed);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ParseCurves(BodyObject, OutParsed);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ParseTimeStretch(BodyObject, OutParsed);
	if (!Result.bSuccess)
	{
		return Result;
	}

	return ParseMetadataRegions(Compiler, Context, Montage, BodyObject, bResolveFragments, OutParsed);
}

FAssetDocumentCapabilityResult ExtractAssetRef(
	const FAssetDocumentFragmentCompiler& Compiler,
	UObject* OwnerAsset,
	UObject* ValueObject,
	const FString& JsonPath,
	TSharedRef<FJsonObject>& OutFragment)
{
	FAssetDocumentFragmentExtractContext ExtractContext;
	ExtractContext.OwnerAsset = OwnerAsset;
	ExtractContext.ValueObject = ValueObject;
	ExtractContext.Kind = TEXT("AssetRef");
	ExtractContext.JsonPath = JsonPath;

	const FAssetDocumentFragmentResult FragmentResult = Compiler.Extract(ExtractContext, OutFragment);
	return FragmentResult.bSuccess ? FAssetDocumentCapabilityResult::Success() : FragmentFailure(FragmentResult);
}

FAssetDocumentCapabilityResult ExtractMetadataObject(
	const FAssetDocumentFragmentCompiler& Compiler,
	UObject* OwnerAsset,
	UAnimMetaData* MetadataObject,
	const FString& JsonPath,
	TSharedRef<FJsonObject>& OutFragment)
{
	FAssetDocumentFragmentExtractContext ExtractContext;
	ExtractContext.OwnerAsset = OwnerAsset;
	ExtractContext.ValueObject = MetadataObject;
	ExtractContext.Kind = TEXT("EmbeddedObject");
	ExtractContext.JsonPath = JsonPath;

	const FAssetDocumentFragmentResult FragmentResult = Compiler.Extract(ExtractContext, OutFragment);
	return FragmentResult.bSuccess ? FAssetDocumentCapabilityResult::Success() : FragmentFailure(FragmentResult);
}

FAssetDocumentCapabilityResult ExtractMetadataFragmentArrayRegion(
	const FAssetDocumentFragmentCompiler& Compiler,
	const FAssetDocumentCapabilityContext& Context,
	const UAnimMontage* Montage,
	TSharedPtr<FJsonValue>& OutValue)
{
	FAssetDocumentFragmentArrayHooks Hooks;
	Hooks.Extract = [&Compiler, Montage](
		const FAssetDocumentRegionContext&,
		TArray<TSharedRef<FJsonObject>>& OutEntries)
	{
		OutEntries.Reset();
		const TArray<UAnimMetaData*>& AssetMetadata = Montage->GetMetaData();
		OutEntries.Reserve(AssetMetadata.Num());
		for (int32 MetadataIndex = 0; MetadataIndex < AssetMetadata.Num(); ++MetadataIndex)
		{
			UAnimMetaData* MetadataObject = AssetMetadata[MetadataIndex];
			if (!MetadataObject)
			{
				continue;
			}

			TSharedRef<FJsonObject> MetadataFragment = MakeShared<FJsonObject>();
			FAssetDocumentCapabilityResult Result = ExtractMetadataObject(
				Compiler,
				const_cast<UAnimMontage*>(Montage),
				MetadataObject,
				FString::Printf(TEXT("/Body/Metadata/%d"), MetadataIndex),
				MetadataFragment);
			if (!Result.bSuccess)
			{
				return Result;
			}
			OutEntries.Add(MetadataFragment);
		}

		return FAssetDocumentCapabilityResult::Success(TEXT("Extracted AnimMontage Metadata fragments"));
	};

	const FAssetDocumentFragmentArrayRegionAdapter Adapter(
		MakeAnimMontageMetadataFragmentArrayConfig(),
		MoveTemp(Hooks));
	return Adapter.ExtractRegion(MakeAnimMontageMetadataFragmentArrayRegionContext(Context), OutValue);
}

FFloatProperty* FindTimeStretchFloatProperty(const FName PropertyName)
{
	UScriptStruct* TimeStretchStruct = FTimeStretchCurve::StaticStruct();
	return TimeStretchStruct ? CastField<FFloatProperty>(TimeStretchStruct->FindPropertyByName(PropertyName)) : nullptr;
}

FAssetDocumentCapabilityResult SetTimeStretchFloatProperty(
	FTimeStretchCurve& TimeStretchCurve,
	const FName PropertyName,
	float Value,
	const FString& Path,
	const TCHAR* ErrorCode)
{
	FFloatProperty* Property = FindTimeStretchFloatProperty(PropertyName);
	if (!Property)
	{
		return BodyFailure(FString::Printf(TEXT("FTimeStretchCurve property '%s' was not found"), *PropertyName.ToString()), Path, ErrorCode);
	}

	Property->SetPropertyValue_InContainer(&TimeStretchCurve, Value);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult GetTimeStretchFloatProperty(
	const FTimeStretchCurve& TimeStretchCurve,
	const FName PropertyName,
	float& OutValue,
	const FString& Path,
	const TCHAR* ErrorCode)
{
	FFloatProperty* Property = FindTimeStretchFloatProperty(PropertyName);
	if (!Property)
	{
		return BodyFailure(FString::Printf(TEXT("FTimeStretchCurve property '%s' was not found"), *PropertyName.ToString()), Path, ErrorCode);
	}

	OutValue = Property->GetPropertyValue_InContainer(&TimeStretchCurve);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyMontageOwnedCurves(UAnimMontage& Montage, const TArray<FParsedFloatCurve>& Curves)
{
	IAnimationDataController& Controller = Montage.GetController();
	IAnimationDataController::FScopedBracket Bracket(Controller, FText::FromString(TEXT("Apply AssetDocument Montage Curves")), false);

	Controller.RemoveAllCurvesOfType(ERawCurveTrackTypes::RCT_Float, false);
	for (const FParsedFloatCurve& Curve : Curves)
	{
		const FAnimationCurveIdentifier CurveId(Curve.Name, ERawCurveTrackTypes::RCT_Float);
		if (!Controller.AddCurve(CurveId, Curve.Flags, false))
		{
			return BodyFailure(FString::Printf(TEXT("Failed to add montage curve '%s'"), *Curve.Name.ToString()), TEXT("/Body/Curves"), TEXT("CurveApplyFailed"));
		}

		TArray<FParsedFloatCurveKey> SortedKeys = Curve.Keys;
		SortedKeys.Sort([](const FParsedFloatCurveKey& Left, const FParsedFloatCurveKey& Right)
		{
			return Left.Time < Right.Time;
		});

		TArray<FRichCurveKey> RichKeys;
		RichKeys.Reserve(SortedKeys.Num());
		for (const FParsedFloatCurveKey& Key : SortedKeys)
		{
			RichKeys.Add(FRichCurveKey(Key.Time, Key.Value));
		}

		if (!Controller.SetCurveKeys(CurveId, RichKeys, false))
		{
			return BodyFailure(FString::Printf(TEXT("Failed to set montage curve keys for '%s'"), *Curve.Name.ToString()), TEXT("/Body/Curves"), TEXT("CurveApplyFailed"));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

bool HasMontageFloatCurve(const UAnimMontage& Montage, FName CurveName)
{
	if (CurveName.IsNone())
	{
		return true;
	}

	const IAnimationDataModel* DataModel = Montage.GetDataModel();
	return DataModel && DataModel->FindFloatCurve(FAnimationCurveIdentifier(CurveName, ERawCurveTrackTypes::RCT_Float)) != nullptr;
}

FAssetDocumentCapabilityResult ValidateTimeStretchCurveReferenceBeforeMutation(const UAnimMontage& Montage, const FParsedAnimMontageBody& ParsedBody)
{
	if (!ParsedBody.bHasTimeStretchCurveName || ParsedBody.TimeStretchCurveName.IsNone())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	if (ParsedBody.bHasCurves)
	{
		const bool bCurveProvided = ParsedBody.Curves.ContainsByPredicate([&ParsedBody](const FParsedFloatCurve& Curve)
		{
			return Curve.Name == ParsedBody.TimeStretchCurveName;
		});
		if (bCurveProvided)
		{
			return FAssetDocumentCapabilityResult::Success();
		}
	}
	else if (HasMontageFloatCurve(Montage, ParsedBody.TimeStretchCurveName))
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	return BodyFailure(TEXT("TimeStretchCurveName must reference a montage-owned float curve"), TEXT("/Body/TimeStretch/TimeStretchCurveName"), TEXT("MissingTimeStretchCurve"));
}

void BakeMontageTimeStretchCurve(UAnimMontage& Montage)
{
	Montage.TimeStretchCurve.Reset();

	if (Montage.TimeStretchCurveName.IsNone())
	{
		return;
	}

	const IAnimationDataModel* DataModel = Montage.GetDataModel();
	const FFloatCurve* TimeStretchFloatCurve = DataModel
		? DataModel->FindFloatCurve(FAnimationCurveIdentifier(Montage.TimeStretchCurveName, ERawCurveTrackTypes::RCT_Float))
		: nullptr;
	if (!TimeStretchFloatCurve)
	{
		return;
	}

	Montage.TimeStretchCurve.BakeFromFloatCurve(*TimeStretchFloatCurve, Montage.GetPlayLength());
}

FAssetDocumentCapabilityResult ExtractCurveFlags(
	const FFloatCurve& Curve,
	const FString& Path,
	TArray<TSharedPtr<FJsonValue>>& OutFlags)
{
	OutFlags.Reset();
	if (Curve.GetCurveTypeFlags() != AACF_DefaultCurve)
	{
		return BodyFailure(
			FString::Printf(TEXT("Curve '%s' has unsupported flags 0x%x"), *Curve.GetName().ToString(), Curve.GetCurveTypeFlags()),
			Path,
			TEXT("UnsupportedCurveFlags"));
	}

	OutFlags.Add(MakeShared<FJsonValueString>(TEXT("Default")));
	return FAssetDocumentCapabilityResult::Success();
}
}

const TArray<FName>& FAnimMontageAssetDocumentCapability::GetCanonicalBodyKeys()
{
	static const TArray<FName> Keys = {
		TEXT("Skeleton"),
		TEXT("PreviewMesh"),
		TEXT("References"),
		TEXT("Preview"),
		TEXT("Sync"),
		TEXT("RootMotion"),
		TEXT("Metadata"),
		TEXT("SectionMetadata"),
		TEXT("TimeStretch"),
		TEXT("Curves"),
		TEXT("SlotAnimTracks"),
		TEXT("CompositeSections"),
		TEXT("Notifies"),
		TEXT("NotifyStates"),
		TEXT("Blend"),
	};
	return Keys;
}

FName FAnimMontageAssetDocumentCapability::GetName() const
{
	return TEXT("AnimMontageBody");
}

TArray<FName> FAnimMontageAssetDocumentCapability::GetInternalAdapterNames() const
{
	return {
		TEXT("AnimMontageBody"),
		TEXT("AnimMontageNotifyPlacementAdapter"),
	};
}

int32 FAnimMontageAssetDocumentCapability::GetApplyOrder() const
{
	return 0;
}

bool FAnimMontageAssetDocumentCapability::SupportsAsset(const UObject* Asset) const
{
	return Asset && Asset->IsA<UAnimMontage>();
}

bool FAnimMontageAssetDocumentCapability::SupportsClass(const UClass* AssetClass) const
{
	return AssetClass == UAnimMontage::StaticClass();
}

TSharedRef<FJsonObject> FAnimMontageAssetDocumentCapability::GetSchemaHint() const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Skeleton"), TEXT("null | AssetRef<USkeleton>"));
	Schema->SetStringField(TEXT("PreviewMesh"), TEXT("null | AssetRef<USkeletalMesh>"));
	Schema->SetStringField(TEXT("References"), TEXT("object"));
	Schema->SetStringField(TEXT("Preview"), TEXT("object"));
	Schema->SetStringField(TEXT("Sync"), TEXT("object"));
	Schema->SetStringField(TEXT("RootMotion"), TEXT("object"));
	Schema->SetStringField(TEXT("Metadata"), TEXT("array<EmbeddedObject|DefinitionRef>"));
	Schema->SetStringField(TEXT("SectionMetadata"), TEXT("map<SectionName,array<EmbeddedObject|DefinitionRef>>"));
	Schema->SetStringField(TEXT("TimeStretch"), TEXT("object"));
	Schema->SetStringField(TEXT("Curves"), TEXT("array<FloatCurve>"));
	Schema->SetStringField(TEXT("SlotAnimTracks"), TEXT("array<SlotAnimTrack>"));
	Schema->SetStringField(TEXT("CompositeSections"), TEXT("array<CompositeSection>"));
	Schema->SetStringField(TEXT("Notifies"), TEXT("array<AnimNotifyPlacement>"));
	Schema->SetStringField(TEXT("NotifyStates"), TEXT("array<AnimNotifyStatePlacement>"));
	Schema->SetStringField(TEXT("Blend"), TEXT("object"));
	return Schema;
}

FAssetDocumentCapabilityResult FAnimMontageAssetDocumentCapability::Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	if (!SupportsClass(Context.AssetClass))
	{
		return BodyFailure(TEXT("AnimMontage body validation requires UAnimMontage class"), TEXT("/Class"), TEXT("UnsupportedClass"));
	}

	if (BodyJson->Type != EJson::Object)
	{
		return BodyFailure(TEXT("Body must be a JSON object"), TEXT("/Body"), TEXT("InvalidBodyType"));
	}

	const TSharedPtr<FJsonObject> BodyObject = BodyJson->AsObject();
	if (!BodyObject.IsValid())
	{
		return BodyFailure(TEXT("Body must be a JSON object"), TEXT("/Body"), TEXT("InvalidBodyType"));
	}

	return ValidateBodyObject(Context, BodyObject.ToSharedRef());
}

FAssetDocumentCapabilityResult FAnimMontageAssetDocumentCapability::Preflight(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	UAnimMontage* Montage = Cast<UAnimMontage>(Context.Asset);
	if (!Montage)
	{
		return BodyFailure(TEXT("AnimMontage body preflight requires UAnimMontage asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	if (BodyJson->Type != EJson::Object)
	{
		return BodyFailure(TEXT("Body must be a JSON object"), TEXT("/Body"), TEXT("InvalidBodyType"));
	}

	const TSharedPtr<FJsonObject> BodyObject = BodyJson->AsObject();
	if (!BodyObject.IsValid())
	{
		return BodyFailure(TEXT("Body must be a JSON object"), TEXT("/Body"), TEXT("InvalidBodyType"));
	}

	FAssetDocumentFragmentCompiler Compiler;
	Compiler.RegisterBuiltInAdapters();

	UAnimMontage* PreflightMontage = NewObject<UAnimMontage>(GetTransientPackage(), UAnimMontage::StaticClass(), NAME_None, RF_Transient);
	PreflightMontage->CompositeSections = Montage->CompositeSections;
	FAssetDocumentCapabilityContext PreflightContext = Context;
	PreflightContext.Asset = PreflightMontage;
	PreflightContext.AssetClass = UAnimMontage::StaticClass();

	FParsedAnimMontageBody ParsedBody;
	return ParseAnimMontageBody(&Compiler, PreflightContext, PreflightMontage, BodyObject.ToSharedRef(), true, Montage->SlotAnimTracks.Num(), ParsedBody);
}

FAssetDocumentCapabilityResult FAnimMontageAssetDocumentCapability::Apply(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson)
{
	UAnimMontage* Montage = Cast<UAnimMontage>(Context.Asset);
	if (!Montage)
	{
		return BodyFailure(TEXT("AnimMontage body apply requires UAnimMontage asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	if (BodyJson->Type != EJson::Object)
	{
		return BodyFailure(TEXT("Body must be a JSON object"), TEXT("/Body"), TEXT("InvalidBodyType"));
	}

	const TSharedPtr<FJsonObject> BodyObject = BodyJson->AsObject();
	if (!BodyObject.IsValid())
	{
		return BodyFailure(TEXT("Body must be a JSON object"), TEXT("/Body"), TEXT("InvalidBodyType"));
	}

	FAssetDocumentFragmentCompiler Compiler;
	Compiler.RegisterBuiltInAdapters();

	FParsedAnimMontageBody ParsedBody;
	const FAssetDocumentCapabilityResult Result = ParseAnimMontageBody(&Compiler, Context, Montage, BodyObject.ToSharedRef(), true, Montage->SlotAnimTracks.Num(), ParsedBody);
	if (!Result.bSuccess)
	{
		return Result;
	}

	const FAssetDocumentCapabilityResult TimeStretchPreflightResult = ValidateTimeStretchCurveReferenceBeforeMutation(*Montage, ParsedBody);
	if (!TimeStretchPreflightResult.bSuccess)
	{
		return TimeStretchPreflightResult;
	}

	if (ParsedBody.bHasMetadata)
	{
		const FAssetDocumentCapabilityResult MoveResult = MoveMetadataArrayToMontage(ParsedBody.Metadata, Montage, TEXT("/Body/Metadata"));
		if (!MoveResult.bSuccess)
		{
			return MoveResult;
		}
	}
	if (ParsedBody.bHasSectionMetadata)
	{
		for (TPair<FName, TArray<TObjectPtr<UAnimMetaData>>>& Pair : ParsedBody.SectionMetadataByName)
		{
			const FAssetDocumentCapabilityResult MoveResult = MoveMetadataArrayToMontage(
				Pair.Value,
				Montage,
				FString::Printf(TEXT("/Body/SectionMetadata/%s"), *Pair.Key.ToString()));
			if (!MoveResult.bSuccess)
			{
				return MoveResult;
			}
		}
	}

	if (ParsedBody.bHasSkeleton)
	{
		Montage->SetSkeleton(ParsedBody.Skeleton);
	}
	if (ParsedBody.bHasPreviewMesh)
	{
		Montage->SetPreviewMesh(ParsedBody.PreviewMesh, false);
	}
	if (ParsedBody.bHasPreviewBasePose)
	{
		Montage->PreviewBasePose = ParsedBody.PreviewBasePose;
	}
	bool bShouldCollectMarkers = false;
	if (ParsedBody.bHasSlotAnimTracks)
	{
		Montage->SlotAnimTracks = MoveTemp(ParsedBody.SlotAnimTracks);
		Montage->SetCompositeLength(ParsedBody.CompositeLength);
		bShouldCollectMarkers = true;
	}
	if (ParsedBody.bHasCompositeSections)
	{
		Montage->CompositeSections = MoveTemp(ParsedBody.CompositeSections);
	}
	if (ParsedBody.NotifyPlacements.bHasNotifies || ParsedBody.NotifyPlacements.bHasNotifyStates)
	{
		TArray<FAnimNotifyEvent> UpdatedNotifies;
		UpdatedNotifies.Reserve(Montage->Notifies.Num() + ParsedBody.NotifyPlacements.Notifies.Num() + ParsedBody.NotifyPlacements.NotifyStates.Num());
		for (const FAnimNotifyEvent& ExistingNotify : Montage->Notifies)
		{
			if (ParsedBody.NotifyPlacements.bHasNotifies && FAnimMontageNotifyPlacementAdapter::IsManagedNotifyEvent(ExistingNotify, Montage))
			{
				continue;
			}
			if (ParsedBody.NotifyPlacements.bHasNotifyStates && FAnimMontageNotifyPlacementAdapter::IsManagedNotifyStateEvent(ExistingNotify, Montage))
			{
				continue;
			}
			UpdatedNotifies.Add(ExistingNotify);
		}

		UpdatedNotifies.Append(ParsedBody.NotifyPlacements.Notifies);
		UpdatedNotifies.Append(ParsedBody.NotifyPlacements.NotifyStates);
		UpdatedNotifies.Sort();
		Montage->Notifies = MoveTemp(UpdatedNotifies);
		Montage->RefreshCacheData();
	}
	if (ParsedBody.bHasBlendInTime)
	{
		Montage->BlendIn.SetBlendTime(ParsedBody.BlendInTime);
	}
	if (ParsedBody.bHasBlendOutTime)
	{
		Montage->BlendOut.SetBlendTime(ParsedBody.BlendOutTime);
	}
	if (ParsedBody.bHasBlendModeIn)
	{
		Montage->BlendModeIn = ParsedBody.BlendModeIn;
	}
	if (ParsedBody.bHasBlendModeOut)
	{
		Montage->BlendModeOut = ParsedBody.BlendModeOut;
	}
	if (ParsedBody.bHasBlendOutTriggerTime)
	{
		Montage->BlendOutTriggerTime = ParsedBody.BlendOutTriggerTime;
	}
	if (ParsedBody.bHasEnableAutoBlendOut)
	{
		Montage->bEnableAutoBlendOut = ParsedBody.bEnableAutoBlendOut;
	}
	if (ParsedBody.bHasSyncGroup)
	{
		Montage->SyncGroup = ParsedBody.SyncGroup;
		bShouldCollectMarkers = true;
	}
	if (ParsedBody.bHasSyncSlotIndex)
	{
		Montage->SyncSlotIndex = ParsedBody.SyncSlotIndex;
		bShouldCollectMarkers = true;
	}
	if (ParsedBody.bHasRootMotionTranslation)
	{
		Montage->bEnableRootMotionTranslation = ParsedBody.bRootMotionTranslation;
	}
	if (ParsedBody.bHasRootMotionRotation)
	{
		Montage->bEnableRootMotionRotation = ParsedBody.bRootMotionRotation;
	}
	if (ParsedBody.bHasRootMotionRootLock)
	{
		Montage->RootMotionRootLock = ParsedBody.RootMotionRootLock;
	}
	if (ParsedBody.bHasCurves)
	{
		const FAssetDocumentCapabilityResult CurveApplyResult = ApplyMontageOwnedCurves(*Montage, ParsedBody.Curves);
		if (!CurveApplyResult.bSuccess)
		{
			return CurveApplyResult;
		}
	}
	if (ParsedBody.bHasTimeStretchCurveName)
	{
		Montage->TimeStretchCurveName = ParsedBody.TimeStretchCurveName;
	}
	if (ParsedBody.bHasTimeStretchSamplingRate)
	{
		const FAssetDocumentCapabilityResult TimeStretchResult = SetTimeStretchFloatProperty(
			Montage->TimeStretchCurve,
			TEXT("SamplingRate"),
			ParsedBody.TimeStretchSamplingRate,
			TEXT("/Body/TimeStretch/SamplingRate"),
			TEXT("InvalidTimeStretchSamplingRate"));
		if (!TimeStretchResult.bSuccess)
		{
			return TimeStretchResult;
		}
	}
	if (ParsedBody.bHasTimeStretchCurveValueMinPrecision)
	{
		const FAssetDocumentCapabilityResult TimeStretchResult = SetTimeStretchFloatProperty(
			Montage->TimeStretchCurve,
			TEXT("CurveValueMinPrecision"),
			ParsedBody.TimeStretchCurveValueMinPrecision,
			TEXT("/Body/TimeStretch/CurveValueMinPrecision"),
			TEXT("InvalidTimeStretchCurveValueMinPrecision"));
		if (!TimeStretchResult.bSuccess)
		{
			return TimeStretchResult;
		}
	}
	if (ParsedBody.bHasCurves || ParsedBody.bHasTimeStretch)
	{
		BakeMontageTimeStretchCurve(*Montage);
	}
	if (ParsedBody.bHasMetadata)
	{
		Montage->EmptyMetaData();
		for (UAnimMetaData* MetadataObject : ParsedBody.Metadata)
		{
			Montage->AddMetaData(MetadataObject);
		}
	}
	if (ParsedBody.bHasSectionMetadata)
	{
		for (FCompositeSection& Section : Montage->CompositeSections)
		{
			Section.MetaData.Reset();
			if (TArray<TObjectPtr<UAnimMetaData>>* SectionMetadata = ParsedBody.SectionMetadataByName.Find(Section.SectionName))
			{
				Section.MetaData.Reserve(SectionMetadata->Num());
				for (UAnimMetaData* MetadataObject : *SectionMetadata)
				{
					Section.MetaData.Add(MetadataObject);
				}
			}
		}
	}
	if (bShouldCollectMarkers)
	{
		RepairDerivedMontageMarkerCacheAfterApply(*Montage);
	}

	Montage->MarkPackageDirty();
	return FAssetDocumentCapabilityResult::Success(TEXT("AnimMontage Body applied"));
}

FAssetDocumentCapabilityResult FAnimMontageAssetDocumentCapability::Extract(const FAssetDocumentCapabilityContext& Context, TSharedRef<FJsonObject>& OutBodyJson) const
{
	const UAnimMontage* Montage = Cast<UAnimMontage>(Context.Asset);
	if (!Montage)
	{
		return BodyFailure(TEXT("AnimMontage body extract requires UAnimMontage asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	FAssetDocumentFragmentCompiler Compiler;
	Compiler.RegisterBuiltInAdapters();

	if (USkeleton* Skeleton = Montage->GetSkeleton())
	{
		TSharedRef<FJsonObject> References = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> SkeletonRef = MakeShared<FJsonObject>();
		FAssetDocumentCapabilityResult Result = ExtractAssetRef(Compiler, const_cast<UAnimMontage*>(Montage), Skeleton, TEXT("/Body/References/Skeleton"), SkeletonRef);
		if (!Result.bSuccess)
		{
			return Result;
		}
		References->SetObjectField(TEXT("Skeleton"), SkeletonRef);
		OutBodyJson->SetObjectField(TEXT("References"), References);
	}

	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	bool bHasPreview = false;
	if (USkeletalMesh* PreviewMesh = Montage->GetPreviewMesh())
	{
		TSharedRef<FJsonObject> PreviewMeshRef = MakeShared<FJsonObject>();
		FAssetDocumentCapabilityResult Result = ExtractAssetRef(Compiler, const_cast<UAnimMontage*>(Montage), PreviewMesh, TEXT("/Body/Preview/PreviewMesh"), PreviewMeshRef);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Preview->SetObjectField(TEXT("PreviewMesh"), PreviewMeshRef);
		bHasPreview = true;
	}
	if (UAnimSequence* PreviewBasePose = Montage->PreviewBasePose)
	{
		TSharedRef<FJsonObject> PreviewBasePoseRef = MakeShared<FJsonObject>();
		FAssetDocumentCapabilityResult Result = ExtractAssetRef(Compiler, const_cast<UAnimMontage*>(Montage), PreviewBasePose, TEXT("/Body/Preview/PreviewBasePose"), PreviewBasePoseRef);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Preview->SetObjectField(TEXT("PreviewBasePose"), PreviewBasePoseRef);
		bHasPreview = true;
	}
	if (bHasPreview)
	{
		OutBodyJson->SetObjectField(TEXT("Preview"), Preview);
	}

	TSharedRef<FJsonObject> Sync = MakeShared<FJsonObject>();
	Sync->SetStringField(TEXT("SyncGroup"), Montage->SyncGroup.ToString());
	Sync->SetNumberField(TEXT("SyncSlotIndex"), Montage->SyncSlotIndex);
	OutBodyJson->SetObjectField(TEXT("Sync"), Sync);

	TSharedRef<FJsonObject> RootMotion = MakeShared<FJsonObject>();
	RootMotion->SetBoolField(TEXT("bEnableRootMotionTranslation"), Montage->bEnableRootMotionTranslation);
	RootMotion->SetBoolField(TEXT("bEnableRootMotionRotation"), Montage->bEnableRootMotionRotation);
	RootMotion->SetStringField(TEXT("RootMotionRootLock"), RootMotionRootLockToString(Montage->RootMotionRootLock));
	OutBodyJson->SetObjectField(TEXT("RootMotion"), RootMotion);

	TSharedPtr<FJsonValue> MetadataValue;
	FAssetDocumentCapabilityResult MetadataResult = ExtractMetadataFragmentArrayRegion(Compiler, Context, Montage, MetadataValue);
	if (!MetadataResult.bSuccess)
	{
		return MetadataResult;
	}
	OutBodyJson->SetField(TEXT("Metadata"), MetadataValue);

	TArray<TSharedPtr<FJsonValue>> SlotAnimTracks;
	for (const FSlotAnimationTrack& SlotAnimTrack : Montage->SlotAnimTracks)
	{
		TSharedRef<FJsonObject> SlotObject = MakeShared<FJsonObject>();
		SlotObject->SetStringField(TEXT("SlotName"), SlotAnimTrack.SlotName.ToString());

		TSharedRef<FJsonObject> AnimTrackObject = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> AnimSegments;
		for (const FAnimSegment& Segment : SlotAnimTrack.AnimTrack.AnimSegments)
		{
			TSharedRef<FJsonObject> SegmentObject = MakeShared<FJsonObject>();
			if (UAnimSequenceBase* AnimReference = Segment.GetAnimReference())
			{
				TSharedRef<FJsonObject> AnimReferenceRef = MakeShared<FJsonObject>();
				FAssetDocumentCapabilityResult Result = ExtractAssetRef(Compiler, const_cast<UAnimMontage*>(Montage), AnimReference, TEXT("/Body/SlotAnimTracks/AnimTrack/AnimSegments/AnimReference"), AnimReferenceRef);
				if (!Result.bSuccess)
				{
					return Result;
				}
				SegmentObject->SetObjectField(TEXT("AnimReference"), AnimReferenceRef);
			}
			SegmentObject->SetNumberField(TEXT("StartPos"), Segment.StartPos);
			SegmentObject->SetNumberField(TEXT("AnimStartTime"), Segment.AnimStartTime);
			SegmentObject->SetNumberField(TEXT("AnimEndTime"), Segment.AnimEndTime);
			SegmentObject->SetNumberField(TEXT("AnimPlayRate"), Segment.AnimPlayRate);
			SegmentObject->SetNumberField(TEXT("LoopingCount"), Segment.LoopingCount);
			AnimSegments.Add(MakeShared<FJsonValueObject>(SegmentObject));
		}
		AnimTrackObject->SetArrayField(TEXT("AnimSegments"), AnimSegments);
		SlotObject->SetObjectField(TEXT("AnimTrack"), AnimTrackObject);
		SlotAnimTracks.Add(MakeShared<FJsonValueObject>(SlotObject));
	}
	OutBodyJson->SetArrayField(TEXT("SlotAnimTracks"), SlotAnimTracks);

	TArray<TSharedPtr<FJsonValue>> CompositeSections;
	for (const FCompositeSection& Section : Montage->CompositeSections)
	{
		TSharedRef<FJsonObject> SectionObject = MakeShared<FJsonObject>();
		SectionObject->SetStringField(TEXT("SectionName"), Section.SectionName.ToString());
		SectionObject->SetNumberField(TEXT("LinkableTime"), Section.GetTime());
		if (!Section.NextSectionName.IsNone())
		{
			SectionObject->SetStringField(TEXT("NextSectionName"), Section.NextSectionName.ToString());
		}
		CompositeSections.Add(MakeShared<FJsonValueObject>(SectionObject));
	}
	OutBodyJson->SetArrayField(TEXT("CompositeSections"), CompositeSections);

	TSharedRef<FJsonObject> SectionMetadata = MakeShared<FJsonObject>();
	for (const FCompositeSection& Section : Montage->CompositeSections)
	{
		TArray<TSharedPtr<FJsonValue>> SectionMetadataValues;
		const TArray<UAnimMetaData*>& SectionMetadataObjects = Section.GetMetaData();
		for (int32 MetadataIndex = 0; MetadataIndex < SectionMetadataObjects.Num(); ++MetadataIndex)
		{
			UAnimMetaData* MetadataObject = SectionMetadataObjects[MetadataIndex];
			if (!MetadataObject)
			{
				continue;
			}

			TSharedRef<FJsonObject> MetadataFragment = MakeShared<FJsonObject>();
			FAssetDocumentCapabilityResult Result = ExtractMetadataObject(
				Compiler,
				const_cast<UAnimMontage*>(Montage),
				MetadataObject,
				FString::Printf(TEXT("/Body/SectionMetadata/%s/%d"), *Section.SectionName.ToString(), MetadataIndex),
				MetadataFragment);
			if (!Result.bSuccess)
			{
				return Result;
			}
			SectionMetadataValues.Add(MakeShared<FJsonValueObject>(MetadataFragment));
		}
		SectionMetadata->SetArrayField(Section.SectionName.ToString(), SectionMetadataValues);
	}
	OutBodyJson->SetObjectField(TEXT("SectionMetadata"), SectionMetadata);

	TArray<TSharedPtr<FJsonValue>> Curves;
	if (const IAnimationDataModel* DataModel = Montage->GetDataModel())
	{
		const TArray<FFloatCurve>& FloatCurves = DataModel->GetFloatCurves();
		Curves.Reserve(FloatCurves.Num());
		for (const FFloatCurve& FloatCurve : FloatCurves)
		{
			TSharedRef<FJsonObject> CurveObject = MakeShared<FJsonObject>();
			CurveObject->SetStringField(TEXT("Name"), FloatCurve.GetName().ToString());
			TArray<TSharedPtr<FJsonValue>> CurveFlags;
			const FAssetDocumentCapabilityResult CurveFlagsResult = ExtractCurveFlags(
				FloatCurve,
				FString::Printf(TEXT("/Body/Curves/%d/Flags"), Curves.Num()),
				CurveFlags);
			if (!CurveFlagsResult.bSuccess)
			{
				return CurveFlagsResult;
			}
			CurveObject->SetArrayField(TEXT("Flags"), CurveFlags);

			TArray<float> Times;
			TArray<float> Values;
			FloatCurve.GetKeys(Times, Values);

			TArray<TSharedPtr<FJsonValue>> Keys;
			const int32 KeyCount = FMath::Min(Times.Num(), Values.Num());
			Keys.Reserve(KeyCount);
			for (int32 KeyIndex = 0; KeyIndex < KeyCount; ++KeyIndex)
			{
				TSharedRef<FJsonObject> KeyObject = MakeShared<FJsonObject>();
				KeyObject->SetNumberField(TEXT("Time"), Times[KeyIndex]);
				KeyObject->SetNumberField(TEXT("Value"), Values[KeyIndex]);
				Keys.Add(MakeShared<FJsonValueObject>(KeyObject));
			}
			CurveObject->SetArrayField(TEXT("Keys"), Keys);
			Curves.Add(MakeShared<FJsonValueObject>(CurveObject));
		}
	}
	OutBodyJson->SetArrayField(TEXT("Curves"), Curves);

	TSharedRef<FJsonObject> TimeStretch = MakeShared<FJsonObject>();
	TimeStretch->SetStringField(
		TEXT("TimeStretchCurveName"),
		HasMontageFloatCurve(*Montage, Montage->TimeStretchCurveName) ? Montage->TimeStretchCurveName.ToString() : FString());

	float TimeStretchSamplingRate = 60.0f;
	FAssetDocumentCapabilityResult TimeStretchResult = GetTimeStretchFloatProperty(
		Montage->TimeStretchCurve,
		TEXT("SamplingRate"),
		TimeStretchSamplingRate,
		TEXT("/Body/TimeStretch/SamplingRate"),
		TEXT("InvalidTimeStretchSamplingRate"));
	if (!TimeStretchResult.bSuccess)
	{
		return TimeStretchResult;
	}
	TimeStretch->SetNumberField(TEXT("SamplingRate"), TimeStretchSamplingRate);

	float TimeStretchCurveValueMinPrecision = 0.01f;
	TimeStretchResult = GetTimeStretchFloatProperty(
		Montage->TimeStretchCurve,
		TEXT("CurveValueMinPrecision"),
		TimeStretchCurveValueMinPrecision,
		TEXT("/Body/TimeStretch/CurveValueMinPrecision"),
		TEXT("InvalidTimeStretchCurveValueMinPrecision"));
	if (!TimeStretchResult.bSuccess)
	{
		return TimeStretchResult;
	}
	TimeStretch->SetNumberField(TEXT("CurveValueMinPrecision"), TimeStretchCurveValueMinPrecision);
	OutBodyJson->SetObjectField(TEXT("TimeStretch"), TimeStretch);

	FAnimMontageNotifyPlacementAdapter NotifyPlacementAdapter;
	FAssetDocumentCapabilityResult NotifyExtractResult = NotifyPlacementAdapter.Extract(Compiler, Montage, OutBodyJson);
	if (!NotifyExtractResult.bSuccess)
	{
		return NotifyExtractResult;
	}

	TSharedRef<FJsonObject> Blend = MakeShared<FJsonObject>();
	Blend->SetNumberField(TEXT("BlendInTime"), Montage->GetDefaultBlendInTime());
	Blend->SetNumberField(TEXT("BlendOutTime"), Montage->GetDefaultBlendOutTime());
	Blend->SetStringField(TEXT("BlendModeIn"), MontageBlendModeToString(Montage->BlendModeIn));
	Blend->SetStringField(TEXT("BlendModeOut"), MontageBlendModeToString(Montage->BlendModeOut));
	Blend->SetNumberField(TEXT("BlendOutTriggerTime"), Montage->BlendOutTriggerTime);
	Blend->SetBoolField(TEXT("bEnableAutoBlendOut"), Montage->bEnableAutoBlendOut);
	OutBodyJson->SetObjectField(TEXT("Blend"), Blend);

	return FAssetDocumentCapabilityResult::Success(TEXT("AnimMontage Body extracted"));
}

FAssetDocumentCapabilityResult FAnimMontageAssetDocumentCapability::Diff(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& DesiredJson, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	FAssetDocumentPreviewApplyDiffHooks Hooks;
	Hooks.ValidateDesiredBody = [this](
		const FAssetDocumentCapabilityContext& ValidateContext,
		const TSharedRef<FJsonObject>& DesiredBody)
	{
		return ValidateBodyObject(ValidateContext, DesiredBody);
	};
	Hooks.DuplicatePreviewAsset = [](const FAssetDocumentCapabilityContext& DiffContext, UObject*& OutPreviewAsset)
	{
		UAnimMontage* CurrentMontage = Cast<UAnimMontage>(DiffContext.Asset);
		if (!CurrentMontage)
		{
			return BodyFailure(TEXT("AnimMontage body diff requires UAnimMontage asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
		}

		UAnimMontage* PreviewMontage = DuplicateObject<UAnimMontage>(CurrentMontage, GetTransientPackage());
		if (!PreviewMontage)
		{
			return BodyFailure(TEXT("Failed to duplicate AnimMontage for Body diff"), TEXT("/Body"), TEXT("DuplicateFailed"));
		}

		OutPreviewAsset = PreviewMontage;
		return FAssetDocumentCapabilityResult::Success(TEXT("Duplicated AnimMontage for Body diff"));
	};
	Hooks.MakePreviewContext = [](const FAssetDocumentCapabilityContext& DiffContext, UObject* PreviewAsset)
	{
		FAssetDocumentCapabilityContext PreviewContext = DiffContext;
		PreviewContext.Asset = PreviewAsset;
		PreviewContext.AssetClass = UAnimMontage::StaticClass();
		PreviewContext.bIsDryRun = true;
		return PreviewContext;
	};
	Hooks.ApplyDesiredBody = [this](
		const FAssetDocumentCapabilityContext& PreviewContext,
		const TSharedRef<FJsonValue>& DesiredBody)
	{
		FAssetDocumentCapabilityContext MutablePreviewContext = PreviewContext;
		return const_cast<FAnimMontageAssetDocumentCapability*>(this)->Apply(MutablePreviewContext, DesiredBody);
	};
	Hooks.ExtractBody = [this](
		const FAssetDocumentCapabilityContext& ExtractContext,
		const TSharedRef<FJsonObject>& OutBody)
	{
		TSharedRef<FJsonObject> MutableOutBody = OutBody;
		return Extract(ExtractContext, MutableOutBody);
	};

	FAssetDocumentPreviewApplyDiffAdapter Adapter(MoveTemp(Hooks));
	const FAssetDocumentCapabilityResult DiffResult = Adapter.DiffBody(Context, DesiredJson, OutDiffEntries);
	if (!DiffResult.bSuccess)
	{
		return DiffResult;
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("AnimMontage Body diffed"));
}

FAssetDocumentCapabilityResult FAnimMontageAssetDocumentCapability::ValidateBodyObject(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonObject>& BodyObject) const
{
	FParsedAnimMontageBody ParsedBody;
	const FAssetDocumentCapabilityResult Result = ParseAnimMontageBody(nullptr, Context, nullptr, BodyObject, false, INDEX_NONE, ParsedBody);
	if (!Result.bSuccess)
	{
		return Result;
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("AnimMontage Body is valid"));
}
