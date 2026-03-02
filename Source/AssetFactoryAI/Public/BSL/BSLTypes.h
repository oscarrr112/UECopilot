// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Blueprint Script Language (BSL) - Type Definitions
 *
 * BSL is a simple scripting language that compiles to Unreal Blueprint graphs.
 *
 * Example:
 * ```
 * blueprint BP_Player extends Actor {
 *   var Health: int = 100
 *
 *   event BeginPlay {
 *     PrintString("Hello")
 *   }
 *
 *   function GetHealth() -> (Value: int) {
 *     Value = Health
 *   }
 * }
 * ```
 */

namespace BSL
{

// Forward declarations
struct FExpression;
struct FStatement;

/**
 * BSL Variable Types
 */
enum class EType : uint8
{
	Void,
	Bool,
	Int,
	Float,
	String,
	Name,
	Text,
	Vector,
	Rotator,
	Transform,
	Object,		// Requires SubType (class name)
	Class,		// Requires SubType
	Array,		// Requires SubType (element type)
	Unknown
};

/**
 * Type information with optional subtype
 */
struct FTypeInfo
{
	EType Type = EType::Unknown;
	FString SubType;	// For Object/Class: class name, For Array: element type

	bool IsValid() const { return Type != EType::Unknown; }

	static FTypeInfo FromString(const FString& TypeStr);
	FString ToString() const;
};

/**
 * Variable declaration
 */
struct FVariable
{
	FString Name;
	FTypeInfo Type;
	TSharedPtr<FExpression> DefaultValue;
	bool bIsOutput = false;		// For function output parameters

	FVariable() = default;
	FVariable(const FString& InName, const FTypeInfo& InType)
		: Name(InName), Type(InType) {}
};

/**
 * Expression types
 */
enum class EExpressionType : uint8
{
	// Literals
	Literal_Bool,
	Literal_Int,
	Literal_Float,
	Literal_String,
	Literal_Vector,
	Literal_Rotator,

	// References
	Variable,			// Variable reference
	FunctionCall,		// Function call with arguments
	MemberAccess,		// obj.member
	ArrayAccess,		// arr[index]

	// Operators
	BinaryOp,			// a + b, a && b, etc.
	UnaryOp,			// -a, !a

	// Special
	Self,				// 'self' keyword
	Cast,				// Cast<Type>(expr)
	StructLiteral,		// Vector(x, y, z) / Rotator(p, y, r) / Transform(...)
};

/**
 * Binary operators
 */
enum class EBinaryOp : uint8
{
	// Arithmetic
	Add,			// +
	Subtract,		// -
	Multiply,		// *
	Divide,			// /
	Modulo,			// %

	// Comparison
	Equal,			// ==
	NotEqual,		// !=
	Less,			// <
	LessEqual,		// <=
	Greater,		// >
	GreaterEqual,	// >=

	// Logical
	And,			// &&
	Or,				// ||
};

/**
 * Unary operators
 */
enum class EUnaryOp : uint8
{
	Negate,			// -
	Not				// !
};

/**
 * Expression base
 */
struct FExpression
{
	EExpressionType Type;

	// Literal values
	bool BoolValue = false;
	int32 IntValue = 0;
	float FloatValue = 0.0f;
	FString StringValue;
	FVector VectorValue;
	FRotator RotatorValue;

	// Variable/Function reference
	FString Name;
	TArray<TSharedPtr<FExpression>> Arguments;

	// Binary/Unary operators
	EBinaryOp BinaryOp;
	EUnaryOp UnaryOp;
	TSharedPtr<FExpression> Left;
	TSharedPtr<FExpression> Right;	// nullptr for unary

	// Member access
	TSharedPtr<FExpression> Object;
	FString MemberName;

	// Cast
	FTypeInfo CastType;

	// StructLiteral
	FString StructTypeName;							// "Vector", "Rotator", "Transform"
	TArray<TSharedPtr<FExpression>> StructFields;	// constructor arguments

	// Source location for error reporting
	int32 Line = 0;
	int32 Column = 0;

	FExpression() : Type(EExpressionType::Literal_Int) {}
	explicit FExpression(EExpressionType InType) : Type(InType) {}

	// Factory methods for convenience
	static TSharedPtr<FExpression> MakeBool(bool Value);
	static TSharedPtr<FExpression> MakeInt(int32 Value);
	static TSharedPtr<FExpression> MakeFloat(float Value);
	static TSharedPtr<FExpression> MakeString(const FString& Value);
	static TSharedPtr<FExpression> MakeVariable(const FString& Name);
	static TSharedPtr<FExpression> MakeFunctionCall(const FString& Name, const TArray<TSharedPtr<FExpression>>& Args);
	static TSharedPtr<FExpression> MakeBinaryOp(EBinaryOp Op, TSharedPtr<FExpression> Left, TSharedPtr<FExpression> Right);
	static TSharedPtr<FExpression> MakeUnaryOp(EUnaryOp Op, TSharedPtr<FExpression> Operand);
};

/**
 * Statement types
 */
enum class EStatementType : uint8
{
	// Variable operations
	VariableDecl,		// var x: int = 0
	Assignment,			// x = expr
	MultiAssignment,	// (a, b) = FuncCall()
	ArraySet,			// arr[i] = expr

	// Control flow
	If,					// if (cond) { } else { }
	While,				// while (cond) { }
	For,				// for (i in 0..10) { }
	ForEach,			// foreach (item in array) { }
	Return,				// return or return (a, b)
	Break,				// break
	Continue,			// continue

	// Other
	ExpressionStmt,		// FunctionCall() as statement
	Block,				// { statements }
	Switch,				// switch (x) { case v: {} default: {} }
	RawNode,			// @node("Timeline", {...})
};

/**
 * One arm of a switch statement
 */
struct FSwitchCase
{
	TSharedPtr<FExpression> Value;              // nullptr = default case
	TArray<TSharedPtr<FStatement>> Body;
};

/**
 * Statement base
 */
struct FStatement
{
	EStatementType Type;

	// Variable declaration
	FVariable DeclaredVariable;

	// Assignment
	FString AssignTarget;
	TArray<FString> MultiAssignTargets;
	TSharedPtr<FExpression> AssignValue;
	TSharedPtr<FExpression> AssignIndexExpr;  // 索引表达式（用于 ArraySet）

	// If statement
	TSharedPtr<FExpression> Condition;
	TArray<TSharedPtr<FStatement>> ThenBody;
	TArray<TSharedPtr<FStatement>> ElseBody;

	// Loop statements
	FString LoopVariable;
	TSharedPtr<FExpression> LoopStart;
	TSharedPtr<FExpression> LoopEnd;
	TSharedPtr<FExpression> LoopCollection;
	TArray<TSharedPtr<FStatement>> LoopBody;

	// Block/Body
	TArray<TSharedPtr<FStatement>> Statements;

	// Expression statement
	TSharedPtr<FExpression> Expression;

	// Return values
	TArray<TSharedPtr<FExpression>> ReturnValues;

	// Switch statement
	TArray<FSwitchCase> SwitchCases;

	// RawNode (escape hatch): @node("Timeline", {...})
	FString RawNodeType;
	FString RawNodeParamsJson;

	// Source location
	int32 Line = 0;
	int32 Column = 0;

	FStatement() : Type(EStatementType::Block) {}
	explicit FStatement(EStatementType InType) : Type(InType) {}
};

/**
 * Function/Event declaration
 */
struct FFunction
{
	FString Name;
	bool bIsEvent = false;			// true for events (BeginPlay, Tick, etc.)
	bool bIsPure = false;			// Pure function (no side effects)

	TArray<FVariable> Inputs;		// Input parameters
	TArray<FVariable> Outputs;		// Output parameters (return values)
	TArray<FVariable> LocalVariables;
	TArray<TSharedPtr<FStatement>> Body;

	int32 Line = 0;
};

/**
 * Blueprint declaration - root of the AST
 */
struct FBlueprint
{
	FString Name;
	FString ParentClass;

	TArray<FVariable> Variables;	// Member variables
	TArray<FFunction> Functions;	// Functions and events

	// Get all events
	TArray<FFunction*> GetEvents();

	// Get all non-event functions
	TArray<FFunction*> GetFunctions();
};

/**
 * Parse result
 */
struct FParseResult
{
	bool bSuccess = false;
	FBlueprint Blueprint;
	TArray<FString> Errors;
	TArray<FString> Warnings;
};

} // namespace BSL
