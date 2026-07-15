// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentBlackboardKeyRegionAdapter.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "Regions/AssetDocumentBlackboardKeySchemaUtils.h"

#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_NativeEnum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardData.h"
#include "GameFramework/Actor.h"

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

struct FResolvedBlackboardKeySpec
{
	FAssetDocumentBlackboardKeySpec Spec;
	FAssetDocumentBlackboardResolvedKeyMetadata Metadata;
	FString Path;
};

bool IsEngineDerivedSelfKey(const FBlackboardEntry& Entry)
{
	const UBlackboardKeyType_Object* ObjectKey = Cast<UBlackboardKeyType_Object>(Entry.KeyType);
	return Entry.EntryName == FBlackboard::KeySelf
		&& ObjectKey
		&& ObjectKey->GetClass() == UBlackboardKeyType_Object::StaticClass()
		&& ObjectKey->BaseClass == AActor::StaticClass()
		&& ObjectKey->DefaultValue == nullptr
		&& Entry.EntryDescription.IsEmpty()
		&& Entry.EntryCategory.IsNone()
		&& !Entry.bInstanceSynced;
}

FAssetDocumentCapabilityResult ExtractAuthoredKeys(
	const UBlackboardData* Blackboard,
	const FString& Path,
	TArray<TSharedRef<FJsonObject>>& OutElements)
{
	if (!Blackboard)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Extracted empty authored blackboard keys"));
	}

	for (const FBlackboardEntry& Entry : Blackboard->Keys)
	{
		if (!IsEngineDerivedSelfKey(Entry))
		{
			TSharedPtr<FJsonObject> ExtractedKey;
			const FString KeyPath = FString::Printf(
				TEXT("%s/%s"),
				*Path,
				*FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Entry.EntryName.ToString()));
			const FAssetDocumentCapabilityResult Result = FAssetDocumentBlackboardKeySchemaUtils::ExtractKey(
				Entry,
				KeyPath,
				ExtractedKey);
			if (!Result.bSuccess)
			{
				return Result;
			}
			OutElements.Add(ExtractedKey.ToSharedRef());
		}
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted authored blackboard keys"));
}

FAssetDocumentCapabilityResult ResolveKeySpecs(
	const FAssetDocumentRegionContext& Context,
	const TArray<TSharedRef<FJsonObject>>& Elements,
	TArray<FResolvedBlackboardKeySpec>& OutResolvedSpecs)
{
	TArray<FAssetDocumentBlackboardKeySpec> Specs;
	FAssetDocumentCapabilityResult Result = ParseAndValidateKeys(Context, Elements, Specs);
	if (!Result.bSuccess)
	{
		return Result;
	}

	OutResolvedSpecs.Reset();
	OutResolvedSpecs.Reserve(Specs.Num());
	for (int32 Index = 0; Index < Specs.Num(); ++Index)
	{
		FResolvedBlackboardKeySpec Resolved;
		Resolved.Spec = MoveTemp(Specs[Index]);
		Resolved.Path = MakeKeyPath(Context, Elements[Index], Index);
		Result = FAssetDocumentBlackboardKeySchemaUtils::ResolveKeyMetadata(Resolved.Spec, Resolved.Path, Resolved.Metadata);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutResolvedSpecs.Add(MoveTemp(Resolved));
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Resolved blackboard key specs"));
}

void ApplyResolvedMetadataToKeyType(
	UBlackboardKeyType* KeyType,
	const FAssetDocumentBlackboardResolvedKeyMetadata& Metadata)
{
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
}

FAssetDocumentCapabilityResult BuildBlackboardEntry(
	UBlackboardData* Blackboard,
	const FResolvedBlackboardKeySpec& ResolvedSpec,
	FBlackboardEntry& OutEntry)
{
	const FAssetDocumentBlackboardKeySpec& Spec = ResolvedSpec.Spec;
	const FAssetDocumentBlackboardResolvedKeyMetadata& Metadata = ResolvedSpec.Metadata;
	const FString& Path = ResolvedSpec.Path;

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

	ApplyResolvedMetadataToKeyType(KeyType, Metadata);
	const FAssetDocumentCapabilityResult PropertiesResult = FAssetDocumentBlackboardKeySchemaUtils::ApplyKeyTypeProperties(
		KeyType,
		Spec,
		Path);
	if (!PropertiesResult.bSuccess)
	{
		return PropertiesResult;
	}

	OutEntry = FBlackboardEntry();
	OutEntry.EntryName = Spec.Name;
	OutEntry.KeyType = KeyType;
	OutEntry.EntryDescription = Spec.Description;
	OutEntry.EntryCategory = Spec.Category;
	OutEntry.bInstanceSynced = Spec.bInstanceSynced;
	return FAssetDocumentCapabilityResult::Success(TEXT("Built blackboard key entry"));
}

FBlackboardEntry MakeBlackboardEntryFromResolvedSpec(
	UBlackboardKeyType* KeyType,
	const FResolvedBlackboardKeySpec& ResolvedSpec)
{
	FBlackboardEntry Entry;
	Entry.EntryName = ResolvedSpec.Spec.Name;
	Entry.KeyType = KeyType;
	Entry.EntryDescription = ResolvedSpec.Spec.Description;
	Entry.EntryCategory = ResolvedSpec.Spec.Category;
	Entry.bInstanceSynced = ResolvedSpec.Spec.bInstanceSynced;
	return Entry;
}

FAssetDocumentCapabilityResult BuildSemanticKeyJson(
	const FResolvedBlackboardKeySpec& ResolvedSpec,
	TSharedPtr<FJsonObject>& OutJson)
{
	UBlackboardKeyType* KeyType = NewObject<UBlackboardKeyType>(
		GetTransientPackage(),
		ResolvedSpec.Metadata.KeyTypeClass,
		NAME_None,
		RF_Transient);
	if (!KeyType)
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			ResolvedSpec.Path,
			TEXT("BlackboardKeyTypePreviewFailed"),
			FString::Printf(TEXT("Failed to create preview blackboard key type for '%s'"), *ResolvedSpec.Spec.Name.ToString()));
	}

	ApplyResolvedMetadataToKeyType(KeyType, ResolvedSpec.Metadata);
	const FAssetDocumentCapabilityResult PropertiesResult = FAssetDocumentBlackboardKeySchemaUtils::ApplyKeyTypeProperties(
		KeyType,
		ResolvedSpec.Spec,
		ResolvedSpec.Path);
	if (!PropertiesResult.bSuccess)
	{
		return PropertiesResult;
	}
	return FAssetDocumentBlackboardKeySchemaUtils::ExtractKey(
		MakeBlackboardEntryFromResolvedSpec(KeyType, ResolvedSpec),
		ResolvedSpec.Path,
		OutJson);
}

TSharedPtr<FJsonValue> MakeObjectValue(const TSharedRef<FJsonObject>& Object)
{
	return MakeShared<FJsonValueObject>(Object);
}

TSharedPtr<FJsonValue> MakeObjectArrayValue(const TArray<TSharedRef<FJsonObject>>& Elements)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Elements.Num());
	for (const TSharedRef<FJsonObject>& Element : Elements)
	{
		Values.Add(MakeObjectValue(Element));
	}
	return MakeShared<FJsonValueArray>(MoveTemp(Values));
}

TSharedPtr<FJsonValue> MakeNameArrayValue(const TArray<FString>& Names)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Names.Num());
	for (const FString& Name : Names)
	{
		Values.Add(MakeShared<FJsonValueString>(Name));
	}
	return MakeShared<FJsonValueArray>(MoveTemp(Values));
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

TArray<FString> ExtractKeyNames(const TArray<TSharedRef<FJsonObject>>& Elements)
{
	TArray<FString> Names;
	Names.Reserve(Elements.Num());
	for (const TSharedRef<FJsonObject>& Element : Elements)
	{
		FString Name;
		if (Element->TryGetStringField(TEXT("Name"), Name))
		{
			Names.Add(Name);
		}
	}
	return Names;
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

		TArray<FResolvedBlackboardKeySpec> ResolvedSpecs;
		FAssetDocumentCapabilityResult Result = ResolveKeySpecs(Context, Elements, ResolvedSpecs);
		if (!Result.bSuccess)
		{
			return Result;
		}

		TArray<TSharedRef<FJsonObject>> CurrentElements;
		Result = ExtractAuthoredKeys(Blackboard, RegionPath(Context), CurrentElements);
		if (!Result.bSuccess)
		{
			return Result;
		}
		TArray<TSharedRef<FJsonObject>> DesiredElements;
		for (const FResolvedBlackboardKeySpec& ResolvedSpec : ResolvedSpecs)
		{
			TSharedPtr<FJsonObject> DesiredElement;
			Result = BuildSemanticKeyJson(ResolvedSpec, DesiredElement);
			if (!Result.bSuccess)
			{
				return Result;
			}
			DesiredElements.Add(DesiredElement.ToSharedRef());
		}

		const FString CurrentJson = FAssetDocumentJsonRegionUtils::JsonValueToComparableString(
			MakeObjectArrayValue(CurrentElements));
		const FString DesiredJson = FAssetDocumentJsonRegionUtils::JsonValueToComparableString(
			MakeObjectArrayValue(DesiredElements));

		bOutChanged = CurrentJson != DesiredJson;
		if (Context.bIsDryRun || !bOutChanged)
		{
			return FAssetDocumentCapabilityResult::Success(TEXT("Applied blackboard keys"));
		}

		TArray<FBlackboardEntry> NewEntries;
		NewEntries.Reserve(ResolvedSpecs.Num() + 1);
		if (const FBlackboardEntry* SelfEntry = Blackboard->Keys.FindByPredicate([](const FBlackboardEntry& Entry)
		{
			return IsEngineDerivedSelfKey(Entry);
		}))
		{
			NewEntries.Add(*SelfEntry);
		}
		for (const FResolvedBlackboardKeySpec& ResolvedSpec : ResolvedSpecs)
		{
			FBlackboardEntry Entry;
			Result = BuildBlackboardEntry(Blackboard, ResolvedSpec, Entry);
			if (!Result.bSuccess)
			{
				return Result;
			}
			NewEntries.Add(MoveTemp(Entry));
		}

		Blackboard->Keys = MoveTemp(NewEntries);
		Blackboard->UpdateParentKeys();
		Blackboard->UpdateKeyIDs();
		Blackboard->UpdateIfHasSynchronizedKeys();
		Blackboard->PropagateKeyChangesToDerivedBlackboardAssets();
		Blackboard->MarkPackageDirty();
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

		return ExtractAuthoredKeys(Blackboard, RegionPath(Context), OutElements);
	};

	Hooks.DiffElements = [](
		const FAssetDocumentRegionContext& Context,
		const TArray<TSharedRef<FJsonObject>>& DesiredElements,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
	{
		TArray<FResolvedBlackboardKeySpec> DesiredSpecs;
		FAssetDocumentCapabilityResult Result = ResolveKeySpecs(Context, DesiredElements, DesiredSpecs);
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
		Result = ExtractAuthoredKeys(Blackboard, RegionPath(Context), CurrentElements);
		if (!Result.bSuccess)
		{
			return Result;
		}

		TMap<FString, TSharedRef<FJsonObject>> CurrentByName = MakeKeyMap(CurrentElements);
		const TArray<FString> CurrentOrder = ExtractKeyNames(CurrentElements);
		const TArray<FString> DesiredOrder = [&DesiredSpecs]()
		{
			TArray<FString> Names;
			Names.Reserve(DesiredSpecs.Num());
			for (const FResolvedBlackboardKeySpec& Spec : DesiredSpecs)
			{
				Names.Add(Spec.Spec.Name.ToString());
			}
			return Names;
		}();
		const FString CurrentOrderJson = FAssetDocumentJsonRegionUtils::JsonValueToComparableString(MakeNameArrayValue(CurrentOrder));
		const FString DesiredOrderJson = FAssetDocumentJsonRegionUtils::JsonValueToComparableString(MakeNameArrayValue(DesiredOrder));
		if (CurrentOrderJson != DesiredOrderJson)
		{
			FAssetDocumentJsonRegionUtils::AddDiffEntry(
				OutDiffEntries,
				RegionPath(Context),
				TEXT("changed"),
				MakeNameArrayValue(CurrentOrder),
				MakeNameArrayValue(DesiredOrder));
		}

		TSet<FString> SeenDesiredNames;
		for (int32 Index = 0; Index < DesiredSpecs.Num(); ++Index)
		{
			const FString Name = DesiredSpecs[Index].Spec.Name.ToString();
			SeenDesiredNames.Add(Name);
			const TSharedRef<FJsonObject>* Current = CurrentByName.Find(Name);
			TSharedPtr<FJsonObject> DesiredElement;
			Result = BuildSemanticKeyJson(DesiredSpecs[Index], DesiredElement);
			if (!Result.bSuccess)
			{
				return Result;
			}
			const TSharedPtr<FJsonValue> DesiredValue = MakeObjectValue(DesiredElement.ToSharedRef());
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
