// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentWidgetBlueprintRegionWrappers.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "Regions/AssetDocumentGraphRegionWrapperAdapter.h"
#include "Profiles/WidgetBlueprintAnimationAdapter.h"
#include "Profiles/WidgetBlueprintBindingAdapter.h"
#include "Profiles/WidgetBlueprintGraphAdapter.h"
#include "Profiles/WidgetBlueprintTreeAdapter.h"

#include "WidgetBlueprint.h"

namespace
{
TSharedRef<FJsonObject> MakeSchemaHint(FName AdapterName)
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Adapter"), AdapterName.ToString());
	return Schema;
}

bool SupportsBodyKey(const FAssetDocumentRegionContext& Context, const TCHAR* BodyKey)
{
	const FString RegionId = FString::Printf(TEXT("Body.%s"), BodyKey);
	const FString JsonPointer = FAssetDocumentJsonRegionUtils::MakeBodyPath(BodyKey);
	return Context.RegionId == FName(*RegionId)
		|| Context.BodyPath == RegionId
		|| Context.JsonPointer == JsonPointer;
}

FString WidgetWrapperRegionPath(const FAssetDocumentRegionContext& Context)
{
	return Context.JsonPointer.IsEmpty() ? Context.BodyPath : Context.JsonPointer;
}

void AddCanonicalDiffEntry(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& CurrentValue,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
{
	const FString Status = FAssetDocumentJsonRegionUtils::JsonValueToComparableString(CurrentValue) ==
		FAssetDocumentJsonRegionUtils::JsonValueToComparableString(DesiredValue)
		? TEXT("unchanged")
		: TEXT("changed");
	FAssetDocumentJsonRegionUtils::AddDiffEntry(
		OutDiffEntries,
		WidgetWrapperRegionPath(Context),
		Status,
		CurrentValue,
		DesiredValue);
}

FAssetDocumentGraphRegionWrapperConfig MakeWidgetBlueprintGraphWrapperConfig()
{
	FAssetDocumentGraphRegionWrapperConfig Config;
	Config.AdapterName = FWidgetBlueprintGraphRegionAdapter::AdapterName();
	Config.RegionId = TEXT("Body.WidgetBlueprintGraphRegions");
	Config.BodyPath = TEXT("Body.WidgetBlueprintGraphRegions");
	Config.JsonPointer = TEXT("/Body");
	Config.SchemaLabel = TEXT("WidgetBlueprintGraphRegions");
	return Config;
}

FAssetDocumentGraphRegionWrapperHooks MakeWidgetBlueprintGraphWrapperHooks(
	UBlueprint* DesiredStateBlueprint,
	const FWidgetBlueprintRegionAdapterHooks& Hooks,
	const FAssetDocumentRegionContext* OriginalContext,
	FAssetDocumentRegionContext* MutableOriginalContext)
{
	FAssetDocumentGraphRegionWrapperHooks GraphHooks;
	GraphHooks.Validate = [&Hooks, OriginalContext](
		const FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& BodyObject)
	{
		if (Hooks.Validate)
		{
			return Hooks.Validate(*OriginalContext, MakeShared<FJsonValueObject>(BodyObject));
		}
		return FWidgetBlueprintGraphAdapter().ValidateRegions(Context, BodyObject);
	};
	GraphHooks.Preflight = [DesiredStateBlueprint, &Hooks, MutableOriginalContext](
		FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& BodyObject)
	{
		if (Hooks.Preflight)
		{
			return Hooks.Preflight(*MutableOriginalContext, MakeShared<FJsonValueObject>(BodyObject));
		}
		return FWidgetBlueprintGraphAdapter().PreflightRegions(
			Context,
			BodyObject,
			DesiredStateBlueprint ? DesiredStateBlueprint : Cast<UBlueprint>(Context.Asset));
	};
	GraphHooks.Apply = [&Hooks, MutableOriginalContext](
		FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& BodyObject,
		bool& bOutChanged)
	{
		if (Hooks.Apply)
		{
			return Hooks.Apply(*MutableOriginalContext, MakeShared<FJsonValueObject>(BodyObject), bOutChanged);
		}
		return FWidgetBlueprintGraphAdapter().ApplyRegions(Context, BodyObject, bOutChanged);
	};
	GraphHooks.Extract = [&Hooks, OriginalContext](
		const FAssetDocumentCapabilityContext& Context,
		TSharedRef<FJsonObject>& OutBodyObject)
	{
		if (Hooks.Extract)
		{
			TSharedPtr<FJsonValue> ExtractedValue;
			const FAssetDocumentCapabilityResult Result =
				Hooks.Extract(*OriginalContext, ExtractedValue);
			if (!Result.bSuccess)
			{
				return Result;
			}
			const TSharedPtr<FJsonObject> ExtractedObject = ExtractedValue.IsValid() && ExtractedValue->Type == EJson::Object
				? ExtractedValue->AsObject()
				: nullptr;
			if (!ExtractedValue.IsValid() || ExtractedValue->Type != EJson::Object || !ExtractedObject.IsValid())
			{
				return FAssetDocumentCapabilityResult::Failure(
					TEXT("Custom WidgetBlueprint graph extract hook must return an object"),
					TEXT("/Body"),
					TEXT("InvalidGraphRegionHookResult"));
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ExtractedObject->Values)
			{
				OutBodyObject->SetField(Pair.Key, Pair.Value);
			}
			return Result;
		}
		return FWidgetBlueprintGraphAdapter().ExtractRegions(Context, OutBodyObject);
	};
	GraphHooks.Diff = [&Hooks, OriginalContext](
		const FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& BodyObject,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
	{
		if (Hooks.Diff)
		{
			return Hooks.Diff(
				*OriginalContext,
				MakeShared<FJsonValueObject>(BodyObject),
				OutDiffEntries);
		}
		return FWidgetBlueprintGraphAdapter().DiffRegions(Context, BodyObject, OutDiffEntries);
	};
	return GraphHooks;
}

FAssetDocumentGraphRegionWrapperAdapter MakeWidgetBlueprintGraphWrapperAdapter(
	UBlueprint* DesiredStateBlueprint,
	const FWidgetBlueprintRegionAdapterHooks& Hooks,
	const FAssetDocumentRegionContext* OriginalContext,
	FAssetDocumentRegionContext* MutableOriginalContext = nullptr)
{
	return FAssetDocumentGraphRegionWrapperAdapter(
		MakeWidgetBlueprintGraphWrapperConfig(),
		MakeWidgetBlueprintGraphWrapperHooks(DesiredStateBlueprint, Hooks, OriginalContext, MutableOriginalContext));
}
}

FWidgetBlueprintTreeRegionAdapter::FWidgetBlueprintTreeRegionAdapter(FWidgetBlueprintRegionAdapterHooks InHooks)
	: Hooks(MoveTemp(InHooks))
{
}

FName FWidgetBlueprintTreeRegionAdapter::AdapterName()
{
	return TEXT("WidgetBlueprintTree");
}

FName FWidgetBlueprintTreeRegionAdapter::GetName() const
{
	return AdapterName();
}

bool FWidgetBlueprintTreeRegionAdapter::SupportsRegion(const FAssetDocumentRegionContext& Context) const
{
	return SupportsBodyKey(Context, TEXT("WidgetTree"));
}

TSharedRef<FJsonObject> FWidgetBlueprintTreeRegionAdapter::GetSchemaHint(const FAssetDocumentRegionContext&) const
{
	return MakeSchemaHint(GetName());
}

FAssetDocumentCapabilityResult FWidgetBlueprintTreeRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	return Hooks.Validate ? Hooks.Validate(Context, DesiredValue) : FWidgetBlueprintTreeAdapter::Validate(DesiredValue);
}

FAssetDocumentCapabilityResult FWidgetBlueprintTreeRegionAdapter::PreflightRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	return Hooks.Preflight
		? Hooks.Preflight(Context, DesiredValue)
		: FWidgetBlueprintTreeAdapter::Preflight(Cast<UWidgetBlueprint>(Context.Asset), DesiredValue);
}

FAssetDocumentCapabilityResult FWidgetBlueprintTreeRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	if (Hooks.Apply)
	{
		return Hooks.Apply(Context, DesiredValue, bOutChanged);
	}
	return FWidgetBlueprintTreeAdapter::Apply(Cast<UWidgetBlueprint>(Context.Asset), DesiredValue, &bOutChanged);
}

FAssetDocumentCapabilityResult FWidgetBlueprintTreeRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	if (Hooks.Extract)
	{
		return Hooks.Extract(Context, OutCurrentValue);
	}

	TSharedRef<FJsonObject> WidgetTreeJson = FWidgetBlueprintTreeAdapter::MakeDefaultWidgetTree();
	const FAssetDocumentCapabilityResult Result =
		FWidgetBlueprintTreeAdapter::Extract(Cast<UWidgetBlueprint>(Context.Asset), WidgetTreeJson);
	if (Result.bSuccess)
	{
		OutCurrentValue = MakeShared<FJsonValueObject>(WidgetTreeJson);
	}
	return Result;
}

FAssetDocumentCapabilityResult FWidgetBlueprintTreeRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	return Hooks.Diff
		? Hooks.Diff(Context, DesiredValue, OutDiffEntries)
		: FWidgetBlueprintTreeAdapter::Diff(Cast<UWidgetBlueprint>(Context.Asset), DesiredValue, OutDiffEntries);
}

FWidgetBlueprintBindingRegionAdapter::FWidgetBlueprintBindingRegionAdapter(FWidgetBlueprintRegionAdapterHooks InHooks)
	: Hooks(MoveTemp(InHooks))
{
}

FName FWidgetBlueprintBindingRegionAdapter::AdapterName()
{
	return TEXT("WidgetBlueprintBindings");
}

FName FWidgetBlueprintBindingRegionAdapter::GetName() const
{
	return AdapterName();
}

bool FWidgetBlueprintBindingRegionAdapter::SupportsRegion(const FAssetDocumentRegionContext& Context) const
{
	return SupportsBodyKey(Context, TEXT("Bindings"));
}

TSharedRef<FJsonObject> FWidgetBlueprintBindingRegionAdapter::GetSchemaHint(const FAssetDocumentRegionContext&) const
{
	return MakeSchemaHint(GetName());
}

FAssetDocumentCapabilityResult FWidgetBlueprintBindingRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	return Hooks.Validate ? Hooks.Validate(Context, DesiredValue) : FWidgetBlueprintBindingAdapter::Validate(DesiredValue);
}

FAssetDocumentCapabilityResult FWidgetBlueprintBindingRegionAdapter::PreflightRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	return Hooks.Preflight
		? Hooks.Preflight(Context, DesiredValue)
		: FWidgetBlueprintBindingAdapter::Preflight(Cast<UWidgetBlueprint>(Context.Asset), DesiredValue);
}

FAssetDocumentCapabilityResult FWidgetBlueprintBindingRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	if (Hooks.Apply)
	{
		return Hooks.Apply(Context, DesiredValue, bOutChanged);
	}
	return FWidgetBlueprintBindingAdapter::Apply(Cast<UWidgetBlueprint>(Context.Asset), DesiredValue, &bOutChanged);
}

FAssetDocumentCapabilityResult FWidgetBlueprintBindingRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	if (Hooks.Extract)
	{
		return Hooks.Extract(Context, OutCurrentValue);
	}

	TArray<TSharedPtr<FJsonValue>> Bindings;
	const FAssetDocumentCapabilityResult Result =
		FWidgetBlueprintBindingAdapter::Extract(Cast<UWidgetBlueprint>(Context.Asset), Bindings);
	if (Result.bSuccess)
	{
		OutCurrentValue = MakeShared<FJsonValueArray>(MoveTemp(Bindings));
	}
	return Result;
}

FAssetDocumentCapabilityResult FWidgetBlueprintBindingRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	if (Hooks.Diff)
	{
		return Hooks.Diff(Context, DesiredValue, OutDiffEntries);
	}

	TSharedPtr<FJsonValue> CurrentValue;
	FAssetDocumentCapabilityResult Result = ExtractRegion(Context, CurrentValue);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TSharedPtr<FJsonValue> CanonicalDesiredValue;
	Result = FWidgetBlueprintBindingAdapter::CanonicalizeDesired(DesiredValue, CanonicalDesiredValue);
	if (!Result.bSuccess)
	{
		return Result;
	}

	AddCanonicalDiffEntry(Context, CurrentValue, CanonicalDesiredValue, OutDiffEntries);
	return FAssetDocumentCapabilityResult::Success(TEXT("Diffed WidgetBlueprint Bindings"));
}

FWidgetBlueprintAnimationRegionAdapter::FWidgetBlueprintAnimationRegionAdapter(FWidgetBlueprintRegionAdapterHooks InHooks)
	: Hooks(MoveTemp(InHooks))
{
}

FName FWidgetBlueprintAnimationRegionAdapter::AdapterName()
{
	return TEXT("WidgetBlueprintAnimations");
}

FName FWidgetBlueprintAnimationRegionAdapter::GetName() const
{
	return AdapterName();
}

bool FWidgetBlueprintAnimationRegionAdapter::SupportsRegion(const FAssetDocumentRegionContext& Context) const
{
	return SupportsBodyKey(Context, TEXT("Animations"));
}

TSharedRef<FJsonObject> FWidgetBlueprintAnimationRegionAdapter::GetSchemaHint(const FAssetDocumentRegionContext&) const
{
	return MakeSchemaHint(GetName());
}

FAssetDocumentCapabilityResult FWidgetBlueprintAnimationRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	return Hooks.Validate ? Hooks.Validate(Context, DesiredValue) : FWidgetBlueprintAnimationAdapter::Validate(DesiredValue);
}

FAssetDocumentCapabilityResult FWidgetBlueprintAnimationRegionAdapter::PreflightRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	return Hooks.Preflight
		? Hooks.Preflight(Context, DesiredValue)
		: FWidgetBlueprintAnimationAdapter::Preflight(Cast<UWidgetBlueprint>(Context.Asset), DesiredValue);
}

FAssetDocumentCapabilityResult FWidgetBlueprintAnimationRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	if (Hooks.Apply)
	{
		return Hooks.Apply(Context, DesiredValue, bOutChanged);
	}
	return FWidgetBlueprintAnimationAdapter::Apply(Cast<UWidgetBlueprint>(Context.Asset), DesiredValue, &bOutChanged);
}

FAssetDocumentCapabilityResult FWidgetBlueprintAnimationRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	if (Hooks.Extract)
	{
		return Hooks.Extract(Context, OutCurrentValue);
	}

	TArray<TSharedPtr<FJsonValue>> Animations;
	const FAssetDocumentCapabilityResult Result =
		FWidgetBlueprintAnimationAdapter::Extract(Cast<UWidgetBlueprint>(Context.Asset), Animations);
	if (Result.bSuccess)
	{
		OutCurrentValue = MakeShared<FJsonValueArray>(MoveTemp(Animations));
	}
	return Result;
}

FAssetDocumentCapabilityResult FWidgetBlueprintAnimationRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	return Hooks.Diff
		? Hooks.Diff(Context, DesiredValue, OutDiffEntries)
		: FWidgetBlueprintAnimationAdapter::Diff(Cast<UWidgetBlueprint>(Context.Asset), DesiredValue, OutDiffEntries);
}

FWidgetBlueprintGraphRegionAdapter::FWidgetBlueprintGraphRegionAdapter(FWidgetBlueprintRegionAdapterHooks InHooks)
	: Hooks(MoveTemp(InHooks))
{
}

FWidgetBlueprintGraphRegionAdapter::FWidgetBlueprintGraphRegionAdapter(
	UBlueprint* InDesiredStateBlueprint,
	FWidgetBlueprintRegionAdapterHooks InHooks)
	: Hooks(MoveTemp(InHooks))
	, DesiredStateBlueprint(InDesiredStateBlueprint)
{
}

FName FWidgetBlueprintGraphRegionAdapter::AdapterName()
{
	return TEXT("WidgetBlueprintGraph");
}

FName FWidgetBlueprintGraphRegionAdapter::GetName() const
{
	return AdapterName();
}

bool FWidgetBlueprintGraphRegionAdapter::SupportsRegion(const FAssetDocumentRegionContext& Context) const
{
	return MakeWidgetBlueprintGraphWrapperAdapter(DesiredStateBlueprint, Hooks, &Context).SupportsRegion(Context);
}

TSharedRef<FJsonObject> FWidgetBlueprintGraphRegionAdapter::GetSchemaHint(
	const FAssetDocumentRegionContext& Context) const
{
	return MakeWidgetBlueprintGraphWrapperAdapter(DesiredStateBlueprint, Hooks, &Context).GetSchemaHint(Context);
}

FAssetDocumentCapabilityResult FWidgetBlueprintGraphRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	return MakeWidgetBlueprintGraphWrapperAdapter(DesiredStateBlueprint, Hooks, &Context).ValidateRegion(Context, DesiredValue);
}

FAssetDocumentCapabilityResult FWidgetBlueprintGraphRegionAdapter::PreflightRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	return MakeWidgetBlueprintGraphWrapperAdapter(DesiredStateBlueprint, Hooks, &Context, &Context).PreflightRegion(
		Context,
		DesiredValue);
}

FAssetDocumentCapabilityResult FWidgetBlueprintGraphRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	return MakeWidgetBlueprintGraphWrapperAdapter(DesiredStateBlueprint, Hooks, &Context, &Context).ApplyRegion(
		Context,
		DesiredValue,
		bOutChanged);
}

FAssetDocumentCapabilityResult FWidgetBlueprintGraphRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	return MakeWidgetBlueprintGraphWrapperAdapter(DesiredStateBlueprint, Hooks, &Context).ExtractRegion(
		Context,
		OutCurrentValue);
}

FAssetDocumentCapabilityResult FWidgetBlueprintGraphRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	return MakeWidgetBlueprintGraphWrapperAdapter(DesiredStateBlueprint, Hooks, &Context).DiffRegion(
		Context,
		DesiredValue,
		OutDiffEntries);
}
