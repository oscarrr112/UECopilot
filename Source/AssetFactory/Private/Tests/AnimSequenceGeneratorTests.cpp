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
const TCHAR* ReflectedPropertiesAssetName = TEXT("AS_ReflectedProperties");

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

TSharedPtr<FJsonObject> MakeReflectedPropertiesConfig()
{
	TSharedPtr<FJsonObject> Config = MakeAnimSequenceBaseConfig();

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetNumberField(TEXT("RateScale"), 1.25);
	Config->SetObjectField(TEXT("Properties"), Properties);

	return Config;
}

TSharedPtr<FJsonObject> MakeInvalidPropertiesConfig()
{
	TSharedPtr<FJsonObject> Config = MakeAnimSequenceBaseConfig();
	Config->SetField(TEXT("Properties"), MakeShared<FJsonValueNumber>(1.0));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimSequenceReflectedPropertiesExtractTest,
	"AssetFactory.AnimSequence.ReflectedPropertiesAndScalarExtract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimSequenceReflectedPropertiesExtractTest::RunTest(const FString& Parameters)
{
	FAnimSequenceGenerator Generator;

	const FGenerationResult Result = Generator.Generate(
		ReflectedPropertiesAssetName,
		TestAnimPath,
		EGenerationAction::CreateOrUpdate,
		MakeReflectedPropertiesConfig());
	TestTrue(TEXT("AnimSequence with reflected Properties is generated"), Result.IsSuccess());

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(Result.GeneratedAsset);
	if (!AnimSequence)
	{
		AnimSequence = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Generated/Animation/AS_ReflectedProperties.AS_ReflectedProperties"));
	}
	TestNotNull(TEXT("Generated AnimSequence is loadable"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	TestEqual(TEXT("RateScale is applied through generic Properties"), AnimSequence->RateScale, 1.25f);

	const TSharedPtr<FJsonObject> Extracted = Generator.Extract(AnimSequence, false);
	TestTrue(TEXT("Extract returns a config object"), Extracted.IsValid());
	if (!Extracted.IsValid())
	{
		return false;
	}

	FString SkeletonPath;
	TestTrue(TEXT("Extract includes Skeleton"), Extracted->TryGetStringField(TEXT("Skeleton"), SkeletonPath));
	TestEqual(TEXT("Extracted Skeleton path"), SkeletonPath, FString(TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton")));

	FString PreviewMeshPath;
	TestTrue(TEXT("Extract includes PreviewMesh"), Extracted->TryGetStringField(TEXT("PreviewMesh"), PreviewMeshPath));
	TestEqual(TEXT("Extracted PreviewMesh path"), PreviewMeshPath, FString(TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP")));

	const TSharedPtr<FJsonObject>* FrameRateObject = nullptr;
	TestTrue(TEXT("Extract includes FrameRate"), Extracted->TryGetObjectField(TEXT("FrameRate"), FrameRateObject));
	TestTrue(TEXT("Extracted FrameRate object is valid"), FrameRateObject && FrameRateObject->IsValid());
	if (!FrameRateObject || !FrameRateObject->IsValid())
	{
		return false;
	}

	double Numerator = 0.0;
	double Denominator = 0.0;
	TestTrue(TEXT("FrameRate includes Numerator"), (*FrameRateObject)->TryGetNumberField(TEXT("Numerator"), Numerator));
	TestTrue(TEXT("FrameRate includes Denominator"), (*FrameRateObject)->TryGetNumberField(TEXT("Denominator"), Denominator));
	TestEqual(TEXT("Extracted FrameRate Numerator"), static_cast<int32>(Numerator), 30);
	TestEqual(TEXT("Extracted FrameRate Denominator"), static_cast<int32>(Denominator), 1);

	double NumberOfFrames = 0.0;
	TestTrue(TEXT("Extract includes NumberOfFrames"), Extracted->TryGetNumberField(TEXT("NumberOfFrames"), NumberOfFrames));
	TestEqual(TEXT("Extracted NumberOfFrames"), static_cast<int32>(NumberOfFrames), 12);

	double RateScale = 0.0;
	TestTrue(TEXT("Extract includes RateScale"), Extracted->TryGetNumberField(TEXT("RateScale"), RateScale));
	TestEqual(TEXT("Extracted RateScale"), static_cast<float>(RateScale), 1.25f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimSequencePropertiesMustBeObjectTest,
	"AssetFactory.AnimSequence.PropertiesMustBeObject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimSequencePropertiesMustBeObjectTest::RunTest(const FString& Parameters)
{
	FAnimSequenceGenerator Generator;

	const FGenerationResult Result = Generator.Generate(
		ReflectedPropertiesAssetName,
		TestAnimPath,
		EGenerationAction::CreateOrUpdate,
		MakeInvalidPropertiesConfig());

	TestEqual(TEXT("Non-object Properties patch fails"), Result.Status, EGenerationStatus::Failed);
	TestEqual(TEXT("Non-object Properties reports validation error"), Result.Message, FString(TEXT("'Properties' must be an object")));

	return true;
}

#endif
