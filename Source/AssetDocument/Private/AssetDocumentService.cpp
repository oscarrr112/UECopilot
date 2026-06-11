// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentService.h"

#include "Dom/JsonValue.h"

FAssetDocumentResult FAssetDocumentResult::Success(const FString& InMessage)
{
	FAssetDocumentResult Result;
	Result.Status = EAssetDocumentResultStatus::Success;
	Result.Message = InMessage;
	return Result;
}

FAssetDocumentResult FAssetDocumentResult::Failure(const FString& InMessage)
{
	FAssetDocumentResult Result;
	Result.Status = EAssetDocumentResultStatus::Failed;
	Result.Message = InMessage;
	return Result;
}

TSharedPtr<FJsonObject> FAssetDocumentResult::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetBoolField(TEXT("success"), IsSuccess());
	Json->SetStringField(TEXT("message"), Message);
	Json->SetStringField(TEXT("target"), Target);
	Json->SetStringField(TEXT("asset_path"), AssetPath);
	Json->SetStringField(TEXT("sidecar_file_path"), SidecarFilePath);
	Json->SetBoolField(TEXT("saved_asset"), bSavedAsset);
	Json->SetBoolField(TEXT("wrote_sidecar"), bWroteSidecar);

	TArray<TSharedPtr<FJsonValue>> DiagnosticValues;
	for (const FAssetDocumentDiagnostic& Diagnostic : Diagnostics)
	{
		TSharedPtr<FJsonObject> DiagnosticJson = MakeShared<FJsonObject>();
		DiagnosticJson->SetStringField(TEXT("path"), Diagnostic.Path);
		DiagnosticJson->SetStringField(TEXT("code"), Diagnostic.Code);
		DiagnosticJson->SetStringField(TEXT("message"), Diagnostic.Message);
		DiagnosticValues.Add(MakeShared<FJsonValueObject>(DiagnosticJson));
	}
	Json->SetArrayField(TEXT("diagnostics"), DiagnosticValues);

	if (Payload.IsValid())
	{
		Json->SetObjectField(TEXT("payload"), Payload);
	}

	return Json;
}

FAssetDocumentResult FAssetDocumentService::Apply(const FAssetDocumentApplyRequest& Request)
{
	FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("AssetDocument Apply is not implemented"));
	Result.bSavedAsset = false;
	Result.bWroteSidecar = false;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::ApplyFile(const FAssetDocumentApplyFileRequest& Request)
{
	FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("AssetDocument ApplyFile is not implemented"));
	Result.Target = Request.FilePath;
	Result.SidecarFilePath = Request.FilePath;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::Inspect(const FAssetDocumentInspectRequest& Request) const
{
	FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("AssetDocument Inspect is not implemented"));
	Result.Target = Request.ClassOrAsset;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::Extract(const FAssetDocumentExtractRequest& Request) const
{
	FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("AssetDocument Extract is not implemented"));
	Result.Target = Request.AssetPath;
	Result.AssetPath = Request.AssetPath;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::Validate(const FAssetDocumentValidateRequest& Request) const
{
	FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("AssetDocument Validate is not implemented"));
	Result.Target = Request.FilePath;
	Result.SidecarFilePath = Request.FilePath;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::Diff(const FAssetDocumentDiffRequest& Request) const
{
	FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("AssetDocument Diff is not implemented"));
	Result.Target = Request.FilePath;
	Result.SidecarFilePath = Request.FilePath;
	return Result;
}
