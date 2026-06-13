#include "Projectors/AssetDocumentProjectionTypes.h"

#include "Dom/JsonObject.h"

TSharedRef<FJsonObject> FAssetDocumentProjectionMetrics::ToJson() const
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetNumberField(TEXT("AssetSpecificFields"), AssetSpecificFields);
	Json->SetNumberField(TEXT("ReusableProjectedFields"), ReusableProjectedFields);
	Json->SetNumberField(TEXT("SkippedFields"), SkippedFields);
	return Json;
}

FAssetDocumentProjectionResult FAssetDocumentProjectionResult::Success()
{
	FAssetDocumentProjectionResult Result;
	Result.bSuccess = true;
	return Result;
}

FAssetDocumentProjectionResult FAssetDocumentProjectionResult::Failure(const FString& InMessage, const FString& Path, const FString& Code)
{
	FAssetDocumentProjectionResult Result;
	Result.bSuccess = false;
	Result.Message = InMessage;
	Result.Diagnostics.Add({ Path, Code, InMessage });
	return Result;
}
