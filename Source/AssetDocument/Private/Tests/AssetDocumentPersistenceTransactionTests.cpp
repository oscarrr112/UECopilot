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
	UObject* Asset = FindObject<UObject>(nullptr, *PersistenceTestToObjectPath(Target));
	if (!Asset && IFileManager::Get().FileExists(*PersistenceTestToPackagePath(Target)))
	{
		Asset = LoadObject<UObject>(nullptr, *PersistenceTestToObjectPath(Target));
	}
	if (Asset)
	{
		TArray<UObject*> Objects{Asset};
		ObjectTools::DeleteObjectsUnchecked(Objects);
	}
	IFileManager::Get().Delete(*PersistenceTestToPackagePath(Target), false, true);
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
	FAssetDocumentPersistenceExistingInstallFailureTest,
	"AssetFactory.AssetDocument.Persistence.Existing.PackageInstallFailureRestoresLiveAndDisk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistenceExistingInstallFailureTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	const FString Target = PersistenceTestMakeTarget(TEXT("DA_ExistingInstallFailure"));
	PersistenceTestCleanup(Target);
	FAssetDocumentService Service;
	TestTrue(TEXT("Creates the durable baseline"), PersistenceTestApply(Service, PersistenceTestMakeGenericDocument(Target, TEXT("Create"), TEXT("before"), 7)).IsSuccess());

	TArray64<uint8> DiskBefore;
	TestTrue(TEXT("Reads the durable baseline bytes"), PersistenceTestLoadBytes(PersistenceTestToPackagePath(Target), DiskBefore));
	UTestDataAsset* Asset = FindObject<UTestDataAsset>(nullptr, *PersistenceTestToObjectPath(Target));
	TestNotNull(TEXT("Finds the live durable baseline"), Asset);
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
	TestFalse(TEXT("Failure after package install rejects Apply"), Result.IsSuccess());
	TestEqual(TEXT("Existing live string is restored"), Asset->TestString, FString(TEXT("before")));
	TestEqual(TEXT("Existing live integer is restored"), Asset->TestInt, 7);

	TArray64<uint8> DiskAfter;
	TestTrue(TEXT("Reads package bytes after rollback"), PersistenceTestLoadBytes(PersistenceTestToPackagePath(Target), DiskAfter));
	TestTrue(TEXT("Existing package bytes are restored exactly"), DiskAfter == DiskBefore);
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
	FAssetDocumentPersistenceNewInstallFailureTest,
	"AssetFactory.AssetDocument.Persistence.New.PackageInstallFailureRemovesLiveAndDisk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPersistenceNewInstallFailureTest::RunTest(const FString&)
{
	using namespace AssetDocumentPersistenceTransactionTests;
	const FString Target = PersistenceTestMakeTarget(TEXT("DA_NewInstallFailure"));
	PersistenceTestCleanup(Target);
	FAssetDocumentService Service;
	FPersistenceTestScopedHookReset Hooks;
	FAssetDocumentServiceTestHooks::FailNextPersistenceAtPhase(
		EAssetDocumentServicePersistencePhase::AfterPackageInstall,
		PersistenceTestMakeFailure(TEXT("ForcedNewPackageInstallFailure"), TEXT("forced new-package failure after install")));
	const FAssetDocumentResult Result = PersistenceTestApply(
		Service,
		PersistenceTestMakeGenericDocument(Target, TEXT("Create"), TEXT("created"), 23));
	TestFalse(TEXT("New-package install failure rejects Apply"), Result.IsSuccess());
	TestFalse(TEXT("New-package install failure removes canonical package"), IFileManager::Get().FileExists(*PersistenceTestToPackagePath(Target)));
	TestNull(TEXT("New-package install failure removes the live asset"), FindObject<UObject>(nullptr, *PersistenceTestToObjectPath(Target)));

	FAssetDocumentServiceTestHooks::Clear();
	TestTrue(
		TEXT("Retry after new-package install failure succeeds"),
		PersistenceTestApply(Service, PersistenceTestMakeGenericDocument(Target, TEXT("Create"), TEXT("retry"), 29)).IsSuccess());
	PersistenceTestCleanup(Target);
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
	UBlackboardKeyType* KeyTypeBefore = BlackboardBefore && BlackboardBefore->Keys.Num() == 1
		? BlackboardBefore->Keys[0].KeyType.Get()
		: nullptr;
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
	TestEqual(TEXT("Strict sidecar preparation rollback restores one key"), BlackboardAfter ? BlackboardAfter->Keys.Num() : 0, 1);
	if (BlackboardAfter && BlackboardAfter->Keys.Num() == 1)
	{
		TestTrue(TEXT("Strict sidecar preparation rollback preserves key-type identity"), BlackboardAfter->Keys[0].KeyType.Get() == KeyTypeBefore);
		const UBlackboardKeyType_Int* IntKey = Cast<UBlackboardKeyType_Int>(BlackboardAfter->Keys[0].KeyType.Get());
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
	UBlackboardKeyType* KeyTypeBefore = BlackboardBefore && BlackboardBefore->Keys.Num() == 1
		? BlackboardBefore->Keys[0].KeyType.Get()
		: nullptr;
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
			FAssetDocumentAtomicFile::FailNextWriteAtForTest(EAssetDocumentAtomicFileFailurePoint::Rename);
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
	TestEqual(TEXT("Blackboard local key count is restored"), BlackboardAfter ? BlackboardAfter->Keys.Num() : 0, 1);
	if (BlackboardAfter && BlackboardAfter->Keys.Num() == 1)
	{
		TestTrue(TEXT("Blackboard key-type pointer identity is exact after rollback"), BlackboardAfter->Keys[0].KeyType.Get() == KeyTypeBefore);
		const UBlackboardKeyType_Int* IntKey = Cast<UBlackboardKeyType_Int>(BlackboardAfter->Keys[0].KeyType.Get());
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
