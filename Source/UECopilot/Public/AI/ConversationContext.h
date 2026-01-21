// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AI/OpenAICompatibleService.h"
#include "ConversationContext.generated.h"

/**
 * Conversation Context - Manages chat history and context for AI interactions
 */
UCLASS(BlueprintType)
class UECOPILOT_API UAIConversationContext : public UObject
{
	GENERATED_BODY()

public:
	UAIConversationContext();

	/** Create a new conversation context */
	static UAIConversationContext* Create(UObject* Outer, const FString& SystemPrompt = TEXT(""));

	/** Clear all messages except system prompt */
	UFUNCTION(BlueprintCallable, Category = "Conversation")
	void ClearHistory();

	/** Add a user message */
	UFUNCTION(BlueprintCallable, Category = "Conversation")
	void AddUserMessage(const FString& Content);

	/** Add an assistant message */
	UFUNCTION(BlueprintCallable, Category = "Conversation")
	void AddAssistantMessage(const FString& Content);

	/** Set or update the system prompt */
	UFUNCTION(BlueprintCallable, Category = "Conversation")
	void SetSystemPrompt(const FString& Prompt);

	/** Get the system prompt */
	UFUNCTION(BlueprintPure, Category = "Conversation")
	FString GetSystemPrompt() const;

	/** Get all messages for API request */
	UFUNCTION(BlueprintCallable, Category = "Conversation")
	TArray<FChatMessage> GetMessages() const;

	/** Get message count (excluding system prompt) */
	UFUNCTION(BlueprintPure, Category = "Conversation")
	int32 GetMessageCount() const;

	/** Inject project context (existing blueprints, classes, etc.) */
	UFUNCTION(BlueprintCallable, Category = "Conversation")
	void InjectProjectContext(const FString& Context);

	/** Get the injected project context */
	UFUNCTION(BlueprintPure, Category = "Conversation")
	FString GetProjectContext() const { return ProjectContext; }

	/** Set maximum history length (0 = unlimited) */
	UFUNCTION(BlueprintCallable, Category = "Conversation")
	void SetMaxHistoryLength(int32 MaxLength);

	/** Remove oldest messages to fit within max length */
	void TrimHistory();

private:
	/** System prompt */
	UPROPERTY()
	FString SystemPrompt;

	/** Project context to inject */
	UPROPERTY()
	FString ProjectContext;

	/** Conversation history (excluding system prompt) */
	UPROPERTY()
	TArray<FChatMessage> History;

	/** Maximum number of messages to keep (0 = unlimited) */
	UPROPERTY()
	int32 MaxHistoryLength = 20;
};

/**
 * Project Context Collector - Extracts context from the UE project
 */
UCLASS()
class UECOPILOT_API UProjectContextCollector : public UObject
{
	GENERATED_BODY()

public:
	/** Collect context from current project */
	static FString CollectContext();

	/** Get list of all blueprint classes in project */
	static TArray<FString> GetProjectBlueprints();

	/** Get class hierarchy for a given class */
	static FString GetClassHierarchy(UClass* Class);

	/** Get function signatures for a class */
	static TArray<FString> GetClassFunctions(UClass* Class);

	/** Get variable info for a blueprint */
	static FString GetBlueprintVariables(UBlueprint* Blueprint);

	/** Get summary of a specific blueprint */
	static FString GetBlueprintSummary(UBlueprint* Blueprint);
};
