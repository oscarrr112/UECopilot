// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Graphs/AssetDocumentGraphTypes.h"

class UClass;
class UObject;

struct FAssetDocumentNodeAdapterContext
{
	UObject* Asset = nullptr;
	FString GraphPath;
};

struct FAssetDocumentUnsupportedNodeDiagnostic
{
	FString Code;
	FString Path;
	FString Class;
	FString Capability;
	TSharedPtr<FJsonObject> Member;
	FString Reason;
	FString SuggestedAction;

	TSharedRef<FJsonObject> ToJsonObject() const;
};

class IAssetDocumentNodeAdapter
{
public:
	virtual ~IAssetDocumentNodeAdapter() = default;

	virtual FString GetClassPath() const = 0;
	virtual FString GetCapability() const { return FString(); }
};

class FAssetDocumentNodeAdapterRegistry
{
public:
	void RegisterAdapter(const FString& ClassPath, TSharedRef<IAssetDocumentNodeAdapter> Adapter);
	void RegisterAdapter(UClass* Class, TSharedRef<IAssetDocumentNodeAdapter> Adapter);

	TSharedPtr<IAssetDocumentNodeAdapter> FindAdapter(const FString& ClassPath) const;
	TSharedPtr<IAssetDocumentNodeAdapter> FindAdapter(UClass* Class) const;

	void Reset();

private:
	TMap<FString, TSharedPtr<IAssetDocumentNodeAdapter>> AdaptersByClassPath;
};
