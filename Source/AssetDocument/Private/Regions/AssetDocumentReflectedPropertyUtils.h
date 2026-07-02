// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class FStructProperty;

class FAssetDocumentReflectedPropertyUtils
{
public:
	static FAssetDocumentCapabilityResult ValidateProperties(
		UObject* Object,
		const TSharedRef<FJsonObject>& Properties,
		const FString& Path);
	static FAssetDocumentCapabilityResult ApplyProperties(
		UObject* Object,
		const TSharedRef<FJsonObject>& Properties,
		const FString& Path);
	static FAssetDocumentCapabilityResult ExtractAuthoredProperties(
		UObject* Object,
		TSharedRef<FJsonObject>& OutProperties,
		const FString& Path);
	static FAssetDocumentCapabilityResult DiffProperties(
		UObject* Object,
		const TSharedRef<FJsonObject>& DesiredProperties,
		const FString& Path,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries);
	static FAssetDocumentCapabilityResult ApplyBlackboardKeySelector(
		FStructProperty* Property,
		void* ValuePtr,
		const TSharedRef<FJsonObject>& Json,
		const FString& Path);
	static FAssetDocumentCapabilityResult ExtractBlackboardKeySelector(
		FStructProperty* Property,
		const void* ValuePtr,
		TSharedPtr<FJsonValue>& OutValue,
		const FString& Path);
};
