// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetFactoryEditorModule.h"
#include "Commands/AssetFactoryCommands.h"
#include "UI/SAIChatWindow.h"
#include "UI/SBSLTestWindow.h"
#include "ToolMenus.h"
#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "ISettingsModule.h"
#include "AssetFactoryAISettings.h"

#define LOCTEXT_NAMESPACE "FAssetFactoryEditorModule"

DEFINE_LOG_CATEGORY(LogAssetFactoryEditor);

static const FName CopilotChatTabId("AssetFactoryAIChat");
static const FName BSLTestTabId("AssetFactoryBSLTest");

void FAssetFactoryEditorModule::StartupModule()
{
	UE_LOG(LogAssetFactoryEditor, Log, TEXT("AssetFactoryEditor module starting up"));

	// Register commands
	FAssetFactoryCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FAssetFactoryCommands::Get().OpenChatWindow,
		FExecuteAction::CreateRaw(this, &FAssetFactoryEditorModule::OnOpenChatWindow),
		FCanExecuteAction()
	);

	PluginCommands->MapAction(
		FAssetFactoryCommands::Get().OpenSettings,
		FExecuteAction::CreateRaw(this, &FAssetFactoryEditorModule::OnOpenSettings),
		FCanExecuteAction()
	);

	// Register tab spawners
	RegisterChatWindowTab();
	RegisterBSLTestTab();

	// Register menu extensions
	RegisterMenuExtensions();

	UE_LOG(LogAssetFactoryEditor, Log, TEXT("AssetFactoryEditor module started"));
}

void FAssetFactoryEditorModule::ShutdownModule()
{
	UE_LOG(LogAssetFactoryEditor, Log, TEXT("AssetFactoryEditor module shutting down"));

	// Unregister everything
	UnregisterMenuExtensions();
	UnregisterChatWindowTab();
	UnregisterBSLTestTab();

	FAssetFactoryCommands::Unregister();

	UE_LOG(LogAssetFactoryEditor, Log, TEXT("AssetFactoryEditor module shutdown"));
}

FAssetFactoryEditorModule& FAssetFactoryEditorModule::Get()
{
	return FModuleManager::LoadModuleChecked<FAssetFactoryEditorModule>("AssetFactoryEditor");
}

bool FAssetFactoryEditorModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("AssetFactoryEditor");
}

void FAssetFactoryEditorModule::OpenChatWindow()
{
	FGlobalTabmanager::Get()->TryInvokeTab(CopilotChatTabId);
}

FName FAssetFactoryEditorModule::GetChatWindowTabId()
{
	return CopilotChatTabId;
}

void FAssetFactoryEditorModule::RegisterChatWindowTab()
{
	if (bChatWindowRegistered)
	{
		return;
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(CopilotChatTabId,
		FOnSpawnTab::CreateRaw(this, &FAssetFactoryEditorModule::SpawnChatWindowTab))
		.SetDisplayName(LOCTEXT("ChatWindowTabTitle", "Asset Factory AI"))
		.SetTooltipText(LOCTEXT("ChatWindowTabTooltip", "Open the UE Copilot AI chat window"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help"));

	bChatWindowRegistered = true;
}

void FAssetFactoryEditorModule::UnregisterChatWindowTab()
{
	if (bChatWindowRegistered)
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(CopilotChatTabId);
		bChatWindowRegistered = false;
	}
}

TSharedRef<SDockTab> FAssetFactoryEditorModule::SpawnChatWindowTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SAIChatWindow)
		];
}

void FAssetFactoryEditorModule::RegisterBSLTestTab()
{
	if (bBSLTestRegistered)
	{
		return;
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(BSLTestTabId,
		FOnSpawnTab::CreateRaw(this, &FAssetFactoryEditorModule::SpawnBSLTestTab))
		.SetDisplayName(LOCTEXT("BSLTestTabTitle", "BSL Compiler Test"))
		.SetTooltipText(LOCTEXT("BSLTestTabTooltip", "Test BSL compiler without AI"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"));

	bBSLTestRegistered = true;
}

void FAssetFactoryEditorModule::UnregisterBSLTestTab()
{
	if (bBSLTestRegistered)
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(BSLTestTabId);
		bBSLTestRegistered = false;
	}
}

TSharedRef<SDockTab> FAssetFactoryEditorModule::SpawnBSLTestTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SBSLTestWindow)
		];
}

void FAssetFactoryEditorModule::OpenBSLTestWindow()
{
	FGlobalTabmanager::Get()->TryInvokeTab(BSLTestTabId);
}

void FAssetFactoryEditorModule::RegisterMenuExtensions()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([this]()
	{
		// Add to Tools menu
		UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		if (ToolsMenu)
		{
			FToolMenuSection& Section = ToolsMenu->FindOrAddSection("AssetFactoryAI");
			Section.Label = LOCTEXT("UECopilotMenuLabel", "Asset Factory AI");

			Section.AddMenuEntry(
				"OpenChatWindow",
				LOCTEXT("OpenChatWindowLabel", "Open Copilot"),
				LOCTEXT("OpenChatWindowTooltip", "Open the UE Copilot AI chat window"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help"),
				FUIAction(FExecuteAction::CreateRaw(this, &FAssetFactoryEditorModule::OnOpenChatWindow))
			);
			Section.AddMenuEntry(
				"OpenSettings",
				LOCTEXT("OpenSettingsLabel", "Copilot Settings"),
				LOCTEXT("OpenSettingsTooltip", "Open UE Copilot settings"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FAssetFactoryEditorModule::OnOpenSettings))
			);

			Section.AddMenuEntry(
				"OpenBSLTest",
				LOCTEXT("OpenBSLTestLabel", "BSL Compiler Test"),
				LOCTEXT("OpenBSLTestTooltip", "Open BSL compiler test window"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"),
				FUIAction(FExecuteAction::CreateRaw(this, &FAssetFactoryEditorModule::OnOpenBSLTest))
			);
		}

		// Add toolbar button
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		if (ToolbarMenu)
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("AssetFactoryAI");

			Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				"CopilotToolbarButton",
				FUIAction(FExecuteAction::CreateRaw(this, &FAssetFactoryEditorModule::OnOpenChatWindow)),
				LOCTEXT("ToolbarButtonLabel", "Copilot"),
				LOCTEXT("ToolbarButtonTooltip", "Open UE Copilot AI Assistant"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help")
			));
		}
	}));
}

void FAssetFactoryEditorModule::UnregisterMenuExtensions()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FAssetFactoryEditorModule::AddToolbarExtension(FToolBarBuilder& Builder)
{
	Builder.AddToolBarButton(FAssetFactoryCommands::Get().OpenChatWindow);
}

void FAssetFactoryEditorModule::AddMenuExtension(FMenuBuilder& Builder)
{
	Builder.AddMenuEntry(FAssetFactoryCommands::Get().OpenChatWindow);
	Builder.AddMenuEntry(FAssetFactoryCommands::Get().OpenSettings);
}

void FAssetFactoryEditorModule::OnOpenChatWindow()
{
	OpenChatWindow();
}

void FAssetFactoryEditorModule::OnOpenSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->ShowViewer("Editor", "Plugins", "Asset Factory AI");
	}
}

void FAssetFactoryEditorModule::OnOpenBSLTest()
{
	OpenBSLTestWindow();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAssetFactoryEditorModule, AssetFactoryEditor)
