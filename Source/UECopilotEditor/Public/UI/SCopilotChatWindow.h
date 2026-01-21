// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "AI/OpenAICompatibleService.h"
#include "AI/ConversationContext.h"

class SMultiLineEditableTextBox;
class SScrollBox;
class SVerticalBox;

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
class UECOPILOTEDITOR_API SCopilotChatWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCopilotChatWindow) {}
	SLATE_END_ARGS()

	/** Construct this widget */
	void Construct(const FArguments& InArgs);

	/** Destructor */
	virtual ~SCopilotChatWindow();

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

	/** Add a message to the chat */
	void AddMessage(EChatMessageRole Role, const FString& Content, bool bIsStreaming = false);

	/** Update the last message (for streaming) */
	void UpdateLastMessage(const FString& AdditionalContent);

	/** Scroll to bottom of chat */
	void ScrollToBottom();

private:
	/** Chat message history */
	TArray<FChatDisplayMessage> Messages;

	/** Conversation context for AI */
	UPROPERTY()
	UAIConversationContext* ConversationContext = nullptr;

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
};
