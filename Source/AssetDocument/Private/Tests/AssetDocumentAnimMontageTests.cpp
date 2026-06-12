// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentService.h"

#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
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
	const FAssetDocumentService Service;
	FAssetDocumentValidateRequest Request;
	Request.Document = Document;
	return Service.Validate(Request);
}
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
