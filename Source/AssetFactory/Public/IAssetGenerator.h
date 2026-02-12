// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenerationTypes.h"
#include "Dom/JsonObject.h"

/**
 * Interface for asset generators
 * Each generator handles a specific asset type (DataAsset, InputAction, etc.)
 */
class ASSETFACTORY_API IAssetGenerator
{
public:
	virtual ~IAssetGenerator() = default;

	/**
	 * Get the asset type this generator handles (e.g., "DataAsset", "InputAction")
	 */
	virtual FString GetAssetType() const = 0;

	/**
	 * Generate an asset from JSON configuration
	 * @param Name - Asset name
	 * @param Path - Content path (e.g., "/Game/Data")
	 * @param Action - Create, Update, or CreateOrUpdate
	 * @param Config - JSON configuration object containing type-specific fields
	 * @return Generation result with status and created asset
	 */
	virtual FGenerationResult Generate(
		const FString& Name,
		const FString& Path,
		EGenerationAction Action,
		TSharedPtr<FJsonObject> Config
	) = 0;

	/**
	 * Get the priority of this generator (lower = processed first)
	 * Used for dependency ordering (e.g., InputAction before InputMappingContext)
	 */
	virtual int32 GetPriority() const { return 100; }

	/**
	 * Check if this generator can handle the given configuration
	 */
	virtual bool CanGenerate(TSharedPtr<FJsonObject> Config) const
	{
		return Config.IsValid();
	}

	/**
	 * Validate the configuration before generation.
	 * Called by AssetFactorySubsystem before Generate().
	 * @param Config - JSON configuration to validate
	 * @param Action - The generation action (Create, Update, CreateOrUpdate)
	 * @return Empty optional if valid, error message string if invalid
	 */
	virtual TOptional<FString> ValidateConfig(TSharedPtr<FJsonObject> Config, EGenerationAction Action = EGenerationAction::Create) const
	{
		return TOptional<FString>();  // Default: no validation, always passes
	}

	/**
	 * Get the list of required fields for this generator.
	 * Used for documentation generation and validation hints.
	 * @return Array of required field names
	 */
	virtual TArray<FString> GetRequiredFields() const
	{
		return {};
	}

	/**
	 * Extract asset configuration as JSON (reverse of Generate)
	 * @param Asset - The asset to extract configuration from
	 * @param bDiffOnly - If true, only extract values different from defaults
	 * @return JSON configuration that can be used with Generate(), or nullptr if extraction failed
	 */
	virtual TSharedPtr<FJsonObject> Extract(UObject* Asset, bool bDiffOnly = false) const
	{
		return nullptr;  // Default: not implemented
	}

	/**
	 * Check if this generator can extract from the given asset
	 */
	virtual bool CanExtract(UObject* Asset) const
	{
		return false;  // Default: not supported
	}

protected:
	/**
	 * Helper: Check if asset already exists
	 */
	bool DoesAssetExist(const FString& Path, const FString& Name) const
	{
		FString FullPath = Path / Name;
		if (!FullPath.StartsWith(TEXT("/")))
		{
			FullPath = TEXT("/") + FullPath;
		}
		return FPackageName::DoesPackageExist(FullPath);
	}

	/**
	 * Helper: Load existing asset
	 */
	UObject* LoadExistingAsset(const FString& Path, const FString& Name) const
	{
		FString FullPath = Path / Name;
		if (!FullPath.StartsWith(TEXT("/")))
		{
			FullPath = TEXT("/") + FullPath;
		}
		return LoadObject<UObject>(nullptr, *FullPath);
	}

	/**
	 * Helper: Get string field from JSON with default value
	 */
	FString GetStringField(TSharedPtr<FJsonObject> Config, const FString& FieldName, const FString& DefaultValue = TEXT("")) const
	{
		FString Value;
		if (Config.IsValid() && Config->TryGetStringField(FieldName, Value))
		{
			return Value;
		}
		return DefaultValue;
	}

	/**
	 * Helper: Get array field from JSON
	 */
	const TArray<TSharedPtr<FJsonValue>>* GetArrayField(TSharedPtr<FJsonObject> Config, const FString& FieldName) const
	{
		if (Config.IsValid())
		{
			return Config->Values.Find(FieldName) ? &Config->GetArrayField(FieldName) : nullptr;
		}
		return nullptr;
	}

	/**
	 * Helper: Get object field from JSON (returns nullptr if field doesn't exist or isn't an object)
	 */
	TSharedPtr<FJsonObject> GetObjectField(TSharedPtr<FJsonObject> Config, const FString& FieldName) const
	{
		if (Config.IsValid() && Config->HasTypedField<EJson::Object>(FieldName))
		{
			return Config->GetObjectField(FieldName);
		}
		return nullptr;
	}
};
