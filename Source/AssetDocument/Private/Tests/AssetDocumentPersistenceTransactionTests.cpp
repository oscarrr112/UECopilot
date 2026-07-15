// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentAtomicFile.h"
#include "AssetDocumentCanonicalJson.h"
#include "AssetDocumentService.h"
#include "AssetDocumentServiceTestHooks.h"
#include "TestDataAsset.h"

#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "IO/IoHash.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "EdGraph/EdGraph.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
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
	UObject* Asset = FindObject<UObject>(nullptr, *PersistenceTestToObjectPath(Target));
	if (!Asset && IFileManager::Get().FileExists(*PackagePath))
	{
		Asset = LoadObject<UObject>(nullptr, *PersistenceTestToObjectPath(Target));
	}
	// Persistence rollback intentionally replaces the package inode/timestamp.
	// Remove the test file before marking the loaded asset deleted so the asset
	// registry cannot mistake that controlled replacement for an external edit.
	IFileManager::Get().Delete(*PackagePath, false, true);
	if (Asset)
	{
		TArray<UObject*> Objects{Asset};
		ObjectTools::DeleteObjectsUnchecked(Objects);
	}
	IFileManager::Get().Delete(*PackagePath, false, true);
	if (!SidecarPath.IsEmpty())
	{
		IFileManager::Get().Delete(*SidecarPath, false, true);
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
	FIoHash SavedHash;
	int64 FileSize = 0;
	bool bDirty = false;
};

FPersistenceTestPackageMetadata PersistenceTestCapturePackageMetadata(const UPackage* Package)
{
	FPersistenceTestPackageMetadata Metadata;
	if (Package)
	{
		Metadata.LoadedFilename = Package->GetLoadedPath().GetLocalFullPath();
		FPaths::NormalizeFilename(Metadata.LoadedFilename);
		Metadata.PackageFlags = Package->GetPackageFlags();
		Metadata.SavedHash = Package->GetSavedHash();
		Metadata.FileSize = Package->GetFileSize();
		Metadata.bDirty = Package->IsDirty();
	}
	return Metadata;
}

void PersistenceTestMetadataEqual(
	FAutomationTestBase& Test,
	const FPersistenceTestPackageMetadata& Actual,
	const FPersistenceTestPackageMetadata& Expected,
	const TCHAR* Prefix)
{
	Test.TestEqual(FString::Printf(TEXT("%s loaded filename"), Prefix), Actual.LoadedFilename, Expected.LoadedFilename);
	Test.TestEqual(FString::Printf(TEXT("%s package flags"), Prefix), Actual.PackageFlags, Expected.PackageFlags);
	Test.TestTrue(FString::Printf(TEXT("%s saved hash"), Prefix), Actual.SavedHash == Expected.SavedHash);
	Test.TestEqual(FString::Printf(TEXT("%s file size"), Prefix), Actual.FileSize, Expected.FileSize);
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
	TestTrue(TEXT("Creates the generic standard-save baseline"), PersistenceTestApply(Service, PersistenceTestMakeGenericDocument(Target, TEXT("Create"), TEXT("before"), 7)).IsSuccess());
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
	TestTrue(TEXT("Generic Apply bypasses the strict package-install hook"), Result.IsSuccess());
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
	TestTrue(TEXT("Creates durable BehaviorTree identity baseline"), Document.IsValid() && PersistenceTestApply(Service, Document).IsSuccess());

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
	TestTrue(TEXT("Creates package-atomic rollback baseline"), Initial.IsValid() && PersistenceTestApply(Service, Initial).IsSuccess());
	UBlackboardData* BlackboardBefore = FindObject<UBlackboardData>(nullptr, *PersistenceTestToObjectPath(Target));
	UBlackboardKeyType* KeyTypeBefore = PersistenceTestFindKeyType(BlackboardBefore);
	const FString CanonicalBefore = PersistenceTestExtractCanonical(*this, Service, Target);
	TArray64<uint8> PackageBefore;
	TestNotNull(TEXT("Finds package-atomic rollback baseline"), BlackboardBefore);
	TestNotNull(TEXT("Finds package-atomic rollback key type"), KeyTypeBefore);
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
	TestTrue(TEXT("Creates the snapshot-readiness baseline"), Initial.IsValid() && PersistenceTestApply(Service, Initial).IsSuccess());
	UBlackboardData* BlackboardBefore = FindObject<UBlackboardData>(nullptr, *PersistenceTestToObjectPath(Target));
	UBlackboardKeyType* KeyTypeBefore = PersistenceTestFindKeyType(BlackboardBefore);
	const FString CanonicalBefore = PersistenceTestExtractCanonical(*this, Service, Target);
	TArray64<uint8> DiskBefore;
	TestNotNull(TEXT("Finds the snapshot-readiness live baseline"), BlackboardBefore);
	TestNotNull(TEXT("Finds the snapshot-readiness key type"), KeyTypeBefore);
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
	TestTrue(TEXT("Retry after new strict-package install failure succeeds"), PersistenceTestApply(Service, Document).IsSuccess());
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
	TestTrue(TEXT("Creates the stale-sibling baseline"), Initial.IsValid() && PersistenceTestApply(Service, Initial).IsSuccess());
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
	TestTrue(TEXT("Retry commits the strict package update"), PersistenceTestApply(Service, Update).IsSuccess());
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
	const FDelegateHandle SavedHandle = UPackage::PackageSavedWithContextEvent.AddLambda(
		[&](const FString& Filename, UPackage* Package, FObjectPostSaveContext Context)
		{
			if (Package && Package->GetName() == Target)
			{
				++MatchingEventCount;
				LastEventFilename = Filename;
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

	const FAssetDocumentResult CreateResult = PersistenceTestApply(Service, Document);
	TestTrue(TEXT("Canonical metadata fixture create succeeds"), CreateResult.IsSuccess());
	UBlackboardData* Blackboard = FindObject<UBlackboardData>(nullptr, *PersistenceTestToObjectPath(Target));
	TestNotNull(TEXT("Finds canonical metadata Blackboard"), Blackboard);
	UPackage* Package = Blackboard ? Blackboard->GetOutermost() : nullptr;
	const FPersistenceTestPackageMetadata Metadata = PersistenceTestCapturePackageMetadata(Package);
	FString ExpectedCanonicalFilename = CanonicalFilename;
	FPaths::NormalizeFilename(ExpectedCanonicalFilename);
	TestEqual(TEXT("LoadedPath is canonical after strict success"), Metadata.LoadedFilename, ExpectedCanonicalFilename);
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
	const TSet<UObject*> OwnedBefore = PersistenceTestCollectOwned(Blackboard);
	Document->SetStringField(TEXT("Action"), TEXT("Update"));
	const FAssetDocumentResult UpdateResult = PersistenceTestApply(Service, Document);
	TestTrue(TEXT("Canonical metadata no-op update succeeds"), UpdateResult.IsSuccess());
	UBlackboardData* BlackboardAfter = FindObject<UBlackboardData>(nullptr, *PersistenceTestToObjectPath(Target));
	TestTrue(TEXT("Canonical metadata refresh preserves asset identity"), BlackboardAfter == Blackboard);
	TestTrue(TEXT("Canonical metadata refresh preserves key-type identity"), PersistenceTestFindKeyType(BlackboardAfter) == KeyTypeBefore);
	const TSet<UObject*> OwnedAfter = PersistenceTestCollectOwned(BlackboardAfter);
	TestTrue(TEXT("Canonical metadata refresh preserves owned UObject identities"), OwnedAfter.Includes(OwnedBefore) && OwnedBefore.Includes(OwnedAfter));
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
	TestTrue(TEXT("Retry proves fresh verification failure cleanup is complete"), PersistenceTestApply(Service, Document).IsSuccess());
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
	TestTrue(TEXT("Creates strict-sidecar preparation baseline"), Initial.IsValid() && PersistenceTestApply(Service, Initial).IsSuccess());

	TArray64<uint8> PackageBefore;
	TestTrue(TEXT("Reads strict-sidecar preparation baseline package bytes"), PersistenceTestLoadBytes(PersistenceTestToPackagePath(Target), PackageBefore));
	UBlackboardData* BlackboardBefore = FindObject<UBlackboardData>(nullptr, *PersistenceTestToObjectPath(Target));
	TestNotNull(TEXT("Finds strict-sidecar preparation live baseline"), BlackboardBefore);
	const int32 KeyCountBefore = BlackboardBefore ? BlackboardBefore->Keys.Num() : 0;
	UBlackboardKeyType* KeyTypeBefore = PersistenceTestFindKeyType(BlackboardBefore);
	TestNotNull(TEXT("Finds strict-sidecar preparation key-type baseline"), KeyTypeBefore);

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
	TestTrue(TEXT("Creates Blackboard durable baseline"), Initial.IsValid() && PersistenceTestApply(Service, Initial).IsSuccess());

	TArray64<uint8> PackageBefore;
	TestTrue(TEXT("Reads Blackboard baseline package bytes"), PersistenceTestLoadBytes(PersistenceTestToPackagePath(Target), PackageBefore));
	UBlackboardData* BlackboardBefore = FindObject<UBlackboardData>(nullptr, *PersistenceTestToObjectPath(Target));
	TestNotNull(TEXT("Finds Blackboard live baseline"), BlackboardBefore);
	const FPersistenceTestPackageMetadata MetadataBefore = PersistenceTestCapturePackageMetadata(
		BlackboardBefore ? BlackboardBefore->GetOutermost() : nullptr);
	const int32 KeyCountBefore = BlackboardBefore ? BlackboardBefore->Keys.Num() : 0;
	UBlackboardKeyType* KeyTypeBefore = PersistenceTestFindKeyType(BlackboardBefore);
	TestNotNull(TEXT("Finds Blackboard key-type identity baseline"), KeyTypeBefore);
	TSharedPtr<FJsonObject> Update = PersistenceTestMakeBlackboardDocument(*this, Service, Target, TEXT("Update"), 17);
	FString SidecarContents;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SidecarContents);
	TestTrue(TEXT("Serializes Blackboard sidecar fixture"), FJsonSerializer::Serialize(Update.ToSharedRef(), Writer));
	TestTrue(TEXT("Writes Blackboard sidecar fixture"), FFileHelper::SaveStringToFile(SidecarContents, *SidecarPath));
	TArray64<uint8> SidecarBefore;
	TestTrue(TEXT("Reads Blackboard sidecar fixture bytes"), PersistenceTestLoadBytes(SidecarPath, SidecarBefore));

	FPersistenceTestScopedHookReset Hooks;
	FAssetDocumentServiceTestHooks::RunNextPersistenceCallbackAtPhase(
		EAssetDocumentServicePersistencePhase::AfterPackageInstall,
		[]()
		{
			FAssetDocumentAtomicFile::FailNextWriteAtForTest(EAssetDocumentAtomicFileFailurePoint::DirectoryFlush);
			FAssetDocumentAtomicFile::ForceNextCommittedRenameRollbackFailureForTest();
		});
	FAssetDocumentApplyFileRequest Request;
	Request.FilePath = SidecarPath;
	Request.bSaveAsset = true;
	Request.bAllowSidecarRewrite = true;
	const FAssetDocumentResult Result = Service.ApplyFile(Request);
	TestFalse(TEXT("Atomic sidecar failure rejects ApplyFile"), Result.IsSuccess());
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
	TestTrue(TEXT("Creates no-save sidecar baseline"), Initial.IsValid() && PersistenceTestApply(Service, Initial).IsSuccess());

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
	TestTrue(TEXT("ApplyFile without save still applies in memory"), Result.IsSuccess());
	TestFalse(TEXT("ApplyFile without save never reports a sidecar write"), Result.bWroteSidecar);
	TArray64<uint8> SidecarAfter;
	TestTrue(TEXT("Reads no-save sidecar bytes after ApplyFile"), PersistenceTestLoadBytes(SidecarPath, SidecarAfter));
	TestTrue(TEXT("ApplyFile without save preserves exact sidecar bytes"), SidecarAfter == SidecarBefore);
	PersistenceTestCleanup(Target, SidecarPath);
	return true;
}

#endif
