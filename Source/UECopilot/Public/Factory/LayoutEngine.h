// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JSON/BlueprintJSONSchema.h"
#include "LayoutEngine.generated.h"

class UK2Node;
class UEdGraph;

/**
 * Layout settings
 */
USTRUCT(BlueprintType)
struct UECOPILOT_API FLayoutSettings
{
	GENERATED_BODY()

	/** Horizontal spacing between nodes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float HorizontalSpacing = 300.0f;

	/** Vertical spacing between nodes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float VerticalSpacing = 150.0f;

	/** Starting X position */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float StartX = 0.0f;

	/** Starting Y position */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float StartY = 0.0f;

	/** Maximum nodes per column (0 = unlimited) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	int32 MaxNodesPerColumn = 0;

	/** Prioritize execution flow (white lines) over data flow */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	bool bPrioritizeExecFlow = true;
};

/**
 * Node layer info for Sugiyama layout
 */
struct FNodeLayerInfo
{
	UK2Node* Node = nullptr;
	int32 Layer = 0;
	int32 Position = 0;
	TArray<UK2Node*> Predecessors;
	TArray<UK2Node*> Successors;
};

/**
 * Layout Engine - Automatic layout for blueprint nodes
 * Uses a modified Sugiyama algorithm for hierarchical layout
 */
UCLASS()
class UECOPILOT_API ULayoutEngine : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Auto-layout all nodes in a graph
	 * @param Graph - The graph to layout
	 * @param Settings - Layout settings
	 */
	UFUNCTION(BlueprintCallable, Category = "Layout")
	static void AutoLayoutGraph(UEdGraph* Graph, const FLayoutSettings& Settings = FLayoutSettings());

	/**
	 * Auto-layout specified nodes
	 * @param Nodes - Nodes to layout
	 * @param Settings - Layout settings
	 */
	static void AutoLayoutNodes(TArray<UK2Node*>& Nodes, const FLayoutSettings& Settings = FLayoutSettings());

	/**
	 * Calculate positions for nodes without applying
	 * @param NodeData - Array of node data with connections
	 * @param Settings - Layout settings
	 * @return Map of node IDs to positions
	 */
	UFUNCTION(BlueprintCallable, Category = "Layout")
	static TMap<FString, FNodePosition> CalculateLayout(
		const TArray<FBlueprintNodeData>& NodeData,
		const FLayoutSettings& Settings = FLayoutSettings());

private:
	/**
	 * Build adjacency information from nodes
	 */
	static void BuildAdjacencyInfo(
		TArray<UK2Node*>& Nodes,
		TMap<UK2Node*, FNodeLayerInfo>& OutNodeInfo,
		bool bPrioritizeExec);

	/**
	 * Assign layers using topological sort (longest path)
	 */
	static void AssignLayers(TMap<UK2Node*, FNodeLayerInfo>& NodeInfo);

	/**
	 * Order nodes within each layer to minimize crossings
	 */
	static void MinimizeCrossings(
		TMap<UK2Node*, FNodeLayerInfo>& NodeInfo,
		int32 NumLayers);

	/**
	 * Calculate final positions
	 */
	static void CalculatePositions(
		TMap<UK2Node*, FNodeLayerInfo>& NodeInfo,
		int32 NumLayers,
		const FLayoutSettings& Settings);

	/**
	 * Get connected nodes through exec pins (flow)
	 */
	static TArray<UK2Node*> GetExecSuccessors(UK2Node* Node);
	static TArray<UK2Node*> GetExecPredecessors(UK2Node* Node);

	/**
	 * Get connected nodes through data pins
	 */
	static TArray<UK2Node*> GetDataSuccessors(UK2Node* Node);
	static TArray<UK2Node*> GetDataPredecessors(UK2Node* Node);

	/**
	 * Count edge crossings between two layers
	 */
	static int32 CountCrossings(
		const TArray<FNodeLayerInfo*>& Layer1,
		const TArray<FNodeLayerInfo*>& Layer2);

	/**
	 * Calculate barycenter for node positioning
	 */
	static float CalculateBarycenter(
		FNodeLayerInfo* NodeInfo,
		const TArray<FNodeLayerInfo*>& AdjacentLayer,
		bool bUsePredecessors);
};
