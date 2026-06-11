// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentModule.h"
#include "AssetDocumentHttpRoutes.h"
#include "AssetDocumentService.h"

DEFINE_LOG_CATEGORY(LogAssetDocument);

FAssetDocumentModule::~FAssetDocumentModule() = default;

void FAssetDocumentModule::StartupModule()
{
	UE_LOG(LogAssetDocument, Log, TEXT("AssetDocument module starting up"));
	Service = MakeUnique<FAssetDocumentService>();
	HttpRoutes = MakeUnique<FAssetDocumentHttpRoutes>(*Service);
	HttpRoutes->Register();
}

void FAssetDocumentModule::ShutdownModule()
{
	HttpRoutes.Reset();
	Service.Reset();
	UE_LOG(LogAssetDocument, Log, TEXT("AssetDocument module shut down"));
}

FAssetDocumentModule& FAssetDocumentModule::Get()
{
	return FModuleManager::LoadModuleChecked<FAssetDocumentModule>(TEXT("AssetDocument"));
}

bool FAssetDocumentModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(TEXT("AssetDocument"));
}

FAssetDocumentService& FAssetDocumentModule::GetService()
{
	check(Service.IsValid());
	return *Service;
}

IMPLEMENT_MODULE(FAssetDocumentModule, AssetDocument)
