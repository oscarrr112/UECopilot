// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FAssetDocumentService;

class FAssetDocumentHttpRoutes
{
public:
	explicit FAssetDocumentHttpRoutes(FAssetDocumentService& InService);
	~FAssetDocumentHttpRoutes();

	void Register();
	void Unregister();

private:
	TSharedPtr<FJsonObject> HandleApply(const FString& RequestBody, const TMap<FString, FString>& QueryParams, int32& OutStatusCode);
	TSharedPtr<FJsonObject> HandleApplyFile(const FString& RequestBody, const TMap<FString, FString>& QueryParams, int32& OutStatusCode);
	TSharedPtr<FJsonObject> HandleSchema(const FString& RequestBody, const TMap<FString, FString>& QueryParams, int32& OutStatusCode);
	TSharedPtr<FJsonObject> HandleInspect(const FString& RequestBody, const TMap<FString, FString>& QueryParams, int32& OutStatusCode);
	TSharedPtr<FJsonObject> HandleExtract(const FString& RequestBody, const TMap<FString, FString>& QueryParams, int32& OutStatusCode);
	TSharedPtr<FJsonObject> HandleValidate(const FString& RequestBody, const TMap<FString, FString>& QueryParams, int32& OutStatusCode);
	TSharedPtr<FJsonObject> HandleDiff(const FString& RequestBody, const TMap<FString, FString>& QueryParams, int32& OutStatusCode);

	FAssetDocumentService& Service;
	TArray<FDelegateHandle> RouteHandles;
};
