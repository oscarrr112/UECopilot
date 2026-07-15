// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentApplyTransaction.h"

#include "AssetDocumentPropertyAdapter.h"
#include "AssetDocumentServiceTestHooks.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace
{
TSet<UObject*> AssetDocumentCollectRecursiveOwned(UObject* Asset)
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

int32 AssetDocumentOwnedDepth(const UObject* Object, const UObject* Asset)
{
	int32 Depth = 0;
	for (const UObject* Outer = Object ? Object->GetOuter() : nullptr; Outer && Outer != Asset; Outer = Outer->GetOuter())
	{
		++Depth;
	}
	return Depth;
}

void AssetDocumentAddRollbackDiagnostic(
	TArray<FAssetDocumentDiagnostic>& Diagnostics,
	const FString& Code,
	const FString& Message)
{
	FAssetDocumentDiagnostic Diagnostic;
	Diagnostic.Path = TEXT("/Apply/Rollback");
	Diagnostic.Code = Code;
	Diagnostic.Message = Message;
	Diagnostics.Add(MoveTemp(Diagnostic));
}
}

FAssetDocumentApplyTransaction::FAssetDocumentApplyTransaction(
	UObject* InExistingAsset,
	const TSet<FName>& ExcludedPropertyNames)
	: ExistingAsset(InExistingAsset)
{
	if (!ExistingAsset)
	{
		return;
	}

	ExistingPackage = ExistingAsset->GetOutermost();
	bWasPackageDirty = ExistingPackage && ExistingPackage->IsDirty();
	ReflectedPropertySnapshot = FAssetDocumentPropertyAdapter::ExtractWritablePropertiesToJson(
		ExistingAsset,
		false,
		&ExcludedPropertyNames);
	OriginalOwnedObjects = AssetDocumentCollectRecursiveOwned(ExistingAsset);
	if (ExistingAsset->GetClass() != UBehaviorTree::StaticClass()
		&& ExistingAsset->GetClass() != UBlackboardData::StaticClass())
	{
		return;
	}
	bHasObjectSnapshots = true;
	if (!ExistingPackage)
	{
		bSnapshotReady = false;
		SnapshotError = TEXT("Strict AssetDocument snapshot requires an existing outer package");
		return;
	}

#if WITH_DEV_AUTOMATION_TESTS
	FAssetDocumentDiagnostic ForcedSnapshotFailure;
	if (FAssetDocumentServiceTestHooks::ConsumeApplySnapshotFailure(ForcedSnapshotFailure))
	{
		bSnapshotReady = false;
		SnapshotError = ForcedSnapshotFailure.Message.IsEmpty()
			? TEXT("Injected strict AssetDocument snapshot failure")
			: ForcedSnapshotFailure.Message;
		return;
	}
#endif

	TArray<UObject*> ObjectsToSnapshot;
	if (ExistingPackage)
	{
		GetObjectsWithOuter(ExistingPackage, ObjectsToSnapshot, true);
	}
	if (!ObjectsToSnapshot.Contains(ExistingAsset))
	{
		ObjectsToSnapshot.Add(ExistingAsset);
	}
	ObjectSnapshots.Reserve(ObjectsToSnapshot.Num());
	for (UObject* Object : ObjectsToSnapshot)
	{
		FObjectSnapshot& Snapshot = ObjectSnapshots.AddDefaulted_GetRef();
		Snapshot.Object.Reset(Object);
		Snapshot.OriginalOuter = Object->GetOuter();
		Snapshot.OriginalName = Object->GetFName();
		Snapshot.OriginalFlags = Object->GetFlags();
		Snapshot.bWasRooted = Object->IsRooted();
		Snapshot.OriginalDepth = AssetDocumentOwnedDepth(Object, ExistingPackage);
		// Full serialization is intentional. Delta serialization would omit values
		// equal to archetype defaults and could not restore an exact live snapshot.
		FObjectWriter Writer(
			Object,
			Snapshot.SerializedBytes,
			false,
			false,
			false,
			PPF_DuplicateVerbatim);
		if (Writer.IsError())
		{
			bObjectSnapshotsComplete = false;
			bSnapshotReady = false;
			if (SnapshotError.IsEmpty())
			{
				SnapshotError = FString::Printf(
					TEXT("Failed to serialize strict AssetDocument snapshot object '%s'"),
					*Object->GetPathName());
			}
		}
	}
	ObjectSnapshots.Sort([](const FObjectSnapshot& A, const FObjectSnapshot& B)
	{
		return A.OriginalDepth < B.OriginalDepth;
	});
}

void FAssetDocumentApplyTransaction::AttachLifecycleResult(
	const FAssetDocumentLifecycleResult& InLifecycleResult)
{
	LifecycleResult = InLifecycleResult;
}

#if WITH_DEV_AUTOMATION_TESTS
void FAssetDocumentApplyTransaction::BindForcedFailureGeneration(uint64 FailureGeneration)
{
	ForcedFailureGeneration = FailureGeneration;
}
#endif

void FAssetDocumentApplyTransaction::Commit()
{
	bActive = false;
}

void FAssetDocumentApplyTransaction::Rollback(TArray<FAssetDocumentDiagnostic>& OutDiagnostics)
{
	if (!bActive)
	{
		return;
	}
	bActive = false;

	if (ExistingAsset)
	{
		RollbackExisting(OutDiagnostics);
	}
	if (FAssetDocumentLifecycle::HasCleanupWork(LifecycleResult))
	{
		FAssetDocumentLifecycle::CleanupCreatedAsset(LifecycleResult, OutDiagnostics);
	}

#if WITH_DEV_AUTOMATION_TESTS
	FAssetDocumentDiagnostic ForcedDiagnostic;
	if (ForcedFailureGeneration.IsSet()
		&& FAssetDocumentServiceTestHooks::ConsumeRollbackVerificationFailure(
			ForcedFailureGeneration.GetValue(),
			ForcedDiagnostic))
	{
		OutDiagnostics.Add(MoveTemp(ForcedDiagnostic));
	}
#endif
}

void FAssetDocumentApplyTransaction::RollbackExisting(
	TArray<FAssetDocumentDiagnostic>& OutDiagnostics)
{
	RemoveNewOwnedObjects(OutDiagnostics);
	if (bHasObjectSnapshots)
	{
		RestoreOriginalObjectIdentities(OutDiagnostics);
		RestoreOriginalObjectBytes(OutDiagnostics);
	}
	if ((!bHasObjectSnapshots || !bObjectSnapshotsComplete || bObjectSnapshotRestoreFailed)
		&& ReflectedPropertySnapshot.IsValid())
	{
		const FAssetDocumentPropertyApplyResult PropertyResult =
			FAssetDocumentPropertyAdapter::ApplyProperties(ExistingAsset, ReflectedPropertySnapshot);
		if (!PropertyResult.bSuccess)
		{
			AssetDocumentAddRollbackDiagnostic(
				OutDiagnostics,
				TEXT("AssetDocumentPropertyRollbackFailed"),
				PropertyResult.Message.IsEmpty()
					? TEXT("Failed to restore reflected Properties during AssetDocument rollback")
					: PropertyResult.Message);
		}
	}
	VerifyOwnedObjects(OutDiagnostics);
	if (ExistingPackage)
	{
		ExistingPackage->SetDirtyFlag(bWasPackageDirty);
	}
}

void FAssetDocumentApplyTransaction::RestoreOriginalObjectIdentities(
	TArray<FAssetDocumentDiagnostic>& OutDiagnostics)
{
	bool bIdentityRestoreFailed = false;
	for (FObjectSnapshot& Snapshot : ObjectSnapshots)
	{
		UObject* Object = Snapshot.Object.Get();
		if (!Object)
		{
			bIdentityRestoreFailed = true;
			continue;
		}
		if (Object->IsUnreachable() || Object->HasAnyInternalFlags(EInternalObjectFlags::Garbage))
		{
			Object->ClearGarbage();
			Object->ClearInternalFlags(EInternalObjectFlags::Unreachable);
		}

		if (Object->GetOuter() != Snapshot.OriginalOuter
			|| Object->GetFName() != Snapshot.OriginalName)
		{
			if (!Object->Rename(
					*Snapshot.OriginalName.ToString(),
					Snapshot.OriginalOuter,
					REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty))
			{
				bIdentityRestoreFailed = true;
			}
		}

		const EObjectFlags ImmutableFlags = RF_MarkAsNative | RF_MarkAsRootSet | RF_MirroredGarbage;
		const EObjectFlags MutableMask = static_cast<EObjectFlags>(RF_AllFlags & ~ImmutableFlags);
		const EObjectFlags CurrentMutableFlags = static_cast<EObjectFlags>(Object->GetFlags() & MutableMask);
		const EObjectFlags OriginalMutableFlags = static_cast<EObjectFlags>(Snapshot.OriginalFlags & MutableMask);
		Object->ClearFlags(static_cast<EObjectFlags>(CurrentMutableFlags & ~OriginalMutableFlags));
		Object->SetFlags(static_cast<EObjectFlags>(OriginalMutableFlags & ~CurrentMutableFlags));
		if (Snapshot.bWasRooted && !Object->IsRooted())
		{
			Object->AddToRoot();
		}
		else if (!Snapshot.bWasRooted && Object->IsRooted())
		{
			Object->RemoveFromRoot();
		}
	}

	if (bIdentityRestoreFailed)
	{
		AssetDocumentAddRollbackDiagnostic(
			OutDiagnostics,
			TEXT("AssetDocumentObjectIdentityRollbackFailed"),
			TEXT("Failed to restore every original UObject outer/name/flag identity during rollback"));
	}
}

void FAssetDocumentApplyTransaction::RestoreOriginalObjectBytes(
	TArray<FAssetDocumentDiagnostic>& OutDiagnostics)
{
	bool bRestoreFailed = !bObjectSnapshotsComplete;
	for (FObjectSnapshot& Snapshot : ObjectSnapshots)
	{
		UObject* Object = Snapshot.Object.Get();
		if (!Object)
		{
			bRestoreFailed = true;
			continue;
		}
		FObjectReader Reader(Snapshot.SerializedBytes);
		Reader.SetPortFlags(PPF_DuplicateVerbatim);
		Object->Serialize(Reader);
		bRestoreFailed |= Reader.IsError();
	}
	for (int32 SnapshotIndex = ObjectSnapshots.Num() - 1; SnapshotIndex >= 0; --SnapshotIndex)
	{
		if (UObject* Object = ObjectSnapshots[SnapshotIndex].Object.Get())
		{
			// Mirrors the repair notification used by UE's editor transaction path.
			// BT graph pins and Blackboard derived caches are allowed to rebuild from
			// the just-restored serialized authored state here.
			Object->PostEditUndo();
		}
	}
	bObjectSnapshotRestoreFailed = bRestoreFailed;

	if (bRestoreFailed)
	{
		AssetDocumentAddRollbackDiagnostic(
			OutDiagnostics,
			TEXT("AssetDocumentObjectSnapshotRollbackFailed"),
			TEXT("Failed to restore every original UObject serialized snapshot during rollback"));
	}
}

void FAssetDocumentApplyTransaction::RemoveNewOwnedObjects(
	TArray<FAssetDocumentDiagnostic>& OutDiagnostics)
{
	TArray<UObject*> NewOwnedObjects;
	for (UObject* Object : AssetDocumentCollectRecursiveOwned(ExistingAsset))
	{
		if (!OriginalOwnedObjects.Contains(Object))
		{
			NewOwnedObjects.Add(Object);
		}
	}

	NewOwnedObjects.Sort([this](const UObject& A, const UObject& B)
	{
		return AssetDocumentOwnedDepth(&A, ExistingAsset)
			> AssetDocumentOwnedDepth(&B, ExistingAsset);
	});

	bool bCleanupFailed = false;
	for (UObject* Object : NewOwnedObjects)
	{
		if (!Object)
		{
			continue;
		}

		if (Object->IsRooted())
		{
			Object->RemoveFromRoot();
		}
		Object->ClearFlags(RF_Public | RF_Standalone);
		Object->SetFlags(RF_Transient);
		const FName TransientName = MakeUniqueObjectName(
			GetTransientPackage(),
			Object->GetClass(),
			Object->GetFName());
		if (!Object->Rename(
			*TransientName.ToString(),
			GetTransientPackage(),
			REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty))
		{
			bCleanupFailed = true;
		}
		Object->MarkAsGarbage();
	}

	if (bCleanupFailed)
	{
		AssetDocumentAddRollbackDiagnostic(
			OutDiagnostics,
			TEXT("AssetDocumentOwnedObjectCleanupFailed"),
			TEXT("Failed to detach every newly owned UObject during AssetDocument rollback"));
	}
}

void FAssetDocumentApplyTransaction::VerifyOwnedObjects(
	TArray<FAssetDocumentDiagnostic>& OutDiagnostics) const
{
	const TSet<UObject*> CurrentOwnedObjects = AssetDocumentCollectRecursiveOwned(ExistingAsset);
	bool bExact = CurrentOwnedObjects.Num() == OriginalOwnedObjects.Num();
	if (bExact)
	{
		for (UObject* Object : OriginalOwnedObjects)
		{
			if (!CurrentOwnedObjects.Contains(Object))
			{
				bExact = false;
				break;
			}
		}
	}

	if (!bExact)
	{
		AssetDocumentAddRollbackDiagnostic(
			OutDiagnostics,
			TEXT("AssetDocumentOwnedObjectRollbackFailed"),
			TEXT("Recursive UObject ownership did not match the pre-apply snapshot after rollback"));
	}
}
