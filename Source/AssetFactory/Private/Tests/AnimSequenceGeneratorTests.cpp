// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/AnimSequenceGenerator.h"

#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/FrameRate.h"
#include "Misc/Guid.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
const TCHAR* TestAnimPath = TEXT("/Game/Generated/Animation");

FString MakeUniqueTestAssetName(const TCHAR* Prefix)
{
	return FString::Printf(TEXT("%s_%s"), Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
}

FString MakeAnimSequenceObjectPath(const FString& AssetName)
{
	return FString::Printf(TEXT("%s/%s.%s"), TestAnimPath, *AssetName, *AssetName);
}

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

TSharedPtr<FJsonObject> MakeHugeFrameRatePatchConfig()
{
	TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> FrameRate = MakeShared<FJsonObject>();
	FrameRate->SetNumberField(TEXT("Numerator"), static_cast<double>(MAX_int32) + 1.0);
	FrameRate->SetNumberField(TEXT("Denominator"), 1);
	Config->SetObjectField(TEXT("FrameRate"), FrameRate);
	Config->SetNumberField(TEXT("NumberOfFrames"), 12);

	return Config;
}

TSharedPtr<FJsonObject> MakeHugeNumberOfFramesPatchConfig()
{
	TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> FrameRate = MakeShared<FJsonObject>();
	FrameRate->SetNumberField(TEXT("Numerator"), 30);
	FrameRate->SetNumberField(TEXT("Denominator"), 1);
	Config->SetObjectField(TEXT("FrameRate"), FrameRate);
	Config->SetNumberField(TEXT("NumberOfFrames"), static_cast<double>(MAX_int32) + 1.0);

	return Config;
}

TSharedPtr<FJsonObject> MakeFloatCurve(const TCHAR* CurveName)
{
	TSharedPtr<FJsonObject> Curve = MakeShared<FJsonObject>();
	Curve->SetStringField(TEXT("Name"), CurveName);

	TArray<TSharedPtr<FJsonValue>> Keys;
	TSharedPtr<FJsonObject> FirstKey = MakeShared<FJsonObject>();
	FirstKey->SetNumberField(TEXT("Time"), 0.0);
	FirstKey->SetNumberField(TEXT("Value"), 1.0);
	Keys.Add(MakeShared<FJsonValueObject>(FirstKey));

	TSharedPtr<FJsonObject> SecondKey = MakeShared<FJsonObject>();
	SecondKey->SetNumberField(TEXT("Time"), 0.25);
	SecondKey->SetNumberField(TEXT("Value"), 2.0);
	Keys.Add(MakeShared<FJsonValueObject>(SecondKey));

	Curve->SetArrayField(TEXT("Keys"), Keys);
	return Curve;
}

TSharedPtr<FJsonObject> MakeSingleFloatCurveConfig()
{
	TSharedPtr<FJsonObject> Config = MakeAnimSequenceBaseConfig();

	TArray<TSharedPtr<FJsonValue>> FloatCurves;
	FloatCurves.Add(MakeShared<FJsonValueObject>(MakeFloatCurve(TEXT("ExistingCurve"))));
	Config->SetArrayField(TEXT("FloatCurves"), FloatCurves);

	return Config;
}

TSharedPtr<FJsonObject> MakeInvalidFloatCurvesPatchConfig()
{
	TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> FrameRate = MakeShared<FJsonObject>();
	FrameRate->SetNumberField(TEXT("Numerator"), 60);
	FrameRate->SetNumberField(TEXT("Denominator"), 1);
	Config->SetObjectField(TEXT("FrameRate"), FrameRate);
	Config->SetNumberField(TEXT("NumberOfFrames"), 24);
	Config->SetNumberField(TEXT("RateScale"), 1.75);

	TArray<TSharedPtr<FJsonValue>> FloatCurves;
	FloatCurves.Add(MakeShared<FJsonValueObject>(MakeFloatCurve(TEXT("DuplicateCurve"))));
	FloatCurves.Add(MakeShared<FJsonValueObject>(MakeFloatCurve(TEXT("DuplicateCurve"))));
	Config->SetArrayField(TEXT("FloatCurves"), FloatCurves);

	return Config;
}

TSharedPtr<FJsonObject> MakeInvalidFloatCurvesCreateConfig()
{
	TSharedPtr<FJsonObject> Config = MakeAnimSequenceBaseConfig();

	TArray<TSharedPtr<FJsonValue>> FloatCurves;
	FloatCurves.Add(MakeShared<FJsonValueObject>(MakeFloatCurve(TEXT("DuplicateCurve"))));
	FloatCurves.Add(MakeShared<FJsonValueObject>(MakeFloatCurve(TEXT("DuplicateCurve"))));
	Config->SetArrayField(TEXT("FloatCurves"), FloatCurves);

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

TSharedPtr<FJsonObject> MakeInvalidPropertiesPatchConfig()
{
	TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> FrameRate = MakeShared<FJsonObject>();
	FrameRate->SetNumberField(TEXT("Numerator"), 60);
	FrameRate->SetNumberField(TEXT("Denominator"), 1);
	Config->SetObjectField(TEXT("FrameRate"), FrameRate);
	Config->SetNumberField(TEXT("NumberOfFrames"), 24);
	Config->SetNumberField(TEXT("RateScale"), 1.75);

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetNumberField(TEXT("DefinitelyNotARealAnimSequenceProperty"), 42.0);
	Config->SetObjectField(TEXT("Properties"), Properties);

	return Config;
}

TSharedPtr<FJsonObject> MakeInvalidPropertiesCreateConfig()
{
	TSharedPtr<FJsonObject> Config = MakeAnimSequenceBaseConfig();

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetNumberField(TEXT("DefinitelyNotARealAnimSequenceProperty"), 42.0);
	Config->SetObjectField(TEXT("Properties"), Properties);

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
	const FString AssetName = MakeUniqueTestAssetName(TEXT("AS_PrevalidateTimingFields"));
	const FString AssetObjectPath = MakeAnimSequenceObjectPath(AssetName);

	const FGenerationResult BaseResult = Generator.Generate(
		AssetName,
		TestAnimPath,
		EGenerationAction::Create,
		MakeAnimSequenceBaseConfig());
	TestTrue(TEXT("Base AnimSequence is generated"), BaseResult.IsSuccess());

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(BaseResult.GeneratedAsset);
	if (!AnimSequence)
	{
		AnimSequence = LoadObject<UAnimSequence>(nullptr, *AssetObjectPath);
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
		AssetName,
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
	FAnimSequenceRejectsHugeTimingFieldsTest,
	"AssetFactory.AnimSequence.RejectsHugeTimingFieldsBeforeMutation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimSequenceRejectsHugeTimingFieldsTest::RunTest(const FString& Parameters)
{
	FAnimSequenceGenerator Generator;
	const FString AssetName = MakeUniqueTestAssetName(TEXT("AS_TimingUpperBoundPrevalidation"));
	const FString AssetObjectPath = MakeAnimSequenceObjectPath(AssetName);

	const FGenerationResult BaseResult = Generator.Generate(
		AssetName,
		TestAnimPath,
		EGenerationAction::Create,
		MakeAnimSequenceBaseConfig());
	TestTrue(TEXT("Base AnimSequence is generated"), BaseResult.IsSuccess());

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(BaseResult.GeneratedAsset);
	if (!AnimSequence)
	{
		AnimSequence = LoadObject<UAnimSequence>(nullptr, *AssetObjectPath);
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

	const FGenerationResult HugeFrameRateResult = Generator.Generate(
		AssetName,
		TestAnimPath,
		EGenerationAction::Update,
		MakeHugeFrameRatePatchConfig());

	TestEqual(TEXT("Huge FrameRate numerator patch fails"), HugeFrameRateResult.Status, EGenerationStatus::Failed);
	TestEqual(TEXT("Huge FrameRate numerator reports positive integer validation"), HugeFrameRateResult.Message, FString(TEXT("'FrameRate.Numerator' must be a positive integer")));

	DataModel = AnimSequence->GetDataModel();
	TestNotNull(TEXT("AnimSequence still has a data model after huge FrameRate patch"), DataModel);
	if (!DataModel)
	{
		return false;
	}
	TestEqual(TEXT("FrameRate is unchanged after huge FrameRate patch"), DataModel->GetFrameRate(), OriginalFrameRate);
	TestEqual(TEXT("NumberOfFrames is unchanged after huge FrameRate patch"), DataModel->GetNumberOfFrames(), OriginalNumberOfFrames);

	const FGenerationResult HugeNumberOfFramesResult = Generator.Generate(
		AssetName,
		TestAnimPath,
		EGenerationAction::Update,
		MakeHugeNumberOfFramesPatchConfig());

	TestEqual(TEXT("Huge NumberOfFrames patch fails"), HugeNumberOfFramesResult.Status, EGenerationStatus::Failed);
	TestEqual(TEXT("Huge NumberOfFrames reports positive integer validation"), HugeNumberOfFramesResult.Message, FString(TEXT("'NumberOfFrames' must be a positive integer")));

	DataModel = AnimSequence->GetDataModel();
	TestNotNull(TEXT("AnimSequence still has a data model after huge NumberOfFrames patch"), DataModel);
	if (!DataModel)
	{
		return false;
	}
	TestEqual(TEXT("FrameRate is unchanged after huge NumberOfFrames patch"), DataModel->GetFrameRate(), OriginalFrameRate);
	TestEqual(TEXT("NumberOfFrames is unchanged after huge NumberOfFrames patch"), DataModel->GetNumberOfFrames(), OriginalNumberOfFrames);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimSequencePrevalidateFloatCurvesBeforeMutationTest,
	"AssetFactory.AnimSequence.PrevalidateFloatCurvesBeforeMutation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimSequencePrevalidateFloatCurvesBeforeMutationTest::RunTest(const FString& Parameters)
{
	FAnimSequenceGenerator Generator;
	const FString AssetName = MakeUniqueTestAssetName(TEXT("AS_FloatCurvesPrevalidation"));
	const FString AssetObjectPath = MakeAnimSequenceObjectPath(AssetName);

	const FGenerationResult BaseResult = Generator.Generate(
		AssetName,
		TestAnimPath,
		EGenerationAction::Create,
		MakeSingleFloatCurveConfig());
	TestTrue(TEXT("Base AnimSequence with FloatCurves is generated"), BaseResult.IsSuccess());

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(BaseResult.GeneratedAsset);
	if (!AnimSequence)
	{
		AnimSequence = LoadObject<UAnimSequence>(nullptr, *AssetObjectPath);
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
	TestEqual(TEXT("Base AnimSequence has one float curve"), DataModel->GetFloatCurves().Num(), 1);
	if (DataModel->GetFloatCurves().Num() != 1)
	{
		return false;
	}

	const FFrameRate OriginalFrameRate = DataModel->GetFrameRate();
	const int32 OriginalNumberOfFrames = DataModel->GetNumberOfFrames();
	const FName OriginalCurveName = DataModel->GetFloatCurves()[0].GetName();
	const int32 OriginalCurveKeyCount = DataModel->GetFloatCurves()[0].FloatCurve.GetConstRefOfKeys().Num();
	const float OriginalRateScale = AnimSequence->RateScale;

	const FGenerationResult InvalidResult = Generator.Generate(
		AssetName,
		TestAnimPath,
		EGenerationAction::Update,
		MakeInvalidFloatCurvesPatchConfig());

	TestEqual(TEXT("Invalid FloatCurves patch fails"), InvalidResult.Status, EGenerationStatus::Failed);
	TestEqual(TEXT("Invalid FloatCurves patch reports duplicate validation"), InvalidResult.Message, FString(TEXT("FloatCurves contains duplicate Name 'DuplicateCurve'")));

	DataModel = AnimSequence->GetDataModel();
	TestNotNull(TEXT("AnimSequence still has a data model after failed FloatCurves patch"), DataModel);
	if (!DataModel)
	{
		return false;
	}
	TestEqual(TEXT("FrameRate is unchanged after failed FloatCurves patch"), DataModel->GetFrameRate(), OriginalFrameRate);
	TestEqual(TEXT("NumberOfFrames is unchanged after failed FloatCurves patch"), DataModel->GetNumberOfFrames(), OriginalNumberOfFrames);
	TestEqual(TEXT("RateScale is unchanged after failed FloatCurves patch"), AnimSequence->RateScale, OriginalRateScale);
	TestEqual(TEXT("Float curve count is unchanged after failed FloatCurves patch"), DataModel->GetFloatCurves().Num(), 1);
	if (DataModel->GetFloatCurves().Num() != 1)
	{
		return false;
	}
	TestEqual(TEXT("Float curve name is unchanged after failed FloatCurves patch"), DataModel->GetFloatCurves()[0].GetName(), OriginalCurveName);
	TestEqual(TEXT("Float curve key count is unchanged after failed FloatCurves patch"), DataModel->GetFloatCurves()[0].FloatCurve.GetConstRefOfKeys().Num(), OriginalCurveKeyCount);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimSequenceReflectedPropertiesExtractTest,
	"AssetFactory.AnimSequence.ReflectedPropertiesAndScalarExtract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimSequenceReflectedPropertiesExtractTest::RunTest(const FString& Parameters)
{
	FAnimSequenceGenerator Generator;
	const FString AssetName = MakeUniqueTestAssetName(TEXT("AS_ReflectedProperties"));
	const FString AssetObjectPath = MakeAnimSequenceObjectPath(AssetName);

	const FGenerationResult Result = Generator.Generate(
		AssetName,
		TestAnimPath,
		EGenerationAction::Create,
		MakeReflectedPropertiesConfig());
	TestTrue(TEXT("AnimSequence with reflected Properties is generated"), Result.IsSuccess());

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(Result.GeneratedAsset);
	if (!AnimSequence)
	{
		AnimSequence = LoadObject<UAnimSequence>(nullptr, *AssetObjectPath);
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
	const FString AssetName = MakeUniqueTestAssetName(TEXT("AS_PropertiesMustBeObject"));
	const FString AssetObjectPath = MakeAnimSequenceObjectPath(AssetName);

	const TOptional<FString> ValidationError = Generator.ValidateConfig(MakeInvalidPropertiesConfig(), EGenerationAction::Update);
	TestTrue(TEXT("ValidateConfig rejects non-object Properties"), ValidationError.IsSet());
	if (!ValidationError.IsSet())
	{
		return false;
	}
	TestEqual(TEXT("ValidateConfig reports non-object Properties"), ValidationError.GetValue(), FString(TEXT("'Properties' must be an object")));

	const FGenerationResult BaseResult = Generator.Generate(
		AssetName,
		TestAnimPath,
		EGenerationAction::Create,
		MakeAnimSequenceBaseConfig());
	TestTrue(TEXT("Base AnimSequence is generated"), BaseResult.IsSuccess());

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(BaseResult.GeneratedAsset);
	if (!AnimSequence)
	{
		AnimSequence = LoadObject<UAnimSequence>(nullptr, *AssetObjectPath);
	}
	TestNotNull(TEXT("Generated AnimSequence is loadable"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	const float OriginalRateScale = AnimSequence->RateScale;

	const FGenerationResult Result = Generator.Generate(
		AssetName,
		TestAnimPath,
		EGenerationAction::Update,
		MakeInvalidPropertiesConfig());

	TestEqual(TEXT("Non-object Properties patch fails"), Result.Status, EGenerationStatus::Failed);
	TestEqual(TEXT("Non-object Properties reports validation error"), Result.Message, FString(TEXT("'Properties' must be an object")));
	TestEqual(TEXT("RateScale is unchanged after non-object Properties patch"), AnimSequence->RateScale, OriginalRateScale);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimSequencePrevalidatePropertiesBeforeMutationTest,
	"AssetFactory.AnimSequence.PrevalidatePropertiesBeforeMutation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimSequencePrevalidatePropertiesBeforeMutationTest::RunTest(const FString& Parameters)
{
	FAnimSequenceGenerator Generator;
	const FString AssetName = MakeUniqueTestAssetName(TEXT("AS_PropertiesPrevalidation"));
	const FString AssetObjectPath = MakeAnimSequenceObjectPath(AssetName);

	const FGenerationResult BaseResult = Generator.Generate(
		AssetName,
		TestAnimPath,
		EGenerationAction::Create,
		MakeAnimSequenceBaseConfig());
	TestTrue(TEXT("Base AnimSequence is generated"), BaseResult.IsSuccess());

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(BaseResult.GeneratedAsset);
	if (!AnimSequence)
	{
		AnimSequence = LoadObject<UAnimSequence>(nullptr, *AssetObjectPath);
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
	const float OriginalRateScale = AnimSequence->RateScale;
	const FString OriginalPreviewMeshPath = AnimSequence->GetPreviewMesh(false)
		? AnimSequence->GetPreviewMesh(false)->GetPathName()
		: FString();

	const FGenerationResult InvalidResult = Generator.Generate(
		AssetName,
		TestAnimPath,
		EGenerationAction::Update,
		MakeInvalidPropertiesPatchConfig());

	TestEqual(TEXT("Invalid Properties patch fails"), InvalidResult.Status, EGenerationStatus::Failed);
	TestEqual(TEXT("Invalid Properties patch reports generic properties failure"), InvalidResult.Message, FString(TEXT("Failed to apply AnimSequence Properties")));

	DataModel = AnimSequence->GetDataModel();
	TestNotNull(TEXT("AnimSequence still has a data model after failed Properties patch"), DataModel);
	if (!DataModel)
	{
		return false;
	}

	TestEqual(TEXT("FrameRate is unchanged after failed Properties patch"), DataModel->GetFrameRate(), OriginalFrameRate);
	TestEqual(TEXT("NumberOfFrames is unchanged after failed Properties patch"), DataModel->GetNumberOfFrames(), OriginalNumberOfFrames);
	TestEqual(TEXT("RateScale is unchanged after failed Properties patch"), AnimSequence->RateScale, OriginalRateScale);
	const FString CurrentPreviewMeshPath = AnimSequence->GetPreviewMesh(false)
		? AnimSequence->GetPreviewMesh(false)->GetPathName()
		: FString();
	TestEqual(TEXT("PreviewMesh is unchanged after failed Properties patch"), CurrentPreviewMeshPath, OriginalPreviewMeshPath);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimSequencePrevalidateCreatePropertiesBeforeAssetCreationTest,
	"AssetFactory.AnimSequence.PrevalidateCreatePropertiesBeforeAssetCreation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimSequencePrevalidateCreatePropertiesBeforeAssetCreationTest::RunTest(const FString& Parameters)
{
	FAnimSequenceGenerator Generator;
	const FString InvalidCreatePropertiesPreflightAssetName = MakeUniqueTestAssetName(TEXT("AS_InvalidCreatePropertiesPreflight"));
	const FString InvalidAssetObjectPath = MakeAnimSequenceObjectPath(InvalidCreatePropertiesPreflightAssetName);

	const FGenerationResult InvalidResult = Generator.Generate(
		InvalidCreatePropertiesPreflightAssetName,
		TestAnimPath,
		EGenerationAction::Create,
		MakeInvalidPropertiesCreateConfig());

	TestEqual(TEXT("Invalid create Properties patch fails"), InvalidResult.Status, EGenerationStatus::Failed);
	TestEqual(TEXT("Invalid create Properties patch reports generic properties failure"), InvalidResult.Message, FString(TEXT("Failed to apply AnimSequence Properties")));
	TestNull(
		TEXT("Invalid create Properties patch does not leave a loadable AnimSequence asset"),
		LoadObject<UAnimSequence>(nullptr, *InvalidAssetObjectPath));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimSequenceCreateHugeTimingFailureDoesNotLeaveAssetTest,
	"AssetFactory.AnimSequence.CreateHugeTimingFailureDoesNotLeaveAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimSequenceCreateHugeTimingFailureDoesNotLeaveAssetTest::RunTest(const FString& Parameters)
{
	FAnimSequenceGenerator Generator;
	const FString AssetName = MakeUniqueTestAssetName(TEXT("AS_CreateHugeTimingAtomicity"));
	const FString AssetObjectPath = MakeAnimSequenceObjectPath(AssetName);

	TSharedPtr<FJsonObject> Config = MakeAnimSequenceBaseConfig();
	TSharedPtr<FJsonObject> FrameRate = MakeShared<FJsonObject>();
	FrameRate->SetNumberField(TEXT("Numerator"), static_cast<double>(MAX_int32) + 1.0);
	FrameRate->SetNumberField(TEXT("Denominator"), 1);
	Config->SetObjectField(TEXT("FrameRate"), FrameRate);

	const FGenerationResult Result = Generator.Generate(
		AssetName,
		TestAnimPath,
		EGenerationAction::Create,
		Config);

	TestEqual(TEXT("Huge create FrameRate fails"), Result.Status, EGenerationStatus::Failed);
	TestEqual(TEXT("Huge create FrameRate reports validation"), Result.Message, FString(TEXT("'FrameRate.Numerator' must be a positive integer")));
	TestNull(
		TEXT("Huge create FrameRate failure does not leave a loadable AnimSequence asset"),
		LoadObject<UAnimSequence>(nullptr, *AssetObjectPath));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimSequenceCreateInvalidFloatCurvesFailureDoesNotLeaveAssetTest,
	"AssetFactory.AnimSequence.CreateInvalidFloatCurvesFailureDoesNotLeaveAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimSequenceCreateInvalidFloatCurvesFailureDoesNotLeaveAssetTest::RunTest(const FString& Parameters)
{
	FAnimSequenceGenerator Generator;
	const FString AssetName = MakeUniqueTestAssetName(TEXT("AS_CreateInvalidFloatCurvesAtomicity"));
	const FString AssetObjectPath = MakeAnimSequenceObjectPath(AssetName);

	const FGenerationResult Result = Generator.Generate(
		AssetName,
		TestAnimPath,
		EGenerationAction::Create,
		MakeInvalidFloatCurvesCreateConfig());

	TestEqual(TEXT("Invalid create FloatCurves fails"), Result.Status, EGenerationStatus::Failed);
	TestEqual(TEXT("Invalid create FloatCurves reports duplicate validation"), Result.Message, FString(TEXT("FloatCurves contains duplicate Name 'DuplicateCurve'")));
	TestNull(
		TEXT("Invalid create FloatCurves failure does not leave a loadable AnimSequence asset"),
		LoadObject<UAnimSequence>(nullptr, *AssetObjectPath));

	return true;
}

#endif
