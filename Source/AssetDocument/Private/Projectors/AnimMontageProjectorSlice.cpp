#include "Projectors/AnimMontageProjectorSlice.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "Profiles/AnimMontageAssetDocumentCapability.h"
#include "Projectors/AssetDocumentProjectionTypes.h"
#include "Projectors/AssetDocumentRefMaterializer.h"

namespace
{
constexpr const TCHAR* BodyPath = TEXT("/Body");

FAnimMontageProjectorSliceResult ToSliceFailure(const FAssetDocumentUpdateResult& UpdateResult)
{
	return FAnimMontageProjectorSliceResult::Failure(UpdateResult.Message);
}

bool IsJsonArray(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && Value->Type == EJson::Array;
}

bool IsJsonObject(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && Value->Type == EJson::Object;
}

bool IsJsonNullOrObject(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && (Value->Type == EJson::Null || Value->Type == EJson::Object);
}

FString MakeBodyFieldPath(const FString& FieldName)
{
	return FString::Printf(TEXT("%s/%s"), BodyPath, *FieldName);
}

struct FStagedObjectReference
{
	bool bPresent = false;
	UObject* Object = nullptr;
};

struct FStagedBlendUpdate
{
	bool bPresent = false;
	bool bHasBlendInTime = false;
	bool bHasBlendOutTime = false;
	float BlendInTime = 0.0f;
	float BlendOutTime = 0.0f;
};

struct FStagedMontageApply
{
	FStagedObjectReference Skeleton;
	FStagedObjectReference PreviewMesh;
	bool bHasSlotAnimTracks = false;
	TArray<FSlotAnimationTrack> SlotAnimTracks;
	float CompositeLength = 0.0f;
	bool bHasCompositeSections = false;
	TArray<FCompositeSection> CompositeSections;
	bool bHasNotifies = false;
	bool bHasNotifyStates = false;
	FStagedBlendUpdate Blend;
};

FAnimMontageProjectorSliceResult RequireArrayField(const TSharedRef<FJsonObject>& Body, const FString& FieldName)
{
	if (const TSharedPtr<FJsonValue>* Value = Body->Values.Find(FieldName))
	{
		if (!IsJsonArray(*Value))
		{
			return FAnimMontageProjectorSliceResult::Failure(FString::Printf(TEXT("%s must be an array."), *MakeBodyFieldPath(FieldName)));
		}
	}
	return FAnimMontageProjectorSliceResult::Success();
}

FAnimMontageProjectorSliceResult RequireNullOrObjectField(const TSharedRef<FJsonObject>& Body, const FString& FieldName)
{
	if (const TSharedPtr<FJsonValue>* Value = Body->Values.Find(FieldName))
	{
		if (!IsJsonNullOrObject(*Value))
		{
			return FAnimMontageProjectorSliceResult::Failure(FString::Printf(TEXT("%s must be null or an object."), *MakeBodyFieldPath(FieldName)));
		}
	}
	return FAnimMontageProjectorSliceResult::Success();
}

FAnimMontageProjectorSliceResult RequireObjectField(const TSharedRef<FJsonObject>& Body, const FString& FieldName)
{
	if (const TSharedPtr<FJsonValue>* Value = Body->Values.Find(FieldName))
	{
		if (!IsJsonObject(*Value))
		{
			return FAnimMontageProjectorSliceResult::Failure(FString::Printf(TEXT("%s must be an object."), *MakeBodyFieldPath(FieldName)));
		}
	}
	return FAnimMontageProjectorSliceResult::Success();
}

FAnimMontageProjectorSliceResult ReadOptionalNonNegativeNumber(
	const TSharedRef<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	bool& bOutHasValue,
	double& OutValue)
{
	bOutHasValue = false;
	if (const TSharedPtr<FJsonValue>* Value = Object->Values.Find(FieldName))
	{
		if (!Value->IsValid() || (*Value)->Type != EJson::Number)
		{
			return FAnimMontageProjectorSliceResult::Failure(FString::Printf(TEXT("%s must be a number."), *Path));
		}

		OutValue = (*Value)->AsNumber();
		if (OutValue < 0.0)
		{
			return FAnimMontageProjectorSliceResult::Failure(FString::Printf(TEXT("%s must be non-negative."), *Path));
		}

		bOutHasValue = true;
	}
	return FAnimMontageProjectorSliceResult::Success();
}

FAnimMontageProjectorSliceResult ReadOptionalNumber(
	const TSharedRef<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	bool& bOutHasValue,
	double& OutValue)
{
	bOutHasValue = false;
	if (const TSharedPtr<FJsonValue>* Value = Object->Values.Find(FieldName))
	{
		if (!Value->IsValid() || (*Value)->Type != EJson::Number)
		{
			return FAnimMontageProjectorSliceResult::Failure(FString::Printf(TEXT("%s must be a number."), *Path));
		}
		OutValue = (*Value)->AsNumber();
		bOutHasValue = true;
	}
	return FAnimMontageProjectorSliceResult::Success();
}

FAnimMontageProjectorSliceResult ValidateCompositeSections(const TSharedRef<FJsonObject>& Body)
{
	const TArray<TSharedPtr<FJsonValue>>* Sections = nullptr;
	if (!Body->TryGetArrayField(TEXT("CompositeSections"), Sections))
	{
		return FAnimMontageProjectorSliceResult::Success();
	}

	TSet<FName> SectionNames;
	TMap<int32, FName> NextSectionNames;

	for (int32 SectionIndex = 0; SectionIndex < Sections->Num(); ++SectionIndex)
	{
		const FString SectionPath = FString::Printf(TEXT("/Body/CompositeSections/%d"), SectionIndex);
		const TSharedPtr<FJsonValue>& SectionValue = (*Sections)[SectionIndex];
		if (!SectionValue.IsValid() || SectionValue->Type != EJson::Object)
		{
			return FAnimMontageProjectorSliceResult::Failure(FString::Printf(TEXT("%s must be an object."), *SectionPath));
		}

		const TSharedPtr<FJsonObject> SectionObject = SectionValue->AsObject();
		FString SectionNameString;
		if (!SectionObject.IsValid() || !SectionObject->TryGetStringField(TEXT("SectionName"), SectionNameString) || SectionNameString.TrimStartAndEnd().IsEmpty())
		{
			return FAnimMontageProjectorSliceResult::Failure(FString::Printf(TEXT("%s/SectionName must be non-empty."), *SectionPath));
		}
		SectionNames.Add(FName(*SectionNameString));

		FString NextSectionNameString;
		if (SectionObject->TryGetStringField(TEXT("NextSectionName"), NextSectionNameString) && !NextSectionNameString.TrimStartAndEnd().IsEmpty())
		{
			NextSectionNames.Add(SectionIndex, FName(*NextSectionNameString));
		}

		bool bHasNumber = false;
		double NumberValue = 0.0;
		const FAnimMontageProjectorSliceResult TimeResult = ReadOptionalNonNegativeNumber(
			SectionObject.ToSharedRef(),
			TEXT("LinkableTime"),
			SectionPath / TEXT("LinkableTime"),
			bHasNumber,
			NumberValue);
		if (!TimeResult.bSuccess)
		{
			return TimeResult;
		}
	}

	for (const TPair<int32, FName>& Pair : NextSectionNames)
	{
		if (!SectionNames.Contains(Pair.Value))
		{
			return FAnimMontageProjectorSliceResult::Failure(
				FString::Printf(TEXT("/Body/CompositeSections/%d/NextSectionName references missing section '%s'."),
					Pair.Key,
					*Pair.Value.ToString()));
		}
	}

	return FAnimMontageProjectorSliceResult::Success();
}

FAnimMontageProjectorSliceResult ValidateBlend(const TSharedRef<FJsonObject>& Body)
{
	const TSharedPtr<FJsonObject>* Blend = nullptr;
	if (!Body->TryGetObjectField(TEXT("Blend"), Blend) || !Blend || !Blend->IsValid())
	{
		return FAnimMontageProjectorSliceResult::Success();
	}

	bool bHasNumber = false;
	double NumberValue = 0.0;
	FAnimMontageProjectorSliceResult Result = ReadOptionalNonNegativeNumber(
		(*Blend).ToSharedRef(),
		TEXT("BlendInTime"),
		TEXT("/Body/Blend/BlendInTime"),
		bHasNumber,
		NumberValue);
	if (!Result.bSuccess)
	{
		return Result;
	}

	return ReadOptionalNonNegativeNumber(
		(*Blend).ToSharedRef(),
		TEXT("BlendOutTime"),
		TEXT("/Body/Blend/BlendOutTime"),
		bHasNumber,
		NumberValue);
}

FAnimMontageProjectorSliceResult ValidateEmptyNotifyArrayOnly(const TSharedRef<FJsonObject>& Body, const FString& FieldName)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Body->TryGetArrayField(FieldName, Values))
	{
		return FAnimMontageProjectorSliceResult::Success();
	}

	if (Values->Num() > 0)
	{
		return FAnimMontageProjectorSliceResult::Failure(FString::Printf(TEXT("%s currently supports only an empty array."), *MakeBodyFieldPath(FieldName)));
	}

	return FAnimMontageProjectorSliceResult::Success();
}

FAnimMontageProjectorSliceResult ValidateSlotAnimTracks(const TSharedRef<FJsonObject>& Body)
{
	const TArray<TSharedPtr<FJsonValue>>* SlotAnimTracks = nullptr;
	if (!Body->TryGetArrayField(TEXT("SlotAnimTracks"), SlotAnimTracks))
	{
		return FAnimMontageProjectorSliceResult::Success();
	}

	for (int32 SlotIndex = 0; SlotIndex < SlotAnimTracks->Num(); ++SlotIndex)
	{
		const FString SlotPath = FString::Printf(TEXT("/Body/SlotAnimTracks/%d"), SlotIndex);
		const TSharedPtr<FJsonValue>& SlotValue = (*SlotAnimTracks)[SlotIndex];
		if (!SlotValue.IsValid() || SlotValue->Type != EJson::Object)
		{
			return FAnimMontageProjectorSliceResult::Failure(FString::Printf(TEXT("%s must be an object."), *SlotPath));
		}

		const TSharedPtr<FJsonObject> SlotObject = SlotValue->AsObject();
		FString SlotNameString;
		if (!SlotObject.IsValid() || !SlotObject->TryGetStringField(TEXT("SlotName"), SlotNameString) || SlotNameString.TrimStartAndEnd().IsEmpty())
		{
			return FAnimMontageProjectorSliceResult::Failure(FString::Printf(TEXT("%s/SlotName must be non-empty."), *SlotPath));
		}

		const TSharedPtr<FJsonObject>* AnimTrack = nullptr;
		if (!SlotObject->TryGetObjectField(TEXT("AnimTrack"), AnimTrack) || !AnimTrack || !AnimTrack->IsValid())
		{
			return FAnimMontageProjectorSliceResult::Failure(FString::Printf(TEXT("%s/AnimTrack must be an object."), *SlotPath));
		}

		const TArray<TSharedPtr<FJsonValue>>* AnimSegments = nullptr;
		if (!(*AnimTrack)->TryGetArrayField(TEXT("AnimSegments"), AnimSegments))
		{
			return FAnimMontageProjectorSliceResult::Failure(FString::Printf(TEXT("%s/AnimTrack/AnimSegments must be an array."), *SlotPath));
		}

		for (int32 SegmentIndex = 0; SegmentIndex < AnimSegments->Num(); ++SegmentIndex)
		{
			const FString SegmentPath = FString::Printf(TEXT("%s/AnimTrack/AnimSegments/%d"), *SlotPath, SegmentIndex);
			const TSharedPtr<FJsonValue>& SegmentValue = (*AnimSegments)[SegmentIndex];
			if (!SegmentValue.IsValid() || SegmentValue->Type != EJson::Object)
			{
				return FAnimMontageProjectorSliceResult::Failure(FString::Printf(TEXT("%s must be an object."), *SegmentPath));
			}

			const TSharedPtr<FJsonObject> SegmentObject = SegmentValue->AsObject();
			if (!SegmentObject.IsValid())
			{
				return FAnimMontageProjectorSliceResult::Failure(FString::Printf(TEXT("%s must be an object."), *SegmentPath));
			}

			if (const TSharedPtr<FJsonValue>* AnimReference = SegmentObject->Values.Find(TEXT("AnimReference")))
			{
				if (!IsJsonNullOrObject(*AnimReference))
				{
					return FAnimMontageProjectorSliceResult::Failure(FString::Printf(TEXT("%s/AnimReference must be null or an object."), *SegmentPath));
				}
			}

			bool bHasNumber = false;
			double NumberValue = 0.0;
			const TArray<FString> NonNegativeFields = {
				TEXT("StartPos"),
				TEXT("AnimStartTime"),
				TEXT("AnimEndTime"),
			};
			for (const FString& FieldName : NonNegativeFields)
			{
				const FAnimMontageProjectorSliceResult NumberResult = ReadOptionalNonNegativeNumber(
					SegmentObject.ToSharedRef(),
					FieldName,
					SegmentPath / FieldName,
					bHasNumber,
					NumberValue);
				if (!NumberResult.bSuccess)
				{
					return NumberResult;
				}
			}

			FAnimMontageProjectorSliceResult NumberResult = ReadOptionalNumber(
				SegmentObject.ToSharedRef(),
				TEXT("AnimPlayRate"),
				SegmentPath / TEXT("AnimPlayRate"),
				bHasNumber,
				NumberValue);
			if (!NumberResult.bSuccess)
			{
				return NumberResult;
			}
			if (bHasNumber && NumberValue <= 0.0)
			{
				return FAnimMontageProjectorSliceResult::Failure(FString::Printf(TEXT("%s/AnimPlayRate must be greater than zero."), *SegmentPath));
			}

			NumberResult = ReadOptionalNumber(
				SegmentObject.ToSharedRef(),
				TEXT("LoopingCount"),
				SegmentPath / TEXT("LoopingCount"),
				bHasNumber,
				NumberValue);
			if (!NumberResult.bSuccess)
			{
				return NumberResult;
			}
			if (bHasNumber)
			{
				const double RoundedLoopingCount = FMath::RoundToDouble(NumberValue);
				if (NumberValue <= 0.0 || !FMath::IsNearlyEqual(NumberValue, RoundedLoopingCount))
				{
					return FAnimMontageProjectorSliceResult::Failure(FString::Printf(TEXT("%s/LoopingCount must be a positive integer."), *SegmentPath));
				}
			}
		}
	}

	return FAnimMontageProjectorSliceResult::Success();
}

FAnimMontageProjectorSliceResult StageObjectReference(
	const TSharedRef<FJsonObject>& Body,
	const FString& FieldName,
	const UClass* ExpectedClass,
	FStagedObjectReference& OutReference)
{
	const TSharedPtr<FJsonValue>* Value = Body->Values.Find(FieldName);
	if (!Value)
	{
		return FAnimMontageProjectorSliceResult::Success();
	}

	OutReference.bPresent = true;
	UObject* ResolvedObject = nullptr;
	if (Value->IsValid() && (*Value)->Type == EJson::Object)
	{
		FAssetDocumentRefMaterializer Materializer;
		const FAssetDocumentUpdateResult RefResult = Materializer.ResolveAssetRef(
			(*Value)->AsObject().ToSharedRef(),
			ExpectedClass,
			MakeBodyFieldPath(FieldName),
			ResolvedObject);
		if (!RefResult.bSuccess)
		{
			return ToSliceFailure(RefResult);
		}
	}

	OutReference.Object = ResolvedObject;
	return FAnimMontageProjectorSliceResult::Success();
}

FAnimMontageProjectorSliceResult StageSlotAnimTracks(const TSharedRef<FJsonObject>& Body, FStagedMontageApply& OutStagedApply)
{
	const TArray<TSharedPtr<FJsonValue>>* SlotValues = nullptr;
	if (!Body->TryGetArrayField(TEXT("SlotAnimTracks"), SlotValues))
	{
		return FAnimMontageProjectorSliceResult::Success();
	}

	OutStagedApply.bHasSlotAnimTracks = true;
	TArray<FSlotAnimationTrack> NewSlotAnimTracks;
	float CompositeLength = 0.0f;
	FAssetDocumentRefMaterializer Materializer;

	for (int32 SlotIndex = 0; SlotIndex < SlotValues->Num(); ++SlotIndex)
	{
		const FString SlotPath = FString::Printf(TEXT("/Body/SlotAnimTracks/%d"), SlotIndex);
		const TSharedPtr<FJsonObject> SlotObject = (*SlotValues)[SlotIndex]->AsObject();

		FSlotAnimationTrack SlotAnimTrack;
		FString SlotNameString;
		SlotObject->TryGetStringField(TEXT("SlotName"), SlotNameString);
		SlotAnimTrack.SlotName = FName(*SlotNameString);

		const TSharedPtr<FJsonObject>* AnimTrack = nullptr;
		SlotObject->TryGetObjectField(TEXT("AnimTrack"), AnimTrack);
		const TArray<TSharedPtr<FJsonValue>>* AnimSegments = nullptr;
		(*AnimTrack)->TryGetArrayField(TEXT("AnimSegments"), AnimSegments);

		for (int32 SegmentIndex = 0; SegmentIndex < AnimSegments->Num(); ++SegmentIndex)
		{
			const FString SegmentPath = FString::Printf(TEXT("%s/AnimTrack/AnimSegments/%d"), *SlotPath, SegmentIndex);
			const TSharedPtr<FJsonObject> SegmentObject = (*AnimSegments)[SegmentIndex]->AsObject();

			FAnimSegment Segment;
			if (const TSharedPtr<FJsonValue>* AnimReference = SegmentObject->Values.Find(TEXT("AnimReference")))
			{
				UObject* ResolvedObject = nullptr;
				if (AnimReference->IsValid() && (*AnimReference)->Type == EJson::Object)
				{
					const FAssetDocumentUpdateResult RefResult = Materializer.ResolveAssetRef(
						(*AnimReference)->AsObject().ToSharedRef(),
						UAnimSequenceBase::StaticClass(),
						SegmentPath / TEXT("AnimReference"),
						ResolvedObject);
					if (!RefResult.bSuccess)
					{
						return ToSliceFailure(RefResult);
					}
				}
				Segment.SetAnimReference(Cast<UAnimSequenceBase>(ResolvedObject));
			}

			double NumberValue = 0.0;
			if (SegmentObject->TryGetNumberField(TEXT("StartPos"), NumberValue))
			{
				Segment.StartPos = static_cast<float>(NumberValue);
			}
			if (SegmentObject->TryGetNumberField(TEXT("AnimStartTime"), NumberValue))
			{
				Segment.AnimStartTime = static_cast<float>(NumberValue);
			}
			if (SegmentObject->TryGetNumberField(TEXT("AnimEndTime"), NumberValue))
			{
				Segment.AnimEndTime = static_cast<float>(NumberValue);
			}
			if (SegmentObject->TryGetNumberField(TEXT("AnimPlayRate"), NumberValue))
			{
				Segment.AnimPlayRate = static_cast<float>(NumberValue);
			}
			if (SegmentObject->TryGetNumberField(TEXT("LoopingCount"), NumberValue))
			{
				Segment.LoopingCount = static_cast<int32>(FMath::RoundToDouble(NumberValue));
			}
			CompositeLength = FMath::Max(CompositeLength, Segment.GetEndPos());
			SlotAnimTrack.AnimTrack.AnimSegments.Add(Segment);
		}

		NewSlotAnimTracks.Add(SlotAnimTrack);
	}

	OutStagedApply.SlotAnimTracks = MoveTemp(NewSlotAnimTracks);
	OutStagedApply.CompositeLength = CompositeLength;
	return FAnimMontageProjectorSliceResult::Success();
}

FAnimMontageProjectorSliceResult StageCompositeSections(const TSharedRef<FJsonObject>& Body, FStagedMontageApply& OutStagedApply)
{
	const TArray<TSharedPtr<FJsonValue>>* SectionValues = nullptr;
	if (!Body->TryGetArrayField(TEXT("CompositeSections"), SectionValues))
	{
		return FAnimMontageProjectorSliceResult::Success();
	}

	OutStagedApply.bHasCompositeSections = true;
	TArray<FCompositeSection> NewSections;
	for (const TSharedPtr<FJsonValue>& SectionValue : *SectionValues)
	{
		const TSharedPtr<FJsonObject> SectionObject = SectionValue->AsObject();
		FCompositeSection Section;

		FString SectionNameString;
		SectionObject->TryGetStringField(TEXT("SectionName"), SectionNameString);
		Section.SectionName = FName(*SectionNameString);

		double NumberValue = 0.0;
		if (SectionObject->TryGetNumberField(TEXT("LinkableTime"), NumberValue))
		{
			Section.SetTime(static_cast<float>(NumberValue));
		}

		FString NextSectionNameString;
		if (SectionObject->TryGetStringField(TEXT("NextSectionName"), NextSectionNameString) && !NextSectionNameString.TrimStartAndEnd().IsEmpty())
		{
			Section.NextSectionName = FName(*NextSectionNameString);
		}

		NewSections.Add(Section);
	}

	OutStagedApply.CompositeSections = MoveTemp(NewSections);
	return FAnimMontageProjectorSliceResult::Success();
}

FAnimMontageProjectorSliceResult StageBlend(const TSharedRef<FJsonObject>& Body, FStagedMontageApply& OutStagedApply)
{
	const TSharedPtr<FJsonObject>* Blend = nullptr;
	if (!Body->TryGetObjectField(TEXT("Blend"), Blend) || !Blend || !Blend->IsValid())
	{
		return FAnimMontageProjectorSliceResult::Success();
	}

	OutStagedApply.Blend.bPresent = true;
	double NumberValue = 0.0;
	if ((*Blend)->TryGetNumberField(TEXT("BlendInTime"), NumberValue))
	{
		OutStagedApply.Blend.BlendInTime = static_cast<float>(NumberValue);
		OutStagedApply.Blend.bHasBlendInTime = true;
	}
	if ((*Blend)->TryGetNumberField(TEXT("BlendOutTime"), NumberValue))
	{
		OutStagedApply.Blend.BlendOutTime = static_cast<float>(NumberValue);
		OutStagedApply.Blend.bHasBlendOutTime = true;
	}
	return FAnimMontageProjectorSliceResult::Success();
}

void AppendQuotedJsonString(const FString& String, FString& Out)
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
			Out.AppendChar(Character);
			break;
		}
	}
	Out += TEXT("\"");
}

FString MontageProjectorJsonValueToComparableString(const TSharedPtr<FJsonValue>& Value)
{
	TFunction<void(TSharedPtr<FJsonValue>, FString&)> AppendValue;
	AppendValue = [&AppendValue](TSharedPtr<FJsonValue> JsonValue, FString& Out)
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
			Out += TEXT("[");
			for (int32 Index = 0; Index < JsonValue->AsArray().Num(); ++Index)
			{
				if (Index > 0)
				{
					Out += TEXT(",");
				}
				AppendValue(JsonValue->AsArray()[Index], Out);
			}
			Out += TEXT("]");
			break;
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
					AppendValue(FieldValue ? *FieldValue : MakeShared<FJsonValueNull>(), Out);
				}
				Out += TEXT("}");
				break;
			}
		default:
			Out += TEXT("null");
			break;
		}
	};

	FString Out;
	AppendValue(Value.IsValid() ? Value : MakeShared<FJsonValueNull>(), Out);
	return Out;
}
}

FAnimMontageProjectorSliceResult FAnimMontageProjectorSliceResult::Success()
{
	FAnimMontageProjectorSliceResult Result;
	Result.bSuccess = true;
	return Result;
}

FAnimMontageProjectorSliceResult FAnimMontageProjectorSliceResult::Failure(const FString& InMessage)
{
	FAnimMontageProjectorSliceResult Result;
	Result.bSuccess = false;
	Result.Message = InMessage;
	return Result;
}

FAnimMontageProjectorSliceResult FAnimMontageProjectorSlice::ExtractBody(const UAnimMontage& Montage, TSharedRef<FJsonObject> ProjectedBody) const
{
	FAssetDocumentProjectionMetrics Metrics;
	FAssetDocumentCapabilityContext Context;
	Context.Asset = const_cast<UAnimMontage*>(&Montage);
	Context.AssetClass = UAnimMontage::StaticClass();

	FAnimMontageAssetDocumentCapability ProductionCapability;
	const FAssetDocumentCapabilityResult ExtractResult = ProductionCapability.Extract(Context, ProjectedBody);
	if (!ExtractResult.bSuccess)
	{
		return FAnimMontageProjectorSliceResult::Failure(ExtractResult.Message);
	}

	Metrics.AssetSpecificFields = ProjectedBody->Values.Num();

	ProjectedBody->SetObjectField(TEXT("_ProjectionMetrics"), Metrics.ToJson());

	return FAnimMontageProjectorSliceResult::Success();
}

FAnimMontageProjectorSliceResult FAnimMontageProjectorSlice::ValidateBody(const UAnimMontage& Montage, const TSharedRef<FJsonObject>& ProjectedBody) const
{
	(void)Montage;

	const TArray<FString> ArrayFields = {
		TEXT("SlotAnimTracks"),
		TEXT("CompositeSections"),
		TEXT("Notifies"),
		TEXT("NotifyStates"),
	};
	for (const FString& FieldName : ArrayFields)
	{
		const FAnimMontageProjectorSliceResult Result = RequireArrayField(ProjectedBody, FieldName);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	for (const FString& FieldName : {FString(TEXT("Skeleton")), FString(TEXT("PreviewMesh"))})
	{
		const FAnimMontageProjectorSliceResult Result = RequireNullOrObjectField(ProjectedBody, FieldName);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	FAnimMontageProjectorSliceResult Result = RequireObjectField(ProjectedBody, TEXT("Blend"));
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ValidateEmptyNotifyArrayOnly(ProjectedBody, TEXT("Notifies"));
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ValidateEmptyNotifyArrayOnly(ProjectedBody, TEXT("NotifyStates"));
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ValidateSlotAnimTracks(ProjectedBody);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ValidateCompositeSections(ProjectedBody);
	if (!Result.bSuccess)
	{
		return Result;
	}

	return ValidateBlend(ProjectedBody);
}

FAnimMontageProjectorSliceResult FAnimMontageProjectorSlice::ApplyBody(UAnimMontage& Montage, const TSharedRef<FJsonObject>& ProjectedBody) const
{
	FAnimMontageProjectorSliceResult Result = ValidateBody(Montage, ProjectedBody);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = FAnimMontageProjectorSliceResult::Success();

	FStagedMontageApply StagedApply;
	FAnimMontageProjectorSliceResult ApplyResult = StageObjectReference(
		ProjectedBody,
		TEXT("Skeleton"),
		USkeleton::StaticClass(),
		StagedApply.Skeleton);
	if (!ApplyResult.bSuccess)
	{
		return ApplyResult;
	}

	ApplyResult = StageObjectReference(
		ProjectedBody,
		TEXT("PreviewMesh"),
		USkeletalMesh::StaticClass(),
		StagedApply.PreviewMesh);
	if (!ApplyResult.bSuccess)
	{
		return ApplyResult;
	}

	const TArray<TFunction<FAnimMontageProjectorSliceResult()>> Operations = {
		[&]() { return StageSlotAnimTracks(ProjectedBody, StagedApply); },
		[&]() { return StageCompositeSections(ProjectedBody, StagedApply); },
		[&]()
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			StagedApply.bHasNotifies = ProjectedBody->TryGetArrayField(TEXT("Notifies"), Values);
			return FAnimMontageProjectorSliceResult::Success();
		},
		[&]()
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			StagedApply.bHasNotifyStates = ProjectedBody->TryGetArrayField(TEXT("NotifyStates"), Values);
			return FAnimMontageProjectorSliceResult::Success();
		},
		[&]() { return StageBlend(ProjectedBody, StagedApply); },
	};

	for (const TFunction<FAnimMontageProjectorSliceResult()>& Operation : Operations)
	{
		ApplyResult = Operation();
		if (!ApplyResult.bSuccess)
		{
			return ApplyResult;
		}
	}

	if (StagedApply.Skeleton.bPresent)
	{
		Montage.SetSkeleton(Cast<USkeleton>(StagedApply.Skeleton.Object));
		Result.ChangedPaths.AddUnique(TEXT("/Body/Skeleton"));
	}
	if (StagedApply.PreviewMesh.bPresent)
	{
		Montage.SetPreviewMesh(Cast<USkeletalMesh>(StagedApply.PreviewMesh.Object), false);
		Result.ChangedPaths.AddUnique(TEXT("/Body/PreviewMesh"));
	}
	if (StagedApply.bHasSlotAnimTracks)
	{
		Montage.SlotAnimTracks = MoveTemp(StagedApply.SlotAnimTracks);
		Montage.SetCompositeLength(StagedApply.CompositeLength);
		Result.ChangedPaths.AddUnique(TEXT("/Body/SlotAnimTracks"));
	}
	if (StagedApply.bHasCompositeSections)
	{
		Montage.CompositeSections = MoveTemp(StagedApply.CompositeSections);
		Result.ChangedPaths.AddUnique(TEXT("/Body/CompositeSections"));
	}
	if (StagedApply.bHasNotifies)
	{
		Result.ChangedPaths.AddUnique(TEXT("/Body/Notifies"));
	}
	if (StagedApply.bHasNotifyStates)
	{
		Result.ChangedPaths.AddUnique(TEXT("/Body/NotifyStates"));
	}
	if (StagedApply.Blend.bPresent)
	{
		bool bChangedBlend = false;
		if (StagedApply.Blend.bHasBlendInTime)
		{
			Montage.BlendIn.SetBlendTime(StagedApply.Blend.BlendInTime);
			bChangedBlend = true;
		}
		if (StagedApply.Blend.bHasBlendOutTime)
		{
			Montage.BlendOut.SetBlendTime(StagedApply.Blend.BlendOutTime);
			bChangedBlend = true;
		}
		if (bChangedBlend)
		{
			Result.ChangedPaths.AddUnique(TEXT("/Body/Blend"));
		}
	}

	if (Result.ChangedPaths.Num() > 0)
	{
		Montage.MarkPackageDirty();
	}
	return Result;
}

FAnimMontageProjectorSliceResult FAnimMontageProjectorSlice::DiffBody(const UAnimMontage& Montage, const TSharedRef<FJsonObject>& ProjectedBody, TArray<FString>& OutChangedPaths) const
{
	OutChangedPaths.Reset();

	FAnimMontageProjectorSliceResult Result = ValidateBody(Montage, ProjectedBody);
	if (!Result.bSuccess)
	{
		return Result;
	}

	UAnimMontage* PreviewMontage = DuplicateObject<UAnimMontage>(const_cast<UAnimMontage*>(&Montage), GetTransientPackage());
	if (!PreviewMontage)
	{
		return FAnimMontageProjectorSliceResult::Failure(TEXT("Failed to duplicate AnimMontage for Body diff."));
	}

	Result = ApplyBody(*PreviewMontage, ProjectedBody);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TSharedRef<FJsonObject> CurrentBody = MakeShared<FJsonObject>();
	Result = ExtractBody(Montage, CurrentBody);
	if (!Result.bSuccess)
	{
		return Result;
	}
	CurrentBody->RemoveField(TEXT("_ProjectionMetrics"));

	TSharedRef<FJsonObject> PreviewBody = MakeShared<FJsonObject>();
	Result = ExtractBody(*PreviewMontage, PreviewBody);
	if (!Result.bSuccess)
	{
		return Result;
	}
	PreviewBody->RemoveField(TEXT("_ProjectionMetrics"));

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ProjectedBody->Values)
	{
		if (Pair.Key == TEXT("_ProjectionMetrics") || Pair.Key == TEXT("_Skipped"))
		{
			continue;
		}

		const TSharedPtr<FJsonValue>* CurrentValue = CurrentBody->Values.Find(Pair.Key);
		const TSharedPtr<FJsonValue>* PreviewValue = PreviewBody->Values.Find(Pair.Key);
		if (MontageProjectorJsonValueToComparableString(CurrentValue ? *CurrentValue : MakeShared<FJsonValueNull>())
			!= MontageProjectorJsonValueToComparableString(PreviewValue ? *PreviewValue : MakeShared<FJsonValueNull>()))
		{
			OutChangedPaths.AddUnique(MakeBodyFieldPath(Pair.Key));
		}
	}

	Result = FAnimMontageProjectorSliceResult::Success();
	Result.ChangedPaths = OutChangedPaths;
	return Result;
}
