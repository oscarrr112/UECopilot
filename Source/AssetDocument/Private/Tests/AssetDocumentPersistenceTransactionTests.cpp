// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentAtomicFile.h"
#include "AssetDocumentCanonicalJson.h"
#include "AssetDocumentEditorSync.h"
#include "AssetDocumentService.h"
#include "AssetDocumentServiceTestHooks.h"
#include "AssetDocumentSidecar.h"
#include "TestDataAsset.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "HAL/FileManager.h"
#include "IO/IoHash.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Linker.h"
#include "UObject/UObjectHash.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AssetDocumentPersistenceTransactionTests
{
FString PersistenceTestMakeTarget(const TCHAR* Stem)
{
	return FString::Printf(
		TEXT("/Game/AssetDocumentTests/Persistence/%s_%s"),
		Stem,
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
}

FString PersistenceTestToObjectPath(const FString& Target)
{
	return FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
}

FString PersistenceTestToPackagePath(const FString& Target)
{
	return FPackageName::LongPackageNameToFilename(Target, FPackageName::GetAssetPackageExtension());
}

void PersistenceTestCleanup(const FString& Target, const FString& SidecarPath = FString())
{
	const FString PackagePath = PersistenceTestToPackagePath(Target);
	const FString PackageBasePath = PackagePath.LeftChop(
		FCString::Strlen(TEXT(".uasset")));
	const TArray<FString> PackageSegmentPaths{
		PackagePath,
		PackageBasePath + TEXT(".uexp"),
		PackageBasePath + TEXT(".ubulk"),
		PackageBasePath + TEXT(".uptnl"),
		PackageBasePath + TEXT(".m.ubulk"),
		PackageBasePath + TEXT(".upayload")};
	auto DeletePackageSegments = [&PackageSegmentPaths]()
	{
		for (const FString& SegmentPath : PackageSegmentPaths)
		{
			IFileManager::Get().Delete(*SegmentPath, false, true);
		}
	};
	UObject* Asset = FindObject<UObject>(nullptr, *PersistenceTestToObjectPath(Target));
	if (!Asset && IFileManager::Get().FileExists(*PackagePath))
	{
		Asset = LoadObject<UObject>(nullptr, *PersistenceTestToObjectPath(Target));
	}
	if (Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		ResetLoaders(Package);
		if (!SidecarPath.IsEmpty())
		{
			FAssetDocumentEditorSync::FScopedSidecarWrite Guard(SidecarPath);
			IFileManager::Get().Delete(*SidecarPath, false, true);
		}
		DeletePackageSegments();
		// Test teardown must not call AssetDeleted while a queued directory
		// modification for the just-saved package can still be delivered. Remove
		// the whole package from the registry and retire the live asset directly;
		// this avoids CachedEmptyPackages and its false external-modification warning.
		FAssetRegistryModule::GetRegistry().PackageDeleted(Package);
		Asset->ClearFlags(RF_Public | RF_Standalone);
		Asset->MarkAsGarbage();
		Package->SetDirtyFlag(false);
	}
	else
	{
		DeletePackageSegments();
		if (!SidecarPath.IsEmpty())
		{
			FAssetDocumentEditorSync::FScopedSidecarWrite Guard(SidecarPath);
			IFileManager::Get().Delete(*SidecarPath, false, true);
		}
	}
}

TSharedPtr<FJsonObject> PersistenceTestMakeGenericDocument(
	const FString& Target,
	const FString& Action,
	const FString& Value,
	int32 Number)
{
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetStringField(TEXT("TestString"), Value);
	Properties->SetNumberField(TEXT("TestInt"), Number);

	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("AssetType"), TEXT("GenericAsset"));
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), TEXT("/Script/AssetFactory.TestDataAsset"));
	Document->SetStringField(TEXT("Action"), Action);
	Document->SetObjectField(TEXT("Properties"), Properties);
	return Document;
}

TSharedPtr<FJsonObject> PersistenceTestMakeBlackboardDocument(
	FAutomationTestBase& Test,
	FAssetDocumentService& Service,
	const FString& Target,
	const FString& Action,
	int32 DefaultValue)
{
	FAssetDocumentTemplateRequest TemplateRequest;
	TemplateRequest.Class = TEXT("/Script/AIModule.BlackboardData");
	TemplateRequest.Target = Target;
	const FAssetDocumentResult TemplateResult = Service.CreateTemplate(TemplateRequest);
	Test.TestTrue(TEXT("Blackboard persistence fixture template succeeds"), TemplateResult.IsSuccess());
	TSharedPtr<FJsonObject> Document = TemplateResult.Payload;
	if (!Document.IsValid())
	{
		return Document;
	}
	Document->SetStringField(TEXT("Action"), Action);

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetNumberField(TEXT("DefaultValue"), DefaultValue);
	TSharedPtr<FJsonObject> ClassRef = MakeShared<FJsonObject>();
	ClassRef->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ClassRef->SetStringField(TEXT("Path"), UBlackboardKeyType_Int::StaticClass()->GetPathName());
	TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
	Key->SetStringField(TEXT("Name"), TEXT("PersistenceKey"));
	Key->SetObjectField(TEXT("KeyTypeClass"), ClassRef);
	Key->SetObjectField(TEXT("KeyTypeProperties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Keys;
	Keys.Add(MakeShared<FJsonValueObject>(Key));
	Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Keys"), MoveTemp(Keys));
	return Document;
}

TSharedPtr<FJsonObject> PersistenceTestMakePosition(double X, double Y)
{
	TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
	Position->SetNumberField(TEXT("X"), X);
	Position->SetNumberField(TEXT("Y"), Y);
	return Position;
}

TSharedPtr<FJsonObject> PersistenceTestMakeBehaviorNode(
	const FString& Id,
	const FString& ClassPath,
	const FString& NodeName,
	double X,
	double Y,
	TArray<TSharedPtr<FJsonValue>> Children = {})
{
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetStringField(TEXT("NodeName"), NodeName);
	TSharedPtr<FJsonObject> Editor = MakeShared<FJsonObject>();
	Editor->SetObjectField(TEXT("Position"), PersistenceTestMakePosition(X, Y));
	TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("Id"), Id);
	Node->SetStringField(TEXT("Class"), ClassPath);
	Node->SetObjectField(TEXT("Properties"), Properties);
	Node->SetArrayField(TEXT("Decorators"), {});
	Node->SetArrayField(TEXT("Services"), {});
	Node->SetArrayField(TEXT("Children"), MoveTemp(Children));
	Node->SetObjectField(TEXT("Editor"), Editor);
	return Node;
}

TSharedPtr<FJsonObject> PersistenceTestMakeBehaviorTreeDocument(
	FAutomationTestBase& Test,
	FAssetDocumentService& Service,
	const FString& Target,
	const FString& Action)
{
	FAssetDocumentTemplateRequest TemplateRequest;
	TemplateRequest.Class = TEXT("/Script/AIModule.BehaviorTree");
	TemplateRequest.Target = Target;
	const FAssetDocumentResult TemplateResult = Service.CreateTemplate(TemplateRequest);
	Test.TestTrue(TEXT("BehaviorTree persistence fixture template succeeds"), TemplateResult.IsSuccess());
	TSharedPtr<FJsonObject> Document = TemplateResult.Payload;
	if (!Document.IsValid())
	{
		return Document;
	}
	Document->SetStringField(TEXT("Action"), Action);
	TArray<TSharedPtr<FJsonValue>> Children;
	Children.Add(MakeShared<FJsonValueObject>(PersistenceTestMakeBehaviorNode(
		FGuid::NewGuid().ToString(EGuidFormats::Digits),
		TEXT("/Script/AIModule.BTTask_Wait"),
		TEXT("Persistence wait"),
		100.0,
		300.0)));
	TSharedPtr<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetStringField(TEXT("GraphGuid"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
	Tree->SetObjectField(TEXT("Root"), PersistenceTestMakeBehaviorNode(
		FGuid::NewGuid().ToString(EGuidFormats::Digits),
		TEXT("/Script/AIModule.BTComposite_Selector"),
		TEXT("Persistence selector"),
		100.0,
		100.0,
		MoveTemp(Children)));
	Tree->SetArrayField(TEXT("Comments"), {});
	Document->GetObjectField(TEXT("Body"))->SetObjectField(TEXT("Tree"), Tree);
	return Document;
}

FString PersistenceTestExtractCanonical(
	FAutomationTestBase& Test,
	FAssetDocumentService& Service,
	const FString& Target)
{
	FAssetDocumentExtractRequest Request;
	Request.AssetPath = PersistenceTestToObjectPath(Target);
	Request.bDiffOnly = false;
	Request.bIncludeAllWritable = true;
	const FAssetDocumentResult Result = Service.Extract(Request);
	Test.TestTrue(TEXT("Persistence canonical extract succeeds"), Result.IsSuccess());
	if (!Result.Payload.IsValid())
	{
		return FString();
	}
	return FAssetDocumentCanonicalJson::WriteCanonicalJson(
		MakeShared<FJsonValueObject>(Result.Payload.ToSharedRef()));
}

TSet<UObject*> PersistenceTestCollectOwned(UObject* Asset)
{
	if (!Asset)
	{
		return {};
	}
	TArray<UObject*> Objects;
	GetObjectsWithOuter(Asset, Objects, true);
	TSet<UObject*> Result;
	for (UObject* Object : Objects)
	{
		Result.Add(Object);
	}
	return Result;
}

UBlackboardKeyType* PersistenceTestFindKeyType(
	UBlackboardData* Blackboard,
	const FName EntryName = TEXT("PersistenceKey"))
{
	if (!Blackboard)
	{
		return nullptr;
	}
	for (const FBlackboardEntry& Entry : Blackboard->Keys)
	{
		if (Entry.EntryName == EntryName)
		{
			return Entry.KeyType.Get();
		}
	}
	return nullptr;
}

struct FPersistenceTestPackageMetadata
{
	FString LoadedFilename;
	uint32 PackageFlags = 0;
	TArray<int32> ChunkIDs;
	FIoHash SavedHash;
	FGuid PersistentGuid;
	int64 FileSize = 0;
	FPackageFileVersion LinkerPackageVersion;
	int32 LinkerLicenseeVersion = 0;
	FCustomVersionContainer LinkerCustomVersions;
	bool bIsCookedForEditor = false;
	bool bDirty = false;
};

FPersistenceTestPackageMetadata PersistenceTestCapturePackageMetadata(const UPackage* Package)
{
	FPersistenceTestPackageMetadata Metadata;
	if (Package)
	{
		Metadata.LoadedFilename = Package->GetLoadedPath().GetLocalFullPath();
		if (!Metadata.LoadedFilename.IsEmpty())
		{
			Metadata.LoadedFilename = FPaths::ConvertRelativePathToFull(Metadata.LoadedFilename);
		}
		FPaths::NormalizeFilename(Metadata.LoadedFilename);
		Metadata.PackageFlags = Package->GetPackageFlags();
		Metadata.ChunkIDs = Package->GetChunkIDs();
		Metadata.SavedHash = Package->GetSavedHash();
		Metadata.PersistentGuid = Package->GetPersistentGuid();
		Metadata.FileSize = Package->GetFileSize();
		Metadata.LinkerPackageVersion = Package->GetLinkerPackageVersion();
		Metadata.LinkerLicenseeVersion = Package->GetLinkerLicenseeVersion();
		Metadata.LinkerCustomVersions = Package->GetLinkerCustomVersions();
		Metadata.bIsCookedForEditor = Package->bIsCookedForEditor;
		Metadata.bDirty = Package->IsDirty();
	}
	return Metadata;
}

bool PersistenceTestCustomVersionsEqual(
	const FCustomVersionContainer& Actual,
	const FCustomVersionContainer& Expected)
{
	const FCustomVersionArray& ActualVersions = Actual.GetAllVersions();
	const FCustomVersionArray& ExpectedVersions = Expected.GetAllVersions();
	if (ActualVersions.Num() != ExpectedVersions.Num())
	{
		return false;
	}
	for (const FCustomVersion& ActualVersion : ActualVersions)
	{
		const FCustomVersion* ExpectedVersion = Expected.GetVersion(ActualVersion.Key);
		if (!ExpectedVersion || ExpectedVersion->Version != ActualVersion.Version)
		{
			return false;
		}
	}
	return true;
}

void PersistenceTestMetadataEqual(
	FAutomationTestBase& Test,
	const FPersistenceTestPackageMetadata& Actual,
	const FPersistenceTestPackageMetadata& Expected,
	const TCHAR* Prefix)
{
	Test.TestEqual(FString::Printf(TEXT("%s loaded filename"), Prefix), Actual.LoadedFilename, Expected.LoadedFilename);
	Test.TestEqual(FString::Printf(TEXT("%s package flags"), Prefix), Actual.PackageFlags, Expected.PackageFlags);
	Test.TestTrue(FString::Printf(TEXT("%s chunk ids"), Prefix), Actual.ChunkIDs == Expected.ChunkIDs);
	Test.TestTrue(FString::Printf(TEXT("%s saved hash"), Prefix), Actual.SavedHash == Expected.SavedHash);
	Test.TestTrue(FString::Printf(TEXT("%s persistent guid"), Prefix), Actual.PersistentGuid == Expected.PersistentGuid);
	Test.TestEqual(FString::Printf(TEXT("%s file size"), Prefix), Actual.FileSize, Expected.FileSize);
	Test.TestTrue(
		FString::Printf(TEXT("%s linker package version"), Prefix),
		Actual.LinkerPackageVersion == Expected.LinkerPackageVersion);
	Test.TestEqual(
		FString::Printf(TEXT("%s linker licensee version"), Prefix),
		Actual.LinkerLicenseeVersion,
		Expected.LinkerLicenseeVersion);
	Test.TestTrue(
		FString::Printf(TEXT("%s linker custom versions"), Prefix),
		PersistenceTestCustomVersionsEqual(Actual.LinkerCustomVersions, Expected.LinkerCustomVersions));
	Test.TestEqual(
		FString::Printf(TEXT("%s cooked-for-editor state"), Prefix),
		Actual.bIsCookedForEditor,
		Expected.bIsCookedForEditor);
	Test.TestEqual(FString::Printf(TEXT("%s dirty state"), Prefix), Actual.bDirty, Expected.bDirty);
}

FAssetDocumentResult PersistenceTestApply(
	FAssetDocumentService& Service,
	const TSharedPtr<FJsonObject>& Document,
	bool bSave = true)
{
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = bSave;
	return Service.Apply(Request);
}

FString PersistenceTestDescribeResult(const FAssetDocumentResult& Result)
{
	TArray<FString> Parts;
	Parts.Add(FString::Printf(
		TEXT("status=%d message='%s' target='%s' asset_path='%s' saved=%s sidecar=%s"),
		static_cast<int32>(Result.Status),
		*Result.Message,
		*Result.Target,
		*Result.AssetPath,
		Result.bSavedAsset ? TEXT("true") : TEXT("false"),
		Result.bWroteSidecar ? TEXT("true") : TEXT("false")));
	for (const FAssetDocumentDiagnostic& Diagnostic : Result.Diagnostics)
	{
		Parts.Add(FString::Printf(
			TEXT("diagnostic{code='%s' path='%s' message='%s'}"),
			*Diagnostic.Code,
			*Diagnostic.Path,
			*Diagnostic.Message));
	}
	return FString::Join(Parts, TEXT(" | "));
}

bool PersistenceTestRequireApplySuccess(
	FAutomationTestBase& Test,
	const TCHAR* Context,
	FAssetDocumentService& Service,
	const TSharedPtr<FJsonObject>& Document,
	bool bSave = true)
{
	if (!Document.IsValid())
	{
		Test.AddError(FString::Printf(TEXT("%s: fixture document is invalid"), Context));
		return false;
	}
	const FAssetDocumentResult Result = PersistenceTestApply(Service, Document, bSave);
	if (!Result.IsSuccess())
	{
		Test.AddError(FString::Printf(
			TEXT("%s: %s"),
			Context,
			*PersistenceTestDescribeResult(Result)));
		return false;
	}
	return true;
}

bool PersistenceTestLoadBytes(const FString& Path, TArray64<uint8>& OutBytes)
{
	OutBytes.Reset();
	return FFileHelper::LoadFileToArray(OutBytes, *Path);
}

FAssetDocumentDiagnostic PersistenceTestMakeFailure(const TCHAR* Code, const TCHAR* Message)
{
	FAssetDocumentDiagnostic Diagnostic;
	Diagnostic.Path = TEXT("/Apply/Persistence");
	Diagnostic.Code = Code;
	Diagnostic.Message = Message;
	return Diagnostic;
}

class FPersistenceTestScopedHookReset
{
public:
	FPersistenceTestScopedHookReset() { FAssetDocumentServiceTestHooks::Clear(); }
	~FPersistenceTestScopedHookReset()
	{
		FAssetDocumentAtomicFile::ResetFailureForTest();
		FAssetDocumentServiceTestHooks::Clear();
	}
};

bool PersistenceTestHasDiagnostic(
	const FAssetDocumentResult& Result,
	const TCHAR* Code)
{
	return Result.Diagnostics.ContainsByPredicate(
		[Code](const FAssetDocumentDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == Code;
		});
}

bool PersistenceTestWriteDocument(
	const FString& Path,
	const TSharedPtr<FJsonObject>& Document)
{
	if (!Document.IsValid())
	{
		return false;
	}
	FString Contents;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Contents);
	return FJsonSerializer::Serialize(Document.ToSharedRef(), Writer)
		&& FFileHelper::SaveStringToFile(Contents, *Path);
}

bool PersistenceTestWriteDocumentControlled(
	const FString& Path,
	const TSharedPtr<FJsonObject>& Document)
{
	FAssetDocumentEditorSync::FScopedSidecarWrite Guard(Path);
	return PersistenceTestWriteDocument(Path, Document);
}

bool PersistenceTestEstablishSynchronizedBlackboardSidecar(
	FAutomationTestBase& Test,
	FAssetDocumentService& Service,
	const FString& Target,
	const FString& SidecarPath,
	int32 DefaultValue,
	TSharedPtr<FJsonObject>& OutSynchronizedDocument)
{
	OutSynchronizedDocument.Reset();
	TSharedPtr<FJsonObject> BaselineDocument = PersistenceTestMakeBlackboardDocument(
		Test,
		Service,
		Target,
		TEXT("Update"),
		DefaultValue);
	if (!BaselineDocument.IsValid())
	{
		Test.AddError(TEXT("Synchronized sidecar baseline document is invalid"));
		return false;
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SidecarPath), true);
	if (!PersistenceTestWriteDocumentControlled(SidecarPath, BaselineDocument))
	{
		Test.AddError(FString::Printf(
			TEXT("Failed to write synchronized sidecar baseline '%s'"),
			*SidecarPath));
		return false;
	}

	FAssetDocumentApplyFileRequest ApplyFileRequest;
	ApplyFileRequest.FilePath = SidecarPath;
	ApplyFileRequest.bSaveAsset = true;
	ApplyFileRequest.bAllowSidecarRewrite = true;
	const FAssetDocumentResult BaselineResult = Service.ApplyFile(ApplyFileRequest);
	if (!BaselineResult.IsSuccess() || !BaselineResult.bWroteSidecar)
	{
		Test.AddError(FString::Printf(
			TEXT("Failed to establish synchronized sidecar baseline: %s"),
			*PersistenceTestDescribeResult(BaselineResult)));
		return false;
	}

	FString LoadError;
	if (!FAssetDocumentSidecar::LoadJsonFile(
			SidecarPath,
			OutSynchronizedDocument,
			LoadError)
		|| !OutSynchronizedDocument.IsValid())
	{
		Test.AddError(FString::Printf(
			TEXT("Failed to reload synchronized sidecar baseline '%s': %s"),
			*SidecarPath,
			*LoadError));
		return false;
	}
	const TSharedPtr<FJsonObject>* Meta = nullptr;
	const TSharedPtr<FJsonObject>* Sync = nullptr;
	if (!OutSynchronizedDocument->TryGetObjectField(TEXT("_meta"), Meta)
		|| !Meta
		|| !Meta->IsValid()
		|| !(*Meta)->TryGetObjectField(TEXT("sync"), Sync)
		|| !Sync
		|| !Sync->IsValid())
	{
		Test.AddError(TEXT("Synchronized sidecar baseline is missing _meta.sync"));
		return false;
	}
	return true;
}

bool PersistenceTestSetFirstBlackboardKeyDefault(
	const TSharedPtr<FJsonObject>& Document,
	int32 DefaultValue)
{
	if (!Document.IsValid())
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Body = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
	if (!Document->TryGetObjectField(TEXT("Body"), Body)
		|| !Body
		|| !Body->IsValid()
		|| !(*Body)->TryGetArrayField(TEXT("Keys"), Keys)
		|| !Keys
		|| Keys->Num() == 0)
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Key = (*Keys)[0].IsValid()
		? (*Keys)[0]->AsObject()
		: nullptr;
	const TSharedPtr<FJsonObject>* Properties = nullptr;
	if (!Key.IsValid()
		|| !Key->TryGetObjectField(TEXT("KeyTypeProperties"), Properties)
		|| !Properties
		|| !Properties->IsValid())
	{
		return false;
	}
	(*Properties)->SetNumberField(TEXT("DefaultValue"), DefaultValue);
	Document->SetStringField(TEXT("Action"), TEXT("Update"));
	return true;
}

bool PersistenceTestRunInstalledSegmentTamperCase(
	FAutomationTestBase& Test,
	EAssetDocumentServicePersistencePhase TamperPhase,
	const TCHAR* Stem,
	const TCHAR* AssertionPrefix)
{
	const FString Target = PersistenceTestMakeTarget(Stem);
	const FString PackagePath = PersistenceTestToPackagePath(Target);
	const FString SidecarPath = FPackageName::LongPackageNameToFilename(Target, TEXT(".assetdoc.json"));
	PersistenceTestCleanup(Target, SidecarPath);
	ON_SCOPE_EXIT
	{
		PersistenceTestCleanup(Target, SidecarPath);
	};

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Initial = PersistenceTestMakeBlackboardDocument(
		Test,
		Service,
		Target,
		TEXT("Create"),
		7);
	if (!PersistenceTestRequireApplySuccess(
			Test,
			TEXT("Creates installed-segment tamper baseline"),
			Service,
			Initial))
	{
		return false;
	}

	TSharedPtr<FJsonObject> Update;
	if (!PersistenceTestEstablishSynchronizedBlackboardSidecar(
			Test,
			Service,
			Target,
			SidecarPath,
			7,
			Update)
		|| !PersistenceTestSetFirstBlackboardKeyDefault(Update, 29))
	{
		return false;
	}
	UBlackboardData* BlackboardBefore = FindObject<UBlackboardData>(
		nullptr,
		*PersistenceTestToObjectPath(Target));
	UBlackboardKeyType* KeyTypeBefore = PersistenceTestFindKeyType(BlackboardBefore);
	Test.TestNotNull(FString::Printf(TEXT("%s finds live baseline"), AssertionPrefix), BlackboardBefore);
	Test.TestNotNull(FString::Printf(TEXT("%s finds key-type baseline"), AssertionPrefix), KeyTypeBefore);
	if (!BlackboardBefore || !KeyTypeBefore)
	{
		return false;
	}

	TArray64<uint8> PackageBytesBefore;
	Test.TestTrue(
		FString::Printf(TEXT("%s reads baseline package bytes"), AssertionPrefix),
		PersistenceTestLoadBytes(PackagePath, PackageBytesBefore));
	Test.TestTrue(
		FString::Printf(TEXT("%s writes sidecar fixture"), AssertionPrefix),
		PersistenceTestWriteDocumentControlled(SidecarPath, Update));
	TArray64<uint8> SidecarBytesBefore;
	Test.TestTrue(
		FString::Printf(TEXT("%s reads baseline sidecar bytes"), AssertionPrefix),
		PersistenceTestLoadBytes(SidecarPath, SidecarBytesBefore));

	int32 CanonicalEventCount = 0;
	FString ExpectedCanonicalFilename = FPaths::ConvertRelativePathToFull(PackagePath);
	FPaths::NormalizeFilename(ExpectedCanonicalFilename);
	const FDelegateHandle SavedHandle = UPackage::PackageSavedWithContextEvent.AddLambda(
		[&](const FString& Filename, UPackage* SavedPackage, FObjectPostSaveContext)
		{
			FString NormalizedFilename = FPaths::ConvertRelativePathToFull(Filename);
			FPaths::NormalizeFilename(NormalizedFilename);
			if (SavedPackage && SavedPackage->GetName() == Target && NormalizedFilename == ExpectedCanonicalFilename)
			{
				++CanonicalEventCount;
			}
		});
	ON_SCOPE_EXIT
	{
		UPackage::PackageSavedWithContextEvent.Remove(SavedHandle);
	};

	bool bTamperedCanonicalSegment = false;
	FPersistenceTestScopedHookReset Hooks;
	FAssetDocumentServiceTestHooks::RunNextPersistenceCallbackAtPhase(
		TamperPhase,
		[&]()
		{
			TArray64<uint8> TamperedBytes;
			if (!PersistenceTestLoadBytes(PackagePath, TamperedBytes) || TamperedBytes.IsEmpty())
			{
				return;
			}
			TamperedBytes.Last() ^= 0x5a;
			bTamperedCanonicalSegment = FFileHelper::SaveArrayToFile(TamperedBytes, *PackagePath);
		});
	FAssetDocumentApplyRequest Request;
	Request.Document = Update;
	Request.SourceDocumentPath = SidecarPath;
	Request.bWriteSidecar = true;
	Request.bSaveAsset = true;
	const FAssetDocumentResult Failure = Service.Apply(Request);
	Test.TestTrue(
		FString::Printf(
			TEXT("%s executes real canonical tamper; %s"),
			AssertionPrefix,
			*PersistenceTestDescribeResult(Failure)),
		bTamperedCanonicalSegment);
	Test.TestFalse(
		FString::Printf(TEXT("%s rejects Apply"), AssertionPrefix),
		Failure.IsSuccess());
	Test.TestTrue(
		FString::Printf(TEXT("%s reports installed-byte verification"), AssertionPrefix),
		PersistenceTestHasDiagnostic(Failure, TEXT("AssetDocumentInstalledPackageVerificationFailed")));
	Test.TestEqual(
		FString::Printf(TEXT("%s emits no canonical save event"), AssertionPrefix),
		CanonicalEventCount,
		0);

	TArray64<uint8> PackageBytesAfter;
	TArray64<uint8> SidecarBytesAfter;
	Test.TestTrue(
		FString::Printf(TEXT("%s reads rolled-back package"), AssertionPrefix),
		PersistenceTestLoadBytes(PackagePath, PackageBytesAfter));
	Test.TestTrue(
		FString::Printf(TEXT("%s reads rolled-back sidecar"), AssertionPrefix),
		PersistenceTestLoadBytes(SidecarPath, SidecarBytesAfter));
	Test.TestTrue(
		FString::Printf(TEXT("%s restores exact package bytes"), AssertionPrefix),
		PackageBytesAfter == PackageBytesBefore);
	Test.TestTrue(
		FString::Printf(TEXT("%s restores exact sidecar bytes"), AssertionPrefix),
		SidecarBytesAfter == SidecarBytesBefore);
	UBlackboardData* BlackboardAfter = FindObject<UBlackboardData>(
		nullptr,
		*PersistenceTestToObjectPath(Target));
	Test.TestTrue(
		FString::Printf(TEXT("%s preserves asset identity"), AssertionPrefix),
		BlackboardAfter == BlackboardBefore);
	Test.TestTrue(
		FString::Printf(TEXT("%s preserves key-type identity"), AssertionPrefix),
		PersistenceTestFindKeyType(BlackboardAfter) == KeyTypeBefore);
	const UBlackboardKeyType_Int* IntKey = Cast<UBlackboardKeyType_Int>(
		PersistenceTestFindKeyType(BlackboardAfter));
	Test.TestNotNull(
		FString::Printf(TEXT("%s restores Int key type"), AssertionPrefix),
		IntKey);
	if (IntKey)
	{
		Test.TestEqual(
			FString::Printf(TEXT("%s restores key default"), AssertionPrefix),
			IntKey->DefaultValue,
			7);
	}
	return true;
}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPersistenceGenericUsesStandardSaveTest,
	"AssetFactory.AssetDocument.Persistence.Generic.UsesStandardSaveAndBypassesStrictHooks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistenceGenericUsesStandardSaveTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	const FString Target = PersistenceTestMakeTarget(TEXT("DA_StandardSave"));
	PersistenceTestCleanup(Target);
	FAssetDocumentService Service;
	if (!PersistenceTestRequireApplySuccess(
			*this,
			TEXT("Creates the generic standard-save baseline"),
			Service,
			PersistenceTestMakeGenericDocument(Target, TEXT("Create"), TEXT("before"), 7)))
	{
		PersistenceTestCleanup(Target);
		return false;
	}
	UTestDataAsset* Asset = FindObject<UTestDataAsset>(nullptr, *PersistenceTestToObjectPath(Target));
	TestNotNull(TEXT("Finds the live generic baseline"), Asset);
	if (!Asset)
	{
		PersistenceTestCleanup(Target);
		return false;
	}

	FPersistenceTestScopedHookReset Hooks;
	FAssetDocumentServiceTestHooks::FailNextPersistenceAtPhase(
		EAssetDocumentServicePersistencePhase::AfterPackageInstall,
		PersistenceTestMakeFailure(TEXT("ForcedPackageInstallFailure"), TEXT("forced failure after package install")));
	const FAssetDocumentResult Result = PersistenceTestApply(
		Service,
		PersistenceTestMakeGenericDocument(Target, TEXT("Update"), TEXT("after"), 19));
	if (!Result.IsSuccess())
	{
		AddError(FString::Printf(
			TEXT("Generic Apply bypasses the strict package-install hook: %s"),
			*PersistenceTestDescribeResult(Result)));
		PersistenceTestCleanup(Target);
		return false;
	}
	TestEqual(TEXT("Generic standard save keeps the applied string"), Asset->TestString, FString(TEXT("after")));
	TestEqual(TEXT("Generic standard save keeps the applied integer"), Asset->TestInt, 19);

	TArray64<uint8> DiskAfter;
	TestTrue(TEXT("Generic standard save writes a canonical package"), PersistenceTestLoadBytes(PersistenceTestToPackagePath(Target), DiskAfter));
	TestTrue(TEXT("Generic standard save writes non-empty package bytes"), DiskAfter.Num() > 0);
	PersistenceTestCleanup(Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPersistenceExistingBehaviorTreeIdentityTest,
	"AssetFactory.AssetDocument.Persistence.Existing.BehaviorTree.PostLiveSuccessFailureRestoresExactIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistenceExistingBehaviorTreeIdentityTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	const FString Target = PersistenceTestMakeTarget(TEXT("BT_ExistingIdentity"));
	PersistenceTestCleanup(Target);
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = PersistenceTestMakeBehaviorTreeDocument(*this, Service, Target, TEXT("Create"));
	if (!PersistenceTestRequireApplySuccess(
			*this,
			TEXT("Creates durable BehaviorTree identity baseline"),
			Service,
			Document))
	{
		PersistenceTestCleanup(Target);
		return false;
	}

	UBehaviorTree* BehaviorTree = FindObject<UBehaviorTree>(nullptr, *PersistenceTestToObjectPath(Target));
	TestNotNull(TEXT("Finds BehaviorTree identity baseline"), BehaviorTree);
	if (!BehaviorTree)
	{
		PersistenceTestCleanup(Target);
		return false;
	}
	UEdGraph* GraphBefore = BehaviorTree->BTGraph;
	UBTCompositeNode* RootBefore = BehaviorTree->RootNode;
	UBTTaskNode* TaskBefore = RootBefore && RootBefore->Children.Num() == 1
		? RootBefore->Children[0].ChildTask.Get()
		: nullptr;
	TestNotNull(TEXT("BehaviorTree graph identity baseline exists"), GraphBefore);
	TestNotNull(TEXT("BehaviorTree root identity baseline exists"), RootBefore);
	TestNotNull(TEXT("BehaviorTree runtime task identity baseline exists"), TaskBefore);
	const UObject* GraphOuterBefore = GraphBefore ? GraphBefore->GetOuter() : nullptr;
	const FName GraphNameBefore = GraphBefore ? GraphBefore->GetFName() : NAME_None;
	const TSet<UObject*> OwnedBefore = PersistenceTestCollectOwned(BehaviorTree);
	const FString CanonicalBefore = PersistenceTestExtractCanonical(*this, Service, Target);
	TArray64<uint8> DiskBefore;
	TestTrue(TEXT("Reads BehaviorTree identity baseline package"), PersistenceTestLoadBytes(PersistenceTestToPackagePath(Target), DiskBefore));

	Document->SetStringField(TEXT("Action"), TEXT("Update"));
	TSharedPtr<FJsonObject> Root = Document->GetObjectField(TEXT("Body"))
		->GetObjectField(TEXT("Tree"))
		->GetObjectField(TEXT("Root"));
	Root->GetObjectField(TEXT("Properties"))->SetStringField(TEXT("NodeName"), TEXT("Persistence replacement selector"));
	FPersistenceTestScopedHookReset Hooks;
	FAssetDocumentServiceTestHooks::FailNextPersistenceAtPhase(
		EAssetDocumentServicePersistencePhase::AfterPackageInstall,
		PersistenceTestMakeFailure(
			TEXT("ForcedBehaviorTreePostInstallFailure"),
			TEXT("forced BehaviorTree failure after verified package install")));
	const FAssetDocumentResult Result = PersistenceTestApply(Service, Document);
	TestFalse(TEXT("Post-live-success BehaviorTree persistence failure rejects Apply"), Result.IsSuccess());
	TestTrue(TEXT("BehaviorTree asset pointer identity is exact"), FindObject<UBehaviorTree>(nullptr, *PersistenceTestToObjectPath(Target)) == BehaviorTree);
	TestTrue(TEXT("BehaviorTree graph pointer identity is exact"), BehaviorTree->BTGraph == GraphBefore);
	TestTrue(TEXT("BehaviorTree root pointer identity is exact"), BehaviorTree->RootNode.Get() == RootBefore);
	TestTrue(
		TEXT("BehaviorTree runtime task pointer identity is exact"),
		BehaviorTree->RootNode
			&& BehaviorTree->RootNode->Children.Num() == 1
			&& BehaviorTree->RootNode->Children[0].ChildTask.Get() == TaskBefore);
	if (BehaviorTree->BTGraph)
	{
		TestTrue(TEXT("BehaviorTree graph outer is exact"), BehaviorTree->BTGraph->GetOuter() == GraphOuterBefore);
		TestEqual(TEXT("BehaviorTree graph name is exact"), BehaviorTree->BTGraph->GetFName(), GraphNameBefore);
	}
	const TSet<UObject*> OwnedAfter = PersistenceTestCollectOwned(BehaviorTree);
	TestTrue(TEXT("BehaviorTree owned-object identity set is exact"), OwnedAfter.Includes(OwnedBefore) && OwnedBefore.Includes(OwnedAfter));
	TestEqual(TEXT("BehaviorTree canonical extract is exact after rollback"), PersistenceTestExtractCanonical(*this, Service, Target), CanonicalBefore);
	TestFalse(TEXT("BehaviorTree package dirty state is restored"), BehaviorTree->GetOutermost()->IsDirty());
	TArray64<uint8> DiskAfter;
	TestTrue(TEXT("Reads BehaviorTree package after rollback"), PersistenceTestLoadBytes(PersistenceTestToPackagePath(Target), DiskAfter));
	TestTrue(TEXT("BehaviorTree package bytes are exact after rollback"), DiskAfter == DiskBefore);
	PersistenceTestCleanup(Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPersistencePackageAtomicOuterRollbackTest,
	"AssetFactory.AssetDocument.Persistence.Blackboard.PackageAtomicInternalRollbackFailureUsesOuterRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistencePackageAtomicOuterRollbackTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	const FString Target = PersistenceTestMakeTarget(TEXT("BB_PackageAtomicOuterRollback"));
	PersistenceTestCleanup(Target);
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Initial = PersistenceTestMakeBlackboardDocument(*this, Service, Target, TEXT("Create"), 61);
	if (!PersistenceTestRequireApplySuccess(
			*this,
			TEXT("Creates package-atomic rollback baseline"),
			Service,
			Initial))
	{
		PersistenceTestCleanup(Target);
		return false;
	}
	UBlackboardData* BlackboardBefore = FindObject<UBlackboardData>(nullptr, *PersistenceTestToObjectPath(Target));
	UBlackboardKeyType* KeyTypeBefore = PersistenceTestFindKeyType(BlackboardBefore);
	const FString CanonicalBefore = PersistenceTestExtractCanonical(*this, Service, Target);
	TArray64<uint8> PackageBefore;
	TestNotNull(TEXT("Finds package-atomic rollback baseline"), BlackboardBefore);
	TestNotNull(TEXT("Finds package-atomic rollback key type"), KeyTypeBefore);
	if (!BlackboardBefore || !KeyTypeBefore)
	{
		PersistenceTestCleanup(Target);
		return false;
	}
	TestTrue(TEXT("Reads package-atomic rollback baseline bytes"), PersistenceTestLoadBytes(PersistenceTestToPackagePath(Target), PackageBefore));

	FPersistenceTestScopedHookReset Hooks;
	FAssetDocumentServiceTestHooks::RunNextPersistenceCallbackAtPhase(
		EAssetDocumentServicePersistencePhase::AfterFreshReloadBeforeVerification,
		[]()
		{
			FAssetDocumentAtomicFile::FailNextWriteAtForTest(EAssetDocumentAtomicFileFailurePoint::DirectoryFlush);
			FAssetDocumentAtomicFile::ForceNextCommittedRenameRollbackFailureForTest();
		});
	TSharedPtr<FJsonObject> Update = PersistenceTestMakeBlackboardDocument(*this, Service, Target, TEXT("Update"), 67);
	const FAssetDocumentResult Result = PersistenceTestApply(Service, Update);
	TestFalse(TEXT("Package atomic internal rollback failure rejects Apply"), Result.IsSuccess());
	TestTrue(TEXT("Package atomic failure reports the directory-flush stage"), Result.Message.Contains(TEXT("AtomicFile.DirectoryFlush")));
	UBlackboardData* BlackboardAfter = FindObject<UBlackboardData>(nullptr, *PersistenceTestToObjectPath(Target));
	TestTrue(TEXT("Package outer rollback preserves live asset identity"), BlackboardAfter == BlackboardBefore);
	TestTrue(TEXT("Package outer rollback preserves key-type identity"), PersistenceTestFindKeyType(BlackboardAfter) == KeyTypeBefore);
	TestEqual(TEXT("Package outer rollback restores canonical live state"), PersistenceTestExtractCanonical(*this, Service, Target), CanonicalBefore);
	TArray64<uint8> PackageAfter;
	TestTrue(TEXT("Reads package after outer rollback"), PersistenceTestLoadBytes(PersistenceTestToPackagePath(Target), PackageAfter));
	TestTrue(TEXT("Package outer rollback restores exact canonical bytes"), PackageAfter == PackageBefore);
	PersistenceTestCleanup(Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPersistencePackageRollbackFailurePoisonTest,
	"AssetFactory.AssetDocument.Persistence.Blackboard.PackageRollbackFailurePoisonsPackage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistencePackageRollbackFailurePoisonTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	const FString Target = PersistenceTestMakeTarget(TEXT("BB_PackageRollbackPoison"));
	PersistenceTestCleanup(Target);
	ON_SCOPE_EXIT
	{
		PersistenceTestCleanup(Target);
	};

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Initial = PersistenceTestMakeBlackboardDocument(
		*this,
		Service,
		Target,
		TEXT("Create"),
		5);
	if (!PersistenceTestRequireApplySuccess(
			*this,
			TEXT("Creates package rollback poison baseline"),
			Service,
			Initial))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Update = PersistenceTestMakeBlackboardDocument(
		*this,
		Service,
		Target,
		TEXT("Update"),
		41);

	bool bReachedAfterPackageInstall = false;
	FPersistenceTestScopedHookReset Hooks;
	FAssetDocumentServiceTestHooks::RunNextPersistenceCallbackAtPhase(
		EAssetDocumentServicePersistencePhase::AfterPackageInstall,
		[&]()
		{
			bReachedAfterPackageInstall = true;
			// No atomic operation occurs between the primary phase failure and
			// RollbackInstalledPackage, so this injection targets the rollback write.
			FAssetDocumentAtomicFile::FailNextWriteAtForTest(
				EAssetDocumentAtomicFileFailurePoint::Open);
		});
	FAssetDocumentServiceTestHooks::FailNextPersistenceAtPhase(
		EAssetDocumentServicePersistencePhase::AfterPackageInstall,
		PersistenceTestMakeFailure(
			TEXT("ForcedPackageRollbackPoisonPrimaryFailure"),
			TEXT("forced primary failure before package rollback injection")));
	const FAssetDocumentResult Failure = PersistenceTestApply(Service, Update);
	TestTrue(
		FString::Printf(
			TEXT("Package rollback poison reaches AfterPackageInstall; %s"),
			*PersistenceTestDescribeResult(Failure)),
		bReachedAfterPackageInstall);
	TestFalse(TEXT("Package rollback failure rejects Apply"), Failure.IsSuccess());
	TestTrue(
		FString::Printf(
			TEXT("Package rollback failure is reported; %s"),
			*PersistenceTestDescribeResult(Failure)),
		PersistenceTestHasDiagnostic(Failure, TEXT("AssetDocumentPersistenceRollbackFailed")));
	TestTrue(
		FString::Printf(
			TEXT("Package rollback failure poisons the package; %s"),
			*PersistenceTestDescribeResult(Failure)),
		PersistenceTestHasDiagnostic(Failure, TEXT("AssetDocumentPackagePoisonedRestartRequired")));

	FAssetDocumentValidateRequest ValidateRequest;
	ValidateRequest.Document = Update;
	const FAssetDocumentResult PoisonedValidate = Service.Validate(ValidateRequest);
	TestFalse(TEXT("Package rollback poison rejects Validate"), PoisonedValidate.IsSuccess());
	TestTrue(
		FString::Printf(
			TEXT("Package rollback poison Validate requires restart; %s"),
			*PersistenceTestDescribeResult(PoisonedValidate)),
		PersistenceTestHasDiagnostic(
			PoisonedValidate,
			TEXT("AssetDocumentPackagePoisonedRestartRequired")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPersistenceSnapshotReadinessTest,
	"AssetFactory.AssetDocument.Persistence.Blackboard.SnapshotFailureFailsBeforeLiveMutation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistenceSnapshotReadinessTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	const FString Target = PersistenceTestMakeTarget(TEXT("BB_SnapshotReadiness"));
	PersistenceTestCleanup(Target);
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Initial = PersistenceTestMakeBlackboardDocument(*this, Service, Target, TEXT("Create"), 13);
	if (!PersistenceTestRequireApplySuccess(
			*this,
			TEXT("Creates the snapshot-readiness baseline"),
			Service,
			Initial))
	{
		PersistenceTestCleanup(Target);
		return false;
	}
	UBlackboardData* BlackboardBefore = FindObject<UBlackboardData>(nullptr, *PersistenceTestToObjectPath(Target));
	UBlackboardKeyType* KeyTypeBefore = PersistenceTestFindKeyType(BlackboardBefore);
	const FString CanonicalBefore = PersistenceTestExtractCanonical(*this, Service, Target);
	TArray64<uint8> DiskBefore;
	TestNotNull(TEXT("Finds the snapshot-readiness live baseline"), BlackboardBefore);
	TestNotNull(TEXT("Finds the snapshot-readiness key type"), KeyTypeBefore);
	if (!BlackboardBefore || !KeyTypeBefore)
	{
		PersistenceTestCleanup(Target);
		return false;
	}
	TestTrue(TEXT("Reads snapshot-readiness baseline bytes"), PersistenceTestLoadBytes(PersistenceTestToPackagePath(Target), DiskBefore));

	FPersistenceTestScopedHookReset Hooks;
	FAssetDocumentServiceTestHooks::FailNextApplySnapshot(
		PersistenceTestMakeFailure(
			TEXT("ForcedAssetDocumentSnapshotFailure"),
			TEXT("forced snapshot capture failure before live mutation")));
	TSharedPtr<FJsonObject> Update = PersistenceTestMakeBlackboardDocument(*this, Service, Target, TEXT("Update"), 37);
	const FAssetDocumentResult Result = PersistenceTestApply(Service, Update);
	TestFalse(TEXT("Snapshot capture failure rejects strict Apply"), Result.IsSuccess());
	TestTrue(TEXT("Snapshot capture failure reports the injected reason"), Result.Message.Contains(TEXT("snapshot capture failure")));
	UBlackboardData* BlackboardAfter = FindObject<UBlackboardData>(nullptr, *PersistenceTestToObjectPath(Target));
	TestTrue(TEXT("Snapshot failure preserves live asset identity"), BlackboardAfter == BlackboardBefore);
	TestTrue(TEXT("Snapshot failure preserves key-type identity"), PersistenceTestFindKeyType(BlackboardAfter) == KeyTypeBefore);
	TestEqual(TEXT("Snapshot failure preserves canonical live state"), PersistenceTestExtractCanonical(*this, Service, Target), CanonicalBefore);
	TArray64<uint8> DiskAfter;
	TestTrue(TEXT("Reads package after snapshot failure"), PersistenceTestLoadBytes(PersistenceTestToPackagePath(Target), DiskAfter));
	TestTrue(TEXT("Snapshot failure preserves exact package bytes"), DiskAfter == DiskBefore);
	PersistenceTestCleanup(Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPersistenceNewStrictInstallFailureTest,
	"AssetFactory.AssetDocument.Persistence.Blackboard.NewPostInstallFailureRemovesLiveAndDisk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistenceNewStrictInstallFailureTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	const FString Target = PersistenceTestMakeTarget(TEXT("BB_NewInstallFailure"));
	PersistenceTestCleanup(Target);
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = PersistenceTestMakeBlackboardDocument(*this, Service, Target, TEXT("Create"), 41);
	FPersistenceTestScopedHookReset Hooks;
	FAssetDocumentServiceTestHooks::FailNextPersistenceAtPhase(
		EAssetDocumentServicePersistencePhase::AfterPackageInstall,
		PersistenceTestMakeFailure(
			TEXT("ForcedNewStrictPackageInstallFailure"),
			TEXT("forced new strict-package failure after install")));
	const FAssetDocumentResult Result = PersistenceTestApply(Service, Document);
	TestFalse(TEXT("New strict-package install failure rejects Apply"), Result.IsSuccess());
	TestFalse(TEXT("New strict-package install failure removes canonical package"), IFileManager::Get().FileExists(*PersistenceTestToPackagePath(Target)));
	TestNull(TEXT("New strict-package install failure removes the live asset"), FindObject<UObject>(nullptr, *PersistenceTestToObjectPath(Target)));

	FAssetDocumentServiceTestHooks::Clear();
	if (!PersistenceTestRequireApplySuccess(
			*this,
			TEXT("Retry after new strict-package install failure succeeds"),
			Service,
			Document))
	{
		PersistenceTestCleanup(Target);
		return false;
	}
	PersistenceTestCleanup(Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPersistenceStaleSiblingTest,
	"AssetFactory.AssetDocument.Persistence.Blackboard.StaleSiblingDeleteRollsBackAndCommits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistenceStaleSiblingTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	const FString Target = PersistenceTestMakeTarget(TEXT("BB_StaleSibling"));
	const FString CanonicalHeaderPath = PersistenceTestToPackagePath(Target);
	const FString CanonicalBasePath = CanonicalHeaderPath.LeftChop(FCString::Strlen(TEXT(".uasset")));
	struct FStaleSiblingFixture
	{
		FString Path;
		TArray64<uint8> Bytes;
	};
	const TArray<FStaleSiblingFixture> StaleSiblings{
		{CanonicalBasePath + TEXT(".uexp"), {0xe1, 0xe2, 0xe3}},
		{CanonicalBasePath + TEXT(".ubulk"), {0xc1, 0xc2, 0xc3, 0xc4}},
		{CanonicalBasePath + TEXT(".uptnl"), {0xd1, 0xd2, 0xd3, 0xd4, 0xd5}},
		{CanonicalBasePath + TEXT(".m.ubulk"), {0xb1, 0xb2, 0xb3, 0xb4}},
		{CanonicalBasePath + TEXT(".upayload"), {0xa1, 0xa2, 0xa3, 0xa4, 0xa5}}};
	PersistenceTestCleanup(Target);
	for (const FStaleSiblingFixture& Sibling : StaleSiblings)
	{
		IFileManager::Get().Delete(*Sibling.Path, false, true);
	}
	ON_SCOPE_EXIT
	{
		PersistenceTestCleanup(Target);
		for (const FStaleSiblingFixture& Sibling : StaleSiblings)
		{
			IFileManager::Get().Delete(*Sibling.Path, false, true);
		}
	};
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Initial = PersistenceTestMakeBlackboardDocument(*this, Service, Target, TEXT("Create"), 2);
	if (!PersistenceTestRequireApplySuccess(
			*this,
			TEXT("Creates the stale-sibling baseline"),
			Service,
			Initial))
	{
		return false;
	}
	for (const FStaleSiblingFixture& Sibling : StaleSiblings)
	{
		TestTrue(
			FString::Printf(TEXT("Writes stale canonical package sibling '%s'"), *Sibling.Path),
			FFileHelper::SaveArrayToFile(Sibling.Bytes, *Sibling.Path));
	}

	TSharedPtr<FJsonObject> Update = PersistenceTestMakeBlackboardDocument(*this, Service, Target, TEXT("Update"), 3);
	FPersistenceTestScopedHookReset Hooks;
	FAssetDocumentServiceTestHooks::FailNextPersistenceAtPhase(
		EAssetDocumentServicePersistencePhase::AfterPackageInstall,
		PersistenceTestMakeFailure(TEXT("ForcedStaleSiblingRollback"), TEXT("forced failure after stale sibling deletion")));
	const FAssetDocumentResult Failure = PersistenceTestApply(Service, Update);
	TestFalse(TEXT("Post-install failure rejects stale-sibling update"), Failure.IsSuccess());
	for (const FStaleSiblingFixture& Sibling : StaleSiblings)
	{
		TArray64<uint8> RestoredSiblingBytes;
		TestTrue(
			FString::Printf(TEXT("Rollback restores stale sibling '%s'"), *Sibling.Path),
			PersistenceTestLoadBytes(Sibling.Path, RestoredSiblingBytes));
		TestTrue(
			FString::Printf(TEXT("Rollback restores exact stale sibling bytes '%s'"), *Sibling.Path),
			RestoredSiblingBytes == Sibling.Bytes);
	}

	FAssetDocumentServiceTestHooks::Clear();
	if (!PersistenceTestRequireApplySuccess(
			*this,
			TEXT("Retry commits the strict package update"),
			Service,
			Update))
	{
		return false;
	}
	for (const FStaleSiblingFixture& Sibling : StaleSiblings)
	{
		TestFalse(
			FString::Printf(TEXT("Successful strict package install deletes stale sibling '%s'"), *Sibling.Path),
			IFileManager::Get().FileExists(*Sibling.Path));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPersistenceCanonicalMetadataEventTest,
	"AssetFactory.AssetDocument.Persistence.Blackboard.CanonicalMetadataEventAndIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistenceCanonicalMetadataEventTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	const FString Target = PersistenceTestMakeTarget(TEXT("BB_CanonicalMetadata"));
	const FString CanonicalFilename = PersistenceTestToPackagePath(Target);
	PersistenceTestCleanup(Target);
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = PersistenceTestMakeBlackboardDocument(*this, Service, Target, TEXT("Create"), 53);
	int32 MatchingEventCount = 0;
	FString LastEventFilename;
	bool bLastEventWasAutosave = true;
	bool bLastEventUpdatedLoadedPath = false;
	UPackage* LastEventPackage = nullptr;
	bool bObservedPackageBeforeMetadataRefresh = false;
	bool bWasInMemoryOnlyBeforeMetadataRefresh = false;
	FPersistenceTestScopedHookReset Hooks;
	FAssetDocumentServiceTestHooks::RunNextPersistenceCallbackAtPhase(
		EAssetDocumentServicePersistencePhase::AfterPackageInstall,
		[&]()
		{
			UPackage* InstalledPackage = FindObject<UPackage>(nullptr, *Target);
			bObservedPackageBeforeMetadataRefresh = InstalledPackage != nullptr;
			bWasInMemoryOnlyBeforeMetadataRefresh = InstalledPackage
				&& InstalledPackage->HasAnyPackageFlags(PKG_InMemoryOnly);
		});
	const FDelegateHandle SavedHandle = UPackage::PackageSavedWithContextEvent.AddLambda(
		[&](const FString& Filename, UPackage* Package, FObjectPostSaveContext Context)
		{
			if (Package && Package->GetName() == Target)
			{
				++MatchingEventCount;
				LastEventFilename = FPaths::ConvertRelativePathToFull(Filename);
				FPaths::NormalizeFilename(LastEventFilename);
				bLastEventWasAutosave = Context.IsFromAutoSave();
				bLastEventUpdatedLoadedPath = Context.IsUpdatingLoadedPath();
				LastEventPackage = Package;
			}
		});
	ON_SCOPE_EXIT
	{
		UPackage::PackageSavedWithContextEvent.Remove(SavedHandle);
		PersistenceTestCleanup(Target);
	};

	if (!PersistenceTestRequireApplySuccess(
			*this,
			TEXT("Canonical metadata fixture create succeeds"),
			Service,
			Document))
	{
		return false;
	}
	UBlackboardData* Blackboard = FindObject<UBlackboardData>(nullptr, *PersistenceTestToObjectPath(Target));
	TestNotNull(TEXT("Finds canonical metadata Blackboard"), Blackboard);
	if (!Blackboard)
	{
		return false;
	}
	UPackage* Package = Blackboard ? Blackboard->GetOutermost() : nullptr;
	const FPersistenceTestPackageMetadata Metadata = PersistenceTestCapturePackageMetadata(Package);
	FString ExpectedCanonicalFilename = CanonicalFilename;
	ExpectedCanonicalFilename = FPaths::ConvertRelativePathToFull(ExpectedCanonicalFilename);
	FPaths::NormalizeFilename(ExpectedCanonicalFilename);
	TestTrue(TEXT("Observes the installed live package before metadata refresh"), bObservedPackageBeforeMetadataRefresh);
	TestTrue(TEXT("New live package is initially PKG_InMemoryOnly"), bWasInMemoryOnlyBeforeMetadataRefresh);
	TestEqual(TEXT("LoadedPath is canonical after strict success"), Metadata.LoadedFilename, ExpectedCanonicalFilename);
	TestFalse(TEXT("Strict success clears PKG_InMemoryOnly"), Package && Package->HasAnyPackageFlags(PKG_InMemoryOnly));
	TestFalse(TEXT("Strict success clears PKG_NewlyCreated"), Package && Package->HasAnyPackageFlags(PKG_NewlyCreated));
	TestTrue(TEXT("Strict success records a valid saved hash"), Metadata.SavedHash != FIoHash());
	TestTrue(TEXT("Strict success records a positive canonical file size"), Metadata.FileSize > 0);
	TestEqual(TEXT("Strict success file size matches canonical header"), Metadata.FileSize, IFileManager::Get().FileSize(*CanonicalFilename));
	TestTrue(TEXT("Strict success emits staging and canonical save events"), MatchingEventCount >= 2);
	TestEqual(TEXT("Last save event filename is canonical"), LastEventFilename, ExpectedCanonicalFilename);
	TestFalse(TEXT("Last canonical save event is not autosave"), bLastEventWasAutosave);
	TestTrue(TEXT("Last canonical save event updates LoadedPath"), bLastEventUpdatedLoadedPath);
	TestTrue(TEXT("Last canonical save event carries the live package"), LastEventPackage == Package);

	UBlackboardKeyType* KeyTypeBefore = PersistenceTestFindKeyType(Blackboard);
	TestNotNull(TEXT("Finds canonical metadata key type"), KeyTypeBefore);
	if (!KeyTypeBefore)
	{
		return false;
	}
	const TSet<UObject*> OwnedBefore = PersistenceTestCollectOwned(Blackboard);
	Document->SetStringField(TEXT("Action"), TEXT("Update"));
	if (!PersistenceTestRequireApplySuccess(
			*this,
			TEXT("Canonical metadata no-op update succeeds"),
			Service,
			Document))
	{
		return false;
	}
	UBlackboardData* BlackboardAfter = FindObject<UBlackboardData>(nullptr, *PersistenceTestToObjectPath(Target));
	TestTrue(TEXT("Canonical metadata refresh preserves asset identity"), BlackboardAfter == Blackboard);
	if (!BlackboardAfter)
	{
		return false;
	}
	TestTrue(TEXT("Canonical metadata refresh preserves key-type identity"), PersistenceTestFindKeyType(BlackboardAfter) == KeyTypeBefore);
	const TSet<UObject*> OwnedAfter = PersistenceTestCollectOwned(BlackboardAfter);
	TestTrue(TEXT("Canonical metadata refresh preserves owned UObject identities"), OwnedAfter.Includes(OwnedBefore) && OwnedBefore.Includes(OwnedAfter));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPersistenceMetadataPreCommitRollbackTest,
	"AssetFactory.AssetDocument.Persistence.Blackboard.MetadataPreCommitFailureRestoresExactState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistenceMetadataPreCommitRollbackTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	const FString Target = PersistenceTestMakeTarget(TEXT("BB_MetadataPreCommitRollback"));
	const FString SidecarPath = FPackageName::LongPackageNameToFilename(Target, TEXT(".assetdoc.json"));
	PersistenceTestCleanup(Target, SidecarPath);
	ON_SCOPE_EXIT
	{
		PersistenceTestCleanup(Target, SidecarPath);
	};

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = PersistenceTestMakeBlackboardDocument(
		*this,
		Service,
		Target,
		TEXT("Create"),
		7);
	if (!PersistenceTestRequireApplySuccess(
			*this,
			TEXT("Creates metadata pre-commit rollback baseline"),
			Service,
			Document))
	{
		return false;
	}
	if (!PersistenceTestEstablishSynchronizedBlackboardSidecar(
			*this,
			Service,
			Target,
			SidecarPath,
			7,
			Document))
	{
		return false;
	}

	UBlackboardData* BlackboardBefore = FindObject<UBlackboardData>(
		nullptr,
		*PersistenceTestToObjectPath(Target));
	UBlackboardKeyType* KeyTypeBefore = PersistenceTestFindKeyType(BlackboardBefore);
	TestNotNull(TEXT("Finds metadata pre-commit Blackboard baseline"), BlackboardBefore);
	TestNotNull(TEXT("Finds metadata pre-commit key-type baseline"), KeyTypeBefore);
	if (!BlackboardBefore || !KeyTypeBefore)
	{
		return false;
	}

	UPackage* PackageBefore = BlackboardBefore->GetOutermost();
	const FPersistenceTestPackageMetadata MetadataBefore = PersistenceTestCapturePackageMetadata(PackageBefore);
	TArray64<uint8> PackageBytesBefore;
	TestTrue(
		TEXT("Reads metadata pre-commit baseline package bytes"),
		PersistenceTestLoadBytes(PersistenceTestToPackagePath(Target), PackageBytesBefore));

	Document->SetStringField(TEXT("Action"), TEXT("Update"));
	TArray<TSharedPtr<FJsonValue>> UpdatedKeys = Document->GetObjectField(TEXT("Body"))->GetArrayField(TEXT("Keys"));
	TSharedPtr<FJsonObject> LongKeyProperties = MakeShared<FJsonObject>();
	LongKeyProperties->SetNumberField(TEXT("DefaultValue"), 29);
	TSharedPtr<FJsonObject> LongKeyClass = MakeShared<FJsonObject>();
	LongKeyClass->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	LongKeyClass->SetStringField(TEXT("Path"), UBlackboardKeyType_Int::StaticClass()->GetPathName());
	TSharedPtr<FJsonObject> LongKey = MakeShared<FJsonObject>();
	LongKey->SetStringField(
		TEXT("Name"),
		TEXT("MetadataRollbackKey_With_A_Deliberately_Long_Name_That_Changes_The_Serialized_Package_Size"));
	LongKey->SetObjectField(TEXT("KeyTypeClass"), LongKeyClass);
	LongKey->SetObjectField(TEXT("KeyTypeProperties"), LongKeyProperties);
	UpdatedKeys.Add(MakeShared<FJsonValueObject>(LongKey));
	Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("Keys"), MoveTemp(UpdatedKeys));

	FString SidecarContents;
	TSharedRef<TJsonWriter<>> SidecarWriter = TJsonWriterFactory<>::Create(&SidecarContents);
	TestTrue(TEXT("Serializes metadata pre-commit sidecar"), FJsonSerializer::Serialize(Document.ToSharedRef(), SidecarWriter));
	TestTrue(
		TEXT("Writes metadata pre-commit sidecar"),
		PersistenceTestWriteDocumentControlled(SidecarPath, Document));
	TArray64<uint8> SidecarBytesBefore;
	TestTrue(
		TEXT("Reads metadata pre-commit sidecar bytes"),
		PersistenceTestLoadBytes(SidecarPath, SidecarBytesBefore));

	int32 CanonicalEventCount = 0;
	FString ExpectedCanonicalFilename = FPaths::ConvertRelativePathToFull(PersistenceTestToPackagePath(Target));
	FPaths::NormalizeFilename(ExpectedCanonicalFilename);
	const FDelegateHandle SavedHandle = UPackage::PackageSavedWithContextEvent.AddLambda(
		[&](const FString& Filename, UPackage* SavedPackage, FObjectPostSaveContext)
		{
			FString NormalizedFilename = FPaths::ConvertRelativePathToFull(Filename);
			FPaths::NormalizeFilename(NormalizedFilename);
			if (SavedPackage && SavedPackage->GetName() == Target && NormalizedFilename == ExpectedCanonicalFilename)
			{
				++CanonicalEventCount;
			}
		});
	ON_SCOPE_EXIT
	{
		UPackage::PackageSavedWithContextEvent.Remove(SavedHandle);
	};

	bool bObservedMetadataAfterNewBind = false;
	FPersistenceTestPackageMetadata MetadataAfterNewBind;
	FPersistenceTestScopedHookReset Hooks;
	FAssetDocumentServiceTestHooks::RunNextPersistenceCallbackAtPhase(
		EAssetDocumentServicePersistencePhase::AfterCanonicalMetadataBindBeforeCommit,
		[&]()
		{
			UBlackboardData* BoundBlackboard = FindObject<UBlackboardData>(
				nullptr,
				*PersistenceTestToObjectPath(Target));
			bObservedMetadataAfterNewBind = BoundBlackboard != nullptr;
			MetadataAfterNewBind = PersistenceTestCapturePackageMetadata(
				BoundBlackboard ? BoundBlackboard->GetOutermost() : nullptr);
		});
	FAssetDocumentServiceTestHooks::FailNextPersistenceAtPhase(
		EAssetDocumentServicePersistencePhase::AfterCanonicalMetadataBindBeforeCommit,
		PersistenceTestMakeFailure(
			TEXT("ForcedMetadataBindFailure"),
			TEXT("forced failure after canonical metadata bind before commit")));
	FAssetDocumentApplyRequest UpdateRequest;
	UpdateRequest.Document = Document;
	UpdateRequest.SourceDocumentPath = SidecarPath;
	UpdateRequest.bWriteSidecar = true;
	UpdateRequest.bSaveAsset = true;
	const FAssetDocumentResult Failure = Service.Apply(UpdateRequest);
	TestFalse(TEXT("Metadata pre-commit failure rejects update"), Failure.IsSuccess());
	TestTrue(
		FString::Printf(
			TEXT("Observes metadata after the new canonical bind; %s"),
			*PersistenceTestDescribeResult(Failure)),
		bObservedMetadataAfterNewBind);
	TestTrue(
		TEXT("New canonical bind changes private package file size"),
		MetadataAfterNewBind.FileSize != MetadataBefore.FileSize);
	TestTrue(
		TEXT("New canonical bind changes package saved hash"),
		MetadataAfterNewBind.SavedHash != MetadataBefore.SavedHash);
	TestEqual(TEXT("Failed metadata bind emits no canonical save event"), CanonicalEventCount, 0);

	TArray64<uint8> PackageBytesAfter;
	TArray64<uint8> SidecarBytesAfter;
	TestTrue(
		TEXT("Reads package after metadata pre-commit rollback"),
		PersistenceTestLoadBytes(PersistenceTestToPackagePath(Target), PackageBytesAfter));
	TestTrue(
		TEXT("Reads sidecar after metadata pre-commit rollback"),
		PersistenceTestLoadBytes(SidecarPath, SidecarBytesAfter));
	TestTrue(
		TEXT("Metadata pre-commit failure restores exact package bytes"),
		PackageBytesAfter == PackageBytesBefore);
	TestTrue(
		TEXT("Metadata pre-commit failure restores exact sidecar bytes"),
		SidecarBytesAfter == SidecarBytesBefore);
	UBlackboardData* BlackboardAfter = FindObject<UBlackboardData>(
		nullptr,
		*PersistenceTestToObjectPath(Target));
	TestTrue(TEXT("Metadata pre-commit failure preserves asset identity"), BlackboardAfter == BlackboardBefore);
	TestTrue(
		TEXT("Metadata pre-commit failure preserves key-type identity"),
		PersistenceTestFindKeyType(BlackboardAfter) == KeyTypeBefore);
	TestTrue(
		TEXT("Metadata pre-commit failure preserves package identity"),
		BlackboardAfter && BlackboardAfter->GetOutermost() == PackageBefore);
	PersistenceTestMetadataEqual(
		*this,
		PersistenceTestCapturePackageMetadata(BlackboardAfter ? BlackboardAfter->GetOutermost() : nullptr),
		MetadataBefore,
		TEXT("Metadata pre-commit failure restores package metadata"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPersistencePreVerifySegmentTamperTest,
	"AssetFactory.AssetDocument.Persistence.Blackboard.PreVerifySegmentTamperRollsBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistencePreVerifySegmentTamperTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	return PersistenceTestRunInstalledSegmentTamperCase(
		*this,
		EAssetDocumentServicePersistencePhase::BeforeInstalledPackageVerification,
		TEXT("BB_PreVerifyTamper"),
		TEXT("Pre-verify segment tamper"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPersistencePostVerifySegmentTamperTest,
	"AssetFactory.AssetDocument.Persistence.Blackboard.PostVerifySegmentTamperRollsBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistencePostVerifySegmentTamperTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	return PersistenceTestRunInstalledSegmentTamperCase(
		*this,
		EAssetDocumentServicePersistencePhase::AfterInstalledPackageVerificationBeforeMetadataBind,
		TEXT("BB_PostVerifyTamper"),
		TEXT("Post-verify segment tamper"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPersistencePostBindSegmentTamperTest,
	"AssetFactory.AssetDocument.Persistence.Blackboard.PostBindSegmentTamperRollsBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistencePostBindSegmentTamperTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	return PersistenceTestRunInstalledSegmentTamperCase(
		*this,
		EAssetDocumentServicePersistencePhase::AfterCanonicalMetadataBindBeforeCommit,
		TEXT("BB_PostBindTamper"),
		TEXT("Post-bind segment tamper"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPersistenceMetadataRecoveryDoubleFailureTest,
	"AssetFactory.AssetDocument.Persistence.Blackboard.MetadataRecoveryDoubleFailurePoisonsPackage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistenceMetadataRecoveryDoubleFailureTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	const FString Target = PersistenceTestMakeTarget(TEXT("BB_MetadataRecoveryDoubleFailure"));
	const FString ObjectPath = PersistenceTestToObjectPath(Target);
	const FString PackagePath = PersistenceTestToPackagePath(Target);
	const FString SidecarPath = FPackageName::LongPackageNameToFilename(Target, TEXT(".assetdoc.json"));
	PersistenceTestCleanup(Target, SidecarPath);
	ON_SCOPE_EXIT
	{
		PersistenceTestCleanup(Target, SidecarPath);
	};

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Initial = PersistenceTestMakeBlackboardDocument(
		*this,
		Service,
		Target,
		TEXT("Create"),
		11);
	if (!PersistenceTestRequireApplySuccess(
			*this,
			TEXT("Creates metadata double-failure baseline"),
			Service,
			Initial))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Update;
	if (!PersistenceTestEstablishSynchronizedBlackboardSidecar(
			*this,
			Service,
			Target,
			SidecarPath,
			11,
			Update)
		|| !PersistenceTestSetFirstBlackboardKeyDefault(Update, 47))
	{
		return false;
	}

	UBlackboardData* BlackboardBefore = FindObject<UBlackboardData>(nullptr, *ObjectPath);
	UBlackboardKeyType* KeyTypeBefore = PersistenceTestFindKeyType(BlackboardBefore);
	TestNotNull(TEXT("Finds metadata double-failure Blackboard baseline"), BlackboardBefore);
	TestNotNull(TEXT("Finds metadata double-failure key-type baseline"), KeyTypeBefore);
	if (!BlackboardBefore || !KeyTypeBefore)
	{
		return false;
	}

	TArray64<uint8> PackageBytesBefore;
	TestTrue(
		TEXT("Reads metadata double-failure package baseline"),
		PersistenceTestLoadBytes(PackagePath, PackageBytesBefore));
	TestTrue(
		TEXT("Writes metadata double-failure sidecar fixture"),
		PersistenceTestWriteDocumentControlled(SidecarPath, Update));
	TArray64<uint8> SidecarBytesBefore;
	TestTrue(
		TEXT("Reads metadata double-failure sidecar baseline"),
		PersistenceTestLoadBytes(SidecarPath, SidecarBytesBefore));

	int32 CanonicalEventCount = 0;
	FString ExpectedCanonicalFilename = FPaths::ConvertRelativePathToFull(PackagePath);
	FPaths::NormalizeFilename(ExpectedCanonicalFilename);
	const FDelegateHandle SavedHandle = UPackage::PackageSavedWithContextEvent.AddLambda(
		[&](const FString& Filename, UPackage* SavedPackage, FObjectPostSaveContext)
		{
			FString NormalizedFilename = FPaths::ConvertRelativePathToFull(Filename);
			FPaths::NormalizeFilename(NormalizedFilename);
			if (SavedPackage && SavedPackage->GetName() == Target && NormalizedFilename == ExpectedCanonicalFilename)
			{
				++CanonicalEventCount;
			}
		});
	ON_SCOPE_EXIT
	{
		UPackage::PackageSavedWithContextEvent.Remove(SavedHandle);
	};

	FPersistenceTestScopedHookReset Hooks;
	FAssetDocumentServiceTestHooks::FailNextPersistenceAtPhase(
		EAssetDocumentServicePersistencePhase::AfterCanonicalMetadataBindBeforeCommit,
		PersistenceTestMakeFailure(
			TEXT("ForcedNewMetadataBindFailure"),
			TEXT("forced new canonical metadata bind failure")));
	FAssetDocumentServiceTestHooks::FailNextMetadataRecoveryRefresh(
		PersistenceTestMakeFailure(
			TEXT("ForcedOldMetadataRecoveryFailure"),
			TEXT("forced old canonical metadata recovery failure")));
	FAssetDocumentApplyRequest UpdateRequest;
	UpdateRequest.Document = Update;
	UpdateRequest.SourceDocumentPath = SidecarPath;
	UpdateRequest.bWriteSidecar = true;
	UpdateRequest.bSaveAsset = true;
	const FAssetDocumentResult Failure = Service.Apply(UpdateRequest);
	TestFalse(TEXT("Metadata recovery double failure rejects Apply"), Failure.IsSuccess());
	TestTrue(
		FString::Printf(
			TEXT("Metadata recovery double failure reports restart-required poison; %s"),
			*PersistenceTestDescribeResult(Failure)),
		PersistenceTestHasDiagnostic(Failure, TEXT("AssetDocumentPackagePoisonedRestartRequired")));
	TestEqual(TEXT("Metadata recovery double failure emits no canonical event"), CanonicalEventCount, 0);

	TArray64<uint8> PackageBytesAfterFailure;
	TArray64<uint8> SidecarBytesAfterFailure;
	TestTrue(
		TEXT("Reads package after metadata recovery double failure"),
		PersistenceTestLoadBytes(PackagePath, PackageBytesAfterFailure));
	TestTrue(
		TEXT("Reads sidecar after metadata recovery double failure"),
		PersistenceTestLoadBytes(SidecarPath, SidecarBytesAfterFailure));
	TestTrue(
		TEXT("Metadata recovery double failure still restores exact disk package bytes"),
		PackageBytesAfterFailure == PackageBytesBefore);
	TestTrue(
		TEXT("Metadata recovery double failure still restores exact sidecar bytes"),
		SidecarBytesAfterFailure == SidecarBytesBefore);

	auto TestPoisonedResult = [this](const TCHAR* Operation, const FAssetDocumentResult& Result)
	{
		TestFalse(FString::Printf(TEXT("Poisoned package rejects %s"), Operation), Result.IsSuccess());
		TestTrue(
			FString::Printf(TEXT("Poisoned %s reports restart-required diagnostic"), Operation),
			PersistenceTestHasDiagnostic(Result, TEXT("AssetDocumentPackagePoisonedRestartRequired")));
	};

	TestPoisonedResult(TEXT("Apply"), Service.Apply(UpdateRequest));
	FAssetDocumentApplyFileRequest ApplyFileRequest;
	ApplyFileRequest.FilePath = SidecarPath;
	ApplyFileRequest.bSaveAsset = true;
	ApplyFileRequest.bAllowSidecarRewrite = true;
	TestPoisonedResult(TEXT("ApplyFile"), Service.ApplyFile(ApplyFileRequest));
	FAssetDocumentValidateRequest ValidateRequest;
	ValidateRequest.Document = Update;
	TestPoisonedResult(TEXT("Validate"), Service.Validate(ValidateRequest));
	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = Update;
	TestPoisonedResult(TEXT("Diff"), Service.Diff(DiffRequest));
	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = ObjectPath;
	TestPoisonedResult(TEXT("Extract"), Service.Extract(ExtractRequest));
	FAssetDocumentInspectRequest InspectRequest;
	InspectRequest.ClassOrAsset = ObjectPath;
	TestPoisonedResult(TEXT("Inspect"), Service.Inspect(InspectRequest));
	FAssetDocumentProfileRequest ProfileRequest;
	ProfileRequest.ClassOrAsset = ObjectPath;
	TestPoisonedResult(TEXT("InspectProfile"), Service.InspectProfile(ProfileRequest));

	TArray64<uint8> PackageBytesAfterRejections;
	TArray64<uint8> SidecarBytesAfterRejections;
	TestTrue(
		TEXT("Reads package after poisoned operations are rejected"),
		PersistenceTestLoadBytes(PackagePath, PackageBytesAfterRejections));
	TestTrue(
		TEXT("Reads sidecar after poisoned operations are rejected"),
		PersistenceTestLoadBytes(SidecarPath, SidecarBytesAfterRejections));
	TestTrue(
		TEXT("Poisoned operation rejection never rewrites canonical package"),
		PackageBytesAfterRejections == PackageBytesBefore);
	TestTrue(
		TEXT("Poisoned operation rejection never rewrites sidecar"),
		SidecarBytesAfterRejections == SidecarBytesBefore);
	TestEqual(TEXT("Poisoned operation rejection emits no canonical event"), CanonicalEventCount, 0);
	UBlackboardData* BlackboardAfter = FindObject<UBlackboardData>(nullptr, *ObjectPath);
	TestTrue(TEXT("Double failure preserves live asset identity"), BlackboardAfter == BlackboardBefore);
	TestTrue(
		TEXT("Double failure preserves live key-type identity"),
		PersistenceTestFindKeyType(BlackboardAfter) == KeyTypeBefore);
	const UBlackboardKeyType_Int* IntKey = Cast<UBlackboardKeyType_Int>(
		PersistenceTestFindKeyType(BlackboardAfter));
	TestNotNull(TEXT("Double failure preserves Int key type"), IntKey);
	if (IntKey)
	{
		TestEqual(TEXT("Double failure restores old key default"), IntKey->DefaultValue, 11);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPersistenceBlackboardFreshReloadTest,
	"AssetFactory.AssetDocument.Persistence.Blackboard.ForcedFreshVerificationFailureRollsBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistenceBlackboardFreshReloadTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	const FString Target = PersistenceTestMakeTarget(TEXT("BB_FreshReload"));
	PersistenceTestCleanup(Target);
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = PersistenceTestMakeBlackboardDocument(*this, Service, Target, TEXT("Create"), 11);
	if (!Document.IsValid())
	{
		return false;
	}

	FPersistenceTestScopedHookReset Hooks;
	FAssetDocumentServiceTestHooks::FailNextPersistenceAtPhase(
		EAssetDocumentServicePersistencePhase::AfterFreshReloadBeforeVerification,
		PersistenceTestMakeFailure(
			TEXT("ForcedFreshVerificationFailure"),
			TEXT("forced failure after isolated fresh reload")));
	const FAssetDocumentResult Result = PersistenceTestApply(Service, Document);
	TestFalse(TEXT("Forced post-fresh-load verification failure rejects Apply"), Result.IsSuccess());
	TestNull(TEXT("Fresh verification failure removes the live new asset"), FindObject<UObject>(nullptr, *PersistenceTestToObjectPath(Target)));
	TestFalse(TEXT("Fresh verification failure never installs a canonical package"), IFileManager::Get().FileExists(*PersistenceTestToPackagePath(Target)));
	FAssetDocumentServiceTestHooks::Clear();
	if (!PersistenceTestRequireApplySuccess(
			*this,
			TEXT("Retry proves fresh verification failure cleanup is complete"),
			Service,
			Document))
	{
		PersistenceTestCleanup(Target);
		return false;
	}
	PersistenceTestCleanup(Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPersistenceBlackboardSidecarPreparationFailureTest,
	"AssetFactory.AssetDocument.Persistence.Blackboard.SidecarPreparationFailureRollsBackBeforeInstall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistenceBlackboardSidecarPreparationFailureTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	const FString Target = PersistenceTestMakeTarget(TEXT("BB_SidecarPreparationFailure"));
	const FString SidecarPath = FPackageName::LongPackageNameToFilename(Target, TEXT(".assetdoc.json"));
	PersistenceTestCleanup(Target, SidecarPath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SidecarPath), true);
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Initial = PersistenceTestMakeBlackboardDocument(*this, Service, Target, TEXT("Create"), 7);
	if (!PersistenceTestRequireApplySuccess(
			*this,
			TEXT("Creates strict-sidecar preparation baseline"),
			Service,
			Initial))
	{
		PersistenceTestCleanup(Target, SidecarPath);
		return false;
	}

	TArray64<uint8> PackageBefore;
	TestTrue(TEXT("Reads strict-sidecar preparation baseline package bytes"), PersistenceTestLoadBytes(PersistenceTestToPackagePath(Target), PackageBefore));
	UBlackboardData* BlackboardBefore = FindObject<UBlackboardData>(nullptr, *PersistenceTestToObjectPath(Target));
	TestNotNull(TEXT("Finds strict-sidecar preparation live baseline"), BlackboardBefore);
	const int32 KeyCountBefore = BlackboardBefore ? BlackboardBefore->Keys.Num() : 0;
	UBlackboardKeyType* KeyTypeBefore = PersistenceTestFindKeyType(BlackboardBefore);
	TestNotNull(TEXT("Finds strict-sidecar preparation key-type baseline"), KeyTypeBefore);
	if (!BlackboardBefore || !KeyTypeBefore)
	{
		PersistenceTestCleanup(Target, SidecarPath);
		return false;
	}

	TSharedPtr<FJsonObject> Update = PersistenceTestMakeBlackboardDocument(*this, Service, Target, TEXT("Update"), 23);
	TSharedPtr<FJsonObject> MalformedSync = MakeShared<FJsonObject>();
	MalformedSync->SetStringField(TEXT("schemaVersion"), TEXT("bad"));
	TSharedPtr<FJsonObject> MalformedMeta = MakeShared<FJsonObject>();
	MalformedMeta->SetObjectField(TEXT("sync"), MalformedSync);
	Update->SetObjectField(TEXT("_meta"), MalformedMeta);
	FString SidecarContents;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SidecarContents);
	TestTrue(TEXT("Serializes malformed strict-sidecar fixture"), FJsonSerializer::Serialize(Update.ToSharedRef(), Writer));
	TestTrue(TEXT("Writes malformed strict-sidecar fixture"), FFileHelper::SaveStringToFile(SidecarContents, *SidecarPath));
	TArray64<uint8> SidecarBefore;
	TestTrue(TEXT("Reads malformed strict-sidecar fixture bytes"), PersistenceTestLoadBytes(SidecarPath, SidecarBefore));

	FAssetDocumentApplyFileRequest Request;
	Request.FilePath = SidecarPath;
	Request.bSaveAsset = true;
	Request.bAllowSidecarRewrite = true;
	const FAssetDocumentResult Result = Service.ApplyFile(Request);
	TestFalse(TEXT("Strict sidecar preparation failure rejects ApplyFile"), Result.IsSuccess());
	TestTrue(TEXT("Strict sidecar preparation failure preserves its diagnostic"), Result.Message.Contains(TEXT("schemaVersion")));
	TestFalse(
		TEXT("Strict sidecar preparation failure is not reported as a skipped success"),
		Result.Payload.IsValid() && Result.Payload->HasField(TEXT("sidecar_sync_update_skipped")));

	TArray64<uint8> PackageAfter;
	TArray64<uint8> SidecarAfter;
	TestTrue(TEXT("Reads package after strict sidecar preparation rollback"), PersistenceTestLoadBytes(PersistenceTestToPackagePath(Target), PackageAfter));
	TestTrue(TEXT("Reads sidecar after strict sidecar preparation rollback"), PersistenceTestLoadBytes(SidecarPath, SidecarAfter));
	TestTrue(TEXT("Strict sidecar preparation failure preserves exact package bytes"), PackageAfter == PackageBefore);
	TestTrue(TEXT("Strict sidecar preparation failure preserves exact sidecar bytes"), SidecarAfter == SidecarBefore);
	UBlackboardData* BlackboardAfter = FindObject<UBlackboardData>(nullptr, *PersistenceTestToObjectPath(Target));
	TestTrue(TEXT("Strict sidecar preparation rollback preserves asset identity"), BlackboardAfter == BlackboardBefore);
	TestEqual(TEXT("Strict sidecar preparation rollback restores exact key count"), BlackboardAfter ? BlackboardAfter->Keys.Num() : 0, KeyCountBefore);
	if (BlackboardAfter)
	{
		UBlackboardKeyType* KeyTypeAfter = PersistenceTestFindKeyType(BlackboardAfter);
		TestTrue(TEXT("Strict sidecar preparation rollback preserves key-type identity"), KeyTypeAfter == KeyTypeBefore);
		const UBlackboardKeyType_Int* IntKey = Cast<UBlackboardKeyType_Int>(KeyTypeAfter);
		TestNotNull(TEXT("Strict sidecar preparation rollback restores Int key type"), IntKey);
		if (IntKey)
		{
			TestEqual(TEXT("Strict sidecar preparation rollback restores key default"), IntKey->DefaultValue, 7);
		}
	}
	PersistenceTestCleanup(Target, SidecarPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPersistenceBlackboardSidecarFailureTest,
	"AssetFactory.AssetDocument.Persistence.Blackboard.SidecarAtomicFailureRestoresPackageAndFails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistenceBlackboardSidecarFailureTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	const FString Target = PersistenceTestMakeTarget(TEXT("BB_SidecarFailure"));
	const FString SidecarPath = FPackageName::LongPackageNameToFilename(Target, TEXT(".assetdoc.json"));
	PersistenceTestCleanup(Target, SidecarPath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SidecarPath), true);
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Initial = PersistenceTestMakeBlackboardDocument(*this, Service, Target, TEXT("Create"), 3);
	if (!PersistenceTestRequireApplySuccess(
			*this,
			TEXT("Creates Blackboard durable baseline"),
			Service,
			Initial))
	{
		PersistenceTestCleanup(Target, SidecarPath);
		return false;
	}
	TSharedPtr<FJsonObject> Update;
	if (!PersistenceTestEstablishSynchronizedBlackboardSidecar(
			*this,
			Service,
			Target,
			SidecarPath,
			3,
			Update)
		|| !PersistenceTestSetFirstBlackboardKeyDefault(Update, 17))
	{
		PersistenceTestCleanup(Target, SidecarPath);
		return false;
	}

	TArray64<uint8> PackageBefore;
	TestTrue(TEXT("Reads Blackboard baseline package bytes"), PersistenceTestLoadBytes(PersistenceTestToPackagePath(Target), PackageBefore));
	UBlackboardData* BlackboardBefore = FindObject<UBlackboardData>(nullptr, *PersistenceTestToObjectPath(Target));
	TestNotNull(TEXT("Finds Blackboard live baseline"), BlackboardBefore);
	const FPersistenceTestPackageMetadata MetadataBefore = PersistenceTestCapturePackageMetadata(
		BlackboardBefore ? BlackboardBefore->GetOutermost() : nullptr);
	const int32 KeyCountBefore = BlackboardBefore ? BlackboardBefore->Keys.Num() : 0;
	UBlackboardKeyType* KeyTypeBefore = PersistenceTestFindKeyType(BlackboardBefore);
	TestNotNull(TEXT("Finds Blackboard key-type identity baseline"), KeyTypeBefore);
	if (!BlackboardBefore || !KeyTypeBefore)
	{
		PersistenceTestCleanup(Target, SidecarPath);
		return false;
	}
	FString SidecarContents;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SidecarContents);
	TestTrue(TEXT("Serializes Blackboard sidecar fixture"), FJsonSerializer::Serialize(Update.ToSharedRef(), Writer));
	TestTrue(
		TEXT("Writes Blackboard sidecar fixture"),
		PersistenceTestWriteDocumentControlled(SidecarPath, Update));
	TArray64<uint8> SidecarBefore;
	TestTrue(TEXT("Reads Blackboard sidecar fixture bytes"), PersistenceTestLoadBytes(SidecarPath, SidecarBefore));

	bool bReachedAfterPackageInstall = false;
	FPersistenceTestScopedHookReset Hooks;
	FAssetDocumentServiceTestHooks::RunNextPersistenceCallbackAtPhase(
		EAssetDocumentServicePersistencePhase::AfterPackageInstall,
		[&]()
		{
			bReachedAfterPackageInstall = true;
			FAssetDocumentAtomicFile::FailNextWriteAtForTest(EAssetDocumentAtomicFileFailurePoint::DirectoryFlush);
			FAssetDocumentAtomicFile::ForceNextCommittedRenameRollbackFailureForTest();
		});
	FAssetDocumentApplyFileRequest Request;
	Request.FilePath = SidecarPath;
	Request.bSaveAsset = true;
	Request.bAllowSidecarRewrite = true;
	const FAssetDocumentResult Result = Service.ApplyFile(Request);
	TestTrue(
		FString::Printf(
			TEXT("Atomic sidecar failure reaches AfterPackageInstall; %s"),
			*PersistenceTestDescribeResult(Result)),
		bReachedAfterPackageInstall);
	TestFalse(TEXT("Atomic sidecar failure rejects ApplyFile"), Result.IsSuccess());
	TestTrue(
		FString::Printf(
			TEXT("Atomic sidecar failure reports the injected AtomicFile stage; %s"),
			*PersistenceTestDescribeResult(Result)),
		Result.Message.Contains(TEXT("AtomicFile.DirectoryFlush"))
			|| Result.Diagnostics.ContainsByPredicate(
				[](const FAssetDocumentDiagnostic& Diagnostic)
				{
					return Diagnostic.Message.Contains(TEXT("AtomicFile.DirectoryFlush"));
				}));
	TestFalse(
		TEXT("Atomic sidecar failure is not reported as a skipped success"),
		Result.Payload.IsValid() && Result.Payload->HasField(TEXT("sidecar_sync_update_skipped")));

	TArray64<uint8> PackageAfter;
	TArray64<uint8> SidecarAfter;
	TestTrue(TEXT("Reads package after sidecar rollback"), PersistenceTestLoadBytes(PersistenceTestToPackagePath(Target), PackageAfter));
	TestTrue(TEXT("Reads sidecar after atomic failure"), PersistenceTestLoadBytes(SidecarPath, SidecarAfter));
	TestTrue(TEXT("Sidecar failure restores exact prior package bytes"), PackageAfter == PackageBefore);
	TestTrue(TEXT("Atomic sidecar failure preserves exact sidecar bytes"), SidecarAfter == SidecarBefore);
	UBlackboardData* BlackboardAfter = FindObject<UBlackboardData>(nullptr, *PersistenceTestToObjectPath(Target));
	TestTrue(TEXT("Blackboard asset pointer identity is exact after rollback"), BlackboardAfter == BlackboardBefore);
	PersistenceTestMetadataEqual(
		*this,
		PersistenceTestCapturePackageMetadata(BlackboardAfter ? BlackboardAfter->GetOutermost() : nullptr),
		MetadataBefore,
		TEXT("Sidecar failure restores package metadata"));
	TestEqual(TEXT("Blackboard local key count is restored"), BlackboardAfter ? BlackboardAfter->Keys.Num() : 0, KeyCountBefore);
	if (BlackboardAfter)
	{
		UBlackboardKeyType* KeyTypeAfter = PersistenceTestFindKeyType(BlackboardAfter);
		TestTrue(TEXT("Blackboard key-type pointer identity is exact after rollback"), KeyTypeAfter == KeyTypeBefore);
		const UBlackboardKeyType_Int* IntKey = Cast<UBlackboardKeyType_Int>(KeyTypeAfter);
		TestNotNull(TEXT("Restored Blackboard key type remains Int"), IntKey);
		if (IntKey)
		{
			TestEqual(TEXT("Restored Blackboard key default is exact"), IntKey->DefaultValue, 3);
		}
	}
	PersistenceTestCleanup(Target, SidecarPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPersistenceSidecarRollbackFailurePoisonTest,
	"AssetFactory.AssetDocument.Persistence.Blackboard.SidecarRollbackFailurePoisonsPackage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistenceSidecarRollbackFailurePoisonTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	const FString Target = PersistenceTestMakeTarget(TEXT("BB_SidecarRollbackPoison"));
	const FString SidecarPath = FPackageName::LongPackageNameToFilename(
		Target,
		TEXT(".assetdoc.json"));
	PersistenceTestCleanup(Target, SidecarPath);
	ON_SCOPE_EXIT
	{
		PersistenceTestCleanup(Target, SidecarPath);
	};

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Initial = PersistenceTestMakeBlackboardDocument(
		*this,
		Service,
		Target,
		TEXT("Create"),
		13);
	if (!PersistenceTestRequireApplySuccess(
			*this,
			TEXT("Creates sidecar rollback poison baseline"),
			Service,
			Initial))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Update;
	if (!PersistenceTestEstablishSynchronizedBlackboardSidecar(
			*this,
			Service,
			Target,
			SidecarPath,
			13,
			Update)
		|| !PersistenceTestSetFirstBlackboardKeyDefault(Update, 43)
		|| !PersistenceTestWriteDocumentControlled(SidecarPath, Update))
	{
		return false;
	}

	bool bReachedMetadataBind = false;
	FPersistenceTestScopedHookReset Hooks;
	FAssetDocumentServiceTestHooks::RunNextPersistenceCallbackAtPhase(
		EAssetDocumentServicePersistencePhase::AfterCanonicalMetadataBindBeforeCommit,
		[&]()
		{
			bReachedMetadataBind = true;
			// The forced metadata failure returns directly into FailPersistence;
			// its first atomic operation is restoration of the installed sidecar.
			FAssetDocumentAtomicFile::FailNextWriteAtForTest(
				EAssetDocumentAtomicFileFailurePoint::Open);
		});
	FAssetDocumentServiceTestHooks::FailNextPersistenceAtPhase(
		EAssetDocumentServicePersistencePhase::AfterCanonicalMetadataBindBeforeCommit,
		PersistenceTestMakeFailure(
			TEXT("ForcedSidecarRollbackPoisonPrimaryFailure"),
			TEXT("forced primary failure before sidecar rollback injection")));
	FAssetDocumentApplyRequest Request;
	Request.Document = Update;
	Request.SourceDocumentPath = SidecarPath;
	Request.bWriteSidecar = true;
	Request.bSaveAsset = true;
	const FAssetDocumentResult Failure = Service.Apply(Request);
	TestTrue(
		FString::Printf(
			TEXT("Sidecar rollback poison reaches metadata bind; %s"),
			*PersistenceTestDescribeResult(Failure)),
		bReachedMetadataBind);
	TestFalse(TEXT("Sidecar rollback failure rejects Apply"), Failure.IsSuccess());
	TestTrue(
		FString::Printf(
			TEXT("Sidecar rollback failure is reported; %s"),
			*PersistenceTestDescribeResult(Failure)),
		PersistenceTestHasDiagnostic(Failure, TEXT("AssetDocumentSidecarRollbackFailed")));
	TestTrue(
		FString::Printf(
			TEXT("Sidecar rollback failure poisons the package; %s"),
			*PersistenceTestDescribeResult(Failure)),
		PersistenceTestHasDiagnostic(Failure, TEXT("AssetDocumentPackagePoisonedRestartRequired")));

	FAssetDocumentValidateRequest ValidateRequest;
	ValidateRequest.Document = Update;
	const FAssetDocumentResult PoisonedValidate = Service.Validate(ValidateRequest);
	TestFalse(TEXT("Sidecar rollback poison rejects Validate"), PoisonedValidate.IsSuccess());
	TestTrue(
		FString::Printf(
			TEXT("Sidecar rollback poison Validate requires restart; %s"),
			*PersistenceTestDescribeResult(PoisonedValidate)),
		PersistenceTestHasDiagnostic(
			PoisonedValidate,
			TEXT("AssetDocumentPackagePoisonedRestartRequired")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPersistenceApplyFileNoSaveSidecarTest,
	"AssetFactory.AssetDocument.Persistence.Blackboard.ApplyFileWithoutSaveLeavesSidecarBytesExact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistenceApplyFileNoSaveSidecarTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	const FString Target = PersistenceTestMakeTarget(TEXT("BB_NoSaveSidecar"));
	const FString SidecarPath = FPackageName::LongPackageNameToFilename(Target, TEXT(".assetdoc.json"));
	PersistenceTestCleanup(Target, SidecarPath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SidecarPath), true);
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Initial = PersistenceTestMakeBlackboardDocument(*this, Service, Target, TEXT("Create"), 5);
	if (!PersistenceTestRequireApplySuccess(
			*this,
			TEXT("Creates no-save sidecar baseline"),
			Service,
			Initial))
	{
		PersistenceTestCleanup(Target, SidecarPath);
		return false;
	}

	TSharedPtr<FJsonObject> Update = PersistenceTestMakeBlackboardDocument(*this, Service, Target, TEXT("Update"), 31);
	FString SidecarContents;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SidecarContents);
	TestTrue(TEXT("Serializes no-save sidecar fixture"), FJsonSerializer::Serialize(Update.ToSharedRef(), Writer));
	TestTrue(TEXT("Writes no-save sidecar fixture"), FFileHelper::SaveStringToFile(SidecarContents, *SidecarPath));
	TArray64<uint8> SidecarBefore;
	TestTrue(TEXT("Reads no-save sidecar bytes"), PersistenceTestLoadBytes(SidecarPath, SidecarBefore));

	FAssetDocumentApplyFileRequest Request;
	Request.FilePath = SidecarPath;
	Request.bSaveAsset = false;
	Request.bAllowSidecarRewrite = true;
	const FAssetDocumentResult Result = Service.ApplyFile(Request);
	if (!Result.IsSuccess())
	{
		AddError(FString::Printf(
			TEXT("ApplyFile without save still applies in memory: %s"),
			*PersistenceTestDescribeResult(Result)));
		PersistenceTestCleanup(Target, SidecarPath);
		return false;
	}
	TestFalse(TEXT("ApplyFile without save never reports a sidecar write"), Result.bWroteSidecar);
	TArray64<uint8> SidecarAfter;
	TestTrue(TEXT("Reads no-save sidecar bytes after ApplyFile"), PersistenceTestLoadBytes(SidecarPath, SidecarAfter));
	TestTrue(TEXT("ApplyFile without save preserves exact sidecar bytes"), SidecarAfter == SidecarBefore);
	PersistenceTestCleanup(Target, SidecarPath);
	return true;
}

#endif
