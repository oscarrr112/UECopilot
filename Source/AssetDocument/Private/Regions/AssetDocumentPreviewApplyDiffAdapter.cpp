// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentPreviewApplyDiffAdapter.h"

#include "AssetDocumentJsonRegionUtils.h"

namespace
{
FAssetDocumentCapabilityResult InvalidHookFailure(const FString& HookName)
{
	return FAssetDocumentJsonRegionUtils::Failure(
		TEXT("/Body"),
		TEXT("InvalidPreviewApplyDiffAdapter"),
		FString::Printf(TEXT("Preview apply diff adapter is missing %s hook"), *HookName));
}
}

FAssetDocumentPreviewApplyDiffAdapter::FAssetDocumentPreviewApplyDiffAdapter(FAssetDocumentPreviewApplyDiffHooks InHooks)
	: Hooks(MoveTemp(InHooks))
{
}

TSharedRef<FJsonObject> FAssetDocumentPreviewApplyDiffAdapter::MakeBodyObjectForDiff(const TSharedRef<FJsonObject>& BodyObject)
{
	if (!BodyObject->HasField(TEXT("_Skipped")))
	{
		return BodyObject;
	}

	TSharedRef<FJsonObject> DiffBody = MakeShared<FJsonObject>();
	DiffBody->Values = BodyObject->Values;
	DiffBody->RemoveField(TEXT("_Skipped"));
	return DiffBody;
}

FAssetDocumentCapabilityResult FAssetDocumentPreviewApplyDiffAdapter::DiffBody(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonValue>& DesiredJson,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	if (DesiredJson->Type != EJson::Object)
	{
		return FAssetDocumentJsonRegionUtils::Failure(TEXT("/Body"), TEXT("InvalidBodyType"), TEXT("Body must be a JSON object"));
	}

	const TSharedPtr<FJsonObject> DesiredBody = DesiredJson->AsObject();
	if (!DesiredBody.IsValid())
	{
		return FAssetDocumentJsonRegionUtils::Failure(TEXT("/Body"), TEXT("InvalidBodyType"), TEXT("Body must be a JSON object"));
	}

	const TSharedRef<FJsonObject> DesiredBodyForDiff = MakeBodyObjectForDiff(DesiredBody.ToSharedRef());
	const TSharedRef<FJsonValue> DesiredJsonForDiff = MakeShared<FJsonValueObject>(DesiredBodyForDiff);

	if (!Hooks.ValidateDesiredBody)
	{
		return InvalidHookFailure(TEXT("ValidateDesiredBody"));
	}
	FAssetDocumentCapabilityResult Result = Hooks.ValidateDesiredBody(Context, DesiredBodyForDiff);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (!Hooks.DuplicatePreviewAsset)
	{
		return InvalidHookFailure(TEXT("DuplicatePreviewAsset"));
	}
	UObject* PreviewAsset = nullptr;
	Result = Hooks.DuplicatePreviewAsset(Context, PreviewAsset);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (!PreviewAsset)
	{
		return FAssetDocumentJsonRegionUtils::Failure(TEXT("/Body"), TEXT("DuplicateFailed"), TEXT("Failed to duplicate asset for Body diff"));
	}

	if (!Hooks.MakePreviewContext)
	{
		return InvalidHookFailure(TEXT("MakePreviewContext"));
	}
	FAssetDocumentCapabilityContext PreviewContext = Hooks.MakePreviewContext(Context, PreviewAsset);

	if (!Hooks.ApplyDesiredBody)
	{
		return InvalidHookFailure(TEXT("ApplyDesiredBody"));
	}
	Result = Hooks.ApplyDesiredBody(PreviewContext, DesiredJsonForDiff);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (!Hooks.ExtractBody)
	{
		return InvalidHookFailure(TEXT("ExtractBody"));
	}
	TSharedRef<FJsonObject> CurrentBody = MakeShared<FJsonObject>();
	Result = Hooks.ExtractBody(Context, CurrentBody);
	if (!Result.bSuccess)
	{
		return Result;
	}
	CurrentBody = MakeBodyObjectForDiff(CurrentBody);

	TSharedRef<FJsonObject> PreviewBody = MakeShared<FJsonObject>();
	Result = Hooks.ExtractBody(PreviewContext, PreviewBody);
	if (!Result.bSuccess)
	{
		return Result;
	}
	PreviewBody = MakeBodyObjectForDiff(PreviewBody);

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : DesiredBodyForDiff->Values)
	{
		if (Pair.Key == TEXT("_Skipped"))
		{
			continue;
		}

		const TSharedPtr<FJsonValue>* CurrentValue = CurrentBody->Values.Find(Pair.Key);
		const TSharedPtr<FJsonValue> Current = CurrentValue ? *CurrentValue : MakeShared<FJsonValueNull>();
		const TSharedPtr<FJsonValue>* DesiredValue = PreviewBody->Values.Find(Pair.Key);
		const TSharedPtr<FJsonValue> Desired = DesiredValue ? *DesiredValue : MakeShared<FJsonValueNull>();
		const FString JsonPointer = FAssetDocumentJsonRegionUtils::MakeBodyPath(Pair.Key);

		if (Hooks.DiffBodyKey)
		{
			bool bHandled = false;
			FAssetDocumentPreviewApplyDiffBodyKeyContext KeyContext;
			KeyContext.CurrentContext = &Context;
			KeyContext.PreviewContext = &PreviewContext;
			KeyContext.BodyKey = Pair.Key;
			KeyContext.JsonPointer = JsonPointer;
			KeyContext.CurrentValue = Current;
			KeyContext.DesiredValue = Desired;
			Result = Hooks.DiffBodyKey(KeyContext, OutDiffEntries, bHandled);
			if (!Result.bSuccess)
			{
				return Result;
			}
			if (bHandled)
			{
				continue;
			}
		}

		const FString Status =
			FAssetDocumentJsonRegionUtils::JsonValueToComparableString(Current) ==
			FAssetDocumentJsonRegionUtils::JsonValueToComparableString(Desired)
				? TEXT("unchanged")
				: TEXT("changed");
		FAssetDocumentJsonRegionUtils::AddDiffEntry(
			OutDiffEntries,
			JsonPointer,
			Status,
			Current,
			Desired);
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Preview apply Body diffed"));
}
