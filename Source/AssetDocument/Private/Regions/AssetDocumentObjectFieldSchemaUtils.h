// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentRegion.h"

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

struct FAssetDocumentObjectFieldSpec
{
	FString Name;
	EJson Type = EJson::None;
	bool bRequired = false;
	FString MissingCode;
	FString TypeMismatchCode;
	FString TypeMismatchMessage;
};

struct FAssetDocumentObjectFieldSchema
{
	TArray<FAssetDocumentObjectFieldSpec> Fields;
	bool bRejectUnknownFields = true;
	FString UnknownFieldCode;
	FString UnknownFieldMessageFormat;
};

struct FAssetDocumentObjectFieldSchemaUtils
{
	static FAssetDocumentCapabilityResult ValidateObjectFields(
		const FAssetDocumentRegionContext& Context,
		const TSharedRef<FJsonObject>& Object,
		const FAssetDocumentObjectFieldSchema& Schema);

	static FString MakeFieldPath(
		const FAssetDocumentRegionContext& Context,
		const FString& FieldName);
};
