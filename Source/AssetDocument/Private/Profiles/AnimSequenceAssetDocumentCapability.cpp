// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/AnimSequenceAssetDocumentCapability.h"

#include "AssetDocumentFragmentCompiler.h"

#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimTypes.h"
#include "Curves/RichCurve.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "UObject/UObjectGlobals.h"

namespace
{
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

TSharedRef<FJsonObject> MakeBodyObjectForDiff(const TSharedRef<FJsonObject>& BodyObject)
{
	if (!BodyObject->HasField(TEXT("_Skipped")))
	{
		return BodyObject;
	}

	TSharedRef<FJsonObject> DiffBody = MakeShared<FJsonObject>();
	DiffBody->Values = BodyObject->Values;
	DiffBody->RemoveField(TEXT("_Skipped"));
	return DiffBody;
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
	if (Flags == AACF_DefaultCurve)
	{
		FlagValues.Add(MakeShared<FJsonValueString>(TEXT("Default")));
		return FlagValues;
	}
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
	const TArray<FString> NotImplementedBodyKeys = {
		TEXT("Notifies"),
		TEXT("NotifyStates"),
		TEXT("NotifyTracks"),
		TEXT("SyncMarkers"),
		TEXT("Metadata"),
		TEXT("AssetUserData"),
	};
	for (const FString& BodyKey : NotImplementedBodyKeys)
	{
		if (BodyObject->HasField(BodyKey))
		{
			const FString Path = FString::Printf(TEXT("/Body/%s"), *BodyKey);
			return BodyFailure(
				FString::Printf(TEXT("Body.%s is declared but not implemented by the AnimSequence Task 2 scalar capability"), *BodyKey),
				Path,
				TEXT("NotImplementedBodyRegion"));
		}
	}

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
			return BodyFailure(FString::Printf(TEXT("Body.%s is not an AnimSequence AssetDocument authored field"), *UnsupportedKey), Path, TEXT("UnsupportedAuthoredField"));
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

struct FParsedAnimSequenceCurve
{
	FName Name = NAME_None;
	int32 Flags = AACF_DefaultCurve;
	TArray<FRichCurveKey> Keys;
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

			FRichCurveKey Key(static_cast<float>(Time), static_cast<float>(Value));
			Key.InterpMode = InterpMode;
			ParsedCurve.Keys.Add(Key);
		}

		ParsedCurve.Keys.Sort([](const FRichCurveKey& Left, const FRichCurveKey& Right)
		{
			return Left.Time < Right.Time;
		});
		for (int32 KeyIndex = 1; KeyIndex < ParsedCurve.Keys.Num(); ++KeyIndex)
		{
			if (ParsedCurve.Keys[KeyIndex - 1].Time == ParsedCurve.Keys[KeyIndex].Time)
			{
				return BodyFailure(TEXT("Curve key times must be unique"), FString::Printf(TEXT("%s/Keys/%d/Time"), *CurvePath, KeyIndex), TEXT("DuplicateCurveKeyTime"));
			}
		}

		OutCurves.Add(MoveTemp(ParsedCurve));
	}

	OutCurves.Sort([](const FParsedAnimSequenceCurve& Left, const FParsedAnimSequenceCurve& Right)
	{
		return Left.Name.LexicalLess(Right.Name);
	});
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyAnimSequenceCurves(UAnimSequence* Sequence, const TArray<FParsedAnimSequenceCurve>& Curves)
{
	if (!Sequence)
	{
		return BodyFailure(TEXT("AnimSequence curve apply requires UAnimSequence asset"), TEXT("/Body/Curves"), TEXT("UnsupportedAsset"));
	}

	IAnimationDataController& Controller = Sequence->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Apply AnimSequence AssetDocument Curves")), false);

	auto CloseAndFail = [&Controller](const FString& Message, const FString& Path, const FString& Code) -> FAssetDocumentCapabilityResult
	{
		Controller.CloseBracket(false);
		return BodyFailure(Message, Path, Code);
	};

	Controller.RemoveAllCurvesOfType(ERawCurveTrackTypes::RCT_Float, false);
	for (const FParsedAnimSequenceCurve& Curve : Curves)
	{
		const FAnimationCurveIdentifier CurveId(Curve.Name, ERawCurveTrackTypes::RCT_Float);
		const FString CurvePath = FString::Printf(TEXT("/Body/Curves/%s"), *Curve.Name.ToString());
		if (!Controller.AddCurve(CurveId, Curve.Flags, false))
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

TArray<TSharedPtr<FJsonValue>> ExtractAnimSequenceCurves(const UAnimSequence* Sequence)
{
	TArray<TSharedPtr<FJsonValue>> CurveValues;
	const IAnimationDataModel* DataModel = Sequence ? Sequence->GetDataModel() : nullptr;
	if (!DataModel)
	{
		return CurveValues;
	}

	TArray<const FFloatCurve*> FloatCurves;
	for (const FFloatCurve& FloatCurve : DataModel->GetFloatCurves())
	{
		FloatCurves.Add(&FloatCurve);
	}
	FloatCurves.Sort([](const FFloatCurve& Left, const FFloatCurve& Right)
	{
		return Left.GetName().LexicalLess(Right.GetName());
	});

	for (const FFloatCurve* FloatCurve : FloatCurves)
	{
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
};

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
		RejectUnknownObjectFields(BodyObject, TEXT("Preview"), { TEXT("PreviewMesh") }),
		RejectUnknownObjectFields(BodyObject, TEXT("Playback"), { TEXT("RateScale") }),
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
		TSharedPtr<FJsonObject> PreviewObject;
		FAssetDocumentCapabilityResult Result = RequireObjectValue(*PreviewValue, TEXT("/Body/Preview"), PreviewObject);
		if (!Result.bSuccess)
		{
			return Result;
		}

		UObject* PreviewMeshObject = nullptr;
		Result = ParseAssetRef(Compiler, Context, Sequence, PreviewObject.ToSharedRef(), TEXT("PreviewMesh"), USkeletalMesh::StaticClass(), TEXT("/Body/Preview/PreviewMesh"), bResolveFragments, true, OutParsed.bHasPreviewMesh, PreviewMeshObject);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutParsed.PreviewMesh = Cast<USkeletalMesh>(PreviewMeshObject);
	}

	if (const TSharedPtr<FJsonValue>* PlaybackValue = BodyObject->Values.Find(TEXT("Playback")))
	{
		TSharedPtr<FJsonObject> PlaybackObject;
		FAssetDocumentCapabilityResult Result = RequireObjectValue(*PlaybackValue, TEXT("/Body/Playback"), PlaybackObject);
		if (!Result.bSuccess)
		{
			return Result;
		}

		double RateScale = 0.0;
		Result = ReadOptionalNumber(PlaybackObject.ToSharedRef(), TEXT("RateScale"), TEXT("/Body/Playback/RateScale"), OutParsed.bHasRateScale, RateScale);
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
	return {GetName()};
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
	Hint->SetStringField(TEXT("Preview"), TEXT("object: PreviewMesh, PreviewPoseAsset"));
	Hint->SetStringField(TEXT("Playback"), TEXT("object: RateScale; derived length/sample fields are extract-only"));
	Hint->SetStringField(TEXT("Additive"), TEXT("object: AdditiveAnimType, RefPoseType, RefFrameIndex, RefPoseSeq"));
	Hint->SetStringField(TEXT("RootMotion"), TEXT("object: bEnableRootMotion, RootMotionRootLock, bForceRootLock, bUseNormalizedRootMotionScale"));
	Hint->SetStringField(TEXT("Compression"), TEXT("object: CompressionErrorThresholdScale, BoneCompressionSettings, CurveCompressionSettings, bDoNotOverrideCompression"));
	Hint->SetStringField(TEXT("Curves"), TEXT("array: sequence-owned float curves"));
	Hint->SetStringField(TEXT("Notifies"), TEXT("array: point notify placements"));
	Hint->SetStringField(TEXT("NotifyStates"), TEXT("array: ranged notify-state placements"));
	Hint->SetStringField(TEXT("NotifyTracks"), TEXT("array: notify track names/order"));
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
	if (ParsedBody.bHasPreviewMesh)
	{
		Sequence->SetPreviewMesh(ParsedBody.PreviewMesh, false);
	}
	if (ParsedBody.bHasRateScale)
	{
		Sequence->RateScale = ParsedBody.RateScale;
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
	if (ParsedBody.bHasCurves)
	{
		const FAssetDocumentCapabilityResult CurvesResult = ApplyAnimSequenceCurves(Sequence, ParsedBody.Curves);
		if (!CurvesResult.bSuccess)
		{
			return CurvesResult;
		}
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

	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	if (USkeletalMesh* PreviewMesh = Sequence->GetPreviewMesh())
	{
		TSharedRef<FJsonObject> PreviewMeshRef = MakeShared<FJsonObject>();
		FAssetDocumentCapabilityResult Result = ExtractAssetRef(Compiler, const_cast<UAnimSequence*>(Sequence), PreviewMesh, TEXT("/Body/Preview/PreviewMesh"), PreviewMeshRef);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Preview->SetObjectField(TEXT("PreviewMesh"), PreviewMeshRef);
	}
	OutBodyJson->SetObjectField(TEXT("Preview"), Preview);

	TSharedRef<FJsonObject> Playback = MakeShared<FJsonObject>();
	Playback->SetNumberField(TEXT("RateScale"), Sequence->RateScale);
	OutBodyJson->SetObjectField(TEXT("Playback"), Playback);

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

	return FAssetDocumentCapabilityResult::Success(TEXT("AnimSequence Body extracted"));
}

FAssetDocumentCapabilityResult FAnimSequenceAssetDocumentCapability::Diff(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& DesiredJson, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	if (DesiredJson->Type != EJson::Object)
	{
		return BodyFailure(TEXT("Body must be a JSON object"), TEXT("/Body"), TEXT("InvalidBodyType"));
	}

	const TSharedPtr<FJsonObject> DesiredBody = DesiredJson->AsObject();
	if (!DesiredBody.IsValid())
	{
		return BodyFailure(TEXT("Body must be a JSON object"), TEXT("/Body"), TEXT("InvalidBodyType"));
	}

	const bool bHasSkippedMetadata = DesiredBody->HasField(TEXT("_Skipped"));
	const TSharedRef<FJsonObject> DesiredBodyForDiff = MakeBodyObjectForDiff(DesiredBody.ToSharedRef());
	const TSharedRef<FJsonValue> DesiredJsonForDiff = bHasSkippedMetadata
		? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueObject>(DesiredBodyForDiff))
		: DesiredJson;

	FAssetDocumentFragmentCompiler Compiler;
	Compiler.RegisterBuiltInAdapters();

	FParsedAnimSequenceBody ParsedForValidation;
	const FAssetDocumentCapabilityResult ValidateResult = ParseAnimSequenceBody(&Compiler, Context, Cast<UAnimSequence>(Context.Asset), DesiredBodyForDiff, false, ParsedForValidation);
	if (!ValidateResult.bSuccess)
	{
		return ValidateResult;
	}

	UAnimSequence* CurrentSequence = Cast<UAnimSequence>(Context.Asset);
	if (!CurrentSequence)
	{
		return BodyFailure(TEXT("AnimSequence body diff requires UAnimSequence asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	UAnimSequence* PreviewSequence = DuplicateObject<UAnimSequence>(CurrentSequence, GetTransientPackage());
	if (!PreviewSequence)
	{
		return BodyFailure(TEXT("Failed to duplicate AnimSequence for Body diff"), TEXT("/Body"), TEXT("DuplicateFailed"));
	}

	FAssetDocumentCapabilityContext PreviewContext = Context;
	PreviewContext.Asset = PreviewSequence;
	PreviewContext.AssetClass = UAnimSequence::StaticClass();
	PreviewContext.bIsDryRun = true;

	FAssetDocumentCapabilityResult ApplyResult = const_cast<FAnimSequenceAssetDocumentCapability*>(this)->Apply(PreviewContext, DesiredJsonForDiff);
	if (!ApplyResult.bSuccess)
	{
		return ApplyResult;
	}

	TSharedRef<FJsonObject> CurrentBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ExtractResult = Extract(Context, CurrentBody);
	if (!ExtractResult.bSuccess)
	{
		return ExtractResult;
	}
	CurrentBody->RemoveField(TEXT("_Skipped"));

	TSharedRef<FJsonObject> PreviewBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult PreviewExtractResult = Extract(PreviewContext, PreviewBody);
	if (!PreviewExtractResult.bSuccess)
	{
		return PreviewExtractResult;
	}
	PreviewBody->RemoveField(TEXT("_Skipped"));

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : DesiredBodyForDiff->Values)
	{
		if (Pair.Key == TEXT("_Skipped"))
		{
			continue;
		}

		const TSharedPtr<FJsonValue>* CurrentValue = CurrentBody->Values.Find(Pair.Key);
		const TSharedPtr<FJsonValue> Current = CurrentValue ? *CurrentValue : MakeShared<FJsonValueNull>();
		const TSharedPtr<FJsonValue>* DesiredValue = PreviewBody->Values.Find(Pair.Key);
		const TSharedPtr<FJsonValue> Desired = DesiredValue ? *DesiredValue : MakeShared<FJsonValueNull>();
		const FString Status = JsonValueToComparableString(Current) == JsonValueToComparableString(Desired)
			? TEXT("unchanged")
			: TEXT("changed");
		AddBodyDiffEntry(OutDiffEntries, FString::Printf(TEXT("/Body/%s"), *Pair.Key), Status, Current, Desired);
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
