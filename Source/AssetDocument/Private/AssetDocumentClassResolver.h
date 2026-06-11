// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UClass;

class FAssetDocumentClassResolver
{
public:
	static bool ResolveClass(const FString& ClassName, UClass*& OutClass, FString& OutError);

private:
	static bool ValidateResolvedClass(UClass* Class, FString& OutError);
};
