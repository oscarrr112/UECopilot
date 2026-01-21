// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "OpenAICompatibleService.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAIService, Log, All);

/**
 * Chat message role
 */
UENUM(BlueprintType)
enum class EChatMessageRole : uint8
{
	System		UMETA(DisplayName = "System"),
	User		UMETA(DisplayName = "User"),
	Assistant	UMETA(DisplayName = "Assistant")
};

/**
 * Single chat message
 */
USTRUCT(BlueprintType)
struct UECOPILOT_API FChatMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Chat")
	EChatMessageRole Role = EChatMessageRole::User;

	UPROPERTY(BlueprintReadWrite, Category = "Chat")
	FString Content;

	FChatMessage() = default;
	FChatMessage(EChatMessageRole InRole, const FString& InContent)
		: Role(InRole), Content(InContent) {}
};

/**
 * AI Response data
 */
USTRUCT(BlueprintType)
struct UECOPILOT_API FAIResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Response")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Response")
	FString Content;

	UPROPERTY(BlueprintReadOnly, Category = "Response")
	FString ErrorMessage;

	UPROPERTY(BlueprintReadOnly, Category = "Response")
	int32 PromptTokens = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Response")
	int32 CompletionTokens = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Response")
	int32 TotalTokens = 0;
};

// Delegate declarations
DECLARE_DELEGATE_OneParam(FOnAIResponseReceived, const FAIResponse& /*Response*/);
DECLARE_DELEGATE_OneParam(FOnAIStreamChunk, const FString& /*Chunk*/);
DECLARE_DELEGATE(FOnAIStreamComplete);
DECLARE_DELEGATE_OneParam(FOnAIError, const FString& /*ErrorMessage*/);

/**
 * OpenAI Compatible Service - Unified interface for AI services
 * Supports Deepseek, Ollama, GLM, OpenAI, and custom endpoints
 */
UCLASS()
class UECOPILOT_API UOpenAICompatibleService : public UObject
{
	GENERATED_BODY()

public:
	UOpenAICompatibleService();

	/** Get singleton instance */
	static UOpenAICompatibleService* Get();

	/**
	 * Send a chat completion request
	 * @param Messages - Array of chat messages
	 * @param OnComplete - Callback when response is received
	 */
	void SendChatRequest(const TArray<FChatMessage>& Messages, FOnAIResponseReceived OnComplete);

	/**
	 * Send a chat completion request with streaming
	 * @param Messages - Array of chat messages
	 * @param OnChunk - Callback for each streamed chunk
	 * @param OnComplete - Callback when streaming completes
	 * @param OnError - Callback on error
	 */
	void SendChatRequestStreaming(
		const TArray<FChatMessage>& Messages,
		FOnAIStreamChunk OnChunk,
		FOnAIStreamComplete OnComplete,
		FOnAIError OnError);

	/**
	 * Cancel any pending request
	 */
	void CancelRequest();

	/**
	 * Check if a request is in progress
	 */
	bool IsRequestInProgress() const { return bRequestInProgress; }

private:
	/** Build request JSON body */
	FString BuildRequestBody(const TArray<FChatMessage>& Messages, bool bStream) const;

	/** Parse non-streaming response */
	FAIResponse ParseResponse(const FString& ResponseContent) const;

	/** Parse streaming chunk */
	FString ParseStreamChunk(const FString& ChunkData) const;

	/** HTTP request complete handler */
	void OnHttpRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess);

private:
	/** Current pending request */
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> CurrentRequest;

	/** Response callback */
	FOnAIResponseReceived ResponseCallback;

	/** Streaming callbacks */
	FOnAIStreamChunk StreamChunkCallback;
	FOnAIStreamComplete StreamCompleteCallback;
	FOnAIError StreamErrorCallback;

	/** Request in progress flag */
	bool bRequestInProgress = false;

	/** Accumulated streaming response */
	FString AccumulatedResponse;

	/** Current request ID for tracking */
	int32 CurrentRequestId = 0;

	/** Request start time for measuring duration */
	double RequestStartTime = 0.0;

	/** Singleton instance */
	static UOpenAICompatibleService* Instance;
};
