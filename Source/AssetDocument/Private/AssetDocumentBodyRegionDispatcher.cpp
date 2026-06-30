// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentBodyRegionDispatcher.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "AssetDocumentRegionRuntime.h"

namespace
{
FString ExtractExactBodyKey(
	const FAssetDocumentRegionBinding& Binding,
	const FAssetDocumentRegionPolicy* Policy)
{
	if (Policy && Policy->BodyPath.StartsWith(TEXT("Body.")))
	{
		const FString BodyKey = Policy->BodyPath.RightChop(5);
		if (!BodyKey.IsEmpty() && !BodyKey.Contains(TEXT(".")) && !BodyKey.Contains(TEXT("/")))
		{
			return BodyKey;
		}
	}

	return Binding.BodyKey.ToString();
}

FString ResolveExactBodyKey(
	const FAssetDocumentRegionBinding& Binding,
	const TMap<FName, FString>& BodyKeysByRegionId)
{
	if (const FString* ExactBodyKey = BodyKeysByRegionId.Find(Binding.RegionId))
	{
		return *ExactBodyKey;
	}
	return Binding.BodyKey.ToString();
}

FString MakeRegionJsonPointer(const FString& BodyKey)
{
	return FAssetDocumentJsonRegionUtils::MakeBodyPath(BodyKey);
}

bool StringArrayContainsExact(const TArray<FString>& Values, const FString& Candidate)
{
	return Values.ContainsByPredicate([&Candidate](const FString& Value)
	{
		return Value.Equals(Candidate, ESearchCase::CaseSensitive);
	});
}

const FAssetDocumentRegionBinding* FindExactBinding(
	const TMap<FString, FAssetDocumentRegionBinding>& BindingsByBodyKey,
	const FString& BodyKey)
{
	for (const TPair<FString, FAssetDocumentRegionBinding>& Pair : BindingsByBodyKey)
	{
		if (Pair.Key.Equals(BodyKey, ESearchCase::CaseSensitive))
		{
			return &Pair.Value;
		}
	}
	return nullptr;
}

bool TryFindExactBodyValue(
	const TSharedRef<FJsonObject>& BodyObject,
	const FString& BodyKey,
	TSharedPtr<FJsonValue>& OutValue)
{
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BodyObject->Values)
	{
		if (Pair.Key.Equals(BodyKey, ESearchCase::CaseSensitive))
		{
			OutValue = Pair.Value;
			return true;
		}
	}

	OutValue.Reset();
	return false;
}

FAssetDocumentRegionContext MakeRegionContext(
	const FAssetDocumentCapabilityContext& CapabilityContext,
	const FAssetDocumentRegionBinding& Binding,
	const FAssetDocumentRegionPolicy& Policy)
{
	const FString ExactBodyKey = ExtractExactBodyKey(Binding, &Policy);
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
	Context.BodyPath = Policy.BodyPath.IsEmpty() ? FString::Printf(TEXT("Body.%s"), *ExactBodyKey) : Policy.BodyPath;
	Context.JsonPointer = MakeRegionJsonPointer(ExactBodyKey);
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
	const TMap<FName, FString>& BodyKeysByRegionId,
	const FString& Code,
	const FString& Message)
{
	return FAssetDocumentJsonRegionUtils::Failure(
		MakeRegionJsonPointer(ResolveExactBodyKey(Binding, BodyKeysByRegionId)),
		Code,
		Message);
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
	TMap<FName, FAssetDocumentRegionPolicy> PoliciesByRegionId;
	for (const FAssetDocumentRegionPolicy& Policy : RegionPolicies)
	{
		PoliciesByRegionId.Add(Policy.RegionId, Policy);
	}

	TArray<FString> BodyKeys;
	TSet<FName> RegionIds;
	for (const FAssetDocumentRegionBinding& Binding : RegionBindings)
	{
		if (Binding.BodyKey.IsNone())
		{
			return ConfigFailure(TEXT("Region binding BodyKey must not be empty"));
		}
		const FString ExactBodyKey = ExtractExactBodyKey(Binding, PoliciesByRegionId.Find(Binding.RegionId));
		if (StringArrayContainsExact(BodyKeys, ExactBodyKey))
		{
			return ConfigFailure(
				FString::Printf(TEXT("Duplicate BodyKey binding %s"), *Binding.BodyKey.ToString()),
				MakeRegionJsonPointer(ExactBodyKey));
		}
		BodyKeys.Add(ExactBodyKey);

		if (Binding.RegionId.IsNone())
		{
			return ConfigFailure(
				FString::Printf(TEXT("Region binding for Body.%s must have a RegionId"), *Binding.BodyKey.ToString()),
				MakeRegionJsonPointer(ExactBodyKey));
		}
		if (RegionIds.Contains(Binding.RegionId))
		{
			return ConfigFailure(
				FString::Printf(TEXT("Duplicate RegionId binding %s"), *Binding.RegionId.ToString()),
				MakeRegionJsonPointer(ExactBodyKey));
		}
		RegionIds.Add(Binding.RegionId);

		if (Binding.AdapterName.IsNone())
		{
			return ConfigFailure(
				FString::Printf(TEXT("Region binding for Body.%s must have an AdapterName"), *Binding.BodyKey.ToString()),
				MakeRegionJsonPointer(ExactBodyKey));
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
				MakeRegionJsonPointer(ExtractExactBodyKey(Binding, nullptr)));
		}
		if (!AdaptersByName.Contains(Binding.AdapterName))
		{
			return ConfigFailure(
				FString::Printf(TEXT("Missing adapter %s for Body.%s"), *Binding.AdapterName.ToString(), *Binding.BodyKey.ToString()),
				MakeRegionJsonPointer(ExtractExactBodyKey(Binding, PoliciesByRegionId.Find(Binding.RegionId))));
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
				MakeRegionJsonPointer(ExtractExactBodyKey(Binding, Policy)));
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
				MakeRegionJsonPointer(ExtractExactBodyKey(Binding, Policy)));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateKnownAndRequiredKeys(
	const TSharedRef<FJsonObject>& BodyObject,
	const TMap<FString, FAssetDocumentRegionBinding>& BindingsByBodyKey)
{
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BodyObject->Values)
	{
		if (!FindExactBinding(BindingsByBodyKey, Pair.Key))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				FAssetDocumentJsonRegionUtils::MakeBodyPath(Pair.Key),
				TEXT("UnknownBodyRegion"),
				FString::Printf(TEXT("Unknown Body region %s"), *Pair.Key));
		}
	}

	for (const TPair<FString, FAssetDocumentRegionBinding>& Pair : BindingsByBodyKey)
	{
		const FAssetDocumentRegionBinding& Binding = Pair.Value;
		TSharedPtr<FJsonValue> RequiredValue;
		if (Binding.bRequired && !TryFindExactBodyValue(BodyObject, Pair.Key, RequiredValue))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeRegionJsonPointer(Pair.Key),
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
	const TArray<FAssetDocumentRegionBinding>& RegionBindings,
	const TMap<FName, FString>& BodyKeysByRegionId)
{
	TArray<FAssetDocumentRegionBinding> PresentBindings;
	for (const FAssetDocumentRegionBinding& Binding : RegionBindings)
	{
		TSharedPtr<FJsonValue> BodyValue;
		if (TryFindExactBodyValue(BodyObject, ResolveExactBodyKey(Binding, BodyKeysByRegionId), BodyValue))
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
	const TMap<FName, FString>& BodyKeysByRegionId,
	const TMap<FName, IAssetDocumentRegionAdapter*>& AdaptersByName)
{
	for (const FAssetDocumentRegionBinding& Binding : GetPresentBindingsInApplyOrder(BodyObject, RegionBindings, BodyKeysByRegionId))
	{
		const FAssetDocumentRegionPolicy* Policy = PoliciesByRegionId.Find(Binding.RegionId);
		IAssetDocumentRegionAdapter* const* Adapter = AdaptersByName.Find(Binding.AdapterName);
		if (!Policy || !Adapter || !*Adapter)
		{
			return ConfigFailure(
				FString::Printf(TEXT("Missing runtime configuration for Body region %s"), *Binding.BodyKey.ToString()),
				MakeRegionJsonPointer(ResolveExactBodyKey(Binding, BodyKeysByRegionId)));
		}

		const FAssetDocumentRegionContext RegionContext = MakeRegionContext(Context, Binding, *Policy);
		const FString ExactBodyKey = ResolveExactBodyKey(Binding, BodyKeysByRegionId);
		TSharedPtr<FJsonValue> BodyValue;
		TryFindExactBodyValue(BodyObject, ExactBodyKey, BodyValue);
		const FAssetDocumentCapabilityResult Result =
			FAssetDocumentRegionRuntime::Validate(RegionContext, BodyValue, **Adapter);
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
	for (const FAssetDocumentRegionPolicy& Policy : InRegionPolicies)
	{
		PoliciesByRegionId.Add(Policy.RegionId, Policy);
	}

	for (const FAssetDocumentRegionBinding& Binding : RegionBindings)
	{
		const FString ExactBodyKey = ExtractExactBodyKey(Binding, PoliciesByRegionId.Find(Binding.RegionId));
		BodyKeysByRegionId.Add(Binding.RegionId, ExactBodyKey);
		BindingsByBodyKey.Add(ExactBodyKey, Binding);
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

	Result = ValidatePresentRegions(Context, BodyObject.ToSharedRef(), RegionBindings, PoliciesByRegionId, BodyKeysByRegionId, AdaptersByName);
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

	for (const FAssetDocumentRegionBinding& Binding : GetPresentBindingsInApplyOrder(BodyObject.ToSharedRef(), RegionBindings, BodyKeysByRegionId))
	{
		const FAssetDocumentRegionPolicy* Policy = PoliciesByRegionId.Find(Binding.RegionId);
		if (!Policy)
		{
			return FailureForBinding(
				Binding,
				BodyKeysByRegionId,
				TEXT("MissingRegionPolicy"),
				FString::Printf(TEXT("Missing policy for Body region %s"), *Binding.BodyKey.ToString()));
		}

		IAssetDocumentRegionAdapter* const* Adapter = AdaptersByName.Find(Binding.AdapterName);
		if (!Adapter || !*Adapter)
		{
			return FailureForBinding(
				Binding,
				BodyKeysByRegionId,
				TEXT("MissingRegionAdapter"),
				FString::Printf(TEXT("Missing adapter %s for Body region %s"), *Binding.AdapterName.ToString(), *Binding.BodyKey.ToString()));
		}

		FAssetDocumentRegionContext RegionContext = MakeRegionContext(Context, Binding, *Policy);
		const FString ExactBodyKey = ResolveExactBodyKey(Binding, BodyKeysByRegionId);
		TSharedPtr<FJsonValue> BodyValue;
		TryFindExactBodyValue(BodyObject.ToSharedRef(), ExactBodyKey, BodyValue);
		Result = FAssetDocumentRegionRuntime::Preflight(RegionContext, BodyValue, **Adapter);
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

	for (const FAssetDocumentRegionBinding& Binding : GetPresentBindingsInApplyOrder(BodyObject.ToSharedRef(), RegionBindings, BodyKeysByRegionId))
	{
		const FAssetDocumentRegionPolicy* Policy = PoliciesByRegionId.Find(Binding.RegionId);
		IAssetDocumentRegionAdapter* const* Adapter = AdaptersByName.Find(Binding.AdapterName);
		if (!Policy || !Adapter || !*Adapter)
		{
			return FailureForBinding(
				Binding,
				BodyKeysByRegionId,
				!Policy ? TEXT("MissingRegionPolicy") : TEXT("MissingRegionAdapter"),
				FString::Printf(TEXT("Missing runtime configuration for Body region %s"), *Binding.BodyKey.ToString()));
		}

		FAssetDocumentRegionContext RegionContext = MakeRegionContext(Context, Binding, *Policy);
		const FString ExactBodyKey = ResolveExactBodyKey(Binding, BodyKeysByRegionId);
		TSharedPtr<FJsonValue> BodyValue;
		TryFindExactBodyValue(BodyObject.ToSharedRef(), ExactBodyKey, BodyValue);
		bool bChanged = false;
		Result = FAssetDocumentRegionRuntime::Apply(RegionContext, BodyValue, **Adapter, bChanged);
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
				BodyKeysByRegionId,
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
		OutBodyObject->SetField(ResolveExactBodyKey(Binding, BodyKeysByRegionId), RegionValue.IsValid() ? RegionValue : MakeShared<FJsonValueNull>());
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

	Result = ValidatePresentRegions(Context, DesiredBodyObject.ToSharedRef(), RegionBindings, PoliciesByRegionId, BodyKeysByRegionId, AdaptersByName);
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

	for (const FAssetDocumentRegionBinding& Binding : GetPresentBindingsInApplyOrder(DesiredBodyObject.ToSharedRef(), RegionBindings, BodyKeysByRegionId))
	{
		const FAssetDocumentRegionPolicy* Policy = PoliciesByRegionId.Find(Binding.RegionId);
		IAssetDocumentRegionAdapter* const* Adapter = AdaptersByName.Find(Binding.AdapterName);
		if (!Policy || !Adapter || !*Adapter)
		{
			return FailureForBinding(
				Binding,
				BodyKeysByRegionId,
				!Policy ? TEXT("MissingRegionPolicy") : TEXT("MissingRegionAdapter"),
				FString::Printf(TEXT("Missing runtime configuration for Body region %s"), *Binding.BodyKey.ToString()));
		}

		const FAssetDocumentRegionContext RegionContext = MakeRegionContext(Context, Binding, *Policy);
		const FString ExactBodyKey = ResolveExactBodyKey(Binding, BodyKeysByRegionId);
		TSharedPtr<FJsonValue> DesiredValue;
		TryFindExactBodyValue(DesiredBodyObject.ToSharedRef(), ExactBodyKey, DesiredValue);
		Result = FAssetDocumentRegionRuntime::Diff(RegionContext, DesiredValue, **Adapter, OutDiffEntries);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Diffed Body regions"));
}
