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

				// Connection to another node (including MakeLiteral nodes for literal values)
				FBlueprintPinConnection Conn;
				Conn.SourceNodeId = ValueInfo->Key;
				Conn.SourcePinName = ValueInfo->Value;
				ValuePin.Connections.Add(Conn);
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

	// First pass: count how many If statements need Sequence branches
	// An If needs a branch if there are more statements after it
	int32 SequenceBranchCount = 0;
	for (int32 i = 0; i < Statements.Num(); i++)
	{
		const TSharedPtr<FStatement>& Stmt = Statements[i];
		if (Stmt && Stmt->Type == EStatementType::If && (i + 1 < Statements.Num()))
		{
			SequenceBranchCount++;
		}
	}

	// If we need a Sequence, create one with enough branches
	FString SeqNodeId;
	int32 CurrentSeqBranch = 0;

	if (SequenceBranchCount > 0)
	{
		// Create Sequence node with (SequenceBranchCount + 1) outputs
		// Each If gets one branch, plus one final branch for remaining statements
		FBlueprintNodeData SeqNode;
		SeqNode.NodeId = GenerateNodeId(TEXT("seq"));
		SeqNode.NodeType = EBlueprintNodeType::Flow_Sequence;
		SeqNode.Position = {350.0f, 0.0f};
		SeqNode.SequenceOutputCount = SequenceBranchCount + 1;  // +1 for final statements

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
		SeqNodeId = SeqNode.NodeId;
	}

	// Second pass: compile statements
	for (int32 i = 0; i < Statements.Num(); i++)
	{
		const TSharedPtr<FStatement>& Stmt = Statements[i];
		if (!Stmt) continue;

		// Check if this is an if statement with more statements after it
		bool bNeedSequenceBranch = (Stmt->Type == EStatementType::If) && (i + 1 < Statements.Num());

		if (bNeedSequenceBranch)
		{
			// Compile the if statement from Sequence's then_X output
			FString IfEntryNodeId = SeqNodeId;
			FString IfEntryPinName = FString::Printf(TEXT("then_%d"), CurrentSeqBranch);
			CurrentSeqBranch++;

			if (!CompileStatement(*Stmt, OutNodes, IfEntryNodeId, IfEntryPinName))
			{
				return false;
			}

			// Update last node/pin to continue from next Sequence branch
			LastNodeId = SeqNodeId;
			LastPinName = FString::Printf(TEXT("then_%d"), CurrentSeqBranch);
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
		// Check if this is an array index assignment (arr[index] = value)
		if (Stmt.ArrayIndexExpr.IsValid())
		{
			// Create Array_Set node
			FBlueprintNodeData SetNode;
			SetNode.NodeId = GenerateNodeId(TEXT("arr_set"));
			SetNode.NodeType = EBlueprintNodeType::Array_Set;
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

			// Connect array (TargetArray pin) - using the stored expression
			if (Stmt.Expression.IsValid())
			{
				FString ArrayNodeId;
				FString ArrayPinName = CompileExpression(*Stmt.Expression, OutNodes, ArrayNodeId);

				if (!ArrayNodeId.IsEmpty())
				{
					FBlueprintPinData ArrayPin;
					ArrayPin.Name = TEXT("TargetArray");
					ArrayPin.Direction = EBlueprintPinDirection::Input;
					FBlueprintPinConnection Conn;
					Conn.SourceNodeId = ArrayNodeId;
					Conn.SourcePinName = ArrayPinName;
					ArrayPin.Connections.Add(Conn);
					SetNode.Pins.Add(ArrayPin);
				}
			}

			// Connect index
			{
				FString IndexNodeId;
				FString IndexPinName = CompileExpression(*Stmt.ArrayIndexExpr, OutNodes, IndexNodeId);

				FBlueprintPinData IndexPin;
				IndexPin.Name = TEXT("Index");
				IndexPin.Direction = EBlueprintPinDirection::Input;

				if (!IndexNodeId.IsEmpty())
				{
					FBlueprintPinConnection Conn;
					Conn.SourceNodeId = IndexNodeId;
					Conn.SourcePinName = IndexPinName;
					IndexPin.Connections.Add(Conn);
				}
				else if (Stmt.ArrayIndexExpr->Type == EExpressionType::Literal_Int)
				{
					IndexPin.DefaultValue = FString::FromInt(Stmt.ArrayIndexExpr->IntValue);
				}
				SetNode.Pins.Add(IndexPin);
			}

			// Connect value
			if (Stmt.AssignValue.IsValid())
			{
				FString ValueNodeId;
				FString ValuePinName = CompileExpression(*Stmt.AssignValue, OutNodes, ValueNodeId);

				FBlueprintPinData ValuePin;
				ValuePin.Name = TEXT("Item");
				ValuePin.Direction = EBlueprintPinDirection::Input;

				if (!ValueNodeId.IsEmpty())
				{
					FBlueprintPinConnection Conn;
					Conn.SourceNodeId = ValueNodeId;
					Conn.SourcePinName = ValuePinName;
					ValuePin.Connections.Add(Conn);
				}
				else
				{
					// Handle literal values
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
					// For literals, create a MakeLiteral node so the value can be read later
					// This is needed when the output parameter is used in expressions (e.g., Sum = Sum + i)
					FString LiteralValue;
					FString MakeLiteralFunc;
					switch (Stmt.AssignValue->Type)
					{
					case EExpressionType::Literal_Bool:
						LiteralValue = Stmt.AssignValue->BoolValue ? TEXT("true") : TEXT("false");
						MakeLiteralFunc = TEXT("MakeLiteralBool");
						break;
					case EExpressionType::Literal_Int:
						LiteralValue = FString::FromInt(Stmt.AssignValue->IntValue);
						MakeLiteralFunc = TEXT("MakeLiteralInt");
						break;
					case EExpressionType::Literal_Float:
						LiteralValue = FString::SanitizeFloat(Stmt.AssignValue->FloatValue);
						MakeLiteralFunc = TEXT("MakeLiteralDouble");
						break;
					case EExpressionType::Literal_String:
						LiteralValue = Stmt.AssignValue->StringValue;
						MakeLiteralFunc = TEXT("MakeLiteralString");
						break;
					default:
						break;
					}
					if (!LiteralValue.IsEmpty())
					{
						// Create a MakeLiteral node
						FBlueprintNodeData LiteralNode;
						LiteralNode.NodeId = GenerateNodeId(TEXT("literal"));
						LiteralNode.NodeType = EBlueprintNodeType::PureFunction;
						LiteralNode.FunctionReference = MakeLiteralFunc;
						LiteralNode.Position = {200.0f, 0.0f};

						// Set the value as default on the input pin
						FBlueprintPinData ValuePin;
						ValuePin.Name = TEXT("Value");
						ValuePin.Direction = EBlueprintPinDirection::Input;
						ValuePin.DefaultValue = LiteralValue;
						LiteralNode.Pins.Add(ValuePin);

						OutNodes.Add(LiteralNode);

						// Store the literal node as the value source
						OutputValueMap.Add(Stmt.AssignTarget, TPair<FString, FString>(LiteralNode.NodeId, TEXT("ReturnValue")));
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
		// Expression as statement - could be function call or method call
		if (Stmt.Expression.IsValid())
		{
			if (Stmt.Expression->Type == EExpressionType::FunctionCall)
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
			else if (Stmt.Expression->Type == EExpressionType::MethodCall)
			{
				// Handle array method calls: arr.Add(x), arr.Remove(x), arr.Clear()
				FString MethodName = Stmt.Expression->MemberName;

				EBlueprintNodeType NodeType = EBlueprintNodeType::CallFunction;
				if (MethodName == TEXT("Add"))
				{
					NodeType = EBlueprintNodeType::Array_Add;
				}
				else if (MethodName == TEXT("Remove"))
				{
					NodeType = EBlueprintNodeType::Array_Remove;
				}
				else if (MethodName == TEXT("Clear"))
				{
					NodeType = EBlueprintNodeType::Array_Clear;
				}

				FBlueprintNodeData MethodNode;
				MethodNode.NodeId = GenerateNodeId(TEXT("arr_method"));
				MethodNode.NodeType = NodeType;
				MethodNode.Position = {400.0f, 0.0f};

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
					MethodNode.Pins.Add(ExecPin);
				}

				// Connect array (TargetArray pin)
				if (Stmt.Expression->Object.IsValid())
				{
					FString ArrayNodeId;
					FString ArrayPinName = CompileExpression(*Stmt.Expression->Object, OutNodes, ArrayNodeId);

					if (!ArrayNodeId.IsEmpty())
					{
						FBlueprintPinData ArrayPin;
						ArrayPin.Name = TEXT("TargetArray");
						ArrayPin.Direction = EBlueprintPinDirection::Input;
						FBlueprintPinConnection Conn;
						Conn.SourceNodeId = ArrayNodeId;
						Conn.SourcePinName = ArrayPinName;
						ArrayPin.Connections.Add(Conn);
						MethodNode.Pins.Add(ArrayPin);
					}
				}

				// Connect arguments (for Add and Remove)
				if (MethodName == TEXT("Add") || MethodName == TEXT("Remove"))
				{
					if (Stmt.Expression->Arguments.Num() > 0 && Stmt.Expression->Arguments[0].IsValid())
					{
						FString ArgNodeId;
						FString ArgPinName = CompileExpression(*Stmt.Expression->Arguments[0], OutNodes, ArgNodeId);

						FBlueprintPinData ArgPin;
						ArgPin.Name = MethodName == TEXT("Add") ? TEXT("NewItem") : TEXT("Item");
						ArgPin.Direction = EBlueprintPinDirection::Input;

						if (!ArgNodeId.IsEmpty())
						{
							FBlueprintPinConnection Conn;
							Conn.SourceNodeId = ArgNodeId;
							Conn.SourcePinName = ArgPinName;
							ArgPin.Connections.Add(Conn);
						}
						else
						{
							// Handle literal values
							const FExpression& Arg = *Stmt.Expression->Arguments[0];
							switch (Arg.Type)
							{
							case EExpressionType::Literal_Int:
								ArgPin.DefaultValue = FString::FromInt(Arg.IntValue);
								break;
							case EExpressionType::Literal_Float:
								ArgPin.DefaultValue = FString::SanitizeFloat(Arg.FloatValue);
								break;
							case EExpressionType::Literal_String:
								ArgPin.DefaultValue = Arg.StringValue;
								break;
							case EExpressionType::Literal_Bool:
								ArgPin.DefaultValue = Arg.BoolValue ? TEXT("true") : TEXT("false");
								break;
							default:
								break;
							}
						}
						MethodNode.Pins.Add(ArgPin);
					}
				}

				OutNodes.Add(MethodNode);
				InOutLastExecNodeId = MethodNode.NodeId;
				InOutLastExecPinName = TEXT("then");
			}
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

	case EStatementType::For:
	{
		// For loop: for i in start..end { body }
		// Check if loop body contains break statement to determine which macro to use
		bool bHasBreak = ContainsBreakStatement(Stmt.LoopBody);

		FBlueprintNodeData ForNode;
		ForNode.NodeId = GenerateNodeId(TEXT("for"));
		ForNode.NodeType = bHasBreak ? EBlueprintNodeType::Flow_ForLoopWithBreak : EBlueprintNodeType::Flow_ForLoop;
		ForNode.Position = {400.0f, 0.0f};

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
			ForNode.Pins.Add(ExecPin);
		}

		// Compile start (FirstIndex) expression
		if (Stmt.LoopStart.IsValid())
		{
			FString StartNodeId;
			FString StartPinName = CompileExpression(*Stmt.LoopStart, OutNodes, StartNodeId);

			FBlueprintPinData FirstIndexPin;
			FirstIndexPin.Name = TEXT("FirstIndex");
			FirstIndexPin.Direction = EBlueprintPinDirection::Input;

			if (!StartNodeId.IsEmpty())
			{
				FBlueprintPinConnection Conn;
				Conn.SourceNodeId = StartNodeId;
				Conn.SourcePinName = StartPinName;
				FirstIndexPin.Connections.Add(Conn);
			}
			else if (Stmt.LoopStart->Type == EExpressionType::Literal_Int)
			{
				FirstIndexPin.DefaultValue = FString::FromInt(Stmt.LoopStart->IntValue);
			}
			ForNode.Pins.Add(FirstIndexPin);
		}

		// Compile end (LastIndex) expression
		if (Stmt.LoopEnd.IsValid())
		{
			FString EndNodeId;
			FString EndPinName = CompileExpression(*Stmt.LoopEnd, OutNodes, EndNodeId);

			FBlueprintPinData LastIndexPin;
			LastIndexPin.Name = TEXT("LastIndex");
			LastIndexPin.Direction = EBlueprintPinDirection::Input;

			if (!EndNodeId.IsEmpty())
			{
				FBlueprintPinConnection Conn;
				Conn.SourceNodeId = EndNodeId;
				Conn.SourcePinName = EndPinName;
				LastIndexPin.Connections.Add(Conn);
			}
			else if (Stmt.LoopEnd->Type == EExpressionType::Literal_Int)
			{
				LastIndexPin.DefaultValue = FString::FromInt(Stmt.LoopEnd->IntValue);
			}
			ForNode.Pins.Add(LastIndexPin);
		}

		OutNodes.Add(ForNode);

		// Map the loop variable to the ForLoop's Index output pin
		// When the loop body references 'i', it should connect to ForNode.Index
		FString LoopVarName = Stmt.LoopVariable;
		VariableNodeMap.Add(LoopVarName, ForNode.NodeId);
		VariableTypeMap.Add(LoopVarName, EType::Int);

		// Push loop context for break/continue support
		FLoopContext LoopCtx;
		LoopCtx.LoopNodeId = ForNode.NodeId;
		LoopCtx.bHasBreak = bHasBreak;
		LoopContextStack.Push(LoopCtx);

		// Compile the loop body from the LoopBody pin
		FString BodyLastNodeId = ForNode.NodeId;
		FString BodyLastPinName = TEXT("LoopBody");
		CompileStatements(Stmt.LoopBody, OutNodes, BodyLastNodeId, BodyLastPinName);

		// Pop loop context
		LoopContextStack.Pop();

		// After loop completes, execution continues from Completed pin
		InOutLastExecNodeId = ForNode.NodeId;
		InOutLastExecPinName = TEXT("Completed");

		// Remove the loop variable from the map (it's only valid inside the loop)
		VariableNodeMap.Remove(LoopVarName);
		VariableTypeMap.Remove(LoopVarName);

		return true;
	}

	case EStatementType::While:
	{
		// While loop: while (condition) { body }
		// UE's WhileLoop macro expects condition to be checked inside the loop
		// Pin layout: execute -> LoopBody (exec out), Condition (bool in) -> Completed (exec out)
		FBlueprintNodeData WhileNode;
		WhileNode.NodeId = GenerateNodeId(TEXT("while"));
		WhileNode.NodeType = EBlueprintNodeType::Flow_WhileLoop;
		WhileNode.Position = {400.0f, 0.0f};

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
			WhileNode.Pins.Add(ExecPin);
		}

		// Compile condition expression
		if (Stmt.Condition.IsValid())
		{
			FString CondNodeId;
			FString CondPinName = CompileExpression(*Stmt.Condition, OutNodes, CondNodeId);

			FBlueprintPinData CondPin;
			CondPin.Name = TEXT("Condition");
			CondPin.Direction = EBlueprintPinDirection::Input;

			if (!CondNodeId.IsEmpty())
			{
				FBlueprintPinConnection Conn;
				Conn.SourceNodeId = CondNodeId;
				Conn.SourcePinName = CondPinName;
				CondPin.Connections.Add(Conn);
			}
			else if (Stmt.Condition->Type == EExpressionType::Literal_Bool)
			{
				CondPin.DefaultValue = Stmt.Condition->BoolValue ? TEXT("true") : TEXT("false");
			}
			WhileNode.Pins.Add(CondPin);
		}

		OutNodes.Add(WhileNode);

		// Push loop context for break/continue support
		// Note: WhileLoop macro doesn't have a Break pin, so we use a different approach
		FLoopContext LoopCtx;
		LoopCtx.LoopNodeId = WhileNode.NodeId;
		LoopCtx.bHasBreak = false;  // WhileLoop doesn't support break pin directly
		LoopContextStack.Push(LoopCtx);

		// Compile the loop body from the LoopBody pin
		FString BodyLastNodeId = WhileNode.NodeId;
		FString BodyLastPinName = TEXT("LoopBody");
		CompileStatements(Stmt.LoopBody, OutNodes, BodyLastNodeId, BodyLastPinName);

		// Pop loop context
		LoopContextStack.Pop();

		// After loop completes, execution continues from Completed pin
		InOutLastExecNodeId = WhileNode.NodeId;
		InOutLastExecPinName = TEXT("Completed");

		return true;
	}

	case EStatementType::Break:
	{
		// Break statement - connects to the enclosing loop's Break pin
		if (LoopContextStack.Num() == 0)
		{
			Error(TEXT("Break statement outside of loop"));
			return false;
		}

		const FLoopContext& CurrentLoop = LoopContextStack.Last();

		if (!CurrentLoop.bHasBreak)
		{
			// WhileLoop doesn't have a Break pin, warn and skip
			Warning(TEXT("Break in while loop - not supported (use a condition variable instead)"));
			// End execution flow, nothing after break executes
			InOutLastExecNodeId.Empty();
			InOutLastExecPinName.Empty();
			return true;
		}

		// For ForLoopWithBreak, we need to connect to its Break pin
		// Find the loop node in OutNodes and add the Break pin connection
		if (!InOutLastExecNodeId.IsEmpty())
		{
			// Find the loop node and add Break pin connection
			for (FBlueprintNodeData& Node : OutNodes)
			{
				if (Node.NodeId == CurrentLoop.LoopNodeId)
				{
					FBlueprintPinData BreakPin;
					BreakPin.Name = TEXT("Break");
					BreakPin.Direction = EBlueprintPinDirection::Input;
					FBlueprintPinConnection Conn;
					Conn.SourceNodeId = InOutLastExecNodeId;
					Conn.SourcePinName = InOutLastExecPinName;
					BreakPin.Connections.Add(Conn);
					Node.Pins.Add(BreakPin);
					break;
				}
			}
		}

		// After break, no more statements should execute in this branch
		InOutLastExecNodeId.Empty();
		InOutLastExecPinName.Empty();
		return true;
	}

	case EStatementType::Continue:
	{
		// Continue statement - skip the rest of the loop body and go to next iteration
		// In UE blueprints, this is done by simply not connecting to subsequent nodes
		// The loop body ends, and the loop macro handles the next iteration

		if (LoopContextStack.Num() == 0)
		{
			Error(TEXT("Continue statement outside of loop"));
			return false;
		}

		// After continue, no more statements should execute in this branch
		// The loop macro will automatically go to the next iteration
		InOutLastExecNodeId.Empty();
		InOutLastExecPinName.Empty();
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
				// Return the stored expression result (either a computed node or a MakeLiteral node)
				OutNodeId = ValueInfo->Key;
				return ValueInfo->Value;
			}
		}

		// Check if this variable is mapped to a special node (function entry or loop index)
		if (FString* MappedNodeId = VariableNodeMap.Find(Expr.Name))
		{
			if (*MappedNodeId == TEXT("fn_entry"))
			{
				// Function parameter - reference from fn_entry node
				OutNodeId = *MappedNodeId;
				return Expr.Name;
			}
			else if (MappedNodeId->StartsWith(TEXT("for_")))
			{
				// Loop variable - reference from ForLoop node's Index pin
				OutNodeId = *MappedNodeId;
				return TEXT("Index");
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

	case EExpressionType::ArrayLiteral:
	{
		// Array literal: [1, 2, 3] - for now, we'll create empty array if no elements
		// or warn that array literals with initializers need MakeArray node
		if (Expr.ArrayElements.Num() == 0)
		{
			// Empty array - this will be handled by the variable type
			OutNodeId.Empty();
			return TEXT("");
		}
		else
		{
			// Non-empty array literal - would need MakeArray node
			// For now, warn and return empty
			Warning(TEXT("Array literals with elements are not yet fully supported"));
			OutNodeId.Empty();
			return TEXT("");
		}
	}

	case EExpressionType::ArrayAccess:
	{
		// arr[index] - create Array_Get node
		FBlueprintNodeData GetNode;
		GetNode.NodeId = GenerateNodeId(TEXT("arr_get"));
		GetNode.NodeType = EBlueprintNodeType::Array_Get;
		GetNode.Position = {300.0f, 100.0f};

		// Connect array (TargetArray pin)
		if (Expr.Left.IsValid())
		{
			FString ArrayNodeId;
			FString ArrayPinName = CompileExpression(*Expr.Left, OutNodes, ArrayNodeId);

			if (!ArrayNodeId.IsEmpty())
			{
				FBlueprintPinData ArrayPin;
				ArrayPin.Name = TEXT("TargetArray");
				ArrayPin.Direction = EBlueprintPinDirection::Input;
				FBlueprintPinConnection Conn;
				Conn.SourceNodeId = ArrayNodeId;
				Conn.SourcePinName = ArrayPinName;
				ArrayPin.Connections.Add(Conn);
				GetNode.Pins.Add(ArrayPin);
			}
		}

		// Connect index
		if (Expr.Right.IsValid())
		{
			FString IndexNodeId;
			FString IndexPinName = CompileExpression(*Expr.Right, OutNodes, IndexNodeId);

			FBlueprintPinData IndexPin;
			IndexPin.Name = TEXT("Index");
			IndexPin.Direction = EBlueprintPinDirection::Input;

			if (!IndexNodeId.IsEmpty())
			{
				FBlueprintPinConnection Conn;
				Conn.SourceNodeId = IndexNodeId;
				Conn.SourcePinName = IndexPinName;
				IndexPin.Connections.Add(Conn);
			}
			else if (Expr.Right->Type == EExpressionType::Literal_Int)
			{
				IndexPin.DefaultValue = FString::FromInt(Expr.Right->IntValue);
			}
			GetNode.Pins.Add(IndexPin);
		}

		OutNodes.Add(GetNode);
		OutNodeId = GetNode.NodeId;
		return TEXT("Item");  // Array_Get returns on "Item" pin
	}

	case EExpressionType::MethodCall:
	{
		// obj.method(args) - handle array methods
		FString MethodName = Expr.MemberName;

		// Check if this is an array method
		if (MethodName == TEXT("Length"))
		{
			// Array.Length() -> Array_Length node
			FBlueprintNodeData LengthNode;
			LengthNode.NodeId = GenerateNodeId(TEXT("arr_len"));
			LengthNode.NodeType = EBlueprintNodeType::Array_Length;
			LengthNode.Position = {300.0f, 100.0f};

			if (Expr.Object.IsValid())
			{
				FString ArrayNodeId;
				FString ArrayPinName = CompileExpression(*Expr.Object, OutNodes, ArrayNodeId);

				if (!ArrayNodeId.IsEmpty())
				{
					FBlueprintPinData ArrayPin;
					ArrayPin.Name = TEXT("TargetArray");
					ArrayPin.Direction = EBlueprintPinDirection::Input;
					FBlueprintPinConnection Conn;
					Conn.SourceNodeId = ArrayNodeId;
					Conn.SourcePinName = ArrayPinName;
					ArrayPin.Connections.Add(Conn);
					LengthNode.Pins.Add(ArrayPin);
				}
			}

			OutNodes.Add(LengthNode);
			OutNodeId = LengthNode.NodeId;
			return TEXT("ReturnValue");
		}
		else
		{
			// Generic method call - treat as pure function
			FBlueprintNodeData CallNode;
			CallNode.NodeId = GenerateNodeId(TEXT("method"));
			CallNode.NodeType = EBlueprintNodeType::PureFunction;
			CallNode.FunctionReference = MethodName;
			CallNode.Position = {300.0f, 100.0f};

			// Connect object as target
			if (Expr.Object.IsValid())
			{
				FString ObjNodeId;
				FString ObjPinName = CompileExpression(*Expr.Object, OutNodes, ObjNodeId);

				if (!ObjNodeId.IsEmpty())
				{
					FBlueprintPinData Pin;
					Pin.Name = TEXT("Target");
					Pin.Direction = EBlueprintPinDirection::Input;
					FBlueprintPinConnection Conn;
					Conn.SourceNodeId = ObjNodeId;
					Conn.SourcePinName = ObjPinName;
					Pin.Connections.Add(Conn);
					CallNode.Pins.Add(Pin);
				}
			}

			// Add arguments
			int32 ArgIndex = 0;
			for (const TSharedPtr<FExpression>& Arg : Expr.Arguments)
			{
				if (!Arg) continue;

				FString ArgNodeId;
				FString ArgPinName = CompileExpression(*Arg, OutNodes, ArgNodeId);

				FBlueprintPinData ArgPin;
				ArgPin.Name = FString::Printf(TEXT("Arg%d"), ArgIndex);
				ArgPin.Direction = EBlueprintPinDirection::Input;

				if (!ArgNodeId.IsEmpty())
				{
					FBlueprintPinConnection Conn;
					Conn.SourceNodeId = ArgNodeId;
					Conn.SourcePinName = ArgPinName;
					ArgPin.Connections.Add(Conn);
				}
				CallNode.Pins.Add(ArgPin);
				ArgIndex++;
			}

			OutNodes.Add(CallNode);
			OutNodeId = CallNode.NodeId;
			return TEXT("ReturnValue");
		}
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

bool FCompiler::ContainsBreakStatement(const TArray<TSharedPtr<FStatement>>& Statements)
{
	for (const TSharedPtr<FStatement>& Stmt : Statements)
	{
		if (!Stmt.IsValid())
		{
			continue;
		}

		if (Stmt->Type == EStatementType::Break)
		{
			return true;
		}

		// Recursively check nested statements (if/else blocks)
		if (Stmt->Type == EStatementType::If)
		{
			if (ContainsBreakStatement(Stmt->ThenBody))
			{
				return true;
			}
			if (ContainsBreakStatement(Stmt->ElseBody))
			{
				return true;
			}
		}

		// Note: We don't recurse into nested loops because break in a nested loop
		// only breaks the inner loop, not the outer one
	}

	return false;
}

} // namespace BSL
