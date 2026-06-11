// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentPropertyAdapter.h"

#include "Utils/PropertySetterUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"

FAssetDocumentPropertyApplyResult FAssetDocumentPropertyAdapter::ApplyProperties(UObject* Asset, TSharedPtr<FJsonObject> Properties)
{
	FAssetDocumentPropertyApplyResult Result;
	if (!Asset)
	{
		Result.Message = TEXT("Asset is required");
		return Result;
	}

	if (!Properties.IsValid() || Properties->Values.Num() == 0)
	{
		Result.bSuccess = true;
		Result.Message = TEXT("No properties to apply");
		return Result;
	}

	UObject* PreflightAsset = DuplicateObject<UObject>(Asset, GetTransientPackage());
	if (!PreflightAsset)
	{
		Result.Message = TEXT("Failed to create transient preflight asset");
		return Result;
	}

	if (!ApplyPropertiesDirect(PreflightAsset, Properties, Result.Diagnostics))
	{
		Result.Message = Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Message : TEXT("Property preflight failed");
		return Result;
	}

	if (!ApplyPropertiesDirect(Asset, Properties, Result.Diagnostics))
	{
		Result.Message = Result.Diagnostics.Num() > 0 ? Result.Diagnostics.Last().Message : TEXT("Property apply failed");
		return Result;
	}

	Result.bSuccess = true;
	Result.Message = TEXT("Properties applied");
	return Result;
}

bool FAssetDocumentPropertyAdapter::ApplyPropertiesDirect(UObject* Asset, TSharedPtr<FJsonObject> Properties, TArray<FAssetDocumentDiagnostic>& OutDiagnostics)
{
	bool bAllSucceeded = true;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
	{
		bAllSucceeded &= ApplySingleProperty(Asset, Pair.Key, Pair.Value, OutDiagnostics);
	}
	return bAllSucceeded;
}

bool FAssetDocumentPropertyAdapter::ApplySingleProperty(UObject* Asset, const FString& PropertyName, TSharedPtr<FJsonValue> JsonValue, TArray<FAssetDocumentDiagnostic>& OutDiagnostics)
{
	if (!JsonValue.IsValid())
	{
		AddDiagnostic(OutDiagnostics, PropertyName, TEXT("InvalidValue"), TEXT("Property value is invalid"));
		return false;
	}

	FProperty* Property = FindFProperty<FProperty>(Asset->GetClass(), *PropertyName);
	if (!Property)
	{
		AddDiagnostic(OutDiagnostics, PropertyName, TEXT("UnknownProperty"), FString::Printf(TEXT("Property '%s' does not exist"), *PropertyName));
		return false;
	}

	bool bIsTyped = false;
	FString TypeName;
	FString TypeError;
	TSharedPtr<FJsonValue> ValueToApply = JsonValue;
	if (!TryGetTypedValue(JsonValue, TypeName, ValueToApply, bIsTyped, TypeError))
	{
		AddDiagnostic(OutDiagnostics, PropertyName, TEXT("InvalidTypedValue"), TypeError);
		return false;
	}

	if (bIsTyped && TypeName.Contains(TEXT(":")))
	{
		AddDiagnostic(OutDiagnostics, PropertyName, TEXT("UnsupportedTypedSubtype"), FString::Printf(TEXT("Typed property type '%s' with subtype is not supported by sidecar v1"), *TypeName));
		return false;
	}

	if (!FPropertySetterUtils::SetPropertyFromJson(Asset, Property, ValueToApply))
	{
		AddDiagnostic(OutDiagnostics, PropertyName, TEXT("SetPropertyFailed"), FString::Printf(TEXT("Failed to set property '%s'"), *PropertyName));
		return false;
	}

	return true;
}

bool FAssetDocumentPropertyAdapter::TryGetTypedValue(TSharedPtr<FJsonValue> JsonValue, FString& OutType, TSharedPtr<FJsonValue>& OutValue, bool& bOutIsTyped, FString& OutError)
{
	bOutIsTyped = false;
	if (JsonValue->Type != EJson::Object)
	{
		return true;
	}

	TSharedPtr<FJsonObject> ValueObject = JsonValue->AsObject();
	if (!ValueObject.IsValid())
	{
		return true;
	}

	const bool bHasType = ValueObject->HasField(TEXT("type"));
	const bool bHasValue = ValueObject->HasField(TEXT("value"));
	if (!bHasType && !bHasValue)
	{
		return true;
	}

	bOutIsTyped = true;
	if (!ValueObject->TryGetStringField(TEXT("type"), OutType) || OutType.IsEmpty())
	{
		OutError = TEXT("Typed property requires a non-empty string 'type'");
		return false;
	}

	if (!bHasValue)
	{
		OutError = TEXT("Typed property requires a 'value' field");
		return false;
	}

	OutValue = ValueObject->TryGetField(TEXT("value"));
	if (!OutValue.IsValid())
	{
		OutError = TEXT("Typed property value field is invalid");
		return false;
	}

	return true;
}

void FAssetDocumentPropertyAdapter::AddDiagnostic(TArray<FAssetDocumentDiagnostic>& Diagnostics, const FString& PropertyName, const FString& Code, const FString& Message)
{
	FAssetDocumentDiagnostic Diagnostic;
	Diagnostic.Path = FString::Printf(TEXT("Properties.%s"), *PropertyName);
	Diagnostic.Code = Code;
	Diagnostic.Message = Message;
	Diagnostics.Add(Diagnostic);
}
