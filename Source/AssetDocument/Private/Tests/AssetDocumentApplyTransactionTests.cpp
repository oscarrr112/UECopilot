// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentService.h"

#include "AssetDocumentCanonicalJson.h"
#include "AssetDocumentLifecycle.h"
#include "AssetDocumentServiceTestHooks.h"
#include "Profiles/BehaviorTreeAssetDocumentMaterializer.h"
#include "Profiles/BlackboardDataAssetDocumentProfile.h"
#include "TestDataAsset.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardData.h"
#include "Dom/JsonValue.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "UMGEditorModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectHash.h"
#include "WidgetBlueprint.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AssetDocumentApplyTransactionTests
{
FString MakeUniqueTarget(const TCHAR* Stem)
{
	return FString::Printf(
		TEXT("/Game/AssetDocumentTests/Task7b2/%s_%s"),
		Stem,
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
}

FString ToObjectPath(const FString& Target)
{
	return FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
}

FString ToPackageFileName(const FString& Target)
{
	return FPackageName::LongPackageNameToFilename(Target, FPackageName::GetAssetPackageExtension());
}

void CleanupAsset(const FString& Target)
{
	const FString ObjectPath = ToObjectPath(Target);
	const FString PackageFileName = ToPackageFileName(Target);
	UObject* Asset = FindObject<UObject>(nullptr, *ObjectPath);
	if (!Asset && IFileManager::Get().FileExists(*PackageFileName))
	{
		Asset = LoadObject<UObject>(nullptr, *ObjectPath);
	}
	if (Asset)
	{
		TArray<UObject*> Objects{Asset};
		ObjectTools::DeleteObjectsUnchecked(Objects);
	}
	IFileManager::Get().Delete(*PackageFileName, false, true);
}

bool RegistryContains(const FString& ObjectPath)
{
	IAssetRegistry& Registry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	return Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid();
}

class FScopedRegistryEvents
{
public:
	explicit FScopedRegistryEvents(const FString& InObjectPath)
		: Registry(FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get())
		, ObjectPath(InObjectPath)
	{
		AddedHandle = Registry.OnAssetAdded().AddLambda([this](const FAssetData& AssetData)
		{
			if (AssetData.GetObjectPathString() == ObjectPath)
			{
				++Added;
			}
		});
		RemovedHandle = Registry.OnAssetRemoved().AddLambda([this](const FAssetData& AssetData)
		{
			if (AssetData.GetObjectPathString() == ObjectPath)
			{
				++Removed;
			}
		});
	}

	~FScopedRegistryEvents()
	{
		Registry.OnAssetAdded().Remove(AddedHandle);
		Registry.OnAssetRemoved().Remove(RemovedHandle);
	}

	int32 Added = 0;
	int32 Removed = 0;

private:
	IAssetRegistry& Registry;
	FString ObjectPath;
	FDelegateHandle AddedHandle;
	FDelegateHandle RemovedHandle;
};

class FScopedServiceHookReset
{
public:
	FScopedServiceHookReset()
	{
		FAssetDocumentServiceTestHooks::Clear();
	}

	~FScopedServiceHookReset()
	{
		FAssetDocumentServiceTestHooks::Clear();
	}
};

FAssetDocumentDiagnostic MakeDiagnostic(
	const FString& Path,
	const FString& Code,
	const FString& Message)
{
	FAssetDocumentDiagnostic Diagnostic;
	Diagnostic.Path = Path;
	Diagnostic.Code = Code;
	Diagnostic.Message = Message;
	return Diagnostic;
}

bool HasDiagnostic(const FAssetDocumentResult& Result, const FString& Code, const FString& Path)
{
	return Result.Diagnostics.ContainsByPredicate([&Code, &Path](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == Code && Diagnostic.Path == Path;
	});
}

TSharedRef<FJsonObject> MakeAssetRef(const FString& ObjectPath)
{
	TSharedRef<FJsonObject> AssetRef = MakeShared<FJsonObject>();
	AssetRef->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	AssetRef->SetStringField(TEXT("Path"), ObjectPath);
	return AssetRef;
}

TSet<FString> CollectNamedTransientArtifacts(const FString& NameFragment)
{
	TSet<FString> Result;
	for (TObjectIterator<UObject> It; It; ++It)
	{
		UObject* Object = *It;
		UPackage* Package = Object ? Object->GetOutermost() : nullptr;
		if (Object
			&& Package
			&& (Package->HasAnyFlags(RF_Transient)
				|| Package->GetName().StartsWith(TEXT("/Temp/AssetDocumentPreview_")))
			&& Object->GetName().Contains(NameFragment))
		{
			Result.Add(Object->GetPathName());
		}
	}
	return Result;
}

TSet<FString> CollectNamedRootedArtifacts(const FString& NameFragment)
{
	TSet<FString> Result;
	for (TObjectIterator<UObject> It; It; ++It)
	{
		UObject* Object = *It;
		if (Object && Object->IsRooted() && Object->GetName().Contains(NameFragment))
		{
			Result.Add(Object->GetPathName());
		}
	}
	return Result;
}

void VerifyBlueprintGeneratedIdentity(
	FAutomationTestBase& Test,
	const FString& Label,
	UObject* Asset,
	UClass* ExpectedGeneratedClassType)
{
	UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
	Test.TestNotNull(*FString::Printf(TEXT("%s retry is a Blueprint"), *Label), Blueprint);
	if (!Blueprint)
	{
		return;
	}

	Test.TestNotNull(*FString::Printf(TEXT("%s has GeneratedClass"), *Label), Blueprint->GeneratedClass.Get());
	if (Blueprint->GeneratedClass)
	{
		Test.TestEqual(
			*FString::Printf(TEXT("%s GeneratedClass has exact generated-class type"), *Label),
			Blueprint->GeneratedClass->GetClass(),
			ExpectedGeneratedClassType);
		Test.TestTrue(
			*FString::Printf(TEXT("%s GeneratedClass points to Self"), *Label),
			Blueprint->GeneratedClass->ClassGeneratedBy == Blueprint);
		Test.TestTrue(
			*FString::Printf(TEXT("%s GeneratedClass is in the live package"), *Label),
			Blueprint->GeneratedClass->GetOutermost() == Blueprint->GetOutermost());
	}

	Test.TestNotNull(*FString::Printf(TEXT("%s has SkeletonGeneratedClass"), *Label), Blueprint->SkeletonGeneratedClass.Get());
	if (Blueprint->SkeletonGeneratedClass)
	{
		Test.TestEqual(
			*FString::Printf(TEXT("%s SkeletonGeneratedClass has exact generated-class type"), *Label),
			Blueprint->SkeletonGeneratedClass->GetClass(),
			ExpectedGeneratedClassType);
		Test.TestTrue(
			*FString::Printf(TEXT("%s SkeletonGeneratedClass points to Self"), *Label),
			Blueprint->SkeletonGeneratedClass->ClassGeneratedBy == Blueprint);
		Test.TestTrue(
			*FString::Printf(TEXT("%s SkeletonGeneratedClass is in the live package"), *Label),
			Blueprint->SkeletonGeneratedClass->GetOutermost() == Blueprint->GetOutermost());
	}
}

class FScopedWidgetBlueprintCreatedEvents
{
public:
	explicit FScopedWidgetBlueprintCreatedEvents(const FString& InNameFragment)
		: Module(FModuleManager::LoadModuleChecked<IUMGEditorModule>(TEXT("UMGEditor")))
		, NameFragment(InNameFragment)
	{
		Handle = Module.OnWidgetBlueprintCreated().AddLambda([this](IUMGEditorModule::FWidgetBlueprintCreatedArgs Args)
		{
			if (Args.Blueprint && Args.Blueprint->GetName().Contains(NameFragment))
			{
				++Count;
			}
		});
	}

	~FScopedWidgetBlueprintCreatedEvents()
	{
		Module.OnWidgetBlueprintCreated().Remove(Handle);
	}

	int32 Count = 0;

private:
	IUMGEditorModule& Module;
	FString NameFragment;
	FDelegateHandle Handle;
};

TSet<UObject*> CollectRecursiveOwned(UObject* Asset)
{
	TArray<UObject*> Objects;
	if (Asset)
	{
		GetObjectsWithOuter(Asset, Objects, true);
	}
	TSet<UObject*> Result;
	for (UObject* Object : Objects)
	{
		Result.Add(Object);
	}
	return Result;
}

TSet<UObject*> CollectPackageObjects(UPackage* Package)
{
	TArray<UObject*> Objects;
	if (Package)
	{
		GetObjectsWithOuter(Package, Objects, true);
	}
	TSet<UObject*> Result;
	for (UObject* Object : Objects)
	{
		if (Object)
		{
			Result.Add(Object);
		}
	}
	return Result;
}

bool HaveExactStringSet(const TSet<FString>& A, const TSet<FString>& B)
{
	if (A.Num() != B.Num())
	{
		return false;
	}
	for (const FString& Value : A)
	{
		if (!B.Contains(Value))
		{
			return false;
		}
	}
	return true;
}

void DiscardTestPackageContents(UPackage* Package)
{
	const TSet<UObject*> Objects = CollectPackageObjects(Package);
	for (UObject* Object : Objects)
	{
		if (!Object || Objects.Contains(Object->GetOuter()))
		{
			continue;
		}
		if (Object->IsRooted())
		{
			Object->RemoveFromRoot();
		}
		const FName TransientName = MakeUniqueObjectName(
			GetTransientPackage(),
			Object->GetClass(),
			FName(*FString::Printf(TEXT("AssetDocumentTestDiscard_%s"), *Object->GetName())));
		Object->Rename(
			*TransientName.ToString(),
			GetTransientPackage(),
			REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
	}
	for (UObject* Object : Objects)
	{
		if (Object)
		{
			if (Object->IsRooted())
			{
				Object->RemoveFromRoot();
			}
			Object->ClearFlags(RF_Public | RF_Standalone);
			Object->SetFlags(RF_Transient);
			Object->MarkAsGarbage();
		}
	}
	if (Package)
	{
		if (Package->IsRooted())
		{
			Package->RemoveFromRoot();
		}
		Package->ClearDirtyFlag();
		Package->MarkAsGarbage();
	}
}

const FBlackboardEntry* FindEngineDerivedSelfKey(const UBlackboardData* Blackboard)
{
	return Blackboard ? Blackboard->Keys.FindByPredicate([](const FBlackboardEntry& Entry)
	{
		const UBlackboardKeyType_Object* ObjectKey = Cast<UBlackboardKeyType_Object>(Entry.KeyType);
		return Entry.EntryName == FBlackboard::KeySelf
			&& ObjectKey
			&& ObjectKey->GetClass() == UBlackboardKeyType_Object::StaticClass()
			&& ObjectKey->BaseClass == AActor::StaticClass()
			&& ObjectKey->DefaultValue == nullptr
			&& Entry.EntryDescription.IsEmpty()
			&& Entry.EntryCategory.IsNone()
			&& !Entry.bInstanceSynced;
	}) : nullptr;
}

bool HaveExactObjectSet(const TSet<UObject*>& A, const TSet<UObject*>& B)
{
	if (A.Num() != B.Num())
	{
		return false;
	}
	for (UObject* Object : A)
	{
		if (!B.Contains(Object))
		{
			return false;
		}
	}
	return true;
}

FString DescribeOwnedObject(UObject* Object)
{
	if (!Object)
	{
		return TEXT("<null>");
	}

	return FString::Printf(
		TEXT("Ptr=%p Path=%s Class=%s Outer=%s Flags=0x%08x InternalFlags=0x%08x Rooted=%s Garbage=%s"),
		static_cast<const void*>(Object),
		*Object->GetPathName(),
		*GetNameSafe(Object->GetClass()),
		*GetPathNameSafe(Object->GetOuter()),
		static_cast<uint32>(Object->GetFlags()),
		static_cast<uint32>(Object->GetInternalFlags()),
		Object->IsRooted() ? TEXT("true") : TEXT("false"),
		Object->HasAnyInternalFlags(EInternalObjectFlags::Garbage) ? TEXT("true") : TEXT("false"));
}

struct FOwnedObjectSetSnapshot
{
	TSet<UPTRINT> Addresses;
	TMap<UPTRINT, FString> Descriptions;
};

FOwnedObjectSetSnapshot SnapshotOwnedObjectSet(const TSet<UObject*>& Objects)
{
	FOwnedObjectSetSnapshot Snapshot;
	for (UObject* Object : Objects)
	{
		const UPTRINT Address = reinterpret_cast<UPTRINT>(Object);
		Snapshot.Addresses.Add(Address);
		Snapshot.Descriptions.Add(Address, DescribeOwnedObject(Object));
	}
	return Snapshot;
}

void AddOwnedObjectSetDiff(
	FAutomationTestBase& Test,
	const FString& Label,
	const FOwnedObjectSetSnapshot& Before,
	const FOwnedObjectSetSnapshot& After)
{
	for (const UPTRINT Address : Before.Addresses)
	{
		if (!After.Addresses.Contains(Address))
		{
			Test.AddInfo(FString::Printf(
				TEXT("%s missing: %s"),
				*Label,
				*Before.Descriptions.FindRef(Address)));
		}
	}
	for (const UPTRINT Address : After.Addresses)
	{
		if (!Before.Addresses.Contains(Address))
		{
			Test.AddInfo(FString::Printf(
				TEXT("%s added: %s"),
				*Label,
				*After.Descriptions.FindRef(Address)));
		}
	}
}

FString ExtractCanonical(
	FAutomationTestBase& Test,
	FAssetDocumentService& Service,
	const FString& ObjectPath,
	const FString& Label)
{
	FAssetDocumentExtractRequest Request;
	Request.AssetPath = ObjectPath;
	Request.bDiffOnly = false;
	const FAssetDocumentResult Result = Service.Extract(Request);
	Test.TestTrue(*FString::Printf(TEXT("%s extracts"), *Label), Result.IsSuccess());
	Test.TestTrue(*FString::Printf(TEXT("%s extract has payload"), *Label), Result.Payload.IsValid());
	return Result.Payload.IsValid()
		? FAssetDocumentCanonicalJson::WriteCanonicalJson(MakeShared<FJsonValueObject>(Result.Payload.ToSharedRef()))
		: FString();
}

TSharedPtr<FJsonObject> MakeTemplate(
	FAutomationTestBase& Test,
	FAssetDocumentService& Service,
	const FString& Target,
	const FString& ClassPath,
	const FString& Action)
{
	FAssetDocumentTemplateRequest Request;
	Request.Target = Target;
	Request.Class = ClassPath;
	const FAssetDocumentResult Result = Service.CreateTemplate(Request);
	Test.TestTrue(*FString::Printf(TEXT("CreateTemplate succeeds for %s"), *ClassPath), Result.IsSuccess());
	Test.TestTrue(*FString::Printf(TEXT("CreateTemplate returns %s payload"), *ClassPath), Result.Payload.IsValid());
	if (!Result.Payload.IsValid())
	{
		return nullptr;
	}
	Result.Payload->SetStringField(TEXT("Action"), Action);
	return Result.Payload;
}

TSharedPtr<FJsonObject> MakeBlueprintFamilyDocument(
	FAutomationTestBase& Test,
	FAssetDocumentService& Service,
	const FString& Target,
	const FString& ClassPath)
{
	TSharedPtr<FJsonObject> Document = MakeTemplate(
		Test,
		Service,
		Target,
		ClassPath,
		TEXT("Create"));
	if (!Document.IsValid() || ClassPath != TEXT("/Script/Engine.AnimBlueprint"))
	{
		return Document;
	}

	TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));
	Body->SetObjectField(
		TEXT("TargetSkeleton"),
		MakeAssetRef(TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh_Skeleton.DefaultSkeletalMesh_Skeleton")));
	TSharedPtr<FJsonObject> Preview = Body->GetObjectField(TEXT("Preview"));
	Preview->SetObjectField(
		TEXT("PreviewSkeletalMesh"),
		MakeAssetRef(TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh.DefaultSkeletalMesh")));
	return Document;
}

TSharedPtr<FJsonObject> GetObjectField(
	FAutomationTestBase& Test,
	const TSharedPtr<FJsonObject>& Object,
	const FString& Field,
	const FString& Label)
{
	const TSharedPtr<FJsonObject>* Value = nullptr;
	const bool bFound = Object.IsValid()
		&& Object->TryGetObjectField(Field, Value)
		&& Value
		&& Value->IsValid();
	Test.TestTrue(*FString::Printf(TEXT("%s contains object %s"), *Label, *Field), bFound);
	return bFound ? *Value : nullptr;
}

TSharedPtr<FJsonObject> MakeGenericDocument(
	const FString& Target,
	const FString& Action,
	const FString& TestString,
	int32 TestInt)
{
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetStringField(TEXT("TestString"), TestString);
	Properties->SetNumberField(TEXT("TestInt"), TestInt);

	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("AssetType"), TEXT("GenericAsset"));
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), TEXT("/Script/AssetFactory.TestDataAsset"));
	Document->SetStringField(TEXT("Action"), Action);
	Document->SetObjectField(TEXT("Properties"), Properties);
	return Document;
}

TSharedRef<FJsonObject> MakeClassRef(UClass* Class)
{
	TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
	Ref->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	Ref->SetStringField(TEXT("Path"), Class->GetPathName());
	return Ref;
}

TSharedRef<FJsonObject> MakeBlackboardKey(const FString& Name, int32 DefaultValue)
{
	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetNumberField(TEXT("DefaultValue"), DefaultValue);

	TSharedRef<FJsonObject> Key = MakeShared<FJsonObject>();
	Key->SetStringField(TEXT("Name"), Name);
	Key->SetObjectField(TEXT("KeyTypeClass"), MakeClassRef(UBlackboardKeyType_Int::StaticClass()));
	Key->SetObjectField(TEXT("KeyTypeProperties"), Properties);
	return Key;
}

TSharedPtr<FJsonObject> MakeBlackboardDocument(
	FAutomationTestBase& Test,
	FAssetDocumentService& Service,
	const FString& Target,
	const FString& Action,
	const TArray<TSharedRef<FJsonObject>>& Keys)
{
	TSharedPtr<FJsonObject> Document = MakeTemplate(
		Test,
		Service,
		Target,
		TEXT("/Script/AIModule.BlackboardData"),
		Action);
	TSharedPtr<FJsonObject> Body = GetObjectField(Test, Document, TEXT("Body"), TEXT("Blackboard template"));
	if (!Body.IsValid())
	{
		return Document;
	}

	TArray<TSharedPtr<FJsonValue>> KeyValues;
	for (const TSharedRef<FJsonObject>& Key : Keys)
	{
		KeyValues.Add(MakeShared<FJsonValueObject>(Key));
	}
	Body->SetArrayField(TEXT("Keys"), MoveTemp(KeyValues));
	return Document;
}

TSharedPtr<FJsonObject> MakePosition(double X, double Y)
{
	TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
	Position->SetNumberField(TEXT("X"), X);
	Position->SetNumberField(TEXT("Y"), Y);
	return Position;
}

TSharedPtr<FJsonObject> MakeEditor(double X, double Y)
{
	TSharedPtr<FJsonObject> Editor = MakeShared<FJsonObject>();
	Editor->SetObjectField(TEXT("Position"), MakePosition(X, Y));
	return Editor;
}

TSharedPtr<FJsonObject> MakeBehaviorNode(
	const FString& Id,
	const FString& ClassPath,
	const FString& NodeName,
	double X,
	double Y,
	TArray<TSharedPtr<FJsonValue>> Children = {})
{
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetStringField(TEXT("NodeName"), NodeName);

	TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("Id"), Id);
	Node->SetStringField(TEXT("Class"), ClassPath);
	Node->SetObjectField(TEXT("Properties"), Properties);
	Node->SetArrayField(TEXT("Decorators"), {});
	Node->SetArrayField(TEXT("Services"), {});
	Node->SetArrayField(TEXT("Children"), MoveTemp(Children));
	Node->SetObjectField(TEXT("Editor"), MakeEditor(X, Y));
	return Node;
}

TSharedPtr<FJsonObject> MakeBehaviorTreeJson()
{
	TArray<TSharedPtr<FJsonValue>> Children;
	Children.Add(MakeShared<FJsonValueObject>(MakeBehaviorNode(
		FGuid::NewGuid().ToString(EGuidFormats::Digits),
		TEXT("/Script/AIModule.BTTask_Wait"),
		TEXT("Initial wait"),
		100.0,
		300.0)));

	TSharedPtr<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetStringField(TEXT("GraphGuid"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
	Tree->SetObjectField(TEXT("Root"), MakeBehaviorNode(
		FGuid::NewGuid().ToString(EGuidFormats::Digits),
		TEXT("/Script/AIModule.BTComposite_Selector"),
		TEXT("Initial selector"),
		100.0,
		100.0,
		MoveTemp(Children)));
	Tree->SetArrayField(TEXT("Comments"), {});
	return Tree;
}

TSharedPtr<FJsonObject> MakeBehaviorTreeDocument(
	FAutomationTestBase& Test,
	FAssetDocumentService& Service,
	const FString& Target,
	const FString& Action)
{
	TSharedPtr<FJsonObject> Document = MakeTemplate(
		Test,
		Service,
		Target,
		TEXT("/Script/AIModule.BehaviorTree"),
		Action);
	TSharedPtr<FJsonObject> Body = GetObjectField(Test, Document, TEXT("Body"), TEXT("BehaviorTree template"));
	if (Body.IsValid())
	{
		Body->SetObjectField(TEXT("Tree"), MakeBehaviorTreeJson());
	}
	return Document;
}

FAssetDocumentResult ApplyDocument(
	FAssetDocumentService& Service,
	const TSharedPtr<FJsonObject>& Document,
	bool bSaveAsset)
{
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = bSaveAsset;
	return Service.Apply(Request);
}

FAssetDocumentResult ApplyWithoutSave(
	FAssetDocumentService& Service,
	const TSharedPtr<FJsonObject>& Document)
{
	return ApplyDocument(Service, Document, false);
}

void ExpectNoGhost(
	FAutomationTestBase& Test,
	const FString& Label,
	const FString& Target)
{
	const FString ObjectPath = ToObjectPath(Target);
	UObject* GhostObject = FindObject<UObject>(nullptr, *ObjectPath);
	UPackage* GhostPackage = FindPackage(nullptr, *Target);
	auto AddGhostInfo = [&Test, &Label](const TCHAR* Kind, UObject* Object)
	{
		if (Object)
		{
			Test.AddInfo(FString::Printf(
				TEXT("%s ghost %s: Path=%s Class=%s Outer=%s Flags=0x%08x InternalFlags=0x%08x Rooted=%s Garbage=%s"),
				*Label,
				Kind,
				*Object->GetPathName(),
				*GetNameSafe(Object->GetClass()),
				*GetPathNameSafe(Object->GetOuter()),
				static_cast<uint32>(Object->GetFlags()),
				static_cast<uint32>(Object->GetInternalFlags()),
				Object->IsRooted() ? TEXT("true") : TEXT("false"),
				Object->HasAnyInternalFlags(EInternalObjectFlags::Garbage) ? TEXT("true") : TEXT("false")));
		}
	};
	AddGhostInfo(TEXT("object"), GhostObject);
	AddGhostInfo(TEXT("package"), GhostPackage);
	Test.TestNull(*FString::Printf(TEXT("%s leaves no object"), *Label), GhostObject);
	Test.TestNull(*FString::Printf(TEXT("%s leaves no package"), *Label), GhostPackage);
	Test.TestFalse(*FString::Printf(TEXT("%s leaves no registry entry"), *Label), RegistryContains(ObjectPath));
	Test.TestFalse(
		*FString::Printf(TEXT("%s leaves no package file"), *Label),
		IFileManager::Get().FileExists(*ToPackageFileName(Target)));
}

bool RunNewFailureAndRetry(
	FAutomationTestBase& Test,
	FAssetDocumentService& Service,
	const FString& Label,
	const FString& Target,
	const TSharedPtr<FJsonObject>& Document,
	EAssetDocumentServiceApplyPhase Phase,
	bool bExpectedLiveMaterialization,
	bool bCheckTransientArtifacts = false,
	TFunction<void(UObject*)> VerifyRetry = {},
	int32* OutWidgetPreviewCreatedEvents = nullptr)
{
	CleanupAsset(Target);
	FScopedServiceHookReset HookReset;
	const FString AssetName = FPackageName::GetLongPackageAssetName(Target);
	const TSet<FString> TransientArtifactsBefore = bCheckTransientArtifacts
		? CollectNamedTransientArtifacts(AssetName)
		: TSet<FString>();
	const TSet<FString> RootedArtifactsBefore = bCheckTransientArtifacts
		? CollectNamedRootedArtifacts(AssetName)
		: TSet<FString>();
	const FAssetDocumentDiagnostic Primary = MakeDiagnostic(
		TEXT("/Apply/Phase"),
		TEXT("ForcedAssetDocumentServicePhaseFailure"),
		FString::Printf(TEXT("Forced %s failure"), *Label));
	FAssetDocumentServiceTestHooks::FailNextApplyAtPhase(Phase, Primary);

	{
		FScopedRegistryEvents Events(ToObjectPath(Target));
		TUniquePtr<FScopedWidgetBlueprintCreatedEvents> WidgetEvents;
		if (OutWidgetPreviewCreatedEvents)
		{
			WidgetEvents = MakeUnique<FScopedWidgetBlueprintCreatedEvents>(AssetName);
		}
		const FAssetDocumentResult Failed = ApplyWithoutSave(Service, Document);
		if (WidgetEvents)
		{
			*OutWidgetPreviewCreatedEvents = WidgetEvents->Count;
		}
		Test.TestFalse(*FString::Printf(TEXT("%s forced apply fails"), *Label), Failed.IsSuccess());
		Test.TestTrue(
			*FString::Printf(TEXT("%s keeps primary diagnostic"), *Label),
			HasDiagnostic(Failed, Primary.Code, Primary.Path));
		Test.TestFalse(
			*FString::Printf(TEXT("%s emits no cleanup detach diagnostic"), *Label),
			HasDiagnostic(Failed, TEXT("AssetDocumentCleanupDetachFailed"), TEXT("/Apply/Rollback")));
		Test.TestEqual(
			*FString::Printf(TEXT("%s AssetAdded count follows phase contract"), *Label),
			Events.Added,
			bExpectedLiveMaterialization ? 1 : 0);
		Test.TestEqual(
			*FString::Printf(TEXT("%s AssetRemoved count follows phase contract"), *Label),
			Events.Removed,
			bExpectedLiveMaterialization ? 1 : 0);
	}

	ExpectNoGhost(Test, Label, Target);
	if (bCheckTransientArtifacts)
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		const TSet<FString> TransientArtifactsAfter = CollectNamedTransientArtifacts(AssetName);
		bool bExactTransientArtifacts = TransientArtifactsAfter.Num() == TransientArtifactsBefore.Num();
		for (const FString& Artifact : TransientArtifactsBefore)
		{
			bExactTransientArtifacts = bExactTransientArtifacts && TransientArtifactsAfter.Contains(Artifact);
		}
		Test.TestTrue(
			*FString::Printf(TEXT("%s leaves no transient Blueprint companion artifact"), *Label),
			bExactTransientArtifacts);

		const TSet<FString> RootedArtifactsAfter = CollectNamedRootedArtifacts(AssetName);
		bool bExactRootedArtifacts = RootedArtifactsAfter.Num() == RootedArtifactsBefore.Num();
		for (const FString& Artifact : RootedArtifactsBefore)
		{
			bExactRootedArtifacts = bExactRootedArtifacts && RootedArtifactsAfter.Contains(Artifact);
		}
		Test.TestTrue(
			*FString::Printf(TEXT("%s leaves no newly rooted Blueprint companion artifact"), *Label),
			bExactRootedArtifacts);
	}
	FAssetDocumentServiceTestHooks::Clear();

	const FAssetDocumentResult Retry = ApplyWithoutSave(Service, Document);
	Test.TestTrue(*FString::Printf(TEXT("%s retry succeeds"), *Label), Retry.IsSuccess());
	UObject* RetryAsset = FindObject<UObject>(nullptr, *ToObjectPath(Target));
	Test.TestNotNull(
		*FString::Printf(TEXT("%s retry materializes object"), *Label),
		RetryAsset);
	Test.TestTrue(
		*FString::Printf(TEXT("%s retry registers object"), *Label),
		RegistryContains(ToObjectPath(Target)));
	if (VerifyRetry)
	{
		VerifyRetry(RetryAsset);
	}

	CleanupAsset(Target);
	return true;
}

bool RunLifecycleFailureAndRetry(
	FAutomationTestBase& Test,
	FAssetDocumentService& Service,
	const FString& Label,
	const FString& Target,
	const TSharedPtr<FJsonObject>& Document,
	EAssetDocumentLifecycleCreatePhase Phase,
	bool bInjectCleanupDiagnostic)
{
	CleanupAsset(Target);
	FScopedServiceHookReset HookReset;
	const FAssetDocumentDiagnostic Primary = MakeDiagnostic(
		TEXT("/Apply/Lifecycle"),
		TEXT("ForcedAssetDocumentLifecycleCreateFailure"),
		FString::Printf(TEXT("Forced %s lifecycle failure"), *Label));
	const FAssetDocumentDiagnostic Cleanup = MakeDiagnostic(
		TEXT("/Apply/Rollback"),
		TEXT("ForcedAssetDocumentLifecycleCleanupFailure"),
		FString::Printf(TEXT("Forced %s cleanup verification failure"), *Label));
	FAssetDocumentServiceTestHooks::FailNextLifecycleCreateAtPhase(Phase, Primary);
	if (bInjectCleanupDiagnostic)
	{
		FAssetDocumentServiceTestHooks::FailNextLifecycleCleanupVerification(Cleanup);
	}

	{
		FScopedRegistryEvents Events(ToObjectPath(Target));
		const FAssetDocumentResult Failed = ApplyWithoutSave(Service, Document);
		Test.TestFalse(*FString::Printf(TEXT("%s lifecycle apply fails"), *Label), Failed.IsSuccess());
		Test.TestTrue(
			*FString::Printf(TEXT("%s keeps lifecycle primary diagnostic"), *Label),
			HasDiagnostic(Failed, Primary.Code, Primary.Path));
		Test.TestEqual(*FString::Printf(TEXT("%s emits no AssetAdded"), *Label), Events.Added, 0);
		Test.TestEqual(*FString::Printf(TEXT("%s emits no AssetRemoved"), *Label), Events.Removed, 0);
		if (bInjectCleanupDiagnostic)
		{
			Test.TestTrue(
				*FString::Printf(TEXT("%s appends cleanup diagnostic"), *Label),
				HasDiagnostic(Failed, Cleanup.Code, Cleanup.Path));
			if (Failed.Diagnostics.Num() >= 2)
			{
				Test.TestEqual(
					*FString::Printf(TEXT("%s primary diagnostic stays first"), *Label),
					Failed.Diagnostics[0].Code,
					Primary.Code);
				Test.TestEqual(
					*FString::Printf(TEXT("%s cleanup diagnostic is appended"), *Label),
					Failed.Diagnostics.Last().Code,
					Cleanup.Code);
			}
		}
	}

	ExpectNoGhost(Test, Label, Target);
	CleanupAsset(Target);
	FAssetDocumentServiceTestHooks::Clear();
	const FAssetDocumentResult Retry = ApplyWithoutSave(Service, Document);
	Test.TestTrue(*FString::Printf(TEXT("%s retry succeeds"), *Label), Retry.IsSuccess());
	CleanupAsset(Target);
	return true;
}

bool RunPreExistingBlueprintFamilyBodyFailureAndRetry(
	FAutomationTestBase& Test,
	FAssetDocumentService& Service,
	const FString& Label,
	const FString& ClassPath,
	UClass* ExpectedAssetClass,
	UClass* ExpectedGeneratedClassType)
{
	const FString Target = FString::Printf(
		TEXT("/Game/AssetDocumentTests/Task7b2/%s_PreExisting_%s"),
		*Label,
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString ObjectPath = ToObjectPath(Target);
	const FString AssetName = FPackageName::GetLongPackageAssetName(Target);
	CleanupAsset(Target);
	FScopedServiceHookReset HookReset;

	UPackage* PreExistingPackage = CreatePackage(*Target);
	Test.TestNotNull(*FString::Printf(TEXT("%s pre-existing package fixture creates"), *Label), PreExistingPackage);
	if (!PreExistingPackage)
	{
		return false;
	}
	TStrongObjectPtr<UPackage> PackageGuard(PreExistingPackage);
	UTestDataAsset* BaselineSentinel = NewObject<UTestDataAsset>(
		PreExistingPackage,
		TEXT("BaselineSentinel"),
		RF_Transactional);
	Test.TestNotNull(*FString::Printf(TEXT("%s baseline sentinel creates"), *Label), BaselineSentinel);
	if (!BaselineSentinel)
	{
		DiscardTestPackageContents(PreExistingPackage);
		PackageGuard.Reset();
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		return false;
	}
	TStrongObjectPtr<UTestDataAsset> SentinelGuard(BaselineSentinel);
	TWeakObjectPtr<UTestDataAsset> BaselineSentinelWeak(BaselineSentinel);
	Test.TestFalse(
		*FString::Printf(TEXT("%s baseline sentinel has no Standalone retention"), *Label),
		BaselineSentinel->HasAnyFlags(RF_Standalone));
	PreExistingPackage->SetDirtyFlag(true);

	const TSet<UObject*> PackageObjectsBefore = CollectPackageObjects(PreExistingPackage);
	const FOwnedObjectSetSnapshot PackageSnapshotBefore = SnapshotOwnedObjectSet(PackageObjectsBefore);
	const TSet<FString> TransientArtifactsBefore = CollectNamedTransientArtifacts(AssetName);
	const TSet<FString> RootedArtifactsBefore = CollectNamedRootedArtifacts(AssetName);
	const TSharedPtr<FJsonObject> Document = MakeBlueprintFamilyDocument(
		Test,
		Service,
		Target,
		ClassPath);
	if (!Document.IsValid())
	{
		DiscardTestPackageContents(PreExistingPackage);
		SentinelGuard.Reset();
		PackageGuard.Reset();
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		return false;
	}
	BaselineSentinel = nullptr;
	SentinelGuard.Reset();

	const FAssetDocumentDiagnostic Primary = MakeDiagnostic(
		TEXT("/Apply/AfterNewLiveBody"),
		TEXT("ForcedPreExistingBlueprintBodyFailure"),
		FString::Printf(TEXT("Forced %s failure after live Body apply"), *Label));
	bool bLateBodyCompanionCreated = false;
	TWeakObjectPtr<UObject> LateBodyCompanionWeak;
	FAssetDocumentServiceTestHooks::FailNextApplyAtPhase(
		EAssetDocumentServiceApplyPhase::AfterNewLiveBody,
		Primary,
		[PreExistingPackage, AssetName, &bLateBodyCompanionCreated, &LateBodyCompanionWeak]()
		{
			const FName CompanionName = MakeUniqueObjectName(
				PreExistingPackage,
				UTestDataAsset::StaticClass(),
				FName(*FString::Printf(TEXT("REINST_%s_GeneratedClassCompanion"), *AssetName)));
			UObject* LateBodyCompanion = NewObject<UTestDataAsset>(
				PreExistingPackage,
				CompanionName,
				RF_Public | RF_Standalone);
			bLateBodyCompanionCreated = LateBodyCompanion != nullptr;
			LateBodyCompanionWeak = LateBodyCompanion;
		});

	{
		FScopedRegistryEvents Events(ObjectPath);
		const FAssetDocumentResult Failed = ApplyWithoutSave(Service, Document);
		Test.TestFalse(*FString::Printf(TEXT("%s forced Body apply fails"), *Label), Failed.IsSuccess());
		Test.TestTrue(
			*FString::Printf(TEXT("%s keeps primary diagnostic"), *Label),
			HasDiagnostic(Failed, Primary.Code, Primary.Path));
		Test.TestFalse(
			*FString::Printf(TEXT("%s emits no cleanup detach diagnostic"), *Label),
			HasDiagnostic(Failed, TEXT("AssetDocumentCleanupDetachFailed"), TEXT("/Apply/Rollback")));
		Test.TestEqual(*FString::Printf(TEXT("%s emits one AssetAdded"), *Label), Events.Added, 1);
		Test.TestEqual(*FString::Printf(TEXT("%s emits one AssetRemoved"), *Label), Events.Removed, 1);
	}
	Test.TestTrue(
		*FString::Printf(TEXT("%s failure callback creates a late package companion"), *Label),
		bLateBodyCompanionCreated);

	Test.TestNull(
		*FString::Printf(TEXT("%s removes the failed live asset"), *Label),
		FindObject<UObject>(nullptr, *ObjectPath));
	Test.TestFalse(
		*FString::Printf(TEXT("%s removes the failed registry asset"), *Label),
		RegistryContains(ObjectPath));
	Test.TestTrue(
		*FString::Printf(TEXT("%s preserves package identity"), *Label),
		FindPackage(nullptr, *Target) == PreExistingPackage);
	Test.TestTrue(
		*FString::Printf(TEXT("%s restores the dirty package state"), *Label),
		PreExistingPackage->IsDirty());

	const TSet<UObject*> PackageObjectsAfter = CollectPackageObjects(PreExistingPackage);
	const bool bExactPackageObjects = HaveExactObjectSet(PackageObjectsBefore, PackageObjectsAfter);
	Test.TestTrue(
		*FString::Printf(TEXT("%s restores the exact pre-existing package object baseline"), *Label),
		bExactPackageObjects);
	if (!bExactPackageObjects)
	{
		AddOwnedObjectSetDiff(
			Test,
			FString::Printf(TEXT("%s package-object delta"), *Label),
			PackageSnapshotBefore,
			SnapshotOwnedObjectSet(PackageObjectsAfter));
	}
	Test.TestTrue(
		*FString::Printf(TEXT("%s preserves the weak baseline sentinel across the full Apply window"), *Label),
		BaselineSentinelWeak.IsValid());
	if (UObject* PreservedSentinel = BaselineSentinelWeak.GetEvenIfUnreachable())
	{
		Test.TestFalse(
			*FString::Printf(TEXT("%s does not mark the baseline sentinel as garbage"), *Label),
			PreservedSentinel->HasAnyInternalFlags(EInternalObjectFlags::Garbage));
		Test.TestTrue(
			*FString::Printf(TEXT("%s keeps the baseline sentinel in the original package"), *Label),
			PreservedSentinel->GetOuter() == PreExistingPackage);
		Test.TestTrue(
			*FString::Printf(TEXT("%s exact package baseline contains the original sentinel identity"), *Label),
			PackageObjectsAfter.Contains(PreservedSentinel));
	}

	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	Test.TestFalse(
		*FString::Printf(TEXT("%s collects the late package companion"), *Label),
		LateBodyCompanionWeak.IsValid());
	Test.TestTrue(
		*FString::Printf(TEXT("%s leaves no transient Blueprint companion artifact"), *Label),
		HaveExactStringSet(TransientArtifactsBefore, CollectNamedTransientArtifacts(AssetName)));
	Test.TestTrue(
		*FString::Printf(TEXT("%s leaves no newly rooted Blueprint companion artifact"), *Label),
		HaveExactStringSet(RootedArtifactsBefore, CollectNamedRootedArtifacts(AssetName)));

	FAssetDocumentServiceTestHooks::Clear();
	const FAssetDocumentResult Retry = ApplyWithoutSave(Service, Document);
	Test.TestTrue(*FString::Printf(TEXT("%s retry succeeds"), *Label), Retry.IsSuccess());
	UObject* RetryAsset = FindObject<UObject>(nullptr, *ObjectPath);
	Test.TestNotNull(*FString::Printf(TEXT("%s retry materializes object"), *Label), RetryAsset);
	if (RetryAsset)
	{
		Test.TestEqual(
			*FString::Printf(TEXT("%s retry has exact asset class"), *Label),
			RetryAsset->GetClass(),
			ExpectedAssetClass);
		VerifyBlueprintGeneratedIdentity(Test, Label, RetryAsset, ExpectedGeneratedClassType);
	}

	CleanupAsset(Target);
	DiscardTestPackageContents(PreExistingPackage);
	PackageGuard.Reset();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyTransactionExistingGenericTest,
	"AssetFactory.AssetDocument.ApplyTransaction.Existing.Generic.PropertiesRollbackDiagnosticsAndRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyTransactionExistingGenericTest::RunTest(const FString&)
{
	using namespace AssetDocumentApplyTransactionTests;
	const FString Target = MakeUniqueTarget(TEXT("DA_ExistingProperties"));
	CleanupAsset(Target);
	FAssetDocumentService Service;
	TestTrue(
		TEXT("Generic rollback fixture creates"),
		ApplyWithoutSave(Service, MakeGenericDocument(Target, TEXT("Create"), TEXT("before"), 7)).IsSuccess());

	UTestDataAsset* Asset = FindObject<UTestDataAsset>(nullptr, *ToObjectPath(Target));
	TestNotNull(TEXT("Generic rollback fixture resolves"), Asset);
	if (!Asset)
	{
		return false;
	}
	UPackage* Package = Asset->GetOutermost();
	Package->SetDirtyFlag(false);
	const TSet<UObject*> OwnedBefore = CollectRecursiveOwned(Asset);

	FScopedServiceHookReset HookReset;
	const FAssetDocumentDiagnostic Primary = MakeDiagnostic(
		TEXT("/Apply/AfterLiveProperties"),
		TEXT("ForcedAssetDocumentServicePhaseFailure"),
		TEXT("Forced failure after live reflected Properties"));
	const FAssetDocumentDiagnostic Rollback = MakeDiagnostic(
		TEXT("/Apply/Rollback"),
		TEXT("ForcedAssetDocumentRollbackVerificationFailure"),
		TEXT("Forced rollback verification diagnostic"));
	FAssetDocumentServiceTestHooks::FailNextApplyAtPhase(
		EAssetDocumentServiceApplyPhase::AfterLiveProperties,
		Primary);
	FAssetDocumentServiceTestHooks::FailNextRollbackVerification(Rollback);

	const TSharedPtr<FJsonObject> Update = MakeGenericDocument(Target, TEXT("Update"), TEXT("after"), 19);
	const FAssetDocumentResult Failed = ApplyWithoutSave(Service, Update);
	TestFalse(TEXT("Forced post-Properties failure rejects existing update"), Failed.IsSuccess());
	TestEqual(TEXT("Primary failure remains the result message"), Failed.Message, Primary.Message);
	TestEqual(TEXT("Primary and rollback diagnostics are both retained"), Failed.Diagnostics.Num(), 2);
	if (Failed.Diagnostics.Num() == 2)
	{
		TestEqual(TEXT("Primary diagnostic remains first"), Failed.Diagnostics[0].Code, Primary.Code);
		TestEqual(TEXT("Rollback diagnostic is appended"), Failed.Diagnostics[1].Code, Rollback.Code);
	}
	TestEqual(TEXT("TestString rolls back across Properties to service failure"), Asset->TestString, FString(TEXT("before")));
	TestEqual(TEXT("TestInt rolls back across Properties to service failure"), Asset->TestInt, 7);
	TestTrue(TEXT("Generic recursive owned set is exact after rollback"), HaveExactObjectSet(OwnedBefore, CollectRecursiveOwned(Asset)));
	TestFalse(TEXT("Generic clean dirty state is restored"), Package->IsDirty());

	FAssetDocumentServiceTestHooks::Clear();
	const FAssetDocumentResult Retry = ApplyWithoutSave(Service, Update);
	TestTrue(TEXT("Existing update succeeds on retry"), Retry.IsSuccess());
	TestEqual(TEXT("Retry applies TestString"), Asset->TestString, FString(TEXT("after")));
	TestEqual(TEXT("Retry applies TestInt"), Asset->TestInt, 19);
	CleanupAsset(Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyTransactionNewOwnedGarbageTest,
	"AssetFactory.AssetDocument.ApplyTransaction.Existing.Generic.NewOwnedTreeIsGarbageAfterRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyTransactionNewOwnedGarbageTest::RunTest(const FString&)
{
	using namespace AssetDocumentApplyTransactionTests;
	const FString Target = MakeUniqueTarget(TEXT("DA_NewOwnedGarbage"));
	CleanupAsset(Target);
	FAssetDocumentService Service;
	TestTrue(
		TEXT("New-owned rollback fixture creates"),
		ApplyWithoutSave(Service, MakeGenericDocument(Target, TEXT("Create"), TEXT("before"), 7)).IsSuccess());

	UTestDataAsset* Asset = FindObject<UTestDataAsset>(nullptr, *ToObjectPath(Target));
	TestNotNull(TEXT("New-owned rollback fixture resolves"), Asset);
	if (!Asset)
	{
		return false;
	}
	Asset->GetOutermost()->SetDirtyFlag(false);
	const TSet<UObject*> OwnedBefore = CollectRecursiveOwned(Asset);
	TWeakObjectPtr<UObject> ParentWeak;
	TWeakObjectPtr<UObject> ChildWeak;
	bool bInjectedParentCreated = false;
	bool bInjectedChildCreated = false;

	FScopedServiceHookReset HookReset;
	const FAssetDocumentDiagnostic Primary = MakeDiagnostic(
		TEXT("/Apply/AfterLiveProperties"),
		TEXT("ForcedNewOwnedRollbackFailure"),
		TEXT("Forced failure with a newly owned UObject tree"));
	FAssetDocumentServiceTestHooks::FailNextApplyAtPhase(
		EAssetDocumentServiceApplyPhase::AfterLiveProperties,
		Primary,
		[Asset, &ParentWeak, &ChildWeak, &bInjectedParentCreated, &bInjectedChildCreated]()
		{
			UObject* InjectedParent = NewObject<UTestDataAsset>(Asset, NAME_None, RF_Public | RF_Standalone);
			UObject* InjectedChild = NewObject<UTestDataAsset>(InjectedParent, NAME_None, RF_Public | RF_Standalone);
			ParentWeak = InjectedParent;
			ChildWeak = InjectedChild;
			bInjectedParentCreated = InjectedParent != nullptr;
			bInjectedChildCreated = InjectedChild != nullptr;
			InjectedParent->AddToRoot();
			InjectedChild->AddToRoot();
		});

	const FAssetDocumentResult Result = ApplyWithoutSave(
		Service,
		MakeGenericDocument(Target, TEXT("Update"), TEXT("after"), 19));
	TestFalse(TEXT("Forced new-owned update fails"), Result.IsSuccess());
	TestTrue(TEXT("New-owned failure keeps primary diagnostic"), HasDiagnostic(Result, Primary.Code, Primary.Path));
	TestEqual(TEXT("New-owned rollback restores TestString"), Asset->TestString, FString(TEXT("before")));
	TestEqual(TEXT("New-owned rollback restores TestInt"), Asset->TestInt, 7);
	TestFalse(TEXT("New-owned rollback restores clean package state"), Asset->GetOutermost()->IsDirty());
	TestTrue(TEXT("New-owned callback creates parent"), bInjectedParentCreated);
	TestTrue(TEXT("New-owned callback creates child"), bInjectedChildCreated);
	if (UObject* InjectedParent = ParentWeak.GetEvenIfUnreachable())
	{
		TestFalse(TEXT("Rollback removes newly owned parent from RootSet"), InjectedParent->IsRooted());
		TestTrue(TEXT("Rollback marks newly owned parent as garbage"), InjectedParent->HasAnyInternalFlags(EInternalObjectFlags::Garbage));
	}
	if (UObject* InjectedChild = ChildWeak.GetEvenIfUnreachable())
	{
		TestFalse(TEXT("Rollback removes newly owned child from RootSet"), InjectedChild->IsRooted());
		TestTrue(TEXT("Rollback marks newly owned child as garbage"), InjectedChild->HasAnyInternalFlags(EInternalObjectFlags::Garbage));
	}
	TestTrue(TEXT("New-owned recursive set is exact after rollback"), HaveExactObjectSet(OwnedBefore, CollectRecursiveOwned(Asset)));

	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestFalse(TEXT("Newly owned parent weak ref expires after GC"), ParentWeak.IsValid());
	TestFalse(TEXT("Newly owned child weak ref expires after GC"), ChildWeak.IsValid());
	if (UObject* LeakedParent = ParentWeak.Get())
	{
		LeakedParent->ClearFlags(RF_Public | RF_Standalone);
		LeakedParent->MarkAsGarbage();
	}
	if (UObject* LeakedChild = ChildWeak.Get())
	{
		LeakedChild->ClearFlags(RF_Public | RF_Standalone);
		LeakedChild->MarkAsGarbage();
	}
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	CleanupAsset(Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyTransactionDirtyExistingTest,
	"AssetFactory.AssetDocument.ApplyTransaction.Existing.Generic.DirtyDurableAssetFailsClosedAfterStaging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyTransactionDirtyExistingTest::RunTest(const FString&)
{
	using namespace AssetDocumentApplyTransactionTests;
	const FString Target = MakeUniqueTarget(TEXT("DA_DirtyExisting"));
	CleanupAsset(Target);
	FAssetDocumentService Service;
	TestTrue(
		TEXT("Dirty fixture creates"),
		ApplyWithoutSave(Service, MakeGenericDocument(Target, TEXT("Create"), TEXT("saved-baseline"), 3)).IsSuccess());

	UTestDataAsset* Asset = FindObject<UTestDataAsset>(nullptr, *ToObjectPath(Target));
	TestNotNull(TEXT("Dirty fixture resolves"), Asset);
	if (!Asset)
	{
		return false;
	}
	Asset->TestString = TEXT("unsaved-user-state");
	Asset->GetOutermost()->SetDirtyFlag(true);

	FScopedServiceHookReset HookReset;
	const FAssetDocumentDiagnostic StagingPrimary = MakeDiagnostic(
		TEXT("/Apply/AfterStagedDocument"),
		TEXT("ForcedAssetDocumentServicePhaseFailure"),
		TEXT("Forced failure proves staging precedes durable dirty guard"));
	FAssetDocumentServiceTestHooks::FailNextApplyAtPhase(
		EAssetDocumentServiceApplyPhase::AfterStagedDocument,
		StagingPrimary);
	const FAssetDocumentResult StagingFailure = ApplyDocument(
		Service,
		MakeGenericDocument(Target, TEXT("Update"), TEXT("authored-overwrite"), 99),
		true);
	TestFalse(TEXT("Forced staged durable update fails"), StagingFailure.IsSuccess());
	TestTrue(
		TEXT("Durable dirty guard runs after complete staging"),
		HasDiagnostic(StagingFailure, StagingPrimary.Code, StagingPrimary.Path));
	TestEqual(TEXT("Staging failure preserves unsaved value"), Asset->TestString, FString(TEXT("unsaved-user-state")));
	TestEqual(TEXT("Staging failure preserves unrelated property"), Asset->TestInt, 3);
	TestTrue(TEXT("Staging failure preserves dirty flag"), Asset->GetOutermost()->IsDirty());

	FAssetDocumentServiceTestHooks::Clear();
	const FAssetDocumentResult Result = ApplyDocument(
		Service,
		MakeGenericDocument(Target, TEXT("Update"), TEXT("authored-overwrite"), 99),
		true);
	TestFalse(TEXT("Dirty existing asset fails closed"), Result.IsSuccess());
	TestTrue(
		TEXT("Dirty fail-closed diagnostic is exact"),
		HasDiagnostic(Result, TEXT("DirtyExistingAssetTransactionUnsupported"), TEXT("/Target")));
	TestEqual(TEXT("Dirty fail-closed preserves unsaved value"), Asset->TestString, FString(TEXT("unsaved-user-state")));
	TestEqual(TEXT("Dirty fail-closed preserves unrelated property"), Asset->TestInt, 3);
	TestTrue(TEXT("Dirty fail-closed preserves dirty flag"), Asset->GetOutermost()->IsDirty());
	CleanupAsset(Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyTransactionDirtyInMemorySuccessTest,
	"AssetFactory.AssetDocument.ApplyTransaction.Existing.Generic.DirtyInMemoryUpdateSucceeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyTransactionDirtyInMemorySuccessTest::RunTest(const FString&)
{
	using namespace AssetDocumentApplyTransactionTests;
	const FString Target = MakeUniqueTarget(TEXT("DA_DirtyMemorySuccess"));
	CleanupAsset(Target);
	FAssetDocumentService Service;
	TestTrue(
		TEXT("Dirty in-memory fixture creates without save"),
		ApplyWithoutSave(Service, MakeGenericDocument(Target, TEXT("Create"), TEXT("created"), 3)).IsSuccess());

	UTestDataAsset* Asset = FindObject<UTestDataAsset>(nullptr, *ToObjectPath(Target));
	TestNotNull(TEXT("Dirty in-memory fixture resolves"), Asset);
	if (!Asset)
	{
		return false;
	}
	Asset->TestString = TEXT("unsaved-current");
	Asset->TestInt = 11;
	Asset->GetOutermost()->SetDirtyFlag(true);

	TSharedPtr<FJsonObject> Update = MakeGenericDocument(Target, TEXT("Update"), TEXT("unused"), 29);
	Update->GetObjectField(TEXT("Properties"))->RemoveField(TEXT("TestString"));
	const FAssetDocumentResult Result = ApplyWithoutSave(Service, Update);
	TestTrue(TEXT("Dirty in-memory update succeeds without durable save"), Result.IsSuccess());
	TestEqual(TEXT("Dirty in-memory update preserves unauthored current string"), Asset->TestString, FString(TEXT("unsaved-current")));
	TestEqual(TEXT("Dirty in-memory update applies authored integer"), Asset->TestInt, 29);
	TestTrue(TEXT("Dirty in-memory update preserves dirty state"), Asset->GetOutermost()->IsDirty());
	CleanupAsset(Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyTransactionDirtyInMemoryRollbackTest,
	"AssetFactory.AssetDocument.ApplyTransaction.Existing.Generic.DirtyInMemoryForcedFailureRollsBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyTransactionDirtyInMemoryRollbackTest::RunTest(const FString&)
{
	using namespace AssetDocumentApplyTransactionTests;
	const FString Target = MakeUniqueTarget(TEXT("DA_DirtyMemoryRollback"));
	CleanupAsset(Target);
	FAssetDocumentService Service;
	TestTrue(
		TEXT("Dirty rollback fixture creates without save"),
		ApplyWithoutSave(Service, MakeGenericDocument(Target, TEXT("Create"), TEXT("created"), 5)).IsSuccess());

	UTestDataAsset* Asset = FindObject<UTestDataAsset>(nullptr, *ToObjectPath(Target));
	TestNotNull(TEXT("Dirty rollback fixture resolves"), Asset);
	if (!Asset)
	{
		return false;
	}
	Asset->TestString = TEXT("unsaved-current");
	Asset->TestInt = 13;
	Asset->GetOutermost()->SetDirtyFlag(true);

	FScopedServiceHookReset HookReset;
	const FAssetDocumentDiagnostic Primary = MakeDiagnostic(
		TEXT("/Apply/AfterLiveProperties"),
		TEXT("ForcedDirtyInMemoryRollbackFailure"),
		TEXT("Forced dirty in-memory rollback failure"));
	FAssetDocumentServiceTestHooks::FailNextApplyAtPhase(
		EAssetDocumentServiceApplyPhase::AfterLiveProperties,
		Primary);
	const FAssetDocumentResult Result = ApplyWithoutSave(
		Service,
		MakeGenericDocument(Target, TEXT("Update"), TEXT("authored-overwrite"), 41));
	TestFalse(TEXT("Forced dirty in-memory update fails"), Result.IsSuccess());
	TestTrue(TEXT("Forced dirty in-memory update keeps primary diagnostic"), HasDiagnostic(Result, Primary.Code, Primary.Path));
	TestEqual(TEXT("Dirty rollback restores current string"), Asset->TestString, FString(TEXT("unsaved-current")));
	TestEqual(TEXT("Dirty rollback restores current integer"), Asset->TestInt, 13);
	TestTrue(TEXT("Dirty rollback restores dirty state"), Asset->GetOutermost()->IsDirty());
	CleanupAsset(Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyTransactionLifecycleStateTest,
	"AssetFactory.AssetDocument.ApplyTransaction.NewAsset.Lifecycle.PackageAndPreRegistryFailuresCleanupAndRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyTransactionLifecycleStateTest::RunTest(const FString&)
{
	using namespace AssetDocumentApplyTransactionTests;
	FAssetDocumentService Service;
	const FString PackageTarget = MakeUniqueTarget(TEXT("DA_LifecycleAfterPackage"));
	RunLifecycleFailureAndRetry(
		*this,
		Service,
		TEXT("AfterPackageCreate"),
		PackageTarget,
		MakeGenericDocument(PackageTarget, TEXT("Create"), TEXT("package"), 1),
		EAssetDocumentLifecycleCreatePhase::AfterPackageCreate,
		false);

	const FString PreRegistryTarget = MakeUniqueTarget(TEXT("DA_LifecycleBeforeRegistry"));
	RunLifecycleFailureAndRetry(
		*this,
		Service,
		TEXT("AfterAssetCreateBeforeRegistry"),
		PreRegistryTarget,
		MakeGenericDocument(PreRegistryTarget, TEXT("Create"), TEXT("asset"), 2),
		EAssetDocumentLifecycleCreatePhase::AfterAssetCreateBeforeRegistry,
		true);

	const FString FallbackTarget = MakeUniqueTarget(TEXT("DA_LifecycleFallbackPrimary"));
	CleanupAsset(FallbackTarget);
	FScopedServiceHookReset HookReset;
	const FAssetDocumentDiagnostic NoDiagnosticPrimary = MakeDiagnostic(
		TEXT("/Apply/Lifecycle"),
		TEXT("SuppressedLifecycleHookDiagnostic"),
		TEXT("Forced lifecycle Error without a structured primary diagnostic"));
	const FAssetDocumentDiagnostic Cleanup = MakeDiagnostic(
		TEXT("/Apply/Rollback"),
		TEXT("ForcedAssetDocumentLifecycleCleanupFailure"),
		TEXT("Forced cleanup diagnostic after lifecycle fallback primary"));
	FAssetDocumentServiceTestHooks::FailNextLifecycleCreateAtPhase(
		EAssetDocumentLifecycleCreatePhase::AfterAssetCreateBeforeRegistry,
		NoDiagnosticPrimary,
		false);
	FAssetDocumentServiceTestHooks::FailNextLifecycleCleanupVerification(Cleanup);
	{
		FScopedRegistryEvents Events(ToObjectPath(FallbackTarget));
		const FAssetDocumentResult Failed = ApplyWithoutSave(
			Service,
			MakeGenericDocument(FallbackTarget, TEXT("Create"), TEXT("fallback"), 3));
		TestFalse(TEXT("Lifecycle Error without diagnostic fails"), Failed.IsSuccess());
		TestEqual(TEXT("Lifecycle Error remains the result message"), Failed.Message, NoDiagnosticPrimary.Message);
		TestTrue(
			TEXT("Lifecycle Error receives stable fallback primary"),
			HasDiagnostic(Failed, TEXT("AssetDocumentLifecycleFailed"), TEXT("/Target")));
		TestTrue(
			TEXT("Lifecycle fallback path appends cleanup secondary"),
			HasDiagnostic(Failed, Cleanup.Code, Cleanup.Path));
		TestEqual(TEXT("Lifecycle fallback path has primary and cleanup diagnostics"), Failed.Diagnostics.Num(), 2);
		if (Failed.Diagnostics.Num() == 2)
		{
			TestEqual(TEXT("Lifecycle fallback primary is first"), Failed.Diagnostics[0].Code, FString(TEXT("AssetDocumentLifecycleFailed")));
			TestEqual(TEXT("Lifecycle fallback primary preserves Error message"), Failed.Diagnostics[0].Message, NoDiagnosticPrimary.Message);
			TestEqual(TEXT("Lifecycle fallback cleanup is last"), Failed.Diagnostics[1].Code, Cleanup.Code);
		}
		TestEqual(TEXT("Lifecycle fallback pre-registry failure emits no AssetAdded"), Events.Added, 0);
		TestEqual(TEXT("Lifecycle fallback pre-registry failure emits no AssetRemoved"), Events.Removed, 0);
	}
	ExpectNoGhost(*this, TEXT("Lifecycle fallback primary"), FallbackTarget);
	FAssetDocumentServiceTestHooks::Clear();
	const FAssetDocumentResult FallbackRetry = ApplyWithoutSave(
		Service,
		MakeGenericDocument(FallbackTarget, TEXT("Create"), TEXT("retry"), 5));
	TestTrue(TEXT("Lifecycle fallback primary retry succeeds"), FallbackRetry.IsSuccess());
	CleanupAsset(FallbackTarget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyTransactionExpiredLifecycleObjectTest,
	"AssetFactory.AssetDocument.ApplyTransaction.NewAsset.Lifecycle.ExpiredCreatedObjectHandlesAreIgnored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyTransactionExpiredLifecycleObjectTest::RunTest(const FString&)
{
	TWeakObjectPtr<UObject> ExpiredCreatedObject;
	{
		UTestDataAsset* CreatedObject = NewObject<UTestDataAsset>(
			GetTransientPackage(),
			NAME_None,
			RF_Transient);
		TestNotNull(TEXT("Expired lifecycle handle fixture creates a real UObject"), CreatedObject);
		if (!CreatedObject)
		{
			return false;
		}
		ExpiredCreatedObject = CreatedObject;
		CreatedObject->MarkAsGarbage();
	}
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestFalse(TEXT("Lifecycle created-object weak handle expires after real UObject collection"), ExpiredCreatedObject.IsValid());

	FAssetDocumentLifecycleResult LifecycleResult;
	LifecycleResult.bCreated = true;
	LifecycleResult.CreatedObjects.Add(ExpiredCreatedObject);
	FAssetDocumentLifecycle::CleanupCreatedAsset(LifecycleResult);
	TestFalse(TEXT("Lifecycle cleanup safely ignores expired created-object handle"), ExpiredCreatedObject.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyTransactionPreExistingBlueprintFamiliesTest,
	"AssetFactory.AssetDocument.ApplyTransaction.NewAsset.PreExistingPackage.BlueprintFamiliesAfterLiveBodyCleanupAndRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyTransactionPreExistingBlueprintFamiliesTest::RunTest(const FString&)
{
	using namespace AssetDocumentApplyTransactionTests;
	FAssetDocumentService Service;
	const bool bBlueprintCompleted = RunPreExistingBlueprintFamilyBodyFailureAndRetry(
		*this,
		Service,
		TEXT("Blueprint"),
		TEXT("/Script/Engine.Blueprint"),
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass());
	TestTrue(TEXT("Blueprint pre-existing-package matrix phase completes"), bBlueprintCompleted);

	const bool bAnimBlueprintCompleted = RunPreExistingBlueprintFamilyBodyFailureAndRetry(
		*this,
		Service,
		TEXT("AnimBlueprint"),
		TEXT("/Script/Engine.AnimBlueprint"),
		UAnimBlueprint::StaticClass(),
		UAnimBlueprintGeneratedClass::StaticClass());
	TestTrue(TEXT("AnimBlueprint pre-existing-package matrix phase completes"), bAnimBlueprintCompleted);

	const bool bWidgetBlueprintCompleted = RunPreExistingBlueprintFamilyBodyFailureAndRetry(
		*this,
		Service,
		TEXT("WidgetBlueprint"),
		TEXT("/Script/UMGEditor.WidgetBlueprint"),
		UWidgetBlueprint::StaticClass(),
		UWidgetBlueprintGeneratedClass::StaticClass());
	TestTrue(TEXT("WidgetBlueprint pre-existing-package matrix phase completes"), bWidgetBlueprintCompleted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentLifecycleCreatedDeltaWithoutStateFlagsTest,
	"AssetFactory.AssetDocument.ApplyTransaction.NewAsset.Lifecycle.CreatedDeltaCleansWhenStateFlagsFalse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentLifecycleCreatedDeltaWithoutStateFlagsTest::RunTest(const FString&)
{
	using namespace AssetDocumentApplyTransactionTests;
	const FString Target = MakeUniqueTarget(TEXT("DA_LifecycleCreatedDeltaNoFlags"));
	const FString ObjectPath = ToObjectPath(Target);
	CleanupAsset(Target);

	UPackage* PreExistingPackage = CreatePackage(*Target);
	TestNotNull(TEXT("Lifecycle created-delta pre-existing package fixture creates"), PreExistingPackage);
	if (!PreExistingPackage)
	{
		return false;
	}
	TStrongObjectPtr<UPackage> PackageGuard(PreExistingPackage);
	UTestDataAsset* BaselineSentinel = NewObject<UTestDataAsset>(
		PreExistingPackage,
		TEXT("BaselineSentinel"),
		RF_Transactional);
	TestNotNull(TEXT("Lifecycle created-delta baseline sentinel creates"), BaselineSentinel);
	if (!BaselineSentinel)
	{
		DiscardTestPackageContents(PreExistingPackage);
		PackageGuard.Reset();
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		return false;
	}
	TStrongObjectPtr<UTestDataAsset> SentinelGuard(BaselineSentinel);
	TWeakObjectPtr<UTestDataAsset> BaselineSentinelWeak(BaselineSentinel);
	TestFalse(
		TEXT("Lifecycle created-delta baseline sentinel has no Standalone retention"),
		BaselineSentinel->HasAnyFlags(RF_Standalone));
	PreExistingPackage->SetDirtyFlag(true);
	const TSet<UObject*> PackageObjectsBefore = CollectPackageObjects(PreExistingPackage);
	const FOwnedObjectSetSnapshot PackageSnapshotBefore = SnapshotOwnedObjectSet(PackageObjectsBefore);

	UTestDataAsset* CreatedObject = NewObject<UTestDataAsset>(
		PreExistingPackage,
		*FPackageName::GetLongPackageAssetName(Target),
		RF_Public | RF_Standalone);
	FAssetDocumentLifecycleResult LifecycleResult;
	LifecycleResult.Asset = CreatedObject;
	LifecycleResult.Package = PreExistingPackage;
	LifecycleResult.bPackageWasDirty = true;
	LifecycleResult.bPackageObjectBaselineCaptured = true;
	for (UObject* BaselineObject : PackageObjectsBefore)
	{
		LifecycleResult.PackageObjectsBeforeCreation.Add(BaselineObject);
	}
	LifecycleResult.ObjectPath = ObjectPath;
	LifecycleResult.CreatedObjects.Add(CreatedObject);
	TestNotNull(TEXT("Lifecycle created-delta fixture materializes a live object"), LifecycleResult.Asset);
	TestFalse(TEXT("Lifecycle created-delta fixture does not own pre-existing package"), LifecycleResult.bOwnsPackage);
	TestTrue(TEXT("Lifecycle created-delta fixture captures created objects"), !LifecycleResult.CreatedObjects.IsEmpty());
	if (!LifecycleResult.Asset)
	{
		DiscardTestPackageContents(PreExistingPackage);
		SentinelGuard.Reset();
		PackageGuard.Reset();
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		return false;
	}

	TWeakObjectPtr<UObject> CreatedObjectWeak = LifecycleResult.Asset;
	TestFalse(TEXT("Lifecycle created-delta cleanup entry has bCreated=false"), LifecycleResult.bCreated);
	TestFalse(TEXT("Lifecycle created-delta cleanup entry has bOwnsPackage=false"), LifecycleResult.bOwnsPackage);
	TestFalse(TEXT("Lifecycle created-delta cleanup entry has bRegistryAnnounced=false"), LifecycleResult.bRegistryAnnounced);

	BaselineSentinel = nullptr;
	SentinelGuard.Reset();
	TArray<FAssetDocumentDiagnostic> CleanupDiagnostics;
	FAssetDocumentLifecycle::CleanupCreatedAsset(LifecycleResult, CleanupDiagnostics);
	TestEqual(TEXT("Lifecycle created-delta cleanup emits no diagnostics"), CleanupDiagnostics.Num(), 0);
	TestTrue(TEXT("Lifecycle created-delta cleanup preserves package identity"), FindPackage(nullptr, *Target) == PreExistingPackage);
	TestTrue(TEXT("Lifecycle created-delta cleanup restores package dirty state"), PreExistingPackage->IsDirty());
	const TSet<UObject*> PackageObjectsAfter = CollectPackageObjects(PreExistingPackage);
	const bool bExactPackageObjects = HaveExactObjectSet(PackageObjectsBefore, PackageObjectsAfter);
	TestTrue(TEXT("Lifecycle created-delta cleanup restores exact package baseline"), bExactPackageObjects);
	if (!bExactPackageObjects)
	{
		AddOwnedObjectSetDiff(
			*this,
			TEXT("Lifecycle created-delta package-object delta"),
			PackageSnapshotBefore,
			SnapshotOwnedObjectSet(PackageObjectsAfter));
	}
	TestTrue(TEXT("Lifecycle created-delta cleanup preserves weak baseline sentinel"), BaselineSentinelWeak.IsValid());
	if (UObject* PreservedSentinel = BaselineSentinelWeak.GetEvenIfUnreachable())
	{
		TestFalse(
			TEXT("Lifecycle created-delta cleanup does not mark baseline sentinel as garbage"),
			PreservedSentinel->HasAnyInternalFlags(EInternalObjectFlags::Garbage));
		TestTrue(
			TEXT("Lifecycle created-delta cleanup keeps baseline sentinel in the original package"),
			PreservedSentinel->GetOuter() == PreExistingPackage);
		TestTrue(
			TEXT("Lifecycle created-delta exact baseline contains the original sentinel identity"),
			PackageObjectsAfter.Contains(PreservedSentinel));
	}
	TestFalse(TEXT("Lifecycle created-delta weak ref expires after cleanup GC"), CreatedObjectWeak.IsValid());

	if (CreatedObjectWeak.IsValid())
	{
		LifecycleResult.bCreated = true;
		FAssetDocumentLifecycle::CleanupCreatedAsset(LifecycleResult);
	}
	DiscardTestPackageContents(PreExistingPackage);
	PackageGuard.Reset();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyTransactionPreExistingPackageOwnershipTest,
	"AssetFactory.AssetDocument.ApplyTransaction.NewAsset.PreExistingPackageOwnershipIsPreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyTransactionPreExistingPackageOwnershipTest::RunTest(const FString&)
{
	using namespace AssetDocumentApplyTransactionTests;
	const FString Target = MakeUniqueTarget(TEXT("DA_PreExistingPackage"));
	CleanupAsset(Target);
	UPackage* PreExistingPackage = CreatePackage(*Target);
	TestNotNull(TEXT("Pre-existing package fixture creates"), PreExistingPackage);
	if (!PreExistingPackage)
	{
		return false;
	}
	TStrongObjectPtr<UPackage> PackageGuard(PreExistingPackage);
	PreExistingPackage->SetDirtyFlag(true);
	FAssetDocumentService Service;
	FScopedServiceHookReset HookReset;
	const FAssetDocumentDiagnostic Primary = MakeDiagnostic(
		TEXT("/Apply/AfterNewLiveMaterialize"),
		TEXT("ForcedPreExistingPackageFailure"),
		TEXT("Forced failure after live asset materialization in a pre-existing package"));
	FAssetDocumentServiceTestHooks::FailNextApplyAtPhase(
		EAssetDocumentServiceApplyPhase::AfterNewLiveMaterialize,
		Primary);

	{
		FScopedRegistryEvents Events(ToObjectPath(Target));
		const FAssetDocumentResult Result = ApplyWithoutSave(
			Service,
			MakeGenericDocument(Target, TEXT("Create"), TEXT("materialized"), 3));
		TestFalse(TEXT("Pre-existing package forced apply fails"), Result.IsSuccess());
		TestTrue(TEXT("Pre-existing package failure keeps primary diagnostic"), HasDiagnostic(Result, Primary.Code, Primary.Path));
		TestEqual(TEXT("Pre-existing package failure emits one AssetAdded"), Events.Added, 1);
		TestEqual(TEXT("Pre-existing package failure emits one AssetRemoved"), Events.Removed, 1);
	}

	TestNull(TEXT("Pre-existing package cleanup removes only target object"), FindObject<UObject>(nullptr, *ToObjectPath(Target)));
	TestFalse(TEXT("Pre-existing package cleanup removes registry asset"), RegistryContains(ToObjectPath(Target)));
	const bool bPackagePreserved = FindPackage(nullptr, *Target) == PreExistingPackage;
	TestTrue(TEXT("Pre-existing package identity is preserved"), bPackagePreserved);
	TestTrue(TEXT("Pre-existing package dirty state is preserved"), PreExistingPackage->IsDirty());
	TestFalse(TEXT("Pre-existing package failure writes no file"), IFileManager::Get().FileExists(*ToPackageFileName(Target)));

	FAssetDocumentServiceTestHooks::Clear();
	if (bPackagePreserved)
	{
		const FAssetDocumentResult Retry = ApplyWithoutSave(
			Service,
			MakeGenericDocument(Target, TEXT("Create"), TEXT("retry"), 5));
		TestTrue(TEXT("Pre-existing package retry succeeds"), Retry.IsSuccess());
	}

	CleanupAsset(Target);
	if (UPackage* RemainingPackage = FindPackage(nullptr, *Target))
	{
		RemainingPackage->ClearDirtyFlag();
		RemainingPackage->MarkAsGarbage();
	}
	PackageGuard.Reset();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyTransactionExistingBlackboardTest,
	"AssetFactory.AssetDocument.ApplyTransaction.Existing.BlackboardData.BodyFailureKeepsExactState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyTransactionExistingBlackboardTest::RunTest(const FString&)
{
	using namespace AssetDocumentApplyTransactionTests;
	const FString Target = MakeUniqueTarget(TEXT("BB_ExistingBody"));
	CleanupAsset(Target);
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = MakeBlackboardDocument(
		*this,
		Service,
		Target,
		TEXT("Create"),
		{MakeBlackboardKey(TEXT("Stable"), 7)});
	TestTrue(TEXT("Blackboard rollback fixture creates"), ApplyWithoutSave(Service, Document).IsSuccess());

	UBlackboardData* Blackboard = FindObject<UBlackboardData>(nullptr, *ToObjectPath(Target));
	TestNotNull(TEXT("Blackboard rollback fixture resolves"), Blackboard);
	if (!Blackboard)
	{
		return false;
	}
	Blackboard->GetOutermost()->SetDirtyFlag(false);
	const FString CanonicalBefore = ExtractCanonical(*this, Service, ToObjectPath(Target), TEXT("Blackboard before"));
	const TSet<UObject*> OwnedBefore = CollectRecursiveOwned(Blackboard);
	const FOwnedObjectSetSnapshot OwnedBeforeSnapshot = SnapshotOwnedObjectSet(OwnedBefore);
	const FBlackboardEntry* SelfEntryBefore = FindEngineDerivedSelfKey(Blackboard);
	TestNotNull(TEXT("Blackboard baseline has engine-derived SelfActor key"), SelfEntryBefore);
	UBlackboardKeyType* const SelfKeyTypeBefore = SelfEntryBefore ? SelfEntryBefore->KeyType : nullptr;
	UObject* const SelfKeyOuterBefore = SelfKeyTypeBefore ? SelfKeyTypeBefore->GetOuter() : nullptr;
	TestTrue(TEXT("Blackboard baseline SelfActor key type is owned by the asset"), SelfKeyOuterBefore == Blackboard);
	if (!SelfKeyTypeBefore || !SelfKeyOuterBefore)
	{
		CleanupAsset(Target);
		return false;
	}

	TSharedPtr<FJsonObject> Body = GetObjectField(*this, Document, TEXT("Body"), TEXT("Blackboard update"));
	Body->SetArrayField(TEXT("Keys"), {
		MakeShared<FJsonValueObject>(MakeBlackboardKey(TEXT("Stable"), 11)),
		MakeShared<FJsonValueObject>(MakeBlackboardKey(TEXT("Added"), 17)),
	});
	Document->SetStringField(TEXT("Action"), TEXT("Update"));
	FBlackboardDataAssetDocumentCapability::FailNextLiveApplyAfterMutationForTest();
	const FAssetDocumentResult Failed = ApplyWithoutSave(Service, Document);
	TestFalse(TEXT("Forced Blackboard Body failure rejects update"), Failed.IsSuccess());
	TestTrue(
		TEXT("Blackboard primary Body diagnostic is retained"),
		HasDiagnostic(Failed, TEXT("ForcedBlackboardLiveApplyFailure"), TEXT("/Body")));
	TestEqual(
		TEXT("Blackboard canonical document is restored"),
		ExtractCanonical(*this, Service, ToObjectPath(Target), TEXT("Blackboard after")),
		CanonicalBefore);
	const TSet<UObject*> OwnedAfter = CollectRecursiveOwned(Blackboard);
	const FOwnedObjectSetSnapshot OwnedAfterSnapshot = SnapshotOwnedObjectSet(OwnedAfter);
	const FBlackboardEntry* SelfEntryAfter = FindEngineDerivedSelfKey(Blackboard);
	TestNotNull(TEXT("Blackboard rollback keeps engine-derived SelfActor key"), SelfEntryAfter);
	UBlackboardKeyType* const SelfKeyTypeAfter = SelfEntryAfter ? SelfEntryAfter->KeyType : nullptr;
	TestTrue(TEXT("Blackboard rollback preserves SelfActor key type identity"), SelfKeyTypeAfter == SelfKeyTypeBefore);
	TestTrue(
		TEXT("Blackboard rollback preserves SelfActor key type Outer identity"),
		SelfKeyTypeAfter && SelfKeyTypeAfter->GetOuter() == SelfKeyOuterBefore);
	const bool bOwnedSetExact = HaveExactObjectSet(OwnedBefore, OwnedAfter);
	if (!bOwnedSetExact)
	{
		AddOwnedObjectSetDiff(
			*this,
			TEXT("Blackboard recursive owned set"),
			OwnedBeforeSnapshot,
			OwnedAfterSnapshot);
	}
	TestTrue(TEXT("Blackboard recursive owned set is exact"), bOwnedSetExact);
	TestFalse(TEXT("Blackboard clean dirty state is restored"), Blackboard->GetOutermost()->IsDirty());
	CleanupAsset(Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyTransactionRollbackHookIsolationTest,
	"AssetFactory.AssetDocument.ApplyTransaction.Hooks.RollbackDiagnosticBindsToForcedPhase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyTransactionRollbackHookIsolationTest::RunTest(const FString&)
{
	using namespace AssetDocumentApplyTransactionTests;
	const FString ExistingTarget = MakeUniqueTarget(TEXT("BB_HookIsolationExisting"));
	const FString NewTarget = MakeUniqueTarget(TEXT("BB_HookIsolationNew"));
	CleanupAsset(ExistingTarget);
	CleanupAsset(NewTarget);
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> ExistingDocument = MakeBlackboardDocument(
		*this,
		Service,
		ExistingTarget,
		TEXT("Create"),
		{MakeBlackboardKey(TEXT("Stable"), 7)});
	TestTrue(TEXT("Hook isolation existing fixture creates"), ApplyWithoutSave(Service, ExistingDocument).IsSuccess());
	UBlackboardData* ExistingBlackboard = FindObject<UBlackboardData>(nullptr, *ToObjectPath(ExistingTarget));
	TestNotNull(TEXT("Hook isolation existing fixture resolves"), ExistingBlackboard);
	if (!ExistingBlackboard)
	{
		return false;
	}
	ExistingBlackboard->GetOutermost()->SetDirtyFlag(false);

	FScopedServiceHookReset HookReset;
	const FAssetDocumentDiagnostic Primary = MakeDiagnostic(
		TEXT("/Apply/AfterNewLiveBody"),
		TEXT("ForcedBoundPhaseFailure"),
		TEXT("Forced new live Body phase failure"));
	const FAssetDocumentDiagnostic Rollback = MakeDiagnostic(
		TEXT("/Apply/Rollback"),
		TEXT("ForcedBoundRollbackVerificationFailure"),
		TEXT("Rollback diagnostic bound to the forced phase"));
	FAssetDocumentServiceTestHooks::FailNextApplyAtPhase(
		EAssetDocumentServiceApplyPhase::AfterNewLiveBody,
		Primary);
	FAssetDocumentServiceTestHooks::FailNextRollbackVerification(Rollback);

	ExistingDocument->SetStringField(TEXT("Action"), TEXT("Update"));
	FBlackboardDataAssetDocumentCapability::FailNextLiveApplyAfterMutationForTest();
	const FAssetDocumentResult UnrelatedFailure = ApplyWithoutSave(Service, ExistingDocument);
	TestFalse(TEXT("Unrelated real Blackboard rollback fails as injected"), UnrelatedFailure.IsSuccess());
	TestFalse(
		TEXT("Unrelated real rollback does not consume bound rollback diagnostic"),
		HasDiagnostic(UnrelatedFailure, Rollback.Code, Rollback.Path));

	TSharedPtr<FJsonObject> NewDocument = MakeBlackboardDocument(
		*this,
		Service,
		NewTarget,
		TEXT("Create"),
		{MakeBlackboardKey(TEXT("Created"), 17)});
	const FAssetDocumentResult BoundFailure = ApplyWithoutSave(Service, NewDocument);
	TestFalse(TEXT("Bound new live Body phase fails"), BoundFailure.IsSuccess());
	TestTrue(TEXT("Bound failure keeps primary diagnostic"), HasDiagnostic(BoundFailure, Primary.Code, Primary.Path));
	TestTrue(TEXT("Bound failure receives its rollback diagnostic"), HasDiagnostic(BoundFailure, Rollback.Code, Rollback.Path));
	if (BoundFailure.Diagnostics.Num() >= 2)
	{
		TestEqual(TEXT("Bound primary diagnostic precedes rollback diagnostic"), BoundFailure.Diagnostics[0].Code, Primary.Code);
		TestEqual(TEXT("Bound rollback diagnostic is appended"), BoundFailure.Diagnostics.Last().Code, Rollback.Code);
	}
	ExpectNoGhost(*this, TEXT("Bound new live Body phase"), NewTarget);
	CleanupAsset(ExistingTarget);
	CleanupAsset(NewTarget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyTransactionExistingBehaviorTreeTest,
	"AssetFactory.AssetDocument.ApplyTransaction.Existing.BehaviorTree.BodyFailureKeepsExactState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyTransactionExistingBehaviorTreeTest::RunTest(const FString&)
{
	using namespace AssetDocumentApplyTransactionTests;
	const FString Target = MakeUniqueTarget(TEXT("BT_ExistingBody"));
	CleanupAsset(Target);
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = MakeBehaviorTreeDocument(*this, Service, Target, TEXT("Create"));
	TestTrue(TEXT("BehaviorTree rollback fixture creates"), ApplyWithoutSave(Service, Document).IsSuccess());

	UBehaviorTree* BehaviorTree = FindObject<UBehaviorTree>(nullptr, *ToObjectPath(Target));
	TestNotNull(TEXT("BehaviorTree rollback fixture resolves"), BehaviorTree);
	if (!BehaviorTree)
	{
		return false;
	}
	BehaviorTree->GetOutermost()->SetDirtyFlag(false);
	const FString CanonicalBefore = ExtractCanonical(*this, Service, ToObjectPath(Target), TEXT("BehaviorTree before"));
	const TSet<UObject*> OwnedBefore = CollectRecursiveOwned(BehaviorTree);

	TSharedPtr<FJsonObject> Body = GetObjectField(*this, Document, TEXT("Body"), TEXT("BehaviorTree update"));
	TSharedPtr<FJsonObject> Tree = GetObjectField(*this, Body, TEXT("Tree"), TEXT("BehaviorTree Body"));
	TSharedPtr<FJsonObject> Root = GetObjectField(*this, Tree, TEXT("Root"), TEXT("BehaviorTree Tree"));
	TSharedPtr<FJsonObject> Properties = GetObjectField(*this, Root, TEXT("Properties"), TEXT("BehaviorTree root"));
	Properties->SetStringField(TEXT("NodeName"), TEXT("Replacement before forced graph failure"));
	Document->SetStringField(TEXT("Action"), TEXT("Update"));
	FBehaviorTreeAssetDocumentMaterializer::FailNextTreeGraphSwapForTest();
	const FAssetDocumentResult Failed = ApplyWithoutSave(Service, Document);
	TestFalse(TEXT("Forced BehaviorTree Body failure rejects update"), Failed.IsSuccess());
	TestTrue(
		TEXT("BehaviorTree primary Body diagnostic is retained"),
		HasDiagnostic(Failed, TEXT("ForcedBehaviorTreeGraphSwapFailure"), TEXT("/Body/Tree")));
	TestEqual(
		TEXT("BehaviorTree canonical document is restored"),
		ExtractCanonical(*this, Service, ToObjectPath(Target), TEXT("BehaviorTree after")),
		CanonicalBefore);
	TestTrue(TEXT("BehaviorTree recursive owned set is exact"), HaveExactObjectSet(OwnedBefore, CollectRecursiveOwned(BehaviorTree)));
	TestFalse(TEXT("BehaviorTree clean dirty state is restored"), BehaviorTree->GetOutermost()->IsDirty());
	CleanupAsset(Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentApplyTransactionNewAssetMatrixTest,
	"AssetFactory.AssetDocument.ApplyTransaction.NewAsset.PhaseFailuresCleanupAndRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentApplyTransactionNewAssetMatrixTest::RunTest(const FString&)
{
	using namespace AssetDocumentApplyTransactionTests;
	FAssetDocumentService Service;

	const FString StagedTarget = MakeUniqueTarget(TEXT("DA_AfterStagedDocument"));
	RunNewFailureAndRetry(
		*this,
		Service,
		TEXT("AfterStagedDocument"),
		StagedTarget,
		MakeGenericDocument(StagedTarget, TEXT("Create"), TEXT("staged"), 1),
		EAssetDocumentServiceApplyPhase::AfterStagedDocument,
		false);

	const FString StagedBlackboardTarget = MakeUniqueTarget(TEXT("BB_AfterStagedDocument"));
	RunNewFailureAndRetry(
		*this,
		Service,
		TEXT("BlackboardAfterStagedDocument"),
		StagedBlackboardTarget,
		MakeBlackboardDocument(
			*this,
			Service,
			StagedBlackboardTarget,
			TEXT("Create"),
			{MakeBlackboardKey(TEXT("Staged"), 23)}),
		EAssetDocumentServiceApplyPhase::AfterStagedDocument,
		false);

	const FString StagedBehaviorTreeTarget = MakeUniqueTarget(TEXT("BT_AfterStagedDocument"));
	RunNewFailureAndRetry(
		*this,
		Service,
		TEXT("BehaviorTreeAfterStagedDocument"),
		StagedBehaviorTreeTarget,
		MakeBehaviorTreeDocument(*this, Service, StagedBehaviorTreeTarget, TEXT("Create")),
		EAssetDocumentServiceApplyPhase::AfterStagedDocument,
		false);

	const FString StagedBlueprintTarget = MakeUniqueTarget(TEXT("BP_AfterStagedDocument"));
	const bool bBlueprintPhaseCompleted = RunNewFailureAndRetry(
		*this,
		Service,
		TEXT("UBlueprintAfterStagedDocument"),
		StagedBlueprintTarget,
		MakeBlueprintFamilyDocument(
			*this,
			Service,
			StagedBlueprintTarget,
			TEXT("/Script/Engine.Blueprint")),
		EAssetDocumentServiceApplyPhase::AfterStagedDocument,
		false,
		true,
		[this](UObject* Asset)
		{
			TestTrue(TEXT("UBlueprint retry keeps exact top-level type"), Asset && Asset->GetClass() == UBlueprint::StaticClass());
			VerifyBlueprintGeneratedIdentity(
				*this,
				TEXT("UBlueprint"),
				Asset,
				UBlueprintGeneratedClass::StaticClass());
		});

	const FString StagedAnimBlueprintTarget = MakeUniqueTarget(TEXT("ABP_AfterStagedDocument"));
	const bool bAnimBlueprintPhaseCompleted = RunNewFailureAndRetry(
		*this,
		Service,
		TEXT("AnimBlueprintAfterStagedDocument"),
		StagedAnimBlueprintTarget,
		MakeBlueprintFamilyDocument(
			*this,
			Service,
			StagedAnimBlueprintTarget,
			TEXT("/Script/Engine.AnimBlueprint")),
		EAssetDocumentServiceApplyPhase::AfterStagedDocument,
		false,
		true,
		[this](UObject* Asset)
		{
			TestTrue(TEXT("AnimBlueprint retry keeps exact top-level type"), Asset && Asset->GetClass() == UAnimBlueprint::StaticClass());
			VerifyBlueprintGeneratedIdentity(
				*this,
				TEXT("AnimBlueprint"),
				Asset,
				UAnimBlueprintGeneratedClass::StaticClass());
		});

	const FString StagedWidgetBlueprintTarget = MakeUniqueTarget(TEXT("WBP_AfterStagedDocument"));
	int32 WidgetPreviewCreatedEvents = 0;
	const bool bWidgetBlueprintPhaseCompleted = RunNewFailureAndRetry(
		*this,
		Service,
		TEXT("WidgetBlueprintAfterStagedDocument"),
		StagedWidgetBlueprintTarget,
		MakeBlueprintFamilyDocument(
			*this,
			Service,
			StagedWidgetBlueprintTarget,
			TEXT("/Script/UMGEditor.WidgetBlueprint")),
		EAssetDocumentServiceApplyPhase::AfterStagedDocument,
		false,
		true,
		[this](UObject* Asset)
		{
			TestTrue(TEXT("WidgetBlueprint retry keeps exact top-level type"), Asset && Asset->GetClass() == UWidgetBlueprint::StaticClass());
			VerifyBlueprintGeneratedIdentity(
				*this,
				TEXT("WidgetBlueprint"),
				Asset,
				UWidgetBlueprintGeneratedClass::StaticClass());
		},
		&WidgetPreviewCreatedEvents);
	TestEqual(TEXT("WidgetBlueprint transient preview emits no real creation event"), WidgetPreviewCreatedEvents, 0);
	TestTrue(
		TEXT("BP, AnimBP, and WidgetBP phase failures all complete transient and rooted artifact checks"),
		bBlueprintPhaseCompleted && bAnimBlueprintPhaseCompleted && bWidgetBlueprintPhaseCompleted);

	const FString MaterializeTarget = MakeUniqueTarget(TEXT("DA_AfterNewLiveMaterialize"));
	RunNewFailureAndRetry(
		*this,
		Service,
		TEXT("AfterNewLiveMaterialize"),
		MaterializeTarget,
		MakeGenericDocument(MaterializeTarget, TEXT("Create"), TEXT("materialized"), 2),
		EAssetDocumentServiceApplyPhase::AfterNewLiveMaterialize,
		true);

	const FString PropertiesTarget = MakeUniqueTarget(TEXT("DA_AfterLiveProperties"));
	RunNewFailureAndRetry(
		*this,
		Service,
		TEXT("AfterLiveProperties"),
		PropertiesTarget,
		MakeGenericDocument(PropertiesTarget, TEXT("Create"), TEXT("properties"), 3),
		EAssetDocumentServiceApplyPhase::AfterLiveProperties,
		true);

	const FString BlackboardTarget = MakeUniqueTarget(TEXT("BB_AfterNewLiveBody"));
	RunNewFailureAndRetry(
		*this,
		Service,
		TEXT("BlackboardAfterNewLiveBody"),
		BlackboardTarget,
		MakeBlackboardDocument(
			*this,
			Service,
			BlackboardTarget,
			TEXT("Create"),
			{MakeBlackboardKey(TEXT("Created"), 29)}),
		EAssetDocumentServiceApplyPhase::AfterNewLiveBody,
		true);

	const FString BehaviorTreeTarget = MakeUniqueTarget(TEXT("BT_AfterNewLiveBody"));
	RunNewFailureAndRetry(
		*this,
		Service,
		TEXT("BehaviorTreeAfterNewLiveBody"),
		BehaviorTreeTarget,
		MakeBehaviorTreeDocument(*this, Service, BehaviorTreeTarget, TEXT("Create")),
		EAssetDocumentServiceApplyPhase::AfterNewLiveBody,
		true);

	return true;
}

#endif
