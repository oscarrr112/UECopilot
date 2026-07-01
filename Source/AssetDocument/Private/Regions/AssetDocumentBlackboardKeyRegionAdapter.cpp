// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentBlackboardKeyRegionAdapter.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "Regions/AssetDocumentBlackboardKeySchemaUtils.h"

#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_NativeEnum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardData.h"

namespace
{
FString RegionPath(const FAssetDocumentRegionContext& Context)
{
	return Context.JsonPointer.IsEmpty() ? Context.BodyPath : Context.JsonPointer;
}

FString MakeKeyPath(const FAssetDocumentRegionContext& Context, const TSharedRef<FJsonObject>& Element, int32 Index)
{
	const TSharedPtr<FJsonValue> NameValue = Element->TryGetField(TEXT("Name"));
	if (NameValue.IsValid() && NameValue->Type == EJson::String)
	{
		const FString Name = NameValue->AsString().TrimStartAndEnd();
		if (!Name.IsEmpty())
		{
			return FString::Printf(TEXT("%s/%s"), *RegionPath(Context), *FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Name));
		}
	}
	return FString::Printf(TEXT("%s/%d"), *RegionPath(Context), Index);
}

FAssetDocumentCapabilityResult ParseAndValidateKey(
	const FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& Element,
	int32 Index,
	FAssetDocumentBlackboardKeySpec& OutSpec)
{
	const FString KeyPath = MakeKeyPath(Context, Element, Index);
	FAssetDocumentCapabilityResult Result = FAssetDocumentBlackboardKeySchemaUtils::ParseKey(Element, KeyPath, OutSpec);
	if (!Result.bSuccess)
	{
		return Result;
	}

	return FAssetDocumentBlackboardKeySchemaUtils::ValidateKeySpec(OutSpec, KeyPath);
}

FAssetDocumentCapabilityResult ParseAndValidateKeys(
	const FAssetDocumentRegionContext& Context,
	const TArray<TSharedRef<FJsonObject>>& Elements,
	TArray<FAssetDocumentBlackboardKeySpec>& OutSpecs)
{
	OutSpecs.Reset();
	OutSpecs.Reserve(Elements.Num());

	for (int32 Index = 0; Index < Elements.Num(); ++Index)
	{
		FAssetDocumentBlackboardKeySpec Spec;
		FAssetDocumentCapabilityResult Result = ParseAndValidateKey(Context, Elements[Index], Index, Spec);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutSpecs.Add(MoveTemp(Spec));
	}

	return FAssetDocumentBlackboardKeySchemaUtils::ValidateUniqueLocalKeys(OutSpecs, RegionPath(Context));
}

FAssetDocumentCapabilityResult BuildBlackboardEntry(
	UBlackboardData* Blackboard,
	const FAssetDocumentBlackboardKeySpec& Spec,
	const FString& Path,
	FBlackboardEntry& OutEntry)
{
	FAssetDocumentBlackboardResolvedKeyMetadata Metadata;
	FAssetDocumentCapabilityResult Result = FAssetDocumentBlackboardKeySchemaUtils::ResolveKeyMetadata(Spec, Path, Metadata);
	if (!Result.bSuccess)
	{
		return Result;
	}

	UBlackboardKeyType* KeyType = NewObject<UBlackboardKeyType>(
		Blackboard,
		Metadata.KeyTypeClass,
		NAME_None,
		RF_Transactional);
	if (!KeyType)
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			Path,
			TEXT("BlackboardKeyTypeCreateFailed"),
			FString::Printf(TEXT("Failed to create blackboard key type for '%s'"), *Spec.Name.ToString()));
	}

	if (UBlackboardKeyType_Object* ObjectKey = Cast<UBlackboardKeyType_Object>(KeyType))
	{
		ObjectKey->BaseClass = Metadata.BaseClass;
	}
	else if (UBlackboardKeyType_Class* ClassKey = Cast<UBlackboardKeyType_Class>(KeyType))
	{
		ClassKey->BaseClass = Metadata.BaseClass;
	}
	else if (UBlackboardKeyType_Enum* EnumKey = Cast<UBlackboardKeyType_Enum>(KeyType))
	{
		EnumKey->EnumType = Cast<UEnum>(Metadata.EnumObject);
	}
	else if (UBlackboardKeyType_NativeEnum* NativeEnumKey = Cast<UBlackboardKeyType_NativeEnum>(KeyType))
	{
		NativeEnumKey->EnumType = Cast<UEnum>(Metadata.EnumObject);
		NativeEnumKey->EnumName = Metadata.EnumObject ? Metadata.EnumObject->GetPathName() : FString();
	}

	OutEntry = FBlackboardEntry();
	OutEntry.EntryName = Spec.Name;
	OutEntry.KeyType = KeyType;
	OutEntry.EntryDescription = Spec.Description;
	OutEntry.bInstanceSynced = Spec.bInstanceSynced;
	return FAssetDocumentCapabilityResult::Success(TEXT("Built blackboard key entry"));
}

TSharedPtr<FJsonValue> MakeObjectValue(const TSharedRef<FJsonObject>& Object)
{
	return MakeShared<FJsonValueObject>(Object);
}

TMap<FString, TSharedRef<FJsonObject>> MakeKeyMap(const TArray<TSharedRef<FJsonObject>>& Elements)
{
	TMap<FString, TSharedRef<FJsonObject>> Result;
	for (const TSharedRef<FJsonObject>& Element : Elements)
	{
		FString Name;
		if (Element->TryGetStringField(TEXT("Name"), Name))
		{
			Result.Add(Name, Element);
		}
	}
	return Result;
}

FString KeyDiffPath(const FAssetDocumentRegionContext& Context, const FString& Name)
{
	return FString::Printf(TEXT("%s/%s"), *RegionPath(Context), *FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Name));
}

FAssetDocumentNamedArrayRegionAdapterConfig MakeBlackboardKeyConfig(FName AdapterName)
{
	FAssetDocumentNamedArrayRegionAdapterConfig Config;
	Config.Name = AdapterName;
	Config.IdentityField = TEXT("Name");
	Config.MissingIdentityCode = TEXT("MissingBlackboardKeyName");
	Config.DuplicateIdentityCode = TEXT("DuplicateBlackboardKey");
	Config.bCanonicalizeByIdentity = false;
	Config.bPreserveAuthoredApplyOrder = true;
	return Config;
}

FAssetDocumentNamedArrayRegionAdapterHooks MakeBlackboardKeyHooks()
{
	FAssetDocumentNamedArrayRegionAdapterHooks Hooks;
	Hooks.ValidateElement = [](
		const FAssetDocumentRegionContext& Context,
		const TSharedRef<FJsonObject>& Element,
		int32 Index)
	{
		FAssetDocumentBlackboardKeySpec Spec;
		return ParseAndValidateKey(Context, Element, Index, Spec);
	};

	Hooks.ApplyElements = [](
		FAssetDocumentRegionContext& Context,
		const TArray<TSharedRef<FJsonObject>>& Elements,
		bool& bOutChanged)
	{
		bOutChanged = false;
		UBlackboardData* Blackboard = Cast<UBlackboardData>(Context.Asset);
		if (!Blackboard)
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				RegionPath(Context),
				TEXT("UnsupportedAsset"),
				TEXT("Blackboard key apply requires UBlackboardData asset"));
		}

		TArray<FAssetDocumentBlackboardKeySpec> Specs;
		FAssetDocumentCapabilityResult Result = ParseAndValidateKeys(Context, Elements, Specs);
		if (!Result.bSuccess)
		{
			return Result;
		}

		TArray<FBlackboardEntry> NewEntries;
		NewEntries.Reserve(Specs.Num());
		for (int32 Index = 0; Index < Specs.Num(); ++Index)
		{
			FBlackboardEntry Entry;
			Result = BuildBlackboardEntry(
				Blackboard,
				Specs[Index],
				MakeKeyPath(Context, Elements[Index], Index),
				Entry);
			if (!Result.bSuccess)
			{
				return Result;
			}
			NewEntries.Add(MoveTemp(Entry));
		}

		TArray<TSharedRef<FJsonObject>> CurrentElements;
		for (const FBlackboardEntry& Entry : Blackboard->Keys)
		{
			CurrentElements.Add(FAssetDocumentBlackboardKeySchemaUtils::ExtractKey(Entry));
		}
		TArray<TSharedRef<FJsonObject>> DesiredElements;
		for (const FAssetDocumentBlackboardKeySpec& Spec : Specs)
		{
			DesiredElements.Add(Spec.CanonicalJson.ToSharedRef());
		}

		const FString CurrentJson = FAssetDocumentJsonRegionUtils::JsonValueToComparableString(
			MakeShared<FJsonValueArray>([&CurrentElements]()
			{
				TArray<TSharedPtr<FJsonValue>> Values;
				for (const TSharedRef<FJsonObject>& Element : CurrentElements)
				{
					Values.Add(MakeObjectValue(Element));
				}
				return Values;
			}()));
		const FString DesiredJson = FAssetDocumentJsonRegionUtils::JsonValueToComparableString(
			MakeShared<FJsonValueArray>([&DesiredElements]()
			{
				TArray<TSharedPtr<FJsonValue>> Values;
				for (const TSharedRef<FJsonObject>& Element : DesiredElements)
				{
					Values.Add(MakeObjectValue(Element));
				}
				return Values;
			}()));

		bOutChanged = CurrentJson != DesiredJson;
		if (!Context.bIsDryRun)
		{
			Blackboard->Keys = MoveTemp(NewEntries);
			if (bOutChanged)
			{
				Blackboard->MarkPackageDirty();
			}
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("Applied blackboard keys"));
	};

	Hooks.ExtractElements = [](const FAssetDocumentRegionContext& Context, TArray<TSharedRef<FJsonObject>>& OutElements)
	{
		const UBlackboardData* Blackboard = Cast<UBlackboardData>(Context.Asset);
		if (!Blackboard)
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				RegionPath(Context),
				TEXT("UnsupportedAsset"),
				TEXT("Blackboard key extract requires UBlackboardData asset"));
		}

		for (const FBlackboardEntry& Entry : Blackboard->Keys)
		{
			OutElements.Add(FAssetDocumentBlackboardKeySchemaUtils::ExtractKey(Entry));
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("Extracted blackboard keys"));
	};

	Hooks.DiffElements = [](
		const FAssetDocumentRegionContext& Context,
		const TArray<TSharedRef<FJsonObject>>& DesiredElements,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
	{
		TArray<FAssetDocumentBlackboardKeySpec> DesiredSpecs;
		FAssetDocumentCapabilityResult Result = ParseAndValidateKeys(Context, DesiredElements, DesiredSpecs);
		if (!Result.bSuccess)
		{
			return Result;
		}

		const UBlackboardData* Blackboard = Cast<UBlackboardData>(Context.Asset);
		if (!Blackboard)
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				RegionPath(Context),
				TEXT("UnsupportedAsset"),
				TEXT("Blackboard key diff requires UBlackboardData asset"));
		}

		TArray<TSharedRef<FJsonObject>> CurrentElements;
		for (const FBlackboardEntry& Entry : Blackboard->Keys)
		{
			CurrentElements.Add(FAssetDocumentBlackboardKeySchemaUtils::ExtractKey(Entry));
		}

		TMap<FString, TSharedRef<FJsonObject>> CurrentByName = MakeKeyMap(CurrentElements);
		TSet<FString> SeenDesiredNames;
		for (int32 Index = 0; Index < DesiredSpecs.Num(); ++Index)
		{
			const FString Name = DesiredSpecs[Index].Name.ToString();
			SeenDesiredNames.Add(Name);
			const TSharedRef<FJsonObject>* Current = CurrentByName.Find(Name);
			const TSharedPtr<FJsonValue> DesiredValue = MakeObjectValue(DesiredSpecs[Index].CanonicalJson.ToSharedRef());
			const TSharedPtr<FJsonValue> CurrentValue = Current ? MakeObjectValue(*Current) : MakeShared<FJsonValueNull>();
			const bool bSame = Current &&
				FAssetDocumentJsonRegionUtils::JsonValueToComparableString(CurrentValue) ==
				FAssetDocumentJsonRegionUtils::JsonValueToComparableString(DesiredValue);
			FAssetDocumentJsonRegionUtils::AddDiffEntry(
				OutDiffEntries,
				KeyDiffPath(Context, Name),
				bSame ? TEXT("unchanged") : TEXT("changed"),
				CurrentValue,
				DesiredValue);
		}

		for (const TSharedRef<FJsonObject>& CurrentElement : CurrentElements)
		{
			FString Name;
			if (CurrentElement->TryGetStringField(TEXT("Name"), Name) && !SeenDesiredNames.Contains(Name))
			{
				FAssetDocumentJsonRegionUtils::AddDiffEntry(
					OutDiffEntries,
					KeyDiffPath(Context, Name),
					TEXT("changed"),
					MakeObjectValue(CurrentElement),
					MakeShared<FJsonValueNull>());
			}
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("Diffed blackboard keys"));
	};

	return Hooks;
}
}

FAssetDocumentBlackboardKeyRegionAdapter::FAssetDocumentBlackboardKeyRegionAdapter(FName InName)
	: InnerAdapter(MakeBlackboardKeyConfig(InName), MakeBlackboardKeyHooks())
{
}

FName FAssetDocumentBlackboardKeyRegionAdapter::GetName() const
{
	return InnerAdapter.GetName();
}

bool FAssetDocumentBlackboardKeyRegionAdapter::SupportsRegion(const FAssetDocumentRegionContext& Context) const
{
	return InnerAdapter.SupportsRegion(Context);
}

TSharedRef<FJsonObject> FAssetDocumentBlackboardKeyRegionAdapter::GetSchemaHint(const FAssetDocumentRegionContext& Context) const
{
	return InnerAdapter.GetSchemaHint(Context);
}

FAssetDocumentCapabilityResult FAssetDocumentBlackboardKeyRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	return InnerAdapter.ValidateRegion(Context, DesiredValue);
}

FAssetDocumentCapabilityResult FAssetDocumentBlackboardKeyRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	return InnerAdapter.ApplyRegion(Context, DesiredValue, bOutChanged);
}

FAssetDocumentCapabilityResult FAssetDocumentBlackboardKeyRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	return InnerAdapter.ExtractRegion(Context, OutCurrentValue);
}

FAssetDocumentCapabilityResult FAssetDocumentBlackboardKeyRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	return InnerAdapter.DiffRegion(Context, DesiredValue, OutDiffEntries);
}
