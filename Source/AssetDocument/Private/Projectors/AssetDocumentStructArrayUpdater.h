#pragma once

#include "CoreMinimal.h"
#include "Projectors/AssetDocumentUpdatePlan.h"

class FJsonValue;

using FAssetDocumentStructFieldApplyOverride = TFunction<bool(const FString& FieldName, const TSharedPtr<FJsonValue>& JsonValue, void* StructValuePtr, FString& OutError)>;

class FAssetDocumentStructArrayUpdater
{
public:
	FAssetDocumentUpdateResult ReplaceStructArray(
		const TArray<TSharedPtr<FJsonValue>>& SourceArray,
		UScriptStruct* StructType,
		const FString& Path,
		const FAssetDocumentStructFieldApplyOverride& FieldOverride,
		TFunctionRef<void(void*)> AddStructValue) const;

private:
	bool ApplyPrimitiveField(UScriptStruct* StructType, void* StructValuePtr, FProperty* Field, const TSharedPtr<FJsonValue>& JsonValue) const;
	bool IsSupportedPrimitiveField(FProperty* Field) const;
};
