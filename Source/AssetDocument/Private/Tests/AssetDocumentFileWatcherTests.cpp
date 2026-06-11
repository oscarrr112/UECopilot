// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentFileWatcher.h"

#include "AssetDocumentService.h"
#include "TestDataAsset.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
FString GetObjectPath(const FString& Target)
{
	return FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
}

void CleanupWatcherTestAsset(const FString& Target)
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

TSharedPtr<FJsonObject> MakeApplyDocument(const FString& Target)
{
	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("AssetType"), TEXT("GenericAsset"));
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), TEXT("TestDataAsset"));
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	return Document;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentFileWatcherHelpersTest,
	"AssetFactory.AssetDocument.FileWatcher.Helpers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentFileWatcherHelpersTest::RunTest(const FString& Parameters)
{
	const FString TestFolder = FString::Printf(TEXT("AssetDocumentWatcherTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString TestDirectory = FPaths::Combine(FPaths::ProjectContentDir(), TestFolder);
	const FString SidecarPath = FPaths::Combine(TestDirectory, TEXT("DA_Watched.assetdoc.json"));
	const FString NestedDirectory = FPaths::Combine(TestDirectory, TEXT("Nested"));
	const FString NestedSidecarPath = FPaths::Combine(NestedDirectory, TEXT("DA_Nested.assetdoc.json"));
	const FString LegacySidecarPath = FPaths::Combine(TestDirectory, TEXT("DA_Legacy.assetdocument.json"));
	const FString NonSidecarPath = FPaths::Combine(TestDirectory, TEXT("DA_Watched.json"));

	IFileManager& FileManager = IFileManager::Get();
	TestTrue(TEXT("Creates watcher test directory"), FileManager.MakeDirectory(*TestDirectory, true));
	TestTrue(TEXT("Creates nested watcher test directory"), FileManager.MakeDirectory(*NestedDirectory, true));

	TestTrue(TEXT("Recognizes .assetdoc.json sidecar path"), FAssetDocumentFileWatcher::IsAssetDocumentSidecarPath(SidecarPath));
	TestFalse(TEXT("Rejects non-sidecar JSON path"), FAssetDocumentFileWatcher::IsAssetDocumentSidecarPath(NonSidecarPath));

	FAssetDocumentWatchedFileState FirstState;
	TestFalse(TEXT("Missing file has no watched state"), FAssetDocumentFileWatcher::TryReadFileState(SidecarPath, FirstState));

	TestTrue(TEXT("Writes watcher sidecar fixture"), FFileHelper::SaveStringToFile(TEXT("{\"SchemaVersion\":1}"), *SidecarPath));
	TestTrue(TEXT("Reads initial watched file state"), FAssetDocumentFileWatcher::TryReadFileState(SidecarPath, FirstState));
	TestTrue(TEXT("State is stable when size and timestamp are unchanged"), FAssetDocumentFileWatcher::IsStableFileState(SidecarPath, FirstState));

	TestTrue(TEXT("Updates watcher sidecar fixture"), FFileHelper::SaveStringToFile(TEXT("{\"SchemaVersion\":1,\"Changed\":true}"), *SidecarPath));
	TestFalse(TEXT("State is not stable after file changes"), FAssetDocumentFileWatcher::IsStableFileState(SidecarPath, FirstState));

	TestTrue(TEXT("Writes nested watcher sidecar fixture"), FFileHelper::SaveStringToFile(TEXT("{\"SchemaVersion\":1}"), *NestedSidecarPath));
	TestTrue(TEXT("Writes legacy sidecar fixture"), FFileHelper::SaveStringToFile(TEXT("{\"SchemaVersion\":1}"), *LegacySidecarPath));
	TestTrue(TEXT("Writes non-sidecar fixture"), FFileHelper::SaveStringToFile(TEXT("{\"SchemaVersion\":1}"), *NonSidecarPath));

	TArray<FString> ScannedSidecars;
	FAssetDocumentFileWatcher::CollectAssetDocumentSidecarsUnderDirectory(TestDirectory, ScannedSidecars);
	TestTrue(TEXT("Rescan helper finds top-level sidecar"), ScannedSidecars.Contains(FPaths::ConvertRelativePathToFull(SidecarPath)));
	TestTrue(TEXT("Rescan helper finds nested sidecar"), ScannedSidecars.Contains(FPaths::ConvertRelativePathToFull(NestedSidecarPath)));
	TestFalse(TEXT("Rescan helper ignores legacy extension"), ScannedSidecars.Contains(FPaths::ConvertRelativePathToFull(LegacySidecarPath)));
	TestFalse(TEXT("Rescan helper ignores non-sidecar JSON"), ScannedSidecars.Contains(FPaths::ConvertRelativePathToFull(NonSidecarPath)));

	TestTrue(TEXT("Cleans up watcher helper directory"), FileManager.DeleteDirectory(*TestDirectory, false, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentFileWatcherDirtySkipTest,
	"AssetFactory.AssetDocument.FileWatcher.DirtySkip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentFileWatcherDirtySkipTest::RunTest(const FString& Parameters)
{
	const FString Target = TEXT("/Game/AssetDocumentWatcherTests/DA_DirtySkip");
	CleanupWatcherTestAsset(Target);

	FAssetDocumentService Service;
	FAssetDocumentApplyRequest Request;
	Request.Document = MakeApplyDocument(Target);
	Request.bSaveAsset = true;
	Request.Document->GetObjectField(TEXT("Properties"))->SetStringField(TEXT("TestString"), TEXT("before dirty"));

	const FAssetDocumentResult CreateResult = Service.Apply(Request);
	TestTrue(TEXT("Creates dirty-skip fixture asset"), CreateResult.IsSuccess());

	UPackage* Package = FindPackage(nullptr, *Target);
	TestNotNull(TEXT("Dirty-skip fixture package exists"), Package);
	if (Package)
	{
		Package->MarkPackageDirty();
	}

	FString Warning;
	TestTrue(TEXT("Watcher protects dirty package from auto apply"), FAssetDocumentFileWatcher::ShouldSkipAutoApplyForDirtyPackage(Target, Warning));
	TestTrue(TEXT("Dirty skip reports warning text"), Warning.Contains(TEXT("dirty"), ESearchCase::IgnoreCase));

	if (Package)
	{
		Package->ClearDirtyFlag();
	}
	Warning.Reset();
	TestFalse(TEXT("Watcher does not skip clean package"), FAssetDocumentFileWatcher::ShouldSkipAutoApplyForDirtyPackage(Target, Warning));

	CleanupWatcherTestAsset(Target);
	return true;
}

#endif
