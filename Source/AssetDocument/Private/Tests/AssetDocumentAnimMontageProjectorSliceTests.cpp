#if WITH_DEV_AUTOMATION_TESTS

#include "Projectors/AnimMontageProjectorSlice.h"

#include "Animation/AnimMontage.h"
#include "Dom/JsonObject.h"
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
	Section.StartTime = 0.0f;
	Section.NextSectionName = NAME_None;

	TSharedRef<FJsonObject> ProjectedBody = MakeShared<FJsonObject>();
	const FAnimMontageProjectorSlice Projector;
	const FAnimMontageProjectorSliceResult Result = Projector.ExtractBody(*Montage, ProjectedBody);

	if (!TestTrue(TEXT("Projector extraction succeeds"), Result.bSuccess))
	{
		return false;
	}
	TestTrue(TEXT("ProjectedBody has SlotAnimTracks"), ProjectedBody->HasField(TEXT("SlotAnimTracks")));
	TestTrue(TEXT("ProjectedBody has CompositeSections"), ProjectedBody->HasField(TEXT("CompositeSections")));
	TestTrue(TEXT("ProjectedBody has Notifies"), ProjectedBody->HasField(TEXT("Notifies")));
	TestTrue(TEXT("ProjectedBody has NotifyStates"), ProjectedBody->HasField(TEXT("NotifyStates")));
	TestTrue(TEXT("ProjectedBody has Blend"), ProjectedBody->HasField(TEXT("Blend")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
