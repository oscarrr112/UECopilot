#if WITH_DEV_AUTOMATION_TESTS

#include "Projectors/AnimMontageProjectorSlice.h"

#include "Animation/AnimMontage.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageProjectorSliceExtractsCurrentBodyShapeTest,
	"AssetFactory.AssetDocument.ProjectorSlice.AnimMontage.ExtractsCurrentBodyShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageProjectorSliceExtractsCurrentBodyShapeTest::RunTest(const FString& Parameters)
{
	UAnimMontage* Montage = NewObject<UAnimMontage>(
		GetTransientPackage(),
		UAnimMontage::StaticClass(),
		FName(TEXT("AssetDocumentProjectorSliceMontage")));
	TestNotNull(TEXT("Montage fixture is created"), Montage);
	if (!Montage)
	{
		return false;
	}

	Montage->BlendIn.SetBlendTime(0.15f);
	Montage->BlendOut.SetBlendTime(0.25f);

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
		TestEqual(TEXT("BlendInTime is projected"), (*Blend)->GetNumberField(TEXT("BlendInTime")), 0.15);
		TestEqual(TEXT("BlendOutTime is projected"), (*Blend)->GetNumberField(TEXT("BlendOutTime")), 0.25);
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

#endif // WITH_DEV_AUTOMATION_TESTS
