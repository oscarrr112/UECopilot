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

FAssetDocumentCapabilityResult ConfigFailure(const FString& Message, const FString& Path = TEXT("/Body"))
{
	return FAssetDocumentJsonRegionUtils::Failure(Path, TEXT("InvalidRegionDispatcherConfig"), Message);
}

FAssetDocumentCapabilityResult ValidateDispatcherConfig(
	const TArray<FAssetDocumentRegionBinding>& RegionBindings,
	const TArray<FAssetDocumentRegionPolicy>& RegionPolicies,
	const TMap<FName, IAssetDocumentRegionAdapter*>& AdaptersByName)
{
	TSet<FName> BodyKeys;
	TSet<FName> RegionIds;
	for (const FAssetDocumentRegionBinding& Binding : RegionBindings)
	{
		if (Binding.BodyKey.IsNone())
		{
			return ConfigFailure(TEXT("Region binding BodyKey must not be empty"));
		}
		if (BodyKeys.Contains(Binding.BodyKey))
		{
			return ConfigFailure(
				FString::Printf(TEXT("Duplicate BodyKey binding %s"), *Binding.BodyKey.ToString()),
				MakeRegionJsonPointer(Binding));
		}
		BodyKeys.Add(Binding.BodyKey);

		if (Binding.RegionId.IsNone())
		{
			return ConfigFailure(
				FString::Printf(TEXT("Region binding for Body.%s must have a RegionId"), *Binding.BodyKey.ToString()),
				MakeRegionJsonPointer(Binding));
		}
		if (RegionIds.Contains(Binding.RegionId))
		{
			return ConfigFailure(
				FString::Printf(TEXT("Duplicate RegionId binding %s"), *Binding.RegionId.ToString()),
				MakeRegionJsonPointer(Binding));
		}
		RegionIds.Add(Binding.RegionId);

		if (Binding.AdapterName.IsNone())
		{
			return ConfigFailure(
				FString::Printf(TEXT("Region binding for Body.%s must have an AdapterName"), *Binding.BodyKey.ToString()),
				MakeRegionJsonPointer(Binding));
		}
	}

	TSet<FName> PolicyRegionIds;
	for (const FAssetDocumentRegionPolicy& Policy : RegionPolicies)
	{
		if (Policy.RegionId.IsNone())
		{
			return ConfigFailure(TEXT("Region policy RegionId must not be empty"));
		}
		if (PolicyRegionIds.Contains(Policy.RegionId))
		{
			return ConfigFailure(FString::Printf(TEXT("Duplicate policy RegionId %s"), *Policy.RegionId.ToString()));
		}
		PolicyRegionIds.Add(Policy.RegionId);
	}

	for (const TPair<FName, IAssetDocumentRegionAdapter*>& Pair : AdaptersByName)
	{
		if (Pair.Key.IsNone())
		{
			return ConfigFailure(TEXT("Adapter map key must not be empty"));
		}
		if (!Pair.Value)
		{
			return ConfigFailure(FString::Printf(TEXT("Adapter map entry %s must not be null"), *Pair.Key.ToString()));
		}
		if (Pair.Value->GetName() != Pair.Key)
		{
			return ConfigFailure(
				FString::Printf(TEXT("Adapter map key %s does not match adapter name %s"), *Pair.Key.ToString(), *Pair.Value->GetName().ToString()));
		}
	}

	for (const FAssetDocumentRegionBinding& Binding : RegionBindings)
	{
		if (!PolicyRegionIds.Contains(Binding.RegionId))
		{
			return ConfigFailure(
				FString::Printf(TEXT("Missing policy for Body.%s region %s"), *Binding.BodyKey.ToString(), *Binding.RegionId.ToString()),
				MakeRegionJsonPointer(Binding));
		}
		if (!AdaptersByName.Contains(Binding.AdapterName))
		{
			return ConfigFailure(
				FString::Printf(TEXT("Missing adapter %s for Body.%s"), *Binding.AdapterName.ToString(), *Binding.BodyKey.ToString()),
				MakeRegionJsonPointer(Binding));
		}

		const FAssetDocumentRegionPolicy* Policy = RegionPolicies.FindByPredicate(
			[&Binding](const FAssetDocumentRegionPolicy& Candidate)
			{
				return Candidate.RegionId == Binding.RegionId;
			});
		IAssetDocumentRegionAdapter* const* Adapter = AdaptersByName.Find(Binding.AdapterName);
		if (!Policy || !Adapter || !*Adapter)
		{
			return ConfigFailure(
				FString::Printf(TEXT("Missing runtime configuration for Body.%s"), *Binding.BodyKey.ToString()),
				MakeRegionJsonPointer(Binding));
		}

		const FAssetDocumentCapabilityContext EmptyCapabilityContext;
		const FAssetDocumentRegionContext RegionContext = MakeRegionContext(EmptyCapabilityContext, Binding, *Policy);
		if (!(*Adapter)->SupportsRegion(RegionContext))
		{
			return ConfigFailure(
				FString::Printf(
					TEXT("Adapter %s does not support Body.%s region %s"),
					*Binding.AdapterName.ToString(),
					*Binding.BodyKey.ToString(),
					*Binding.RegionId.ToString()),
				MakeRegionJsonPointer(Binding));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
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

FAssetDocumentCapabilityResult ValidatePresentRegions(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& BodyObject,
	const TArray<FAssetDocumentRegionBinding>& RegionBindings,
	const TMap<FName, FAssetDocumentRegionPolicy>& PoliciesByRegionId,
	const TMap<FName, IAssetDocumentRegionAdapter*>& AdaptersByName)
{
	for (const FAssetDocumentRegionBinding& Binding : GetPresentBindingsInApplyOrder(BodyObject, RegionBindings))
	{
		const FAssetDocumentRegionPolicy* Policy = PoliciesByRegionId.Find(Binding.RegionId);
		IAssetDocumentRegionAdapter* const* Adapter = AdaptersByName.Find(Binding.AdapterName);
		if (!Policy || !Adapter || !*Adapter)
		{
			return ConfigFailure(
				FString::Printf(TEXT("Missing runtime configuration for Body region %s"), *Binding.BodyKey.ToString()),
				MakeRegionJsonPointer(Binding));
		}

		const FAssetDocumentRegionContext RegionContext = MakeRegionContext(Context, Binding, *Policy);
		const FAssetDocumentCapabilityResult Result =
			FAssetDocumentRegionRuntime::Validate(RegionContext, BodyObject->TryGetField(Binding.BodyKey.ToString()), **Adapter);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated Body regions"));
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
	, ConfigValidationResult(FAssetDocumentCapabilityResult::Success())
{
	for (const FAssetDocumentRegionBinding& Binding : RegionBindings)
	{
		BindingsByBodyKey.Add(Binding.BodyKey, Binding);
	}

	for (const FAssetDocumentRegionPolicy& Policy : InRegionPolicies)
	{
		PoliciesByRegionId.Add(Policy.RegionId, Policy);
	}

	ConfigValidationResult = ValidateDispatcherConfig(RegionBindings, InRegionPolicies, AdaptersByName);
}

FAssetDocumentCapabilityResult FAssetDocumentBodyRegionDispatcher::ValidateBody(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonValue>& BodyJson) const
{
	if (!ConfigValidationResult.bSuccess)
	{
		return ConfigValidationResult;
	}

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

	Result = ValidatePresentRegions(Context, BodyObject.ToSharedRef(), RegionBindings, PoliciesByRegionId, AdaptersByName);
	if (!Result.bSuccess)
	{
		return Result;
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
	if (!ConfigValidationResult.bSuccess)
	{
		return ConfigValidationResult;
	}

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
	if (!ConfigValidationResult.bSuccess)
	{
		return ConfigValidationResult;
	}

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
	if (!ConfigValidationResult.bSuccess)
	{
		return ConfigValidationResult;
	}

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
	if (!ConfigValidationResult.bSuccess)
	{
		return ConfigValidationResult;
	}

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

	Result = ValidatePresentRegions(Context, DesiredBodyObject.ToSharedRef(), RegionBindings, PoliciesByRegionId, AdaptersByName);
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
