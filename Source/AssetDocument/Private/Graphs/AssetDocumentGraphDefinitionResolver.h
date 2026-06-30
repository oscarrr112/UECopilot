// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "Graphs/AssetDocumentGraphTypes.h"

struct FAssetDocumentGraphDefinitionResolveOptions
{
	FString DefinitionsPath = TEXT("/Definitions");
	FString GraphsPath = TEXT("/Body/UbergraphPages");
};

struct FAssetDocumentGraphDefinitionResolveResult
{
	TArray<FAssetDocumentGraphSpec> Graphs;
	TArray<FAssetDocumentGraphDiagnostic> Diagnostics;

	bool IsValid() const { return Diagnostics.IsEmpty(); }
	void AddDiagnostic(const FString& Code, const FString& Path, const FString& Message);
};

class FAssetDocumentGraphDefinitionResolver
{
public:
	static FAssetDocumentGraphDefinitionResolveResult ResolveGraphArray(
		const TArray<FAssetDocumentGraphSpec>& Graphs,
		const TSharedPtr<FJsonObject>& Definitions,
		const FAssetDocumentGraphDefinitionResolveOptions& Options = FAssetDocumentGraphDefinitionResolveOptions());
};
