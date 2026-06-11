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
	const TArray<FString> Targets = { InvalidClassTarget, AbstractClassTarget, CreateTarget, UpdateTarget, TypedSubtypeTarget };

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

	for (const FString& Target : Targets)
	{
		CleanupTestAsset(Target);
	}

	return true;
}

#endif
