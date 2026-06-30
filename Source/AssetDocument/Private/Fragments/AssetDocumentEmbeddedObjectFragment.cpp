// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentFragment.h"

#include "AssetDocumentClassResolver.h"
#include "AssetDocumentPropertyAdapter.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UObjectGlobals.h"

namespace
{
class FAssetDocumentEmbeddedObjectFragmentAdapter final : public IAssetDocumentFragmentAdapter
{
public:
	virtual FName GetKind() const override
	{
		return TEXT("EmbeddedObject");
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
			return FAssetDocumentFragmentResult::Failure(TEXT("EmbeddedObject field 'Class' is required."), Context.JsonPath, TEXT("missing-embeddedobject-class"));
		}
		return FAssetDocumentFragmentResult::Success();
	}

	virtual FAssetDocumentFragmentResult Compile(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const override
	{
		FString ClassName;
		if (!FragmentJson->TryGetStringField(TEXT("Class"), ClassName) || ClassName.TrimStartAndEnd().IsEmpty())
		{
			return FAssetDocumentFragmentResult::Failure(TEXT("EmbeddedObject field 'Class' is required."), Context.JsonPath, TEXT("missing-embeddedobject-class"));
		}

		if (!Context.Outer)
		{
			return FAssetDocumentFragmentResult::Failure(TEXT("EmbeddedObject requires Context.Outer."), Context.JsonPath, TEXT("embeddedobject-missing-outer"));
		}

		UClass* ResolvedClass = nullptr;
		FString Error;
		if (!FAssetDocumentClassResolver::ResolveClass(ClassName, ResolvedClass, Error))
		{
			return FAssetDocumentFragmentResult::Failure(Error, Context.JsonPath, TEXT("embeddedobject-class-resolve-failed"));
		}

		if (Context.ExpectedBaseClass && !ResolvedClass->IsChildOf(Context.ExpectedBaseClass))
		{
			return FAssetDocumentFragmentResult::Failure(
				FString::Printf(TEXT("Class '%s' is not a child of '%s'."), *ResolvedClass->GetName(), *Context.ExpectedBaseClass->GetName()),
				Context.JsonPath,
				TEXT("embeddedobject-base-class-mismatch"));
		}

		if (ResolvedClass->HasAnyClassFlags(CLASS_Abstract))
		{
			return FAssetDocumentFragmentResult::Failure(
				FString::Printf(TEXT("Class '%s' is abstract."), *ResolvedClass->GetName()),
				Context.JsonPath,
				TEXT("embeddedobject-abstract-class"));
		}

		const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
		const bool bHasProperties = FragmentJson->TryGetObjectField(TEXT("Properties"), PropertiesPtr) && PropertiesPtr && PropertiesPtr->IsValid();
		if (bHasProperties)
		{
			const FAssetDocumentPropertyApplyResult PreflightResult = FAssetDocumentPropertyAdapter::PreflightProperties(ResolvedClass, *PropertiesPtr);
			if (!PreflightResult.bSuccess)
			{
				FAssetDocumentFragmentResult Failure = FAssetDocumentFragmentResult::Failure(PreflightResult.Message, Context.JsonPath, TEXT("embeddedobject-preflight-failed"));
				Failure.Diagnostics.Append(PreflightResult.Diagnostics);
				return Failure;
			}
		}

		UObject* CreatedObject = NewObject<UObject>(Context.Outer, ResolvedClass, NAME_None, RF_Transactional);
		if (!CreatedObject)
		{
			return FAssetDocumentFragmentResult::Failure(
				FString::Printf(TEXT("Failed to create embedded object '%s'."), *ResolvedClass->GetName()),
				Context.JsonPath,
				TEXT("embeddedobject-create-failed"));
		}

		if (bHasProperties)
		{
			const FAssetDocumentPropertyApplyResult ApplyResult = FAssetDocumentPropertyAdapter::ApplyProperties(CreatedObject, *PropertiesPtr);
			if (!ApplyResult.bSuccess)
			{
				FAssetDocumentFragmentResult Failure = FAssetDocumentFragmentResult::Failure(ApplyResult.Message, Context.JsonPath, TEXT("embeddedobject-apply-failed"));
				Failure.Diagnostics.Append(ApplyResult.Diagnostics);
				return Failure;
			}
		}

		FAssetDocumentFragmentResult Result = FAssetDocumentFragmentResult::Success();
		Result.Object = CreatedObject;
		Result.Value = MakeShared<FJsonValueString>(CreatedObject->GetPathName());
		return Result;
	}

	virtual FAssetDocumentFragmentResult Extract(const FAssetDocumentFragmentExtractContext& Context, TSharedRef<FJsonObject>& OutFragmentJson) const override
	{
		if (!Context.ValueObject)
		{
			return FAssetDocumentFragmentResult::Failure(TEXT("EmbeddedObject extraction requires ValueObject."), Context.JsonPath, TEXT("embeddedobject-extract-missing-object"));
		}

		OutFragmentJson->SetStringField(TEXT("Kind"), GetKind().ToString());
		OutFragmentJson->SetStringField(TEXT("Class"), Context.ValueObject->GetClass()->GetPathName());
		return FAssetDocumentFragmentResult::Success();
	}
};
}

TSharedRef<IAssetDocumentFragmentAdapter> CreateAssetDocumentEmbeddedObjectFragmentAdapter()
{
	return MakeShared<FAssetDocumentEmbeddedObjectFragmentAdapter>();
}
