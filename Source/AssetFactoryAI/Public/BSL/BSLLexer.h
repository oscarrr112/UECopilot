// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace BSL
{

/**
 * Token types
 */
enum class ETokenType : uint8
{
	// End of file
	EndOfFile,

	// Literals
	Integer,
	Float,
	String,
	True,
	False,

	// Identifiers and keywords
	Identifier,
	Blueprint,
	Extends,
	Var,
	Event,
	Function,
	If,
	Else,
	While,
	For,
	ForEach,
	In,
	Return,
	Break,
	Continue,
	Self,
	Cast,
	Switch,
	Case,
	Default,

	// Operators
	Plus,			// +
	Minus,			// -
	Star,			// *
	Slash,			// /
	Percent,		// %
	Equal,			// =
	EqualEqual,		// ==
	NotEqual,		// !=
	Less,			// <
	LessEqual,		// <=
	Greater,		// >
	GreaterEqual,	// >=
	And,			// &&
	Or,				// ||
	Not,			// !
	Arrow,			// ->
	Dot,			// .
	DotDot,			// ..

	// Delimiters
	LeftParen,		// (
	RightParen,		// )
	LeftBrace,		// {
	RightBrace,		// }
	LeftBracket,	// [
	RightBracket,	// ]
	Comma,			// ,
	Colon,			// :
	Semicolon,		// ;
	At,				// @

	// Special
	Newline,
	Error
};

/**
 * Token
 */
struct FToken
{
	ETokenType Type = ETokenType::EndOfFile;
	FString Value;
	int32 Line = 1;
	int32 Column = 1;

	FToken() = default;
	FToken(ETokenType InType, const FString& InValue, int32 InLine, int32 InColumn)
		: Type(InType), Value(InValue), Line(InLine), Column(InColumn) {}

	bool IsKeyword() const;
	bool IsOperator() const;
	bool IsLiteral() const;

	FString ToString() const;
	static FString TokenTypeToString(ETokenType Type);
};

/**
 * Lexer - converts source text to tokens
 */
class FLexer
{
public:
	FLexer(const FString& InSource);

	/** Get the next token */
	FToken NextToken();

	/** Peek at the next token without consuming it */
	FToken PeekToken();

	/** Check if we've reached the end */
	bool IsAtEnd() const;

	/** Get current line number */
	int32 GetLine() const { return Line; }

	/** Get current column number */
	int32 GetColumn() const { return Column; }

	/** Get all errors */
	const TArray<FString>& GetErrors() const { return Errors; }

private:
	/** Skip whitespace (but not newlines, they might be significant) */
	void SkipWhitespace();

	/** Skip single-line comment */
	void SkipLineComment();

	/** Skip multi-line comment */
	void SkipBlockComment();

	/** Read an identifier or keyword */
	FToken ReadIdentifier();

	/** Read a number (integer or float) */
	FToken ReadNumber();

	/** Read a string literal */
	FToken ReadString();

	/** Advance to next character */
	TCHAR Advance();

	/** Peek at current character */
	TCHAR Peek() const;

	/** Peek at next character */
	TCHAR PeekNext() const;

	/** Check if current character matches and consume it */
	bool Match(TCHAR Expected);

	/** Create a token at current position */
	FToken MakeToken(ETokenType Type, const FString& Value = TEXT(""));

	/** Create an error token */
	FToken MakeError(const FString& Message);

	/** Check if a string is a keyword */
	static ETokenType CheckKeyword(const FString& Identifier);

private:
	FString Source;
	int32 Current = 0;
	int32 Line = 1;
	int32 Column = 1;

	TOptional<FToken> PeekedToken;
	TArray<FString> Errors;
};

} // namespace BSL
