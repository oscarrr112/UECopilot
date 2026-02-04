// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factory/NodeSpawner.h"
#include "AssetFactoryAI.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_ForEachElementInEnum.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "K2Node_MakeArray.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_FunctionEntry.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY(LogNodeSpawner);

FNodeSpawnResult UNodeSpawner::SpawnNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint)
{
	FNodeSpawnResult Result;

	if (!Graph || !Blueprint)
	{
		Result.ErrorMessage = TEXT("Invalid graph or blueprint");
		return Result;
	}

	switch (NodeData.NodeType)
	{
	// Events
	case EBlueprintNodeType::Event_BeginPlay:
	case EBlueprintNodeType::Event_Tick:
	case EBlueprintNodeType::Event_Custom:
	case EBlueprintNodeType::Event_Input:
		return SpawnEventNode(Graph, NodeData, Blueprint);

	// Flow Control
	case EBlueprintNodeType::Flow_Branch:
	case EBlueprintNodeType::Flow_Sequence:
	case EBlueprintNodeType::Flow_ForLoop:
	case EBlueprintNodeType::Flow_ForEachLoop:
	case EBlueprintNodeType::Flow_WhileLoop:
	case EBlueprintNodeType::Flow_DoOnce:
	case EBlueprintNodeType::Flow_Gate:
	case EBlueprintNodeType::Flow_Delay:
		return SpawnFlowControlNode(Graph, NodeData, Blueprint);

	// Functions
	case EBlueprintNodeType::CallFunction:
	case EBlueprintNodeType::PureFunction:
		return SpawnFunctionCallNode(Graph, NodeData, Blueprint);

	// Variables
	case EBlueprintNodeType::Variable_Get:
	case EBlueprintNodeType::Variable_Set:
	case EBlueprintNodeType::Variable_GetLocal:
	case EBlueprintNodeType::Variable_SetLocal:
		return SpawnVariableNode(Graph, NodeData, Blueprint);

	// Math
	case EBlueprintNodeType::Math_Add:
	case EBlueprintNodeType::Math_Subtract:
	case EBlueprintNodeType::Math_Multiply:
	case EBlueprintNodeType::Math_Divide:
		return SpawnMathNode(Graph, NodeData, Blueprint);

	// Comparison
	case EBlueprintNodeType::Compare_Equal:
	case EBlueprintNodeType::Compare_NotEqual:
	case EBlueprintNodeType::Compare_Greater:
	case EBlueprintNodeType::Compare_Less:
	case EBlueprintNodeType::Compare_GreaterEqual:
	case EBlueprintNodeType::Compare_LessEqual:
		return SpawnComparisonNode(Graph, NodeData, Blueprint);

	// Logic
	case EBlueprintNodeType::Logic_And:
	case EBlueprintNodeType::Logic_Or:
	case EBlueprintNodeType::Logic_Not:
		return SpawnLogicNode(Graph, NodeData, Blueprint);

	// Cast
	case EBlueprintNodeType::Cast:
		return SpawnCastNode(Graph, NodeData, Blueprint);

	// Array
	case EBlueprintNodeType::Array_Add:
	case EBlueprintNodeType::Array_Remove:
	case EBlueprintNodeType::Array_Get:
	case EBlueprintNodeType::Array_Set:
	case EBlueprintNodeType::Array_Length:
	case EBlueprintNodeType::Array_Clear:
		return SpawnArrayNode(Graph, NodeData, Blueprint);

	// Return node for functions
	case EBlueprintNodeType::Return:
		return SpawnReturnNode(Graph, NodeData, Blueprint);

	default:
		Result.ErrorMessage = FString::Printf(TEXT("Unsupported node type: %d"), static_cast<int32>(NodeData.NodeType));
		return Result;
	}
}

FNodeSpawnResult UNodeSpawner::SpawnEventNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint)
{
	FNodeSpawnResult Result;

	UK2Node* Node = nullptr;
	FName EventFunctionName;

	// Determine event function name based on node type
	switch (NodeData.NodeType)
	{
	case EBlueprintNodeType::Event_BeginPlay:
		EventFunctionName = FName(TEXT("ReceiveBeginPlay"));
		break;
	case EBlueprintNodeType::Event_Tick:
		EventFunctionName = FName(TEXT("ReceiveTick"));
		break;
	case EBlueprintNodeType::Event_Custom:
		// Custom events are handled separately
		break;
	default:
		Result.ErrorMessage = TEXT("Unknown event type");
		return Result;
	}

	// For built-in events, first check if the event already exists in the graph
	if (NodeData.NodeType != EBlueprintNodeType::Event_Custom && !EventFunctionName.IsNone())
	{
		for (UEdGraphNode* ExistingNode : Graph->Nodes)
		{
			if (UK2Node_Event* ExistingEvent = Cast<UK2Node_Event>(ExistingNode))
			{
				if (ExistingEvent->EventReference.GetMemberName() == EventFunctionName)
				{
					// Found existing event node, reuse it
					UE_LOG(LogAssetFactoryAI, Log, TEXT("Reusing existing event node: %s"), *EventFunctionName.ToString());
					Result.bSuccess = true;
					Result.Node = ExistingEvent;
					return Result;
				}
			}
		}
	}

	// Event doesn't exist, create new one
	switch (NodeData.NodeType)
	{
	case EBlueprintNodeType::Event_BeginPlay:
	{
		UFunction* BeginPlayFunc = AActor::StaticClass()->FindFunctionByName(EventFunctionName);
		if (BeginPlayFunc)
		{
			UK2Node_Event* EventNode = CreateNode<UK2Node_Event>(Graph);
			EventNode->EventReference.SetExternalMember(EventFunctionName, AActor::StaticClass());
			EventNode->bOverrideFunction = true;
			EventNode->AllocateDefaultPins();
			Node = EventNode;
		}
		break;
	}

	case EBlueprintNodeType::Event_Tick:
	{
		UFunction* TickFunc = AActor::StaticClass()->FindFunctionByName(EventFunctionName);
		if (TickFunc)
		{
			UK2Node_Event* EventNode = CreateNode<UK2Node_Event>(Graph);
			EventNode->EventReference.SetExternalMember(EventFunctionName, AActor::StaticClass());
			EventNode->bOverrideFunction = true;
			EventNode->AllocateDefaultPins();
			Node = EventNode;
		}
		break;
	}

	case EBlueprintNodeType::Event_Custom:
	{
		// For custom events, check if it already exists by name
		FName CustomEventName = FName(*NodeData.EventName);
		for (UEdGraphNode* ExistingNode : Graph->Nodes)
		{
			if (UK2Node_CustomEvent* ExistingCustom = Cast<UK2Node_CustomEvent>(ExistingNode))
			{
				if (ExistingCustom->CustomFunctionName == CustomEventName)
				{
					Result.bSuccess = true;
					Result.Node = ExistingCustom;
					return Result;
				}
			}
		}

		// Create new custom event
		UK2Node_CustomEvent* CustomEvent = CreateNode<UK2Node_CustomEvent>(Graph);
		CustomEvent->CustomFunctionName = CustomEventName;
		CustomEvent->AllocateDefaultPins();
		Node = CustomEvent;
		break;
	}

	default:
		break;
	}

	if (Node)
	{
		SetNodePosition(Node, NodeData.Position);
		Result.bSuccess = true;
		Result.Node = Node;
	}
	else
	{
		Result.ErrorMessage = TEXT("Failed to create event node");
	}

	return Result;
}

FNodeSpawnResult UNodeSpawner::SpawnFunctionCallNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint)
{
	FNodeSpawnResult Result;

	UFunction* Function = nullptr;
	FString FunctionRef = NodeData.FunctionReference;

	// Check if it's already a full path
	if (FunctionRef.StartsWith(TEXT("/")))
	{
		Function = FindFunctionByPath(FunctionRef);
	}
	else
	{
		// Simple function name - try common libraries
		TArray<FString> LibrariesToTry = {
			TEXT("/Script/Engine.KismetSystemLibrary"),
			TEXT("/Script/Engine.KismetMathLibrary"),
			TEXT("/Script/Engine.KismetStringLibrary"),
			TEXT("/Script/Engine.KismetTextLibrary"),
			TEXT("/Script/Engine.KismetArrayLibrary"),
			TEXT("/Script/Engine.GameplayStatics"),
			TEXT("/Script/Engine.KismetMaterialLibrary"),
			TEXT("/Script/Engine.KismetRenderingLibrary"),
			TEXT("/Script/Engine.KismetInputLibrary"),
		};

		for (const FString& Library : LibrariesToTry)
		{
			FString FullPath = FString::Printf(TEXT("%s.%s"), *Library, *FunctionRef);
			Function = FindFunctionByPath(FullPath);
			if (Function)
			{
				UE_LOG(LogAssetFactoryAI, Log, TEXT("Resolved function '%s' to '%s'"), *FunctionRef, *FullPath);
				break;
			}
		}
	}

	if (!Function)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Function not found: %s"), *NodeData.FunctionReference);
		return Result;
	}

	UK2Node_CallFunction* FuncNode = CreateNode<UK2Node_CallFunction>(Graph);
	FuncNode->SetFromFunction(Function);
	FuncNode->AllocateDefaultPins();

	SetNodePosition(FuncNode, NodeData.Position);

	Result.bSuccess = true;
	Result.Node = FuncNode;

	return Result;
}

FNodeSpawnResult UNodeSpawner::SpawnFlowControlNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint)
{
	FNodeSpawnResult Result;

	UK2Node* Node = nullptr;

	switch (NodeData.NodeType)
	{
	case EBlueprintNodeType::Flow_Branch:
	{
		UK2Node_IfThenElse* BranchNode = CreateNode<UK2Node_IfThenElse>(Graph);
		BranchNode->AllocateDefaultPins();
		Node = BranchNode;
		break;
	}

	case EBlueprintNodeType::Flow_Sequence:
	{
		UK2Node_ExecutionSequence* SeqNode = CreateNode<UK2Node_ExecutionSequence>(Graph);
		SeqNode->AllocateDefaultPins();
		Node = SeqNode;
		break;
	}

	case EBlueprintNodeType::Flow_Delay:
	{
		// Delay is a latent function
		UFunction* DelayFunc = FindFunctionByPath(TEXT("/Script/Engine.KismetSystemLibrary.Delay"));
		if (DelayFunc)
		{
			UK2Node_CallFunction* DelayNode = CreateNode<UK2Node_CallFunction>(Graph);
			DelayNode->SetFromFunction(DelayFunc);
			DelayNode->AllocateDefaultPins();
			Node = DelayNode;
		}
		break;
	}

	case EBlueprintNodeType::Flow_DoOnce:
	{
		// DoOnce is typically a macro
		UFunction* DoOnceFunc = FindFunctionByPath(TEXT("/Script/Engine.KismetSystemLibrary.DoOnce"));
		if (DoOnceFunc)
		{
			UK2Node_CallFunction* DoOnceNode = CreateNode<UK2Node_CallFunction>(Graph);
			DoOnceNode->SetFromFunction(DoOnceFunc);
			DoOnceNode->AllocateDefaultPins();
			Node = DoOnceNode;
		}
		break;
	}

	case EBlueprintNodeType::Flow_ForLoop:
	{
		// ForLoop is a macro in the standard library
		UFunction* ForLoopFunc = FindFunctionByPath(TEXT("/Script/Engine.KismetSystemLibrary.ForLoop"));
		if (!ForLoopFunc)
		{
			// Try alternative path
			ForLoopFunc = FindFunctionByPath(TEXT("/Script/Engine.KismetMathLibrary.ForLoop"));
		}
		if (ForLoopFunc)
		{
			UK2Node_CallFunction* ForLoopNode = CreateNode<UK2Node_CallFunction>(Graph);
			ForLoopNode->SetFromFunction(ForLoopFunc);
			ForLoopNode->AllocateDefaultPins();
			Node = ForLoopNode;
		}
		else
		{
			Result.ErrorMessage = TEXT("ForLoop function not found - use ForLoopWithBreak macro instead");
			return Result;
		}
		break;
	}

	case EBlueprintNodeType::Flow_WhileLoop:
	{
		// WhileLoop is typically implemented as a macro
		UFunction* WhileFunc = FindFunctionByPath(TEXT("/Script/Engine.KismetSystemLibrary.WhileLoop"));
		if (WhileFunc)
		{
			UK2Node_CallFunction* WhileNode = CreateNode<UK2Node_CallFunction>(Graph);
			WhileNode->SetFromFunction(WhileFunc);
			WhileNode->AllocateDefaultPins();
			Node = WhileNode;
		}
		else
		{
			Result.ErrorMessage = TEXT("WhileLoop not directly available - implement using Branch in a loop");
			return Result;
		}
		break;
	}

	default:
		Result.ErrorMessage = TEXT("Flow control node type not yet implemented");
		return Result;
	}

	if (Node)
	{
		SetNodePosition(Node, NodeData.Position);
		Result.bSuccess = true;
		Result.Node = Node;
	}
	else
	{
		Result.ErrorMessage = TEXT("Failed to create flow control node");
	}

	return Result;
}

FNodeSpawnResult UNodeSpawner::SpawnVariableNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint)
{
	FNodeSpawnResult Result;

	FName VarName(*NodeData.VariableName);

	// Check if variable exists
	FProperty* Property = FindFProperty<FProperty>(Blueprint->GeneratedClass, VarName);
	if (!Property)
	{
		// Try to find in new variables
		bool bFound = false;
		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			if (Var.VarName == VarName)
			{
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			Result.ErrorMessage = FString::Printf(TEXT("Variable not found: %s"), *NodeData.VariableName);
			return Result;
		}
	}

	UK2Node* Node = nullptr;

	if (NodeData.NodeType == EBlueprintNodeType::Variable_Get || NodeData.NodeType == EBlueprintNodeType::Variable_GetLocal)
	{
		UK2Node_VariableGet* GetNode = CreateNode<UK2Node_VariableGet>(Graph);
		GetNode->VariableReference.SetSelfMember(VarName);
		GetNode->AllocateDefaultPins();
		Node = GetNode;
	}
	else
	{
		UK2Node_VariableSet* SetNode = CreateNode<UK2Node_VariableSet>(Graph);
		SetNode->VariableReference.SetSelfMember(VarName);
		SetNode->AllocateDefaultPins();
		Node = SetNode;
	}

	if (Node)
	{
		SetNodePosition(Node, NodeData.Position);
		Result.bSuccess = true;
		Result.Node = Node;
	}
	else
	{
		Result.ErrorMessage = TEXT("Failed to create variable node");
	}

	return Result;
}

FNodeSpawnResult UNodeSpawner::SpawnMathNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint)
{
	FNodeSpawnResult Result;

	FString FunctionName;

	switch (NodeData.NodeType)
	{
	case EBlueprintNodeType::Math_Add:
		FunctionName = TEXT("Add");
		break;
	case EBlueprintNodeType::Math_Subtract:
		FunctionName = TEXT("Subtract");
		break;
	case EBlueprintNodeType::Math_Multiply:
		FunctionName = TEXT("Multiply");
		break;
	case EBlueprintNodeType::Math_Divide:
		FunctionName = TEXT("Divide");
		break;
	default:
		Result.ErrorMessage = TEXT("Unknown math operation");
		return Result;
	}

	// Use specific operand type if provided by compiler, otherwise fall back to default order
	UFunction* Function = nullptr;

	if (!NodeData.OperandType.IsEmpty())
	{
		// Use the operand type specified by the compiler
		FString SpecificPath = FString::Printf(TEXT("/Script/Engine.KismetMathLibrary.%s_%s"), *FunctionName, *NodeData.OperandType);
		Function = FindFunctionByPath(SpecificPath);
	}

	if (!Function)
	{
		// Fall back to trying common types (Double first for UE5)
		TArray<FString> FunctionPaths = {
			FString::Printf(TEXT("/Script/Engine.KismetMathLibrary.%s_DoubleDouble"), *FunctionName),
			FString::Printf(TEXT("/Script/Engine.KismetMathLibrary.%s_FloatFloat"), *FunctionName),
			FString::Printf(TEXT("/Script/Engine.KismetMathLibrary.%s_IntInt"), *FunctionName)
		};

		for (const FString& Path : FunctionPaths)
		{
			Function = FindFunctionByPath(Path);
			if (Function)
			{
				break;
			}
		}
	}

	if (!Function)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Math function not found for: %s"), *FunctionName);
		return Result;
	}

	UK2Node_CallFunction* FuncNode = CreateNode<UK2Node_CallFunction>(Graph);
	FuncNode->SetFromFunction(Function);
	FuncNode->AllocateDefaultPins();

	SetNodePosition(FuncNode, NodeData.Position);

	Result.bSuccess = true;
	Result.Node = FuncNode;

	return Result;
}

FNodeSpawnResult UNodeSpawner::SpawnComparisonNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint)
{
	FNodeSpawnResult Result;

	FString FunctionName;

	switch (NodeData.NodeType)
	{
	case EBlueprintNodeType::Compare_Equal:
		FunctionName = TEXT("EqualEqual");
		break;
	case EBlueprintNodeType::Compare_NotEqual:
		FunctionName = TEXT("NotEqual");
		break;
	case EBlueprintNodeType::Compare_Greater:
		FunctionName = TEXT("Greater");
		break;
	case EBlueprintNodeType::Compare_Less:
		FunctionName = TEXT("Less");
		break;
	case EBlueprintNodeType::Compare_GreaterEqual:
		FunctionName = TEXT("GreaterEqual");
		break;
	case EBlueprintNodeType::Compare_LessEqual:
		FunctionName = TEXT("LessEqual");
		break;
	default:
		Result.ErrorMessage = TEXT("Unknown comparison operation");
		return Result;
	}

	// Use specific operand type if provided by compiler, otherwise fall back to default order
	UFunction* Function = nullptr;
	FString TriedPaths;

	if (!NodeData.OperandType.IsEmpty())
	{
		// Use the operand type specified by the compiler
		FString SpecificPath = FString::Printf(TEXT("/Script/Engine.KismetMathLibrary.%s_%s"), *FunctionName, *NodeData.OperandType);
		Function = FindFunctionByPath(SpecificPath);
		if (!Function)
		{
			TriedPaths = SpecificPath;
		}
	}

	if (!Function)
	{
		// Fall back to trying common types
		TArray<FString> FunctionPaths = {
			FString::Printf(TEXT("/Script/Engine.KismetMathLibrary.%s_IntInt"), *FunctionName),
			FString::Printf(TEXT("/Script/Engine.KismetMathLibrary.%s_DoubleDouble"), *FunctionName),
			FString::Printf(TEXT("/Script/Engine.KismetMathLibrary.%s_FloatFloat"), *FunctionName)
		};

		for (const FString& Path : FunctionPaths)
		{
			Function = FindFunctionByPath(Path);
			if (Function)
			{
				break;
			}
			TriedPaths += Path + TEXT(", ");
		}
	}

	if (!Function)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Comparison function not found. Tried: %s"), *TriedPaths);
		return Result;
	}

	UK2Node_CallFunction* FuncNode = CreateNode<UK2Node_CallFunction>(Graph);
	FuncNode->SetFromFunction(Function);
	FuncNode->AllocateDefaultPins();

	SetNodePosition(FuncNode, NodeData.Position);

	Result.bSuccess = true;
	Result.Node = FuncNode;

	return Result;
}

FNodeSpawnResult UNodeSpawner::SpawnLogicNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint)
{
	FNodeSpawnResult Result;

	FString FunctionPath;

	switch (NodeData.NodeType)
	{
	case EBlueprintNodeType::Logic_And:
		FunctionPath = TEXT("/Script/Engine.KismetMathLibrary.BooleanAND");
		break;
	case EBlueprintNodeType::Logic_Or:
		FunctionPath = TEXT("/Script/Engine.KismetMathLibrary.BooleanOR");
		break;
	case EBlueprintNodeType::Logic_Not:
		FunctionPath = TEXT("/Script/Engine.KismetMathLibrary.Not_PreBool");
		break;
	default:
		Result.ErrorMessage = TEXT("Unknown logic operation");
		return Result;
	}

	UFunction* Function = FindFunctionByPath(FunctionPath);
	if (!Function)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Logic function not found: %s"), *FunctionPath);
		return Result;
	}

	UK2Node_CallFunction* FuncNode = CreateNode<UK2Node_CallFunction>(Graph);
	FuncNode->SetFromFunction(Function);
	FuncNode->AllocateDefaultPins();

	SetNodePosition(FuncNode, NodeData.Position);

	Result.bSuccess = true;
	Result.Node = FuncNode;

	return Result;
}

FNodeSpawnResult UNodeSpawner::SpawnCastNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint)
{
	FNodeSpawnResult Result;

	UClass* TargetClass = FindClassByPath(NodeData.TargetClass);
	if (!TargetClass)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Target class not found: %s"), *NodeData.TargetClass);
		return Result;
	}

	UK2Node_DynamicCast* CastNode = CreateNode<UK2Node_DynamicCast>(Graph);
	CastNode->TargetType = TargetClass;
	CastNode->AllocateDefaultPins();

	SetNodePosition(CastNode, NodeData.Position);

	Result.bSuccess = true;
	Result.Node = CastNode;

	return Result;
}

FNodeSpawnResult UNodeSpawner::SpawnArrayNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint)
{
	FNodeSpawnResult Result;

	FString FunctionPath;

	switch (NodeData.NodeType)
	{
	case EBlueprintNodeType::Array_Add:
		FunctionPath = TEXT("/Script/Engine.KismetArrayLibrary.Array_Add");
		break;
	case EBlueprintNodeType::Array_Remove:
		FunctionPath = TEXT("/Script/Engine.KismetArrayLibrary.Array_RemoveItem");
		break;
	case EBlueprintNodeType::Array_Get:
		FunctionPath = TEXT("/Script/Engine.KismetArrayLibrary.Array_Get");
		break;
	case EBlueprintNodeType::Array_Set:
		FunctionPath = TEXT("/Script/Engine.KismetArrayLibrary.Array_Set");
		break;
	case EBlueprintNodeType::Array_Length:
		FunctionPath = TEXT("/Script/Engine.KismetArrayLibrary.Array_Length");
		break;
	case EBlueprintNodeType::Array_Clear:
		FunctionPath = TEXT("/Script/Engine.KismetArrayLibrary.Array_Clear");
		break;
	default:
		Result.ErrorMessage = TEXT("Unknown array operation");
		return Result;
	}

	UFunction* Function = FindFunctionByPath(FunctionPath);
	if (!Function)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Array function not found: %s"), *FunctionPath);
		return Result;
	}

	UK2Node_CallFunction* FuncNode = CreateNode<UK2Node_CallFunction>(Graph);
	FuncNode->SetFromFunction(Function);
	FuncNode->AllocateDefaultPins();

	SetNodePosition(FuncNode, NodeData.Position);

	Result.bSuccess = true;
	Result.Node = FuncNode;

	return Result;
}

UFunction* UNodeSpawner::FindFunctionByPath(const FString& FunctionPath)
{
	// Path format: /Script/ModuleName.ClassName.FunctionName
	// or: /Script/ModuleName.ClassName:FunctionName

	FString Path = FunctionPath;

	// Try to find directly as a function member reference
	int32 LastDotIndex;
	if (Path.FindLastChar('.', LastDotIndex))
	{
		FString ClassPath = Path.Left(LastDotIndex);
		FString FunctionName = Path.Mid(LastDotIndex + 1);

		// Find the class
		UClass* Class = FindClassByPath(ClassPath);
		if (Class)
		{
			return Class->FindFunctionByName(FName(*FunctionName));
		}
	}

	// Try colon separator
	int32 ColonIndex;
	if (Path.FindLastChar(':', ColonIndex))
	{
		FString ClassPath = Path.Left(ColonIndex);
		FString FunctionName = Path.Mid(ColonIndex + 1);

		UClass* Class = FindClassByPath(ClassPath);
		if (Class)
		{
			return Class->FindFunctionByName(FName(*FunctionName));
		}
	}

	UE_LOG(LogNodeSpawner, Warning, TEXT("Could not find function: %s"), *FunctionPath);
	return nullptr;
}

UClass* UNodeSpawner::FindClassByPath(const FString& ClassPath)
{
	// Try to find by path
	UClass* Class = FindObject<UClass>(nullptr, *ClassPath);

	if (!Class)
	{
		// Try loading
		Class = LoadObject<UClass>(nullptr, *ClassPath);
	}

	if (!Class)
	{
		// Try common variations
		FString ModifiedPath = ClassPath;

		// Handle blueprint classes
		if (!ModifiedPath.StartsWith(TEXT("/Script/")))
		{
			// Try as blueprint class
			ModifiedPath = ClassPath + TEXT("_C");
			Class = LoadObject<UClass>(nullptr, *ModifiedPath);
		}
	}

	return Class;
}

void UNodeSpawner::SetNodePosition(UK2Node* Node, const FNodePosition& Position)
{
	if (Node)
	{
		Node->NodePosX = static_cast<int32>(Position.X);
		Node->NodePosY = static_cast<int32>(Position.Y);
	}
}

FNodeSpawnResult UNodeSpawner::SpawnReturnNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint)
{
	FNodeSpawnResult Result;

	// Check if this is a function graph (has function entry)
	UK2Node_FunctionEntry* EntryNode = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		if (EntryNode)
		{
			break;
		}
	}

	if (!EntryNode)
	{
		// Not a function graph, return nodes don't apply
		Result.ErrorMessage = TEXT("Return node can only be used in function graphs");
		return Result;
	}

	// Check if there's already a result node
	UK2Node_FunctionResult* ResultNode = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		ResultNode = Cast<UK2Node_FunctionResult>(Node);
		if (ResultNode)
		{
			break;
		}
	}

	if (!ResultNode)
	{
		// Create new result node
		ResultNode = CreateNode<UK2Node_FunctionResult>(Graph);
		ResultNode->AllocateDefaultPins();
	}

	SetNodePosition(ResultNode, NodeData.Position);

	Result.bSuccess = true;
	Result.Node = ResultNode;

	return Result;
}
