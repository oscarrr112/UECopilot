// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"

#include "Animation/AnimTypes.h"

class FAssetDocumentFragmentCompiler;
class UAnimMontage;

struct FAnimMontageNotifyPlacementResult
{
	bool bHasNotifies = false;
	TArray<FAnimNotifyEvent> Notifies;
	bool bHasNotifyStates = false;
	TArray<FAnimNotifyEvent> NotifyStates;
};

class FAnimMontageNotifyPlacementAdapter
{
public:
	FAssetDocumentCapabilityResult Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonObject>& BodyObject) const;
	FAssetDocumentCapabilityResult Compile(
		const FAssetDocumentFragmentCompiler& Compiler,
		const FAssetDocumentCapabilityContext& Context,
		UAnimMontage* Montage,
		const TSharedRef<FJsonObject>& BodyObject,
		FAnimMontageNotifyPlacementResult& OutResult) const;
	FAssetDocumentCapabilityResult Extract(
		const FAssetDocumentFragmentCompiler& Compiler,
		const UAnimMontage* Montage,
		TSharedRef<FJsonObject>& OutBodyJson) const;
};
