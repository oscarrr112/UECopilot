// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/AnimSequenceAssetDocumentCapability.h"

#include "AssetDocumentBodyRegionDispatcher.h"
#include "AssetDocumentClassResolver.h"
#include "AssetDocumentFragmentCompiler.h"
#include "AssetDocumentJsonRegionUtils.h"
#include "AssetDocumentPropertyAdapter.h"
#include "AssetDocumentRegionRuntime.h"
#include "Profiles/AnimSequenceAssetDocumentProfile.h"
#include "Regions/AssetDocumentFragmentArrayRegionAdapter.h"
#include "Regions/AssetDocumentNamedArrayRegionAdapter.h"
#include "Regions/AssetDocumentObjectFieldSchemaUtils.h"
#include "Regions/AssetDocumentObjectRegionAdapter.h"
#include "Regions/AssetDocumentPreviewApplyDiffAdapter.h"
#include "Regions/AssetDocumentTimelinePlacementRegionAdapter.h"

#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimMetaData.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimTypes.h"
#include "Curves/RichCurve.h"
#include "Dom/JsonValue.h"
#include "Engine/AssetUserData.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

#include <cmath>

namespace
{
const TCHAR* ManagedMetadataObjectPrefix = TEXT("AssetDocumentManaged_AnimSequenceMetadata_");
const TCHAR* ManagedAssetUserDataObjectPrefix = TEXT("AssetDocumentManaged_AnimSequenceAssetUserData_");
const TCHAR* ManagedNamedObjectMarker = TEXT("Named_");
const TCHAR* ManagedNameDelimiter = TEXT("__");

struct FParsedManagedObjectFragment
{
	FString ExplicitName;
	TObjectPtr<UObject> Object;
};

struct FManagedObjectMoveRecord
{
	TObjectPtr<UObject> Object;
	TObjectPtr<UObject> OriginalOuter;
	FName OriginalName;
};

bool IsKnownBodyKey(const FString& BodyKey)
{
	for (const FName& KnownBodyKey : FAnimSequenceAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (KnownBodyKey.ToString() == BodyKey)
		{
			return true;
		}
	}
	return false;
}

bool IsObject(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && Value->Type == EJson::Object;
}

bool IsArray(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && Value->Type == EJson::Array;
}

FAssetDocumentCapabilityResult BodyFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FAssetDocumentCapabilityResult FragmentFailure(const FAssetDocumentFragmentResult& FragmentResult)
{
	FAssetDocumentCapabilityResult Result = FAssetDocumentCapabilityResult::Failure(FragmentResult.Message);
	Result.Diagnostics = FragmentResult.Diagnostics;
	return Result;
}

FString JsonValueToComparableString(TSharedPtr<FJsonValue> Value)
{
	auto AppendQuotedJsonString = [](const FString& String, FString& Out)
	{
		Out += TEXT("\"");
		for (int32 Index = 0; Index < String.Len(); ++Index)
		{
			const TCHAR Character = String[Index];
			switch (Character)
			{
			case TEXT('"'):
				Out += TEXT("\\\"");
				break;
			case TEXT('\\'):
				Out += TEXT("\\\\");
				break;
			case TEXT('\b'):
				Out += TEXT("\\b");
				break;
			case TEXT('\f'):
				Out += TEXT("\\f");
				break;
			case TEXT('\n'):
				Out += TEXT("\\n");
				break;
			case TEXT('\r'):
				Out += TEXT("\\r");
				break;
			case TEXT('\t'):
				Out += TEXT("\\t");
				break;
			default:
				if (Character < 0x20)
				{
					Out += FString::Printf(TEXT("\\u%04x"), static_cast<int32>(Character));
				}
				else
				{
					Out.AppendChar(Character);
				}
				break;
			}
		}
		Out += TEXT("\"");
	};

	TFunction<void(TSharedPtr<FJsonValue>, FString&)> AppendCanonicalJsonValue;
	AppendCanonicalJsonValue = [&AppendCanonicalJsonValue, &AppendQuotedJsonString](TSharedPtr<FJsonValue> JsonValue, FString& Out)
	{
		if (!JsonValue.IsValid() || JsonValue->Type == EJson::Null || JsonValue->Type == EJson::None)
		{
			Out += TEXT("null");
			return;
		}

		switch (JsonValue->Type)
		{
		case EJson::String:
			AppendQuotedJsonString(JsonValue->AsString(), Out);
			break;
		case EJson::Number:
			Out += FString::Printf(TEXT("%.17g"), JsonValue->AsNumber());
			break;
		case EJson::Boolean:
			Out += JsonValue->AsBool() ? TEXT("true") : TEXT("false");
			break;
		case EJson::Array:
			{
				Out += TEXT("[");
				const TArray<TSharedPtr<FJsonValue>>& Array = JsonValue->AsArray();
				for (int32 Index = 0; Index < Array.Num(); ++Index)
				{
					if (Index > 0)
					{
						Out += TEXT(",");
					}
					AppendCanonicalJsonValue(Array[Index], Out);
				}
				Out += TEXT("]");
				break;
			}
		case EJson::Object:
			{
				const TSharedPtr<FJsonObject> Object = JsonValue->AsObject();
				if (!Object.IsValid())
				{
					Out += TEXT("null");
					break;
				}

				TArray<FString> Keys;
				Object->Values.GetKeys(Keys);
				Keys.Sort();

				Out += TEXT("{");
				for (int32 Index = 0; Index < Keys.Num(); ++Index)
				{
					if (Index > 0)
					{
						Out += TEXT(",");
					}
					AppendQuotedJsonString(Keys[Index], Out);
					Out += TEXT(":");
					const TSharedPtr<FJsonValue>* FieldValue = Object->Values.Find(Keys[Index]);
					AppendCanonicalJsonValue(FieldValue ? *FieldValue : MakeShared<FJsonValueNull>(), Out);
				}
				Out += TEXT("}");
				break;
			}
		default:
			Out += TEXT("null");
			break;
		}
	};

	FString JsonText;
	AppendCanonicalJsonValue(Value.IsValid() ? Value : MakeShared<FJsonValueNull>(), JsonText);
	return JsonText;
}

void AddBodyDiffEntry(
	TArray<TSharedPtr<FJsonValue>>& Entries,
	const FString& Path,
	const FString& Status,
	TSharedPtr<FJsonValue> Current,
	TSharedPtr<FJsonValue> Desired)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("path"), Path);
	Entry->SetStringField(TEXT("status"), Status);
	Entry->SetField(TEXT("current"), Current.IsValid() ? Current : MakeShared<FJsonValueNull>());
	Entry->SetField(TEXT("desired"), Desired.IsValid() ? Desired : MakeShared<FJsonValueNull>());
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
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

FAssetDocumentCapabilityResult ReadOptionalNumber(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* FieldName,
	const FString& Path,
	bool& bOutHasValue,
	double& OutValue)
{
	bOutHasValue = false;
	OutValue = 0.0;
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
		return BodyFailure(FString::Printf(TEXT("%s must be non-negative"), FieldName), Path, TEXT("InvalidNumericField"));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadOptionalString(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* FieldName,
	const FString& Path,
	bool& bOutHasValue,
	FString& OutValue)
{
	bOutHasValue = false;
	OutValue.Reset();
	if (const TSharedPtr<FJsonValue>* Value = Object->Values.Find(FieldName))
	{
		if (!Value->IsValid() || (*Value)->Type != EJson::String)
		{
			return BodyFailure(FString::Printf(TEXT("%s must be a string"), FieldName), Path, TEXT("InvalidStringField"));
		}

		bOutHasValue = true;
		OutValue = (*Value)->AsString();
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadOptionalNonNegativeInteger(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* FieldName,
	const FString& Path,
	bool& bOutHasValue,
	int32& OutValue)
{
	double NumberValue = 0.0;
	const FAssetDocumentCapabilityResult Result = ReadOptionalNonNegativeNumber(Object, FieldName, Path, bOutHasValue, NumberValue);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (bOutHasValue)
	{
		const int32 IntegerValue = static_cast<int32>(NumberValue);
		if (!FMath::IsNearlyEqual(NumberValue, static_cast<double>(IntegerValue)))
		{
			return BodyFailure(FString::Printf(TEXT("%s must be an integer"), FieldName), Path, TEXT("InvalidIntegerField"));
		}
		OutValue = IntegerValue;
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult CompileAssetRef(
	const FAssetDocumentFragmentCompiler& Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimSequence* Sequence,
	const TSharedRef<FJsonObject>& Fragment,
	UClass* ExpectedBaseClass,
	const FString& JsonPath,
	FAssetDocumentFragmentResult& OutFragmentResult)
{
	FAssetDocumentFragmentContext FragmentContext;
	FragmentContext.OwnerAsset = Sequence;
	FragmentContext.Outer = Sequence ? static_cast<UObject*>(Sequence) : GetTransientPackage();
	FragmentContext.ExpectedBaseClass = ExpectedBaseClass;
	FragmentContext.Definitions = Context.Definitions;
	FragmentContext.JsonPath = JsonPath;

	OutFragmentResult = Compiler.Compile(Fragment, FragmentContext);
	return OutFragmentResult.bSuccess ? FAssetDocumentCapabilityResult::Success() : FragmentFailure(OutFragmentResult);
}

FAssetDocumentCapabilityResult ValidateAssetRef(
	const FAssetDocumentFragmentCompiler& Compiler,
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& Fragment,
	UClass* ExpectedBaseClass,
	const FString& JsonPath)
{
	FAssetDocumentFragmentContext FragmentContext;
	FragmentContext.OwnerAsset = Context.Asset;
	FragmentContext.Outer = Context.Asset ? Context.Asset : GetTransientPackage();
	FragmentContext.ExpectedBaseClass = ExpectedBaseClass;
	FragmentContext.Definitions = Context.Definitions;
	FragmentContext.JsonPath = JsonPath;

	const FAssetDocumentFragmentResult FragmentResult = Compiler.Validate(Fragment, FragmentContext);
	return FragmentResult.bSuccess ? FAssetDocumentCapabilityResult::Success() : FragmentFailure(FragmentResult);
}

FAssetDocumentCapabilityResult ParseAssetRef(
	const FAssetDocumentFragmentCompiler* Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimSequence* Sequence,
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* FieldName,
	UClass* ExpectedBaseClass,
	const FString& Path,
	bool bResolveFragments,
	bool bAllowNull,
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
		if (!bAllowNull)
		{
			return BodyFailure(FString::Printf(TEXT("%s cannot be null"), FieldName), Path, TEXT("NullNotAllowed"));
		}
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
	if (!Compiler)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	if (!bResolveFragments)
	{
		return ValidateAssetRef(*Compiler, Context, FragmentObject.ToSharedRef(), ExpectedBaseClass, Path);
	}

	if (!Sequence)
	{
		return BodyFailure(TEXT("Asset reference resolution requires an AnimSequence asset"), Path, TEXT("UnsupportedAsset"));
	}

	FAssetDocumentFragmentResult FragmentResult;
	const FAssetDocumentCapabilityResult CompileResult = CompileAssetRef(*Compiler, Context, Sequence, FragmentObject.ToSharedRef(), ExpectedBaseClass, Path, FragmentResult);
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

bool TryParseAdditiveAnimType(const FString& Value, EAdditiveAnimationType& OutValue)
{
	if (Value == TEXT("AAT_None"))
	{
		OutValue = AAT_None;
		return true;
	}
	if (Value == TEXT("AAT_LocalSpaceBase"))
	{
		OutValue = AAT_LocalSpaceBase;
		return true;
	}
	if (Value == TEXT("AAT_RotationOffsetMeshSpace"))
	{
		OutValue = AAT_RotationOffsetMeshSpace;
		return true;
	}
	return false;
}

FString AdditiveAnimTypeToString(EAdditiveAnimationType Value)
{
	switch (Value)
	{
	case AAT_LocalSpaceBase:
		return TEXT("AAT_LocalSpaceBase");
	case AAT_RotationOffsetMeshSpace:
		return TEXT("AAT_RotationOffsetMeshSpace");
	case AAT_None:
	default:
		return TEXT("AAT_None");
	}
}

bool TryParseRefPoseType(const FString& Value, EAdditiveBasePoseType& OutValue)
{
	if (Value == TEXT("ABPT_None"))
	{
		OutValue = ABPT_None;
		return true;
	}
	if (Value == TEXT("ABPT_RefPose"))
	{
		OutValue = ABPT_RefPose;
		return true;
	}
	if (Value == TEXT("ABPT_AnimScaled"))
	{
		OutValue = ABPT_AnimScaled;
		return true;
	}
	if (Value == TEXT("ABPT_AnimFrame"))
	{
		OutValue = ABPT_AnimFrame;
		return true;
	}
	if (Value == TEXT("ABPT_LocalAnimFrame"))
	{
		OutValue = ABPT_LocalAnimFrame;
		return true;
	}
	return false;
}

FString RefPoseTypeToString(EAdditiveBasePoseType Value)
{
	switch (Value)
	{
	case ABPT_RefPose:
		return TEXT("ABPT_RefPose");
	case ABPT_AnimScaled:
		return TEXT("ABPT_AnimScaled");
	case ABPT_AnimFrame:
		return TEXT("ABPT_AnimFrame");
	case ABPT_LocalAnimFrame:
		return TEXT("ABPT_LocalAnimFrame");
	case ABPT_None:
	default:
		return TEXT("ABPT_None");
	}
}

FString CurveInterpModeToString(ERichCurveInterpMode Value)
{
	switch (Value)
	{
	case RCIM_Constant:
		return TEXT("RCIM_Constant");
	case RCIM_Cubic:
		return TEXT("RCIM_Cubic");
	case RCIM_Linear:
	default:
		return TEXT("RCIM_Linear");
	}
}

bool TryParseCurveInterpMode(const FString& Value, ERichCurveInterpMode& OutInterpMode)
{
	if (Value == TEXT("RCIM_Linear") || Value == TEXT("Linear"))
	{
		OutInterpMode = RCIM_Linear;
		return true;
	}
	if (Value == TEXT("RCIM_Constant") || Value == TEXT("Constant"))
	{
		OutInterpMode = RCIM_Constant;
		return true;
	}
	if (Value == TEXT("RCIM_Cubic") || Value == TEXT("Cubic"))
	{
		OutInterpMode = RCIM_Cubic;
		return true;
	}
	return false;
}

bool TryParseCurveFlag(const FString& Value, int32& OutFlag)
{
	if (Value == TEXT("Default"))
	{
		OutFlag = AACF_DefaultCurve;
		return true;
	}
	if (Value == TEXT("Editable") || Value == TEXT("AACF_Editable"))
	{
		OutFlag = AACF_Editable;
		return true;
	}
	if (Value == TEXT("DriveMorphTarget") || Value == TEXT("AACF_DriveMorphTarget_DEPRECATED"))
	{
		OutFlag = AACF_DriveMorphTarget_DEPRECATED;
		return true;
	}
	if (Value == TEXT("DriveAttribute") || Value == TEXT("AACF_DriveAttribute_DEPRECATED"))
	{
		OutFlag = AACF_DriveAttribute_DEPRECATED;
		return true;
	}
	return false;
}

TArray<TSharedPtr<FJsonValue>> CurveFlagsToJsonArray(int32 Flags)
{
	TArray<TSharedPtr<FJsonValue>> FlagValues;
	if ((Flags & AACF_DriveMorphTarget_DEPRECATED) != 0)
	{
		FlagValues.Add(MakeShared<FJsonValueString>(TEXT("DriveMorphTarget")));
	}
	if ((Flags & AACF_DriveAttribute_DEPRECATED) != 0)
	{
		FlagValues.Add(MakeShared<FJsonValueString>(TEXT("DriveAttribute")));
	}
	if ((Flags & AACF_Editable) != 0)
	{
		FlagValues.Add(MakeShared<FJsonValueString>(TEXT("Editable")));
	}
	return FlagValues;
}

FAssetDocumentCapabilityResult RejectUnsupportedAuthoredFields(const TSharedRef<FJsonObject>& BodyObject)
{
	const TArray<FString> UnsupportedBodyKeys = {
		TEXT("Import"),
		TEXT("RawTracks"),
		TEXT("CompressedData"),
	};
	for (const FString& UnsupportedKey : UnsupportedBodyKeys)
	{
		if (BodyObject->HasField(UnsupportedKey))
		{
			const FString Path = FString::Printf(TEXT("/Body/%s"), *UnsupportedKey);
			return BodyFailure(
				FString::Printf(
					TEXT("Body.%s is not an AnimSequence AssetDocument authored field. AnimSequence AssetDocument is post-import only; use supported Body sections such as References, Preview, Playback, Additive, RootMotion, Compression, Curves, Notifies, NotifyStates, NotifyTracks, SyncMarkers, Metadata, or AssetUserData instead of legacy/raw/import/compressed names."),
					*UnsupportedKey),
				Path,
				TEXT("UnsupportedAuthoredField"));
		}
	}

	const TSharedPtr<FJsonObject>* PlaybackObject = nullptr;
	if (BodyObject->TryGetObjectField(TEXT("Playback"), PlaybackObject) && PlaybackObject && PlaybackObject->IsValid())
	{
		const TArray<FString> UnsupportedPlaybackFields = {
			TEXT("PlayLength"),
			TEXT("NumberOfSampledKeys"),
			TEXT("SamplingFrameRate"),
		};
		for (const FString& UnsupportedField : UnsupportedPlaybackFields)
		{
			if ((*PlaybackObject)->HasField(UnsupportedField))
			{
				const FString Path = FString::Printf(TEXT("/Body/Playback/%s"), *UnsupportedField);
				return BodyFailure(
					FString::Printf(TEXT("Body.Playback.%s is derived data and cannot be authored"), *UnsupportedField),
					Path,
					TEXT("UnsupportedAuthoredField"));
			}
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult RejectUnknownObjectFields(
	const TSharedRef<FJsonObject>& BodyObject,
	const TCHAR* SectionName,
	const TArray<FString>& AllowedFields)
{
	const TSharedPtr<FJsonObject>* SectionObject = nullptr;
	if (!BodyObject->TryGetObjectField(SectionName, SectionObject) || !SectionObject || !SectionObject->IsValid())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*SectionObject)->Values)
	{
		if (!AllowedFields.Contains(Pair.Key))
		{
			const FString Path = FString::Printf(TEXT("/Body/%s/%s"), SectionName, *Pair.Key);
			return BodyFailure(
				FString::Printf(TEXT("Body.%s.%s is not supported by the AnimSequence Task 2 scalar capability"), SectionName, *Pair.Key),
				Path,
				TEXT("UnsupportedAuthoredField"));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentObjectFieldSchema MakeAnimSequencePreviewSchema()
{
	FAssetDocumentObjectFieldSchema Schema;
	Schema.Fields.Add({
		TEXT("PreviewMesh"),
		EJson::Object,
		false,
		TEXT("MissingPreviewMesh"),
		TEXT("InvalidObjectReference"),
		FString(),
		true,
	});
	Schema.bRejectUnknownFields = true;
	Schema.UnknownFieldCode = TEXT("UnsupportedAuthoredField");
	Schema.UnknownFieldMessageFormat = TEXT("Body.Preview.%s is not supported by the AnimSequence Task 2 scalar capability");
	return Schema;
}

FAssetDocumentObjectFieldSchema MakeAnimSequencePlaybackSchema()
{
	FAssetDocumentObjectFieldSchema Schema;
	Schema.Fields.Add({
		TEXT("RateScale"),
		EJson::Number,
		false,
		TEXT("MissingRateScale"),
		TEXT("InvalidNumericField"),
		TEXT("RateScale must be a number"),
	});
	Schema.bRejectUnknownFields = true;
	Schema.UnknownFieldCode = TEXT("UnsupportedAuthoredField");
	Schema.UnknownFieldMessageFormat = TEXT("Body.Playback.%s is not supported by the AnimSequence Task 2 scalar capability");
	return Schema;
}

FAssetDocumentCapabilityResult ValidateAnimSequenceObjectFieldSchema(
	const TSharedRef<FJsonObject>& BodyObject,
	const TCHAR* SectionName,
	const FAssetDocumentObjectFieldSchema& Schema)
{
	const TSharedPtr<FJsonObject>* SectionObject = nullptr;
	if (!BodyObject->TryGetObjectField(SectionName, SectionObject) || !SectionObject || !SectionObject->IsValid())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FAssetDocumentRegionContext RegionContext;
	RegionContext.RegionId = *FString::Printf(TEXT("Body.%s"), SectionName);
	RegionContext.BodyPath = FString::Printf(TEXT("Body.%s"), SectionName);
	RegionContext.JsonPointer = FString::Printf(TEXT("/Body/%s"), SectionName);
	return FAssetDocumentObjectFieldSchemaUtils::ValidateObjectFields(
		RegionContext,
		(*SectionObject).ToSharedRef(),
		Schema);
}

struct FParsedAnimSequenceCurve
{
	FName Name = NAME_None;
	int32 Flags = AACF_DefaultCurve;
	TArray<FRichCurveKey> Keys;
};

struct FParsedAnimSequenceCurveKey
{
	int32 AuthoredIndex = INDEX_NONE;
	FRichCurveKey Key;
};

struct FFloatCurveView
{
	const FFloatCurve* Curve = nullptr;
};

FAssetDocumentCapabilityResult ParseAnimSequenceCurves(
	const TSharedRef<FJsonObject>& BodyObject,
	bool& bOutHasCurves,
	TArray<FParsedAnimSequenceCurve>& OutCurves)
{
	bOutHasCurves = false;
	OutCurves.Reset();

	const TSharedPtr<FJsonValue>* CurvesValue = BodyObject->Values.Find(TEXT("Curves"));
	if (!CurvesValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const TArray<TSharedPtr<FJsonValue>>* CurveValues = nullptr;
	FAssetDocumentCapabilityResult Result = RequireArrayValue(*CurvesValue, TEXT("/Body/Curves"), CurveValues);
	if (!Result.bSuccess)
	{
		return Result;
	}

	bOutHasCurves = true;
	TSet<FName> CurveNames;
	for (int32 CurveIndex = 0; CurveIndex < CurveValues->Num(); ++CurveIndex)
	{
		const FString CurvePath = FString::Printf(TEXT("/Body/Curves/%d"), CurveIndex);
		TSharedPtr<FJsonObject> CurveObject;
		Result = RequireObjectValue((*CurveValues)[CurveIndex], CurvePath, CurveObject);
		if (!Result.bSuccess)
		{
			return Result;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : CurveObject->Values)
		{
			if (Pair.Key != TEXT("Name") &&
				Pair.Key != TEXT("CurveType") &&
				Pair.Key != TEXT("Flags") &&
				Pair.Key != TEXT("Keys"))
			{
				return BodyFailure(
					FString::Printf(TEXT("Body.Curves[%d].%s is not supported"), CurveIndex, *Pair.Key),
					FString::Printf(TEXT("%s/%s"), *CurvePath, *Pair.Key),
					TEXT("UnsupportedAuthoredField"));
			}
		}

		FParsedAnimSequenceCurve ParsedCurve;
		bool bHasName = false;
		FString CurveNameString;
		Result = ReadOptionalString(CurveObject.ToSharedRef(), TEXT("Name"), FString::Printf(TEXT("%s/Name"), *CurvePath), bHasName, CurveNameString);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (!bHasName || CurveNameString.IsEmpty())
		{
			return BodyFailure(TEXT("Curve Name is required"), FString::Printf(TEXT("%s/Name"), *CurvePath), TEXT("MissingCurveName"));
		}
		ParsedCurve.Name = FName(*CurveNameString);
		if (ParsedCurve.Name.IsNone())
		{
			return BodyFailure(TEXT("Curve Name must not resolve to NAME_None"), FString::Printf(TEXT("%s/Name"), *CurvePath), TEXT("InvalidCurveName"));
		}
		if (CurveNames.Contains(ParsedCurve.Name))
		{
			return BodyFailure(TEXT("Curve names must be unique"), FString::Printf(TEXT("%s/Name"), *CurvePath), TEXT("DuplicateCurveName"));
		}
		CurveNames.Add(ParsedCurve.Name);

		bool bHasCurveType = false;
		FString CurveType;
		Result = ReadOptionalString(CurveObject.ToSharedRef(), TEXT("CurveType"), FString::Printf(TEXT("%s/CurveType"), *CurvePath), bHasCurveType, CurveType);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (bHasCurveType && CurveType != TEXT("Float"))
		{
			return BodyFailure(TEXT("Only float curves are supported for Body.Curves"), FString::Printf(TEXT("%s/CurveType"), *CurvePath), TEXT("DeferredCurveType"));
		}

		if (const TSharedPtr<FJsonValue>* FlagsValue = CurveObject->Values.Find(TEXT("Flags")))
		{
			const TArray<TSharedPtr<FJsonValue>>* FlagValues = nullptr;
			Result = RequireArrayValue(*FlagsValue, FString::Printf(TEXT("%s/Flags"), *CurvePath), FlagValues);
			if (!Result.bSuccess)
			{
				return Result;
			}
			ParsedCurve.Flags = 0;
			for (int32 FlagIndex = 0; FlagIndex < FlagValues->Num(); ++FlagIndex)
			{
				const TSharedPtr<FJsonValue>& FlagValue = (*FlagValues)[FlagIndex];
				const FString FlagPath = FString::Printf(TEXT("%s/Flags/%d"), *CurvePath, FlagIndex);
				if (!FlagValue.IsValid() || FlagValue->Type != EJson::String)
				{
					return BodyFailure(TEXT("Curve Flags entries must be strings"), FlagPath, TEXT("InvalidCurveFlag"));
				}

				int32 ParsedFlag = 0;
				if (!TryParseCurveFlag(FlagValue->AsString(), ParsedFlag))
				{
					return BodyFailure(TEXT("Curve flag is not supported"), FlagPath, TEXT("InvalidCurveFlag"));
				}
				ParsedCurve.Flags |= ParsedFlag;
			}
		}

		const TSharedPtr<FJsonValue>* KeysValue = CurveObject->Values.Find(TEXT("Keys"));
		if (!KeysValue)
		{
			return BodyFailure(TEXT("Curve Keys array is required"), FString::Printf(TEXT("%s/Keys"), *CurvePath), TEXT("MissingCurveKeys"));
		}

		const TArray<TSharedPtr<FJsonValue>>* KeyValues = nullptr;
		Result = RequireArrayValue(*KeysValue, FString::Printf(TEXT("%s/Keys"), *CurvePath), KeyValues);
		if (!Result.bSuccess)
		{
			return Result;
		}

		for (int32 KeyIndex = 0; KeyIndex < KeyValues->Num(); ++KeyIndex)
		{
			const FString KeyPath = FString::Printf(TEXT("%s/Keys/%d"), *CurvePath, KeyIndex);
			TSharedPtr<FJsonObject> KeyObject;
			Result = RequireObjectValue((*KeyValues)[KeyIndex], KeyPath, KeyObject);
			if (!Result.bSuccess)
			{
				return Result;
			}

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : KeyObject->Values)
			{
				if (Pair.Key != TEXT("Time") &&
					Pair.Key != TEXT("Value") &&
					Pair.Key != TEXT("InterpMode") &&
					Pair.Key != TEXT("Interpolation"))
				{
					return BodyFailure(
						FString::Printf(TEXT("Body.Curves[%d].Keys[%d].%s is not supported"), CurveIndex, KeyIndex, *Pair.Key),
						FString::Printf(TEXT("%s/%s"), *KeyPath, *Pair.Key),
						TEXT("UnsupportedAuthoredField"));
				}
			}

			bool bHasTime = false;
			double Time = 0.0;
			Result = ReadOptionalNumber(KeyObject.ToSharedRef(), TEXT("Time"), FString::Printf(TEXT("%s/Time"), *KeyPath), bHasTime, Time);
			if (!Result.bSuccess)
			{
				return Result;
			}
			if (!bHasTime || Time < 0.0 || !FMath::IsFinite(Time))
			{
				return BodyFailure(TEXT("Curve key Time must be finite and non-negative"), FString::Printf(TEXT("%s/Time"), *KeyPath), TEXT("InvalidCurveKeyTime"));
			}
			const float TimeFloat = static_cast<float>(Time);
			if (!FMath::IsFinite(TimeFloat))
			{
				return BodyFailure(TEXT("Curve key Time must fit in a finite float"), FString::Printf(TEXT("%s/Time"), *KeyPath), TEXT("InvalidCurveKeyTime"));
			}

			bool bHasValue = false;
			double Value = 0.0;
			Result = ReadOptionalNumber(KeyObject.ToSharedRef(), TEXT("Value"), FString::Printf(TEXT("%s/Value"), *KeyPath), bHasValue, Value);
			if (!Result.bSuccess)
			{
				return Result;
			}
			if (!bHasValue || !FMath::IsFinite(Value))
			{
				return BodyFailure(TEXT("Curve key Value must be finite"), FString::Printf(TEXT("%s/Value"), *KeyPath), TEXT("InvalidCurveKeyValue"));
			}
			const float ValueFloat = static_cast<float>(Value);
			if (!FMath::IsFinite(ValueFloat))
			{
				return BodyFailure(TEXT("Curve key Value must fit in a finite float"), FString::Printf(TEXT("%s/Value"), *KeyPath), TEXT("InvalidCurveKeyValue"));
			}

			ERichCurveInterpMode InterpMode = RCIM_Linear;
			bool bHasInterpMode = false;
			FString InterpModeString;
			Result = ReadOptionalString(KeyObject.ToSharedRef(), TEXT("InterpMode"), FString::Printf(TEXT("%s/InterpMode"), *KeyPath), bHasInterpMode, InterpModeString);
			if (!Result.bSuccess)
			{
				return Result;
			}
			if (!bHasInterpMode)
			{
				Result = ReadOptionalString(KeyObject.ToSharedRef(), TEXT("Interpolation"), FString::Printf(TEXT("%s/Interpolation"), *KeyPath), bHasInterpMode, InterpModeString);
				if (!Result.bSuccess)
				{
					return Result;
				}
			}
			if (bHasInterpMode && !TryParseCurveInterpMode(InterpModeString, InterpMode))
			{
				const FString InterpPath = KeyObject->HasField(TEXT("InterpMode"))
					? FString::Printf(TEXT("%s/InterpMode"), *KeyPath)
					: FString::Printf(TEXT("%s/Interpolation"), *KeyPath);
				return BodyFailure(TEXT("Curve key interpolation is not supported"), InterpPath, TEXT("InvalidCurveInterpolation"));
			}

			FRichCurveKey Key(TimeFloat, ValueFloat);
			Key.InterpMode = InterpMode;
			ParsedCurve.Keys.Add(Key);
		}

		TArray<FParsedAnimSequenceCurveKey> SortedKeys;
		SortedKeys.Reserve(ParsedCurve.Keys.Num());
		for (int32 KeyIndex = 0; KeyIndex < ParsedCurve.Keys.Num(); ++KeyIndex)
		{
			FParsedAnimSequenceCurveKey SortedKey;
			SortedKey.AuthoredIndex = KeyIndex;
			SortedKey.Key = ParsedCurve.Keys[KeyIndex];
			SortedKeys.Add(SortedKey);
		}
		SortedKeys.Sort([](const FParsedAnimSequenceCurveKey& Left, const FParsedAnimSequenceCurveKey& Right)
		{
			if (Left.Key.Time == Right.Key.Time)
			{
				return Left.AuthoredIndex < Right.AuthoredIndex;
			}
			return Left.Key.Time < Right.Key.Time;
		});
		for (int32 KeyIndex = 1; KeyIndex < SortedKeys.Num(); ++KeyIndex)
		{
			if (SortedKeys[KeyIndex - 1].Key.Time == SortedKeys[KeyIndex].Key.Time)
			{
				return BodyFailure(TEXT("Curve key times must be unique"), FString::Printf(TEXT("%s/Keys/%d/Time"), *CurvePath, SortedKeys[KeyIndex].AuthoredIndex), TEXT("DuplicateCurveKeyTime"));
			}
		}
		ParsedCurve.Keys.Reset(SortedKeys.Num());
		for (const FParsedAnimSequenceCurveKey& SortedKey : SortedKeys)
		{
			ParsedCurve.Keys.Add(SortedKey.Key);
		}

		OutCurves.Add(MoveTemp(ParsedCurve));
	}

	OutCurves.Sort([](const FParsedAnimSequenceCurve& Left, const FParsedAnimSequenceCurve& Right)
	{
		return Left.Name.LexicalLess(Right.Name);
	});
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyAnimSequenceCurvesToSequence(UAnimSequence* Sequence, const TArray<FParsedAnimSequenceCurve>& Curves)
{
	if (!Sequence)
	{
		return BodyFailure(TEXT("AnimSequence curve apply requires UAnimSequence asset"), TEXT("/Body/Curves"), TEXT("UnsupportedAsset"));
	}
	if (Curves.IsEmpty())
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("AnimSequence Curves patch is empty"));
	}

	IAnimationDataController& Controller = Sequence->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Apply AnimSequence AssetDocument Curves")), false);

	auto CloseAndFail = [&Controller](const FString& Message, const FString& Path, const FString& Code) -> FAssetDocumentCapabilityResult
	{
		Controller.CloseBracket(false);
		return BodyFailure(Message, Path, Code);
	};

	for (const FParsedAnimSequenceCurve& Curve : Curves)
	{
		const FAnimationCurveIdentifier CurveId(Curve.Name, ERawCurveTrackTypes::RCT_Float);
		const FString CurvePath = FString::Printf(TEXT("/Body/Curves/%s"), *Curve.Name.ToString());
		const IAnimationDataModel* DataModel = Sequence->GetDataModel();
		const bool bExistingCurve = DataModel && DataModel->FindCurve(CurveId) != nullptr;
		if (!bExistingCurve && !Controller.AddCurve(CurveId, Curve.Flags, false))
		{
			return CloseAndFail(TEXT("Failed to add AnimSequence float curve"), CurvePath, TEXT("CurveControllerFailure"));
		}
		if (!Controller.SetCurveKeys(CurveId, Curve.Keys, false))
		{
			return CloseAndFail(TEXT("Failed to set AnimSequence float curve keys"), CurvePath, TEXT("CurveControllerFailure"));
		}
		if (!Controller.SetCurveFlags(CurveId, Curve.Flags, false))
		{
			return CloseAndFail(TEXT("Failed to set AnimSequence float curve flags"), CurvePath, TEXT("CurveControllerFailure"));
		}
	}

	Controller.CloseBracket(false);
	return FAssetDocumentCapabilityResult::Success(TEXT("AnimSequence Curves applied"));
}

FAssetDocumentCapabilityResult ApplyAnimSequenceCurves(UAnimSequence* Sequence, const TArray<FParsedAnimSequenceCurve>& Curves)
{
	UAnimSequence* PreviewSequence = DuplicateObject<UAnimSequence>(Sequence, GetTransientPackage());
	if (!PreviewSequence)
	{
		return BodyFailure(TEXT("Failed to duplicate AnimSequence for curve validation"), TEXT("/Body/Curves"), TEXT("DuplicateFailed"));
	}

	const FAssetDocumentCapabilityResult PreviewResult = ApplyAnimSequenceCurvesToSequence(PreviewSequence, Curves);
	if (!PreviewResult.bSuccess)
	{
		return PreviewResult;
	}

	return ApplyAnimSequenceCurvesToSequence(Sequence, Curves);
}

TArray<TSharedPtr<FJsonValue>> ExtractAnimSequenceCurves(const UAnimSequence* Sequence)
{
	TArray<TSharedPtr<FJsonValue>> CurveValues;
	const IAnimationDataModel* DataModel = Sequence ? Sequence->GetDataModel() : nullptr;
	if (!DataModel)
	{
		return CurveValues;
	}

	TArray<FFloatCurveView> FloatCurves;
	for (const FFloatCurve& FloatCurve : DataModel->GetFloatCurves())
	{
		FloatCurves.Add({ &FloatCurve });
	}
	FloatCurves.Sort([](const FFloatCurveView& Left, const FFloatCurveView& Right)
	{
		if (!Left.Curve)
		{
			return Right.Curve != nullptr;
		}
		if (!Right.Curve)
		{
			return false;
		}
		return Left.Curve->GetName().LexicalLess(Right.Curve->GetName());
	});

	for (const FFloatCurveView& FloatCurveView : FloatCurves)
	{
		const FFloatCurve* FloatCurve = FloatCurveView.Curve;
		if (!FloatCurve)
		{
			continue;
		}

		TSharedRef<FJsonObject> CurveObject = MakeShared<FJsonObject>();
		CurveObject->SetStringField(TEXT("Name"), FloatCurve->GetName().ToString());
		CurveObject->SetStringField(TEXT("CurveType"), TEXT("Float"));
		CurveObject->SetArrayField(TEXT("Flags"), CurveFlagsToJsonArray(FloatCurve->GetCurveTypeFlags()));

		TArray<FRichCurveKey> Keys = FloatCurve->FloatCurve.GetCopyOfKeys();
		Keys.Sort([](const FRichCurveKey& Left, const FRichCurveKey& Right)
		{
			return Left.Time < Right.Time;
		});

		TArray<TSharedPtr<FJsonValue>> KeyValues;
		for (const FRichCurveKey& Key : Keys)
		{
			TSharedRef<FJsonObject> KeyObject = MakeShared<FJsonObject>();
			KeyObject->SetNumberField(TEXT("Time"), Key.Time);
			KeyObject->SetNumberField(TEXT("Value"), Key.Value);
			KeyObject->SetStringField(TEXT("InterpMode"), CurveInterpModeToString(Key.InterpMode));
			KeyValues.Add(MakeShared<FJsonValueObject>(KeyObject));
		}
		CurveObject->SetArrayField(TEXT("Keys"), KeyValues);
		CurveValues.Add(MakeShared<FJsonValueObject>(CurveObject));
	}

	return CurveValues;
}

const TCHAR* ManagedNotifyObjectPrefix = TEXT("AssetDocumentManaged_AnimSequenceNotify_");
const TCHAR* ManagedNotifyStateObjectPrefix = TEXT("AssetDocumentManaged_AnimSequenceNotifyState_");

struct FParsedAnimSequenceNotifyTrack
{
	FName Name = NAME_None;
};

struct FParsedAnimSequenceNotifyPlacement
{
	FName Name = NAME_None;
	FName NotifyName = NAME_None;
	float Time = 0.0f;
	FName TrackName = TEXT("Default");
	UClass* NotifyClass = nullptr;
	TSharedPtr<FJsonObject> NotifyFragment;
	int32 SourceIndex = INDEX_NONE;
};

struct FParsedAnimSequenceNotifyStatePlacement
{
	FName Name = NAME_None;
	float Time = 0.0f;
	float Duration = 0.0f;
	FName TrackName = TEXT("Default");
	UClass* NotifyStateClass = nullptr;
	TSharedPtr<FJsonObject> NotifyStateFragment;
	int32 SourceIndex = INDEX_NONE;
};

struct FParsedAnimSequenceSyncMarker
{
	FName Name = NAME_None;
	float Time = 0.0f;
};

FString BodyArrayItemPath(const TCHAR* SectionName, int32 Index)
{
	return FString::Printf(TEXT("/Body/%s/%d"), SectionName, Index);
}

FString BodyArrayFieldPath(const TCHAR* SectionName, int32 Index, const TCHAR* FieldName)
{
	return FString::Printf(TEXT("/Body/%s/%d/%s"), SectionName, Index, FieldName);
}

bool FindAnimSequencePilotPolicy(const FName BodyKey, FAssetDocumentRegionPolicy& OutPolicy)
{
	const FName RegionId(*FString::Printf(TEXT("Body.%s"), *BodyKey.ToString()));
	for (const FAssetDocumentRegionPolicy& Policy : FAnimSequenceAssetDocumentProfile::MakePilotRegionPolicies())
	{
		if (Policy.RegionId == RegionId)
		{
			OutPolicy = Policy;
			return true;
		}
	}
	return false;
}

FAssetDocumentCapabilityResult MissingAnimSequencePilotPolicyFailure(const FName BodyKey)
{
	return BodyFailure(
		FString::Printf(TEXT("Missing AnimSequence pilot profile policy for Body.%s"), *BodyKey.ToString()),
		FAssetDocumentJsonRegionUtils::MakeBodyPath(BodyKey.ToString()),
		TEXT("MissingPilotRegionPolicy"));
}

FAssetDocumentRegionContext MakeAnimSequencePilotRegionContext(
	const FAssetDocumentCapabilityContext& CapabilityContext,
	const FName BodyKey,
	const FAssetDocumentRegionPolicy& Policy)
{
	FAssetDocumentRegionContext RegionContext;
	RegionContext.Asset = CapabilityContext.Asset;
	RegionContext.AssetClass = CapabilityContext.AssetClass;
	RegionContext.TargetAssetPath = CapabilityContext.TargetAssetPath;
	RegionContext.SourceDocumentPath = CapabilityContext.SourceDocumentPath;
	RegionContext.Definitions = CapabilityContext.Definitions;
	RegionContext.Result = CapabilityContext.Result;
	RegionContext.bIsDryRun = CapabilityContext.bIsDryRun;
	RegionContext.Policy = &Policy;
	RegionContext.RegionId = Policy.RegionId;
	RegionContext.BodyPath = Policy.BodyPath;
	RegionContext.JsonPointer = FAssetDocumentJsonRegionUtils::MakeBodyPath(BodyKey.ToString());
	return RegionContext;
}

bool HasManagedObjectName(const UObject* Object, const TCHAR* Prefix)
{
	return Object && Object->GetName().StartsWith(Prefix);
}

void MarkManagedNotifyObject(UObject* NotifyObject, UAnimSequence* Sequence, bool bState)
{
	if (!NotifyObject || !Sequence)
	{
		return;
	}

	const TCHAR* Prefix = bState ? ManagedNotifyStateObjectPrefix : ManagedNotifyObjectPrefix;
	const FName ManagedObjectName = MakeUniqueObjectName(Sequence, NotifyObject->GetClass(), Prefix);
	NotifyObject->Rename(*ManagedObjectName.ToString(), Sequence, REN_DontCreateRedirectors | REN_NonTransactional);
}

bool IsManagedAnimSequenceNotifyEvent(const FAnimNotifyEvent& Event, const UAnimSequence* Sequence)
{
	return Event.Notify
		&& HasManagedObjectName(Event.Notify, ManagedNotifyObjectPrefix)
		&& (!Sequence || Event.Notify->GetOuter() == Sequence);
}

bool IsManagedAnimSequenceNotifyStateEvent(const FAnimNotifyEvent& Event, const UAnimSequence* Sequence)
{
	return Event.NotifyStateClass
		&& HasManagedObjectName(Event.NotifyStateClass, ManagedNotifyStateObjectPrefix)
		&& (!Sequence || Event.NotifyStateClass->GetOuter() == Sequence);
}

FAssetDocumentCapabilityResult RejectUnknownArrayObjectFields(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* SectionName,
	int32 Index,
	const TArray<FString>& AllowedFields)
{
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		if (!AllowedFields.Contains(Pair.Key))
		{
			const FString Path = BodyArrayFieldPath(SectionName, Index, *Pair.Key);
			return BodyFailure(FString::Printf(TEXT("Body.%s.%s is not supported"), SectionName, *Pair.Key), Path, TEXT("UnsupportedAuthoredField"));
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadRequiredStringField(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* FieldName,
	const FString& Path,
	FString& OutValue)
{
	if (!Object->TryGetStringField(FieldName, OutValue) || OutValue.TrimStartAndEnd().IsEmpty())
	{
		return BodyFailure(FString::Printf(TEXT("%s must be a non-empty string"), FieldName), Path, TEXT("InvalidStringField"));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadRequiredNumberField(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* FieldName,
	const FString& Path,
	double& OutValue)
{
	const TSharedPtr<FJsonValue>* Value = Object->Values.Find(FieldName);
	if (!Value || !Value->IsValid() || (*Value)->Type != EJson::Number)
	{
		return BodyFailure(FString::Printf(TEXT("%s must be a number"), FieldName), Path, TEXT("InvalidNumericField"));
	}
	OutValue = (*Value)->AsNumber();
	if (!std::isfinite(OutValue) || OutValue < -static_cast<double>(MAX_flt) || OutValue > static_cast<double>(MAX_flt))
	{
		return BodyFailure(FString::Printf(TEXT("%s must fit in a float"), FieldName), Path, TEXT("InvalidNumericField"));
	}
	const float FloatValue = static_cast<float>(OutValue);
	if (!FMath::IsFinite(FloatValue))
	{
		return BodyFailure(FString::Printf(TEXT("%s must fit in a finite float"), FieldName), Path, TEXT("InvalidNumericField"));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateTimelineTime(const UAnimSequence* Sequence, double Time, const FString& Path, const FString& Code)
{
	if (!std::isfinite(Time) || Time > static_cast<double>(MAX_flt))
	{
		return BodyFailure(TEXT("Timeline time must be finite and fit in a float"), Path, Code);
	}
	if (Time < 0.0)
	{
		return BodyFailure(TEXT("Timeline time must be non-negative"), Path, Code);
	}

	if (Sequence)
	{
		double PlayLength = Sequence->GetPlayLength();
		if (PlayLength <= 0.0)
		{
			if (const IAnimationDataModel* DataModel = Sequence->GetDataModel())
			{
				PlayLength = DataModel->GetPlayLength();
			}
		}
		if (PlayLength > 0.0 && Time > PlayLength + UE_KINDA_SMALL_NUMBER)
		{
			return BodyFailure(TEXT("Timeline time must be within the AnimSequence play length"), Path, Code);
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

double GetAnimSequenceTimelineMaxTime(const UAnimSequence* Sequence)
{
	if (!Sequence)
	{
		return 0.0;
	}

	double PlayLength = Sequence->GetPlayLength();
	if (PlayLength <= 0.0)
	{
		if (const IAnimationDataModel* DataModel = Sequence->GetDataModel())
		{
			PlayLength = DataModel->GetPlayLength();
		}
	}
	return PlayLength;
}

FAssetDocumentCapabilityResult MapAnimSequenceSyncMarkerTimelineFailure(
	const FAssetDocumentCapabilityResult& Result)
{
	if (Result.bSuccess || Result.Diagnostics.IsEmpty())
	{
		return Result;
	}

	const FAssetDocumentDiagnostic& Diagnostic = Result.Diagnostics[0];
	FString Path = Diagnostic.Path;
	FString Code = Diagnostic.Code;
	if (Code == TEXT("InvalidTimelinePlacementRegionType")
		|| Code == TEXT("InvalidTimelinePlacementEntryType"))
	{
		Code = TEXT("InvalidBodySectionType");
	}
	else if (Code == TEXT("InvalidTimelinePlacementNumber")
		|| Code == TEXT("MissingTimelinePlacementTime"))
	{
		Code = TEXT("InvalidNumericField");
	}
	else if (Code == TEXT("InvalidTimelinePlacementTime"))
	{
		Code = TEXT("InvalidSyncMarkerTime");
	}
	else if (Code == TEXT("InvalidTimelinePlacementName")
		|| Code == TEXT("MissingTimelinePlacementName"))
	{
		Code = TEXT("InvalidStringField");
	}
	else if (Code == TEXT("DuplicateTimelinePlacementKey"))
	{
		Code = TEXT("DuplicateSyncMarkerKey");
		if (!Path.EndsWith(TEXT("/Name")))
		{
			Path = FAssetDocumentTimelinePlacementUtils::MakeFieldPath(Path, TEXT("Name"));
		}
	}

	return BodyFailure(Result.Message, Path, Code);
}

FAssetDocumentCapabilityResult MapAnimSequenceNotifyTimelineFailure(
	const FAssetDocumentCapabilityResult& Result,
	const TCHAR* SectionName,
	const TCHAR* DuplicateCode)
{
	if (Result.bSuccess || Result.Diagnostics.IsEmpty())
	{
		return Result;
	}

	const FAssetDocumentDiagnostic& Diagnostic = Result.Diagnostics[0];
	FString Path = Diagnostic.Path;
	FString Code = Diagnostic.Code;
	if (Code == TEXT("InvalidTimelinePlacementRegionType")
		|| Code == TEXT("InvalidTimelinePlacementEntryType"))
	{
		Code = TEXT("InvalidBodySectionType");
	}
	else if (Code == TEXT("InvalidTimelinePlacementNumber")
		|| Code == TEXT("MissingTimelinePlacementTime")
		|| Code == TEXT("MissingTimelinePlacementDuration"))
	{
		Code = TEXT("InvalidNumericField");
	}
	else if (Code == TEXT("InvalidTimelinePlacementTime"))
	{
		Code = TEXT("InvalidNotifyTime");
	}
	else if (Code == TEXT("InvalidTimelinePlacementDuration"))
	{
		Code = Result.Message.Contains(TEXT("positive"))
			? FString(TEXT("InvalidNotifyStateDuration"))
			: FString(TEXT("InvalidNumericField"));
	}
	else if (Code == TEXT("InvalidTimelinePlacementEndTime"))
	{
		Code = TEXT("InvalidNotifyStateDuration");
	}
	else if (Code == TEXT("InvalidTimelinePlacementName")
		|| Code == TEXT("MissingTimelinePlacementName")
		|| Code == TEXT("InvalidTimelinePlacementTrackName"))
	{
		Code = TEXT("InvalidStringField");
	}
	else if (Code == TEXT("DuplicateTimelinePlacementKey"))
	{
		Code = DuplicateCode;
		if (!Path.EndsWith(TEXT("/Name")))
		{
			Path = FAssetDocumentTimelinePlacementUtils::MakeFieldPath(Path, TEXT("Name"));
		}
	}

	return BodyFailure(Result.Message, Path, Code);
}

FAssetDocumentCapabilityResult ValidateAnimSequenceSyncMarkerTimeFloatSafety(
	const TSharedPtr<FJsonValue>& SectionValue)
{
	if (!SectionValue.IsValid() || SectionValue->Type != EJson::Array)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const TArray<TSharedPtr<FJsonValue>>& Values = SectionValue->AsArray();
	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		const TSharedPtr<FJsonValue>& EntryValue = Values[Index];
		if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
		{
			continue;
		}

		const TSharedPtr<FJsonObject> EntryObject = EntryValue->AsObject();
		if (!EntryObject.IsValid())
		{
			continue;
		}

		const TSharedPtr<FJsonValue> TimeValue = EntryObject->TryGetField(TEXT("Time"));
		if (!TimeValue.IsValid() || TimeValue->Type != EJson::Number)
		{
			continue;
		}

		const double Time = TimeValue->AsNumber();
		if (!std::isfinite(Time) || Time < -static_cast<double>(MAX_flt) || Time > static_cast<double>(MAX_flt))
		{
			return BodyFailure(TEXT("Time must fit in a float"), BodyArrayFieldPath(TEXT("SyncMarkers"), Index, TEXT("Time")), TEXT("InvalidNumericField"));
		}
		const float FloatTime = static_cast<float>(Time);
		if (!FMath::IsFinite(FloatTime))
		{
			return BodyFailure(TEXT("Time must fit in a finite float"), BodyArrayFieldPath(TEXT("SyncMarkers"), Index, TEXT("Time")), TEXT("InvalidNumericField"));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateAnimSequencePlacementFloatSafety(
	const TSharedPtr<FJsonValue>& SectionValue,
	const TCHAR* SectionName,
	const TArray<const TCHAR*>& NumericFieldNames)
{
	if (!SectionValue.IsValid() || SectionValue->Type != EJson::Array)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const TArray<TSharedPtr<FJsonValue>>& Values = SectionValue->AsArray();
	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		const TSharedPtr<FJsonValue>& EntryValue = Values[Index];
		if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
		{
			continue;
		}

		const TSharedPtr<FJsonObject> EntryObject = EntryValue->AsObject();
		if (!EntryObject.IsValid())
		{
			continue;
		}

		for (const TCHAR* FieldName : NumericFieldNames)
		{
			const TSharedPtr<FJsonValue> NumberValue = EntryObject->TryGetField(FieldName);
			if (!NumberValue.IsValid() || NumberValue->Type != EJson::Number)
			{
				continue;
			}

			const double Number = NumberValue->AsNumber();
			if (!std::isfinite(Number) || Number < -static_cast<double>(MAX_flt) || Number > static_cast<double>(MAX_flt))
			{
				return BodyFailure(
					FString::Printf(TEXT("%s must fit in a float"), FieldName),
					BodyArrayFieldPath(SectionName, Index, FieldName),
					TEXT("InvalidNumericField"));
			}
			const float FloatNumber = static_cast<float>(Number);
			if (!FMath::IsFinite(FloatNumber))
			{
				return BodyFailure(
					FString::Printf(TEXT("%s must fit in a finite float"), FieldName),
					BodyArrayFieldPath(SectionName, Index, FieldName),
					TEXT("InvalidNumericField"));
			}
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ResolveNotifyObjectFragmentClass(
	const TSharedRef<FJsonObject>& FragmentObject,
	UClass* ExpectedBaseClass,
	const FString& Path,
	const FString& InvalidCode,
	UClass*& OutClass)
{
	OutClass = nullptr;

	FString Kind;
	FAssetDocumentCapabilityResult Result = ReadRequiredStringField(FragmentObject, TEXT("Kind"), Path / TEXT("Kind"), Kind);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (Kind != TEXT("EmbeddedObject") && Kind != TEXT("ClassRef"))
	{
		return BodyFailure(TEXT("Notify object fragment must be EmbeddedObject or ClassRef"), Path / TEXT("Kind"), InvalidCode);
	}

	FString ClassPath;
	if (!FragmentObject->TryGetStringField(TEXT("Class"), ClassPath) || ClassPath.TrimStartAndEnd().IsEmpty())
	{
		if (!FragmentObject->TryGetStringField(TEXT("Path"), ClassPath) || ClassPath.TrimStartAndEnd().IsEmpty())
		{
			return BodyFailure(TEXT("Notify object fragment requires Class"), Path / TEXT("Class"), InvalidCode);
		}
	}

	UClass* ResolvedClass = FindObject<UClass>(nullptr, *ClassPath);
	if (!ResolvedClass)
	{
		ResolvedClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath);
	}
	if (!ResolvedClass || !ResolvedClass->IsChildOf(ExpectedBaseClass))
	{
		return BodyFailure(TEXT("Notify object fragment did not resolve to the expected notify class"), Path, InvalidCode);
	}
	if (ResolvedClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return BodyFailure(TEXT("Notify class is abstract and cannot be instantiated"), Path, InvalidCode);
	}

	OutClass = ResolvedClass;
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ResolveNotifyObjectFragmentField(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* FieldName,
	UClass* ExpectedBaseClass,
	const FString& Path,
	const FString& InvalidCode,
	bool& bOutHasFragment,
	TSharedPtr<FJsonObject>& OutFragment,
	UClass*& OutClass)
{
	bOutHasFragment = false;
	OutFragment.Reset();
	OutClass = nullptr;

	const TSharedPtr<FJsonValue>* Value = Object->Values.Find(FieldName);
	if (!Value)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FAssetDocumentCapabilityResult Result = RequireObjectValue(*Value, Path, OutFragment);
	if (!Result.bSuccess)
	{
		return Result;
	}

	FString Kind;
	Result = ReadRequiredStringField(OutFragment.ToSharedRef(), TEXT("Kind"), Path / TEXT("Kind"), Kind);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ResolveNotifyObjectFragmentClass(OutFragment.ToSharedRef(), ExpectedBaseClass, Path, InvalidCode, OutClass);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (Kind == TEXT("EmbeddedObject"))
	{
		const TSharedPtr<FJsonValue>* PropertiesValue = OutFragment->Values.Find(TEXT("Properties"));
		if (PropertiesValue)
		{
			TSharedPtr<FJsonObject> PropertiesObject;
			Result = RequireObjectValue(*PropertiesValue, Path / TEXT("Properties"), PropertiesObject);
			if (!Result.bSuccess)
			{
				return Result;
			}

			const FAssetDocumentPropertyApplyResult PropertyPreflightResult = FAssetDocumentPropertyAdapter::PreflightProperties(OutClass, PropertiesObject);
			if (!PropertyPreflightResult.bSuccess)
			{
				return BodyFailure(
					PropertyPreflightResult.Message.IsEmpty() ? TEXT("Notify object fragment property preflight failed") : PropertyPreflightResult.Message,
					Path,
					TEXT("embeddedobject-preflight-failed"));
			}
		}
	}

	bOutHasFragment = true;
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ResolveClassRefField(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* FieldName,
	UClass* ExpectedBaseClass,
	const FString& Path,
	const FString& InvalidCode,
	bool& bOutHasClass,
	UClass*& OutClass)
{
	bOutHasClass = false;
	OutClass = nullptr;
	const TSharedPtr<FJsonValue>* Value = Object->Values.Find(FieldName);
	if (!Value)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TSharedPtr<FJsonObject> ClassObject;
	FAssetDocumentCapabilityResult Result = RequireObjectValue(*Value, Path, ClassObject);
	if (!Result.bSuccess)
	{
		return Result;
	}

	FString Kind;
	Result = ReadRequiredStringField(ClassObject.ToSharedRef(), TEXT("Kind"), Path / TEXT("Kind"), Kind);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (Kind != TEXT("ClassRef"))
	{
		return BodyFailure(TEXT("Class must be a ClassRef fragment"), Path / TEXT("Kind"), InvalidCode);
	}

	FString ClassPath;
	Result = ReadRequiredStringField(ClassObject.ToSharedRef(), TEXT("Path"), Path / TEXT("Path"), ClassPath);
	if (!Result.bSuccess)
	{
		return Result;
	}

	UClass* ResolvedClass = FindObject<UClass>(nullptr, *ClassPath);
	if (!ResolvedClass)
	{
		ResolvedClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath);
	}
	if (!ResolvedClass || !ResolvedClass->IsChildOf(ExpectedBaseClass))
	{
		return BodyFailure(TEXT("ClassRef did not resolve to the expected notify class"), Path, InvalidCode);
	}
	if (ResolvedClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return BodyFailure(TEXT("Notify class is abstract and cannot be instantiated"), Path, InvalidCode);
	}

	bOutHasClass = true;
	OutClass = ResolvedClass;
	return FAssetDocumentCapabilityResult::Success();
}

FString ReadClassRefIdentityForDuplicateKey(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* ClassFieldName,
	const TCHAR* ObjectFieldName)
{
	const TSharedPtr<FJsonObject>* FragmentObject = nullptr;
	if (Object->TryGetObjectField(ObjectFieldName, FragmentObject) && FragmentObject && FragmentObject->IsValid())
	{
		FString ClassPath;
		if ((*FragmentObject)->TryGetStringField(TEXT("Class"), ClassPath) && !ClassPath.TrimStartAndEnd().IsEmpty())
		{
			return ClassPath;
		}
		if ((*FragmentObject)->TryGetStringField(TEXT("Path"), ClassPath) && !ClassPath.TrimStartAndEnd().IsEmpty())
		{
			return ClassPath;
		}
	}

	const TSharedPtr<FJsonObject>* ClassObject = nullptr;
	if (Object->TryGetObjectField(ClassFieldName, ClassObject) && ClassObject && ClassObject->IsValid())
	{
		FString ClassPath;
		if ((*ClassObject)->TryGetStringField(TEXT("Path"), ClassPath) && !ClassPath.TrimStartAndEnd().IsEmpty())
		{
			return ClassPath;
		}
	}

	return TEXT("Named");
}

FString ReadPlacementTrackName(const TSharedRef<FJsonObject>& Object)
{
	FString TrackName;
	if (Object->TryGetStringField(TEXT("Track"), TrackName) && !TrackName.TrimStartAndEnd().IsEmpty())
	{
		return TrackName;
	}
	if (Object->TryGetStringField(TEXT("TrackName"), TrackName) && !TrackName.TrimStartAndEnd().IsEmpty())
	{
		return TrackName;
	}
	return TEXT("Default");
}

FAssetDocumentCapabilityResult RejectAmbiguousTrackAlias(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* SectionName,
	int32 Index)
{
	if (Object->Values.Contains(TEXT("Track")) && Object->Values.Contains(TEXT("TrackName")))
	{
		return BodyFailure(TEXT("Track and TrackName cannot both be authored"), BodyArrayFieldPath(SectionName, Index, TEXT("TrackName")), TEXT("AmbiguousTrackNameAlias"));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseAnimSequenceNotifyTracks(
	const TSharedRef<FJsonObject>& BodyObject,
	bool& bOutHasTracks,
	TArray<FParsedAnimSequenceNotifyTrack>& OutTracks)
{
	bOutHasTracks = false;
	OutTracks.Reset();
	const TSharedPtr<FJsonValue>* SectionValue = BodyObject->Values.Find(TEXT("NotifyTracks"));
	if (!SectionValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	bOutHasTracks = true;
	const FAssetDocumentNamedArrayRegionAdapterConfig NotifyTracksConfig =
		FAnimSequenceAssetDocumentProfile::MakeNotifyTracksNamedArrayConfig();
	const TArray<FString> NotifyTracksIdentityFields =
		FAnimSequenceAssetDocumentProfile::MakeNotifyTracksIdentityFieldNames();
	FAssetDocumentNamedArrayRegionAdapterHooks Hooks;
	Hooks.ValidateElement = [&OutTracks, NotifyTracksConfig, NotifyTracksIdentityFields](const FAssetDocumentRegionContext&, const TSharedRef<FJsonObject>& TrackObject, const int32 Index)
	{
		FAssetDocumentCapabilityResult Result = RejectUnknownArrayObjectFields(TrackObject, TEXT("NotifyTracks"), Index, NotifyTracksIdentityFields);
		if (!Result.bSuccess)
		{
			return Result;
		}
		for (const FString& AliasField : NotifyTracksConfig.IdentityAliases)
		{
			if (TrackObject->Values.Contains(AliasField) && TrackObject->Values.Contains(NotifyTracksConfig.IdentityField))
			{
				return BodyFailure(
					FString::Printf(TEXT("NotifyTracks item cannot author both %s and %s"), *AliasField, *NotifyTracksConfig.IdentityField),
					FAnimSequenceAssetDocumentProfile::MakeNotifyTracksIdentityJsonPointer(Index),
					TEXT("AmbiguousTrackNameAlias"));
			}
		}

		FString NameString;
		if (!TrackObject->TryGetStringField(NotifyTracksConfig.IdentityField, NameString) || NameString.TrimStartAndEnd().IsEmpty())
		{
			const FString& FallbackField = NotifyTracksConfig.IdentityAliases.Num() > 0
				? NotifyTracksConfig.IdentityAliases[0]
				: NotifyTracksConfig.IdentityField;
			Result = ReadRequiredStringField(
				TrackObject,
				*FallbackField,
				FAnimSequenceAssetDocumentProfile::MakeNotifyTracksIdentityJsonPointer(Index),
				NameString);
			if (!Result.bSuccess)
			{
				return Result;
			}
		}
		const FName TrackName(*NameString);
		if (TrackName.IsNone())
		{
			return BodyFailure(
				TEXT("Notify track name cannot be None"),
				FAnimSequenceAssetDocumentProfile::MakeNotifyTracksIdentityJsonPointer(Index),
				TEXT("InvalidNotifyTrackName"));
		}

		FParsedAnimSequenceNotifyTrack ParsedTrack;
		ParsedTrack.Name = TrackName;
		OutTracks.Add(ParsedTrack);
		return FAssetDocumentCapabilityResult::Success();
	};

	FAssetDocumentNamedArrayRegionAdapter Adapter(
		NotifyTracksConfig,
		MoveTemp(Hooks));
	FAssetDocumentRegionPolicy Policy;
	if (!FindAnimSequencePilotPolicy(TEXT("NotifyTracks"), Policy))
	{
		return MissingAnimSequencePilotPolicyFailure(TEXT("NotifyTracks"));
	}
	const FAssetDocumentCapabilityContext CapabilityContext;
	const FAssetDocumentRegionContext RegionContext =
		MakeAnimSequencePilotRegionContext(CapabilityContext, TEXT("NotifyTracks"), Policy);

	return FAssetDocumentRegionRuntime::Validate(RegionContext, *SectionValue, Adapter);
}

FAssetDocumentCapabilityResult ParseAnimSequenceNotifies(
	const TSharedRef<FJsonObject>& BodyObject,
	const UAnimSequence* Sequence,
	bool& bOutHasNotifies,
	TArray<FParsedAnimSequenceNotifyPlacement>& OutNotifies)
{
	bOutHasNotifies = false;
	OutNotifies.Reset();
	const TSharedPtr<FJsonValue>* SectionValue = BodyObject->Values.Find(TEXT("Notifies"));
	if (!SectionValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	bOutHasNotifies = true;
	FAssetDocumentCapabilityResult Result =
		ValidateAnimSequencePlacementFloatSafety(*SectionValue, TEXT("Notifies"), { TEXT("Time") });
	if (!Result.bSuccess)
	{
		return Result;
	}

	FAssetDocumentTimelinePlacementRegionConfig Config;
	Config.AdapterName = TEXT("AnimSequenceNotifiesTimelinePlacement");
	Config.RegionId = TEXT("Body.Notifies");
	Config.BodyPath = TEXT("Notifies");
	Config.JsonPointer = TEXT("/Body/Notifies");
	Config.TimeFieldName = TEXT("Time");
	Config.TrackNameFieldName = TEXT("TrackName");
	Config.bHasTrackIdentity = true;

	FAssetDocumentTimelinePlacementHooks Hooks;
	Hooks.BuildDuplicateKey = [](const FAssetDocumentTimelinePlacementEntry& Entry)
	{
		const TSharedRef<FJsonObject> NotifyObject = Entry.EntryObject.ToSharedRef();
		FString NameString;
		NotifyObject->TryGetStringField(TEXT("Name"), NameString);
		FString NotifyNameString;
		const bool bHasNotifyName =
			NotifyObject->TryGetStringField(TEXT("NotifyName"), NotifyNameString)
			&& !NotifyNameString.TrimStartAndEnd().IsEmpty();
		const float Time = Entry.Time.IsSet() ? static_cast<float>(Entry.Time.GetValue()) : 0.0f;
		return FString::Printf(TEXT("%s|%.6f|%s|%s|%s"),
			*NameString,
			Time,
			bHasNotifyName ? *NotifyNameString : *NameString,
			*ReadClassRefIdentityForDuplicateKey(NotifyObject, TEXT("Class"), TEXT("Notify")),
			*ReadPlacementTrackName(NotifyObject));
	};
	Hooks.Validate = [Sequence, &OutNotifies](const FAssetDocumentRegionContext&, TArray<FAssetDocumentTimelinePlacementEntry>& Entries)
	{
		for (const FAssetDocumentTimelinePlacementEntry& Entry : Entries)
		{
			const TSharedRef<FJsonObject> NotifyObject = Entry.EntryObject.ToSharedRef();
			FAssetDocumentCapabilityResult Result = RejectUnknownArrayObjectFields(NotifyObject, TEXT("Notifies"), Entry.Index, { TEXT("Name"), TEXT("Time"), TEXT("NotifyName"), TEXT("Notify"), TEXT("Class"), TEXT("Track"), TEXT("TrackName") });
			if (!Result.bSuccess)
			{
				return Result;
			}
			Result = RejectAmbiguousTrackAlias(NotifyObject, TEXT("Notifies"), Entry.Index);
			if (!Result.bSuccess)
			{
				return Result;
			}

			FString NameString;
			Result = ReadRequiredStringField(NotifyObject, TEXT("Name"), BodyArrayFieldPath(TEXT("Notifies"), Entry.Index, TEXT("Name")), NameString);
			if (!Result.bSuccess)
			{
				return Result;
			}

			const double Time = Entry.Time.GetValue();
			Result = ValidateTimelineTime(Sequence, Time, BodyArrayFieldPath(TEXT("Notifies"), Entry.Index, TEXT("Time")), TEXT("InvalidNotifyTime"));
			if (!Result.bSuccess)
			{
				return Result;
			}

			bool bHasClass = false;
			UClass* NotifyClass = nullptr;
			Result = ResolveClassRefField(NotifyObject, TEXT("Class"), UAnimNotify::StaticClass(), BodyArrayFieldPath(TEXT("Notifies"), Entry.Index, TEXT("Class")), TEXT("InvalidNotifyClass"), bHasClass, NotifyClass);
			if (!Result.bSuccess)
			{
				return Result;
			}

			bool bHasNotifyFragment = false;
			TSharedPtr<FJsonObject> NotifyFragment;
			UClass* NotifyFragmentClass = nullptr;
			Result = ResolveNotifyObjectFragmentField(NotifyObject, TEXT("Notify"), UAnimNotify::StaticClass(), BodyArrayFieldPath(TEXT("Notifies"), Entry.Index, TEXT("Notify")), TEXT("InvalidNotifyObject"), bHasNotifyFragment, NotifyFragment, NotifyFragmentClass);
			if (!Result.bSuccess)
			{
				return Result;
			}
			if (bHasClass && bHasNotifyFragment)
			{
				return BodyFailure(TEXT("Notify placement cannot declare both Class and Notify"), BodyArrayItemPath(TEXT("Notifies"), Entry.Index), TEXT("AmbiguousNotifyObject"));
			}

			FString NotifyNameString;
			const bool bHasNotifyName = NotifyObject->TryGetStringField(TEXT("NotifyName"), NotifyNameString) && !NotifyNameString.TrimStartAndEnd().IsEmpty();
			if (!bHasClass && !bHasNotifyFragment)
			{
				return BodyFailure(TEXT("Notify placement requires Notify or Class"), BodyArrayItemPath(TEXT("Notifies"), Entry.Index), TEXT("MissingNotifyObject"));
			}

			FParsedAnimSequenceNotifyPlacement ParsedNotify;
			ParsedNotify.Name = FName(*NameString);
			ParsedNotify.NotifyName = bHasNotifyName ? FName(*NotifyNameString) : ParsedNotify.Name;
			ParsedNotify.Time = static_cast<float>(Time);
			ParsedNotify.TrackName = FName(*ReadPlacementTrackName(NotifyObject));
			ParsedNotify.NotifyClass = bHasNotifyFragment ? NotifyFragmentClass : NotifyClass;
			ParsedNotify.NotifyFragment = NotifyFragment;
			ParsedNotify.SourceIndex = Entry.Index;
			OutNotifies.Add(ParsedNotify);
		}
		return FAssetDocumentCapabilityResult::Success();
	};

	TArray<FAssetDocumentTimelinePlacementEntry> Entries;
	Result = FAssetDocumentTimelinePlacementUtils::ParsePlacementEntries(
		*SectionValue,
		Config,
		TEXT("/Body/Notifies"),
		nullptr,
		Entries,
		&Hooks);
	if (!Result.bSuccess)
	{
		return MapAnimSequenceNotifyTimelineFailure(Result, TEXT("Notifies"), TEXT("DuplicateNotifyKey"));
	}

	FAssetDocumentRegionContext RegionContext;
	Result = Hooks.Validate(RegionContext, Entries);
	if (!Result.bSuccess)
	{
		return Result;
	}

	OutNotifies.Sort([](const FParsedAnimSequenceNotifyPlacement& Left, const FParsedAnimSequenceNotifyPlacement& Right)
	{
		if (!FMath::IsNearlyEqual(Left.Time, Right.Time))
		{
			return Left.Time < Right.Time;
		}
		return Left.Name.LexicalLess(Right.Name);
	});
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseAnimSequenceNotifyStates(
	const TSharedRef<FJsonObject>& BodyObject,
	const UAnimSequence* Sequence,
	bool& bOutHasNotifyStates,
	TArray<FParsedAnimSequenceNotifyStatePlacement>& OutNotifyStates)
{
	bOutHasNotifyStates = false;
	OutNotifyStates.Reset();
	const TSharedPtr<FJsonValue>* SectionValue = BodyObject->Values.Find(TEXT("NotifyStates"));
	if (!SectionValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	bOutHasNotifyStates = true;
	FAssetDocumentCapabilityResult Result =
		ValidateAnimSequencePlacementFloatSafety(*SectionValue, TEXT("NotifyStates"), { TEXT("Time"), TEXT("Duration") });
	if (!Result.bSuccess)
	{
		return Result;
	}

	FAssetDocumentTimelinePlacementRegionConfig Config;
	Config.AdapterName = TEXT("AnimSequenceNotifyStatesTimelinePlacement");
	Config.RegionId = TEXT("Body.NotifyStates");
	Config.BodyPath = TEXT("NotifyStates");
	Config.JsonPointer = TEXT("/Body/NotifyStates");
	Config.TimeFieldName = TEXT("Time");
	Config.DurationFieldName = TEXT("Duration");
	Config.bHasDuration = true;
	Config.bRequireDuration = true;
	Config.bRequirePositiveDuration = true;
	Config.bValidateEndTime = true;
	Config.TrackNameFieldName = TEXT("TrackName");
	Config.bHasTrackIdentity = true;

	FAssetDocumentTimelineRange Range;
	const double MaxTime = GetAnimSequenceTimelineMaxTime(Sequence);
	if (MaxTime > 0.0)
	{
		Range.MaxTime = MaxTime + UE_KINDA_SMALL_NUMBER;
		Range.bHasMaxTime = true;
	}

	FAssetDocumentTimelinePlacementHooks Hooks;
	Hooks.BuildDuplicateKey = [](const FAssetDocumentTimelinePlacementEntry& Entry)
	{
		const TSharedRef<FJsonObject> StateObject = Entry.EntryObject.ToSharedRef();
		FString NameString;
		StateObject->TryGetStringField(TEXT("Name"), NameString);
		const float Time = Entry.Time.IsSet() ? static_cast<float>(Entry.Time.GetValue()) : 0.0f;
		const float Duration = Entry.Duration.IsSet() ? static_cast<float>(Entry.Duration.GetValue()) : 0.0f;
		return FString::Printf(TEXT("%s|%.6f|%.6f|%s|%s"),
			*NameString,
			Time,
			Duration,
			*ReadClassRefIdentityForDuplicateKey(StateObject, TEXT("Class"), TEXT("NotifyState")),
			*ReadPlacementTrackName(StateObject));
	};
	Hooks.Validate = [Sequence, &OutNotifyStates](const FAssetDocumentRegionContext&, TArray<FAssetDocumentTimelinePlacementEntry>& Entries)
	{
		for (const FAssetDocumentTimelinePlacementEntry& Entry : Entries)
		{
			const TSharedRef<FJsonObject> StateObject = Entry.EntryObject.ToSharedRef();
			FAssetDocumentCapabilityResult Result = RejectUnknownArrayObjectFields(StateObject, TEXT("NotifyStates"), Entry.Index, { TEXT("Name"), TEXT("Time"), TEXT("Duration"), TEXT("NotifyState"), TEXT("Class"), TEXT("Track"), TEXT("TrackName") });
			if (!Result.bSuccess)
			{
				return Result;
			}
			Result = RejectAmbiguousTrackAlias(StateObject, TEXT("NotifyStates"), Entry.Index);
			if (!Result.bSuccess)
			{
				return Result;
			}

			FString NameString;
			Result = ReadRequiredStringField(StateObject, TEXT("Name"), BodyArrayFieldPath(TEXT("NotifyStates"), Entry.Index, TEXT("Name")), NameString);
			if (!Result.bSuccess)
			{
				return Result;
			}

			const double Time = Entry.Time.GetValue();
			Result = ValidateTimelineTime(Sequence, Time, BodyArrayFieldPath(TEXT("NotifyStates"), Entry.Index, TEXT("Time")), TEXT("InvalidNotifyTime"));
			if (!Result.bSuccess)
			{
				return Result;
			}

			const double Duration = Entry.Duration.GetValue();
			Result = ValidateTimelineTime(Sequence, Time + Duration, BodyArrayFieldPath(TEXT("NotifyStates"), Entry.Index, TEXT("Duration")), TEXT("InvalidNotifyStateDuration"));
			if (!Result.bSuccess)
			{
				return Result;
			}

			bool bHasClass = false;
			UClass* NotifyStateClass = nullptr;
			Result = ResolveClassRefField(StateObject, TEXT("Class"), UAnimNotifyState::StaticClass(), BodyArrayFieldPath(TEXT("NotifyStates"), Entry.Index, TEXT("Class")), TEXT("InvalidNotifyStateClass"), bHasClass, NotifyStateClass);
			if (!Result.bSuccess)
			{
				return Result;
			}

			bool bHasNotifyStateFragment = false;
			TSharedPtr<FJsonObject> NotifyStateFragment;
			UClass* NotifyStateFragmentClass = nullptr;
			Result = ResolveNotifyObjectFragmentField(StateObject, TEXT("NotifyState"), UAnimNotifyState::StaticClass(), BodyArrayFieldPath(TEXT("NotifyStates"), Entry.Index, TEXT("NotifyState")), TEXT("InvalidNotifyStateObject"), bHasNotifyStateFragment, NotifyStateFragment, NotifyStateFragmentClass);
			if (!Result.bSuccess)
			{
				return Result;
			}
			if (bHasClass && bHasNotifyStateFragment)
			{
				return BodyFailure(TEXT("Notify state placement cannot declare both Class and NotifyState"), BodyArrayItemPath(TEXT("NotifyStates"), Entry.Index), TEXT("AmbiguousNotifyStateObject"));
			}
			if (!bHasClass && !bHasNotifyStateFragment)
			{
				return BodyFailure(TEXT("Notify state placement requires NotifyState or Class"), BodyArrayFieldPath(TEXT("NotifyStates"), Entry.Index, TEXT("NotifyState")), TEXT("MissingNotifyStateClass"));
			}

			FParsedAnimSequenceNotifyStatePlacement ParsedState;
			ParsedState.Name = FName(*NameString);
			ParsedState.Time = static_cast<float>(Time);
			ParsedState.Duration = static_cast<float>(Duration);
			ParsedState.TrackName = FName(*ReadPlacementTrackName(StateObject));
			ParsedState.NotifyStateClass = bHasNotifyStateFragment ? NotifyStateFragmentClass : NotifyStateClass;
			ParsedState.NotifyStateFragment = NotifyStateFragment;
			ParsedState.SourceIndex = Entry.Index;
			OutNotifyStates.Add(ParsedState);
		}
		return FAssetDocumentCapabilityResult::Success();
	};

	TArray<FAssetDocumentTimelinePlacementEntry> Entries;
	Result = FAssetDocumentTimelinePlacementUtils::ParsePlacementEntries(
		*SectionValue,
		Config,
		TEXT("/Body/NotifyStates"),
		&Range,
		Entries,
		&Hooks);
	if (!Result.bSuccess)
	{
		return MapAnimSequenceNotifyTimelineFailure(Result, TEXT("NotifyStates"), TEXT("DuplicateNotifyStateKey"));
	}

	FAssetDocumentRegionContext RegionContext;
	Result = Hooks.Validate(RegionContext, Entries);
	if (!Result.bSuccess)
	{
		return Result;
	}

	OutNotifyStates.Sort([](const FParsedAnimSequenceNotifyStatePlacement& Left, const FParsedAnimSequenceNotifyStatePlacement& Right)
	{
		if (!FMath::IsNearlyEqual(Left.Time, Right.Time))
		{
			return Left.Time < Right.Time;
		}
		return Left.Name.LexicalLess(Right.Name);
	});
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseAnimSequenceSyncMarkers(
	const TSharedRef<FJsonObject>& BodyObject,
	const UAnimSequence* Sequence,
	bool& bOutHasSyncMarkers,
	TArray<FParsedAnimSequenceSyncMarker>& OutSyncMarkers)
{
	bOutHasSyncMarkers = false;
	OutSyncMarkers.Reset();
	const TSharedPtr<FJsonValue>* SectionValue = BodyObject->Values.Find(TEXT("SyncMarkers"));
	if (!SectionValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	bOutHasSyncMarkers = true;
	FAssetDocumentTimelinePlacementRegionConfig Config;
	Config.AdapterName = TEXT("AnimSequenceSyncMarkersTimelinePlacement");
	Config.RegionId = TEXT("Body.SyncMarkers");
	Config.BodyPath = TEXT("SyncMarkers");
	Config.JsonPointer = TEXT("/Body/SyncMarkers");
	Config.TimeFieldName = TEXT("Time");
	Config.NameFieldName = TEXT("Name");
	Config.bHasName = true;
	Config.bRequireName = true;

	FAssetDocumentTimelinePlacementHooks Hooks;
	Hooks.BuildDuplicateKey = [](const FAssetDocumentTimelinePlacementEntry& Entry)
	{
		const FString NameString = Entry.Name.IsSet() ? Entry.Name.GetValue() : FString();
		const float Time = Entry.Time.IsSet() ? static_cast<float>(Entry.Time.GetValue()) : 0.0f;
		return FString::Printf(TEXT("%s|%.6f"), *NameString, Time);
	};
	Hooks.Validate = [](const FAssetDocumentRegionContext&, TArray<FAssetDocumentTimelinePlacementEntry>& Entries)
	{
		for (const FAssetDocumentTimelinePlacementEntry& Entry : Entries)
		{
			if (!Entry.EntryObject.IsValid())
			{
				return BodyFailure(TEXT("Expected a JSON object"), Entry.JsonPointer, TEXT("InvalidBodySectionType"));
			}
			FAssetDocumentCapabilityResult Result =
				RejectUnknownArrayObjectFields(Entry.EntryObject.ToSharedRef(), TEXT("SyncMarkers"), Entry.Index, { TEXT("Name"), TEXT("Time") });
			if (!Result.bSuccess)
			{
				return Result;
			}
		}
		return FAssetDocumentCapabilityResult::Success();
	};

	FAssetDocumentTimelineRange Range;
	const double MaxTime = GetAnimSequenceTimelineMaxTime(Sequence);
	if (MaxTime > 0.0)
	{
		Range.MaxTime = MaxTime + UE_KINDA_SMALL_NUMBER;
		Range.bHasMaxTime = true;
	}

	FAssetDocumentCapabilityResult Result = ValidateAnimSequenceSyncMarkerTimeFloatSafety(*SectionValue);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TArray<FAssetDocumentTimelinePlacementEntry> Entries;
	Result = FAssetDocumentTimelinePlacementUtils::ParsePlacementEntries(
		*SectionValue,
		Config,
		TEXT("/Body/SyncMarkers"),
		&Range,
		Entries,
		&Hooks);
	if (!Result.bSuccess)
	{
		return MapAnimSequenceSyncMarkerTimelineFailure(Result);
	}

	FAssetDocumentRegionContext RegionContext;
	Result = Hooks.Validate(RegionContext, Entries);
	if (!Result.bSuccess)
	{
		return Result;
	}

	for (const FAssetDocumentTimelinePlacementEntry& Entry : Entries)
	{
		FParsedAnimSequenceSyncMarker ParsedMarker;
		ParsedMarker.Name = FName(*Entry.Name.GetValue());
		ParsedMarker.Time = static_cast<float>(Entry.Time.GetValue());
		OutSyncMarkers.Add(ParsedMarker);
	}

	OutSyncMarkers.Sort([](const FParsedAnimSequenceSyncMarker& Left, const FParsedAnimSequenceSyncMarker& Right)
	{
		if (!FMath::IsNearlyEqual(Left.Time, Right.Time))
		{
			return Left.Time < Right.Time;
		}
		return Left.Name.LexicalLess(Right.Name);
	});
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateExplicitNotifyTrackReferences(
	const TArray<FParsedAnimSequenceNotifyTrack>& Tracks,
	const TArray<FParsedAnimSequenceNotifyPlacement>& Notifies,
	const TArray<FParsedAnimSequenceNotifyStatePlacement>& NotifyStates)
{
	TSet<FName> DeclaredTracks;
	for (const FParsedAnimSequenceNotifyTrack& Track : Tracks)
	{
		DeclaredTracks.Add(Track.Name);
	}

	for (const FParsedAnimSequenceNotifyPlacement& Notify : Notifies)
	{
		if (!DeclaredTracks.Contains(Notify.TrackName))
		{
			return BodyFailure(TEXT("Notify references a track not declared in Body.NotifyTracks"), BodyArrayFieldPath(TEXT("Notifies"), Notify.SourceIndex, TEXT("TrackName")), TEXT("UnknownNotifyTrack"));
		}
	}

	for (const FParsedAnimSequenceNotifyStatePlacement& NotifyState : NotifyStates)
	{
		if (!DeclaredTracks.Contains(NotifyState.TrackName))
		{
			return BodyFailure(TEXT("NotifyState references a track not declared in Body.NotifyTracks"), BodyArrayFieldPath(TEXT("NotifyStates"), NotifyState.SourceIndex, TEXT("TrackName")), TEXT("UnknownNotifyTrack"));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

int32 EnsureNotifyTrackIndex(TArray<FAnimNotifyTrack>& Tracks, const FName TrackName)
{
	const FName EffectiveTrackName = TrackName.IsNone() ? FName(TEXT("Default")) : TrackName;
	for (int32 Index = 0; Index < Tracks.Num(); ++Index)
	{
		if (Tracks[Index].TrackName == EffectiveTrackName)
		{
			return Index;
		}
	}

	Tracks.Add(FAnimNotifyTrack(EffectiveTrackName, FLinearColor::White));
	return Tracks.Num() - 1;
}

FAssetDocumentCapabilityResult CompileAnimSequenceNotifyObject(
	const FAssetDocumentFragmentCompiler& Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimSequence* Sequence,
	UObject* ObjectOuter,
	const TSharedPtr<FJsonObject>& Fragment,
	UClass* ObjectClass,
	UClass* ExpectedBaseClass,
	const FString& Path,
	UObject*& OutObject)
{
	OutObject = nullptr;
	if (!Sequence)
	{
		return BodyFailure(TEXT("AnimSequence notify object compilation requires an asset"), Path, TEXT("UnsupportedAsset"));
	}
	if (!ObjectOuter)
	{
		return BodyFailure(TEXT("AnimSequence notify object compilation requires an outer"), Path, TEXT("UnsupportedAsset"));
	}
	if (!ObjectClass || !ObjectClass->IsChildOf(ExpectedBaseClass))
	{
		return BodyFailure(TEXT("Notify object class did not resolve to the expected base class"), Path, TEXT("InvalidNotifyObject"));
	}

	if (Fragment.IsValid())
	{
		FString Kind;
		FAssetDocumentCapabilityResult Result = ReadRequiredStringField(Fragment.ToSharedRef(), TEXT("Kind"), Path / TEXT("Kind"), Kind);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (Kind == TEXT("EmbeddedObject"))
		{
			FAssetDocumentFragmentContext FragmentContext;
			FragmentContext.OwnerAsset = Sequence;
			FragmentContext.Outer = ObjectOuter;
			FragmentContext.ExpectedBaseClass = ExpectedBaseClass;
			FragmentContext.Definitions = Context.Definitions;
			FragmentContext.JsonPath = Path;

			const FAssetDocumentFragmentResult FragmentResult = Compiler.Compile(Fragment.ToSharedRef(), FragmentContext);
			if (!FragmentResult.bSuccess)
			{
				return FragmentFailure(FragmentResult);
			}
			OutObject = FragmentResult.Object;
			if (!OutObject || !OutObject->IsA(ExpectedBaseClass))
			{
				return BodyFailure(TEXT("Notify object fragment did not produce the expected notify object"), Path, TEXT("InvalidNotifyObject"));
			}
			return FAssetDocumentCapabilityResult::Success();
		}
	}

	OutObject = NewObject<UObject>(ObjectOuter, ObjectClass, NAME_None, RF_Transactional);
	if (!OutObject)
	{
		return BodyFailure(TEXT("Failed to create notify object"), Path, TEXT("InvalidNotifyObject"));
	}
	return FAssetDocumentCapabilityResult::Success();
}

void FinalizeStagedAnimSequenceNotifyObjects(UAnimSequence* Sequence, TArray<FAnimNotifyEvent>& Notifies, const UObject* StagingOuter)
{
	if (!Sequence || !StagingOuter)
	{
		return;
	}

	for (FAnimNotifyEvent& NotifyEvent : Notifies)
	{
		if (NotifyEvent.Notify && NotifyEvent.Notify->GetOuter() == StagingOuter)
		{
			MarkManagedNotifyObject(NotifyEvent.Notify, Sequence, false);
		}
		if (NotifyEvent.NotifyStateClass && NotifyEvent.NotifyStateClass->GetOuter() == StagingOuter)
		{
			MarkManagedNotifyObject(NotifyEvent.NotifyStateClass, Sequence, true);
		}
	}
}

FAssetDocumentCapabilityResult ExtractAnimSequenceEmbeddedNotifyObject(
	const FAssetDocumentFragmentCompiler& Compiler,
	const UAnimSequence* Sequence,
	UObject* NotifyObject,
	const FString& JsonPath,
	TSharedRef<FJsonObject>& OutFragment)
{
	FAssetDocumentFragmentExtractContext ExtractContext;
	ExtractContext.OwnerAsset = const_cast<UAnimSequence*>(Sequence);
	ExtractContext.ValueObject = NotifyObject;
	ExtractContext.Kind = TEXT("EmbeddedObject");
	ExtractContext.JsonPath = JsonPath;

	const FAssetDocumentFragmentResult FragmentResult = Compiler.Extract(ExtractContext, OutFragment);
	if (!FragmentResult.bSuccess)
	{
		return FragmentFailure(FragmentResult);
	}

	if (TSharedPtr<FJsonObject> Properties = FAssetDocumentPropertyAdapter::ExtractWritablePropertiesToJson(NotifyObject, true))
	{
		OutFragment->SetObjectField(TEXT("Properties"), Properties);
	}

	return FAssetDocumentCapabilityResult::Success();
}

TSharedRef<FJsonObject> MakeClassRefObject(const UClass* Class)
{
	TSharedRef<FJsonObject> ClassRef = MakeShared<FJsonObject>();
	ClassRef->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ClassRef->SetStringField(TEXT("Path"), Class ? Class->GetPathName() : FString());
	return ClassRef;
}

TArray<TSharedPtr<FJsonValue>> ExtractAnimSequenceNotifyTracks(const UAnimSequence* Sequence)
{
	TArray<TSharedPtr<FJsonValue>> TrackValues;
	if (!Sequence)
	{
		return TrackValues;
	}

	for (const FAnimNotifyTrack& Track : Sequence->AnimNotifyTracks)
	{
		TSharedRef<FJsonObject> TrackObject = MakeShared<FJsonObject>();
		TrackObject->SetStringField(TEXT("TrackName"), Track.TrackName.ToString());
		TrackValues.Add(MakeShared<FJsonValueObject>(TrackObject));
	}
	return TrackValues;
}

FAssetDocumentCapabilityResult ExtractAnimSequenceNotifies(
	const FAssetDocumentFragmentCompiler& Compiler,
	const UAnimSequence* Sequence,
	bool bStates,
	TArray<TSharedPtr<FJsonValue>>& OutValues)
{
	OutValues.Reset();
	if (!Sequence)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	int32 ExtractedIndex = 0;
	for (const FAnimNotifyEvent& Event : Sequence->Notifies)
	{
		const bool bEventIsState = Event.NotifyStateClass != nullptr;
		if (bEventIsState != bStates)
		{
			continue;
		}
		if (bStates && !IsManagedAnimSequenceNotifyStateEvent(Event, Sequence))
		{
			continue;
		}
		if (!bStates && !IsManagedAnimSequenceNotifyEvent(Event, Sequence))
		{
			continue;
		}

		TSharedRef<FJsonObject> Placement = MakeShared<FJsonObject>();
		Placement->SetStringField(TEXT("Name"), Event.NotifyName.IsNone() ? FString(TEXT("Notify")) : Event.NotifyName.ToString());
		Placement->SetNumberField(TEXT("Time"), Event.GetTime());
		const FString TrackName = Sequence->AnimNotifyTracks.IsValidIndex(Event.TrackIndex)
			? Sequence->AnimNotifyTracks[Event.TrackIndex].TrackName.ToString()
			: FString(TEXT("Default"));
		Placement->SetStringField(TEXT("TrackName"), TrackName);

		if (bStates)
		{
			Placement->SetNumberField(TEXT("Duration"), Event.GetDuration());
			TSharedRef<FJsonObject> NotifyStateFragment = MakeShared<FJsonObject>();
			const FAssetDocumentCapabilityResult Result = ExtractAnimSequenceEmbeddedNotifyObject(
				Compiler,
				Sequence,
				Event.NotifyStateClass,
				BodyArrayFieldPath(TEXT("NotifyStates"), ExtractedIndex, TEXT("NotifyState")),
				NotifyStateFragment);
			if (!Result.bSuccess)
			{
				return Result;
			}
			Placement->SetObjectField(TEXT("NotifyState"), NotifyStateFragment);
		}
		else
		{
			Placement->SetStringField(TEXT("NotifyName"), Event.NotifyName.ToString());
			if (Event.Notify)
			{
				TSharedRef<FJsonObject> NotifyFragment = MakeShared<FJsonObject>();
				const FAssetDocumentCapabilityResult Result = ExtractAnimSequenceEmbeddedNotifyObject(
					Compiler,
					Sequence,
					Event.Notify,
					BodyArrayFieldPath(TEXT("Notifies"), ExtractedIndex, TEXT("Notify")),
					NotifyFragment);
				if (!Result.bSuccess)
				{
					return Result;
				}
				Placement->SetObjectField(TEXT("Notify"), NotifyFragment);
			}
		}

		OutValues.Add(MakeShared<FJsonValueObject>(Placement));
		++ExtractedIndex;
	}

	OutValues.Sort([](const TSharedPtr<FJsonValue>& LeftValue, const TSharedPtr<FJsonValue>& RightValue)
	{
		const TSharedPtr<FJsonObject> Left = LeftValue->AsObject();
		const TSharedPtr<FJsonObject> Right = RightValue->AsObject();
		const double LeftTime = Left.IsValid() ? Left->GetNumberField(TEXT("Time")) : 0.0;
		const double RightTime = Right.IsValid() ? Right->GetNumberField(TEXT("Time")) : 0.0;
		if (!FMath::IsNearlyEqual(LeftTime, RightTime))
		{
			return LeftTime < RightTime;
		}
		return Left->GetStringField(TEXT("Name")) < Right->GetStringField(TEXT("Name"));
	});
	return FAssetDocumentCapabilityResult::Success();
}

FString BodyArrayFieldPath(const TCHAR* ArrayName, int32 Index)
{
	return FString::Printf(TEXT("/Body/%s/%d"), ArrayName, Index);
}

bool IsValidManagedObjectExplicitName(const FString& Name)
{
	if (Name.IsEmpty() || Name.Contains(ManagedNameDelimiter))
	{
		return false;
	}
	for (const TCHAR Char : Name)
	{
		if (!FChar::IsAlnum(Char) && Char != TEXT('_'))
		{
			return false;
		}
	}
	return true;
}

FName MakeManagedObjectName(UObject* Outer, UClass* ObjectClass, const TCHAR* Prefix, const FString& ExplicitName)
{
	if (!ExplicitName.IsEmpty())
	{
		const FString BaseName = FString::Printf(TEXT("%s%s%s%s"), Prefix, ManagedNamedObjectMarker, *ExplicitName, ManagedNameDelimiter);
		return MakeUniqueObjectName(Outer, ObjectClass, *BaseName);
	}
	return MakeUniqueObjectName(Outer, ObjectClass, *FString::Printf(TEXT("%sAuto"), Prefix));
}

bool TryExtractExplicitNameFromManagedObjectName(const FString& ObjectName, const TCHAR* Prefix, FString& OutExplicitName)
{
	OutExplicitName.Reset();
	const FString NamedPrefix = FString::Printf(TEXT("%s%s"), Prefix, ManagedNamedObjectMarker);
	if (!ObjectName.StartsWith(NamedPrefix))
	{
		return false;
	}

	const int32 NameStart = NamedPrefix.Len();
	const int32 DelimiterIndex = ObjectName.Find(ManagedNameDelimiter, ESearchCase::CaseSensitive, ESearchDir::FromStart, NameStart);
	if (DelimiterIndex == INDEX_NONE || DelimiterIndex <= NameStart)
	{
		return false;
	}

	OutExplicitName = ObjectName.Mid(NameStart, DelimiterIndex - NameStart);
	return !OutExplicitName.IsEmpty();
}

FAssetDocumentCapabilityResult GetObjectFragmentFromArrayEntry(
	const TSharedPtr<FJsonValue>& Value,
	const TCHAR* ArrayName,
	int32 Index,
	TSharedPtr<FJsonObject>& OutEntryObject,
	TSharedPtr<FJsonObject>& OutFragmentObject,
	FString& OutExplicitName)
{
	const FString EntryPath = BodyArrayFieldPath(ArrayName, Index);
	const FAssetDocumentCapabilityResult ObjectResult = RequireObjectValue(Value, EntryPath, OutEntryObject);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}

	OutExplicitName.Reset();
	if (const TSharedPtr<FJsonValue>* NameValue = OutEntryObject->Values.Find(TEXT("Name")))
	{
		if (!NameValue->IsValid() || (*NameValue)->Type != EJson::String)
		{
			return BodyFailure(TEXT("Name must be a string"), EntryPath / TEXT("Name"), TEXT("InvalidStringField"));
		}
		OutExplicitName = (*NameValue)->AsString();
		if (!IsValidManagedObjectExplicitName(OutExplicitName))
		{
			return BodyFailure(TEXT("Name must be non-empty and contain only letters, digits, or '_'"), EntryPath / TEXT("Name"), TEXT("InvalidObjectFragmentName"));
		}
	}

	if (const TSharedPtr<FJsonValue>* ObjectValue = OutEntryObject->Values.Find(TEXT("Object")))
	{
		TSharedPtr<FJsonObject> WrappedObject;
		const FAssetDocumentCapabilityResult WrappedObjectResult = RequireObjectValue(*ObjectValue, EntryPath / TEXT("Object"), WrappedObject);
		if (!WrappedObjectResult.bSuccess)
		{
			return WrappedObjectResult;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : OutEntryObject->Values)
		{
			if (Pair.Key != TEXT("Name") && Pair.Key != TEXT("Object"))
			{
				return BodyFailure(
					FString::Printf(TEXT("Body.%s entries do not support field '%s'"), ArrayName, *Pair.Key),
					EntryPath / Pair.Key,
					TEXT("UnsupportedAuthoredField"));
			}
		}
		OutFragmentObject = WrappedObject;
		return FAssetDocumentCapabilityResult::Success();
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : OutEntryObject->Values)
	{
		if (Pair.Key != TEXT("Name") && Pair.Key != TEXT("Kind") && Pair.Key != TEXT("Class") && Pair.Key != TEXT("Properties"))
		{
			return BodyFailure(
				FString::Printf(TEXT("Body.%s entries do not support field '%s'"), ArrayName, *Pair.Key),
				EntryPath / Pair.Key,
				TEXT("UnsupportedAuthoredField"));
		}
	}

	OutFragmentObject = OutEntryObject;
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateManagedObjectFragmentShape(
	const TSharedRef<FJsonObject>& FragmentObject,
	const TCHAR* ArrayName,
	int32 Index,
	UClass* ExpectedBaseClass)
{
	const FString EntryPath = BodyArrayFieldPath(ArrayName, Index);
	FString Kind;
	FAssetDocumentCapabilityResult Result = ReadRequiredStringField(FragmentObject, TEXT("Kind"), EntryPath / TEXT("Kind"), Kind);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (Kind != TEXT("EmbeddedObject"))
	{
		return BodyFailure(TEXT("Object fragment Kind must be EmbeddedObject"), EntryPath / TEXT("Kind"), TEXT("UnsupportedObjectFragmentKind"));
	}

	FString ClassName;
	Result = ReadRequiredStringField(FragmentObject, TEXT("Class"), EntryPath / TEXT("Class"), ClassName);
	if (!Result.bSuccess)
	{
		return Result;
	}

	UClass* ResolvedClass = nullptr;
	FString ResolveError;
	if (!FAssetDocumentClassResolver::ResolveClass(ClassName, ResolvedClass, ResolveError))
	{
		return BodyFailure(ResolveError, EntryPath, TEXT("embeddedobject-class-resolve-failed"));
	}
	if (ExpectedBaseClass && (!ResolvedClass || !ResolvedClass->IsChildOf(ExpectedBaseClass)))
	{
		return BodyFailure(
			FString::Printf(TEXT("Class '%s' is not a child of '%s'."), ResolvedClass ? *ResolvedClass->GetName() : TEXT("<null>"), *ExpectedBaseClass->GetName()),
			EntryPath,
			TEXT("embeddedobject-base-class-mismatch"));
	}

	if (const TSharedPtr<FJsonValue>* PropertiesValue = FragmentObject->Values.Find(TEXT("Properties")))
	{
		TSharedPtr<FJsonObject> PropertiesObject;
		Result = RequireObjectValue(*PropertiesValue, EntryPath / TEXT("Properties"), PropertiesObject);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult CompileManagedObjectFragments(
	const FAssetDocumentFragmentCompiler& Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimSequence* Sequence,
	UObject* FragmentOuter,
	const TArray<TSharedPtr<FJsonValue>>& Values,
	const TCHAR* ArrayName,
	UClass* ExpectedBaseClass,
	const TCHAR* DuplicateCode,
	TArray<FParsedManagedObjectFragment>& OutObjects)
{
	OutObjects.Reset();
	OutObjects.Reserve(Values.Num());

	TSet<FString> SemanticKeys;
	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		TSharedPtr<FJsonObject> EntryObject;
		TSharedPtr<FJsonObject> FragmentObject;
		FString ExplicitName;
		FAssetDocumentCapabilityResult EntryResult = GetObjectFragmentFromArrayEntry(Values[Index], ArrayName, Index, EntryObject, FragmentObject, ExplicitName);
		if (!EntryResult.bSuccess)
		{
			return EntryResult;
		}

		if (!FragmentObject.IsValid())
		{
			return BodyFailure(TEXT("Object fragment entry is invalid"), BodyArrayFieldPath(ArrayName, Index), TEXT("InvalidObjectFragment"));
		}

		FAssetDocumentCapabilityResult ShapeResult = ValidateManagedObjectFragmentShape(FragmentObject.ToSharedRef(), ArrayName, Index, ExpectedBaseClass);
		if (!ShapeResult.bSuccess)
		{
			return ShapeResult;
		}

		FAssetDocumentFragmentContext FragmentContext;
		FragmentContext.OwnerAsset = Sequence;
		FragmentContext.Outer = FragmentOuter;
		FragmentContext.ExpectedBaseClass = ExpectedBaseClass;
		FragmentContext.Definitions = Context.Definitions;
		FragmentContext.JsonPath = BodyArrayFieldPath(ArrayName, Index);

		const FAssetDocumentFragmentResult FragmentResult = Compiler.Compile(FragmentObject.ToSharedRef(), FragmentContext);
		if (!FragmentResult.bSuccess)
		{
			return FragmentFailure(FragmentResult);
		}
		if (!FragmentResult.Object || !FragmentResult.Object->IsA(ExpectedBaseClass))
		{
			return BodyFailure(TEXT("Object fragment did not resolve to the expected base class"), BodyArrayFieldPath(ArrayName, Index), TEXT("InvalidObjectFragment"));
		}

		const FString SemanticKey = FragmentResult.Object->GetClass()->GetPathName() / ExplicitName;
		if (SemanticKeys.Contains(SemanticKey))
		{
			return BodyFailure(TEXT("Duplicate object fragment semantic key"), BodyArrayFieldPath(ArrayName, Index) / TEXT("Class"), DuplicateCode);
		}
		SemanticKeys.Add(SemanticKey);
		FParsedManagedObjectFragment ParsedFragment;
		ParsedFragment.ExplicitName = ExplicitName;
		ParsedFragment.Object = FragmentResult.Object;
		OutObjects.Add(ParsedFragment);
	}

	return FAssetDocumentCapabilityResult::Success();
}

struct FAnimSequenceManagedObjectFragmentArrayRegionSpec
{
	FName AdapterName;
	FName RegionId;
	FString BodyPath;
	FString JsonPointer;
	FString SchemaLabel;
	const TCHAR* ArrayName = nullptr;
	UClass* ExpectedBaseClass = nullptr;
	const TCHAR* DuplicateCode = nullptr;
	const TCHAR* UnsupportedAssetMessage = nullptr;
};

FAnimSequenceManagedObjectFragmentArrayRegionSpec MakeAnimSequenceMetadataFragmentArrayRegionSpec()
{
	FAnimSequenceManagedObjectFragmentArrayRegionSpec Spec;
	Spec.AdapterName = TEXT("AnimSequenceMetadataFragmentArray");
	Spec.RegionId = TEXT("Body.Metadata");
	Spec.BodyPath = TEXT("Body.Metadata");
	Spec.JsonPointer = TEXT("/Body/Metadata");
	Spec.SchemaLabel = TEXT("AnimSequence Metadata");
	Spec.ArrayName = TEXT("Metadata");
	Spec.ExpectedBaseClass = UAnimMetaData::StaticClass();
	Spec.DuplicateCode = TEXT("DuplicateMetadataKey");
	Spec.UnsupportedAssetMessage = TEXT("Object fragment resolution requires an AnimSequence asset");
	return Spec;
}

FAnimSequenceManagedObjectFragmentArrayRegionSpec MakeAnimSequenceAssetUserDataFragmentArrayRegionSpec()
{
	FAnimSequenceManagedObjectFragmentArrayRegionSpec Spec;
	Spec.AdapterName = TEXT("AnimSequenceAssetUserDataFragmentArray");
	Spec.RegionId = TEXT("Body.AssetUserData");
	Spec.BodyPath = TEXT("Body.AssetUserData");
	Spec.JsonPointer = TEXT("/Body/AssetUserData");
	Spec.SchemaLabel = TEXT("AnimSequence AssetUserData");
	Spec.ArrayName = TEXT("AssetUserData");
	Spec.ExpectedBaseClass = UAssetUserData::StaticClass();
	Spec.DuplicateCode = TEXT("DuplicateAssetUserDataKey");
	Spec.UnsupportedAssetMessage = TEXT("Object fragment resolution requires an AnimSequence asset");
	return Spec;
}

FAssetDocumentFragmentArrayRegionConfig MakeAnimSequenceManagedObjectFragmentArrayConfig(
	const FAnimSequenceManagedObjectFragmentArrayRegionSpec& Spec)
{
	FAssetDocumentFragmentArrayRegionConfig Config;
	Config.AdapterName = Spec.AdapterName;
	Config.RegionId = Spec.RegionId;
	Config.BodyPath = Spec.BodyPath;
	Config.JsonPointer = Spec.JsonPointer;
	Config.SchemaLabel = Spec.SchemaLabel;
	return Config;
}

FAssetDocumentRegionContext MakeAnimSequenceManagedObjectFragmentArrayRegionContext(
	const FAssetDocumentCapabilityContext& Context,
	const FAnimSequenceManagedObjectFragmentArrayRegionSpec& Spec)
{
	FAssetDocumentRegionContext RegionContext;
	RegionContext.Asset = Context.Asset;
	RegionContext.AssetClass = Context.AssetClass;
	RegionContext.TargetAssetPath = Context.TargetAssetPath;
	RegionContext.SourceDocumentPath = Context.SourceDocumentPath;
	RegionContext.Definitions = Context.Definitions;
	RegionContext.bIsDryRun = Context.bIsDryRun;
	RegionContext.Result = Context.Result;
	RegionContext.RegionId = Spec.RegionId;
	RegionContext.BodyPath = Spec.BodyPath;
	RegionContext.JsonPointer = Spec.JsonPointer;
	return RegionContext;
}

FAssetDocumentCapabilityResult ValidateManagedObjectFragmentEntries(
	const TArray<FAssetDocumentFragmentArrayEntry>& Entries,
	const TCHAR* ArrayName,
	UClass* ExpectedBaseClass,
	const TCHAR* DuplicateCode)
{
	TSet<FString> SemanticKeys;
	for (const FAssetDocumentFragmentArrayEntry& Entry : Entries)
	{
		TSharedPtr<FJsonValue> EntryValue = MakeShared<FJsonValueObject>(Entry.FragmentObject);
		TSharedPtr<FJsonObject> EntryObject;
		TSharedPtr<FJsonObject> FragmentObject;
		FString ExplicitName;
		FAssetDocumentCapabilityResult EntryResult =
			GetObjectFragmentFromArrayEntry(EntryValue, ArrayName, Entry.Index, EntryObject, FragmentObject, ExplicitName);
		if (!EntryResult.bSuccess)
		{
			return EntryResult;
		}

		FString ClassName;
		if (FragmentObject.IsValid() && FragmentObject->TryGetStringField(TEXT("Class"), ClassName) && !ClassName.IsEmpty())
		{
			const FString SemanticKey = ClassName / ExplicitName;
			if (SemanticKeys.Contains(SemanticKey))
			{
				return BodyFailure(TEXT("Duplicate object fragment semantic key"), BodyArrayFieldPath(ArrayName, Entry.Index) / TEXT("Class"), DuplicateCode);
			}
			SemanticKeys.Add(SemanticKey);
		}
		if (FragmentObject.IsValid())
		{
			FAssetDocumentCapabilityResult ShapeResult =
				ValidateManagedObjectFragmentShape(FragmentObject.ToSharedRef(), ArrayName, Entry.Index, ExpectedBaseClass);
			if (!ShapeResult.bSuccess)
			{
				return ShapeResult;
			}
		}
	}

	return FAssetDocumentCapabilityResult::Success();
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

FAssetDocumentCapabilityResult CompileManagedObjectFragments(
	const FAssetDocumentFragmentCompiler& Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimSequence* Sequence,
	UObject* FragmentOuter,
	const TArray<FAssetDocumentFragmentArrayEntry>& Entries,
	const TCHAR* ArrayName,
	UClass* ExpectedBaseClass,
	const TCHAR* DuplicateCode,
	TArray<FParsedManagedObjectFragment>& OutObjects)
{
	const TArray<TSharedPtr<FJsonValue>> Values = FragmentArrayEntriesToValues(Entries);
	return CompileManagedObjectFragments(
		Compiler,
		Context,
		Sequence,
		FragmentOuter,
		Values,
		ArrayName,
		ExpectedBaseClass,
		DuplicateCode,
		OutObjects);
}

FAssetDocumentCapabilityResult ParseManagedObjectFragmentArrayRegion(
	const FAssetDocumentFragmentCompiler* Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimSequence* Sequence,
	const TSharedRef<FJsonObject>& BodyObject,
	const FAnimSequenceManagedObjectFragmentArrayRegionSpec& Spec,
	bool bResolveFragments,
	bool& bOutHasArray,
	TArray<FParsedManagedObjectFragment>& OutObjects)
{
	bOutHasArray = false;
	OutObjects.Reset();

	const TSharedPtr<FJsonValue>* ArrayValue = BodyObject->Values.Find(Spec.ArrayName);
	if (!ArrayValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FAssetDocumentFragmentArrayHooks Hooks;
	Hooks.Validate = [Compiler, &Context, Sequence, &Spec, bResolveFragments, &OutObjects](
		const FAssetDocumentRegionContext&,
		const TArray<FAssetDocumentFragmentArrayEntry>& Entries)
	{
		FAssetDocumentCapabilityResult Result = ValidateManagedObjectFragmentEntries(
			Entries,
			Spec.ArrayName,
			Spec.ExpectedBaseClass,
			Spec.DuplicateCode);
		if (!Result.bSuccess)
		{
			return Result;
		}

		if (!bResolveFragments)
		{
			return FAssetDocumentCapabilityResult::Success();
		}
		if (!Compiler || !Sequence)
		{
			return BodyFailure(Spec.UnsupportedAssetMessage, Spec.JsonPointer, TEXT("UnsupportedAsset"));
		}

		UObject* FragmentOuter = NewObject<UAnimSequence>(GetTransientPackage(), UAnimSequence::StaticClass(), NAME_None, RF_Transient);
		return CompileManagedObjectFragments(
			*Compiler,
			Context,
			Sequence,
			FragmentOuter,
			Entries,
			Spec.ArrayName,
			Spec.ExpectedBaseClass,
			Spec.DuplicateCode,
			OutObjects);
	};

	const FAssetDocumentFragmentArrayRegionAdapter Adapter(
		MakeAnimSequenceManagedObjectFragmentArrayConfig(Spec),
		MoveTemp(Hooks));
	const FAssetDocumentRegionContext RegionContext = MakeAnimSequenceManagedObjectFragmentArrayRegionContext(Context, Spec);
	const FAssetDocumentCapabilityResult Result = Adapter.ValidateRegion(RegionContext, *ArrayValue);
	if (Result.bSuccess)
	{
		bOutHasArray = true;
	}
	return Result;
}

FAssetDocumentCapabilityResult ParseManagedMetadataFragmentArray(
	const FAssetDocumentFragmentCompiler* Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimSequence* Sequence,
	const TSharedRef<FJsonObject>& BodyObject,
	bool bResolveFragments,
	bool& bOutHasArray,
	TArray<FParsedManagedObjectFragment>& OutObjects)
{
	const FAnimSequenceManagedObjectFragmentArrayRegionSpec Spec = MakeAnimSequenceMetadataFragmentArrayRegionSpec();
	return ParseManagedObjectFragmentArrayRegion(
		Compiler,
		Context,
		Sequence,
		BodyObject,
		Spec,
		bResolveFragments,
		bOutHasArray,
		OutObjects);
}

FAssetDocumentCapabilityResult ParseManagedAssetUserDataFragmentArray(
	const FAssetDocumentFragmentCompiler* Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimSequence* Sequence,
	const TSharedRef<FJsonObject>& BodyObject,
	bool bResolveFragments,
	bool& bOutHasArray,
	TArray<FParsedManagedObjectFragment>& OutObjects)
{
	const FAnimSequenceManagedObjectFragmentArrayRegionSpec Spec = MakeAnimSequenceAssetUserDataFragmentArrayRegionSpec();
	return ParseManagedObjectFragmentArrayRegion(
		Compiler,
		Context,
		Sequence,
		BodyObject,
		Spec,
		bResolveFragments,
		bOutHasArray,
		OutObjects);
}

bool IsManagedObjectByName(const UObject* Object, const TCHAR* Prefix)
{
	return Object && Object->GetName().StartsWith(Prefix);
}

void RollbackManagedObjectMoves(TArray<FManagedObjectMoveRecord>& MoveRecords)
{
	for (int32 Index = MoveRecords.Num() - 1; Index >= 0; --Index)
	{
		FManagedObjectMoveRecord& Record = MoveRecords[Index];
		if (Record.Object && Record.OriginalOuter)
		{
			Record.Object->Rename(*Record.OriginalName.ToString(), Record.OriginalOuter, REN_DontCreateRedirectors | REN_NonTransactional);
		}
	}
	MoveRecords.Reset();
}

FAssetDocumentCapabilityResult MoveManagedObjectsToSequence(
	UAnimSequence* Sequence,
	TArray<FParsedManagedObjectFragment>& Objects,
	UClass* ExpectedBaseClass,
	const TCHAR* Prefix,
	const TCHAR* ArrayName,
	const TCHAR* FailureCode,
	TArray<FManagedObjectMoveRecord>& MoveRecords)
{
	for (int32 Index = 0; Index < Objects.Num(); ++Index)
	{
		UObject* Object = Objects[Index].Object;
		if (!Object || !Object->IsA(ExpectedBaseClass))
		{
			return BodyFailure(TEXT("Object fragment resolved to an invalid object"), BodyArrayFieldPath(ArrayName, Index), TEXT("InvalidObjectFragment"));
		}

		if (Object->GetOuter() == Sequence && Object->GetName().StartsWith(Prefix))
		{
			continue;
		}

		const FName ObjectName = MakeManagedObjectName(Sequence, Object->GetClass(), Prefix, Objects[Index].ExplicitName);
		FManagedObjectMoveRecord MoveRecord;
		MoveRecord.Object = Object;
		MoveRecord.OriginalOuter = Object->GetOuter();
		MoveRecord.OriginalName = Object->GetFName();
		if (!Object->Rename(*ObjectName.ToString(), Sequence, REN_DontCreateRedirectors | REN_NonTransactional))
		{
			RollbackManagedObjectMoves(MoveRecords);
			return BodyFailure(TEXT("Failed to attach object fragment to AnimSequence"), BodyArrayFieldPath(ArrayName, Index), FailureCode);
		}
		MoveRecords.Add(MoveRecord);
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReplaceManagedAssetUserDataByReflection(
	UAnimSequence* Sequence,
	const TArray<FParsedManagedObjectFragment>& NewUserData)
{
	if (!Sequence)
	{
		return BodyFailure(TEXT("AnimSequence body apply requires UAnimSequence asset"), TEXT("/Body/AssetUserData"), TEXT("UnsupportedAsset"));
	}

	FArrayProperty* AssetUserDataProperty = FindFProperty<FArrayProperty>(UAnimationAsset::StaticClass(), TEXT("AssetUserData"));
	if (!AssetUserDataProperty)
	{
		return BodyFailure(TEXT("UAnimationAsset.AssetUserData array property was not found"), TEXT("/Body/AssetUserData"), TEXT("AssetUserDataPropertyMissing"));
	}

	FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(AssetUserDataProperty->Inner);
	if (!ObjectProperty || !ObjectProperty->PropertyClass || !ObjectProperty->PropertyClass->IsChildOf(UAssetUserData::StaticClass()))
	{
		return BodyFailure(TEXT("UAnimationAsset.AssetUserData array property has an unsupported inner type"), TEXT("/Body/AssetUserData"), TEXT("AssetUserDataPropertyInvalid"));
	}

	void* ArrayPtr = AssetUserDataProperty->ContainerPtrToValuePtr<void>(Sequence);
	FScriptArrayHelper ArrayHelper(AssetUserDataProperty, ArrayPtr);
	for (int32 Index = ArrayHelper.Num() - 1; Index >= 0; --Index)
	{
		UAssetUserData* ExistingUserData = Cast<UAssetUserData>(ObjectProperty->GetObjectPropertyValue(ArrayHelper.GetRawPtr(Index)));
		if (IsManagedObjectByName(ExistingUserData, ManagedAssetUserDataObjectPrefix))
		{
			ArrayHelper.RemoveValues(Index);
		}
	}

	for (const FParsedManagedObjectFragment& ParsedFragment : NewUserData)
	{
		UAssetUserData* UserDataObject = Cast<UAssetUserData>(ParsedFragment.Object);
		if (!UserDataObject)
		{
			return BodyFailure(TEXT("AssetUserData object fragment resolved to an invalid object"), TEXT("/Body/AssetUserData"), TEXT("InvalidObjectFragment"));
		}

		const int32 NewIndex = ArrayHelper.AddValue();
		ObjectProperty->SetObjectPropertyValue(ArrayHelper.GetRawPtr(NewIndex), UserDataObject);
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractEmbeddedObjectFragment(
	const FAssetDocumentFragmentCompiler& Compiler,
	UObject* OwnerAsset,
	UObject* Object,
	const FString& JsonPath,
	TSharedRef<FJsonObject>& OutFragment)
{
	FAssetDocumentFragmentExtractContext ExtractContext;
	ExtractContext.OwnerAsset = OwnerAsset;
	ExtractContext.ValueObject = Object;
	ExtractContext.Kind = TEXT("EmbeddedObject");
	ExtractContext.JsonPath = JsonPath;

	const FAssetDocumentFragmentResult FragmentResult = Compiler.Extract(ExtractContext, OutFragment);
	if (!FragmentResult.bSuccess)
	{
		return FragmentFailure(FragmentResult);
	}
	if (TSharedPtr<FJsonObject> Properties = FAssetDocumentPropertyAdapter::ExtractWritablePropertiesToJson(Object, true))
	{
		OutFragment->SetObjectField(TEXT("Properties"), Properties);
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractManagedMetadata(
	const FAssetDocumentFragmentCompiler& Compiler,
	const UAnimSequence* Sequence,
	TArray<TSharedPtr<FJsonValue>>& OutValues)
{
	OutValues.Reset();
	if (!Sequence)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	int32 ExtractedIndex = 0;
	for (UAnimMetaData* MetadataObject : Sequence->GetMetaData())
	{
		if (!IsManagedObjectByName(MetadataObject, ManagedMetadataObjectPrefix))
		{
			continue;
		}

		TSharedRef<FJsonObject> Fragment = MakeShared<FJsonObject>();
		const FAssetDocumentCapabilityResult Result = ExtractEmbeddedObjectFragment(
			Compiler,
			const_cast<UAnimSequence*>(Sequence),
			MetadataObject,
			BodyArrayFieldPath(TEXT("Metadata"), ExtractedIndex),
			Fragment);
		if (!Result.bSuccess)
		{
			return Result;
		}
		FString ExplicitName;
		if (TryExtractExplicitNameFromManagedObjectName(MetadataObject->GetName(), ManagedMetadataObjectPrefix, ExplicitName))
		{
			Fragment->SetStringField(TEXT("Name"), ExplicitName);
		}
		OutValues.Add(MakeShared<FJsonValueObject>(Fragment));
		++ExtractedIndex;
	}
	OutValues.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
	{
		return JsonValueToComparableString(Left) < JsonValueToComparableString(Right);
	});
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractManagedObjectFragmentArrayRegion(
	const FAssetDocumentFragmentCompiler& Compiler,
	const FAssetDocumentCapabilityContext& Context,
	const UAnimSequence* Sequence,
	const FAnimSequenceManagedObjectFragmentArrayRegionSpec& Spec,
	TFunctionRef<FAssetDocumentCapabilityResult(
		const FAssetDocumentFragmentCompiler&,
		const UAnimSequence*,
		TArray<TSharedPtr<FJsonValue>>&)> ExtractObjects,
	TSharedPtr<FJsonValue>& OutValue)
{
	FAssetDocumentFragmentArrayHooks Hooks;
	Hooks.Extract = [&Compiler, Sequence, &Spec, ExtractObjects](
		const FAssetDocumentRegionContext&,
		TArray<TSharedRef<FJsonObject>>& OutEntries)
	{
		TArray<TSharedPtr<FJsonValue>> ExtractedObjects;
		FAssetDocumentCapabilityResult Result = ExtractObjects(Compiler, Sequence, ExtractedObjects);
		if (!Result.bSuccess)
		{
			return Result;
		}

		OutEntries.Reset();
		OutEntries.Reserve(ExtractedObjects.Num());
		for (int32 Index = 0; Index < ExtractedObjects.Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& EntryValue = ExtractedObjects[Index];
			if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object || !EntryValue->AsObject().IsValid())
			{
				return BodyFailure(
					TEXT("Expected a JSON object for fragment array entry"),
					BodyArrayFieldPath(Spec.ArrayName, Index),
					TEXT("InvalidFragmentArrayEntryType"));
			}
			OutEntries.Add(EntryValue->AsObject().ToSharedRef());
		}

		return FAssetDocumentCapabilityResult::Success(FString::Printf(TEXT("Extracted %s fragments"), *Spec.SchemaLabel));
	};

	const FAssetDocumentFragmentArrayRegionAdapter Adapter(
		MakeAnimSequenceManagedObjectFragmentArrayConfig(Spec),
		MoveTemp(Hooks));
	const FAssetDocumentRegionContext RegionContext = MakeAnimSequenceManagedObjectFragmentArrayRegionContext(Context, Spec);
	return Adapter.ExtractRegion(RegionContext, OutValue);
}

FAssetDocumentCapabilityResult ExtractManagedMetadataRegion(
	const FAssetDocumentFragmentCompiler& Compiler,
	const FAssetDocumentCapabilityContext& Context,
	const UAnimSequence* Sequence,
	TSharedPtr<FJsonValue>& OutValue)
{
	const FAnimSequenceManagedObjectFragmentArrayRegionSpec Spec = MakeAnimSequenceMetadataFragmentArrayRegionSpec();
	return ExtractManagedObjectFragmentArrayRegion(
		Compiler,
		Context,
		Sequence,
		Spec,
		ExtractManagedMetadata,
		OutValue);
}

FAssetDocumentCapabilityResult ExtractManagedAssetUserData(
	const FAssetDocumentFragmentCompiler& Compiler,
	const UAnimSequence* Sequence,
	TArray<TSharedPtr<FJsonValue>>& OutValues)
{
	OutValues.Reset();
	if (!Sequence)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const TArray<UAssetUserData*>* UserDataArray = Sequence->GetAssetUserDataArray();
	if (!UserDataArray)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	int32 ExtractedIndex = 0;
	for (UAssetUserData* UserDataObject : *UserDataArray)
	{
		if (!IsManagedObjectByName(UserDataObject, ManagedAssetUserDataObjectPrefix))
		{
			continue;
		}

		TSharedRef<FJsonObject> Fragment = MakeShared<FJsonObject>();
		const FAssetDocumentCapabilityResult Result = ExtractEmbeddedObjectFragment(
			Compiler,
			const_cast<UAnimSequence*>(Sequence),
			UserDataObject,
			BodyArrayFieldPath(TEXT("AssetUserData"), ExtractedIndex),
			Fragment);
		if (!Result.bSuccess)
		{
			return Result;
		}
		FString ExplicitName;
		if (TryExtractExplicitNameFromManagedObjectName(UserDataObject->GetName(), ManagedAssetUserDataObjectPrefix, ExplicitName))
		{
			Fragment->SetStringField(TEXT("Name"), ExplicitName);
		}
		OutValues.Add(MakeShared<FJsonValueObject>(Fragment));
		++ExtractedIndex;
	}
	OutValues.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
	{
		return JsonValueToComparableString(Left) < JsonValueToComparableString(Right);
	});
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractManagedAssetUserDataRegion(
	const FAssetDocumentFragmentCompiler& Compiler,
	const FAssetDocumentCapabilityContext& Context,
	const UAnimSequence* Sequence,
	TSharedPtr<FJsonValue>& OutValue)
{
	const FAnimSequenceManagedObjectFragmentArrayRegionSpec Spec = MakeAnimSequenceAssetUserDataFragmentArrayRegionSpec();
	return ExtractManagedObjectFragmentArrayRegion(
		Compiler,
		Context,
		Sequence,
		Spec,
		ExtractManagedAssetUserData,
		OutValue);
}

TArray<TSharedPtr<FJsonValue>> ExtractAnimSequenceSyncMarkers(const UAnimSequence* Sequence)
{
	TArray<TSharedPtr<FJsonValue>> MarkerValues;
	if (!Sequence)
	{
		return MarkerValues;
	}

	TArray<FAnimSyncMarker> SortedMarkers = Sequence->AuthoredSyncMarkers;
	SortedMarkers.Sort([](const FAnimSyncMarker& Left, const FAnimSyncMarker& Right)
	{
		if (!FMath::IsNearlyEqual(Left.Time, Right.Time))
		{
			return Left.Time < Right.Time;
		}
		return Left.MarkerName.LexicalLess(Right.MarkerName);
	});

	for (const FAnimSyncMarker& Marker : SortedMarkers)
	{
		TSharedRef<FJsonObject> MarkerObject = MakeShared<FJsonObject>();
		MarkerObject->SetStringField(TEXT("Name"), Marker.MarkerName.ToString());
		MarkerObject->SetNumberField(TEXT("Time"), Marker.Time);
		MarkerValues.Add(MakeShared<FJsonValueObject>(MarkerObject));
	}
	return MarkerValues;
}

struct FParsedAnimSequenceBody
{
	bool bHasSkeleton = false;
	USkeleton* Skeleton = nullptr;
	bool bHasRetargetSource = false;
	FName RetargetSource = NAME_None;
	bool bHasRetargetSourceAsset = false;
	USkeletalMesh* RetargetSourceAsset = nullptr;
	bool bHasPreviewMesh = false;
	USkeletalMesh* PreviewMesh = nullptr;
	bool bHasRateScale = false;
	float RateScale = 1.0f;
	bool bHasAdditiveAnimType = false;
	EAdditiveAnimationType AdditiveAnimType = AAT_None;
	bool bHasRefPoseType = false;
	EAdditiveBasePoseType RefPoseType = ABPT_None;
	bool bHasRefFrameIndex = false;
	int32 RefFrameIndex = 0;
	bool bHasRefPoseSeq = false;
	UAnimSequence* RefPoseSeq = nullptr;
	bool bHasEnableRootMotion = false;
	bool bEnableRootMotion = false;
	bool bHasRootMotionRootLock = false;
	ERootMotionRootLock::Type RootMotionRootLock = ERootMotionRootLock::RefPose;
	bool bHasForceRootLock = false;
	bool bForceRootLock = false;
	bool bHasUseNormalizedRootMotionScale = false;
	bool bUseNormalizedRootMotionScale = false;
	bool bHasCompressionErrorThresholdScale = false;
	float CompressionErrorThresholdScale = 1.0f;
	bool bHasBoneCompressionSettings = false;
	UAnimBoneCompressionSettings* BoneCompressionSettings = nullptr;
	bool bHasCurveCompressionSettings = false;
	UAnimCurveCompressionSettings* CurveCompressionSettings = nullptr;
	bool bHasDoNotOverrideCompression = false;
	bool bDoNotOverrideCompression = false;
	bool bHasCurves = false;
	TArray<FParsedAnimSequenceCurve> Curves;
	bool bHasNotifies = false;
	TArray<FParsedAnimSequenceNotifyPlacement> Notifies;
	bool bHasNotifyStates = false;
	TArray<FParsedAnimSequenceNotifyStatePlacement> NotifyStates;
	bool bHasNotifyTracks = false;
	TArray<FParsedAnimSequenceNotifyTrack> NotifyTracks;
	bool bHasSyncMarkers = false;
	TArray<FParsedAnimSequenceSyncMarker> SyncMarkers;
	bool bHasMetadata = false;
	TArray<FParsedManagedObjectFragment> Metadata;
	bool bHasAssetUserData = false;
	TArray<FParsedManagedObjectFragment> AssetUserData;
};

FAssetDocumentCapabilityResult ParseAnimSequencePreviewRegion(
	const FAssetDocumentFragmentCompiler* Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimSequence* Sequence,
	const TSharedPtr<FJsonValue>& PreviewValue,
	bool bResolveFragments,
	FParsedAnimSequenceBody& OutParsed)
{
	FAssetDocumentObjectRegionAdapterHooks Hooks;
	Hooks.ValidateObject = [Compiler, &Context, Sequence, bResolveFragments, &OutParsed](
		const FAssetDocumentRegionContext&,
		const TSharedRef<FJsonObject>& PreviewObject)
	{
		UObject* PreviewMeshObject = nullptr;
		FAssetDocumentCapabilityResult Result = ParseAssetRef(
			Compiler,
			Context,
			Sequence,
			PreviewObject,
			TEXT("PreviewMesh"),
			USkeletalMesh::StaticClass(),
			TEXT("/Body/Preview/PreviewMesh"),
			bResolveFragments,
			true,
			OutParsed.bHasPreviewMesh,
			PreviewMeshObject);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutParsed.PreviewMesh = Cast<USkeletalMesh>(PreviewMeshObject);
		return FAssetDocumentCapabilityResult::Success(TEXT("Parsed AnimSequence Preview region"));
	};

	FAssetDocumentObjectRegionAdapter Adapter(FAnimSequenceAssetDocumentProfile::PreviewObjectRegionAdapterName(), MoveTemp(Hooks));
	FAssetDocumentRegionPolicy Policy;
	if (!FindAnimSequencePilotPolicy(TEXT("Preview"), Policy))
	{
		return MissingAnimSequencePilotPolicyFailure(TEXT("Preview"));
	}
	const FAssetDocumentRegionContext RegionContext =
		MakeAnimSequencePilotRegionContext(Context, TEXT("Preview"), Policy);
	return FAssetDocumentRegionRuntime::Validate(RegionContext, PreviewValue, Adapter);
}

FAssetDocumentCapabilityResult ParseAnimSequencePlaybackRegion(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedPtr<FJsonValue>& PlaybackValue,
	FParsedAnimSequenceBody& OutParsed)
{
	FAssetDocumentObjectRegionAdapterHooks Hooks;
	Hooks.ValidateObject = [&OutParsed](const FAssetDocumentRegionContext&, const TSharedRef<FJsonObject>& PlaybackObject)
	{
		double RateScale = 0.0;
		FAssetDocumentCapabilityResult Result = ReadOptionalNumber(
			PlaybackObject,
			TEXT("RateScale"),
			TEXT("/Body/Playback/RateScale"),
			OutParsed.bHasRateScale,
			RateScale);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (OutParsed.bHasRateScale)
		{
			if (RateScale <= 0.0)
			{
				return BodyFailure(TEXT("RateScale must be positive"), TEXT("/Body/Playback/RateScale"), TEXT("InvalidRateScale"));
			}
			OutParsed.RateScale = static_cast<float>(RateScale);
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("Parsed AnimSequence Playback region"));
	};

	FAssetDocumentObjectRegionAdapter Adapter(FAnimSequenceAssetDocumentProfile::PlaybackObjectRegionAdapterName(), MoveTemp(Hooks));
	FAssetDocumentRegionPolicy Policy;
	if (!FindAnimSequencePilotPolicy(TEXT("Playback"), Policy))
	{
		return MissingAnimSequencePilotPolicyFailure(TEXT("Playback"));
	}
	const FAssetDocumentRegionContext RegionContext =
		MakeAnimSequencePilotRegionContext(Context, TEXT("Playback"), Policy);
	return FAssetDocumentRegionRuntime::Validate(RegionContext, PlaybackValue, Adapter);
}

TSharedRef<FJsonObject> MakeAnimSequencePilotBodySubset(const TSharedRef<FJsonObject>& BodyObject)
{
	TSharedRef<FJsonObject> PilotBody = MakeShared<FJsonObject>();
	for (const TCHAR* BodyKey : {TEXT("Preview"), TEXT("Playback"), TEXT("NotifyTracks")})
	{
		if (const TSharedPtr<FJsonValue>* Value = BodyObject->Values.Find(BodyKey))
		{
			PilotBody->SetField(BodyKey, *Value);
		}
	}
	return PilotBody;
}

FAssetDocumentCapabilityResult ValidateAnimSequencePilotRegionsThroughDispatcher(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& BodyObject)
{
	FAssetDocumentObjectRegionAdapter ObjectAdapter(FAnimSequenceAssetDocumentProfile::PilotObjectRegionAdapterName());
	FAssetDocumentNamedArrayRegionAdapter NamedArrayAdapter(
		FAnimSequenceAssetDocumentProfile::MakeNotifyTracksNamedArrayConfig());

	const TArray<FAssetDocumentRegionBinding> Bindings = FAnimSequenceAssetDocumentProfile::MakePilotRegionBindings();
	const TArray<FAssetDocumentRegionPolicy> Policies = FAnimSequenceAssetDocumentProfile::MakePilotRegionPolicies();
	TMap<FName, IAssetDocumentRegionAdapter*> Adapters;
	Adapters.Add(ObjectAdapter.GetName(), &ObjectAdapter);
	Adapters.Add(NamedArrayAdapter.GetName(), &NamedArrayAdapter);

	const FAssetDocumentBodyRegionDispatcher Dispatcher(Bindings, Policies, Adapters);
	return Dispatcher.ValidateBody(
		Context,
		MakeShared<FJsonValueObject>(MakeAnimSequencePilotBodySubset(BodyObject)));
}

FAssetDocumentCapabilityResult ApplyAnimSequencePreviewRegion(
	FAssetDocumentCapabilityContext& Context,
	UAnimSequence* Sequence,
	const TSharedPtr<FJsonValue>& DesiredValue,
	const FParsedAnimSequenceBody& ParsedBody,
	bool& bOutChanged)
{
	FAssetDocumentObjectRegionAdapterHooks Hooks;
	Hooks.ApplyObject = [Sequence, &ParsedBody](FAssetDocumentRegionContext&, const TSharedRef<FJsonObject>&, bool& bHookChanged)
	{
		bHookChanged = false;
		if (!ParsedBody.bHasPreviewMesh)
		{
			return FAssetDocumentCapabilityResult::Success(TEXT("Preview region has no authored PreviewMesh"));
		}

		bHookChanged = Sequence->GetPreviewMesh() != ParsedBody.PreviewMesh;
		Sequence->SetPreviewMesh(ParsedBody.PreviewMesh, false);
		return FAssetDocumentCapabilityResult::Success(TEXT("Applied AnimSequence Preview region"));
	};

	FAssetDocumentObjectRegionAdapter Adapter(FAnimSequenceAssetDocumentProfile::PreviewObjectRegionAdapterName(), MoveTemp(Hooks));
	FAssetDocumentRegionPolicy Policy;
	if (!FindAnimSequencePilotPolicy(TEXT("Preview"), Policy))
	{
		return MissingAnimSequencePilotPolicyFailure(TEXT("Preview"));
	}
	FAssetDocumentRegionContext RegionContext =
		MakeAnimSequencePilotRegionContext(Context, TEXT("Preview"), Policy);
	return FAssetDocumentRegionRuntime::Apply(RegionContext, DesiredValue, Adapter, bOutChanged);
}

FAssetDocumentCapabilityResult ApplyAnimSequencePlaybackRegion(
	FAssetDocumentCapabilityContext& Context,
	UAnimSequence* Sequence,
	const TSharedPtr<FJsonValue>& DesiredValue,
	const FParsedAnimSequenceBody& ParsedBody,
	bool& bOutChanged)
{
	FAssetDocumentObjectRegionAdapterHooks Hooks;
	Hooks.ApplyObject = [Sequence, &ParsedBody](FAssetDocumentRegionContext&, const TSharedRef<FJsonObject>&, bool& bHookChanged)
	{
		bHookChanged = false;
		if (!ParsedBody.bHasRateScale)
		{
			return FAssetDocumentCapabilityResult::Success(TEXT("Playback region has no authored RateScale"));
		}

		bHookChanged = !FMath::IsNearlyEqual(Sequence->RateScale, ParsedBody.RateScale);
		Sequence->RateScale = ParsedBody.RateScale;
		return FAssetDocumentCapabilityResult::Success(TEXT("Applied AnimSequence Playback region"));
	};

	FAssetDocumentObjectRegionAdapter Adapter(FAnimSequenceAssetDocumentProfile::PlaybackObjectRegionAdapterName(), MoveTemp(Hooks));
	FAssetDocumentRegionPolicy Policy;
	if (!FindAnimSequencePilotPolicy(TEXT("Playback"), Policy))
	{
		return MissingAnimSequencePilotPolicyFailure(TEXT("Playback"));
	}
	FAssetDocumentRegionContext RegionContext =
		MakeAnimSequencePilotRegionContext(Context, TEXT("Playback"), Policy);
	return FAssetDocumentRegionRuntime::Apply(RegionContext, DesiredValue, Adapter, bOutChanged);
}

FAssetDocumentCapabilityResult ApplyAnimSequenceNotifyTracksRegion(
	FAssetDocumentCapabilityContext& Context,
	const UAnimSequence* Sequence,
	const TSharedPtr<FJsonValue>& DesiredValue,
	const FParsedAnimSequenceBody& ParsedBody,
	TArray<FAnimNotifyTrack>& StagedTracks,
	bool& bOutChanged)
{
	FAssetDocumentNamedArrayRegionAdapterHooks Hooks;
	Hooks.ApplyElements = [Sequence, &ParsedBody, &StagedTracks](FAssetDocumentRegionContext&, const TArray<TSharedRef<FJsonObject>>&, bool& bHookChanged)
	{
		StagedTracks.Reset();
		StagedTracks.Reserve(ParsedBody.NotifyTracks.Num());
		for (const FParsedAnimSequenceNotifyTrack& Track : ParsedBody.NotifyTracks)
		{
			StagedTracks.Add(FAnimNotifyTrack(Track.Name, FLinearColor::White));
		}
		bHookChanged = !Sequence || Sequence->AnimNotifyTracks.Num() != StagedTracks.Num();
		if (!bHookChanged && Sequence)
		{
			for (int32 Index = 0; Index < StagedTracks.Num(); ++Index)
			{
				if (Sequence->AnimNotifyTracks[Index].TrackName != StagedTracks[Index].TrackName)
				{
					bHookChanged = true;
					break;
				}
			}
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("Applied AnimSequence NotifyTracks region"));
	};

	FAssetDocumentNamedArrayRegionAdapter Adapter(
		FAnimSequenceAssetDocumentProfile::MakeNotifyTracksNamedArrayConfig(),
		MoveTemp(Hooks));

	FAssetDocumentRegionPolicy Policy;
	if (!FindAnimSequencePilotPolicy(TEXT("NotifyTracks"), Policy))
	{
		return MissingAnimSequencePilotPolicyFailure(TEXT("NotifyTracks"));
	}
	FAssetDocumentRegionContext RegionContext =
		MakeAnimSequencePilotRegionContext(Context, TEXT("NotifyTracks"), Policy);
	return FAssetDocumentRegionRuntime::Apply(RegionContext, DesiredValue, Adapter, bOutChanged);
}

bool TryGetAnimSequenceFrameCount(const UAnimSequence* Sequence, int32& OutFrameCount)
{
	OutFrameCount = 0;
	if (!Sequence)
	{
		return false;
	}

	const int32 SampledKeys = Sequence->GetNumberOfSampledKeys();
	if (SampledKeys > 0)
	{
		OutFrameCount = SampledKeys;
		return true;
	}

	if (IAnimationDataModel* DataModel = Sequence->GetDataModel())
	{
		const int32 DataModelKeys = DataModel->GetNumberOfKeys();
		if (DataModelKeys > 0)
		{
			OutFrameCount = DataModelKeys;
			return true;
		}

		const int32 DataModelFrames = DataModel->GetNumberOfFrames();
		if (DataModelFrames >= 0)
		{
			OutFrameCount = DataModelFrames + 1;
			return true;
		}
	}

	return false;
}

FAssetDocumentCapabilityResult ValidateParsedBodyAgainstSequence(
	const UAnimSequence* Sequence,
	const FParsedAnimSequenceBody& ParsedBody)
{
	if (!Sequence)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	if (ParsedBody.bHasSkeleton && ParsedBody.Skeleton && Sequence->GetSkeleton() && Sequence->GetSkeleton() != ParsedBody.Skeleton)
	{
		return BodyFailure(TEXT("Body.References.Skeleton must match the current AnimSequence skeleton in Task 2"), TEXT("/Body/References/Skeleton"), TEXT("SkeletonMismatch"));
	}

	if (ParsedBody.bHasPreviewMesh && ParsedBody.PreviewMesh && Sequence->GetSkeleton() && ParsedBody.PreviewMesh->GetSkeleton() != Sequence->GetSkeleton())
	{
		return BodyFailure(TEXT("Body.Preview.PreviewMesh skeleton must match the AnimSequence skeleton"), TEXT("/Body/Preview/PreviewMesh"), TEXT("PreviewMeshSkeletonMismatch"));
	}

	if (ParsedBody.bHasRefFrameIndex)
	{
		const UAnimSequence* FrameSource = Sequence;
		if (ParsedBody.bHasRefPoseSeq && ParsedBody.RefPoseSeq)
		{
			FrameSource = ParsedBody.RefPoseSeq;
		}
		else if (!ParsedBody.bHasRefPoseSeq && Sequence->RefPoseSeq)
		{
			FrameSource = Sequence->RefPoseSeq;
		}

		int32 FrameCount = 0;
		if (!TryGetAnimSequenceFrameCount(FrameSource, FrameCount) || FrameCount <= 0)
		{
			return BodyFailure(TEXT("Unable to validate Additive.RefFrameIndex because the frame count is unavailable"), TEXT("/Body/Additive/RefFrameIndex"), TEXT("UnsupportedRefFrameIndexValidation"));
		}
		if (ParsedBody.RefFrameIndex >= FrameCount)
		{
			return BodyFailure(
				FString::Printf(TEXT("Additive.RefFrameIndex must be less than available frame count %d"), FrameCount),
				TEXT("/Body/Additive/RefFrameIndex"),
				TEXT("InvalidRefFrameIndex"));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateBodyObjectShape(const TSharedRef<FJsonObject>& BodyObject)
{
	const FAssetDocumentCapabilityResult UnsupportedResult = RejectUnsupportedAuthoredFields(BodyObject);
	if (!UnsupportedResult.bSuccess)
	{
		return UnsupportedResult;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BodyObject->Values)
	{
		const FString Path = FString::Printf(TEXT("/Body/%s"), *Pair.Key);
		if (Pair.Key == TEXT("_Skipped"))
		{
			return BodyFailure(TEXT("Body._Skipped is extract-only diagnostic metadata and cannot be authored"), Path, TEXT("ExtractOnlyBodyKey"));
		}
		if (!IsKnownBodyKey(Pair.Key))
		{
			return BodyFailure(
				FString::Printf(TEXT("Unknown AnimSequence Body key '%s'"), *Pair.Key),
				Path,
				TEXT("UnknownBodyKey"));
		}
	}

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

	const TArray<FAssetDocumentCapabilityResult> Results = {
		RequireObject(TEXT("References")),
		RequireObject(TEXT("Preview")),
		RequireObject(TEXT("Playback")),
		RequireObject(TEXT("Additive")),
		RequireObject(TEXT("RootMotion")),
		RequireObject(TEXT("Compression")),
		RequireArray(TEXT("Curves")),
		RequireArray(TEXT("Notifies")),
		RequireArray(TEXT("NotifyStates")),
		RequireArray(TEXT("NotifyTracks")),
		RequireArray(TEXT("SyncMarkers")),
		RequireArray(TEXT("Metadata")),
		RequireArray(TEXT("AssetUserData")),
	};

	for (const FAssetDocumentCapabilityResult& Result : Results)
	{
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	const TArray<FAssetDocumentCapabilityResult> FieldResults = {
		RejectUnknownObjectFields(BodyObject, TEXT("References"), { TEXT("Skeleton"), TEXT("RetargetSource"), TEXT("RetargetSourceAsset") }),
		ValidateAnimSequenceObjectFieldSchema(BodyObject, TEXT("Preview"), MakeAnimSequencePreviewSchema()),
		ValidateAnimSequenceObjectFieldSchema(BodyObject, TEXT("Playback"), MakeAnimSequencePlaybackSchema()),
		RejectUnknownObjectFields(BodyObject, TEXT("Additive"), { TEXT("AdditiveAnimType"), TEXT("RefPoseType"), TEXT("RefFrameIndex"), TEXT("RefPoseSeq") }),
		RejectUnknownObjectFields(BodyObject, TEXT("RootMotion"), { TEXT("bEnableRootMotion"), TEXT("RootMotionRootLock"), TEXT("bForceRootLock"), TEXT("bUseNormalizedRootMotionScale") }),
		RejectUnknownObjectFields(BodyObject, TEXT("Compression"), { TEXT("CompressionErrorThresholdScale"), TEXT("BoneCompressionSettings"), TEXT("CurveCompressionSettings"), TEXT("bDoNotOverrideCompression") }),
	};
	for (const FAssetDocumentCapabilityResult& Result : FieldResults)
	{
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseAnimSequenceBody(
	const FAssetDocumentFragmentCompiler* Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimSequence* Sequence,
	const TSharedRef<FJsonObject>& BodyObject,
	bool bResolveFragments,
	FParsedAnimSequenceBody& OutParsed)
{
	const FAssetDocumentCapabilityResult ShapeResult = ValidateBodyObjectShape(BodyObject);
	if (!ShapeResult.bSuccess)
	{
		return ShapeResult;
	}

	const FAssetDocumentCapabilityResult PilotDispatchResult =
		ValidateAnimSequencePilotRegionsThroughDispatcher(Context, BodyObject);
	if (!PilotDispatchResult.bSuccess)
	{
		return PilotDispatchResult;
	}

	if (const TSharedPtr<FJsonValue>* ReferencesValue = BodyObject->Values.Find(TEXT("References")))
	{
		TSharedPtr<FJsonObject> ReferencesObject;
		FAssetDocumentCapabilityResult Result = RequireObjectValue(*ReferencesValue, TEXT("/Body/References"), ReferencesObject);
		if (!Result.bSuccess)
		{
			return Result;
		}

		UObject* SkeletonObject = nullptr;
		Result = ParseAssetRef(Compiler, Context, Sequence, ReferencesObject.ToSharedRef(), TEXT("Skeleton"), USkeleton::StaticClass(), TEXT("/Body/References/Skeleton"), bResolveFragments, false, OutParsed.bHasSkeleton, SkeletonObject);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutParsed.Skeleton = Cast<USkeleton>(SkeletonObject);

		FString RetargetSourceString;
		Result = ReadOptionalString(ReferencesObject.ToSharedRef(), TEXT("RetargetSource"), TEXT("/Body/References/RetargetSource"), OutParsed.bHasRetargetSource, RetargetSourceString);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (OutParsed.bHasRetargetSource)
		{
			OutParsed.RetargetSource = FName(*RetargetSourceString);
		}

		UObject* RetargetSourceAssetObject = nullptr;
		Result = ParseAssetRef(Compiler, Context, Sequence, ReferencesObject.ToSharedRef(), TEXT("RetargetSourceAsset"), USkeletalMesh::StaticClass(), TEXT("/Body/References/RetargetSourceAsset"), bResolveFragments, true, OutParsed.bHasRetargetSourceAsset, RetargetSourceAssetObject);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutParsed.RetargetSourceAsset = Cast<USkeletalMesh>(RetargetSourceAssetObject);
	}

	if (const TSharedPtr<FJsonValue>* PreviewValue = BodyObject->Values.Find(TEXT("Preview")))
	{
		FAssetDocumentCapabilityResult Result =
			ParseAnimSequencePreviewRegion(Compiler, Context, Sequence, *PreviewValue, bResolveFragments, OutParsed);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	if (const TSharedPtr<FJsonValue>* PlaybackValue = BodyObject->Values.Find(TEXT("Playback")))
	{
		FAssetDocumentCapabilityResult Result =
			ParseAnimSequencePlaybackRegion(Context, *PlaybackValue, OutParsed);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	if (const TSharedPtr<FJsonValue>* AdditiveValue = BodyObject->Values.Find(TEXT("Additive")))
	{
		TSharedPtr<FJsonObject> AdditiveObject;
		FAssetDocumentCapabilityResult Result = RequireObjectValue(*AdditiveValue, TEXT("/Body/Additive"), AdditiveObject);
		if (!Result.bSuccess)
		{
			return Result;
		}

		FString EnumString;
		Result = ReadOptionalString(AdditiveObject.ToSharedRef(), TEXT("AdditiveAnimType"), TEXT("/Body/Additive/AdditiveAnimType"), OutParsed.bHasAdditiveAnimType, EnumString);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (OutParsed.bHasAdditiveAnimType && !TryParseAdditiveAnimType(EnumString, OutParsed.AdditiveAnimType))
		{
			return BodyFailure(TEXT("AdditiveAnimType is not supported"), TEXT("/Body/Additive/AdditiveAnimType"), TEXT("InvalidEnumValue"));
		}

		Result = ReadOptionalString(AdditiveObject.ToSharedRef(), TEXT("RefPoseType"), TEXT("/Body/Additive/RefPoseType"), OutParsed.bHasRefPoseType, EnumString);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (OutParsed.bHasRefPoseType && !TryParseRefPoseType(EnumString, OutParsed.RefPoseType))
		{
			return BodyFailure(TEXT("RefPoseType is not supported"), TEXT("/Body/Additive/RefPoseType"), TEXT("InvalidEnumValue"));
		}

		Result = ReadOptionalNonNegativeInteger(AdditiveObject.ToSharedRef(), TEXT("RefFrameIndex"), TEXT("/Body/Additive/RefFrameIndex"), OutParsed.bHasRefFrameIndex, OutParsed.RefFrameIndex);
		if (!Result.bSuccess)
		{
			return Result;
		}

		UObject* RefPoseSeqObject = nullptr;
		Result = ParseAssetRef(Compiler, Context, Sequence, AdditiveObject.ToSharedRef(), TEXT("RefPoseSeq"), UAnimSequence::StaticClass(), TEXT("/Body/Additive/RefPoseSeq"), bResolveFragments, true, OutParsed.bHasRefPoseSeq, RefPoseSeqObject);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutParsed.RefPoseSeq = Cast<UAnimSequence>(RefPoseSeqObject);
	}

	if (const TSharedPtr<FJsonValue>* RootMotionValue = BodyObject->Values.Find(TEXT("RootMotion")))
	{
		TSharedPtr<FJsonObject> RootMotionObject;
		FAssetDocumentCapabilityResult Result = RequireObjectValue(*RootMotionValue, TEXT("/Body/RootMotion"), RootMotionObject);
		if (!Result.bSuccess)
		{
			return Result;
		}

		Result = ReadOptionalBool(RootMotionObject.ToSharedRef(), TEXT("bEnableRootMotion"), TEXT("/Body/RootMotion/bEnableRootMotion"), OutParsed.bHasEnableRootMotion, OutParsed.bEnableRootMotion);
		if (!Result.bSuccess)
		{
			return Result;
		}

		FString RootLockString;
		Result = ReadOptionalString(RootMotionObject.ToSharedRef(), TEXT("RootMotionRootLock"), TEXT("/Body/RootMotion/RootMotionRootLock"), OutParsed.bHasRootMotionRootLock, RootLockString);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (OutParsed.bHasRootMotionRootLock && !TryParseRootMotionRootLock(RootLockString, OutParsed.RootMotionRootLock))
		{
			return BodyFailure(TEXT("RootMotionRootLock is not supported"), TEXT("/Body/RootMotion/RootMotionRootLock"), TEXT("InvalidEnumValue"));
		}

		Result = ReadOptionalBool(RootMotionObject.ToSharedRef(), TEXT("bForceRootLock"), TEXT("/Body/RootMotion/bForceRootLock"), OutParsed.bHasForceRootLock, OutParsed.bForceRootLock);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = ReadOptionalBool(RootMotionObject.ToSharedRef(), TEXT("bUseNormalizedRootMotionScale"), TEXT("/Body/RootMotion/bUseNormalizedRootMotionScale"), OutParsed.bHasUseNormalizedRootMotionScale, OutParsed.bUseNormalizedRootMotionScale);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	if (const TSharedPtr<FJsonValue>* CompressionValue = BodyObject->Values.Find(TEXT("Compression")))
	{
		TSharedPtr<FJsonObject> CompressionObject;
		FAssetDocumentCapabilityResult Result = RequireObjectValue(*CompressionValue, TEXT("/Body/Compression"), CompressionObject);
		if (!Result.bSuccess)
		{
			return Result;
		}

		double CompressionErrorThresholdScale = 0.0;
		Result = ReadOptionalNonNegativeNumber(CompressionObject.ToSharedRef(), TEXT("CompressionErrorThresholdScale"), TEXT("/Body/Compression/CompressionErrorThresholdScale"), OutParsed.bHasCompressionErrorThresholdScale, CompressionErrorThresholdScale);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (OutParsed.bHasCompressionErrorThresholdScale)
		{
			OutParsed.CompressionErrorThresholdScale = static_cast<float>(CompressionErrorThresholdScale);
		}

		UObject* BoneCompressionObject = nullptr;
		Result = ParseAssetRef(Compiler, Context, Sequence, CompressionObject.ToSharedRef(), TEXT("BoneCompressionSettings"), UAnimBoneCompressionSettings::StaticClass(), TEXT("/Body/Compression/BoneCompressionSettings"), bResolveFragments, true, OutParsed.bHasBoneCompressionSettings, BoneCompressionObject);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutParsed.BoneCompressionSettings = Cast<UAnimBoneCompressionSettings>(BoneCompressionObject);

		UObject* CurveCompressionObject = nullptr;
		Result = ParseAssetRef(Compiler, Context, Sequence, CompressionObject.ToSharedRef(), TEXT("CurveCompressionSettings"), UAnimCurveCompressionSettings::StaticClass(), TEXT("/Body/Compression/CurveCompressionSettings"), bResolveFragments, true, OutParsed.bHasCurveCompressionSettings, CurveCompressionObject);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutParsed.CurveCompressionSettings = Cast<UAnimCurveCompressionSettings>(CurveCompressionObject);

		Result = ReadOptionalBool(CompressionObject.ToSharedRef(), TEXT("bDoNotOverrideCompression"), TEXT("/Body/Compression/bDoNotOverrideCompression"), OutParsed.bHasDoNotOverrideCompression, OutParsed.bDoNotOverrideCompression);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	{
		FAssetDocumentCapabilityResult Result = ParseAnimSequenceCurves(BodyObject, OutParsed.bHasCurves, OutParsed.Curves);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	{
		FAssetDocumentCapabilityResult Result = ParseAnimSequenceNotifyTracks(BodyObject, OutParsed.bHasNotifyTracks, OutParsed.NotifyTracks);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = ParseAnimSequenceNotifies(BodyObject, Sequence, OutParsed.bHasNotifies, OutParsed.Notifies);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = ParseAnimSequenceNotifyStates(BodyObject, Sequence, OutParsed.bHasNotifyStates, OutParsed.NotifyStates);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = ParseAnimSequenceSyncMarkers(BodyObject, Sequence, OutParsed.bHasSyncMarkers, OutParsed.SyncMarkers);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (OutParsed.bHasNotifyTracks)
		{
			Result = ValidateExplicitNotifyTrackReferences(OutParsed.NotifyTracks, OutParsed.Notifies, OutParsed.NotifyStates);
			if (!Result.bSuccess)
			{
				return Result;
			}
		}
	}

	{
		FAssetDocumentCapabilityResult Result = ParseManagedMetadataFragmentArray(
			Compiler,
			Context,
			Sequence,
			BodyObject,
			bResolveFragments,
			OutParsed.bHasMetadata,
			OutParsed.Metadata);
		if (!Result.bSuccess)
		{
			return Result;
		}

		Result = ParseManagedAssetUserDataFragmentArray(
			Compiler,
			Context,
			Sequence,
			BodyObject,
			bResolveFragments,
			OutParsed.bHasAssetUserData,
			OutParsed.AssetUserData);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	const FAssetDocumentCapabilityResult SequenceValidationResult = ValidateParsedBodyAgainstSequence(Sequence, OutParsed);
	if (!SequenceValidationResult.bSuccess)
	{
		return SequenceValidationResult;
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("AnimSequence Body parsed"));
}
}

const TArray<FName>& FAnimSequenceAssetDocumentCapability::GetCanonicalBodyKeys()
{
	static const TArray<FName> BodyKeys = {
		TEXT("References"),
		TEXT("Preview"),
		TEXT("Playback"),
		TEXT("Additive"),
		TEXT("RootMotion"),
		TEXT("Compression"),
		TEXT("Curves"),
		TEXT("Notifies"),
		TEXT("NotifyStates"),
		TEXT("NotifyTracks"),
		TEXT("SyncMarkers"),
		TEXT("Metadata"),
		TEXT("AssetUserData"),
	};
	return BodyKeys;
}

FName FAnimSequenceAssetDocumentCapability::GetName() const
{
	return TEXT("AnimSequenceBody");
}

TArray<FName> FAnimSequenceAssetDocumentCapability::GetInternalAdapterNames() const
{
	return {
		GetName(),
		FAnimSequenceAssetDocumentProfile::PilotObjectRegionAdapterName(),
		FAnimSequenceAssetDocumentProfile::PreviewObjectRegionAdapterName(),
		FAnimSequenceAssetDocumentProfile::PlaybackObjectRegionAdapterName(),
		FAnimSequenceAssetDocumentProfile::NotifyTracksNamedArrayRegionAdapterName(),
		TEXT("AnimSequenceNotifiesTimelinePlacement"),
		TEXT("AnimSequenceNotifyStatesTimelinePlacement"),
	};
}

int32 FAnimSequenceAssetDocumentCapability::GetApplyOrder() const
{
	return 100;
}

bool FAnimSequenceAssetDocumentCapability::SupportsAsset(const UObject* Asset) const
{
	return Asset && Asset->IsA<UAnimSequence>();
}

bool FAnimSequenceAssetDocumentCapability::SupportsClass(const UClass* AssetClass) const
{
	return AssetClass == UAnimSequence::StaticClass();
}

TSharedRef<FJsonObject> FAnimSequenceAssetDocumentCapability::GetSchemaHint() const
{
	TSharedRef<FJsonObject> Hint = MakeShared<FJsonObject>();
	Hint->SetStringField(TEXT("References"), TEXT("object: Skeleton, RetargetSource, RetargetSourceAsset"));
	Hint->SetStringField(TEXT("Preview"), TEXT("object: PreviewMesh"));
	Hint->SetStringField(TEXT("Playback"), TEXT("object: RateScale; derived length/sample fields are extract-only"));
	Hint->SetStringField(TEXT("Additive"), TEXT("object: AdditiveAnimType, RefPoseType, RefFrameIndex, RefPoseSeq"));
	Hint->SetStringField(TEXT("RootMotion"), TEXT("object: bEnableRootMotion, RootMotionRootLock, bForceRootLock, bUseNormalizedRootMotionScale"));
	Hint->SetStringField(TEXT("Compression"), TEXT("object: CompressionErrorThresholdScale, BoneCompressionSettings, CurveCompressionSettings, bDoNotOverrideCompression"));
	Hint->SetStringField(TEXT("Curves"), TEXT("array: sequence-owned float curve sparse patches; deletion is deferred"));
	Hint->SetStringField(TEXT("Notifies"), TEXT("array: point notify placements with Notify embedded object fragments"));
	Hint->SetStringField(TEXT("NotifyStates"), TEXT("array: ranged notify-state placements with NotifyState embedded object fragments"));
	Hint->SetStringField(TEXT("NotifyTracks"), TEXT("array: notify TrackName values/order"));
	Hint->SetStringField(TEXT("SyncMarkers"), TEXT("array: authored sync marker timeline"));
	Hint->SetStringField(TEXT("Metadata"), TEXT("array: UAnimMetaData embedded object fragments"));
	Hint->SetStringField(TEXT("AssetUserData"), TEXT("array: UAssetUserData embedded object fragments"));
	return Hint;
}

FAssetDocumentCapabilityResult FAnimSequenceAssetDocumentCapability::Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
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

	FParsedAnimSequenceBody ParsedBody;
	const FAssetDocumentCapabilityResult Result = ParseAnimSequenceBody(&Compiler, Context, Cast<UAnimSequence>(Context.Asset), BodyObject.ToSharedRef(), false, ParsedBody);
	if (!Result.bSuccess)
	{
		return Result;
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("AnimSequence Body is valid"));
}

FAssetDocumentCapabilityResult FAnimSequenceAssetDocumentCapability::Preflight(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
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

	UAnimSequence* Sequence = Cast<UAnimSequence>(Context.Asset);
	FParsedAnimSequenceBody ParsedBody;
	return ParseAnimSequenceBody(&Compiler, Context, Sequence, BodyObject.ToSharedRef(), Sequence != nullptr, ParsedBody);
}

FAssetDocumentCapabilityResult FAnimSequenceAssetDocumentCapability::Apply(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson)
{
	UAnimSequence* Sequence = Cast<UAnimSequence>(Context.Asset);
	if (!Sequence)
	{
		return BodyFailure(TEXT("AnimSequence body apply requires UAnimSequence asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
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

	FParsedAnimSequenceBody ParsedBody;
	const FAssetDocumentCapabilityResult Result = ParseAnimSequenceBody(&Compiler, Context, Sequence, BodyObject.ToSharedRef(), true, ParsedBody);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (ParsedBody.bHasSkeleton && ParsedBody.Skeleton && Sequence->GetSkeleton() && Sequence->GetSkeleton() != ParsedBody.Skeleton)
	{
		return BodyFailure(TEXT("Body.References.Skeleton must match the current AnimSequence skeleton in Task 2"), TEXT("/Body/References/Skeleton"), TEXT("SkeletonMismatch"));
	}

	const bool bHasTimelineRegions = ParsedBody.bHasNotifyTracks || ParsedBody.bHasNotifies || ParsedBody.bHasNotifyStates || ParsedBody.bHasSyncMarkers;
	TArray<FAnimNotifyTrack> StagedTracks;
	TArray<FAnimNotifyEvent> StagedNotifies;
	TArray<FAnimSyncMarker> StagedMarkers;
	UObject* NotifyObjectStagingOuter = nullptr;
	if (bHasTimelineRegions)
	{
		if (ParsedBody.bHasNotifies || ParsedBody.bHasNotifyStates)
		{
			NotifyObjectStagingOuter = NewObject<UAnimSequence>(GetTransientPackage(), UAnimSequence::StaticClass(), NAME_None, RF_Transient);
		}

		TArray<FName> OriginalTrackNames;
		OriginalTrackNames.Reserve(Sequence->AnimNotifyTracks.Num());
		for (const FAnimNotifyTrack& Track : Sequence->AnimNotifyTracks)
		{
			OriginalTrackNames.Add(Track.TrackName);
		}

		if (ParsedBody.bHasNotifyTracks)
		{
			const TSharedPtr<FJsonValue>* NotifyTracksValue = BodyObject->Values.Find(TEXT("NotifyTracks"));
			if (!NotifyTracksValue)
			{
				return BodyFailure(TEXT("Body.NotifyTracks was parsed but source value is missing"), TEXT("/Body/NotifyTracks"), TEXT("MissingBodySection"));
			}
			bool bNotifyTracksChanged = false;
			const FAssetDocumentCapabilityResult NotifyTracksApplyResult =
				ApplyAnimSequenceNotifyTracksRegion(Context, Sequence, *NotifyTracksValue, ParsedBody, StagedTracks, bNotifyTracksChanged);
			if (!NotifyTracksApplyResult.bSuccess)
			{
				return NotifyTracksApplyResult;
			}
		}
		else
		{
			StagedTracks = Sequence->AnimNotifyTracks;
		}
		if (StagedTracks.IsEmpty())
		{
			StagedTracks.Add(FAnimNotifyTrack(TEXT("Default"), FLinearColor::White));
		}

		StagedNotifies.Reserve(Sequence->Notifies.Num() + ParsedBody.Notifies.Num() + ParsedBody.NotifyStates.Num());
		for (const FAnimNotifyEvent& ExistingNotify : Sequence->Notifies)
		{
			if (ParsedBody.bHasNotifies && IsManagedAnimSequenceNotifyEvent(ExistingNotify, Sequence))
			{
				continue;
			}
			if (ParsedBody.bHasNotifyStates && IsManagedAnimSequenceNotifyStateEvent(ExistingNotify, Sequence))
			{
				continue;
			}
			FAnimNotifyEvent PreservedNotify = ExistingNotify;
			const FName OriginalTrackName = OriginalTrackNames.IsValidIndex(ExistingNotify.TrackIndex)
				? OriginalTrackNames[ExistingNotify.TrackIndex]
				: FName(TEXT("Default"));
			if (!OriginalTrackName.IsNone())
			{
				PreservedNotify.TrackIndex = EnsureNotifyTrackIndex(StagedTracks, OriginalTrackName);
			}
			else
			{
				PreservedNotify.TrackIndex = 0;
			}
			StagedNotifies.Add(PreservedNotify);
		}

		if (ParsedBody.bHasNotifies)
		{
			for (const FParsedAnimSequenceNotifyPlacement& ParsedNotify : ParsedBody.Notifies)
			{
				FAnimNotifyEvent NotifyEvent;
				NotifyEvent.NotifyName = ParsedNotify.NotifyName;
				NotifyEvent.TrackIndex = EnsureNotifyTrackIndex(StagedTracks, ParsedNotify.TrackName);
				NotifyEvent.SetTime(ParsedNotify.Time);
				NotifyEvent.RefreshTriggerOffset(Sequence->CalculateOffsetForNotify(ParsedNotify.Time));
				UObject* NotifyObject = nullptr;
				const FAssetDocumentCapabilityResult CompileNotifyResult = CompileAnimSequenceNotifyObject(
					Compiler,
					Context,
					Sequence,
					NotifyObjectStagingOuter,
					ParsedNotify.NotifyFragment,
					ParsedNotify.NotifyClass,
					UAnimNotify::StaticClass(),
					BodyArrayFieldPath(TEXT("Notifies"), ParsedNotify.SourceIndex, ParsedNotify.NotifyFragment.IsValid() ? TEXT("Notify") : TEXT("Class")),
					NotifyObject);
				if (!CompileNotifyResult.bSuccess)
				{
					return CompileNotifyResult;
				}
				NotifyEvent.Notify = Cast<UAnimNotify>(NotifyObject);
#if WITH_EDITORONLY_DATA
				NotifyEvent.Guid = FGuid::NewGuid();
#endif
				StagedNotifies.Add(NotifyEvent);
			}
		}

		if (ParsedBody.bHasNotifyStates)
		{
			for (const FParsedAnimSequenceNotifyStatePlacement& ParsedState : ParsedBody.NotifyStates)
			{
				FAnimNotifyEvent NotifyEvent;
				NotifyEvent.NotifyName = ParsedState.Name;
				NotifyEvent.TrackIndex = EnsureNotifyTrackIndex(StagedTracks, ParsedState.TrackName);
				NotifyEvent.SetTime(ParsedState.Time);
				NotifyEvent.RefreshTriggerOffset(Sequence->CalculateOffsetForNotify(ParsedState.Time));
				NotifyEvent.SetDuration(ParsedState.Duration);
				NotifyEvent.RefreshEndTriggerOffset(Sequence->CalculateOffsetForNotify(ParsedState.Time + ParsedState.Duration));
				UObject* NotifyStateObject = nullptr;
				const FAssetDocumentCapabilityResult CompileNotifyStateResult = CompileAnimSequenceNotifyObject(
					Compiler,
					Context,
					Sequence,
					NotifyObjectStagingOuter,
					ParsedState.NotifyStateFragment,
					ParsedState.NotifyStateClass,
					UAnimNotifyState::StaticClass(),
					BodyArrayFieldPath(TEXT("NotifyStates"), ParsedState.SourceIndex, ParsedState.NotifyStateFragment.IsValid() ? TEXT("NotifyState") : TEXT("Class")),
					NotifyStateObject);
				if (!CompileNotifyStateResult.bSuccess)
				{
					return CompileNotifyStateResult;
				}
				NotifyEvent.NotifyStateClass = Cast<UAnimNotifyState>(NotifyStateObject);
#if WITH_EDITORONLY_DATA
				NotifyEvent.Guid = FGuid::NewGuid();
#endif
				StagedNotifies.Add(NotifyEvent);
			}
		}

		StagedNotifies.Sort();

		if (ParsedBody.bHasSyncMarkers)
		{
			StagedMarkers.Reserve(ParsedBody.SyncMarkers.Num());
			for (const FParsedAnimSequenceSyncMarker& ParsedMarker : ParsedBody.SyncMarkers)
			{
				FAnimSyncMarker Marker;
				Marker.MarkerName = ParsedMarker.Name;
				Marker.Time = ParsedMarker.Time;
#if WITH_EDITORONLY_DATA
				Marker.TrackIndex = 0;
				Marker.Guid = FGuid::NewGuid();
#endif
				StagedMarkers.Add(Marker);
			}
			StagedMarkers.Sort([](const FAnimSyncMarker& Left, const FAnimSyncMarker& Right)
			{
				if (!FMath::IsNearlyEqual(Left.Time, Right.Time))
				{
					return Left.Time < Right.Time;
				}
				return Left.MarkerName.LexicalLess(Right.MarkerName);
			});
		}
	}

	if (ParsedBody.bHasCurves)
	{
		const FAssetDocumentCapabilityResult CurvesResult = ApplyAnimSequenceCurves(Sequence, ParsedBody.Curves);
		if (!CurvesResult.bSuccess)
		{
			return CurvesResult;
		}
	}

	if (bHasTimelineRegions)
	{
		if (ParsedBody.bHasNotifies || ParsedBody.bHasNotifyStates || ParsedBody.bHasNotifyTracks)
		{
			FinalizeStagedAnimSequenceNotifyObjects(Sequence, StagedNotifies, NotifyObjectStagingOuter);
			Sequence->AnimNotifyTracks = MoveTemp(StagedTracks);
			Sequence->Notifies = MoveTemp(StagedNotifies);
			Sequence->SortNotifies();
			Sequence->InitializeNotifyTrack();
			if (Sequence->GetPlayLength() > 0.0f)
			{
				Sequence->ClampNotifiesAtEndOfSequence();
			}
			Sequence->RefreshCacheData();
		}

		if (ParsedBody.bHasSyncMarkers)
		{
			Sequence->AuthoredSyncMarkers = MoveTemp(StagedMarkers);
			Sequence->RefreshSyncMarkerDataFromAuthored();
			Sequence->RefreshCacheData();
		}
	}

	if (ParsedBody.bHasMetadata || ParsedBody.bHasAssetUserData)
	{
		TArray<FManagedObjectMoveRecord> MoveRecords;
		if (ParsedBody.bHasMetadata)
		{
			const FAssetDocumentCapabilityResult MoveResult = MoveManagedObjectsToSequence(
				Sequence,
				ParsedBody.Metadata,
				UAnimMetaData::StaticClass(),
				ManagedMetadataObjectPrefix,
				TEXT("Metadata"),
				TEXT("MetadataOuterMoveFailed"),
				MoveRecords);
			if (!MoveResult.bSuccess)
			{
				return MoveResult;
			}
		}

		if (ParsedBody.bHasAssetUserData)
		{
			const FAssetDocumentCapabilityResult MoveResult = MoveManagedObjectsToSequence(
				Sequence,
				ParsedBody.AssetUserData,
				UAssetUserData::StaticClass(),
				ManagedAssetUserDataObjectPrefix,
				TEXT("AssetUserData"),
				TEXT("AssetUserDataOuterMoveFailed"),
				MoveRecords);
			if (!MoveResult.bSuccess)
			{
				return MoveResult;
			}
		}

		if (ParsedBody.bHasAssetUserData)
		{
			const FAssetDocumentCapabilityResult ReplaceResult = ReplaceManagedAssetUserDataByReflection(Sequence, ParsedBody.AssetUserData);
			if (!ReplaceResult.bSuccess)
			{
				RollbackManagedObjectMoves(MoveRecords);
				return ReplaceResult;
			}
		}

		if (ParsedBody.bHasMetadata)
		{
			TArray<UAnimMetaData*> ExistingManagedMetadata;
			for (UAnimMetaData* MetadataObject : Sequence->GetMetaData())
			{
				if (IsManagedObjectByName(MetadataObject, ManagedMetadataObjectPrefix))
				{
					ExistingManagedMetadata.Add(MetadataObject);
				}
			}
			if (!ExistingManagedMetadata.IsEmpty())
			{
				Sequence->RemoveMetaData(MakeArrayView(ExistingManagedMetadata));
			}
			for (const FParsedManagedObjectFragment& MetadataFragment : ParsedBody.Metadata)
			{
				Sequence->AddMetaData(CastChecked<UAnimMetaData>(MetadataFragment.Object));
			}
		}
	}

	if (ParsedBody.bHasRetargetSource)
	{
		Sequence->RetargetSource = ParsedBody.RetargetSource;
	}
	if (ParsedBody.bHasRetargetSourceAsset)
	{
		if (ParsedBody.RetargetSourceAsset)
		{
			Sequence->SetRetargetSourceAsset(ParsedBody.RetargetSourceAsset);
		}
		else
		{
			Sequence->ClearRetargetSourceAsset();
		}
	}
	if (const TSharedPtr<FJsonValue>* PreviewValue = BodyObject->Values.Find(TEXT("Preview")))
	{
		bool bPreviewChanged = false;
		const FAssetDocumentCapabilityResult PreviewApplyResult =
			ApplyAnimSequencePreviewRegion(Context, Sequence, *PreviewValue, ParsedBody, bPreviewChanged);
		if (!PreviewApplyResult.bSuccess)
		{
			return PreviewApplyResult;
		}
	}
	if (const TSharedPtr<FJsonValue>* PlaybackValue = BodyObject->Values.Find(TEXT("Playback")))
	{
		bool bPlaybackChanged = false;
		const FAssetDocumentCapabilityResult PlaybackApplyResult =
			ApplyAnimSequencePlaybackRegion(Context, Sequence, *PlaybackValue, ParsedBody, bPlaybackChanged);
		if (!PlaybackApplyResult.bSuccess)
		{
			return PlaybackApplyResult;
		}
	}
	if (ParsedBody.bHasAdditiveAnimType)
	{
		Sequence->AdditiveAnimType = ParsedBody.AdditiveAnimType;
	}
	if (ParsedBody.bHasRefPoseType)
	{
		Sequence->RefPoseType = ParsedBody.RefPoseType;
	}
	if (ParsedBody.bHasRefFrameIndex)
	{
		Sequence->RefFrameIndex = ParsedBody.RefFrameIndex;
	}
	if (ParsedBody.bHasRefPoseSeq)
	{
		Sequence->RefPoseSeq = ParsedBody.RefPoseSeq;
	}
	if (ParsedBody.bHasEnableRootMotion)
	{
		Sequence->bEnableRootMotion = ParsedBody.bEnableRootMotion;
	}
	if (ParsedBody.bHasRootMotionRootLock)
	{
		Sequence->RootMotionRootLock = ParsedBody.RootMotionRootLock;
	}
	if (ParsedBody.bHasForceRootLock)
	{
		Sequence->bForceRootLock = ParsedBody.bForceRootLock;
	}
	if (ParsedBody.bHasUseNormalizedRootMotionScale)
	{
		Sequence->bUseNormalizedRootMotionScale = ParsedBody.bUseNormalizedRootMotionScale;
	}
	if (ParsedBody.bHasCompressionErrorThresholdScale)
	{
		Sequence->CompressionErrorThresholdScale = ParsedBody.CompressionErrorThresholdScale;
	}
	if (ParsedBody.bHasBoneCompressionSettings)
	{
		Sequence->BoneCompressionSettings = ParsedBody.BoneCompressionSettings;
	}
	if (ParsedBody.bHasCurveCompressionSettings)
	{
		Sequence->CurveCompressionSettings = ParsedBody.CurveCompressionSettings;
	}
	if (ParsedBody.bHasDoNotOverrideCompression)
	{
		Sequence->bDoNotOverrideCompression = ParsedBody.bDoNotOverrideCompression;
	}
	const bool bNeedsCacheRefresh =
		ParsedBody.bHasRetargetSourceAsset ||
		ParsedBody.bHasPreviewMesh ||
		ParsedBody.bHasAdditiveAnimType ||
		ParsedBody.bHasRefPoseType ||
		ParsedBody.bHasRefFrameIndex ||
		ParsedBody.bHasRefPoseSeq ||
		ParsedBody.bHasEnableRootMotion ||
		ParsedBody.bHasRootMotionRootLock ||
		ParsedBody.bHasForceRootLock ||
		ParsedBody.bHasUseNormalizedRootMotionScale ||
		ParsedBody.bHasCompressionErrorThresholdScale ||
		ParsedBody.bHasBoneCompressionSettings ||
		ParsedBody.bHasCurveCompressionSettings ||
		ParsedBody.bHasDoNotOverrideCompression;
	if (!Context.bIsDryRun && bNeedsCacheRefresh)
	{
		// ValidateCompressionSettings is protected in UE 5.7; RefreshCacheData is the public post-apply refresh hook.
		Sequence->RefreshCacheData();
	}
	Sequence->MarkPackageDirty();
	return FAssetDocumentCapabilityResult::Success(TEXT("AnimSequence Body applied"));
}

FAssetDocumentCapabilityResult ExtractAnimSequencePreviewRegion(
	const FAssetDocumentFragmentCompiler& Compiler,
	const FAssetDocumentCapabilityContext& Context,
	const UAnimSequence* Sequence,
	TSharedPtr<FJsonValue>& OutValue)
{
	FAssetDocumentObjectRegionAdapterHooks Hooks;
	Hooks.ExtractObject = [&Compiler, Sequence](const FAssetDocumentRegionContext&, TSharedRef<FJsonObject>& OutObject)
	{
		if (USkeletalMesh* PreviewMesh = Sequence->GetPreviewMesh())
		{
			TSharedRef<FJsonObject> PreviewMeshRef = MakeShared<FJsonObject>();
			FAssetDocumentCapabilityResult Result = ExtractAssetRef(
				Compiler,
				const_cast<UAnimSequence*>(Sequence),
				PreviewMesh,
				TEXT("/Body/Preview/PreviewMesh"),
				PreviewMeshRef);
			if (!Result.bSuccess)
			{
				return Result;
			}
			OutObject->SetObjectField(TEXT("PreviewMesh"), PreviewMeshRef);
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("Extracted AnimSequence Preview"));
	};

	FAssetDocumentObjectRegionAdapter Adapter(FAnimSequenceAssetDocumentProfile::PreviewObjectRegionAdapterName(), MoveTemp(Hooks));
	FAssetDocumentRegionPolicy Policy;
	if (!FindAnimSequencePilotPolicy(TEXT("Preview"), Policy))
	{
		return MissingAnimSequencePilotPolicyFailure(TEXT("Preview"));
	}
	const FAssetDocumentRegionContext RegionContext =
		MakeAnimSequencePilotRegionContext(Context, TEXT("Preview"), Policy);
	return FAssetDocumentRegionRuntime::Extract(RegionContext, Adapter, OutValue);
}

FAssetDocumentCapabilityResult ExtractAnimSequencePlaybackRegion(
	const FAssetDocumentCapabilityContext& Context,
	const UAnimSequence* Sequence,
	TSharedPtr<FJsonValue>& OutValue)
{
	FAssetDocumentObjectRegionAdapterHooks Hooks;
	Hooks.ExtractObject = [Sequence](const FAssetDocumentRegionContext&, TSharedRef<FJsonObject>& OutObject)
	{
		OutObject->SetNumberField(TEXT("RateScale"), Sequence->RateScale);
		return FAssetDocumentCapabilityResult::Success(TEXT("Extracted AnimSequence Playback"));
	};

	FAssetDocumentObjectRegionAdapter Adapter(FAnimSequenceAssetDocumentProfile::PlaybackObjectRegionAdapterName(), MoveTemp(Hooks));
	FAssetDocumentRegionPolicy Policy;
	if (!FindAnimSequencePilotPolicy(TEXT("Playback"), Policy))
	{
		return MissingAnimSequencePilotPolicyFailure(TEXT("Playback"));
	}
	const FAssetDocumentRegionContext RegionContext =
		MakeAnimSequencePilotRegionContext(Context, TEXT("Playback"), Policy);
	return FAssetDocumentRegionRuntime::Extract(RegionContext, Adapter, OutValue);
}

FAssetDocumentCapabilityResult ExtractAnimSequenceNotifyTracksRegion(
	const FAssetDocumentCapabilityContext& Context,
	const UAnimSequence* Sequence,
	TSharedPtr<FJsonValue>& OutValue)
{
	FAssetDocumentNamedArrayRegionAdapterHooks Hooks;
	Hooks.ExtractElements = [Sequence](const FAssetDocumentRegionContext&, TArray<TSharedRef<FJsonObject>>& OutElements)
	{
		if (!Sequence)
		{
			return FAssetDocumentCapabilityResult::Success(TEXT("No AnimSequence for NotifyTracks extract"));
		}

		for (const FAnimNotifyTrack& Track : Sequence->AnimNotifyTracks)
		{
			TSharedRef<FJsonObject> TrackObject = MakeShared<FJsonObject>();
			TrackObject->SetStringField(TEXT("TrackName"), Track.TrackName.ToString());
			OutElements.Add(TrackObject);
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("Extracted AnimSequence NotifyTracks"));
	};

	FAssetDocumentNamedArrayRegionAdapter Adapter(
		FAnimSequenceAssetDocumentProfile::MakeNotifyTracksNamedArrayConfig(),
		MoveTemp(Hooks));
	FAssetDocumentRegionPolicy Policy;
	if (!FindAnimSequencePilotPolicy(TEXT("NotifyTracks"), Policy))
	{
		return MissingAnimSequencePilotPolicyFailure(TEXT("NotifyTracks"));
	}
	const FAssetDocumentRegionContext RegionContext =
		MakeAnimSequencePilotRegionContext(Context, TEXT("NotifyTracks"), Policy);
	return FAssetDocumentRegionRuntime::Extract(RegionContext, Adapter, OutValue);
}

FAssetDocumentCapabilityResult DiffAnimSequenceObjectPilotRegion(
	const FAssetDocumentCapabilityContext& Context,
	const FName BodyKey,
	const TSharedPtr<FJsonValue>& CurrentValue,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
{
	FAssetDocumentObjectRegionAdapterHooks Hooks;
	Hooks.ExtractObject = [CurrentValue](const FAssetDocumentRegionContext&, TSharedRef<FJsonObject>& OutObject)
	{
		if (!CurrentValue.IsValid() || CurrentValue->Type != EJson::Object || !CurrentValue->AsObject().IsValid())
		{
			return BodyFailure(TEXT("Current object region value is invalid"), TEXT("/Body"), TEXT("InvalidCurrentBodySectionType"));
		}
		OutObject = CurrentValue->AsObject().ToSharedRef();
		return FAssetDocumentCapabilityResult::Success(TEXT("Extracted current object pilot region"));
	};
	Hooks.DiffObject = [CurrentValue, DesiredValue](const FAssetDocumentRegionContext& RegionContext, const TSharedRef<FJsonObject>&, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
	{
		const FString Status = JsonValueToComparableString(CurrentValue) == JsonValueToComparableString(DesiredValue)
			? TEXT("unchanged")
			: TEXT("changed");
		AddBodyDiffEntry(OutDiffEntries, RegionContext.JsonPointer, Status, CurrentValue, DesiredValue);
		return FAssetDocumentCapabilityResult::Success(TEXT("Diffed AnimSequence object pilot region"));
	};

	FAssetDocumentObjectRegionAdapter Adapter(FName(*FString::Printf(TEXT("AnimSequence%sObjectRegionAdapter"), *BodyKey.ToString())), MoveTemp(Hooks));
	FAssetDocumentRegionPolicy Policy;
	if (!FindAnimSequencePilotPolicy(BodyKey, Policy))
	{
		return MissingAnimSequencePilotPolicyFailure(BodyKey);
	}
	const FAssetDocumentRegionContext RegionContext =
		MakeAnimSequencePilotRegionContext(Context, BodyKey, Policy);
	return FAssetDocumentRegionRuntime::Diff(RegionContext, DesiredValue, Adapter, OutDiffEntries);
}

FAssetDocumentCapabilityResult DiffAnimSequenceNotifyTracksPilotRegion(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedPtr<FJsonValue>& CurrentValue,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
{
	FAssetDocumentNamedArrayRegionAdapterHooks Hooks;
	Hooks.ExtractElements = [CurrentValue](const FAssetDocumentRegionContext&, TArray<TSharedRef<FJsonObject>>& OutElements)
	{
		if (!CurrentValue.IsValid() || CurrentValue->Type != EJson::Array)
		{
			return BodyFailure(TEXT("Current NotifyTracks value is invalid"), TEXT("/Body/NotifyTracks"), TEXT("InvalidCurrentBodySectionType"));
		}
		for (const TSharedPtr<FJsonValue>& Entry : CurrentValue->AsArray())
		{
			if (!Entry.IsValid() || Entry->Type != EJson::Object || !Entry->AsObject().IsValid())
			{
				return BodyFailure(TEXT("Current NotifyTracks entry is invalid"), TEXT("/Body/NotifyTracks"), TEXT("InvalidCurrentBodySectionType"));
			}
			OutElements.Add(Entry->AsObject().ToSharedRef());
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("Extracted current NotifyTracks pilot region"));
	};
	Hooks.DiffElements = [CurrentValue, DesiredValue](const FAssetDocumentRegionContext& RegionContext, const TArray<TSharedRef<FJsonObject>>&, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
	{
		const FString Status = JsonValueToComparableString(CurrentValue) == JsonValueToComparableString(DesiredValue)
			? TEXT("unchanged")
			: TEXT("changed");
		AddBodyDiffEntry(OutDiffEntries, RegionContext.JsonPointer, Status, CurrentValue, DesiredValue);
		return FAssetDocumentCapabilityResult::Success(TEXT("Diffed AnimSequence NotifyTracks pilot region"));
	};

	FAssetDocumentNamedArrayRegionAdapter Adapter(
		FAnimSequenceAssetDocumentProfile::MakeNotifyTracksNamedArrayConfig(),
		MoveTemp(Hooks));
	FAssetDocumentRegionPolicy Policy;
	if (!FindAnimSequencePilotPolicy(TEXT("NotifyTracks"), Policy))
	{
		return MissingAnimSequencePilotPolicyFailure(TEXT("NotifyTracks"));
	}
	const FAssetDocumentRegionContext RegionContext =
		MakeAnimSequencePilotRegionContext(Context, TEXT("NotifyTracks"), Policy);
	return FAssetDocumentRegionRuntime::Diff(RegionContext, DesiredValue, Adapter, OutDiffEntries);
}

FAssetDocumentCapabilityResult FAnimSequenceAssetDocumentCapability::Extract(const FAssetDocumentCapabilityContext& Context, TSharedRef<FJsonObject>& OutBodyJson) const
{
	const UAnimSequence* Sequence = Cast<UAnimSequence>(Context.Asset);
	if (!Sequence)
	{
		return BodyFailure(TEXT("AnimSequence body extract requires UAnimSequence asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	FAssetDocumentFragmentCompiler Compiler;
	Compiler.RegisterBuiltInAdapters();

	TSharedRef<FJsonObject> References = MakeShared<FJsonObject>();
	if (USkeleton* Skeleton = Sequence->GetSkeleton())
	{
		TSharedRef<FJsonObject> SkeletonRef = MakeShared<FJsonObject>();
		FAssetDocumentCapabilityResult Result = ExtractAssetRef(Compiler, const_cast<UAnimSequence*>(Sequence), Skeleton, TEXT("/Body/References/Skeleton"), SkeletonRef);
		if (!Result.bSuccess)
		{
			return Result;
		}
		References->SetObjectField(TEXT("Skeleton"), SkeletonRef);
	}
	References->SetStringField(TEXT("RetargetSource"), Sequence->RetargetSource.ToString());
	if (USkeletalMesh* RetargetSourceAsset = Sequence->GetRetargetSourceAsset().LoadSynchronous())
	{
		TSharedRef<FJsonObject> RetargetSourceAssetRef = MakeShared<FJsonObject>();
		FAssetDocumentCapabilityResult Result = ExtractAssetRef(Compiler, const_cast<UAnimSequence*>(Sequence), RetargetSourceAsset, TEXT("/Body/References/RetargetSourceAsset"), RetargetSourceAssetRef);
		if (!Result.bSuccess)
		{
			return Result;
		}
		References->SetObjectField(TEXT("RetargetSourceAsset"), RetargetSourceAssetRef);
	}
	OutBodyJson->SetObjectField(TEXT("References"), References);

	TSharedPtr<FJsonValue> PreviewValue;
	FAssetDocumentCapabilityResult PilotExtractResult =
		ExtractAnimSequencePreviewRegion(Compiler, Context, Sequence, PreviewValue);
	if (!PilotExtractResult.bSuccess)
	{
		return PilotExtractResult;
	}
	OutBodyJson->SetField(TEXT("Preview"), PreviewValue);

	TSharedPtr<FJsonValue> PlaybackValue;
	PilotExtractResult = ExtractAnimSequencePlaybackRegion(Context, Sequence, PlaybackValue);
	if (!PilotExtractResult.bSuccess)
	{
		return PilotExtractResult;
	}
	OutBodyJson->SetField(TEXT("Playback"), PlaybackValue);

	TSharedRef<FJsonObject> Additive = MakeShared<FJsonObject>();
	Additive->SetStringField(TEXT("AdditiveAnimType"), AdditiveAnimTypeToString(static_cast<EAdditiveAnimationType>(Sequence->AdditiveAnimType.GetValue())));
	Additive->SetStringField(TEXT("RefPoseType"), RefPoseTypeToString(static_cast<EAdditiveBasePoseType>(Sequence->RefPoseType.GetValue())));
	Additive->SetNumberField(TEXT("RefFrameIndex"), Sequence->RefFrameIndex);
	if (Sequence->RefPoseSeq)
	{
		TSharedRef<FJsonObject> RefPoseSeqRef = MakeShared<FJsonObject>();
		FAssetDocumentCapabilityResult Result = ExtractAssetRef(Compiler, const_cast<UAnimSequence*>(Sequence), Sequence->RefPoseSeq, TEXT("/Body/Additive/RefPoseSeq"), RefPoseSeqRef);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Additive->SetObjectField(TEXT("RefPoseSeq"), RefPoseSeqRef);
	}
	else
	{
		Additive->SetField(TEXT("RefPoseSeq"), MakeShared<FJsonValueNull>());
	}
	OutBodyJson->SetObjectField(TEXT("Additive"), Additive);

	TSharedRef<FJsonObject> RootMotion = MakeShared<FJsonObject>();
	RootMotion->SetBoolField(TEXT("bEnableRootMotion"), Sequence->bEnableRootMotion);
	RootMotion->SetStringField(TEXT("RootMotionRootLock"), RootMotionRootLockToString(Sequence->RootMotionRootLock));
	RootMotion->SetBoolField(TEXT("bForceRootLock"), Sequence->bForceRootLock);
	RootMotion->SetBoolField(TEXT("bUseNormalizedRootMotionScale"), Sequence->bUseNormalizedRootMotionScale);
	OutBodyJson->SetObjectField(TEXT("RootMotion"), RootMotion);

	TSharedRef<FJsonObject> Compression = MakeShared<FJsonObject>();
	Compression->SetNumberField(TEXT("CompressionErrorThresholdScale"), Sequence->CompressionErrorThresholdScale);
	if (Sequence->BoneCompressionSettings)
	{
		TSharedRef<FJsonObject> BoneCompressionRef = MakeShared<FJsonObject>();
		FAssetDocumentCapabilityResult Result = ExtractAssetRef(Compiler, const_cast<UAnimSequence*>(Sequence), Sequence->BoneCompressionSettings, TEXT("/Body/Compression/BoneCompressionSettings"), BoneCompressionRef);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Compression->SetObjectField(TEXT("BoneCompressionSettings"), BoneCompressionRef);
	}
	if (Sequence->CurveCompressionSettings)
	{
		TSharedRef<FJsonObject> CurveCompressionRef = MakeShared<FJsonObject>();
		FAssetDocumentCapabilityResult Result = ExtractAssetRef(Compiler, const_cast<UAnimSequence*>(Sequence), Sequence->CurveCompressionSettings, TEXT("/Body/Compression/CurveCompressionSettings"), CurveCompressionRef);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Compression->SetObjectField(TEXT("CurveCompressionSettings"), CurveCompressionRef);
	}
	Compression->SetBoolField(TEXT("bDoNotOverrideCompression"), !!Sequence->bDoNotOverrideCompression);
	OutBodyJson->SetObjectField(TEXT("Compression"), Compression);

	OutBodyJson->SetArrayField(TEXT("Curves"), ExtractAnimSequenceCurves(Sequence));
	TSharedPtr<FJsonValue> NotifyTracksValue;
	PilotExtractResult = ExtractAnimSequenceNotifyTracksRegion(Context, Sequence, NotifyTracksValue);
	if (!PilotExtractResult.bSuccess)
	{
		return PilotExtractResult;
	}
	OutBodyJson->SetField(TEXT("NotifyTracks"), NotifyTracksValue);
	TArray<TSharedPtr<FJsonValue>> ExtractedNotifies;
	FAssetDocumentCapabilityResult NotifyExtractResult = ExtractAnimSequenceNotifies(Compiler, Sequence, false, ExtractedNotifies);
	if (!NotifyExtractResult.bSuccess)
	{
		return NotifyExtractResult;
	}
	OutBodyJson->SetArrayField(TEXT("Notifies"), ExtractedNotifies);
	TArray<TSharedPtr<FJsonValue>> ExtractedNotifyStates;
	NotifyExtractResult = ExtractAnimSequenceNotifies(Compiler, Sequence, true, ExtractedNotifyStates);
	if (!NotifyExtractResult.bSuccess)
	{
		return NotifyExtractResult;
	}
	OutBodyJson->SetArrayField(TEXT("NotifyStates"), ExtractedNotifyStates);
	OutBodyJson->SetArrayField(TEXT("SyncMarkers"), ExtractAnimSequenceSyncMarkers(Sequence));
	TSharedPtr<FJsonValue> ExtractedMetadata;
	FAssetDocumentCapabilityResult ObjectFragmentExtractResult = ExtractManagedMetadataRegion(Compiler, Context, Sequence, ExtractedMetadata);
	if (!ObjectFragmentExtractResult.bSuccess)
	{
		return ObjectFragmentExtractResult;
	}
	OutBodyJson->SetField(TEXT("Metadata"), ExtractedMetadata);
	TSharedPtr<FJsonValue> ExtractedAssetUserData;
	ObjectFragmentExtractResult = ExtractManagedAssetUserDataRegion(Compiler, Context, Sequence, ExtractedAssetUserData);
	if (!ObjectFragmentExtractResult.bSuccess)
	{
		return ObjectFragmentExtractResult;
	}
	OutBodyJson->SetField(TEXT("AssetUserData"), ExtractedAssetUserData);

	return FAssetDocumentCapabilityResult::Success(TEXT("AnimSequence Body extracted"));
}

FAssetDocumentCapabilityResult FAnimSequenceAssetDocumentCapability::Diff(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& DesiredJson, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	FAssetDocumentFragmentCompiler Compiler;
	Compiler.RegisterBuiltInAdapters();

	FAssetDocumentPreviewApplyDiffHooks Hooks;
	Hooks.ValidateDesiredBody = [&Compiler](
		const FAssetDocumentCapabilityContext& ValidateContext,
		const TSharedRef<FJsonObject>& DesiredBody)
	{
		FParsedAnimSequenceBody ParsedForValidation;
		return ParseAnimSequenceBody(
			&Compiler,
			ValidateContext,
			Cast<UAnimSequence>(ValidateContext.Asset),
			DesiredBody,
			false,
			ParsedForValidation);
	};
	Hooks.DuplicatePreviewAsset = [](const FAssetDocumentCapabilityContext& DiffContext, UObject*& OutPreviewAsset)
	{
		UAnimSequence* CurrentSequence = Cast<UAnimSequence>(DiffContext.Asset);
		if (!CurrentSequence)
		{
			return BodyFailure(TEXT("AnimSequence body diff requires UAnimSequence asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
		}

		UAnimSequence* PreviewSequence = DuplicateObject<UAnimSequence>(CurrentSequence, GetTransientPackage());
		if (!PreviewSequence)
		{
			return BodyFailure(TEXT("Failed to duplicate AnimSequence for Body diff"), TEXT("/Body"), TEXT("DuplicateFailed"));
		}

		OutPreviewAsset = PreviewSequence;
		return FAssetDocumentCapabilityResult::Success(TEXT("Duplicated AnimSequence for Body diff"));
	};
	Hooks.MakePreviewContext = [](const FAssetDocumentCapabilityContext& DiffContext, UObject* PreviewAsset)
	{
		FAssetDocumentCapabilityContext PreviewContext = DiffContext;
		PreviewContext.Asset = PreviewAsset;
		PreviewContext.AssetClass = UAnimSequence::StaticClass();
		PreviewContext.bIsDryRun = true;
		return PreviewContext;
	};
	Hooks.ApplyDesiredBody = [this](
		const FAssetDocumentCapabilityContext& PreviewContext,
		const TSharedRef<FJsonValue>& DesiredBody)
	{
		FAssetDocumentCapabilityContext MutablePreviewContext = PreviewContext;
		return const_cast<FAnimSequenceAssetDocumentCapability*>(this)->Apply(MutablePreviewContext, DesiredBody);
	};
	Hooks.ExtractBody = [this](
		const FAssetDocumentCapabilityContext& ExtractContext,
		const TSharedRef<FJsonObject>& OutBody)
	{
		TSharedRef<FJsonObject> MutableOutBody = OutBody;
		return Extract(ExtractContext, MutableOutBody);
	};
	Hooks.DiffBodyKey = [](
		const FAssetDocumentPreviewApplyDiffBodyKeyContext& KeyContext,
		TArray<TSharedPtr<FJsonValue>>& OutEntries,
		bool& bOutHandled)
	{
		bOutHandled = false;
		if (!KeyContext.CurrentContext)
		{
			return FAssetDocumentCapabilityResult::Failure(
				TEXT("Missing current diff context"),
				TEXT("/Body"),
				TEXT("InvalidPreviewApplyDiffAdapter"));
		}
		if (KeyContext.BodyKey == TEXT("Preview") || KeyContext.BodyKey == TEXT("Playback"))
		{
			bOutHandled = true;
			return DiffAnimSequenceObjectPilotRegion(
				*KeyContext.CurrentContext,
				FName(*KeyContext.BodyKey),
				KeyContext.CurrentValue,
				KeyContext.DesiredValue,
				OutEntries);
		}
		if (KeyContext.BodyKey == TEXT("NotifyTracks"))
		{
			bOutHandled = true;
			return DiffAnimSequenceNotifyTracksPilotRegion(
				*KeyContext.CurrentContext,
				KeyContext.CurrentValue,
				KeyContext.DesiredValue,
				OutEntries);
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("Default diff"));
	};

	FAssetDocumentPreviewApplyDiffAdapter Adapter(MoveTemp(Hooks));
	const FAssetDocumentCapabilityResult DiffResult = Adapter.DiffBody(Context, DesiredJson, OutDiffEntries);
	if (!DiffResult.bSuccess)
	{
		return DiffResult;
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("AnimSequence Body diffed"));
}

FAssetDocumentCapabilityResult FAnimSequenceAssetDocumentCapability::ValidateBodyObject(const TSharedRef<FJsonObject>& BodyObject) const
{
	FAssetDocumentFragmentCompiler Compiler;
	Compiler.RegisterBuiltInAdapters();

	FAssetDocumentCapabilityContext Context;
	FParsedAnimSequenceBody ParsedBody;
	const FAssetDocumentCapabilityResult Result = ParseAnimSequenceBody(&Compiler, Context, nullptr, BodyObject, false, ParsedBody);
	if (!Result.bSuccess)
	{
		return Result;
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("AnimSequence Body is valid"));
}
