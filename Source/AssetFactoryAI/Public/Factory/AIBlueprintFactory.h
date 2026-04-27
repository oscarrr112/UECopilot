// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JSON/BlueprintJSONSchema.h"
#include "Factory/LayoutEngine.h"
#include "AIBlueprintFactory.generated.h"

class UBlueprint;
class UEdGraph;
class UK2Node;

DECLARE_LOG_CATEGORY_EXTERN(LogBlueprintFactory, Log, All);

/**
 * Blueprint generation result
 */
USTRUCT(BlueprintType)
struct ASSETFACTORYAI_API FBlueprintGenerationResult
{
	GENERATED_BODY()

	/** Was generation successful? */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bSuccess = false;

	/** Generated or modified blueprint */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	TObjectPtr<UBlueprint> Blueprint = nullptr;

	/** Error message if failed */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString ErrorMessage;

	/** Warnings during generation */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	TArray<FString> Warnings;

	/** Created node IDs mapped to actual nodes */
	TMap<FString, UK2Node*> CreatedNodes;
};

/**
 * AI Blueprint Factory - Creates and modifies blueprints from intermediate representation
 */
UCLASS()
class ASSETFACTORYAI_API UAIBlueprintFactory : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Create a new blueprint from JSON data
	 * @param Data - Blueprint data from JSON
	 * @param PackagePath - Package path for the new blueprint (e.g., "/Game/Blueprints")
	 * @param bAutoLayout - Apply automatic layout to nodes
	 * @return Generation result
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Factory")
	static FBlueprintGenerationResult CreateBlueprint(
		const FBlueprintData& Data,
		const FString& PackagePath,
		bool bAutoLayout = true);

	/**
	 * Create a temporary blueprint for preview (not saved)
	 * @param Data - Blueprint data from JSON
	 * @return Generation result (blueprint is transient)
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Factory")
	static FBlueprintGenerationResult CreatePreviewBlueprint(const FBlueprintData& Data);

	/**
	 * Modify an existing blueprint
	 * @param Blueprint - Blueprint to modify
	 * @param Data - New blueprint data
	 * @param bMerge - If true, merge with existing; if false, replace
	 * @return Generation result
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Factory")
	static FBlueprintGenerationResult ModifyBlueprint(
		UBlueprint* Blueprint,
		const FBlueprintData& Data,
		bool bMerge = false);

	/**
	 * Add a graph to a blueprint
	 * @param Blueprint - Target blueprint
	 * @param GraphData - Graph data to add
	 * @return Generation result
	 */
	static FBlueprintGenerationResult AddGraph(
		UBlueprint* Blueprint,
		const FBlueprintGraphData& GraphData,
		bool bMerge = true);

	/**
	 * Add a variable to a blueprint
	 * @param Blueprint - Target blueprint
	 * @param VarData - Variable data to add
	 * @return Success
	 */
	static bool AddVariable(UBlueprint* Blueprint, const FBlueprintVariableData& VarData);

	/**
	 * Connect nodes based on pin connection data
	 * @param NodeMap - Map of node IDs to actual nodes
	 * @param NodeData - Array of node data containing connection info
	 * @param OutErrors - Errors encountered during connection
	 * @return Number of successful connections
	 */
	static int32 ConnectNodes(
		const TMap<FString, UK2Node*>& NodeMap,
		const TArray<FBlueprintNodeData>& NodeData,
		TArray<FString>& OutErrors);

private:
	/**
	 * Find or create parent class
	 */
	static UClass* ResolveParentClass(const FString& ParentClassPath);

	/**
	 * Create variables in blueprint
	 */
	static void CreateVariables(UBlueprint* Blueprint, const TArray<FBlueprintVariableData>& Variables, TArray<FString>& OutWarnings);

	/**
	 * Create event graph
	 */
	static bool CreateEventGraph(
		UBlueprint* Blueprint,
		const FBlueprintGraphData& GraphData,
		TMap<FString, UK2Node*>& OutNodeMap,
		TArray<FString>& OutWarnings,
		bool bMerge = true);

	/**
	 * Create function graph
	 */
	static bool CreateFunctionGraph(
		UBlueprint* Blueprint,
		const FBlueprintGraphData& GraphData,
		TMap<FString, UK2Node*>& OutNodeMap,
		TArray<FString>& OutWarnings);

	/**
	 * Get pin type from string
	 */
	static FEdGraphPinType GetPinType(EBlueprintVarType VarType, const FString& TypeClass = TEXT(""));

	/**
	 * Convert variable type enum to FEdGraphPinType
	 */
	static FEdGraphPinType VarTypeToPinType(const FBlueprintVariableData& VarData);

	/**
	 * Find pin on node by name
	 */
	static UEdGraphPin* FindPinByName(UK2Node* Node, const FString& PinName, EEdGraphPinDirection Direction = EGPD_MAX);
};
