// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BSL/BSLTypes.h"
#include "BSL/BSLLexer.h"

namespace BSL
{

/**
 * Parser - converts tokens to AST
 *
 * Grammar (simplified):
 *
 * blueprint     = "blueprint" IDENTIFIER "extends" IDENTIFIER "{" members "}"
 * members       = (variable | function | event)*
 * variable      = "var" IDENTIFIER ":" type ("=" expression)?
 * function      = "function" IDENTIFIER "(" params ")" ("->" "(" outputs ")")? block
 * event         = "event" IDENTIFIER ("(" params ")")? block
 * params        = (param ("," param)*)?
 * param         = IDENTIFIER ":" type
 * outputs       = (output ("," output)*)?
 * output        = IDENTIFIER ":" type
 * block         = "{" statements "}"
 * statements    = statement*
 * statement     = varDecl | assignment | ifStmt | whileStmt | forStmt | returnStmt | exprStmt
 * varDecl       = "var" IDENTIFIER ":" type ("=" expression)?
 * assignment    = target "=" expression | "(" targets ")" "=" expression
 * ifStmt        = "if" "(" expression ")" block ("else" (ifStmt | block))?
 * whileStmt     = "while" "(" expression ")" block
 * forStmt       = "for" "("? IDENTIFIER "in" expression ".." expression ")"? block
 * returnStmt    = "return" ("(" expressions ")")?
 * expression    = logicOr
 * logicOr       = logicAnd ("||" logicAnd)*
 * logicAnd      = equality ("&&" equality)*
 * equality      = comparison (("==" | "!=") comparison)*
 * comparison    = term (("<" | "<=" | ">" | ">=") term)*
 * term          = factor (("+" | "-") factor)*
 * factor        = unary (("*" | "/" | "%") unary)*
 * unary         = ("!" | "-") unary | postfix
 * postfix       = primary (call | member | index)*
 * call          = "(" arguments ")"
 * member        = "." IDENTIFIER
 * index         = "[" expression "]"
 * primary       = IDENTIFIER | literal | "(" expression ")" | "self"
 */
class FParser
{
public:
	FParser(const FString& Source);

	/** Parse the source and return the AST */
	FParseResult Parse();

private:
	// Blueprint structure
	bool ParseBlueprint(FBlueprint& OutBlueprint);
	bool ParseVariable(FVariable& OutVar);
	bool ParseFunction(FFunction& OutFunc);
	bool ParseEvent(FFunction& OutEvent);
	bool ParseParameters(TArray<FVariable>& OutParams);
	bool ParseOutputs(TArray<FVariable>& OutOutputs);
	FTypeInfo ParseType();

	// Statements
	TSharedPtr<FStatement> ParseStatement();
	TSharedPtr<FStatement> ParseVarDecl();
	TSharedPtr<FStatement> ParseAssignment();
	TSharedPtr<FStatement> ParseIf();
	TSharedPtr<FStatement> ParseWhile();
	TSharedPtr<FStatement> ParseFor();
	TSharedPtr<FStatement> ParseSwitch();
	TSharedPtr<FStatement> ParseRawNode();
	TSharedPtr<FStatement> ParseReturn();
	TSharedPtr<FStatement> ParseExpressionStatement();
	TArray<TSharedPtr<FStatement>> ParseBlock();

	// Expressions (precedence climbing)
	TSharedPtr<FExpression> ParseExpression();
	TSharedPtr<FExpression> ParseLogicOr();
	TSharedPtr<FExpression> ParseLogicAnd();
	TSharedPtr<FExpression> ParseEquality();
	TSharedPtr<FExpression> ParseComparison();
	TSharedPtr<FExpression> ParseTerm();
	TSharedPtr<FExpression> ParseFactor();
	TSharedPtr<FExpression> ParseUnary();
	TSharedPtr<FExpression> ParsePostfix();
	TSharedPtr<FExpression> ParsePrimary();
	TArray<TSharedPtr<FExpression>> ParseArguments();

	// Helpers
	FToken Advance();
	FToken Peek();
	FToken Previous();
	bool Check(ETokenType Type);
	bool Match(ETokenType Type);
	bool Match(std::initializer_list<ETokenType> Types);
	FToken Consume(ETokenType Type, const FString& Message);
	bool IsAtEnd();

	void Error(const FString& Message);
	void Error(const FToken& Token, const FString& Message);
	void Warning(const FString& Message);
	void Synchronize();

private:
	FLexer Lexer;
	FToken CurrentToken;
	FToken PreviousToken;
	TArray<FString> Errors;
	TArray<FString> Warnings;
	bool bHadError = false;
};

} // namespace BSL
