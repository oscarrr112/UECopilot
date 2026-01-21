// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUECopilotEditor, Log, All);

class FToolBarBuilder;
class FMenuBuilder;
class FUICommandList;

/**
 * UE Copilot Editor Module - Provides editor UI for AI-assisted blueprint generation
 */
class FUECopilotEditorModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * Singleton-like access to this module's interface.
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static FUECopilotEditorModule& Get();

	/**
	 * Checks to see if this module is loaded and ready.
	 * @return True if the module is loaded and ready to use
	 */
	static bool IsAvailable();

	/**
	 * Open the AI Chat window
	 */
	void OpenChatWindow();

	/**
	 * Open the BSL Compiler Test window
	 */
	void OpenBSLTestWindow();

	/**
	 * Get the chat window tab ID
	 */
	static FName GetChatWindowTabId();

private:
	/** Register menu extensions */
	void RegisterMenuExtensions();

	/** Unregister menu extensions */
	void UnregisterMenuExtensions();

	/** Register the chat window tab spawner */
	void RegisterChatWindowTab();

	/** Unregister the chat window tab spawner */
	void UnregisterChatWindowTab();

	/** Create the chat window tab */
	TSharedRef<class SDockTab> SpawnChatWindowTab(const class FSpawnTabArgs& Args);

	/** Register the BSL test tab spawner */
	void RegisterBSLTestTab();

	/** Unregister the BSL test tab spawner */
	void UnregisterBSLTestTab();

	/** Create the BSL test tab */
	TSharedRef<class SDockTab> SpawnBSLTestTab(const class FSpawnTabArgs& Args);

	/** Add toolbar extension */
	void AddToolbarExtension(FToolBarBuilder& Builder);

	/** Add menu extension */
	void AddMenuExtension(FMenuBuilder& Builder);

	/** Plugin commands */
	void OnOpenChatWindow();
	void OnOpenSettings();
	void OnOpenBSLTest();

	/** Plugin commands list */
	TSharedPtr<FUICommandList> PluginCommands;

	/** Has the chat window been registered */
	bool bChatWindowRegistered = false;

	/** Has the BSL test window been registered */
	bool bBSLTestRegistered = false;
};
