// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentService.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/AutomationTest.h"
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

#endif
