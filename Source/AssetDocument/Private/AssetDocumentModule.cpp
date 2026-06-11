// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentModule.h"
#include "AssetDocumentEditorSync.h"
#include "AssetDocumentHttpRoutes.h"
#include "AssetDocumentService.h"

DEFINE_LOG_CATEGORY(LogAssetDocument);

FAssetDocumentModule::FAssetDocumentModule() = default;
FAssetDocumentModule::~FAssetDocumentModule() = default;

void FAssetDocumentModule::StartupModule()
{
	UE_LOG(LogAssetDocument, Log, TEXT("AssetDocument module starting up"));
	Service = MakeShared<FAssetDocumentService>();
	HttpRoutes = MakeUnique<FAssetDocumentHttpRoutes>(Service.ToSharedRef());
	HttpRoutes->Register();
	EditorSync = MakeUnique<FAssetDocumentEditorSync>();
	EditorSync->Register();
}

void FAssetDocumentModule::ShutdownModule()
{
	EditorSync.Reset();
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
