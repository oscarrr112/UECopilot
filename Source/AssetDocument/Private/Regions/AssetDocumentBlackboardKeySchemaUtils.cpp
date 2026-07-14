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

bool TryRequireStringField(
	const TSharedRef<FJsonObject>& Json,
	const FString& Path,
	const FString& FieldName,
	const FString& InvalidCode,
	FString& OutValue,
	FAssetDocumentCapabilityResult& OutFailure,
	bool bTrimValue = true)
{
	OutValue.Empty();
	const TSharedPtr<FJsonValue> Field = Json->TryGetField(FieldName);
	if (!Field.IsValid())
	{
		return false;
	}

	if (Field->Type != EJson::String)
	{
		OutFailure = FAssetDocumentJsonRegionUtils::Failure(
			MakeChildPath(Path, FieldName),
			InvalidCode,
			FString::Printf(TEXT("Blackboard key %s must be a string"), *FieldName));
		return false;
	}

	OutValue = bTrimValue ? Field->AsString().TrimStartAndEnd() : Field->AsString();
	return true;
}

TSharedRef<FJsonObject> MakeReferenceObject(const FString& Kind, const FString& Path)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("Kind"), Kind);
	Json->SetStringField(TEXT("Path"), Path);
	return Json;
}

bool TryRequireReferencePathField(
	const TSharedRef<FJsonObject>& Json,
	const FString& Path,
	const FString& FieldName,
	const FString& InvalidCode,
	const FString& ExpectedKind,
	FString& OutValue,
	FAssetDocumentCapabilityResult& OutFailure)
{
	OutValue.Empty();
	OutFailure = FAssetDocumentCapabilityResult::Success(TEXT(""));
	const TSharedPtr<FJsonValue> Field = Json->TryGetField(FieldName);
	if (!Field.IsValid())
	{
		return false;
	}

	if (Field->Type == EJson::String)
	{
		OutValue = Field->AsString().TrimStartAndEnd();
		return true;
	}

	const TSharedPtr<FJsonObject>* RefObjectPtr = nullptr;
	if (!Field->TryGetObject(RefObjectPtr) || !RefObjectPtr || !RefObjectPtr->IsValid())
	{
		OutFailure = FAssetDocumentJsonRegionUtils::Failure(
			MakeChildPath(Path, FieldName),
			InvalidCode,
			FString::Printf(TEXT("Blackboard key %s must be a string or %s object"), *FieldName, *ExpectedKind));
		return false;
	}

	const TSharedRef<FJsonObject> RefObject = (*RefObjectPtr).ToSharedRef();
	const FString RefPath = MakeChildPath(Path, FieldName);
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : RefObject->Values)
	{
		if (Pair.Key != TEXT("Kind") && Pair.Key != TEXT("Path"))
		{
			OutFailure = FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(RefPath, Pair.Key),
				InvalidCode,
				FString::Printf(TEXT("Unknown %s reference field '%s'"), *FieldName, *Pair.Key));
			return false;
		}
	}

	FString Kind;
	if (!TryRequireStringField(RefObject, RefPath, TEXT("Kind"), InvalidCode, Kind, OutFailure))
	{
		if (!OutFailure.bSuccess)
		{
			return false;
		}
		OutFailure = FAssetDocumentJsonRegionUtils::Failure(
			MakeChildPath(RefPath, TEXT("Kind")),
			InvalidCode,
			FString::Printf(TEXT("Blackboard key %s reference requires Kind"), *FieldName));
		return false;
	}
	if (Kind != ExpectedKind)
	{
		OutFailure = FAssetDocumentJsonRegionUtils::Failure(
			MakeChildPath(RefPath, TEXT("Kind")),
			InvalidCode,
			FString::Printf(TEXT("Blackboard key %s reference Kind must be %s"), *FieldName, *ExpectedKind));
		return false;
	}

	FString ReferencePath;
	if (!TryRequireStringField(RefObject, RefPath, TEXT("Path"), InvalidCode, ReferencePath, OutFailure))
	{
		if (!OutFailure.bSuccess)
		{
			return false;
		}
		OutFailure = FAssetDocumentJsonRegionUtils::Failure(
			MakeChildPath(RefPath, TEXT("Path")),
			InvalidCode,
			FString::Printf(TEXT("Blackboard key %s reference requires Path"), *FieldName));
		return false;
	}
	if (ReferencePath.IsEmpty())
	{
		OutFailure = FAssetDocumentJsonRegionUtils::Failure(
			MakeChildPath(RefPath, TEXT("Path")),
			InvalidCode,
			FString::Printf(TEXT("Blackboard key %s reference Path must be non-empty"), *FieldName));
		return false;
	}

	OutValue = ReferencePath;
	return true;
}

bool TryRequireBoolField(
	const TSharedRef<FJsonObject>& Json,
	const FString& Path,
	const FString& FieldName,
	const FString& InvalidCode,
	bool& OutValue,
	FAssetDocumentCapabilityResult& OutFailure)
{
	OutValue = false;
	const TSharedPtr<FJsonValue> Field = Json->TryGetField(FieldName);
	if (!Field.IsValid())
	{
		return false;
	}

	if (Field->Type != EJson::Boolean)
	{
		OutFailure = FAssetDocumentJsonRegionUtils::Failure(
			MakeChildPath(Path, FieldName),
			InvalidCode,
			FString::Printf(TEXT("Blackboard key %s must be a boolean"), *FieldName));
		return false;
	}

	OutValue = Field->AsBool();
	return true;
}

bool IsKnownKeyField(const FString& FieldName)
{
	return FieldName == TEXT("Name") ||
		FieldName == TEXT("Type") ||
		FieldName == TEXT("KeyTypeClass") ||
		FieldName == TEXT("BaseClass") ||
		FieldName == TEXT("Enum") ||
		FieldName == TEXT("Description") ||
		FieldName == TEXT("bInstanceSynced");
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

bool TryGetAliasTypeForClass(const UClass* KeyTypeClass, FString& OutType)
{
	OutType.Empty();
	if (!KeyTypeClass)
	{
		return false;
	}

	const FString CandidateType = CanonicalTypeFromClass(KeyTypeClass);
	UClass* AliasClass = nullptr;
	FString AliasType;
	if (ResolveAlias(CandidateType, AliasClass, AliasType) && AliasClass == KeyTypeClass)
	{
		OutType = AliasType;
		return true;
	}

	return false;
}

FAssetDocumentBlackboardKeyLookupEntry MakeLookupEntry(const FBlackboardEntry& Entry, bool bInherited)
{
	FAssetDocumentBlackboardKeyLookupEntry LookupEntry;
	LookupEntry.Name = Entry.EntryName;
	LookupEntry.bInherited = bInherited;

	if (Entry.KeyType)
	{
		UBlackboardKeyType* EffectiveKeyType = Entry.KeyType->UpdateDeprecatedKey();
		if (!EffectiveKeyType)
		{
			EffectiveKeyType = Entry.KeyType;
		}

		LookupEntry.KeyType = EffectiveKeyType;
		LookupEntry.KeyTypeClass = EffectiveKeyType->GetClass();
		if (const UBlackboardKeyType_Object* ObjectKey = Cast<UBlackboardKeyType_Object>(EffectiveKeyType))
		{
			LookupEntry.BaseClass = ObjectKey->BaseClass;
		}
		else if (const UBlackboardKeyType_Class* ClassKey = Cast<UBlackboardKeyType_Class>(EffectiveKeyType))
		{
			LookupEntry.BaseClass = ClassKey->BaseClass;
		}
		else if (const UBlackboardKeyType_Enum* EnumKey = Cast<UBlackboardKeyType_Enum>(EffectiveKeyType))
		{
			LookupEntry.EnumObject = EnumKey->EnumType;
		}
		else if (const UBlackboardKeyType_NativeEnum* NativeEnumKey = Cast<UBlackboardKeyType_NativeEnum>(EffectiveKeyType))
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

	FAssetDocumentCapabilityResult FieldFailure = FAssetDocumentCapabilityResult::Success(TEXT(""));
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Json->Values)
	{
		if (!IsKnownKeyField(Field.Key))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(Path, Field.Key),
				TEXT("UnknownBlackboardKeyField"),
				FString::Printf(TEXT("Unknown blackboard key field '%s'"), *Field.Key));
		}
	}

	FString Name;
	const TSharedPtr<FJsonValue> NameField = Json->TryGetField(TEXT("Name"));
	if (!NameField.IsValid())
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			MakeChildPath(Path, TEXT("Name")),
			TEXT("MissingBlackboardKeyName"),
			TEXT("Blackboard key Name is required"));
	}
	if (!TryRequireStringField(Json, Path, TEXT("Name"), TEXT("InvalidBlackboardKeyName"), Name, FieldFailure))
	{
		return FieldFailure;
	}
	if (Name.IsEmpty())
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			MakeChildPath(Path, TEXT("Name")),
			TEXT("InvalidBlackboardKeyName"),
			TEXT("Blackboard key Name must be a non-empty string"));
	}
	OutSpec.Name = FName(*Name);

	FString Type;
	if (!TryRequireStringField(Json, Path, TEXT("Type"), TEXT("InvalidBlackboardKeyType"), Type, FieldFailure) &&
		!FieldFailure.bSuccess)
	{
		return FieldFailure;
	}

	FString KeyTypeClass;
	if (!TryRequireReferencePathField(Json, Path, TEXT("KeyTypeClass"), TEXT("InvalidBlackboardKeyType"), TEXT("ClassRef"), KeyTypeClass, FieldFailure) &&
		!FieldFailure.bSuccess)
	{
		return FieldFailure;
	}
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

	FString BaseClass;
	if (!TryRequireReferencePathField(Json, Path, TEXT("BaseClass"), TEXT("InvalidBlackboardKeyBaseClass"), TEXT("ClassRef"), BaseClass, FieldFailure) &&
		!FieldFailure.bSuccess)
	{
		return FieldFailure;
	}
	if (!BaseClass.IsEmpty())
	{
		OutSpec.BaseClass = TSoftClassPtr<UObject>(FSoftObjectPath(BaseClass));
	}

	FString EnumRef;
	if (!TryRequireReferencePathField(Json, Path, TEXT("Enum"), TEXT("InvalidBlackboardKeyEnum"), TEXT("AssetRef"), EnumRef, FieldFailure) &&
		!FieldFailure.bSuccess)
	{
		return FieldFailure;
	}
	if (!EnumRef.IsEmpty())
	{
		OutSpec.Enum = TSoftObjectPtr<UObject>(FSoftObjectPath(EnumRef));
	}

	FString Description;
	if (!TryRequireStringField(Json, Path, TEXT("Description"), TEXT("InvalidBlackboardKeyDescription"), Description, FieldFailure, false) &&
		!FieldFailure.bSuccess)
	{
		return FieldFailure;
	}
	if (!Description.IsEmpty())
	{
		OutSpec.Description = Description;
	}

	bool bInstanceSynced = false;
	if (!TryRequireBoolField(Json, Path, TEXT("bInstanceSynced"), TEXT("InvalidBlackboardKeyInstanceSynced"), bInstanceSynced, FieldFailure) &&
		!FieldFailure.bSuccess)
	{
		return FieldFailure;
	}
	if (bInstanceSynced)
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
		OutSpec.CanonicalJson->SetObjectField(TEXT("KeyTypeClass"), MakeReferenceObject(TEXT("ClassRef"), KeyTypeClass));
	}
	if (!BaseClass.IsEmpty())
	{
		OutSpec.CanonicalJson->SetObjectField(TEXT("BaseClass"), MakeReferenceObject(TEXT("ClassRef"), BaseClass));
	}
	if (!EnumRef.IsEmpty())
	{
		OutSpec.CanonicalJson->SetObjectField(TEXT("Enum"), MakeReferenceObject(TEXT("AssetRef"), EnumRef));
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
			if (!Spec.Type.IsEmpty())
			{
				UClass* AliasClass = nullptr;
				FString AliasCanonicalType;
				if (!ResolveAlias(Spec.Type, AliasClass, AliasCanonicalType))
				{
					return FAssetDocumentJsonRegionUtils::Failure(
						MakeChildPath(Path, TEXT("Type")),
						TEXT("InvalidBlackboardKeyType"),
						FString::Printf(TEXT("Unsupported blackboard key Type '%s'"), *Spec.Type));
				}

				if (AliasClass != OutClass)
				{
					return FAssetDocumentJsonRegionUtils::Failure(
						MakeChildPath(Path, TEXT("KeyTypeClass")),
						TEXT("ConflictingBlackboardKeyType"),
						FString::Printf(
							TEXT("Type '%s' conflicts with KeyTypeClass '%s'"),
							*Spec.Type,
							*Spec.KeyTypeClass.ToSoftObjectPath().ToString()));
				}

				OutCanonicalType = AliasCanonicalType;
			}
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

FAssetDocumentCapabilityResult FAssetDocumentBlackboardKeySchemaUtils::ResolveKeyMetadata(
	const FAssetDocumentBlackboardKeySpec& Spec,
	const FString& Path,
	FAssetDocumentBlackboardResolvedKeyMetadata& OutMetadata)
{
	OutMetadata = FAssetDocumentBlackboardResolvedKeyMetadata();

	const FAssetDocumentCapabilityResult ResolveResult = ResolveKeyTypeClass(
		Spec,
		Path,
		OutMetadata.KeyTypeClass,
		OutMetadata.CanonicalType);
	if (!ResolveResult.bSuccess)
	{
		return ResolveResult;
	}

	if (OutMetadata.KeyTypeClass->IsChildOf(UBlackboardKeyType_Object::StaticClass()) ||
		OutMetadata.KeyTypeClass->IsChildOf(UBlackboardKeyType_Class::StaticClass()))
	{
		const FString BaseClassPath = Spec.BaseClass.ToSoftObjectPath().ToString();
		if (BaseClassPath.IsEmpty())
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(Path, TEXT("BaseClass")),
				TEXT("MissingBlackboardKeyBaseClass"),
				TEXT("Object and Class blackboard keys require BaseClass"));
		}
		OutMetadata.BaseClass = ResolveClassReference(BaseClassPath, UObject::StaticClass());
		if (!OutMetadata.BaseClass)
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(Path, TEXT("BaseClass")),
				TEXT("InvalidBlackboardKeyBaseClass"),
				FString::Printf(TEXT("BaseClass '%s' could not be resolved"), *BaseClassPath));
		}
	}

	if (OutMetadata.KeyTypeClass->IsChildOf(UBlackboardKeyType_Enum::StaticClass()) ||
		OutMetadata.KeyTypeClass->IsChildOf(UBlackboardKeyType_NativeEnum::StaticClass()))
	{
		const FString EnumPath = Spec.Enum.ToSoftObjectPath().ToString();
		if (EnumPath.IsEmpty())
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(Path, TEXT("Enum")),
				TEXT("MissingBlackboardKeyEnum"),
				TEXT("Enum blackboard keys require Enum"));
		}
		OutMetadata.EnumObject = ResolveEnumReference(EnumPath);
		if (!OutMetadata.EnumObject)
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(Path, TEXT("Enum")),
				TEXT("InvalidBlackboardKeyEnum"),
				FString::Printf(TEXT("Enum '%s' could not be resolved"), *EnumPath));
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Resolved blackboard key metadata"));
}

FAssetDocumentCapabilityResult FAssetDocumentBlackboardKeySchemaUtils::ValidateKeySpec(
	const FAssetDocumentBlackboardKeySpec& Spec,
	const FString& Path)
{
	FAssetDocumentBlackboardResolvedKeyMetadata Metadata;
	const FAssetDocumentCapabilityResult MetadataResult = ResolveKeyMetadata(Spec, Path, Metadata);
	if (!MetadataResult.bSuccess)
	{
		return MetadataResult;
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
		FString AliasType;
		if (TryGetAliasTypeForClass(Entry.KeyType->GetClass(), AliasType))
		{
			Json->SetStringField(TEXT("Type"), AliasType);
		}
		else
		{
			Json->SetObjectField(TEXT("KeyTypeClass"), MakeReferenceObject(TEXT("ClassRef"), Entry.KeyType->GetClass()->GetPathName()));
		}

		if (const UBlackboardKeyType_Object* ObjectKey = Cast<UBlackboardKeyType_Object>(Entry.KeyType))
		{
			if (ObjectKey->BaseClass)
			{
				Json->SetObjectField(TEXT("BaseClass"), MakeReferenceObject(TEXT("ClassRef"), ObjectKey->BaseClass->GetPathName()));
			}
		}
		else if (const UBlackboardKeyType_Class* ClassKey = Cast<UBlackboardKeyType_Class>(Entry.KeyType))
		{
			if (ClassKey->BaseClass)
			{
				Json->SetObjectField(TEXT("BaseClass"), MakeReferenceObject(TEXT("ClassRef"), ClassKey->BaseClass->GetPathName()));
			}
		}
		else if (const UBlackboardKeyType_Enum* EnumKey = Cast<UBlackboardKeyType_Enum>(Entry.KeyType))
		{
			if (EnumKey->EnumType)
			{
				Json->SetObjectField(TEXT("Enum"), MakeReferenceObject(TEXT("AssetRef"), EnumKey->EnumType->GetPathName()));
			}
			else if (!EnumKey->EnumName.IsEmpty())
			{
				Json->SetObjectField(TEXT("Enum"), MakeReferenceObject(TEXT("AssetRef"), EnumKey->EnumName));
			}
		}
		else if (const UBlackboardKeyType_NativeEnum* NativeEnumKey = Cast<UBlackboardKeyType_NativeEnum>(Entry.KeyType))
		{
			if (NativeEnumKey->EnumType)
			{
				Json->SetObjectField(TEXT("Enum"), MakeReferenceObject(TEXT("AssetRef"), NativeEnumKey->EnumType->GetPathName()));
			}
			else if (!NativeEnumKey->EnumName.IsEmpty())
			{
				Json->SetObjectField(TEXT("Enum"), MakeReferenceObject(TEXT("AssetRef"), NativeEnumKey->EnumName));
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
