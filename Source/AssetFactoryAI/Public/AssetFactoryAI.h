// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAssetFactoryAI, Log, All);

class FAssetFactoryAIModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * Singleton-like access to this module's interface.
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static FAssetFactoryAIModule& Get();

	/**
	 * Checks to see if this module is loaded and ready.
	 * @return True if the module is loaded and ready to use
	 */
	static bool IsAvailable();
};
