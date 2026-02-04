// Copyright ProjectRPG. All Rights Reserved.

#include "AssetGeneratorRegistry.h"
#include "AssetFactoryModule.h"

FAssetGeneratorRegistry& FAssetGeneratorRegistry::Get()
{
	static FAssetGeneratorRegistry Instance;
	return Instance;
}

void FAssetGeneratorRegistry::RegisterGenerator(TSharedRef<IAssetGenerator> Generator)
{
	const FString AssetType = Generator->GetAssetType();

	if (Generators.Contains(AssetType))
	{
		UE_LOG(LogAssetFactory, Warning, TEXT("Generator for '%s' already registered, replacing"), *AssetType);
	}

	Generators.Add(AssetType, Generator);
	UE_LOG(LogAssetFactory, Log, TEXT("Registered generator for '%s' (priority: %d)"), *AssetType, Generator->GetPriority());
}

void FAssetGeneratorRegistry::UnregisterGenerator(const FString& AssetType)
{
	if (Generators.Remove(AssetType) > 0)
	{
		UE_LOG(LogAssetFactory, Log, TEXT("Unregistered generator for '%s'"), *AssetType);
	}
}

IAssetGenerator* FAssetGeneratorRegistry::FindGenerator(const FString& AssetType) const
{
	const TSharedRef<IAssetGenerator>* Found = Generators.Find(AssetType);
	return Found ? &Found->Get() : nullptr;
}

TArray<FString> FAssetGeneratorRegistry::GetRegisteredTypes() const
{
	TArray<FString> Types;
	Generators.GetKeys(Types);
	return Types;
}

TArray<TSharedRef<IAssetGenerator>> FAssetGeneratorRegistry::GetGeneratorsSortedByPriority() const
{
	TArray<TSharedRef<IAssetGenerator>> SortedGenerators;

	for (const auto& Pair : Generators)
	{
		SortedGenerators.Add(Pair.Value);
	}

	SortedGenerators.Sort([](const TSharedRef<IAssetGenerator>& A, const TSharedRef<IAssetGenerator>& B)
	{
		return A->GetPriority() < B->GetPriority();
	});

	return SortedGenerators;
}

void FAssetGeneratorRegistry::Clear()
{
	Generators.Empty();
	UE_LOG(LogAssetFactory, Log, TEXT("Cleared all generators"));
}
