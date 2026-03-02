// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "BSL/BSLTypes.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

namespace BSL
{

/**
 * 反编译结果
 * 包含还原后的 FBlueprint AST，以及过程中产生的警告和错误信息
 */
struct FDecompileResult
{
	bool bSuccess = false;
	FBlueprint Blueprint;
	TArray<FString> Warnings;
	TArray<FString> Errors;
};

/**
 * FBlueprintDecompiler - 将 UBlueprint（UEdGraph）逆向还原为 BSL AST
 *
 * 支持的节点类型：
 *   - UK2Node_Event        -> FFunction (bIsEvent=true)
 *   - UK2Node_FunctionEntry -> FFunction (bIsEvent=false)
 *   - UK2Node_CallFunction  -> FStatement(ExpressionStmt) / FExpression(FunctionCall)
 *   - UK2Node_IfThenElse   -> FStatement(If)
 *   - UK2Node_VariableSet  -> FStatement(Assignment)
 *   - UK2Node_VariableGet  -> FExpression(Variable)
 *   - 其余节点             -> FStatement(RawNode)，并记录警告
 *
 * 使用方法：
 *   FDecompileResult Result = FBlueprintDecompiler::Decompile(BP);
 *   if (Result.bSuccess) { BSL::FEmitter().Emit(Result.Blueprint); }
 */
class ASSETFACTORYAI_API FBlueprintDecompiler
{
public:
	/** 将整个 UBlueprint 反编译为 BSL AST */
	static FDecompileResult Decompile(UBlueprint* BP);

private:
	FBlueprintDecompiler() = default;

	/**
	 * 将一个函数图（FunctionGraph）反编译，结果写入 OutFunc
	 * 会查找图中的 UK2Node_FunctionEntry 作为入口点
	 */
	void DecompileGraph(UEdGraph* Graph, FFunction& OutFunc);

	/**
	 * 从某个 exec 输出 Pin 开始，沿执行链遍历所有节点，
	 * 返回对应的语句列表
	 */
	TArray<TSharedPtr<FStatement>> WalkExecChain(UEdGraphPin* ExecPin);

	/**
	 * 将单个节点转换为一条语句
	 * 无法识别的节点返回 RawNode 语句并记录警告
	 */
	TSharedPtr<FStatement> NodeToStatement(UEdGraphNode* Node);

	/**
	 * 将数据 Pin（或其连接的源节点）转换为表达式
	 * 如果 Pin 有默认值则返回字面量，否则追溯到源节点
	 */
	TSharedPtr<FExpression> PinToExpression(UEdGraphPin* DataPin);

	/** 反编译过程中产生的警告（非致命） */
	TArray<FString> Warnings;
};

} // namespace BSL
