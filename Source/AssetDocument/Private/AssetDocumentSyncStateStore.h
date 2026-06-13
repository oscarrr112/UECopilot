// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentSyncState.h"

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FAssetDocumentSyncStateStore
{
public:
	static bool LoadFromDocumentJson(const TSharedRef<FJsonObject>& DocumentJson, FAssetDocumentSyncState& OutState, FString& OutError);
	static void WriteToDocumentJson(const TSharedRef<FJsonObject>& DocumentJson, const FAssetDocumentSyncState& State);
	static void UpdateRegionState(FAssetDocumentSyncState& State, FName RegionId, const FAssetDocumentRegionSyncState& RegionState);
};
