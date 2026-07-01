// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "Graphs/AssetDocumentAnimationGraphRuntime.h"

class UBlueprintNodeSpawner;

struct FAssetDocumentAnimationGraphNodeActionCandidate
{
	FAssetDocumentAnimationGraphNodeSpawnCandidate Candidate;
	const UBlueprintNodeSpawner* Spawner = nullptr;
	bool bFallback = false;
};

class FAssetDocumentAnimationGraphNodeActionProvider final
	: public IAssetDocumentAnimationGraphCandidateProvider
{
public:
	explicit FAssetDocumentAnimationGraphNodeActionProvider(
		bool bInContextSensitive = true,
		uint32 InContextTargetMask = DefaultContextTargetMask(),
		bool bInAllowClassFallback = true);

	static uint32 DefaultContextTargetMask();

	TArray<FAssetDocumentAnimationGraphNodeActionCandidate> FindActionCandidates(
		const FAssetDocumentNodeSpec& NodeSpec,
		const FAssetDocumentAnimationGraphContext& Context) const;

	virtual TArray<FAssetDocumentAnimationGraphNodeSpawnCandidate> FindCandidates(
		const FAssetDocumentNodeSpec& NodeSpec,
		const FAssetDocumentAnimationGraphContext& Context) const override;

	virtual FAssetDocumentCapabilityResult SpawnNode(
		const FAssetDocumentGraphSpec& GraphSpec,
		const FAssetDocumentNodeSpec& NodeSpec,
		const FAssetDocumentAnimationGraphContext& Context,
		const FAssetDocumentAnimationGraphNodeSpawnCandidate& Candidate,
		UEdGraphNode*& OutNode) const override;

private:
	bool bContextSensitive = true;
	uint32 ContextTargetMask = 0;
	bool bAllowClassFallback = true;
};
