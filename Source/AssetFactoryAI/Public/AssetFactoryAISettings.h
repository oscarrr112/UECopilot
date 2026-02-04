// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AssetFactoryAISettings.generated.h"

/**
 * AI Service Provider Type
 */
UENUM(BlueprintType)
enum class EAIServiceProvider : uint8
{
	Deepseek		UMETA(DisplayName = "Deepseek"),
	Ollama			UMETA(DisplayName = "Ollama (Local)"),
	GLM				UMETA(DisplayName = "GLM (Zhipu)"),
	OpenAI			UMETA(DisplayName = "OpenAI"),
	Custom			UMETA(DisplayName = "Custom OpenAI Compatible")
};

/**
 * UE Copilot Settings - Stored in Editor Preferences
 */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "Asset Factory AI"))
class ASSETFACTORYAI_API UAssetFactoryAISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAssetFactoryAISettings();

	/** Get singleton instance */
	static UAssetFactoryAISettings* Get();

	//~ Begin UDeveloperSettings Interface
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Asset Factory AI"); }
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
	//~ End UDeveloperSettings Interface

	/** Get the API endpoint URL based on current provider */
	FString GetEndpointURL() const;

	/** Get the API key for current provider */
	FString GetAPIKey() const;

	/** Get the model name for current provider */
	FString GetModelName() const;

public:
	/** The AI service provider to use */
	UPROPERTY(config, EditAnywhere, Category = "AI Service")
	EAIServiceProvider ServiceProvider = EAIServiceProvider::Deepseek;

	/** API Key for Deepseek */
	UPROPERTY(config, EditAnywhere, Category = "Deepseek", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::Deepseek", PasswordField = true))
	FString DeepseekAPIKey;

	/** Model name for Deepseek */
	UPROPERTY(config, EditAnywhere, Category = "Deepseek", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::Deepseek"))
	FString DeepseekModel = TEXT("deepseek-chat");

	/** Ollama server URL */
	UPROPERTY(config, EditAnywhere, Category = "Ollama", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::Ollama"))
	FString OllamaEndpoint = TEXT("http://localhost:11434/v1/chat/completions");

	/** Model name for Ollama */
	UPROPERTY(config, EditAnywhere, Category = "Ollama", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::Ollama"))
	FString OllamaModel = TEXT("llama3");

	/** API Key for GLM */
	UPROPERTY(config, EditAnywhere, Category = "GLM", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::GLM", PasswordField = true))
	FString GLMAPIKey;

	/** Model name for GLM */
	UPROPERTY(config, EditAnywhere, Category = "GLM", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::GLM"))
	FString GLMModel = TEXT("glm-4");

	/** API Key for OpenAI */
	UPROPERTY(config, EditAnywhere, Category = "OpenAI", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::OpenAI", PasswordField = true))
	FString OpenAIAPIKey;

	/** Model name for OpenAI */
	UPROPERTY(config, EditAnywhere, Category = "OpenAI", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::OpenAI"))
	FString OpenAIModel = TEXT("gpt-4");

	/** Custom endpoint URL */
	UPROPERTY(config, EditAnywhere, Category = "Custom", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::Custom"))
	FString CustomEndpoint;

	/** Custom API Key */
	UPROPERTY(config, EditAnywhere, Category = "Custom", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::Custom", PasswordField = true))
	FString CustomAPIKey;

	/** Custom Model name */
	UPROPERTY(config, EditAnywhere, Category = "Custom", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::Custom"))
	FString CustomModel;

	/** Maximum tokens for response */
	UPROPERTY(config, EditAnywhere, Category = "Generation", meta = (ClampMin = "256", ClampMax = "32768"))
	int32 MaxTokens = 4096;

	/** Temperature for response randomness */
	UPROPERTY(config, EditAnywhere, Category = "Generation", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Temperature = 0.7f;

	/** Enable streaming response */
	UPROPERTY(config, EditAnywhere, Category = "Generation")
	bool bEnableStreaming = true;

	/** Request timeout in seconds */
	UPROPERTY(config, EditAnywhere, Category = "Network", meta = (ClampMin = "10", ClampMax = "300"))
	int32 RequestTimeoutSeconds = 60;
};
