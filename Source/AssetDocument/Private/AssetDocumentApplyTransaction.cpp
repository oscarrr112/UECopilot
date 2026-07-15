// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentApplyTransaction.h"

#include "AssetDocumentPropertyAdapter.h"
#include "AssetDocumentServiceTestHooks.h"

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
	if (ReflectedPropertySnapshot.IsValid())
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

	RemoveNewOwnedObjects(OutDiagnostics);
	VerifyOwnedObjects(OutDiagnostics);
	if (ExistingPackage)
	{
		ExistingPackage->SetDirtyFlag(bWasPackageDirty);
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
