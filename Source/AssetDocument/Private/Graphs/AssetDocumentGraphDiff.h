// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "Graphs/AssetDocumentGraphTypes.h"

struct FAssetDocumentGraphDiffEntry
{
	FString Path;
	FString Status;
	FString Message;
	TSharedPtr<FJsonValue> Desired;
	TSharedPtr<FJsonValue> Current;

	TSharedRef<FJsonObject> ToJsonObject() const;
};

class FAssetDocumentGraphDiff
{
public:
	static TArray<FAssetDocumentGraphDiffEntry> CompareUbergraphPages(
		const TArray<FAssetDocumentGraphSpec>& DesiredGraphs,
		const TArray<FAssetDocumentGraphSpec>& CurrentGraphs,
		const TSharedPtr<FJsonObject>& Definitions);

	static FAssetDocumentGraphDiffEntry MakeUnsupported(const FString& Path, const FString& Message);
};
