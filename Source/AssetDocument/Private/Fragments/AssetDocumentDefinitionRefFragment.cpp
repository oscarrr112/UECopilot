// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentFragmentCompiler.h"

#include "Dom/JsonObject.h"

namespace
{
class FAssetDocumentDefinitionRefFragmentAdapter final : public IAssetDocumentFragmentAdapter
{
public:
	explicit FAssetDocumentDefinitionRefFragmentAdapter(const FAssetDocumentFragmentCompiler& InCompiler)
		: Compiler(InCompiler)
	{
	}

	virtual FName GetKind() const override
	{
		return TEXT("DefinitionRef");
	}

	virtual bool SupportsContext(const FAssetDocumentFragmentContext& Context) const override
	{
		return true;
	}

	virtual FAssetDocumentFragmentResult Validate(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const override
	{
		FString Id;
		if (!FragmentJson->TryGetStringField(TEXT("Id"), Id) || Id.TrimStartAndEnd().IsEmpty())
		{
			return FAssetDocumentFragmentResult::Failure(TEXT("DefinitionRef field 'Id' is required."), Context.JsonPath, TEXT("missing-definitionref-id"));
		}
		return FAssetDocumentFragmentResult::Success();
	}

	virtual FAssetDocumentFragmentResult Compile(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const override
	{
		FString Id;
		if (!FragmentJson->TryGetStringField(TEXT("Id"), Id) || Id.TrimStartAndEnd().IsEmpty())
		{
			return FAssetDocumentFragmentResult::Failure(TEXT("DefinitionRef field 'Id' is required."), Context.JsonPath, TEXT("missing-definitionref-id"));
		}

		if (!Context.Definitions || !Context.Definitions->IsValid())
		{
			return FAssetDocumentFragmentResult::Failure(TEXT("DefinitionRef requires Context.Definitions."), Context.JsonPath, TEXT("definitionref-missing-definitions"));
		}

		if (Context.DefinitionStack.Contains(Id))
		{
			return FAssetDocumentFragmentResult::Failure(
				FString::Printf(TEXT("DefinitionRef cycle detected at '%s'."), *Id),
				Context.JsonPath,
				TEXT("definitionref-cycle"));
		}

		const TSharedPtr<FJsonObject>* DefinitionJsonPtr = nullptr;
		if (!(*Context.Definitions)->TryGetObjectField(Id, DefinitionJsonPtr) || !DefinitionJsonPtr || !DefinitionJsonPtr->IsValid())
		{
			return FAssetDocumentFragmentResult::Failure(
				FString::Printf(TEXT("Definition '%s' was not found."), *Id),
				Context.JsonPath,
				TEXT("definitionref-missing-id"));
		}

		FAssetDocumentFragmentContext DefinitionContext = Context;
		DefinitionContext.DefinitionStack.Add(Id);
		DefinitionContext.JsonPath = FString::Printf(TEXT("%s -> /Definitions/%s"), *Context.JsonPath, *Id);
		return Compiler.Compile(DefinitionJsonPtr->ToSharedRef(), DefinitionContext);
	}

	virtual FAssetDocumentFragmentResult Extract(const FAssetDocumentFragmentExtractContext& Context, TSharedRef<FJsonObject>& OutFragmentJson) const override
	{
		return FAssetDocumentFragmentResult::Failure(TEXT("DefinitionRef extraction is not supported."), Context.JsonPath, TEXT("definitionref-extract-unsupported"));
	}

private:
	const FAssetDocumentFragmentCompiler& Compiler;
};
}

TSharedRef<IAssetDocumentFragmentAdapter> CreateAssetDocumentDefinitionRefFragmentAdapter(const FAssetDocumentFragmentCompiler& Compiler)
{
	return MakeShared<FAssetDocumentDefinitionRefFragmentAdapter>(Compiler);
}
