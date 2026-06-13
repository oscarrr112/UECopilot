// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentSidecarSyncEngine.h"

namespace
{
bool HasLastSyncState(const FAssetDocumentRegionSyncState* LastSyncState)
{
	return LastSyncState != nullptr;
}

EAssetDocumentSyncDirection DecideInitialRegion(
	const FString& CurrentSidecarHash,
	const FString& CurrentAssetEvidenceHash)
{
	if (CurrentAssetEvidenceHash.IsEmpty())
	{
		return EAssetDocumentSyncDirection::NoChange;
	}

	if (CurrentSidecarHash.IsEmpty())
	{
		return EAssetDocumentSyncDirection::RegenerateSidecarRegion;
	}

	return EAssetDocumentSyncDirection::NeedsInitialBaseline;
}
}

FAssetDocumentRegionSyncDecision FAssetDocumentSidecarSyncEngine::DecideRegion(
	const FString& RegionId,
	const FString& CurrentSidecarHash,
	const FString& CurrentAssetEvidenceHash,
	const FAssetDocumentRegionSyncState* LastSyncState)
{
	FAssetDocumentRegionSyncDecision Decision;
	Decision.RegionId = RegionId;
	Decision.CurrentSidecarHash = CurrentSidecarHash;
	Decision.CurrentAssetEvidenceHash = CurrentAssetEvidenceHash;
	if (LastSyncState)
	{
		Decision.LastSidecarHash = LastSyncState->SidecarHash;
		Decision.LastAssetEvidenceHash = LastSyncState->AssetEvidenceHash;
	}

	if (!HasLastSyncState(LastSyncState))
	{
		Decision.Direction = DecideInitialRegion(CurrentSidecarHash, CurrentAssetEvidenceHash);
		return Decision;
	}

	const bool bSidecarChanged = CurrentSidecarHash != LastSyncState->SidecarHash;
	const bool bAssetChanged = CurrentAssetEvidenceHash != LastSyncState->AssetEvidenceHash;

	if (bSidecarChanged && bAssetChanged)
	{
		Decision.Direction = EAssetDocumentSyncDirection::Conflict;
	}
	else if (bSidecarChanged)
	{
		Decision.Direction = EAssetDocumentSyncDirection::ApplySidecarToAsset;
	}
	else if (bAssetChanged)
	{
		Decision.Direction = EAssetDocumentSyncDirection::RegenerateSidecarRegion;
	}
	else
	{
		Decision.Direction = EAssetDocumentSyncDirection::NoChange;
	}

	return Decision;
}

TArray<FAssetDocumentRegionSyncDecision> FAssetDocumentSidecarSyncEngine::DecideDocument(
	const TMap<FString, FAssetDocumentCurrentRegionHashes>& CurrentRegionHashes,
	const FAssetDocumentSyncState& LastSyncState)
{
	TArray<FAssetDocumentRegionSyncDecision> Decisions;
	Decisions.Reserve(CurrentRegionHashes.Num());

	for (const TPair<FString, FAssetDocumentCurrentRegionHashes>& CurrentRegion : CurrentRegionHashes)
	{
		const FAssetDocumentRegionSyncState* LastRegionState = LastSyncState.Regions.Find(CurrentRegion.Key);
		Decisions.Add(DecideRegion(
			CurrentRegion.Key,
			CurrentRegion.Value.SidecarHash,
			CurrentRegion.Value.AssetEvidenceHash,
			LastRegionState));
	}

	Decisions.Sort(
		[](const FAssetDocumentRegionSyncDecision& Left, const FAssetDocumentRegionSyncDecision& Right)
		{
			return Left.RegionId < Right.RegionId;
		});
	return Decisions;
}

FAssetDocumentSyncResolutionAction FAssetDocumentSidecarSyncEngine::MakeAction(
	const FAssetDocumentRegionSyncDecision& Decision)
{
	FAssetDocumentSyncResolutionAction Action;
	Action.RegionId = Decision.RegionId;
	Action.Direction = Decision.Direction;
	Action.bShouldApplySidecarToAsset = Decision.Direction == EAssetDocumentSyncDirection::ApplySidecarToAsset;
	Action.bShouldRegenerateSidecarRegion = Decision.Direction == EAssetDocumentSyncDirection::RegenerateSidecarRegion;
	return Action;
}

FAssetDocumentSyncResolutionAction FAssetDocumentSidecarSyncEngine::ApplyResolution(
	const FAssetDocumentRegionSyncDecision& Decision,
	EAssetDocumentConflictResolution Resolution)
{
	if (Decision.Direction != EAssetDocumentSyncDirection::Conflict)
	{
		return MakeAction(Decision);
	}

	FAssetDocumentSyncResolutionAction Action;
	Action.RegionId = Decision.RegionId;

	switch (Resolution)
	{
	case EAssetDocumentConflictResolution::AcceptSidecar:
		Action.Direction = EAssetDocumentSyncDirection::ApplySidecarToAsset;
		Action.bShouldApplySidecarToAsset = true;
		break;
	case EAssetDocumentConflictResolution::AcceptAsset:
		Action.Direction = EAssetDocumentSyncDirection::RegenerateSidecarRegion;
		Action.bShouldRegenerateSidecarRegion = true;
		break;
	default:
		Action.Direction = Decision.Direction;
		break;
	}

	return Action;
}
