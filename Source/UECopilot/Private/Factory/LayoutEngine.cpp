// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factory/LayoutEngine.h"
#include "K2Node.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_VariableGet.h"
#include "K2Node_CallFunction.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"

//////////////////////////////////////////////////////////////////////////
// FNodePlacementGrid Implementation

void FNodePlacementGrid::AddPlacement(const FNodePlacement& Placement, float Padding)
{
	// Simply add without pushing - use PlaceAndPush for push behavior
	Placements.Add(Placement);
}

bool FNodePlacementGrid::CheckOverlap(const FNodePlacement& Placement, float Padding) const
{
	for (const FNodePlacement& Existing : Placements)
	{
		if (Existing.Node != Placement.Node && Placement.Overlaps(Existing, Padding))
		{
			return true;
		}
	}
	return false;
}

TArray<FNodePlacement*> FNodePlacementGrid::GetOverlappingPlacements(const FNodePlacement& Placement, float Padding)
{
	TArray<FNodePlacement*> Result;
	for (FNodePlacement& Existing : Placements)
	{
		if (Existing.Node != Placement.Node && Placement.Overlaps(Existing, Padding))
		{
			Result.Add(&Existing);
		}
	}
	return Result;
}

void FNodePlacementGrid::PlaceAndPush(const FNodePlacement& Placement, float Padding, bool bPushDown)
{
	// First, find all nodes that would overlap with this new placement
	TArray<int32> OverlappingIndices;
	for (int32 i = 0; i < Placements.Num(); i++)
	{
		if (Placements[i].Node != Placement.Node && Placement.Overlaps(Placements[i], Padding))
		{
			OverlappingIndices.Add(i);
		}
	}

	// Add the new placement
	int32 NewIndex = Placements.Num();
	Placements.Add(Placement);

	// Push overlapping nodes
	if (OverlappingIndices.Num() > 0)
	{
		TSet<int32> AlreadyPushed;
		AlreadyPushed.Add(NewIndex); // Don't push the node we just added

		for (int32 OverlapIdx : OverlappingIndices)
		{
			if (AlreadyPushed.Contains(OverlapIdx))
			{
				continue;
			}

			// Calculate how much to push
			FNodePlacement& Overlapping = Placements[OverlapIdx];
			float PushAmount;

			if (bPushDown)
			{
				// Push down: move overlapping node below the new node
				float NewBottom = Placement.Y + Placement.Height + Padding;
				PushAmount = NewBottom - Overlapping.Y;
			}
			else
			{
				// Push up: move overlapping node above the new node
				float NewTop = Placement.Y - Padding - Overlapping.Height;
				PushAmount = NewTop - Overlapping.Y;
			}

			if (FMath::Abs(PushAmount) > 0.1f)
			{
				Overlapping.Y += PushAmount;
				AlreadyPushed.Add(OverlapIdx);

				// Recursively push any nodes that now overlap with this pushed node
				PushOverlappingNodes(OverlapIdx, bPushDown ? Padding : -Padding, Padding, AlreadyPushed);
			}
		}
	}
}

void FNodePlacementGrid::PushOverlappingNodes(int32 PlacementIndex, float PushDirection, float Padding, TSet<int32>& AlreadyPushed)
{
	FNodePlacement& Current = Placements[PlacementIndex];

	for (int32 i = 0; i < Placements.Num(); i++)
	{
		if (i == PlacementIndex || AlreadyPushed.Contains(i))
		{
			continue;
		}

		FNodePlacement& Other = Placements[i];
		if (Current.Overlaps(Other, Padding))
		{
			// Calculate push amount
			float PushAmount;
			if (PushDirection > 0)
			{
				// Pushing down
				float CurrentBottom = Current.Y + Current.Height + Padding;
				PushAmount = CurrentBottom - Other.Y;
			}
			else
			{
				// Pushing up
				float CurrentTop = Current.Y - Padding - Other.Height;
				PushAmount = CurrentTop - Other.Y;
			}

			if (FMath::Abs(PushAmount) > 0.1f)
			{
				Other.Y += PushAmount;
				AlreadyPushed.Add(i);

				// Recursively push
				PushOverlappingNodes(i, PushDirection, Padding, AlreadyPushed);
			}
		}
	}
}

void FNodePlacementGrid::UpdatePlacement(UK2Node* Node, float NewX, float NewY)
{
	for (FNodePlacement& Placement : Placements)
	{
		if (Placement.Node == Node)
		{
			Placement.X = NewX;
			Placement.Y = NewY;
			break;
		}
	}
}

void FNodePlacementGrid::ApplyToNodes()
{
	for (const FNodePlacement& Placement : Placements)
	{
		if (Placement.Node)
		{
			Placement.Node->NodePosX = static_cast<int32>(Placement.X);
			Placement.Node->NodePosY = static_cast<int32>(Placement.Y);
		}
	}
}

void FNodePlacementGrid::Clear()
{
	Placements.Empty();
}

//////////////////////////////////////////////////////////////////////////

void ULayoutEngine::AutoLayoutGraph(UEdGraph* Graph, const FLayoutSettings& Settings)
{
	if (!Graph)
	{
		return;
	}

	TArray<UK2Node*> K2Nodes;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node* K2Node = Cast<UK2Node>(Node))
		{
			K2Nodes.Add(K2Node);
		}
	}

	if (K2Nodes.Num() == 0)
	{
		return;
	}

	// Find subgraphs (each event/entry and its connected nodes)
	TArray<FSubgraphInfo> Subgraphs = FindSubgraphs(K2Nodes);

	if (Subgraphs.Num() == 0)
	{
		// Fallback to old behavior
		AutoLayoutNodes(K2Nodes, Settings);
		return;
	}

	// Layout each subgraph separately
	float CurrentY = Settings.StartY;
	const float SubgraphSpacing = 200.0f; // Spacing between event groups

	for (FSubgraphInfo& Subgraph : Subgraphs)
	{
		if (Subgraph.Nodes.Num() == 0)
		{
			continue;
		}

		// Layout this subgraph (data nodes are positioned inside AutoLayoutNodes)
		AutoLayoutNodes(Subgraph.Nodes, Settings);

		// Calculate bounding box after layout
		CalculateBoundingBox(Subgraph.Nodes, Subgraph.MinX, Subgraph.MaxX, Subgraph.MinY, Subgraph.MaxY);

		// Move subgraph to current Y position
		float OffsetY = CurrentY - Subgraph.MinY;
		for (UK2Node* Node : Subgraph.Nodes)
		{
			Node->NodePosY += static_cast<int32>(OffsetY);
		}

		// Update current Y for next subgraph
		CurrentY += (Subgraph.MaxY - Subgraph.MinY) + SubgraphSpacing;
	}
}

void ULayoutEngine::AutoLayoutNodes(TArray<UK2Node*>& Nodes, const FLayoutSettings& Settings)
{
	if (Nodes.Num() == 0)
	{
		return;
	}

	// Hybrid approach:
	// 1. Assign integer layers to all nodes (exec nodes via Sugiyama, data nodes via consumer backtracking)
	// 2. Calculate Y positions using MinimizeCrossings and CalculatePositions
	// 3. Then shift data node X to half-layer positions (keeping Y unchanged)

	TArray<UK2Node*> ExecNodes;
	TArray<UK2Node*> DataNodes;

	for (UK2Node* Node : Nodes)
	{
		if (IsPureDataNode(Node))
		{
			DataNodes.Add(Node);
		}
		else
		{
			ExecNodes.Add(Node);
		}
	}

	TMap<UK2Node*, FNodeLayerInfo> NodeInfo;
	TSet<UK2Node*> ExecNodeSet(ExecNodes);

	// Step 1: Layout exec nodes using standard Sugiyama
	if (ExecNodes.Num() > 0)
	{
		BuildAdjacencyInfo(ExecNodes, NodeInfo, true);
		AssignLayers(NodeInfo);
	}

	// Step 2: Assign INTEGER layers to data nodes (consumer layer - 1)
	// This is used for Y position calculation (MinimizeCrossings)
	TMap<UK2Node*, int32> DataNodeIntLayers;

	bool bChanged = true;
	int32 MaxIterations = DataNodes.Num() + 1;
	int32 Iteration = 0;

	while (bChanged && Iteration < MaxIterations)
	{
		bChanged = false;
		Iteration++;

		for (UK2Node* DataNode : DataNodes)
		{
			int32 MinConsumerLayer = INT_MAX;

			for (UEdGraphPin* Pin : DataNode->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output)
				{
					continue;
				}
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin || !LinkedPin->GetOwningNode())
					{
						continue;
					}
					UK2Node* Consumer = Cast<UK2Node>(LinkedPin->GetOwningNode());
					if (!Consumer)
					{
						continue;
					}

					int32 ConsumerLayer = INT_MAX;
					if (ExecNodeSet.Contains(Consumer))
					{
						if (FNodeLayerInfo* Info = NodeInfo.Find(Consumer))
						{
							ConsumerLayer = Info->Layer;
						}
					}
					else if (DataNodeIntLayers.Contains(Consumer))
					{
						ConsumerLayer = DataNodeIntLayers[Consumer];
					}

					if (ConsumerLayer < MinConsumerLayer)
					{
						MinConsumerLayer = ConsumerLayer;
					}
				}
			}

			if (MinConsumerLayer != INT_MAX)
			{
				int32 NewLayer = MinConsumerLayer - 1;
				if (NewLayer < 0) NewLayer = 0;

				if (!DataNodeIntLayers.Contains(DataNode) || DataNodeIntLayers[DataNode] != NewLayer)
				{
					DataNodeIntLayers.Add(DataNode, NewLayer);
					bChanged = true;
				}
			}
		}
	}

	// Handle orphan data nodes
	for (UK2Node* DataNode : DataNodes)
	{
		if (!DataNodeIntLayers.Contains(DataNode))
		{
			DataNodeIntLayers.Add(DataNode, 0);
		}
	}

	// Step 3: Add data nodes to NodeInfo with integer layers
	for (UK2Node* DataNode : DataNodes)
	{
		FNodeLayerInfo Info;
		Info.Node = DataNode;
		Info.Layer = DataNodeIntLayers[DataNode];
		NodeInfo.Add(DataNode, Info);
	}

	// Find total layers
	int32 NumLayers = 0;
	for (const auto& Pair : NodeInfo)
	{
		NumLayers = FMath::Max(NumLayers, Pair.Value.Layer + 1);
	}
	if (NumLayers == 0)
	{
		NumLayers = 1;
	}

	// Step 4: Minimize crossings and calculate positions for ALL nodes
	// This determines Y positions based on integer layers
	MinimizeCrossings(NodeInfo, NumLayers);
	CalculatePositions(NodeInfo, NumLayers, Settings);

	// Step 5: Calculate FLOAT layers for X positioning (half-layer steps)
	// Y stays exactly as calculated above
	TMap<UK2Node*, float> DataNodeFloatLayers;

	bChanged = true;
	Iteration = 0;
	while (bChanged && Iteration < MaxIterations)
	{
		bChanged = false;
		Iteration++;

		for (UK2Node* DataNode : DataNodes)
		{
			float MinConsumerLayer = FLT_MAX;

			for (UEdGraphPin* Pin : DataNode->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output)
				{
					continue;
				}
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin || !LinkedPin->GetOwningNode())
					{
						continue;
					}
					UK2Node* Consumer = Cast<UK2Node>(LinkedPin->GetOwningNode());
					if (!Consumer)
					{
						continue;
					}

					float ConsumerLayer = FLT_MAX;
					if (ExecNodeSet.Contains(Consumer))
					{
						if (FNodeLayerInfo* Info = NodeInfo.Find(Consumer))
						{
							ConsumerLayer = static_cast<float>(Info->Layer);
						}
					}
					else if (DataNodeFloatLayers.Contains(Consumer))
					{
						ConsumerLayer = DataNodeFloatLayers[Consumer];
					}

					if (ConsumerLayer < MinConsumerLayer)
					{
						MinConsumerLayer = ConsumerLayer;
					}
				}
			}

			if (MinConsumerLayer != FLT_MAX)
			{
				float NewLayer = MinConsumerLayer - 0.5f;
				if (NewLayer < 0) NewLayer = 0;

				if (!DataNodeFloatLayers.Contains(DataNode) ||
					FMath::Abs(DataNodeFloatLayers[DataNode] - NewLayer) > 0.01f)
				{
					DataNodeFloatLayers.Add(DataNode, NewLayer);
					bChanged = true;
				}
			}
		}
	}

	// Step 6: Apply float layers to X positions only
	for (UK2Node* DataNode : DataNodes)
	{
		if (DataNodeFloatLayers.Contains(DataNode))
		{
			float FloatLayer = DataNodeFloatLayers[DataNode];
			DataNode->NodePosX = static_cast<int32>(Settings.StartX + FloatLayer * Settings.HorizontalSpacing);
		}
	}

	// Step 7: Enforce exec pin order constraint
	// For nodes with multiple output exec pins (Sequence, ForLoop, etc.),
	// ensure successor nodes' Y positions match pin order
	EnforceExecPinOrder(ExecNodes, Settings);
}

TMap<FString, FNodePosition> ULayoutEngine::CalculateLayout(
	const TArray<FBlueprintNodeData>& NodeData,
	const FLayoutSettings& Settings)
{
	TMap<FString, FNodePosition> Result;

	if (NodeData.Num() == 0)
	{
		return Result;
	}

	// Build dependency graph from connection data
	TMap<FString, TArray<FString>> Successors;
	TMap<FString, TArray<FString>> Predecessors;
	TMap<FString, int32> Layers;

	for (const FBlueprintNodeData& Node : NodeData)
	{
		Successors.Add(Node.NodeId, TArray<FString>());
		Predecessors.Add(Node.NodeId, TArray<FString>());
	}

	// Build connections
	for (const FBlueprintNodeData& Node : NodeData)
	{
		for (const FBlueprintPinData& Pin : Node.Pins)
		{
			for (const FBlueprintPinConnection& Conn : Pin.Connections)
			{
				// This node depends on the connected node
				if (!Predecessors[Node.NodeId].Contains(Conn.SourceNodeId))
				{
					Predecessors[Node.NodeId].Add(Conn.SourceNodeId);
				}
				if (Successors.Contains(Conn.SourceNodeId))
				{
					if (!Successors[Conn.SourceNodeId].Contains(Node.NodeId))
					{
						Successors[Conn.SourceNodeId].Add(Node.NodeId);
					}
				}
			}
		}
	}

	// Assign layers (longest path from sources)
	TSet<FString> Visited;
	TArray<FString> Queue;

	// Find source nodes (no predecessors)
	for (const FBlueprintNodeData& Node : NodeData)
	{
		if (Predecessors[Node.NodeId].Num() == 0)
		{
			Queue.Add(Node.NodeId);
			Layers.Add(Node.NodeId, 0);
		}
	}

	// Process queue
	while (Queue.Num() > 0)
	{
		FString Current = Queue[0];
		Queue.RemoveAt(0);

		if (Visited.Contains(Current))
		{
			continue;
		}
		Visited.Add(Current);

		int32 CurrentLayer = Layers.FindRef(Current);

		for (const FString& Succ : Successors[Current])
		{
			int32 NewLayer = CurrentLayer + 1;
			if (!Layers.Contains(Succ) || Layers[Succ] < NewLayer)
			{
				Layers.Add(Succ, NewLayer);
			}
			Queue.Add(Succ);
		}
	}

	// Handle any unvisited nodes (cycles or disconnected)
	for (const FBlueprintNodeData& Node : NodeData)
	{
		if (!Layers.Contains(Node.NodeId))
		{
			Layers.Add(Node.NodeId, 0);
		}
	}

	// Group by layer
	TMap<int32, TArray<FString>> LayerNodes;
	int32 MaxLayer = 0;
	for (const auto& Pair : Layers)
	{
		LayerNodes.FindOrAdd(Pair.Value).Add(Pair.Key);
		MaxLayer = FMath::Max(MaxLayer, Pair.Value);
	}

	// Calculate positions
	for (int32 Layer = 0; Layer <= MaxLayer; Layer++)
	{
		if (TArray<FString>* NodesInLayer = LayerNodes.Find(Layer))
		{
			float X = Settings.StartX + Layer * Settings.HorizontalSpacing;

			for (int32 i = 0; i < NodesInLayer->Num(); i++)
			{
				float Y = Settings.StartY + i * Settings.VerticalSpacing;

				FNodePosition Pos;
				Pos.X = X;
				Pos.Y = Y;

				Result.Add((*NodesInLayer)[i], Pos);
			}
		}
	}

	return Result;
}

void ULayoutEngine::BuildAdjacencyInfo(
	TArray<UK2Node*>& Nodes,
	TMap<UK2Node*, FNodeLayerInfo>& OutNodeInfo,
	bool bPrioritizeExec)
{
	// Initialize node info
	for (UK2Node* Node : Nodes)
	{
		FNodeLayerInfo Info;
		Info.Node = Node;
		OutNodeInfo.Add(Node, Info);
	}

	// Build adjacency
	TSet<UK2Node*> NodeSet(Nodes);

	for (UK2Node* Node : Nodes)
	{
		FNodeLayerInfo& Info = OutNodeInfo[Node];

		// Get successors
		TArray<UK2Node*> ExecSucc = GetExecSuccessors(Node);
		TArray<UK2Node*> DataSucc = GetDataSuccessors(Node);

		for (UK2Node* Succ : ExecSucc)
		{
			if (NodeSet.Contains(Succ) && !Info.Successors.Contains(Succ))
			{
				Info.Successors.Add(Succ);
			}
		}

		if (!bPrioritizeExec)
		{
			for (UK2Node* Succ : DataSucc)
			{
				if (NodeSet.Contains(Succ) && !Info.Successors.Contains(Succ))
				{
					Info.Successors.Add(Succ);
				}
			}
		}

		// Get predecessors
		TArray<UK2Node*> ExecPred = GetExecPredecessors(Node);
		TArray<UK2Node*> DataPred = GetDataPredecessors(Node);

		for (UK2Node* Pred : ExecPred)
		{
			if (NodeSet.Contains(Pred) && !Info.Predecessors.Contains(Pred))
			{
				Info.Predecessors.Add(Pred);
			}
		}

		if (!bPrioritizeExec)
		{
			for (UK2Node* Pred : DataPred)
			{
				if (NodeSet.Contains(Pred) && !Info.Predecessors.Contains(Pred))
				{
					Info.Predecessors.Add(Pred);
				}
			}
		}
	}
}

void ULayoutEngine::AssignLayers(TMap<UK2Node*, FNodeLayerInfo>& NodeInfo)
{
	// Find source nodes (no predecessors)
	TArray<UK2Node*> Queue;
	TSet<UK2Node*> Processed;

	for (auto& Pair : NodeInfo)
	{
		if (Pair.Value.Predecessors.Num() == 0)
		{
			Pair.Value.Layer = 0;
			Queue.Add(Pair.Key);
		}
	}

	// Assign layers using longest path
	while (Queue.Num() > 0)
	{
		UK2Node* Current = Queue[0];
		Queue.RemoveAt(0);

		if (Processed.Contains(Current))
		{
			continue;
		}
		Processed.Add(Current);

		FNodeLayerInfo& CurrentInfo = NodeInfo[Current];

		for (UK2Node* Succ : CurrentInfo.Successors)
		{
			if (NodeInfo.Contains(Succ))
			{
				FNodeLayerInfo& SuccInfo = NodeInfo[Succ];
				int32 NewLayer = CurrentInfo.Layer + 1;

				if (SuccInfo.Layer < NewLayer)
				{
					SuccInfo.Layer = NewLayer;
				}

				// Check if all predecessors processed
				bool bAllPredProcessed = true;
				for (UK2Node* Pred : SuccInfo.Predecessors)
				{
					if (!Processed.Contains(Pred))
					{
						bAllPredProcessed = false;
						break;
					}
				}

				if (bAllPredProcessed)
				{
					Queue.Add(Succ);
				}
			}
		}
	}

	// Handle any unprocessed nodes (cycles)
	for (auto& Pair : NodeInfo)
	{
		if (!Processed.Contains(Pair.Key))
		{
			Pair.Value.Layer = 0;
		}
	}
}

void ULayoutEngine::MinimizeCrossings(TMap<UK2Node*, FNodeLayerInfo>& NodeInfo, int32 NumLayers)
{
	// Group nodes by layer
	TArray<TArray<FNodeLayerInfo*>> Layers;
	Layers.SetNum(NumLayers);

	for (auto& Pair : NodeInfo)
	{
		if (Pair.Value.Layer >= 0 && Pair.Value.Layer < NumLayers)
		{
			Layers[Pair.Value.Layer].Add(&Pair.Value);
		}
	}

	// Initialize positions within each layer
	for (int32 L = 0; L < NumLayers; L++)
	{
		for (int32 i = 0; i < Layers[L].Num(); i++)
		{
			Layers[L][i]->Position = i;
		}
	}

	// Barycenter method - iterate a few times
	const int32 MaxIterations = 4;
	for (int32 Iter = 0; Iter < MaxIterations; Iter++)
	{
		// Forward pass
		for (int32 L = 1; L < NumLayers; L++)
		{
			for (FNodeLayerInfo* Info : Layers[L])
			{
				float BC = CalculateBarycenter(Info, Layers[L - 1], true);
				if (BC >= 0)
				{
					Info->Position = static_cast<int32>(BC);
				}
			}

			// Sort by position
			Layers[L].Sort([](const FNodeLayerInfo& A, const FNodeLayerInfo& B)
			{
				return A.Position < B.Position;
			});

			// Reassign positions
			for (int32 i = 0; i < Layers[L].Num(); i++)
			{
				Layers[L][i]->Position = i;
			}
		}

		// Backward pass
		for (int32 L = NumLayers - 2; L >= 0; L--)
		{
			for (FNodeLayerInfo* Info : Layers[L])
			{
				float BC = CalculateBarycenter(Info, Layers[L + 1], false);
				if (BC >= 0)
				{
					Info->Position = static_cast<int32>(BC);
				}
			}

			// Sort by position
			Layers[L].Sort([](const FNodeLayerInfo& A, const FNodeLayerInfo& B)
			{
				return A.Position < B.Position;
			});

			// Reassign positions
			for (int32 i = 0; i < Layers[L].Num(); i++)
			{
				Layers[L][i]->Position = i;
			}
		}
	}
}

void ULayoutEngine::CalculatePositions(
	TMap<UK2Node*, FNodeLayerInfo>& NodeInfo,
	int32 NumLayers,
	const FLayoutSettings& Settings)
{
	// Group by layer
	TArray<TArray<FNodeLayerInfo*>> Layers;
	Layers.SetNum(NumLayers);

	for (auto& Pair : NodeInfo)
	{
		if (Pair.Value.Layer >= 0 && Pair.Value.Layer < NumLayers)
		{
			Layers[Pair.Value.Layer].Add(&Pair.Value);
		}
	}

	// Sort each layer by position
	for (TArray<FNodeLayerInfo*>& Layer : Layers)
	{
		Layer.Sort([](const FNodeLayerInfo& A, const FNodeLayerInfo& B)
		{
			return A.Position < B.Position;
		});
	}

	// Apply positions
	for (int32 L = 0; L < NumLayers; L++)
	{
		float X = Settings.StartX + L * Settings.HorizontalSpacing;

		for (int32 i = 0; i < Layers[L].Num(); i++)
		{
			float Y = Settings.StartY + i * Settings.VerticalSpacing;

			if (UK2Node* Node = Layers[L][i]->Node)
			{
				Node->NodePosX = static_cast<int32>(X);
				Node->NodePosY = static_cast<int32>(Y);
			}
		}
	}
}

TArray<UK2Node*> ULayoutEngine::GetExecSuccessors(UK2Node* Node)
{
	TArray<UK2Node*> Result;

	if (!Node)
	{
		return Result;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin && LinkedPin->GetOwningNode())
				{
					// Skip connections to "Break" pin - it's a back-edge to loop node
					// This keeps Successors/Predecessors symmetric
					FString LinkedPinName = LinkedPin->PinName.ToString();
					if (LinkedPinName.Equals(TEXT("Break"), ESearchCase::IgnoreCase))
					{
						continue;
					}

					if (UK2Node* LinkedNode = Cast<UK2Node>(LinkedPin->GetOwningNode()))
					{
						if (!Result.Contains(LinkedNode))
						{
							Result.Add(LinkedNode);
						}
					}
				}
			}
		}
	}

	return Result;
}

TArray<UK2Node*> ULayoutEngine::GetExecPredecessors(UK2Node* Node)
{
	TArray<UK2Node*> Result;

	if (!Node)
	{
		return Result;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			// Skip "Break" pin - it's a back-edge from loop body, not a true predecessor
			// This prevents cycles in the layout graph for ForLoopWithBreak
			FString PinName = Pin->PinName.ToString();
			if (PinName.Equals(TEXT("Break"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin && LinkedPin->GetOwningNode())
				{
					if (UK2Node* LinkedNode = Cast<UK2Node>(LinkedPin->GetOwningNode()))
					{
						if (!Result.Contains(LinkedNode))
						{
							Result.Add(LinkedNode);
						}
					}
				}
			}
		}
	}

	return Result;
}

TArray<UK2Node*> ULayoutEngine::GetDataSuccessors(UK2Node* Node)
{
	TArray<UK2Node*> Result;

	if (!Node)
	{
		return Result;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
		{
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin && LinkedPin->GetOwningNode())
				{
					if (UK2Node* LinkedNode = Cast<UK2Node>(LinkedPin->GetOwningNode()))
					{
						if (!Result.Contains(LinkedNode))
						{
							Result.Add(LinkedNode);
						}
					}
				}
			}
		}
	}

	return Result;
}

TArray<UK2Node*> ULayoutEngine::GetDataPredecessors(UK2Node* Node)
{
	TArray<UK2Node*> Result;

	if (!Node)
	{
		return Result;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
		{
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin && LinkedPin->GetOwningNode())
				{
					if (UK2Node* LinkedNode = Cast<UK2Node>(LinkedPin->GetOwningNode()))
					{
						if (!Result.Contains(LinkedNode))
						{
							Result.Add(LinkedNode);
						}
					}
				}
			}
		}
	}

	return Result;
}

int32 ULayoutEngine::CountCrossings(
	const TArray<FNodeLayerInfo*>& Layer1,
	const TArray<FNodeLayerInfo*>& Layer2)
{
	int32 Crossings = 0;

	// For each pair of edges, check if they cross
	for (int32 i = 0; i < Layer1.Num(); i++)
	{
		for (int32 j = i + 1; j < Layer1.Num(); j++)
		{
			// Get positions of connected nodes in Layer2
			for (UK2Node* Succ1 : Layer1[i]->Successors)
			{
				for (UK2Node* Succ2 : Layer1[j]->Successors)
				{
					// Find positions in Layer2
					int32 Pos1 = -1, Pos2 = -1;
					for (int32 k = 0; k < Layer2.Num(); k++)
					{
						if (Layer2[k]->Node == Succ1) Pos1 = k;
						if (Layer2[k]->Node == Succ2) Pos2 = k;
					}

					// Check for crossing (i < j but Pos1 > Pos2)
					if (Pos1 >= 0 && Pos2 >= 0 && Pos1 > Pos2)
					{
						Crossings++;
					}
				}
			}
		}
	}

	return Crossings;
}

float ULayoutEngine::CalculateBarycenter(
	FNodeLayerInfo* NodeInfo,
	const TArray<FNodeLayerInfo*>& AdjacentLayer,
	bool bUsePredecessors)
{
	if (!NodeInfo)
	{
		return -1.0f;
	}

	TArray<UK2Node*>& Adjacent = bUsePredecessors ? NodeInfo->Predecessors : NodeInfo->Successors;

	if (Adjacent.Num() == 0)
	{
		return -1.0f;
	}

	float Sum = 0.0f;
	int32 Count = 0;

	for (UK2Node* AdjNode : Adjacent)
	{
		for (int32 i = 0; i < AdjacentLayer.Num(); i++)
		{
			if (AdjacentLayer[i]->Node == AdjNode)
			{
				Sum += i;
				Count++;
				break;
			}
		}
	}

	return Count > 0 ? Sum / Count : -1.0f;
}

TArray<FSubgraphInfo> ULayoutEngine::FindSubgraphs(TArray<UK2Node*>& Nodes)
{
	TArray<FSubgraphInfo> Result;
	TSet<UK2Node*> Assigned;
	TSet<UK2Node*> NodeSet(Nodes);

	// First pass: find all root nodes (events, function entries)
	TArray<UK2Node*> RootNodes;
	for (UK2Node* Node : Nodes)
	{
		if (IsRootNode(Node))
		{
			RootNodes.Add(Node);
		}
	}

	// For each root, collect its connected subgraph
	for (UK2Node* Root : RootNodes)
	{
		if (Assigned.Contains(Root))
		{
			continue;
		}

		FSubgraphInfo Subgraph;
		Subgraph.RootNode = Root;

		TSet<UK2Node*> SubgraphNodes;
		CollectConnectedNodes(Root, SubgraphNodes, Assigned);

		// Filter to only include nodes in our graph
		for (UK2Node* Node : SubgraphNodes)
		{
			if (NodeSet.Contains(Node))
			{
				Subgraph.Nodes.Add(Node);
				Assigned.Add(Node);
			}
		}

		if (Subgraph.Nodes.Num() > 0)
		{
			Result.Add(Subgraph);
		}
	}

	// Handle any orphaned nodes (not connected to any event)
	TArray<UK2Node*> OrphanedNodes;
	for (UK2Node* Node : Nodes)
	{
		if (!Assigned.Contains(Node))
		{
			OrphanedNodes.Add(Node);
		}
	}

	if (OrphanedNodes.Num() > 0)
	{
		FSubgraphInfo OrphanSubgraph;
		OrphanSubgraph.Nodes = OrphanedNodes;
		Result.Add(OrphanSubgraph);
	}

	return Result;
}

void ULayoutEngine::CollectConnectedNodes(UK2Node* Root, TSet<UK2Node*>& OutNodes, TSet<UK2Node*>& Visited)
{
	if (!Root || Visited.Contains(Root))
	{
		return;
	}

	Visited.Add(Root);
	OutNodes.Add(Root);

	// Follow all connections (both exec and data)
	for (UEdGraphPin* Pin : Root->Pins)
	{
		if (!Pin) continue;

		for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (LinkedPin && LinkedPin->GetOwningNode())
			{
				if (UK2Node* LinkedNode = Cast<UK2Node>(LinkedPin->GetOwningNode()))
				{
					CollectConnectedNodes(LinkedNode, OutNodes, Visited);
				}
			}
		}
	}
}

bool ULayoutEngine::IsRootNode(UK2Node* Node)
{
	if (!Node)
	{
		return false;
	}

	// Check if it's an event node
	if (Cast<UK2Node_Event>(Node))
	{
		return true;
	}

	// Check if it's a function entry node
	if (Cast<UK2Node_FunctionEntry>(Node))
	{
		return true;
	}

	return false;
}

bool ULayoutEngine::IsPureDataNode(UK2Node* Node)
{
	if (!Node)
	{
		return false;
	}

	// Variable Get nodes are pure data nodes
	if (Cast<UK2Node_VariableGet>(Node))
	{
		return true;
	}

	// Check if it's a pure function (no exec pins)
	bool bHasExecPin = false;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			bHasExecPin = true;
			break;
		}
	}

	return !bHasExecPin;
}

// Helper function to recursively find exec node consumers through data node chains
static void FindExecConsumers(
	UK2Node* Node,
	const TSet<UK2Node*>& ExecNodeSet,
	TSet<UK2Node*>& OutExecConsumers,
	TSet<UK2Node*>& Visited)
{
	if (!Node || Visited.Contains(Node))
	{
		return;
	}
	Visited.Add(Node);

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->Direction != EGPD_Output)
		{
			continue;
		}

		for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (!LinkedPin || !LinkedPin->GetOwningNode())
			{
				continue;
			}

			UK2Node* Consumer = Cast<UK2Node>(LinkedPin->GetOwningNode());
			if (!Consumer)
			{
				continue;
			}

			if (ExecNodeSet.Contains(Consumer))
			{
				// Found an exec consumer
				OutExecConsumers.Add(Consumer);
			}
			else
			{
				// Consumer is another data node, recurse
				FindExecConsumers(Consumer, ExecNodeSet, OutExecConsumers, Visited);
			}
		}
	}
}

void ULayoutEngine::PositionDataNodesNearConsumers(
	TArray<UK2Node*>& DataNodes,
	TArray<UK2Node*>& ExecNodes,
	const FLayoutSettings& Settings)
{
	if (DataNodes.Num() == 0)
	{
		return;
	}

	// Ultra-simple approach: position each data node above its first consumer
	// with simple vertical stacking for overlaps

	const float OffsetX = 180.0f;  // How far left of consumer
	const float OffsetY = -80.0f;  // How far above consumer (negative = above)
	const float StackSpacing = 100.0f;  // Vertical spacing when stacking

	// Track occupied positions (X -> list of Y ranges)
	TMap<int32, TArray<int32>> OccupiedPositions;

	// First, mark exec node positions as occupied
	for (UK2Node* ExecNode : ExecNodes)
	{
		int32 GridX = ExecNode->NodePosX / 100;  // Quantize to grid
		OccupiedPositions.FindOrAdd(GridX).Add(ExecNode->NodePosY);
	}

	// For each data node, find where to place it
	for (UK2Node* DataNode : DataNodes)
	{
		// Find the first consumer
		UK2Node* Consumer = nullptr;
		for (UEdGraphPin* Pin : DataNode->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output)
			{
				continue;
			}
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin && LinkedPin->GetOwningNode())
				{
					Consumer = Cast<UK2Node>(LinkedPin->GetOwningNode());
					if (Consumer)
					{
						break;
					}
				}
			}
			if (Consumer)
			{
				break;
			}
		}

		int32 TargetX, TargetY;

		if (Consumer)
		{
			// Position relative to consumer
			TargetX = Consumer->NodePosX - static_cast<int32>(OffsetX);
			TargetY = Consumer->NodePosY + static_cast<int32>(OffsetY);
		}
		else
		{
			// Orphan node
			TargetX = static_cast<int32>(Settings.StartX - OffsetX);
			TargetY = static_cast<int32>(Settings.StartY);
		}

		// Check for overlap and stack if needed
		int32 GridX = TargetX / 100;
		TArray<int32>& YPositions = OccupiedPositions.FindOrAdd(GridX);

		// Find a Y position that doesn't overlap
		bool bOverlap = true;
		int32 Attempts = 0;
		while (bOverlap && Attempts < 20)
		{
			bOverlap = false;
			for (int32 OccupiedY : YPositions)
			{
				if (FMath::Abs(TargetY - OccupiedY) < static_cast<int32>(StackSpacing))
				{
					bOverlap = true;
					TargetY = OccupiedY - static_cast<int32>(StackSpacing);  // Move up
					break;
				}
			}
			Attempts++;
		}

		// Apply position
		DataNode->NodePosX = TargetX;
		DataNode->NodePosY = TargetY;

		// Mark as occupied
		YPositions.Add(TargetY);
	}
}

void ULayoutEngine::AdjustDataNodePositions(TArray<UK2Node*>& Nodes, const FLayoutSettings& Settings)
{
	// This function is now deprecated, use PositionDataNodesNearConsumers instead
	// Kept for API compatibility
	TArray<UK2Node*> DataNodes;
	TArray<UK2Node*> ExecNodes;

	for (UK2Node* Node : Nodes)
	{
		if (IsPureDataNode(Node))
		{
			DataNodes.Add(Node);
		}
		else
		{
			ExecNodes.Add(Node);
		}
	}

	PositionDataNodesNearConsumers(DataNodes, ExecNodes, Settings);
}

void ULayoutEngine::CalculateBoundingBox(const TArray<UK2Node*>& Nodes, float& OutMinX, float& OutMaxX, float& OutMinY, float& OutMaxY)
{
	if (Nodes.Num() == 0)
	{
		OutMinX = OutMaxX = OutMinY = OutMaxY = 0;
		return;
	}

	OutMinX = OutMinY = FLT_MAX;
	OutMaxX = OutMaxY = -FLT_MAX;

	const float NodeWidth = 200.0f;  // Approximate node width
	const float NodeHeight = 100.0f; // Approximate node height

	for (UK2Node* Node : Nodes)
	{
		if (!Node) continue;

		OutMinX = FMath::Min(OutMinX, static_cast<float>(Node->NodePosX));
		OutMaxX = FMath::Max(OutMaxX, static_cast<float>(Node->NodePosX) + NodeWidth);
		OutMinY = FMath::Min(OutMinY, static_cast<float>(Node->NodePosY));
		OutMaxY = FMath::Max(OutMaxY, static_cast<float>(Node->NodePosY) + NodeHeight);
	}
}

TArray<TPair<int32, UK2Node*>> ULayoutEngine::GetOrderedExecSuccessors(UK2Node* Node)
{
	TArray<TPair<int32, UK2Node*>> Result;

	if (!Node)
	{
		return Result;
	}

	// Collect output exec pins with their indices
	int32 PinIndex = 0;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin && LinkedPin->GetOwningNode())
				{
					if (UK2Node* LinkedNode = Cast<UK2Node>(LinkedPin->GetOwningNode()))
					{
						// Use the pin's visual index (order in Pins array for output exec pins)
						Result.Add(TPair<int32, UK2Node*>(PinIndex, LinkedNode));
					}
				}
			}
			PinIndex++;
		}
	}

	// Sort by pin index (lower index = higher pin = should have lower Y)
	Result.Sort([](const TPair<int32, UK2Node*>& A, const TPair<int32, UK2Node*>& B)
	{
		return A.Key < B.Key;
	});

	return Result;
}

void ULayoutEngine::EnforceExecPinOrder(TArray<UK2Node*>& Nodes, const FLayoutSettings& Settings)
{
	TSet<UK2Node*> NodeSet(Nodes);

	// For each node with multiple output exec pins, enforce order constraint
	for (UK2Node* Node : Nodes)
	{
		if (!Node)
		{
			continue;
		}

		// Get ordered successors (by pin index)
		TArray<TPair<int32, UK2Node*>> OrderedSuccessors = GetOrderedExecSuccessors(Node);

		// Only process if there are multiple successors in our node set
		TArray<UK2Node*> ValidSuccessors;
		for (const auto& Pair : OrderedSuccessors)
		{
			if (NodeSet.Contains(Pair.Value) && !ValidSuccessors.Contains(Pair.Value))
			{
				ValidSuccessors.Add(Pair.Value);
			}
		}

		if (ValidSuccessors.Num() < 2)
		{
			continue;
		}

		// Check if successors are in correct Y order (lower pin index -> lower Y)
		// If not, swap their Y positions
		for (int32 i = 0; i < ValidSuccessors.Num() - 1; i++)
		{
			UK2Node* Upper = ValidSuccessors[i];
			UK2Node* Lower = ValidSuccessors[i + 1];

			// Upper pin should have lower or equal Y
			if (Upper->NodePosY > Lower->NodePosY)
			{
				// Swap Y positions
				int32 TempY = Upper->NodePosY;
				Upper->NodePosY = Lower->NodePosY;
				Lower->NodePosY = TempY;
			}
		}
	}

	// Second pass: propagate position changes to subgraphs
	// For nodes that were swapped, recursively adjust their successors
	// This is a simplified propagation that moves entire subgraphs
	for (UK2Node* Node : Nodes)
	{
		if (!Node)
		{
			continue;
		}

		TArray<TPair<int32, UK2Node*>> OrderedSuccessors = GetOrderedExecSuccessors(Node);

		if (OrderedSuccessors.Num() < 2)
		{
			continue;
		}

		// For each pair of consecutive branches, ensure minimum spacing
		for (int32 i = 0; i < OrderedSuccessors.Num() - 1; i++)
		{
			UK2Node* CurrentBranchRoot = nullptr;
			UK2Node* NextBranchRoot = nullptr;

			for (const auto& Pair : OrderedSuccessors)
			{
				if (Pair.Key == i && NodeSet.Contains(Pair.Value))
				{
					CurrentBranchRoot = Pair.Value;
				}
				if (Pair.Key == i + 1 && NodeSet.Contains(Pair.Value))
				{
					NextBranchRoot = Pair.Value;
				}
			}

			if (!CurrentBranchRoot || !NextBranchRoot)
			{
				continue;
			}

			// Find all nodes reachable from CurrentBranchRoot (in same layer or later)
			// and ensure they don't overlap with NextBranchRoot's subgraph
			// This is a simplified check - just ensure minimum gap between branch roots
			float MinGap = Settings.VerticalSpacing * 0.5f;
			float CurrentBottom = CurrentBranchRoot->NodePosY + 100.0f; // Approximate height
			float NextTop = NextBranchRoot->NodePosY;

			if (NextTop < CurrentBottom + MinGap)
			{
				// Push down the next branch
				float Offset = (CurrentBottom + MinGap) - NextTop;
				NextBranchRoot->NodePosY += static_cast<int32>(Offset);

				// Also push down all nodes that are successors of NextBranchRoot
				TSet<UK2Node*> Visited;
				TArray<UK2Node*> Queue;
				Queue.Add(NextBranchRoot);

				while (Queue.Num() > 0)
				{
					UK2Node* Current = Queue[0];
					Queue.RemoveAt(0);

					if (Visited.Contains(Current) || Current == NextBranchRoot)
					{
						if (Current != NextBranchRoot)
						{
							continue;
						}
						Visited.Add(Current);
					}
					else
					{
						Visited.Add(Current);
						Current->NodePosY += static_cast<int32>(Offset);
					}

					// Add exec successors to queue
					TArray<UK2Node*> Successors = GetExecSuccessors(Current);
					for (UK2Node* Succ : Successors)
					{
						if (NodeSet.Contains(Succ) && !Visited.Contains(Succ))
						{
							Queue.Add(Succ);
						}
					}
				}
			}
		}
	}
}
