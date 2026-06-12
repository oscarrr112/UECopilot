// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentTypes.h"

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

struct ASSETDOCUMENT_API FAssetDocumentTemplateContext
{
	FString Target;
	FString ClassPath;
};

struct ASSETDOCUMENT_API FAssetDocumentCapabilityContext
{
	UObject* Asset = nullptr;
	UClass* AssetClass = nullptr;
	FString TargetAssetPath;
	FString SourceDocumentPath;
	bool bIsDryRun = false;
	FAssetDocumentResult* Result = nullptr;
};

struct ASSETDOCUMENT_API FAssetDocumentCapabilityResult
{
	bool bSuccess = false;
	FString Message;
	TArray<FAssetDocumentDiagnostic> Diagnostics;
	TSharedPtr<FJsonObject> Payload;

	static FAssetDocumentCapabilityResult Success(const FString& InMessage)
	{
		FAssetDocumentCapabilityResult Result;
		Result.bSuccess = true;
		Result.Message = InMessage;
		return Result;
	}

	static FAssetDocumentCapabilityResult Success(const FString& InMessage, TSharedPtr<FJsonObject> InPayload)
	{
		FAssetDocumentCapabilityResult Result = Success(InMessage);
		Result.Payload = InPayload;
		return Result;
	}

	static FAssetDocumentCapabilityResult Failure(const FString& InMessage)
	{
		FAssetDocumentCapabilityResult Result;
		Result.bSuccess = false;
		Result.Message = InMessage;
		return Result;
	}

	static FAssetDocumentCapabilityResult Failure(const FString& InMessage, TArray<FAssetDocumentDiagnostic> InDiagnostics)
	{
		FAssetDocumentCapabilityResult Result = Failure(InMessage);
		Result.Diagnostics = MoveTemp(InDiagnostics);
		return Result;
	}
};

class ASSETDOCUMENT_API IAssetDocumentCapability
{
public:
	virtual ~IAssetDocumentCapability() = default;

	virtual FName GetName() const = 0;
	virtual int32 GetApplyOrder() const = 0;
	virtual bool SupportsAsset(const FAssetDocumentCapabilityContext& Context) const = 0;
	virtual bool SupportsClass(UClass* AssetClass) const = 0;
	virtual TSharedPtr<FJsonObject> GetSchemaHint(const FAssetDocumentCapabilityContext& Context) const = 0;
	virtual FAssetDocumentCapabilityResult Validate(const FAssetDocumentCapabilityContext& Context, const TSharedPtr<FJsonValue>& BodyValue) const = 0;
	virtual FAssetDocumentCapabilityResult Apply(const FAssetDocumentCapabilityContext& Context, const TSharedPtr<FJsonValue>& BodyValue) = 0;
	virtual FAssetDocumentCapabilityResult Extract(const FAssetDocumentCapabilityContext& Context) const = 0;
	virtual FAssetDocumentCapabilityResult Diff(const FAssetDocumentCapabilityContext& Context, const TSharedPtr<FJsonValue>& BodyValue) const = 0;
};

class ASSETDOCUMENT_API IAssetDocumentProfile
{
public:
	virtual ~IAssetDocumentProfile() = default;

	virtual UClass* GetExactClass() const = 0;
	virtual FName GetDocumentShape() const = 0;
	virtual TSharedPtr<FJsonObject> CreateTemplate(const FAssetDocumentTemplateContext& Context) const = 0;
	virtual TArray<FString> GetBodyKeys() const = 0;
	virtual TSharedPtr<IAssetDocumentCapability> ResolveBodyAdapter(const FString& BodyKey) const = 0;
};
