// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentFragmentCompiler.h"

TSharedRef<IAssetDocumentFragmentAdapter> CreateAssetDocumentAssetRefFragmentAdapter();
TSharedRef<IAssetDocumentFragmentAdapter> CreateAssetDocumentClassRefFragmentAdapter();
TSharedRef<IAssetDocumentFragmentAdapter> CreateAssetDocumentStructValueFragmentAdapter();
TSharedRef<IAssetDocumentFragmentAdapter> CreateAssetDocumentEmbeddedObjectFragmentAdapter();
TSharedRef<IAssetDocumentFragmentAdapter> CreateAssetDocumentDefinitionRefFragmentAdapter(const FAssetDocumentFragmentCompiler& Compiler);

namespace
{
FAssetDocumentDiagnostic MakeFragmentDiagnostic(const FString& Path, const FString& Code, const FString& Message)
{
	FAssetDocumentDiagnostic Diagnostic;
	Diagnostic.Path = Path;
	Diagnostic.Code = Code;
	Diagnostic.Message = Message;
	return Diagnostic;
}
}

FAssetDocumentFragmentResult FAssetDocumentFragmentResult::Success()
{
	FAssetDocumentFragmentResult Result;
	Result.bSuccess = true;
	return Result;
}

FAssetDocumentFragmentResult FAssetDocumentFragmentResult::Failure(const FString& Message, const FString& Path, const FString& Code)
{
	FAssetDocumentFragmentResult Result;
	Result.bSuccess = false;
	Result.Message = Message;
	if (!Path.IsEmpty() || !Code.IsEmpty())
	{
		Result.Diagnostics.Add(MakeFragmentDiagnostic(Path, Code, Message));
	}
	return Result;
}

void FAssetDocumentFragmentCompiler::RegisterAdapter(TSharedRef<IAssetDocumentFragmentAdapter> Adapter)
{
	Adapters.Add(Adapter->GetKind(), Adapter);
}

void FAssetDocumentFragmentCompiler::RegisterBuiltInAdapters()
{
	RegisterAdapter(CreateAssetDocumentAssetRefFragmentAdapter());
	RegisterAdapter(CreateAssetDocumentClassRefFragmentAdapter());
	RegisterAdapter(CreateAssetDocumentStructValueFragmentAdapter());
	RegisterAdapter(CreateAssetDocumentEmbeddedObjectFragmentAdapter());
	RegisterAdapter(CreateAssetDocumentDefinitionRefFragmentAdapter(*this));
}

FAssetDocumentFragmentResult FAssetDocumentFragmentCompiler::Validate(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const
{
	const IAssetDocumentFragmentAdapter* Adapter = nullptr;
	const FAssetDocumentFragmentResult ResolveResult = ResolveAdapter(FragmentJson, Context, Adapter);
	if (!ResolveResult.bSuccess)
	{
		return ResolveResult;
	}

	return Adapter->Validate(FragmentJson, Context);
}

FAssetDocumentFragmentResult FAssetDocumentFragmentCompiler::Compile(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const
{
	const IAssetDocumentFragmentAdapter* Adapter = nullptr;
	const FAssetDocumentFragmentResult ResolveResult = ResolveAdapter(FragmentJson, Context, Adapter);
	if (!ResolveResult.bSuccess)
	{
		return ResolveResult;
	}

	return Adapter->Compile(FragmentJson, Context);
}

FAssetDocumentFragmentResult FAssetDocumentFragmentCompiler::Extract(const FAssetDocumentFragmentExtractContext& Context, TSharedRef<FJsonObject>& OutFragmentJson) const
{
	FAssetDocumentFragmentContext AdapterContext;
	AdapterContext.OwnerAsset = Context.OwnerAsset;
	AdapterContext.JsonPath = Context.JsonPath;
	AdapterContext.Role = Context.Role;

	if (Context.Kind.IsNone())
	{
		return MissingKindFailure(AdapterContext);
	}

	const FName Kind = Context.Kind;
	const IAssetDocumentFragmentAdapter* Adapter = FindAdapter(Kind, AdapterContext);
	if (!Adapter)
	{
		return UnknownKindFailure(Kind, AdapterContext);
	}

	return Adapter->Extract(Context, OutFragmentJson);
}

const IAssetDocumentFragmentAdapter* FAssetDocumentFragmentCompiler::FindAdapter(FName Kind, const FAssetDocumentFragmentContext& Context) const
{
	if (const TSharedRef<IAssetDocumentFragmentAdapter>* Adapter = Adapters.Find(Kind))
	{
		if ((*Adapter)->SupportsContext(Context))
		{
			return &Adapter->Get();
		}
	}

	return nullptr;
}

FAssetDocumentFragmentResult FAssetDocumentFragmentCompiler::ResolveAdapter(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context, const IAssetDocumentFragmentAdapter*& OutAdapter) const
{
	OutAdapter = nullptr;

	const TSharedPtr<FJsonValue> KindValue = FragmentJson->TryGetField(TEXT("Kind"));
	if (!KindValue.IsValid() || KindValue->Type != EJson::String)
	{
		return MissingKindFailure(Context);
	}

	const FName Kind(*KindValue->AsString());
	OutAdapter = FindAdapter(Kind, Context);
	if (!OutAdapter)
	{
		return UnknownKindFailure(Kind, Context);
	}

	return FAssetDocumentFragmentResult::Success();
}

FAssetDocumentFragmentResult FAssetDocumentFragmentCompiler::MissingKindFailure(const FAssetDocumentFragmentContext& Context) const
{
	return FAssetDocumentFragmentResult::Failure(TEXT("Fragment Kind must be a string."), Context.JsonPath, TEXT("missing-fragment-kind"));
}

FAssetDocumentFragmentResult FAssetDocumentFragmentCompiler::UnknownKindFailure(FName Kind, const FAssetDocumentFragmentContext& Context) const
{
	return FAssetDocumentFragmentResult::Failure(
		FString::Printf(TEXT("No fragment adapter is registered for Kind '%s'."), *Kind.ToString()),
		Context.JsonPath,
		TEXT("unknown-fragment-kind"));
}
