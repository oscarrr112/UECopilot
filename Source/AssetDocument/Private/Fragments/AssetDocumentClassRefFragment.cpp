// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentFragment.h"

#include "AssetDocumentClassResolver.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
class FAssetDocumentClassRefFragmentAdapter final : public IAssetDocumentFragmentAdapter
{
public:
	virtual FName GetKind() const override
	{
		return TEXT("ClassRef");
	}

	virtual bool SupportsContext(const FAssetDocumentFragmentContext& Context) const override
	{
		return true;
	}

	virtual FAssetDocumentFragmentResult Validate(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const override
	{
		FString ClassName;
		if (!FragmentJson->TryGetStringField(TEXT("Class"), ClassName) || ClassName.TrimStartAndEnd().IsEmpty())
		{
			return FAssetDocumentFragmentResult::Failure(TEXT("ClassRef field 'Class' is required."), Context.JsonPath, TEXT("missing-classref-class"));
		}
		return FAssetDocumentFragmentResult::Success();
	}

	virtual FAssetDocumentFragmentResult Compile(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const override
	{
		FString ClassName;
		if (!FragmentJson->TryGetStringField(TEXT("Class"), ClassName) || ClassName.TrimStartAndEnd().IsEmpty())
		{
			return FAssetDocumentFragmentResult::Failure(TEXT("ClassRef field 'Class' is required."), Context.JsonPath, TEXT("missing-classref-class"));
		}

		UClass* ResolvedClass = nullptr;
		FString Error;
		if (!FAssetDocumentClassResolver::ResolveClass(ClassName, ResolvedClass, Error))
		{
			return FAssetDocumentFragmentResult::Failure(Error, Context.JsonPath, TEXT("classref-resolve-failed"));
		}

		if (Context.ExpectedBaseClass && !ResolvedClass->IsChildOf(Context.ExpectedBaseClass))
		{
			return FAssetDocumentFragmentResult::Failure(
				FString::Printf(TEXT("Class '%s' is not a child of '%s'."), *ResolvedClass->GetName(), *Context.ExpectedBaseClass->GetName()),
				Context.JsonPath,
				TEXT("classref-base-class-mismatch"));
		}

		FAssetDocumentFragmentResult Result = FAssetDocumentFragmentResult::Success();
		Result.Class = ResolvedClass;
		Result.Value = MakeShared<FJsonValueString>(ResolvedClass->GetPathName());
		return Result;
	}

	virtual FAssetDocumentFragmentResult Extract(const FAssetDocumentFragmentExtractContext& Context, TSharedRef<FJsonObject>& OutFragmentJson) const override
	{
		UClass* Class = Context.ValueObject ? Context.ValueObject->GetClass() : nullptr;
		if (!Class)
		{
			return FAssetDocumentFragmentResult::Failure(TEXT("ClassRef extraction requires ValueObject."), Context.JsonPath, TEXT("classref-extract-missing-object"));
		}

		OutFragmentJson->SetStringField(TEXT("Kind"), GetKind().ToString());
		OutFragmentJson->SetStringField(TEXT("Class"), Class->GetPathName());
		return FAssetDocumentFragmentResult::Success();
	}
};
}

TSharedRef<IAssetDocumentFragmentAdapter> CreateAssetDocumentClassRefFragmentAdapter()
{
	return MakeShared<FAssetDocumentClassRefFragmentAdapter>();
}
