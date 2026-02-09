// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SAIChatWindow.h"
#include "AssetFactoryAISettings.h"
#include "JSON/BlueprintJSONParser.h"
#include "JSON/BlueprintJSONSchema.h"
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
#include "Styling/AppStyle.h"
#include "ISettingsModule.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "Selection.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/FieldIterator.h"
#include "Editor.h"

namespace
{
	FString NormalizeIdentifier(const FString& Input)
	{
		FString Result = Input.ToLower();
		Result.ReplaceInline(TEXT("_"), TEXT(""));
		Result.ReplaceInline(TEXT(" "), TEXT(""));
		Result.ReplaceInline(TEXT("-"), TEXT(""));
		Result.ReplaceInline(TEXT("."), TEXT(""));
		return Result;
	}

	bool ContainsLegacyFunctionFormat(const FString& JsonContent)
	{
		const bool bHasFunctions = JsonContent.Contains(TEXT("\"functions\""));
		const bool bHasLegacyFields =
			JsonContent.Contains(TEXT("\"body\"")) ||
			JsonContent.Contains(TEXT("\"return_type\"")) ||
			JsonContent.Contains(TEXT("\"parameters\""));
		return bHasFunctions && bHasLegacyFields;
	}

	bool FindBlueprintVariableName(UBlueprint* Blueprint, const FBlueprintData& Data, const FString& Key, FString& OutVariableName)
	{
		if (Key.IsEmpty())
		{
			return false;
		}

		const FString NormalizedKey = NormalizeIdentifier(Key);
		const auto Matches = [&NormalizedKey](const FString& Candidate)
		{
			return !Candidate.IsEmpty() && NormalizeIdentifier(Candidate) == NormalizedKey;
		};

		for (const FBlueprintVariableData& Var : Data.Variables)
		{
			if (Matches(Var.Name))
			{
				OutVariableName = Var.Name;
				return true;
			}
		}

		if (Blueprint)
		{
			for (const FBPVariableDescription& Desc : Blueprint->NewVariables)
			{
				FString Candidate = Desc.VarName.ToString();
				if (Matches(Candidate))
				{
					OutVariableName = Candidate;
					return true;
				}
			}

			if (Blueprint->GeneratedClass)
			{
				for (TFieldIterator<FProperty> It(Blueprint->GeneratedClass); It; ++It)
				{
					FString Candidate = It->GetName();
					if (Matches(Candidate))
					{
						OutVariableName = Candidate;
						return true;
					}
				}
			}
		}

		return false;
	}

	bool TryCreateSyntheticEventNode(const FString& NodeId, FBlueprintNodeData& OutNode)
	{
		if (!NodeId.StartsWith(TEXT("event_"), ESearchCase::IgnoreCase))
		{
			return false;
		}

		FString EventKey = NodeId.Mid(6);
		FString NormalizedKey = NormalizeIdentifier(EventKey);

		if (NormalizedKey == TEXT("tick"))
		{
			OutNode.NodeId = NodeId;
			OutNode.NodeType = EBlueprintNodeType::Event_Tick;
			OutNode.EventName = TEXT("Tick");
			return true;
		}

		if (NormalizedKey == TEXT("beginplay") || NormalizedKey == TEXT("begin"))
		{
			OutNode.NodeId = NodeId;
			OutNode.NodeType = EBlueprintNodeType::Event_BeginPlay;
			OutNode.EventName = TEXT("BeginPlay");
			return true;
		}

		OutNode.NodeId = NodeId;
		OutNode.NodeType = EBlueprintNodeType::Event_Custom;
		OutNode.EventName = EventKey;
		return true;
	}

	bool TryCreateSyntheticVariableNode(UBlueprint* Blueprint, const FBlueprintData& Data, const FString& NodeId, FBlueprintNodeData& OutNode)
	{
		bool bIsGetter = NodeId.StartsWith(TEXT("get_"), ESearchCase::IgnoreCase);
		bool bIsSetter = NodeId.StartsWith(TEXT("set_"), ESearchCase::IgnoreCase);

		if (!bIsGetter && !bIsSetter)
		{
			return false;
		}

		const int32 PrefixLen = bIsGetter ? 4 : 4;
		FString Key = NodeId.Mid(PrefixLen);
		if (Key.IsEmpty())
		{
			return false;
		}

		FString VariableName;
		if (!FindBlueprintVariableName(Blueprint, Data, Key, VariableName))
		{
			return false;
		}

		OutNode.NodeId = NodeId;
		OutNode.NodeType = bIsGetter ? EBlueprintNodeType::Variable_Get : EBlueprintNodeType::Variable_Set;
		OutNode.VariableName = VariableName;
		return true;
	}

	bool TryCreateSyntheticNode(UBlueprint* Blueprint, const FBlueprintData& Data, const FString& NodeId, FBlueprintNodeData& OutNode)
	{
		if (TryCreateSyntheticEventNode(NodeId, OutNode))
		{
			return true;
		}

		return TryCreateSyntheticVariableNode(Blueprint, Data, NodeId, OutNode);
	}

	bool TryCreateSyntheticFunctionNode(const FString& NodeId, FBlueprintNodeData& OutNode)
	{
		const FString Key = NormalizeIdentifier(NodeId);
		if (Key == TEXT("fnentry") || Key == TEXT("functionentry") || Key == TEXT("entry"))
		{
			OutNode.NodeId = NodeId;
			OutNode.NodeType = EBlueprintNodeType::Unknown;
			return true;
		}

		if (Key == TEXT("fnresult") || Key == TEXT("functionresult"))
		{
			OutNode.NodeId = NodeId;
			OutNode.NodeType = EBlueprintNodeType::Return;
			return true;
		}

		return false;
	}

	bool IsTargetEventGraph(const FBlueprintGraphData& Graph)
	{
		if (Graph.Name.IsEmpty())
		{
			return true;
		}

		if (Graph.Name.Contains(TEXT("Tick"), ESearchCase::IgnoreCase))
		{
			return true;
		}

		static const TArray<FString> SpecialNames = { TEXT("EventGraph"), TEXT("Ubergraph") };
		for (const FString& Candidate : SpecialNames)
		{
			if (Graph.Name.Equals(Candidate, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
}
	FString NormalizeSkillText(const FString& Input)
	{
		FString Result;
		Result.Reserve(Input.Len());
		for (TCHAR Ch : Input.ToLower())
		{
			if (FChar::IsAlnum(Ch))
			{
				Result.AppendChar(Ch);
			}
		}
		return Result;
	}

	FString ResolveSkillParentBucket(UBlueprint* Blueprint)
	{
		if (!Blueprint || !Blueprint->ParentClass)
		{
			return TEXT("unknown");
		}

		const FString ParentClassPath = NormalizeSkillText(Blueprint->ParentClass->GetPathName());
		if (ParentClassPath.Contains(TEXT("userwidget")) || ParentClassPath.Contains(TEXT("widget")))
		{
			return TEXT("widget");
		}
		if (ParentClassPath.Contains(TEXT("character")) || ParentClassPath.Contains(TEXT("pawn")))
		{
			return TEXT("character");
		}
		if (ParentClassPath.Contains(TEXT("gamemode")) || ParentClassPath.Contains(TEXT("gamestate")) ||
			ParentClassPath.Contains(TEXT("playercontroller")) || ParentClassPath.Contains(TEXT("playerstate")))
		{
			return TEXT("gameframework");
		}
		if (ParentClassPath.Contains(TEXT("actorcomponent")) || ParentClassPath.Contains(TEXT("scenecomponent")) ||
			ParentClassPath.Contains(TEXT("component")) || ParentClassPath.Contains(TEXT("actor")))
		{
			return TEXT("actor");
		}
		return TEXT("unknown");
	}

	FString ResolveSkillNeedBucket(const FString& Requirement)
	{
		const FString Req = NormalizeSkillText(Requirement);
		if (Req.Contains(TEXT("function")) || Req.Contains(TEXT("return")) || Req.Contains(TEXT("branch")) ||
			Req.Contains(TEXT("if")) || Req.Contains(TEXT("compare")) || Req.Contains(TEXT("output")))
		{
			return TEXT("function");
		}
		if (Req.Contains(TEXT("tick")) || Req.Contains(TEXT("beginplay")) || Req.Contains(TEXT("event")) ||
			Req.Contains(TEXT("trigger")) || Req.Contains(TEXT("overlap")) || Req.Contains(TEXT("onclick")) ||
			Req.Contains(TEXT("pressed")) || Req.Contains(TEXT("released")))
		{
			return TEXT("event");
		}
		return TEXT("mixed");
	}

	TArray<FString> GetRuntimeSkillIds(UBlueprint* Blueprint, const FString& Requirement)
	{
		const FString ParentBucket = ResolveSkillParentBucket(Blueprint);
		const FString NeedBucket = ResolveSkillNeedBucket(Requirement);
		TArray<FString> SkillIds;

		if (NeedBucket == TEXT("function"))
		{
			SkillIds.Add(TEXT("ue-bp-function-logic"));
		}
		else if (NeedBucket == TEXT("event"))
		{
			SkillIds.Add(TEXT("ue-bp-event-graph-logic"));
		}
		else
		{
			SkillIds.Add(TEXT("ue-bp-function-logic"));
			SkillIds.Add(TEXT("ue-bp-event-graph-logic"));
		}

		if (ParentBucket == TEXT("actor"))
		{
			SkillIds.Add(TEXT("ue-bp-actor-interaction"));
		}
		else if (ParentBucket == TEXT("character"))
		{
			SkillIds.Add(TEXT("ue-bp-character-gameplay"));
		}
		else if (ParentBucket == TEXT("widget"))
		{
			SkillIds.Add(TEXT("ue-bp-widget-ui-logic"));
		}
		else if (ParentBucket == TEXT("gameframework"))
		{
			SkillIds.Add(TEXT("ue-bp-gameframework-rules"));
		}

		TArray<FString> UniqueSkills;
		for (const FString& SkillId : SkillIds)
		{
			UniqueSkills.AddUnique(SkillId);
		}
		SkillIds = MoveTemp(UniqueSkills);
		return SkillIds;
	}

	bool TryLoadSkillSnippet(const FString& SkillId, FString& OutSnippet)
	{
		const FString SkillPath = FPaths::Combine(
			FPaths::ProjectPluginsDir(),
			TEXT("UECopilot"),
			TEXT("Skills"),
			SkillId,
			TEXT("SKILL.md"));

		FString Raw;
		if (!FPaths::FileExists(SkillPath) || !FFileHelper::LoadFileToString(Raw, *SkillPath))
		{
			return false;
		}

		TArray<FString> Lines;
		Raw.ParseIntoArrayLines(Lines, true);

		FString Snippet;
		int32 Taken = 0;
		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStartAndEnd();
			if (Trimmed.IsEmpty())
			{
				continue;
			}
			if (Trimmed.StartsWith(TEXT("#")))
			{
				continue;
			}

			Snippet += Trimmed + TEXT("\n");
			++Taken;
			if (Taken >= 10 || Snippet.Len() >= 900)
			{
				break;
			}
		}

		OutSnippet = Snippet.TrimStartAndEnd();
		return !OutSnippet.IsEmpty();
	}

	FString BuildRuntimeSkillPrompt(UBlueprint* Blueprint, const FString& Requirement)
	{
		const TArray<FString> SkillIds = GetRuntimeSkillIds(Blueprint, Requirement);
		if (SkillIds.Num() == 0)
		{
			return FString();
		}

		FString Prompt = TEXT("\nRuntime skill routing (auto-selected):\n");
		for (const FString& SkillId : SkillIds)
		{
			FString SkillSnippet;
			Prompt += FString::Printf(TEXT("- %s"), *SkillId);
			if (TryLoadSkillSnippet(SkillId, SkillSnippet))
			{
				Prompt += TEXT(" constraints:\n");
				Prompt += SkillSnippet;
				Prompt += TEXT("\n");
			}
			else
			{
				Prompt += TEXT(" constraints: (SKILL.md not found, use default best practice)\n");
			}
		}

		Prompt += TEXT("Apply these skill constraints while producing blueprint JSON.\n");
		return Prompt;
	}
}

#define LOCTEXT_NAMESPACE "SAIChatWindow"

DEFINE_LOG_CATEGORY_STATIC(LogSAIChatWindow, Log, All);
const FString SAIChatWindow::QuickCommandPrefix = TEXT("/");

void SAIChatWindow::Construct(const FArguments& InArgs)
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

SAIChatWindow::~SAIChatWindow()
{
	// Cancel any pending requests
	if (UOpenAICompatibleService* Service = UOpenAICompatibleService::Get())
	{
		Service->CancelRequest();
	}
}

TSharedRef<SWidget> SAIChatWindow::BuildToolbar()
{
	return SNew(SHorizontalBox)

		// Title
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ChatWindowTitle", "Asset Factory AI"))
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
			.OnClicked(this, &SAIChatWindow::OnClearClicked)
			.ToolTipText(LOCTEXT("ClearTooltip", "Clear chat history"))
		]

		// Settings button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("SettingsButton", "Settings"))
			.OnClicked(this, &SAIChatWindow::OnSettingsClicked)
			.ToolTipText(LOCTEXT("SettingsTooltip", "Open UE Copilot settings"))
		];
}

TSharedRef<SWidget> SAIChatWindow::BuildChatMessageList()
{
	return SAssignNew(MessageScrollBox, SScrollBox)
		+ SScrollBox::Slot()
		[
			SAssignNew(MessageListBox, SVerticalBox)
		];
}

TSharedRef<SWidget> SAIChatWindow::BuildInputArea()
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
				.OnTextCommitted(this, &SAIChatWindow::OnInputCommitted)
				.OnKeyDownHandler(this, &SAIChatWindow::OnInputKeyDown)
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
			.OnClicked(this, &SAIChatWindow::OnSendClicked)
			.IsEnabled_Lambda([this]() { return !bIsProcessing; })
		];
}

TSharedRef<SWidget> SAIChatWindow::CreateMessageWidget(const FChatDisplayMessage& Message)
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

void SAIChatWindow::RefreshMessageList()
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

void SAIChatWindow::SendMessage()
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

	// New user input resets schema correction retry state.
	bSchemaCorrectionRetried = false;

	UE_LOG(LogSAIChatWindow, Log, TEXT("User input: %s"), *InputText);

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

	TArray<FChatMessage> OutgoingMessages = ConversationContext->GetMessages();
	LogConversationMessages(OutgoingMessages);

	UOpenAICompatibleService* Service = UOpenAICompatibleService::Get();
	UAssetFactoryAISettings* Settings = UAssetFactoryAISettings::Get();

	if (Settings->bEnableStreaming)
	{
		CurrentStreamingResponse.Empty();
		AddMessage(EChatMessageRole::Assistant, TEXT(""), true);

		Service->SendChatRequestStreaming(
			OutgoingMessages,
			FOnAIStreamChunk::CreateSP(this, &SAIChatWindow::OnAIStreamChunk),
			FOnAIStreamComplete::CreateSP(this, &SAIChatWindow::OnAIStreamComplete),
			FOnAIError::CreateSP(this, &SAIChatWindow::OnAIError)
		);
	}
	else
	{
		Service->SendChatRequest(
			OutgoingMessages,
			FOnAIResponseReceived::CreateSP(this, &SAIChatWindow::OnAIResponseReceived)
		);
	}
}

FReply SAIChatWindow::OnSendClicked()
{
	SendMessage();
	return FReply::Handled();
}

void SAIChatWindow::OnInputCommitted(const FText& Text, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		// Only send on Shift+Enter to allow multiline input
		// Regular Enter creates newline
	}
}

FReply SAIChatWindow::OnInputKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
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

void SAIChatWindow::OnAIResponseReceived(const FAIResponse& Response)
{
	bIsProcessing = false;

	UE_LOG(LogSAIChatWindow, Log, TEXT("AI response raw: %s"), *Response.Content);

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

void SAIChatWindow::OnAIStreamChunk(const FString& Chunk)
{
	CurrentStreamingResponse += Chunk;
	UpdateLastMessage(Chunk);
}

void SAIChatWindow::OnAIStreamComplete()
{
	bIsProcessing = false;

	UE_LOG(LogSAIChatWindow, Log, TEXT("AI streaming response complete: %s"), *CurrentStreamingResponse);

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

void SAIChatWindow::OnAIError(const FString& ErrorMessage)
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

void SAIChatWindow::ProcessAIResponse(const FString& ResponseContent)
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
		UE_LOG(LogSAIChatWindow, Log, TEXT("Extracted BSL block: %s"), *BSLContent);
		// Try BSL compilation
		BSL::FCompileResult CompileResult = BSL::FCompiler::Compile(BSLContent);

		if (CompileResult.bSuccess)
		{
			LogBlueprintDataSummary(CompileResult.BlueprintData, TEXT("BSL compiled"));
			EnrichBlueprintData(CompileResult.BlueprintData);
			EnsureTickGraph(CompileResult.BlueprintData);
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

	if (!JSONContent.IsEmpty())
	{
		UE_LOG(LogSAIChatWindow, Log, TEXT("Extracted JSON block: %s"), *JSONContent);
	}

	if (!JSONContent.IsEmpty() && JSONContent.StartsWith(TEXT("{")))
	{
		if (ContainsLegacyFunctionFormat(JSONContent))
		{
			PendingBlueprintData.Reset();

			if (!bSchemaCorrectionRetried)
			{
				bSchemaCorrectionRetried = true;
				AddMessage(EChatMessageRole::System,
					TEXT("AI returned an unsupported function format (body/return_type/parameters). Retrying once with strict node-graph schema."));

				FString CorrectionPrompt = TEXT("Rewrite ONLY the previous JSON using graph format. ")
					TEXT("For function logic, use functions[].nodes with explicit node_id/node_type/pins. ")
					TEXT("Do NOT use body, return_type, or parameters fields. Return JSON only.");
				CorrectionPrompt += GetBlueprintSchemaHint();

				ConversationContext->AddUserMessage(CorrectionPrompt);
				bIsProcessing = true;
				UOpenAICompatibleService::Get()->SendChatRequest(
					ConversationContext->GetMessages(),
					FOnAIResponseReceived::CreateSP(this, &SAIChatWindow::OnAIResponseReceived)
				);
			}
			else
			{
				AddMessage(EChatMessageRole::System,
					TEXT("AI still returned unsupported function format after retry. Please run /modify again; function logic must be expressed as nodes, not body text."));
			}
			return;
		}

		FBlueprintParseResult ParseResult = UBlueprintJSONParser::ParseBlueprintJSON(JSONContent);

		if (ParseResult.bSuccess)
		{
			LogBlueprintDataSummary(ParseResult.BlueprintData, TEXT("JSON parsed"));
			EnrichBlueprintData(ParseResult.BlueprintData);
			EnsureTickGraph(ParseResult.BlueprintData);
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
		else
		{
			PendingBlueprintData.Reset();
			AddMessage(EChatMessageRole::System,
				FString::Printf(TEXT("Failed to parse JSON response: %s"), *ParseResult.ErrorMessage));
		}
	}
}

FReply SAIChatWindow::OnClearClicked()
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

void SAIChatWindow::EnrichBlueprintData(FBlueprintData& Data)
{
	if (Data.Name.IsEmpty() && bIsModifyMode && BlueprintBeingModified.IsValid())
	{
		Data.Name = BlueprintBeingModified->GetName();
	}

	if (Data.ParentClass.IsEmpty() && bIsModifyMode && BlueprintBeingModified.IsValid() && BlueprintBeingModified->ParentClass)
	{
		Data.ParentClass = BlueprintBeingModified->ParentClass->GetName();
	}
}

void SAIChatWindow::EnsureTickGraph(FBlueprintData& Data) const
{
	if (!BlueprintBeingModified.IsValid())
	{
		return;
	}

	UBlueprint* Blueprint = BlueprintBeingModified.Get();
	if (!Blueprint)
	{
		return;
	}

	for (FBlueprintGraphData& Graph : Data.EventGraphs)
	{
		if (!IsTargetEventGraph(Graph))
		{
			continue;
		}

		Graph.Name = TEXT("EventGraph");

		TSet<FString> ExistingNodeIds;
		TSet<FString> ReferencedNodeIds;

		for (const FBlueprintNodeData& Node : Graph.Nodes)
		{
			if (!Node.NodeId.IsEmpty())
			{
				ExistingNodeIds.Add(Node.NodeId);
			}

			for (const FBlueprintPinData& Pin : Node.Pins)
			{
				for (const FBlueprintPinConnection& Conn : Pin.Connections)
				{
					if (!Conn.SourceNodeId.IsEmpty())
					{
						ReferencedNodeIds.Add(Conn.SourceNodeId);
					}
				}
			}
		}

		for (const FString& SourceId : ReferencedNodeIds)
		{
			if (ExistingNodeIds.Contains(SourceId))
			{
				continue;
			}

			FBlueprintNodeData Synthetic;
			if (TryCreateSyntheticNode(Blueprint, Data, SourceId, Synthetic))
			{
				Graph.Nodes.Add(Synthetic);
				ExistingNodeIds.Add(SourceId);
			}
		}
	}

	for (FBlueprintGraphData& Graph : Data.Functions)
	{
		TSet<FString> ExistingNodeIds;
		TSet<FString> ReferencedNodeIds;

		for (const FBlueprintNodeData& Node : Graph.Nodes)
		{
			if (!Node.NodeId.IsEmpty())
			{
				ExistingNodeIds.Add(Node.NodeId);
			}

			for (const FBlueprintPinData& Pin : Node.Pins)
			{
				for (const FBlueprintPinConnection& Conn : Pin.Connections)
				{
					if (!Conn.SourceNodeId.IsEmpty())
					{
						ReferencedNodeIds.Add(Conn.SourceNodeId);
					}
				}
			}
		}

		for (const FString& SourceId : ReferencedNodeIds)
		{
			if (ExistingNodeIds.Contains(SourceId))
			{
				continue;
			}

			FBlueprintNodeData Synthetic;
			if (TryCreateSyntheticFunctionNode(SourceId, Synthetic) || TryCreateSyntheticVariableNode(Blueprint, Data, SourceId, Synthetic))
			{
				Graph.Nodes.Add(Synthetic);
				ExistingNodeIds.Add(SourceId);
			}
		}
	}
}

FReply SAIChatWindow::OnSettingsClicked()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->ShowViewer("Editor", "Plugins", "Asset Factory AI");
	}

	return FReply::Handled();
}

void SAIChatWindow::ExecuteQuickCommand(const FString& Command)
{
	FString Cmd, Args;
	ParseQuickCommand(Command, Cmd, Args);
	bSchemaCorrectionRetried = false;

	if (Cmd.Equals(TEXT("generate"), ESearchCase::IgnoreCase))
	{
		FString Prompt = Args.IsEmpty()
			? TEXT("Generate a blueprint based on my description.")
			: FString::Printf(TEXT("Generate a blueprint: %s"), *Args);
		Prompt += GetBlueprintSchemaHint();

		AddMessage(EChatMessageRole::User, Command);
		UE_LOG(LogSAIChatWindow, Log, TEXT("Generate prompt: %s"), *Prompt);
			ConversationContext->AddUserMessage(Prompt);

		bIsProcessing = true;
		UOpenAICompatibleService::Get()->SendChatRequest(
			ConversationContext->GetMessages(),
			FOnAIResponseReceived::CreateSP(this, &SAIChatWindow::OnAIResponseReceived)
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
			FOnAIResponseReceived::CreateSP(this, &SAIChatWindow::OnAIResponseReceived)
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

		AddMessage(EChatMessageRole::User, FString::Printf(TEXT("/modify %s: %s"), *SelectedBP->GetName(), *Args));

		// Simplified prompt to reduce request size
		FString Prompt = BuildModifyPromptForTest(SelectedBP, Args);

		UE_LOG(LogSAIChatWindow, Log, TEXT("Modify prompt: %s"), *Prompt);
		ConversationContext->AddUserMessage(Prompt);

		bIsProcessing = true;
		UOpenAICompatibleService::Get()->SendChatRequest(
			ConversationContext->GetMessages(),
			FOnAIResponseReceived::CreateSP(this, &SAIChatWindow::OnAIResponseReceived)
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

bool SAIChatWindow::ParseQuickCommand(const FString& Input, FString& OutCommand, FString& OutArgs)
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

FString SAIChatWindow::BuildModifyPromptForTest(UBlueprint* SelectedBlueprint, const FString& Args) const
{
	if (!SelectedBlueprint)
	{
		return FString();
	}

	const FString BPSummary = GetBlueprintSummaryForAI(SelectedBlueprint);
	FString Prompt = FString::Printf(
		TEXT("Modify blueprint %s:\n%s\n\nChanges: %s\n\nReturn JSON with only new/changed elements."),
		*SelectedBlueprint->GetName(), *BPSummary, *Args);
	Prompt += BuildRuntimeSkillPrompt(SelectedBlueprint, Args);
	Prompt += GetBlueprintSchemaHint();
	return Prompt;
}

#if WITH_DEV_AUTOMATION_TESTS
bool SAIChatWindow::ParseQuickCommandForTest(const FString& Input, FString& OutCommand, FString& OutArgs)
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

bool SAIChatWindow::TryBuildModifyPromptFromInputForTest(
	const FString& Input,
	UBlueprint* SelectedBlueprint,
	FString& OutPrompt,
	FString& OutError) const
{
	OutPrompt.Empty();
	OutError.Empty();

	FString Cmd;
	FString Args;
	if (!ParseQuickCommandForTest(Input, Cmd, Args))
	{
		OutError = TEXT("Input is not a quick command");
		return false;
	}

	if (!Cmd.Equals(TEXT("modify"), ESearchCase::IgnoreCase))
	{
		OutError = TEXT("Only /modify is supported by this helper");
		return false;
	}

	if (!SelectedBlueprint)
	{
		OutError = TEXT("No blueprint selected");
		return false;
	}

	if (Args.IsEmpty())
	{
		OutError = TEXT("Modify args are empty");
		return false;
	}

	OutPrompt = BuildModifyPromptForTest(SelectedBlueprint, Args);
	return !OutPrompt.IsEmpty();
}
#endif

FString SAIChatWindow::GetSystemPrompt() const
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

void SAIChatWindow::AddMessage(EChatMessageRole Role, const FString& Content, bool bIsStreaming)
{
	FChatDisplayMessage NewMessage;
	NewMessage.Role = Role;
	NewMessage.Content = Content;
	NewMessage.Timestamp = FDateTime::Now();
	NewMessage.bIsStreaming = bIsStreaming;

	Messages.Add(NewMessage);

	RefreshMessageList();
}

void SAIChatWindow::UpdateLastMessage(const FString& AdditionalContent)
{
	if (Messages.Num() > 0)
	{
		Messages.Last().Content += AdditionalContent;
		RefreshMessageList();
	}
}

void SAIChatWindow::ScrollToBottom()
{
	if (MessageScrollBox.IsValid())
	{
		MessageScrollBox->ScrollToEnd();
	}
}

FString RoleToString(EChatMessageRole Role)
{
	switch (Role)
	{
	case EChatMessageRole::User: return TEXT("User");
	case EChatMessageRole::Assistant: return TEXT("Assistant");
	case EChatMessageRole::System: return TEXT("System");
	default: return TEXT("Unknown");
	}
}

void SAIChatWindow::LogConversationMessages(const TArray<FChatMessage>& MessageList) const
{
	for (int32 Index = 0; Index < MessageList.Num(); ++Index)
	{
		const FChatMessage& Message = MessageList[Index];
		FString Content = Message.Content.Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT("\\n"));
		Content = Content.Left(256);
		UE_LOG(LogSAIChatWindow, Log, TEXT("Outgoing[%d] %s: %s"), Index, *RoleToString(Message.Role), *Content);
	}
}

void SAIChatWindow::LogBlueprintDataSummary(const FBlueprintData& Data, const TCHAR* Source) const
{
	UE_LOG(LogSAIChatWindow, Log, TEXT("%s blueprint summary: name=%s parent=%s vars=%d events=%d funcs=%d"),
		Source,
		Data.Name.IsEmpty() ? TEXT("(empty)") : *Data.Name,
		Data.ParentClass.IsEmpty() ? TEXT("(empty)") : *Data.ParentClass,
		Data.Variables.Num(),
		Data.EventGraphs.Num(),
		Data.Functions.Num());
}

UBlueprint* SAIChatWindow::GetSelectedBlueprint() const
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

FString SAIChatWindow::GetBlueprintSummaryForAI(UBlueprint* Blueprint) const
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
FString SAIChatWindow::GetBlueprintSchemaHint()
{
	return TEXT("\nPlease return JSON following this pattern:\n")
		TEXT("{\n")
		TEXT("  \"name\": \"BP_Name\",\n")
		TEXT("  \"parent_class\": \"Actor\",\n")
		TEXT("  \"functions\": [\n")
		TEXT("    {\n")
		TEXT("      \"name\": \"GetLocation\",\n")
		TEXT("      \"outputs\": [{\"name\": \"ReturnValue\", \"type\": \"int\"}],\n")
		TEXT("      \"nodes\": [\n")
		TEXT("        {\"node_id\": \"get_aaa\", \"node_type\": \"Variable_Get\", \"variable\": \"aaa\"},\n")
		TEXT("        {\"node_id\": \"cmp_gt\", \"node_type\": \"Compare_Greater\", \"pins\": {\"A\": {\"connection\": \"get_aaa.aaa\"}, \"B\": {\"value\": \"0\"}}},\n")
		TEXT("        {\"node_id\": \"branch\", \"node_type\": \"Flow_Branch\", \"pins\": {\"execute\": {\"connection\": \"fn_entry.then\"}, \"Condition\": {\"connection\": \"cmp_gt.ReturnValue\"}}},\n")
		TEXT("        {\"node_id\": \"return_true\", \"node_type\": \"Return\", \"pins\": {\"execute\": {\"connection\": \"branch.Then\"}, \"ReturnValue\": {\"connection\": \"get_aaa.aaa\"}}},\n")
		TEXT("        {\"node_id\": \"fn_result_else\", \"node_type\": \"Return\", \"pins\": {\"execute\": {\"connection\": \"branch.Else\"}, \"ReturnValue\": {\"value\": \"0\"}}}\n")
		TEXT("      ]\n")
		TEXT("    }\n")
		TEXT("  ],\n")
		TEXT("  \"event_graphs\": [\n")
		TEXT("    {\n")
		TEXT("      \"name\": \"Tick\",\n")
		TEXT("      \"nodes\": [\n")
		TEXT("        {\n")
		TEXT("          \"node_id\": \"math_subtract\",\n")
		TEXT("          \"node_type\": \"Math_Subtract\",\n")
		TEXT("          \"pins\": {\n")
		TEXT("            \"A\": {\"connection\": \"get_health.Health\"},\n")
		TEXT("            \"B\": {\"value\": \"1\"}\n")
		TEXT("          }\n")
		TEXT("        },\n")
		TEXT("        {\n")
		TEXT("          \"node_id\": \"set_health\",\n")
		TEXT("          \"node_type\": \"Variable_Set\",\n")
		TEXT("          \"pins\": {\n")
		TEXT("            \"execute\": {\"connection\": \"event_tick.then\"},\n")
		TEXT("            \"Health\": {\"connection\": \"math_subtract.ReturnValue\"}\n")
		TEXT("          }\n")
		TEXT("        }\n")
		TEXT("      ]\n")
		TEXT("    }\n")
		TEXT("  ]\n")
		TEXT("}\n")
		TEXT("Rules:\n")
		TEXT("- Function logic must be represented with graph nodes in functions[].nodes.\n")
		TEXT("- In function graphs, use the built-in entry node id \"fn_entry\" for exec flow.\n")
		TEXT("- Prefer \"inputs\"/\"outputs\" for function signatures.\n")
		TEXT("- Use \"ReturnValue\" as return pin name for non-void functions.\n")
		TEXT("- Do NOT use field \"body\".\n")
		TEXT("- Return JSON only, no markdown.");
}
