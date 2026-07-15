// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentLifecycle.h"
#include "AssetDocumentModule.h"
#include "AssetDocumentServiceTestHooks.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/AnimBlueprintFactory.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Guid.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

namespace
{
TSet<UObject*> CollectAssetDocumentPackageObjects(UPackage* Package)
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

void RemoveAssetDocumentOwnedObjectRoot(UObject* Object, const TCHAR* Context)
{
	if (!Object || !Object->IsRooted())
	{
		return;
	}

	UE_LOG(
		LogAssetDocument,
		Log,
		TEXT("Removing root from owned %s object before discard: Path=%s Class=%s Outer=%s Flags=0x%08x InternalFlags=0x%08x"),
		Context,
		*Object->GetPathName(),
		*GetNameSafe(Object->GetClass()),
		*GetPathNameSafe(Object->GetOuter()),
		static_cast<uint32>(Object->GetFlags()),
		static_cast<uint32>(Object->GetInternalFlags()));
	Object->RemoveFromRoot();
}

void DiscardAssetDocumentOwnedObject(UObject* Object, const TCHAR* Context)
{
	if (!Object)
	{
		return;
	}

	RemoveAssetDocumentOwnedObjectRoot(Object, Context);

	Object->ClearFlags(RF_Public | RF_Standalone);
	Object->SetFlags(RF_Transient);
	Object->MarkAsGarbage();
}

void DetachAssetDocumentOwnedRoots(
	const TSet<UObject*>& ObjectsToDiscard,
	UPackage* Package,
	const TCHAR* Context,
	TArray<FAssetDocumentDiagnostic>& OutDiagnostics)
{
	// Package is deliberately not part of ObjectsToDiscard. Direct package children
	// are therefore owned roots, while descendants whose Outer is also in the set
	// move with their root. For pre-existing packages the set itself remains the
	// ownership boundary, so unrelated package objects can never be detached here.
	for (UObject* Object : ObjectsToDiscard)
	{
		if (!Object || Object == Package || ObjectsToDiscard.Contains(Object->GetOuter()))
		{
			continue;
		}

		const FString OriginalPath = Object->GetPathName();
		RemoveAssetDocumentOwnedObjectRoot(Object, Context);
		const FName DetachedName = MakeUniqueObjectName(
			GetTransientPackage(),
			Object->GetClass(),
			FName(*FString::Printf(TEXT("AssetDocumentDiscard_%s"), *Object->GetName())));
		if (!Object->Rename(
			*DetachedName.ToString(),
			GetTransientPackage(),
			REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty))
		{
			FAssetDocumentDiagnostic Diagnostic;
			Diagnostic.Path = TEXT("/Apply/Rollback");
			Diagnostic.Code = TEXT("AssetDocumentCleanupDetachFailed");
			Diagnostic.Message = FString::Printf(
				TEXT("Failed to detach lifecycle-owned object '%s' during %s cleanup"),
				*OriginalPath,
				Context);
			OutDiagnostics.Add(Diagnostic);
			UE_LOG(LogAssetDocument, Warning, TEXT("%s"), *Diagnostic.Message);
		}
	}
}

void CaptureAssetDocumentCreatedObjects(
	const TSet<UObject*>& Before,
	UPackage* Package,
	FAssetDocumentLifecycleResult& Result)
{
	for (UObject* Object : CollectAssetDocumentPackageObjects(Package))
	{
		if (!Before.Contains(Object))
		{
			Result.CreatedObjects.Add(TWeakObjectPtr<UObject>(Object));
		}
	}
}

void CaptureAssetDocumentPackageObjectBaseline(
	const TSet<UObject*>& PackageObjects,
	FAssetDocumentLifecycleResult& Result)
{
	Result.bPackageObjectBaselineCaptured = true;
	Result.PackageObjectsBeforeCreation.Reset(PackageObjects.Num());
	Result.PackageObjectBaselineGuards.Reset(PackageObjects.Num());
	for (UObject* Object : PackageObjects)
	{
		Result.PackageObjectsBeforeCreation.Add(TWeakObjectPtr<UObject>(Object));
		Result.PackageObjectBaselineGuards.Emplace(Object);
	}
}

TSet<UObject*> ResolveAssetDocumentPackageObjectBaseline(
	const FAssetDocumentLifecycleResult& Result,
	TArray<TStrongObjectPtr<UObject>>* OutBaselineGuards = nullptr)
{
	TSet<UObject*> BaselineObjects;
	if (OutBaselineGuards)
	{
		OutBaselineGuards->Reset(Result.PackageObjectsBeforeCreation.Num());
	}
	for (const TWeakObjectPtr<UObject>& BaselineObjectHandle : Result.PackageObjectsBeforeCreation)
	{
		if (UObject* BaselineObject = BaselineObjectHandle.GetEvenIfUnreachable())
		{
			BaselineObjects.Add(BaselineObject);
			if (OutBaselineGuards)
			{
				OutBaselineGuards->Emplace(BaselineObject);
			}
		}
	}
	return BaselineObjects;
}

#if WITH_DEV_AUTOMATION_TESTS
bool ConsumeAssetDocumentLifecycleFailure(
	EAssetDocumentLifecycleCreatePhase Phase,
	FAssetDocumentLifecycleResult& Result)
{
	FAssetDocumentDiagnostic Diagnostic;
	bool bEmitDiagnostic = true;
	if (!FAssetDocumentServiceTestHooks::ConsumeLifecycleCreateFailure(
		Phase,
		Diagnostic,
		&bEmitDiagnostic))
	{
		return false;
	}

	Result.Error = Diagnostic.Message;
	if (bEmitDiagnostic)
	{
		Result.Diagnostics.Add(MoveTemp(Diagnostic));
	}
	return true;
}
#endif
}

bool FAssetDocumentLifecycle::TryParseAction(const FString& ActionName, EAssetDocumentLifecycleAction& OutAction, FString& OutError)
{
	if (ActionName == TEXT("Create"))
	{
		OutAction = EAssetDocumentLifecycleAction::Create;
		return true;
	}

	if (ActionName == TEXT("Update"))
	{
		OutAction = EAssetDocumentLifecycleAction::Update;
		return true;
	}

	if (ActionName == TEXT("CreateOrUpdate"))
	{
		OutAction = EAssetDocumentLifecycleAction::CreateOrUpdate;
		return true;
	}

	OutError = FString::Printf(TEXT("Unsupported Action '%s'"), *ActionName);
	return false;
}

FAssetDocumentLifecycleResult FAssetDocumentLifecycle::Resolve(
	const FString& Target,
	UClass* Class,
	EAssetDocumentLifecycleAction Action)
{
	FAssetDocumentLifecycleResult Result;
	Result.ObjectPath = MakeObjectPath(Target);

	UObject* ExistingAsset = FindObject<UObject>(nullptr, *Result.ObjectPath);
	const FString PackageFileName = FPackageName::LongPackageNameToFilename(Target, FPackageName::GetAssetPackageExtension());
	const bool bPackageFileExists = IFileManager::Get().FileExists(*PackageFileName);
	if (Action == EAssetDocumentLifecycleAction::Create && (ExistingAsset || bPackageFileExists))
	{
		// Create only needs an existence answer. Do not load an on-disk target merely to
		// reject it; this keeps lifecycle preflight free of package/owned-object mutation.
		Result.Error = FString::Printf(TEXT("Create failed because target '%s' already exists"), *Target);
		return Result;
	}

	if (!ExistingAsset && bPackageFileExists)
	{
		// Update/CreateOrUpdate deliberately load existing state so downstream read-only
		// validation/preflight can inspect the real asset. The lookup itself must not dirty
		// the package, announce assets, or create owned objects.
		ExistingAsset = LoadObject<UObject>(nullptr, *Result.ObjectPath);
	}

	UPackage* TargetPackage = ExistingAsset
		? ExistingAsset->GetOutermost()
		: FindPackage(nullptr, *Target);
	if (TargetPackage)
	{
		Result.Package = TargetPackage;
		Result.bPackageWasDirty = TargetPackage->IsDirty();
		CaptureAssetDocumentPackageObjectBaseline(
			CollectAssetDocumentPackageObjects(TargetPackage),
			Result);
	}

	if (ExistingAsset && Class == UBlueprint::StaticClass() && ExistingAsset->GetClass() != UBlueprint::StaticClass())
	{
		Result.Error = FString::Printf(TEXT("Existing asset '%s' is not an exact UBlueprint asset"), *Result.ObjectPath);
		return Result;
	}

	if (ExistingAsset && Class == UWidgetBlueprint::StaticClass() && ExistingAsset->GetClass() != UWidgetBlueprint::StaticClass())
	{
		Result.Error = FString::Printf(TEXT("Existing asset '%s' is not an exact UWidgetBlueprint asset"), *Result.ObjectPath);
		return Result;
	}

	if (ExistingAsset && Class == UAnimBlueprint::StaticClass() && ExistingAsset->GetClass() != UAnimBlueprint::StaticClass())
	{
		Result.Error = FString::Printf(TEXT("Existing asset '%s' is not an exact UAnimBlueprint asset"), *Result.ObjectPath);
		return Result;
	}

	if (ExistingAsset && Class == UBlackboardData::StaticClass() && ExistingAsset->GetClass() != UBlackboardData::StaticClass())
	{
		Result.Error = FString::Printf(TEXT("Existing asset '%s' is not an exact UBlackboardData asset"), *Result.ObjectPath);
		return Result;
	}

	if (ExistingAsset && Class == UBehaviorTree::StaticClass() && ExistingAsset->GetClass() != UBehaviorTree::StaticClass())
	{
		Result.Error = FString::Printf(TEXT("Existing asset '%s' is not an exact UBehaviorTree asset"), *Result.ObjectPath);
		return Result;
	}

	if (ExistingAsset && Class != UBlueprint::StaticClass() && !ExistingAsset->IsA(Class))
	{
		Result.Error = FString::Printf(TEXT("Existing asset '%s' is not a '%s'"), *Result.ObjectPath, *Class->GetName());
		return Result;
	}

	if (Action == EAssetDocumentLifecycleAction::Update && !ExistingAsset)
	{
		Result.Error = FString::Printf(TEXT("Update failed because target '%s' does not exist"), *Target);
		return Result;
	}

	if (ExistingAsset)
	{
		Result.Asset = ExistingAsset;
	}
	return Result;
}

FAssetDocumentLifecycleResult FAssetDocumentLifecycle::CreateOrLoad(const FString& Target, UClass* Class, EAssetDocumentLifecycleAction Action, TSharedPtr<FJsonObject> Document)
{
	FAssetDocumentLifecycleResult Result = Resolve(Target, Class, Action);
	if (!Result.Error.IsEmpty() || Result.Asset)
	{
		return Result;
	}

	const FString AssetName = FPackageName::GetLongPackageAssetName(Target);
	if (AssetName.IsEmpty())
	{
		Result.Error = FString::Printf(TEXT("Invalid target '%s'"), *Target);
		return Result;
	}
	if (!ValidateCreateDocument(Class, Document, Result.Error))
	{
		return Result;
	}

	UPackage* PreExistingPackage = Result.Package
		? Result.Package
		: FindPackage(nullptr, *Target);
	const bool bPackageWasDirty = Result.bPackageObjectBaselineCaptured
		? Result.bPackageWasDirty
		: (PreExistingPackage && PreExistingPackage->IsDirty());
	UPackage* Package = CreatePackage(*Target);
	if (!Package)
	{
		Result.Error = FString::Printf(TEXT("Failed to create package '%s'"), *Target);
		return Result;
	}
	Result.Package = Package;
	Result.bOwnsPackage = PreExistingPackage == nullptr;
	Result.bPackageWasDirty = bPackageWasDirty;
	if (!Result.bPackageObjectBaselineCaptured)
	{
		CaptureAssetDocumentPackageObjectBaseline(
			CollectAssetDocumentPackageObjects(Package),
			Result);
	}
	const TSet<UObject*> ObjectsBeforeCreation =
		ResolveAssetDocumentPackageObjectBaseline(Result);

#if WITH_DEV_AUTOMATION_TESTS
	if (ConsumeAssetDocumentLifecycleFailure(
		EAssetDocumentLifecycleCreatePhase::AfterPackageCreate,
		Result))
	{
		return Result;
	}
#endif

	FAssetDocumentLifecycleResult CreatedResult;

	if (Class == UBlueprint::StaticClass())
	{
		CreatedResult = CreateBlueprintAsset(Target, Package, AssetName, Document, false);
	}
	else if (Class == UAnimBlueprint::StaticClass())
	{
		CreatedResult = CreateAnimBlueprintAsset(Target, Package, AssetName, Document, false, false);
	}
	else if (Class == UWidgetBlueprint::StaticClass())
	{
		CreatedResult = CreateWidgetBlueprintAsset(Target, Package, AssetName, Document, false, false);
	}
	else if (Class == UBlackboardData::StaticClass())
	{
		CreatedResult = CreateBlackboardDataAsset(Target, Package, AssetName, Document, false);
	}
	else if (Class == UBehaviorTree::StaticClass())
	{
		CreatedResult = CreateBehaviorTreeAsset(Target, Package, AssetName, Document, false);
	}
	else
	{
		CreatedResult.ObjectPath = Result.ObjectPath;
		CreatedResult.Asset = NewObject<UObject>(Package, Class, *AssetName, RF_Public | RF_Standalone);
		CreatedResult.bCreated = CreatedResult.Asset != nullptr;
		if (!CreatedResult.Asset)
		{
			CreatedResult.Error = FString::Printf(TEXT("Failed to create asset '%s'"), *Result.ObjectPath);
		}
	}

	CreatedResult.Package = Package;
	CreatedResult.bOwnsPackage = Result.bOwnsPackage;
	CreatedResult.bPackageWasDirty = Result.bPackageWasDirty;
	CreatedResult.bPackageObjectBaselineCaptured = Result.bPackageObjectBaselineCaptured;
	CreatedResult.PackageObjectsBeforeCreation = Result.PackageObjectsBeforeCreation;
	CreatedResult.PackageObjectBaselineGuards = Result.PackageObjectBaselineGuards;
	CaptureAssetDocumentCreatedObjects(ObjectsBeforeCreation, Package, CreatedResult);
	if (!CreatedResult.Error.IsEmpty() || !CreatedResult.Asset)
	{
		return CreatedResult;
	}

#if WITH_DEV_AUTOMATION_TESTS
	if (ConsumeAssetDocumentLifecycleFailure(
		EAssetDocumentLifecycleCreatePhase::AfterAssetCreateBeforeRegistry,
		CreatedResult))
	{
		return CreatedResult;
	}
#endif

	FAssetRegistryModule::AssetCreated(CreatedResult.Asset);
	CreatedResult.bRegistryAnnounced = true;
	if (Class == UBlackboardData::StaticClass() || Class == UBehaviorTree::StaticClass())
	{
		Package->MarkPackageDirty();
	}
	return CreatedResult;
}

FAssetDocumentLifecycleResult FAssetDocumentLifecycle::CreateTransientPreview(
	const FString& Target,
	UClass* Class,
	const TSharedPtr<FJsonObject>& Document)
{
	FAssetDocumentLifecycleResult Result;
	Result.ObjectPath = MakeObjectPath(Target);
	if (!Class)
	{
		Result.Error = TEXT("Asset class is required for transient preview creation");
		return Result;
	}
	if (!ValidateCreateDocument(Class, Document, Result.Error))
	{
		return Result;
	}

	const FString RequestedAssetName = FPackageName::GetLongPackageAssetName(Target);
	if (RequestedAssetName.IsEmpty())
	{
		Result.Error = FString::Printf(TEXT("Invalid target '%s'"), *Target);
		return Result;
	}

	const FString PreviewPackageName = FString::Printf(
		TEXT("/Temp/AssetDocumentPreview_%s_%s"),
		*RequestedAssetName,
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UPackage* TransientPackage = CreatePackage(*PreviewPackageName);
	if (!TransientPackage)
	{
		Result.Error = FString::Printf(TEXT("Failed to create transient preview package for '%s'"), *Result.ObjectPath);
		return Result;
	}
	TransientPackage->SetFlags(RF_Transient);
	Result.Package = TransientPackage;
	Result.bOwnsPackage = true;
	const TSet<UObject*> ObjectsBeforeCreation = CollectAssetDocumentPackageObjects(TransientPackage);
	CaptureAssetDocumentPackageObjectBaseline(ObjectsBeforeCreation, Result);
	const FName PreviewName(*RequestedAssetName);
	const FString AssetName = PreviewName.ToString();
	FAssetDocumentLifecycleResult CreatedResult;
	if (Class == UBlueprint::StaticClass())
	{
		CreatedResult = CreateBlueprintAsset(Target, TransientPackage, AssetName, Document, false);
	}
	else if (Class == UAnimBlueprint::StaticClass())
	{
		CreatedResult = CreateAnimBlueprintAsset(Target, TransientPackage, AssetName, Document, false, true);
	}
	else if (Class == UWidgetBlueprint::StaticClass())
	{
		CreatedResult = CreateWidgetBlueprintAsset(Target, TransientPackage, AssetName, Document, false, true);
	}
	else if (Class == UBlackboardData::StaticClass())
	{
		CreatedResult = CreateBlackboardDataAsset(Target, TransientPackage, AssetName, Document, false);
	}
	else if (Class == UBehaviorTree::StaticClass())
	{
		CreatedResult = CreateBehaviorTreeAsset(Target, TransientPackage, AssetName, Document, false);
	}
	else
	{
		CreatedResult.ObjectPath = Result.ObjectPath;
		CreatedResult.Asset = NewObject<UObject>(TransientPackage, Class, PreviewName, RF_Transient);
		CreatedResult.bCreated = CreatedResult.Asset != nullptr;
		if (!CreatedResult.Asset)
		{
			CreatedResult.Error = FString::Printf(TEXT("Failed to create transient preview for '%s'"), *Result.ObjectPath);
		}
	}

	CreatedResult.Package = TransientPackage;
	CreatedResult.bOwnsPackage = true;
	CreatedResult.bPackageObjectBaselineCaptured = Result.bPackageObjectBaselineCaptured;
	CreatedResult.PackageObjectsBeforeCreation = Result.PackageObjectsBeforeCreation;
	CreatedResult.PackageObjectBaselineGuards = Result.PackageObjectBaselineGuards;
	CaptureAssetDocumentCreatedObjects(ObjectsBeforeCreation, TransientPackage, CreatedResult);
	for (const TWeakObjectPtr<UObject>& CreatedObjectHandle : CreatedResult.CreatedObjects)
	{
		if (UObject* CreatedObject = CreatedObjectHandle.Get())
		{
			CreatedObject->ClearFlags(RF_Public | RF_Standalone);
			CreatedObject->SetFlags(RF_Transient);
		}
	}
	TransientPackage->SetDirtyFlag(false);
	return CreatedResult;
}

bool FAssetDocumentLifecycle::HasCleanupWork(
	const FAssetDocumentLifecycleResult& LifecycleResult)
{
	if (LifecycleResult.bCreated
		|| LifecycleResult.bOwnsPackage
		|| LifecycleResult.bRegistryAnnounced
		|| !LifecycleResult.CreatedObjects.IsEmpty())
	{
		return true;
	}

	if (!LifecycleResult.bPackageObjectBaselineCaptured)
	{
		return false;
	}

	UPackage* Package = LifecycleResult.Package
		? LifecycleResult.Package
		: (LifecycleResult.Asset ? LifecycleResult.Asset->GetOutermost() : nullptr);
	if (!Package)
	{
		return false;
	}

	const TSet<UObject*> BaselineObjects = ResolveAssetDocumentPackageObjectBaseline(LifecycleResult);
	for (UObject* PackageObject : CollectAssetDocumentPackageObjects(Package))
	{
		if (!BaselineObjects.Contains(PackageObject))
		{
			return true;
		}
	}
	return false;
}

void FAssetDocumentLifecycle::CleanupCreatedAsset(
	const FAssetDocumentLifecycleResult& LifecycleResult,
	TArray<FAssetDocumentDiagnostic>& OutDiagnostics)
{
	if (!HasCleanupWork(LifecycleResult))
	{
		return;
	}

	UObject* Asset = LifecycleResult.Asset;
	UPackage* Package = LifecycleResult.Package
		? LifecycleResult.Package
		: (Asset ? Asset->GetOutermost() : nullptr);

	if (LifecycleResult.bRegistryAnnounced && Asset)
	{
		FAssetRegistryModule::AssetDeleted(Asset);
	}

	TArray<TStrongObjectPtr<UObject>> BaselineObjectGuards;
	const TSet<UObject*> BaselineObjects = LifecycleResult.bOwnsPackage
		? TSet<UObject*>()
		: ResolveAssetDocumentPackageObjectBaseline(LifecycleResult, &BaselineObjectGuards);
	TSet<UObject*> ObjectsToDiscard;
	if (Asset && (LifecycleResult.bOwnsPackage || !BaselineObjects.Contains(Asset)))
	{
		ObjectsToDiscard.Add(Asset);
		TArray<UObject*> OwnedObjects;
		GetObjectsWithOuter(Asset, OwnedObjects, true);
		for (UObject* OwnedObject : OwnedObjects)
		{
			if (LifecycleResult.bOwnsPackage || !BaselineObjects.Contains(OwnedObject))
			{
				ObjectsToDiscard.Add(OwnedObject);
			}
		}
	}
	if (LifecycleResult.bOwnsPackage)
	{
		for (UObject* PackageObject : CollectAssetDocumentPackageObjects(Package))
		{
			ObjectsToDiscard.Add(PackageObject);
		}
	}
	else
	{
		if (LifecycleResult.bPackageObjectBaselineCaptured)
		{
			for (UObject* PackageObject : CollectAssetDocumentPackageObjects(Package))
			{
				if (!BaselineObjects.Contains(PackageObject))
				{
					ObjectsToDiscard.Add(PackageObject);
				}
			}
		}
		for (const TWeakObjectPtr<UObject>& CreatedObjectHandle : LifecycleResult.CreatedObjects)
		{
			if (UObject* CreatedObject = CreatedObjectHandle.GetEvenIfUnreachable();
				CreatedObject && !BaselineObjects.Contains(CreatedObject))
			{
				ObjectsToDiscard.Add(CreatedObject);
			}
		}
	}
	DetachAssetDocumentOwnedRoots(
		ObjectsToDiscard,
		Package,
		TEXT("lifecycle-created"),
		OutDiagnostics);

	for (UObject* Object : ObjectsToDiscard)
	{
		if (Object && Object != Package)
		{
			DiscardAssetDocumentOwnedObject(Object, TEXT("lifecycle-created"));
		}
	}

	if (Package)
	{
		if (LifecycleResult.bOwnsPackage)
		{
			Package->ClearDirtyFlag();
			DiscardAssetDocumentOwnedObject(Package, TEXT("lifecycle-package"));
		}
		else
		{
			Package->SetDirtyFlag(LifecycleResult.bPackageWasDirty);
		}
	}

#if WITH_DEV_AUTOMATION_TESTS
	FAssetDocumentDiagnostic ForcedDiagnostic;
	if (FAssetDocumentServiceTestHooks::ConsumeLifecycleCleanupVerificationFailure(ForcedDiagnostic))
	{
		OutDiagnostics.Add(MoveTemp(ForcedDiagnostic));
	}
#endif

	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	if (Package && !LifecycleResult.bOwnsPackage)
	{
		Package->SetDirtyFlag(LifecycleResult.bPackageWasDirty);
	}
}

void FAssetDocumentLifecycle::CleanupCreatedAsset(
	const FAssetDocumentLifecycleResult& LifecycleResult)
{
	TArray<FAssetDocumentDiagnostic> IgnoredDiagnostics;
	CleanupCreatedAsset(LifecycleResult, IgnoredDiagnostics);
}

void FAssetDocumentLifecycle::DiscardTransientPreview(
	const FAssetDocumentLifecycleResult& PreviewResult,
	TArray<FAssetDocumentDiagnostic>& OutDiagnostics)
{
	if (!PreviewResult.Asset && !PreviewResult.bOwnsPackage)
	{
		return;
	}

	TSet<UObject*> ObjectsToDiscard;
	if (PreviewResult.Asset)
	{
		ObjectsToDiscard.Add(PreviewResult.Asset);
		TArray<UObject*> OwnedObjects;
		GetObjectsWithOuter(PreviewResult.Asset, OwnedObjects, true);
		for (UObject* OwnedObject : OwnedObjects)
		{
			ObjectsToDiscard.Add(OwnedObject);
		}
	}
	if (PreviewResult.bOwnsPackage)
	{
		for (UObject* PackageObject : CollectAssetDocumentPackageObjects(PreviewResult.Package))
		{
			ObjectsToDiscard.Add(PackageObject);
		}
	}
	else
	{
		for (const TWeakObjectPtr<UObject>& CreatedObjectHandle : PreviewResult.CreatedObjects)
		{
			if (UObject* CreatedObject = CreatedObjectHandle.Get())
			{
				ObjectsToDiscard.Add(CreatedObject);
			}
		}
	}
	DetachAssetDocumentOwnedRoots(
		ObjectsToDiscard,
		PreviewResult.Package,
		TEXT("preview"),
		OutDiagnostics);

	for (UObject* Object : ObjectsToDiscard)
	{
		if (Object && Object != PreviewResult.Package)
		{
			DiscardAssetDocumentOwnedObject(Object, TEXT("preview"));
		}
	}
	if (PreviewResult.bOwnsPackage && PreviewResult.Package)
	{
		PreviewResult.Package->ClearDirtyFlag();
		DiscardAssetDocumentOwnedObject(PreviewResult.Package, TEXT("preview-package"));
	}
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
}

void FAssetDocumentLifecycle::DiscardTransientPreview(
	const FAssetDocumentLifecycleResult& PreviewResult)
{
	TArray<FAssetDocumentDiagnostic> IgnoredDiagnostics;
	DiscardTransientPreview(PreviewResult, IgnoredDiagnostics);
}

FString FAssetDocumentLifecycle::MakeObjectPath(const FString& Target)
{
	return FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
}

bool FAssetDocumentLifecycle::TryResolveBlueprintParentClass(const TSharedPtr<FJsonObject>& Document, UClass*& OutParentClass, FString& OutError)
{
	OutParentClass = nullptr;

	if (!Document.IsValid())
	{
		OutError = TEXT("UBlueprint creation requires Body.ParentClass");
		return false;
	}

	const TSharedPtr<FJsonObject>* Body = nullptr;
	if (!Document->TryGetObjectField(TEXT("Body"), Body) || !Body || !Body->IsValid())
	{
		OutError = TEXT("UBlueprint creation requires Body.ParentClass");
		return false;
	}

	const TSharedPtr<FJsonObject>* ParentClass = nullptr;
	if (!(*Body)->TryGetObjectField(TEXT("ParentClass"), ParentClass) || !ParentClass || !ParentClass->IsValid())
	{
		OutError = TEXT("UBlueprint creation requires Body.ParentClass");
		return false;
	}

	FString Kind;
	if (!(*ParentClass)->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("ClassRef"))
	{
		OutError = TEXT("Body.ParentClass.Kind must be ClassRef");
		return false;
	}

	FString ParentClassPath;
	if (!(*ParentClass)->TryGetStringField(TEXT("Class"), ParentClassPath) || ParentClassPath.IsEmpty())
	{
		OutError = TEXT("Body.ParentClass.Class is required");
		return false;
	}

	OutParentClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ParentClassPath);
	if (!OutParentClass)
	{
		OutError = FString::Printf(TEXT("Failed to resolve Body.ParentClass.Class '%s'"), *ParentClassPath);
		return false;
	}

	if (!OutParentClass->IsChildOf(UObject::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Body.ParentClass.Class '%s' is not a UObject class"), *OutParentClass->GetName());
		return false;
	}

	if (OutParentClass->HasAnyClassFlags(CLASS_Abstract))
	{
		OutError = FString::Printf(TEXT("Body.ParentClass.Class '%s' is abstract"), *OutParentClass->GetName());
		return false;
	}

	return true;
}

bool TryReadLifecycleParentClassRef(const TSharedPtr<FJsonObject>& Document, UClass*& OutParentClass, FString& OutError, const TCHAR* AssetClassName)
{
	OutParentClass = nullptr;

	if (!Document.IsValid())
	{
		OutError = FString::Printf(TEXT("%s creation requires Body.ParentClass"), AssetClassName);
		return false;
	}

	const TSharedPtr<FJsonObject>* Body = nullptr;
	if (!Document->TryGetObjectField(TEXT("Body"), Body) || !Body || !Body->IsValid())
	{
		OutError = FString::Printf(TEXT("%s creation requires Body.ParentClass"), AssetClassName);
		return false;
	}

	const TSharedPtr<FJsonObject>* ParentClass = nullptr;
	if (!(*Body)->TryGetObjectField(TEXT("ParentClass"), ParentClass) || !ParentClass || !ParentClass->IsValid())
	{
		OutError = FString::Printf(TEXT("%s creation requires Body.ParentClass"), AssetClassName);
		return false;
	}

	FString Kind;
	if (!(*ParentClass)->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("ClassRef"))
	{
		OutError = TEXT("Body.ParentClass.Kind must be ClassRef");
		return false;
	}

	FString ParentClassPath;
	if (!(*ParentClass)->TryGetStringField(TEXT("Class"), ParentClassPath) || ParentClassPath.IsEmpty())
	{
		OutError = TEXT("Body.ParentClass.Class is required");
		return false;
	}

	OutParentClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ParentClassPath);
	if (!OutParentClass)
	{
		OutError = FString::Printf(TEXT("Failed to resolve Body.ParentClass.Class '%s'"), *ParentClassPath);
		return false;
	}

	return true;
}

bool TryReadLifecycleBody(const TSharedPtr<FJsonObject>& Document, TSharedPtr<FJsonObject>& OutBody, FString& OutError, const TCHAR* AssetClassName)
{
	if (!Document.IsValid())
	{
		OutError = FString::Printf(TEXT("%s creation requires Body"), AssetClassName);
		return false;
	}

	const TSharedPtr<FJsonObject>* Body = nullptr;
	if (!Document->TryGetObjectField(TEXT("Body"), Body) || !Body || !Body->IsValid())
	{
		OutError = FString::Printf(TEXT("%s creation requires Body"), AssetClassName);
		return false;
	}

	OutBody = *Body;
	return true;
}

bool TryReadLifecycleBoolField(
	const TSharedPtr<FJsonObject>& Body,
	const TCHAR* ObjectFieldName,
	const TCHAR* BoolFieldName,
	bool bDefaultValue,
	bool& bOutValue,
	FString& OutError)
{
	bOutValue = bDefaultValue;
	const TSharedPtr<FJsonObject>* ObjectField = nullptr;
	if (!Body.IsValid() || !Body->TryGetObjectField(ObjectFieldName, ObjectField) || !ObjectField || !ObjectField->IsValid())
	{
		return true;
	}

	if (!(*ObjectField)->HasField(BoolFieldName))
	{
		return true;
	}

	if (!(*ObjectField)->TryGetBoolField(BoolFieldName, bOutValue))
	{
		OutError = FString::Printf(TEXT("Body.%s.%s must be a boolean"), ObjectFieldName, BoolFieldName);
		return false;
	}

	return true;
}

bool TryResolveLifecycleAssetRef(
	const TSharedPtr<FJsonValue>& Value,
	UClass* ExpectedClass,
	const FString& FieldPath,
	UObject*& OutObject,
	FString& OutError)
{
	OutObject = nullptr;
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		return true;
	}
	if (Value->Type != EJson::Object)
	{
		OutError = FString::Printf(TEXT("%s must be an AssetRef object or null"), *FieldPath);
		return false;
	}

	const TSharedPtr<FJsonObject> AssetRef = Value->AsObject();
	if (!AssetRef.IsValid())
	{
		OutError = FString::Printf(TEXT("%s must be an AssetRef object or null"), *FieldPath);
		return false;
	}

	FString Kind;
	if (!AssetRef->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("AssetRef"))
	{
		OutError = FString::Printf(TEXT("%s.Kind must be AssetRef"), *FieldPath);
		return false;
	}

	FString Path;
	if (!AssetRef->TryGetStringField(TEXT("Path"), Path) || Path.TrimStartAndEnd().IsEmpty())
	{
		if (!AssetRef->TryGetStringField(TEXT("Asset"), Path) || Path.TrimStartAndEnd().IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s.Path is required"), *FieldPath);
			return false;
		}
	}

	OutObject = StaticLoadObject(ExpectedClass, nullptr, *Path);
	if (!OutObject || !OutObject->IsA(ExpectedClass))
	{
		OutError = FString::Printf(TEXT("%s '%s' did not resolve to %s"), *FieldPath, *Path, ExpectedClass ? *ExpectedClass->GetName() : TEXT("object"));
		return false;
	}

	return true;
}

bool TryResolveLifecycleBodyAssetRef(
	const TSharedPtr<FJsonObject>& Body,
	const TCHAR* BodyFieldName,
	UClass* ExpectedClass,
	UObject*& OutObject,
	FString& OutError)
{
	return TryResolveLifecycleAssetRef(
		Body.IsValid() ? Body->TryGetField(BodyFieldName) : nullptr,
		ExpectedClass,
		FString::Printf(TEXT("Body.%s"), BodyFieldName),
		OutObject,
		OutError);
}

bool TryResolveLifecycleNestedAssetRef(
	const TSharedPtr<FJsonObject>& Body,
	const TCHAR* ObjectFieldName,
	const TCHAR* AssetFieldName,
	UClass* ExpectedClass,
	UObject*& OutObject,
	FString& OutError)
{
	OutObject = nullptr;
	const TSharedPtr<FJsonObject>* ObjectField = nullptr;
	if (!Body.IsValid() || !Body->TryGetObjectField(ObjectFieldName, ObjectField) || !ObjectField || !ObjectField->IsValid())
	{
		return true;
	}

	return TryResolveLifecycleAssetRef(
		(*ObjectField)->TryGetField(AssetFieldName),
		ExpectedClass,
		FString::Printf(TEXT("Body.%s.%s"), ObjectFieldName, AssetFieldName),
		OutObject,
		OutError);
}

bool FAssetDocumentLifecycle::TryResolveWidgetBlueprintParentClass(const TSharedPtr<FJsonObject>& Document, UClass*& OutParentClass, FString& OutError)
{
	if (!TryReadLifecycleParentClassRef(Document, OutParentClass, OutError, TEXT("WidgetBlueprint")))
	{
		return false;
	}

	if (!OutParentClass->IsChildOf(UUserWidget::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Body.ParentClass.Class '%s' is not a UUserWidget subclass"), *OutParentClass->GetName());
		return false;
	}

	if (OutParentClass->HasAnyClassFlags(CLASS_Abstract) && OutParentClass != UUserWidget::StaticClass())
	{
		OutError = FString::Printf(TEXT("Body.ParentClass.Class '%s' is abstract"), *OutParentClass->GetName());
		return false;
	}

	if (OutParentClass->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		OutError = FString::Printf(TEXT("Body.ParentClass.Class '%s' is deprecated or newer-version-only"), *OutParentClass->GetName());
		return false;
	}

	return true;
}

bool FAssetDocumentLifecycle::TryResolveAnimBlueprintParentClass(const TSharedPtr<FJsonObject>& Document, UClass*& OutParentClass, FString& OutError)
{
	if (!TryReadLifecycleParentClassRef(Document, OutParentClass, OutError, TEXT("AnimBlueprint")))
	{
		return false;
	}

	if (!OutParentClass->IsChildOf(UAnimInstance::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Body.ParentClass.Class '%s' is not a UAnimInstance subclass"), *OutParentClass->GetName());
		return false;
	}

	if (OutParentClass->HasAnyClassFlags(CLASS_Abstract) && OutParentClass != UAnimInstance::StaticClass())
	{
		OutError = FString::Printf(TEXT("Body.ParentClass.Class '%s' is abstract"), *OutParentClass->GetName());
		return false;
	}

	if (OutParentClass->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		OutError = FString::Printf(TEXT("Body.ParentClass.Class '%s' is deprecated or newer-version-only"), *OutParentClass->GetName());
		return false;
	}

	return true;
}

bool FAssetDocumentLifecycle::ValidateCreateDocument(
	UClass* Class,
	const TSharedPtr<FJsonObject>& Document,
	FString& OutError)
{
	OutError.Reset();
	if (!Class)
	{
		OutError = TEXT("Asset class is required for creation");
		return false;
	}

	UClass* ParentClass = nullptr;
	if (Class == UBlueprint::StaticClass())
	{
		return TryResolveBlueprintParentClass(Document, ParentClass, OutError);
	}
	if (Class == UWidgetBlueprint::StaticClass())
	{
		return TryResolveWidgetBlueprintParentClass(Document, ParentClass, OutError);
	}
	if (Class != UAnimBlueprint::StaticClass())
	{
		return true;
	}

	TSharedPtr<FJsonObject> Body;
	if (!TryReadLifecycleBody(Document, Body, OutError, TEXT("AnimBlueprint"))
		|| !TryResolveAnimBlueprintParentClass(Document, ParentClass, OutError))
	{
		return false;
	}

	bool bIsTemplate = false;
	if (!TryReadLifecycleBoolField(Body, TEXT("Template"), TEXT("bIsTemplate"), false, bIsTemplate, OutError))
	{
		return false;
	}

	UObject* ResolvedSkeletonObject = nullptr;
	if (!TryResolveLifecycleBodyAssetRef(Body, TEXT("TargetSkeleton"), USkeleton::StaticClass(), ResolvedSkeletonObject, OutError))
	{
		return false;
	}
	if (bIsTemplate && ResolvedSkeletonObject)
	{
		OutError = TEXT("Template AnimationBlueprints cannot author TargetSkeleton");
		return false;
	}
	if (!bIsTemplate && !ResolvedSkeletonObject)
	{
		OutError = TEXT("MissingTargetSkeleton: non-template AnimBlueprint creation requires TargetSkeleton");
		return false;
	}

	UObject* ResolvedPreviewMeshObject = nullptr;
	return TryResolveLifecycleNestedAssetRef(
		Body,
		TEXT("Preview"),
		TEXT("PreviewSkeletalMesh"),
		USkeletalMesh::StaticClass(),
		ResolvedPreviewMeshObject,
		OutError);
}

FAssetDocumentLifecycleResult FAssetDocumentLifecycle::CreateAnimBlueprintAsset(const FString& Target, UPackage* Package, const FString& AssetName, const TSharedPtr<FJsonObject>& Document, bool bAnnounceToRegistry, bool bIsTransientPreview)
{
	FAssetDocumentLifecycleResult Result;
	Result.ObjectPath = MakeObjectPath(Target);

	TSharedPtr<FJsonObject> Body;
	if (!TryReadLifecycleBody(Document, Body, Result.Error, TEXT("AnimBlueprint")))
	{
		return Result;
	}

	UClass* ParentClass = nullptr;
	if (!TryResolveAnimBlueprintParentClass(Document, ParentClass, Result.Error))
	{
		return Result;
	}

	bool bIsTemplate = false;
	if (!TryReadLifecycleBoolField(Body, TEXT("Template"), TEXT("bIsTemplate"), false, bIsTemplate, Result.Error))
	{
		return Result;
	}

	UObject* ResolvedSkeletonObject = nullptr;
	if (!TryResolveLifecycleBodyAssetRef(Body, TEXT("TargetSkeleton"), USkeleton::StaticClass(), ResolvedSkeletonObject, Result.Error))
	{
		return Result;
	}
	if (bIsTemplate && ResolvedSkeletonObject)
	{
		Result.Error = TEXT("Template AnimationBlueprints cannot author TargetSkeleton");
		return Result;
	}
	if (!bIsTemplate && !ResolvedSkeletonObject)
	{
		Result.Error = TEXT("MissingTargetSkeleton: non-template AnimBlueprint creation requires TargetSkeleton");
		return Result;
	}

	UObject* ResolvedPreviewMeshObject = nullptr;
	if (!TryResolveLifecycleNestedAssetRef(
		Body,
		TEXT("Preview"),
		TEXT("PreviewSkeletalMesh"),
		USkeletalMesh::StaticClass(),
		ResolvedPreviewMeshObject,
		Result.Error))
	{
		return Result;
	}

	UAnimBlueprint* AnimBlueprint = nullptr;
	if (bIsTransientPreview)
	{
		AnimBlueprint = Cast<UAnimBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			ParentClass,
			Package,
			*AssetName,
			BPTYPE_Normal,
			UAnimBlueprint::StaticClass(),
			UAnimBlueprintGeneratedClass::StaticClass()));
		if (AnimBlueprint)
		{
			USkeleton* TargetSkeleton = bIsTemplate ? nullptr : Cast<USkeleton>(ResolvedSkeletonObject);
			AnimBlueprint->bIsTemplate = bIsTemplate;
			AnimBlueprint->TargetSkeleton = TargetSkeleton;
			if (UAnimBlueprintGeneratedClass* GeneratedClass = Cast<UAnimBlueprintGeneratedClass>(AnimBlueprint->GeneratedClass))
			{
				GeneratedClass->TargetSkeleton = TargetSkeleton;
			}
			if (UAnimBlueprintGeneratedClass* SkeletonClass = Cast<UAnimBlueprintGeneratedClass>(AnimBlueprint->SkeletonGeneratedClass))
			{
				SkeletonClass->TargetSkeleton = TargetSkeleton;
			}
			if (TargetSkeleton && ResolvedPreviewMeshObject)
			{
				AnimBlueprint->SetPreviewMesh(Cast<USkeletalMesh>(ResolvedPreviewMeshObject));
			}
		}
	}
	else
	{
		UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
		Factory->BlueprintType = BPTYPE_Normal;
		Factory->ParentClass = ParentClass;
		Factory->TargetSkeleton = bIsTemplate ? nullptr : Cast<USkeleton>(ResolvedSkeletonObject);
		Factory->PreviewSkeletalMesh = Cast<USkeletalMesh>(ResolvedPreviewMeshObject);
		Factory->bTemplate = bIsTemplate;
		AnimBlueprint = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(
			UAnimBlueprint::StaticClass(),
			Package,
			*AssetName,
			RF_Public | RF_Standalone,
			nullptr,
			GWarn));
	}
	if (!AnimBlueprint)
	{
		Result.Error = FString::Printf(TEXT("Failed to create AnimBlueprint asset '%s'"), *Result.ObjectPath);
		return Result;
	}

	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
	if (AnimBlueprint->Status == BS_Error)
	{
		Result.Asset = AnimBlueprint;
		Result.bCreated = true;
		Result.Error = FString::Printf(TEXT("Failed to compile AnimBlueprint asset '%s'"), *Result.ObjectPath);
		return Result;
	}

	if (bAnnounceToRegistry)
	{
		FAssetRegistryModule::AssetCreated(AnimBlueprint);
	}

	Result.Asset = AnimBlueprint;
	Result.bCreated = true;
	return Result;
}

FAssetDocumentLifecycleResult FAssetDocumentLifecycle::CreateBlueprintAsset(const FString& Target, UPackage* Package, const FString& AssetName, const TSharedPtr<FJsonObject>& Document, bool bAnnounceToRegistry)
{
	FAssetDocumentLifecycleResult Result;
	Result.ObjectPath = MakeObjectPath(Target);

	UClass* ParentClass = nullptr;
	if (!TryResolveBlueprintParentClass(Document, ParentClass, Result.Error))
	{
		return Result;
	}

	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		Package,
		*AssetName,
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass());
	if (!Blueprint)
	{
		Result.Error = FString::Printf(TEXT("Failed to create UBlueprint asset '%s'"), *Result.ObjectPath);
		return Result;
	}

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (Blueprint->Status == BS_Error)
	{
		Result.Asset = Blueprint;
		Result.bCreated = true;
		Result.Error = FString::Printf(TEXT("Failed to compile UBlueprint asset '%s'"), *Result.ObjectPath);
		return Result;
	}

	if (bAnnounceToRegistry)
	{
		FAssetRegistryModule::AssetCreated(Blueprint);
	}

	Result.Asset = Blueprint;
	Result.bCreated = true;
	return Result;
}

FAssetDocumentLifecycleResult FAssetDocumentLifecycle::CreateBlackboardDataAsset(const FString& Target, UPackage* Package, const FString& AssetName, const TSharedPtr<FJsonObject>&, bool bAnnounceToRegistry)
{
	FAssetDocumentLifecycleResult Result;
	Result.ObjectPath = MakeObjectPath(Target);

	UBlackboardData* BlackboardData = NewObject<UBlackboardData>(
		Package,
		*AssetName,
		RF_Public | RF_Standalone | RF_Transactional);
	if (!BlackboardData)
	{
		Result.Error = FString::Printf(TEXT("Failed to create BlackboardData asset '%s'"), *Result.ObjectPath);
		return Result;
	}

	if (bAnnounceToRegistry)
	{
		FAssetRegistryModule::AssetCreated(BlackboardData);
		Package->MarkPackageDirty();
	}

	Result.Asset = BlackboardData;
	Result.bCreated = true;
	return Result;
}

FAssetDocumentLifecycleResult FAssetDocumentLifecycle::CreateBehaviorTreeAsset(const FString& Target, UPackage* Package, const FString& AssetName, const TSharedPtr<FJsonObject>&, bool bAnnounceToRegistry)
{
	FAssetDocumentLifecycleResult Result;
	Result.ObjectPath = MakeObjectPath(Target);

	UBehaviorTree* BehaviorTree = NewObject<UBehaviorTree>(
		Package,
		*AssetName,
		RF_Public | RF_Standalone | RF_Transactional);
	if (!BehaviorTree)
	{
		Result.Error = FString::Printf(TEXT("Failed to create BehaviorTree asset '%s'"), *Result.ObjectPath);
		return Result;
	}

	if (bAnnounceToRegistry)
	{
		FAssetRegistryModule::AssetCreated(BehaviorTree);
		Package->MarkPackageDirty();
	}

	Result.Asset = BehaviorTree;
	Result.bCreated = true;
	return Result;
}

FAssetDocumentLifecycleResult FAssetDocumentLifecycle::CreateWidgetBlueprintAsset(const FString& Target, UPackage* Package, const FString& AssetName, const TSharedPtr<FJsonObject>& Document, bool bAnnounceToRegistry, bool bIsTransientPreview)
{
	FAssetDocumentLifecycleResult Result;
	Result.ObjectPath = MakeObjectPath(Target);

	UClass* ParentClass = nullptr;
	if (!TryResolveWidgetBlueprintParentClass(Document, ParentClass, Result.Error))
	{
		return Result;
	}

	UWidgetBlueprint* WidgetBlueprint = nullptr;
	if (bIsTransientPreview)
	{
		WidgetBlueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			ParentClass,
			Package,
			*AssetName,
			BPTYPE_Normal,
			UWidgetBlueprint::StaticClass(),
			UWidgetBlueprintGeneratedClass::StaticClass()));
	}
	else
	{
		UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
		Factory->BlueprintType = BPTYPE_Normal;
		Factory->ParentClass = ParentClass;
		WidgetBlueprint = Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(
			UWidgetBlueprint::StaticClass(),
			Package,
			*AssetName,
			RF_Public | RF_Standalone,
			nullptr,
			GWarn));
	}
	if (!WidgetBlueprint)
	{
		Result.Error = FString::Printf(TEXT("Failed to create WidgetBlueprint asset '%s'"), *Result.ObjectPath);
		return Result;
	}

#if WITH_EDITORONLY_DATA
	if (WidgetBlueprint->WidgetTree)
	{
		WidgetBlueprint->WidgetTree->RootWidget = nullptr;
		WidgetBlueprint->WidgetTree->NamedSlotBindings.Empty();
	}
	WidgetBlueprint->Bindings.Empty();
	WidgetBlueprint->Animations.Empty();
#endif

	FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
	if (WidgetBlueprint->Status == BS_Error)
	{
		Result.Asset = WidgetBlueprint;
		Result.bCreated = true;
		Result.Error = FString::Printf(TEXT("Failed to compile WidgetBlueprint asset '%s'"), *Result.ObjectPath);
		return Result;
	}

	if (bAnnounceToRegistry)
	{
		FAssetRegistryModule::AssetCreated(WidgetBlueprint);
	}

	Result.Asset = WidgetBlueprint;
	Result.bCreated = true;
	return Result;
}
