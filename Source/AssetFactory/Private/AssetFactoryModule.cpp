// Copyright ProjectRPG. All Rights Reserved.

#include "AssetFactoryModule.h"
#include "AssetGeneratorRegistry.h"
#include "AssetFactorySubsystem.h"
#include "AssetFactoryHttpServer.h"
#include "AssetFactorySettings.h"
#include "ToolMenus.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Editor.h"

// Include generators
#include "Generators/DataAssetGenerator.h"
#include "Generators/CurveFloatGenerator.h"
#include "Generators/CurveVectorGenerator.h"
#include "Generators/InputActionGenerator.h"
#include "Generators/InputMappingContextGenerator.h"
#include "Generators/BlueprintGenerator.h"
#include "Generators/WidgetBlueprintGenerator.h"
#include "Generators/MaterialGenerator.h"
#include "Generators/DataTableGenerator.h"
#include "Generators/BlackboardDataGenerator.h"
#include "Generators/BehaviorTreeGenerator.h"
#include "Generators/StateTreeGenerator.h"
#include "Generators/GameplayTagGenerator.h"

#define LOCTEXT_NAMESPACE "FAssetFactoryModule"

DEFINE_LOG_CATEGORY(LogAssetFactory);

void FAssetFactoryModule::StartupModule()
{
	UE_LOG(LogAssetFactory, Log, TEXT("AssetFactory module starting up"));

	// Register generators
	RegisterGenerators();

	// Register menus after ToolMenus is ready
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAssetFactoryModule::RegisterMenus));

	// Start HTTP server if enabled
	StartHttpServer();
}

void FAssetFactoryModule::ShutdownModule()
{
	UE_LOG(LogAssetFactory, Log, TEXT("AssetFactory module shutting down"));

	// Stop HTTP server
	StopHttpServer();

	// Unregister menus
	UnregisterMenus();

	// Clear generator registry
	FAssetGeneratorRegistry::Get().Clear();
}

void FAssetFactoryModule::RegisterGenerators()
{
	FAssetGeneratorRegistry& Registry = FAssetGeneratorRegistry::Get();

	// Register all built-in generators
	Registry.RegisterGenerator(MakeShared<FBlackboardDataGenerator>());
	Registry.RegisterGenerator(MakeShared<FBehaviorTreeGenerator>());  // Priority 30: BT after BB
	Registry.RegisterGenerator(MakeShared<FGameplayTagGenerator>());  // Highest priority: tags must exist before other assets
	Registry.RegisterGenerator(MakeShared<FStateTreeGenerator>());  // Priority 40: StateTree after BT/BB/tags, before generic assets
	Registry.RegisterGenerator(MakeShared<FMaterialGenerator>());  // Before widgets that use materials
	Registry.RegisterGenerator(MakeShared<FDataAssetGenerator>());
	Registry.RegisterGenerator(MakeShared<FCurveFloatGenerator>());
	Registry.RegisterGenerator(MakeShared<FCurveVectorGenerator>());
	Registry.RegisterGenerator(MakeShared<FInputActionGenerator>());
	Registry.RegisterGenerator(MakeShared<FInputMappingContextGenerator>());
	Registry.RegisterGenerator(MakeShared<FBlueprintGenerator>());
	Registry.RegisterGenerator(MakeShared<FWidgetBlueprintGenerator>());
	Registry.RegisterGenerator(MakeShared<FDataTableGenerator>());

	UE_LOG(LogAssetFactory, Log, TEXT("Registered %d generators"), Registry.GetRegisteredTypes().Num());
}

void FAssetFactoryModule::RegisterMenus()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus)
	{
		return;
	}

	// Extend Tools menu
	UToolMenu* ToolsMenu = ToolMenus->ExtendMenu("LevelEditor.MainMenu.Tools");
	if (ToolsMenu)
	{
		FToolMenuSection& Section = ToolsMenu->FindOrAddSection("AssetFactory");
		Section.Label = LOCTEXT("AssetFactorySection", "Asset Factory");

		Section.AddMenuEntry(
			"GenerateFromJSON",
			LOCTEXT("GenerateFromJSON", "Generate from JSON..."),
			LOCTEXT("GenerateFromJSONTooltip", "Select a JSON file to generate assets"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Import"),
			FUIAction(FExecuteAction::CreateRaw(this, &FAssetFactoryModule::OnGenerateFromJSON))
		);
	}

	UE_LOG(LogAssetFactory, Log, TEXT("Registered editor menus"));
}

void FAssetFactoryModule::UnregisterMenus()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (ToolMenus)
	{
		ToolMenus->RemoveSection("LevelEditor.MainMenu.Tools", "AssetFactory");
	}
}

void FAssetFactoryModule::OnGenerateFromJSON()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Desktop platform not available"));
		return;
	}

	// Open file dialog
	TArray<FString> OutFiles;
	const FString DefaultPath = FPaths::ProjectConfigDir();
	const FString FileTypes = TEXT("JSON Files (*.json)|*.json");

	const void* ParentWindow = FSlateApplication::Get().GetActiveTopLevelWindow().IsValid()
		? FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle()
		: nullptr;

	bool bOpened = DesktopPlatform->OpenFileDialog(
		ParentWindow,
		TEXT("Select JSON Configuration File"),
		DefaultPath,
		TEXT(""),
		FileTypes,
		EFileDialogFlags::None,
		OutFiles
	);

	if (!bOpened || OutFiles.Num() == 0)
	{
		return;
	}

	const FString& FilePath = OutFiles[0];
	UE_LOG(LogAssetFactory, Log, TEXT("Selected file: %s"), *FilePath);

	// Get subsystem and generate
	UAssetFactorySubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetFactorySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogAssetFactory, Error, TEXT("AssetFactorySubsystem not available"));
		return;
	}

	FGenerationReport Report = Subsystem->GenerateFromFile(FilePath);

	// Show notification
	FNotificationInfo Info(FText::Format(
		LOCTEXT("GenerationComplete", "Asset Generation Complete\n{0}"),
		FText::FromString(Report.GetSummary())
	));
	Info.bFireAndForget = true;
	Info.ExpireDuration = 5.0f;
	Info.bUseSuccessFailIcons = true;

	if (Report.HasFailures())
	{
		Info.Image = FCoreStyle::Get().GetBrush("Icons.Error");
	}
	else
	{
		Info.Image = FCoreStyle::Get().GetBrush("Icons.SuccessWithColor");
	}

	FSlateNotificationManager::Get().AddNotification(Info);
}

void FAssetFactoryModule::StartHttpServer()
{
	const UAssetFactorySettings* Settings = UAssetFactorySettings::Get();
	if (!Settings || !Settings->bEnableHttpServer)
	{
		UE_LOG(LogAssetFactory, Log, TEXT("HTTP Server is disabled in settings"));
		return;
	}

	if (!HttpServer)
	{
		HttpServer = MakeUnique<FAssetFactoryHttpServer>();
	}

	if (!HttpServer->IsRunning())
	{
		HttpServer->Start(Settings->HttpServerPort);
	}
}

void FAssetFactoryModule::StopHttpServer()
{
	if (HttpServer && HttpServer->IsRunning())
	{
		HttpServer->Stop();
	}
	HttpServer.Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAssetFactoryModule, AssetFactory)
