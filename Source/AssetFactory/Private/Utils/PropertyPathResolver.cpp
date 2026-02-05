// Copyright ProjectRPG. All Rights Reserved.

#include "Utils/PropertyPathResolver.h"

TSharedPtr<FJsonValue> FPropertyPathResolver::Resolve(TSharedPtr<FJsonObject> Root, const FString& Path)
{
	if (!Root.IsValid() || Path.IsEmpty())
	{
		return nullptr;
	}

	// Parse path into segments
	TArray<FString> Segments = ParsePath(Path);
	if (Segments.Num() == 0)
	{
		return nullptr;
	}

	// Start with root as a JSON value
	TSharedPtr<FJsonValue> Current = MakeShared<FJsonValueObject>(Root);

	// Resolve each segment
	for (const FString& Segment : Segments)
	{
		Current = ResolveSegment(Current, Segment);
		if (!Current.IsValid())
		{
			return nullptr;
		}
	}

	return Current;
}

TSharedPtr<FJsonObject> FPropertyPathResolver::ResolveMultiple(TSharedPtr<FJsonObject> Root, const TArray<FString>& Paths)
{
	TSharedPtr<FJsonObject> Results = MakeShared<FJsonObject>();

	for (const FString& Path : Paths)
	{
		TSharedPtr<FJsonValue> Value = Resolve(Root, Path);
		if (Value.IsValid())
		{
			Results->SetField(Path, Value);
		}
	}

	return Results;
}

TArray<FString> FPropertyPathResolver::ParsePath(const FString& Path)
{
	TArray<FString> Segments;

	// Split by '.' but handle array brackets
	FString CurrentSegment;
	bool bInBracket = false;

	for (int32 i = 0; i < Path.Len(); ++i)
	{
		TCHAR Char = Path[i];

		if (Char == TEXT('['))
		{
			bInBracket = true;
			CurrentSegment.AppendChar(Char);
		}
		else if (Char == TEXT(']'))
		{
			bInBracket = false;
			CurrentSegment.AppendChar(Char);
		}
		else if (Char == TEXT('.') && !bInBracket)
		{
			// End of segment
			if (!CurrentSegment.IsEmpty())
			{
				Segments.Add(CurrentSegment);
				CurrentSegment.Empty();
			}
		}
		else
		{
			CurrentSegment.AppendChar(Char);
		}
	}

	// Add last segment
	if (!CurrentSegment.IsEmpty())
	{
		Segments.Add(CurrentSegment);
	}

	return Segments;
}

TSharedPtr<FJsonValue> FPropertyPathResolver::ResolveSegment(TSharedPtr<FJsonValue> Current, const FString& Segment)
{
	if (!Current.IsValid())
	{
		return nullptr;
	}

	// Check if this is an array access
	FString PropertyName;
	int32 ArrayIndex = -1;
	bool bWildcard = false;

	if (ParseArrayAccess(Segment, PropertyName, ArrayIndex, bWildcard))
	{
		// First get the property if there's a name before the bracket
		if (!PropertyName.IsEmpty())
		{
			const TSharedPtr<FJsonObject>* ObjPtr;
			if (!Current->TryGetObject(ObjPtr) || !ObjPtr->IsValid())
			{
				return nullptr;
			}

			Current = (*ObjPtr)->TryGetField(PropertyName);
			if (!Current.IsValid())
			{
				return nullptr;
			}
		}

		// Now handle array access
		const TArray<TSharedPtr<FJsonValue>>* ArrayPtr;
		if (!Current->TryGetArray(ArrayPtr))
		{
			return nullptr;
		}

		if (bWildcard)
		{
			// Return all elements as a new array (for further processing)
			// This allows chaining like "Components[*].Name"
			return Current; // Return the array itself
		}
		else
		{
			// Specific index access
			if (ArrayIndex < 0 || ArrayIndex >= ArrayPtr->Num())
			{
				return nullptr;
			}
			return (*ArrayPtr)[ArrayIndex];
		}
	}
	else
	{
		// Simple property access
		const TSharedPtr<FJsonObject>* ObjPtr;
		if (Current->TryGetObject(ObjPtr) && ObjPtr->IsValid())
		{
			return (*ObjPtr)->TryGetField(Segment);
		}

		// Handle wildcard array expansion - apply property to each element
		const TArray<TSharedPtr<FJsonValue>>* ArrayPtr;
		if (Current->TryGetArray(ArrayPtr))
		{
			// This handles "Array[*].Property" - extract Property from each element
			TArray<TSharedPtr<FJsonValue>> Results;
			for (const TSharedPtr<FJsonValue>& Element : *ArrayPtr)
			{
				const TSharedPtr<FJsonObject>* ElementObj;
				if (Element->TryGetObject(ElementObj) && ElementObj->IsValid())
				{
					TSharedPtr<FJsonValue> Value = (*ElementObj)->TryGetField(Segment);
					if (Value.IsValid())
					{
						Results.Add(Value);
					}
				}
			}
			if (Results.Num() > 0)
			{
				return MakeShared<FJsonValueArray>(Results);
			}
		}

		return nullptr;
	}
}

bool FPropertyPathResolver::ParseArrayAccess(const FString& Segment, FString& OutPropertyName, int32& OutIndex, bool& bOutWildcard)
{
	OutPropertyName.Empty();
	OutIndex = -1;
	bOutWildcard = false;

	// Find bracket position
	int32 BracketStart = Segment.Find(TEXT("["));
	if (BracketStart == INDEX_NONE)
	{
		return false; // Not an array access
	}

	int32 BracketEnd = Segment.Find(TEXT("]"), ESearchCase::IgnoreCase, ESearchDir::FromStart, BracketStart);
	if (BracketEnd == INDEX_NONE)
	{
		return false; // Malformed
	}

	// Extract property name (before bracket)
	if (BracketStart > 0)
	{
		OutPropertyName = Segment.Left(BracketStart);
	}

	// Extract index
	FString IndexStr = Segment.Mid(BracketStart + 1, BracketEnd - BracketStart - 1);

	if (IndexStr == TEXT("*"))
	{
		bOutWildcard = true;
	}
	else
	{
		OutIndex = FCString::Atoi(*IndexStr);
	}

	return true;
}
