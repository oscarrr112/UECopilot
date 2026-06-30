// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentFragment.h"

#include "AssetDocumentClassResolver.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"

namespace
{
FString GetRequiredStringField(const TSharedRef<FJsonObject>& FragmentJson, const TCHAR* FieldName, const FAssetDocumentFragmentContext& Context, FAssetDocumentFragmentResult& OutFailure)
{
	FString Value;
	if (!FragmentJson->TryGetStringField(FieldName, Value) || Value.TrimStartAndEnd().IsEmpty())
	{
		OutFailure = FAssetDocumentFragmentResult::Failure(
			FString::Printf(TEXT("AssetRef field '%s' is required."), FieldName),
			Context.JsonPath,
			TEXT("missing-assetref-field"));
		return FString();
	}
	return Value;
}

FString NormalizeObjectPath(const FString& Path)
{
	FString ObjectPath = Path;
	ObjectPath.TrimStartAndEndInline();
	if (ObjectPath.StartsWith(TEXT("/")) && !ObjectPath.Contains(TEXT(".")))
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(ObjectPath);
		if (!AssetName.IsEmpty())
		{
			ObjectPath = FString::Printf(TEXT("%s.%s"), *ObjectPath, *AssetName);
		}
	}
	return ObjectPath;
}

class FAssetDocumentAssetRefFragmentAdapter final : public IAssetDocumentFragmentAdapter
{
public:
	virtual FName GetKind() const override
	{
		return TEXT("AssetRef");
	}

	virtual bool SupportsContext(const FAssetDocumentFragmentContext& Context) const override
	{
		return true;
	}

	virtual FAssetDocumentFragmentResult Validate(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const override
	{
		FAssetDocumentFragmentResult Failure = FAssetDocumentFragmentResult::Success();
		GetRequiredStringField(FragmentJson, TEXT("Path"), Context, Failure);
		return Failure.bSuccess ? FAssetDocumentFragmentResult::Success() : Failure;
	}

	virtual FAssetDocumentFragmentResult Compile(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const override
	{
		FAssetDocumentFragmentResult Failure = FAssetDocumentFragmentResult::Success();
		const FString OriginalPath = GetRequiredStringField(FragmentJson, TEXT("Path"), Context, Failure);
		if (!Failure.bSuccess)
		{
			return Failure;
		}

		const FString ObjectPath = NormalizeObjectPath(OriginalPath);
		UObject* LoadedAsset = LoadObject<UObject>(nullptr, *ObjectPath);
		if (!LoadedAsset)
		{
			return FAssetDocumentFragmentResult::Failure(
				FString::Printf(TEXT("Failed to load asset '%s'."), *ObjectPath),
				Context.JsonPath,
				TEXT("assetref-load-failed"));
		}

		if (Context.ExpectedBaseClass && !LoadedAsset->IsA(Context.ExpectedBaseClass))
		{
			return FAssetDocumentFragmentResult::Failure(
				FString::Printf(TEXT("Asset '%s' is not a '%s'."), *ObjectPath, *Context.ExpectedBaseClass->GetName()),
				Context.JsonPath,
				TEXT("assetref-base-class-mismatch"));
		}

		FString ClassName;
		if (FragmentJson->TryGetStringField(TEXT("Class"), ClassName) && !ClassName.TrimStartAndEnd().IsEmpty())
		{
			UClass* ResolvedClass = nullptr;
			FString Error;
			if (!FAssetDocumentClassResolver::ResolveClass(ClassName, ResolvedClass, Error))
			{
				return FAssetDocumentFragmentResult::Failure(Error, Context.JsonPath, TEXT("assetref-class-resolve-failed"));
			}
			if (!LoadedAsset->IsA(ResolvedClass))
			{
				return FAssetDocumentFragmentResult::Failure(
					FString::Printf(TEXT("Asset '%s' is not a '%s'."), *ObjectPath, *ResolvedClass->GetName()),
					Context.JsonPath,
					TEXT("assetref-class-mismatch"));
			}
		}

		FAssetDocumentFragmentResult Result = FAssetDocumentFragmentResult::Success();
		Result.Object = LoadedAsset;
		Result.Value = MakeShared<FJsonValueString>(OriginalPath);
		return Result;
	}

	virtual FAssetDocumentFragmentResult Extract(const FAssetDocumentFragmentExtractContext& Context, TSharedRef<FJsonObject>& OutFragmentJson) const override
	{
		if (!Context.ValueObject)
		{
			return FAssetDocumentFragmentResult::Failure(TEXT("AssetRef extraction requires ValueObject."), Context.JsonPath, TEXT("assetref-extract-missing-object"));
		}

		OutFragmentJson->SetStringField(TEXT("Kind"), GetKind().ToString());
		OutFragmentJson->SetStringField(TEXT("Path"), Context.ValueObject->GetPathName());
		return FAssetDocumentFragmentResult::Success();
	}
};
}

TSharedRef<IAssetDocumentFragmentAdapter> CreateAssetDocumentAssetRefFragmentAdapter()
{
	return MakeShared<FAssetDocumentAssetRefFragmentAdapter>();
}
