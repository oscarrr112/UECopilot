// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentFragmentCompiler.h"

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

FAssetDocumentFragmentResult FAssetDocumentFragmentResult::Success(const FString& InMessage)
{
	FAssetDocumentFragmentResult Result;
	Result.bSuccess = true;
	Result.Message = InMessage;
	return Result;
}

FAssetDocumentFragmentResult FAssetDocumentFragmentResult::Success(TSharedPtr<FJsonValue> InValue)
{
	FAssetDocumentFragmentResult Result = Success(TEXT("success"));
	Result.Value = InValue;
	return Result;
}

FAssetDocumentFragmentResult FAssetDocumentFragmentResult::Failure(const FString& InMessage)
{
	FAssetDocumentFragmentResult Result;
	Result.bSuccess = false;
	Result.Message = InMessage;
	return Result;
}

FAssetDocumentFragmentResult FAssetDocumentFragmentResult::Failure(const FString& InMessage, TArray<FAssetDocumentDiagnostic> InDiagnostics)
{
	FAssetDocumentFragmentResult Result = Failure(InMessage);
	Result.Diagnostics = MoveTemp(InDiagnostics);
	return Result;
}

void FAssetDocumentFragmentCompiler::RegisterAdapter(TSharedRef<IAssetDocumentFragmentAdapter> Adapter)
{
	Adapters.Add(Adapter->GetKind(), Adapter);
}

FAssetDocumentFragmentResult FAssetDocumentFragmentCompiler::Validate(const TSharedPtr<FJsonObject>& Fragment, const FAssetDocumentFragmentContext& Context) const
{
	const IAssetDocumentFragmentAdapter* Adapter = nullptr;
	const FAssetDocumentFragmentResult ResolveResult = ResolveAdapter(Fragment, Context, Adapter);
	if (!ResolveResult.bSuccess)
	{
		return ResolveResult;
	}

	return Adapter->Validate(Fragment, Context);
}

FAssetDocumentFragmentResult FAssetDocumentFragmentCompiler::Compile(const TSharedPtr<FJsonObject>& Fragment, const FAssetDocumentFragmentContext& Context) const
{
	const IAssetDocumentFragmentAdapter* Adapter = nullptr;
	const FAssetDocumentFragmentResult ResolveResult = ResolveAdapter(Fragment, Context, Adapter);
	if (!ResolveResult.bSuccess)
	{
		return ResolveResult;
	}

	return Adapter->Compile(Fragment, Context);
}

FAssetDocumentFragmentResult FAssetDocumentFragmentCompiler::Extract(FName Kind, const FAssetDocumentFragmentExtractContext& Context) const
{
	FAssetDocumentFragmentContext AdapterContext;
	AdapterContext.OwnerAsset = Context.OwnerAsset;
	AdapterContext.JsonPath = Context.JsonPath;
	AdapterContext.Role = Context.Role;

	const IAssetDocumentFragmentAdapter* Adapter = FindAdapter(Kind, AdapterContext);
	if (!Adapter)
	{
		return UnknownKindFailure(Kind, AdapterContext);
	}

	return Adapter->Extract(Context);
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

FAssetDocumentFragmentResult FAssetDocumentFragmentCompiler::ResolveAdapter(const TSharedPtr<FJsonObject>& Fragment, const FAssetDocumentFragmentContext& Context, const IAssetDocumentFragmentAdapter*& OutAdapter) const
{
	OutAdapter = nullptr;

	if (!Fragment.IsValid())
	{
		return MissingKindFailure(Context);
	}

	const TSharedPtr<FJsonValue> KindValue = Fragment->TryGetField(TEXT("Kind"));
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

	return FAssetDocumentFragmentResult::Success(TEXT("adapter resolved"));
}

FAssetDocumentFragmentResult FAssetDocumentFragmentCompiler::MissingKindFailure(const FAssetDocumentFragmentContext& Context) const
{
	TArray<FAssetDocumentDiagnostic> Diagnostics;
	Diagnostics.Add(MakeFragmentDiagnostic(Context.JsonPath, TEXT("missing-fragment-kind"), TEXT("Fragment Kind must be a string.")));
	return FAssetDocumentFragmentResult::Failure(TEXT("Fragment Kind must be a string."), MoveTemp(Diagnostics));
}

FAssetDocumentFragmentResult FAssetDocumentFragmentCompiler::UnknownKindFailure(FName Kind, const FAssetDocumentFragmentContext& Context) const
{
	TArray<FAssetDocumentDiagnostic> Diagnostics;
	Diagnostics.Add(MakeFragmentDiagnostic(Context.JsonPath, TEXT("unknown-fragment-kind"), FString::Printf(TEXT("No fragment adapter is registered for Kind '%s'."), *Kind.ToString())));
	return FAssetDocumentFragmentResult::Failure(TEXT("Unknown fragment Kind."), MoveTemp(Diagnostics));
}
