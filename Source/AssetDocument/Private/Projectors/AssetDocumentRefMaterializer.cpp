#include "Projectors/AssetDocumentRefMaterializer.h"

#include "Dom/JsonObject.h"
#include "UObject/SoftObjectPath.h"

FAssetDocumentUpdateResult FAssetDocumentRefMaterializer::ResolveAssetRef(const TSharedRef<FJsonObject>& RefJson, const UClass* ExpectedClass, const FString& Path, UObject*& OutObject) const
{
	OutObject = nullptr;

	FString Kind;
	if (!RefJson->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("AssetRef"))
	{
		return FAssetDocumentUpdateResult::Failure(FString::Printf(TEXT("Asset reference at '%s' must have Kind='AssetRef'."), *Path));
	}

	FString AssetPath;
	if (!RefJson->TryGetStringField(TEXT("Path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
	{
		return FAssetDocumentUpdateResult::Failure(FString::Printf(TEXT("Asset reference at '%s' requires a string Path."), *Path));
	}

	FSoftObjectPath SoftObjectPath(AssetPath);
	UObject* ResolvedObject = SoftObjectPath.ResolveObject();
	if (!ResolvedObject)
	{
		ResolvedObject = SoftObjectPath.TryLoad();
	}

	if (!ResolvedObject)
	{
		return FAssetDocumentUpdateResult::Failure(FString::Printf(TEXT("Failed to load asset reference '%s' at '%s'."), *AssetPath, *Path));
	}

	if (ExpectedClass && !ResolvedObject->IsA(ExpectedClass))
	{
		return FAssetDocumentUpdateResult::Failure(
			FString::Printf(TEXT("Asset reference at '%s' resolved class '%s', expected '%s'."),
				*Path,
				*ResolvedObject->GetClass()->GetPathName(),
				*ExpectedClass->GetPathName()));
	}

	OutObject = ResolvedObject;
	return FAssetDocumentUpdateResult::Success();
}
