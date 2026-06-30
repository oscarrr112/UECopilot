#include "Projectors/AssetDocumentStructArrayUpdater.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Utils/PropertySetterUtils.h"

FAssetDocumentUpdateResult FAssetDocumentStructArrayUpdater::ReplaceStructArray(
	const TArray<TSharedPtr<FJsonValue>>& SourceArray,
	UScriptStruct* StructType,
	const FString& Path,
	const FAssetDocumentStructFieldApplyOverride& FieldOverride,
	TFunctionRef<void(const void* StructValuePtr)> AddStructValue) const
{
	if (!StructType)
	{
		return FAssetDocumentUpdateResult::Failure(TEXT("Struct array update requires a script struct type."));
	}

	FAssetDocumentUpdateResult Result = FAssetDocumentUpdateResult::Success();

	for (int32 ItemIndex = 0; ItemIndex < SourceArray.Num(); ++ItemIndex)
	{
		const TSharedPtr<FJsonValue>& ItemValue = SourceArray[ItemIndex];
		const TSharedPtr<FJsonObject>* ItemObjectPtr = nullptr;
		if (!ItemValue.IsValid() || !ItemValue->TryGetObject(ItemObjectPtr) || !ItemObjectPtr || !ItemObjectPtr->IsValid())
		{
			++Result.Metrics.ValidationFailures;
			return FAssetDocumentUpdateResult::Failure(FString::Printf(TEXT("Struct array item '%d' at '%s' must be an object."), ItemIndex, *Path));
		}

		FStructOnScope StructScope(StructType);
		void* StructValuePtr = StructScope.GetStructMemory();
		if (!StructValuePtr)
		{
			++Result.Metrics.ValidationFailures;
			return FAssetDocumentUpdateResult::Failure(FString::Printf(TEXT("Failed to allocate struct value for '%s'."), *StructType->GetName()));
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ItemObjectPtr)->Values)
		{
			const FString& FieldName = Pair.Key;
			FProperty* Field = StructType->FindPropertyByName(*FieldName);
			if (!Field)
			{
				++Result.Metrics.ValidationFailures;
				return FAssetDocumentUpdateResult::Failure(
					FString::Printf(TEXT("Struct array field '%s' at '%s[%d]' was not found in struct '%s'."),
						*FieldName,
						*Path,
						ItemIndex,
						*StructType->GetName()));
			}

			FString OverrideError;
			if (FieldOverride && FieldOverride(FieldName, Pair.Value, StructValuePtr, OverrideError))
			{
				++Result.Metrics.AssetSpecificOperations;
				continue;
			}

			if (!OverrideError.IsEmpty())
			{
				++Result.Metrics.ValidationFailures;
				return FAssetDocumentUpdateResult::Failure(OverrideError);
			}

			if (!ApplyPrimitiveField(StructType, StructValuePtr, Field, Pair.Value))
			{
				++Result.Metrics.ValidationFailures;
				return FAssetDocumentUpdateResult::Failure(
					FString::Printf(TEXT("Failed to apply primitive field '%s' at '%s[%d]'."), *FieldName, *Path, ItemIndex));
			}

			++Result.Metrics.ReusableOperations;
		}

		AddStructValue(StructValuePtr);
	}

	Result.ChangedPaths.Add(Path);
	return Result;
}

bool FAssetDocumentStructArrayUpdater::ApplyPrimitiveField(UScriptStruct* StructType, void* StructValuePtr, FProperty* Field, const TSharedPtr<FJsonValue>& JsonValue) const
{
	if (!StructType || !StructValuePtr || !Field || !JsonValue.IsValid() || !IsSupportedPrimitiveField(Field))
	{
		return false;
	}

	TSharedRef<FJsonObject> FieldObject = MakeShared<FJsonObject>();
	FieldObject->SetField(Field->GetName(), JsonValue);
	return FPropertySetterUtils::SetStructFromJson(StructType, StructValuePtr, MakeShared<FJsonValueObject>(FieldObject));
}

bool FAssetDocumentStructArrayUpdater::IsSupportedPrimitiveField(FProperty* Field) const
{
	return CastField<FNameProperty>(Field)
		|| CastField<FStrProperty>(Field)
		|| CastField<FNumericProperty>(Field)
		|| CastField<FBoolProperty>(Field);
}
