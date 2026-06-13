#pragma once

#include "CoreMinimal.h"

class FJsonObject;

struct FAssetDocumentProjectionDiagnostic
{
	FString Path;
	FString Code;
	FString Message;
};

struct FAssetDocumentProjectionMetrics
{
	int32 AssetSpecificFields = 0;
	int32 ReusableProjectedFields = 0;
	int32 SkippedFields = 0;

	TSharedRef<FJsonObject> ToJson() const;
};

struct FAssetDocumentProjectionResult
{
	bool bSuccess = true;
	FString Message;
	TArray<FAssetDocumentProjectionDiagnostic> Diagnostics;
	FAssetDocumentProjectionMetrics Metrics;

	static FAssetDocumentProjectionResult Success();
	static FAssetDocumentProjectionResult Failure(const FString& Message, const FString& Path, const FString& Code);
};
