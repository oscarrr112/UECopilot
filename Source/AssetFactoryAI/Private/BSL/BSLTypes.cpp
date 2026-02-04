// Copyright Epic Games, Inc. All Rights Reserved.

#include "BSL/BSLTypes.h"

namespace BSL
{

FTypeInfo FTypeInfo::FromString(const FString& TypeStr)
{
	FTypeInfo Result;

	FString LowerType = TypeStr.ToLower();

	// Handle array types: Array<ElementType>
	if (LowerType.StartsWith(TEXT("array<")) && LowerType.EndsWith(TEXT(">")))
	{
		Result.Type = EType::Array;
		Result.SubType = TypeStr.Mid(6, TypeStr.Len() - 7);  // Extract element type
		return Result;
	}

	// Basic types
	if (LowerType == TEXT("void")) Result.Type = EType::Void;
	else if (LowerType == TEXT("bool") || LowerType == TEXT("boolean")) Result.Type = EType::Bool;
	else if (LowerType == TEXT("int") || LowerType == TEXT("int32") || LowerType == TEXT("integer")) Result.Type = EType::Int;
	else if (LowerType == TEXT("float") || LowerType == TEXT("double")) Result.Type = EType::Float;
	else if (LowerType == TEXT("string") || LowerType == TEXT("fstring")) Result.Type = EType::String;
	else if (LowerType == TEXT("name") || LowerType == TEXT("fname")) Result.Type = EType::Name;
	else if (LowerType == TEXT("text") || LowerType == TEXT("ftext")) Result.Type = EType::Text;
	else if (LowerType == TEXT("vector") || LowerType == TEXT("fvector")) Result.Type = EType::Vector;
	else if (LowerType == TEXT("rotator") || LowerType == TEXT("frotator")) Result.Type = EType::Rotator;
	else if (LowerType == TEXT("transform") || LowerType == TEXT("ftransform")) Result.Type = EType::Transform;
	else
	{
		// Assume it's an object/class type
		Result.Type = EType::Object;
		Result.SubType = TypeStr;
	}

	return Result;
}

FString FTypeInfo::ToString() const
{
	switch (Type)
	{
	case EType::Void: return TEXT("void");
	case EType::Bool: return TEXT("bool");
	case EType::Int: return TEXT("int");
	case EType::Float: return TEXT("float");
	case EType::String: return TEXT("string");
	case EType::Name: return TEXT("name");
	case EType::Text: return TEXT("text");
	case EType::Vector: return TEXT("vector");
	case EType::Rotator: return TEXT("rotator");
	case EType::Transform: return TEXT("transform");
	case EType::Object: return SubType.IsEmpty() ? TEXT("Object") : SubType;
	case EType::Class: return FString::Printf(TEXT("Class<%s>"), *SubType);
	case EType::Array: return FString::Printf(TEXT("Array<%s>"), *SubType);
	default: return TEXT("unknown");
	}
}

// Expression factory methods

TSharedPtr<FExpression> FExpression::MakeBool(bool Value)
{
	TSharedPtr<FExpression> Expr = MakeShared<FExpression>(EExpressionType::Literal_Bool);
	Expr->BoolValue = Value;
	return Expr;
}

TSharedPtr<FExpression> FExpression::MakeInt(int32 Value)
{
	TSharedPtr<FExpression> Expr = MakeShared<FExpression>(EExpressionType::Literal_Int);
	Expr->IntValue = Value;
	return Expr;
}

TSharedPtr<FExpression> FExpression::MakeFloat(float Value)
{
	TSharedPtr<FExpression> Expr = MakeShared<FExpression>(EExpressionType::Literal_Float);
	Expr->FloatValue = Value;
	return Expr;
}

TSharedPtr<FExpression> FExpression::MakeString(const FString& Value)
{
	TSharedPtr<FExpression> Expr = MakeShared<FExpression>(EExpressionType::Literal_String);
	Expr->StringValue = Value;
	return Expr;
}

TSharedPtr<FExpression> FExpression::MakeVariable(const FString& Name)
{
	TSharedPtr<FExpression> Expr = MakeShared<FExpression>(EExpressionType::Variable);
	Expr->Name = Name;
	return Expr;
}

TSharedPtr<FExpression> FExpression::MakeFunctionCall(const FString& Name, const TArray<TSharedPtr<FExpression>>& Args)
{
	TSharedPtr<FExpression> Expr = MakeShared<FExpression>(EExpressionType::FunctionCall);
	Expr->Name = Name;
	Expr->Arguments = Args;
	return Expr;
}

TSharedPtr<FExpression> FExpression::MakeBinaryOp(EBinaryOp Op, TSharedPtr<FExpression> Left, TSharedPtr<FExpression> Right)
{
	TSharedPtr<FExpression> Expr = MakeShared<FExpression>(EExpressionType::BinaryOp);
	Expr->BinaryOp = Op;
	Expr->Left = Left;
	Expr->Right = Right;
	return Expr;
}

TSharedPtr<FExpression> FExpression::MakeUnaryOp(EUnaryOp Op, TSharedPtr<FExpression> Operand)
{
	TSharedPtr<FExpression> Expr = MakeShared<FExpression>(EExpressionType::UnaryOp);
	Expr->UnaryOp = Op;
	Expr->Left = Operand;
	return Expr;
}

// Blueprint helper methods

TArray<FFunction*> FBlueprint::GetEvents()
{
	TArray<FFunction*> Events;
	for (FFunction& Func : Functions)
	{
		if (Func.bIsEvent)
		{
			Events.Add(&Func);
		}
	}
	return Events;
}

TArray<FFunction*> FBlueprint::GetFunctions()
{
	TArray<FFunction*> Funcs;
	for (FFunction& Func : Functions)
	{
		if (!Func.bIsEvent)
		{
			Funcs.Add(&Func);
		}
	}
	return Funcs;
}

} // namespace BSL
