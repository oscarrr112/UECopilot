// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/BehaviorTreeAssetDocumentProfile.h"

#include "AssetDocumentBodyRegionDispatcher.h"
#include "AssetDocumentJsonRegionUtils.h"
#include "AssetDocumentPolicyRegistry.h"
#include "Profiles/BehaviorTreeAssetDocumentMaterializer.h"
#include "Regions/AssetDocumentEditorLayoutRegionAdapter.h"
#include "Regions/AssetDocumentTreeRegionAdapter.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"

namespace
{
bool BehaviorTreeMakeRegionPolicy(
	FName PresetName,
	FName RegionId,
	EAssetDocumentRegionKind RegionKind,
	TArray<FString> ManagedUePropertyPaths,
	FName CanonicalizerHookName,
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
	if (!CanonicalizerHookName.IsNone())
	{
		Override.CanonicalizerHookName = CanonicalizerHookName;
	}
	return FAssetDocumentPolicyRegistry::ExpandPreset(Preset, Override, OutPolicy);
}

bool BehaviorTreeMakeRegionPolicy(
	FName PresetName,
	FName RegionId,
	EAssetDocumentRegionKind RegionKind,
	TArray<FString> ManagedUePropertyPaths,
	FAssetDocumentRegionPolicy& OutPolicy)
{
	return BehaviorTreeMakeRegionPolicy(PresetName, RegionId, RegionKind, MoveTemp(ManagedUePropertyPaths), NAME_None, OutPolicy);
}

FString BehaviorTreeRegionPath(const FAssetDocumentRegionContext& Context)
{
	return Context.JsonPointer.IsEmpty() ? Context.BodyPath : Context.JsonPointer;
}

FString BehaviorTreeNormalizeObjectPath(const FString& Path)
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

FAssetDocumentCapabilityResult RemapDispatcherCompatibilityCodes(FAssetDocumentCapabilityResult Result)
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

FAssetDocumentCapabilityResult ResolveBehaviorTreeBlackboardRef(
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	UBlackboardData*& OutBlackboard)
{
	OutBlackboard = nullptr;
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Resolved null BehaviorTree Blackboard"));
	}
	FString DirectPath;
	if (Value->TryGetString(DirectPath))
	{
		DirectPath.TrimStartAndEndInline();
		if (DirectPath.IsEmpty())
		{
			return FAssetDocumentJsonRegionUtils::Failure(Path, TEXT("MissingBehaviorTreeBlackboardPath"), TEXT("BehaviorTree BlackboardAsset path cannot be empty"));
		}
		OutBlackboard = LoadObject<UBlackboardData>(nullptr, *BehaviorTreeNormalizeObjectPath(DirectPath));
		return OutBlackboard
			? FAssetDocumentCapabilityResult::Success(TEXT("Resolved BehaviorTree BlackboardAsset"))
			: FAssetDocumentJsonRegionUtils::Failure(Path, TEXT("UnresolvedBehaviorTreeBlackboard"), FString::Printf(TEXT("BehaviorTree BlackboardAsset '%s' did not resolve"), *DirectPath));
	}

	TSharedPtr<FJsonObject> Object;
	FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(Value, Path, Object);
	if (!Result.bSuccess)
	{
		return Result;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
	{
		if (Field.Key != TEXT("Kind") && Field.Key != TEXT("Path"))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				FString::Printf(TEXT("%s/%s"), *Path, *FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Field.Key)),
				TEXT("UnknownField"),
				FString::Printf(TEXT("Unknown Blackboard AssetRef field %s"), *Field.Key));
		}
	}

	FString Kind;
	if (!Object->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("AssetRef"))
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			FString::Printf(TEXT("%s/Kind"), *Path),
			TEXT("InvalidBehaviorTreeBlackboardRef"),
			TEXT("BehaviorTree Blackboard.Kind must be AssetRef"));
	}

	FString AssetPath;
	if (!Object->TryGetStringField(TEXT("Path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			FString::Printf(TEXT("%s/Path"), *Path),
			TEXT("MissingBehaviorTreeBlackboardPath"),
			TEXT("BehaviorTree Blackboard.Path is required"));
	}

	OutBlackboard = LoadObject<UBlackboardData>(nullptr, *BehaviorTreeNormalizeObjectPath(AssetPath));
	if (!OutBlackboard)
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			Path,
			TEXT("UnresolvedBehaviorTreeBlackboard"),
			FString::Printf(TEXT("BehaviorTree Blackboard '%s' did not resolve to UBlackboardData"), *AssetPath));
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Resolved BehaviorTree Blackboard"));
}

TSharedPtr<FJsonValue> MakeBlackboardAssetRefValue(const UBlackboardData* Blackboard)
{
	if (!Blackboard)
	{
		return MakeShared<FJsonValueNull>();
	}

	TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
	Ref->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	Ref->SetStringField(TEXT("Path"), Blackboard->GetPathName());
	return MakeShared<FJsonValueObject>(Ref);
}

bool IsJsonEmptyObject(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && Value->Type == EJson::Object && Value->AsObject().IsValid() && Value->AsObject()->Values.Num() == 0;
}

bool IsJsonEmptyArray(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && Value->Type == EJson::Array && Value->AsArray().Num() == 0;
}

FAssetDocumentCapabilityResult ValidateStrictEmptyTree(const TSharedPtr<FJsonObject>& Tree, const FString& Path)
{
	if (!Tree.IsValid())
	{
		return FAssetDocumentJsonRegionUtils::Failure(Path, TEXT("InvalidBodySectionType"), TEXT("Body.Tree must be an object"));
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Tree->Values)
	{
		if (Field.Key != TEXT("RootDecorators") && Field.Key != TEXT("RootDecoratorLogic") && Field.Key != TEXT("Root"))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				FString::Printf(TEXT("%s/%s"), *Path, *FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Field.Key)),
				TEXT("UnknownField"),
				FString::Printf(TEXT("Unknown BehaviorTree Tree field %s"), *Field.Key));
		}
	}

	if (!IsJsonEmptyArray(Tree->TryGetField(TEXT("RootDecorators"))))
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			FString::Printf(TEXT("%s/RootDecorators"), *Path),
			TEXT("UnsupportedBehaviorTreeRegion"),
			TEXT("BehaviorTree RootDecorators materialization is deferred to Task 7"));
	}

	if (!IsJsonEmptyArray(Tree->TryGetField(TEXT("RootDecoratorLogic"))))
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			FString::Printf(TEXT("%s/RootDecoratorLogic"), *Path),
			TEXT("UnsupportedBehaviorTreeRegion"),
			TEXT("BehaviorTree RootDecoratorLogic materialization is deferred to Task 7"));
	}

	if (!IsJsonEmptyObject(Tree->TryGetField(TEXT("Root"))))
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			FString::Printf(TEXT("%s/Root"), *Path),
			TEXT("UnsupportedBehaviorTreeRegion"),
			TEXT("BehaviorTree semantic tree materialization is deferred to Task 7"));
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated strict empty BehaviorTree Tree"));
}

bool IsStrictEmptyTreeObject(const TSharedPtr<FJsonObject>& Tree)
{
	return ValidateStrictEmptyTree(Tree, TEXT("/Body/Tree")).bSuccess;
}

bool IsStrictEmptyTreeValue(const TSharedPtr<FJsonValue>& Value, TSharedPtr<FJsonObject>& OutTree)
{
	OutTree.Reset();
	if (!Value.IsValid() || Value->Type != EJson::Object || !Value->AsObject().IsValid())
	{
		return false;
	}

	OutTree = Value->AsObject();
	return IsStrictEmptyTreeObject(OutTree);
}

TSharedPtr<FJsonValue> MakeEmptyTreeValue()
{
	TSharedRef<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetArrayField(TEXT("RootDecorators"), TArray<TSharedPtr<FJsonValue>>());
	Tree->SetArrayField(TEXT("RootDecoratorLogic"), TArray<TSharedPtr<FJsonValue>>());
	Tree->SetObjectField(TEXT("Root"), MakeShared<FJsonObject>());
	return MakeShared<FJsonValueObject>(Tree);
}

TSharedPtr<FJsonValue> MakeEmptyObjectValue()
{
	return MakeShared<FJsonValueObject>(MakeShared<FJsonObject>());
}

class FBehaviorTreeBlackboardRegionAdapter final : public IAssetDocumentRegionAdapter
{
public:
	explicit FBehaviorTreeBlackboardRegionAdapter(FName InName)
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
		Schema->SetStringField(TEXT("Shape"), TEXT("UBlackboardData object path | AssetRef<UBlackboardData> | null"));
		return Schema;
	}

	virtual FAssetDocumentCapabilityResult ValidateRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue) const override
	{
		UBlackboardData* Blackboard = nullptr;
		return ResolveBehaviorTreeBlackboardRef(DesiredValue, BehaviorTreeRegionPath(Context), Blackboard);
	}

	virtual FAssetDocumentCapabilityResult ApplyRegion(
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		bool& bOutChanged) override
	{
		bOutChanged = false;
		UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(Context.Asset);
		if (!BehaviorTree)
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				BehaviorTreeRegionPath(Context),
				TEXT("UnsupportedAsset"),
				TEXT("BehaviorTree Blackboard apply requires UBehaviorTree asset"));
		}

		UBlackboardData* Blackboard = nullptr;
		FAssetDocumentCapabilityResult Result = ResolveBehaviorTreeBlackboardRef(DesiredValue, BehaviorTreeRegionPath(Context), Blackboard);
		if (!Result.bSuccess)
		{
			return Result;
		}

		bOutChanged = BehaviorTree->BlackboardAsset != Blackboard;
		if (!Context.bIsDryRun)
		{
			BehaviorTree->BlackboardAsset = Blackboard;
			if (bOutChanged)
			{
				BehaviorTree->MarkPackageDirty();
			}
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("Applied BehaviorTree Blackboard"));
	}

	virtual FAssetDocumentCapabilityResult ExtractRegion(
		const FAssetDocumentRegionContext& Context,
		TSharedPtr<FJsonValue>& OutCurrentValue) const override
	{
		const UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(Context.Asset);
		OutCurrentValue = MakeBlackboardAssetRefValue(BehaviorTree ? BehaviorTree->BlackboardAsset : nullptr);
		return FAssetDocumentCapabilityResult::Success(TEXT("Extracted BehaviorTree Blackboard"));
	}

	virtual FAssetDocumentCapabilityResult DiffRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const override
	{
		UBlackboardData* DesiredBlackboard = nullptr;
		FAssetDocumentCapabilityResult Result = ResolveBehaviorTreeBlackboardRef(DesiredValue, BehaviorTreeRegionPath(Context), DesiredBlackboard);
		if (!Result.bSuccess)
		{
			return Result;
		}

		TSharedPtr<FJsonValue> CurrentValue;
		Result = ExtractRegion(Context, CurrentValue);
		if (!Result.bSuccess)
		{
			return Result;
		}

		const TSharedPtr<FJsonValue> DesiredCanonicalValue = MakeBlackboardAssetRefValue(DesiredBlackboard);
		const bool bSame =
			FAssetDocumentJsonRegionUtils::JsonValueToComparableString(CurrentValue) ==
			FAssetDocumentJsonRegionUtils::JsonValueToComparableString(DesiredCanonicalValue);
		FAssetDocumentJsonRegionUtils::AddDiffEntry(
			OutDiffEntries,
			BehaviorTreeRegionPath(Context),
			bSame ? TEXT("unchanged") : TEXT("changed"),
			CurrentValue,
			DesiredCanonicalValue);
		return FAssetDocumentCapabilityResult::Success(TEXT("Diffed BehaviorTree Blackboard"));
	}

private:
	FName Name;
};

FAssetDocumentTreeRegionAdapter MakeBehaviorTreeSemanticTreeAdapter(FName Name)
{
	FAssetDocumentTreeRegionAdapterConfig TreeConfig;
	TreeConfig.Name = Name;
	TreeConfig.DuplicateNodeIdCode = TEXT("DuplicateBehaviorTreeNodeId");
	TreeConfig.bDuplicateNodeIdUsesSemanticPath = true;
	FAssetDocumentTreeRegionAdapterHooks TreeHooks;
	TreeHooks.ValidateTree = &FBehaviorTreeAssetDocumentMaterializer::ValidateTree;
	TreeHooks.ApplyTree = &FBehaviorTreeAssetDocumentMaterializer::ApplyTree;
	TreeHooks.ExtractTree = &FBehaviorTreeAssetDocumentMaterializer::ExtractTree;
	TreeHooks.DiffTree = &FBehaviorTreeAssetDocumentMaterializer::DiffTree;
	return FAssetDocumentTreeRegionAdapter(TreeConfig, MoveTemp(TreeHooks));
}

class FBehaviorTreeSemanticTreeRegionAdapter final : public IAssetDocumentRegionAdapter
{
public:
	explicit FBehaviorTreeSemanticTreeRegionAdapter(FName InName)
		: Name(InName)
	{
	}

	virtual FName GetName() const override
	{
		return Name;
	}

	virtual bool SupportsRegion(const FAssetDocumentRegionContext& Context) const override
	{
		FAssetDocumentTreeRegionAdapter TreeAdapter = MakeBehaviorTreeSemanticTreeAdapter(Name);
		return TreeAdapter.SupportsRegion(Context);
	}

	virtual TSharedRef<FJsonObject> GetSchemaHint(const FAssetDocumentRegionContext& Context) const override
	{
		FAssetDocumentTreeRegionAdapter TreeAdapter = MakeBehaviorTreeSemanticTreeAdapter(Name);
		TSharedRef<FJsonObject> Schema = TreeAdapter.GetSchemaHint(Context);
		Schema->SetStringField(TEXT("EmptyTree"), TEXT("Root={} with empty Comments"));
		return Schema;
	}

	virtual FAssetDocumentCapabilityResult ValidateRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue) const override
	{
		TSharedPtr<FJsonObject> Tree;
		FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(DesiredValue, BehaviorTreeRegionPath(Context), Tree);
		return Result.bSuccess ? FBehaviorTreeAssetDocumentMaterializer::ValidateTree(Context, Tree.ToSharedRef()) : Result;
	}

	virtual FAssetDocumentCapabilityResult ApplyRegion(
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		bool& bOutChanged) override
	{
		TSharedPtr<FJsonObject> Tree;
		FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(DesiredValue, BehaviorTreeRegionPath(Context), Tree);
		return Result.bSuccess ? FBehaviorTreeAssetDocumentMaterializer::ApplyTree(Context, Tree.ToSharedRef(), bOutChanged) : Result;
	}

	virtual FAssetDocumentCapabilityResult ExtractRegion(
		const FAssetDocumentRegionContext& Context,
		TSharedPtr<FJsonValue>& OutCurrentValue) const override
	{
		TSharedRef<FJsonObject> Tree = MakeShared<FJsonObject>();
		FAssetDocumentCapabilityResult Result = FBehaviorTreeAssetDocumentMaterializer::ExtractTree(Context, Tree);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutCurrentValue = MakeShared<FJsonValueObject>(Tree);
		return FAssetDocumentCapabilityResult::Success(TEXT("Extracted BehaviorTree authored graph source"));
	}

	virtual FAssetDocumentCapabilityResult DiffRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const override
	{
		TSharedPtr<FJsonObject> DesiredTree;
		FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(DesiredValue, BehaviorTreeRegionPath(Context), DesiredTree);
		return Result.bSuccess ? FBehaviorTreeAssetDocumentMaterializer::DiffTree(Context, DesiredTree.ToSharedRef(), OutDiffEntries) : Result;
	}

private:
	FName Name;
};

class FBehaviorTreeStrictEmptyTreeRegionAdapter final : public IAssetDocumentRegionAdapter
{
public:
	explicit FBehaviorTreeStrictEmptyTreeRegionAdapter(FName InName)
		: Name(InName)
	{
	}

	virtual FName GetName() const override
	{
		return Name;
	}

	virtual bool SupportsRegion(const FAssetDocumentRegionContext& Context) const override
	{
		return !Context.Policy || Context.Policy->RegionKind == EAssetDocumentRegionKind::Graph;
	}

	virtual TSharedRef<FJsonObject> GetSchemaHint(const FAssetDocumentRegionContext&) const override
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("Adapter"), GetName().ToString());
		Schema->SetStringField(TEXT("Authoring"), TEXT("strict-empty-until-task-7"));
		return Schema;
	}

	virtual FAssetDocumentCapabilityResult ValidateRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue) const override
	{
		TSharedPtr<FJsonObject> Tree;
		FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(DesiredValue, BehaviorTreeRegionPath(Context), Tree);
		if (!Result.bSuccess)
		{
			return Result;
		}
		return ValidateStrictEmptyTree(Tree, BehaviorTreeRegionPath(Context));
	}

	virtual FAssetDocumentCapabilityResult ApplyRegion(
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		bool& bOutChanged) override
	{
		bOutChanged = false;
		return ValidateRegion(Context, DesiredValue);
	}

	virtual FAssetDocumentCapabilityResult ExtractRegion(
		const FAssetDocumentRegionContext&,
		TSharedPtr<FJsonValue>& OutCurrentValue) const override
	{
		OutCurrentValue = MakeEmptyTreeValue();
		return FAssetDocumentCapabilityResult::Success(TEXT("Extracted empty BehaviorTree Tree"));
	}

	virtual FAssetDocumentCapabilityResult DiffRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const override
	{
		const FAssetDocumentCapabilityResult Result = ValidateRegion(Context, DesiredValue);
		if (!Result.bSuccess)
		{
			return Result;
		}

		const TSharedPtr<FJsonValue> CurrentValue = MakeEmptyTreeValue();
		const bool bSame =
			FAssetDocumentJsonRegionUtils::JsonValueToComparableString(CurrentValue) ==
			FAssetDocumentJsonRegionUtils::JsonValueToComparableString(DesiredValue);
		FAssetDocumentJsonRegionUtils::AddDiffEntry(
			OutDiffEntries,
			BehaviorTreeRegionPath(Context),
			bSame ? TEXT("unchanged") : TEXT("changed"),
			CurrentValue,
			DesiredValue);
		return FAssetDocumentCapabilityResult::Success(TEXT("Diffed empty BehaviorTree Tree"));
	}

private:
	FName Name;
};

class FBehaviorTreeStrictEmptyObjectRegionAdapter final : public IAssetDocumentRegionAdapter
{
public:
	explicit FBehaviorTreeStrictEmptyObjectRegionAdapter(FName InName)
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
		Schema->SetStringField(TEXT("Authoring"), TEXT("strict-empty-until-task-9"));
		return Schema;
	}

	virtual FAssetDocumentCapabilityResult ValidateRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue) const override
	{
		TSharedPtr<FJsonObject> Object;
		FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(DesiredValue, BehaviorTreeRegionPath(Context), Object);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (Object->Values.Num() > 0)
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				BehaviorTreeRegionPath(Context),
				TEXT("UnsupportedBehaviorTreeRegion"),
				TEXT("BehaviorTree EditorLayout materialization is deferred to Task 9"));
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("Validated empty BehaviorTree object region"));
	}

	virtual FAssetDocumentCapabilityResult ApplyRegion(
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		bool& bOutChanged) override
	{
		bOutChanged = false;
		return ValidateRegion(Context, DesiredValue);
	}

	virtual FAssetDocumentCapabilityResult ExtractRegion(
		const FAssetDocumentRegionContext&,
		TSharedPtr<FJsonValue>& OutCurrentValue) const override
	{
		OutCurrentValue = MakeEmptyObjectValue();
		return FAssetDocumentCapabilityResult::Success(TEXT("Extracted empty BehaviorTree object region"));
	}

	virtual FAssetDocumentCapabilityResult DiffRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const override
	{
		const FAssetDocumentCapabilityResult Result = ValidateRegion(Context, DesiredValue);
		if (!Result.bSuccess)
		{
			return Result;
		}
		const TSharedPtr<FJsonValue> CurrentValue = MakeEmptyObjectValue();
		const bool bSame =
			FAssetDocumentJsonRegionUtils::JsonValueToComparableString(CurrentValue) ==
			FAssetDocumentJsonRegionUtils::JsonValueToComparableString(DesiredValue);
		FAssetDocumentJsonRegionUtils::AddDiffEntry(
			OutDiffEntries,
			BehaviorTreeRegionPath(Context),
			bSame ? TEXT("unchanged") : TEXT("changed"),
			CurrentValue,
			DesiredValue);
		return FAssetDocumentCapabilityResult::Success(TEXT("Diffed empty BehaviorTree object region"));
	}

private:
	FName Name;
};

FAssetDocumentCapabilityResult ValidateBehaviorTreeContext(const FAssetDocumentCapabilityContext& Context)
{
	if (Context.Asset && Context.Asset->GetClass() != UBehaviorTree::StaticClass())
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			TEXT("/Body"),
			TEXT("UnsupportedAsset"),
			TEXT("BehaviorTree Body requires an exact UBehaviorTree asset"));
	}
	if (Context.AssetClass && Context.AssetClass != UBehaviorTree::StaticClass())
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			TEXT("/Body"),
			TEXT("UnsupportedAssetClass"),
			TEXT("BehaviorTree Body requires exact UBehaviorTree class"));
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Validated BehaviorTree context"));
}

FAssetDocumentCapabilityResult DispatchBehaviorTreeBody(
	TFunctionRef<FAssetDocumentCapabilityResult(const FAssetDocumentBodyRegionDispatcher&)> Dispatch)
{
	FBehaviorTreeBlackboardRegionAdapter BlackboardAdapter(FBehaviorTreeAssetDocumentProfile::BlackboardRegionAdapterName());
	FBehaviorTreeSemanticTreeRegionAdapter TreeAdapter(FBehaviorTreeAssetDocumentProfile::TreeRegionAdapterName());

	TMap<FName, IAssetDocumentRegionAdapter*> Adapters;
	Adapters.Add(BlackboardAdapter.GetName(), &BlackboardAdapter);
	Adapters.Add(TreeAdapter.GetName(), &TreeAdapter);

	FAssetDocumentBodyRegionDispatcherHooks Hooks;
	Hooks.ValidateCrossRegion = &FBehaviorTreeAssetDocumentMaterializer::ValidateBodyCrossRegion;

	const FBehaviorTreeAssetDocumentProfile Profile;
	const FAssetDocumentBodyRegionDispatcher Dispatcher(
		FBehaviorTreeAssetDocumentProfile::MakeRegionBindings(),
		Profile.GetRegionPolicies(),
		Adapters,
		MoveTemp(Hooks));
	return Dispatch(Dispatcher);
}
}

TArray<FName> FBehaviorTreeAssetDocumentCapability::GetCanonicalBodyKeys()
{
	return {
		TEXT("BlackboardAsset"),
		TEXT("Tree"),
	};
}

FName FBehaviorTreeAssetDocumentCapability::GetName() const
{
	return TEXT("BehaviorTreeBodyCapability");
}

TArray<FName> FBehaviorTreeAssetDocumentCapability::GetInternalAdapterNames() const
{
	return {
		FBehaviorTreeAssetDocumentProfile::BlackboardRegionAdapterName(),
		FBehaviorTreeAssetDocumentProfile::TreeRegionAdapterName(),
	};
}

int32 FBehaviorTreeAssetDocumentCapability::GetApplyOrder() const
{
	return 100;
}

bool FBehaviorTreeAssetDocumentCapability::SupportsAsset(const UObject* Asset) const
{
	return Asset && SupportsClass(Asset->GetClass());
}

bool FBehaviorTreeAssetDocumentCapability::SupportsClass(const UClass* AssetClass) const
{
	return AssetClass == UBehaviorTree::StaticClass();
}

TSharedRef<FJsonObject> FBehaviorTreeAssetDocumentCapability::GetSchemaHint() const
{
	TSharedRef<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetStringField(TEXT("GraphGuid"), TEXT("canonical 32-hex UBehaviorTreeGraph identity"));
	Tree->SetStringField(TEXT("Root"), TEXT("graph-source BehaviorTree node with NodeGuid identity, direct Children, Decorators, Services, and Editor state"));
	Tree->SetStringField(TEXT("Comments"), TEXT("array<BehaviorTreeComment> with stable NodeGuid identity and full UE comment-box state"));

	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("BlackboardAsset"), TEXT("UBlackboardData object path | AssetRef<UBlackboardData> | null"));
	Schema->SetObjectField(TEXT("Tree"), Tree);
	return Schema;
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentCapability::Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	FAssetDocumentCapabilityResult Result = ValidateBehaviorTreeContext(Context);
	if (!Result.bSuccess)
	{
		return Result;
	}
	return RemapDispatcherCompatibilityCodes(DispatchBehaviorTreeBody(
		[&Context, &BodyJson](const FAssetDocumentBodyRegionDispatcher& Dispatcher)
		{
			return Dispatcher.ValidateBody(Context, BodyJson);
		}));
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentCapability::Apply(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson)
{
	FAssetDocumentCapabilityResult Result = ValidateBehaviorTreeContext(Context);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TSet<FName> AppliedRegions;
	return RemapDispatcherCompatibilityCodes(DispatchBehaviorTreeBody(
		[&Context, &BodyJson, &AppliedRegions](const FAssetDocumentBodyRegionDispatcher& Dispatcher)
		{
			return Dispatcher.ApplyBody(Context, BodyJson, AppliedRegions);
		}));
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentCapability::Extract(const FAssetDocumentCapabilityContext& Context, TSharedRef<FJsonObject>& OutBodyJson) const
{
	FAssetDocumentCapabilityResult Result = ValidateBehaviorTreeContext(Context);
	if (!Result.bSuccess)
	{
		return Result;
	}
	return RemapDispatcherCompatibilityCodes(DispatchBehaviorTreeBody(
		[&Context, &OutBodyJson](const FAssetDocumentBodyRegionDispatcher& Dispatcher)
		{
			return Dispatcher.ExtractBody(Context, OutBodyJson);
		}));
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentCapability::Diff(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonValue>& DesiredJson,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	FAssetDocumentCapabilityResult Result = ValidateBehaviorTreeContext(Context);
	if (!Result.bSuccess)
	{
		return Result;
	}
	return RemapDispatcherCompatibilityCodes(DispatchBehaviorTreeBody(
		[&Context, &DesiredJson, &OutDiffEntries](const FAssetDocumentBodyRegionDispatcher& Dispatcher)
		{
			return Dispatcher.DiffBody(Context, DesiredJson, OutDiffEntries);
		}));
}

FName FBehaviorTreeAssetDocumentProfile::BlackboardRegionAdapterName()
{
	return TEXT("BehaviorTreeBlackboardRegionAdapter");
}

FName FBehaviorTreeAssetDocumentProfile::TreeRegionAdapterName()
{
	return TEXT("BehaviorTreeTreeRegionAdapter");
}

FName FBehaviorTreeAssetDocumentProfile::EditorLayoutRegionAdapterName()
{
	return TEXT("BehaviorTreeEditorLayoutRegionAdapter");
}

TArray<FAssetDocumentRegionBinding> FBehaviorTreeAssetDocumentProfile::MakeRegionBindings()
{
	return {
		{TEXT("BlackboardAsset"), TEXT("Body.BlackboardAsset"), BlackboardRegionAdapterName(), 10, false},
		{TEXT("Tree"), TEXT("Body.Tree"), TreeRegionAdapterName(), 20, false},
	};
}

UClass* FBehaviorTreeAssetDocumentProfile::GetExactClass() const
{
	return UBehaviorTree::StaticClass();
}

TSharedRef<FJsonObject> FBehaviorTreeAssetDocumentProfile::GetDocumentShape() const
{
	TSharedRef<FJsonObject> Shape = MakeShared<FJsonObject>();
	Shape->SetStringField(TEXT("Definitions"), TEXT("map<string, Fragment>"));
	Shape->SetStringField(TEXT("Properties"), TEXT("reflected UBehaviorTree properties not owned by Body regions"));
	Shape->SetObjectField(TEXT("Body"), BodyCapability.GetSchemaHint());
	return Shape;
}

TSharedRef<FJsonObject> FBehaviorTreeAssetDocumentProfile::CreateTemplate(const FAssetDocumentTemplateContext& Context) const
{
	TSharedRef<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetStringField(TEXT("GraphGuid"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
	Tree->SetObjectField(TEXT("Root"), MakeShared<FJsonObject>());
	Tree->SetArrayField(TEXT("Comments"), TArray<TSharedPtr<FJsonValue>>());

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetField(TEXT("BlackboardAsset"), MakeShared<FJsonValueNull>());
	Body->SetObjectField(TEXT("Tree"), Tree);

	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetNumberField(TEXT("SchemaVersion"), 1);
	Template->SetStringField(TEXT("Target"), Context.Target);
	Template->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BehaviorTree"));
	Template->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Template->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Body"), Body);
	return Template;
}

TArray<FName> FBehaviorTreeAssetDocumentProfile::GetBodyKeys() const
{
	return FBehaviorTreeAssetDocumentCapability::GetCanonicalBodyKeys();
}

const IAssetDocumentCapability* FBehaviorTreeAssetDocumentProfile::ResolveBodyAdapter(FName BodyKey) const
{
	if (BodyKey == TEXT("Body"))
	{
		return &BodyCapability;
	}

	for (const FName& KnownBodyKey : FBehaviorTreeAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (BodyKey == KnownBodyKey)
		{
			return &BodyCapability;
		}
	}

	return nullptr;
}

TArray<FAssetDocumentRegionPolicy> FBehaviorTreeAssetDocumentProfile::GetRegionPolicies() const
{
	TArray<FAssetDocumentRegionPolicy> Policies;
	Policies.Reserve(2);

	FAssetDocumentRegionPolicy Policy;
	if (BehaviorTreeMakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.BlackboardAsset"), EAssetDocumentRegionKind::Object, {TEXT("BlackboardAsset")}, Policy))
	{
		Policy.ExplicitDeleteValues.Add(FAssetDocumentExplicitDeleteValues::Null());
		Policies.Add(Policy);
	}
	if (BehaviorTreeMakeRegionPolicy(
		TEXT("ManagedRegion"),
		TEXT("Body.Tree"),
		EAssetDocumentRegionKind::Object,
		{TEXT("BTGraph")},
		TEXT("BehaviorTreePostApply"),
		Policy))
	{
		Policies.Add(Policy);
	}

	return Policies;
}
