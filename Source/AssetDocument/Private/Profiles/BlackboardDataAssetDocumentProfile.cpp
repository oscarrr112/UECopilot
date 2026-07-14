// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/BlackboardDataAssetDocumentProfile.h"

#include "AssetDocumentBodyRegionDispatcher.h"
#include "AssetDocumentJsonRegionUtils.h"
#include "AssetDocumentPolicyRegistry.h"
#include "Regions/AssetDocumentBlackboardKeyRegionAdapter.h"

#include "BehaviorTree/BlackboardData.h"
#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"

namespace
{
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
	return BlackboardRemapDispatcherCompatibilityCodes(DispatchBlackboardDataBody(
		[&Context, &BodyJson](const FAssetDocumentBodyRegionDispatcher& Dispatcher)
		{
			return Dispatcher.ValidateBody(Context, BodyJson);
		}));
}

FAssetDocumentCapabilityResult FBlackboardDataAssetDocumentCapability::Apply(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson)
{
	FAssetDocumentCapabilityResult Result = ValidateBlackboardDataContext(Context);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TSet<FName> AppliedRegions;
	return BlackboardRemapDispatcherCompatibilityCodes(DispatchBlackboardDataBody(
		[&Context, &BodyJson, &AppliedRegions](const FAssetDocumentBodyRegionDispatcher& Dispatcher)
		{
			return Dispatcher.ApplyBody(Context, BodyJson, AppliedRegions);
		}));
}

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
