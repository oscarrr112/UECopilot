// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class UObject;

struct FAssetDocumentPreviewApplyDiffBodyKeyContext
{
	const FAssetDocumentCapabilityContext* CurrentContext = nullptr;
	const FAssetDocumentCapabilityContext* PreviewContext = nullptr;
	FString BodyKey;
	FString JsonPointer;
	TSharedPtr<FJsonValue> CurrentValue;
	TSharedPtr<FJsonValue> DesiredValue;
};

struct FAssetDocumentPreviewApplyDiffHooks
{
	TFunction<FAssetDocumentCapabilityResult(const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonObject>&)> ValidateDesiredBody;
	TFunction<FAssetDocumentCapabilityResult(const FAssetDocumentCapabilityContext&, UObject*&)> DuplicatePreviewAsset;
	TFunction<FAssetDocumentCapabilityContext(const FAssetDocumentCapabilityContext&, UObject*)> MakePreviewContext;
	TFunction<FAssetDocumentCapabilityResult(const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonValue>&)> ApplyDesiredBody;
	TFunction<FAssetDocumentCapabilityResult(const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonObject>&)> ExtractBody;
	TFunction<FAssetDocumentCapabilityResult(const FAssetDocumentPreviewApplyDiffBodyKeyContext&, TArray<TSharedPtr<FJsonValue>>&, bool&)> DiffBodyKey;
};

class FAssetDocumentPreviewApplyDiffAdapter
{
public:
	explicit FAssetDocumentPreviewApplyDiffAdapter(FAssetDocumentPreviewApplyDiffHooks InHooks);

	FAssetDocumentCapabilityResult DiffBody(
		const FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonValue>& DesiredJson,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const;

	static TSharedRef<FJsonObject> MakeBodyObjectForDiff(const TSharedRef<FJsonObject>& BodyObject);

private:
	FAssetDocumentPreviewApplyDiffHooks Hooks;
};
