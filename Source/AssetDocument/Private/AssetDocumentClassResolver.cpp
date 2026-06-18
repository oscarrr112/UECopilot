// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentClassResolver.h"

#include "Utils/ClassFinderUtils.h"

#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "UObject/Package.h"

bool FAssetDocumentClassResolver::ResolveClass(const FString& ClassName, UClass*& OutClass, FString& OutError)
{
	OutClass = nullptr;

	FString NormalizedClassName = ClassName;
	NormalizedClassName.TrimStartAndEndInline();
	if (NormalizedClassName.IsEmpty())
	{
		OutError = TEXT("Class is required");
		return false;
	}

	if (!NormalizedClassName.StartsWith(TEXT("/")))
	{
		OutClass = FClassFinderUtils::FindClassByName(NormalizedClassName, UObject::StaticClass(), true);
	}

	if (!OutClass)
	{
		OutClass = StaticLoadClass(UObject::StaticClass(), nullptr, *NormalizedClassName);
	}

	if (!OutClass)
	{
		OutClass = FindObject<UClass>(nullptr, *NormalizedClassName);
	}

	return ValidateResolvedClass(OutClass, OutError);
}

bool FAssetDocumentClassResolver::ValidateResolvedClass(UClass* Class, FString& OutError)
{
	if (!Class)
	{
		OutError = TEXT("Failed to resolve asset class");
		return false;
	}

	if (!Class->IsChildOf(UObject::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Resolved class '%s' is not a UObject class"), *Class->GetName());
		return false;
	}

	if (Class->HasAnyClassFlags(CLASS_Abstract))
	{
		OutError = FString::Printf(TEXT("Resolved class '%s' is abstract"), *Class->GetName());
		return false;
	}

	if (Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		OutError = FString::Printf(TEXT("Resolved class '%s' is deprecated or newer-version-only"), *Class->GetName());
		return false;
	}

	if (Class->IsChildOf(UClass::StaticClass()) ||
		Class->IsChildOf(UPackage::StaticClass()) ||
		Class->IsChildOf(UWorld::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Resolved class '%s' is not supported for asset document creation"), *Class->GetName());
		return false;
	}

	if (Class->IsChildOf(UBlueprint::StaticClass()) && Class != UBlueprint::StaticClass())
	{
		OutError = FString::Printf(TEXT("Resolved class '%s' is a Blueprint-derived asset class that requires an exact AssetDocument profile"), *Class->GetName());
		return false;
	}

	if (Class->IsChildOf(UMaterial::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Resolved class '%s' is not supported by GenericAsset sidecar v1"), *Class->GetName());
		return false;
	}

	return true;
}
