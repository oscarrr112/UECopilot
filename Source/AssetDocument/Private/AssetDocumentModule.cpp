// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentModule.h"
#include "AssetDocumentEditorSync.h"
#include "AssetDocumentFileWatcher.h"
#include "AssetDocumentHttpRoutes.h"
#include "AssetDocumentProfileRegistry.h"
#include "AssetDocumentService.h"
#include "Profiles/AnimMontageAssetDocumentProfile.h"
#include "Profiles/AnimSequenceAssetDocumentProfile.h"
#include "Profiles/UBlueprintAssetDocumentProfile.h"

DEFINE_LOG_CATEGORY(LogAssetDocument);

FAssetDocumentModule::FAssetDocumentModule() = default;
FAssetDocumentModule::~FAssetDocumentModule() = default;

void FAssetDocumentModule::StartupModule()
{
	UE_LOG(LogAssetDocument, Log, TEXT("AssetDocument module starting up"));
	FAssetDocumentService::GetProfileRegistry().Register(MakeShared<FAnimMontageAssetDocumentProfile>());
	FAssetDocumentService::GetProfileRegistry().Register(MakeShared<FAnimSequenceAssetDocumentProfile>());
	FAssetDocumentService::GetProfileRegistry().Register(MakeShared<FUBlueprintAssetDocumentProfile>());
	Service = MakeShared<FAssetDocumentService>();
	HttpRoutes = MakeUnique<FAssetDocumentHttpRoutes>(Service.ToSharedRef());
	HttpRoutes->Register();
	EditorSync = MakeUnique<FAssetDocumentEditorSync>();
	EditorSync->Register();
	FileWatcher = MakeUnique<FAssetDocumentFileWatcher>(Service.ToSharedRef());
	FileWatcher->Register();
}

void FAssetDocumentModule::ShutdownModule()
{
	FileWatcher.Reset();
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
