// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

enum class EAssetDocumentResultStatus : uint8
{
	Success,
	Failed,
	PartialFailed
};

struct ASSETDOCUMENT_API FAssetDocumentDiagnostic
{
	FString Path;
	FString Code;
	FString Message;
};

struct ASSETDOCUMENT_API FAssetDocumentApplyRequest
{
	TSharedPtr<FJsonObject> Document;
	bool bWriteSidecar = false;
	bool bSaveAsset = true;
};

struct ASSETDOCUMENT_API FAssetDocumentApplyFileRequest
{
	FString FilePath;
	bool bSaveAsset = true;
	bool bAllowSidecarRewrite = true;
	bool bTriggeredByWatcher = false;
};

struct ASSETDOCUMENT_API FAssetDocumentInspectRequest
{
	FString ClassOrAsset;
};

struct ASSETDOCUMENT_API FAssetDocumentExtractRequest
{
	FString AssetPath;
	bool bDiffOnly = true;
	bool bIncludeAllWritable = false;
};

struct ASSETDOCUMENT_API FAssetDocumentValidateRequest
{
	TSharedPtr<FJsonObject> Document;
	FString FilePath;
};

struct ASSETDOCUMENT_API FAssetDocumentDiffRequest
{
	TSharedPtr<FJsonObject> Document;
	FString FilePath;
};

struct ASSETDOCUMENT_API FAssetDocumentResult
{
	EAssetDocumentResultStatus Status = EAssetDocumentResultStatus::Failed;
	FString Message;
	FString Target;
	FString SidecarFilePath;
	FString AssetPath;
	bool bSavedAsset = false;
	bool bWroteSidecar = false;
	TArray<FAssetDocumentDiagnostic> Diagnostics;
	TSharedPtr<FJsonObject> Payload;

	static FAssetDocumentResult Success(const FString& InMessage);
	static FAssetDocumentResult Failure(const FString& InMessage);
	bool IsSuccess() const { return Status == EAssetDocumentResultStatus::Success; }
	TSharedPtr<FJsonObject> ToJson() const;
};
