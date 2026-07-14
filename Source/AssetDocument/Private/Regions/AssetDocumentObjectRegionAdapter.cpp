// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentObjectRegionAdapter.h"

#include "AssetDocumentJsonRegionUtils.h"

namespace
{
FString ObjectAdapterRegionPath(const FAssetDocumentRegionContext& Context)
{
	return Context.JsonPointer.IsEmpty() ? Context.BodyPath : Context.JsonPointer;
}

bool ObjectAdapterIsObjectRegion(const FAssetDocumentRegionContext& Context)
{
	return !Context.Policy || Context.Policy->RegionKind == EAssetDocumentRegionKind::Object;
}

FAssetDocumentCapabilityResult RequireObjectRegionValue(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& Value,
	TSharedPtr<FJsonObject>& OutObject)
{
	return FAssetDocumentJsonRegionUtils::RequireObjectValue(Value, ObjectAdapterRegionPath(Context), OutObject);
}

FAssetDocumentCapabilityResult ObjectAdapterMissingHookFailure(
	const FAssetDocumentRegionContext& Context,
	const FString& Code,
	const FString& Operation)
{
	return FAssetDocumentJsonRegionUtils::Failure(
		ObjectAdapterRegionPath(Context),
		Code,
		FString::Printf(TEXT("Object region %s requires an explicit %s hook"), *Context.BodyPath, *Operation));
}
}

FAssetDocumentObjectRegionAdapter::FAssetDocumentObjectRegionAdapter(
	FName InName,
	FAssetDocumentObjectRegionAdapterHooks InHooks)
	: Name(InName)
	, Hooks(MoveTemp(InHooks))
{
}

FName FAssetDocumentObjectRegionAdapter::DefaultAdapterName()
{
	return TEXT("AssetDocumentObjectRegionAdapter");
}

FName FAssetDocumentObjectRegionAdapter::GetName() const
{
	return Name;
}

bool FAssetDocumentObjectRegionAdapter::SupportsRegion(const FAssetDocumentRegionContext& Context) const
{
	return ObjectAdapterIsObjectRegion(Context);
}

TSharedRef<FJsonObject> FAssetDocumentObjectRegionAdapter::GetSchemaHint(const FAssetDocumentRegionContext& Context) const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Adapter"), GetName().ToString());
	Schema->SetStringField(TEXT("Shape"), TEXT("object"));
	Schema->SetStringField(TEXT("BodyPath"), Context.BodyPath);
	return Schema;
}

FAssetDocumentCapabilityResult FAssetDocumentObjectRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	TSharedPtr<FJsonObject> DesiredObject;
	FAssetDocumentCapabilityResult Result = RequireObjectRegionValue(Context, DesiredValue, DesiredObject);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (Hooks.ValidateObject)
	{
		return Hooks.ValidateObject(Context, DesiredObject.ToSharedRef());
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated object region"));
}

FAssetDocumentCapabilityResult FAssetDocumentObjectRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	bOutChanged = false;

	TSharedPtr<FJsonObject> DesiredObject;
	FAssetDocumentCapabilityResult Result = RequireObjectRegionValue(Context, DesiredValue, DesiredObject);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (Hooks.ValidateObject)
	{
		Result = Hooks.ValidateObject(Context, DesiredObject.ToSharedRef());
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	if (Hooks.ApplyObject)
	{
		return Hooks.ApplyObject(Context, DesiredObject.ToSharedRef(), bOutChanged);
	}

	return ObjectAdapterMissingHookFailure(Context, TEXT("MissingRegionApplyHook"), TEXT("apply"));
}

FAssetDocumentCapabilityResult FAssetDocumentObjectRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	OutCurrentValue.Reset();
	if (!Hooks.ExtractObject)
	{
		return ObjectAdapterMissingHookFailure(Context, TEXT("MissingRegionExtractHook"), TEXT("extract"));
	}

	TSharedRef<FJsonObject> CurrentObject = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult Result = Hooks.ExtractObject(Context, CurrentObject);
	if (!Result.bSuccess)
	{
		return Result;
	}

	OutCurrentValue = MakeShared<FJsonValueObject>(CurrentObject);
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted object region"));
}

FAssetDocumentCapabilityResult FAssetDocumentObjectRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	TSharedPtr<FJsonObject> DesiredObject;
	FAssetDocumentCapabilityResult Result = RequireObjectRegionValue(Context, DesiredValue, DesiredObject);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (Hooks.ValidateObject)
	{
		Result = Hooks.ValidateObject(Context, DesiredObject.ToSharedRef());
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	if (Hooks.DiffObject)
	{
		return Hooks.DiffObject(Context, DesiredObject.ToSharedRef(), OutDiffEntries);
	}

	return ObjectAdapterMissingHookFailure(Context, TEXT("MissingRegionDiffHook"), TEXT("diff"));
}
