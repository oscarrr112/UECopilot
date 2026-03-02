// Copyright Epic Games, Inc. All Rights Reserved.

#include "BSL/BSLParser.h"

namespace BSL
{

FParser::FParser(const FString& Source)
	: Lexer(Source)
{
	// Prime the parser with the first token
	CurrentToken = Lexer.NextToken();
}

FParseResult FParser::Parse()
{
	FParseResult Result;

	if (ParseBlueprint(Result.Blueprint))
	{
		Result.bSuccess = !bHadError;
	}

	Result.Errors = Errors;
	Result.Warnings = Warnings;

	return Result;
}

bool FParser::ParseBlueprint(FBlueprint& OutBlueprint)
{
	// "blueprint" IDENTIFIER "extends" IDENTIFIER "{" members "}"

	if (!Match(ETokenType::Blueprint))
	{
		Error(TEXT("Expected 'blueprint'"));
		return false;
	}

	FToken NameToken = Consume(ETokenType::Identifier, TEXT("Expected blueprint name"));
	if (NameToken.Type == ETokenType::Error) return false;
	OutBlueprint.Name = NameToken.Value;

	if (!Match(ETokenType::Extends))
	{
		Error(TEXT("Expected 'extends'"));
		return false;
	}

	FToken ParentToken = Consume(ETokenType::Identifier, TEXT("Expected parent class name"));
	if (ParentToken.Type == ETokenType::Error) return false;
	OutBlueprint.ParentClass = ParentToken.Value;

	Consume(ETokenType::LeftBrace, TEXT("Expected '{'"));

	// Parse members
	while (!Check(ETokenType::RightBrace) && !IsAtEnd())
	{
		if (Check(ETokenType::Var))
		{
			FVariable Var;
			if (ParseVariable(Var))
			{
				OutBlueprint.Variables.Add(Var);
			}
		}
		else if (Check(ETokenType::Event))
		{
			FFunction Event;
			if (ParseEvent(Event))
			{
				OutBlueprint.Functions.Add(Event);
			}
		}
		else if (Check(ETokenType::Function))
		{
			FFunction Func;
			if (ParseFunction(Func))
			{
				OutBlueprint.Functions.Add(Func);
			}
		}
		else
		{
			Error(Peek(), TEXT("Expected 'var', 'event', or 'function'"));
			Synchronize();
		}
	}

	Consume(ETokenType::RightBrace, TEXT("Expected '}'"));

	return true;
}

bool FParser::ParseVariable(FVariable& OutVar)
{
	// "var" IDENTIFIER ":" type ("=" expression)?

	Consume(ETokenType::Var, TEXT("Expected 'var'"));

	FToken NameToken = Consume(ETokenType::Identifier, TEXT("Expected variable name"));
	if (NameToken.Type == ETokenType::Error) return false;
	OutVar.Name = NameToken.Value;

	Consume(ETokenType::Colon, TEXT("Expected ':'"));

	OutVar.Type = ParseType();
	if (!OutVar.Type.IsValid())
	{
		Error(TEXT("Expected type"));
		return false;
	}

	// Optional default value
	if (Match(ETokenType::Equal))
	{
		OutVar.DefaultValue = ParseExpression();
	}

	return true;
}

bool FParser::ParseFunction(FFunction& OutFunc)
{
	// "function" IDENTIFIER "(" params ")" ("->" "(" outputs ")")? block

	Consume(ETokenType::Function, TEXT("Expected 'function'"));
	OutFunc.bIsEvent = false;
	OutFunc.Line = Previous().Line;

	FToken NameToken = Consume(ETokenType::Identifier, TEXT("Expected function name"));
	if (NameToken.Type == ETokenType::Error) return false;
	OutFunc.Name = NameToken.Value;

	Consume(ETokenType::LeftParen, TEXT("Expected '('"));
	ParseParameters(OutFunc.Inputs);
	Consume(ETokenType::RightParen, TEXT("Expected ')'"));

	// Optional outputs
	if (Match(ETokenType::Arrow))
	{
		Consume(ETokenType::LeftParen, TEXT("Expected '(' after '->'"));
		ParseOutputs(OutFunc.Outputs);
		Consume(ETokenType::RightParen, TEXT("Expected ')' after outputs"));
	}

	// Body
	OutFunc.Body = ParseBlock();

	return true;
}

bool FParser::ParseEvent(FFunction& OutEvent)
{
	// "event" IDENTIFIER ("(" params ")")? block

	Consume(ETokenType::Event, TEXT("Expected 'event'"));
	OutEvent.bIsEvent = true;
	OutEvent.Line = Previous().Line;

	FToken NameToken = Consume(ETokenType::Identifier, TEXT("Expected event name"));
	if (NameToken.Type == ETokenType::Error) return false;
	OutEvent.Name = NameToken.Value;

	// Optional parameters (e.g., event Tick(DeltaTime: float))
	if (Match(ETokenType::LeftParen))
	{
		ParseParameters(OutEvent.Inputs);
		Consume(ETokenType::RightParen, TEXT("Expected ')'"));
	}

	// Body
	OutEvent.Body = ParseBlock();

	return true;
}

bool FParser::ParseParameters(TArray<FVariable>& OutParams)
{
	// (param ("," param)*)?

	if (Check(ETokenType::RightParen))
	{
		return true;  // No parameters
	}

	do
	{
		FVariable Param;

		FToken NameToken = Consume(ETokenType::Identifier, TEXT("Expected parameter name"));
		if (NameToken.Type == ETokenType::Error) return false;
		Param.Name = NameToken.Value;

		Consume(ETokenType::Colon, TEXT("Expected ':'"));

		Param.Type = ParseType();
		if (!Param.Type.IsValid())
		{
			Error(TEXT("Expected type"));
			return false;
		}

		OutParams.Add(Param);
	} while (Match(ETokenType::Comma));

	return true;
}

bool FParser::ParseOutputs(TArray<FVariable>& OutOutputs)
{
	// (output ("," output)*)?

	if (Check(ETokenType::RightParen))
	{
		return true;  // No outputs
	}

	do
	{
		FVariable Output;
		Output.bIsOutput = true;

		FToken NameToken = Consume(ETokenType::Identifier, TEXT("Expected output name"));
		if (NameToken.Type == ETokenType::Error) return false;
		Output.Name = NameToken.Value;

		Consume(ETokenType::Colon, TEXT("Expected ':'"));

		Output.Type = ParseType();
		if (!Output.Type.IsValid())
		{
			Error(TEXT("Expected type"));
			return false;
		}

		OutOutputs.Add(Output);
	} while (Match(ETokenType::Comma));

	return true;
}

FTypeInfo FParser::ParseType()
{
	FToken TypeToken = Consume(ETokenType::Identifier, TEXT("Expected type name"));
	if (TypeToken.Type == ETokenType::Error) return FTypeInfo();

	FString TypeName = TypeToken.Value;

	// Check for Array<T>
	if (Match(ETokenType::Less))
	{
		FTypeInfo ElementType = ParseType();
		Consume(ETokenType::Greater, TEXT("Expected '>'"));

		FTypeInfo ArrayType;
		ArrayType.Type = EType::Array;
		ArrayType.SubType = ElementType.ToString();
		return ArrayType;
	}

	return FTypeInfo::FromString(TypeName);
}

TSharedPtr<FStatement> FParser::ParseStatement()
{
	if (Match(ETokenType::At))
	{
		return ParseRawNode();
	}
	if (Check(ETokenType::Var))
	{
		return ParseVarDecl();
	}
	if (Check(ETokenType::If))
	{
		return ParseIf();
	}
	if (Check(ETokenType::While))
	{
		return ParseWhile();
	}
	if (Check(ETokenType::For))
	{
		return ParseFor();
	}
	if (Check(ETokenType::Switch))
	{
		return ParseSwitch();
	}
	if (Check(ETokenType::Return))
	{
		return ParseReturn();
	}
	if (Check(ETokenType::Break))
	{
		Advance();
		TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::Break);
		Stmt->Line = Previous().Line;
		return Stmt;
	}
	if (Check(ETokenType::Continue))
	{
		Advance();
		TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::Continue);
		Stmt->Line = Previous().Line;
		return Stmt;
	}

	// Assignment or expression statement
	// Look ahead to see if this is an assignment
	if (Check(ETokenType::Identifier))
	{
		// Could be: x = expr, or x.y = expr, or (a, b) = expr, or just expr
		return ParseAssignment();
	}

	if (Check(ETokenType::LeftParen))
	{
		// Could be multi-assignment: (a, b) = FuncCall()
		return ParseAssignment();
	}

	return ParseExpressionStatement();
}

TSharedPtr<FStatement> FParser::ParseVarDecl()
{
	TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::VariableDecl);
	Stmt->Line = Peek().Line;

	FVariable Var;
	if (!ParseVariable(Var))
	{
		return nullptr;
	}

	Stmt->DeclaredVariable = Var;
	return Stmt;
}

TSharedPtr<FStatement> FParser::ParseAssignment()
{
	int32 StartLine = Peek().Line;

	// Check for multi-assignment: (a, b, c) = FuncCall()
	if (Match(ETokenType::LeftParen))
	{
		TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::MultiAssignment);
		Stmt->Line = StartLine;

		// Parse target list
		do
		{
			FToken NameToken = Consume(ETokenType::Identifier, TEXT("Expected variable name"));
			Stmt->MultiAssignTargets.Add(NameToken.Value);
		} while (Match(ETokenType::Comma));

		Consume(ETokenType::RightParen, TEXT("Expected ')'"));
		Consume(ETokenType::Equal, TEXT("Expected '='"));

		Stmt->AssignValue = ParseExpression();
		return Stmt;
	}

	// Parse left-hand side (could be just an identifier or a member access)
	TSharedPtr<FExpression> LHS = ParsePostfix();

	if (Match(ETokenType::Equal))
	{
		// This is an assignment
		TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::Assignment);
		Stmt->Line = StartLine;

		if (LHS->Type == EExpressionType::Variable)
		{
			Stmt->AssignTarget = LHS->Name;
		}
		else if (LHS->Type == EExpressionType::MemberAccess)
		{
			// For now, store the full expression for member access
			Stmt->Expression = LHS;  // Store LHS expression
			Stmt->AssignTarget = LHS->MemberName;
		}
		else if (LHS->Type == EExpressionType::ArrayAccess)
		{
			// arr[i] = value → ArraySet 语句
			// LHS->Left 是数组变量表达式，LHS->Right 是索引表达式
			Stmt->Type = EStatementType::ArraySet;
			if (LHS->Left.IsValid() && LHS->Left->Type == EExpressionType::Variable)
			{
				Stmt->AssignTarget = LHS->Left->Name;
			}
			else
			{
				Error(TEXT("Array set: expected a simple variable as array target"));
				return nullptr;
			}
			Stmt->AssignIndexExpr = LHS->Right;
		}
		else
		{
			Error(TEXT("Invalid assignment target"));
			return nullptr;
		}

		Stmt->AssignValue = ParseExpression();
		return Stmt;
	}

	// Not an assignment, just an expression statement
	TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::ExpressionStmt);
	Stmt->Line = StartLine;
	Stmt->Expression = LHS;
	return Stmt;
}

TSharedPtr<FStatement> FParser::ParseIf()
{
	TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::If);
	Stmt->Line = Peek().Line;

	Consume(ETokenType::If, TEXT("Expected 'if'"));
	Consume(ETokenType::LeftParen, TEXT("Expected '('"));

	Stmt->Condition = ParseExpression();

	Consume(ETokenType::RightParen, TEXT("Expected ')'"));

	Stmt->ThenBody = ParseBlock();

	if (Match(ETokenType::Else))
	{
		if (Check(ETokenType::If))
		{
			// else if
			TSharedPtr<FStatement> ElseIf = ParseIf();
			Stmt->ElseBody.Add(ElseIf);
		}
		else
		{
			Stmt->ElseBody = ParseBlock();
		}
	}

	return Stmt;
}

TSharedPtr<FStatement> FParser::ParseWhile()
{
	TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::While);
	Stmt->Line = Peek().Line;

	Consume(ETokenType::While, TEXT("Expected 'while'"));
	Consume(ETokenType::LeftParen, TEXT("Expected '('"));

	Stmt->Condition = ParseExpression();

	Consume(ETokenType::RightParen, TEXT("Expected ')'"));

	Stmt->LoopBody = ParseBlock();

	return Stmt;
}

TSharedPtr<FStatement> FParser::ParseFor()
{
	TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::For);
	Stmt->Line = Peek().Line;

	Consume(ETokenType::For, TEXT("Expected 'for'"));

	FToken VarToken = Consume(ETokenType::Identifier, TEXT("Expected loop variable"));
	Stmt->LoopVariable = VarToken.Value;

	Consume(ETokenType::In, TEXT("Expected 'in'"));

	Stmt->LoopStart = ParseExpression();

	Consume(ETokenType::DotDot, TEXT("Expected '..'"));

	Stmt->LoopEnd = ParseExpression();

	Stmt->LoopBody = ParseBlock();

	return Stmt;
}

TSharedPtr<FStatement> FParser::ParseSwitch()
{
	Consume(ETokenType::Switch, TEXT("Expected 'switch'"));
	TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::Switch);
	Stmt->Line = Previous().Line;

	Consume(ETokenType::LeftParen, TEXT("Expected '(' after 'switch'"));
	Stmt->Condition = ParseExpression();
	Consume(ETokenType::RightParen, TEXT("Expected ')' after switch condition"));
	Consume(ETokenType::LeftBrace, TEXT("Expected '{' to start switch body"));

	while (!Check(ETokenType::RightBrace) && !IsAtEnd())
	{
		FSwitchCase SwitchCase;

		if (Check(ETokenType::Case))
		{
			Advance();  // consume 'case'
			SwitchCase.Value = ParseExpression();
			Consume(ETokenType::Colon, TEXT("Expected ':' after case value"));
		}
		else if (Check(ETokenType::Default))
		{
			Advance();  // consume 'default'
			SwitchCase.Value = nullptr;  // nullptr = default case
			Consume(ETokenType::Colon, TEXT("Expected ':' after 'default'"));
		}
		else
		{
			break;
		}

		if (Check(ETokenType::LeftBrace))
		{
			SwitchCase.Body = ParseBlock();
		}

		Stmt->SwitchCases.Add(SwitchCase);
	}

	Consume(ETokenType::RightBrace, TEXT("Expected '}' to end switch"));
	return Stmt;
}

TSharedPtr<FStatement> FParser::ParseReturn()
{
	TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::Return);
	Stmt->Line = Peek().Line;

	Consume(ETokenType::Return, TEXT("Expected 'return'"));

	// Optional return values: return (a, b, c)
	if (Match(ETokenType::LeftParen))
	{
		if (!Check(ETokenType::RightParen))
		{
			do
			{
				Stmt->ReturnValues.Add(ParseExpression());
			} while (Match(ETokenType::Comma));
		}
		Consume(ETokenType::RightParen, TEXT("Expected ')'"));
	}
	else if (!Check(ETokenType::RightBrace) && !IsAtEnd())
	{
		// Single return value without parentheses
		Stmt->ReturnValues.Add(ParseExpression());
	}

	return Stmt;
}

TSharedPtr<FStatement> FParser::ParseExpressionStatement()
{
	TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::ExpressionStmt);
	Stmt->Line = Peek().Line;
	Stmt->Expression = ParseExpression();
	return Stmt;
}

TArray<TSharedPtr<FStatement>> FParser::ParseBlock()
{
	TArray<TSharedPtr<FStatement>> Statements;

	Consume(ETokenType::LeftBrace, TEXT("Expected '{'"));

	while (!Check(ETokenType::RightBrace) && !IsAtEnd())
	{
		TSharedPtr<FStatement> Stmt = ParseStatement();
		if (Stmt)
		{
			Statements.Add(Stmt);
		}
	}

	Consume(ETokenType::RightBrace, TEXT("Expected '}'"));

	return Statements;
}

// Expression parsing with precedence

TSharedPtr<FExpression> FParser::ParseExpression()
{
	return ParseLogicOr();
}

TSharedPtr<FExpression> FParser::ParseLogicOr()
{
	TSharedPtr<FExpression> Expr = ParseLogicAnd();

	while (Match(ETokenType::Or))
	{
		TSharedPtr<FExpression> Right = ParseLogicAnd();
		Expr = FExpression::MakeBinaryOp(EBinaryOp::Or, Expr, Right);
	}

	return Expr;
}

TSharedPtr<FExpression> FParser::ParseLogicAnd()
{
	TSharedPtr<FExpression> Expr = ParseEquality();

	while (Match(ETokenType::And))
	{
		TSharedPtr<FExpression> Right = ParseEquality();
		Expr = FExpression::MakeBinaryOp(EBinaryOp::And, Expr, Right);
	}

	return Expr;
}

TSharedPtr<FExpression> FParser::ParseEquality()
{
	TSharedPtr<FExpression> Expr = ParseComparison();

	while (Match({ETokenType::EqualEqual, ETokenType::NotEqual}))
	{
		EBinaryOp Op = Previous().Type == ETokenType::EqualEqual ? EBinaryOp::Equal : EBinaryOp::NotEqual;
		TSharedPtr<FExpression> Right = ParseComparison();
		Expr = FExpression::MakeBinaryOp(Op, Expr, Right);
	}

	return Expr;
}

TSharedPtr<FExpression> FParser::ParseComparison()
{
	TSharedPtr<FExpression> Expr = ParseTerm();

	while (Match({ETokenType::Less, ETokenType::LessEqual, ETokenType::Greater, ETokenType::GreaterEqual}))
	{
		EBinaryOp Op;
		switch (Previous().Type)
		{
		case ETokenType::Less: Op = EBinaryOp::Less; break;
		case ETokenType::LessEqual: Op = EBinaryOp::LessEqual; break;
		case ETokenType::Greater: Op = EBinaryOp::Greater; break;
		case ETokenType::GreaterEqual: Op = EBinaryOp::GreaterEqual; break;
		default: Op = EBinaryOp::Less; break;
		}
		TSharedPtr<FExpression> Right = ParseTerm();
		Expr = FExpression::MakeBinaryOp(Op, Expr, Right);
	}

	return Expr;
}

TSharedPtr<FExpression> FParser::ParseTerm()
{
	TSharedPtr<FExpression> Expr = ParseFactor();

	while (Match({ETokenType::Plus, ETokenType::Minus}))
	{
		EBinaryOp Op = Previous().Type == ETokenType::Plus ? EBinaryOp::Add : EBinaryOp::Subtract;
		TSharedPtr<FExpression> Right = ParseFactor();
		Expr = FExpression::MakeBinaryOp(Op, Expr, Right);
	}

	return Expr;
}

TSharedPtr<FExpression> FParser::ParseFactor()
{
	TSharedPtr<FExpression> Expr = ParseUnary();

	while (Match({ETokenType::Star, ETokenType::Slash, ETokenType::Percent}))
	{
		EBinaryOp Op;
		switch (Previous().Type)
		{
		case ETokenType::Star: Op = EBinaryOp::Multiply; break;
		case ETokenType::Slash: Op = EBinaryOp::Divide; break;
		case ETokenType::Percent: Op = EBinaryOp::Modulo; break;
		default: Op = EBinaryOp::Multiply; break;
		}
		TSharedPtr<FExpression> Right = ParseUnary();
		Expr = FExpression::MakeBinaryOp(Op, Expr, Right);
	}

	return Expr;
}

TSharedPtr<FExpression> FParser::ParseUnary()
{
	if (Match({ETokenType::Not, ETokenType::Minus}))
	{
		EUnaryOp Op = Previous().Type == ETokenType::Not ? EUnaryOp::Not : EUnaryOp::Negate;
		TSharedPtr<FExpression> Operand = ParseUnary();
		return FExpression::MakeUnaryOp(Op, Operand);
	}

	return ParsePostfix();
}

TSharedPtr<FExpression> FParser::ParsePostfix()
{
	TSharedPtr<FExpression> Expr = ParsePrimary();

	while (true)
	{
		if (Match(ETokenType::LeftParen))
		{
			// Function call
			TArray<TSharedPtr<FExpression>> Args = ParseArguments();
			Consume(ETokenType::RightParen, TEXT("Expected ')'"));

			TSharedPtr<FExpression> Call = MakeShared<FExpression>(EExpressionType::FunctionCall);
			if (Expr->Type == EExpressionType::MemberAccess)
			{
				// Method call: obj.method(args) — preserve target object and method name
				Call->Name = Expr->MemberName;
				Call->Object = Expr->Object;
			}
			else
			{
				// Plain function call: FunctionName(args)
				Call->Name = Expr->Name;
			}
			Call->Arguments = Args;
			Expr = Call;
		}
		else if (Match(ETokenType::Dot))
		{
			// Member access
			FToken MemberToken = Consume(ETokenType::Identifier, TEXT("Expected member name"));

			TSharedPtr<FExpression> Member = MakeShared<FExpression>(EExpressionType::MemberAccess);
			Member->Object = Expr;
			Member->MemberName = MemberToken.Value;
			Expr = Member;
		}
		else if (Match(ETokenType::LeftBracket))
		{
			// Array access
			TSharedPtr<FExpression> Index = ParseExpression();
			Consume(ETokenType::RightBracket, TEXT("Expected ']'"));

			TSharedPtr<FExpression> ArrayAccess = MakeShared<FExpression>(EExpressionType::ArrayAccess);
			ArrayAccess->Left = Expr;
			ArrayAccess->Right = Index;
			Expr = ArrayAccess;
		}
		else
		{
			break;
		}
	}

	return Expr;
}

TSharedPtr<FExpression> FParser::ParsePrimary()
{
	// Literals
	if (Match(ETokenType::True))
	{
		return FExpression::MakeBool(true);
	}
	if (Match(ETokenType::False))
	{
		return FExpression::MakeBool(false);
	}
	if (Match(ETokenType::Integer))
	{
		return FExpression::MakeInt(FCString::Atoi(*Previous().Value));
	}
	if (Match(ETokenType::Float))
	{
		return FExpression::MakeFloat(FCString::Atof(*Previous().Value));
	}
	if (Match(ETokenType::String))
	{
		return FExpression::MakeString(Previous().Value);
	}

	// Self
	if (Match(ETokenType::Self))
	{
		TSharedPtr<FExpression> Expr = MakeShared<FExpression>(EExpressionType::Self);
		return Expr;
	}

	// Parenthesized expression or tuple
	if (Match(ETokenType::LeftParen))
	{
		TSharedPtr<FExpression> Expr = ParseExpression();
		Consume(ETokenType::RightParen, TEXT("Expected ')'"));
		return Expr;
	}

	// Identifier — check for struct literals first: Vector(...) / Rotator(...) / Transform(...)
	if (Check(ETokenType::Identifier))
	{
		const FString IdName = Peek().Value;
		if (IdName == TEXT("Vector") || IdName == TEXT("Rotator") || IdName == TEXT("Transform"))
		{
			Advance();  // consume identifier

			if (Check(ETokenType::LeftParen))
			{
				Advance();  // consume '('
				TSharedPtr<FExpression> StructExpr = MakeShared<FExpression>(EExpressionType::StructLiteral);
				StructExpr->StructTypeName = IdName;
				StructExpr->StructFields = ParseArguments();
				Consume(ETokenType::RightParen, TEXT("Expected ')' after struct fields"));
				return StructExpr;
			}

			// Not a struct literal — identifier was already consumed, treat it as a plain variable
			return FExpression::MakeVariable(IdName);
		}
	}

	// Identifier
	if (Match(ETokenType::Identifier))
	{
		return FExpression::MakeVariable(Previous().Value);
	}

	Error(Peek(), TEXT("Expected expression"));
	return nullptr;
}

TArray<TSharedPtr<FExpression>> FParser::ParseArguments()
{
	TArray<TSharedPtr<FExpression>> Args;

	if (!Check(ETokenType::RightParen))
	{
		do
		{
			Args.Add(ParseExpression());
		} while (Match(ETokenType::Comma));
	}

	return Args;
}

// Helper methods

FToken FParser::Advance()
{
	if (!IsAtEnd())
	{
		PreviousToken = CurrentToken;
		CurrentToken = Lexer.NextToken();
	}
	return PreviousToken;
}

FToken FParser::Peek()
{
	return CurrentToken;
}

FToken FParser::Previous()
{
	return PreviousToken;
}

bool FParser::Check(ETokenType Type)
{
	if (IsAtEnd()) return false;
	return Peek().Type == Type;
}

bool FParser::Match(ETokenType Type)
{
	if (Check(Type))
	{
		Advance();
		return true;
	}
	return false;
}

bool FParser::Match(std::initializer_list<ETokenType> Types)
{
	for (ETokenType Type : Types)
	{
		if (Check(Type))
		{
			Advance();
			return true;
		}
	}
	return false;
}

FToken FParser::Consume(ETokenType Type, const FString& Message)
{
	if (Check(Type)) return Advance();
	Error(Peek(), Message);
	return FToken(ETokenType::Error, Message, Peek().Line, Peek().Column);
}

bool FParser::IsAtEnd()
{
	return Peek().Type == ETokenType::EndOfFile;
}

void FParser::Error(const FString& Message)
{
	Error(Peek(), Message);
}

void FParser::Error(const FToken& Token, const FString& Message)
{
	bHadError = true;
	FString ErrorMsg = FString::Printf(TEXT("Line %d, Column %d: %s (got '%s')"),
		Token.Line, Token.Column, *Message, *Token.Value);
	Errors.Add(ErrorMsg);
}

void FParser::Warning(const FString& Message)
{
	FString WarningMsg = FString::Printf(TEXT("Line %d: %s"), Peek().Line, *Message);
	Warnings.Add(WarningMsg);
}

void FParser::Synchronize()
{
	Advance();

	while (!IsAtEnd())
	{
		// Stop at statement boundaries
		switch (Peek().Type)
		{
		case ETokenType::Var:
		case ETokenType::Function:
		case ETokenType::Event:
		case ETokenType::If:
		case ETokenType::While:
		case ETokenType::For:
		case ETokenType::Return:
		case ETokenType::RightBrace:
			return;
		default:
			Advance();
			break;
		}
	}
}

TSharedPtr<FStatement> FParser::ParseRawNode()
{
	// 已消费 '@'，此处解析 @node("NodeType", { params })
	// 消费 "node" 关键字（作为 Identifier）
	FToken NodeKeyword = Consume(ETokenType::Identifier, TEXT("Expected 'node' after '@'"));
	if (NodeKeyword.Type == ETokenType::Error) return nullptr;
	if (NodeKeyword.Value != TEXT("node"))
	{
		Error(NodeKeyword, TEXT("Expected 'node' after '@'"));
		return nullptr;
	}

	Consume(ETokenType::LeftParen, TEXT("Expected '(' after '@node'"));

	// 消费节点类型字符串
	FToken NodeTypeToken = Consume(ETokenType::String, TEXT("Expected node type string after '@node('"));
	if (NodeTypeToken.Type == ETokenType::Error) return nullptr;

	TSharedPtr<FStatement> Stmt = MakeShared<FStatement>(EStatementType::RawNode);
	Stmt->Line = NodeTypeToken.Line;
	Stmt->RawNodeType = NodeTypeToken.Value;

	// 可选的参数对象：, { key: val, ... }
	if (Check(ETokenType::Comma))
	{
		Advance(); // 消费 ','

		if (Check(ETokenType::LeftBrace))
		{
			// 从 token 流重建 JSON 文本
			FString JsonText;
			int32 Depth = 0;
			bool bDone = false;

			while (!IsAtEnd() && !bDone)
			{
				FToken T = Peek();
				if (T.Type == ETokenType::LeftBrace)
				{
					Advance();
					Depth++;
					JsonText += TEXT("{");
				}
				else if (T.Type == ETokenType::RightBrace)
				{
					Advance();
					Depth--;
					JsonText += TEXT("}");
					if (Depth == 0) bDone = true;
				}
				else if (T.Type == ETokenType::String)
				{
					Advance();
					JsonText += TEXT("\"");
					// 对字符串内部的引号进行转义
					FString Escaped = T.Value.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
					JsonText += Escaped;
					JsonText += TEXT("\"");
				}
				else if (T.Type == ETokenType::Colon)
				{
					Advance();
					JsonText += TEXT(":");
				}
				else if (T.Type == ETokenType::Comma)
				{
					Advance();
					JsonText += TEXT(",");
				}
				else if (T.Type == ETokenType::Integer || T.Type == ETokenType::Float)
				{
					Advance();
					JsonText += T.Value;
				}
				else if (T.Type == ETokenType::True)
				{
					Advance();
					JsonText += TEXT("true");
				}
				else if (T.Type == ETokenType::False)
				{
					Advance();
					JsonText += TEXT("false");
				}
				else if (T.Type == ETokenType::Identifier)
				{
					// JSON 的键名如果没有引号，添加引号
					Advance();
					JsonText += TEXT("\"");
					JsonText += T.Value;
					JsonText += TEXT("\"");
				}
				else
				{
					// 跳过未知 token（防止死循环需退出）
					Advance();
				}
			}

			Stmt->RawNodeParamsJson = JsonText;
		}
	}

	Consume(ETokenType::RightParen, TEXT("Expected ')' to close @node"));
	return Stmt;
}

} // namespace BSL
