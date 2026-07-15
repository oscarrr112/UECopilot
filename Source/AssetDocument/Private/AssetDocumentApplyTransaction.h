// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentLifecycle.h"
#include "AssetDocumentTypes.h"
#include "CoreMinimal.h"

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

private:
	void RollbackExisting(TArray<FAssetDocumentDiagnostic>& OutDiagnostics);
	void RemoveNewOwnedObjects(TArray<FAssetDocumentDiagnostic>& OutDiagnostics);
	void VerifyOwnedObjects(TArray<FAssetDocumentDiagnostic>& OutDiagnostics) const;

	UObject* ExistingAsset = nullptr;
	UPackage* ExistingPackage = nullptr;
	bool bWasPackageDirty = false;
	TSharedPtr<FJsonObject> ReflectedPropertySnapshot;
	TSet<UObject*> OriginalOwnedObjects;
	FAssetDocumentLifecycleResult LifecycleResult;
#if WITH_DEV_AUTOMATION_TESTS
	TOptional<uint64> ForcedFailureGeneration;
#endif
	bool bActive = true;
};
