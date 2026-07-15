// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentLifecycle.h"
#include "AssetDocumentTypes.h"
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/StrongObjectPtr.h"

class FJsonObject;

class FAssetDocumentApplyTransaction
{
public:
	explicit FAssetDocumentApplyTransaction(
		UObject* ExistingAsset,
		const TSet<FName>& ExcludedPropertyNames);

	void AttachLifecycleResult(const FAssetDocumentLifecycleResult& InLifecycleResult);
#if WITH_DEV_AUTOMATION_TESTS
	void BindForcedFailureGeneration(uint64 FailureGeneration);
#endif
	void Commit();
	void Rollback(TArray<FAssetDocumentDiagnostic>& OutDiagnostics);
	bool IsSnapshotReady() const { return bSnapshotReady; }
	const FString& GetSnapshotError() const { return SnapshotError; }

private:
	struct FObjectSnapshot
	{
		TStrongObjectPtr<UObject> Object;
		UObject* OriginalOuter = nullptr;
		FName OriginalName;
		EObjectFlags OriginalFlags = RF_NoFlags;
		bool bWasRooted = false;
		int32 OriginalDepth = 0;
		TArray<uint8> SerializedBytes;
	};

	void RollbackExisting(TArray<FAssetDocumentDiagnostic>& OutDiagnostics);
	void RemoveNewOwnedObjects(TArray<FAssetDocumentDiagnostic>& OutDiagnostics);
	void RestoreOriginalObjectIdentities(TArray<FAssetDocumentDiagnostic>& OutDiagnostics);
	void RestoreOriginalObjectBytes(TArray<FAssetDocumentDiagnostic>& OutDiagnostics);
	void VerifyOwnedObjects(TArray<FAssetDocumentDiagnostic>& OutDiagnostics) const;

	UObject* ExistingAsset = nullptr;
	UPackage* ExistingPackage = nullptr;
	bool bWasPackageDirty = false;
	TSharedPtr<FJsonObject> ReflectedPropertySnapshot;
	TSet<UObject*> OriginalOwnedObjects;
	TArray<FObjectSnapshot> ObjectSnapshots;
	bool bHasObjectSnapshots = false;
	bool bObjectSnapshotsComplete = true;
	bool bObjectSnapshotRestoreFailed = false;
	bool bSnapshotReady = true;
	FString SnapshotError;
	FAssetDocumentLifecycleResult LifecycleResult;
#if WITH_DEV_AUTOMATION_TESTS
	TOptional<uint64> ForcedFailureGeneration;
#endif
	bool bActive = true;
};
