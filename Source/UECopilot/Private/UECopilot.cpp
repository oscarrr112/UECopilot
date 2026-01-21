// Copyright Epic Games, Inc. All Rights Reserved.

#include "UECopilot.h"

#define LOCTEXT_NAMESPACE "FUECopilotModule"

DEFINE_LOG_CATEGORY(LogUECopilot);

void FUECopilotModule::StartupModule()
{
	UE_LOG(LogUECopilot, Log, TEXT("UECopilot module started"));
}

void FUECopilotModule::ShutdownModule()
{
	UE_LOG(LogUECopilot, Log, TEXT("UECopilot module shutdown"));
}

FUECopilotModule& FUECopilotModule::Get()
{
	return FModuleManager::LoadModuleChecked<FUECopilotModule>("UECopilot");
}

bool FUECopilotModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("UECopilot");
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUECopilotModule, UECopilot)
