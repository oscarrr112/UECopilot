// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentFragment.h"

class FAssetDocumentFragmentCompiler
{
public:
	void RegisterAdapter(TSharedRef<IAssetDocumentFragmentAdapter> Adapter);

	FAssetDocumentFragmentResult Validate(const TSharedPtr<FJsonObject>& Fragment, const FAssetDocumentFragmentContext& Context) const;
	FAssetDocumentFragmentResult Compile(const TSharedPtr<FJsonObject>& Fragment, const FAssetDocumentFragmentContext& Context) const;
	FAssetDocumentFragmentResult Extract(FName Kind, const FAssetDocumentFragmentExtractContext& Context) const;

private:
	const IAssetDocumentFragmentAdapter* FindAdapter(FName Kind, const FAssetDocumentFragmentContext& Context) const;
	FAssetDocumentFragmentResult ResolveAdapter(const TSharedPtr<FJsonObject>& Fragment, const FAssetDocumentFragmentContext& Context, const IAssetDocumentFragmentAdapter*& OutAdapter) const;
	FAssetDocumentFragmentResult MissingKindFailure(const FAssetDocumentFragmentContext& Context) const;
	FAssetDocumentFragmentResult UnknownKindFailure(FName Kind, const FAssetDocumentFragmentContext& Context) const;

	TMap<FName, TSharedRef<IAssetDocumentFragmentAdapter>> Adapters;
};
