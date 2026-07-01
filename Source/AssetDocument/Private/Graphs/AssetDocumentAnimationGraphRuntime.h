// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"
#include "Graphs/AssetDocumentGraphTypes.h"

#include "CoreMinimal.h"

class UEdGraph;
class UObject;

struct FAssetDocumentAnimationGraphContext
{
	UObject* Asset = nullptr;
	UEdGraph* Graph = nullptr;
	FString GraphPath;
	FString GraphKind;
};

struct FAssetDocumentAnimationGraphNodeSpawnCandidate
{
	FString ClassPath;
	TSharedPtr<FJsonObject> Spawner;
	FString ActionKey;
	FString MenuName;
	bool bSpawnable = true;
};

class IAssetDocumentAnimationGraphCandidateProvider
{
public:
	virtual ~IAssetDocumentAnimationGraphCandidateProvider() = default;

	virtual TArray<FAssetDocumentAnimationGraphNodeSpawnCandidate> FindCandidates(
		const FAssetDocumentNodeSpec& NodeSpec,
		const FAssetDocumentAnimationGraphContext& Context) const = 0;
};

class IAssetDocumentAnimationGraphStructuralHook
{
public:
	virtual ~IAssetDocumentAnimationGraphStructuralHook() = default;

	virtual FAssetDocumentCapabilityResult LocateOrCreateGraph(
		const FAssetDocumentGraphSpec& GraphSpec,
		FAssetDocumentAnimationGraphContext& InOutContext) = 0;

	virtual FAssetDocumentCapabilityResult RepairAfterApply(
		const FAssetDocumentGraphSpec& GraphSpec,
		const FAssetDocumentAnimationGraphContext& Context) = 0;
};

class FAssetDocumentAnimationGraphRuntime
{
public:
	explicit FAssetDocumentAnimationGraphRuntime(
		TSharedPtr<IAssetDocumentAnimationGraphCandidateProvider> InCandidateProvider = nullptr);

	FAssetDocumentCapabilityResult ValidateGraph(
		const FAssetDocumentGraphSpec& GraphSpec,
		const FAssetDocumentAnimationGraphContext& Context) const;

	FAssetDocumentCapabilityResult ApplyGraph(
		const FAssetDocumentGraphSpec& GraphSpec,
		FAssetDocumentAnimationGraphContext& Context,
		IAssetDocumentAnimationGraphStructuralHook& Hook) const;

	FAssetDocumentCapabilityResult ExtractGraph(
		const FAssetDocumentAnimationGraphContext& Context,
		FAssetDocumentGraphSpec& OutGraph) const;

private:
	FAssetDocumentCapabilityResult ResolveCandidate(
		const FAssetDocumentGraphSpec& GraphSpec,
		const FAssetDocumentNodeSpec& NodeSpec,
		const FAssetDocumentAnimationGraphContext& Context,
		FAssetDocumentAnimationGraphNodeSpawnCandidate& OutCandidate) const;

	TSharedPtr<IAssetDocumentAnimationGraphCandidateProvider> CandidateProvider;
};
