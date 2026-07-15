// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentTypes.h"
#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

enum class EAssetDocumentServiceApplyPhase : uint8
{
	AfterStagedDocument,
	AfterNewLiveMaterialize,
	AfterLiveProperties,
	AfterNewLiveBody,
};

enum class EAssetDocumentLifecycleCreatePhase : uint8
{
	AfterPackageCreate,
	AfterAssetCreateBeforeRegistry,
};

class FAssetDocumentServiceTestHooks
{
public:
	static void FailNextApplyAtPhase(
		EAssetDocumentServiceApplyPhase Phase,
		const FAssetDocumentDiagnostic& Diagnostic,
		TFunction<void()> BeforeFailure = {});

	static void FailNextRollbackVerification(const FAssetDocumentDiagnostic& Diagnostic);
	static void FailNextLifecycleCreateAtPhase(
		EAssetDocumentLifecycleCreatePhase Phase,
		const FAssetDocumentDiagnostic& Diagnostic,
		bool bEmitDiagnostic = true);
	static void FailNextLifecycleCleanupVerification(const FAssetDocumentDiagnostic& Diagnostic);

	static bool ConsumeApplyFailure(
		EAssetDocumentServiceApplyPhase Phase,
		FAssetDocumentDiagnostic& OutDiagnostic,
		uint64* OutFailureGeneration = nullptr);

	static bool ConsumeRollbackVerificationFailure(
		uint64 FailureGeneration,
		FAssetDocumentDiagnostic& OutDiagnostic);
	static bool ConsumeLifecycleCreateFailure(
		EAssetDocumentLifecycleCreatePhase Phase,
		FAssetDocumentDiagnostic& OutDiagnostic,
		bool* bOutEmitDiagnostic = nullptr);
	static bool ConsumeLifecycleCleanupVerificationFailure(FAssetDocumentDiagnostic& OutDiagnostic);

	static void Clear();
};

#endif
