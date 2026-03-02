// Copyright Epic Games, Inc. All Rights Reserved.

#include "BSL/BSLEmitter.h"

namespace BSL
{

// ============================================================
// 公开接口
// ============================================================

FString FEmitter::Emit(const FBlueprint& Blueprint)
{
	FString Out;

	// blueprint 声明头
	Out += FString::Printf(TEXT("blueprint %s"), *Blueprint.Name);
	if (!Blueprint.ParentClass.IsEmpty())
	{
		Out += FString::Printf(TEXT(" extends %s"), *Blueprint.ParentClass);
	}
	Out += TEXT(" {\n");

	// 成员变量
	for (const FVariable& Var : Blueprint.Variables)
	{
		Out += EmitVariableDecl(Var, 1);
	}

	// 函数与事件（变量与函数之间空一行）
	if (Blueprint.Variables.Num() > 0 && Blueprint.Functions.Num() > 0)
	{
		Out += TEXT("\n");
	}

	for (int32 i = 0; i < Blueprint.Functions.Num(); i++)
	{
		if (i > 0)
		{
			Out += TEXT("\n");
		}
		Out += EmitFunction(Blueprint.Functions[i]);
	}

	Out += TEXT("}\n");
	return Out;
}

// ============================================================
// 私有实现
// ============================================================

FString FEmitter::EmitFunction(const FFunction& Func)
{
	FString Out;
	FString Pad = IndentStr(1);

	// 关键字：event / function；纯函数附加 pure 修饰符
	FString Keyword;
	if (Func.bIsEvent)
	{
		Keyword = TEXT("event");
	}
	else
	{
		// BSLParser 暂不支持 'pure' 修饰符，纯函数与普通函数均输出 function
		// pure 语义通过 FFunction::bIsPure 在 AST 中保留（round-trip 时会丢失）
		Keyword = TEXT("function");
	}

	Out += Pad + Keyword + TEXT(" ") + Func.Name;

	// 输入参数列表
	if (Func.Inputs.Num() > 0)
	{
		Out += TEXT("(");
		for (int32 i = 0; i < Func.Inputs.Num(); i++)
		{
			if (i > 0) Out += TEXT(", ");
			const FVariable& In = Func.Inputs[i];
			Out += In.Name + TEXT(": ") + EmitTypeInfo(In.Type);
		}
		Out += TEXT(")");
	}
	else if (!Func.bIsEvent)
	{
		// 非事件函数写出空括号
		Out += TEXT("()");
	}

	// 输出参数列表（使用箭头语法）
	if (Func.Outputs.Num() > 0)
	{
		Out += TEXT(" -> (");
		for (int32 i = 0; i < Func.Outputs.Num(); i++)
		{
			if (i > 0) Out += TEXT(", ");
			const FVariable& Outv = Func.Outputs[i];
			Out += Outv.Name + TEXT(": ") + EmitTypeInfo(Outv.Type);
		}
		Out += TEXT(")");
	}

	Out += TEXT(" {\n");

	// 本地变量声明（在函数体开头）
	for (const FVariable& Local : Func.LocalVariables)
	{
		Out += EmitVariableDecl(Local, 2);
	}

	// 函数体语句
	Out += EmitBlock(Func.Body, 2);

	Out += Pad + TEXT("}\n");
	return Out;
}

FString FEmitter::EmitBlock(const TArray<TSharedPtr<FStatement>>& Stmts, int32 Indent)
{
	FString Out;
	for (const TSharedPtr<FStatement>& Stmt : Stmts)
	{
		if (Stmt.IsValid())
		{
			Out += EmitStatement(*Stmt, Indent);
		}
	}
	return Out;
}

FString FEmitter::EmitStatement(const FStatement& Stmt, int32 Indent)
{
	FString Pad = IndentStr(Indent);

	switch (Stmt.Type)
	{
	// ----------------------------------------------------------
	// var x: type [= default]
	// ----------------------------------------------------------
	case EStatementType::VariableDecl:
		return EmitVariableDecl(Stmt.DeclaredVariable, Indent);

	// ----------------------------------------------------------
	// target = value
	// ----------------------------------------------------------
	case EStatementType::Assignment:
		{
			FString ValStr = Stmt.AssignValue.IsValid()
				? EmitExpression(*Stmt.AssignValue)
				: TEXT("/* null */");
			return Pad + Stmt.AssignTarget + TEXT(" = ") + ValStr + TEXT("\n");
		}

	// ----------------------------------------------------------
	// (a, b, ...) = value
	// ----------------------------------------------------------
	case EStatementType::MultiAssignment:
		{
			FString Targets = TEXT("(");
			for (int32 i = 0; i < Stmt.MultiAssignTargets.Num(); i++)
			{
				if (i > 0) Targets += TEXT(", ");
				Targets += Stmt.MultiAssignTargets[i];
			}
			Targets += TEXT(")");

			FString ValStr = Stmt.AssignValue.IsValid()
				? EmitExpression(*Stmt.AssignValue)
				: TEXT("/* null */");

			return Pad + Targets + TEXT(" = ") + ValStr + TEXT("\n");
		}

	// ----------------------------------------------------------
	// arr[index] = value
	// ----------------------------------------------------------
	case EStatementType::ArraySet:
		{
			FString IndexStr = Stmt.AssignIndexExpr.IsValid()
				? EmitExpression(*Stmt.AssignIndexExpr)
				: TEXT("0");
			FString ValStr = Stmt.AssignValue.IsValid()
				? EmitExpression(*Stmt.AssignValue)
				: TEXT("/* null */");
			return Pad + Stmt.AssignTarget + TEXT("[") + IndexStr + TEXT("] = ") + ValStr + TEXT("\n");
		}

	// ----------------------------------------------------------
	// ExpressionStmt：函数调用作为语句
	// ----------------------------------------------------------
	case EStatementType::ExpressionStmt:
		{
			if (Stmt.Expression.IsValid())
			{
				return Pad + EmitExpression(*Stmt.Expression) + TEXT("\n");
			}
			return Pad + TEXT("// [empty expression statement]\n");
		}

	// ----------------------------------------------------------
	// return / return expr / return (a, b)
	// ----------------------------------------------------------
	case EStatementType::Return:
		{
			if (Stmt.ReturnValues.Num() == 0)
			{
				return Pad + TEXT("return\n");
			}
			if (Stmt.ReturnValues.Num() == 1)
			{
				FString ValStr = Stmt.ReturnValues[0].IsValid()
					? EmitExpression(*Stmt.ReturnValues[0])
					: TEXT("/* null */");
				return Pad + TEXT("return ") + ValStr + TEXT("\n");
			}
			// 多返回值
			FString Parts;
			for (int32 i = 0; i < Stmt.ReturnValues.Num(); i++)
			{
				if (i > 0) Parts += TEXT(", ");
				Parts += Stmt.ReturnValues[i].IsValid()
					? EmitExpression(*Stmt.ReturnValues[i])
					: TEXT("/* null */");
			}
			return Pad + TEXT("return (") + Parts + TEXT(")\n");
		}

	// ----------------------------------------------------------
	// break / continue
	// ----------------------------------------------------------
	case EStatementType::Break:
		return Pad + TEXT("break\n");

	case EStatementType::Continue:
		return Pad + TEXT("continue\n");

	// ----------------------------------------------------------
	// if (cond) { ... } [else { ... }]
	// ----------------------------------------------------------
	case EStatementType::If:
		{
			FString CondStr = Stmt.Condition.IsValid()
				? EmitExpression(*Stmt.Condition)
				: TEXT("/* null */");

			FString Out = Pad + TEXT("if (") + CondStr + TEXT(") {\n");
			Out += EmitBlock(Stmt.ThenBody, Indent + 1);
			Out += Pad + TEXT("}");

			if (Stmt.ElseBody.Num() > 0)
			{
				Out += TEXT(" else {\n");
				Out += EmitBlock(Stmt.ElseBody, Indent + 1);
				Out += Pad + TEXT("}");
			}
			return Out + TEXT("\n");
		}

	// ----------------------------------------------------------
	// while (cond) { ... }
	// ----------------------------------------------------------
	case EStatementType::While:
		{
			FString CondStr = Stmt.Condition.IsValid()
				? EmitExpression(*Stmt.Condition)
				: TEXT("/* null */");

			FString Out = Pad + TEXT("while (") + CondStr + TEXT(") {\n");
			Out += EmitBlock(Stmt.LoopBody, Indent + 1);
			return Out + Pad + TEXT("}\n");
		}

	// ----------------------------------------------------------
	// for i in start..end { ... }
	// ----------------------------------------------------------
	case EStatementType::For:
		{
			// 避免浮点字面量与 .. 粘连（如 1.0..10 → 1.0. 被 Lexer 误识别为浮点）
			auto SafeRange = [](const FString& S, EExpressionType T) -> FString {
				if (T == EExpressionType::Literal_Float) return TEXT("(") + S + TEXT(")");
				return S;
			};
			FString StartStr = Stmt.LoopStart.IsValid() ? EmitExpression(*Stmt.LoopStart) : TEXT("0");
			FString EndStr   = Stmt.LoopEnd.IsValid()   ? EmitExpression(*Stmt.LoopEnd)   : TEXT("0");
			EExpressionType StartType = Stmt.LoopStart.IsValid() ? Stmt.LoopStart->Type : EExpressionType::Literal_Int;
			EExpressionType EndType   = Stmt.LoopEnd.IsValid()   ? Stmt.LoopEnd->Type   : EExpressionType::Literal_Int;
			StartStr = SafeRange(StartStr, StartType);
			EndStr   = SafeRange(EndStr,   EndType);
			FString Out = FString::Printf(TEXT("%sfor %s in %s..%s {\n"), *Pad, *Stmt.LoopVariable, *StartStr, *EndStr);
			Out += EmitBlock(Stmt.LoopBody, Indent + 1);
			return Out + Pad + TEXT("}\n");
		}

	// ----------------------------------------------------------
	// foreach item in collection { ... }
	// ----------------------------------------------------------
	case EStatementType::ForEach:
		{
			FString CollStr = Stmt.LoopCollection.IsValid()
				? EmitExpression(*Stmt.LoopCollection)
				: TEXT("[]");

			FString Out = FString::Printf(TEXT("%sforeach %s in %s {\n"),
				*Pad, *Stmt.LoopVariable, *CollStr);
			Out += EmitBlock(Stmt.LoopBody, Indent + 1);
			return Out + Pad + TEXT("}\n");
		}

	// ----------------------------------------------------------
	// switch (expr) { case v: { ... } default: { ... } }
	// ----------------------------------------------------------
	case EStatementType::Switch:
		{
			FString CondStr = Stmt.Condition.IsValid()
				? EmitExpression(*Stmt.Condition)
				: TEXT("/* null */");

			FString Out = Pad + TEXT("switch (") + CondStr + TEXT(") {\n");
			FString CasePad = IndentStr(Indent + 1);

			for (const FSwitchCase& SwitchCase : Stmt.SwitchCases)
			{
				if (SwitchCase.Value.IsValid())
				{
					// case 值:
					Out += CasePad + TEXT("case ") + EmitExpression(*SwitchCase.Value) + TEXT(":\n");
				}
				else
				{
					// default:
					Out += CasePad + TEXT("default:\n");
				}
				Out += EmitBlock(SwitchCase.Body, Indent + 2);
			}
			return Out + Pad + TEXT("}\n");
		}

	// ----------------------------------------------------------
	// Block（嵌套块，不常用但需要支持）
	// ----------------------------------------------------------
	case EStatementType::Block:
		{
			FString Out = Pad + TEXT("{\n");
			Out += EmitBlock(Stmt.Statements, Indent + 1);
			return Out + Pad + TEXT("}\n");
		}

	// ----------------------------------------------------------
	// @node("NodeType", { json params }) 逃生舱
	// ----------------------------------------------------------
	case EStatementType::RawNode:
		{
			// 对节点类型名中的特殊字符（引号、反斜杠）进行转义，保证 round-trip 时 Lexer 能正确解析
			FString SafeType = Stmt.RawNodeType
				.Replace(TEXT("\\"), TEXT("\\\\"))
				.Replace(TEXT("\""), TEXT("\\\""));
			FString ParamsStr = Stmt.RawNodeParamsJson.IsEmpty() ? TEXT("{}") : Stmt.RawNodeParamsJson;
			return Pad + FString::Printf(TEXT("@node(\"%s\", %s)\n"), *SafeType, *ParamsStr);
		}

	default:
		return Pad + TEXT("// [unknown statement]\n");
	}
}

FString FEmitter::EmitExpression(const FExpression& Expr)
{
	switch (Expr.Type)
	{
	// ----------------------------------------------------------
	// 字面值
	// ----------------------------------------------------------
	case EExpressionType::Literal_Bool:
		return Expr.BoolValue ? TEXT("true") : TEXT("false");

	case EExpressionType::Literal_Int:
		return FString::FromInt(Expr.IntValue);

	case EExpressionType::Literal_Float:
		// SanitizeFloat 会确保有小数点，符合 BSL 浮点数格式
		return FString::SanitizeFloat(Expr.FloatValue);

	case EExpressionType::Literal_String:
		// 字符串字面值加双引号，并转义内部双引号
		{
			FString Escaped = Expr.StringValue.Replace(TEXT("\\"), TEXT("\\\\"));
			Escaped = Escaped.Replace(TEXT("\""), TEXT("\\\""));
			return TEXT("\"") + Escaped + TEXT("\"");
		}

	case EExpressionType::Literal_Vector:
		// Vector(X, Y, Z)
		return FString::Printf(TEXT("Vector(%s, %s, %s)"),
			*FString::SanitizeFloat(Expr.VectorValue.X),
			*FString::SanitizeFloat(Expr.VectorValue.Y),
			*FString::SanitizeFloat(Expr.VectorValue.Z));

	case EExpressionType::Literal_Rotator:
		// Rotator(Pitch, Yaw, Roll)
		return FString::Printf(TEXT("Rotator(%s, %s, %s)"),
			*FString::SanitizeFloat(Expr.RotatorValue.Pitch),
			*FString::SanitizeFloat(Expr.RotatorValue.Yaw),
			*FString::SanitizeFloat(Expr.RotatorValue.Roll));

	// ----------------------------------------------------------
	// 引用
	// ----------------------------------------------------------
	case EExpressionType::Variable:
		return Expr.Name;

	case EExpressionType::Self:
		return TEXT("self");

	// ----------------------------------------------------------
	// 成员访问：object.member
	// ----------------------------------------------------------
	case EExpressionType::MemberAccess:
		{
			FString ObjStr = Expr.Object.IsValid()
				? EmitExpression(*Expr.Object)
				: TEXT("/* null */");
			return ObjStr + TEXT(".") + Expr.MemberName;
		}

	// ----------------------------------------------------------
	// 函数调用：[object.]name(args...)
	// ----------------------------------------------------------
	case EExpressionType::FunctionCall:
		{
			FString Callee;
			if (Expr.Object.IsValid())
			{
				Callee = EmitExpression(*Expr.Object) + TEXT(".") + Expr.Name;
			}
			else
			{
				Callee = Expr.Name;
			}

			FString Args;
			for (int32 i = 0; i < Expr.Arguments.Num(); i++)
			{
				if (i > 0) Args += TEXT(", ");
				Args += Expr.Arguments[i].IsValid()
					? EmitExpression(*Expr.Arguments[i])
					: TEXT("/* null */");
			}
			return Callee + TEXT("(") + Args + TEXT(")");
		}

	// ----------------------------------------------------------
	// 数组下标访问：arr[index]
	// ----------------------------------------------------------
	case EExpressionType::ArrayAccess:
		{
			FString ArrStr = Expr.Left.IsValid()
				? EmitExpression(*Expr.Left)
				: TEXT("/* null */");
			FString IdxStr = Expr.Right.IsValid()
				? EmitExpression(*Expr.Right)
				: TEXT("/* null */");
			return ArrStr + TEXT("[") + IdxStr + TEXT("]");
		}

	// ----------------------------------------------------------
	// 二元运算：(left op right)
	// ----------------------------------------------------------
	case EExpressionType::BinaryOp:
		{
			FString LeftStr = Expr.Left.IsValid()
				? EmitExpression(*Expr.Left)
				: TEXT("?");
			FString RightStr = Expr.Right.IsValid()
				? EmitExpression(*Expr.Right)
				: TEXT("?");

			FString Op;
			switch (Expr.BinaryOp)
			{
			case EBinaryOp::Add:          Op = TEXT(" + ");  break;
			case EBinaryOp::Subtract:     Op = TEXT(" - ");  break;
			case EBinaryOp::Multiply:     Op = TEXT(" * ");  break;
			case EBinaryOp::Divide:       Op = TEXT(" / ");  break;
			case EBinaryOp::Modulo:       Op = TEXT(" % ");  break;
			case EBinaryOp::Equal:        Op = TEXT(" == "); break;
			case EBinaryOp::NotEqual:     Op = TEXT(" != "); break;
			case EBinaryOp::Less:         Op = TEXT(" < ");  break;
			case EBinaryOp::LessEqual:    Op = TEXT(" <= "); break;
			case EBinaryOp::Greater:      Op = TEXT(" > ");  break;
			case EBinaryOp::GreaterEqual: Op = TEXT(" >= "); break;
			case EBinaryOp::And:          Op = TEXT(" && "); break;
			case EBinaryOp::Or:           Op = TEXT(" || "); break;
			default:                      Op = TEXT(" ? ");  break;
			}

			return TEXT("(") + LeftStr + Op + RightStr + TEXT(")");
		}

	// ----------------------------------------------------------
	// 一元运算：-operand 或 !operand
	// ----------------------------------------------------------
	case EExpressionType::UnaryOp:
		{
			FString Operand = Expr.Left.IsValid()
				? EmitExpression(*Expr.Left)
				: TEXT("?");

			switch (Expr.UnaryOp)
			{
			case EUnaryOp::Negate: return TEXT("-") + Operand;
			case EUnaryOp::Not:    return TEXT("!") + Operand;
			default:               return TEXT("(?)") + Operand;
			}
		}

	// ----------------------------------------------------------
	// 类型转换：Cast<Type>(expr)
	// ----------------------------------------------------------
	case EExpressionType::Cast:
		{
			FString Inner = Expr.Left.IsValid()
				? EmitExpression(*Expr.Left)
				: TEXT("/* null */");
			return FString::Printf(TEXT("Cast<%s>(%s)"),
				*EmitTypeInfo(Expr.CastType), *Inner);
		}

	// ----------------------------------------------------------
	// 结构体字面值：TypeName(field0, field1, ...)
	// ----------------------------------------------------------
	case EExpressionType::StructLiteral:
		{
			FString Fields;
			for (int32 i = 0; i < Expr.StructFields.Num(); i++)
			{
				if (i > 0) Fields += TEXT(", ");
				Fields += Expr.StructFields[i].IsValid()
					? EmitExpression(*Expr.StructFields[i])
					: TEXT("/* null */");
			}
			return Expr.StructTypeName + TEXT("(") + Fields + TEXT(")");
		}

	default:
		return TEXT("/* unknown expression */");
	}
}

// ============================================================
// 辅助函数
// ============================================================

FString FEmitter::EmitTypeInfo(const FTypeInfo& TypeInfo) const
{
	return TypeInfo.ToString();
}

FString FEmitter::EmitVariableDecl(const FVariable& Var, int32 Indent)
{
	FString Pad = IndentStr(Indent);
	FString Line = Pad + TEXT("var ") + Var.Name + TEXT(": ") + EmitTypeInfo(Var.Type);

	if (Var.DefaultValue.IsValid())
	{
		Line += TEXT(" = ") + EmitExpression(*Var.DefaultValue);
	}

	return Line + TEXT("\n");
}

FString FEmitter::IndentStr(int32 Level) const
{
	FString S;
	// 每层缩进 2 个空格
	for (int32 i = 0; i < Level; i++)
	{
		S += TEXT("  ");
	}
	return S;
}

} // namespace BSL
