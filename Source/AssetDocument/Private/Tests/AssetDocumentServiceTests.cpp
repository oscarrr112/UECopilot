// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentService.h"

#include "TestDataAsset.h"

#include "Dom/JsonValue.h"
#include "ObjectTools.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
TSharedPtr<FJsonObject> MakeGenericAssetDocument(const FString& Target)
{
	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("AssetType"), TEXT("GenericAsset"));
	if (!Target.IsEmpty())
	{
		Document->SetStringField(TEXT("Target"), Target);
	}
	return Document;
}

TSharedPtr<FJsonObject> MakeApplyDocument(const FString& Target, const FString& ClassName, const FString& Action)
{
	TSharedPtr<FJsonObject> Document = MakeGenericAssetDocument(Target);
	Document->SetStringField(TEXT("Class"), ClassName);
	Document->SetStringField(TEXT("Action"), Action);
	Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	return Document;
}

void SetProperty(TSharedPtr<FJsonObject> Document, const FString& PropertyName, TSharedPtr<FJsonValue> Value)
{
	TSharedPtr<FJsonObject> Properties = Document->GetObjectField(TEXT("Properties"));
	Properties->SetField(PropertyName, Value);
}

FString GetObjectPath(const FString& Target)
{
	return FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
}

void CleanupTestAsset(const FString& Target)
{
	const FString ObjectPath = GetObjectPath(Target);
	const FString PackageFileName = FPackageName::LongPackageNameToFilename(Target, FPackageName::GetAssetPackageExtension());

	UObject* ExistingAsset = FindObject<UObject>(nullptr, *ObjectPath);
	if (!ExistingAsset && IFileManager::Get().FileExists(*PackageFileName))
	{
		ExistingAsset = LoadObject<UObject>(nullptr, *ObjectPath);
	}

	if (ExistingAsset)
	{
		TArray<UObject*> ObjectsToDelete;
		ObjectsToDelete.Add(ExistingAsset);
		ObjectTools::DeleteObjectsUnchecked(ObjectsToDelete);
	}

	if (IFileManager::Get().FileExists(*PackageFileName))
	{
		IFileManager::Get().Delete(*PackageFileName, false, true);
	}
}

TSharedPtr<FJsonObject> FindObjectByStringField(const TArray<TSharedPtr<FJsonValue>>& Values, const FString& FieldName, const FString& FieldValue)
{
	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		if (!Value.IsValid() || Value->Type != EJson::Object)
		{
			continue;
		}

		TSharedPtr<FJsonObject> Object = Value->AsObject();
		if (Object.IsValid() && Object->GetStringField(FieldName) == FieldValue)
		{
			return Object;
		}
	}
	return nullptr;
}

bool HasArrayField(TSharedPtr<FJsonObject> Object, const FString& FieldName)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	return Object.IsValid() && Object->TryGetArrayField(FieldName, Values) && Values;
}

TSharedPtr<FJsonObject> GetPayloadObject(TSharedPtr<FJsonObject> Payload, const FString& FieldName)
{
	const TSharedPtr<FJsonObject>* Object = nullptr;
	if (Payload.IsValid() && Payload->TryGetObjectField(FieldName, Object) && Object)
	{
		return *Object;
	}
	return nullptr;
}

bool WriteJsonObjectToFile(TSharedPtr<FJsonObject> Document, const FString& FilePath)
{
	FString JsonText;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	if (!FJsonSerializer::Serialize(Document.ToSharedRef(), Writer))
	{
		return false;
	}
	return FFileHelper::SaveStringToFile(JsonText, *FilePath);
}

FString GetTestSidecarPath()
{
	FString FilePath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/DA_Test.assetdoc.json"));
	FPaths::NormalizeFilename(FilePath);
	return FilePath;
}

FString MakeUniqueTestFolderName()
{
	return FString::Printf(TEXT("AssetDocumentSidecarTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
}

FString MakeSidecarJson(const FString& Target)
{
	return FString::Printf(
		TEXT("{\n")
		TEXT("\t\"SchemaVersion\": 1,\n")
		TEXT("\t\"AssetType\": \"GenericAsset\",\n")
		TEXT("\t\"Target\": \"%s\"\n")
		TEXT("}\n"),
		*Target);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentReadTest,
	"AssetFactory.AssetDocument.Read",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentReadTest::RunTest(const FString& Parameters)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/DA_ReadSide");
	const FString MissingTarget = TEXT("/Game/AssetDocumentTests/DA_ReadMissing");
	const FString ObjectPath = GetObjectPath(Target);
	const FString SidecarPath = FPackageName::LongPackageNameToFilename(Target, TEXT(".assetdoc.json"));
	CleanupTestAsset(Target);
	IFileManager::Get().Delete(*SidecarPath, false, true);

	FAssetDocumentService Service;

	FAssetDocumentApplyRequest CreateRequest;
	CreateRequest.Document = MakeApplyDocument(Target, TEXT("/Script/AssetFactory.TestDataAsset"), TEXT("Create"));
	SetProperty(CreateRequest.Document, TEXT("TestString"), MakeShared<FJsonValueString>(TEXT("read-side")));
	SetProperty(CreateRequest.Document, TEXT("TestInt"), MakeShared<FJsonValueNumber>(12));

	const FAssetDocumentResult CreateResult = Service.Apply(CreateRequest);
	TestTrue(TEXT("Creates fixture asset for read-side tests"), CreateResult.IsSuccess());

	UPackage* Package = FindPackage(nullptr, *Target);
	UTestDataAsset* Asset = LoadObject<UTestDataAsset>(nullptr, *ObjectPath);
	TestNotNull(TEXT("Read-side fixture asset loads"), Asset);
	if (!Package && Asset)
	{
		Package = Asset->GetOutermost();
	}
	if (Package)
	{
		Package->ClearDirtyFlag();
	}

	{
		FAssetDocumentInspectRequest Request;
		Request.ClassOrAsset = Target;

		const FAssetDocumentResult Result = Service.Inspect(Request);

		TestTrue(TEXT("Inspect succeeds for a TestDataAsset asset path"), Result.IsSuccess());
		TestTrue(TEXT("Inspect returns payload"), Result.Payload.IsValid());
		const TArray<TSharedPtr<FJsonValue>>* Properties = nullptr;
		TestTrue(TEXT("Inspect payload has properties array"), Result.Payload.IsValid() && Result.Payload->TryGetArrayField(TEXT("properties"), Properties));
		TestTrue(TEXT("Inspect payload has skipped array"), HasArrayField(Result.Payload, TEXT("skipped")));
		if (Properties)
		{
			TSharedPtr<FJsonObject> StringRow = FindObjectByStringField(*Properties, TEXT("name"), TEXT("TestString"));
			TestTrue(TEXT("Inspect includes TestString row"), StringRow.IsValid());
			if (StringRow.IsValid())
			{
				TestEqual(TEXT("Inspect row has name"), StringRow->GetStringField(TEXT("name")), FString(TEXT("TestString")));
				TestFalse(TEXT("Inspect row has ue_type"), StringRow->GetStringField(TEXT("ue_type")).IsEmpty());
				TestEqual(TEXT("Inspect row has type_token"), StringRow->GetStringField(TEXT("type_token")), FString(TEXT("String")));
				TestTrue(TEXT("Inspect row has current_value"), StringRow->HasField(TEXT("current_value")));
				TestTrue(TEXT("Inspect row has default_value"), StringRow->HasField(TEXT("default_value")));
				TestTrue(TEXT("Inspect row is writable"), StringRow->GetBoolField(TEXT("writable")));
				TestEqual(TEXT("Inspect current value comes from asset"), StringRow->GetStringField(TEXT("current_value")), FString(TEXT("read-side")));
				TestEqual(TEXT("Inspect default value comes from CDO"), StringRow->GetStringField(TEXT("default_value")), FString());
			}

			TSharedPtr<FJsonObject> IntRow = FindObjectByStringField(*Properties, TEXT("name"), TEXT("TestInt"));
			TestTrue(TEXT("Inspect includes TestInt row"), IntRow.IsValid());
			if (IntRow.IsValid())
			{
				TestEqual(TEXT("Inspect maps integer token"), IntRow->GetStringField(TEXT("type_token")), FString(TEXT("Int")));
			}
		}
		TestFalse(TEXT("Inspect does not dirty asset package"), Package && Package->IsDirty());
	}

	{
		FAssetDocumentExtractRequest Request;
		Request.AssetPath = Target;
		Request.bDiffOnly = true;

		const FAssetDocumentResult Result = Service.Extract(Request);

		TestTrue(TEXT("Extract succeeds for TestDataAsset"), Result.IsSuccess());
		TestTrue(TEXT("Extract returns draft payload"), Result.Payload.IsValid());
		if (Result.Payload.IsValid())
		{
			TestEqual(TEXT("Extract draft schema version"), Result.Payload->GetNumberField(TEXT("SchemaVersion")), 1.0);
			TestEqual(TEXT("Extract draft asset type"), Result.Payload->GetStringField(TEXT("AssetType")), FString(TEXT("GenericAsset")));
			TestEqual(TEXT("Extract draft target"), Result.Payload->GetStringField(TEXT("Target")), Target);
			TestEqual(TEXT("Extract draft class"), Result.Payload->GetStringField(TEXT("Class")), FString(TEXT("/Script/AssetFactory.TestDataAsset")));

			TSharedPtr<FJsonObject> Properties = GetPayloadObject(Result.Payload, TEXT("Properties"));
			TestTrue(TEXT("Extract draft has Properties object"), Properties.IsValid());
			if (Properties.IsValid())
			{
				TestTrue(TEXT("Extract diff-only includes changed string"), Properties->HasField(TEXT("TestString")));
				TestFalse(TEXT("Extract diff-only omits default float"), Properties->HasField(TEXT("TestFloat")));
			}
		}
		TestFalse(TEXT("Extract does not dirty asset package"), Package && Package->IsDirty());
	}

	{
		const FDateTime TimestampBefore = IFileManager::Get().GetTimeStamp(*FPackageName::LongPackageNameToFilename(Target, FPackageName::GetAssetPackageExtension()));

		FAssetDocumentValidateRequest Request;
		Request.Document = MakeGenericAssetDocument(Target);

		const FAssetDocumentResult Result = Service.Validate(Request);

		const FDateTime TimestampAfter = IFileManager::Get().GetTimeStamp(*FPackageName::LongPackageNameToFilename(Target, FPackageName::GetAssetPackageExtension()));
		TestTrue(TEXT("Validate succeeds for inline document"), Result.IsSuccess());
		TestFalse(TEXT("Validate does not dirty asset package"), Package && Package->IsDirty());
		TestEqual(TEXT("Validate does not save asset package"), TimestampAfter, TimestampBefore);
	}

	{
		TSharedPtr<FJsonObject> DiffDocument = MakeGenericAssetDocument(Target);
		DiffDocument->SetStringField(TEXT("Class"), TEXT("/Script/AssetFactory.TestDataAsset"));
		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
		Properties->SetStringField(TEXT("TestString"), TEXT("after-diff"));
		Properties->SetNumberField(TEXT("TestInt"), 12);
		Properties->SetStringField(TEXT("DefinitelyMissing"), TEXT("missing"));
		DiffDocument->SetObjectField(TEXT("Properties"), Properties);
		TestTrue(TEXT("Writes temporary diff sidecar"), WriteJsonObjectToFile(DiffDocument, SidecarPath));

		FAssetDocumentDiffRequest Request;
		Request.FilePath = SidecarPath;

		const FAssetDocumentResult Result = Service.Diff(Request);

		TestTrue(TEXT("Diff succeeds for TestDataAsset sidecar"), Result.IsSuccess());
		TestTrue(TEXT("Diff returns payload"), Result.Payload.IsValid());
		if (Result.Payload.IsValid())
		{
			TestTrue(TEXT("Diff payload has changed array"), HasArrayField(Result.Payload, TEXT("changed")));
			TestTrue(TEXT("Diff payload has unchanged array"), HasArrayField(Result.Payload, TEXT("unchanged")));
			TestTrue(TEXT("Diff payload has skipped array"), HasArrayField(Result.Payload, TEXT("skipped")));
			TestTrue(TEXT("Diff payload has failed array"), HasArrayField(Result.Payload, TEXT("failed")));

			const TArray<TSharedPtr<FJsonValue>>* Changed = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Unchanged = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Failed = nullptr;
			Result.Payload->TryGetArrayField(TEXT("changed"), Changed);
			Result.Payload->TryGetArrayField(TEXT("unchanged"), Unchanged);
			Result.Payload->TryGetArrayField(TEXT("failed"), Failed);
			TestTrue(TEXT("Diff reports changed property"), Changed && FindObjectByStringField(*Changed, TEXT("name"), TEXT("TestString")).IsValid());
			TestTrue(TEXT("Diff reports unchanged property"), Unchanged && FindObjectByStringField(*Unchanged, TEXT("name"), TEXT("TestInt")).IsValid());
			TestTrue(TEXT("Diff reports failed missing property"), Failed && FindObjectByStringField(*Failed, TEXT("name"), TEXT("DefinitelyMissing")).IsValid());
		}
		TestFalse(TEXT("Diff does not dirty asset package"), Package && Package->IsDirty());
	}

	IFileManager::Get().Delete(*SidecarPath, false, true);
	CleanupTestAsset(Target);
	CleanupTestAsset(MissingTarget);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarValidationTest,
	"AssetFactory.AssetDocument.Sidecar.Validate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarValidationTest::RunTest(const FString& Parameters)
{
	const FString SidecarPath = GetTestSidecarPath();
	const FAssetDocumentService Service;

	{
		FAssetDocumentValidateRequest Request;
		Request.FilePath = SidecarPath;
		Request.Document = MakeGenericAssetDocument(TEXT(""));

		const FAssetDocumentResult Result = Service.Validate(Request);

		TestFalse(TEXT("Missing Target fails when validating a sidecar file"), Result.IsSuccess());
		TestTrue(TEXT("Missing Target reports a Target validation error"), Result.Message.Contains(TEXT("Target")));
	}

	{
		FAssetDocumentValidateRequest Request;
		Request.FilePath = SidecarPath;
		Request.Document = MakeGenericAssetDocument(TEXT("/Game/Data/DA_Other"));

		const FAssetDocumentResult Result = Service.Validate(Request);

		TestFalse(TEXT("Target mismatch fails when sidecar path resolves to a different object path"), Result.IsSuccess());
		TestTrue(TEXT("Target mismatch reports the expected sidecar object path"), Result.Message.Contains(TEXT("/Game/Data/DA_Test")));
	}

	{
		FAssetDocumentValidateRequest Request;
		Request.FilePath = SidecarPath;
		Request.Document = MakeGenericAssetDocument(TEXT("/Game/Data/DA_Test"));

		const FAssetDocumentResult Result = Service.Validate(Request);

		TestTrue(TEXT("Matching Target succeeds for a sidecar file"), Result.IsSuccess());
		TestEqual(TEXT("Matching sidecar result target"), Result.Target, FString(TEXT("/Game/Data/DA_Test")));
		TestTrue(TEXT("Matching sidecar result includes payload"), Result.Payload.IsValid());
		if (Result.Payload.IsValid())
		{
			TestEqual(TEXT("Payload target is normalized"), Result.Payload->GetStringField(TEXT("target")), FString(TEXT("/Game/Data/DA_Test")));
			TestEqual(TEXT("Payload sidecar path is normalized"), Result.Payload->GetStringField(TEXT("sidecar_file_path")), SidecarPath);
		}
	}

	{
		FAssetDocumentValidateRequest Request;
		Request.Document = MakeGenericAssetDocument(TEXT(""));

		const FAssetDocumentResult Result = Service.Validate(Request);

		TestTrue(TEXT("Inline document without FilePath does not require Target path matching"), Result.IsSuccess());
		TestTrue(TEXT("Inline document result includes payload"), Result.Payload.IsValid());
	}

	{
		const FString TestFolder = MakeUniqueTestFolderName();
		const FString Target = FString::Printf(TEXT("/Game/%s/DA_FileLoad"), *TestFolder);
		FString TestDirectory = FPaths::Combine(FPaths::ProjectContentDir(), TestFolder);
		FString FilePath = FPaths::Combine(TestDirectory, TEXT("DA_FileLoad.assetdoc.json"));
		FPaths::NormalizeFilename(TestDirectory);
		FPaths::NormalizeFilename(FilePath);

		IFileManager& FileManager = IFileManager::Get();
		TestTrue(TEXT("Creates temporary sidecar test directory"), FileManager.MakeDirectory(*TestDirectory, true));
		TestTrue(TEXT("Writes temporary sidecar document"), FFileHelper::SaveStringToFile(MakeSidecarJson(Target), *FilePath));

		FAssetDocumentValidateRequest Request;
		Request.FilePath = FilePath;

		const FAssetDocumentResult Result = Service.Validate(Request);

		TestTrue(TEXT("Validate loads a sidecar document from disk when Document is null"), Result.IsSuccess());
		TestEqual(TEXT("File-loaded sidecar result target"), Result.Target, Target);
		TestTrue(TEXT("File-loaded sidecar result includes payload"), Result.Payload.IsValid());
		if (Result.Payload.IsValid())
		{
			TestEqual(TEXT("File-loaded payload target is normalized"), Result.Payload->GetStringField(TEXT("target")), Target);
			TestEqual(TEXT("File-loaded payload sidecar path is normalized"), Result.Payload->GetStringField(TEXT("sidecar_file_path")), FilePath);
		}

		TestTrue(TEXT("Cleans up temporary sidecar test directory"), FileManager.DeleteDirectory(*TestDirectory, false, true));
	}

	{
		const FString TestFolder = MakeUniqueTestFolderName();
		FString MissingFilePath = FPaths::Combine(FPaths::ProjectContentDir(), TestFolder, TEXT("Missing.assetdoc.json"));
		FPaths::NormalizeFilename(MissingFilePath);

		FAssetDocumentValidateRequest Request;
		Request.FilePath = MissingFilePath;

		const FAssetDocumentResult Result = Service.Validate(Request);

		TestFalse(TEXT("Validate fails when requested sidecar file is missing"), Result.IsSuccess());
		TestTrue(TEXT("Missing sidecar file reports a read failure"), Result.Message.Contains(TEXT("Failed to read JSON file")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyTest,
	"AssetFactory.AssetDocument.Apply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyTest::RunTest(const FString& Parameters)
{
	const FString InvalidClassTarget = TEXT("/Game/AssetDocumentTests/DA_InvalidClass");
	const FString AbstractClassTarget = TEXT("/Game/AssetDocumentTests/DA_AbstractClass");
	const FString CreateTarget = TEXT("/Game/AssetDocumentTests/DA_CreateSucceeds");
	const FString UpdateTarget = TEXT("/Game/AssetDocumentTests/DA_UpdatePreserves");
	const FString TypedSubtypeTarget = TEXT("/Game/AssetDocumentTests/DA_TypedSubtypeFails");
	const FString FailedSubtypeRetryTarget = TEXT("/Game/AssetDocumentTests/DA_FailedSubtypeRetry");
	const FString FailedUnknownPropertyRetryTarget = TEXT("/Game/AssetDocumentTests/DA_FailedUnknownPropertyRetry");
	const FString TypedMismatchTarget = TEXT("/Game/AssetDocumentTests/DA_TypedMismatchFails");
	const TArray<FString> Targets = {
		InvalidClassTarget,
		AbstractClassTarget,
		CreateTarget,
		UpdateTarget,
		TypedSubtypeTarget,
		FailedSubtypeRetryTarget,
		FailedUnknownPropertyRetryTarget,
		TypedMismatchTarget
	};

	for (const FString& Target : Targets)
	{
		CleanupTestAsset(Target);
	}

	FAssetDocumentService Service;

	{
		FAssetDocumentApplyRequest Request;
		Request.Document = MakeApplyDocument(InvalidClassTarget, TEXT("DefinitelyMissingAssetDocumentClass"), TEXT("Create"));

		const FAssetDocumentResult Result = Service.Apply(Request);

		TestFalse(TEXT("Invalid class fails"), Result.IsSuccess());
		TestTrue(TEXT("Invalid class reports class resolution failure"), Result.Message.Contains(TEXT("class"), ESearchCase::IgnoreCase));
	}

	{
		FAssetDocumentApplyRequest Request;
		Request.Document = MakeApplyDocument(AbstractClassTarget, TEXT("DataAsset"), TEXT("Create"));

		const FAssetDocumentResult Result = Service.Apply(Request);

		TestFalse(TEXT("Abstract class fails"), Result.IsSuccess());
		TestTrue(TEXT("Abstract class reports abstract rejection"), Result.Message.Contains(TEXT("abstract"), ESearchCase::IgnoreCase));
	}

	{
		FAssetDocumentApplyRequest Request;
		Request.Document = MakeApplyDocument(CreateTarget, TEXT("TestDataAsset"), TEXT("Create"));
		SetProperty(Request.Document, TEXT("TestString"), MakeShared<FJsonValueString>(TEXT("hello")));
		SetProperty(Request.Document, TEXT("TestInt"), MakeShared<FJsonValueNumber>(42));
		SetProperty(Request.Document, TEXT("TestFloat"), MakeShared<FJsonValueNumber>(3.5));
		SetProperty(Request.Document, TEXT("bTestBool"), MakeShared<FJsonValueBoolean>(true));
		SetProperty(Request.Document, TEXT("TestName"), MakeShared<FJsonValueString>(TEXT("CreatedName")));
		SetProperty(Request.Document, TEXT("TestText"), MakeShared<FJsonValueString>(TEXT("CreatedText")));

		const FAssetDocumentResult Result = Service.Apply(Request);

		TestTrue(TEXT("Create TestDataAsset succeeds"), Result.IsSuccess());
		TestTrue(TEXT("Create saves asset"), Result.bSavedAsset);

		UTestDataAsset* Asset = LoadObject<UTestDataAsset>(nullptr, *GetObjectPath(CreateTarget));
		TestNotNull(TEXT("Created TestDataAsset loads"), Asset);
		if (Asset)
		{
			TestEqual(TEXT("Created string property"), Asset->TestString, FString(TEXT("hello")));
			TestEqual(TEXT("Created int property"), Asset->TestInt, 42);
			TestEqual(TEXT("Created float property"), Asset->TestFloat, 3.5f);
			TestTrue(TEXT("Created bool property"), Asset->bTestBool);
			TestEqual(TEXT("Created name property"), Asset->TestName, FName(TEXT("CreatedName")));
			TestEqual(TEXT("Created text property"), Asset->TestText.ToString(), FString(TEXT("CreatedText")));
		}
	}

	{
		FAssetDocumentApplyRequest CreateRequest;
		CreateRequest.Document = MakeApplyDocument(UpdateTarget, TEXT("/Script/AssetFactory.TestDataAsset"), TEXT("Create"));
		SetProperty(CreateRequest.Document, TEXT("TestString"), MakeShared<FJsonValueString>(TEXT("before")));
		SetProperty(CreateRequest.Document, TEXT("TestInt"), MakeShared<FJsonValueNumber>(7));

		const FAssetDocumentResult CreateResult = Service.Apply(CreateRequest);
		TestTrue(TEXT("Create fixture for update succeeds"), CreateResult.IsSuccess());

		FAssetDocumentApplyRequest UpdateRequest;
		UpdateRequest.Document = MakeApplyDocument(UpdateTarget, TEXT("/Script/AssetFactory.TestDataAsset"), TEXT("Update"));
		SetProperty(UpdateRequest.Document, TEXT("TestString"), MakeShared<FJsonValueString>(TEXT("after")));

		const FAssetDocumentResult UpdateResult = Service.Apply(UpdateRequest);

		TestTrue(TEXT("Update one property succeeds"), UpdateResult.IsSuccess());

		UTestDataAsset* Asset = LoadObject<UTestDataAsset>(nullptr, *GetObjectPath(UpdateTarget));
		TestNotNull(TEXT("Updated TestDataAsset loads"), Asset);
		if (Asset)
		{
			TestEqual(TEXT("Updated specified property"), Asset->TestString, FString(TEXT("after")));
			TestEqual(TEXT("Update leaves unspecified property unchanged"), Asset->TestInt, 7);
		}
	}

	{
		FAssetDocumentApplyRequest Request;
		Request.Document = MakeApplyDocument(TypedSubtypeTarget, TEXT("TestDataAsset"), TEXT("Create"));

		TSharedPtr<FJsonObject> TypedValue = MakeShared<FJsonObject>();
		TypedValue->SetStringField(TEXT("type"), TEXT("Object:StaticMesh"));
		TypedValue->SetStringField(TEXT("value"), TEXT("/Game/MissingMesh"));
		SetProperty(Request.Document, TEXT("TestString"), MakeShared<FJsonValueObject>(TypedValue));

		const FAssetDocumentResult Result = Service.Apply(Request);

		TestFalse(TEXT("Typed type with subtype fails"), Result.IsSuccess());
		TestTrue(TEXT("Typed subtype reports unsupported typed subtype"), Result.Message.Contains(TEXT("type"), ESearchCase::IgnoreCase) || Result.Message.Contains(TEXT("subtype"), ESearchCase::IgnoreCase));
	}

	{
		FAssetDocumentApplyRequest BadRequest;
		BadRequest.Document = MakeApplyDocument(FailedSubtypeRetryTarget, TEXT("TestDataAsset"), TEXT("Create"));

		TSharedPtr<FJsonObject> TypedValue = MakeShared<FJsonObject>();
		TypedValue->SetStringField(TEXT("type"), TEXT("Object:StaticMesh"));
		TypedValue->SetStringField(TEXT("value"), TEXT("/Game/MissingMesh"));
		SetProperty(BadRequest.Document, TEXT("TestString"), MakeShared<FJsonValueObject>(TypedValue));

		const FAssetDocumentResult BadResult = Service.Apply(BadRequest);
		TestFalse(TEXT("Failed Create with bad typed subtype fails"), BadResult.IsSuccess());
		TestNull(TEXT("Failed Create with bad typed subtype leaves no live asset"), FindObject<UObject>(nullptr, *GetObjectPath(FailedSubtypeRetryTarget)));

		FAssetDocumentApplyRequest RetryRequest;
		RetryRequest.Document = MakeApplyDocument(FailedSubtypeRetryTarget, TEXT("TestDataAsset"), TEXT("Create"));
		SetProperty(RetryRequest.Document, TEXT("TestString"), MakeShared<FJsonValueString>(TEXT("retry ok")));

		const FAssetDocumentResult RetryResult = Service.Apply(RetryRequest);
		TestTrue(TEXT("Valid Create after bad typed subtype succeeds"), RetryResult.IsSuccess());
	}

	{
		FAssetDocumentApplyRequest BadRequest;
		BadRequest.Document = MakeApplyDocument(FailedUnknownPropertyRetryTarget, TEXT("TestDataAsset"), TEXT("Create"));
		SetProperty(BadRequest.Document, TEXT("DefinitelyUnknownProperty"), MakeShared<FJsonValueString>(TEXT("bad")));

		const FAssetDocumentResult BadResult = Service.Apply(BadRequest);
		TestFalse(TEXT("Failed Create with unknown property fails"), BadResult.IsSuccess());
		TestNull(TEXT("Failed Create with unknown property leaves no live asset"), FindObject<UObject>(nullptr, *GetObjectPath(FailedUnknownPropertyRetryTarget)));

		FAssetDocumentApplyRequest RetryRequest;
		RetryRequest.Document = MakeApplyDocument(FailedUnknownPropertyRetryTarget, TEXT("TestDataAsset"), TEXT("Create"));
		SetProperty(RetryRequest.Document, TEXT("TestString"), MakeShared<FJsonValueString>(TEXT("retry ok")));

		const FAssetDocumentResult RetryResult = Service.Apply(RetryRequest);
		TestTrue(TEXT("Valid Create after unknown property succeeds"), RetryResult.IsSuccess());
	}

	{
		FAssetDocumentApplyRequest Request;
		Request.Document = MakeApplyDocument(TEXT("AssetDocumentTests/DA_InvalidTarget"), TEXT("TestDataAsset"), TEXT("Create"));
		SetProperty(Request.Document, TEXT("TestString"), MakeShared<FJsonValueString>(TEXT("bad target")));

		const FAssetDocumentResult Result = Service.Apply(Request);

		TestFalse(TEXT("Invalid target path fails"), Result.IsSuccess());
		TestTrue(TEXT("Invalid target path reports target/package validation"), Result.Message.Contains(TEXT("Target")) || Result.Message.Contains(TEXT("package"), ESearchCase::IgnoreCase));
		TestNull(TEXT("Invalid target path does not create a package"), FindPackage(nullptr, TEXT("AssetDocumentTests/DA_InvalidTarget")));
	}

	{
		FAssetDocumentApplyRequest Request;
		Request.Document = MakeApplyDocument(TypedMismatchTarget, TEXT("TestDataAsset"), TEXT("Create"));

		TSharedPtr<FJsonObject> TypedValue = MakeShared<FJsonObject>();
		TypedValue->SetStringField(TEXT("type"), TEXT("String"));
		TypedValue->SetNumberField(TEXT("value"), 5);
		SetProperty(Request.Document, TEXT("TestInt"), MakeShared<FJsonValueObject>(TypedValue));

		const FAssetDocumentResult Result = Service.Apply(Request);

		TestFalse(TEXT("Typed type mismatch without subtype fails"), Result.IsSuccess());
		TestTrue(TEXT("Typed type mismatch reports diagnostic"), Result.Message.Contains(TEXT("type"), ESearchCase::IgnoreCase) || Result.Message.Contains(TEXT("TestInt")));
		TestTrue(TEXT("Typed type mismatch returns diagnostics"), Result.Diagnostics.Num() > 0);
	}

	for (const FString& Target : Targets)
	{
		CleanupTestAsset(Target);
	}

	return true;
}

#endif
