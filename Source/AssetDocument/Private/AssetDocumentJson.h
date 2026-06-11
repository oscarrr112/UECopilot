// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FAssetDocumentJson
{
public:
	static bool LoadJsonFile(const FString& FilePath, TSharedPtr<FJsonObject>& OutJson, FString& OutError);
	static bool WriteJsonFile(const FString& FilePath, const TSharedPtr<FJsonObject>& Json, FString& OutError);
};
