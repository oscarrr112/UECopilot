#pragma once

#include "CoreMinimal.h"
#include "Projectors/AssetDocumentUpdatePlan.h"

class FJsonObject;

class FAssetDocumentRefMaterializer
{
public:
	// Null references are represented before this object-ref API is called; callers should clear the destination directly for JSON null values.
	FAssetDocumentUpdateResult ResolveAssetRef(const TSharedRef<FJsonObject>& RefJson, const UClass* ExpectedClass, const FString& Path, UObject*& OutObject) const;
};
