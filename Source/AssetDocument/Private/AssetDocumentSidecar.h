// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FAssetDocumentSidecar
{
public:
	static FString ResolveObjectPathFromSidecar(const FString& FilePath);
	static FString ResolveSidecarPathFromObjectPath(const FString& ObjectPath);
	static bool LoadJsonFile(const FString& FilePath, TSharedPtr<FJsonObject>& OutJson, FString& OutError);
	static bool WriteJsonFile(const FString& FilePath, const TSharedPtr<FJsonObject>& Json, FString& OutError);
	static bool ValidateTargetMatchesSidecar(const FString& FilePath, const TSharedPtr<FJsonObject>& Json, FString& OutError);
};
