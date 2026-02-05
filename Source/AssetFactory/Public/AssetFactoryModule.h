// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAssetFactory, Log, All);

class FAssetFactoryHttpServer;

/**
 * Asset Factory Module
 * Provides JSON-based asset generation functionality
 *
 * Features:
 * - JSON-based asset generation (Blueprint, Material, Widget, DataAsset, etc.)
 * - HTTP Server for external tool integration (AI Agents, MCP)
 * - Editor menu integration
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

	/** Get HTTP Server instance */
	FAssetFactoryHttpServer* GetHttpServer() const { return HttpServer.Get(); }

private:
	/** Register all built-in generators */
	void RegisterGenerators();

	/** Register editor menus */
	void RegisterMenus();

	/** Unregister editor menus */
	void UnregisterMenus();

	/** Menu action: Open file dialog and generate assets */
	void OnGenerateFromJSON();

	/** Start HTTP server if enabled in settings */
	void StartHttpServer();

	/** Stop HTTP server */
	void StopHttpServer();

	/** Handle for menu extension */
	FDelegateHandle MenuExtensionHandle;

	/** HTTP Server for external tool integration */
	TUniquePtr<FAssetFactoryHttpServer> HttpServer;
};
