// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentSyncState.h"

#include "CoreMinimal.h"

enum class EAssetDocumentSyncDirection : uint8
{
	NoChange,
	ApplySidecarToAsset,
	RegenerateSidecarRegion,
	Conflict,
	NeedsInitialBaseline
};

enum class EAssetDocumentConflictResolution : uint8
{
	AcceptSidecar,
	AcceptAsset
};

struct FAssetDocumentCurrentRegionHashes
{
	FString SidecarHash;
	FString AssetEvidenceHash;
};

struct FAssetDocumentRegionSyncDecision
{
	FString RegionId;
	EAssetDocumentSyncDirection Direction = EAssetDocumentSyncDirection::NoChange;
	FString CurrentSidecarHash;
	FString CurrentAssetEvidenceHash;
	FString LastSidecarHash;
	FString LastAssetEvidenceHash;
};

struct FAssetDocumentSyncResolutionAction
{
	FString RegionId;
	EAssetDocumentSyncDirection Direction = EAssetDocumentSyncDirection::NoChange;
	bool bShouldApplySidecarToAsset = false;
	bool bShouldRegenerateSidecarRegion = false;
};

class FAssetDocumentSidecarSyncEngine
{
public:
	static FAssetDocumentRegionSyncDecision DecideRegion(
		const FString& RegionId,
		const FString& CurrentSidecarHash,
		const FString& CurrentAssetEvidenceHash,
		const FAssetDocumentRegionSyncState* LastSyncState);

	static TArray<FAssetDocumentRegionSyncDecision> DecideDocument(
		const TMap<FString, FAssetDocumentCurrentRegionHashes>& CurrentRegionHashes,
		const FAssetDocumentSyncState& LastSyncState);

	static TArray<FAssetDocumentRegionSyncDecision> DecideDocument(
		const TMap<FString, FString>& CurrentSidecarHashes,
		const TMap<FString, FString>& CurrentAssetEvidenceHashes,
		const FAssetDocumentSyncState& LastSyncState);

	static FAssetDocumentSyncResolutionAction MakeAction(const FAssetDocumentRegionSyncDecision& Decision);

	static FAssetDocumentSyncResolutionAction ApplyResolution(
		const FAssetDocumentRegionSyncDecision& Decision,
		EAssetDocumentConflictResolution Resolution);
};
