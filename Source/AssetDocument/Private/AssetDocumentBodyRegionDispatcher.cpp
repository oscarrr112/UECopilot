// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentBodyRegionDispatcher.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "AssetDocumentRegionRuntime.h"

namespace
{
FString MakeRegionBodyPath(const FAssetDocumentRegionBinding& Binding)
{
	return FString::Printf(TEXT("Body.%s"), *Binding.BodyKey.ToString());
}

FString MakeRegionJsonPointer(const FAssetDocumentRegionBinding& Binding)
{
	return FAssetDocumentJsonRegionUtils::MakeBodyPath(Binding.BodyKey.ToString());
}

FAssetDocumentRegionContext MakeRegionContext(
	const FAssetDocumentCapabilityContext& CapabilityContext,
	const FAssetDocumentRegionBinding& Binding,
	const FAssetDocumentRegionPolicy& Policy)
{
	FAssetDocumentRegionContext Context;
	Context.Asset = CapabilityContext.Asset;
	Context.AssetClass = CapabilityContext.AssetClass;
	Context.TargetAssetPath = CapabilityContext.TargetAssetPath;
	Context.SourceDocumentPath = CapabilityContext.SourceDocumentPath;
	Context.Definitions = CapabilityContext.Definitions;
	Context.Result = CapabilityContext.Result;
	Context.bIsDryRun = CapabilityContext.bIsDryRun;
	Context.Policy = &Policy;
	Context.RegionId = Binding.RegionId;
	Context.BodyPath = Policy.BodyPath.IsEmpty() ? MakeRegionBodyPath(Binding) : Policy.BodyPath;
	Context.JsonPointer = MakeRegionJsonPointer(Binding);
	return Context;
}

FAssetDocumentCapabilityResult RequireBodyObject(
	const TSharedRef<FJsonValue>& BodyJson,
	TSharedPtr<FJsonObject>& OutBodyObject)
{
	return FAssetDocumentJsonRegionUtils::RequireObjectValue(BodyJson, TEXT("/Body"), OutBodyObject);
}

FAssetDocumentCapabilityResult FailureForBinding(
	const FAssetDocumentRegionBinding& Binding,
	const FString& Code,
	const FString& Message)
{
	return FAssetDocumentJsonRegionUtils::Failure(MakeRegionJsonPointer(Binding), Code, Message);
}

FAssetDocumentCapabilityResult ValidateKnownAndRequiredKeys(
	const TSharedRef<FJsonObject>& BodyObject,
	const TMap<FName, FAssetDocumentRegionBinding>& BindingsByBodyKey)
{
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BodyObject->Values)
	{
		if (!BindingsByBodyKey.Contains(FName(*Pair.Key)))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				FAssetDocumentJsonRegionUtils::MakeBodyPath(Pair.Key),
				TEXT("UnknownBodyRegion"),
				FString::Printf(TEXT("Unknown Body region %s"), *Pair.Key));
		}
	}

	for (const TPair<FName, FAssetDocumentRegionBinding>& Pair : BindingsByBodyKey)
	{
		const FAssetDocumentRegionBinding& Binding = Pair.Value;
		if (Binding.bRequired && !BodyObject->HasField(Binding.BodyKey.ToString()))
		{
			return FailureForBinding(
				Binding,
				TEXT("MissingRequiredBodyRegion"),
				FString::Printf(TEXT("Missing required Body region %s"), *Binding.BodyKey.ToString()));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

TArray<FAssetDocumentRegionBinding> SortBindingsByApplyOrder(TArray<FAssetDocumentRegionBinding> Bindings)
{
	Bindings.Sort(
		[](const FAssetDocumentRegionBinding& Left, const FAssetDocumentRegionBinding& Right)
		{
			if (Left.ApplyOrder != Right.ApplyOrder)
			{
				return Left.ApplyOrder < Right.ApplyOrder;
			}
			return Left.BodyKey.LexicalLess(Right.BodyKey);
		});
	return Bindings;
}

TArray<FAssetDocumentRegionBinding> GetPresentBindingsInApplyOrder(
	const TSharedRef<FJsonObject>& BodyObject,
	const TArray<FAssetDocumentRegionBinding>& RegionBindings)
{
	TArray<FAssetDocumentRegionBinding> PresentBindings;
	for (const FAssetDocumentRegionBinding& Binding : RegionBindings)
	{
		if (BodyObject->HasField(Binding.BodyKey.ToString()))
		{
			PresentBindings.Add(Binding);
		}
	}

	PresentBindings = SortBindingsByApplyOrder(MoveTemp(PresentBindings));
	return PresentBindings;
}
}

FAssetDocumentBodyRegionDispatcher::FAssetDocumentBodyRegionDispatcher(
	TArray<FAssetDocumentRegionBinding> InRegionBindings,
	TArray<FAssetDocumentRegionPolicy> InRegionPolicies,
	TMap<FName, IAssetDocumentRegionAdapter*> InAdapters,
	FAssetDocumentBodyRegionDispatcherHooks InHooks)
	: RegionBindings(MoveTemp(InRegionBindings))
	, AdaptersByName(MoveTemp(InAdapters))
	, Hooks(MoveTemp(InHooks))
{
	for (const FAssetDocumentRegionBinding& Binding : RegionBindings)
	{
		BindingsByBodyKey.Add(Binding.BodyKey, Binding);
	}

	for (const FAssetDocumentRegionPolicy& Policy : InRegionPolicies)
	{
		PoliciesByRegionId.Add(Policy.RegionId, Policy);
	}
}

FAssetDocumentCapabilityResult FAssetDocumentBodyRegionDispatcher::ValidateBody(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonValue>& BodyJson) const
{
	TSharedPtr<FJsonObject> BodyObject;
	FAssetDocumentCapabilityResult Result = RequireBodyObject(BodyJson, BodyObject);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ValidateKnownAndRequiredKeys(BodyObject.ToSharedRef(), BindingsByBodyKey);
	if (!Result.bSuccess)
	{
		return Result;
	}

	for (const FAssetDocumentRegionBinding& Binding : GetPresentBindingsInApplyOrder(BodyObject.ToSharedRef(), RegionBindings))
	{
		const FAssetDocumentRegionPolicy* Policy = PoliciesByRegionId.Find(Binding.RegionId);
		if (!Policy)
		{
			return FailureForBinding(
				Binding,
				TEXT("MissingRegionPolicy"),
				FString::Printf(TEXT("Missing policy for Body region %s"), *Binding.BodyKey.ToString()));
		}

		IAssetDocumentRegionAdapter* const* Adapter = AdaptersByName.Find(Binding.AdapterName);
		if (!Adapter || !*Adapter)
		{
			return FailureForBinding(
				Binding,
				TEXT("MissingRegionAdapter"),
				FString::Printf(TEXT("Missing adapter %s for Body region %s"), *Binding.AdapterName.ToString(), *Binding.BodyKey.ToString()));
		}

		const FAssetDocumentRegionContext RegionContext = MakeRegionContext(Context, Binding, *Policy);
		Result = FAssetDocumentRegionRuntime::Validate(RegionContext, BodyObject->TryGetField(Binding.BodyKey.ToString()), **Adapter);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	if (Hooks.ValidateCrossRegion)
	{
		return Hooks.ValidateCrossRegion(Context, BodyObject.ToSharedRef());
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated Body regions"));
}

FAssetDocumentCapabilityResult FAssetDocumentBodyRegionDispatcher::PreflightBody(
	FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonValue>& BodyJson) const
{
	TSharedPtr<FJsonObject> BodyObject;
	FAssetDocumentCapabilityResult Result = RequireBodyObject(BodyJson, BodyObject);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ValidateKnownAndRequiredKeys(BodyObject.ToSharedRef(), BindingsByBodyKey);
	if (!Result.bSuccess)
	{
		return Result;
	}

	for (const FAssetDocumentRegionBinding& Binding : GetPresentBindingsInApplyOrder(BodyObject.ToSharedRef(), RegionBindings))
	{
		const FAssetDocumentRegionPolicy* Policy = PoliciesByRegionId.Find(Binding.RegionId);
		if (!Policy)
		{
			return FailureForBinding(
				Binding,
				TEXT("MissingRegionPolicy"),
				FString::Printf(TEXT("Missing policy for Body region %s"), *Binding.BodyKey.ToString()));
		}

		IAssetDocumentRegionAdapter* const* Adapter = AdaptersByName.Find(Binding.AdapterName);
		if (!Adapter || !*Adapter)
		{
			return FailureForBinding(
				Binding,
				TEXT("MissingRegionAdapter"),
				FString::Printf(TEXT("Missing adapter %s for Body region %s"), *Binding.AdapterName.ToString(), *Binding.BodyKey.ToString()));
		}

		FAssetDocumentRegionContext RegionContext = MakeRegionContext(Context, Binding, *Policy);
		Result = FAssetDocumentRegionRuntime::Preflight(RegionContext, BodyObject->TryGetField(Binding.BodyKey.ToString()), **Adapter);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	if (Hooks.ValidateCrossRegion)
	{
		return Hooks.ValidateCrossRegion(Context, BodyObject.ToSharedRef());
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Preflighted Body regions"));
}

FAssetDocumentCapabilityResult FAssetDocumentBodyRegionDispatcher::ApplyBody(
	FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonValue>& BodyJson,
	TSet<FName>& OutAppliedRegions) const
{
	OutAppliedRegions.Reset();

	const FAssetDocumentCapabilityResult PreflightResult = PreflightBody(Context, BodyJson);
	if (!PreflightResult.bSuccess)
	{
		return PreflightResult;
	}

	TSharedPtr<FJsonObject> BodyObject;
	FAssetDocumentCapabilityResult Result = RequireBodyObject(BodyJson, BodyObject);
	if (!Result.bSuccess)
	{
		return Result;
	}

	for (const FAssetDocumentRegionBinding& Binding : GetPresentBindingsInApplyOrder(BodyObject.ToSharedRef(), RegionBindings))
	{
		const FAssetDocumentRegionPolicy* Policy = PoliciesByRegionId.Find(Binding.RegionId);
		IAssetDocumentRegionAdapter* const* Adapter = AdaptersByName.Find(Binding.AdapterName);
		if (!Policy || !Adapter || !*Adapter)
		{
			return FailureForBinding(
				Binding,
				!Policy ? TEXT("MissingRegionPolicy") : TEXT("MissingRegionAdapter"),
				FString::Printf(TEXT("Missing runtime configuration for Body region %s"), *Binding.BodyKey.ToString()));
		}

		FAssetDocumentRegionContext RegionContext = MakeRegionContext(Context, Binding, *Policy);
		bool bChanged = false;
		Result = FAssetDocumentRegionRuntime::Apply(RegionContext, BodyObject->TryGetField(Binding.BodyKey.ToString()), **Adapter, bChanged);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (bChanged)
		{
			OutAppliedRegions.Add(Binding.RegionId);
		}
	}

	if (Hooks.PostApplyRepair)
	{
		return Hooks.PostApplyRepair(Context, OutAppliedRegions);
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Applied Body regions"));
}

FAssetDocumentCapabilityResult FAssetDocumentBodyRegionDispatcher::ExtractBody(
	const FAssetDocumentCapabilityContext& Context,
	TSharedRef<FJsonObject>& OutBodyObject) const
{
	OutBodyObject = MakeShared<FJsonObject>();

	for (const FAssetDocumentRegionBinding& Binding : SortBindingsByApplyOrder(RegionBindings))
	{
		const FAssetDocumentRegionPolicy* Policy = PoliciesByRegionId.Find(Binding.RegionId);
		IAssetDocumentRegionAdapter* const* Adapter = AdaptersByName.Find(Binding.AdapterName);
		if (!Policy || !Adapter || !*Adapter)
		{
			return FailureForBinding(
				Binding,
				!Policy ? TEXT("MissingRegionPolicy") : TEXT("MissingRegionAdapter"),
				FString::Printf(TEXT("Missing runtime configuration for Body region %s"), *Binding.BodyKey.ToString()));
		}

		const FAssetDocumentRegionContext RegionContext = MakeRegionContext(Context, Binding, *Policy);
		TSharedPtr<FJsonValue> RegionValue;
		const FAssetDocumentCapabilityResult Result = FAssetDocumentRegionRuntime::Extract(RegionContext, **Adapter, RegionValue);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutBodyObject->SetField(Binding.BodyKey.ToString(), RegionValue.IsValid() ? RegionValue : MakeShared<FJsonValueNull>());
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted Body regions"));
}

FAssetDocumentCapabilityResult FAssetDocumentBodyRegionDispatcher::DiffBody(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonValue>& DesiredBodyJson,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	TSharedPtr<FJsonObject> DesiredBodyObject;
	FAssetDocumentCapabilityResult Result = RequireBodyObject(DesiredBodyJson, DesiredBodyObject);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ValidateKnownAndRequiredKeys(DesiredBodyObject.ToSharedRef(), BindingsByBodyKey);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (Hooks.ValidateCrossRegion)
	{
		Result = Hooks.ValidateCrossRegion(Context, DesiredBodyObject.ToSharedRef());
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	for (const FAssetDocumentRegionBinding& Binding : GetPresentBindingsInApplyOrder(DesiredBodyObject.ToSharedRef(), RegionBindings))
	{
		const FAssetDocumentRegionPolicy* Policy = PoliciesByRegionId.Find(Binding.RegionId);
		IAssetDocumentRegionAdapter* const* Adapter = AdaptersByName.Find(Binding.AdapterName);
		if (!Policy || !Adapter || !*Adapter)
		{
			return FailureForBinding(
				Binding,
				!Policy ? TEXT("MissingRegionPolicy") : TEXT("MissingRegionAdapter"),
				FString::Printf(TEXT("Missing runtime configuration for Body region %s"), *Binding.BodyKey.ToString()));
		}

		const FAssetDocumentRegionContext RegionContext = MakeRegionContext(Context, Binding, *Policy);
		Result = FAssetDocumentRegionRuntime::Diff(RegionContext, DesiredBodyObject->TryGetField(Binding.BodyKey.ToString()), **Adapter, OutDiffEntries);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Diffed Body regions"));
}
