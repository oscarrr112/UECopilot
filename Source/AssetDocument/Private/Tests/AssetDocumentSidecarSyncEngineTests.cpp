// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentSidecarSyncEngine.h"

#include "AssetDocumentSyncState.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
FAssetDocumentRegionSyncState MakeLastState(const FString& SidecarHash, const FString& AssetEvidenceHash)
{
	FAssetDocumentRegionSyncState State;
	State.SidecarHash = SidecarHash;
	State.AssetEvidenceHash = AssetEvidenceHash;
	return State;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarSyncNoChangeTest,
	"AssetDocument.SidecarSync.NoChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarSyncNoChangeTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentRegionSyncState LastState = MakeLastState(TEXT("sha1:sidecar"), TEXT("sha1:asset"));
	const FAssetDocumentRegionSyncDecision Decision = FAssetDocumentSidecarSyncEngine::DecideRegion(
		TEXT("Body.Blend"),
		TEXT("sha1:sidecar"),
		TEXT("sha1:asset"),
		&LastState);

	TestEqual(TEXT("Unchanged hashes produce NoChange"), Decision.Direction, EAssetDocumentSyncDirection::NoChange);
	TestEqual(TEXT("Decision preserves region id"), Decision.RegionId, FString(TEXT("Body.Blend")));
	TestEqual(TEXT("Decision preserves current sidecar hash"), Decision.CurrentSidecarHash, FString(TEXT("sha1:sidecar")));
	TestEqual(TEXT("Decision preserves current asset evidence hash"), Decision.CurrentAssetEvidenceHash, FString(TEXT("sha1:asset")));
	TestEqual(TEXT("Decision preserves last sidecar hash"), Decision.LastSidecarHash, FString(TEXT("sha1:sidecar")));
	TestEqual(TEXT("Decision preserves last asset evidence hash"), Decision.LastAssetEvidenceHash, FString(TEXT("sha1:asset")));

	const FAssetDocumentRegionSyncState UnsetLastSidecarState = MakeLastState(FString(), TEXT("sha1:asset"));
	const FAssetDocumentRegionSyncDecision UnsetRegionDecision = FAssetDocumentSidecarSyncEngine::DecideRegion(
		TEXT("Body.Notifies"),
		FString(),
		TEXT("sha1:asset"),
		&UnsetLastSidecarState);
	TestEqual(TEXT("Existing baseline allows an empty sidecar hash to remain unchanged"), UnsetRegionDecision.Direction, EAssetDocumentSyncDirection::NoChange);
	TestTrue(TEXT("Decision preserves empty last sidecar hash"), UnsetRegionDecision.LastSidecarHash.IsEmpty());
	TestEqual(TEXT("Decision preserves last asset hash when sidecar is unset"), UnsetRegionDecision.LastAssetEvidenceHash, FString(TEXT("sha1:asset")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarSyncSidecarOnlyChangeTest,
	"AssetDocument.SidecarSync.SidecarOnlyChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarSyncSidecarOnlyChangeTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentRegionSyncState LastState = MakeLastState(TEXT("sha1:sidecar-old"), TEXT("sha1:asset"));
	const FAssetDocumentRegionSyncDecision Decision = FAssetDocumentSidecarSyncEngine::DecideRegion(
		TEXT("Body.Blend"),
		TEXT("sha1:sidecar-new"),
		TEXT("sha1:asset"),
		&LastState);

	TestEqual(TEXT("Sidecar-only change applies sidecar to asset"), Decision.Direction, EAssetDocumentSyncDirection::ApplySidecarToAsset);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarSyncAssetOnlyChangeTest,
	"AssetDocument.SidecarSync.AssetOnlyChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarSyncAssetOnlyChangeTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentRegionSyncState LastState = MakeLastState(TEXT("sha1:sidecar"), TEXT("sha1:asset-old"));
	const FAssetDocumentRegionSyncDecision Decision = FAssetDocumentSidecarSyncEngine::DecideRegion(
		TEXT("Body.Blend"),
		TEXT("sha1:sidecar"),
		TEXT("sha1:asset-new"),
		&LastState);

	TestEqual(TEXT("Asset-only change regenerates sidecar region"), Decision.Direction, EAssetDocumentSyncDirection::RegenerateSidecarRegion);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarSyncConflictTest,
	"AssetDocument.SidecarSync.Conflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarSyncConflictTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentRegionSyncState LastState = MakeLastState(TEXT("sha1:sidecar-old"), TEXT("sha1:asset-old"));
	const FAssetDocumentRegionSyncDecision Decision = FAssetDocumentSidecarSyncEngine::DecideRegion(
		TEXT("Body.Blend"),
		TEXT("sha1:sidecar-new"),
		TEXT("sha1:asset-new"),
		&LastState);

	TestEqual(TEXT("Both sides changed produces conflict"), Decision.Direction, EAssetDocumentSyncDirection::Conflict);

	const FAssetDocumentSyncResolutionAction UnresolvedAction = FAssetDocumentSidecarSyncEngine::MakeAction(Decision);
	TestFalse(TEXT("Conflict does not automatically apply sidecar"), UnresolvedAction.bShouldApplySidecarToAsset);
	TestFalse(TEXT("Conflict does not automatically regenerate sidecar"), UnresolvedAction.bShouldRegenerateSidecarRegion);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarSyncInitialBaselineTest,
	"AssetDocument.SidecarSync.InitialBaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarSyncInitialBaselineTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentRegionSyncDecision ExistingSidecarDecision = FAssetDocumentSidecarSyncEngine::DecideRegion(
		TEXT("Body.Blend"),
		TEXT("sha1:sidecar"),
		TEXT("sha1:asset"),
		nullptr);
	TestEqual(TEXT("Existing sidecar and calculable evidence need initial baseline"), ExistingSidecarDecision.Direction, EAssetDocumentSyncDirection::NeedsInitialBaseline);

	const FAssetDocumentRegionSyncDecision MissingEvidenceDecision = FAssetDocumentSidecarSyncEngine::DecideRegion(
		TEXT("Body.Blend"),
		TEXT("sha1:sidecar"),
		FString(),
		nullptr);
	TestEqual(TEXT("Existing sidecar without calculable evidence does not need initial baseline"), MissingEvidenceDecision.Direction, EAssetDocumentSyncDirection::NoChange);

	const FAssetDocumentRegionSyncDecision EmptySidecarDecision = FAssetDocumentSidecarSyncEngine::DecideRegion(
		TEXT("Body.Notifies"),
		FString(),
		TEXT("sha1:asset"),
		nullptr);
	TestEqual(TEXT("Empty sidecar with asset evidence regenerates sidecar"), EmptySidecarDecision.Direction, EAssetDocumentSyncDirection::RegenerateSidecarRegion);

	FAssetDocumentSyncState SyncState;
	SyncState.Regions.Add(TEXT("Body.Blend"), MakeLastState(TEXT("sha1:blend-sidecar"), TEXT("sha1:blend-asset")));
	SyncState.Regions.Add(TEXT("Body.Notifies"), MakeLastState(TEXT("sha1:notifies-sidecar-old"), TEXT("sha1:notifies-asset")));

	TMap<FString, FAssetDocumentCurrentRegionHashes> CurrentHashes;
	CurrentHashes.Add(TEXT("Body.Blend"), FAssetDocumentCurrentRegionHashes{ TEXT("sha1:blend-sidecar"), TEXT("sha1:blend-asset") });
	CurrentHashes.Add(TEXT("Body.Notifies"), FAssetDocumentCurrentRegionHashes{ TEXT("sha1:notifies-sidecar-new"), TEXT("sha1:notifies-asset") });

	const TArray<FAssetDocumentRegionSyncDecision> Decisions = FAssetDocumentSidecarSyncEngine::DecideDocument(CurrentHashes, SyncState);
	TestEqual(TEXT("DecideDocument returns every current region"), Decisions.Num(), 2);
	const FAssetDocumentRegionSyncDecision* BlendDecision = Decisions.FindByPredicate(
		[](const FAssetDocumentRegionSyncDecision& Candidate)
		{
			return Candidate.RegionId == TEXT("Body.Blend");
		});
	const FAssetDocumentRegionSyncDecision* NotifiesDecision = Decisions.FindByPredicate(
		[](const FAssetDocumentRegionSyncDecision& Candidate)
		{
			return Candidate.RegionId == TEXT("Body.Notifies");
		});
	TestNotNull(TEXT("DecideDocument preserves Body.Blend region id"), BlendDecision);
	TestNotNull(TEXT("DecideDocument preserves Body.Notifies region id"), NotifiesDecision);
	if (BlendDecision)
	{
		TestEqual(TEXT("DecideDocument keeps unchanged region"), BlendDecision->Direction, EAssetDocumentSyncDirection::NoChange);
	}
	if (NotifiesDecision)
	{
		TestEqual(TEXT("DecideDocument keeps sidecar-only decision"), NotifiesDecision->Direction, EAssetDocumentSyncDirection::ApplySidecarToAsset);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarSyncAcceptSidecarTest,
	"AssetDocument.SidecarSync.AcceptSidecar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarSyncAcceptSidecarTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionSyncDecision Decision;
	Decision.RegionId = TEXT("Body.Blend");
	Decision.Direction = EAssetDocumentSyncDirection::Conflict;

	const FAssetDocumentSyncResolutionAction Action = FAssetDocumentSidecarSyncEngine::ApplyResolution(
		Decision,
		EAssetDocumentConflictResolution::AcceptSidecar);

	TestEqual(TEXT("AcceptSidecar preserves region id"), Action.RegionId, FString(TEXT("Body.Blend")));
	TestEqual(TEXT("AcceptSidecar converts direction to apply sidecar"), Action.Direction, EAssetDocumentSyncDirection::ApplySidecarToAsset);
	TestTrue(TEXT("AcceptSidecar applies sidecar to asset"), Action.bShouldApplySidecarToAsset);
	TestFalse(TEXT("AcceptSidecar does not regenerate sidecar"), Action.bShouldRegenerateSidecarRegion);

	FAssetDocumentRegionSyncDecision NoChangeDecision;
	NoChangeDecision.RegionId = TEXT("Body.Notifies");
	NoChangeDecision.Direction = EAssetDocumentSyncDirection::NoChange;
	const FAssetDocumentSyncResolutionAction NoChangeAction = FAssetDocumentSidecarSyncEngine::ApplyResolution(
		NoChangeDecision,
		EAssetDocumentConflictResolution::AcceptSidecar);
	TestEqual(TEXT("Resolution does not rewrite non-conflict direction"), NoChangeAction.Direction, EAssetDocumentSyncDirection::NoChange);
	TestFalse(TEXT("Resolution does not apply non-conflict sidecar"), NoChangeAction.bShouldApplySidecarToAsset);
	TestFalse(TEXT("Resolution does not regenerate non-conflict sidecar"), NoChangeAction.bShouldRegenerateSidecarRegion);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarSyncAcceptAssetTest,
	"AssetDocument.SidecarSync.AcceptAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarSyncAcceptAssetTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionSyncDecision Decision;
	Decision.RegionId = TEXT("Body.Blend");
	Decision.Direction = EAssetDocumentSyncDirection::Conflict;

	const FAssetDocumentSyncResolutionAction Action = FAssetDocumentSidecarSyncEngine::ApplyResolution(
		Decision,
		EAssetDocumentConflictResolution::AcceptAsset);

	TestEqual(TEXT("AcceptAsset preserves region id"), Action.RegionId, FString(TEXT("Body.Blend")));
	TestEqual(TEXT("AcceptAsset converts direction to regenerate sidecar"), Action.Direction, EAssetDocumentSyncDirection::RegenerateSidecarRegion);
	TestFalse(TEXT("AcceptAsset does not apply sidecar"), Action.bShouldApplySidecarToAsset);
	TestTrue(TEXT("AcceptAsset regenerates sidecar region"), Action.bShouldRegenerateSidecarRegion);

	return true;
}

#endif
