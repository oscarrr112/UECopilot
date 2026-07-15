// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/BlackboardDataAssetDocumentProfile.h"

#include "AssetDocumentBodyRegionDispatcher.h"
#include "AssetDocumentJsonRegionUtils.h"
#include "AssetDocumentPolicyRegistry.h"
#include "Regions/AssetDocumentBlackboardKeyRegionAdapter.h"
#include "Regions/AssetDocumentBlackboardKeySchemaUtils.h"

#include "BehaviorTree/BlackboardData.h"
#include "Dom/JsonValue.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "Misc/PackageName.h"

namespace
{
#if WITH_DEV_AUTOMATION_TESTS
bool bBlackboardFailNextLiveApplyAfterMutationForTest = false;
TFunction<void(UBlackboardData*)> BlackboardBeforeForcedLiveApplyFailureForTest;
#endif

FString BlackboardRegionPath(const FAssetDocumentRegionContext& Context)
{
	return Context.JsonPointer.IsEmpty() ? Context.BodyPath : Context.JsonPointer;
}

FString BlackboardNormalizeObjectPath(const FString& Path)
{
	FString ObjectPath = Path.TrimStartAndEnd();
	if (ObjectPath.StartsWith(TEXT("/")) && !ObjectPath.Contains(TEXT(".")))
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(ObjectPath);
		if (!AssetName.IsEmpty())
		{
			ObjectPath = FString::Printf(TEXT("%s.%s"), *ObjectPath, *AssetName);
		}
	}
	return ObjectPath;
}

bool BlackboardMakeRegionPolicy(
	FName PresetName,
	FName RegionId,
	EAssetDocumentRegionKind RegionKind,
	TArray<FString> ManagedUePropertyPaths,
	FAssetDocumentRegionPolicy& OutPolicy)
{
	FAssetDocumentRegionPolicyPreset Preset;
	if (!FAssetDocumentPolicyRegistry::GetBuiltinPreset(PresetName, Preset))
	{
		return false;
	}

	FAssetDocumentRegionPolicyOverride Override;
	Override.RegionId = RegionId;
	Override.BodyPath = RegionId.ToString();
	Override.RegionKind = RegionKind;
	if (ManagedUePropertyPaths.Num() > 0)
	{
		Override.ManagedUePropertyPaths = MoveTemp(ManagedUePropertyPaths);
	}
	return FAssetDocumentPolicyRegistry::ExpandPreset(Preset, Override, OutPolicy);
}

FAssetDocumentCapabilityResult BlackboardRemapDispatcherCompatibilityCodes(FAssetDocumentCapabilityResult Result)
{
	if (Result.bSuccess)
	{
		return Result;
	}

	for (FAssetDocumentDiagnostic& Diagnostic : Result.Diagnostics)
	{
		if (Diagnostic.Code == TEXT("UnknownBodyRegion"))
		{
			Diagnostic.Code = TEXT("UnknownBodyKey");
		}
	}
	return Result;
}

FAssetDocumentCapabilityResult ResolveBlackboardAssetRef(
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	UBlackboardData*& OutParent)
{
	OutParent = nullptr;
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Resolved null BlackboardData parent"));
	}

	TSharedPtr<FJsonObject> Object;
	FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(Value, Path, Object);
	if (!Result.bSuccess)
	{
		return Result;
	}

	FString Kind;
	if (!Object->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("AssetRef"))
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			FString::Printf(TEXT("%s/Kind"), *Path),
			TEXT("InvalidBlackboardParentRef"),
			TEXT("Blackboard Parent.Kind must be AssetRef"));
	}

	FString AssetPath;
	if (!Object->TryGetStringField(TEXT("Path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
	{
		if (!Object->TryGetStringField(TEXT("Asset"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				FString::Printf(TEXT("%s/Path"), *Path),
				TEXT("MissingBlackboardParentPath"),
				TEXT("Blackboard Parent.Path is required"));
		}
	}

	OutParent = LoadObject<UBlackboardData>(nullptr, *BlackboardNormalizeObjectPath(AssetPath));
	if (!OutParent)
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			Path,
			TEXT("InvalidBlackboardParent"),
			FString::Printf(TEXT("Blackboard Parent '%s' did not resolve to UBlackboardData"), *AssetPath));
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Resolved BlackboardData parent"));
}

FAssetDocumentCapabilityResult ValidateBlackboardParentChain(
	const UBlackboardData* Blackboard,
	const UBlackboardData* Parent,
	const FString& Path)
{
	if (!Blackboard || !Parent)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Validated BlackboardData parent chain"));
	}

	TSet<const UBlackboardData*> Visited;
	for (const UBlackboardData* Current = Parent; Current; Current = Current->Parent.Get())
	{
		if (Current == Blackboard)
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				Path,
				TEXT("BlackboardParentCycle"),
				TEXT("Blackboard Parent cannot reference itself or one of its descendants"));
		}
		if (Visited.Contains(Current))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				Path,
				TEXT("BlackboardParentCycle"),
				TEXT("Blackboard Parent chain contains a cycle"));
		}
		Visited.Add(Current);
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated BlackboardData parent chain"));
}

FAssetDocumentCapabilityResult ValidateBlackboardDesiredInheritance(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonValue>& BodyJson)
{
	const TSharedPtr<FJsonObject> Body = BodyJson->AsObject();
	if (!Body.IsValid())
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Blackboard body shape is validated by dispatcher"));
	}

	UBlackboardData* DesiredParent = Cast<UBlackboardData>(Context.Asset) ? Cast<UBlackboardData>(Context.Asset)->Parent.Get() : nullptr;
	if (Body->HasField(TEXT("Parent")))
	{
		FAssetDocumentCapabilityResult ParentResult = ResolveBlackboardAssetRef(
			Body->TryGetField(TEXT("Parent")),
			TEXT("/Body/Parent"),
			DesiredParent);
		if (!ParentResult.bSuccess)
		{
			return ParentResult;
		}
	}

	const FString TargetObjectPath = BlackboardNormalizeObjectPath(Context.TargetAssetPath);
	TSet<const UBlackboardData*> Visited;
	TSet<FName> ParentKeyNames;
	for (const UBlackboardData* Current = DesiredParent; Current; Current = Current->Parent.Get())
	{
		if (Visited.Contains(Current) ||
			(Current == Context.Asset) ||
			(!TargetObjectPath.IsEmpty() && Current->GetPathName() == TargetObjectPath))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				TEXT("/Body/Parent"),
				TEXT("BlackboardParentCycle"),
				TEXT("Blackboard Parent cannot reference the target or contain a cycle"));
		}
		Visited.Add(Current);
		for (const FBlackboardEntry& Entry : Current->Keys)
		{
			ParentKeyNames.Add(Entry.EntryName);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
	if (!Body->TryGetArrayField(TEXT("Keys"), Keys) || !Keys)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("No desired local keys to validate against parent"));
	}
	for (int32 Index = 0; Index < Keys->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject> KeyObject = (*Keys)[Index].IsValid() ? (*Keys)[Index]->AsObject() : nullptr;
		if (!KeyObject.IsValid())
		{
			continue;
		}
		FAssetDocumentBlackboardKeySpec Spec;
		const FString IndexPath = FString::Printf(TEXT("/Body/Keys/%d"), Index);
		const FAssetDocumentCapabilityResult ParseResult = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
			KeyObject.ToSharedRef(),
			IndexPath,
			Spec);
		if (!ParseResult.bSuccess)
		{
			return ParseResult;
		}
		if (ParentKeyNames.Contains(Spec.Name))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				FString::Printf(
					TEXT("/Body/Keys/%s/Name"),
					*FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Spec.Name.ToString())),
				TEXT("BlackboardKeyShadowsParent"),
				FString::Printf(TEXT("Local key '%s' shadows a parent key"), *Spec.Name.ToString()));
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated Blackboard parent and local key inheritance"));
}

TSet<UObject*> SnapshotBlackboardOwnedObjects(UBlackboardData* Blackboard)
{
	TSet<UObject*> OwnedObjects;
	if (Blackboard)
	{
		ForEachObjectWithOuter(Blackboard, [&OwnedObjects](UObject* Object)
		{
			OwnedObjects.Add(Object);
		}, true);
	}
	return OwnedObjects;
}

bool MoveNewBlackboardOwnedObjectsToTransient(
	UBlackboardData* Blackboard,
	const TSet<UObject*>& OriginalOwnedObjects)
{
	if (!Blackboard)
	{
		return true;
	}

	TSet<UObject*> NewOwnedObjects;
	ForEachObjectWithOuter(Blackboard, [&OriginalOwnedObjects, &NewOwnedObjects](UObject* Object)
	{
		if (Object && !OriginalOwnedObjects.Contains(Object))
		{
			NewOwnedObjects.Add(Object);
		}
	}, true);

	TArray<UObject*> NewOwnedRoots;
	for (UObject* Object : NewOwnedObjects)
	{
		if (Object && !NewOwnedObjects.Contains(Object->GetOuter()))
		{
			NewOwnedRoots.Add(Object);
		}
	}
	bool bCleanupSucceeded = true;
	for (UObject* Object : NewOwnedRoots)
	{
		const FName TransientName = MakeUniqueObjectName(GetTransientPackage(), Object->GetClass(), Object->GetFName());
		Object->SetFlags(RF_Transient);
		Object->ClearFlags(RF_Public | RF_Standalone);
		bCleanupSucceeded &= Object->Rename(
			*TransientName.ToString(),
			GetTransientPackage(),
			REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
	}

	const TSet<UObject*> RemainingOwnedObjects = SnapshotBlackboardOwnedObjects(Blackboard);
	for (UObject* Object : RemainingOwnedObjects)
	{
		if (!OriginalOwnedObjects.Contains(Object))
		{
			bCleanupSucceeded = false;
		}
	}
	return bCleanupSucceeded;
}

TSharedPtr<FJsonValue> BlackboardMakeAssetRefValue(const UBlackboardData* Blackboard)
{
	if (!Blackboard)
	{
		return MakeShared<FJsonValueNull>();
	}

	TSharedRef<FJsonObject> ParentRef = MakeShared<FJsonObject>();
	ParentRef->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	ParentRef->SetStringField(TEXT("Path"), Blackboard->GetPathName());
	return MakeShared<FJsonValueObject>(ParentRef);
}

TSharedPtr<FJsonValue> ExtractBlackboardParentValue(const UBlackboardData* Blackboard)
{
	return BlackboardMakeAssetRefValue(Blackboard ? Blackboard->Parent.Get() : nullptr);
}

class FBlackboardDataParentRegionAdapter final : public IAssetDocumentRegionAdapter
{
public:
	explicit FBlackboardDataParentRegionAdapter(FName InName)
		: Name(InName)
	{
	}

	virtual FName GetName() const override
	{
		return Name;
	}

	virtual bool SupportsRegion(const FAssetDocumentRegionContext& Context) const override
	{
		return !Context.Policy || Context.Policy->RegionKind == EAssetDocumentRegionKind::Object;
	}

	virtual TSharedRef<FJsonObject> GetSchemaHint(const FAssetDocumentRegionContext&) const override
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("Adapter"), GetName().ToString());
		Schema->SetStringField(TEXT("Shape"), TEXT("AssetRef<UBlackboardData> | null"));
		return Schema;
	}

	virtual FAssetDocumentCapabilityResult ValidateRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue) const override
	{
		UBlackboardData* Parent = nullptr;
		FAssetDocumentCapabilityResult Result = ResolveBlackboardAssetRef(DesiredValue, BlackboardRegionPath(Context), Parent);
		if (!Result.bSuccess)
		{
			return Result;
		}
		return ValidateBlackboardParentChain(Cast<UBlackboardData>(Context.Asset), Parent, BlackboardRegionPath(Context));
	}

	virtual FAssetDocumentCapabilityResult ApplyRegion(
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		bool& bOutChanged) override
	{
		bOutChanged = false;
		UBlackboardData* Blackboard = Cast<UBlackboardData>(Context.Asset);
		if (!Blackboard)
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				BlackboardRegionPath(Context),
				TEXT("UnsupportedAsset"),
				TEXT("Blackboard Parent apply requires UBlackboardData asset"));
		}

		UBlackboardData* Parent = nullptr;
		FAssetDocumentCapabilityResult Result = ResolveBlackboardAssetRef(DesiredValue, BlackboardRegionPath(Context), Parent);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = ValidateBlackboardParentChain(Blackboard, Parent, BlackboardRegionPath(Context));
		if (!Result.bSuccess)
		{
			return Result;
		}

		bOutChanged = Blackboard->Parent.Get() != Parent;
		if (!Context.bIsDryRun)
		{
			Blackboard->Parent = Parent;
			if (bOutChanged)
			{
				Blackboard->UpdateParentKeys();
				Blackboard->UpdateKeyIDs();
				Blackboard->UpdateIfHasSynchronizedKeys();
				Blackboard->PropagateKeyChangesToDerivedBlackboardAssets();
				Blackboard->MarkPackageDirty();
			}
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("Applied BlackboardData Parent"));
	}

	virtual FAssetDocumentCapabilityResult ExtractRegion(
		const FAssetDocumentRegionContext& Context,
		TSharedPtr<FJsonValue>& OutCurrentValue) const override
	{
		OutCurrentValue = ExtractBlackboardParentValue(Cast<UBlackboardData>(Context.Asset));
		return FAssetDocumentCapabilityResult::Success(TEXT("Extracted BlackboardData Parent"));
	}

	virtual FAssetDocumentCapabilityResult DiffRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const override
	{
		FAssetDocumentCapabilityResult Result = ValidateRegion(Context, DesiredValue);
		if (!Result.bSuccess)
		{
			return Result;
		}

		UBlackboardData* DesiredParent = nullptr;
		Result = ResolveBlackboardAssetRef(DesiredValue, BlackboardRegionPath(Context), DesiredParent);
		if (!Result.bSuccess)
		{
			return Result;
		}

		const TSharedPtr<FJsonValue> CurrentValue = ExtractBlackboardParentValue(Cast<UBlackboardData>(Context.Asset));
		const TSharedPtr<FJsonValue> DesiredCanonicalValue = BlackboardMakeAssetRefValue(DesiredParent);
		const bool bSame =
			FAssetDocumentJsonRegionUtils::JsonValueToComparableString(CurrentValue) ==
			FAssetDocumentJsonRegionUtils::JsonValueToComparableString(DesiredCanonicalValue);
		FAssetDocumentJsonRegionUtils::AddDiffEntry(
			OutDiffEntries,
			BlackboardRegionPath(Context),
			bSame ? TEXT("unchanged") : TEXT("changed"),
			CurrentValue,
			DesiredCanonicalValue);
		return FAssetDocumentCapabilityResult::Success(TEXT("Diffed BlackboardData Parent"));
	}

private:
	FName Name;
};

FAssetDocumentCapabilityResult ValidateBlackboardDataContext(const FAssetDocumentCapabilityContext& Context)
{
	if (Context.Asset && Context.Asset->GetClass() != UBlackboardData::StaticClass())
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			TEXT("/Body"),
			TEXT("UnsupportedAsset"),
			TEXT("BlackboardData Body requires an exact UBlackboardData asset"));
	}
	if (Context.AssetClass && Context.AssetClass != UBlackboardData::StaticClass())
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			TEXT("/Body"),
			TEXT("UnsupportedAssetClass"),
			TEXT("BlackboardData Body requires exact UBlackboardData class"));
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Validated BlackboardData context"));
}

FAssetDocumentCapabilityResult DispatchBlackboardDataBody(
	TFunctionRef<FAssetDocumentCapabilityResult(const FAssetDocumentBodyRegionDispatcher&)> Dispatch)
{
	FBlackboardDataParentRegionAdapter ParentAdapter(FBlackboardDataAssetDocumentProfile::ParentRegionAdapterName());
	FAssetDocumentBlackboardKeyRegionAdapter KeysAdapter(FBlackboardDataAssetDocumentProfile::KeysRegionAdapterName());

	TMap<FName, IAssetDocumentRegionAdapter*> Adapters;
	Adapters.Add(ParentAdapter.GetName(), &ParentAdapter);
	Adapters.Add(KeysAdapter.GetName(), &KeysAdapter);

	const FBlackboardDataAssetDocumentProfile Profile;
	const FAssetDocumentBodyRegionDispatcher Dispatcher(
		FBlackboardDataAssetDocumentProfile::MakeRegionBindings(),
		Profile.GetRegionPolicies(),
		Adapters);
	return Dispatch(Dispatcher);
}
}

TArray<FName> FBlackboardDataAssetDocumentCapability::GetCanonicalBodyKeys()
{
	return {
		TEXT("Parent"),
		TEXT("Keys"),
	};
}

FName FBlackboardDataAssetDocumentCapability::GetName() const
{
	return TEXT("BlackboardDataBodyCapability");
}

TArray<FName> FBlackboardDataAssetDocumentCapability::GetInternalAdapterNames() const
{
	return {
		FBlackboardDataAssetDocumentProfile::ParentRegionAdapterName(),
		FBlackboardDataAssetDocumentProfile::KeysRegionAdapterName(),
	};
}

int32 FBlackboardDataAssetDocumentCapability::GetApplyOrder() const
{
	return 100;
}

bool FBlackboardDataAssetDocumentCapability::SupportsAsset(const UObject* Asset) const
{
	return Asset && SupportsClass(Asset->GetClass());
}

bool FBlackboardDataAssetDocumentCapability::SupportsClass(const UClass* AssetClass) const
{
	return AssetClass == UBlackboardData::StaticClass();
}

TSharedRef<FJsonObject> FBlackboardDataAssetDocumentCapability::GetSchemaHint() const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Parent"), TEXT("AssetRef<UBlackboardData> | null"));
	Schema->SetStringField(TEXT("Keys"), TEXT("array<BlackboardKey>"));
	return Schema;
}

FAssetDocumentCapabilityResult FBlackboardDataAssetDocumentCapability::Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	FAssetDocumentCapabilityResult Result = ValidateBlackboardDataContext(Context);
	if (!Result.bSuccess)
	{
		return Result;
	}
	Result = BlackboardRemapDispatcherCompatibilityCodes(DispatchBlackboardDataBody(
		[&Context, &BodyJson](const FAssetDocumentBodyRegionDispatcher& Dispatcher)
		{
			return Dispatcher.ValidateBody(Context, BodyJson);
		}));
	if (!Result.bSuccess)
	{
		return Result;
	}
	return ValidateBlackboardDesiredInheritance(Context, BodyJson);
}

FAssetDocumentCapabilityResult FBlackboardDataAssetDocumentCapability::Preflight(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	FAssetDocumentCapabilityResult Result = ValidateBlackboardDataContext(Context);
	if (!Result.bSuccess)
	{
		return Result;
	}
	Result = BlackboardRemapDispatcherCompatibilityCodes(DispatchBlackboardDataBody(
		[&Context, &BodyJson](const FAssetDocumentBodyRegionDispatcher& Dispatcher)
		{
			return Dispatcher.PreflightBody(Context, BodyJson);
		}));
	if (!Result.bSuccess)
	{
		return Result;
	}
	return ValidateBlackboardDesiredInheritance(Context, BodyJson);
}

FAssetDocumentCapabilityResult FBlackboardDataAssetDocumentCapability::Apply(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson)
{
	FAssetDocumentCapabilityResult Result = ValidateBlackboardDataContext(Context);
	if (!Result.bSuccess)
	{
		return Result;
	}
	Result = ValidateBlackboardDesiredInheritance(Context, BodyJson);
	if (!Result.bSuccess)
	{
		return Result;
	}

	UBlackboardData* Blackboard = Cast<UBlackboardData>(Context.Asset);
	if (Blackboard && !Context.bIsDryRun)
	{
		const FName StagingName = MakeUniqueObjectName(
			GetTransientPackage(),
			UBlackboardData::StaticClass(),
			TEXT("AssetDocumentBlackboardStaging"));
		UBlackboardData* StagingBlackboard = DuplicateObject<UBlackboardData>(
			Blackboard,
			GetTransientPackage(),
			StagingName);
		if (!StagingBlackboard)
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				TEXT("/Body"),
				TEXT("BlackboardStagingFailed"),
				TEXT("Failed to create transient BlackboardData staging asset"));
		}
		StagingBlackboard->SetFlags(RF_Transient);
		FAssetDocumentCapabilityContext StagingContext = Context;
		StagingContext.Asset = StagingBlackboard;
		StagingContext.bIsDryRun = false;
		TSet<FName> StagedRegions;
		Result = BlackboardRemapDispatcherCompatibilityCodes(DispatchBlackboardDataBody(
			[&StagingContext, &BodyJson, &StagedRegions](const FAssetDocumentBodyRegionDispatcher& Dispatcher)
			{
				return Dispatcher.ApplyBody(StagingContext, BodyJson, StagedRegions);
			}));
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	UBlackboardData* OriginalParent = Blackboard ? Blackboard->Parent.Get() : nullptr;
	const TArray<FBlackboardEntry> OriginalKeys = Blackboard ? Blackboard->Keys : TArray<FBlackboardEntry>();
#if WITH_EDITORONLY_DATA
	const TArray<FBlackboardEntry> OriginalParentKeys = Blackboard ? Blackboard->ParentKeys : TArray<FBlackboardEntry>();
#endif
	const TSet<UObject*> OriginalOwnedObjects = SnapshotBlackboardOwnedObjects(Blackboard);
	UPackage* Package = Blackboard ? Blackboard->GetOutermost() : nullptr;
	const bool bWasDirty = Package && Package->IsDirty();

	TSet<FName> AppliedRegions;
	Result = BlackboardRemapDispatcherCompatibilityCodes(DispatchBlackboardDataBody(
		[&Context, &BodyJson, &AppliedRegions](const FAssetDocumentBodyRegionDispatcher& Dispatcher)
		{
			return Dispatcher.ApplyBody(Context, BodyJson, AppliedRegions);
		}));
#if WITH_DEV_AUTOMATION_TESTS
	if (Result.bSuccess
		&& Blackboard
		&& Blackboard->GetOutermost() != GetTransientPackage()
		&& !Blackboard->HasAnyFlags(RF_Transient)
		&& bBlackboardFailNextLiveApplyAfterMutationForTest)
	{
		bBlackboardFailNextLiveApplyAfterMutationForTest = false;
		TFunction<void(UBlackboardData*)> BeforeFailure = MoveTemp(BlackboardBeforeForcedLiveApplyFailureForTest);
		BlackboardBeforeForcedLiveApplyFailureForTest = {};
		if (BeforeFailure)
		{
			BeforeFailure(Blackboard);
		}
		Result = FAssetDocumentJsonRegionUtils::Failure(
			TEXT("/Body"),
			TEXT("ForcedBlackboardLiveApplyFailure"),
			TEXT("Forced BlackboardData live apply failure for automation coverage"));
	}
#endif
	if (!Result.bSuccess && Blackboard)
	{
		Blackboard->Parent = OriginalParent;
		Blackboard->Keys = OriginalKeys;
#if WITH_EDITORONLY_DATA
		Blackboard->ParentKeys = OriginalParentKeys;
#endif
		Blackboard->UpdateKeyIDs();
		Blackboard->UpdateIfHasSynchronizedKeys();
		UBlackboardData::OnUpdateKeys.Broadcast(Blackboard);
		Blackboard->PropagateKeyChangesToDerivedBlackboardAssets();
		if (!MoveNewBlackboardOwnedObjectsToTransient(Blackboard, OriginalOwnedObjects))
		{
			FAssetDocumentDiagnostic CleanupDiagnostic;
			CleanupDiagnostic.Path = TEXT("/Body");
			CleanupDiagnostic.Code = TEXT("BlackboardRollbackCleanupFailed");
			CleanupDiagnostic.Message = TEXT("BlackboardData rollback could not detach every newly owned UObject");
			Result.Diagnostics.Add(MoveTemp(CleanupDiagnostic));
		}
		if (Package && !bWasDirty)
		{
			Package->SetDirtyFlag(false);
		}
	}
	return Result;
}

#if WITH_DEV_AUTOMATION_TESTS
void FBlackboardDataAssetDocumentCapability::FailNextLiveApplyAfterMutationForTest(
	TFunction<void(UBlackboardData*)> BeforeFailure)
{
	bBlackboardFailNextLiveApplyAfterMutationForTest = true;
	BlackboardBeforeForcedLiveApplyFailureForTest = MoveTemp(BeforeFailure);
}
#endif

FAssetDocumentCapabilityResult FBlackboardDataAssetDocumentCapability::Extract(const FAssetDocumentCapabilityContext& Context, TSharedRef<FJsonObject>& OutBodyJson) const
{
	FAssetDocumentCapabilityResult Result = ValidateBlackboardDataContext(Context);
	if (!Result.bSuccess)
	{
		return Result;
	}
	return BlackboardRemapDispatcherCompatibilityCodes(DispatchBlackboardDataBody(
		[&Context, &OutBodyJson](const FAssetDocumentBodyRegionDispatcher& Dispatcher)
		{
			return Dispatcher.ExtractBody(Context, OutBodyJson);
		}));
}

FAssetDocumentCapabilityResult FBlackboardDataAssetDocumentCapability::Diff(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonValue>& DesiredJson,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	FAssetDocumentCapabilityResult Result = ValidateBlackboardDataContext(Context);
	if (!Result.bSuccess)
	{
		return Result;
	}
	Result = ValidateBlackboardDesiredInheritance(Context, DesiredJson);
	if (!Result.bSuccess)
	{
		return Result;
	}
	return BlackboardRemapDispatcherCompatibilityCodes(DispatchBlackboardDataBody(
		[&Context, &DesiredJson, &OutDiffEntries](const FAssetDocumentBodyRegionDispatcher& Dispatcher)
		{
			return Dispatcher.DiffBody(Context, DesiredJson, OutDiffEntries);
		}));
}

FName FBlackboardDataAssetDocumentProfile::ParentRegionAdapterName()
{
	return TEXT("BlackboardDataParentRegionAdapter");
}

FName FBlackboardDataAssetDocumentProfile::KeysRegionAdapterName()
{
	return TEXT("BlackboardDataKeysRegionAdapter");
}

TArray<FAssetDocumentRegionBinding> FBlackboardDataAssetDocumentProfile::MakeRegionBindings()
{
	return {
		{TEXT("Parent"), TEXT("Body.Parent"), ParentRegionAdapterName(), 10, false},
		{TEXT("Keys"), TEXT("Body.Keys"), KeysRegionAdapterName(), 20, false},
	};
}

UClass* FBlackboardDataAssetDocumentProfile::GetExactClass() const
{
	return UBlackboardData::StaticClass();
}

TSharedRef<FJsonObject> FBlackboardDataAssetDocumentProfile::GetDocumentShape() const
{
	TSharedRef<FJsonObject> Shape = MakeShared<FJsonObject>();
	Shape->SetStringField(TEXT("Definitions"), TEXT("map<string, Fragment>"));
	Shape->SetStringField(TEXT("Properties"), TEXT("reflected UBlackboardData properties not owned by Body regions"));
	Shape->SetObjectField(TEXT("Body"), BodyCapability.GetSchemaHint());
	return Shape;
}

TSharedRef<FJsonObject> FBlackboardDataAssetDocumentProfile::CreateTemplate(const FAssetDocumentTemplateContext& Context) const
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetField(TEXT("Parent"), MakeShared<FJsonValueNull>());
	Body->SetArrayField(TEXT("Keys"), TArray<TSharedPtr<FJsonValue>>());

	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetNumberField(TEXT("SchemaVersion"), 1);
	Template->SetStringField(TEXT("Target"), Context.Target);
	Template->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BlackboardData"));
	Template->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Template->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Body"), Body);
	return Template;
}

TArray<FName> FBlackboardDataAssetDocumentProfile::GetBodyKeys() const
{
	return FBlackboardDataAssetDocumentCapability::GetCanonicalBodyKeys();
}

const IAssetDocumentCapability* FBlackboardDataAssetDocumentProfile::ResolveBodyAdapter(FName BodyKey) const
{
	if (BodyKey == TEXT("Body"))
	{
		return &BodyCapability;
	}

	for (const FName& KnownBodyKey : FBlackboardDataAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (BodyKey == KnownBodyKey)
		{
			return &BodyCapability;
		}
	}

	return nullptr;
}

TArray<FAssetDocumentRegionPolicy> FBlackboardDataAssetDocumentProfile::GetRegionPolicies() const
{
	TArray<FAssetDocumentRegionPolicy> Policies;
	Policies.Reserve(2);

	FAssetDocumentRegionPolicy Policy;
	if (BlackboardMakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.Parent"), EAssetDocumentRegionKind::Object, {TEXT("Parent")}, Policy))
	{
		Policy.ExplicitDeleteValues.Add(FAssetDocumentExplicitDeleteValues::Null());
		Policies.Add(Policy);
	}
	if (BlackboardMakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.Keys"), EAssetDocumentRegionKind::Array, {TEXT("Keys")}, Policy))
	{
		Policies.Add(Policy);
	}

	return Policies;
}
