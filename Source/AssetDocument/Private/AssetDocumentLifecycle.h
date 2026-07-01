// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

enum class EAssetDocumentLifecycleAction : uint8
{
	Create,
	Update,
	CreateOrUpdate
};

struct FAssetDocumentLifecycleResult
{
	UObject* Asset = nullptr;
	bool bCreated = false;
	FString ObjectPath;
	FString Error;
};

class FAssetDocumentLifecycle
{
public:
	static bool TryParseAction(const FString& ActionName, EAssetDocumentLifecycleAction& OutAction, FString& OutError);
	static FAssetDocumentLifecycleResult CreateOrLoad(const FString& Target, UClass* Class, EAssetDocumentLifecycleAction Action, TSharedPtr<FJsonObject> Document = nullptr);
	static void CleanupCreatedAsset(const FAssetDocumentLifecycleResult& LifecycleResult);

private:
	static FString MakeObjectPath(const FString& Target);
	static bool TryResolveBlueprintParentClass(const TSharedPtr<FJsonObject>& Document, UClass*& OutParentClass, FString& OutError);
	static bool TryResolveAnimBlueprintParentClass(const TSharedPtr<FJsonObject>& Document, UClass*& OutParentClass, FString& OutError);
	static bool TryResolveWidgetBlueprintParentClass(const TSharedPtr<FJsonObject>& Document, UClass*& OutParentClass, FString& OutError);
	static FAssetDocumentLifecycleResult CreateAnimBlueprintAsset(const FString& Target, UPackage* Package, const FString& AssetName, const TSharedPtr<FJsonObject>& Document);
	static FAssetDocumentLifecycleResult CreateBlueprintAsset(const FString& Target, UPackage* Package, const FString& AssetName, const TSharedPtr<FJsonObject>& Document);
	static FAssetDocumentLifecycleResult CreateWidgetBlueprintAsset(const FString& Target, UPackage* Package, const FString& AssetName, const TSharedPtr<FJsonObject>& Document);
};
