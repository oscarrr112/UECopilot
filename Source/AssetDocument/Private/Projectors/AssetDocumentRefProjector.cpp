#include "Projectors/AssetDocumentRefProjector.h"

#include "Dom/JsonObject.h"
#include "UObject/SoftObjectPath.h"

FAssetDocumentProjectionResult FAssetDocumentRefProjector::ProjectAssetRef(const UObject* Object, const UClass* ExpectedClass, const FString& Path, TSharedPtr<FJsonObject>& OutJson) const
{
	OutJson.Reset();

	if (!Object)
	{
		return FAssetDocumentProjectionResult::Success();
	}

	if (ExpectedClass && !Object->IsA(ExpectedClass))
	{
		return FAssetDocumentProjectionResult::Failure(
			FString::Printf(TEXT("Asset reference at '%s' has class '%s', expected '%s'."),
				*Path,
				*Object->GetClass()->GetPathName(),
				*ExpectedClass->GetPathName()),
			Path,
			TEXT("InvalidAssetRefClass"));
	}

	OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	OutJson->SetStringField(TEXT("Path"), FSoftObjectPath(Object).ToString());
	OutJson->SetStringField(TEXT("Class"), Object->GetClass()->GetPathName());

	return FAssetDocumentProjectionResult::Success();
}
