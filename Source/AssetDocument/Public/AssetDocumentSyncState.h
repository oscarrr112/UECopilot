// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FAssetDocumentRegionSyncState
{
	int32 PolicyVersion = 1;
	FString SidecarHash;
	FString AssetEvidenceHash;
	FString LastSyncedAtUtc;
};

struct FAssetDocumentSyncState
{
	int32 SchemaVersion = 1;
	FString AssetObjectPath;
	FString AssetPackageGuid;
	FString UpdatedAtUtc;
	TMap<FName, FAssetDocumentRegionSyncState> Regions;
};
