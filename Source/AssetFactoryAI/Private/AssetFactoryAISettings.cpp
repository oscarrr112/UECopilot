// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetFactoryAISettings.h"
#include "HAL/PlatformMisc.h"

#define LOCTEXT_NAMESPACE "AssetFactoryAISettings"

UAssetFactoryAISettings::UAssetFactoryAISettings()
{
}

UAssetFactoryAISettings* UAssetFactoryAISettings::Get()
{
	return GetMutableDefault<UAssetFactoryAISettings>();
}

FText UAssetFactoryAISettings::GetSectionText() const
{
	return LOCTEXT("SectionText", "Asset Factory AI");
}

FText UAssetFactoryAISettings::GetSectionDescription() const
{
	return LOCTEXT("SectionDescription", "Configure UE Copilot AI settings including API keys and service providers.");
}

FString UAssetFactoryAISettings::GetEndpointURL() const
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

FString UAssetFactoryAISettings::GetAPIKey() const
{
	FString EnvApiKey;
	if (bPreferEnvironmentApiKey && TryGetEnvironmentAPIKey(EnvApiKey))
	{
		return EnvApiKey;
	}

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

bool UAssetFactoryAISettings::TryGetEnvironmentAPIKey(FString& OutApiKey) const
{
	OutApiKey.Empty();

	auto ReadEnv = [](const FString& Name) -> FString
	{
		if (Name.IsEmpty())
		{
			return FString();
		}
		return FPlatformMisc::GetEnvironmentVariable(*Name).TrimStartAndEnd();
	};

	auto TrySetFrom = [&](const FString& EnvName) -> bool
	{
		const FString Value = ReadEnv(EnvName);
		if (!Value.IsEmpty())
		{
			OutApiKey = Value;
			return true;
		}
		return false;
	};

	switch (ServiceProvider)
	{
	case EAIServiceProvider::Deepseek:
		if (TrySetFrom(DeepseekAPIKeyEnvVar))
		{
			return true;
		}
		break;
	case EAIServiceProvider::GLM:
		if (TrySetFrom(GLMAPIKeyEnvVar))
		{
			return true;
		}
		break;
	case EAIServiceProvider::OpenAI:
		if (TrySetFrom(OpenAIAPIKeyEnvVar))
		{
			return true;
		}
		break;
	case EAIServiceProvider::Custom:
		if (TrySetFrom(CustomAPIKeyEnvVar))
		{
			return true;
		}
		break;
	default:
		break;
	}

	return TrySetFrom(GenericAPIKeyEnvVar);
}

FString UAssetFactoryAISettings::GetModelName() const
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
