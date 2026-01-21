// Copyright Epic Games, Inc. All Rights Reserved.

#include "UECopilotEditor.h"
#include "Commands/CopilotEditorCommands.h"
#include "UI/SCopilotChatWindow.h"
#include "ToolMenus.h"
#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "ISettingsModule.h"
#include "UECopilotSettings.h"

#define LOCTEXT_NAMESPACE "FUECopilotEditorModule"

DEFINE_LOG_CATEGORY(LogUECopilotEditor);

static const FName CopilotChatTabId("UECopilotChat");

void FUECopilotEditorModule::StartupModule()
{
	UE_LOG(LogUECopilotEditor, Log, TEXT("UECopilotEditor module starting up"));

	// Register commands
	FCopilotEditorCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FCopilotEditorCommands::Get().OpenChatWindow,
		FExecuteAction::CreateRaw(this, &FUECopilotEditorModule::OnOpenChatWindow),
		FCanExecuteAction()
	);

	PluginCommands->MapAction(
		FCopilotEditorCommands::Get().OpenSettings,
		FExecuteAction::CreateRaw(this, &FUECopilotEditorModule::OnOpenSettings),
		FCanExecuteAction()
	);

	// Register tab spawner
	RegisterChatWindowTab();

	// Register menu extensions
	RegisterMenuExtensions();

	UE_LOG(LogUECopilotEditor, Log, TEXT("UECopilotEditor module started"));
}

void FUECopilotEditorModule::ShutdownModule()
{
	UE_LOG(LogUECopilotEditor, Log, TEXT("UECopilotEditor module shutting down"));

	// Unregister everything
	UnregisterMenuExtensions();
	UnregisterChatWindowTab();

	FCopilotEditorCommands::Unregister();

	UE_LOG(LogUECopilotEditor, Log, TEXT("UECopilotEditor module shutdown"));
}

FUECopilotEditorModule& FUECopilotEditorModule::Get()
{
	return FModuleManager::LoadModuleChecked<FUECopilotEditorModule>("UECopilotEditor");
}

bool FUECopilotEditorModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("UECopilotEditor");
}

void FUECopilotEditorModule::OpenChatWindow()
{
	FGlobalTabmanager::Get()->TryInvokeTab(CopilotChatTabId);
}

FName FUECopilotEditorModule::GetChatWindowTabId()
{
	return CopilotChatTabId;
}

void FUECopilotEditorModule::RegisterChatWindowTab()
{
	if (bChatWindowRegistered)
	{
		return;
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(CopilotChatTabId,
		FOnSpawnTab::CreateRaw(this, &FUECopilotEditorModule::SpawnChatWindowTab))
		.SetDisplayName(LOCTEXT("ChatWindowTabTitle", "UE Copilot"))
		.SetTooltipText(LOCTEXT("ChatWindowTabTooltip", "Open the UE Copilot AI chat window"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help"));

	bChatWindowRegistered = true;
}

void FUECopilotEditorModule::UnregisterChatWindowTab()
{
	if (bChatWindowRegistered)
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(CopilotChatTabId);
		bChatWindowRegistered = false;
	}
}

TSharedRef<SDockTab> FUECopilotEditorModule::SpawnChatWindowTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SCopilotChatWindow)
		];
}

void FUECopilotEditorModule::RegisterMenuExtensions()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([this]()
	{
		// Add to Tools menu
		UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		if (ToolsMenu)
		{
			FToolMenuSection& Section = ToolsMenu->FindOrAddSection("UECopilot");
			Section.Label = LOCTEXT("UECopilotMenuLabel", "UE Copilot");

			Section.AddMenuEntry(FCopilotEditorCommands::Get().OpenChatWindow);
			Section.AddMenuEntry(FCopilotEditorCommands::Get().OpenSettings);
		}

		// Add toolbar button
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		if (ToolbarMenu)
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("UECopilot");

			FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				FCopilotEditorCommands::Get().OpenChatWindow,
				LOCTEXT("ToolbarButtonLabel", "Copilot"),
				LOCTEXT("ToolbarButtonTooltip", "Open UE Copilot AI Assistant"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help")
			));
		}
	}));
}

void FUECopilotEditorModule::UnregisterMenuExtensions()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FUECopilotEditorModule::AddToolbarExtension(FToolBarBuilder& Builder)
{
	Builder.AddToolBarButton(FCopilotEditorCommands::Get().OpenChatWindow);
}

void FUECopilotEditorModule::AddMenuExtension(FMenuBuilder& Builder)
{
	Builder.AddMenuEntry(FCopilotEditorCommands::Get().OpenChatWindow);
	Builder.AddMenuEntry(FCopilotEditorCommands::Get().OpenSettings);
}

void FUECopilotEditorModule::OnOpenChatWindow()
{
	OpenChatWindow();
}

void FUECopilotEditorModule::OnOpenSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->ShowViewer("Editor", "Plugins", "UE Copilot");
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUECopilotEditorModule, UECopilotEditor)
