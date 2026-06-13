#if WITH_DEV_AUTOMATION_TESTS

#include "Projectors/AnimMontageProjectorSlice.h"

#include "Animation/AnimMontage.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Profiles/AnimMontageAssetDocumentCapability.h"

namespace
{
UAnimMontage* NewTransientMontageForProjectorSlice()
{
	UAnimMontage* Montage = NewObject<UAnimMontage>(
		GetTransientPackage(),
		UAnimMontage::StaticClass(),
		NAME_None);
	if (!Montage)
	{
		return nullptr;
	}

	Montage->BlendIn.SetBlendTime(0.15f);
	Montage->BlendOut.SetBlendTime(0.25f);
	Montage->SlotAnimTracks.Reset();
	Montage->CompositeSections.Reset();

	FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks.AddDefaulted_GetRef();
	SlotTrack.SlotName = FName(TEXT("DefaultSlot"));

	FAnimSegment& Segment = SlotTrack.AnimTrack.AnimSegments.AddDefaulted_GetRef();
	Segment.StartPos = 0.0f;
	Segment.AnimStartTime = 0.0f;
	Segment.AnimEndTime = 1.0f;
	Segment.AnimPlayRate = 1.0f;
	Segment.LoopingCount = 1;

	FCompositeSection& Section = Montage->CompositeSections.AddDefaulted_GetRef();
	Section.SectionName = FName(TEXT("Start"));
	Section.SetTime(0.0f);
	Section.NextSectionName = NAME_None;

	return Montage;
}

TSharedPtr<FJsonValue> CloneJsonValueForSlice(const TSharedPtr<FJsonValue>& Value);

TSharedRef<FJsonObject> CloneJsonObjectForSlice(const TSharedRef<FJsonObject>& Object)
{
	TSharedRef<FJsonObject> Clone = MakeShared<FJsonObject>();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		Clone->SetField(Pair.Key, CloneJsonValueForSlice(Pair.Value));
	}
	return Clone;
}

TSharedPtr<FJsonValue> CloneJsonValueForSlice(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return MakeShared<FJsonValueNull>();
	}

	switch (Value->Type)
	{
	case EJson::Object:
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		if (Object.IsValid())
		{
			return MakeShared<FJsonValueObject>(CloneJsonObjectForSlice(Object.ToSharedRef()));
		}
		return MakeShared<FJsonValueNull>();
	}
	case EJson::Array:
	{
		TArray<TSharedPtr<FJsonValue>> ClonedArray;
		for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
		{
			ClonedArray.Add(CloneJsonValueForSlice(Item));
		}
		return MakeShared<FJsonValueArray>(ClonedArray);
	}
	case EJson::String:
		return MakeShared<FJsonValueString>(Value->AsString());
	case EJson::Number:
		return MakeShared<FJsonValueNumber>(Value->AsNumber());
	case EJson::Boolean:
		return MakeShared<FJsonValueBoolean>(Value->AsBool());
	case EJson::Null:
	default:
		return MakeShared<FJsonValueNull>();
	}
}

void NormalizeMissingOrNullFieldToNull(const TSharedRef<FJsonObject>& Object, const FString& FieldName)
{
	const TSharedPtr<FJsonValue>* ExistingValue = Object->Values.Find(FieldName);
	if (!ExistingValue || !ExistingValue->IsValid() || (*ExistingValue)->Type == EJson::Null)
	{
		Object->SetField(FieldName, MakeShared<FJsonValueNull>());
	}
}

void NormalizeNullableReferencesForSlice(const TSharedRef<FJsonObject>& Body)
{
	NormalizeMissingOrNullFieldToNull(Body, TEXT("Skeleton"));
	NormalizeMissingOrNullFieldToNull(Body, TEXT("PreviewMesh"));

	const TArray<TSharedPtr<FJsonValue>>* SlotAnimTracks = nullptr;
	if (!Body->TryGetArrayField(TEXT("SlotAnimTracks"), SlotAnimTracks))
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& SlotValue : *SlotAnimTracks)
	{
		if (!SlotValue.IsValid() || SlotValue->Type != EJson::Object)
		{
			continue;
		}

		const TSharedPtr<FJsonObject> SlotObject = SlotValue->AsObject();
		const TSharedPtr<FJsonObject>* AnimTrackObject = nullptr;
		if (!SlotObject.IsValid() || !SlotObject->TryGetObjectField(TEXT("AnimTrack"), AnimTrackObject) || !AnimTrackObject || !AnimTrackObject->IsValid())
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* AnimSegments = nullptr;
		if (!(*AnimTrackObject)->TryGetArrayField(TEXT("AnimSegments"), AnimSegments))
		{
			continue;
		}

		for (const TSharedPtr<FJsonValue>& SegmentValue : *AnimSegments)
		{
			if (!SegmentValue.IsValid() || SegmentValue->Type != EJson::Object)
			{
				continue;
			}

			const TSharedPtr<FJsonObject> SegmentObject = SegmentValue->AsObject();
			if (SegmentObject.IsValid())
			{
				NormalizeMissingOrNullFieldToNull(SegmentObject.ToSharedRef(), TEXT("AnimReference"));
			}
		}
	}
}

TSharedRef<FJsonObject> CloneNormalizedBodyForSliceComparison(const TSharedRef<FJsonObject>& Body)
{
	TSharedRef<FJsonObject> Clone = CloneJsonObjectForSlice(Body);
	Clone->RemoveField(TEXT("_ProjectionMetrics"));
	Clone->RemoveField(TEXT("_Skipped"));
	NormalizeNullableReferencesForSlice(Clone);
	return Clone;
}

TSharedRef<FJsonObject> MakeBlendBodyForSlice(double BlendInTime, double BlendOutTime)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Blend = MakeShared<FJsonObject>();
	Blend->SetNumberField(TEXT("BlendInTime"), BlendInTime);
	Blend->SetNumberField(TEXT("BlendOutTime"), BlendOutTime);
	Body->SetObjectField(TEXT("Blend"), Blend);
	return Body;
}

TSharedRef<FJsonObject> MakeSectionsBodyForSlice()
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();

	TSharedRef<FJsonObject> StartSection = MakeShared<FJsonObject>();
	StartSection->SetStringField(TEXT("SectionName"), TEXT("Start"));
	StartSection->SetNumberField(TEXT("LinkableTime"), 0.0);
	StartSection->SetStringField(TEXT("NextSectionName"), TEXT("End"));

	TSharedRef<FJsonObject> EndSection = MakeShared<FJsonObject>();
	EndSection->SetStringField(TEXT("SectionName"), TEXT("End"));
	EndSection->SetNumberField(TEXT("LinkableTime"), 1.25);

	TArray<TSharedPtr<FJsonValue>> Sections;
	Sections.Add(MakeShared<FJsonValueObject>(StartSection));
	Sections.Add(MakeShared<FJsonValueObject>(EndSection));
	Body->SetArrayField(TEXT("CompositeSections"), Sections);
	return Body;
}

TSharedRef<FJsonObject> MakeSlotTracksBodyForSlice(const FString& SlotName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();

	TSharedRef<FJsonObject> Segment = MakeShared<FJsonObject>();
	Segment->SetField(TEXT("AnimReference"), MakeShared<FJsonValueNull>());
	Segment->SetNumberField(TEXT("StartPos"), 0.5);
	Segment->SetNumberField(TEXT("AnimStartTime"), 0.0);
	Segment->SetNumberField(TEXT("AnimEndTime"), 2.0);
	Segment->SetNumberField(TEXT("AnimPlayRate"), 1.5);
	Segment->SetNumberField(TEXT("LoopingCount"), 2);

	TArray<TSharedPtr<FJsonValue>> AnimSegments;
	AnimSegments.Add(MakeShared<FJsonValueObject>(Segment));

	TSharedRef<FJsonObject> AnimTrack = MakeShared<FJsonObject>();
	AnimTrack->SetArrayField(TEXT("AnimSegments"), AnimSegments);

	TSharedRef<FJsonObject> SlotTrack = MakeShared<FJsonObject>();
	SlotTrack->SetStringField(TEXT("SlotName"), SlotName);
	SlotTrack->SetObjectField(TEXT("AnimTrack"), AnimTrack);

	TArray<TSharedPtr<FJsonValue>> SlotTracks;
	SlotTracks.Add(MakeShared<FJsonValueObject>(SlotTrack));
	Body->SetArrayField(TEXT("SlotAnimTracks"), SlotTracks);
	return Body;
}

TSharedRef<FJsonObject> MakeFullUpdateBodyForSlice()
{
	TSharedRef<FJsonObject> Body = MakeSectionsBodyForSlice();
	const TSharedRef<FJsonObject> SlotBody = MakeSlotTracksBodyForSlice(TEXT("UpperBody"));
	Body->SetArrayField(TEXT("SlotAnimTracks"), SlotBody->GetArrayField(TEXT("SlotAnimTracks")));
	const TSharedRef<FJsonObject> BlendBody = MakeBlendBodyForSlice(0.35, 0.45);
	Body->SetObjectField(TEXT("Blend"), BlendBody->GetObjectField(TEXT("Blend")));
	Body->SetArrayField(TEXT("Notifies"), TArray<TSharedPtr<FJsonValue>>());
	Body->SetArrayField(TEXT("NotifyStates"), TArray<TSharedPtr<FJsonValue>>());
	return Body;
}

TSharedRef<FJsonObject> MakeProductionCompatibleUpdateBodyForSlice()
{
	TSharedRef<FJsonObject> Body = MakeSectionsBodyForSlice();
	const TSharedRef<FJsonObject> BlendBody = MakeBlendBodyForSlice(0.35, 0.45);
	Body->SetObjectField(TEXT("Blend"), BlendBody->GetObjectField(TEXT("Blend")));
	Body->SetArrayField(TEXT("Notifies"), TArray<TSharedPtr<FJsonValue>>());
	Body->SetArrayField(TEXT("NotifyStates"), TArray<TSharedPtr<FJsonValue>>());
	return Body;
}

bool ContainsPath(const TArray<FString>& Paths, const FString& Path)
{
	return Paths.Contains(Path);
}

bool JsonValuesEqualForSlice(
	const TSharedPtr<FJsonValue>& Expected,
	const TSharedPtr<FJsonValue>& Actual,
	const FString& Path,
	FString& OutDifference);

bool JsonObjectsEqualForSlice(
	const TSharedRef<FJsonObject>& Expected,
	const TSharedRef<FJsonObject>& Actual,
	const FString& Path,
	FString& OutDifference)
{
	if (Expected->Values.Num() != Actual->Values.Num())
	{
		OutDifference = FString::Printf(
			TEXT("%s object field count differs: expected %d, actual %d"),
			*Path,
			Expected->Values.Num(),
			Actual->Values.Num());
		return false;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Expected->Values)
	{
		const TSharedPtr<FJsonValue>* ActualValue = Actual->Values.Find(Pair.Key);
		if (!ActualValue)
		{
			OutDifference = FString::Printf(TEXT("%s missing field '%s'"), *Path, *Pair.Key);
			return false;
		}

		const FString ChildPath = Path / Pair.Key;
		if (!JsonValuesEqualForSlice(Pair.Value, *ActualValue, ChildPath, OutDifference))
		{
			return false;
		}
	}

	return true;
}

bool JsonArraysEqualForSlice(
	const TArray<TSharedPtr<FJsonValue>>& Expected,
	const TArray<TSharedPtr<FJsonValue>>& Actual,
	const FString& Path,
	FString& OutDifference)
{
	if (Expected.Num() != Actual.Num())
	{
		OutDifference = FString::Printf(
			TEXT("%s array length differs: expected %d, actual %d"),
			*Path,
			Expected.Num(),
			Actual.Num());
		return false;
	}

	for (int32 Index = 0; Index < Expected.Num(); ++Index)
	{
		if (!JsonValuesEqualForSlice(Expected[Index], Actual[Index], FString::Printf(TEXT("%s/%d"), *Path, Index), OutDifference))
		{
			return false;
		}
	}

	return true;
}

bool JsonValuesEqualForSlice(
	const TSharedPtr<FJsonValue>& Expected,
	const TSharedPtr<FJsonValue>& Actual,
	const FString& Path,
	FString& OutDifference)
{
	if (!Expected.IsValid() || !Actual.IsValid())
	{
		if (Expected.IsValid() == Actual.IsValid())
		{
			return true;
		}

		OutDifference = FString::Printf(TEXT("%s validity differs"), *Path);
		return false;
	}

	if (Expected->Type != Actual->Type)
	{
		OutDifference = FString::Printf(
			TEXT("%s type differs: expected %d, actual %d"),
			*Path,
			static_cast<int32>(Expected->Type),
			static_cast<int32>(Actual->Type));
		return false;
	}

	switch (Expected->Type)
	{
	case EJson::Object:
	{
		const TSharedPtr<FJsonObject> ExpectedObject = Expected->AsObject();
		const TSharedPtr<FJsonObject> ActualObject = Actual->AsObject();
		if (!ExpectedObject.IsValid() || !ActualObject.IsValid())
		{
			const bool bBothInvalid = !ExpectedObject.IsValid() && !ActualObject.IsValid();
			if (!bBothInvalid)
			{
				OutDifference = FString::Printf(TEXT("%s object validity differs"), *Path);
			}
			return bBothInvalid;
		}
		return JsonObjectsEqualForSlice(ExpectedObject.ToSharedRef(), ActualObject.ToSharedRef(), Path, OutDifference);
	}
	case EJson::Array:
		return JsonArraysEqualForSlice(Expected->AsArray(), Actual->AsArray(), Path, OutDifference);
	case EJson::String:
		if (Expected->AsString() != Actual->AsString())
		{
			OutDifference = FString::Printf(
				TEXT("%s string differs: expected '%s', actual '%s'"),
				*Path,
				*Expected->AsString(),
				*Actual->AsString());
			return false;
		}
		return true;
	case EJson::Number:
		if (!FMath::IsNearlyEqual(Expected->AsNumber(), Actual->AsNumber(), KINDA_SMALL_NUMBER))
		{
			OutDifference = FString::Printf(
				TEXT("%s number differs: expected %.17g, actual %.17g"),
				*Path,
				Expected->AsNumber(),
				Actual->AsNumber());
			return false;
		}
		return true;
	case EJson::Boolean:
		if (Expected->AsBool() != Actual->AsBool())
		{
			OutDifference = FString::Printf(
				TEXT("%s bool differs: expected %s, actual %s"),
				*Path,
				Expected->AsBool() ? TEXT("true") : TEXT("false"),
				Actual->AsBool() ? TEXT("true") : TEXT("false"));
			return false;
		}
		return true;
	case EJson::Null:
	default:
		return true;
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageProjectorSliceExtractsCurrentBodyShapeTest,
	"AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.ExtractsCurrentBodyShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageProjectorSliceExtractsCurrentBodyShapeTest::RunTest(const FString& Parameters)
{
	UAnimMontage* Montage = NewTransientMontageForProjectorSlice();
	TestNotNull(TEXT("Montage fixture is created"), Montage);
	if (!Montage)
	{
		return false;
	}

	TSharedRef<FJsonObject> ProjectedBody = MakeShared<FJsonObject>();
	const FAnimMontageProjectorSlice Projector;
	const FAnimMontageProjectorSliceResult Result = Projector.ExtractBody(*Montage, ProjectedBody);

	if (!TestTrue(TEXT("Projector extraction succeeds"), Result.bSuccess))
	{
		return false;
	}
	TestTrue(TEXT("ProjectedBody has Skeleton"), ProjectedBody->HasField(TEXT("Skeleton")));
	TestTrue(TEXT("ProjectedBody has PreviewMesh"), ProjectedBody->HasField(TEXT("PreviewMesh")));

	const TSharedPtr<FJsonValue>* SkeletonValue = ProjectedBody->Values.Find(TEXT("Skeleton"));
	TestTrue(TEXT("Skeleton field exists"), SkeletonValue && SkeletonValue->IsValid());
	if (SkeletonValue && SkeletonValue->IsValid())
	{
		TestTrue(TEXT("Transient fixture emits null Skeleton"), (*SkeletonValue)->Type == EJson::Null);
	}

	const TSharedPtr<FJsonValue>* PreviewMeshValue = ProjectedBody->Values.Find(TEXT("PreviewMesh"));
	TestTrue(TEXT("PreviewMesh field exists"), PreviewMeshValue && PreviewMeshValue->IsValid());
	if (PreviewMeshValue && PreviewMeshValue->IsValid())
	{
		TestTrue(TEXT("Transient fixture emits null PreviewMesh"), (*PreviewMeshValue)->Type == EJson::Null);
	}

	TestTrue(TEXT("ProjectedBody has SlotAnimTracks"), ProjectedBody->HasField(TEXT("SlotAnimTracks")));
	TestTrue(TEXT("ProjectedBody has CompositeSections"), ProjectedBody->HasField(TEXT("CompositeSections")));
	TestTrue(TEXT("ProjectedBody has Notifies"), ProjectedBody->HasField(TEXT("Notifies")));
	TestTrue(TEXT("ProjectedBody has NotifyStates"), ProjectedBody->HasField(TEXT("NotifyStates")));
	TestTrue(TEXT("ProjectedBody has Blend"), ProjectedBody->HasField(TEXT("Blend")));

	const TArray<TSharedPtr<FJsonValue>>* SlotAnimTracks = nullptr;
	TestTrue(TEXT("SlotAnimTracks is an array"), ProjectedBody->TryGetArrayField(TEXT("SlotAnimTracks"), SlotAnimTracks));
	TestEqual(TEXT("One slot track is emitted"), SlotAnimTracks ? SlotAnimTracks->Num() : 0, 1);
	if (SlotAnimTracks && SlotAnimTracks->Num() == 1)
	{
		const TSharedPtr<FJsonObject> SlotAnimTrack = (*SlotAnimTracks)[0]->AsObject();
		TestTrue(TEXT("Slot track object exists"), SlotAnimTrack.IsValid());
		if (SlotAnimTrack.IsValid())
		{
			TestEqual(TEXT("SlotName is projected"), SlotAnimTrack->GetStringField(TEXT("SlotName")), FString(TEXT("DefaultSlot")));

			const TSharedPtr<FJsonObject>* AnimTrack = nullptr;
			TestTrue(TEXT("AnimTrack object exists"), SlotAnimTrack->TryGetObjectField(TEXT("AnimTrack"), AnimTrack));
			if (AnimTrack && AnimTrack->IsValid())
			{
				const TArray<TSharedPtr<FJsonValue>>* AnimSegments = nullptr;
				TestTrue(TEXT("AnimSegments is an array"), (*AnimTrack)->TryGetArrayField(TEXT("AnimSegments"), AnimSegments));
				TestEqual(TEXT("One anim segment is emitted"), AnimSegments ? AnimSegments->Num() : 0, 1);
				if (AnimSegments && AnimSegments->Num() == 1)
				{
					const TSharedPtr<FJsonObject> SegmentObject = (*AnimSegments)[0]->AsObject();
					TestTrue(TEXT("Anim segment object exists"), SegmentObject.IsValid());
					if (SegmentObject.IsValid())
					{
						TestEqual(TEXT("Segment StartPos is projected"), SegmentObject->GetNumberField(TEXT("StartPos")), 0.0);
						TestEqual(TEXT("Segment AnimStartTime is projected"), SegmentObject->GetNumberField(TEXT("AnimStartTime")), 0.0);
						TestEqual(TEXT("Segment AnimEndTime is projected"), SegmentObject->GetNumberField(TEXT("AnimEndTime")), 1.0);
						TestEqual(TEXT("Segment AnimPlayRate is projected"), SegmentObject->GetNumberField(TEXT("AnimPlayRate")), 1.0);
						TestEqual(TEXT("Segment LoopingCount is projected"), SegmentObject->GetNumberField(TEXT("LoopingCount")), 1.0);

						const TSharedPtr<FJsonValue>* AnimReferenceValue = SegmentObject->Values.Find(TEXT("AnimReference"));
						TestTrue(TEXT("AnimReference field exists"), AnimReferenceValue && AnimReferenceValue->IsValid());
						if (AnimReferenceValue && AnimReferenceValue->IsValid())
						{
							TestTrue(TEXT("Transient fixture emits null AnimReference"), (*AnimReferenceValue)->Type == EJson::Null);
						}
					}
				}
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* CompositeSections = nullptr;
	TestTrue(TEXT("CompositeSections is an array"), ProjectedBody->TryGetArrayField(TEXT("CompositeSections"), CompositeSections));
	TestEqual(TEXT("One composite section is emitted"), CompositeSections ? CompositeSections->Num() : 0, 1);
	if (CompositeSections && CompositeSections->Num() == 1)
	{
		const TSharedPtr<FJsonObject> SectionObject = (*CompositeSections)[0]->AsObject();
		TestTrue(TEXT("Composite section object exists"), SectionObject.IsValid());
		if (SectionObject.IsValid())
		{
			TestEqual(TEXT("SectionName is projected"), SectionObject->GetStringField(TEXT("SectionName")), FString(TEXT("Start")));
			TestEqual(TEXT("LinkableTime is projected"), SectionObject->GetNumberField(TEXT("LinkableTime")), 0.0);
			TestFalse(TEXT("NAME_None next section is omitted"), SectionObject->HasField(TEXT("NextSectionName")));
			TestFalse(TEXT("StartTime is not emitted by projector slice"), SectionObject->HasField(TEXT("StartTime")));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Notifies = nullptr;
	TestTrue(TEXT("Notifies is an array"), ProjectedBody->TryGetArrayField(TEXT("Notifies"), Notifies));
	TestEqual(TEXT("No managed notifies are emitted"), Notifies ? Notifies->Num() : -1, 0);

	const TArray<TSharedPtr<FJsonValue>>* NotifyStates = nullptr;
	TestTrue(TEXT("NotifyStates is an array"), ProjectedBody->TryGetArrayField(TEXT("NotifyStates"), NotifyStates));
	TestEqual(TEXT("No managed notify states are emitted"), NotifyStates ? NotifyStates->Num() : -1, 0);

	const TSharedPtr<FJsonObject>* Blend = nullptr;
	TestTrue(TEXT("Blend object exists"), ProjectedBody->TryGetObjectField(TEXT("Blend"), Blend));
	if (Blend && Blend->IsValid())
	{
		TestTrue(TEXT("BlendInTime is projected"), FMath::IsNearlyEqual((*Blend)->GetNumberField(TEXT("BlendInTime")), 0.15, KINDA_SMALL_NUMBER));
		TestTrue(TEXT("BlendOutTime is projected"), FMath::IsNearlyEqual((*Blend)->GetNumberField(TEXT("BlendOutTime")), 0.25, KINDA_SMALL_NUMBER));
	}

	const TSharedPtr<FJsonObject>* Metrics = nullptr;
	TestTrue(TEXT("Projection metrics exist"), ProjectedBody->TryGetObjectField(TEXT("_ProjectionMetrics"), Metrics));
	if (Metrics && Metrics->IsValid())
	{
		TestTrue(TEXT("Projection metrics include AssetSpecificFields"), (*Metrics)->HasField(TEXT("AssetSpecificFields")));
		TestTrue(TEXT("Projection metrics include ReusableProjectedFields"), (*Metrics)->HasField(TEXT("ReusableProjectedFields")));
		TestTrue(TEXT("Projection metrics include SkippedFields"), (*Metrics)->HasField(TEXT("SkippedFields")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageProjectorSliceMatchesProductionExtractionTest,
	"AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.MatchesProductionExtraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageProjectorSliceMatchesProductionExtractionTest::RunTest(const FString& Parameters)
{
	UAnimMontage* Montage = NewTransientMontageForProjectorSlice();
	TestNotNull(TEXT("Montage fixture is created"), Montage);
	if (!Montage)
	{
		return false;
	}

	FAssetDocumentCapabilityContext Context;
	Context.Asset = Montage;
	Context.AssetClass = UAnimMontage::StaticClass();

	FAnimMontageAssetDocumentCapability ProductionCapability;
	TSharedRef<FJsonObject> ProductionBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ProductionResult = ProductionCapability.Extract(Context, ProductionBody);
	if (!TestTrue(TEXT("Production extraction succeeds"), ProductionResult.bSuccess))
	{
		TestEqual(TEXT("Production extraction message"), ProductionResult.Message, FString());
		return false;
	}

	FAnimMontageProjectorSlice Projector;
	TSharedRef<FJsonObject> ProjectedBody = MakeShared<FJsonObject>();
	const FAnimMontageProjectorSliceResult ProjectorResult = Projector.ExtractBody(*Montage, ProjectedBody);
	if (!TestTrue(TEXT("Projector extraction succeeds"), ProjectorResult.bSuccess))
	{
		TestEqual(TEXT("Projector extraction message"), ProjectorResult.Message, FString());
		return false;
	}

	const TSharedRef<FJsonObject> NormalizedProduction = CloneNormalizedBodyForSliceComparison(ProductionBody);
	const TSharedRef<FJsonObject> NormalizedProjected = CloneNormalizedBodyForSliceComparison(ProjectedBody);

	FString Difference;
	const bool bBodiesMatch = JsonObjectsEqualForSlice(NormalizedProduction, NormalizedProjected, TEXT("/Body"), Difference);
	TestEqual(TEXT("First JSON mismatch"), Difference, FString());
	TestTrue(TEXT("Projector output matches production body for representative fixture"), bBodiesMatch);

	return bBodiesMatch;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageProjectorSliceRejectsInvalidArrayFieldsTest,
	"AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.Validate.RejectsInvalidArrayFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageProjectorSliceRejectsInvalidArrayFieldsTest::RunTest(const FString& Parameters)
{
	UAnimMontage* Montage = NewTransientMontageForProjectorSlice();
	TestNotNull(TEXT("Montage fixture is created"), Montage);
	if (!Montage)
	{
		return false;
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("SlotAnimTracks"), TEXT("not-array"));

	const FAnimMontageProjectorSlice Projector;
	const FAnimMontageProjectorSliceResult Result = Projector.ValidateBody(*Montage, Body);
	TestFalse(TEXT("Validation rejects non-array SlotAnimTracks"), Result.bSuccess);
	TestTrue(TEXT("Validation reports SlotAnimTracks path"), Result.Message.Contains(TEXT("/Body/SlotAnimTracks")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageProjectorSliceRejectsInvalidBlendTest,
	"AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.Validate.RejectsInvalidBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageProjectorSliceRejectsInvalidBlendTest::RunTest(const FString& Parameters)
{
	UAnimMontage* Montage = NewTransientMontageForProjectorSlice();
	TestNotNull(TEXT("Montage fixture is created"), Montage);
	if (!Montage)
	{
		return false;
	}

	TSharedRef<FJsonObject> Body = MakeBlendBodyForSlice(-0.1, 0.2);
	const FAnimMontageProjectorSlice Projector;
	const FAnimMontageProjectorSliceResult Result = Projector.ValidateBody(*Montage, Body);
	TestFalse(TEXT("Validation rejects negative BlendInTime"), Result.bSuccess);
	TestTrue(TEXT("Validation reports BlendInTime path"), Result.Message.Contains(TEXT("/Body/Blend/BlendInTime")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageProjectorSliceRejectsInvalidSectionsTest,
	"AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.Validate.RejectsInvalidSections",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageProjectorSliceRejectsInvalidSectionsTest::RunTest(const FString& Parameters)
{
	UAnimMontage* Montage = NewTransientMontageForProjectorSlice();
	TestNotNull(TEXT("Montage fixture is created"), Montage);
	if (!Montage)
	{
		return false;
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Section = MakeShared<FJsonObject>();
	Section->SetStringField(TEXT("SectionName"), TEXT("Start"));
	Section->SetStringField(TEXT("NextSectionName"), TEXT("Missing"));
	TArray<TSharedPtr<FJsonValue>> Sections;
	Sections.Add(MakeShared<FJsonValueObject>(Section));
	Body->SetArrayField(TEXT("CompositeSections"), Sections);

	const FAnimMontageProjectorSlice Projector;
	const FAnimMontageProjectorSliceResult Result = Projector.ValidateBody(*Montage, Body);
	TestFalse(TEXT("Validation rejects missing NextSectionName target"), Result.bSuccess);
	TestTrue(TEXT("Validation reports NextSectionName path"), Result.Message.Contains(TEXT("/Body/CompositeSections/0/NextSectionName")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageProjectorSliceUpdatesExistingMontageTest,
	"AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.UpdatesExistingMontage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageProjectorSliceUpdatesExistingMontageTest::RunTest(const FString& Parameters)
{
	UAnimMontage* Montage = NewTransientMontageForProjectorSlice();
	TestNotNull(TEXT("Montage fixture is created"), Montage);
	if (!Montage)
	{
		return false;
	}

	const FAnimMontageProjectorSlice Projector;
	const TSharedRef<FJsonObject> Body = MakeFullUpdateBodyForSlice();
	const FAnimMontageProjectorSliceResult Result = Projector.ApplyBody(*Montage, Body);
	if (!TestTrue(TEXT("Projector apply succeeds"), Result.bSuccess))
	{
		TestTrue(TEXT("Apply result has message"), !Result.Message.IsEmpty());
		return false;
	}

	TestTrue(TEXT("ChangedPaths includes Blend"), ContainsPath(Result.ChangedPaths, TEXT("/Body/Blend")));
	TestTrue(TEXT("ChangedPaths includes CompositeSections"), ContainsPath(Result.ChangedPaths, TEXT("/Body/CompositeSections")));
	TestTrue(TEXT("ChangedPaths includes SlotAnimTracks"), ContainsPath(Result.ChangedPaths, TEXT("/Body/SlotAnimTracks")));

	TestTrue(TEXT("BlendInTime updated"), FMath::IsNearlyEqual(Montage->GetDefaultBlendInTime(), 0.35f, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("BlendOutTime updated"), FMath::IsNearlyEqual(Montage->GetDefaultBlendOutTime(), 0.45f, KINDA_SMALL_NUMBER));
	TestEqual(TEXT("SlotAnimTracks replaced"), Montage->SlotAnimTracks.Num(), 1);
	if (Montage->SlotAnimTracks.Num() == 1)
	{
		TestEqual(TEXT("Slot name updated"), Montage->SlotAnimTracks[0].SlotName, FName(TEXT("UpperBody")));
		TestEqual(TEXT("AnimSegments replaced"), Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num(), 1);
		if (Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() == 1)
		{
			const FAnimSegment& Segment = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
			TestNull(TEXT("Null AnimReference is allowed"), Segment.GetAnimReference().Get());
			TestTrue(TEXT("Segment StartPos updated"), FMath::IsNearlyEqual(Segment.StartPos, 0.5f, KINDA_SMALL_NUMBER));
			TestTrue(TEXT("Segment AnimPlayRate updated"), FMath::IsNearlyEqual(Segment.AnimPlayRate, 1.5f, KINDA_SMALL_NUMBER));
			TestEqual(TEXT("Segment LoopingCount updated"), Segment.LoopingCount, 2);
		}
	}

	TestEqual(TEXT("CompositeSections replaced"), Montage->CompositeSections.Num(), 2);
	if (Montage->CompositeSections.Num() == 2)
	{
		TestEqual(TEXT("First section next updated"), Montage->CompositeSections[0].NextSectionName, FName(TEXT("End")));
		TestTrue(TEXT("Second section time updated"), FMath::IsNearlyEqual(Montage->CompositeSections[1].GetTime(), 1.25f, KINDA_SMALL_NUMBER));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageProjectorSliceMatchesProductionApplyUpdateTest,
	"AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.MatchesProductionApplyUpdate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageProjectorSliceMatchesProductionApplyUpdateTest::RunTest(const FString& Parameters)
{
	UAnimMontage* ProjectorMontage = NewTransientMontageForProjectorSlice();
	UAnimMontage* ProductionMontage = NewTransientMontageForProjectorSlice();
	TestNotNull(TEXT("Projector montage fixture is created"), ProjectorMontage);
	TestNotNull(TEXT("Production montage fixture is created"), ProductionMontage);
	if (!ProjectorMontage || !ProductionMontage)
	{
		return false;
	}

	const TSharedRef<FJsonObject> Body = MakeProductionCompatibleUpdateBodyForSlice();

	FAnimMontageProjectorSlice Projector;
	const FAnimMontageProjectorSliceResult ProjectorResult = Projector.ApplyBody(*ProjectorMontage, Body);
	if (!TestTrue(TEXT("Projector apply succeeds"), ProjectorResult.bSuccess))
	{
		TestEqual(TEXT("Projector apply message"), ProjectorResult.Message, FString());
		return false;
	}

	FAssetDocumentCapabilityContext Context;
	Context.Asset = ProductionMontage;
	Context.AssetClass = UAnimMontage::StaticClass();
	FAnimMontageAssetDocumentCapability ProductionCapability;
	const FAssetDocumentCapabilityResult ProductionResult = ProductionCapability.Apply(
		Context,
		StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueObject>(CloneJsonObjectForSlice(Body))));
	if (!TestTrue(TEXT("Production apply succeeds"), ProductionResult.bSuccess))
	{
		TestEqual(TEXT("Production apply message"), ProductionResult.Message, FString());
		return false;
	}

	TSharedRef<FJsonObject> ProjectorBody = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> ProductionBody = MakeShared<FJsonObject>();
	TestTrue(TEXT("Projector extract after apply succeeds"), Projector.ExtractBody(*ProjectorMontage, ProjectorBody).bSuccess);
	TestTrue(TEXT("Production extract after apply succeeds"), Projector.ExtractBody(*ProductionMontage, ProductionBody).bSuccess);

	const TSharedRef<FJsonObject> NormalizedProjector = CloneNormalizedBodyForSliceComparison(ProjectorBody);
	const TSharedRef<FJsonObject> NormalizedProduction = CloneNormalizedBodyForSliceComparison(ProductionBody);
	FString Difference;
	const bool bBodiesMatch = JsonObjectsEqualForSlice(NormalizedProduction, NormalizedProjector, TEXT("/Body"), Difference);
	TestEqual(TEXT("First JSON mismatch"), Difference, FString());
	TestTrue(TEXT("Projector apply matches production apply"), bBodiesMatch);
	return bBodiesMatch;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageProjectorSliceDiffReportsUpdatePathsTest,
	"AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.DiffReportsUpdatePaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageProjectorSliceDiffReportsUpdatePathsTest::RunTest(const FString& Parameters)
{
	UAnimMontage* Montage = NewTransientMontageForProjectorSlice();
	TestNotNull(TEXT("Montage fixture is created"), Montage);
	if (!Montage)
	{
		return false;
	}

	const FAnimMontageProjectorSlice Projector;
	TArray<FString> ChangedPaths;
	FAnimMontageProjectorSliceResult Result = Projector.DiffBody(*Montage, MakeBlendBodyForSlice(0.15, 0.25), ChangedPaths);
	if (!TestTrue(TEXT("Same desired diff succeeds"), Result.bSuccess))
	{
		TestEqual(TEXT("Same desired diff message"), Result.Message, FString());
		return false;
	}
	TestEqual(TEXT("Same desired returns no changed paths"), ChangedPaths.Num(), 0);

	ChangedPaths.Reset();
	Result = Projector.DiffBody(*Montage, MakeFullUpdateBodyForSlice(), ChangedPaths);
	if (!TestTrue(TEXT("Changed desired diff succeeds"), Result.bSuccess))
	{
		TestEqual(TEXT("Changed desired diff message"), Result.Message, FString());
		return false;
	}
	TestTrue(TEXT("Diff reports Blend"), ContainsPath(ChangedPaths, TEXT("/Body/Blend")));
	TestTrue(TEXT("Diff reports CompositeSections"), ContainsPath(ChangedPaths, TEXT("/Body/CompositeSections")));
	TestTrue(TEXT("Diff reports SlotAnimTracks"), ContainsPath(ChangedPaths, TEXT("/Body/SlotAnimTracks")));
	TestFalse(TEXT("Diff omits absent Skeleton"), ContainsPath(ChangedPaths, TEXT("/Body/Skeleton")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
