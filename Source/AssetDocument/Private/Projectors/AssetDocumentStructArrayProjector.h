#pragma once

#include "CoreMinimal.h"
#include "Projectors/AssetDocumentProjectionTypes.h"

class FJsonObject;
class FJsonValue;

using FAssetDocumentStructFieldOverride = TFunction<bool(const FString& FieldName, FProperty* Field, const void* ValuePtr, TSharedRef<FJsonObject> OutObject)>;

class FAssetDocumentStructArrayProjector
{
public:
	FAssetDocumentProjectionResult ProjectStructArray(
		const TArray<const void*>& Items,
		UStruct* StructType,
		const FString& Path,
		const TSet<FString>& IncludedFields,
		const FAssetDocumentStructFieldOverride& FieldOverride,
		TArray<TSharedPtr<FJsonValue>>& OutArray) const;

private:
	bool ProjectPrimitiveField(FProperty* Field, const void* ValuePtr, TSharedRef<FJsonObject> OutObject) const;
};
