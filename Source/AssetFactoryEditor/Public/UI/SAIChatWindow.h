// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "UObject/StrongObjectPtr.h"
#include "AI/OpenAICompatibleService.h"
#include "AI/AFConversationContext.h"
#include "JSON/BlueprintJSONSchema.h"

class SMultiLineEditableTextBox;
class SScrollBox;
class SVerticalBox;
class UBlueprint;

/**
 * Chat message display data
 */
struct FChatDisplayMessage
{
	EChatMessageRole Role;
	FString Content;
	FDateTime Timestamp;
	bool bIsStreaming = false;
};

/**
 * Copilot Chat Window Widget
 * Provides the main chat interface for AI-assisted blueprint generation
 */
class ASSETFACTORYEDITOR_API SAIChatWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAIChatWindow) {}
	SLATE_END_ARGS()

	/** Construct this widget */
	void Construct(const FArguments& InArgs);

	/** Destructor */
	virtual ~SAIChatWindow();

	/** Build modify prompt from selected blueprint + requested changes. */
	FString BuildModifyPromptForTest(UBlueprint* SelectedBlueprint, const FString& Args) const;

#if WITH_DEV_AUTOMATION_TESTS
	/** Parse a quick command from raw editor input, e.g. "/modify xxx". */
	static bool ParseQuickCommandForTest(const FString& Input, FString& OutCommand, FString& OutArgs);

	/** Build a modify prompt directly from raw user input. */
	bool TryBuildModifyPromptFromInputForTest(
		const FString& Input,
		UBlueprint* SelectedBlueprint,
		FString& OutPrompt,
		FString& OutError) const;

	/** Invoke an MCP tool directly from tests. */
	bool InvokeMCPToolForTest(
		const TArray<FChatMessage>& InMessages,
		const FString& ToolName,
		FString& OutResponseContent,
		FString& OutError) const;

	/** Invoke MCP layout tool directly from tests. */
	bool InvokeMCPLayoutForTest(
		const FBlueprintData& InData,
		FBlueprintData& OutData,
		FString& OutError) const;
#endif

private:
	/** Build the header toolbar */
	TSharedRef<SWidget> BuildToolbar();

	/** Build the chat message list */
	TSharedRef<SWidget> BuildChatMessageList();

	/** Build the input area */
	TSharedRef<SWidget> BuildInputArea();

	/** Create a message widget */
	TSharedRef<SWidget> CreateMessageWidget(const FChatDisplayMessage& Message);

	/** Refresh the message list display */
	void RefreshMessageList();

	/** Send the current input as a message */
	void SendMessage();

	/** Handle send button click */
	FReply OnSendClicked();

	/** Handle input text committed */
	void OnInputCommitted(const FText& Text, ETextCommit::Type CommitType);

	/** Handle key down in input */
	FReply OnInputKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent);

	/** Handle AI response received */
	void OnAIResponseReceived(const FAIResponse& Response);

	/** Handle AI stream chunk */
	void OnAIStreamChunk(const FString& Chunk);

	/** Handle AI stream complete */
	void OnAIStreamComplete();

	/** Handle AI error */
	void OnAIError(const FString& ErrorMessage);

	/** Process AI response for blueprint generation */
	void ProcessAIResponse(const FString& ResponseContent);

	/** Clear chat history */
	FReply OnClearClicked();

	/** Open settings */
	FReply OnSettingsClicked();

	/** Handle quick command */
	void ExecuteQuickCommand(const FString& Command);

	/** Parse quick commands from input */
	bool ParseQuickCommand(const FString& Input, FString& OutCommand, FString& OutArgs);

	/** Get system prompt */
	FString GetSystemPrompt() const;

	/** Get the currently selected blueprint from Content Browser */
	UBlueprint* GetSelectedBlueprint() const;

	/** Get a text summary of a blueprint's structure for AI context */
	FString GetBlueprintSummaryForAI(UBlueprint* Blueprint) const;

	/** Add a message to the chat */
	void AddMessage(EChatMessageRole Role, const FString& Content, bool bIsStreaming = false);

	/** Update the last message (for streaming) */
	void UpdateLastMessage(const FString& AdditionalContent);

	void EnrichBlueprintData(FBlueprintData& Data);
	void LogConversationMessages(const TArray<FChatMessage>& MessageList) const;
	void LogBlueprintDataSummary(const FBlueprintData& Data, const TCHAR* Source) const;
	void EnsureTickGraph(FBlueprintData& Data) const;
	static FString GetBlueprintSchemaHint();
	void SendRequestWithMCPFallback(const FString& ToolName);
	void SendModifyRequestWithMCPFallback();
	bool TryInvokeMCPTool(const TArray<FChatMessage>& Messages, const FString& ToolName, FString& OutResponseContent, FString& OutError) const;
	bool TryInvokeMCPValidateBlueprint(const FBlueprintData& Data, TArray<FString>& OutValidationErrors, FString& OutError) const;
	bool TryInvokeMCPLayoutBlueprint(const FBlueprintData& Data, FBlueprintData& OutLaidOutData, FString& OutError) const;

	/** Scroll to bottom of chat */
	void ScrollToBottom();

private:
	/** Chat message history */
	TArray<FChatDisplayMessage> Messages;

	/** Conversation context for AI (using TStrongObjectPtr to prevent GC) */
	TStrongObjectPtr<UAIConversationContext> ConversationContext;

	/** Message list container */
	TSharedPtr<SVerticalBox> MessageListBox;

	/** Scroll box for messages */
	TSharedPtr<SScrollBox> MessageScrollBox;

	/** Input text box */
	TSharedPtr<SMultiLineEditableTextBox> InputTextBox;

	/** Is a request currently in progress */
	bool bIsProcessing = false;

	/** Current streaming response */
	FString CurrentStreamingResponse;

	/** Quick command prefix */
	static const FString QuickCommandPrefix;

	/** Pending blueprint data from AI response, ready to be applied */
	TOptional<FBlueprintData> PendingBlueprintData;

	/** Blueprint currently being modified (weak pointer to avoid preventing GC) */
	TWeakObjectPtr<UBlueprint> BlueprintBeingModified;

	/** Whether we are in modify mode (vs create mode) */
	bool bIsModifyMode = false;

	/** Prevent infinite retries when AI returns unsupported legacy function format */
	bool bSchemaCorrectionRetried = false;

	/** Input history for arrow key navigation */
	TArray<FString> InputHistory;

	/** Current index in input history (-1 = not browsing, 0 = most recent) */
	int32 HistoryIndex = -1;

	/** Backup of current input when browsing history */
	FString CurrentInputBackup;

	/** Track if cursor was at document start on last up key */
	bool bWasAtDocumentStart = false;
};
