// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SCopilotChatWindow.h"
#include "UECopilotSettings.h"
#include "JSON/BlueprintJSONParser.h"
#include "Factory/AIBlueprintFactory.h"
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

#define LOCTEXT_NAMESPACE "SCopilotChatWindow"

const FString SCopilotChatWindow::QuickCommandPrefix = TEXT("/");

void SCopilotChatWindow::Construct(const FArguments& InArgs)
{
	// Initialize conversation context
	ConversationContext = UAIConversationContext::Create(GetTransientPackage(), GetSystemPrompt());

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
		TEXT("  /generate - Generate a new blueprint\n")
		TEXT("  /explain - Explain a blueprint\n")
		TEXT("  /modify - Modify an existing blueprint\n\n")
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

			// Message content
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Message.Content))
				.AutoWrapText(true)
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
	// Try to extract and parse JSON from response
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
				AddMessage(EChatMessageRole::System,
					FString::Printf(TEXT("Parsed blueprint '%s' successfully. Use /apply to create it."),
						*ParseResult.BlueprintData.Name));

				// Store for later application
				// TODO: Store parsed data for /apply command
			}
			else
			{
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

	if (ConversationContext)
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
	else if (Cmd.Equals(TEXT("explain"), ESearchCase::IgnoreCase))
	{
		AddMessage(EChatMessageRole::System,
			TEXT("/explain command: Select a blueprint in the Content Browser, then use this command to get an explanation."));
	}
	else if (Cmd.Equals(TEXT("modify"), ESearchCase::IgnoreCase))
	{
		AddMessage(EChatMessageRole::System,
			TEXT("/modify command: Select a blueprint in the Content Browser, then describe the changes you want."));
	}
	else if (Cmd.Equals(TEXT("help"), ESearchCase::IgnoreCase))
	{
		AddMessage(EChatMessageRole::Assistant,
			TEXT("Available commands:\n\n")
			TEXT("/generate [description] - Generate a new blueprint\n")
			TEXT("/explain - Explain the selected blueprint\n")
			TEXT("/modify [changes] - Modify the selected blueprint\n")
			TEXT("/help - Show this help message\n\n")
			TEXT("You can also just describe what you want in natural language!"));
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
	return TEXT(R"(You are UE Copilot, an AI assistant for Unreal Engine blueprint development.

Your capabilities:
1. Generate new Blueprint classes from natural language descriptions
2. Explain existing Blueprint functionality
3. Suggest modifications to existing Blueprints
4. Answer questions about Unreal Engine Blueprint systems

When generating blueprints, respond with a JSON structure following this schema:
```json
{
  "blueprint": {
    "name": "BP_Example",
    "parent_class": "/Script/Engine.Actor",
    "variables": [
      {"name": "MyVar", "type": "float", "default_value": "0.0"}
    ],
    "event_graphs": [
      {
        "name": "EventGraph",
        "nodes": [
          {"id": "node_001", "type": "BeginPlay", "position": {"x": 0, "y": 0}, "pins": {}},
          {"id": "node_002", "type": "CallFunction", "function": "/Script/Engine.KismetSystemLibrary.PrintString",
           "position": {"x": 300, "y": 0},
           "pins": {"execute": {"connection": "node_001.then"}, "InString": {"value": "Hello World"}}}
        ]
      }
    ]
  }
}
```

Node types:
- Events: BeginPlay, Tick, CustomEvent, InputEvent
- Flow: Branch, Sequence, ForLoop, ForEachLoop, WhileLoop, Delay, DoOnce
- Functions: CallFunction (with function path like /Script/Engine.ClassName.FunctionName)
- Variables: Get, Set
- Math: Add, Subtract, Multiply, Divide
- Comparison: Equal, NotEqual, Greater, Less, GreaterEqual, LessEqual
- Logic: And, Or, Not
- Cast: Cast (with target_class)

Pin connections use format: "pin_name": {"connection": "source_node_id.source_pin_name"}
Default values use format: "pin_name": {"value": "default_value"}

Always provide clear explanations along with any generated JSON.)");
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

#undef LOCTEXT_NAMESPACE
