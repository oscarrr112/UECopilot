// Copyright Epic Games, Inc. All Rights Reserved.

#include "JSON/BlueprintJSONParser.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

namespace
{
	FString NormalizeNodeTypeKey(const FString& Value)
	{
		FString Normalized = Value;
		Normalized = Normalized.ToLower();
		Normalized.ReplaceInline(TEXT("_"), TEXT(""));
		Normalized.ReplaceInline(TEXT("-"), TEXT(""));
		Normalized.ReplaceInline(TEXT(" "), TEXT(""));
		Normalized.ReplaceInline(TEXT("."), TEXT(""));
		return Normalized;
	}

	EBlueprintNodeType InferNodeTypeFromData(const FBlueprintNodeData& NodeData)
	{
		const FString NodeIdKey = NormalizeNodeTypeKey(NodeData.NodeId);
		const auto HasPin = [&NodeData](const TCHAR* PinName)
		{
			for (const FBlueprintPinData& Pin : NodeData.Pins)
			{
				if (Pin.Name.Equals(PinName, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
			return false;
		};

		if (!NodeData.EventName.IsEmpty())
		{
			const FString EventKey = NormalizeNodeTypeKey(NodeData.EventName);
			if (EventKey.Contains(TEXT("beginplay")) || NodeIdKey.Contains(TEXT("eventbegin")) || NodeIdKey.Contains(TEXT("eventbeginplay")))
			{
				return EBlueprintNodeType::Event_BeginPlay;
			}
			if (EventKey.Contains(TEXT("tick")) || NodeIdKey.Contains(TEXT("eventtick")))
			{
				return EBlueprintNodeType::Event_Tick;
			}
			return EBlueprintNodeType::Event_Custom;
		}

		if (!NodeData.FunctionReference.IsEmpty())
		{
			return HasPin(TEXT("execute")) ? EBlueprintNodeType::CallFunction : EBlueprintNodeType::PureFunction;
		}

		if (!NodeData.VariableName.IsEmpty())
		{
			return HasPin(TEXT("execute")) ? EBlueprintNodeType::Variable_Set : EBlueprintNodeType::Variable_Get;
		}

		if (HasPin(TEXT("Condition")) || NodeIdKey.Contains(TEXT("branch")))
		{
			return EBlueprintNodeType::Flow_Branch;
		}
		if (NodeIdKey.Contains(TEXT("return")))
		{
			return EBlueprintNodeType::Return;
		}

		if (NodeIdKey.Contains(TEXT("comparegreaterequal")) || NodeIdKey.Contains(TEXT("cmpge")))
		{
			return EBlueprintNodeType::Compare_GreaterEqual;
		}
		if (NodeIdKey.Contains(TEXT("comparelessequal")) || NodeIdKey.Contains(TEXT("cmple")))
		{
			return EBlueprintNodeType::Compare_LessEqual;
		}
		if (NodeIdKey.Contains(TEXT("comparegreater")) || NodeIdKey.Contains(TEXT("cmpgt")))
		{
			return EBlueprintNodeType::Compare_Greater;
		}
		if (NodeIdKey.Contains(TEXT("compareless")) || NodeIdKey.Contains(TEXT("cmplt")))
		{
			return EBlueprintNodeType::Compare_Less;
		}
		if (NodeIdKey.Contains(TEXT("compareequal")) || NodeIdKey.Contains(TEXT("cmpeq")))
		{
			return EBlueprintNodeType::Compare_Equal;
		}

		if (NodeIdKey.Contains(TEXT("logicand")) || NodeIdKey.EndsWith(TEXT("and")))
		{
			return EBlueprintNodeType::Logic_And;
		}
		if (NodeIdKey.Contains(TEXT("logicor")) || NodeIdKey.EndsWith(TEXT("or")))
		{
			return EBlueprintNodeType::Logic_Or;
		}
		if (NodeIdKey.Contains(TEXT("logicnot")) || NodeIdKey.EndsWith(TEXT("not")))
		{
			return EBlueprintNodeType::Logic_Not;
		}

		if (NodeIdKey.Contains(TEXT("mathadd")) || NodeIdKey.Contains(TEXT("add")))
		{
			return EBlueprintNodeType::Math_Add;
		}
		if (NodeIdKey.Contains(TEXT("mathsubtract")) || NodeIdKey.Contains(TEXT("sub")))
		{
			return EBlueprintNodeType::Math_Subtract;
		}
		if (NodeIdKey.Contains(TEXT("mathmultiply")) || NodeIdKey.Contains(TEXT("mul")))
		{
			return EBlueprintNodeType::Math_Multiply;
		}
		if (NodeIdKey.Contains(TEXT("mathdivide")) || NodeIdKey.Contains(TEXT("div")))
		{
			return EBlueprintNodeType::Math_Divide;
		}

		if (!NodeData.FunctionReference.IsEmpty())
		{
			return EBlueprintNodeType::CallFunction;
		}

		return EBlueprintNodeType::Unknown;
	}

	const TMap<FString, EBlueprintNodeType>& GetNodeTypeMap()
	{
		static TMap<FString, EBlueprintNodeType> Map;
		if (Map.Num() == 0)
		{
			auto AddEntry = [&](const TCHAR* Name, EBlueprintNodeType Type)
			{
				Map.Add(NormalizeNodeTypeKey(Name), Type);
			};

			AddEntry(TEXT("Event_BeginPlay"), EBlueprintNodeType::Event_BeginPlay);
			AddEntry(TEXT("BeginPlay"), EBlueprintNodeType::Event_BeginPlay);
			AddEntry(TEXT("Event_Tick"), EBlueprintNodeType::Event_Tick);
			AddEntry(TEXT("Tick"), EBlueprintNodeType::Event_Tick);
			AddEntry(TEXT("Event_Custom"), EBlueprintNodeType::Event_Custom);
			AddEntry(TEXT("CustomEvent"), EBlueprintNodeType::Event_Custom);
			AddEntry(TEXT("Event_Input"), EBlueprintNodeType::Event_Input);
			AddEntry(TEXT("InputEvent"), EBlueprintNodeType::Event_Input);

			AddEntry(TEXT("Branch"), EBlueprintNodeType::Flow_Branch);
			AddEntry(TEXT("Flow_Branch"), EBlueprintNodeType::Flow_Branch);
			AddEntry(TEXT("K2Node_Branch"), EBlueprintNodeType::Flow_Branch);
			AddEntry(TEXT("K2Node_IfThenElse"), EBlueprintNodeType::Flow_Branch);
			AddEntry(TEXT("Sequence"), EBlueprintNodeType::Flow_Sequence);
			AddEntry(TEXT("Flow_Sequence"), EBlueprintNodeType::Flow_Sequence);
			AddEntry(TEXT("K2Node_ExecutionSequence"), EBlueprintNodeType::Flow_Sequence);
			AddEntry(TEXT("ForLoop"), EBlueprintNodeType::Flow_ForLoop);
			AddEntry(TEXT("Flow_ForLoop"), EBlueprintNodeType::Flow_ForLoop);
			AddEntry(TEXT("ForEachLoop"), EBlueprintNodeType::Flow_ForEachLoop);
			AddEntry(TEXT("Flow_ForEachLoop"), EBlueprintNodeType::Flow_ForEachLoop);
			AddEntry(TEXT("WhileLoop"), EBlueprintNodeType::Flow_WhileLoop);
			AddEntry(TEXT("Flow_WhileLoop"), EBlueprintNodeType::Flow_WhileLoop);
			AddEntry(TEXT("Switch"), EBlueprintNodeType::Flow_Switch);
			AddEntry(TEXT("Flow_Switch"), EBlueprintNodeType::Flow_Switch);
			AddEntry(TEXT("DoOnce"), EBlueprintNodeType::Flow_DoOnce);
			AddEntry(TEXT("Flow_DoOnce"), EBlueprintNodeType::Flow_DoOnce);
			AddEntry(TEXT("Gate"), EBlueprintNodeType::Flow_Gate);
			AddEntry(TEXT("Flow_Gate"), EBlueprintNodeType::Flow_Gate);
			AddEntry(TEXT("Delay"), EBlueprintNodeType::Flow_Delay);
			AddEntry(TEXT("Flow_Delay"), EBlueprintNodeType::Flow_Delay);

			AddEntry(TEXT("CallFunction"), EBlueprintNodeType::CallFunction);
			AddEntry(TEXT("FunctionCall"), EBlueprintNodeType::CallFunction);
			AddEntry(TEXT("Function_Call"), EBlueprintNodeType::CallFunction);
			AddEntry(TEXT("Call_Function"), EBlueprintNodeType::CallFunction);
			AddEntry(TEXT("K2Node_CallFunction"), EBlueprintNodeType::CallFunction);
			AddEntry(TEXT("PureFunction"), EBlueprintNodeType::PureFunction);
			AddEntry(TEXT("PureCall"), EBlueprintNodeType::PureFunction);
			AddEntry(TEXT("FunctionPure"), EBlueprintNodeType::PureFunction);
			AddEntry(TEXT("Function_Pure"), EBlueprintNodeType::PureFunction);
			AddEntry(TEXT("K2Node_PureFunction"), EBlueprintNodeType::PureFunction);

			AddEntry(TEXT("GetVariable"), EBlueprintNodeType::Variable_Get);
			AddEntry(TEXT("Get"), EBlueprintNodeType::Variable_Get);
			AddEntry(TEXT("Variable_Get"), EBlueprintNodeType::Variable_Get);
			AddEntry(TEXT("K2Node_VariableGet"), EBlueprintNodeType::Variable_Get);
			AddEntry(TEXT("SetVariable"), EBlueprintNodeType::Variable_Set);
			AddEntry(TEXT("Set"), EBlueprintNodeType::Variable_Set);
			AddEntry(TEXT("Variable_Set"), EBlueprintNodeType::Variable_Set);
			AddEntry(TEXT("K2Node_VariableSet"), EBlueprintNodeType::Variable_Set);

			AddEntry(TEXT("Add"), EBlueprintNodeType::Math_Add);
			AddEntry(TEXT("Math_Add"), EBlueprintNodeType::Math_Add);
			AddEntry(TEXT("Subtract"), EBlueprintNodeType::Math_Subtract);
			AddEntry(TEXT("Math_Subtract"), EBlueprintNodeType::Math_Subtract);
			AddEntry(TEXT("Multiply"), EBlueprintNodeType::Math_Multiply);
			AddEntry(TEXT("Math_Multiply"), EBlueprintNodeType::Math_Multiply);
			AddEntry(TEXT("Divide"), EBlueprintNodeType::Math_Divide);
			AddEntry(TEXT("Math_Divide"), EBlueprintNodeType::Math_Divide);

			AddEntry(TEXT("Equal"), EBlueprintNodeType::Compare_Equal);
			AddEntry(TEXT("Compare_Equal"), EBlueprintNodeType::Compare_Equal);
			AddEntry(TEXT("NotEqual"), EBlueprintNodeType::Compare_NotEqual);
			AddEntry(TEXT("Compare_NotEqual"), EBlueprintNodeType::Compare_NotEqual);
			AddEntry(TEXT("Greater"), EBlueprintNodeType::Compare_Greater);
			AddEntry(TEXT("Compare_Greater"), EBlueprintNodeType::Compare_Greater);
			AddEntry(TEXT("Less"), EBlueprintNodeType::Compare_Less);
			AddEntry(TEXT("Compare_Less"), EBlueprintNodeType::Compare_Less);
			AddEntry(TEXT("GreaterEqual"), EBlueprintNodeType::Compare_GreaterEqual);
			AddEntry(TEXT("Compare_GreaterEqual"), EBlueprintNodeType::Compare_GreaterEqual);
			AddEntry(TEXT("LessEqual"), EBlueprintNodeType::Compare_LessEqual);
			AddEntry(TEXT("Compare_LessEqual"), EBlueprintNodeType::Compare_LessEqual);

			AddEntry(TEXT("And"), EBlueprintNodeType::Logic_And);
			AddEntry(TEXT("Logic_And"), EBlueprintNodeType::Logic_And);
			AddEntry(TEXT("Or"), EBlueprintNodeType::Logic_Or);
			AddEntry(TEXT("Logic_Or"), EBlueprintNodeType::Logic_Or);
			AddEntry(TEXT("Not"), EBlueprintNodeType::Logic_Not);
			AddEntry(TEXT("Logic_Not"), EBlueprintNodeType::Logic_Not);

			AddEntry(TEXT("Cast"), EBlueprintNodeType::Cast);
			AddEntry(TEXT("MakeStruct"), EBlueprintNodeType::MakeStruct);
			AddEntry(TEXT("BreakStruct"), EBlueprintNodeType::BreakStruct);

			AddEntry(TEXT("ArrayAdd"), EBlueprintNodeType::Array_Add);
			AddEntry(TEXT("ArrayRemove"), EBlueprintNodeType::Array_Remove);
			AddEntry(TEXT("ArrayGet"), EBlueprintNodeType::Array_Get);
			AddEntry(TEXT("ArraySet"), EBlueprintNodeType::Array_Set);
			AddEntry(TEXT("ArrayLength"), EBlueprintNodeType::Array_Length);
			AddEntry(TEXT("ArrayClear"), EBlueprintNodeType::Array_Clear);
			AddEntry(TEXT("Array_Add"), EBlueprintNodeType::Array_Add);
			AddEntry(TEXT("Array_Remove"), EBlueprintNodeType::Array_Remove);
			AddEntry(TEXT("Array_Get"), EBlueprintNodeType::Array_Get);
			AddEntry(TEXT("Array_Set"), EBlueprintNodeType::Array_Set);
			AddEntry(TEXT("Array_Length"), EBlueprintNodeType::Array_Length);
			AddEntry(TEXT("Array_Clear"), EBlueprintNodeType::Array_Clear);

			AddEntry(TEXT("Literal"), EBlueprintNodeType::Literal);
			AddEntry(TEXT("Constant"), EBlueprintNodeType::Literal);
			AddEntry(TEXT("K2Node_Literal"), EBlueprintNodeType::Literal);
			AddEntry(TEXT("Comment"), EBlueprintNodeType::Comment);
			AddEntry(TEXT("Reroute"), EBlueprintNodeType::Reroute);
			AddEntry(TEXT("Return"), EBlueprintNodeType::Return);
			AddEntry(TEXT("K2Node_FunctionResult"), EBlueprintNodeType::Return);
			AddEntry(TEXT("K2Node_IfThenElse"), EBlueprintNodeType::Flow_Branch);
			AddEntry(TEXT("K2Node_Event"), EBlueprintNodeType::Event_Custom);
			AddEntry(TEXT("K2Node_InputAction"), EBlueprintNodeType::Event_Input);
			AddEntry(TEXT("K2Node_FunctionEntry"), EBlueprintNodeType::Unknown);
		}

		return Map;
	}
}

DEFINE_LOG_CATEGORY(LogBlueprintJSON);

FBlueprintParseResult UBlueprintJSONParser::ParseBlueprintJSON(const FString& JSONString)
{
	FBlueprintParseResult Result;

	if (JSONString.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Empty JSON string");
		return Result;
	}

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JSONString);

	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		Result.ErrorMessage = TEXT("Failed to parse JSON");
		Result.ErrorLine = Reader->GetLineNumber();
		return Result;
	}

	// Check for "blueprint" wrapper
	const TSharedPtr<FJsonObject>* BlueprintObject;
	if (RootObject->TryGetObjectField(TEXT("blueprint"), BlueprintObject))
	{
		Result.bSuccess = ParseBlueprintObject(*BlueprintObject, Result.BlueprintData, Result.ErrorMessage);
	}
	else
	{
		// Try parsing root as blueprint directly
		Result.bSuccess = ParseBlueprintObject(RootObject, Result.BlueprintData, Result.ErrorMessage);
	}

	return Result;
}

FString UBlueprintJSONParser::ExtractJSONFromResponse(const FString& AIResponse)
{
	FString Response = AIResponse;

	// Try to find JSON in markdown code block
	int32 JsonBlockStart = Response.Find(TEXT("```json"));
	if (JsonBlockStart != INDEX_NONE)
	{
		JsonBlockStart += 7; // Skip "```json"

		// Skip any whitespace/newlines after ```json
		while (JsonBlockStart < Response.Len() && (Response[JsonBlockStart] == '\n' || Response[JsonBlockStart] == '\r'))
		{
			JsonBlockStart++;
		}

		int32 JsonBlockEnd = Response.Find(TEXT("```"), ESearchCase::IgnoreCase, ESearchDir::FromStart, JsonBlockStart);
		if (JsonBlockEnd != INDEX_NONE)
		{
			return Response.Mid(JsonBlockStart, JsonBlockEnd - JsonBlockStart).TrimStartAndEnd();
		}
	}

	// Try generic code block
	int32 CodeBlockStart = Response.Find(TEXT("```"));
	if (CodeBlockStart != INDEX_NONE)
	{
		CodeBlockStart += 3;

		// Skip language identifier if present
		int32 NewlinePos = Response.Find(TEXT("\n"), ESearchCase::IgnoreCase, ESearchDir::FromStart, CodeBlockStart);
		if (NewlinePos != INDEX_NONE)
		{
			CodeBlockStart = NewlinePos + 1;
		}

		int32 CodeBlockEnd = Response.Find(TEXT("```"), ESearchCase::IgnoreCase, ESearchDir::FromStart, CodeBlockStart);
		if (CodeBlockEnd != INDEX_NONE)
		{
			return Response.Mid(CodeBlockStart, CodeBlockEnd - CodeBlockStart).TrimStartAndEnd();
		}
	}

	// Try to find JSON object directly
	int32 BraceStart = Response.Find(TEXT("{"));
	int32 BraceEnd = Response.Find(TEXT("}"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);

	if (BraceStart != INDEX_NONE && BraceEnd != INDEX_NONE && BraceEnd > BraceStart)
	{
		return Response.Mid(BraceStart, BraceEnd - BraceStart + 1);
	}

	return Response;
}

bool UBlueprintJSONParser::ValidateBlueprintData(const FBlueprintData& Data, TArray<FString>& OutErrors)
{
	OutErrors.Empty();

	if (Data.Name.IsEmpty())
	{
		OutErrors.Add(TEXT("Blueprint name is required"));
	}

	if (Data.ParentClass.IsEmpty())
	{
		OutErrors.Add(TEXT("Parent class is required"));
	}

	// Validate node connections
	TSet<FString> NodeIds;
	auto CollectNodeIds = [&NodeIds](const TArray<FBlueprintGraphData>& Graphs)
	{
		for (const FBlueprintGraphData& Graph : Graphs)
		{
			for (const FBlueprintNodeData& Node : Graph.Nodes)
			{
				NodeIds.Add(Node.NodeId);
			}
		}
	};

	CollectNodeIds(Data.Functions);
	CollectNodeIds(Data.EventGraphs);
	CollectNodeIds(Data.Macros);

	// Validate all connections reference existing nodes
	auto ValidateConnections = [&NodeIds, &OutErrors](const TArray<FBlueprintGraphData>& Graphs)
	{
		for (const FBlueprintGraphData& Graph : Graphs)
		{
			for (const FBlueprintNodeData& Node : Graph.Nodes)
			{
				for (const FBlueprintPinData& Pin : Node.Pins)
				{
					for (const FBlueprintPinConnection& Conn : Pin.Connections)
					{
						const bool bIsVirtualEntryOrEvent =
							Conn.SourceNodeId.Equals(TEXT("fn_entry"), ESearchCase::IgnoreCase) ||
							Conn.SourceNodeId.Equals(TEXT("fn_result"), ESearchCase::IgnoreCase) ||
							Conn.SourceNodeId.StartsWith(TEXT("event_"), ESearchCase::IgnoreCase);

						if (bIsVirtualEntryOrEvent)
						{
							continue;
						}

						if (!NodeIds.Contains(Conn.SourceNodeId))
						{
							OutErrors.Add(FString::Printf(TEXT("Node '%s' references non-existent node '%s'"),
								*Node.NodeId, *Conn.SourceNodeId));
						}
					}
				}
			}
		}
	};

	ValidateConnections(Data.Functions);
	ValidateConnections(Data.EventGraphs);
	ValidateConnections(Data.Macros);

	return OutErrors.Num() == 0;
}

FString UBlueprintJSONParser::SerializeBlueprintData(const FBlueprintData& Data)
{
	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> BlueprintObject = MakeShared<FJsonObject>();

	BlueprintObject->SetStringField(TEXT("name"), Data.Name);
	BlueprintObject->SetStringField(TEXT("parent_class"), Data.ParentClass);

	// Serialize variables
	TArray<TSharedPtr<FJsonValue>> VariablesArray;
	for (const FBlueprintVariableData& Var : Data.Variables)
	{
		TSharedRef<FJsonObject> VarObject = MakeShared<FJsonObject>();
		VarObject->SetStringField(TEXT("name"), Var.Name);
		VarObject->SetStringField(TEXT("type"), VarTypeToString(Var.Type));
		if (!Var.TypeClass.IsEmpty())
		{
			VarObject->SetStringField(TEXT("type_class"), Var.TypeClass);
		}
		if (!Var.DefaultValue.IsEmpty())
		{
			VarObject->SetStringField(TEXT("default_value"), Var.DefaultValue);
		}
		VarObject->SetBoolField(TEXT("instance_editable"), Var.bInstanceEditable);
		VarObject->SetBoolField(TEXT("expose_on_spawn"), Var.bExposeOnSpawn);
		VarObject->SetBoolField(TEXT("private"), Var.bPrivate);
		VariablesArray.Add(MakeShared<FJsonValueObject>(VarObject));
	}
	BlueprintObject->SetArrayField(TEXT("variables"), VariablesArray);

	// Helper to serialize graphs
	auto SerializeGraphs = [](const TArray<FBlueprintGraphData>& Graphs)
	{
		TArray<TSharedPtr<FJsonValue>> GraphsArray;
		for (const FBlueprintGraphData& Graph : Graphs)
		{
			TSharedRef<FJsonObject> GraphObject = MakeShared<FJsonObject>();
			GraphObject->SetStringField(TEXT("name"), Graph.Name);
			GraphObject->SetBoolField(TEXT("is_pure"), Graph.bIsPure);
			GraphObject->SetBoolField(TEXT("is_const"), Graph.bIsConst);

			// Serialize nodes
			TArray<TSharedPtr<FJsonValue>> NodesArray;
			for (const FBlueprintNodeData& Node : Graph.Nodes)
			{
				TSharedRef<FJsonObject> NodeObject = MakeShared<FJsonObject>();
				NodeObject->SetStringField(TEXT("id"), Node.NodeId);
				NodeObject->SetStringField(TEXT("type"), NodeTypeToString(Node.NodeType));

				if (!Node.FunctionReference.IsEmpty())
				{
					NodeObject->SetStringField(TEXT("function"), Node.FunctionReference);
				}
				if (!Node.EventName.IsEmpty())
				{
					NodeObject->SetStringField(TEXT("event_name"), Node.EventName);
				}
				if (!Node.VariableName.IsEmpty())
				{
					NodeObject->SetStringField(TEXT("variable"), Node.VariableName);
				}

				TSharedRef<FJsonObject> PosObject = MakeShared<FJsonObject>();
				PosObject->SetNumberField(TEXT("x"), Node.Position.X);
				PosObject->SetNumberField(TEXT("y"), Node.Position.Y);
				NodeObject->SetObjectField(TEXT("position"), PosObject);

				// Serialize pins
				TSharedRef<FJsonObject> PinsObject = MakeShared<FJsonObject>();
				for (const FBlueprintPinData& Pin : Node.Pins)
				{
					TSharedRef<FJsonObject> PinObject = MakeShared<FJsonObject>();

					if (Pin.Connections.Num() > 0)
					{
						if (Pin.Connections.Num() == 1)
						{
							PinObject->SetStringField(TEXT("connection"),
								Pin.Connections[0].SourceNodeId + TEXT(".") + Pin.Connections[0].SourcePinName);
						}
						else
						{
							TArray<TSharedPtr<FJsonValue>> ConnsArray;
							for (const FBlueprintPinConnection& Conn : Pin.Connections)
							{
								ConnsArray.Add(MakeShared<FJsonValueString>(Conn.SourceNodeId + TEXT(".") + Conn.SourcePinName));
							}
							PinObject->SetArrayField(TEXT("connections"), ConnsArray);
						}
					}
					else if (!Pin.DefaultValue.IsEmpty())
					{
						PinObject->SetStringField(TEXT("value"), Pin.DefaultValue);
					}

					PinsObject->SetObjectField(Pin.Name, PinObject);
				}
				NodeObject->SetObjectField(TEXT("pins"), PinsObject);

				NodesArray.Add(MakeShared<FJsonValueObject>(NodeObject));
			}
			GraphObject->SetArrayField(TEXT("nodes"), NodesArray);

			GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObject));
		}
		return GraphsArray;
	};

	BlueprintObject->SetArrayField(TEXT("functions"), SerializeGraphs(Data.Functions));
	BlueprintObject->SetArrayField(TEXT("event_graphs"), SerializeGraphs(Data.EventGraphs));

	RootObject->SetObjectField(TEXT("blueprint"), BlueprintObject);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject, Writer);

	return OutputString;
}

bool UBlueprintJSONParser::ParseBlueprintObject(const TSharedPtr<FJsonObject>& JsonObject, FBlueprintData& OutData, FString& OutError)
{
	if (!JsonObject.IsValid())
	{
		OutError = TEXT("Invalid JSON object");
		return false;
	}

	OutData.Name = JsonObject->GetStringField(TEXT("name"));
	OutData.ParentClass = JsonObject->GetStringField(TEXT("parent_class"));

	// Parse variables
	const TArray<TSharedPtr<FJsonValue>>* VariablesArray;
	if (JsonObject->TryGetArrayField(TEXT("variables"), VariablesArray))
	{
		for (const TSharedPtr<FJsonValue>& VarValue : *VariablesArray)
		{
			const TSharedPtr<FJsonObject>* VarObject;
			if (VarValue->TryGetObject(VarObject))
			{
				FBlueprintVariableData VarData;
				if (ParseVariable(*VarObject, VarData, OutError))
				{
					OutData.Variables.Add(VarData);
				}
				else
				{
					return false;
				}
			}
		}
	}

	// Parse functions
	const TArray<TSharedPtr<FJsonValue>>* FunctionsArray;
	if (JsonObject->TryGetArrayField(TEXT("functions"), FunctionsArray))
	{
		for (const TSharedPtr<FJsonValue>& FuncValue : *FunctionsArray)
		{
			const TSharedPtr<FJsonObject>* FuncObject;
			if (FuncValue->TryGetObject(FuncObject))
			{
				FBlueprintGraphData GraphData;
				GraphData.bIsFunction = true;
				if (ParseGraph(*FuncObject, GraphData, OutError))
				{
					OutData.Functions.Add(GraphData);
				}
				else
				{
					return false;
				}
			}
		}
	}

	// Parse event graphs (support both "event_graphs" and legacy "events")
	const TArray<TSharedPtr<FJsonValue>>* EventGraphsArray = nullptr;
	if (JsonObject->TryGetArrayField(TEXT("event_graphs"), EventGraphsArray) ||
		JsonObject->TryGetArrayField(TEXT("events"), EventGraphsArray))
	{
		for (const TSharedPtr<FJsonValue>& GraphValue : *EventGraphsArray)
		{
			const TSharedPtr<FJsonObject>* GraphObject;
			if (GraphValue->TryGetObject(GraphObject))
			{
				FBlueprintGraphData GraphData;
				GraphData.bIsFunction = false;
				if (ParseGraph(*GraphObject, GraphData, OutError))
				{
					OutData.EventGraphs.Add(GraphData);
				}
				else
				{
					return false;
				}
			}
		}
	}

	// Parse macros
	const TArray<TSharedPtr<FJsonValue>>* MacrosArray;
	if (JsonObject->TryGetArrayField(TEXT("macros"), MacrosArray))
	{
		for (const TSharedPtr<FJsonValue>& MacroValue : *MacrosArray)
		{
			const TSharedPtr<FJsonObject>* MacroObject;
			if (MacroValue->TryGetObject(MacroObject))
			{
				FBlueprintGraphData GraphData;
				if (ParseGraph(*MacroObject, GraphData, OutError))
				{
					OutData.Macros.Add(GraphData);
				}
				else
				{
					return false;
				}
			}
		}
	}

	return true;
}

bool UBlueprintJSONParser::ParseVariable(const TSharedPtr<FJsonObject>& JsonObject, FBlueprintVariableData& OutData, FString& OutError)
{
	OutData.Name = JsonObject->GetStringField(TEXT("name"));
	OutData.Type = StringToVarType(JsonObject->GetStringField(TEXT("type")));

	JsonObject->TryGetStringField(TEXT("type_class"), OutData.TypeClass);
	JsonObject->TryGetStringField(TEXT("default_value"), OutData.DefaultValue);
	JsonObject->TryGetStringField(TEXT("category"), OutData.Category);
	JsonObject->TryGetStringField(TEXT("tooltip"), OutData.Tooltip);
	JsonObject->TryGetBoolField(TEXT("instance_editable"), OutData.bInstanceEditable);
	JsonObject->TryGetBoolField(TEXT("expose_on_spawn"), OutData.bExposeOnSpawn);
	JsonObject->TryGetBoolField(TEXT("private"), OutData.bPrivate);

	return true;
}

bool UBlueprintJSONParser::ParseGraph(const TSharedPtr<FJsonObject>& JsonObject, FBlueprintGraphData& OutData, FString& OutError)
{
	OutData.Name = JsonObject->GetStringField(TEXT("name"));
	JsonObject->TryGetBoolField(TEXT("is_pure"), OutData.bIsPure);
	JsonObject->TryGetBoolField(TEXT("is_const"), OutData.bIsConst);
	JsonObject->TryGetStringField(TEXT("description"), OutData.Description);

	// Parse function inputs
	const TArray<TSharedPtr<FJsonValue>>* InputsArray;
	if (JsonObject->TryGetArrayField(TEXT("inputs"), InputsArray))
	{
		for (const TSharedPtr<FJsonValue>& InputValue : *InputsArray)
		{
			const TSharedPtr<FJsonObject>* InputObject;
			if (InputValue->TryGetObject(InputObject))
			{
				FBlueprintPinData InputData;
				if (ParsePin(*InputObject, InputData, OutError))
				{
					OutData.Inputs.Add(InputData);
				}
				else
				{
					return false;
				}
			}
		}
	}

	// Parse function outputs
	const TArray<TSharedPtr<FJsonValue>>* OutputsArray;
	if (JsonObject->TryGetArrayField(TEXT("outputs"), OutputsArray))
	{
		for (const TSharedPtr<FJsonValue>& OutputValue : *OutputsArray)
		{
			const TSharedPtr<FJsonObject>* OutputObject;
			if (OutputValue->TryGetObject(OutputObject))
			{
				FBlueprintPinData OutputData;
				if (ParsePin(*OutputObject, OutputData, OutError))
				{
					OutData.Outputs.Add(OutputData);
				}
				else
				{
					return false;
				}
			}
		}
	}

	// Legacy compatibility: map "parameters" and "return_type" into graph pins.
	const TArray<TSharedPtr<FJsonValue>>* ParametersArray;
	if (OutData.Inputs.Num() == 0 && JsonObject->TryGetArrayField(TEXT("parameters"), ParametersArray))
	{
		for (const TSharedPtr<FJsonValue>& ParamValue : *ParametersArray)
		{
			const TSharedPtr<FJsonObject>* ParamObject;
			if (ParamValue->TryGetObject(ParamObject))
			{
				FBlueprintPinData ParamData;
				ParamData.Direction = EBlueprintPinDirection::Input;
				(*ParamObject)->TryGetStringField(TEXT("name"), ParamData.Name);
				(*ParamObject)->TryGetStringField(TEXT("type"), ParamData.Type);
				if (!ParamData.Name.IsEmpty())
				{
					OutData.Inputs.Add(ParamData);
				}
			}
		}
	}

	FString ReturnType;
	if (OutData.Outputs.Num() == 0 && JsonObject->TryGetStringField(TEXT("return_type"), ReturnType))
	{
		if (!ReturnType.IsEmpty() && !ReturnType.Equals(TEXT("void"), ESearchCase::IgnoreCase))
		{
			FBlueprintPinData ReturnData;
			ReturnData.Name = TEXT("ReturnValue");
			ReturnData.Type = ReturnType;
			ReturnData.Direction = EBlueprintPinDirection::Output;
			OutData.Outputs.Add(ReturnData);
		}
	}

	// Parse nodes
	const TArray<TSharedPtr<FJsonValue>>* NodesArray;
	if (JsonObject->TryGetArrayField(TEXT("nodes"), NodesArray))
	{
		TMap<FString, FString> NodeIdRemap;

		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
		{
			const TSharedPtr<FJsonObject>* NodeObject;
			if (NodeValue->TryGetObject(NodeObject))
			{
				FString RawNodeId;
				if (!(*NodeObject)->TryGetStringField(TEXT("node_id"), RawNodeId))
				{
					(*NodeObject)->TryGetStringField(TEXT("id"), RawNodeId);
				}

				FString RawNodeType;
				if (!(*NodeObject)->TryGetStringField(TEXT("node_type"), RawNodeType))
				{
					(*NodeObject)->TryGetStringField(TEXT("type"), RawNodeType);
				}

				const FString NormalizedRawType = NormalizeNodeTypeKey(RawNodeType);
				if (!RawNodeId.IsEmpty() && (NormalizedRawType == TEXT("functionentry") || NormalizedRawType == TEXT("entry")))
				{
					NodeIdRemap.Add(RawNodeId, TEXT("fn_entry"));
				}
				else if (!RawNodeId.IsEmpty() && (NormalizedRawType == TEXT("functionresult") || NormalizedRawType == TEXT("result")))
				{
					NodeIdRemap.Add(RawNodeId, TEXT("fn_result"));
				}

				FBlueprintNodeData NodeData;
				if (ParseNode(*NodeObject, NodeData, OutError))
				{
					if (const FString* CanonicalId = NodeIdRemap.Find(NodeData.NodeId))
					{
						NodeData.NodeId = *CanonicalId;
					}
					OutData.Nodes.Add(NodeData);
				}
				else
				{
					return false;
				}
			}
		}

		// Rewrite connection source ids if they referenced normalized function entry/result aliases.
		if (NodeIdRemap.Num() > 0)
		{
			for (FBlueprintNodeData& Node : OutData.Nodes)
			{
				for (FBlueprintPinData& Pin : Node.Pins)
				{
					for (FBlueprintPinConnection& Conn : Pin.Connections)
					{
						if (const FString* CanonicalId = NodeIdRemap.Find(Conn.SourceNodeId))
						{
							Conn.SourceNodeId = *CanonicalId;
						}
					}
				}
			}
		}
	}

	return true;
}

bool UBlueprintJSONParser::ParseNode(const TSharedPtr<FJsonObject>& JsonObject, FBlueprintNodeData& OutData, FString& OutError)
{
	if (!JsonObject->TryGetStringField(TEXT("node_id"), OutData.NodeId))
	{
		JsonObject->TryGetStringField(TEXT("id"), OutData.NodeId);
	}

	FString NodeTypeString;
	bool bHasTypeString = JsonObject->TryGetStringField(TEXT("node_type"), NodeTypeString);
	if (!bHasTypeString)
	{
		bHasTypeString = JsonObject->TryGetStringField(TEXT("type"), NodeTypeString);
	}
	const FString NormalizedNodeTypeKey = NormalizeNodeTypeKey(NodeTypeString);

	if (bHasTypeString)
	{
		OutData.NodeType = StringToNodeType(NodeTypeString);
	}
	else
	{
		double NodeTypeNumber = 0.0;
		bool bHasTypeNumber = JsonObject->TryGetNumberField(TEXT("node_type"), NodeTypeNumber);
		if (!bHasTypeNumber)
		{
			bHasTypeNumber = JsonObject->TryGetNumberField(TEXT("type"), NodeTypeNumber);
		}

		if (bHasTypeNumber)
		{
			const int32 NodeTypeInt = static_cast<int32>(NodeTypeNumber);
			const int32 UnknownValue = static_cast<int32>(EBlueprintNodeType::Unknown);
			if (NodeTypeInt >= 0 && NodeTypeInt <= UnknownValue)
			{
				OutData.NodeType = static_cast<EBlueprintNodeType>(NodeTypeInt);
			}
		}
	}

	JsonObject->TryGetStringField(TEXT("function"), OutData.FunctionReference);
	JsonObject->TryGetStringField(TEXT("event_name"), OutData.EventName);
	JsonObject->TryGetStringField(TEXT("variable"), OutData.VariableName);
	JsonObject->TryGetStringField(TEXT("target_class"), OutData.TargetClass);
	JsonObject->TryGetStringField(TEXT("value"), OutData.LiteralValue);
	JsonObject->TryGetStringField(TEXT("comment"), OutData.Comment);

	// Parse position
	const TSharedPtr<FJsonObject>* PosObject;
	if (JsonObject->TryGetObjectField(TEXT("position"), PosObject))
	{
		(*PosObject)->TryGetNumberField(TEXT("x"), OutData.Position.X);
		(*PosObject)->TryGetNumberField(TEXT("y"), OutData.Position.Y);
	}

	// Parse pins
	const TSharedPtr<FJsonObject>* PinsObject;
	if (JsonObject->TryGetObjectField(TEXT("pins"), PinsObject))
	{
		auto AddConnectionFromString = [](const FString& ConnectionStr, FBlueprintPinData& PinData)
		{
			int32 DotIndex;
			if (ConnectionStr.FindChar('.', DotIndex))
			{
				FBlueprintPinConnection Conn;
				Conn.SourceNodeId = ConnectionStr.Left(DotIndex);
				Conn.SourcePinName = ConnectionStr.Mid(DotIndex + 1);
				PinData.Connections.Add(Conn);
				return true;
			}
			return false;
		};

		auto JsonValueToString = [](const TSharedPtr<FJsonValue>& Value) -> FString
		{
			if (!Value.IsValid())
			{
				return FString();
			}

			switch (Value->Type)
			{
			case EJson::String:
				return Value->AsString();
			case EJson::Number:
				return FString::SanitizeFloat(Value->AsNumber());
			case EJson::Boolean:
				return Value->AsBool() ? TEXT("true") : TEXT("false");
			case EJson::Object:
			case EJson::Array:
			{
				FString Serialized;
				const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
				FJsonSerializer::Serialize(Value.ToSharedRef(), TEXT(""), Writer);
				return Serialized;
			}
			default:
				return FString();
			}
		};

		auto AddConnectionFromValue = [&AddConnectionFromString](const TSharedPtr<FJsonValue>& ConnectionValue, FBlueprintPinData& PinData)
		{
			if (!ConnectionValue.IsValid())
			{
				return;
			}

			if (ConnectionValue->Type == EJson::String)
			{
				AddConnectionFromString(ConnectionValue->AsString(), PinData);
				return;
			}

			if (ConnectionValue->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject> ConnObj = ConnectionValue->AsObject();
				if (!ConnObj.IsValid())
				{
					return;
				}

				FString NodeId;
				FString PinName;
				if (!ConnObj->TryGetStringField(TEXT("node_id"), NodeId))
				{
					ConnObj->TryGetStringField(TEXT("source_node"), NodeId);
					if (NodeId.IsEmpty())
					{
						ConnObj->TryGetStringField(TEXT("node"), NodeId);
					}
				}
				if (!ConnObj->TryGetStringField(TEXT("pin_name"), PinName))
				{
					ConnObj->TryGetStringField(TEXT("source_pin"), PinName);
					if (PinName.IsEmpty())
					{
						ConnObj->TryGetStringField(TEXT("pin"), PinName);
					}
				}

				if (!NodeId.IsEmpty() && !PinName.IsEmpty())
				{
					FBlueprintPinConnection Conn;
					Conn.SourceNodeId = NodeId;
					Conn.SourcePinName = PinName;
					PinData.Connections.Add(Conn);
					return;
				}

				if (ConnObj->HasTypedField<EJson::String>(TEXT("connection")))
				{
					FString InlineConnection;
					if (ConnObj->TryGetStringField(TEXT("connection"), InlineConnection))
					{
						AddConnectionFromString(InlineConnection, PinData);
					}
				}
				else if (ConnObj->HasTypedField<EJson::Object>(TEXT("connection")))
				{
					const TSharedPtr<FJsonObject>* InlineConnObj = nullptr;
					if (ConnObj->TryGetObjectField(TEXT("connection"), InlineConnObj) && InlineConnObj && InlineConnObj->IsValid())
					{
						FString InlineNodeId;
						FString InlinePinName;
						(*InlineConnObj)->TryGetStringField(TEXT("node_id"), InlineNodeId);
						(*InlineConnObj)->TryGetStringField(TEXT("pin_name"), InlinePinName);
						if (!InlineNodeId.IsEmpty() && !InlinePinName.IsEmpty())
						{
							FBlueprintPinConnection Conn;
							Conn.SourceNodeId = InlineNodeId;
							Conn.SourcePinName = InlinePinName;
							PinData.Connections.Add(Conn);
						}
					}
				}
			}
		};

		for (const auto& PinPair : (*PinsObject)->Values)
		{
			FBlueprintPinData PinData;
			PinData.Name = PinPair.Key;

			const TSharedPtr<FJsonObject>* PinObject;
			if (PinPair.Value->TryGetObject(PinObject))
			{
				// Check for single connection, supports both string and object payloads.
				if ((*PinObject)->HasField(TEXT("connection")))
				{
					const TSharedPtr<FJsonValue> ConnectionValue = (*PinObject)->Values.FindRef(TEXT("connection"));
					if (ConnectionValue.IsValid())
					{
						AddConnectionFromValue(ConnectionValue, PinData);
					}
				}

				// Check for multiple connections, supports [ "node.pin", {"node_id":"x","pin_name":"y"} ].
				const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray;
				if ((*PinObject)->TryGetArrayField(TEXT("connections"), ConnectionsArray))
				{
					for (const TSharedPtr<FJsonValue>& ConnValue : *ConnectionsArray)
					{
						AddConnectionFromValue(ConnValue, PinData);
					}
				}

				// Check for default value and normalize scalar/object forms into string payload.
				if ((*PinObject)->HasField(TEXT("value")))
				{
					const TSharedPtr<FJsonValue> DefaultValue = (*PinObject)->Values.FindRef(TEXT("value"));
					if (DefaultValue.IsValid())
					{
						PinData.DefaultValue = JsonValueToString(DefaultValue);
					}
				}
			}

			OutData.Pins.Add(PinData);
		}
	}

	if (OutData.NodeType == EBlueprintNodeType::Unknown)
	{
		const bool bIsFunctionBoundaryHelper =
			NormalizedNodeTypeKey == TEXT("functionentry") ||
			NormalizedNodeTypeKey == TEXT("fnentry") ||
			NormalizedNodeTypeKey == TEXT("fnentrythen") ||
			NormalizedNodeTypeKey == TEXT("entry") ||
			NormalizedNodeTypeKey == TEXT("k2nodefunctionentry") ||
			NormalizedNodeTypeKey == TEXT("functionterminator") ||
			NormalizedNodeTypeKey == TEXT("endgraph") ||
			NormalizedNodeTypeKey == TEXT("functionresult") ||
			NormalizedNodeTypeKey == TEXT("fnresult") ||
			NormalizedNodeTypeKey == TEXT("result") ||
			NormalizedNodeTypeKey == TEXT("k2nodefunctionresult") ||
			OutData.NodeId.Equals(TEXT("fn_entry"), ESearchCase::IgnoreCase) ||
			OutData.NodeId.Equals(TEXT("fn_result"), ESearchCase::IgnoreCase);
		if (bIsFunctionBoundaryHelper)
		{
			return true;
		}

		const EBlueprintNodeType InferredType = InferNodeTypeFromData(OutData);
		if (InferredType != EBlueprintNodeType::Unknown)
		{
			OutData.NodeType = InferredType;
			return true;
		}

		// Some models occasionally emit placeholder node objects without type/id.
		// Keep parsing resilient by turning them into ignorable comment nodes.
		if (OutData.NodeId.IsEmpty() && !bHasTypeString)
		{
			OutData.NodeId = FString::Printf(
				TEXT("auto_unknown_%s"),
				*FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
			OutData.NodeType = EBlueprintNodeType::Comment;
			if (OutData.Comment.IsEmpty())
			{
				OutData.Comment = TEXT("Auto-recovered unknown node");
			}
			return true;
		}

		if (bHasTypeString)
		{
			OutError = FString::Printf(TEXT("Unknown node_type '%s' for node '%s'"), *NodeTypeString, *OutData.NodeId);
		}
		else
		{
			OutError = FString::Printf(TEXT("Unknown or missing node_type for node '%s'"), *OutData.NodeId);
		}
		return false;
	}

	return true;
}

bool UBlueprintJSONParser::ParsePin(const TSharedPtr<FJsonObject>& JsonObject, FBlueprintPinData& OutData, FString& OutError)
{
	OutData.Name = JsonObject->GetStringField(TEXT("name"));

	FString DirectionStr;
	if (JsonObject->TryGetStringField(TEXT("direction"), DirectionStr))
	{
		OutData.Direction = DirectionStr.Equals(TEXT("output"), ESearchCase::IgnoreCase)
			? EBlueprintPinDirection::Output
			: EBlueprintPinDirection::Input;
	}

	JsonObject->TryGetStringField(TEXT("type"), OutData.Type);
	JsonObject->TryGetStringField(TEXT("default_value"), OutData.DefaultValue);

	return true;
}

bool UBlueprintJSONParser::ParseConnection(const TSharedPtr<FJsonObject>& JsonObject, FBlueprintPinConnection& OutData, FString& OutError)
{
	OutData.SourceNodeId = JsonObject->GetStringField(TEXT("node_id"));
	OutData.SourcePinName = JsonObject->GetStringField(TEXT("pin_name"));
	return true;
}

EBlueprintNodeType UBlueprintJSONParser::StringToNodeType(const FString& TypeString)
{
	if (TypeString.IsEmpty())
	{
		return EBlueprintNodeType::Unknown;
	}

	int32 NumericType = INDEX_NONE;
	if (LexTryParseString(NumericType, *TypeString))
	{
		const int32 UnknownValue = static_cast<int32>(EBlueprintNodeType::Unknown);
		if (NumericType >= 0 && NumericType <= UnknownValue)
		{
			return static_cast<EBlueprintNodeType>(NumericType);
		}
	}

	const FString Key = NormalizeNodeTypeKey(TypeString);
	if (const EBlueprintNodeType* Found = GetNodeTypeMap().Find(Key))
	{
		return *Found;
	}

	return EBlueprintNodeType::Unknown;
}

EBlueprintVarType UBlueprintJSONParser::StringToVarType(const FString& TypeString)
{
	static TMap<FString, EBlueprintVarType> TypeMap = {
		{TEXT("bool"), EBlueprintVarType::Boolean},
		{TEXT("boolean"), EBlueprintVarType::Boolean},
		{TEXT("int"), EBlueprintVarType::Integer},
		{TEXT("integer"), EBlueprintVarType::Integer},
		{TEXT("int32"), EBlueprintVarType::Integer},
		{TEXT("float"), EBlueprintVarType::Float},
		{TEXT("double"), EBlueprintVarType::Float},
		{TEXT("string"), EBlueprintVarType::String},
		{TEXT("name"), EBlueprintVarType::Name},
		{TEXT("text"), EBlueprintVarType::Text},
		{TEXT("vector"), EBlueprintVarType::Vector},
		{TEXT("rotator"), EBlueprintVarType::Rotator},
		{TEXT("transform"), EBlueprintVarType::Transform},
		{TEXT("object"), EBlueprintVarType::Object},
		{TEXT("class"), EBlueprintVarType::Class},
		{TEXT("struct"), EBlueprintVarType::Struct},
		{TEXT("enum"), EBlueprintVarType::Enum},
		{TEXT("array"), EBlueprintVarType::Array},
		{TEXT("set"), EBlueprintVarType::Set},
		{TEXT("map"), EBlueprintVarType::Map},
	};

	if (const EBlueprintVarType* Found = TypeMap.Find(TypeString.ToLower()))
	{
		return *Found;
	}

	return EBlueprintVarType::Object;
}

FString UBlueprintJSONParser::NodeTypeToString(EBlueprintNodeType Type)
{
	switch (Type)
	{
	case EBlueprintNodeType::Event_BeginPlay: return TEXT("BeginPlay");
	case EBlueprintNodeType::Event_Tick: return TEXT("Tick");
	case EBlueprintNodeType::Event_Custom: return TEXT("CustomEvent");
	case EBlueprintNodeType::Event_Input: return TEXT("InputEvent");
	case EBlueprintNodeType::Flow_Branch: return TEXT("Branch");
	case EBlueprintNodeType::Flow_Sequence: return TEXT("Sequence");
	case EBlueprintNodeType::Flow_ForLoop: return TEXT("ForLoop");
	case EBlueprintNodeType::Flow_ForEachLoop: return TEXT("ForEachLoop");
	case EBlueprintNodeType::Flow_WhileLoop: return TEXT("WhileLoop");
	case EBlueprintNodeType::Flow_Switch: return TEXT("Switch");
	case EBlueprintNodeType::Flow_DoOnce: return TEXT("DoOnce");
	case EBlueprintNodeType::Flow_Gate: return TEXT("Gate");
	case EBlueprintNodeType::Flow_Delay: return TEXT("Delay");
	case EBlueprintNodeType::CallFunction: return TEXT("CallFunction");
	case EBlueprintNodeType::PureFunction: return TEXT("PureFunction");
	case EBlueprintNodeType::Variable_Get: return TEXT("Get");
	case EBlueprintNodeType::Variable_Set: return TEXT("Set");
	case EBlueprintNodeType::Math_Add: return TEXT("Add");
	case EBlueprintNodeType::Math_Subtract: return TEXT("Subtract");
	case EBlueprintNodeType::Math_Multiply: return TEXT("Multiply");
	case EBlueprintNodeType::Math_Divide: return TEXT("Divide");
	case EBlueprintNodeType::Compare_Equal: return TEXT("Equal");
	case EBlueprintNodeType::Compare_NotEqual: return TEXT("NotEqual");
	case EBlueprintNodeType::Compare_Greater: return TEXT("Greater");
	case EBlueprintNodeType::Compare_Less: return TEXT("Less");
	case EBlueprintNodeType::Compare_GreaterEqual: return TEXT("GreaterEqual");
	case EBlueprintNodeType::Compare_LessEqual: return TEXT("LessEqual");
	case EBlueprintNodeType::Logic_And: return TEXT("And");
	case EBlueprintNodeType::Logic_Or: return TEXT("Or");
	case EBlueprintNodeType::Logic_Not: return TEXT("Not");
	case EBlueprintNodeType::Cast: return TEXT("Cast");
	case EBlueprintNodeType::MakeStruct: return TEXT("MakeStruct");
	case EBlueprintNodeType::BreakStruct: return TEXT("BreakStruct");
	case EBlueprintNodeType::Array_Add: return TEXT("ArrayAdd");
	case EBlueprintNodeType::Array_Remove: return TEXT("ArrayRemove");
	case EBlueprintNodeType::Array_Get: return TEXT("ArrayGet");
	case EBlueprintNodeType::Array_Set: return TEXT("ArraySet");
	case EBlueprintNodeType::Array_Length: return TEXT("ArrayLength");
	case EBlueprintNodeType::Array_Clear: return TEXT("ArrayClear");
	case EBlueprintNodeType::Literal: return TEXT("Literal");
	case EBlueprintNodeType::Comment: return TEXT("Comment");
	case EBlueprintNodeType::Reroute: return TEXT("Reroute");
	case EBlueprintNodeType::Return: return TEXT("Return");
	default: return TEXT("Unknown");
	}
}

FString UBlueprintJSONParser::VarTypeToString(EBlueprintVarType Type)
{
	switch (Type)
	{
	case EBlueprintVarType::Boolean: return TEXT("boolean");
	case EBlueprintVarType::Integer: return TEXT("integer");
	case EBlueprintVarType::Float: return TEXT("float");
	case EBlueprintVarType::String: return TEXT("string");
	case EBlueprintVarType::Name: return TEXT("name");
	case EBlueprintVarType::Text: return TEXT("text");
	case EBlueprintVarType::Vector: return TEXT("vector");
	case EBlueprintVarType::Rotator: return TEXT("rotator");
	case EBlueprintVarType::Transform: return TEXT("transform");
	case EBlueprintVarType::Object: return TEXT("object");
	case EBlueprintVarType::Class: return TEXT("class");
	case EBlueprintVarType::Struct: return TEXT("struct");
	case EBlueprintVarType::Enum: return TEXT("enum");
	case EBlueprintVarType::Array: return TEXT("array");
	case EBlueprintVarType::Set: return TEXT("set");
	case EBlueprintVarType::Map: return TEXT("map");
	default: return TEXT("object");
	}
}
