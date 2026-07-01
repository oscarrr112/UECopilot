// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentAnimParentAssetOverrideRegionAdapter.h"

#include "AssetDocumentJsonRegionUtils.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimationAsset.h"
#include "Dom/JsonValue.h"

namespace
{
constexpr const TCHAR* ParentAssetOverridesRegionId = TEXT("Body.ParentAssetOverrides");

FAssetDocumentCapabilityResult Failure(const FString& Path, const FString& Code, const FString& Message)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FString Escape(const FString& Token)
{
	return FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Token);
}

FString GuidToIdentity(const FGuid& Guid)
{
	return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

bool ParseGuidIdentity(const FString& Text, FGuid& OutGuid)
{
	const FString Trimmed = Text.TrimStartAndEnd();
	return !Trimmed.IsEmpty() && FGuid::Parse(Trimmed, OutGuid);
}

FString OverridePath(const FString& GuidIdentity)
{
	return FString::Printf(TEXT("/Body/ParentAssetOverrides/%s"), *Escape(GuidIdentity));
}

TSharedPtr<FJsonValue> MakeAssetRefValue(const UObject* Object)
{
	if (!Object)
	{
		return MakeShared<FJsonValueNull>();
	}

	TSharedRef<FJsonObject> AssetRef = MakeShared<FJsonObject>();
	AssetRef->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	AssetRef->SetStringField(TEXT("Path"), Object->GetPathName());
	return MakeShared<FJsonValueObject>(AssetRef);
}

TSharedRef<FJsonObject> MakeOverrideObject(const FAnimParentNodeAssetOverride& Override)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("ParentNodeGuid"), GuidToIdentity(Override.ParentNodeGuid));
	Object->SetField(TEXT("NewAsset"), MakeAssetRefValue(Override.NewAsset));
	return Object;
}

FAssetDocumentCapabilityResult ResolveAnimationAssetRef(
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	UAnimationAsset*& OutAsset)
{
	OutAsset = nullptr;
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		return Failure(Path, TEXT("MissingParentAssetOverrideAsset"), TEXT("ParentAssetOverrides.NewAsset is required"));
	}
	if (Value->Type != EJson::Object)
	{
		return Failure(Path, TEXT("InvalidParentAssetOverrideAsset"), TEXT("ParentAssetOverrides.NewAsset must be an AssetRef object"));
	}

	const TSharedPtr<FJsonObject> AssetRef = Value->AsObject();
	FString Kind;
	if (!AssetRef.IsValid() || !AssetRef->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("AssetRef"))
	{
		return Failure(Path / TEXT("Kind"), TEXT("InvalidParentAssetOverrideAssetKind"), TEXT("ParentAssetOverrides.NewAsset.Kind must be AssetRef"));
	}

	FString AssetPath;
	if (!AssetRef->TryGetStringField(TEXT("Path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
	{
		if (!AssetRef->TryGetStringField(TEXT("Asset"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
		{
			return Failure(Path / TEXT("Path"), TEXT("MissingParentAssetOverrideAssetPath"), TEXT("ParentAssetOverrides.NewAsset.Path is required"));
		}
	}

	UObject* LoadedObject = StaticLoadObject(UAnimationAsset::StaticClass(), nullptr, *AssetPath);
	OutAsset = Cast<UAnimationAsset>(LoadedObject);
	if (!OutAsset)
	{
		return Failure(
			Path,
			TEXT("UnresolvedParentAssetOverrideAsset"),
			FString::Printf(TEXT("Failed to resolve ParentAssetOverrides.NewAsset '%s' as UAnimationAsset"), *AssetPath));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateOverrideObject(
	const TSharedPtr<FJsonObject>& Object,
	int32 Index,
	TSet<FGuid>& SeenGuids,
	FGuid& OutGuid,
	UAnimationAsset*& OutAsset)
{
	OutGuid.Invalidate();
	OutAsset = nullptr;
	if (!Object.IsValid())
	{
		return Failure(
			FString::Printf(TEXT("/Body/ParentAssetOverrides/%d"), Index),
			TEXT("InvalidParentAssetOverride"),
			TEXT("ParentAssetOverrides entries must be objects"));
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		if (Pair.Key != TEXT("ParentNodeGuid") && Pair.Key != TEXT("NewAsset"))
		{
			return Failure(
				FString::Printf(TEXT("/Body/ParentAssetOverrides/%d/%s"), Index, *Escape(Pair.Key)),
				TEXT("UnknownParentAssetOverrideField"),
				FString::Printf(TEXT("Unknown ParentAssetOverrides field '%s'"), *Pair.Key));
		}
	}

	FString GuidString;
	if (!Object->TryGetStringField(TEXT("ParentNodeGuid"), GuidString) || !ParseGuidIdentity(GuidString, OutGuid))
	{
		return Failure(
			FString::Printf(TEXT("/Body/ParentAssetOverrides/%d/ParentNodeGuid"), Index),
			TEXT("InvalidParentNodeGuid"),
			TEXT("ParentAssetOverrides.ParentNodeGuid must be a GUID string"));
	}

	if (SeenGuids.Contains(OutGuid))
	{
		return Failure(
			FString::Printf(TEXT("/Body/ParentAssetOverrides/%d/ParentNodeGuid"), Index),
			TEXT("DuplicateParentAssetOverrideGuid"),
			FString::Printf(TEXT("Duplicate ParentAssetOverrides identity '%s'"), *GuidToIdentity(OutGuid)));
	}
	SeenGuids.Add(OutGuid);

	return ResolveAnimationAssetRef(
		Object->TryGetField(TEXT("NewAsset")),
		FString::Printf(TEXT("/Body/ParentAssetOverrides/%s/NewAsset"), *GuidToIdentity(OutGuid)),
		OutAsset);
}

FAssetDocumentCapabilityResult ParseDesiredOverrides(
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<FAnimParentNodeAssetOverride>& OutOverrides)
{
	OutOverrides.Reset();
	if (!DesiredValue.IsValid() || DesiredValue->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (DesiredValue->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = DesiredValue->AsObject();
		if (Object.IsValid() && Object->Values.Num() == 0)
		{
			return FAssetDocumentCapabilityResult::Success();
		}
		return Failure(TEXT("/Body/ParentAssetOverrides"), TEXT("InvalidParentAssetOverridesRegionType"), TEXT("Body.ParentAssetOverrides must be an array"));
	}
	if (DesiredValue->Type != EJson::Array)
	{
		return Failure(TEXT("/Body/ParentAssetOverrides"), TEXT("InvalidParentAssetOverridesRegionType"), TEXT("Body.ParentAssetOverrides must be an array"));
	}

	TSet<FGuid> SeenGuids;
	const TArray<TSharedPtr<FJsonValue>>& Values = DesiredValue->AsArray();
	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		const TSharedPtr<FJsonObject> Object = Values[Index].IsValid() ? Values[Index]->AsObject() : nullptr;
		FGuid Guid;
		UAnimationAsset* Asset = nullptr;
		const FAssetDocumentCapabilityResult Result = ValidateOverrideObject(Object, Index, SeenGuids, Guid, Asset);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutOverrides.Add(FAnimParentNodeAssetOverride(Guid, Asset));
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Parsed AnimBlueprint ParentAssetOverrides"));
}

TArray<TSharedPtr<FJsonValue>> MakeOverrideArray(const TArray<FAnimParentNodeAssetOverride>& Overrides)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Overrides.Num());
	for (const FAnimParentNodeAssetOverride& Override : Overrides)
	{
		Values.Add(MakeShared<FJsonValueObject>(MakeOverrideObject(Override)));
	}
	return Values;
}

bool OverridesEqual(const TArray<FAnimParentNodeAssetOverride>& Left, const TArray<FAnimParentNodeAssetOverride>& Right)
{
	if (Left.Num() != Right.Num())
	{
		return false;
	}

	for (int32 Index = 0; Index < Left.Num(); ++Index)
	{
		if (Left[Index].ParentNodeGuid != Right[Index].ParentNodeGuid || Left[Index].NewAsset != Right[Index].NewAsset)
		{
			return false;
		}
	}
	return true;
}

TMap<FGuid, FAnimParentNodeAssetOverride> MakeOverrideMap(const TArray<FAnimParentNodeAssetOverride>& Overrides)
{
	TMap<FGuid, FAnimParentNodeAssetOverride> Map;
	for (const FAnimParentNodeAssetOverride& Override : Overrides)
	{
		Map.Add(Override.ParentNodeGuid, Override);
	}
	return Map;
}
}

FAssetDocumentAnimParentAssetOverrideRegionAdapter::FAssetDocumentAnimParentAssetOverrideRegionAdapter(FName InAdapterName)
	: AdapterName(InAdapterName)
{
}

FName FAssetDocumentAnimParentAssetOverrideRegionAdapter::GetName() const
{
	return AdapterName;
}

bool FAssetDocumentAnimParentAssetOverrideRegionAdapter::SupportsRegion(const FAssetDocumentRegionContext& Context) const
{
	return Context.RegionId == ParentAssetOverridesRegionId;
}

TSharedRef<FJsonObject> FAssetDocumentAnimParentAssetOverrideRegionAdapter::GetSchemaHint(const FAssetDocumentRegionContext&) const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Kind"), TEXT("AnimParentAssetOverrideIdentityArray"));
	Schema->SetStringField(TEXT("Shape"), TEXT("array<{ParentNodeGuid:guid, NewAsset:AssetRef<UAnimationAsset>}>"));
	return Schema;
}

FAssetDocumentCapabilityResult FAssetDocumentAnimParentAssetOverrideRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext&,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	TArray<FAnimParentNodeAssetOverride> Overrides;
	return ParseDesiredOverrides(DesiredValue, Overrides);
}

FAssetDocumentCapabilityResult FAssetDocumentAnimParentAssetOverrideRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	bOutChanged = false;
	TArray<FAnimParentNodeAssetOverride> DesiredOverrides;
	const FAssetDocumentCapabilityResult Result = ParseDesiredOverrides(DesiredValue, DesiredOverrides);
	if (!Result.bSuccess)
	{
		return Result;
	}

	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Context.Asset);
	if (!AnimBlueprint)
	{
		return Result;
	}

	if (!OverridesEqual(AnimBlueprint->ParentAssetOverrides, DesiredOverrides))
	{
		AnimBlueprint->ParentAssetOverrides = MoveTemp(DesiredOverrides);
		bOutChanged = true;
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Applied AnimBlueprint ParentAssetOverrides"));
}

FAssetDocumentCapabilityResult FAssetDocumentAnimParentAssetOverrideRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	const UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Context.Asset);
	OutCurrentValue = MakeShared<FJsonValueArray>(MakeOverrideArray(AnimBlueprint ? AnimBlueprint->ParentAssetOverrides : TArray<FAnimParentNodeAssetOverride>()));
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted AnimBlueprint ParentAssetOverrides"));
}

FAssetDocumentCapabilityResult FAssetDocumentAnimParentAssetOverrideRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	TArray<FAnimParentNodeAssetOverride> DesiredOverrides;
	const FAssetDocumentCapabilityResult Result = ParseDesiredOverrides(DesiredValue, DesiredOverrides);
	if (!Result.bSuccess)
	{
		return Result;
	}

	const UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Context.Asset);
	const TArray<FAnimParentNodeAssetOverride> CurrentOverrides = AnimBlueprint
		? AnimBlueprint->ParentAssetOverrides
		: TArray<FAnimParentNodeAssetOverride>();
	const TMap<FGuid, FAnimParentNodeAssetOverride> CurrentByGuid = MakeOverrideMap(CurrentOverrides);
	const TMap<FGuid, FAnimParentNodeAssetOverride> DesiredByGuid = MakeOverrideMap(DesiredOverrides);

	for (const FAnimParentNodeAssetOverride& DesiredOverride : DesiredOverrides)
	{
		const FAnimParentNodeAssetOverride* CurrentOverride = CurrentByGuid.Find(DesiredOverride.ParentNodeGuid);
		const FString Path = OverridePath(GuidToIdentity(DesiredOverride.ParentNodeGuid));
		if (!CurrentOverride)
		{
			FAssetDocumentJsonRegionUtils::AddDiffEntry(
				OutDiffEntries,
				Path,
				TEXT("added"),
				MakeShared<FJsonValueNull>(),
				MakeShared<FJsonValueObject>(MakeOverrideObject(DesiredOverride)));
			continue;
		}

		if (CurrentOverride->NewAsset != DesiredOverride.NewAsset)
		{
			FAssetDocumentJsonRegionUtils::AddDiffEntry(
				OutDiffEntries,
				Path,
				TEXT("changed"),
				MakeShared<FJsonValueObject>(MakeOverrideObject(*CurrentOverride)),
				MakeShared<FJsonValueObject>(MakeOverrideObject(DesiredOverride)));
		}
	}

	for (const FAnimParentNodeAssetOverride& CurrentOverride : CurrentOverrides)
	{
		if (DesiredByGuid.Contains(CurrentOverride.ParentNodeGuid))
		{
			continue;
		}

		FAssetDocumentJsonRegionUtils::AddDiffEntry(
			OutDiffEntries,
			OverridePath(GuidToIdentity(CurrentOverride.ParentNodeGuid)),
			TEXT("removed"),
			MakeShared<FJsonValueObject>(MakeOverrideObject(CurrentOverride)),
			MakeShared<FJsonValueNull>());
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Diffed AnimBlueprint ParentAssetOverrides"));
}
