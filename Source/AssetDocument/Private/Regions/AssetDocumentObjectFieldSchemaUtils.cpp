// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentObjectFieldSchemaUtils.h"

#include "AssetDocumentJsonRegionUtils.h"

namespace
{
FString ObjectFieldSchemaRegionPath(const FAssetDocumentRegionContext& Context)
{
	return Context.JsonPointer.IsEmpty() ? Context.BodyPath : Context.JsonPointer;
}

FString JsonTypeName(const EJson Type)
{
	switch (Type)
	{
	case EJson::None:
		return TEXT("none");
	case EJson::Null:
		return TEXT("null");
	case EJson::String:
		return TEXT("string");
	case EJson::Number:
		return TEXT("number");
	case EJson::Boolean:
		return TEXT("boolean");
	case EJson::Array:
		return TEXT("array");
	case EJson::Object:
		return TEXT("object");
	default:
		return TEXT("value");
	}
}

FString UnknownFieldMessage(const FAssetDocumentObjectFieldSchema& Schema, const FString& FieldName)
{
	if (!Schema.UnknownFieldMessageFormat.IsEmpty())
	{
		FString Message = Schema.UnknownFieldMessageFormat;
		Message.ReplaceInline(TEXT("%s"), *FieldName);
		return Message;
	}
	return FString::Printf(TEXT("Unknown object field '%s'"), *FieldName);
}

FString TypeMismatchMessage(
	const FAssetDocumentObjectFieldSpec& Field,
	const FAssetDocumentRegionContext& Context)
{
	if (!Field.TypeMismatchMessage.IsEmpty())
	{
		return Field.TypeMismatchMessage;
	}
	return FString::Printf(TEXT("%s.%s must be a %s"), *Context.BodyPath, *Field.Name, *JsonTypeName(Field.Type));
}
}

FString FAssetDocumentObjectFieldSchemaUtils::MakeFieldPath(
	const FAssetDocumentRegionContext& Context,
	const FString& FieldName)
{
	return FString::Printf(
		TEXT("%s/%s"),
		*ObjectFieldSchemaRegionPath(Context),
		*FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(FieldName));
}

FAssetDocumentCapabilityResult FAssetDocumentObjectFieldSchemaUtils::ValidateObjectFields(
	const FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& Object,
	const FAssetDocumentObjectFieldSchema& Schema)
{
	TMap<FString, FAssetDocumentObjectFieldSpec> FieldsByName;
	for (const FAssetDocumentObjectFieldSpec& Field : Schema.Fields)
	{
		if (Field.Name.IsEmpty() || Field.Type == EJson::None)
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				ObjectFieldSchemaRegionPath(Context),
				TEXT("InvalidObjectFieldSchema"),
				TEXT("Object field schema contains an invalid field spec"));
		}
		if (FieldsByName.Contains(Field.Name))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeFieldPath(Context, Field.Name),
				TEXT("InvalidObjectFieldSchema"),
				FString::Printf(TEXT("Object field schema contains duplicate field '%s'"), *Field.Name));
		}
		FieldsByName.Add(Field.Name, Field);
	}

	if (Schema.bRejectUnknownFields)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
		{
			if (!FieldsByName.Contains(Pair.Key))
			{
				return FAssetDocumentJsonRegionUtils::Failure(
					MakeFieldPath(Context, Pair.Key),
					Schema.UnknownFieldCode.IsEmpty() ? TEXT("UnknownObjectField") : Schema.UnknownFieldCode,
					UnknownFieldMessage(Schema, Pair.Key));
			}
		}
	}

	for (const FAssetDocumentObjectFieldSpec& Field : Schema.Fields)
	{
		const TSharedPtr<FJsonValue> Value = Object->TryGetField(Field.Name);
		if (!Value.IsValid())
		{
			if (Field.bRequired)
			{
				return FAssetDocumentJsonRegionUtils::Failure(
					MakeFieldPath(Context, Field.Name),
					Field.MissingCode.IsEmpty() ? TEXT("MissingObjectField") : Field.MissingCode,
					FString::Printf(TEXT("%s.%s is required"), *Context.BodyPath, *Field.Name));
			}
			continue;
		}
		if (Value->Type == EJson::Null && Field.bAllowNull)
		{
			continue;
		}
		if (Value->Type != Field.Type)
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeFieldPath(Context, Field.Name),
				Field.TypeMismatchCode.IsEmpty() ? TEXT("InvalidObjectFieldType") : Field.TypeMismatchCode,
				TypeMismatchMessage(Field, Context));
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated object fields"));
}
