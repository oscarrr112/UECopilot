// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetFactoryAISettings.h"
#include "HAL/PlatformMisc.h"

#define LOCTEXT_NAMESPACE "AssetFactoryAISettings"

namespace
{
	struct FProviderDescriptor
	{
		FString DefaultEndpoint;
		FString UAssetFactoryAISettings::*ApiKeyMember = nullptr;
		FString UAssetFactoryAISettings::*ApiKeyEnvMember = nullptr;
		FString UAssetFactoryAISettings::*ModelMember = nullptr;
		bool bEndpointFromOllama = false;
		bool bEndpointFromCustom = false;
		bool bNoApiKey = false;
	};

	const FProviderDescriptor& GetProviderDescriptor(EAIServiceProvider Provider)
	{
		static const FProviderDescriptor DeepseekDesc{
			TEXT("https://api.deepseek.com/v1/chat/completions"),
			&UAssetFactoryAISettings::DeepseekAPIKey,
			&UAssetFactoryAISettings::DeepseekAPIKeyEnvVar,
			&UAssetFactoryAISettings::DeepseekModel,
			false,
			false,
			false
		};
		static const FProviderDescriptor OllamaDesc{
			TEXT(""),
			nullptr,
			nullptr,
			&UAssetFactoryAISettings::OllamaModel,
			true,
			false,
			true
		};
		static const FProviderDescriptor GlmDesc{
			TEXT("https://open.bigmodel.cn/api/paas/v4/chat/completions"),
			&UAssetFactoryAISettings::GLMAPIKey,
			&UAssetFactoryAISettings::GLMAPIKeyEnvVar,
			&UAssetFactoryAISettings::GLMModel,
			false,
			false,
			false
		};
		static const FProviderDescriptor OpenAIDesc{
			TEXT("https://api.openai.com/v1/chat/completions"),
			&UAssetFactoryAISettings::OpenAIAPIKey,
			&UAssetFactoryAISettings::OpenAIAPIKeyEnvVar,
			&UAssetFactoryAISettings::OpenAIModel,
			false,
			false,
			false
		};
		static const FProviderDescriptor CustomDesc{
			TEXT(""),
			&UAssetFactoryAISettings::CustomAPIKey,
			&UAssetFactoryAISettings::CustomAPIKeyEnvVar,
			&UAssetFactoryAISettings::CustomModel,
			false,
			true,
			false
		};
		static const FProviderDescriptor UnknownDesc{};

		switch (Provider)
		{
		case EAIServiceProvider::Deepseek: return DeepseekDesc;
		case EAIServiceProvider::Ollama: return OllamaDesc;
		case EAIServiceProvider::GLM: return GlmDesc;
		case EAIServiceProvider::OpenAI: return OpenAIDesc;
		case EAIServiceProvider::Custom: return CustomDesc;
		default: return UnknownDesc;
		}
	}
}

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
	const FProviderDescriptor& Desc = GetProviderDescriptor(ServiceProvider);
	if (Desc.bEndpointFromOllama)
	{
		return OllamaEndpoint;
	}
	if (Desc.bEndpointFromCustom)
	{
		return CustomEndpoint;
	}
	return Desc.DefaultEndpoint;
}

FString UAssetFactoryAISettings::GetAPIKey() const
{
	FString EnvApiKey;
	if (bPreferEnvironmentApiKey && TryGetEnvironmentAPIKey(EnvApiKey))
	{
		return EnvApiKey;
	}

	const FProviderDescriptor& Desc = GetProviderDescriptor(ServiceProvider);
	if (Desc.bNoApiKey || Desc.ApiKeyMember == nullptr)
	{
		return TEXT("");
	}

	return this->*(Desc.ApiKeyMember);
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

	const FProviderDescriptor& Desc = GetProviderDescriptor(ServiceProvider);
	if (Desc.ApiKeyEnvMember && TrySetFrom(this->*(Desc.ApiKeyEnvMember)))
	{
		return true;
	}

	return TrySetFrom(GenericAPIKeyEnvVar);
}

FString UAssetFactoryAISettings::GetModelName() const
{
	const FProviderDescriptor& Desc = GetProviderDescriptor(ServiceProvider);
	if (Desc.ModelMember == nullptr)
	{
		return TEXT("");
	}

	return this->*(Desc.ModelMember);
}

#undef LOCTEXT_NAMESPACE
