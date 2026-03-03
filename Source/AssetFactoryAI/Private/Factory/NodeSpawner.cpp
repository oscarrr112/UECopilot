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
#include "K2Node_MakeStruct.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "K2Node_MakeArray.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_FunctionEntry.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UObjectIterator.h"
#include "DynamicConfigUtils.h"

DEFINE_LOG_CATEGORY(LogNodeSpawner);

namespace DC = AssetFactoryAI::DynamicConfig;

namespace
{
	using FSpawnHandler = FNodeSpawnResult(*)(UEdGraph*, const FBlueprintNodeData&, UBlueprint*);
	using FSimpleFlowNodeFactory = UK2Node*(*)(UEdGraph*);

	TArray<FString> ParseCSV(const FString& Input)
	{
		TArray<FString> Values;
		Input.ParseIntoArray(Values, TEXT(","), true);
		for (FString& Value : Values)
		{
			Value.TrimStartAndEndInline();
		}
		Values.RemoveAll([](const FString& Value) { return Value.IsEmpty(); });
		return Values;
	}

	FString GetDynamicError(const TCHAR* Key, const TCHAR* DefaultMessage)
	{
		return DC::GetString(Key, DefaultMessage);
	}

	const TMap<EBlueprintNodeType, FString>& GetMathOperationNames()
	{
		static const TMap<EBlueprintNodeType, FString> Names = {
			{ EBlueprintNodeType::Math_Add, TEXT("Add") },
			{ EBlueprintNodeType::Math_Subtract, TEXT("Subtract") },
			{ EBlueprintNodeType::Math_Multiply, TEXT("Multiply") },
			{ EBlueprintNodeType::Math_Divide, TEXT("Divide") },
		};
		return Names;
	}

	const TMap<EBlueprintNodeType, FString>& GetComparisonOperationNames()
	{
		static const TMap<EBlueprintNodeType, FString> Names = {
			{ EBlueprintNodeType::Compare_Equal, TEXT("EqualEqual") },
			{ EBlueprintNodeType::Compare_NotEqual, TEXT("NotEqual") },
			{ EBlueprintNodeType::Compare_Greater, TEXT("Greater") },
			{ EBlueprintNodeType::Compare_Less, TEXT("Less") },
			{ EBlueprintNodeType::Compare_GreaterEqual, TEXT("GreaterEqual") },
			{ EBlueprintNodeType::Compare_LessEqual, TEXT("LessEqual") },
		};
		return Names;
	}

	const TMap<EBlueprintNodeType, FString>& GetLogicFunctionPaths()
	{
		static const TMap<EBlueprintNodeType, FString> Paths = {
			{ EBlueprintNodeType::Logic_And, TEXT("/Script/Engine.KismetMathLibrary.BooleanAND") },
			{ EBlueprintNodeType::Logic_Or, TEXT("/Script/Engine.KismetMathLibrary.BooleanOR") },
			{ EBlueprintNodeType::Logic_Not, TEXT("/Script/Engine.KismetMathLibrary.Not_PreBool") },
		};
		return Paths;
	}

	const TMap<EBlueprintNodeType, FString>& GetArrayFunctionPaths()
	{
		static const TMap<EBlueprintNodeType, FString> Paths = {
			{ EBlueprintNodeType::Array_Add, TEXT("/Script/Engine.KismetArrayLibrary.Array_Add") },
			{ EBlueprintNodeType::Array_Remove, TEXT("/Script/Engine.KismetArrayLibrary.Array_RemoveItem") },
			{ EBlueprintNodeType::Array_Get, TEXT("/Script/Engine.KismetArrayLibrary.Array_Get") },
			{ EBlueprintNodeType::Array_Set, TEXT("/Script/Engine.KismetArrayLibrary.Array_Set") },
			{ EBlueprintNodeType::Array_Length, TEXT("/Script/Engine.KismetArrayLibrary.Array_Length") },
			{ EBlueprintNodeType::Array_Clear, TEXT("/Script/Engine.KismetArrayLibrary.Array_Clear") },
		};
		return Paths;
	}

	const TArray<FString>& GetDefaultNumericSuffixes()
	{
		static const TArray<FString> Suffixes = {
			TEXT("IntInt"),
			TEXT("DoubleDouble"),
			TEXT("FloatFloat")
		};
		return Suffixes;
	}

	const TArray<FString>& GetDefaultFunctionLibraries()
	{
		static const TArray<FString> Libraries = []()
		{
			const FString Raw = DC::GetString(
				TEXT("NodeSpawner.FunctionLibraries"),
				TEXT("/Script/Engine.KismetSystemLibrary,/Script/Engine.KismetMathLibrary,/Script/Engine.KismetStringLibrary,/Script/Engine.KismetTextLibrary,/Script/Engine.KismetArrayLibrary,/Script/Engine.GameplayStatics,/Script/Engine.KismetMaterialLibrary,/Script/Engine.KismetRenderingLibrary,/Script/Engine.KismetInputLibrary"));
			return ParseCSV(Raw);
		}();
		return Libraries;
	}

	const TMap<EBlueprintNodeType, FName>& GetActorEventNames()
	{
		static const TMap<EBlueprintNodeType, FName> EventNames = []()
		{
			TMap<EBlueprintNodeType, FName> Mapped;
			Mapped.Add(EBlueprintNodeType::Event_BeginPlay, FName(*DC::GetString(TEXT("NodeSpawner.Event.BeginPlay"), TEXT("ReceiveBeginPlay"))));
			Mapped.Add(EBlueprintNodeType::Event_Tick, FName(*DC::GetString(TEXT("NodeSpawner.Event.Tick"), TEXT("ReceiveTick"))));
			return Mapped;
		}();
		return EventNames;
	}

	const TMap<EBlueprintNodeType, TArray<FString>>& GetFlowFunctionCandidates()
	{
		static const TMap<EBlueprintNodeType, TArray<FString>> Candidates = []()
		{
			TMap<EBlueprintNodeType, TArray<FString>> Mapped;
			Mapped.Add(EBlueprintNodeType::Flow_Delay, ParseCSV(DC::GetString(TEXT("NodeSpawner.Flow.DelayCandidates"), TEXT("/Script/Engine.KismetSystemLibrary.Delay"))));
			Mapped.Add(EBlueprintNodeType::Flow_DoOnce, ParseCSV(DC::GetString(TEXT("NodeSpawner.Flow.DoOnceCandidates"), TEXT("/Script/Engine.KismetSystemLibrary.DoOnce"))));
			Mapped.Add(EBlueprintNodeType::Flow_ForLoop, ParseCSV(DC::GetString(TEXT("NodeSpawner.Flow.ForLoopCandidates"), TEXT("/Script/Engine.KismetSystemLibrary.ForLoop,/Script/Engine.KismetMathLibrary.ForLoop"))));
			Mapped.Add(EBlueprintNodeType::Flow_WhileLoop, ParseCSV(DC::GetString(TEXT("NodeSpawner.Flow.WhileLoopCandidates"), TEXT("/Script/Engine.KismetSystemLibrary.WhileLoop"))));
			return Mapped;
		}();
		return Candidates;
	}

	UK2Node* CreateBranchFlowNode(UEdGraph* Graph)
	{
		FGraphNodeCreator<UK2Node_IfThenElse> NodeCreator(*Graph);
		UK2Node_IfThenElse* BranchNode = NodeCreator.CreateNode();
		BranchNode->AllocateDefaultPins();
		NodeCreator.Finalize();
		return BranchNode;
	}

	UK2Node* CreateSequenceFlowNode(UEdGraph* Graph)
	{
		FGraphNodeCreator<UK2Node_ExecutionSequence> NodeCreator(*Graph);
		UK2Node_ExecutionSequence* SequenceNode = NodeCreator.CreateNode();
		SequenceNode->AllocateDefaultPins();
		NodeCreator.Finalize();
		return SequenceNode;
	}

	UK2Node* CreateSwitchIntegerFlowNode(UEdGraph* Graph)
	{
		FGraphNodeCreator<UK2Node_SwitchInteger> NodeCreator(*Graph);
		UK2Node_SwitchInteger* SwitchNode = NodeCreator.CreateNode();
		SwitchNode->AllocateDefaultPins();
		NodeCreator.Finalize();
		return SwitchNode;
	}

	const TMap<EBlueprintNodeType, FSimpleFlowNodeFactory>& GetSimpleFlowNodeFactories()
	{
		static const TMap<EBlueprintNodeType, FSimpleFlowNodeFactory> Factories = {
			{ EBlueprintNodeType::Flow_Branch, &CreateBranchFlowNode },
			{ EBlueprintNodeType::Flow_Sequence, &CreateSequenceFlowNode },
			{ EBlueprintNodeType::Flow_Switch, &CreateSwitchIntegerFlowNode },
		};
		return Factories;
	}

	const TMap<EBlueprintNodeType, FString>& GetFlowFallbackErrors()
	{
		static const TMap<EBlueprintNodeType, FString> Errors = []()
		{
			TMap<EBlueprintNodeType, FString> Mapped;
			Mapped.Add(EBlueprintNodeType::Flow_ForLoop, GetDynamicError(TEXT("NodeSpawner.Error.ForLoopNotFound"), TEXT("ForLoop function not found - use ForLoopWithBreak macro instead")));
			Mapped.Add(EBlueprintNodeType::Flow_WhileLoop, GetDynamicError(TEXT("NodeSpawner.Error.WhileLoopNotFound"), TEXT("WhileLoop not directly available - implement using Branch in a loop")));
			return Mapped;
		}();
		return Errors;
	}

	// Map flow control types to StandardMacros macro graph names
	const TMap<EBlueprintNodeType, FString>& GetFlowMacroNames()
	{
		static const TMap<EBlueprintNodeType, FString> Names = {
			{ EBlueprintNodeType::Flow_ForLoop, TEXT("ForLoop") },
			{ EBlueprintNodeType::Flow_ForEachLoop, TEXT("ForEachLoop") },
			{ EBlueprintNodeType::Flow_WhileLoop, TEXT("WhileLoop") },
		};
		return Names;
	}

	UK2Node* TryCreateMacroInstance(UEdGraph* Graph, EBlueprintNodeType NodeType)
	{
		const FString* MacroName = GetFlowMacroNames().Find(NodeType);
		if (!MacroName)
		{
			return nullptr;
		}

		// Load StandardMacros blueprint (cached by engine after first load)
		// UE 5.7+: /Engine/EditorBlueprintResources/StandardMacros
		// UE 5.x:  /Engine/EditorResources/StandardMacros (older versions)
		static const TCHAR* MacroLibPath = TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros");
		UBlueprint* MacroLib = LoadObject<UBlueprint>(nullptr, MacroLibPath);
		if (!MacroLib)
		{
			UE_LOG(LogAssetFactoryAI, Warning, TEXT("Failed to load StandardMacros blueprint"));
			return nullptr;
		}

		// Find the macro graph by name
		UEdGraph* MacroGraph = nullptr;
		for (UEdGraph* MG : MacroLib->MacroGraphs)
		{
			if (MG && MG->GetName() == *MacroName)
			{
				MacroGraph = MG;
				break;
			}
		}

		if (!MacroGraph)
		{
			UE_LOG(LogAssetFactoryAI, Warning, TEXT("Macro '%s' not found in StandardMacros"), **MacroName);
			return nullptr;
		}

		// Create the macro instance node
		FGraphNodeCreator<UK2Node_MacroInstance> NodeCreator(*Graph);
		UK2Node_MacroInstance* MacroNode = NodeCreator.CreateNode();
		MacroNode->SetMacroGraph(MacroGraph);
		MacroNode->AllocateDefaultPins();
		NodeCreator.Finalize();

		return MacroNode;
	}

	void RegisterManyHandlers(TMap<EBlueprintNodeType, FSpawnHandler>& InHandlers, const TArray<EBlueprintNodeType>& Types, FSpawnHandler Handler)
	{
		for (EBlueprintNodeType Type : Types)
		{
			InHandlers.Add(Type, Handler);
		}
	}

	const TMap<EBlueprintNodeType, FSpawnHandler>& GetNodeSpawnHandlers()
	{
		static TMap<EBlueprintNodeType, FSpawnHandler> Handlers;
		if (Handlers.Num() == 0)
		{
			RegisterManyHandlers(Handlers, {
				EBlueprintNodeType::Event_BeginPlay,
				EBlueprintNodeType::Event_Tick,
				EBlueprintNodeType::Event_Custom,
				EBlueprintNodeType::Event_Input,
			}, &UNodeSpawner::SpawnEventNode);

			RegisterManyHandlers(Handlers, {
				EBlueprintNodeType::Flow_Branch,
				EBlueprintNodeType::Flow_Sequence,
				EBlueprintNodeType::Flow_ForLoop,
				EBlueprintNodeType::Flow_ForEachLoop,
				EBlueprintNodeType::Flow_WhileLoop,
				EBlueprintNodeType::Flow_DoOnce,
				EBlueprintNodeType::Flow_Gate,
				EBlueprintNodeType::Flow_Delay,
				EBlueprintNodeType::Flow_Switch,
			}, &UNodeSpawner::SpawnFlowControlNode);

			RegisterManyHandlers(Handlers, {
				EBlueprintNodeType::CallFunction,
				EBlueprintNodeType::PureFunction,
			}, &UNodeSpawner::SpawnFunctionCallNode);

			RegisterManyHandlers(Handlers, {
				EBlueprintNodeType::Variable_Get,
				EBlueprintNodeType::Variable_Set,
				EBlueprintNodeType::Variable_GetLocal,
				EBlueprintNodeType::Variable_SetLocal,
			}, &UNodeSpawner::SpawnVariableNode);

			RegisterManyHandlers(Handlers, {
				EBlueprintNodeType::Math_Add,
				EBlueprintNodeType::Math_Subtract,
				EBlueprintNodeType::Math_Multiply,
				EBlueprintNodeType::Math_Divide,
			}, &UNodeSpawner::SpawnMathNode);

			RegisterManyHandlers(Handlers, {
				EBlueprintNodeType::Compare_Equal,
				EBlueprintNodeType::Compare_NotEqual,
				EBlueprintNodeType::Compare_Greater,
				EBlueprintNodeType::Compare_Less,
				EBlueprintNodeType::Compare_GreaterEqual,
				EBlueprintNodeType::Compare_LessEqual,
			}, &UNodeSpawner::SpawnComparisonNode);

			RegisterManyHandlers(Handlers, {
				EBlueprintNodeType::Logic_And,
				EBlueprintNodeType::Logic_Or,
				EBlueprintNodeType::Logic_Not,
			}, &UNodeSpawner::SpawnLogicNode);

			Handlers.Add(EBlueprintNodeType::Cast, &UNodeSpawner::SpawnCastNode);
			Handlers.Add(EBlueprintNodeType::MakeStruct, &UNodeSpawner::SpawnMakeStructNode);

			RegisterManyHandlers(Handlers, {
				EBlueprintNodeType::Array_Add,
				EBlueprintNodeType::Array_Remove,
				EBlueprintNodeType::Array_Get,
				EBlueprintNodeType::Array_Set,
				EBlueprintNodeType::Array_Length,
				EBlueprintNodeType::Array_Clear,
			}, &UNodeSpawner::SpawnArrayNode);

			Handlers.Add(EBlueprintNodeType::Return, &UNodeSpawner::SpawnReturnNode);
		}

		return Handlers;
	}
}

FNodeSpawnResult UNodeSpawner::SpawnNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint)
{
	FNodeSpawnResult Result;

	if (!Graph || !Blueprint)
	{
		Result.ErrorMessage = GetDynamicError(TEXT("NodeSpawner.Error.InvalidGraphOrBlueprint"), TEXT("Invalid graph or blueprint"));
		return Result;
	}

	const TMap<EBlueprintNodeType, FSpawnHandler>& Handlers = GetNodeSpawnHandlers();
	if (const FSpawnHandler* Handler = Handlers.Find(NodeData.NodeType))
	{
		return (*Handler)(Graph, NodeData, Blueprint);
	}

	Result.ErrorMessage = FString::Printf(TEXT("Unsupported node type: %d"), static_cast<int32>(NodeData.NodeType));
	return Result;
}

FNodeSpawnResult UNodeSpawner::SpawnEventNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint)
{
	FNodeSpawnResult Result;

	UK2Node* Node = nullptr;
	FName EventFunctionName;

	if (const FName* BuiltInEventName = GetActorEventNames().Find(NodeData.NodeType))
	{
		EventFunctionName = *BuiltInEventName;
	}
	else if (NodeData.NodeType != EBlueprintNodeType::Event_Custom)
	{
		Result.ErrorMessage = GetDynamicError(TEXT("NodeSpawner.Error.UnknownEventType"), TEXT("Unknown event type"));
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
	case EBlueprintNodeType::Event_Tick:
	{
		UFunction* EventFunc = AActor::StaticClass()->FindFunctionByName(EventFunctionName);
		if (EventFunc)
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
		Result.ErrorMessage = GetDynamicError(TEXT("NodeSpawner.Error.FailedCreateEventNode"), TEXT("Failed to create event node"));
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
		// Simple function name - try configured default libraries
		for (const FString& Library : GetDefaultFunctionLibraries())
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
	if (const FSimpleFlowNodeFactory* NodeFactory = GetSimpleFlowNodeFactories().Find(NodeData.NodeType))
	{
		Node = (*NodeFactory)(Graph);
	}
	else
	{
		// Try function path candidates first
		if (const TArray<FString>* CandidatePaths = GetFlowFunctionCandidates().Find(NodeData.NodeType))
		{
			for (const FString& Path : *CandidatePaths)
			{
				UFunction* ResolvedFunction = FindFunctionByPath(Path);
				if (ResolvedFunction)
				{
					UK2Node_CallFunction* FlowNode = CreateNode<UK2Node_CallFunction>(Graph);
					FlowNode->SetFromFunction(ResolvedFunction);
					FlowNode->AllocateDefaultPins();
					Node = FlowNode;
					break;
				}
			}
		}

		// If function not found, try StandardMacros (ForLoop, WhileLoop, ForEachLoop are macros in UE5)
		if (!Node)
		{
			Node = TryCreateMacroInstance(Graph, NodeData.NodeType);
		}

		if (!Node)
		{
			if (const FString* FallbackMessage = GetFlowFallbackErrors().Find(NodeData.NodeType))
			{
				Result.ErrorMessage = *FallbackMessage;
			}
			else
			{
				Result.ErrorMessage = GetDynamicError(TEXT("NodeSpawner.Error.FlowNodeTypeNotImplemented"), TEXT("Flow control node type not yet implemented"));
			}
			return Result;
		}
	}

	if (Node)
	{
		SetNodePosition(Node, NodeData.Position);
		Result.bSuccess = true;
		Result.Node = Node;
	}
	else
	{
		Result.ErrorMessage = GetDynamicError(TEXT("NodeSpawner.Error.FailedCreateFlowNode"), TEXT("Failed to create flow control node"));
	}

	return Result;
}

namespace
{
	FString InferVariableName(const FString& NodeId)
	{
		if (NodeId.IsEmpty())
		{
			return FString();
		}

		FString Lower = NodeId.ToLower();
		const TArray<FString> Prefixes = { TEXT("get_"), TEXT("set_"), TEXT("get"), TEXT("set") };

		for (const FString& Prefix : Prefixes)
		{
			if (Lower.StartsWith(Prefix) && NodeId.Len() > Prefix.Len())
			{
				return NodeId.Mid(Prefix.Len());
			}
		}

		return NodeId;
	}
}

FNodeSpawnResult UNodeSpawner::SpawnVariableNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint)
{
	FNodeSpawnResult Result;

	FString VariableName = NodeData.VariableName;
	if (VariableName.IsEmpty())
	{
		VariableName = InferVariableName(NodeData.NodeId);
	}

	if (VariableName.IsEmpty())
	{
		Result.ErrorMessage = GetDynamicError(TEXT("NodeSpawner.Error.VariableNameEmpty"), TEXT("Variable name is empty"));
		return Result;
	}

	FName VarName(*VariableName);

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
		Result.ErrorMessage = GetDynamicError(TEXT("NodeSpawner.Error.FailedCreateVariableNode"), TEXT("Failed to create variable node"));
	}

	return Result;
}

FNodeSpawnResult UNodeSpawner::SpawnMathNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint)
{
	FNodeSpawnResult Result;

	const FString* FunctionNamePtr = GetMathOperationNames().Find(NodeData.NodeType);
	if (!FunctionNamePtr)
	{
		Result.ErrorMessage = GetDynamicError(TEXT("NodeSpawner.Error.UnknownMathOperation"), TEXT("Unknown math operation"));
		return Result;
	}
	const FString& FunctionName = *FunctionNamePtr;

	// Use specific operand type if provided by compiler, otherwise fall back to default order
	UFunction* Function = nullptr;

	auto TryFindMathFunction = [&](const FString& Suffix) -> UFunction*
	{
		FString Path = FString::Printf(TEXT("/Script/Engine.KismetMathLibrary.%s_%s"), *FunctionName, *Suffix);
		return FindFunctionByPath(Path);
	};

	if (!NodeData.OperandType.IsEmpty())
	{
		Function = TryFindMathFunction(NodeData.OperandType);
	}

	if (!Function)
	{
		// Prefer integer operations when no guidance provided; fall back to double/float
		for (const FString& Suffix : GetDefaultNumericSuffixes())
		{
			Function = TryFindMathFunction(Suffix);
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

	const FString* FunctionNamePtr = GetComparisonOperationNames().Find(NodeData.NodeType);
	if (!FunctionNamePtr)
	{
		Result.ErrorMessage = GetDynamicError(TEXT("NodeSpawner.Error.UnknownComparisonOperation"), TEXT("Unknown comparison operation"));
		return Result;
	}
	const FString& FunctionName = *FunctionNamePtr;

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
		for (const FString& Suffix : GetDefaultNumericSuffixes())
		{
			const FString Path = FString::Printf(TEXT("/Script/Engine.KismetMathLibrary.%s_%s"), *FunctionName, *Suffix);
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

	const FString* FunctionPathPtr = GetLogicFunctionPaths().Find(NodeData.NodeType);
	if (!FunctionPathPtr)
	{
		Result.ErrorMessage = GetDynamicError(TEXT("NodeSpawner.Error.UnknownLogicOperation"), TEXT("Unknown logic operation"));
		return Result;
	}
	const FString& FunctionPath = *FunctionPathPtr;

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

FNodeSpawnResult UNodeSpawner::SpawnMakeStructNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint)
{
	FNodeSpawnResult Result;

	// NodeData.VariableName holds the struct type name ("Vector", "Rotator", "Transform")
	if (NodeData.VariableName.IsEmpty())
	{
		Result.ErrorMessage = TEXT("MakeStruct: VariableName (struct type) is empty");
		return Result;
	}

	// Find the UScriptStruct by name, trying common UE paths
	UScriptStruct* StructType = nullptr;
	TArray<FString> CandidatePaths = {
		FString::Printf(TEXT("/Script/CoreUObject.%s"), *NodeData.VariableName),
		FString::Printf(TEXT("/Script/Engine.%s"), *NodeData.VariableName),
		NodeData.VariableName,
	};
	for (const FString& Path : CandidatePaths)
	{
		StructType = FindObject<UScriptStruct>(nullptr, *Path);
		if (StructType) break;
	}

	if (!StructType)
	{
		Result.ErrorMessage = FString::Printf(TEXT("MakeStruct: struct type '%s' not found"), *NodeData.VariableName);
		return Result;
	}

	FGraphNodeCreator<UK2Node_MakeStruct> NodeCreator(*Graph);
	UK2Node_MakeStruct* MakeNode = NodeCreator.CreateNode();
	MakeNode->StructType = StructType;
	MakeNode->AllocateDefaultPins();
	NodeCreator.Finalize();
	SetNodePosition(MakeNode, NodeData.Position);

	Result.bSuccess = true;
	Result.Node = MakeNode;
	return Result;
}

FNodeSpawnResult UNodeSpawner::SpawnArrayNode(UEdGraph* Graph, const FBlueprintNodeData& NodeData, UBlueprint* Blueprint)
{
	FNodeSpawnResult Result;

	const FString* FunctionPathPtr = GetArrayFunctionPaths().Find(NodeData.NodeType);
	if (!FunctionPathPtr)
	{
		Result.ErrorMessage = GetDynamicError(TEXT("NodeSpawner.Error.UnknownArrayOperation"), TEXT("Unknown array operation"));
		return Result;
	}
	const FString& FunctionPath = *FunctionPathPtr;

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
	const TCHAR Separators[] = { TEXT('.'), TEXT(':') };
	for (const TCHAR Separator : Separators)
	{
		int32 SeparatorIndex = INDEX_NONE;
		if (!FunctionPath.FindLastChar(Separator, SeparatorIndex))
		{
			continue;
		}

		const FString ClassPath = FunctionPath.Left(SeparatorIndex);
		const FString FunctionName = FunctionPath.Mid(SeparatorIndex + 1);
		if (UClass* Class = FindClassByPath(ClassPath))
		{
			if (UFunction* Function = Class->FindFunctionByName(FName(*FunctionName)))
			{
				return Function;
			}
		}
	}

	UE_LOG(LogNodeSpawner, Warning, TEXT("Could not find function: %s"), *FunctionPath);
	return nullptr;
}

UClass* UNodeSpawner::FindClassByPath(const FString& ClassPath)
{
	TArray<FString> Candidates;
	Candidates.Add(ClassPath);

	// Blueprint generated classes often need the _C suffix when not using /Script paths.
	if (!ClassPath.StartsWith(TEXT("/Script/")) && !ClassPath.EndsWith(TEXT("_C")))
	{
		Candidates.Add(ClassPath + TEXT("_C"));
	}

	TSet<FString> Seen;
	for (const FString& Candidate : Candidates)
	{
		if (Seen.Contains(Candidate))
		{
			continue;
		}
		Seen.Add(Candidate);

		if (UClass* FoundClass = FindObject<UClass>(nullptr, *Candidate))
		{
			return FoundClass;
		}

		if (UClass* LoadedClass = LoadObject<UClass>(nullptr, *Candidate))
		{
			return LoadedClass;
		}
	}

	return nullptr;
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
		Result.ErrorMessage = GetDynamicError(TEXT("NodeSpawner.Error.ReturnNodeOnlyInFunctionGraph"), TEXT("Return node can only be used in function graphs"));
		return Result;
	}

	// Find existing result node as a template for user-defined output pins.
	UK2Node_FunctionResult* TemplateResultNode = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		TemplateResultNode = Cast<UK2Node_FunctionResult>(Node);
		if (TemplateResultNode)
		{
			break;
		}
	}

	UK2Node_FunctionResult* ResultNode = nullptr;
	const bool bUseExistingNode = NodeData.NodeId.Equals(TEXT("fn_result"), ESearchCase::IgnoreCase);
	if (bUseExistingNode)
	{
		ResultNode = TemplateResultNode;
	}

	if (!ResultNode)
	{
		// Create a dedicated result node for this return node.
		// This allows branching return paths (e.g., true/false each returning different values).
		ResultNode = CreateNode<UK2Node_FunctionResult>(Graph);
		ResultNode->AllocateDefaultPins();

		if (TemplateResultNode && TemplateResultNode->UserDefinedPins.Num() > 0)
		{
			ResultNode->UserDefinedPins = TemplateResultNode->UserDefinedPins;
			ResultNode->ReconstructNode();
		}
	}

	SetNodePosition(ResultNode, NodeData.Position);

	Result.bSuccess = true;
	Result.Node = ResultNode;

	return Result;
}
