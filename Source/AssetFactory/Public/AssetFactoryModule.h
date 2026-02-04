// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAssetFactory, Log, All);

/**
 * Asset Factory Module
 * Provides JSON-based asset generation functionality
 */
class FAssetFactoryModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Get this module */
	static FAssetFactoryModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FAssetFactoryModule>("AssetFactory");
	}

	/** Check if module is loaded */
	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("AssetFactory");
	}

private:
	/** Register all built-in generators */
	void RegisterGenerators();

	/** Register editor menus */
	void RegisterMenus();

	/** Unregister editor menus */
	void UnregisterMenus();

	/** Menu action: Open file dialog and generate assets */
	void OnGenerateFromJSON();

	/** Handle for menu extension */
	FDelegateHandle MenuExtensionHandle;
};
