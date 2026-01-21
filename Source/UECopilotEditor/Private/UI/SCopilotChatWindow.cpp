// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SCopilotChatWindow.h"
#include "UECopilotSettings.h"
#include "JSON/BlueprintJSONParser.h"
#include "BSL/BSLCompiler.h"
#include "Factory/AIBlueprintFactory.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetToolsModule.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Styling/SlateStyleMacros.h"
#include "EditorStyleSet.h"
#include "ISettingsModule.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "Selection.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "SCopilotChatWindow"

const FString SCopilotChatWindow::QuickCommandPrefix = TEXT("/");

void SCopilotChatWindow::Construct(const FArguments& InArgs)
{
	// Initialize conversation context (using TStrongObjectPtr to prevent GC)
	ConversationContext = TStrongObjectPtr<UAIConversationContext>(
		UAIConversationContext::Create(GetTransientPackage(), GetSystemPrompt()));

	// Inject project context
	FString ProjectContext = UProjectContextCollector::CollectContext();
	ConversationContext->InjectProjectContext(ProjectContext);

	ChildSlot
	[
		SNew(SVerticalBox)

		// Toolbar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			BuildToolbar()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]

		// Chat messages
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			BuildChatMessageList()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]

		// Input area
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			BuildInputArea()
		]
	];

	// Add welcome message
	AddMessage(EChatMessageRole::Assistant,
		TEXT("Welcome to UE Copilot! I can help you generate and modify Blueprints.\n\n")
		TEXT("Quick commands:\n")
		TEXT("  /generate [description] - Generate a new blueprint\n")
		TEXT("  /apply [path] - Create the generated blueprint\n")
		TEXT("  /explain - Explain a blueprint\n")
		TEXT("  /modify - Modify an existing blueprint\n")
		TEXT("  /help - Show all commands\n\n")
		TEXT("Example: \"Create an Actor blueprint that prints 'Hello World' on BeginPlay\""));
}

SCopilotChatWindow::~SCopilotChatWindow()
{
	// Cancel any pending requests
	if (UOpenAICompatibleService* Service = UOpenAICompatibleService::Get())
	{
		Service->CancelRequest();
	}
}

TSharedRef<SWidget> SCopilotChatWindow::BuildToolbar()
{
	return SNew(SHorizontalBox)

		// Title
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ChatWindowTitle", "UE Copilot"))
			.TextStyle(FAppStyle::Get(), "NormalText.Important")
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNullWidget::NullWidget
		]

		// Clear button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("ClearButton", "Clear"))
			.OnClicked(this, &SCopilotChatWindow::OnClearClicked)
			.ToolTipText(LOCTEXT("ClearTooltip", "Clear chat history"))
		]

		// Settings button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("SettingsButton", "Settings"))
			.OnClicked(this, &SCopilotChatWindow::OnSettingsClicked)
			.ToolTipText(LOCTEXT("SettingsTooltip", "Open UE Copilot settings"))
		];
}

TSharedRef<SWidget> SCopilotChatWindow::BuildChatMessageList()
{
	return SAssignNew(MessageScrollBox, SScrollBox)
		+ SScrollBox::Slot()
		[
			SAssignNew(MessageListBox, SVerticalBox)
		];
}

TSharedRef<SWidget> SCopilotChatWindow::BuildInputArea()
{
	return SNew(SHorizontalBox)

		// Input text box
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SBox)
			.MinDesiredHeight(60.0f)
			.MaxDesiredHeight(150.0f)
			[
				SAssignNew(InputTextBox, SMultiLineEditableTextBox)
				.HintText(LOCTEXT("InputHint", "Type a message or use /command..."))
				.OnTextCommitted(this, &SCopilotChatWindow::OnInputCommitted)
				.OnKeyDownHandler(this, &SCopilotChatWindow::OnInputKeyDown)
				.AutoWrapText(true)
			]
		]

		// Send button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Bottom)
		[
			SNew(SButton)
			.Text(LOCTEXT("SendButton", "Send"))
			.OnClicked(this, &SCopilotChatWindow::OnSendClicked)
			.IsEnabled_Lambda([this]() { return !bIsProcessing; })
		];
}

TSharedRef<SWidget> SCopilotChatWindow::CreateMessageWidget(const FChatDisplayMessage& Message)
{
	FString RoleLabel;
	FLinearColor BackgroundColor;

	switch (Message.Role)
	{
	case EChatMessageRole::User:
		RoleLabel = TEXT("You");
		BackgroundColor = FLinearColor(0.2f, 0.3f, 0.4f, 0.3f);
		break;
	case EChatMessageRole::Assistant:
		RoleLabel = TEXT("Copilot");
		BackgroundColor = FLinearColor(0.1f, 0.2f, 0.1f, 0.3f);
		break;
	case EChatMessageRole::System:
		RoleLabel = TEXT("System");
		BackgroundColor = FLinearColor(0.3f, 0.3f, 0.3f, 0.3f);
		break;
	}

	return SNew(SBorder)
		.BorderBackgroundColor(BackgroundColor)
		.Padding(8.0f)
		[
			SNew(SVerticalBox)

			// Role label
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(RoleLabel))
				.TextStyle(FAppStyle::Get(), "NormalText.Important")
			]

			// Message content (using SMultiLineEditableTextBox for copy support)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SMultiLineEditableTextBox)
				.Text(FText::FromString(Message.Content))
				.AutoWrapText(true)
				.IsReadOnly(true)
				.BackgroundColor(FLinearColor::Transparent)
				.Style(FAppStyle::Get(), "NormalEditableTextBox")
			]

			// Streaming indicator
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("StreamingIndicator", "..."))
				.Visibility_Lambda([&Message]() {
					return Message.bIsStreaming ? EVisibility::Visible : EVisibility::Collapsed;
				})
			]
		];
}

void SCopilotChatWindow::RefreshMessageList()
{
	if (!MessageListBox.IsValid())
	{
		return;
	}

	MessageListBox->ClearChildren();

	for (const FChatDisplayMessage& Message : Messages)
	{
		MessageListBox->AddSlot()
			.AutoHeight()
			.Padding(4.0f)
			[
				CreateMessageWidget(Message)
			];
	}

	ScrollToBottom();
}

void SCopilotChatWindow::SendMessage()
{
	if (!InputTextBox.IsValid() || bIsProcessing)
	{
		return;
	}

	FString InputText = InputTextBox->GetText().ToString().TrimStartAndEnd();

	if (InputText.IsEmpty())
	{
		return;
	}

	// Save to input history (avoid duplicates at top)
	if (InputHistory.Num() == 0 || InputHistory[0] != InputText)
	{
		InputHistory.Insert(InputText, 0);
		// Limit history size
		const int32 MaxHistorySize = 50;
		if (InputHistory.Num() > MaxHistorySize)
		{
			InputHistory.SetNum(MaxHistorySize);
		}
	}
	// Reset history navigation state
	HistoryIndex = -1;
	CurrentInputBackup.Empty();
	bWasAtDocumentStart = false;

	// Clear input
	InputTextBox->SetText(FText::GetEmpty());

	// Check for quick command
	FString Command, Args;
	if (ParseQuickCommand(InputText, Command, Args))
	{
		ExecuteQuickCommand(InputText);
		return;
	}

	// Add user message to display
	AddMessage(EChatMessageRole::User, InputText);

	// Add to conversation context
	ConversationContext->AddUserMessage(InputText);

	// Send request to AI
	bIsProcessing = true;

	UOpenAICompatibleService* Service = UOpenAICompatibleService::Get();
	UUECopilotSettings* Settings = UUECopilotSettings::Get();

	if (Settings->bEnableStreaming)
	{
		CurrentStreamingResponse.Empty();
		AddMessage(EChatMessageRole::Assistant, TEXT(""), true);

		Service->SendChatRequestStreaming(
			ConversationContext->GetMessages(),
			FOnAIStreamChunk::CreateSP(this, &SCopilotChatWindow::OnAIStreamChunk),
			FOnAIStreamComplete::CreateSP(this, &SCopilotChatWindow::OnAIStreamComplete),
			FOnAIError::CreateSP(this, &SCopilotChatWindow::OnAIError)
		);
	}
	else
	{
		Service->SendChatRequest(
			ConversationContext->GetMessages(),
			FOnAIResponseReceived::CreateSP(this, &SCopilotChatWindow::OnAIResponseReceived)
		);
	}
}

FReply SCopilotChatWindow::OnSendClicked()
{
	SendMessage();
	return FReply::Handled();
}

void SCopilotChatWindow::OnInputCommitted(const FText& Text, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		// Only send on Shift+Enter to allow multiline input
		// Regular Enter creates newline
	}
}

FReply SCopilotChatWindow::OnInputKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() == EKeys::Enter)
	{
		if (KeyEvent.IsShiftDown())
		{
			// Shift+Enter sends message
			SendMessage();
			return FReply::Handled();
		}
	}
	else if (KeyEvent.GetKey() == EKeys::Escape)
	{
		if (bIsProcessing)
		{
			// Cancel current request
			UOpenAICompatibleService::Get()->CancelRequest();
			bIsProcessing = false;

			AddMessage(EChatMessageRole::System, TEXT("Request cancelled."));
			return FReply::Handled();
		}
	}
	else if (KeyEvent.GetKey() == EKeys::Up)
	{
		if (!InputTextBox.IsValid() || InputHistory.Num() == 0)
		{
			return FReply::Unhandled();
		}

		FString CurrentText = InputTextBox->GetText().ToString();

		// Check if cursor is on the first line
		// We consider it "first line" if we're at the very start (position 0)
		// SMultiLineEditableTextBox doesn't expose cursor position directly,
		// so we use a simpler approach: track consecutive up presses

		if (bWasAtDocumentStart)
		{
			// Second up press at start - navigate history
			if (HistoryIndex == -1)
			{
				// Save current input before browsing
				CurrentInputBackup = CurrentText;
				HistoryIndex = 0;
			}
			else if (HistoryIndex < InputHistory.Num() - 1)
			{
				HistoryIndex++;
			}
			else
			{
				// Already at oldest history item
				return FReply::Handled();
			}

			// Set text from history
			InputTextBox->SetText(FText::FromString(InputHistory[HistoryIndex]));
			return FReply::Handled();
		}
		else
		{
			// First up press - check if text has no newlines (single line)
			// If single line, mark as at document start for next press
			if (!CurrentText.Contains(TEXT("\n")))
			{
				bWasAtDocumentStart = true;
			}
			// Let default handler move cursor
			return FReply::Unhandled();
		}
	}
	else if (KeyEvent.GetKey() == EKeys::Down)
	{
		if (!InputTextBox.IsValid())
		{
			return FReply::Unhandled();
		}

		// Only handle if we're browsing history
		if (HistoryIndex >= 0)
		{
			FString CurrentText = InputTextBox->GetText().ToString();

			// Check if single line (no newlines)
			if (!CurrentText.Contains(TEXT("\n")))
			{
				if (HistoryIndex > 0)
				{
					HistoryIndex--;
					InputTextBox->SetText(FText::FromString(InputHistory[HistoryIndex]));
				}
				else
				{
					// Back to original input
					HistoryIndex = -1;
					InputTextBox->SetText(FText::FromString(CurrentInputBackup));
					CurrentInputBackup.Empty();
					bWasAtDocumentStart = false;
				}
				return FReply::Handled();
			}
		}

		// Reset state when down is pressed on multi-line or not browsing
		bWasAtDocumentStart = false;
		return FReply::Unhandled();
	}
	else
	{
		// Any other key resets the up-arrow state
		bWasAtDocumentStart = false;
	}

	return FReply::Unhandled();
}

void SCopilotChatWindow::OnAIResponseReceived(const FAIResponse& Response)
{
	bIsProcessing = false;

	if (Response.bSuccess)
	{
		AddMessage(EChatMessageRole::Assistant, Response.Content);
		ConversationContext->AddAssistantMessage(Response.Content);

		// Process for potential blueprint generation
		ProcessAIResponse(Response.Content);
	}
	else
	{
		AddMessage(EChatMessageRole::System, FString::Printf(TEXT("Error: %s"), *Response.ErrorMessage));
	}
}

void SCopilotChatWindow::OnAIStreamChunk(const FString& Chunk)
{
	CurrentStreamingResponse += Chunk;
	UpdateLastMessage(Chunk);
}

void SCopilotChatWindow::OnAIStreamComplete()
{
	bIsProcessing = false;

	// Mark last message as not streaming
	if (Messages.Num() > 0)
	{
		Messages.Last().bIsStreaming = false;
	}

	// Add to conversation context
	ConversationContext->AddAssistantMessage(CurrentStreamingResponse);

	// Process response
	ProcessAIResponse(CurrentStreamingResponse);

	RefreshMessageList();
}

void SCopilotChatWindow::OnAIError(const FString& ErrorMessage)
{
	bIsProcessing = false;

	// Update last message if it was streaming
	if (Messages.Num() > 0 && Messages.Last().bIsStreaming)
	{
		Messages.Last().bIsStreaming = false;
		if (Messages.Last().Content.IsEmpty())
		{
			Messages.Last().Content = TEXT("(No response)");
		}
	}

	AddMessage(EChatMessageRole::System, FString::Printf(TEXT("Error: %s"), *ErrorMessage));
}

void SCopilotChatWindow::ProcessAIResponse(const FString& ResponseContent)
{
	// Try to extract BSL code from response (look for code blocks)
	FString BSLContent;

	// Look for ```blueprint or ``` code blocks containing "blueprint ... extends"
	int32 CodeBlockStart = ResponseContent.Find(TEXT("```"));
	if (CodeBlockStart != INDEX_NONE)
	{
		int32 ContentStart = ResponseContent.Find(TEXT("\n"), ESearchCase::IgnoreCase, ESearchDir::FromStart, CodeBlockStart);
		if (ContentStart != INDEX_NONE)
		{
			ContentStart++;  // Skip the newline
			int32 CodeBlockEnd = ResponseContent.Find(TEXT("```"), ESearchCase::IgnoreCase, ESearchDir::FromStart, ContentStart);
			if (CodeBlockEnd != INDEX_NONE)
			{
				BSLContent = ResponseContent.Mid(ContentStart, CodeBlockEnd - ContentStart).TrimStartAndEnd();
			}
		}
	}

	// Check if this looks like BSL (starts with "blueprint")
	if (!BSLContent.IsEmpty() && BSLContent.StartsWith(TEXT("blueprint")))
	{
		// Try BSL compilation
		BSL::FCompileResult CompileResult = BSL::FCompiler::Compile(BSLContent);

		if (CompileResult.bSuccess)
		{
			// Validate
			TArray<FString> ValidationErrors;
			if (UBlueprintJSONParser::ValidateBlueprintData(CompileResult.BlueprintData, ValidationErrors))
			{
				// Store the compiled blueprint data for /apply command
				PendingBlueprintData = CompileResult.BlueprintData;

				FString WarningText;
				if (CompileResult.Warnings.Num() > 0)
				{
					WarningText = TEXT("\n\nWarnings:\n") + FString::Join(CompileResult.Warnings, TEXT("\n"));
				}

				if (bIsModifyMode && BlueprintBeingModified.IsValid())
				{
					AddMessage(EChatMessageRole::System,
						FString::Printf(TEXT("Compiled BSL modifications for '%s' successfully.%s\n\nUse /apply to apply the changes."),
							*BlueprintBeingModified->GetName(), *WarningText));
				}
				else
				{
					AddMessage(EChatMessageRole::System,
						FString::Printf(TEXT("Compiled blueprint '%s' successfully.%s\n\nUse /apply to create it, or /apply [path] to specify a custom path.\nExample: /apply /Game/Blueprints"),
							*CompileResult.BlueprintData.Name, *WarningText));
				}
				return;
			}
			else
			{
				PendingBlueprintData.Reset();
				FString ErrorList = FString::Join(ValidationErrors, TEXT("\n"));
				AddMessage(EChatMessageRole::System,
					FString::Printf(TEXT("Blueprint validation failed:\n%s"), *ErrorList));
				return;
			}
		}
		else
		{
			// BSL compilation failed
			FString ErrorList = FString::Join(CompileResult.Errors, TEXT("\n"));
			AddMessage(EChatMessageRole::System,
				FString::Printf(TEXT("BSL compilation failed:\n%s"), *ErrorList));
			return;
		}
	}

	// Fall back to JSON parsing for backwards compatibility
	FString JSONContent = UBlueprintJSONParser::ExtractJSONFromResponse(ResponseContent);

	if (!JSONContent.IsEmpty() && JSONContent.StartsWith(TEXT("{")))
	{
		FBlueprintParseResult ParseResult = UBlueprintJSONParser::ParseBlueprintJSON(JSONContent);

		if (ParseResult.bSuccess)
		{
			// Validate
			TArray<FString> ValidationErrors;
			if (UBlueprintJSONParser::ValidateBlueprintData(ParseResult.BlueprintData, ValidationErrors))
			{
				// Store the parsed blueprint data for /apply command
				PendingBlueprintData = ParseResult.BlueprintData;

				if (bIsModifyMode && BlueprintBeingModified.IsValid())
				{
					AddMessage(EChatMessageRole::System,
						FString::Printf(TEXT("Parsed JSON modifications for '%s' successfully.\n\nUse /apply to apply the changes to the blueprint."),
							*BlueprintBeingModified->GetName()));
				}
				else
				{
					AddMessage(EChatMessageRole::System,
						FString::Printf(TEXT("Parsed JSON blueprint '%s' successfully.\n\nUse /apply to create it, or /apply [path] to specify a custom path.\nExample: /apply /Game/Blueprints"),
							*ParseResult.BlueprintData.Name));
				}
			}
			else
			{
				PendingBlueprintData.Reset();
				FString ErrorList = FString::Join(ValidationErrors, TEXT("\n"));
				AddMessage(EChatMessageRole::System,
					FString::Printf(TEXT("Blueprint validation failed:\n%s"), *ErrorList));
			}
		}
	}
}

FReply SCopilotChatWindow::OnClearClicked()
{
	Messages.Empty();

	if (ConversationContext.IsValid())
	{
		ConversationContext->ClearHistory();
	}

	RefreshMessageList();

	// Re-add welcome message
	AddMessage(EChatMessageRole::Assistant,
		TEXT("Chat cleared. How can I help you?"));

	return FReply::Handled();
}

FReply SCopilotChatWindow::OnSettingsClicked()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->ShowViewer("Editor", "Plugins", "UE Copilot");
	}

	return FReply::Handled();
}

void SCopilotChatWindow::ExecuteQuickCommand(const FString& Command)
{
	FString Cmd, Args;
	ParseQuickCommand(Command, Cmd, Args);

	if (Cmd.Equals(TEXT("generate"), ESearchCase::IgnoreCase))
	{
		FString Prompt = Args.IsEmpty()
			? TEXT("Generate a blueprint based on my description.")
			: FString::Printf(TEXT("Generate a blueprint: %s"), *Args);

		AddMessage(EChatMessageRole::User, Command);
		ConversationContext->AddUserMessage(Prompt);

		bIsProcessing = true;
		UOpenAICompatibleService::Get()->SendChatRequest(
			ConversationContext->GetMessages(),
			FOnAIResponseReceived::CreateSP(this, &SCopilotChatWindow::OnAIResponseReceived)
		);
	}
	else if (Cmd.Equals(TEXT("apply"), ESearchCase::IgnoreCase))
	{
		// Apply the pending blueprint
		if (!PendingBlueprintData.IsSet())
		{
			AddMessage(EChatMessageRole::System,
				TEXT("No blueprint data to apply. First generate a blueprint using /generate or use /modify to modify an existing one."));
			return;
		}

		if (bIsModifyMode)
		{
			// Modify existing blueprint
			UBlueprint* TargetBlueprint = BlueprintBeingModified.Get();
			if (!TargetBlueprint)
			{
				AddMessage(EChatMessageRole::System,
					TEXT("The blueprint being modified is no longer available. Please use /modify again to select a blueprint."));
				PendingBlueprintData.Reset();
				bIsModifyMode = false;
				return;
			}

			AddMessage(EChatMessageRole::System,
				FString::Printf(TEXT("Modifying blueprint '%s'..."), *TargetBlueprint->GetName()));

			// Modify the blueprint
			FBlueprintGenerationResult Result = UAIBlueprintFactory::ModifyBlueprint(
				TargetBlueprint,
				PendingBlueprintData.GetValue(),
				true  // Merge with existing
			);

			if (Result.bSuccess)
			{
				AddMessage(EChatMessageRole::System,
					FString::Printf(TEXT("Blueprint '%s' modified successfully!\n\nWarnings: %d"),
						*TargetBlueprint->GetName(), Result.Warnings.Num()));

				// Show warnings if any
				if (Result.Warnings.Num() > 0)
				{
					FString WarningList = FString::Join(Result.Warnings, TEXT("\n  - "));
					AddMessage(EChatMessageRole::System,
						FString::Printf(TEXT("Warnings:\n  - %s"), *WarningList));
				}

				// Mark package dirty so user knows to save
				TargetBlueprint->MarkPackageDirty();

				AddMessage(EChatMessageRole::System,
					TEXT("Don't forget to save the blueprint (Ctrl+S in Blueprint Editor or right-click > Save in Content Browser)."));
			}
			else
			{
				AddMessage(EChatMessageRole::System,
					FString::Printf(TEXT("Failed to modify blueprint: %s"), *Result.ErrorMessage));
			}

			// Clear modify mode
			PendingBlueprintData.Reset();
			BlueprintBeingModified.Reset();
			bIsModifyMode = false;
		}
		else
		{
			// Create new blueprint
			FString PackagePath = Args.IsEmpty() ? TEXT("/Game/Blueprints") : Args;

			// Ensure path starts with /Game
			if (!PackagePath.StartsWith(TEXT("/Game")))
			{
				PackagePath = TEXT("/Game") / PackagePath;
			}

			AddMessage(EChatMessageRole::System,
				FString::Printf(TEXT("Creating blueprint '%s' at %s..."),
					*PendingBlueprintData->Name, *PackagePath));

			// Create the blueprint
			FBlueprintGenerationResult Result = UAIBlueprintFactory::CreateBlueprint(
				PendingBlueprintData.GetValue(),
				PackagePath,
				true  // Auto layout
			);

			if (Result.bSuccess)
			{
				FString FullPath = PackagePath / PendingBlueprintData->Name;
				AddMessage(EChatMessageRole::System,
					FString::Printf(TEXT("Blueprint '%s' created successfully at %s\n\nWarnings: %d"),
						*PendingBlueprintData->Name, *FullPath, Result.Warnings.Num()));

				// Show warnings if any
				if (Result.Warnings.Num() > 0)
				{
					FString WarningList = FString::Join(Result.Warnings, TEXT("\n  - "));
					AddMessage(EChatMessageRole::System,
						FString::Printf(TEXT("Warnings:\n  - %s"), *WarningList));
				}

				// Clear pending data after successful creation
				PendingBlueprintData.Reset();

				// Sync content browser to show the new asset
				if (Result.Blueprint)
				{
					TArray<UObject*> AssetsToSync;
					AssetsToSync.Add(Result.Blueprint);
					FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
					ContentBrowserModule.Get().SyncBrowserToAssets(AssetsToSync);
				}
			}
			else
			{
				AddMessage(EChatMessageRole::System,
					FString::Printf(TEXT("Failed to create blueprint: %s"), *Result.ErrorMessage));
			}
		}
	}
	else if (Cmd.Equals(TEXT("explain"), ESearchCase::IgnoreCase))
	{
		// Get selected blueprint
		UBlueprint* SelectedBP = GetSelectedBlueprint();
		if (!SelectedBP)
		{
			AddMessage(EChatMessageRole::System,
				TEXT("No blueprint selected. Please select a blueprint in the Content Browser first."));
			return;
		}

		// Get blueprint summary
		FString BPSummary = GetBlueprintSummaryForAI(SelectedBP);

		AddMessage(EChatMessageRole::User, FString::Printf(TEXT("/explain %s"), *SelectedBP->GetName()));

		FString Prompt = FString::Printf(
			TEXT("Please explain this blueprint:\n\n%s\n\nDescribe what this blueprint does, its purpose, and how it works."),
			*BPSummary);

		ConversationContext->AddUserMessage(Prompt);

		bIsProcessing = true;
		UOpenAICompatibleService::Get()->SendChatRequest(
			ConversationContext->GetMessages(),
			FOnAIResponseReceived::CreateSP(this, &SCopilotChatWindow::OnAIResponseReceived)
		);
	}
	else if (Cmd.Equals(TEXT("modify"), ESearchCase::IgnoreCase))
	{
		// Get selected blueprint
		UBlueprint* SelectedBP = GetSelectedBlueprint();
		if (!SelectedBP)
		{
			AddMessage(EChatMessageRole::System,
				TEXT("No blueprint selected. Please select a blueprint in the Content Browser first."));
			return;
		}

		if (Args.IsEmpty())
		{
			AddMessage(EChatMessageRole::System,
				TEXT("Please specify what changes you want. Example: /modify Add a health variable and decrease it on Event Tick"));
			return;
		}

		// Store the blueprint reference for later modification
		BlueprintBeingModified = SelectedBP;
		bIsModifyMode = true;

		// Clear conversation history to reduce request size for modify operations
		ConversationContext->ClearHistory();

		// Get blueprint summary (simplified)
		FString BPSummary = GetBlueprintSummaryForAI(SelectedBP);

		AddMessage(EChatMessageRole::User, FString::Printf(TEXT("/modify %s: %s"), *SelectedBP->GetName(), *Args));

		// Simplified prompt to reduce request size
		FString Prompt = FString::Printf(
			TEXT("Modify blueprint %s:\n%s\n\nChanges: %s\n\nReturn JSON with only new/changed elements."),
			*SelectedBP->GetName(), *BPSummary, *Args);

		ConversationContext->AddUserMessage(Prompt);

		bIsProcessing = true;
		UOpenAICompatibleService::Get()->SendChatRequest(
			ConversationContext->GetMessages(),
			FOnAIResponseReceived::CreateSP(this, &SCopilotChatWindow::OnAIResponseReceived)
		);
	}
	else if (Cmd.Equals(TEXT("clear"), ESearchCase::IgnoreCase))
	{
		PendingBlueprintData.Reset();
		BlueprintBeingModified.Reset();
		bIsModifyMode = false;
		AddMessage(EChatMessageRole::System, TEXT("Pending blueprint data and modify mode cleared."));
	}
	else if (Cmd.Equals(TEXT("help"), ESearchCase::IgnoreCase))
	{
		AddMessage(EChatMessageRole::Assistant,
			TEXT("Available commands:\n\n")
			TEXT("CREATE NEW BLUEPRINT:\n")
			TEXT("  /generate [description] - Generate a new blueprint\n")
			TEXT("  /apply [path] - Create the blueprint (default: /Game/Blueprints)\n\n")
			TEXT("MODIFY EXISTING BLUEPRINT:\n")
			TEXT("  1. Select a blueprint in Content Browser\n")
			TEXT("  2. /modify [changes] - Describe the changes you want\n")
			TEXT("  3. /apply - Apply the changes\n\n")
			TEXT("OTHER COMMANDS:\n")
			TEXT("  /explain - Explain the selected blueprint\n")
			TEXT("  /clear - Clear pending data and modify mode\n")
			TEXT("  /help - Show this help message\n\n")
			TEXT("You can also describe what you want in natural language!"));
	}
	else
	{
		AddMessage(EChatMessageRole::System,
			FString::Printf(TEXT("Unknown command: %s. Type /help for available commands."), *Cmd));
	}
}

bool SCopilotChatWindow::ParseQuickCommand(const FString& Input, FString& OutCommand, FString& OutArgs)
{
	if (!Input.StartsWith(QuickCommandPrefix))
	{
		return false;
	}

	FString Content = Input.Mid(1); // Remove prefix

	int32 SpaceIndex;
	if (Content.FindChar(' ', SpaceIndex))
	{
		OutCommand = Content.Left(SpaceIndex);
		OutArgs = Content.Mid(SpaceIndex + 1).TrimStartAndEnd();
	}
	else
	{
		OutCommand = Content;
		OutArgs = TEXT("");
	}

	return true;
}

FString SCopilotChatWindow::GetSystemPrompt() const
{
	return TEXT(R"(You are UE Copilot for Unreal Engine. Generate blueprints using BSL (Blueprint Script Language).

BSL SYNTAX:
```
blueprint BP_Name extends ParentClass {
  var VariableName: Type = DefaultValue

  event EventName {
    // statements
  }

  function FunctionName(param: Type) -> (output: Type) {
    // statements
  }
}
```

TYPES: bool, int, float, string, vector, rotator, transform, Actor, Pawn, Character

STATEMENTS:
- Assignment: x = value
- If: if (condition) { } else { }
- While: while (condition) { }
- For: for i in 0..10 { }
- Return: return (value1, value2)
- Function calls: FunctionName(args)

OPERATORS: + - * / == != < <= > >= && || !

EXAMPLE - Counter Actor:
```
blueprint BP_Counter extends Actor {
  var Count: int = 0
  var Message: string = "Hello"

  event BeginPlay {
    Count = Count + 1
    PrintString(Message)
    PrintString(Count)
  }

  event Tick {
    if (Count < 100) {
      Count = Count + 1
    }
  }

  function GetDoubleCount() -> (Result: int) {
    Result = Count * 2
  }
}
```

EXAMPLE - Simple Actor with condition:
```
blueprint BP_Health extends Actor {
  var Health: int = 100
  var MaxHealth: int = 100
  var IsDead: bool = false

  event BeginPlay {
    PrintString("Actor spawned")
  }

  function TakeDamage(Amount: int) {
    Health = Health - Amount
    if (Health <= 0) {
      IsDead = true
      PrintString("Actor died!")
    }
  }
}
```

COMMON FUNCTIONS: PrintString, Delay, GetActorLocation, SetActorLocation, DestroyActor

IMPORTANT: Always wrap BSL code in ``` code blocks.)");
}

void SCopilotChatWindow::AddMessage(EChatMessageRole Role, const FString& Content, bool bIsStreaming)
{
	FChatDisplayMessage NewMessage;
	NewMessage.Role = Role;
	NewMessage.Content = Content;
	NewMessage.Timestamp = FDateTime::Now();
	NewMessage.bIsStreaming = bIsStreaming;

	Messages.Add(NewMessage);

	RefreshMessageList();
}

void SCopilotChatWindow::UpdateLastMessage(const FString& AdditionalContent)
{
	if (Messages.Num() > 0)
	{
		Messages.Last().Content += AdditionalContent;
		RefreshMessageList();
	}
}

void SCopilotChatWindow::ScrollToBottom()
{
	if (MessageScrollBox.IsValid())
	{
		MessageScrollBox->ScrollToEnd();
	}
}

UBlueprint* SCopilotChatWindow::GetSelectedBlueprint() const
{
	// Get selected assets from Content Browser
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FAssetData> SelectedAssets;
	ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

	// Find first blueprint in selection
	for (const FAssetData& AssetData : SelectedAssets)
	{
		if (AssetData.AssetClassPath == UBlueprint::StaticClass()->GetClassPathName())
		{
			UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
			if (Blueprint)
			{
				return Blueprint;
			}
		}
	}

	return nullptr;
}

FString SCopilotChatWindow::GetBlueprintSummaryForAI(UBlueprint* Blueprint) const
{
	if (!Blueprint)
	{
		return TEXT("");
	}

	FString Summary;

	// Basic info
	Summary += FString::Printf(TEXT("Blueprint Name: %s\n"), *Blueprint->GetName());
	Summary += FString::Printf(TEXT("Parent Class: %s\n"),
		Blueprint->ParentClass ? *Blueprint->ParentClass->GetPathName() : TEXT("None"));

	// Variables
	Summary += TEXT("\n## Variables:\n");
	if (Blueprint->NewVariables.Num() == 0)
	{
		Summary += TEXT("(No variables)\n");
	}
	else
	{
		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			FString TypeName = Var.VarType.PinCategory.ToString();
			if (Var.VarType.PinSubCategoryObject.IsValid())
			{
				TypeName = Var.VarType.PinSubCategoryObject->GetName();
			}
			Summary += FString::Printf(TEXT("- %s: %s\n"), *Var.VarName.ToString(), *TypeName);
		}
	}

	// Functions
	Summary += TEXT("\n## Functions:\n");
	if (Blueprint->FunctionGraphs.Num() == 0)
	{
		Summary += TEXT("(No custom functions)\n");
	}
	else
	{
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph)
			{
				Summary += FString::Printf(TEXT("- %s\n"), *Graph->GetName());
			}
		}
	}

	// Event Graphs - list events used
	Summary += TEXT("\n## Event Graphs:\n");
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph)
		{
			Summary += FString::Printf(TEXT("- %s (contains %d nodes)\n"),
				*Graph->GetName(), Graph->Nodes.Num());
		}
	}

	return Summary;
}

#undef LOCTEXT_NAMESPACE
