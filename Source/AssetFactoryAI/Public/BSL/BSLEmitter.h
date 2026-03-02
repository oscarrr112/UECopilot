// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "BSL/BSLTypes.h"

namespace BSL
{

/**
 * BSLEmitter - 将 FBlueprint AST 序列化回 BSL 文本
 *
 * 输出格式示例：
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
class ASSETFACTORYAI_API FEmitter
{
public:
	/** 将 FBlueprint AST 转换为 BSL 文本 */
	FString Emit(const FBlueprint& Blueprint);

private:
	/** 序列化单个函数或事件 */
	FString EmitFunction(const FFunction& Func);

	/** 序列化单条语句，Indent 为缩进层级 */
	FString EmitStatement(const FStatement& Stmt, int32 Indent);

	/** 序列化一个表达式，返回内联字符串 */
	FString EmitExpression(const FExpression& Expr);

	/** 序列化语句块（多条语句） */
	FString EmitBlock(const TArray<TSharedPtr<FStatement>>& Stmts, int32 Indent);

	/** 序列化变量类型信息 */
	FString EmitTypeInfo(const FTypeInfo& TypeInfo) const;

	/** 序列化变量声明（var name: type [= default]） */
	FString EmitVariableDecl(const FVariable& Var, int32 Indent);

	/** 返回 Level 层级对应的缩进字符串（每层 2 个空格） */
	FString IndentStr(int32 Level) const;
};

} // namespace BSL
