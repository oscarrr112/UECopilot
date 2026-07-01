// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentBlackboardKeySchemaUtils.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "Utils/ClassFinderUtils.h"

#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_NativeEnum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "UObject/UObjectIterator.h"

namespace
{
FString MakeChildPath(const FString& Path, const FString& FieldName)
{
	return FString::Printf(
		TEXT("%s/%s"),
		*Path,
		*FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(FieldName));
}

FString MakeDefaultKeyPath(const FAssetDocumentBlackboardKeySpec& Spec)
{
	const FString KeyToken = Spec.Name.IsNone()
		? FString(TEXT("Key"))
		: FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Spec.Name.ToString());
	return FString::Printf(TEXT("/Body/Keys/%s"), *KeyToken);
}

FString TrimmedStringField(const TSharedRef<FJsonObject>& Json, const FString& FieldName)
{
	FString Value;
	if (Json->TryGetStringField(FieldName, Value))
	{
		return Value.TrimStartAndEnd();
	}
	return TEXT("");
}

UClass* ResolveClassReference(const FString& ClassRef, UClass* BaseClass)
{
	const FString TrimmedRef = ClassRef.TrimStartAndEnd();
	if (TrimmedRef.IsEmpty())
	{
		return nullptr;
	}

	if (UClass* FoundClass = FindObject<UClass>(nullptr, *TrimmedRef))
	{
		return FoundClass->IsChildOf(BaseClass) ? FoundClass : nullptr;
	}

	if (UClass* LoadedClass = StaticLoadClass(BaseClass, nullptr, *TrimmedRef))
	{
		return LoadedClass->IsChildOf(BaseClass) ? LoadedClass : nullptr;
	}

	if (UClass* FoundByName = FClassFinderUtils::FindClassByName(TrimmedRef, BaseClass))
	{
		return FoundByName;
	}

	return nullptr;
}

UObject* ResolveEnumReference(const FString& EnumRef)
{
	const FString TrimmedRef = EnumRef.TrimStartAndEnd();
	if (TrimmedRef.IsEmpty())
	{
		return nullptr;
	}

	if (UEnum* LoadedEnum = LoadObject<UEnum>(nullptr, *TrimmedRef))
	{
		return LoadedEnum;
	}

	FString SearchName = TrimmedRef;
	if (!SearchName.StartsWith(TEXT("E")))
	{
		SearchName = TEXT("E") + SearchName;
	}

	for (TObjectIterator<UEnum> It; It; ++It)
	{
		UEnum* Candidate = *It;
		if (Candidate &&
			(Candidate->GetName() == TrimmedRef ||
			Candidate->GetName() == SearchName ||
			Candidate->GetPathName() == TrimmedRef))
		{
			return Candidate;
		}
	}

	return nullptr;
}

bool ResolveAlias(const FString& Type, UClass*& OutClass, FString& OutCanonicalType)
{
	const FString TrimmedType = Type.TrimStartAndEnd();
	if (TrimmedType.Equals(TEXT("Bool"), ESearchCase::IgnoreCase))
	{
		OutClass = UBlackboardKeyType_Bool::StaticClass();
		OutCanonicalType = TEXT("Bool");
		return true;
	}
	if (TrimmedType.Equals(TEXT("Int"), ESearchCase::IgnoreCase))
	{
		OutClass = UBlackboardKeyType_Int::StaticClass();
		OutCanonicalType = TEXT("Int");
		return true;
	}
	if (TrimmedType.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
	{
		OutClass = UBlackboardKeyType_Float::StaticClass();
		OutCanonicalType = TEXT("Float");
		return true;
	}
	if (TrimmedType.Equals(TEXT("String"), ESearchCase::IgnoreCase))
	{
		OutClass = UBlackboardKeyType_String::StaticClass();
		OutCanonicalType = TEXT("String");
		return true;
	}
	if (TrimmedType.Equals(TEXT("Name"), ESearchCase::IgnoreCase))
	{
		OutClass = UBlackboardKeyType_Name::StaticClass();
		OutCanonicalType = TEXT("Name");
		return true;
	}
	if (TrimmedType.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
	{
		OutClass = UBlackboardKeyType_Vector::StaticClass();
		OutCanonicalType = TEXT("Vector");
		return true;
	}
	if (TrimmedType.Equals(TEXT("Rotator"), ESearchCase::IgnoreCase))
	{
		OutClass = UBlackboardKeyType_Rotator::StaticClass();
		OutCanonicalType = TEXT("Rotator");
		return true;
	}
	if (TrimmedType.Equals(TEXT("Object"), ESearchCase::IgnoreCase))
	{
		OutClass = UBlackboardKeyType_Object::StaticClass();
		OutCanonicalType = TEXT("Object");
		return true;
	}
	if (TrimmedType.Equals(TEXT("Class"), ESearchCase::IgnoreCase))
	{
		OutClass = UBlackboardKeyType_Class::StaticClass();
		OutCanonicalType = TEXT("Class");
		return true;
	}
	if (TrimmedType.Equals(TEXT("Enum"), ESearchCase::IgnoreCase))
	{
		OutClass = UBlackboardKeyType_Enum::StaticClass();
		OutCanonicalType = TEXT("Enum");
		return true;
	}

	return false;
}

FString CanonicalTypeFromClass(const UClass* KeyTypeClass)
{
	if (!KeyTypeClass || !KeyTypeClass->IsChildOf(UBlackboardKeyType::StaticClass()))
	{
		return TEXT("");
	}

	FString ClassName = KeyTypeClass->GetName();
	ClassName.RemoveFromStart(TEXT("U"));
	ClassName.RemoveFromStart(TEXT("BlackboardKeyType_"));
	return ClassName;
}

FAssetDocumentBlackboardKeyLookupEntry MakeLookupEntry(const FBlackboardEntry& Entry, bool bInherited)
{
	FAssetDocumentBlackboardKeyLookupEntry LookupEntry;
	LookupEntry.Name = Entry.EntryName;
	LookupEntry.bInherited = bInherited;

	if (Entry.KeyType)
	{
		LookupEntry.KeyTypeClass = Entry.KeyType->GetClass();
		if (const UBlackboardKeyType_Object* ObjectKey = Cast<UBlackboardKeyType_Object>(Entry.KeyType))
		{
			LookupEntry.BaseClass = ObjectKey->BaseClass;
		}
		else if (const UBlackboardKeyType_Class* ClassKey = Cast<UBlackboardKeyType_Class>(Entry.KeyType))
		{
			LookupEntry.BaseClass = ClassKey->BaseClass;
		}
		else if (const UBlackboardKeyType_Enum* EnumKey = Cast<UBlackboardKeyType_Enum>(Entry.KeyType))
		{
			LookupEntry.EnumObject = EnumKey->EnumType;
		}
		else if (const UBlackboardKeyType_NativeEnum* NativeEnumKey = Cast<UBlackboardKeyType_NativeEnum>(Entry.KeyType))
		{
			LookupEntry.EnumObject = NativeEnumKey->EnumType;
		}
	}

	return LookupEntry;
}
}

FAssetDocumentCapabilityResult FAssetDocumentBlackboardKeySchemaUtils::ParseKey(
	const TSharedRef<FJsonObject>& Json,
	const FString& Path,
	FAssetDocumentBlackboardKeySpec& OutSpec)
{
	OutSpec = FAssetDocumentBlackboardKeySpec();

	const FString Name = TrimmedStringField(Json, TEXT("Name"));
	if (Name.IsEmpty())
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			MakeChildPath(Path, TEXT("Name")),
			TEXT("InvalidBlackboardKeyName"),
			TEXT("Blackboard key Name must be a non-empty string"));
	}
	OutSpec.Name = FName(*Name);

	const FString Type = TrimmedStringField(Json, TEXT("Type"));
	const FString KeyTypeClass = TrimmedStringField(Json, TEXT("KeyTypeClass"));
	if (Type.IsEmpty() && KeyTypeClass.IsEmpty())
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			MakeChildPath(Path, TEXT("Type")),
			TEXT("MissingBlackboardKeyType"),
			TEXT("Blackboard key Type or KeyTypeClass is required"));
	}
	OutSpec.Type = Type;

	if (!KeyTypeClass.IsEmpty())
	{
		OutSpec.KeyTypeClass = TSoftClassPtr<UBlackboardKeyType>(FSoftObjectPath(KeyTypeClass));
	}

	const FString BaseClass = TrimmedStringField(Json, TEXT("BaseClass"));
	if (!BaseClass.IsEmpty())
	{
		OutSpec.BaseClass = TSoftClassPtr<UObject>(FSoftObjectPath(BaseClass));
	}

	const FString EnumRef = TrimmedStringField(Json, TEXT("Enum"));
	if (!EnumRef.IsEmpty())
	{
		OutSpec.Enum = TSoftObjectPtr<UObject>(FSoftObjectPath(EnumRef));
	}

	FString Description;
	if (Json->TryGetStringField(TEXT("Description"), Description))
	{
		OutSpec.Description = Description;
	}

	bool bInstanceSynced = false;
	if (Json->TryGetBoolField(TEXT("bInstanceSynced"), bInstanceSynced))
	{
		OutSpec.bInstanceSynced = bInstanceSynced;
	}

	OutSpec.CanonicalJson = MakeShared<FJsonObject>();
	OutSpec.CanonicalJson->SetStringField(TEXT("Name"), Name);
	if (!Type.IsEmpty())
	{
		OutSpec.CanonicalJson->SetStringField(TEXT("Type"), Type);
	}
	if (!KeyTypeClass.IsEmpty())
	{
		OutSpec.CanonicalJson->SetStringField(TEXT("KeyTypeClass"), KeyTypeClass);
	}
	if (!BaseClass.IsEmpty())
	{
		OutSpec.CanonicalJson->SetStringField(TEXT("BaseClass"), BaseClass);
	}
	if (!EnumRef.IsEmpty())
	{
		OutSpec.CanonicalJson->SetStringField(TEXT("Enum"), EnumRef);
	}
	if (!OutSpec.Description.IsEmpty())
	{
		OutSpec.CanonicalJson->SetStringField(TEXT("Description"), OutSpec.Description);
	}
	if (OutSpec.bInstanceSynced)
	{
		OutSpec.CanonicalJson->SetBoolField(TEXT("bInstanceSynced"), true);
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Parsed blackboard key"));
}

FAssetDocumentCapabilityResult FAssetDocumentBlackboardKeySchemaUtils::ResolveKeyTypeClass(
	const FAssetDocumentBlackboardKeySpec& Spec,
	UClass*& OutClass,
	FString& OutCanonicalType)
{
	return ResolveKeyTypeClass(Spec, MakeDefaultKeyPath(Spec), OutClass, OutCanonicalType);
}

FAssetDocumentCapabilityResult FAssetDocumentBlackboardKeySchemaUtils::ResolveKeyTypeClass(
	const FAssetDocumentBlackboardKeySpec& Spec,
	const FString& Path,
	UClass*& OutClass,
	FString& OutCanonicalType)
{
	OutClass = nullptr;
	OutCanonicalType.Empty();

	if (!Spec.KeyTypeClass.IsNull())
	{
		OutClass = Spec.KeyTypeClass.LoadSynchronous();
		if (!OutClass)
		{
			OutClass = ResolveClassReference(
				Spec.KeyTypeClass.ToSoftObjectPath().ToString(),
				UBlackboardKeyType::StaticClass());
		}
		if (OutClass && OutClass->IsChildOf(UBlackboardKeyType::StaticClass()))
		{
			OutCanonicalType = CanonicalTypeFromClass(OutClass);
			return FAssetDocumentCapabilityResult::Success(TEXT("Resolved blackboard key type class"));
		}

		return FAssetDocumentJsonRegionUtils::Failure(
			MakeChildPath(Path, TEXT("KeyTypeClass")),
			TEXT("InvalidBlackboardKeyType"),
			TEXT("KeyTypeClass must resolve to a UBlackboardKeyType subclass"));
	}

	if (ResolveAlias(Spec.Type, OutClass, OutCanonicalType))
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Resolved blackboard key type alias"));
	}

	return FAssetDocumentJsonRegionUtils::Failure(
		MakeChildPath(Path, TEXT("Type")),
		TEXT("InvalidBlackboardKeyType"),
		FString::Printf(TEXT("Unsupported blackboard key Type '%s'"), *Spec.Type));
}

FAssetDocumentCapabilityResult FAssetDocumentBlackboardKeySchemaUtils::ValidateKeySpec(
	const FAssetDocumentBlackboardKeySpec& Spec,
	const FString& Path)
{
	UClass* KeyTypeClass = nullptr;
	FString CanonicalType;
	const FAssetDocumentCapabilityResult ResolveResult = ResolveKeyTypeClass(Spec, Path, KeyTypeClass, CanonicalType);
	if (!ResolveResult.bSuccess)
	{
		return ResolveResult;
	}

	if (KeyTypeClass->IsChildOf(UBlackboardKeyType_Object::StaticClass()) ||
		KeyTypeClass->IsChildOf(UBlackboardKeyType_Class::StaticClass()))
	{
		const FString BaseClassPath = Spec.BaseClass.ToSoftObjectPath().ToString();
		if (BaseClassPath.IsEmpty())
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(Path, TEXT("BaseClass")),
				TEXT("MissingBlackboardKeyBaseClass"),
				TEXT("Object and Class blackboard keys require BaseClass"));
		}
		if (!ResolveClassReference(BaseClassPath, UObject::StaticClass()))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(Path, TEXT("BaseClass")),
				TEXT("InvalidBlackboardKeyBaseClass"),
				FString::Printf(TEXT("BaseClass '%s' could not be resolved"), *BaseClassPath));
		}
	}

	if (KeyTypeClass->IsChildOf(UBlackboardKeyType_Enum::StaticClass()) ||
		KeyTypeClass->IsChildOf(UBlackboardKeyType_NativeEnum::StaticClass()))
	{
		const FString EnumPath = Spec.Enum.ToSoftObjectPath().ToString();
		if (EnumPath.IsEmpty())
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(Path, TEXT("Enum")),
				TEXT("MissingBlackboardKeyEnum"),
				TEXT("Enum blackboard keys require Enum"));
		}
		if (!ResolveEnumReference(EnumPath))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(Path, TEXT("Enum")),
				TEXT("InvalidBlackboardKeyEnum"),
				FString::Printf(TEXT("Enum '%s' could not be resolved"), *EnumPath));
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated blackboard key"));
}

FAssetDocumentCapabilityResult FAssetDocumentBlackboardKeySchemaUtils::ValidateUniqueLocalKeys(
	const TArray<FAssetDocumentBlackboardKeySpec>& Specs,
	const FString& KeysPath)
{
	TSet<FName> SeenNames;
	for (int32 Index = 0; Index < Specs.Num(); ++Index)
	{
		const FAssetDocumentBlackboardKeySpec& Spec = Specs[Index];
		if (Spec.Name.IsNone())
		{
			continue;
		}

		if (SeenNames.Contains(Spec.Name))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				FString::Printf(TEXT("%s/%d/Name"), *KeysPath, Index),
				TEXT("DuplicateBlackboardKey"),
				FString::Printf(TEXT("Duplicate blackboard key '%s'"), *Spec.Name.ToString()));
		}
		SeenNames.Add(Spec.Name);
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated unique blackboard keys"));
}

FAssetDocumentCapabilityResult FAssetDocumentBlackboardKeySchemaUtils::BuildLookup(
	const UBlackboardData* Blackboard,
	TMap<FName, FAssetDocumentBlackboardKeyLookupEntry>& OutLookup)
{
	OutLookup.Reset();
	if (!Blackboard)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Built empty blackboard key lookup"));
	}

	TArray<const UBlackboardData*> Chain;
	TSet<const UBlackboardData*> VisitedBlackboards;
	for (const UBlackboardData* Current = Blackboard; Current; Current = Current->Parent)
	{
		if (VisitedBlackboards.Contains(Current))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				TEXT("/Body/Parent"),
				TEXT("BlackboardParentCycle"),
				TEXT("Blackboard parent chain contains a cycle"));
		}
		VisitedBlackboards.Add(Current);
		Chain.Add(Current);
	}

	for (int32 Index = Chain.Num() - 1; Index >= 0; --Index)
	{
		const UBlackboardData* Current = Chain[Index];
		const bool bInherited = Current != Blackboard;
		for (const FBlackboardEntry& Entry : Current->Keys)
		{
			if (!Entry.EntryName.IsNone())
			{
				OutLookup.Add(Entry.EntryName, MakeLookupEntry(Entry, bInherited));
			}
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Built blackboard key lookup"));
}

bool FAssetDocumentBlackboardKeySchemaUtils::FindKeyInLookup(
	const TMap<FName, FAssetDocumentBlackboardKeyLookupEntry>& Lookup,
	FName KeyName,
	FAssetDocumentBlackboardKeyLookupEntry& OutEntry)
{
	if (const FAssetDocumentBlackboardKeyLookupEntry* Found = Lookup.Find(KeyName))
	{
		OutEntry = *Found;
		return true;
	}
	return false;
}

TSharedRef<FJsonObject> FAssetDocumentBlackboardKeySchemaUtils::ExtractKey(const FBlackboardEntry& Entry)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("Name"), Entry.EntryName.ToString());

	if (Entry.KeyType)
	{
		Json->SetStringField(TEXT("Type"), CanonicalTypeFromClass(Entry.KeyType->GetClass()));

		if (const UBlackboardKeyType_Object* ObjectKey = Cast<UBlackboardKeyType_Object>(Entry.KeyType))
		{
			if (ObjectKey->BaseClass)
			{
				Json->SetStringField(TEXT("BaseClass"), ObjectKey->BaseClass->GetPathName());
			}
		}
		else if (const UBlackboardKeyType_Class* ClassKey = Cast<UBlackboardKeyType_Class>(Entry.KeyType))
		{
			if (ClassKey->BaseClass)
			{
				Json->SetStringField(TEXT("BaseClass"), ClassKey->BaseClass->GetPathName());
			}
		}
		else if (const UBlackboardKeyType_Enum* EnumKey = Cast<UBlackboardKeyType_Enum>(Entry.KeyType))
		{
			if (EnumKey->EnumType)
			{
				Json->SetStringField(TEXT("Enum"), EnumKey->EnumType->GetPathName());
			}
			else if (!EnumKey->EnumName.IsEmpty())
			{
				Json->SetStringField(TEXT("Enum"), EnumKey->EnumName);
			}
		}
		else if (const UBlackboardKeyType_NativeEnum* NativeEnumKey = Cast<UBlackboardKeyType_NativeEnum>(Entry.KeyType))
		{
			if (NativeEnumKey->EnumType)
			{
				Json->SetStringField(TEXT("Enum"), NativeEnumKey->EnumType->GetPathName());
			}
			else if (!NativeEnumKey->EnumName.IsEmpty())
			{
				Json->SetStringField(TEXT("Enum"), NativeEnumKey->EnumName);
			}
		}
	}

	if (Entry.bInstanceSynced)
	{
		Json->SetBoolField(TEXT("bInstanceSynced"), true);
	}
	if (!Entry.EntryDescription.IsEmpty())
	{
		Json->SetStringField(TEXT("Description"), Entry.EntryDescription);
	}

	return Json;
}
