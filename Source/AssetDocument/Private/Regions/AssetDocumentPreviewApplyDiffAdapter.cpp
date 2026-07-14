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

TSharedPtr<FJsonValue> PreviewCloneJsonValue(const TSharedPtr<FJsonValue>& Value);

TSharedRef<FJsonObject> CloneJsonObject(const TSharedRef<FJsonObject>& Object)
{
	TSharedRef<FJsonObject> Clone = MakeShared<FJsonObject>();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		Clone->SetField(Pair.Key, PreviewCloneJsonValue(Pair.Value));
	}
	return Clone;
}

TSharedPtr<FJsonValue> PreviewCloneJsonValue(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return MakeShared<FJsonValueNull>();
	}

	switch (Value->Type)
	{
	case EJson::Object:
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		if (!Object.IsValid())
		{
			return MakeShared<FJsonValueNull>();
		}
		return MakeShared<FJsonValueObject>(CloneJsonObject(Object.ToSharedRef()));
	}
	case EJson::Array:
	{
		TArray<TSharedPtr<FJsonValue>> ClonedArray;
		for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
		{
			ClonedArray.Add(PreviewCloneJsonValue(Item));
		}
		return MakeShared<FJsonValueArray>(MoveTemp(ClonedArray));
	}
	case EJson::String:
		return MakeShared<FJsonValueString>(Value->AsString());
	case EJson::Number:
		return MakeShared<FJsonValueNumber>(Value->AsNumber());
	case EJson::Boolean:
		return MakeShared<FJsonValueBoolean>(Value->AsBool());
	case EJson::Null:
	default:
		return MakeShared<FJsonValueNull>();
	}
}

FAssetDocumentCapabilityResult InvalidPreviewContextFailure()
{
	return FAssetDocumentJsonRegionUtils::Failure(
		TEXT("/Body"),
		TEXT("InvalidPreviewApplyDiffAdapter"),
		TEXT("MakePreviewContext must return a dry-run context for the duplicated preview asset"));
}
}

FAssetDocumentPreviewApplyDiffAdapter::FAssetDocumentPreviewApplyDiffAdapter(FAssetDocumentPreviewApplyDiffHooks InHooks)
	: Hooks(MoveTemp(InHooks))
{
}

TSharedRef<FJsonObject> FAssetDocumentPreviewApplyDiffAdapter::MakeBodyObjectForDiff(const TSharedRef<FJsonObject>& BodyObject)
{
	TSharedRef<FJsonObject> DiffBody = CloneJsonObject(BodyObject);
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

	const TSharedRef<FJsonObject> DesiredBodyForValidation = MakeBodyObjectForDiff(DesiredBody.ToSharedRef());
	const TSharedRef<FJsonObject> DesiredBodyForApply = MakeBodyObjectForDiff(DesiredBody.ToSharedRef());
	const TSharedRef<FJsonObject> DesiredBodyForTraversal = MakeBodyObjectForDiff(DesiredBody.ToSharedRef());
	const TSharedRef<FJsonValue> DesiredJsonForApply = MakeShared<FJsonValueObject>(DesiredBodyForApply);

	if (!Hooks.ValidateDesiredBody)
	{
		return InvalidHookFailure(TEXT("ValidateDesiredBody"));
	}
	FAssetDocumentCapabilityResult Result = Hooks.ValidateDesiredBody(Context, DesiredBodyForValidation);
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
	if (!PreviewContext.Asset || PreviewContext.Asset != PreviewAsset || !PreviewContext.bIsDryRun)
	{
		return InvalidPreviewContextFailure();
	}

	if (!Hooks.ApplyDesiredBody)
	{
		return InvalidHookFailure(TEXT("ApplyDesiredBody"));
	}
	Result = Hooks.ApplyDesiredBody(PreviewContext, DesiredJsonForApply);
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

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : DesiredBodyForTraversal->Values)
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
