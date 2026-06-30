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
	if (CurrentSidecarHash.IsEmpty() || CurrentAssetEvidenceHash.IsEmpty())
	{
		return EAssetDocumentSyncDirection::NoChange;
	}

	return EAssetDocumentSyncDirection::NeedsInitialBaseline;
}

void AddSortedRegionIds(const TSet<FString>& RegionIdSet, TArray<FString>& OutRegionIds)
{
	OutRegionIds.Reset();
	OutRegionIds.Reserve(RegionIdSet.Num());
	for (const FString& RegionId : RegionIdSet)
	{
		OutRegionIds.Add(RegionId);
	}

	OutRegionIds.Sort();
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
	TSet<FString> RegionIdSet;
	RegionIdSet.Reserve(CurrentRegionHashes.Num() + LastSyncState.Regions.Num());
	for (const TPair<FString, FAssetDocumentCurrentRegionHashes>& CurrentRegion : CurrentRegionHashes)
	{
		RegionIdSet.Add(CurrentRegion.Key);
	}
	for (const TPair<FString, FAssetDocumentRegionSyncState>& LastRegion : LastSyncState.Regions)
	{
		RegionIdSet.Add(LastRegion.Key);
	}

	TArray<FString> RegionIds;
	AddSortedRegionIds(RegionIdSet, RegionIds);

	TArray<FAssetDocumentRegionSyncDecision> Decisions;
	Decisions.Reserve(RegionIds.Num());

	for (const FString& RegionId : RegionIds)
	{
		const FAssetDocumentCurrentRegionHashes* CurrentRegion = CurrentRegionHashes.Find(RegionId);
		const FAssetDocumentRegionSyncState* LastRegionState = LastSyncState.Regions.Find(RegionId);
		Decisions.Add(DecideRegion(
			RegionId,
			CurrentRegion ? CurrentRegion->SidecarHash : FString(),
			CurrentRegion ? CurrentRegion->AssetEvidenceHash : FString(),
			LastRegionState));
	}

	return Decisions;
}

TArray<FAssetDocumentRegionSyncDecision> FAssetDocumentSidecarSyncEngine::DecideDocument(
	const TMap<FString, FString>& CurrentSidecarHashes,
	const TMap<FString, FString>& CurrentAssetEvidenceHashes,
	const FAssetDocumentSyncState& LastSyncState)
{
	TSet<FString> RegionIdSet;
	RegionIdSet.Reserve(CurrentSidecarHashes.Num() + CurrentAssetEvidenceHashes.Num() + LastSyncState.Regions.Num());
	for (const TPair<FString, FString>& CurrentSidecar : CurrentSidecarHashes)
	{
		RegionIdSet.Add(CurrentSidecar.Key);
	}
	for (const TPair<FString, FString>& CurrentAssetEvidence : CurrentAssetEvidenceHashes)
	{
		RegionIdSet.Add(CurrentAssetEvidence.Key);
	}
	for (const TPair<FString, FAssetDocumentRegionSyncState>& LastRegion : LastSyncState.Regions)
	{
		RegionIdSet.Add(LastRegion.Key);
	}

	TArray<FString> RegionIds;
	AddSortedRegionIds(RegionIdSet, RegionIds);

	TArray<FAssetDocumentRegionSyncDecision> Decisions;
	Decisions.Reserve(RegionIds.Num());

	for (const FString& RegionId : RegionIds)
	{
		const FString* CurrentSidecarHash = CurrentSidecarHashes.Find(RegionId);
		const FString* CurrentAssetEvidenceHash = CurrentAssetEvidenceHashes.Find(RegionId);
		const FAssetDocumentRegionSyncState* LastRegionState = LastSyncState.Regions.Find(RegionId);
		Decisions.Add(DecideRegion(
			RegionId,
			CurrentSidecarHash ? *CurrentSidecarHash : FString(),
			CurrentAssetEvidenceHash ? *CurrentAssetEvidenceHash : FString(),
			LastRegionState));
	}

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
