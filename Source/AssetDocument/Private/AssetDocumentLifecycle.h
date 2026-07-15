// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentTypes.h"
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class FJsonObject;
class UPackage;

enum class EAssetDocumentLifecycleAction : uint8
{
	Create,
	Update,
	CreateOrUpdate
};

struct FAssetDocumentLifecycleResult
{
	UObject* Asset = nullptr;
	UPackage* Package = nullptr;
	bool bCreated = false;
	bool bOwnsPackage = false;
	bool bRegistryAnnounced = false;
	bool bPackageWasDirty = false;
	bool bPackageObjectBaselineCaptured = false;
	TArray<TWeakObjectPtr<UObject>> PackageObjectsBeforeCreation;
	TArray<TStrongObjectPtr<UObject>> PackageObjectBaselineGuards;
	TArray<TWeakObjectPtr<UObject>> CreatedObjects;
	TArray<FAssetDocumentDiagnostic> Diagnostics;
	FString ObjectPath;
	FString Error;
};

class FAssetDocumentLifecycle
{
public:
	static bool TryParseAction(const FString& ActionName, EAssetDocumentLifecycleAction& OutAction, FString& OutError);
	static FAssetDocumentLifecycleResult Resolve(const FString& Target, UClass* Class, EAssetDocumentLifecycleAction Action);
	static bool ValidateCreateDocument(UClass* Class, const TSharedPtr<FJsonObject>& Document, FString& OutError);
	static FAssetDocumentLifecycleResult CreateTransientPreview(const FString& Target, UClass* Class, const TSharedPtr<FJsonObject>& Document);
	static FAssetDocumentLifecycleResult CreateOrLoad(const FString& Target, UClass* Class, EAssetDocumentLifecycleAction Action, TSharedPtr<FJsonObject> Document = nullptr);
	static bool HasCleanupWork(const FAssetDocumentLifecycleResult& LifecycleResult);
	static void CleanupCreatedAsset(
		const FAssetDocumentLifecycleResult& LifecycleResult,
		TArray<FAssetDocumentDiagnostic>& OutDiagnostics);
	static void CleanupCreatedAsset(const FAssetDocumentLifecycleResult& LifecycleResult);
	static void DiscardTransientPreview(
		const FAssetDocumentLifecycleResult& PreviewResult,
		TArray<FAssetDocumentDiagnostic>& OutDiagnostics);
	static void DiscardTransientPreview(const FAssetDocumentLifecycleResult& PreviewResult);

private:
	static FString MakeObjectPath(const FString& Target);
	static bool TryResolveBlueprintParentClass(const TSharedPtr<FJsonObject>& Document, UClass*& OutParentClass, FString& OutError);
	static bool TryResolveAnimBlueprintParentClass(const TSharedPtr<FJsonObject>& Document, UClass*& OutParentClass, FString& OutError);
	static bool TryResolveWidgetBlueprintParentClass(const TSharedPtr<FJsonObject>& Document, UClass*& OutParentClass, FString& OutError);
	static FAssetDocumentLifecycleResult CreateAnimBlueprintAsset(const FString& Target, UPackage* Package, const FString& AssetName, const TSharedPtr<FJsonObject>& Document, bool bAnnounceToRegistry, bool bIsTransientPreview);
	static FAssetDocumentLifecycleResult CreateBehaviorTreeAsset(const FString& Target, UPackage* Package, const FString& AssetName, const TSharedPtr<FJsonObject>& Document, bool bAnnounceToRegistry);
	static FAssetDocumentLifecycleResult CreateBlackboardDataAsset(const FString& Target, UPackage* Package, const FString& AssetName, const TSharedPtr<FJsonObject>& Document, bool bAnnounceToRegistry);
	static FAssetDocumentLifecycleResult CreateBlueprintAsset(const FString& Target, UPackage* Package, const FString& AssetName, const TSharedPtr<FJsonObject>& Document, bool bAnnounceToRegistry);
	static FAssetDocumentLifecycleResult CreateWidgetBlueprintAsset(const FString& Target, UPackage* Package, const FString& AssetName, const TSharedPtr<FJsonObject>& Document, bool bAnnounceToRegistry, bool bIsTransientPreview);
};
