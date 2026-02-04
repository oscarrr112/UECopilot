// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetFactoryAI.h"

#define LOCTEXT_NAMESPACE "FAssetFactoryAIModule"

DEFINE_LOG_CATEGORY(LogAssetFactoryAI);

void FAssetFactoryAIModule::StartupModule()
{
	UE_LOG(LogAssetFactoryAI, Log, TEXT("AssetFactoryAI module started"));
}

void FAssetFactoryAIModule::ShutdownModule()
{
	UE_LOG(LogAssetFactoryAI, Log, TEXT("AssetFactoryAI module shutdown"));
}

FAssetFactoryAIModule& FAssetFactoryAIModule::Get()
{
	return FModuleManager::LoadModuleChecked<FAssetFactoryAIModule>("AssetFactoryAI");
}

bool FAssetFactoryAIModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("AssetFactoryAI");
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAssetFactoryAIModule, AssetFactoryAI)
