// Copyright Epic Games, Inc. All Rights Reserved.

#include "BSL/BSLCompiler.h"

namespace BSL
{

FCompiler::FCompiler()
{
}

FCompileResult FCompiler::Compile(const FString& Source)
{
	FCompileResult Result;

	// Parse source
	FParser Parser(Source);
	FParseResult ParseResult = Parser.Parse();

	if (!ParseResult.bSuccess)
	{
		Result.Errors = ParseResult.Errors;
		Result.Warnings = ParseResult.Warnings;
		return Result;
	}

	// Compile AST
	return CompileAST(ParseResult.Blueprint);
}

FCompileResult FCompiler::CompileAST(const FBlueprint& Blueprint)
{
	FCompiler Compiler;
	FCompileResult Result;

	if (Compiler.CompileBlueprint(Blueprint, Result.BlueprintData))
	{
		Result.bSuccess = Compiler.Errors.Num() == 0;
	}

	Result.Errors = Compiler.Errors;
	Result.Warnings = Compiler.Warnings;
	return Result;
}

bool FCompiler::CompileBlueprint(const FBlueprint& Source, FBlueprintData& OutData)
{
	OutData.Name = Source.Name;
	OutData.ParentClass = Source.ParentClass;

	// Build variable type map first
	VariableTypeMap.Empty();
	for (const FVariable& Var : Source.Variables)
	{
		VariableTypeMap.Add(Var.Name, Var.Type.Type);
	}

	// Compile variables
	for (const FVariable& Var : Source.Variables)
	{
		OutData.Variables.Add(CompileVariable(Var));
	}

	// Create the default EventGraph - all events go here
	FBlueprintGraphData EventGraph;
	EventGraph.Name = TEXT("EventGraph");
	EventGraph.bIsFunction = false;

	// Compile events into the shared EventGraph, functions into separate graphs
	for (const FFunction& Func : Source.Functions)
	{
		CurrentFunction = &Func;
		VariableNodeMap.Empty();

		if (Func.bIsEvent)
		{
			// All events go into the same EventGraph
			CompileEvent(Func, EventGraph);
		}
		else
		{
			// Functions get their own graph
			FBlueprintGraphData FuncGraph;
			FuncGraph.Name = Func.Name;
			FuncGraph.bIsFunction = true;
			if (CompileFunction(Func, FuncGraph))
			{
				OutData.Functions.Add(FuncGraph);
			}
		}
	}

	// Add the EventGraph if it has any nodes
	if (EventGraph.Nodes.Num() > 0)
	{
		OutData.EventGraphs.Add(EventGraph);
	}

	return true;
}

FBlueprintVariableData FCompiler::CompileVariable(const FVariable& Var)
{
	FBlueprintVariableData Data;
	Data.Name = Var.Name;
	Data.Type = MapType(Var.Type.Type);
	Data.TypeClass = Var.Type.SubType;

	// Handle default value
	if (Var.DefaultValue.IsValid())
	{
		switch (Var.DefaultValue->Type)
		{
		case EExpressionType::Literal_Bool:
			Data.DefaultValue = Var.DefaultValue->BoolValue ? TEXT("true") : TEXT("false");
			break;
		case EExpressionType::Literal_Int:
			Data.DefaultValue = FString::FromInt(Var.DefaultValue->IntValue);
			break;
		case EExpressionType::Literal_Float:
			Data.DefaultValue = FString::SanitizeFloat(Var.DefaultValue->FloatValue);
			break;
		case EExpressionType::Literal_String:
			Data.DefaultValue = Var.DefaultValue->StringValue;
			break;
		default:
			// Complex expressions not supported for defaults
			Warning(FString::Printf(TEXT("Complex default value for '%s' not supported"), *Var.Name));
			break;
		}
	}

	return Data;
}

bool FCompiler::CompileEvent(const FFunction& Event, FBlueprintGraphData& OutGraph)
{
	// Create event node
	FBlueprintNodeData EventNode;
	EventNode.NodeId = GenerateNodeId(TEXT("event"));

	// Map common event names
	if (Event.Name == TEXT("BeginPlay"))
	{
		EventNode.NodeType = EBlueprintNodeType::Event_BeginPlay;
	}
	else if (Event.Name == TEXT("Tick"))
	{
		EventNode.NodeType = EBlueprintNodeType::Event_Tick;
	}
	else
	{
		EventNode.NodeType = EBlueprintNodeType::Event_Custom;
		EventNode.EventName = Event.Name;
	}

	EventNode.Position = {0.0f, 0.0f};
	OutGraph.Nodes.Add(EventNode);

	// Compile body
	FString LastNodeId = EventNode.NodeId;
	FString LastPinName = TEXT("then");

	CompileStatements(Event.Body, OutGraph.Nodes, LastNodeId, LastPinName);

	return true;
}

bool FCompiler::CompileFunction(const FFunction& Func, FBlueprintGraphData& OutGraph)
{
	// Clear output value map for this function
	OutputValueMap.Empty();

	// Set function inputs/outputs
	for (const FVariable& Input : Func.Inputs)
	{
		FBlueprintPinData Pin;
		Pin.Name = Input.Name;
		Pin.Type = Input.Type.ToString();
		Pin.Direction = EBlueprintPinDirection::Output;  // Inputs are output pins on entry node
		OutGraph.Inputs.Add(Pin);

		// Map input parameters to fn_entry node
		// When we reference parameter A, we get it from fn_entry.A
		VariableNodeMap.Add(Input.Name, TEXT("fn_entry"));
	}

	for (const FVariable& Output : Func.Outputs)
	{
		FBlueprintPinData Pin;
		Pin.Name = Output.Name;
		Pin.Type = Output.Type.ToString();
		Pin.Direction = EBlueprintPinDirection::Input;  // Outputs are input pins on result node
		OutGraph.Outputs.Add(Pin);
	}

	// Start execution from fn_entry's "then" pin
	FString LastNodeId = TEXT("fn_entry");
	FString LastPinName = TEXT("then");
	CompileStatements(Func.Body, OutGraph.Nodes, LastNodeId, LastPinName);

	// If we have output values assigned (and no explicit return was compiled),
	// create an implicit return node to connect them to fn_result
	if (OutputValueMap.Num() > 0 && !LastNodeId.IsEmpty())
	{
		FBlueprintNodeData ResultNode;
		ResultNode.NodeId = TEXT("fn_result");
		ResultNode.NodeType = EBlueprintNodeType::Return;
		ResultNode.Position = {800.0f, 0.0f};

		// Connect execution
		FBlueprintPinData ExecPin;
		ExecPin.Name = TEXT("execute");
		ExecPin.Direction = EBlueprintPinDirection::Input;
		FBlueprintPinConnection ExecConn;
		ExecConn.SourceNodeId = LastNodeId;
		ExecConn.SourcePinName = LastPinName;
		ExecPin.Connections.Add(ExecConn);
		ResultNode.Pins.Add(ExecPin);

		// Connect each output value
		for (const FVariable& Output : Func.Outputs)
		{
			if (TPair<FString, FString>* ValueInfo = OutputValueMap.Find(Output.Name))
			{
				FBlueprintPinData ValuePin;
				ValuePin.Name = Output.Name;
				ValuePin.Direction = EBlueprintPinDirection::Input;

				if (ValueInfo->Key == TEXT("__literal__"))
				{
					// Literal value - set as default
					ValuePin.DefaultValue = ValueInfo->Value;
				}
				else
				{
					// Connection to another node
					FBlueprintPinConnection Conn;
					Conn.SourceNodeId = ValueInfo->Key;
					Conn.SourcePinName = ValueInfo->Value;
					ValuePin.Connections.Add(Conn);
				}
				ResultNode.Pins.Add(ValuePin);
			}
		}

		OutGraph.Nodes.Add(ResultNode);
	}

	return true;
}

bool FCompiler::CompileStatements(
	const TArray<TSharedPtr<FStatement>>& Statements,
	TArray<FBlueprintNodeData>& OutNodes,
	const FString& EntryNodeId,
	const FString& EntryPinName)
{
	FString LastNodeId = EntryNodeId;
	FString LastPinName = EntryPinName;

	for (int32 i = 0; i < Statements.Num(); i++)
	{
		const TSharedPtr<FStatement>& Stmt = Statements[i];
		if (!Stmt) continue;

		// Check if this is an if statement with more statements after it
		// If so, we need a Sequence node to ensure subsequent code runs
		bool bNeedSequence = (Stmt->Type == EStatementType::If) && (i + 1 < Statements.Num());

		if (bNeedSequence)
		{
			// Create Sequence node
			FBlueprintNodeData SeqNode;
			SeqNode.NodeId = GenerateNodeId(TEXT("seq"));
			SeqNode.NodeType = EBlueprintNodeType::Flow_Sequence;
			SeqNode.Position = {350.0f, 0.0f};

			// Connect execute from previous node
			if (!LastNodeId.IsEmpty())
			{
				FBlueprintPinData ExecPin;
				ExecPin.Name = TEXT("execute");
				ExecPin.Direction = EBlueprintPinDirection::Input;
				FBlueprintPinConnection Conn;
				Conn.SourceNodeId = LastNodeId;
				Conn.SourcePinName = LastPinName;
				ExecPin.Connections.Add(Conn);
				SeqNode.Pins.Add(ExecPin);
			}

			OutNodes.Add(SeqNode);

			// Compile the if statement from Sequence's then_0 output
			FString IfEntryNodeId = SeqNode.NodeId;
			FString IfEntryPinName = TEXT("then_0");
			if (!CompileStatement(*Stmt, OutNodes, IfEntryNodeId, IfEntryPinName))
			{
				return false;
			}

			// Continue subsequent statements from Sequence's then_1 output
			LastNodeId = SeqNode.NodeId;
			LastPinName = TEXT("then_1");
		}
		else
		{
			if (!CompileStatement(*Stmt, OutNodes, LastNodeId, LastPinName))
			{
				return false;
			}
		}
	}

	return true;
}

bool FCompiler::CompileStatement(
	const FStatement& Stmt,
	TArray<FBlueprintNodeData>& OutNodes,
	FString& InOutLastExecNodeId,
	FString& InOutLastExecPinName)
{
	switch (Stmt.Type)
	{
	case EStatementType::VariableDecl:
	{
		// Local variable declaration - for now just record it
		// TODO: Create local variable in function
		Warning(FString::Printf(TEXT("Local variable '%s' - locals not fully supported yet"),
			*Stmt.DeclaredVariable.Name));
		return true;
	}

	case EStatementType::Assignment:
	{
		// Check if this is an assignment to a function output parameter
		if (IsFunctionOutputParameter(Stmt.AssignTarget))
		{
			// For function outputs, just track the value - don't create a Set node
			// The value will be connected to fn_result at the end of the function
			if (Stmt.AssignValue.IsValid())
			{
				FString ValueNodeId;
				FString ValuePinName = CompileExpression(*Stmt.AssignValue, OutNodes, ValueNodeId);

				if (!ValueNodeId.IsEmpty())
				{
					// Store the value source for this output parameter
					OutputValueMap.Add(Stmt.AssignTarget, TPair<FString, FString>(ValueNodeId, ValuePinName));
				}
				else
				{
					// For literals, store a special marker with the literal value
					// Format: ("__literal__", "value")
					FString LiteralValue;
					switch (Stmt.AssignValue->Type)
					{
					case EExpressionType::Literal_Bool:
						LiteralValue = Stmt.AssignValue->BoolValue ? TEXT("true") : TEXT("false");
						break;
					case EExpressionType::Literal_Int:
						LiteralValue = FString::FromInt(Stmt.AssignValue->IntValue);
						break;
					case EExpressionType::Literal_Float:
						LiteralValue = FString::SanitizeFloat(Stmt.AssignValue->FloatValue);
						break;
					case EExpressionType::Literal_String:
						LiteralValue = Stmt.AssignValue->StringValue;
						break;
					default:
						break;
					}
					if (!LiteralValue.IsEmpty())
					{
						OutputValueMap.Add(Stmt.AssignTarget, TPair<FString, FString>(TEXT("__literal__"), LiteralValue));
					}
				}
			}
			// No execution flow change for output assignments
			return true;
		}

		// Regular variable assignment - create Set node
		FBlueprintNodeData SetNode;
		SetNode.NodeId = GenerateNodeId(TEXT("set"));
		SetNode.NodeType = EBlueprintNodeType::Variable_Set;
		SetNode.VariableName = Stmt.AssignTarget;
		SetNode.Position = {400.0f, 0.0f};

		// Connect execute from previous node
		if (!InOutLastExecNodeId.IsEmpty())
		{
			FBlueprintPinData ExecPin;
			ExecPin.Name = TEXT("execute");
			ExecPin.Direction = EBlueprintPinDirection::Input;

			FBlueprintPinConnection ExecConn;
			ExecConn.SourceNodeId = InOutLastExecNodeId;
			ExecConn.SourcePinName = InOutLastExecPinName;
			ExecPin.Connections.Add(ExecConn);
			SetNode.Pins.Add(ExecPin);
		}

		// Compile the value expression
		if (Stmt.AssignValue.IsValid())
		{
			FString ValueNodeId;
			FString ValuePinName = CompileExpression(*Stmt.AssignValue, OutNodes, ValueNodeId);

			FBlueprintPinData ValuePin;
			// Variable Set node's input pin name is the variable name itself
			ValuePin.Name = Stmt.AssignTarget;
			ValuePin.Direction = EBlueprintPinDirection::Input;

			if (!ValueNodeId.IsEmpty())
			{
				// Connect to the expression result
				FBlueprintPinConnection ValueConn;
				ValueConn.SourceNodeId = ValueNodeId;
				ValueConn.SourcePinName = ValuePinName;
				ValuePin.Connections.Add(ValueConn);
			}
			else
			{
				// Handle literal values - set as default value
				switch (Stmt.AssignValue->Type)
				{
				case EExpressionType::Literal_Bool:
					ValuePin.DefaultValue = Stmt.AssignValue->BoolValue ? TEXT("true") : TEXT("false");
					break;
				case EExpressionType::Literal_Int:
					ValuePin.DefaultValue = FString::FromInt(Stmt.AssignValue->IntValue);
					break;
				case EExpressionType::Literal_Float:
					ValuePin.DefaultValue = FString::SanitizeFloat(Stmt.AssignValue->FloatValue);
					break;
				case EExpressionType::Literal_String:
					ValuePin.DefaultValue = Stmt.AssignValue->StringValue;
					break;
				default:
					break;
				}
			}

			SetNode.Pins.Add(ValuePin);
		}

		OutNodes.Add(SetNode);
		InOutLastExecNodeId = SetNode.NodeId;
		InOutLastExecPinName = TEXT("then");
		return true;
	}

	case EStatementType::ExpressionStmt:
	{
		// Expression as statement - likely a function call
		if (Stmt.Expression.IsValid() && Stmt.Expression->Type == EExpressionType::FunctionCall)
		{
			FBlueprintNodeData CallNode;
			CallNode.NodeId = GenerateNodeId(TEXT("call"));
			CallNode.NodeType = EBlueprintNodeType::CallFunction;
			CallNode.FunctionReference = Stmt.Expression->Name;
			CallNode.Position = {400.0f, 0.0f};

			// Connect execute
			if (!InOutLastExecNodeId.IsEmpty())
			{
				FBlueprintPinData ExecPin;
				ExecPin.Name = TEXT("execute");
				ExecPin.Direction = EBlueprintPinDirection::Input;

				FBlueprintPinConnection ExecConn;
				ExecConn.SourceNodeId = InOutLastExecNodeId;
				ExecConn.SourcePinName = InOutLastExecPinName;
				ExecPin.Connections.Add(ExecConn);
				CallNode.Pins.Add(ExecPin);
			}

			// Compile arguments
			int32 ArgIndex = 0;
			for (const TSharedPtr<FExpression>& Arg : Stmt.Expression->Arguments)
			{
				if (!Arg) continue;

				FString ArgNodeId;
				FString ArgPinName = CompileExpression(*Arg, OutNodes, ArgNodeId);

				// For string literal arguments, use default value instead of connection
				if (Arg->Type == EExpressionType::Literal_String)
				{
					FBlueprintPinData ArgPin;
					ArgPin.Name = TEXT("InString");  // Common name for PrintString
					ArgPin.Direction = EBlueprintPinDirection::Input;
					ArgPin.DefaultValue = Arg->StringValue;
					CallNode.Pins.Add(ArgPin);
				}
				else if (!ArgNodeId.IsEmpty())
				{
					FBlueprintPinData ArgPin;
					ArgPin.Name = FString::Printf(TEXT("Arg%d"), ArgIndex);
					ArgPin.Direction = EBlueprintPinDirection::Input;

					FBlueprintPinConnection ArgConn;
					ArgConn.SourceNodeId = ArgNodeId;
					ArgConn.SourcePinName = ArgPinName;
					ArgPin.Connections.Add(ArgConn);
					CallNode.Pins.Add(ArgPin);
				}

				ArgIndex++;
			}

			OutNodes.Add(CallNode);
			InOutLastExecNodeId = CallNode.NodeId;
			InOutLastExecPinName = TEXT("then");
		}
		return true;
	}

	case EStatementType::If:
	{
		// Create Branch node
		FBlueprintNodeData BranchNode;
		BranchNode.NodeId = GenerateNodeId(TEXT("branch"));
		BranchNode.NodeType = EBlueprintNodeType::Flow_Branch;
		BranchNode.Position = {400.0f, 0.0f};

		// Connect execute
		if (!InOutLastExecNodeId.IsEmpty())
		{
			FBlueprintPinData ExecPin;
			ExecPin.Name = TEXT("execute");
			ExecPin.Direction = EBlueprintPinDirection::Input;

			FBlueprintPinConnection ExecConn;
			ExecConn.SourceNodeId = InOutLastExecNodeId;
			ExecConn.SourcePinName = InOutLastExecPinName;
			ExecPin.Connections.Add(ExecConn);
			BranchNode.Pins.Add(ExecPin);
		}

		// Compile condition
		if (Stmt.Condition.IsValid())
		{
			FString CondNodeId;
			FString CondPinName = CompileExpression(*Stmt.Condition, OutNodes, CondNodeId);

			if (!CondNodeId.IsEmpty())
			{
				FBlueprintPinData CondPin;
				CondPin.Name = TEXT("Condition");
				CondPin.Direction = EBlueprintPinDirection::Input;

				FBlueprintPinConnection CondConn;
				CondConn.SourceNodeId = CondNodeId;
				CondConn.SourcePinName = CondPinName;
				CondPin.Connections.Add(CondConn);
				BranchNode.Pins.Add(CondPin);
			}
		}

		OutNodes.Add(BranchNode);

		// Compile then branch
		FString ThenLastNodeId = BranchNode.NodeId;
		FString ThenLastPinName = TEXT("Then");  // UE uses "Then" not "True"
		CompileStatements(Stmt.ThenBody, OutNodes, ThenLastNodeId, ThenLastPinName);

		// Compile else branch
		if (Stmt.ElseBody.Num() > 0)
		{
			FString ElseLastNodeId = BranchNode.NodeId;
			FString ElseLastPinName = TEXT("Else");  // UE uses "Else" not "False"
			CompileStatements(Stmt.ElseBody, OutNodes, ElseLastNodeId, ElseLastPinName);
		}

		// For simplicity, don't update the exec chain after branches
		// (proper merging would require a Sequence or other mechanism)
		InOutLastExecNodeId.Empty();
		InOutLastExecPinName.Empty();
		return true;
	}

	case EStatementType::Return:
	{
		// For functions with outputs, connect values to fn_result node
		if (Stmt.ReturnValues.Num() > 0 && CurrentFunction && CurrentFunction->Outputs.Num() > 0)
		{
			// Create a placeholder node that represents connections to fn_result
			// The fn_result node is created by AIBlueprintFactory
			FBlueprintNodeData ResultConnNode;
			ResultConnNode.NodeId = TEXT("fn_result");
			ResultConnNode.NodeType = EBlueprintNodeType::Return;
			ResultConnNode.Position = {800.0f, 0.0f};

			// Connect execution
			if (!InOutLastExecNodeId.IsEmpty())
			{
				FBlueprintPinData ExecPin;
				ExecPin.Name = TEXT("execute");
				ExecPin.Direction = EBlueprintPinDirection::Input;
				FBlueprintPinConnection Conn;
				Conn.SourceNodeId = InOutLastExecNodeId;
				Conn.SourcePinName = InOutLastExecPinName;
				ExecPin.Connections.Add(Conn);
				ResultConnNode.Pins.Add(ExecPin);
			}

			// Connect each output value to the result node's input pins
			for (int32 i = 0; i < FMath::Min(Stmt.ReturnValues.Num(), CurrentFunction->Outputs.Num()); i++)
			{
				const FVariable& Output = CurrentFunction->Outputs[i];
				const TSharedPtr<FExpression>& Value = Stmt.ReturnValues[i];

				if (Value.IsValid())
				{
					FString ValueNodeId;
					FString ValuePinName = CompileExpression(*Value, OutNodes, ValueNodeId);

					if (!ValueNodeId.IsEmpty())
					{
						FBlueprintPinData ValuePin;
						// Result node's input pin name is the output parameter name
						ValuePin.Name = Output.Name;
						ValuePin.Direction = EBlueprintPinDirection::Input;
						FBlueprintPinConnection Conn;
						Conn.SourceNodeId = ValueNodeId;
						Conn.SourcePinName = ValuePinName;
						ValuePin.Connections.Add(Conn);
						ResultConnNode.Pins.Add(ValuePin);
					}
				}
			}

			OutNodes.Add(ResultConnNode);
			InOutLastExecNodeId.Empty();  // No execution after return
			InOutLastExecPinName.Empty();
		}
		return true;
	}

	default:
		Warning(FString::Printf(TEXT("Unsupported statement type: %d"), static_cast<int32>(Stmt.Type)));
		return true;
	}
}

FString FCompiler::CompileExpression(
	const FExpression& Expr,
	TArray<FBlueprintNodeData>& OutNodes,
	FString& OutNodeId)
{
	switch (Expr.Type)
	{
	case EExpressionType::Literal_Bool:
	case EExpressionType::Literal_Int:
	case EExpressionType::Literal_Float:
	case EExpressionType::Literal_String:
		// Literals don't create nodes, they're used as default values
		OutNodeId.Empty();
		return TEXT("");

	case EExpressionType::Variable:
	{
		// Check if this is a function output parameter that was previously assigned
		// In that case, we use the stored value source instead of creating a Variable_Get
		if (IsFunctionOutputParameter(Expr.Name))
		{
			if (TPair<FString, FString>* ValueInfo = OutputValueMap.Find(Expr.Name))
			{
				if (ValueInfo->Key != TEXT("__literal__"))
				{
					// Return the stored expression result
					OutNodeId = ValueInfo->Key;
					return ValueInfo->Value;
				}
				// For literals, we can't return a node reference
				// This would require creating a make literal node, which is complex
				// For now, warn about this limitation
				Warning(FString::Printf(TEXT("Cannot read literal output parameter '%s' - use a local variable instead"), *Expr.Name));
			}
		}

		// Check if this is a function input parameter (mapped to fn_entry)
		if (FString* MappedNodeId = VariableNodeMap.Find(Expr.Name))
		{
			if (*MappedNodeId == TEXT("fn_entry"))
			{
				// Function parameter - reference from fn_entry node
				OutNodeId = *MappedNodeId;
				return Expr.Name;
			}
		}

		// For regular variables, always create a new Get node for each reference
		// This creates cleaner layouts when the same variable is used in multiple places
		FBlueprintNodeData GetNode;
		GetNode.NodeId = GenerateNodeId(TEXT("get"));
		GetNode.NodeType = EBlueprintNodeType::Variable_Get;
		GetNode.VariableName = Expr.Name;
		GetNode.Position = {200.0f, 100.0f};

		OutNodes.Add(GetNode);

		OutNodeId = GetNode.NodeId;
		// Variable Get node's output pin name is the variable name itself
		return Expr.Name;
	}

	case EExpressionType::FunctionCall:
	{
		// Pure function call
		FBlueprintNodeData CallNode;
		CallNode.NodeId = GenerateNodeId(TEXT("pure"));
		CallNode.NodeType = EBlueprintNodeType::PureFunction;
		CallNode.FunctionReference = Expr.Name;
		CallNode.Position = {300.0f, 100.0f};

		// Add argument connections
		int32 ArgIndex = 0;
		for (const TSharedPtr<FExpression>& Arg : Expr.Arguments)
		{
			if (!Arg) continue;

			FString ArgNodeId;
			FString ArgPinName = CompileExpression(*Arg, OutNodes, ArgNodeId);

			if (!ArgNodeId.IsEmpty())
			{
				FBlueprintPinData ArgPin;
				ArgPin.Name = FString::Printf(TEXT("Arg%d"), ArgIndex);
				ArgPin.Direction = EBlueprintPinDirection::Input;

				FBlueprintPinConnection Conn;
				Conn.SourceNodeId = ArgNodeId;
				Conn.SourcePinName = ArgPinName;
				ArgPin.Connections.Add(Conn);
				CallNode.Pins.Add(ArgPin);
			}
			else if (Arg->Type == EExpressionType::Literal_String)
			{
				FBlueprintPinData ArgPin;
				ArgPin.Name = FString::Printf(TEXT("Arg%d"), ArgIndex);
				ArgPin.DefaultValue = Arg->StringValue;
				CallNode.Pins.Add(ArgPin);
			}

			ArgIndex++;
		}

		OutNodes.Add(CallNode);
		OutNodeId = CallNode.NodeId;
		return TEXT("ReturnValue");
	}

	case EExpressionType::BinaryOp:
	{
		// Create math/comparison node
		FBlueprintNodeData OpNode;
		OpNode.NodeId = GenerateNodeId(TEXT("op"));
		OpNode.NodeType = MapBinaryOp(Expr.BinaryOp);
		OpNode.Position = {300.0f, 50.0f};

		// Determine operand types
		EType LeftType = GetExpressionType(Expr.Left.Get());
		EType RightType = GetExpressionType(Expr.Right.Get());
		OpNode.OperandType = DetermineOperandType(Expr.Left.Get(), Expr.Right.Get());

		// Check if we need type conversions (when using DoubleDouble with int operands)
		bool bNeedLeftConversion = (OpNode.OperandType == TEXT("DoubleDouble") && LeftType == EType::Int);
		bool bNeedRightConversion = (OpNode.OperandType == TEXT("DoubleDouble") && RightType == EType::Int);

		// Compile left operand
		if (Expr.Left.IsValid())
		{
			FString LeftNodeId;
			FString LeftPinName = CompileExpression(*Expr.Left, OutNodes, LeftNodeId);

			// Insert conversion node if needed
			if (bNeedLeftConversion && !LeftNodeId.IsEmpty())
			{
				FBlueprintNodeData ConvNode;
				ConvNode.NodeId = GenerateNodeId(TEXT("conv"));
				ConvNode.NodeType = EBlueprintNodeType::PureFunction;
				ConvNode.FunctionReference = TEXT("Conv_IntToDouble");
				ConvNode.Position = {250.0f, 50.0f};

				FBlueprintPinData ConvInputPin;
				ConvInputPin.Name = TEXT("InInt");
				ConvInputPin.Direction = EBlueprintPinDirection::Input;
				FBlueprintPinConnection ConvConn;
				ConvConn.SourceNodeId = LeftNodeId;
				ConvConn.SourcePinName = LeftPinName;
				ConvInputPin.Connections.Add(ConvConn);
				ConvNode.Pins.Add(ConvInputPin);

				OutNodes.Add(ConvNode);
				LeftNodeId = ConvNode.NodeId;
				LeftPinName = TEXT("ReturnValue");
			}

			FBlueprintPinData LeftPin;
			LeftPin.Name = TEXT("A");
			LeftPin.Direction = EBlueprintPinDirection::Input;

			if (!LeftNodeId.IsEmpty())
			{
				FBlueprintPinConnection Conn;
				Conn.SourceNodeId = LeftNodeId;
				Conn.SourcePinName = LeftPinName;
				LeftPin.Connections.Add(Conn);
			}
			else if (Expr.Left->Type == EExpressionType::Literal_Int)
			{
				// For int literals going to DoubleDouble, convert to float string
				if (bNeedLeftConversion)
				{
					LeftPin.DefaultValue = FString::SanitizeFloat(static_cast<double>(Expr.Left->IntValue));
				}
				else
				{
					LeftPin.DefaultValue = FString::FromInt(Expr.Left->IntValue);
				}
			}
			else if (Expr.Left->Type == EExpressionType::Literal_Float)
			{
				LeftPin.DefaultValue = FString::SanitizeFloat(Expr.Left->FloatValue);
			}

			OpNode.Pins.Add(LeftPin);
		}

		// Compile right operand
		if (Expr.Right.IsValid())
		{
			FString RightNodeId;
			FString RightPinName = CompileExpression(*Expr.Right, OutNodes, RightNodeId);

			// Insert conversion node if needed
			if (bNeedRightConversion && !RightNodeId.IsEmpty())
			{
				FBlueprintNodeData ConvNode;
				ConvNode.NodeId = GenerateNodeId(TEXT("conv"));
				ConvNode.NodeType = EBlueprintNodeType::PureFunction;
				ConvNode.FunctionReference = TEXT("Conv_IntToDouble");
				ConvNode.Position = {250.0f, 100.0f};

				FBlueprintPinData ConvInputPin;
				ConvInputPin.Name = TEXT("InInt");
				ConvInputPin.Direction = EBlueprintPinDirection::Input;
				FBlueprintPinConnection ConvConn;
				ConvConn.SourceNodeId = RightNodeId;
				ConvConn.SourcePinName = RightPinName;
				ConvInputPin.Connections.Add(ConvConn);
				ConvNode.Pins.Add(ConvInputPin);

				OutNodes.Add(ConvNode);
				RightNodeId = ConvNode.NodeId;
				RightPinName = TEXT("ReturnValue");
			}

			FBlueprintPinData RightPin;
			RightPin.Name = TEXT("B");
			RightPin.Direction = EBlueprintPinDirection::Input;

			if (!RightNodeId.IsEmpty())
			{
				FBlueprintPinConnection Conn;
				Conn.SourceNodeId = RightNodeId;
				Conn.SourcePinName = RightPinName;
				RightPin.Connections.Add(Conn);
			}
			else if (Expr.Right->Type == EExpressionType::Literal_Int)
			{
				// For int literals going to DoubleDouble, convert to float string
				if (bNeedRightConversion)
				{
					RightPin.DefaultValue = FString::SanitizeFloat(static_cast<double>(Expr.Right->IntValue));
				}
				else
				{
					RightPin.DefaultValue = FString::FromInt(Expr.Right->IntValue);
				}
			}
			else if (Expr.Right->Type == EExpressionType::Literal_Float)
			{
				RightPin.DefaultValue = FString::SanitizeFloat(Expr.Right->FloatValue);
			}

			OpNode.Pins.Add(RightPin);
		}

		OutNodes.Add(OpNode);
		OutNodeId = OpNode.NodeId;

		// All UE math/comparison functions use "ReturnValue" as output pin name
		return TEXT("ReturnValue");
	}

	case EExpressionType::UnaryOp:
	{
		if (Expr.UnaryOp == EUnaryOp::Not)
		{
			FBlueprintNodeData NotNode;
			NotNode.NodeId = GenerateNodeId(TEXT("not"));
			NotNode.NodeType = EBlueprintNodeType::Logic_Not;
			NotNode.Position = {300.0f, 50.0f};

			if (Expr.Left.IsValid())
			{
				FString OperandNodeId;
				FString OperandPinName = CompileExpression(*Expr.Left, OutNodes, OperandNodeId);

				if (!OperandNodeId.IsEmpty())
				{
					FBlueprintPinData Pin;
					Pin.Name = TEXT("A");
					FBlueprintPinConnection Conn;
					Conn.SourceNodeId = OperandNodeId;
					Conn.SourcePinName = OperandPinName;
					Pin.Connections.Add(Conn);
					NotNode.Pins.Add(Pin);
				}
			}

			OutNodes.Add(NotNode);
			OutNodeId = NotNode.NodeId;
			return TEXT("ReturnValue");
		}
		// Negate would need a multiply by -1
		break;
	}

	case EExpressionType::MemberAccess:
	{
		// obj.member - create a "get" for the member
		// For now, treat as a function call: GetMember(obj)
		FBlueprintNodeData GetNode;
		GetNode.NodeId = GenerateNodeId(TEXT("member"));
		GetNode.NodeType = EBlueprintNodeType::PureFunction;
		GetNode.FunctionReference = TEXT("Get") + Expr.MemberName;

		if (Expr.Object.IsValid())
		{
			FString ObjNodeId;
			FString ObjPinName = CompileExpression(*Expr.Object, OutNodes, ObjNodeId);

			if (!ObjNodeId.IsEmpty())
			{
				FBlueprintPinData Pin;
				Pin.Name = TEXT("Target");
				FBlueprintPinConnection Conn;
				Conn.SourceNodeId = ObjNodeId;
				Conn.SourcePinName = ObjPinName;
				Pin.Connections.Add(Conn);
				GetNode.Pins.Add(Pin);
			}
		}

		OutNodes.Add(GetNode);
		OutNodeId = GetNode.NodeId;
		return TEXT("ReturnValue");
	}

	default:
		Warning(FString::Printf(TEXT("Unsupported expression type: %d"), static_cast<int32>(Expr.Type)));
		break;
	}

	OutNodeId.Empty();
	return TEXT("");
}

FString FCompiler::GenerateNodeId(const FString& Prefix)
{
	return FString::Printf(TEXT("%s_%d"), *Prefix, ++NodeCounter);
}

EBlueprintVarType FCompiler::MapType(EType Type)
{
	switch (Type)
	{
	case EType::Bool: return EBlueprintVarType::Boolean;
	case EType::Int: return EBlueprintVarType::Integer;
	case EType::Float: return EBlueprintVarType::Float;
	case EType::String: return EBlueprintVarType::String;
	case EType::Name: return EBlueprintVarType::Name;
	case EType::Text: return EBlueprintVarType::Text;
	case EType::Vector: return EBlueprintVarType::Vector;
	case EType::Rotator: return EBlueprintVarType::Rotator;
	case EType::Transform: return EBlueprintVarType::Transform;
	case EType::Object: return EBlueprintVarType::Object;
	case EType::Class: return EBlueprintVarType::Class;
	case EType::Array: return EBlueprintVarType::Array;
	default: return EBlueprintVarType::Object;
	}
}

EBlueprintNodeType FCompiler::MapBinaryOp(EBinaryOp Op)
{
	switch (Op)
	{
	case EBinaryOp::Add: return EBlueprintNodeType::Math_Add;
	case EBinaryOp::Subtract: return EBlueprintNodeType::Math_Subtract;
	case EBinaryOp::Multiply: return EBlueprintNodeType::Math_Multiply;
	case EBinaryOp::Divide: return EBlueprintNodeType::Math_Divide;
	case EBinaryOp::Equal: return EBlueprintNodeType::Compare_Equal;
	case EBinaryOp::NotEqual: return EBlueprintNodeType::Compare_NotEqual;
	case EBinaryOp::Less: return EBlueprintNodeType::Compare_Less;
	case EBinaryOp::LessEqual: return EBlueprintNodeType::Compare_LessEqual;
	case EBinaryOp::Greater: return EBlueprintNodeType::Compare_Greater;
	case EBinaryOp::GreaterEqual: return EBlueprintNodeType::Compare_GreaterEqual;
	case EBinaryOp::And: return EBlueprintNodeType::Logic_And;
	case EBinaryOp::Or: return EBlueprintNodeType::Logic_Or;
	default: return EBlueprintNodeType::Unknown;
	}
}

void FCompiler::Error(const FString& Message)
{
	Errors.Add(Message);
}

void FCompiler::Warning(const FString& Message)
{
	Warnings.Add(Message);
}

EType FCompiler::GetExpressionType(const FExpression* Expr)
{
	if (!Expr)
	{
		return EType::Unknown;
	}

	switch (Expr->Type)
	{
	case EExpressionType::Literal_Bool:
		return EType::Bool;

	case EExpressionType::Literal_Int:
		return EType::Int;

	case EExpressionType::Literal_Float:
		return EType::Float;

	case EExpressionType::Literal_String:
		return EType::String;

	case EExpressionType::Variable:
		// Look up variable type
		if (const EType* Type = VariableTypeMap.Find(Expr->Name))
		{
			return *Type;
		}
		// Check function parameters
		if (CurrentFunction)
		{
			for (const FVariable& Input : CurrentFunction->Inputs)
			{
				if (Input.Name == Expr->Name)
				{
					return Input.Type.Type;
				}
			}
		}
		return EType::Unknown;

	case EExpressionType::BinaryOp:
		// For arithmetic operations, the result type is based on operands
		// For comparison/logic operations, the result is Bool
		if (Expr->BinaryOp >= EBinaryOp::Equal && Expr->BinaryOp <= EBinaryOp::Or)
		{
			return EType::Bool;
		}
		else
		{
			// For math ops, determine from operands (take the "wider" type)
			EType LeftType = GetExpressionType(Expr->Left.Get());
			EType RightType = GetExpressionType(Expr->Right.Get());
			if (LeftType == EType::Float || RightType == EType::Float)
			{
				return EType::Float;
			}
			return LeftType;
		}

	case EExpressionType::FunctionCall:
		// Function calls typically return their declared type
		// For now, assume float for math functions
		return EType::Float;

	default:
		return EType::Unknown;
	}
}

FString FCompiler::DetermineOperandType(const FExpression* Left, const FExpression* Right)
{
	EType LeftType = GetExpressionType(Left);
	EType RightType = GetExpressionType(Right);

	// If both are integers, use IntInt
	if (LeftType == EType::Int && RightType == EType::Int)
	{
		return TEXT("IntInt");
	}

	// If one is int and other is unknown (likely int literal), use IntInt
	if ((LeftType == EType::Int && RightType == EType::Unknown) ||
		(LeftType == EType::Unknown && RightType == EType::Int))
	{
		return TEXT("IntInt");
	}

	// For any float involvement or unknown cases, use DoubleDouble
	return TEXT("DoubleDouble");
}

bool FCompiler::IsFunctionOutputParameter(const FString& Name) const
{
	if (!CurrentFunction)
	{
		return false;
	}

	for (const FVariable& Output : CurrentFunction->Outputs)
	{
		if (Output.Name == Name)
		{
			return true;
		}
	}

	return false;
}

} // namespace BSL
