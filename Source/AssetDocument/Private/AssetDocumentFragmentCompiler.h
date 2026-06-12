// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentFragment.h"

class FAssetDocumentFragmentCompiler
{
public:
	void RegisterAdapter(TSharedRef<IAssetDocumentFragmentAdapter> Adapter);

	FAssetDocumentFragmentResult Validate(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const;
	FAssetDocumentFragmentResult Compile(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const;
	FAssetDocumentFragmentResult Extract(const FAssetDocumentFragmentExtractContext& Context, TSharedRef<FJsonObject>& OutFragmentJson) const;

private:
	const IAssetDocumentFragmentAdapter* FindAdapter(FName Kind, const FAssetDocumentFragmentContext& Context) const;
	FAssetDocumentFragmentResult ResolveAdapter(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context, const IAssetDocumentFragmentAdapter*& OutAdapter) const;
	FAssetDocumentFragmentResult MissingKindFailure(const FAssetDocumentFragmentContext& Context) const;
	FAssetDocumentFragmentResult UnknownKindFailure(FName Kind, const FAssetDocumentFragmentContext& Context) const;

	TMap<FName, TSharedRef<IAssetDocumentFragmentAdapter>> Adapters;
};
