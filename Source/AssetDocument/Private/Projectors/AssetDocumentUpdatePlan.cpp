#include "Projectors/AssetDocumentUpdatePlan.h"

#include "Dom/JsonObject.h"

TSharedRef<FJsonObject> FAssetDocumentUpdateMetrics::ToJson() const
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetNumberField(TEXT("ReusableOperations"), ReusableOperations);
	Json->SetNumberField(TEXT("AssetSpecificOperations"), AssetSpecificOperations);
	Json->SetNumberField(TEXT("ValidationFailures"), ValidationFailures);
	return Json;
}

FAssetDocumentUpdateResult FAssetDocumentUpdateResult::Success()
{
	FAssetDocumentUpdateResult Result;
	Result.bSuccess = true;
	return Result;
}

FAssetDocumentUpdateResult FAssetDocumentUpdateResult::Failure(const FString& InMessage)
{
	FAssetDocumentUpdateResult Result;
	Result.bSuccess = false;
	Result.Message = InMessage;
	Result.Metrics.ValidationFailures = 1;
	return Result;
}
