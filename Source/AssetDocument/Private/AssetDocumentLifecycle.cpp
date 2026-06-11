// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentLifecycle.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"

bool FAssetDocumentLifecycle::TryParseAction(const FString& ActionName, EAssetDocumentLifecycleAction& OutAction, FString& OutError)
{
	if (ActionName == TEXT("Create"))
	{
		OutAction = EAssetDocumentLifecycleAction::Create;
		return true;
	}

	if (ActionName == TEXT("Update"))
	{
		OutAction = EAssetDocumentLifecycleAction::Update;
		return true;
	}

	if (ActionName == TEXT("CreateOrUpdate"))
	{
		OutAction = EAssetDocumentLifecycleAction::CreateOrUpdate;
		return true;
	}

	OutError = FString::Printf(TEXT("Unsupported Action '%s'"), *ActionName);
	return false;
}

FAssetDocumentLifecycleResult FAssetDocumentLifecycle::CreateOrLoad(const FString& Target, UClass* Class, EAssetDocumentLifecycleAction Action)
{
	FAssetDocumentLifecycleResult Result;
	Result.ObjectPath = MakeObjectPath(Target);

	UObject* ExistingAsset = FindObject<UObject>(nullptr, *Result.ObjectPath);
	const FString PackageFileName = FPackageName::LongPackageNameToFilename(Target, FPackageName::GetAssetPackageExtension());
	if (!ExistingAsset && IFileManager::Get().FileExists(*PackageFileName))
	{
		ExistingAsset = LoadObject<UObject>(nullptr, *Result.ObjectPath);
	}

	if (ExistingAsset && !ExistingAsset->IsA(Class))
	{
		Result.Error = FString::Printf(TEXT("Existing asset '%s' is not a '%s'"), *Result.ObjectPath, *Class->GetName());
		return Result;
	}

	if (Action == EAssetDocumentLifecycleAction::Create && ExistingAsset)
	{
		Result.Error = FString::Printf(TEXT("Create failed because target '%s' already exists"), *Target);
		return Result;
	}

	if (Action == EAssetDocumentLifecycleAction::Update && !ExistingAsset)
	{
		Result.Error = FString::Printf(TEXT("Update failed because target '%s' does not exist"), *Target);
		return Result;
	}

	if (ExistingAsset)
	{
		Result.Asset = ExistingAsset;
		return Result;
	}

	const FString AssetName = FPackageName::GetLongPackageAssetName(Target);
	if (AssetName.IsEmpty())
	{
		Result.Error = FString::Printf(TEXT("Invalid target '%s'"), *Target);
		return Result;
	}

	UPackage* Package = CreatePackage(*Target);
	if (!Package)
	{
		Result.Error = FString::Printf(TEXT("Failed to create package '%s'"), *Target);
		return Result;
	}

	UObject* NewAsset = NewObject<UObject>(Package, Class, *AssetName, RF_Public | RF_Standalone);
	if (!NewAsset)
	{
		Result.Error = FString::Printf(TEXT("Failed to create asset '%s'"), *Result.ObjectPath);
		return Result;
	}

	FAssetRegistryModule::AssetCreated(NewAsset);

	Result.Asset = NewAsset;
	Result.bCreated = true;
	return Result;
}

FString FAssetDocumentLifecycle::MakeObjectPath(const FString& Target)
{
	return FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
}
