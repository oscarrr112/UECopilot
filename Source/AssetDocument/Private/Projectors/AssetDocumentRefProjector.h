#pragma once

#include "CoreMinimal.h"
#include "Projectors/AssetDocumentProjectionTypes.h"

class FJsonObject;

class FAssetDocumentRefProjector
{
public:
	FAssetDocumentProjectionResult ProjectAssetRef(const UObject* Object, const UClass* ExpectedClass, const FString& Path, TSharedPtr<FJsonObject>& OutJson) const;
};
