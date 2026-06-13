#pragma once

#include "CoreMinimal.h"
#include "Projectors/AssetDocumentUpdatePlan.h"

class FJsonValue;
class FProperty;
class UScriptStruct;

using FAssetDocumentStructFieldApplyOverride = TFunction<bool(const FString& FieldName, const TSharedPtr<FJsonValue>& JsonValue, void* StructValuePtr, FString& OutError)>;

class FAssetDocumentStructArrayUpdater
{
public:
	// AddStructValue receives a temporary initialized struct value. It must synchronously deep-copy
	// the value during the callback and must not store the pointer; the pointer is invalid after return.
	FAssetDocumentUpdateResult ReplaceStructArray(
		const TArray<TSharedPtr<FJsonValue>>& SourceArray,
		UScriptStruct* StructType,
		const FString& Path,
		const FAssetDocumentStructFieldApplyOverride& FieldOverride,
		TFunctionRef<void(const void* StructValuePtr)> AddStructValue) const;

private:
	bool ApplyPrimitiveField(UScriptStruct* StructType, void* StructValuePtr, FProperty* Field, const TSharedPtr<FJsonValue>& JsonValue) const;
	bool IsSupportedPrimitiveField(FProperty* Field) const;
};
