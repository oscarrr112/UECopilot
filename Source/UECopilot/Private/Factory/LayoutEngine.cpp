// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factory/LayoutEngine.h"
#include "K2Node.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"

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

	if (K2Nodes.Num() > 0)
	{
		AutoLayoutNodes(K2Nodes, Settings);
	}
}

void ULayoutEngine::AutoLayoutNodes(TArray<UK2Node*>& Nodes, const FLayoutSettings& Settings)
{
	if (Nodes.Num() == 0)
	{
		return;
	}

	// Build node adjacency information
	TMap<UK2Node*, FNodeLayerInfo> NodeInfo;
	BuildAdjacencyInfo(Nodes, NodeInfo, Settings.bPrioritizeExecFlow);

	// Assign layers using topological sort
	AssignLayers(NodeInfo);

	// Find max layer
	int32 NumLayers = 0;
	for (const auto& Pair : NodeInfo)
	{
		NumLayers = FMath::Max(NumLayers, Pair.Value.Layer + 1);
	}

	// Order nodes within layers to minimize crossings
	MinimizeCrossings(NodeInfo, NumLayers);

	// Calculate and apply final positions
	CalculatePositions(NodeInfo, NumLayers, Settings);
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
