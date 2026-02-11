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
	virtual FName GetContainerName() const override { return FName("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Asset Factory AI"); }
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
	//~ End UDeveloperSettings Interface

	/** Get the API endpoint URL based on current provider */
	FString GetEndpointURL() const;

	/** Get the API key for current provider */
	FString GetAPIKey() const;

	/** Try read API key from environment variables based on provider/generic fallback */
	bool TryGetEnvironmentAPIKey(FString& OutApiKey) const;

	/** Get the model name for current provider */
	FString GetModelName() const;

public:
	/** The AI service provider to use */
	UPROPERTY(config, EditAnywhere, Category = "AI Service")
	EAIServiceProvider ServiceProvider = EAIServiceProvider::Deepseek;

	/** API Key for Deepseek */
	UPROPERTY(config, EditAnywhere, Category = "Deepseek", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::Deepseek", PasswordField = true))
	FString DeepseekAPIKey;

	/** Environment variable name for Deepseek API key */
	UPROPERTY(config, EditAnywhere, Category = "Deepseek", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::Deepseek"))
	FString DeepseekAPIKeyEnvVar = TEXT("DEEPSEEK_API_KEY");

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

	/** Environment variable name for GLM API key */
	UPROPERTY(config, EditAnywhere, Category = "GLM", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::GLM"))
	FString GLMAPIKeyEnvVar = TEXT("GLM_API_KEY");

	/** Model name for GLM */
	UPROPERTY(config, EditAnywhere, Category = "GLM", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::GLM"))
	FString GLMModel = TEXT("glm-4");

	/** API Key for OpenAI */
	UPROPERTY(config, EditAnywhere, Category = "OpenAI", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::OpenAI", PasswordField = true))
	FString OpenAIAPIKey;

	/** Environment variable name for OpenAI API key */
	UPROPERTY(config, EditAnywhere, Category = "OpenAI", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::OpenAI"))
	FString OpenAIAPIKeyEnvVar = TEXT("OPENAI_API_KEY");

	/** Model name for OpenAI */
	UPROPERTY(config, EditAnywhere, Category = "OpenAI", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::OpenAI"))
	FString OpenAIModel = TEXT("gpt-4");

	/** Custom endpoint URL */
	UPROPERTY(config, EditAnywhere, Category = "Custom", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::Custom"))
	FString CustomEndpoint;

	/** Custom API Key */
	UPROPERTY(config, EditAnywhere, Category = "Custom", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::Custom", PasswordField = true))
	FString CustomAPIKey;

	/** Environment variable name for Custom provider API key */
	UPROPERTY(config, EditAnywhere, Category = "Custom", meta = (EditCondition = "ServiceProvider == EAIServiceProvider::Custom"))
	FString CustomAPIKeyEnvVar = TEXT("ASSETFACTORY_CUSTOM_API_KEY");

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

	/** Enable MCP-first flow for /modify requests */
	UPROPERTY(config, EditAnywhere, Category = "MCP")
	bool bEnableMCPForModify = true;

	/** Enable MCP-first flow for /generate, /explain and non-command chat (non-streaming mode). */
	UPROPERTY(config, EditAnywhere, Category = "MCP")
	bool bEnableMCPForChatRequests = true;

	/** Force MCP-only for chat/modify flows. If enabled, direct HTTP requests are blocked unless fallback is also enabled. */
	UPROPERTY(config, EditAnywhere, Category = "MCP")
	bool bEnableMCPOnlyMode = true;

	/** Fallback to direct HTTP AI service when MCP fails */
	UPROPERTY(config, EditAnywhere, Category = "MCP")
	bool bMCPFallbackToDirectAI = false;

	/** Allow direct HTTP AI calls for debugging only. Keep disabled in MCP-only production mode. */
	UPROPERTY(config, EditAnywhere, Category = "MCP")
	bool bAllowDirectAIHttpForDebug = false;

	/** Command used to launch MCP sidecar (for example: python) */
	UPROPERTY(config, EditAnywhere, Category = "MCP")
	FString MCPServerCommand = TEXT("py");

	/** Optional absolute script path. If empty, plugin default MCP script will be used. */
	UPROPERTY(config, EditAnywhere, Category = "MCP")
	FString MCPServerScriptPath;

	/** MCP sidecar timeout in seconds */
	UPROPERTY(config, EditAnywhere, Category = "MCP", meta = (ClampMin = "5", ClampMax = "300"))
	int32 MCPTimeoutSeconds = 90;

	/** Number of retry attempts for transient MCP upstream HTTP failures (429/5xx/timeout). */
	UPROPERTY(config, EditAnywhere, Category = "MCP", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MCPNetworkRetries = 2;

	/** Prefer API keys from environment variables over config values. */
	UPROPERTY(config, EditAnywhere, Category = "Security")
	bool bPreferEnvironmentApiKey = true;

	/** Generic environment variable fallback for API key when provider-specific key is unavailable. */
	UPROPERTY(config, EditAnywhere, Category = "Security")
	FString GenericAPIKeyEnvVar = TEXT("ASSETFACTORY_API_KEY");

	/** Allow writing API key into MCP request temp JSON file; disable for stricter secret handling when env key is available. */
	UPROPERTY(config, EditAnywhere, Category = "Security")
	bool bWriteApiKeyToMCPRequestFile = false;
};
