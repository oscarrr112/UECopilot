// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FAssetDocumentService;

class FAssetDocumentHttpRoutes
{
public:
	explicit FAssetDocumentHttpRoutes(const TSharedRef<FAssetDocumentService>& InService);
	~FAssetDocumentHttpRoutes();

	void Register();
	void Unregister();

private:
	TWeakPtr<FAssetDocumentService> Service;
	TArray<FDelegateHandle> RouteHandles;
};
