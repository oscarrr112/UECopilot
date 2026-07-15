// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentServiceTestHooks.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
uint64 NextApplyFailureGeneration = 1;
TOptional<EAssetDocumentServiceApplyPhase> PendingApplyFailurePhase;
TOptional<FAssetDocumentDiagnostic> PendingApplyFailureDiagnostic;
TFunction<void()> PendingApplyBeforeFailure;
TOptional<uint64> PendingApplyFailureGeneration;
TOptional<FAssetDocumentDiagnostic> PendingRollbackVerificationDiagnostic;
TOptional<uint64> PendingRollbackVerificationGeneration;
TOptional<EAssetDocumentLifecycleCreatePhase> PendingLifecycleCreateFailurePhase;
TOptional<FAssetDocumentDiagnostic> PendingLifecycleCreateFailureDiagnostic;
bool bPendingLifecycleCreateFailureEmitsDiagnostic = true;
TOptional<FAssetDocumentDiagnostic> PendingLifecycleCleanupVerificationDiagnostic;
TOptional<FAssetDocumentDiagnostic> AssetDocumentServiceTestHooksPendingApplySnapshotFailureDiagnostic;
TOptional<EAssetDocumentServicePersistencePhase> AssetDocumentServiceTestHooksPendingPersistenceFailurePhase;
TOptional<FAssetDocumentDiagnostic> AssetDocumentServiceTestHooksPendingPersistenceFailureDiagnostic;
TOptional<EAssetDocumentServicePersistencePhase> AssetDocumentServiceTestHooksPendingPersistenceCallbackPhase;
TFunction<void()> AssetDocumentServiceTestHooksPendingPersistenceCallback;
TOptional<FAssetDocumentDiagnostic> AssetDocumentServiceTestHooksPendingMetadataRecoveryRefreshFailureDiagnostic;

void CheckAssetDocumentTestHookThread()
{
	checkf(IsInGameThread(), TEXT("AssetDocument service test hooks are game-thread-only"));
}
}

void FAssetDocumentServiceTestHooks::FailNextApplyAtPhase(
	EAssetDocumentServiceApplyPhase Phase,
	const FAssetDocumentDiagnostic& Diagnostic,
	TFunction<void()> BeforeFailure)
{
	CheckAssetDocumentTestHookThread();
	PendingApplyFailurePhase = Phase;
	PendingApplyFailureDiagnostic = Diagnostic;
	PendingApplyBeforeFailure = MoveTemp(BeforeFailure);
	PendingApplyFailureGeneration = NextApplyFailureGeneration++;
}

void FAssetDocumentServiceTestHooks::FailNextRollbackVerification(
	const FAssetDocumentDiagnostic& Diagnostic)
{
	CheckAssetDocumentTestHookThread();
	checkf(
		PendingApplyFailureGeneration.IsSet(),
		TEXT("Rollback verification failure must bind to an armed Apply phase failure"));
	PendingRollbackVerificationDiagnostic = Diagnostic;
	PendingRollbackVerificationGeneration = PendingApplyFailureGeneration.GetValue();
}

void FAssetDocumentServiceTestHooks::FailNextLifecycleCreateAtPhase(
	EAssetDocumentLifecycleCreatePhase Phase,
	const FAssetDocumentDiagnostic& Diagnostic,
	bool bEmitDiagnostic)
{
	CheckAssetDocumentTestHookThread();
	PendingLifecycleCreateFailurePhase = Phase;
	PendingLifecycleCreateFailureDiagnostic = Diagnostic;
	bPendingLifecycleCreateFailureEmitsDiagnostic = bEmitDiagnostic;
}

void FAssetDocumentServiceTestHooks::FailNextLifecycleCleanupVerification(
	const FAssetDocumentDiagnostic& Diagnostic)
{
	CheckAssetDocumentTestHookThread();
	PendingLifecycleCleanupVerificationDiagnostic = Diagnostic;
}

void FAssetDocumentServiceTestHooks::FailNextApplySnapshot(
	const FAssetDocumentDiagnostic& Diagnostic)
{
	CheckAssetDocumentTestHookThread();
	AssetDocumentServiceTestHooksPendingApplySnapshotFailureDiagnostic = Diagnostic;
}

void FAssetDocumentServiceTestHooks::FailNextPersistenceAtPhase(
	EAssetDocumentServicePersistencePhase Phase,
	const FAssetDocumentDiagnostic& Diagnostic)
{
	CheckAssetDocumentTestHookThread();
	AssetDocumentServiceTestHooksPendingPersistenceFailurePhase = Phase;
	AssetDocumentServiceTestHooksPendingPersistenceFailureDiagnostic = Diagnostic;
}

void FAssetDocumentServiceTestHooks::RunNextPersistenceCallbackAtPhase(
	EAssetDocumentServicePersistencePhase Phase,
	TFunction<void()> Callback)
{
	CheckAssetDocumentTestHookThread();
	AssetDocumentServiceTestHooksPendingPersistenceCallbackPhase = Phase;
	AssetDocumentServiceTestHooksPendingPersistenceCallback = MoveTemp(Callback);
}

void FAssetDocumentServiceTestHooks::FailNextMetadataRecoveryRefresh(
	const FAssetDocumentDiagnostic& Diagnostic)
{
	CheckAssetDocumentTestHookThread();
	AssetDocumentServiceTestHooksPendingMetadataRecoveryRefreshFailureDiagnostic = Diagnostic;
}

bool FAssetDocumentServiceTestHooks::ConsumeApplyFailure(
	EAssetDocumentServiceApplyPhase Phase,
	FAssetDocumentDiagnostic& OutDiagnostic,
	uint64* OutFailureGeneration)
{
	CheckAssetDocumentTestHookThread();
	if (!PendingApplyFailurePhase.IsSet()
		|| PendingApplyFailurePhase.GetValue() != Phase
		|| !PendingApplyFailureDiagnostic.IsSet()
		|| !PendingApplyFailureGeneration.IsSet())
	{
		return false;
	}

	OutDiagnostic = PendingApplyFailureDiagnostic.GetValue();
	if (OutFailureGeneration)
	{
		*OutFailureGeneration = PendingApplyFailureGeneration.GetValue();
	}
	TFunction<void()> BeforeFailure = MoveTemp(PendingApplyBeforeFailure);
	PendingApplyFailurePhase.Reset();
	PendingApplyFailureDiagnostic.Reset();
	PendingApplyBeforeFailure = {};
	PendingApplyFailureGeneration.Reset();
	if (BeforeFailure)
	{
		BeforeFailure();
	}
	return true;
}

bool FAssetDocumentServiceTestHooks::ConsumeRollbackVerificationFailure(
	uint64 FailureGeneration,
	FAssetDocumentDiagnostic& OutDiagnostic)
{
	CheckAssetDocumentTestHookThread();
	if (!PendingRollbackVerificationDiagnostic.IsSet()
		|| !PendingRollbackVerificationGeneration.IsSet()
		|| PendingRollbackVerificationGeneration.GetValue() != FailureGeneration)
	{
		return false;
	}

	OutDiagnostic = PendingRollbackVerificationDiagnostic.GetValue();
	PendingRollbackVerificationDiagnostic.Reset();
	PendingRollbackVerificationGeneration.Reset();
	return true;
}

bool FAssetDocumentServiceTestHooks::ConsumeLifecycleCreateFailure(
	EAssetDocumentLifecycleCreatePhase Phase,
	FAssetDocumentDiagnostic& OutDiagnostic,
	bool* bOutEmitDiagnostic)
{
	CheckAssetDocumentTestHookThread();
	if (!PendingLifecycleCreateFailurePhase.IsSet()
		|| PendingLifecycleCreateFailurePhase.GetValue() != Phase
		|| !PendingLifecycleCreateFailureDiagnostic.IsSet())
	{
		return false;
	}

	OutDiagnostic = PendingLifecycleCreateFailureDiagnostic.GetValue();
	if (bOutEmitDiagnostic)
	{
		*bOutEmitDiagnostic = bPendingLifecycleCreateFailureEmitsDiagnostic;
	}
	PendingLifecycleCreateFailurePhase.Reset();
	PendingLifecycleCreateFailureDiagnostic.Reset();
	bPendingLifecycleCreateFailureEmitsDiagnostic = true;
	return true;
}

bool FAssetDocumentServiceTestHooks::ConsumeLifecycleCleanupVerificationFailure(
	FAssetDocumentDiagnostic& OutDiagnostic)
{
	CheckAssetDocumentTestHookThread();
	if (!PendingLifecycleCleanupVerificationDiagnostic.IsSet())
	{
		return false;
	}

	OutDiagnostic = PendingLifecycleCleanupVerificationDiagnostic.GetValue();
	PendingLifecycleCleanupVerificationDiagnostic.Reset();
	return true;
}

bool FAssetDocumentServiceTestHooks::ConsumeApplySnapshotFailure(
	FAssetDocumentDiagnostic& OutDiagnostic)
{
	CheckAssetDocumentTestHookThread();
	if (!AssetDocumentServiceTestHooksPendingApplySnapshotFailureDiagnostic.IsSet())
	{
		return false;
	}
	OutDiagnostic = AssetDocumentServiceTestHooksPendingApplySnapshotFailureDiagnostic.GetValue();
	AssetDocumentServiceTestHooksPendingApplySnapshotFailureDiagnostic.Reset();
	return true;
}

bool FAssetDocumentServiceTestHooks::ConsumePersistenceFailure(
	EAssetDocumentServicePersistencePhase Phase,
	FAssetDocumentDiagnostic& OutDiagnostic)
{
	CheckAssetDocumentTestHookThread();
	if (!AssetDocumentServiceTestHooksPendingPersistenceFailurePhase.IsSet()
		|| AssetDocumentServiceTestHooksPendingPersistenceFailurePhase.GetValue() != Phase
		|| !AssetDocumentServiceTestHooksPendingPersistenceFailureDiagnostic.IsSet())
	{
		return false;
	}

	OutDiagnostic = AssetDocumentServiceTestHooksPendingPersistenceFailureDiagnostic.GetValue();
	AssetDocumentServiceTestHooksPendingPersistenceFailurePhase.Reset();
	AssetDocumentServiceTestHooksPendingPersistenceFailureDiagnostic.Reset();
	return true;
}

void FAssetDocumentServiceTestHooks::ConsumePersistenceCallback(
	EAssetDocumentServicePersistencePhase Phase)
{
	CheckAssetDocumentTestHookThread();
	if (!AssetDocumentServiceTestHooksPendingPersistenceCallbackPhase.IsSet()
		|| AssetDocumentServiceTestHooksPendingPersistenceCallbackPhase.GetValue() != Phase)
	{
		return;
	}

	TFunction<void()> Callback = MoveTemp(AssetDocumentServiceTestHooksPendingPersistenceCallback);
	AssetDocumentServiceTestHooksPendingPersistenceCallbackPhase.Reset();
	AssetDocumentServiceTestHooksPendingPersistenceCallback = {};
	if (Callback)
	{
		Callback();
	}
}

bool FAssetDocumentServiceTestHooks::ConsumeMetadataRecoveryRefreshFailure(
	FAssetDocumentDiagnostic& OutDiagnostic)
{
	CheckAssetDocumentTestHookThread();
	if (!AssetDocumentServiceTestHooksPendingMetadataRecoveryRefreshFailureDiagnostic.IsSet())
	{
		return false;
	}

	OutDiagnostic = AssetDocumentServiceTestHooksPendingMetadataRecoveryRefreshFailureDiagnostic.GetValue();
	AssetDocumentServiceTestHooksPendingMetadataRecoveryRefreshFailureDiagnostic.Reset();
	return true;
}

void FAssetDocumentServiceTestHooks::Clear()
{
	CheckAssetDocumentTestHookThread();
	PendingApplyFailurePhase.Reset();
	PendingApplyFailureDiagnostic.Reset();
	PendingApplyBeforeFailure = {};
	PendingApplyFailureGeneration.Reset();
	PendingRollbackVerificationDiagnostic.Reset();
	PendingRollbackVerificationGeneration.Reset();
	PendingLifecycleCreateFailurePhase.Reset();
	PendingLifecycleCreateFailureDiagnostic.Reset();
	bPendingLifecycleCreateFailureEmitsDiagnostic = true;
	PendingLifecycleCleanupVerificationDiagnostic.Reset();
	AssetDocumentServiceTestHooksPendingApplySnapshotFailureDiagnostic.Reset();
	AssetDocumentServiceTestHooksPendingPersistenceFailurePhase.Reset();
	AssetDocumentServiceTestHooksPendingPersistenceFailureDiagnostic.Reset();
	AssetDocumentServiceTestHooksPendingPersistenceCallbackPhase.Reset();
	AssetDocumentServiceTestHooksPendingPersistenceCallback = {};
	AssetDocumentServiceTestHooksPendingMetadataRecoveryRefreshFailureDiagnostic.Reset();
}

#endif
