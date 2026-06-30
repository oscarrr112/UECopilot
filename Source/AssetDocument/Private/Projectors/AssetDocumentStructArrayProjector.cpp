#include "Projectors/AssetDocumentStructArrayProjector.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"

FAssetDocumentProjectionResult FAssetDocumentStructArrayProjector::ProjectStructArray(
	const TArray<const void*>& Items,
	UStruct* StructType,
	const FString& Path,
	const TSet<FString>& IncludedFields,
	const FAssetDocumentStructFieldOverride& FieldOverride,
	TArray<TSharedPtr<FJsonValue>>& OutArray) const
{
	OutArray.Reset();

	if (!StructType)
	{
		return FAssetDocumentProjectionResult::Failure(TEXT("Struct array projection requires a struct type."), Path, TEXT("MissingStructType"));
	}

	FAssetDocumentProjectionResult Result = FAssetDocumentProjectionResult::Success();

	for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ++ItemIndex)
	{
		const void* ItemPtr = Items[ItemIndex];
		if (!ItemPtr)
		{
			return FAssetDocumentProjectionResult::Failure(
				FString::Printf(TEXT("Struct array item '%d' at '%s' is null."), ItemIndex, *Path),
				FString::Printf(TEXT("%s[%d]"), *Path, ItemIndex),
				TEXT("NullStructArrayItem"));
		}

		TSharedRef<FJsonObject> ItemObject = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(StructType); It; ++It)
		{
			FProperty* Field = *It;
			const FString FieldName = Field->GetName();
			if (IncludedFields.Num() > 0 && !IncludedFields.Contains(FieldName))
			{
				++Result.Metrics.SkippedFields;
				continue;
			}

			const void* FieldValuePtr = Field->ContainerPtrToValuePtr<void>(ItemPtr);
			if (FieldOverride && FieldOverride(FieldName, Field, FieldValuePtr, ItemObject))
			{
				++Result.Metrics.AssetSpecificFields;
				continue;
			}

			if (ProjectPrimitiveField(Field, FieldValuePtr, ItemObject))
			{
				++Result.Metrics.ReusableProjectedFields;
			}
			else
			{
				++Result.Metrics.SkippedFields;
			}
		}

		OutArray.Add(MakeShared<FJsonValueObject>(ItemObject));
	}

	return Result;
}

bool FAssetDocumentStructArrayProjector::ProjectPrimitiveField(FProperty* Field, const void* ValuePtr, TSharedRef<FJsonObject> OutObject) const
{
	if (!Field || !ValuePtr)
	{
		return false;
	}

	const FString FieldName = Field->GetName();
	if (FNameProperty* NameProperty = CastField<FNameProperty>(Field))
	{
		OutObject->SetStringField(FieldName, NameProperty->GetPropertyValue(ValuePtr).ToString());
		return true;
	}

	if (FStrProperty* StringProperty = CastField<FStrProperty>(Field))
	{
		OutObject->SetStringField(FieldName, StringProperty->GetPropertyValue(ValuePtr));
		return true;
	}

	if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Field))
	{
		if (NumericProperty->IsFloatingPoint())
		{
			OutObject->SetNumberField(FieldName, NumericProperty->GetFloatingPointPropertyValue(ValuePtr));
		}
		else if (NumericProperty->IsInteger())
		{
			OutObject->SetNumberField(FieldName, static_cast<double>(NumericProperty->GetSignedIntPropertyValue(ValuePtr)));
		}
		else
		{
			return false;
		}
		return true;
	}

	if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Field))
	{
		OutObject->SetBoolField(FieldName, BoolProperty->GetPropertyValue(ValuePtr));
		return true;
	}

	return false;
}
