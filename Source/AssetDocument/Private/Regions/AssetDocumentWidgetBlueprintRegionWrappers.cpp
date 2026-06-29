// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentWidgetBlueprintRegionWrappers.h"

#include "AssetDocumentJsonRegionUtils.h"
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

FString RegionPath(const FAssetDocumentRegionContext& Context)
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
		RegionPath(Context),
		Status,
		CurrentValue,
		DesiredValue);
}

FAssetDocumentCapabilityResult RequireGraphBodyObject(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TSharedPtr<FJsonObject>& OutBodyObject)
{
	return FAssetDocumentJsonRegionUtils::RequireObjectValue(DesiredValue, RegionPath(Context), OutBodyObject);
}

FAssetDocumentCapabilityContext ToCapabilityContext(const FAssetDocumentRegionContext& Context)
{
	FAssetDocumentCapabilityContext CapabilityContext;
	CapabilityContext.Asset = Context.Asset;
	CapabilityContext.AssetClass = Context.AssetClass;
	CapabilityContext.TargetAssetPath = Context.TargetAssetPath;
	CapabilityContext.SourceDocumentPath = Context.SourceDocumentPath;
	CapabilityContext.Definitions = Context.Definitions;
	CapabilityContext.bIsDryRun = Context.bIsDryRun;
	CapabilityContext.Result = Context.Result;
	return CapabilityContext;
}

bool SupportsGraphBodyRegion(const FAssetDocumentRegionContext& Context)
{
	return Context.RegionId == TEXT("Body.WidgetBlueprintGraphRegions")
		|| Context.BodyPath == TEXT("Body.WidgetBlueprintGraphRegions")
		|| Context.JsonPointer == TEXT("/Body")
		|| SupportsBodyKey(Context, TEXT("UbergraphPages"))
		|| SupportsBodyKey(Context, TEXT("FunctionGraphs"))
		|| SupportsBodyKey(Context, TEXT("MacroGraphs"));
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
	return SupportsGraphBodyRegion(Context);
}

TSharedRef<FJsonObject> FWidgetBlueprintGraphRegionAdapter::GetSchemaHint(const FAssetDocumentRegionContext&) const
{
	return MakeSchemaHint(GetName());
}

FAssetDocumentCapabilityResult FWidgetBlueprintGraphRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	if (Hooks.Validate)
	{
		return Hooks.Validate(Context, DesiredValue);
	}

	TSharedPtr<FJsonObject> BodyObject;
	const FAssetDocumentCapabilityResult ObjectResult = RequireGraphBodyObject(Context, DesiredValue, BodyObject);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}
	return FWidgetBlueprintGraphAdapter().ValidateRegions(ToCapabilityContext(Context), BodyObject.ToSharedRef());
}

FAssetDocumentCapabilityResult FWidgetBlueprintGraphRegionAdapter::PreflightRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	if (Hooks.Preflight)
	{
		return Hooks.Preflight(Context, DesiredValue);
	}

	TSharedPtr<FJsonObject> BodyObject;
	const FAssetDocumentCapabilityResult ObjectResult = RequireGraphBodyObject(Context, DesiredValue, BodyObject);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}
	FAssetDocumentCapabilityContext CapabilityContext = ToCapabilityContext(Context);
	return FWidgetBlueprintGraphAdapter().PreflightRegions(CapabilityContext, BodyObject.ToSharedRef());
}

FAssetDocumentCapabilityResult FWidgetBlueprintGraphRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	if (Hooks.Apply)
	{
		return Hooks.Apply(Context, DesiredValue, bOutChanged);
	}

	TSharedPtr<FJsonObject> BodyObject;
	const FAssetDocumentCapabilityResult ObjectResult = RequireGraphBodyObject(Context, DesiredValue, BodyObject);
	if (!ObjectResult.bSuccess)
	{
		bOutChanged = false;
		return ObjectResult;
	}
	FAssetDocumentCapabilityContext CapabilityContext = ToCapabilityContext(Context);
	return FWidgetBlueprintGraphAdapter().ApplyRegions(CapabilityContext, BodyObject.ToSharedRef(), bOutChanged);
}

FAssetDocumentCapabilityResult FWidgetBlueprintGraphRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	if (Hooks.Extract)
	{
		return Hooks.Extract(Context, OutCurrentValue);
	}

	TSharedRef<FJsonObject> BodyObject = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult Result =
		FWidgetBlueprintGraphAdapter().ExtractRegions(ToCapabilityContext(Context), BodyObject);
	if (Result.bSuccess)
	{
		OutCurrentValue = MakeShared<FJsonValueObject>(BodyObject);
	}
	return Result;
}

FAssetDocumentCapabilityResult FWidgetBlueprintGraphRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	if (Hooks.Diff)
	{
		return Hooks.Diff(Context, DesiredValue, OutDiffEntries);
	}

	TSharedPtr<FJsonObject> BodyObject;
	const FAssetDocumentCapabilityResult ObjectResult = RequireGraphBodyObject(Context, DesiredValue, BodyObject);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}
	return FWidgetBlueprintGraphAdapter().DiffRegions(ToCapabilityContext(Context), BodyObject.ToSharedRef(), OutDiffEntries);
}
