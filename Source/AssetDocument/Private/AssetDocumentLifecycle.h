// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

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
	static FAssetDocumentLifecycleResult CreateOrLoad(const FString& Target, UClass* Class, EAssetDocumentLifecycleAction Action);

private:
	static FString MakeObjectPath(const FString& Target);
};
