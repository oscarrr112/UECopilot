// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/AnimSequenceGenerator.h"

#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/FrameRate.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
const TCHAR* TestAnimPath = TEXT("/Game/Generated/Animation");
const TCHAR* TimingPrevalidationAssetName = TEXT("AS_PrevalidateTimingFields");

TSharedPtr<FJsonObject> MakeAnimSequenceBaseConfig()
{
	TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();
	Config->SetStringField(TEXT("Skeleton"), TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton"));
	Config->SetStringField(TEXT("PreviewMesh"), TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP"));

	TSharedPtr<FJsonObject> FrameRate = MakeShared<FJsonObject>();
	FrameRate->SetNumberField(TEXT("Numerator"), 30);
	FrameRate->SetNumberField(TEXT("Denominator"), 1);
	Config->SetObjectField(TEXT("FrameRate"), FrameRate);
	Config->SetNumberField(TEXT("NumberOfFrames"), 12);

	return Config;
}

TSharedPtr<FJsonObject> MakeInvalidTimingPatchConfig()
{
	TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> FrameRate = MakeShared<FJsonObject>();
	FrameRate->SetNumberField(TEXT("Numerator"), 60);
	FrameRate->SetNumberField(TEXT("Denominator"), 0);
	Config->SetObjectField(TEXT("FrameRate"), FrameRate);
	Config->SetNumberField(TEXT("NumberOfFrames"), 12);

	TArray<TSharedPtr<FJsonValue>> NotifyStates;
	TSharedPtr<FJsonObject> NotifyState = MakeShared<FJsonObject>();
	NotifyState->SetStringField(TEXT("Name"), TEXT("OutOfRange"));
	NotifyState->SetNumberField(TEXT("Time"), 999.0);
	NotifyState->SetNumberField(TEXT("Duration"), 0.1);
	NotifyState->SetNumberField(TEXT("TrackIndex"), 0);
	NotifyStates.Add(MakeShared<FJsonValueObject>(NotifyState));
	Config->SetArrayField(TEXT("NotifyStates"), NotifyStates);

	return Config;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimSequencePrevalidateTimingFieldsTest,
	"AssetFactory.AnimSequence.PrevalidateTimingFieldsBeforeMutation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimSequencePrevalidateTimingFieldsTest::RunTest(const FString& Parameters)
{
	FAnimSequenceGenerator Generator;

	const FGenerationResult BaseResult = Generator.Generate(
		TimingPrevalidationAssetName,
		TestAnimPath,
		EGenerationAction::CreateOrUpdate,
		MakeAnimSequenceBaseConfig());
	TestTrue(TEXT("Base AnimSequence is generated"), BaseResult.IsSuccess());

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(BaseResult.GeneratedAsset);
	if (!AnimSequence)
	{
		AnimSequence = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Generated/Animation/AS_PrevalidateTimingFields.AS_PrevalidateTimingFields"));
	}
	TestNotNull(TEXT("Generated AnimSequence is loadable"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const IAnimationDataModel* DataModel = AnimSequence->GetDataModel();
	TestNotNull(TEXT("Generated AnimSequence has a data model"), DataModel);
	if (!DataModel)
	{
		return false;
	}

	const FFrameRate OriginalFrameRate = DataModel->GetFrameRate();
	const int32 OriginalNumberOfFrames = DataModel->GetNumberOfFrames();
	const int32 OriginalNotifyCount = AnimSequence->Notifies.Num();

	const FGenerationResult InvalidResult = Generator.Generate(
		TimingPrevalidationAssetName,
		TestAnimPath,
		EGenerationAction::Update,
		MakeInvalidTimingPatchConfig());

	TestEqual(TEXT("Invalid timing patch fails on raw FrameRate denominator"), InvalidResult.Status, EGenerationStatus::Failed);
	TestEqual(TEXT("Invalid timing patch reports denominator validation"), InvalidResult.Message, FString(TEXT("'FrameRate.Denominator' must be a positive integer")));

	DataModel = AnimSequence->GetDataModel();
	TestNotNull(TEXT("AnimSequence still has a data model after failed patch"), DataModel);
	if (!DataModel)
	{
		return false;
	}

	TestEqual(TEXT("FrameRate is unchanged after failed patch"), DataModel->GetFrameRate(), OriginalFrameRate);
	TestEqual(TEXT("NumberOfFrames is unchanged after failed patch"), DataModel->GetNumberOfFrames(), OriginalNumberOfFrames);
	TestEqual(TEXT("Notifies are unchanged after failed patch"), AnimSequence->Notifies.Num(), OriginalNotifyCount);

	return true;
}

#endif
