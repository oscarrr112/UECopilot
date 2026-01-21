// Copyright Epic Games, Inc. All Rights Reserved.

#include "UECopilotSettings.h"

#define LOCTEXT_NAMESPACE "UECopilotSettings"

UUECopilotSettings::UUECopilotSettings()
{
}

UUECopilotSettings* UUECopilotSettings::Get()
{
	return GetMutableDefault<UUECopilotSettings>();
}

FText UUECopilotSettings::GetSectionText() const
{
	return LOCTEXT("SectionText", "UE Copilot");
}

FText UUECopilotSettings::GetSectionDescription() const
{
	return LOCTEXT("SectionDescription", "Configure UE Copilot AI settings including API keys and service providers.");
}

FString UUECopilotSettings::GetEndpointURL() const
{
	switch (ServiceProvider)
	{
	case EAIServiceProvider::Deepseek:
		return TEXT("https://api.deepseek.com/v1/chat/completions");
	case EAIServiceProvider::Ollama:
		return OllamaEndpoint;
	case EAIServiceProvider::GLM:
		return TEXT("https://open.bigmodel.cn/api/paas/v4/chat/completions");
	case EAIServiceProvider::OpenAI:
		return TEXT("https://api.openai.com/v1/chat/completions");
	case EAIServiceProvider::Custom:
		return CustomEndpoint;
	default:
		return TEXT("");
	}
}

FString UUECopilotSettings::GetAPIKey() const
{
	switch (ServiceProvider)
	{
	case EAIServiceProvider::Deepseek:
		return DeepseekAPIKey;
	case EAIServiceProvider::Ollama:
		return TEXT(""); // Ollama typically doesn't require API key
	case EAIServiceProvider::GLM:
		return GLMAPIKey;
	case EAIServiceProvider::OpenAI:
		return OpenAIAPIKey;
	case EAIServiceProvider::Custom:
		return CustomAPIKey;
	default:
		return TEXT("");
	}
}

FString UUECopilotSettings::GetModelName() const
{
	switch (ServiceProvider)
	{
	case EAIServiceProvider::Deepseek:
		return DeepseekModel;
	case EAIServiceProvider::Ollama:
		return OllamaModel;
	case EAIServiceProvider::GLM:
		return GLMModel;
	case EAIServiceProvider::OpenAI:
		return OpenAIModel;
	case EAIServiceProvider::Custom:
		return CustomModel;
	default:
		return TEXT("");
	}
}

#undef LOCTEXT_NAMESPACE
