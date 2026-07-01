// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/AssetDocumentGraphFieldRules.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "Dom/JsonObject.h"

namespace
{
FString MakeChildPath(const FString& BasePath, const FString& FieldName)
{
	if (BasePath.IsEmpty())
	{
		return FString::Printf(TEXT("/%s"), *FAssetDocumentGraphFieldRules::MakeJsonPathToken(FieldName));
	}
	return FString::Printf(TEXT("%s/%s"), *BasePath, *FAssetDocumentGraphFieldRules::MakeJsonPathToken(FieldName));
}

FAssetDocumentGraphFieldRuleResult FailureAtField(
	const FAssetDocumentGraphFieldRuleContext& Context,
	const FString& Code,
	const FString& Message)
{
	return FAssetDocumentGraphFieldRuleResult::Failure(Code, Context.JsonPath, Message);
}

FAssetDocumentGraphFieldRuleResult RequireObject(
	const FAssetDocumentGraphFieldRuleContext& Context,
	const TSharedPtr<FJsonValue>& Value,
	const FString& Code,
	TSharedPtr<FJsonObject>& OutObject)
{
	if (!Value.IsValid() || Value->Type != EJson::Object)
	{
		return FailureAtField(Context, Code, TEXT("Expected a JSON object"));
	}

	OutObject = Value->AsObject();
	if (!OutObject.IsValid())
	{
		return FailureAtField(Context, Code, TEXT("Expected a JSON object"));
	}

	return FAssetDocumentGraphFieldRuleResult::Success();
}

FAssetDocumentGraphFieldRuleResult RequireString(
	const FAssetDocumentGraphFieldRuleContext& Context,
	const TSharedPtr<FJsonValue>& Value,
	const FString& Code,
	const FString& Message)
{
	FString StringValue;
	if (!Value.IsValid() || Value->Type != EJson::String || !Value->TryGetString(StringValue) || StringValue.TrimStartAndEnd().IsEmpty())
	{
		return FailureAtField(Context, Code, Message);
	}
	return FAssetDocumentGraphFieldRuleResult::Success();
}

FAssetDocumentGraphFieldRuleResult RequireObjectKind(
	const FAssetDocumentGraphFieldRuleContext& Context,
	const TSharedPtr<FJsonObject>& Object,
	const FString& ExpectedKind,
	const FString& Code)
{
	const TSharedPtr<FJsonValue> KindValue = Object.IsValid() ? Object->TryGetField(TEXT("Kind")) : nullptr;
	FString Kind;
	if (!KindValue.IsValid() || KindValue->Type != EJson::String || !KindValue->TryGetString(Kind) || Kind != ExpectedKind)
	{
		return FAssetDocumentGraphFieldRuleResult::Failure(
			Code,
			MakeChildPath(Context.JsonPath, TEXT("Kind")),
			FString::Printf(TEXT("Kind must be %s"), *ExpectedKind));
	}
	return FAssetDocumentGraphFieldRuleResult::Success();
}

bool TryReadNonEmptyStringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, FString& OutValue)
{
	if (!Object.IsValid() || !Object->TryGetStringField(FieldName, OutValue))
	{
		return false;
	}

	OutValue = OutValue.TrimStartAndEnd();
	return !OutValue.IsEmpty();
}

FAssetDocumentGraphFieldRuleResult ValidateAssetRef(
	const FAssetDocumentGraphFieldRuleContext& Context,
	const TSharedPtr<FJsonValue>& Value)
{
	TSharedPtr<FJsonObject> Object;
	const FAssetDocumentGraphFieldRuleResult ObjectResult =
		RequireObject(Context, Value, TEXT("InvalidGraphFieldAssetRef"), Object);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}

	const FAssetDocumentGraphFieldRuleResult KindResult =
		RequireObjectKind(Context, Object, TEXT("AssetRef"), TEXT("InvalidGraphFieldAssetRefKind"));
	if (!KindResult.bSuccess)
	{
		return KindResult;
	}

	FString Path;
	if (!TryReadNonEmptyStringField(Object, TEXT("Path"), Path))
	{
		return FAssetDocumentGraphFieldRuleResult::Failure(
			TEXT("MissingGraphFieldAssetRefPath"),
			MakeChildPath(Context.JsonPath, TEXT("Path")),
			TEXT("AssetRef.Path must be a non-empty string"));
	}

	return FAssetDocumentGraphFieldRuleResult::Success();
}

FAssetDocumentGraphFieldRuleResult ValidateClassRef(
	const FAssetDocumentGraphFieldRuleContext& Context,
	const TSharedPtr<FJsonValue>& Value)
{
	TSharedPtr<FJsonObject> Object;
	const FAssetDocumentGraphFieldRuleResult ObjectResult =
		RequireObject(Context, Value, TEXT("InvalidGraphFieldClassRef"), Object);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}

	const FAssetDocumentGraphFieldRuleResult KindResult =
		RequireObjectKind(Context, Object, TEXT("ClassRef"), TEXT("InvalidGraphFieldClassRefKind"));
	if (!KindResult.bSuccess)
	{
		return KindResult;
	}

	FString ClassPath;
	const bool bHasClassPath = TryReadNonEmptyStringField(Object, TEXT("Class"), ClassPath);
	FString Path;
	const bool bHasPath = TryReadNonEmptyStringField(Object, TEXT("Path"), Path);
	if (!bHasClassPath && !bHasPath)
	{
		return FAssetDocumentGraphFieldRuleResult::Failure(
			TEXT("MissingGraphFieldClassRefClass"),
			MakeChildPath(Context.JsonPath, TEXT("Class")),
			TEXT("ClassRef.Class or ClassRef.Path must be a non-empty string"));
	}

	if (bHasClassPath && bHasPath && ClassPath != Path)
	{
		return FAssetDocumentGraphFieldRuleResult::Failure(
			TEXT("AmbiguousGraphFieldClassRefPath"),
			MakeChildPath(Context.JsonPath, TEXT("Class")),
			TEXT("ClassRef.Class and ClassRef.Path must not disagree"));
	}

	return FAssetDocumentGraphFieldRuleResult::Success();
}

FAssetDocumentGraphFieldRuleResult ValidateNameLike(
	const FAssetDocumentGraphFieldRuleContext& Context,
	const TSharedPtr<FJsonValue>& Value)
{
	return RequireString(
		Context,
		Value,
		TEXT("InvalidGraphFieldName"),
		TEXT("Expected a non-empty string for name-like graph field"));
}

FAssetDocumentGraphFieldRuleResult ValidateEnum(
	const FAssetDocumentGraphFieldRuleContext& Context,
	const TSharedPtr<FJsonValue>& Value)
{
	return RequireString(
		Context,
		Value,
		TEXT("InvalidGraphFieldEnum"),
		TEXT("Expected a non-empty enum name string"));
}

bool HasNumberField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
{
	double Number = 0.0;
	return Object.IsValid() && Object->TryGetNumberField(FieldName, Number);
}

FAssetDocumentGraphFieldRuleResult ValidateColor(
	const FAssetDocumentGraphFieldRuleContext& Context,
	const TSharedPtr<FJsonValue>& Value)
{
	TSharedPtr<FJsonObject> Object;
	const FAssetDocumentGraphFieldRuleResult ObjectResult =
		RequireObject(Context, Value, TEXT("InvalidGraphFieldColor"), Object);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}

	if (!HasNumberField(Object, TEXT("R")) || !HasNumberField(Object, TEXT("G")) || !HasNumberField(Object, TEXT("B")))
	{
		return FAssetDocumentGraphFieldRuleResult::Failure(
			TEXT("InvalidGraphFieldColor"),
			Context.JsonPath,
			TEXT("Color field must be an object with numeric R, G, and B components"));
	}

	const TSharedPtr<FJsonValue> AlphaValue = Object->TryGetField(TEXT("A"));
	if (AlphaValue.IsValid() && AlphaValue->Type != EJson::Number)
	{
		return FAssetDocumentGraphFieldRuleResult::Failure(
			TEXT("InvalidGraphFieldColor"),
			MakeChildPath(Context.JsonPath, TEXT("A")),
			TEXT("Color.A must be numeric when present"));
	}

	return FAssetDocumentGraphFieldRuleResult::Success();
}

FAssetDocumentGraphFieldRuleResult ValidateVector(
	const FAssetDocumentGraphFieldRuleContext& Context,
	const TSharedPtr<FJsonValue>& Value)
{
	TSharedPtr<FJsonObject> Object;
	const FAssetDocumentGraphFieldRuleResult ObjectResult =
		RequireObject(Context, Value, TEXT("InvalidGraphFieldVector"), Object);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}

	if (!HasNumberField(Object, TEXT("X")) || !HasNumberField(Object, TEXT("Y")) || !HasNumberField(Object, TEXT("Z")))
	{
		return FAssetDocumentGraphFieldRuleResult::Failure(
			TEXT("InvalidGraphFieldVector"),
			Context.JsonPath,
			TEXT("Vector field must be an object with numeric X, Y, and Z components"));
	}

	return FAssetDocumentGraphFieldRuleResult::Success();
}

FAssetDocumentGraphFieldRuleResult ValidateRaw(
	const FAssetDocumentGraphFieldRuleContext& Context,
	const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return FailureAtField(Context, TEXT("InvalidGraphFieldRaw"), TEXT("Expected a JSON value"));
	}

	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		FString Kind;
		if (Object.IsValid()
			&& Object->TryGetStringField(TEXT("Kind"), Kind)
			&& (Kind == TEXT("AssetRef") || Kind == TEXT("ClassRef")))
		{
			return FAssetDocumentGraphFieldRuleResult::Failure(
				TEXT("AmbiguousGraphFieldTrait"),
				MakeChildPath(Context.JsonPath, TEXT("Kind")),
				TEXT("Semantic graph field object requires an explicit non-raw trait"));
		}
	}

	return FAssetDocumentGraphFieldRuleResult::Success();
}
}

FAssetDocumentGraphFieldRuleResult FAssetDocumentGraphFieldRuleResult::Success()
{
	return FAssetDocumentGraphFieldRuleResult();
}

FAssetDocumentGraphFieldRuleResult FAssetDocumentGraphFieldRuleResult::Failure(
	const FString& InCode,
	const FString& InPath,
	const FString& InMessage)
{
	FAssetDocumentGraphFieldRuleResult Result;
	Result.bSuccess = false;
	Result.Code = InCode;
	Result.Path = InPath;
	Result.Message = InMessage;
	return Result;
}

FAssetDocumentGraphFieldRuleResult FAssetDocumentGraphFieldRules::ValidateTraitShape(
	const FAssetDocumentGraphFieldRuleContext& Context,
	EAssetDocumentGraphFieldTrait Trait,
	const TSharedPtr<FJsonValue>& Value)
{
	switch (Trait)
	{
	case EAssetDocumentGraphFieldTrait::Raw:
		return ValidateRaw(Context, Value);
	case EAssetDocumentGraphFieldTrait::AssetRef:
		return ValidateAssetRef(Context, Value);
	case EAssetDocumentGraphFieldTrait::ClassRef:
		return ValidateClassRef(Context, Value);
	case EAssetDocumentGraphFieldTrait::Name:
	case EAssetDocumentGraphFieldTrait::SlotName:
	case EAssetDocumentGraphFieldTrait::SyncGroupName:
	case EAssetDocumentGraphFieldTrait::CachedPoseName:
	case EAssetDocumentGraphFieldTrait::LayerName:
	case EAssetDocumentGraphFieldTrait::BoneName:
	case EAssetDocumentGraphFieldTrait::CurveName:
	case EAssetDocumentGraphFieldTrait::GameplayTag:
		return ValidateNameLike(Context, Value);
	case EAssetDocumentGraphFieldTrait::Enum:
		return ValidateEnum(Context, Value);
	case EAssetDocumentGraphFieldTrait::Color:
		return ValidateColor(Context, Value);
	case EAssetDocumentGraphFieldTrait::Vector:
		return ValidateVector(Context, Value);
	default:
		break;
	}

	return FailureAtField(Context, TEXT("UnsupportedGraphFieldTrait"), TEXT("Unsupported graph field trait"));
}

bool FAssetDocumentGraphFieldRules::ShouldOmitDefaultField(
	const TSharedPtr<FJsonValue>& Value,
	const TSharedPtr<FJsonValue>& DefaultValue,
	bool bFieldAffectsIdentityOrPins)
{
	if (bFieldAffectsIdentityOrPins || !Value.IsValid() || !DefaultValue.IsValid())
	{
		return false;
	}

	return FAssetDocumentJsonRegionUtils::JsonValueToComparableString(Value)
		== FAssetDocumentJsonRegionUtils::JsonValueToComparableString(DefaultValue);
}

TArray<EAssetDocumentGraphFieldApplyStage> FAssetDocumentGraphFieldRules::GetStagedApplyOrder()
{
	return {
		EAssetDocumentGraphFieldApplyStage::Validate,
		EAssetDocumentGraphFieldApplyStage::IdentityAndPins,
		EAssetDocumentGraphFieldApplyStage::Fields
	};
}

FString FAssetDocumentGraphFieldRules::MakeJsonPathToken(const FString& Token)
{
	return FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Token);
}
