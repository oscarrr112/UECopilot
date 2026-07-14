// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentBlackboardKeySchemaUtils.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "Regions/AssetDocumentReflectedPropertyUtils.h"
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
#include "BehaviorTree/Blackboard/BlackboardKeyType_Struct.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "UObject/UnrealType.h"
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
		FieldName == TEXT("KeyTypeProperties") ||
		FieldName == TEXT("BaseClass") ||
		FieldName == TEXT("Enum") ||
		FieldName == TEXT("Description") ||
		FieldName == TEXT("Category") ||
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
	if (TrimmedType.Equals(TEXT("Struct"), ESearchCase::IgnoreCase))
	{
		OutClass = UBlackboardKeyType_Struct::StaticClass();
		OutCanonicalType = TEXT("Struct");
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

bool IsAuthoredKeyTypeProperty(const FProperty* Property)
{
	return Property
		&& Property->HasAnyPropertyFlags(CPF_Edit)
		&& !Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditConst);
}

FAssetDocumentCapabilityResult RemapKeyTypePropertyFailure(
	FAssetDocumentCapabilityResult Result,
	const FString& PropertyName)
{
	if (Result.bSuccess)
	{
		return Result;
	}

	for (FAssetDocumentDiagnostic& Diagnostic : Result.Diagnostics)
	{
		if (Diagnostic.Code == TEXT("UnknownProperty"))
		{
			Diagnostic.Code = PropertyName == TEXT("DefaultValue") && Diagnostic.Path.Contains(TEXT("/DefaultValue/Properties/"))
				? TEXT("UnknownBlackboardStructField")
				: TEXT("UnknownBlackboardKeyTypeProperty");
		}
		else if (Diagnostic.Code == TEXT("NonAuthoredProperty") || Diagnostic.Code == TEXT("BodyTreeProperty"))
		{
			Diagnostic.Code = TEXT("NonAuthoredBlackboardKeyTypeProperty");
		}
		else if (Diagnostic.Code == TEXT("UnsupportedProperty"))
		{
			Diagnostic.Code = TEXT("UnsupportedBlackboardKeyTypeProperty");
		}
		else if (Diagnostic.Code == TEXT("InvalidPropertyValue") ||
			Diagnostic.Code == TEXT("InvalidStructRef") ||
			Diagnostic.Code == TEXT("InvalidStructConstraint") ||
			Diagnostic.Code == TEXT("IncompatibleStructType"))
		{
			Diagnostic.Code = PropertyName == TEXT("DefaultValue")
				? TEXT("InvalidBlackboardKeyDefault")
				: TEXT("InvalidBlackboardKeyTypeProperty");
		}
	}
	return Result;
}

FAssetDocumentCapabilityResult ApplyEnumProperties(
	UBlackboardKeyType_Enum* EnumKey,
	const TSharedRef<FJsonObject>& Properties,
	const FString& Path,
	TSet<FString>& OutHandledFields)
{
	OutHandledFields.Add(TEXT("EnumType"));
	OutHandledFields.Add(TEXT("EnumName"));
	OutHandledFields.Add(TEXT("DefaultValue"));
	UEnum* EnumType = EnumKey ? EnumKey->EnumType.Get() : nullptr;
	FString EnumName = EnumKey ? EnumKey->EnumName : FString();
	FAssetDocumentCapabilityResult Failure = FAssetDocumentCapabilityResult::Success(TEXT(""));

	if (Properties->HasField(TEXT("EnumType")))
	{
		FString EnumPath;
		if (!TryRequireReferencePathField(Properties, Path, TEXT("EnumType"), TEXT("InvalidBlackboardKeyTypeProperty"), TEXT("AssetRef"), EnumPath, Failure))
		{
			return Failure.bSuccess
				? FAssetDocumentJsonRegionUtils::Failure(MakeChildPath(Path, TEXT("EnumType")), TEXT("InvalidBlackboardKeyTypeProperty"), TEXT("EnumType is required"))
				: Failure;
		}
		EnumType = Cast<UEnum>(ResolveEnumReference(EnumPath));
		if (!EnumType)
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(Path, TEXT("EnumType")),
				TEXT("InvalidBlackboardKeyTypeProperty"),
				FString::Printf(TEXT("EnumType '%s' could not be resolved"), *EnumPath));
		}
	}

	if (Properties->HasField(TEXT("EnumName")))
	{
		if (!TryRequireStringField(Properties, Path, TEXT("EnumName"), TEXT("InvalidBlackboardKeyTypeProperty"), EnumName, Failure, false))
		{
			return Failure;
		}
		if (!EnumName.IsEmpty())
		{
			UEnum* NamedEnum = Cast<UEnum>(ResolveEnumReference(EnumName));
			if (!NamedEnum || (EnumType && NamedEnum != EnumType))
			{
				return FAssetDocumentJsonRegionUtils::Failure(
					MakeChildPath(Path, TEXT("EnumName")),
					TEXT("InvalidBlackboardKeyTypeProperty"),
					TEXT("EnumName must resolve to the same enum as EnumType"));
			}
			EnumType = NamedEnum;
		}
	}

	if (!EnumType)
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			MakeChildPath(Path, TEXT("EnumType")),
			TEXT("MissingBlackboardKeyEnum"),
			TEXT("Enum blackboard keys require EnumType or EnumName"));
	}
	if (EnumName.IsEmpty())
	{
		EnumName = EnumType->GetPathName();
	}

	uint8 DefaultValue = EnumKey ? EnumKey->DefaultValue : 0;
	if (const TSharedPtr<FJsonValue> DefaultJson = Properties->TryGetField(TEXT("DefaultValue")))
	{
		int64 ResolvedValue = INDEX_NONE;
		FString EnumeratorName;
		if (DefaultJson->TryGetString(EnumeratorName))
		{
			ResolvedValue = EnumType->GetValueByNameString(EnumeratorName);
		}
		else
		{
			double Number = 0.0;
			if (DefaultJson->TryGetNumber(Number) && FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number)))
			{
				ResolvedValue = static_cast<int64>(Number);
			}
		}
		if (ResolvedValue < 0 || ResolvedValue > MAX_uint8 || EnumType->GetNameStringByValue(ResolvedValue).IsEmpty())
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(Path, TEXT("DefaultValue")),
				TEXT("InvalidBlackboardKeyDefault"),
				TEXT("Enum DefaultValue must name a valid uint8 enumerator"));
		}
		DefaultValue = static_cast<uint8>(ResolvedValue);
	}

	EnumKey->EnumType = EnumType;
	EnumKey->EnumName = EnumName;
	EnumKey->DefaultValue = DefaultValue;
	return FAssetDocumentCapabilityResult::Success(TEXT("Applied enum properties"));
}

FAssetDocumentBlackboardKeyLookupEntry MakeLookupEntry(const FBlackboardEntry& Entry, bool bInherited)
{
	FAssetDocumentBlackboardKeyLookupEntry LookupEntry;
	LookupEntry.Name = Entry.EntryName;
	LookupEntry.bInherited = bInherited;

	if (Entry.KeyType)
	{
		UBlackboardKeyType* EffectiveKeyType = Entry.KeyType;
		if (const UBlackboardKeyType_NativeEnum* NativeEnumKey = Cast<UBlackboardKeyType_NativeEnum>(Entry.KeyType))
		{
			UBlackboardKeyType_Enum* CanonicalEnumKey = NewObject<UBlackboardKeyType_Enum>(GetTransientPackage());
			CanonicalEnumKey->EnumType = NativeEnumKey->EnumType;
			CanonicalEnumKey->EnumName = NativeEnumKey->EnumName;
			EffectiveKeyType = CanonicalEnumKey;
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
	if (const TSharedPtr<FJsonValue> KeyTypeClassValue = Json->TryGetField(TEXT("KeyTypeClass"));
		KeyTypeClassValue.IsValid() && KeyTypeClassValue->Type == EJson::Null)
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			MakeChildPath(Path, TEXT("KeyTypeClass")),
			TEXT("NullBlackboardKeyType"),
			TEXT("Blackboard KeyTypeClass cannot be null"));
	}
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

	OutSpec.KeyTypeProperties = MakeShared<FJsonObject>();
	if (Json->HasField(TEXT("KeyTypeProperties")))
	{
		const TSharedPtr<FJsonObject>* KeyTypePropertiesPtr = nullptr;
		if (!Json->TryGetObjectField(TEXT("KeyTypeProperties"), KeyTypePropertiesPtr) ||
			!KeyTypePropertiesPtr || !KeyTypePropertiesPtr->IsValid())
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(Path, TEXT("KeyTypeProperties")),
				TEXT("InvalidBlackboardKeyTypeProperties"),
				TEXT("Blackboard KeyTypeProperties must be an object"));
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*KeyTypePropertiesPtr)->Values)
		{
			OutSpec.KeyTypeProperties->SetField(Pair.Key, Pair.Value);
		}
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
		if (OutSpec.KeyTypeProperties->HasField(TEXT("BaseClass")))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(Path, TEXT("BaseClass")),
				TEXT("ConflictingBlackboardKeyTypeProperty"),
				TEXT("BaseClass cannot be authored both at key level and in KeyTypeProperties"));
		}
		OutSpec.KeyTypeProperties->SetObjectField(TEXT("BaseClass"), MakeReferenceObject(TEXT("ClassRef"), BaseClass));
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
		if (OutSpec.KeyTypeProperties->HasField(TEXT("EnumType")))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(Path, TEXT("Enum")),
				TEXT("ConflictingBlackboardKeyTypeProperty"),
				TEXT("Enum cannot be authored both at key level and in KeyTypeProperties"));
		}
		OutSpec.KeyTypeProperties->SetObjectField(TEXT("EnumType"), MakeReferenceObject(TEXT("AssetRef"), EnumRef));
		OutSpec.KeyTypeProperties->SetStringField(TEXT("EnumName"), EnumRef);
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

	FString Category;
	if (!TryRequireStringField(Json, Path, TEXT("Category"), TEXT("InvalidBlackboardKeyCategory"), Category, FieldFailure, false) &&
		!FieldFailure.bSuccess)
	{
		return FieldFailure;
	}
	OutSpec.Category = FName(*Category);

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
	if (!KeyTypeClass.IsEmpty())
	{
		OutSpec.CanonicalJson->SetObjectField(TEXT("KeyTypeClass"), MakeReferenceObject(TEXT("ClassRef"), KeyTypeClass));
	}
	OutSpec.CanonicalJson->SetObjectField(TEXT("KeyTypeProperties"), OutSpec.KeyTypeProperties.ToSharedRef());
	OutSpec.CanonicalJson->SetStringField(TEXT("Description"), OutSpec.Description);
	OutSpec.CanonicalJson->SetStringField(TEXT("Category"), OutSpec.Category.ToString());
	OutSpec.CanonicalJson->SetBoolField(TEXT("bInstanceSynced"), OutSpec.bInstanceSynced);

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
			if (OutClass->HasAnyClassFlags(CLASS_Abstract | CLASS_NewerVersionExists))
			{
				return FAssetDocumentJsonRegionUtils::Failure(
					MakeChildPath(Path, TEXT("KeyTypeClass")),
					TEXT("InvalidBlackboardKeyType"),
					TEXT("KeyTypeClass must be a concrete current class"));
			}
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
		if (OutClass->HasAnyClassFlags(CLASS_Abstract | CLASS_NewerVersionExists))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(Path, TEXT("Type")),
				TEXT("InvalidBlackboardKeyType"),
				TEXT("Blackboard key Type must resolve to a concrete current class"));
		}
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

	if (OutMetadata.KeyTypeClass->IsChildOf(UBlackboardKeyType_NativeEnum::StaticClass()) ||
		OutMetadata.KeyTypeClass->HasAnyClassFlags(CLASS_Deprecated))
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			MakeChildPath(Path, TEXT("KeyTypeClass")),
			TEXT("DeprecatedBlackboardKeyType"),
			TEXT("UBlackboardKeyType_NativeEnum is deprecated and cannot be used for new input"));
	}

	UBlackboardKeyType* StagedKeyType = NewObject<UBlackboardKeyType>(
		GetTransientPackage(),
		OutMetadata.KeyTypeClass,
		NAME_None,
		RF_Transient);
	if (!StagedKeyType)
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			Path,
			TEXT("BlackboardKeyTypePreviewFailed"),
			TEXT("Failed to instantiate blackboard key type for validation"));
	}
	const FAssetDocumentCapabilityResult PropertiesResult = ApplyKeyTypeProperties(StagedKeyType, Spec, Path);
	if (!PropertiesResult.bSuccess)
	{
		return PropertiesResult;
	}
	if (const UBlackboardKeyType_Object* ObjectKey = Cast<UBlackboardKeyType_Object>(StagedKeyType))
	{
		OutMetadata.BaseClass = ObjectKey->BaseClass;
	}
	else if (const UBlackboardKeyType_Class* ClassKey = Cast<UBlackboardKeyType_Class>(StagedKeyType))
	{
		OutMetadata.BaseClass = ClassKey->BaseClass;
	}
	else if (const UBlackboardKeyType_Enum* EnumKey = Cast<UBlackboardKeyType_Enum>(StagedKeyType))
	{
		OutMetadata.EnumObject = EnumKey->EnumType;
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

FAssetDocumentCapabilityResult FAssetDocumentBlackboardKeySchemaUtils::ApplyKeyTypeProperties(
	UBlackboardKeyType* KeyType,
	const FAssetDocumentBlackboardKeySpec& Spec,
	const FString& Path)
{
	if (!KeyType)
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			MakeChildPath(Path, TEXT("KeyTypeClass")),
			TEXT("NullBlackboardKeyType"),
			TEXT("Blackboard key type instance is required"));
	}
	if (KeyType->IsA<UBlackboardKeyType_NativeEnum>())
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			MakeChildPath(Path, TEXT("KeyTypeClass")),
			TEXT("DeprecatedBlackboardKeyType"),
			TEXT("UBlackboardKeyType_NativeEnum is deprecated and cannot be used for new input"));
	}

	const TSharedRef<FJsonObject> Properties = Spec.KeyTypeProperties.IsValid()
		? Spec.KeyTypeProperties.ToSharedRef()
		: MakeShared<FJsonObject>();
	const FString PropertiesPath = MakeChildPath(Path, TEXT("KeyTypeProperties"));
	TSet<FString> HandledFields;
	if ((KeyType->IsA<UBlackboardKeyType_Object>() || KeyType->IsA<UBlackboardKeyType_Class>()) &&
		!Properties->HasField(TEXT("BaseClass")))
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			MakeChildPath(PropertiesPath, TEXT("BaseClass")),
			TEXT("MissingBlackboardKeyBaseClass"),
			TEXT("Object and Class blackboard keys require an explicitly authored BaseClass"));
	}

	if (UBlackboardKeyType_Enum* EnumKey = Cast<UBlackboardKeyType_Enum>(KeyType))
	{
		FAssetDocumentCapabilityResult EnumResult = ApplyEnumProperties(EnumKey, Properties, PropertiesPath, HandledFields);
		if (!EnumResult.bSuccess)
		{
			return EnumResult;
		}
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
	{
		if (HandledFields.Contains(Pair.Key))
		{
			continue;
		}

		const FString PropertyPath = MakeChildPath(PropertiesPath, Pair.Key);
		FProperty* Property = FindFProperty<FProperty>(KeyType->GetClass(), *Pair.Key);
		if (!Property)
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				PropertyPath,
				TEXT("UnknownBlackboardKeyTypeProperty"),
				FString::Printf(TEXT("Blackboard key type property '%s' does not exist"), *Pair.Key));
		}

		if (Pair.Key == TEXT("bUseDefaultValue") &&
			(KeyType->IsA<UBlackboardKeyType_Vector>() || KeyType->IsA<UBlackboardKeyType_Rotator>()))
		{
			bool bUseDefaultValue = false;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetBool(bUseDefaultValue))
			{
				return FAssetDocumentJsonRegionUtils::Failure(
					PropertyPath,
					TEXT("InvalidBlackboardKeyTypeProperty"),
					TEXT("bUseDefaultValue must be a boolean"));
			}
			if (UBlackboardKeyType_Vector* VectorKey = Cast<UBlackboardKeyType_Vector>(KeyType))
			{
				VectorKey->bUseDefaultValue = bUseDefaultValue;
			}
			else if (UBlackboardKeyType_Rotator* RotatorKey = Cast<UBlackboardKeyType_Rotator>(KeyType))
			{
				RotatorKey->bUseDefaultValue = bUseDefaultValue;
			}
			continue;
		}

		if (!IsAuthoredKeyTypeProperty(Property))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				PropertyPath,
				TEXT("NonAuthoredBlackboardKeyTypeProperty"),
				FString::Printf(TEXT("Blackboard key type property '%s' is not safely editable"), *Pair.Key));
		}

		TSharedRef<FJsonObject> SingleProperty = MakeShared<FJsonObject>();
		SingleProperty->SetField(Pair.Key, Pair.Value);
		FAssetDocumentCapabilityResult PropertyResult = FAssetDocumentReflectedPropertyUtils::ApplyProperties(
			KeyType,
			SingleProperty,
			PropertiesPath);
		PropertyResult = RemapKeyTypePropertyFailure(MoveTemp(PropertyResult), Pair.Key);
		if (!PropertyResult.bSuccess)
		{
			return PropertyResult;
		}
	}

	if (const UBlackboardKeyType_Object* ObjectKey = Cast<UBlackboardKeyType_Object>(KeyType))
	{
		if (!ObjectKey->BaseClass)
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(PropertiesPath, TEXT("BaseClass")),
				TEXT("MissingBlackboardKeyBaseClass"),
				TEXT("Object blackboard keys require BaseClass"));
		}
		if (ObjectKey->DefaultValue && !ObjectKey->DefaultValue->IsA(ObjectKey->BaseClass))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(PropertiesPath, TEXT("DefaultValue")),
				TEXT("InvalidBlackboardKeyDefault"),
				TEXT("Object DefaultValue must be an instance of BaseClass"));
		}
	}
	else if (const UBlackboardKeyType_Class* ClassKey = Cast<UBlackboardKeyType_Class>(KeyType))
	{
		if (!ClassKey->BaseClass)
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(PropertiesPath, TEXT("BaseClass")),
				TEXT("MissingBlackboardKeyBaseClass"),
				TEXT("Class blackboard keys require BaseClass"));
		}
		if (ClassKey->DefaultValue && !ClassKey->DefaultValue->IsChildOf(ClassKey->BaseClass))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				MakeChildPath(PropertiesPath, TEXT("DefaultValue")),
				TEXT("InvalidBlackboardKeyDefault"),
				TEXT("Class DefaultValue must derive from BaseClass"));
		}
	}

#if WITH_EDITOR
	KeyType->PostEditChange();
#endif
	return FAssetDocumentCapabilityResult::Success(TEXT("Applied blackboard key type properties"));
}

FAssetDocumentCapabilityResult FAssetDocumentBlackboardKeySchemaUtils::ExtractKeyTypeProperties(
	UBlackboardKeyType* KeyType,
	TSharedRef<FJsonObject>& OutProperties,
	const FString& Path)
{
	if (!KeyType)
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			Path,
			TEXT("NullBlackboardKeyType"),
			TEXT("Blackboard key type instance is required"));
	}

	FAssetDocumentCapabilityResult ExtractResult = FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(
		KeyType,
		OutProperties,
		Path);
	ExtractResult = RemapKeyTypePropertyFailure(MoveTemp(ExtractResult), TEXT(""));
	if (!ExtractResult.bSuccess)
	{
		return ExtractResult;
	}

	if (const UBlackboardKeyType_Vector* VectorKey = Cast<UBlackboardKeyType_Vector>(KeyType))
	{
		OutProperties->SetBoolField(TEXT("bUseDefaultValue"), VectorKey->bUseDefaultValue);
	}
	else if (const UBlackboardKeyType_Rotator* RotatorKey = Cast<UBlackboardKeyType_Rotator>(KeyType))
	{
		OutProperties->SetBoolField(TEXT("bUseDefaultValue"), RotatorKey->bUseDefaultValue);
	}
	else if (const UBlackboardKeyType_Enum* EnumKey = Cast<UBlackboardKeyType_Enum>(KeyType))
	{
		if (EnumKey->EnumType)
		{
			OutProperties->SetObjectField(TEXT("EnumType"), MakeReferenceObject(TEXT("AssetRef"), EnumKey->EnumType->GetPathName()));
			OutProperties->SetStringField(TEXT("EnumName"), EnumKey->EnumName.IsEmpty() ? EnumKey->EnumType->GetPathName() : EnumKey->EnumName);
			const FString EnumeratorName = EnumKey->EnumType->GetNameStringByValue(EnumKey->DefaultValue);
			OutProperties->SetStringField(TEXT("DefaultValue"), EnumeratorName);
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted blackboard key type properties"));
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

FAssetDocumentCapabilityResult FAssetDocumentBlackboardKeySchemaUtils::ExtractKey(
	const FBlackboardEntry& Entry,
	const FString& Path,
	TSharedPtr<FJsonObject>& OutJson)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("Name"), Entry.EntryName.ToString());

	UBlackboardKeyType* EffectiveKeyType = Entry.KeyType;
	if (!EffectiveKeyType)
	{
		OutJson.Reset();
		return FAssetDocumentJsonRegionUtils::Failure(
			FString::Printf(TEXT("%s/KeyTypeClass"), *Path),
			TEXT("NullBlackboardKeyType"),
			FString::Printf(TEXT("Blackboard key '%s' has no key type instance"), *Entry.EntryName.ToString()));
	}

	if (const UBlackboardKeyType_NativeEnum* NativeEnumKey = Cast<UBlackboardKeyType_NativeEnum>(EffectiveKeyType))
	{
		UBlackboardKeyType_Enum* CanonicalEnumKey = NewObject<UBlackboardKeyType_Enum>(GetTransientPackage());
		CanonicalEnumKey->EnumType = NativeEnumKey->EnumType;
		CanonicalEnumKey->EnumName = NativeEnumKey->EnumName;
		EffectiveKeyType = CanonicalEnumKey;
	}

	Json->SetObjectField(TEXT("KeyTypeClass"), MakeReferenceObject(TEXT("ClassRef"), EffectiveKeyType->GetClass()->GetPathName()));
	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult PropertyResult = ExtractKeyTypeProperties(
		EffectiveKeyType,
		Properties,
		FString::Printf(TEXT("%s/KeyTypeProperties"), *Path));
	if (!PropertyResult.bSuccess)
	{
		OutJson.Reset();
		return PropertyResult;
	}
	Json->SetObjectField(TEXT("KeyTypeProperties"), Properties);

	Json->SetBoolField(TEXT("bInstanceSynced"), Entry.bInstanceSynced != 0);
	Json->SetStringField(TEXT("Description"), Entry.EntryDescription);
	Json->SetStringField(TEXT("Category"), Entry.EntryCategory.ToString());

	OutJson = Json;
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted blackboard key"));
}
