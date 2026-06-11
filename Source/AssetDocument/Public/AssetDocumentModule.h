// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAssetDocument, Log, All);

class FAssetDocumentService;

class ASSETDOCUMENT_API FAssetDocumentModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FAssetDocumentModule& Get();
	static bool IsAvailable();

	FAssetDocumentService& GetService();

private:
	TUniquePtr<FAssetDocumentService> Service;
};
