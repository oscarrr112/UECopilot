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
 * Node placement record for overlap detection
 */
struct FNodePlacement
{
	UK2Node* Node = nullptr;
	float X = 0;
	float Y = 0;
	float Width = 200.0f;   // Approximate node width
	float Height = 100.0f;  // Approximate node height

	FNodePlacement() = default;
	FNodePlacement(UK2Node* InNode, float InX, float InY, float InWidth = 200.0f, float InHeight = 100.0f)
		: Node(InNode), X(InX), Y(InY), Width(InWidth), Height(InHeight) {}

	bool Overlaps(const FNodePlacement& Other, float Padding = 20.0f) const
	{
		return !(X + Width + Padding <= Other.X ||
				 Other.X + Other.Width + Padding <= X ||
				 Y + Height + Padding <= Other.Y ||
				 Other.Y + Other.Height + Padding <= Y);
	}
};

/**
 * Placement grid for tracking node positions and resolving overlaps
 * Uses a "push" algorithm - when a new node overlaps existing nodes,
 * the existing nodes are pushed away to make room.
 */
class UECOPILOT_API FNodePlacementGrid
{
public:
	/** Add a node placement to the grid, pushing other nodes if needed */
	void AddPlacement(const FNodePlacement& Placement, float Padding = 20.0f);

	/** Check if a position would overlap with existing placements */
	bool CheckOverlap(const FNodePlacement& Placement, float Padding = 20.0f) const;

	/** Get all overlapping placements with the given placement */
	TArray<FNodePlacement*> GetOverlappingPlacements(const FNodePlacement& Placement, float Padding = 20.0f);

	/**
	 * Place a node at desired position, pushing overlapping nodes away
	 * @param Placement - The new node placement
	 * @param Padding - Minimum spacing between nodes
	 * @param bPushDown - If true, push overlapping nodes down; if false, push up
	 */
	void PlaceAndPush(const FNodePlacement& Placement, float Padding = 20.0f, bool bPushDown = true);

	/** Clear all placements */
	void Clear();

	/** Get all placements */
	const TArray<FNodePlacement>& GetPlacements() const { return Placements; }

	/** Update a placement's position (used after pushing) */
	void UpdatePlacement(UK2Node* Node, float NewX, float NewY);

	/** Apply all placement positions to their nodes */
	void ApplyToNodes();

private:
	/** Recursively push overlapping nodes */
	void PushOverlappingNodes(int32 PlacementIndex, float PushAmount, float Padding, TSet<int32>& AlreadyPushed);

	TArray<FNodePlacement> Placements;
};

/**
 * Subgraph info for grouped layout
 */
struct FSubgraphInfo
{
	UK2Node* RootNode = nullptr;
	TArray<UK2Node*> Nodes;
	float MinX = 0, MaxX = 0, MinY = 0, MaxY = 0;
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
	 * Groups by event/function entry and layouts each group separately
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

	/**
	 * Find all subgraphs starting from event/entry nodes
	 */
	static TArray<FSubgraphInfo> FindSubgraphs(TArray<UK2Node*>& Nodes);

	/**
	 * Collect all nodes reachable from a root node
	 */
	static void CollectConnectedNodes(UK2Node* Root, TSet<UK2Node*>& OutNodes, TSet<UK2Node*>& Visited);

	/**
	 * Check if node is an event or function entry
	 */
	static bool IsRootNode(UK2Node* Node);

	/**
	 * Check if node is a pure data node (Variable Get, literals, etc.)
	 */
	static bool IsPureDataNode(UK2Node* Node);

	/**
	 * Adjust positions of pure data nodes to be near their consumers
	 * @deprecated Use PositionDataNodesNearConsumers instead
	 */
	static void AdjustDataNodePositions(TArray<UK2Node*>& Nodes, const FLayoutSettings& Settings);

	/**
	 * Position pure data nodes near their consuming exec nodes
	 * This separates data nodes from the Sugiyama algorithm and places them
	 * to the left of their consumers with proper vertical stacking
	 */
	static void PositionDataNodesNearConsumers(
		TArray<UK2Node*>& DataNodes,
		TArray<UK2Node*>& ExecNodes,
		const FLayoutSettings& Settings);

	/**
	 * Calculate bounding box of nodes
	 */
	static void CalculateBoundingBox(const TArray<UK2Node*>& Nodes, float& OutMinX, float& OutMaxX, float& OutMinY, float& OutMaxY);

	/**
	 * Enforce exec pin order constraint
	 * For nodes with multiple output exec pins (Sequence, Branch, ForLoop, etc.),
	 * ensures that successor nodes' Y positions match pin order (top pin -> lower Y)
	 */
	static void EnforceExecPinOrder(TArray<UK2Node*>& Nodes, const FLayoutSettings& Settings);

	/**
	 * Get ordered exec successors for a node
	 * Returns successor nodes in the order of their connected output exec pins
	 * @param Node - The source node
	 * @return Array of (PinIndex, SuccessorNode) pairs, sorted by pin index
	 */
	static TArray<TPair<int32, UK2Node*>> GetOrderedExecSuccessors(UK2Node* Node);
};
