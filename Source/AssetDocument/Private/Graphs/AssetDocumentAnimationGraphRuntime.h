// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"
#include "Graphs/AssetDocumentGraphTypes.h"

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UObject;
class UBlueprint;

struct FAssetDocumentAnimationGraphContext
{
	UObject* Asset = nullptr;
	UBlueprint* Blueprint = nullptr;
	UEdGraph* Graph = nullptr;
	TArray<UEdGraphPin*> Pins;
	FString GraphPath;
	FString GraphKind;
};

struct FAssetDocumentAnimationGraphNodeSpawnCandidate
{
	FString ClassPath;
	TSharedPtr<FJsonObject> Spawner;
	FString ActionKey;
	FString MenuName;
	FString Category;
	FString SpawnerSignature;
	bool bSpawnable = true;
};

class IAssetDocumentAnimationGraphCandidateProvider
{
public:
	virtual ~IAssetDocumentAnimationGraphCandidateProvider() = default;

	virtual TArray<FAssetDocumentAnimationGraphNodeSpawnCandidate> FindCandidates(
		const FAssetDocumentNodeSpec& NodeSpec,
		const FAssetDocumentAnimationGraphContext& Context) const = 0;

	virtual FAssetDocumentCapabilityResult SpawnNode(
		const FAssetDocumentGraphSpec& GraphSpec,
		const FAssetDocumentNodeSpec& NodeSpec,
		const FAssetDocumentAnimationGraphContext& Context,
		const FAssetDocumentAnimationGraphNodeSpawnCandidate& Candidate,
		UEdGraphNode*& OutNode) const = 0;
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

	static FGuid MakeManagedNodeGuid(
		const FAssetDocumentGraphSpec& GraphSpec,
		const FAssetDocumentNodeSpec& NodeSpec);

	static FString MakeManagedNodeObjectName(const FString& NodeId);
	static bool TryParseManagedNodeObjectName(const FName& ObjectName, FString& OutNodeId);

private:
	FAssetDocumentCapabilityResult ResolveCandidate(
		const FAssetDocumentGraphSpec& GraphSpec,
		const FAssetDocumentNodeSpec& NodeSpec,
		const FAssetDocumentAnimationGraphContext& Context,
		FAssetDocumentAnimationGraphNodeSpawnCandidate& OutCandidate) const;

	FAssetDocumentCapabilityResult MaterializeGraphNodes(
		const FAssetDocumentGraphSpec& GraphSpec,
		const FAssetDocumentAnimationGraphContext& Context) const;

	FAssetDocumentCapabilityResult ApplyGraphAfterPreflight(
		const FAssetDocumentGraphSpec& GraphSpec,
		FAssetDocumentAnimationGraphContext& Context,
		IAssetDocumentAnimationGraphStructuralHook& Hook) const;

	TSharedPtr<IAssetDocumentAnimationGraphCandidateProvider> CandidateProvider;
};
