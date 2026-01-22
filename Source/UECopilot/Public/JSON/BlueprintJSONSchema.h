// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintJSONSchema.generated.h"

/**
 * Node type enumeration
 */
UENUM(BlueprintType)
enum class EBlueprintNodeType : uint8
{
	// Events
	Event_BeginPlay			UMETA(DisplayName = "Event BeginPlay"),
	Event_Tick				UMETA(DisplayName = "Event Tick"),
	Event_Custom			UMETA(DisplayName = "Custom Event"),
	Event_Input				UMETA(DisplayName = "Input Event"),

	// Flow Control
	Flow_Branch				UMETA(DisplayName = "Branch"),
	Flow_Sequence			UMETA(DisplayName = "Sequence"),
	Flow_ForLoop			UMETA(DisplayName = "For Loop"),
	Flow_ForLoopWithBreak	UMETA(DisplayName = "For Loop With Break"),
	Flow_ForEachLoop		UMETA(DisplayName = "For Each Loop"),
	Flow_WhileLoop			UMETA(DisplayName = "While Loop"),
	Flow_Switch				UMETA(DisplayName = "Switch"),
	Flow_DoOnce				UMETA(DisplayName = "Do Once"),
	Flow_Gate				UMETA(DisplayName = "Gate"),
	Flow_Delay				UMETA(DisplayName = "Delay"),

	// Functions
	CallFunction			UMETA(DisplayName = "Call Function"),
	PureFunction			UMETA(DisplayName = "Pure Function"),

	// Variables
	Variable_Get			UMETA(DisplayName = "Get Variable"),
	Variable_Set			UMETA(DisplayName = "Set Variable"),
	Variable_GetLocal		UMETA(DisplayName = "Get Local Variable"),
	Variable_SetLocal		UMETA(DisplayName = "Set Local Variable"),

	// Math
	Math_Add				UMETA(DisplayName = "Add"),
	Math_Subtract			UMETA(DisplayName = "Subtract"),
	Math_Multiply			UMETA(DisplayName = "Multiply"),
	Math_Divide				UMETA(DisplayName = "Divide"),

	// Comparison
	Compare_Equal			UMETA(DisplayName = "Equal"),
	Compare_NotEqual		UMETA(DisplayName = "Not Equal"),
	Compare_Greater			UMETA(DisplayName = "Greater Than"),
	Compare_Less			UMETA(DisplayName = "Less Than"),
	Compare_GreaterEqual	UMETA(DisplayName = "Greater or Equal"),
	Compare_LessEqual		UMETA(DisplayName = "Less or Equal"),

	// Logic
	Logic_And				UMETA(DisplayName = "AND"),
	Logic_Or				UMETA(DisplayName = "OR"),
	Logic_Not				UMETA(DisplayName = "NOT"),

	// Type Conversion
	Cast					UMETA(DisplayName = "Cast"),
	MakeStruct				UMETA(DisplayName = "Make Struct"),
	BreakStruct				UMETA(DisplayName = "Break Struct"),

	// Array Operations
	Array_Add				UMETA(DisplayName = "Array Add"),
	Array_Remove			UMETA(DisplayName = "Array Remove"),
	Array_Get				UMETA(DisplayName = "Array Get"),
	Array_Set				UMETA(DisplayName = "Array Set"),
	Array_Length			UMETA(DisplayName = "Array Length"),
	Array_Clear				UMETA(DisplayName = "Array Clear"),

	// Delegates
	Delegate_Bind			UMETA(DisplayName = "Bind Delegate"),
	Delegate_Unbind			UMETA(DisplayName = "Unbind Delegate"),
	Delegate_Execute		UMETA(DisplayName = "Execute Delegate"),

	// Misc
	Literal					UMETA(DisplayName = "Literal"),
	Comment					UMETA(DisplayName = "Comment"),
	Reroute					UMETA(DisplayName = "Reroute"),
	Return					UMETA(DisplayName = "Return"),

	// Unknown/Custom
	Unknown					UMETA(DisplayName = "Unknown")
};

/**
 * Pin direction
 */
UENUM(BlueprintType)
enum class EBlueprintPinDirection : uint8
{
	Input	UMETA(DisplayName = "Input"),
	Output	UMETA(DisplayName = "Output")
};

/**
 * Variable type
 */
UENUM(BlueprintType)
enum class EBlueprintVarType : uint8
{
	Boolean		UMETA(DisplayName = "Boolean"),
	Integer		UMETA(DisplayName = "Integer"),
	Float		UMETA(DisplayName = "Float"),
	String		UMETA(DisplayName = "String"),
	Name		UMETA(DisplayName = "Name"),
	Text		UMETA(DisplayName = "Text"),
	Vector		UMETA(DisplayName = "Vector"),
	Rotator		UMETA(DisplayName = "Rotator"),
	Transform	UMETA(DisplayName = "Transform"),
	Object		UMETA(DisplayName = "Object"),
	Class		UMETA(DisplayName = "Class"),
	Struct		UMETA(DisplayName = "Struct"),
	Enum		UMETA(DisplayName = "Enum"),
	Array		UMETA(DisplayName = "Array"),
	Set			UMETA(DisplayName = "Set"),
	Map			UMETA(DisplayName = "Map")
};

/**
 * Pin connection representation
 */
USTRUCT(BlueprintType)
struct UECOPILOT_API FBlueprintPinConnection
{
	GENERATED_BODY()

	/** Source node ID */
	UPROPERTY(BlueprintReadWrite, Category = "Connection")
	FString SourceNodeId;

	/** Source pin name */
	UPROPERTY(BlueprintReadWrite, Category = "Connection")
	FString SourcePinName;
};

/**
 * Pin representation
 */
USTRUCT(BlueprintType)
struct UECOPILOT_API FBlueprintPinData
{
	GENERATED_BODY()

	/** Pin name */
	UPROPERTY(BlueprintReadWrite, Category = "Pin")
	FString Name;

	/** Pin direction */
	UPROPERTY(BlueprintReadWrite, Category = "Pin")
	EBlueprintPinDirection Direction = EBlueprintPinDirection::Input;

	/** Pin type (e.g., "exec", "float", "vector") */
	UPROPERTY(BlueprintReadWrite, Category = "Pin")
	FString Type;

	/** Sub-type for container or object types */
	UPROPERTY(BlueprintReadWrite, Category = "Pin")
	FString SubType;

	/** Default value if not connected */
	UPROPERTY(BlueprintReadWrite, Category = "Pin")
	FString DefaultValue;

	/** Connections to other pins */
	UPROPERTY(BlueprintReadWrite, Category = "Pin")
	TArray<FBlueprintPinConnection> Connections;

	/** Is this pin hidden? */
	UPROPERTY(BlueprintReadWrite, Category = "Pin")
	bool bIsHidden = false;
};

/**
 * Node position
 */
USTRUCT(BlueprintType)
struct UECOPILOT_API FNodePosition
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Position")
	float X = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Position")
	float Y = 0.0f;
};

/**
 * Node representation
 */
USTRUCT(BlueprintType)
struct UECOPILOT_API FBlueprintNodeData
{
	GENERATED_BODY()

	/** Unique node identifier */
	UPROPERTY(BlueprintReadWrite, Category = "Node")
	FString NodeId;

	/** Node type */
	UPROPERTY(BlueprintReadWrite, Category = "Node")
	EBlueprintNodeType NodeType = EBlueprintNodeType::Unknown;

	/** For function calls: fully qualified function path */
	UPROPERTY(BlueprintReadWrite, Category = "Node")
	FString FunctionReference;

	/** For custom events: event name */
	UPROPERTY(BlueprintReadWrite, Category = "Node")
	FString EventName;

	/** For variable nodes: variable name */
	UPROPERTY(BlueprintReadWrite, Category = "Node")
	FString VariableName;

	/** For cast nodes: target class */
	UPROPERTY(BlueprintReadWrite, Category = "Node")
	FString TargetClass;

	/** For literal nodes: literal value */
	UPROPERTY(BlueprintReadWrite, Category = "Node")
	FString LiteralValue;

	/** For math/comparison nodes: operand type suffix (e.g., "IntInt", "FloatFloat", "DoubleDouble") */
	UPROPERTY(BlueprintReadWrite, Category = "Node")
	FString OperandType;

	/** For Sequence nodes: number of output pins (default 2) */
	UPROPERTY(BlueprintReadWrite, Category = "Node")
	int32 SequenceOutputCount = 2;

	/** Node comment */
	UPROPERTY(BlueprintReadWrite, Category = "Node")
	FString Comment;

	/** Node position in graph */
	UPROPERTY(BlueprintReadWrite, Category = "Node")
	FNodePosition Position;

	/** Pin data */
	UPROPERTY(BlueprintReadWrite, Category = "Node")
	TArray<FBlueprintPinData> Pins;
};

/**
 * Variable definition
 */
USTRUCT(BlueprintType)
struct UECOPILOT_API FBlueprintVariableData
{
	GENERATED_BODY()

	/** Variable name */
	UPROPERTY(BlueprintReadWrite, Category = "Variable")
	FString Name;

	/** Variable type */
	UPROPERTY(BlueprintReadWrite, Category = "Variable")
	EBlueprintVarType Type = EBlueprintVarType::Boolean;

	/** For Object/Class types: class path */
	UPROPERTY(BlueprintReadWrite, Category = "Variable")
	FString TypeClass;

	/** For Array/Set/Map: element type */
	UPROPERTY(BlueprintReadWrite, Category = "Variable")
	EBlueprintVarType ContainerElementType = EBlueprintVarType::Boolean;

	/** Default value (as string) */
	UPROPERTY(BlueprintReadWrite, Category = "Variable")
	FString DefaultValue;

	/** Category for organization */
	UPROPERTY(BlueprintReadWrite, Category = "Variable")
	FString Category;

	/** Is exposed to editor? */
	UPROPERTY(BlueprintReadWrite, Category = "Variable")
	bool bInstanceEditable = false;

	/** Is exposed on spawn? */
	UPROPERTY(BlueprintReadWrite, Category = "Variable")
	bool bExposeOnSpawn = false;

	/** Is private? */
	UPROPERTY(BlueprintReadWrite, Category = "Variable")
	bool bPrivate = false;

	/** Replication mode */
	UPROPERTY(BlueprintReadWrite, Category = "Variable")
	FString Replication;

	/** Tooltip */
	UPROPERTY(BlueprintReadWrite, Category = "Variable")
	FString Tooltip;
};

/**
 * Function/Event Graph representation
 */
USTRUCT(BlueprintType)
struct UECOPILOT_API FBlueprintGraphData
{
	GENERATED_BODY()

	/** Graph name */
	UPROPERTY(BlueprintReadWrite, Category = "Graph")
	FString Name;

	/** Is this a function graph (vs event graph)? */
	UPROPERTY(BlueprintReadWrite, Category = "Graph")
	bool bIsFunction = false;

	/** Function inputs (for function graphs) */
	UPROPERTY(BlueprintReadWrite, Category = "Graph")
	TArray<FBlueprintPinData> Inputs;

	/** Function outputs (for function graphs) */
	UPROPERTY(BlueprintReadWrite, Category = "Graph")
	TArray<FBlueprintPinData> Outputs;

	/** Is pure function (no exec pins)? */
	UPROPERTY(BlueprintReadWrite, Category = "Graph")
	bool bIsPure = false;

	/** Is const function? */
	UPROPERTY(BlueprintReadWrite, Category = "Graph")
	bool bIsConst = false;

	/** Nodes in this graph */
	UPROPERTY(BlueprintReadWrite, Category = "Graph")
	TArray<FBlueprintNodeData> Nodes;

	/** Local variables */
	UPROPERTY(BlueprintReadWrite, Category = "Graph")
	TArray<FBlueprintVariableData> LocalVariables;

	/** Description */
	UPROPERTY(BlueprintReadWrite, Category = "Graph")
	FString Description;
};

/**
 * Complete Blueprint representation
 */
USTRUCT(BlueprintType)
struct UECOPILOT_API FBlueprintData
{
	GENERATED_BODY()

	/** Blueprint name */
	UPROPERTY(BlueprintReadWrite, Category = "Blueprint")
	FString Name;

	/** Parent class path */
	UPROPERTY(BlueprintReadWrite, Category = "Blueprint")
	FString ParentClass;

	/** Blueprint variables */
	UPROPERTY(BlueprintReadWrite, Category = "Blueprint")
	TArray<FBlueprintVariableData> Variables;

	/** Function graphs */
	UPROPERTY(BlueprintReadWrite, Category = "Blueprint")
	TArray<FBlueprintGraphData> Functions;

	/** Event graphs */
	UPROPERTY(BlueprintReadWrite, Category = "Blueprint")
	TArray<FBlueprintGraphData> EventGraphs;

	/** Macros */
	UPROPERTY(BlueprintReadWrite, Category = "Blueprint")
	TArray<FBlueprintGraphData> Macros;
};
