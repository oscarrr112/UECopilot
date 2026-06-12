// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentService.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/SkeletalMesh.h"
#include "Generators/AnimSequenceGenerator.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
const TCHAR* TestSkeletonPath = TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton");
const TCHAR* TestPreviewMeshPath = TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP");
const TCHAR* TestAnimPath = TEXT("/Game/Generated/Animation");

TSharedPtr<FJsonObject> MakeMontageDocument(const FString& Target)
{
	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimMontage"));
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Body"), MakeShared<FJsonObject>());
	return Document;
}

TSharedPtr<FJsonObject> MakeAssetRef(const FString& Path)
{
	TSharedPtr<FJsonObject> Fragment = MakeShared<FJsonObject>();
	Fragment->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	Fragment->SetStringField(TEXT("Path"), Path);
	return Fragment;
}

FString MakeUniqueTestAssetName(const TCHAR* Prefix)
{
	return FString::Printf(TEXT("%s_%s"), Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
}

FString MakeAnimSequenceObjectPath(const FString& AssetName)
{
	return FString::Printf(TEXT("%s/%s.%s"), TestAnimPath, *AssetName, *AssetName);
}

UAnimSequenceBase* CreateAnimSequenceFixture()
{
	FAnimSequenceGenerator Generator;
	const FString AssetName = MakeUniqueTestAssetName(TEXT("AS_AssetDocumentMontage"));

	TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();
	Config->SetStringField(TEXT("Skeleton"), TestSkeletonPath);
	Config->SetStringField(TEXT("PreviewMesh"), TestPreviewMeshPath);

	TSharedPtr<FJsonObject> FrameRate = MakeShared<FJsonObject>();
	FrameRate->SetNumberField(TEXT("Numerator"), 30);
	FrameRate->SetNumberField(TEXT("Denominator"), 1);
	Config->SetObjectField(TEXT("FrameRate"), FrameRate);
	Config->SetNumberField(TEXT("NumberOfFrames"), 12);

	const FGenerationResult Result = Generator.Generate(AssetName, TestAnimPath, EGenerationAction::CreateOrUpdate, Config);
	if (!Result.IsSuccess())
	{
		return nullptr;
	}

	UAnimSequenceBase* AnimSequence = Cast<UAnimSequenceBase>(Result.GeneratedAsset);
	return AnimSequence ? AnimSequence : LoadObject<UAnimSequenceBase>(nullptr, *MakeAnimSequenceObjectPath(AssetName));
}

bool JsonArrayContainsString(const TArray<TSharedPtr<FJsonValue>>& Values, const FString& Expected)
{
	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		if (Value.IsValid() && Value->AsString() == Expected)
		{
			return true;
		}
	}
	return false;
}

bool HasExpectedBodySections(const TArray<TSharedPtr<FJsonValue>>& BodySections)
{
	return JsonArrayContainsString(BodySections, TEXT("Skeleton"))
		&& JsonArrayContainsString(BodySections, TEXT("PreviewMesh"))
		&& JsonArrayContainsString(BodySections, TEXT("SlotAnimTracks"))
		&& JsonArrayContainsString(BodySections, TEXT("CompositeSections"))
		&& JsonArrayContainsString(BodySections, TEXT("Notifies"))
		&& JsonArrayContainsString(BodySections, TEXT("NotifyStates"))
		&& JsonArrayContainsString(BodySections, TEXT("Blend"));
}

bool HasDiagnostic(const FAssetDocumentResult& Result, const FString& Path, const FString& Code)
{
	for (const FAssetDocumentDiagnostic& Diagnostic : Result.Diagnostics)
	{
		if (Diagnostic.Path == Path && Diagnostic.Code == Code)
		{
			return true;
		}
	}
	return false;
}

bool HasDiagnosticMessage(const FAssetDocumentResult& Result, const FString& Path, const FString& Code, const FString& MessageFragment)
{
	for (const FAssetDocumentDiagnostic& Diagnostic : Result.Diagnostics)
	{
		if (Diagnostic.Path == Path && Diagnostic.Code == Code && Diagnostic.Message.Contains(MessageFragment))
		{
			return true;
		}
	}
	return false;
}

FAssetDocumentResult ValidateDocument(TSharedPtr<FJsonObject> Document)
{
	FAssetDocumentService Service;
	FAssetDocumentValidateRequest Request;
	Request.Document = Document;
	return Service.Validate(Request);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageApplyStructureTest,
	"AssetFactory.AssetDocument.AnimMontage.Apply.Structure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageApplyStructureTest::RunTest(const FString& Parameters)
{
	UAnimSequenceBase* AnimSequence = CreateAnimSequenceFixture();
	TestNotNull(TEXT("AnimSequence fixture is available"), AnimSequence);
	if (!AnimSequence)
	{
		return false;
	}

	TSharedPtr<FJsonObject> Document = MakeMontageDocument(TEXT("/Game/AssetDocumentTests/AM_Structure"));
	TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));
	Body->SetObjectField(TEXT("Skeleton"), MakeAssetRef(TestSkeletonPath));
	Body->SetObjectField(TEXT("PreviewMesh"), MakeAssetRef(TestPreviewMeshPath));

	TSharedPtr<FJsonObject> Segment = MakeShared<FJsonObject>();
	Segment->SetObjectField(TEXT("AnimReference"), MakeAssetRef(AnimSequence->GetPathName()));
	Segment->SetNumberField(TEXT("StartPos"), 0.0);
	Segment->SetNumberField(TEXT("AnimStartTime"), 0.0);
	Segment->SetNumberField(TEXT("AnimEndTime"), 0.25);
	Segment->SetNumberField(TEXT("AnimPlayRate"), 1.0);
	Segment->SetNumberField(TEXT("LoopingCount"), 1.0);

	TArray<TSharedPtr<FJsonValue>> AnimSegments;
	AnimSegments.Add(MakeShared<FJsonValueObject>(Segment));

	TSharedPtr<FJsonObject> AnimTrack = MakeShared<FJsonObject>();
	AnimTrack->SetArrayField(TEXT("AnimSegments"), AnimSegments);

	TSharedPtr<FJsonObject> SlotAnimTrack = MakeShared<FJsonObject>();
	SlotAnimTrack->SetStringField(TEXT("SlotName"), TEXT("DefaultSlot"));
	SlotAnimTrack->SetObjectField(TEXT("AnimTrack"), AnimTrack);

	TArray<TSharedPtr<FJsonValue>> SlotAnimTracks;
	SlotAnimTracks.Add(MakeShared<FJsonValueObject>(SlotAnimTrack));
	Body->SetArrayField(TEXT("SlotAnimTracks"), SlotAnimTracks);

	TSharedPtr<FJsonObject> StartSection = MakeShared<FJsonObject>();
	StartSection->SetStringField(TEXT("SectionName"), TEXT("Start"));
	StartSection->SetNumberField(TEXT("LinkableTime"), 0.0);
	StartSection->SetStringField(TEXT("NextSectionName"), TEXT("End"));

	TSharedPtr<FJsonObject> EndSection = MakeShared<FJsonObject>();
	EndSection->SetStringField(TEXT("SectionName"), TEXT("End"));
	EndSection->SetNumberField(TEXT("LinkableTime"), 0.25);

	TArray<TSharedPtr<FJsonValue>> CompositeSections;
	CompositeSections.Add(MakeShared<FJsonValueObject>(StartSection));
	CompositeSections.Add(MakeShared<FJsonValueObject>(EndSection));
	Body->SetArrayField(TEXT("CompositeSections"), CompositeSections);

	TSharedPtr<FJsonObject> Blend = MakeShared<FJsonObject>();
	Blend->SetNumberField(TEXT("BlendInTime"), 0.1);
	Blend->SetNumberField(TEXT("BlendOutTime"), 0.2);
	Body->SetObjectField(TEXT("Blend"), Blend);

	FAssetDocumentService Service;
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;

	const FAssetDocumentResult Result = Service.Apply(Request);
	TestTrue(TEXT("Apply succeeds for structured AnimMontage body"), Result.IsSuccess());
	if (!Result.IsSuccess())
	{
		AddError(Result.Message);
		return false;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/AssetDocumentTests/AM_Structure.AM_Structure"));
	TestNotNull(TEXT("Applied AnimMontage is loadable"), Montage);
	if (!Montage)
	{
		return false;
	}

	TestNotNull(TEXT("Montage has skeleton"), Montage->GetSkeleton());
	TestNotNull(TEXT("Montage has preview mesh"), Montage->GetPreviewMesh());
	TestEqual(TEXT("Montage has one SlotAnimTrack"), Montage->SlotAnimTracks.Num(), 1);
	if (Montage->SlotAnimTracks.Num() == 1)
	{
		TestEqual(TEXT("SlotAnimTrack name"), Montage->SlotAnimTracks[0].SlotName, FName(TEXT("DefaultSlot")));
		TestEqual(TEXT("SlotAnimTrack has one segment"), Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num(), 1);
		if (Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() == 1)
		{
			TestNotNull(TEXT("AnimSegment has AnimReference"), Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference().Get());
		}
	}

	TestEqual(TEXT("Montage has two CompositeSections"), Montage->CompositeSections.Num(), 2);
	if (Montage->CompositeSections.Num() >= 2)
	{
		TestEqual(TEXT("First section name"), Montage->CompositeSections[0].SectionName, FName(TEXT("Start")));
		TestEqual(TEXT("First section next section"), Montage->CompositeSections[0].NextSectionName, FName(TEXT("End")));
	}

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = TEXT("/Game/AssetDocumentTests/AM_Structure");
	ExtractRequest.bDiffOnly = false;
	ExtractRequest.bIncludeAllWritable = true;

	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds for structured AnimMontage body"), ExtractResult.IsSuccess());
	TestTrue(TEXT("Extract returns payload"), ExtractResult.Payload.IsValid());
	if (ExtractResult.Payload.IsValid())
	{
		const TSharedPtr<FJsonObject>* ExtractedBody = nullptr;
		TestTrue(TEXT("Extract includes Body"), ExtractResult.Payload->TryGetObjectField(TEXT("Body"), ExtractedBody));
		if (ExtractedBody && ExtractedBody->IsValid())
		{
			TestTrue(TEXT("Extract includes Skeleton"), (*ExtractedBody)->HasTypedField<EJson::Object>(TEXT("Skeleton")));
			TestTrue(TEXT("Extract includes PreviewMesh"), (*ExtractedBody)->HasTypedField<EJson::Object>(TEXT("PreviewMesh")));

			const TArray<TSharedPtr<FJsonValue>>* ExtractedSlotAnimTracks = nullptr;
			TestTrue(TEXT("Extract includes SlotAnimTracks"), (*ExtractedBody)->TryGetArrayField(TEXT("SlotAnimTracks"), ExtractedSlotAnimTracks));
			TestTrue(TEXT("Extract has one SlotAnimTrack"), ExtractedSlotAnimTracks && ExtractedSlotAnimTracks->Num() == 1);
			if (ExtractedSlotAnimTracks && ExtractedSlotAnimTracks->Num() == 1)
			{
				const TSharedPtr<FJsonObject> ExtractedSlot = (*ExtractedSlotAnimTracks)[0]->AsObject();
				TestTrue(TEXT("Extracted SlotAnimTrack is object"), ExtractedSlot.IsValid());
				if (ExtractedSlot.IsValid())
				{
					TestEqual(TEXT("Extracted SlotName"), ExtractedSlot->GetStringField(TEXT("SlotName")), FString(TEXT("DefaultSlot")));
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* ExtractedSections = nullptr;
			TestTrue(TEXT("Extract includes CompositeSections"), (*ExtractedBody)->TryGetArrayField(TEXT("CompositeSections"), ExtractedSections));
			TestTrue(TEXT("Extract has two CompositeSections"), ExtractedSections && ExtractedSections->Num() == 2);
			if (ExtractedSections && ExtractedSections->Num() >= 2)
			{
				const TSharedPtr<FJsonObject> ExtractedStartSection = (*ExtractedSections)[0]->AsObject();
				TestTrue(TEXT("Extracted first section is object"), ExtractedStartSection.IsValid());
				if (ExtractedStartSection.IsValid())
				{
					TestEqual(TEXT("Extracted first section name"), ExtractedStartSection->GetStringField(TEXT("SectionName")), FString(TEXT("Start")));
					TestEqual(TEXT("Extracted first section next"), ExtractedStartSection->GetStringField(TEXT("NextSectionName")), FString(TEXT("End")));
				}
			}

			const TSharedPtr<FJsonObject>* ExtractedBlend = nullptr;
			TestTrue(TEXT("Extract includes Blend"), (*ExtractedBody)->TryGetObjectField(TEXT("Blend"), ExtractedBlend));
			if (ExtractedBlend && ExtractedBlend->IsValid())
			{
				TestEqual(TEXT("Extracted BlendInTime"), (*ExtractedBlend)->GetNumberField(TEXT("BlendInTime")), 0.1);
				TestEqual(TEXT("Extracted BlendOutTime"), (*ExtractedBlend)->GetNumberField(TEXT("BlendOutTime")), 0.2);
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageInspectProfileTest,
	"AssetFactory.AssetDocument.AnimMontage.InspectProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageInspectProfileTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentService Service;
	FAssetDocumentProfileRequest Request;
	Request.ClassOrAsset = TEXT("/Script/Engine.AnimMontage");

	const FAssetDocumentResult Result = Service.InspectProfile(Request);
	TestTrue(TEXT("InspectProfile succeeds for AnimMontage class"), Result.IsSuccess());
	TestTrue(TEXT("InspectProfile returns a payload"), Result.Payload.IsValid());
	if (!Result.Payload.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* BodySections = nullptr;
	TestTrue(TEXT("AnimMontage profile includes BodySections"), Result.Payload->TryGetArrayField(TEXT("BodySections"), BodySections));
	if (BodySections)
	{
		TestTrue(TEXT("BodySections includes expected AnimMontage keys"), HasExpectedBodySections(*BodySections));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageCreateTemplateTest,
	"AssetFactory.AssetDocument.AnimMontage.CreateTemplate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageCreateTemplateTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentService Service;
	FAssetDocumentTemplateRequest Request;
	Request.Class = TEXT("/Script/Engine.AnimMontage");
	Request.Target = TEXT("/Game/AssetDocumentTests/AM_Template");

	const FAssetDocumentResult Result = Service.CreateTemplate(Request);
	TestTrue(TEXT("CreateTemplate succeeds for AnimMontage class"), Result.IsSuccess());
	TestTrue(TEXT("CreateTemplate returns a payload"), Result.Payload.IsValid());
	if (!Result.Payload.IsValid())
	{
		return false;
	}

	TestFalse(TEXT("AnimMontage template does not include AssetType"), Result.Payload->HasField(TEXT("AssetType")));
	TestEqual(TEXT("Template class is AnimMontage"), Result.Payload->GetStringField(TEXT("Class")), FString(TEXT("/Script/Engine.AnimMontage")));

	const TSharedPtr<FJsonObject>* Definitions = nullptr;
	TestTrue(TEXT("Template includes Definitions"), Result.Payload->TryGetObjectField(TEXT("Definitions"), Definitions));

	const TSharedPtr<FJsonObject>* Properties = nullptr;
	TestTrue(TEXT("Template includes Properties"), Result.Payload->TryGetObjectField(TEXT("Properties"), Properties));

	const TSharedPtr<FJsonObject>* Body = nullptr;
	TestTrue(TEXT("Template includes Body"), Result.Payload->TryGetObjectField(TEXT("Body"), Body));
	if (Body && Body->IsValid())
	{
		TestTrue(TEXT("Body includes Skeleton"), (*Body)->HasField(TEXT("Skeleton")));
		TestTrue(TEXT("Body includes PreviewMesh"), (*Body)->HasField(TEXT("PreviewMesh")));
		TestTrue(TEXT("Body includes SlotAnimTracks"), (*Body)->HasField(TEXT("SlotAnimTracks")));
		TestTrue(TEXT("Body includes CompositeSections"), (*Body)->HasField(TEXT("CompositeSections")));
		TestTrue(TEXT("Body includes Notifies"), (*Body)->HasField(TEXT("Notifies")));
		TestTrue(TEXT("Body includes NotifyStates"), (*Body)->HasField(TEXT("NotifyStates")));
		TestTrue(TEXT("Body includes Blend"), (*Body)->HasField(TEXT("Blend")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageValidateStructuredWithoutAssetTypeTest,
	"AssetFactory.AssetDocument.AnimMontage.ValidateStructuredWithoutAssetType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageValidateStructuredWithoutAssetTypeTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Document = MakeMontageDocument(TEXT("/Game/AssetDocumentTests/AM_Valid"));

	const FAssetDocumentResult Result = ValidateDocument(Document);
	TestTrue(TEXT("Structured AnimMontage document without AssetType validates"), Result.IsSuccess());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageRejectsUnknownBodyKeyTest,
	"AssetFactory.AssetDocument.AnimMontage.RejectsUnknownBodyKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageRejectsUnknownBodyKeyTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Document = MakeMontageDocument(TEXT("/Game/AssetDocumentTests/AM_Invalid"));
	Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("UnexpectedSection"), TArray<TSharedPtr<FJsonValue>>());

	const FAssetDocumentResult Result = ValidateDocument(Document);
	TestFalse(TEXT("Validate rejects unknown Body key"), Result.IsSuccess());
	TestTrue(TEXT("Validate reports UnknownBodyKey diagnostic"), HasDiagnosticMessage(Result, TEXT("/Body/UnexpectedSection"), TEXT("UnknownBodyKey"), TEXT("Unknown AnimMontage Body key")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageRejectsAbbreviatedSlotsTest,
	"AssetFactory.AssetDocument.AnimMontage.RejectsAbbreviatedSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageRejectsAbbreviatedSlotsTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Document = MakeMontageDocument(TEXT("/Game/AssetDocumentTests/AM_InvalidSlots"));
	Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Slots"), TArray<TSharedPtr<FJsonValue>>());

	const FAssetDocumentResult Result = ValidateDocument(Document);
	TestFalse(TEXT("Validate rejects abbreviated Body.Slots"), Result.IsSuccess());
	TestTrue(TEXT("Validate reports DeprecatedBodyKey diagnostic"), HasDiagnosticMessage(Result, TEXT("/Body/Slots"), TEXT("DeprecatedBodyKey"), TEXT("Use SlotAnimTracks")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageRejectsBodyWithoutProfileTest,
	"AssetFactory.AssetDocument.AnimMontage.RejectsBodyWithoutProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageRejectsBodyWithoutProfileTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Document = MakeMontageDocument(TEXT("/Game/AssetDocumentTests/DA_BodyUnsupported"));
	Document->SetStringField(TEXT("Class"), TEXT("/Script/AssetFactory.TestActorBase"));

	const FAssetDocumentResult Result = ValidateDocument(Document);
	TestFalse(TEXT("Validate rejects Body when class has no profile"), Result.IsSuccess());
	TestTrue(TEXT("Validate reports MissingProfile at Body"), HasDiagnostic(Result, TEXT("/Body"), TEXT("MissingProfile")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageRejectsNonObjectBodyTest,
	"AssetFactory.AssetDocument.AnimMontage.RejectsNonObjectBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageRejectsNonObjectBodyTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Document = MakeMontageDocument(TEXT("/Game/AssetDocumentTests/AM_InvalidBody"));
	Document->SetStringField(TEXT("Body"), TEXT("not an object"));

	const FAssetDocumentResult Result = ValidateDocument(Document);
	TestFalse(TEXT("Validate rejects non-object Body"), Result.IsSuccess());
	TestTrue(TEXT("Validate reports InvalidBodyType at Body"), HasDiagnostic(Result, TEXT("/Body"), TEXT("InvalidBodyType")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageRejectsInvalidBodySectionTypesTest,
	"AssetFactory.AssetDocument.AnimMontage.RejectsInvalidBodySectionTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageRejectsInvalidBodySectionTypesTest::RunTest(const FString& Parameters)
{
	const TArray<FString> ArraySections = {
		TEXT("SlotAnimTracks"),
		TEXT("CompositeSections"),
		TEXT("Notifies"),
		TEXT("NotifyStates"),
	};

	for (const FString& Section : ArraySections)
	{
		TSharedPtr<FJsonObject> Document = MakeMontageDocument(FString::Printf(TEXT("/Game/AssetDocumentTests/AM_Invalid_%s"), *Section));
		Document->GetObjectField(TEXT("Body"))->SetStringField(Section, TEXT("not an array"));

		const FAssetDocumentResult Result = ValidateDocument(Document);
		TestFalse(FString::Printf(TEXT("Validate rejects non-array %s"), *Section), Result.IsSuccess());
		TestTrue(FString::Printf(TEXT("Validate reports InvalidBodySectionType for %s"), *Section), HasDiagnostic(Result, FString::Printf(TEXT("/Body/%s"), *Section), TEXT("InvalidBodySectionType")));
	}

	for (const FString& Section : {FString(TEXT("Skeleton")), FString(TEXT("PreviewMesh"))})
	{
		TSharedPtr<FJsonObject> Document = MakeMontageDocument(FString::Printf(TEXT("/Game/AssetDocumentTests/AM_Invalid_%s"), *Section));
		Document->GetObjectField(TEXT("Body"))->SetStringField(Section, TEXT("not an object"));

		const FAssetDocumentResult Result = ValidateDocument(Document);
		TestFalse(FString::Printf(TEXT("Validate rejects string %s"), *Section), Result.IsSuccess());
		TestTrue(FString::Printf(TEXT("Validate reports InvalidBodySectionType for %s"), *Section), HasDiagnostic(Result, FString::Printf(TEXT("/Body/%s"), *Section), TEXT("InvalidBodySectionType")));
	}

	{
		TSharedPtr<FJsonObject> Document = MakeMontageDocument(TEXT("/Game/AssetDocumentTests/AM_InvalidBlend"));
		Document->GetObjectField(TEXT("Body"))->SetStringField(TEXT("Blend"), TEXT("not an object"));

		const FAssetDocumentResult Result = ValidateDocument(Document);
		TestFalse(TEXT("Validate rejects non-object Blend"), Result.IsSuccess());
		TestTrue(TEXT("Validate reports InvalidBodySectionType for Blend"), HasDiagnostic(Result, TEXT("/Body/Blend"), TEXT("InvalidBodySectionType")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageRegisteredProfileSchemaTest,
	"AssetFactory.AssetDocument.AnimMontage.RegisteredProfileSchema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimMontageRegisteredProfileSchemaTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentService Service;
	const FAssetDocumentResult Result = Service.GetSchema();
	TestTrue(TEXT("GetSchema succeeds"), Result.IsSuccess());
	TestTrue(TEXT("GetSchema returns payload"), Result.Payload.IsValid());
	if (!Result.Payload.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* RegisteredProfiles = nullptr;
	TestTrue(TEXT("Schema includes registered_profiles"), Result.Payload->TryGetArrayField(TEXT("registered_profiles"), RegisteredProfiles));
	if (!RegisteredProfiles)
	{
		return false;
	}

	bool bFoundAnimMontageProfile = false;
	for (const TSharedPtr<FJsonValue>& Entry : *RegisteredProfiles)
	{
		const TSharedPtr<FJsonObject> EntryObject = Entry.IsValid() ? Entry->AsObject() : nullptr;
		if (!EntryObject.IsValid() || EntryObject->GetStringField(TEXT("Class")) != TEXT("/Script/Engine.AnimMontage"))
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* BodySections = nullptr;
		if (EntryObject->TryGetArrayField(TEXT("BodySections"), BodySections) && BodySections && HasExpectedBodySections(*BodySections))
		{
			bFoundAnimMontageProfile = true;
			break;
		}
	}

	TestTrue(TEXT("Schema registered_profiles includes AnimMontage body sections"), bFoundAnimMontageProfile);

	return true;
}

#endif
