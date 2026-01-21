// Copyright Epic Games, Inc. All Rights Reserved.

#include "UECopilotEditor.h"
#include "Commands/CopilotEditorCommands.h"
#include "UI/SCopilotChatWindow.h"
#include "UI/SBSLTestWindow.h"
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
static const FName BSLTestTabId("BSLCompilerTest");

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

	// Register tab spawners
	RegisterChatWindowTab();
	RegisterBSLTestTab();

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
	UnregisterBSLTestTab();

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

void FUECopilotEditorModule::RegisterBSLTestTab()
{
	if (bBSLTestRegistered)
	{
		return;
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(BSLTestTabId,
		FOnSpawnTab::CreateRaw(this, &FUECopilotEditorModule::SpawnBSLTestTab))
		.SetDisplayName(LOCTEXT("BSLTestTabTitle", "BSL Compiler Test"))
		.SetTooltipText(LOCTEXT("BSLTestTabTooltip", "Test BSL compiler without AI"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"));

	bBSLTestRegistered = true;
}

void FUECopilotEditorModule::UnregisterBSLTestTab()
{
	if (bBSLTestRegistered)
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(BSLTestTabId);
		bBSLTestRegistered = false;
	}
}

TSharedRef<SDockTab> FUECopilotEditorModule::SpawnBSLTestTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SBSLTestWindow)
		];
}

void FUECopilotEditorModule::OpenBSLTestWindow()
{
	FGlobalTabmanager::Get()->TryInvokeTab(BSLTestTabId);
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

			Section.AddMenuEntry(
				"OpenChatWindow",
				LOCTEXT("OpenChatWindowLabel", "Open Copilot"),
				LOCTEXT("OpenChatWindowTooltip", "Open the UE Copilot AI chat window"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help"),
				FUIAction(FExecuteAction::CreateRaw(this, &FUECopilotEditorModule::OnOpenChatWindow))
			);
			Section.AddMenuEntry(
				"OpenSettings",
				LOCTEXT("OpenSettingsLabel", "Copilot Settings"),
				LOCTEXT("OpenSettingsTooltip", "Open UE Copilot settings"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FUECopilotEditorModule::OnOpenSettings))
			);

			Section.AddMenuEntry(
				"OpenBSLTest",
				LOCTEXT("OpenBSLTestLabel", "BSL Compiler Test"),
				LOCTEXT("OpenBSLTestTooltip", "Open BSL compiler test window"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"),
				FUIAction(FExecuteAction::CreateRaw(this, &FUECopilotEditorModule::OnOpenBSLTest))
			);
		}

		// Add toolbar button
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		if (ToolbarMenu)
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("UECopilot");

			Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				"CopilotToolbarButton",
				FUIAction(FExecuteAction::CreateRaw(this, &FUECopilotEditorModule::OnOpenChatWindow)),
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

void FUECopilotEditorModule::OnOpenBSLTest()
{
	OpenBSLTestWindow();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUECopilotEditorModule, UECopilotEditor)
