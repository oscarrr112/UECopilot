// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BSL/BSLTypes.h"
#include "BSL/BSLParser.h"
#include "JSON/BlueprintJSONSchema.h"

namespace BSL
{

/**
 * Compile result
 */
struct ASSETFACTORYAI_API FCompileResult
{
	bool bSuccess = false;
	FBlueprintData BlueprintData;
	TArray<FString> Errors;
	TArray<FString> Warnings;
};

/**
 * Compiler - converts BSL AST to FBlueprintData
 *
 * This bridges the BSL language to the existing blueprint generation system.
 */
class ASSETFACTORYAI_API FCompiler
{
public:
	/**
	 * Compile BSL source code to FBlueprintData
	 * @param Source - BSL source code
	 * @return Compile result with FBlueprintData
	 */
	static FCompileResult Compile(const FString& Source);

	/**
	 * Compile BSL AST to FBlueprintData
	 * @param Blueprint - Parsed BSL AST
	 * @return Compile result with FBlueprintData
	 */
	static FCompileResult CompileAST(const FBlueprint& Blueprint);

private:
	FCompiler();

	/** Compile the blueprint structure */
	bool CompileBlueprint(const FBlueprint& Source, FBlueprintData& OutData);

	/** Compile a variable declaration */
	FBlueprintVariableData CompileVariable(const FVariable& Var);

	/** Compile an event to event graph */
	bool CompileEvent(const FFunction& Event, FBlueprintGraphData& OutGraph);

	/** Compile a function */
	bool CompileFunction(const FFunction& Func, FBlueprintGraphData& OutGraph);

	/** Compile statements to nodes */
	bool CompileStatements(
		const TArray<TSharedPtr<FStatement>>& Statements,
		TArray<FBlueprintNodeData>& OutNodes,
		const FString& EntryNodeId,
		const FString& EntryPinName);

	/** Compile a single statement */
	bool CompileStatement(
		const FStatement& Stmt,
		TArray<FBlueprintNodeData>& OutNodes,
		FString& InOutLastExecNodeId,
		FString& InOutLastExecPinName);

	/** Compile an expression to nodes (returns the output pin name) */
	FString CompileExpression(
		const FExpression& Expr,
		TArray<FBlueprintNodeData>& OutNodes,
		FString& OutNodeId);

	/** Generate unique node ID */
	FString GenerateNodeId(const FString& Prefix = TEXT("n"));

	/** Map BSL type to blueprint variable type */
	static EBlueprintVarType MapType(EType Type);

	/** Map BSL binary op to comparison node type */
	static EBlueprintNodeType MapBinaryOp(EBinaryOp Op);

	/** Determine the operand type suffix for math/comparison operations */
	FString DetermineOperandType(const FExpression* Left, const FExpression* Right);

	/** Get the type of an expression */
	EType GetExpressionType(const FExpression* Expr);

	/** Add error */
	void Error(const FString& Message);

	/** Add warning */
	void Warning(const FString& Message);

private:
	int32 NodeCounter = 0;
	TArray<FString> Errors;
	TArray<FString> Warnings;

	// Context for current compilation
	const FFunction* CurrentFunction = nullptr;
	TMap<FString, TPair<FString, FString>> VariableNodeMap;  // Variable name -> (node ID, pin name)
	TMap<FString, EType> VariableTypeMap;    // Variable name -> Type

	// For function output parameters: maps output name -> (node id, pin name)
	TMap<FString, TPair<FString, FString>> OutputValueMap;

	/** Check if a name is a function output parameter */
	bool IsFunctionOutputParameter(const FString& Name) const;

	/** Try to resolve function parameter pin names via UE reflection. Returns false if function not found. */
	bool TryResolveParamNames(const FString& FunctionRef, TArray<FString>& OutNames);

	/** Try to resolve function output parameter pin names via UE reflection. Returns false if function not found. */
	bool TryResolveOutParamNames(const FString& FunctionRef, TArray<FString>& OutNames);

	/** Resolve parent class from blueprint AST */
	UClass* ResolvedParentClass = nullptr;

	/** Self-defined function input/output param names (populated during CompileBlueprint) */
	TMap<FString, TArray<FString>> SelfFunctionInputNames;
	TMap<FString, TArray<FString>> SelfFunctionOutputNames;

	/** Compile the Object expression of a method call and connect it to the 'self' Target pin.
	 *  Emits a Warning if the object cannot be compiled. */
	void TryConnectTargetPin(
		FBlueprintNodeData& CallNode,
		const TSharedPtr<FExpression>& ObjectExpr,
		const FString& FunctionName,
		TArray<FBlueprintNodeData>& OutNodes);
};

} // namespace BSL
