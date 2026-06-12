// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentTypes.h"

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

struct ASSETDOCUMENT_API FAssetDocumentFragmentContext
{
	UObject* OwnerAsset = nullptr;
	UObject* Outer = nullptr;
	UClass* ExpectedBaseClass = nullptr;
	UScriptStruct* ExpectedStruct = nullptr;
	const TSharedPtr<FJsonObject>* Definitions = nullptr;
	TArray<FString> DefinitionStack;
	FString JsonPath;
	FString Role;
};

struct ASSETDOCUMENT_API FAssetDocumentFragmentExtractContext
{
	UObject* OwnerAsset = nullptr;
	UObject* ValueObject = nullptr;
	UScriptStruct* StructType = nullptr;
	const void* StructValue = nullptr;
	FString JsonPath;
	FString Role;
};

struct ASSETDOCUMENT_API FAssetDocumentFragmentResult
{
	bool bSuccess = false;
	FString Message;
	TArray<FAssetDocumentDiagnostic> Diagnostics;
	TSharedPtr<FJsonValue> Value;
	UObject* Object = nullptr;
	UClass* Class = nullptr;
	UScriptStruct* StructType = nullptr;
	TArray<uint8> StructBytes;

	static FAssetDocumentFragmentResult Success(const FString& InMessage);
	static FAssetDocumentFragmentResult Success(TSharedPtr<FJsonValue> InValue);
	static FAssetDocumentFragmentResult Failure(const FString& InMessage);
	static FAssetDocumentFragmentResult Failure(const FString& InMessage, TArray<FAssetDocumentDiagnostic> InDiagnostics);
};

class ASSETDOCUMENT_API IAssetDocumentFragmentAdapter
{
public:
	virtual ~IAssetDocumentFragmentAdapter() = default;

	virtual FName GetKind() const = 0;
	virtual bool SupportsContext(const FAssetDocumentFragmentContext& Context) const = 0;
	virtual FAssetDocumentFragmentResult Validate(const TSharedPtr<FJsonObject>& Fragment, const FAssetDocumentFragmentContext& Context) const = 0;
	virtual FAssetDocumentFragmentResult Compile(const TSharedPtr<FJsonObject>& Fragment, const FAssetDocumentFragmentContext& Context) const = 0;
	virtual FAssetDocumentFragmentResult Extract(const FAssetDocumentFragmentExtractContext& Context) const = 0;
};
