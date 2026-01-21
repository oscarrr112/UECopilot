// Copyright Epic Games, Inc. All Rights Reserved.

#include "JSON/BlueprintJSONParser.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

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

	// Parse event graphs
	const TArray<TSharedPtr<FJsonValue>>* EventGraphsArray;
	if (JsonObject->TryGetArrayField(TEXT("event_graphs"), EventGraphsArray))
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

	// Parse nodes
	const TArray<TSharedPtr<FJsonValue>>* NodesArray;
	if (JsonObject->TryGetArrayField(TEXT("nodes"), NodesArray))
	{
		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
		{
			const TSharedPtr<FJsonObject>* NodeObject;
			if (NodeValue->TryGetObject(NodeObject))
			{
				FBlueprintNodeData NodeData;
				if (ParseNode(*NodeObject, NodeData, OutError))
				{
					OutData.Nodes.Add(NodeData);
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

bool UBlueprintJSONParser::ParseNode(const TSharedPtr<FJsonObject>& JsonObject, FBlueprintNodeData& OutData, FString& OutError)
{
	OutData.NodeId = JsonObject->GetStringField(TEXT("id"));
	OutData.NodeType = StringToNodeType(JsonObject->GetStringField(TEXT("type")));

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
		for (const auto& PinPair : (*PinsObject)->Values)
		{
			FBlueprintPinData PinData;
			PinData.Name = PinPair.Key;

			const TSharedPtr<FJsonObject>* PinObject;
			if (PinPair.Value->TryGetObject(PinObject))
			{
				// Check for connection
				FString ConnectionStr;
				if ((*PinObject)->TryGetStringField(TEXT("connection"), ConnectionStr))
				{
					// Parse "node_id.pin_name" format
					int32 DotIndex;
					if (ConnectionStr.FindChar('.', DotIndex))
					{
						FBlueprintPinConnection Conn;
						Conn.SourceNodeId = ConnectionStr.Left(DotIndex);
						Conn.SourcePinName = ConnectionStr.Mid(DotIndex + 1);
						PinData.Connections.Add(Conn);
					}
				}

				// Check for multiple connections
				const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray;
				if ((*PinObject)->TryGetArrayField(TEXT("connections"), ConnectionsArray))
				{
					for (const TSharedPtr<FJsonValue>& ConnValue : *ConnectionsArray)
					{
						FString ConnStr = ConnValue->AsString();
						int32 DotIndex;
						if (ConnStr.FindChar('.', DotIndex))
						{
							FBlueprintPinConnection Conn;
							Conn.SourceNodeId = ConnStr.Left(DotIndex);
							Conn.SourcePinName = ConnStr.Mid(DotIndex + 1);
							PinData.Connections.Add(Conn);
						}
					}
				}

				// Check for default value
				(*PinObject)->TryGetStringField(TEXT("value"), PinData.DefaultValue);
			}

			OutData.Pins.Add(PinData);
		}
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
	static TMap<FString, EBlueprintNodeType> TypeMap = {
		// Events
		{TEXT("Event_BeginPlay"), EBlueprintNodeType::Event_BeginPlay},
		{TEXT("BeginPlay"), EBlueprintNodeType::Event_BeginPlay},
		{TEXT("Event_Tick"), EBlueprintNodeType::Event_Tick},
		{TEXT("Tick"), EBlueprintNodeType::Event_Tick},
		{TEXT("Event_Custom"), EBlueprintNodeType::Event_Custom},
		{TEXT("CustomEvent"), EBlueprintNodeType::Event_Custom},
		{TEXT("Event_Input"), EBlueprintNodeType::Event_Input},
		{TEXT("InputEvent"), EBlueprintNodeType::Event_Input},

		// Flow Control
		{TEXT("Branch"), EBlueprintNodeType::Flow_Branch},
		{TEXT("Sequence"), EBlueprintNodeType::Flow_Sequence},
		{TEXT("ForLoop"), EBlueprintNodeType::Flow_ForLoop},
		{TEXT("ForEachLoop"), EBlueprintNodeType::Flow_ForEachLoop},
		{TEXT("WhileLoop"), EBlueprintNodeType::Flow_WhileLoop},
		{TEXT("Switch"), EBlueprintNodeType::Flow_Switch},
		{TEXT("DoOnce"), EBlueprintNodeType::Flow_DoOnce},
		{TEXT("Gate"), EBlueprintNodeType::Flow_Gate},
		{TEXT("Delay"), EBlueprintNodeType::Flow_Delay},

		// Functions
		{TEXT("CallFunction"), EBlueprintNodeType::CallFunction},
		{TEXT("PureFunction"), EBlueprintNodeType::PureFunction},

		// Variables
		{TEXT("GetVariable"), EBlueprintNodeType::Variable_Get},
		{TEXT("Get"), EBlueprintNodeType::Variable_Get},
		{TEXT("SetVariable"), EBlueprintNodeType::Variable_Set},
		{TEXT("Set"), EBlueprintNodeType::Variable_Set},

		// Math
		{TEXT("Add"), EBlueprintNodeType::Math_Add},
		{TEXT("Subtract"), EBlueprintNodeType::Math_Subtract},
		{TEXT("Multiply"), EBlueprintNodeType::Math_Multiply},
		{TEXT("Divide"), EBlueprintNodeType::Math_Divide},

		// Comparison
		{TEXT("Equal"), EBlueprintNodeType::Compare_Equal},
		{TEXT("NotEqual"), EBlueprintNodeType::Compare_NotEqual},
		{TEXT("Greater"), EBlueprintNodeType::Compare_Greater},
		{TEXT("Less"), EBlueprintNodeType::Compare_Less},
		{TEXT("GreaterEqual"), EBlueprintNodeType::Compare_GreaterEqual},
		{TEXT("LessEqual"), EBlueprintNodeType::Compare_LessEqual},

		// Logic
		{TEXT("And"), EBlueprintNodeType::Logic_And},
		{TEXT("Or"), EBlueprintNodeType::Logic_Or},
		{TEXT("Not"), EBlueprintNodeType::Logic_Not},

		// Cast and Struct
		{TEXT("Cast"), EBlueprintNodeType::Cast},
		{TEXT("MakeStruct"), EBlueprintNodeType::MakeStruct},
		{TEXT("BreakStruct"), EBlueprintNodeType::BreakStruct},

		// Array
		{TEXT("ArrayAdd"), EBlueprintNodeType::Array_Add},
		{TEXT("ArrayRemove"), EBlueprintNodeType::Array_Remove},
		{TEXT("ArrayGet"), EBlueprintNodeType::Array_Get},
		{TEXT("ArraySet"), EBlueprintNodeType::Array_Set},
		{TEXT("ArrayLength"), EBlueprintNodeType::Array_Length},
		{TEXT("ArrayClear"), EBlueprintNodeType::Array_Clear},

		// Misc
		{TEXT("Literal"), EBlueprintNodeType::Literal},
		{TEXT("Comment"), EBlueprintNodeType::Comment},
		{TEXT("Reroute"), EBlueprintNodeType::Reroute},
		{TEXT("Return"), EBlueprintNodeType::Return},
	};

	if (const EBlueprintNodeType* Found = TypeMap.Find(TypeString))
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
