// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JSON/BlueprintJSONSchema.h"
#include "NodeSpawner.generated.h"

class UEdGraph;
class UK2Node;
class UBlueprint;

DECLARE_LOG_CATEGORY_EXTERN(LogNodeSpawner, Log, All);

/**
 * Node spawn result
 */
USTRUCT(BlueprintType)
struct UECOPILOT_API FNodeSpawnResult
{
	GENERATED_BODY()

	/** Was spawning successful? */
	bool bSuccess = false;

	/** Spawned node (if successful) */
	UK2Node* Node = nullptr;

	/** Error message (if failed) */
	FString ErrorMessage;
};

/**
 * Node Spawner - Creates blueprint nodes from intermediate representation
 */
UCLASS()
class UECOPILOT_API UNodeSpawner : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Spawn a node in a graph from node data
	 * @param Graph - Target graph
	 * @param NodeData - Node data from JSON
	 * @param Blueprint - Owner blueprint
	 * @return Spawn result
	 */
	static FNodeSpawnResult SpawnNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint);

	/**
	 * Spawn an event node
	 */
	static FNodeSpawnResult SpawnEventNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint);

	/**
	 * Spawn a function call node
	 */
	static FNodeSpawnResult SpawnFunctionCallNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint);

	/**
	 * Spawn a flow control node (Branch, Sequence, loops, etc.)
	 */
	static FNodeSpawnResult SpawnFlowControlNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint);

	/**
	 * Spawn a variable get/set node
	 */
	static FNodeSpawnResult SpawnVariableNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint);

	/**
	 * Spawn a math operation node
	 */
	static FNodeSpawnResult SpawnMathNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint);

	/**
	 * Spawn a comparison node
	 */
	static FNodeSpawnResult SpawnComparisonNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint);

	/**
	 * Spawn a logic node (AND, OR, NOT)
	 */
	static FNodeSpawnResult SpawnLogicNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint);

	/**
	 * Spawn a cast node
	 */
	static FNodeSpawnResult SpawnCastNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint);

	/**
	 * Spawn an array operation node
	 */
	static FNodeSpawnResult SpawnArrayNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint);

	/**
	 * Spawn a return node for functions
	 */
	static FNodeSpawnResult SpawnReturnNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint);

	/**
	 * Spawn a macro instance node (for loops like ForLoop, WhileLoop)
	 */
	static FNodeSpawnResult SpawnMacroNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint);

	/**
	 * Find a function by path
	 * @param FunctionPath - Fully qualified function path (e.g., "/Script/Engine.Actor.GetActorLocation")
	 * @return The function if found, nullptr otherwise
	 */
	static UFunction* FindFunctionByPath(const FString& FunctionPath);

	/**
	 * Find a class by path
	 * @param ClassPath - Class path (e.g., "/Script/Engine.Actor")
	 * @return The class if found, nullptr otherwise
	 */
	static UClass* FindClassByPath(const FString& ClassPath);

	/**
	 * Find a macro graph by name from the standard macros blueprint
	 * @param MacroName - Name of the macro (e.g., "ForLoop", "WhileLoop")
	 * @return The macro graph if found, nullptr otherwise
	 */
	static UEdGraph* FindMacroGraph(const FString& MacroName);

private:
	/** Helper to set node position */
	static void SetNodePosition(UK2Node* Node, const FNodePosition& Position);

	/** Helper to create a generic K2 node */
	template<typename T>
	static T* CreateNode(UEdGraph* Graph);
};

// Template implementation
template<typename T>
T* UNodeSpawner::CreateNode(UEdGraph* Graph)
{
	check(Graph);
	T* Node = NewObject<T>(Graph);
	Graph->AddNode(Node, false, false);
	Node->CreateNewGuid();
	Node->PostPlacedNewNode();
	return Node;
}
