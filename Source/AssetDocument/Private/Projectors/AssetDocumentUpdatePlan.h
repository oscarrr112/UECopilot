#pragma once

#include "CoreMinimal.h"

class FJsonObject;

struct FAssetDocumentUpdateOperation
{
	FString Path;
	FString Kind;
};

struct FAssetDocumentUpdateMetrics
{
	int32 ReusableOperations = 0;
	int32 AssetSpecificOperations = 0;
	int32 ValidationFailures = 0;

	TSharedRef<FJsonObject> ToJson() const;
};

struct FAssetDocumentUpdateResult
{
	bool bSuccess = true;
	FString Message;
	TArray<FString> ChangedPaths;
	FAssetDocumentUpdateMetrics Metrics;

	static FAssetDocumentUpdateResult Success();
	static FAssetDocumentUpdateResult Failure(const FString& Message);
};
