// Copyright Epic Games, Inc. All Rights Reserved.

#include "BSL/BSLLexer.h"

namespace BSL
{

// Token methods

bool FToken::IsKeyword() const
{
	return Type >= ETokenType::Blueprint && Type <= ETokenType::Default;
}

bool FToken::IsOperator() const
{
	return Type >= ETokenType::Plus && Type <= ETokenType::DotDot;
}

bool FToken::IsLiteral() const
{
	return Type >= ETokenType::Integer && Type <= ETokenType::False;
}

FString FToken::ToString() const
{
	return FString::Printf(TEXT("[%s '%s' at %d:%d]"),
		*TokenTypeToString(Type), *Value, Line, Column);
}

FString FToken::TokenTypeToString(ETokenType Type)
{
	switch (Type)
	{
	case ETokenType::EndOfFile: return TEXT("EOF");
	case ETokenType::Integer: return TEXT("Integer");
	case ETokenType::Float: return TEXT("Float");
	case ETokenType::String: return TEXT("String");
	case ETokenType::True: return TEXT("true");
	case ETokenType::False: return TEXT("false");
	case ETokenType::Identifier: return TEXT("Identifier");
	case ETokenType::Blueprint: return TEXT("blueprint");
	case ETokenType::Extends: return TEXT("extends");
	case ETokenType::Var: return TEXT("var");
	case ETokenType::Event: return TEXT("event");
	case ETokenType::Function: return TEXT("function");
	case ETokenType::If: return TEXT("if");
	case ETokenType::Else: return TEXT("else");
	case ETokenType::While: return TEXT("while");
	case ETokenType::For: return TEXT("for");
	case ETokenType::ForEach: return TEXT("foreach");
	case ETokenType::In: return TEXT("in");
	case ETokenType::Return: return TEXT("return");
	case ETokenType::Break: return TEXT("break");
	case ETokenType::Continue: return TEXT("continue");
	case ETokenType::Self: return TEXT("self");
	case ETokenType::Cast: return TEXT("Cast");
	case ETokenType::Switch: return TEXT("switch");
	case ETokenType::Case: return TEXT("case");
	case ETokenType::Default: return TEXT("default");
	case ETokenType::Plus: return TEXT("+");
	case ETokenType::Minus: return TEXT("-");
	case ETokenType::Star: return TEXT("*");
	case ETokenType::Slash: return TEXT("/");
	case ETokenType::Percent: return TEXT("%");
	case ETokenType::Equal: return TEXT("=");
	case ETokenType::EqualEqual: return TEXT("==");
	case ETokenType::NotEqual: return TEXT("!=");
	case ETokenType::Less: return TEXT("<");
	case ETokenType::LessEqual: return TEXT("<=");
	case ETokenType::Greater: return TEXT(">");
	case ETokenType::GreaterEqual: return TEXT(">=");
	case ETokenType::And: return TEXT("&&");
	case ETokenType::Or: return TEXT("||");
	case ETokenType::Not: return TEXT("!");
	case ETokenType::Arrow: return TEXT("->");
	case ETokenType::Dot: return TEXT(".");
	case ETokenType::DotDot: return TEXT("..");
	case ETokenType::LeftParen: return TEXT("(");
	case ETokenType::RightParen: return TEXT(")");
	case ETokenType::LeftBrace: return TEXT("{");
	case ETokenType::RightBrace: return TEXT("}");
	case ETokenType::LeftBracket: return TEXT("[");
	case ETokenType::RightBracket: return TEXT("]");
	case ETokenType::Comma: return TEXT(",");
	case ETokenType::Colon: return TEXT(":");
	case ETokenType::Semicolon: return TEXT(";");
	case ETokenType::Newline: return TEXT("Newline");
	case ETokenType::Error: return TEXT("Error");
	default: return TEXT("Unknown");
	}
}

// Lexer implementation

FLexer::FLexer(const FString& InSource)
	: Source(InSource)
{
}

FToken FLexer::NextToken()
{
	// Return peeked token if available
	if (PeekedToken.IsSet())
	{
		FToken Token = PeekedToken.GetValue();
		PeekedToken.Reset();
		return Token;
	}

	SkipWhitespace();

	if (IsAtEnd())
	{
		return MakeToken(ETokenType::EndOfFile);
	}

	TCHAR c = Advance();

	// Identifiers and keywords
	if (FChar::IsAlpha(c) || c == '_')
	{
		Current--;
		Column--;
		return ReadIdentifier();
	}

	// Numbers
	if (FChar::IsDigit(c))
	{
		Current--;
		Column--;
		return ReadNumber();
	}

	// String literals
	if (c == '"')
	{
		return ReadString();
	}

	// Operators and delimiters
	switch (c)
	{
	case '+': return MakeToken(ETokenType::Plus, TEXT("+"));
	case '*': return MakeToken(ETokenType::Star, TEXT("*"));
	case '%': return MakeToken(ETokenType::Percent, TEXT("%"));
	case '(': return MakeToken(ETokenType::LeftParen, TEXT("("));
	case ')': return MakeToken(ETokenType::RightParen, TEXT(")"));
	case '{': return MakeToken(ETokenType::LeftBrace, TEXT("{"));
	case '}': return MakeToken(ETokenType::RightBrace, TEXT("}"));
	case '[': return MakeToken(ETokenType::LeftBracket, TEXT("["));
	case ']': return MakeToken(ETokenType::RightBracket, TEXT("]"));
	case ',': return MakeToken(ETokenType::Comma, TEXT(","));
	case ':': return MakeToken(ETokenType::Colon, TEXT(":"));
	case ';': return MakeToken(ETokenType::Semicolon, TEXT(";"));

	case '-':
		if (Match('>')) return MakeToken(ETokenType::Arrow, TEXT("->"));
		return MakeToken(ETokenType::Minus, TEXT("-"));

	case '/':
		if (Match('/'))
		{
			SkipLineComment();
			return NextToken();
		}
		if (Match('*'))
		{
			SkipBlockComment();
			return NextToken();
		}
		return MakeToken(ETokenType::Slash, TEXT("/"));

	case '.':
		if (Match('.')) return MakeToken(ETokenType::DotDot, TEXT(".."));
		return MakeToken(ETokenType::Dot, TEXT("."));

	case '=':
		if (Match('=')) return MakeToken(ETokenType::EqualEqual, TEXT("=="));
		return MakeToken(ETokenType::Equal, TEXT("="));

	case '!':
		if (Match('=')) return MakeToken(ETokenType::NotEqual, TEXT("!="));
		return MakeToken(ETokenType::Not, TEXT("!"));

	case '<':
		if (Match('=')) return MakeToken(ETokenType::LessEqual, TEXT("<="));
		return MakeToken(ETokenType::Less, TEXT("<"));

	case '>':
		if (Match('=')) return MakeToken(ETokenType::GreaterEqual, TEXT(">="));
		return MakeToken(ETokenType::Greater, TEXT(">"));

	case '&':
		if (Match('&')) return MakeToken(ETokenType::And, TEXT("&&"));
		return MakeError(TEXT("Expected '&&'"));

	case '|':
		if (Match('|')) return MakeToken(ETokenType::Or, TEXT("||"));
		return MakeError(TEXT("Expected '||'"));

	case '\n':
		Line++;
		Column = 1;
		return NextToken();  // Skip newlines for now

	default:
		return MakeError(FString::Printf(TEXT("Unexpected character: '%c'"), c));
	}
}

FToken FLexer::PeekToken()
{
	if (!PeekedToken.IsSet())
	{
		PeekedToken = NextToken();
	}
	return PeekedToken.GetValue();
}

bool FLexer::IsAtEnd() const
{
	return Current >= Source.Len();
}

void FLexer::SkipWhitespace()
{
	while (!IsAtEnd())
	{
		TCHAR c = Peek();
		switch (c)
		{
		case ' ':
		case '\r':
		case '\t':
			Advance();
			break;
		case '\n':
			Advance();
			Line++;
			Column = 1;
			break;
		default:
			return;
		}
	}
}

void FLexer::SkipLineComment()
{
	while (!IsAtEnd() && Peek() != '\n')
	{
		Advance();
	}
}

void FLexer::SkipBlockComment()
{
	int32 Depth = 1;
	while (!IsAtEnd() && Depth > 0)
	{
		if (Peek() == '*' && PeekNext() == '/')
		{
			Advance();
			Advance();
			Depth--;
		}
		else if (Peek() == '/' && PeekNext() == '*')
		{
			Advance();
			Advance();
			Depth++;
		}
		else if (Peek() == '\n')
		{
			Line++;
			Column = 0;
			Advance();
		}
		else
		{
			Advance();
		}
	}

	if (Depth > 0)
	{
		Errors.Add(FString::Printf(TEXT("Line %d: Unterminated block comment"), Line));
	}
}

FToken FLexer::ReadIdentifier()
{
	int32 StartColumn = Column;
	FString Value;

	while (!IsAtEnd() && (FChar::IsAlnum(Peek()) || Peek() == '_'))
	{
		Value += Advance();
	}

	ETokenType Type = CheckKeyword(Value);
	return FToken(Type, Value, Line, StartColumn);
}

FToken FLexer::ReadNumber()
{
	int32 StartColumn = Column;
	FString Value;
	bool bIsFloat = false;

	while (!IsAtEnd() && FChar::IsDigit(Peek()))
	{
		Value += Advance();
	}

	// Check for decimal point
	if (Peek() == '.' && FChar::IsDigit(PeekNext()))
	{
		bIsFloat = true;
		Value += Advance();  // consume '.'

		while (!IsAtEnd() && FChar::IsDigit(Peek()))
		{
			Value += Advance();
		}
	}

	// Check for exponent
	if (Peek() == 'e' || Peek() == 'E')
	{
		bIsFloat = true;
		Value += Advance();

		if (Peek() == '+' || Peek() == '-')
		{
			Value += Advance();
		}

		while (!IsAtEnd() && FChar::IsDigit(Peek()))
		{
			Value += Advance();
		}
	}

	// Check for float suffix
	if (Peek() == 'f' || Peek() == 'F')
	{
		bIsFloat = true;
		Advance();  // consume suffix but don't include in value
	}

	return FToken(bIsFloat ? ETokenType::Float : ETokenType::Integer, Value, Line, StartColumn);
}

FToken FLexer::ReadString()
{
	int32 StartColumn = Column - 1;  // -1 because we already consumed the opening quote
	FString Value;

	while (!IsAtEnd() && Peek() != '"')
	{
		if (Peek() == '\n')
		{
			return MakeError(TEXT("Unterminated string literal"));
		}

		if (Peek() == '\\')
		{
			Advance();  // consume backslash
			if (IsAtEnd())
			{
				return MakeError(TEXT("Unterminated string literal"));
			}

			TCHAR Escaped = Advance();
			switch (Escaped)
			{
			case 'n': Value += '\n'; break;
			case 'r': Value += '\r'; break;
			case 't': Value += '\t'; break;
			case '\\': Value += '\\'; break;
			case '"': Value += '"'; break;
			default:
				Value += '\\';
				Value += Escaped;
				break;
			}
		}
		else
		{
			Value += Advance();
		}
	}

	if (IsAtEnd())
	{
		return MakeError(TEXT("Unterminated string literal"));
	}

	Advance();  // consume closing quote
	return FToken(ETokenType::String, Value, Line, StartColumn);
}

TCHAR FLexer::Advance()
{
	if (IsAtEnd()) return '\0';
	Column++;
	return Source[Current++];
}

TCHAR FLexer::Peek() const
{
	if (IsAtEnd()) return '\0';
	return Source[Current];
}

TCHAR FLexer::PeekNext() const
{
	if (Current + 1 >= Source.Len()) return '\0';
	return Source[Current + 1];
}

bool FLexer::Match(TCHAR Expected)
{
	if (IsAtEnd()) return false;
	if (Source[Current] != Expected) return false;
	Current++;
	Column++;
	return true;
}

FToken FLexer::MakeToken(ETokenType Type, const FString& Value)
{
	return FToken(Type, Value, Line, Column - Value.Len());
}

FToken FLexer::MakeError(const FString& Message)
{
	Errors.Add(FString::Printf(TEXT("Line %d, Column %d: %s"), Line, Column, *Message));
	return FToken(ETokenType::Error, Message, Line, Column);
}

ETokenType FLexer::CheckKeyword(const FString& Identifier)
{
	static TMap<FString, ETokenType> Keywords = {
		{TEXT("blueprint"), ETokenType::Blueprint},
		{TEXT("extends"), ETokenType::Extends},
		{TEXT("var"), ETokenType::Var},
		{TEXT("event"), ETokenType::Event},
		{TEXT("function"), ETokenType::Function},
		{TEXT("if"), ETokenType::If},
		{TEXT("else"), ETokenType::Else},
		{TEXT("while"), ETokenType::While},
		{TEXT("for"), ETokenType::For},
		{TEXT("foreach"), ETokenType::ForEach},
		{TEXT("in"), ETokenType::In},
		{TEXT("return"), ETokenType::Return},
		{TEXT("break"), ETokenType::Break},
		{TEXT("continue"), ETokenType::Continue},
		{TEXT("self"), ETokenType::Self},
		{TEXT("true"), ETokenType::True},
		{TEXT("false"), ETokenType::False},
		{TEXT("Cast"), ETokenType::Cast},
		{TEXT("switch"), ETokenType::Switch},
		{TEXT("case"), ETokenType::Case},
		{TEXT("default"), ETokenType::Default},
	};

	if (const ETokenType* Found = Keywords.Find(Identifier))
	{
		return *Found;
	}
	return ETokenType::Identifier;
}

} // namespace BSL
