// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetGenerator.h"

/**
 * Registry for asset generators
 * Manages registration and lookup of generators by asset type
 */
class ASSETFACTORY_API FAssetGeneratorRegistry
{
public:
	/** Get singleton instance */
	static FAssetGeneratorRegistry& Get();

	/** Register a generator */
	void RegisterGenerator(TSharedRef<IAssetGenerator> Generator);

	/** Unregister a generator by asset type */
	void UnregisterGenerator(const FString& AssetType);

	/** Find generator for asset type */
	IAssetGenerator* FindGenerator(const FString& AssetType) const;

	/** Get all registered asset types */
	TArray<FString> GetRegisteredTypes() const;

	/** Get all generators sorted by priority (lower first) */
	TArray<TSharedRef<IAssetGenerator>> GetGeneratorsSortedByPriority() const;

	/** Clear all registered generators */
	void Clear();

private:
	FAssetGeneratorRegistry() = default;
	~FAssetGeneratorRegistry() = default;

	// Non-copyable
	FAssetGeneratorRegistry(const FAssetGeneratorRegistry&) = delete;
	FAssetGeneratorRegistry& operator=(const FAssetGeneratorRegistry&) = delete;

	TMap<FString, TSharedRef<IAssetGenerator>> Generators;
};
