// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentReflectedPropertyUtils.h"

#include "AssetDocumentCanonicalJson.h"
#include "AssetDocumentJsonRegionUtils.h"
#include "Utils/PropertySetterUtils.h"

#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "Dom/JsonObject.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

namespace
{
const TSet<FName>& GetBodyTreeOwnedPropertyNames()
{
	static const TSet<FName> Names =
	{
		TEXT("Children"),
		TEXT("Services"),
		TEXT("Decorators"),
		TEXT("DecoratorOps"),
		TEXT("RootNode"),
		TEXT("RootDecorators"),
		TEXT("RootDecoratorOps"),
		TEXT("BTGraph")
	};
	return Names;
}

FString JoinPath(const FString& Path, const FString& Token)
{
	return FString::Printf(TEXT("%s/%s"), *Path, *FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Token));
}

FAssetDocumentCapabilityResult Failure(const FString& Path, const FString& Code, const FString& Message)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

bool IsBodyTreeOwnedProperty(const FProperty* Property)
{
	return Property && GetBodyTreeOwnedPropertyNames().Contains(Property->GetFName());
}

bool IsAuthoredEditableProperty(const FProperty* Property)
{
	return Property
		&& Property->HasAnyPropertyFlags(CPF_Edit)
		&& !Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditConst);
}

FString GetNonAuthoredReason(const FProperty* Property)
{
	if (!Property)
	{
		return TEXT("missing");
	}
	if (Property->HasAnyPropertyFlags(CPF_Transient))
	{
		return TEXT("transient");
	}
	if (Property->HasAnyPropertyFlags(CPF_Deprecated))
	{
		return TEXT("deprecated");
	}
	if (Property->HasAnyPropertyFlags(CPF_EditConst))
	{
		return TEXT("edit-const");
	}
	if (!Property->HasAnyPropertyFlags(CPF_Edit))
	{
		return TEXT("non-editable");
	}
	return TEXT("unsupported");
}

bool IsClassReferenceProperty(const FProperty* Property)
{
	if (CastField<FClassProperty>(Property) || CastField<FSoftClassProperty>(Property))
	{
		return true;
	}

	const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
	return ObjectProperty
		&& ObjectProperty->PropertyClass
		&& ObjectProperty->PropertyClass->IsChildOf(UClass::StaticClass());
}

bool IsObjectReferenceProperty(const FProperty* Property)
{
	return !IsClassReferenceProperty(Property)
		&& (CastField<FObjectPropertyBase>(Property) || CastField<FSoftObjectProperty>(Property));
}

TSharedPtr<FJsonObject> MakeReferenceObject(const FString& Kind, const FString& Path)
{
	TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
	Ref->SetStringField(TEXT("Kind"), Kind);
	Ref->SetStringField(TEXT("Path"), Path);
	return Ref;
}

bool TryGetReferencePath(const TSharedPtr<FJsonValue>& Value, const FString& ExpectedKind, FString& OutPath)
{
	if (!Value.IsValid())
	{
		return false;
	}
	if (Value->Type == EJson::Null)
	{
		OutPath.Reset();
		return true;
	}
	if (Value->TryGetString(OutPath))
	{
		return true;
	}

	const TSharedPtr<FJsonObject>* Object = nullptr;
	if (!Value->TryGetObject(Object) || !Object || !Object->IsValid())
	{
		return false;
	}

	FString Kind;
	if (!(*Object)->TryGetStringField(TEXT("Kind"), Kind) || Kind != ExpectedKind)
	{
		return false;
	}

	if ((*Object)->TryGetStringField(TEXT("Path"), OutPath) || (*Object)->TryGetStringField(TEXT("Asset"), OutPath) || (*Object)->TryGetStringField(TEXT("Class"), OutPath))
	{
		OutPath.TrimStartAndEndInline();
		return true;
	}
	return false;
}

TSharedPtr<FJsonValue> NormalizeInputValueForSetter(FProperty* Property, const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return nullptr;
	}

	if (IsClassReferenceProperty(Property))
	{
		FString ClassPath;
		if (TryGetReferencePath(Value, TEXT("ClassRef"), ClassPath))
		{
			return ClassPath.IsEmpty()
				? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>())
				: StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueString>(ClassPath));
		}
	}

	if (IsObjectReferenceProperty(Property))
	{
		FString AssetPath;
		if (TryGetReferencePath(Value, TEXT("AssetRef"), AssetPath))
		{
			return AssetPath.IsEmpty()
				? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>())
				: StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueString>(AssetPath));
		}
	}

	return Value;
}

TSharedPtr<FJsonValue> ExtractReferenceProperty(FProperty* Property, const void* ValuePtr)
{
	if (FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
	{
		const FSoftObjectPtr* SoftPtr = static_cast<const FSoftObjectPtr*>(ValuePtr);
		return SoftPtr && !SoftPtr->IsNull()
			? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueObject>(MakeReferenceObject(TEXT("ClassRef"), SoftPtr->ToSoftObjectPath().ToString())))
			: StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>());
	}

	if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
	{
		UClass* ClassValue = Cast<UClass>(ClassProperty->GetObjectPropertyValue(ValuePtr));
		return ClassValue
			? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueObject>(MakeReferenceObject(TEXT("ClassRef"), ClassValue->GetPathName())))
			: StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>());
	}

	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
		ObjectProperty && ObjectProperty->PropertyClass && ObjectProperty->PropertyClass->IsChildOf(UClass::StaticClass()))
	{
		UClass* ClassValue = Cast<UClass>(ObjectProperty->GetObjectPropertyValue(ValuePtr));
		return ClassValue
			? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueObject>(MakeReferenceObject(TEXT("ClassRef"), ClassValue->GetPathName())))
			: StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>());
	}

	if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
	{
		const FSoftObjectPtr* SoftPtr = static_cast<const FSoftObjectPtr*>(ValuePtr);
		return SoftPtr && !SoftPtr->IsNull()
			? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueObject>(MakeReferenceObject(TEXT("AssetRef"), SoftPtr->ToSoftObjectPath().ToString())))
			: StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>());
	}

	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue(ValuePtr);
		return ObjectValue
			? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueObject>(MakeReferenceObject(TEXT("AssetRef"), ObjectValue->GetPathName())))
			: StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>());
	}

	return nullptr;
}

bool IsBlackboardKeySelectorProperty(FStructProperty* Property)
{
	return Property && Property->Struct == FBlackboardKeySelector::StaticStruct();
}

bool TryGetJsonObject(const TSharedPtr<FJsonValue>& Value, TSharedPtr<FJsonObject>& OutObject)
{
	const TSharedPtr<FJsonObject>* Object = nullptr;
	if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object || !Object->IsValid())
	{
		return false;
	}

	OutObject = *Object;
	return true;
}

UClass* LoadBlackboardKeyTypeClass(const FString& ClassPath)
{
	UClass* LoadedClass = LoadClass<UBlackboardKeyType>(nullptr, *ClassPath);
	if (!LoadedClass)
	{
		LoadedClass = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), nullptr, *ClassPath));
	}

	return LoadedClass && LoadedClass->IsChildOf(UBlackboardKeyType::StaticClass())
		? LoadedClass
		: nullptr;
}

FAssetDocumentCapabilityResult ValidateBlackboardAllowedTypeEntry(
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	UClass*& OutKeyTypeClass,
	TSharedPtr<FJsonObject>& OutProperties)
{
	static const TSet<FString> SupportedFields =
	{
		TEXT("Kind"),
		TEXT("Path"),
		TEXT("Properties")
	};

	OutKeyTypeClass = nullptr;
	OutProperties.Reset();

	TSharedPtr<FJsonObject> EntryObject;
	if (!TryGetJsonObject(Value, EntryObject))
	{
		return Failure(Path, TEXT("InvalidClassRef"), TEXT("AllowedTypes entries must be ClassRef objects"));
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : EntryObject->Values)
	{
		if (!SupportedFields.Contains(Pair.Key))
		{
			return Failure(JoinPath(Path, Pair.Key), TEXT("UnknownProperty"), FString::Printf(TEXT("Unknown AllowedTypes entry field '%s'"), *Pair.Key));
		}
	}

	FString Kind;
	if (!EntryObject->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("ClassRef"))
	{
		return Failure(JoinPath(Path, TEXT("Kind")), TEXT("InvalidClassRef"), TEXT("AllowedTypes entries must use Kind 'ClassRef'"));
	}

	FString ClassPath;
	if (!EntryObject->TryGetStringField(TEXT("Path"), ClassPath))
	{
		return Failure(JoinPath(Path, TEXT("Path")), TEXT("InvalidClassRef"), TEXT("AllowedTypes entries require a class Path"));
	}
	ClassPath.TrimStartAndEndInline();
	if (ClassPath.IsEmpty())
	{
		return Failure(JoinPath(Path, TEXT("Path")), TEXT("InvalidClassRef"), TEXT("AllowedTypes entries require a non-empty class Path"));
	}

	OutKeyTypeClass = LoadBlackboardKeyTypeClass(ClassPath);
	if (!OutKeyTypeClass)
	{
		return Failure(JoinPath(Path, TEXT("Path")), TEXT("InvalidClassRef"), FString::Printf(TEXT("AllowedTypes class '%s' is not a UBlackboardKeyType"), *ClassPath));
	}

	if (EntryObject->HasField(TEXT("Properties")))
	{
		const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
		if (!EntryObject->TryGetObjectField(TEXT("Properties"), PropertiesObject) || !PropertiesObject || !PropertiesObject->IsValid())
		{
			return Failure(JoinPath(Path, TEXT("Properties")), TEXT("InvalidPropertyValue"), TEXT("AllowedTypes Properties must be an object"));
		}

		OutProperties = *PropertiesObject;
		UBlackboardKeyType* ValidationKeyType = NewObject<UBlackboardKeyType>(GetTransientPackage(), OutKeyTypeClass);
		if (!ValidationKeyType)
		{
			return Failure(Path, TEXT("InvalidClassRef"), FString::Printf(TEXT("Failed to instantiate AllowedTypes class '%s'"), *ClassPath));
		}

		const FAssetDocumentCapabilityResult PropertiesResult =
			FAssetDocumentReflectedPropertyUtils::ValidateProperties(ValidationKeyType, OutProperties.ToSharedRef(), JoinPath(Path, TEXT("Properties")));
		if (!PropertiesResult.bSuccess)
		{
			return PropertiesResult;
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateBlackboardKeySelectorJson(const TSharedRef<FJsonObject>& Json, const FString& Path)
{
	static const TSet<FString> SupportedFields =
	{
		TEXT("SelectedKeyName"),
		TEXT("bNoneIsAllowedValue"),
		TEXT("AllowedTypes")
	};

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Json->Values)
	{
		const FString FieldPath = JoinPath(Path, Pair.Key);
		if (!SupportedFields.Contains(Pair.Key))
		{
			return Failure(FieldPath, TEXT("UnknownProperty"), FString::Printf(TEXT("Unknown BlackboardKeySelector field '%s'"), *Pair.Key));
		}

		if (Pair.Key == TEXT("SelectedKeyName"))
		{
			FString Unused;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetString(Unused))
			{
				return Failure(FieldPath, TEXT("InvalidPropertyValue"), TEXT("SelectedKeyName must be a string"));
			}
		}
		else if (Pair.Key == TEXT("bNoneIsAllowedValue"))
		{
			bool Unused = false;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetBool(Unused))
			{
				return Failure(FieldPath, TEXT("InvalidPropertyValue"), TEXT("bNoneIsAllowedValue must be a boolean"));
			}
		}
		else if (Pair.Key == TEXT("AllowedTypes"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetArray(Array))
			{
				return Failure(FieldPath, TEXT("InvalidPropertyValue"), TEXT("AllowedTypes must be an array"));
			}
			for (int32 Index = 0; Index < Array->Num(); ++Index)
			{
				UClass* KeyTypeClass = nullptr;
				TSharedPtr<FJsonObject> KeyTypeProperties;
				const FAssetDocumentCapabilityResult EntryResult = ValidateBlackboardAllowedTypeEntry(
					(*Array)[Index],
					JoinPath(FieldPath, FString::FromInt(Index)),
					KeyTypeClass,
					KeyTypeProperties);
				if (!EntryResult.bSuccess)
				{
					return EntryResult;
				}
			}
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

bool IsPropertyRuntimeSupported(FProperty* Property)
{
	return Property
		&& (IsClassReferenceProperty(Property)
			|| IsObjectReferenceProperty(Property)
			|| CastField<FEnumProperty>(Property)
			|| CastField<FByteProperty>(Property)
			|| CastField<FNumericProperty>(Property)
			|| CastField<FBoolProperty>(Property)
			|| CastField<FStrProperty>(Property)
			|| CastField<FNameProperty>(Property)
			|| CastField<FTextProperty>(Property)
			|| CastField<FStructProperty>(Property)
			|| CastField<FArrayProperty>(Property)
			|| CastField<FMapProperty>(Property)
			|| CastField<FSetProperty>(Property));
}

FAssetDocumentCapabilityResult ValidateAuthoredPropertyValue(FProperty* Property, const TSharedPtr<FJsonValue>& Value, const FString& Path)
{
	if (!Property)
	{
		return Failure(Path, TEXT("InvalidProperty"), TEXT("Property is required"));
	}

	if (!IsPropertyRuntimeSupported(Property))
	{
		return Failure(Path, TEXT("UnsupportedProperty"), FString::Printf(TEXT("Property '%s' is not supported"), *Property->GetName()));
	}

	if (IsClassReferenceProperty(Property) || IsObjectReferenceProperty(Property))
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (IsBlackboardKeySelectorProperty(StructProperty))
		{
			const TSharedPtr<FJsonObject>* SelectorObject = nullptr;
			return Value.IsValid() && Value->TryGetObject(SelectorObject) && SelectorObject && SelectorObject->IsValid()
				? ValidateBlackboardKeySelectorJson((*SelectorObject).ToSharedRef(), Path)
				: FAssetDocumentCapabilityResult::Success();
		}

		const TSharedPtr<FJsonObject>* StructObject = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(StructObject) || !StructObject || !StructObject->IsValid())
		{
			return FAssetDocumentCapabilityResult::Success();
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*StructObject)->Values)
		{
			const FString FieldPath = JoinPath(Path, Pair.Key);
			FProperty* FieldProperty = FindFProperty<FProperty>(StructProperty->Struct, *Pair.Key);
			if (!FieldProperty)
			{
				return Failure(FieldPath, TEXT("UnknownProperty"), FString::Printf(TEXT("Struct field '%s' does not exist"), *Pair.Key));
			}
			if (!IsAuthoredEditableProperty(FieldProperty))
			{
				return Failure(FieldPath, TEXT("NonAuthoredProperty"), FString::Printf(TEXT("Struct field '%s' is not authored: %s"), *Pair.Key, *GetNonAuthoredReason(FieldProperty)));
			}

			const FAssetDocumentCapabilityResult FieldResult = ValidateAuthoredPropertyValue(FieldProperty, Pair.Value, FieldPath);
			if (!FieldResult.bSuccess)
			{
				return FieldResult;
			}
		}
		return FAssetDocumentCapabilityResult::Success();
	}

	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		const TArray<TSharedPtr<FJsonValue>>* ArrayValues = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(ArrayValues))
		{
			return FAssetDocumentCapabilityResult::Success();
		}

		for (int32 Index = 0; Index < ArrayValues->Num(); ++Index)
		{
			const FAssetDocumentCapabilityResult ElementResult =
				ValidateAuthoredPropertyValue(ArrayProperty->Inner, (*ArrayValues)[Index], JoinPath(Path, FString::FromInt(Index)));
			if (!ElementResult.bSuccess)
			{
				return ElementResult;
			}
		}
		return FAssetDocumentCapabilityResult::Success();
	}

	if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		const TSharedPtr<FJsonObject>* MapObject = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(MapObject) || !MapObject || !MapObject->IsValid())
		{
			return FAssetDocumentCapabilityResult::Success();
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*MapObject)->Values)
		{
			const FAssetDocumentCapabilityResult ValueResult =
				ValidateAuthoredPropertyValue(MapProperty->ValueProp, Pair.Value, JoinPath(Path, Pair.Key));
			if (!ValueResult.bSuccess)
			{
				return ValueResult;
			}
		}
		return FAssetDocumentCapabilityResult::Success();
	}

	if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		const TArray<TSharedPtr<FJsonValue>>* SetValues = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(SetValues))
		{
			return FAssetDocumentCapabilityResult::Success();
		}

		for (int32 Index = 0; Index < SetValues->Num(); ++Index)
		{
			const FAssetDocumentCapabilityResult ElementResult =
				ValidateAuthoredPropertyValue(SetProperty->ElementProp, (*SetValues)[Index], JoinPath(Path, FString::FromInt(Index)));
			if (!ElementResult.bSuccess)
			{
				return ElementResult;
			}
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractSingleProperty(FProperty* Property, const void* ValuePtr, TSharedPtr<FJsonValue>& OutValue, const FString& Path)
{
	if (!Property || !ValuePtr)
	{
		return Failure(Path, TEXT("InvalidProperty"), TEXT("Property and value are required"));
	}

	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property); IsBlackboardKeySelectorProperty(StructProperty))
	{
		return FAssetDocumentReflectedPropertyUtils::ExtractBlackboardKeySelector(StructProperty, ValuePtr, OutValue, Path);
	}

	if (IsClassReferenceProperty(Property) || IsObjectReferenceProperty(Property))
	{
		OutValue = ExtractReferenceProperty(Property, ValuePtr);
		return OutValue.IsValid()
			? FAssetDocumentCapabilityResult::Success()
			: Failure(Path, TEXT("UnsupportedProperty"), FString::Printf(TEXT("Property '%s' could not be extracted"), *Property->GetName()));
	}

	OutValue = FPropertySetterUtils::ExtractPropertyToJson(Property, ValuePtr);
	return OutValue.IsValid()
		? FAssetDocumentCapabilityResult::Success()
		: Failure(Path, TEXT("UnsupportedProperty"), FString::Printf(TEXT("Property '%s' is not supported"), *Property->GetName()));
}

FAssetDocumentCapabilityResult ProcessProperties(UObject* Object, const TSharedRef<FJsonObject>& Properties, const FString& Path, bool bApply)
{
	if (!Object)
	{
		return Failure(Path, TEXT("InvalidObject"), TEXT("Object is required"));
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
	{
		const FString PropertyPath = JoinPath(Path, Pair.Key);
		FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), *Pair.Key);
		if (!Property)
		{
			return Failure(PropertyPath, TEXT("UnknownProperty"), FString::Printf(TEXT("Property '%s' does not exist"), *Pair.Key));
		}

		if (IsBodyTreeOwnedProperty(Property))
		{
			return Failure(PropertyPath, TEXT("BodyTreeProperty"), FString::Printf(TEXT("Property '%s' is owned by Body.Tree"), *Pair.Key));
		}

		if (!IsAuthoredEditableProperty(Property))
		{
			return Failure(PropertyPath, TEXT("NonAuthoredProperty"), FString::Printf(TEXT("Property '%s' is not authored: %s"), *Pair.Key, *GetNonAuthoredReason(Property)));
		}

		const TSharedPtr<FJsonValue> ValueToApply = NormalizeInputValueForSetter(Property, Pair.Value);
		if (!ValueToApply.IsValid())
		{
			return Failure(PropertyPath, TEXT("InvalidPropertyValue"), FString::Printf(TEXT("Property '%s' value is invalid"), *Pair.Key));
		}

		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property); IsBlackboardKeySelectorProperty(StructProperty))
		{
			const TSharedPtr<FJsonObject>* SelectorObject = nullptr;
			if (!ValueToApply->TryGetObject(SelectorObject) || !SelectorObject || !SelectorObject->IsValid())
			{
				return Failure(PropertyPath, TEXT("InvalidPropertyValue"), TEXT("BlackboardKeySelector must be an object"));
			}

			const FAssetDocumentCapabilityResult SelectorResult = bApply
				? FAssetDocumentReflectedPropertyUtils::ApplyBlackboardKeySelector(StructProperty, Property->ContainerPtrToValuePtr<void>(Object), (*SelectorObject).ToSharedRef(), PropertyPath)
				: ValidateBlackboardKeySelectorJson((*SelectorObject).ToSharedRef(), PropertyPath);
			if (!SelectorResult.bSuccess)
			{
				return SelectorResult;
			}
			continue;
		}

		const FAssetDocumentCapabilityResult SupportedResult = ValidateAuthoredPropertyValue(Property, Pair.Value, PropertyPath);
		if (!SupportedResult.bSuccess)
		{
			return SupportedResult;
		}

		UObject* TargetObject = Object;
		TStrongObjectPtr<UObject> Duplicate;
		if (!bApply)
		{
			Duplicate.Reset(DuplicateObject<UObject>(Object, GetTransientPackage()));
			TargetObject = Duplicate.Get();
			if (!TargetObject)
			{
				return Failure(Path, TEXT("PreflightFailed"), TEXT("Failed to create property validation duplicate"));
			}
			Property = FindFProperty<FProperty>(TargetObject->GetClass(), *Pair.Key);
		}

		if (!FPropertySetterUtils::SetPropertyFromJson(TargetObject, Property, ValueToApply))
		{
			return Failure(PropertyPath, TEXT("InvalidPropertyValue"), FString::Printf(TEXT("Failed to set property '%s'"), *Pair.Key));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}
}

FAssetDocumentCapabilityResult FAssetDocumentReflectedPropertyUtils::ValidateProperties(
	UObject* Object,
	const TSharedRef<FJsonObject>& Properties,
	const FString& Path)
{
	return ProcessProperties(Object, Properties, Path, false);
}

FAssetDocumentCapabilityResult FAssetDocumentReflectedPropertyUtils::ApplyProperties(
	UObject* Object,
	const TSharedRef<FJsonObject>& Properties,
	const FString& Path)
{
	const FAssetDocumentCapabilityResult ValidateResult = ValidateProperties(Object, Properties, Path);
	if (!ValidateResult.bSuccess)
	{
		return ValidateResult;
	}
	return ProcessProperties(Object, Properties, Path, true);
}

FAssetDocumentCapabilityResult FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(
	UObject* Object,
	TSharedRef<FJsonObject>& OutProperties,
	const FString& Path)
{
	if (!Object)
	{
		return Failure(Path, TEXT("InvalidObject"), TEXT("Object is required"));
	}

	for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
	{
		FProperty* Property = *It;
		if (IsBodyTreeOwnedProperty(Property) || !IsAuthoredEditableProperty(Property))
		{
			continue;
		}

		TSharedPtr<FJsonValue> ExtractedValue;
		const FString PropertyPath = JoinPath(Path, Property->GetName());
		const FAssetDocumentCapabilityResult ExtractResult =
			ExtractSingleProperty(Property, Property->ContainerPtrToValuePtr<void>(Object), ExtractedValue, PropertyPath);
		if (!ExtractResult.bSuccess)
		{
			return ExtractResult;
		}
		if (ExtractedValue.IsValid())
		{
			OutProperties->SetField(Property->GetName(), ExtractedValue);
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FAssetDocumentReflectedPropertyUtils::DiffProperties(
	UObject* Object,
	const TSharedRef<FJsonObject>& DesiredProperties,
	const FString& Path,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
{
	if (!Object)
	{
		return Failure(Path, TEXT("InvalidObject"), TEXT("Object is required"));
	}

	TSharedRef<FJsonObject> CurrentProperties = MakeShared<FJsonObject>();
	FAssetDocumentCapabilityResult Result = ExtractAuthoredProperties(Object, CurrentProperties, Path);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TStrongObjectPtr<UObject> DesiredObject(DuplicateObject<UObject>(Object, GetTransientPackage()));
	if (!DesiredObject.IsValid())
	{
		return Failure(Path, TEXT("PreflightFailed"), TEXT("Failed to create property diff duplicate"));
	}

	Result = ApplyProperties(DesiredObject.Get(), DesiredProperties, Path);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TSharedRef<FJsonObject> DesiredCanonicalProperties = MakeShared<FJsonObject>();
	Result = ExtractAuthoredProperties(DesiredObject.Get(), DesiredCanonicalProperties, Path);
	if (!Result.bSuccess)
	{
		return Result;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : DesiredProperties->Values)
	{
		const FString PropertyPath = JoinPath(Path, Pair.Key);
		const TSharedPtr<FJsonValue> CurrentValue = CurrentProperties->TryGetField(Pair.Key);
		const TSharedPtr<FJsonValue> DesiredValue = DesiredCanonicalProperties->TryGetField(Pair.Key);
		if (FAssetDocumentCanonicalJson::WriteCanonicalJson(CurrentValue.IsValid() ? CurrentValue : MakeShared<FJsonValueNull>())
			!= FAssetDocumentCanonicalJson::WriteCanonicalJson(DesiredValue.IsValid() ? DesiredValue : MakeShared<FJsonValueNull>()))
		{
			FAssetDocumentJsonRegionUtils::AddDiffEntry(OutDiffEntries, PropertyPath, TEXT("changed"), CurrentValue, DesiredValue);
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FAssetDocumentReflectedPropertyUtils::ApplyBlackboardKeySelector(
	FStructProperty* Property,
	void* ValuePtr,
	const TSharedRef<FJsonObject>& Json,
	const FString& Path)
{
	if (!IsBlackboardKeySelectorProperty(Property) || !ValuePtr)
	{
		return Failure(Path, TEXT("InvalidProperty"), TEXT("FBlackboardKeySelector property and value are required"));
	}

	const FAssetDocumentCapabilityResult ValidateResult = ValidateBlackboardKeySelectorJson(Json, Path);
	if (!ValidateResult.bSuccess)
	{
		return ValidateResult;
	}

	FArrayProperty* AllowedTypesProperty = nullptr;
	FObjectPropertyBase* InnerObjectProperty = nullptr;
	TArray<UBlackboardKeyType*> PreparedAllowedTypes;
	const bool bApplyAllowedTypes = Json->HasField(TEXT("AllowedTypes"));
	if (bApplyAllowedTypes)
	{
		AllowedTypesProperty = FindFProperty<FArrayProperty>(FBlackboardKeySelector::StaticStruct(), TEXT("AllowedTypes"));
		InnerObjectProperty = AllowedTypesProperty ? CastField<FObjectPropertyBase>(AllowedTypesProperty->Inner) : nullptr;
		if (!AllowedTypesProperty)
		{
			return Failure(JoinPath(Path, TEXT("AllowedTypes")), TEXT("InvalidProperty"), TEXT("AllowedTypes property is unavailable"));
		}
		if (!InnerObjectProperty)
		{
			return Failure(JoinPath(Path, TEXT("AllowedTypes")), TEXT("InvalidProperty"), TEXT("AllowedTypes inner property is unavailable"));
		}

		const TArray<TSharedPtr<FJsonValue>>* AllowedTypeRefs = nullptr;
		Json->TryGetArrayField(TEXT("AllowedTypes"), AllowedTypeRefs);
		PreparedAllowedTypes.Reserve(AllowedTypeRefs ? AllowedTypeRefs->Num() : 0);
		for (int32 Index = 0; AllowedTypeRefs && Index < AllowedTypeRefs->Num(); ++Index)
		{
			UClass* KeyTypeClass = nullptr;
			TSharedPtr<FJsonObject> KeyTypeProperties;
			const FString EntryPath = JoinPath(JoinPath(Path, TEXT("AllowedTypes")), FString::FromInt(Index));
			const FAssetDocumentCapabilityResult EntryResult =
				ValidateBlackboardAllowedTypeEntry((*AllowedTypeRefs)[Index], EntryPath, KeyTypeClass, KeyTypeProperties);
			if (!EntryResult.bSuccess)
			{
				return EntryResult;
			}

			UBlackboardKeyType* KeyType = NewObject<UBlackboardKeyType>(GetTransientPackage(), KeyTypeClass);
			if (!KeyType)
			{
				return Failure(EntryPath, TEXT("InvalidClassRef"), FString::Printf(TEXT("Failed to instantiate AllowedTypes class '%s'"), *KeyTypeClass->GetPathName()));
			}

			if (KeyTypeProperties.IsValid())
			{
				const FAssetDocumentCapabilityResult PropertiesResult =
					FAssetDocumentReflectedPropertyUtils::ApplyProperties(KeyType, KeyTypeProperties.ToSharedRef(), JoinPath(EntryPath, TEXT("Properties")));
				if (!PropertiesResult.bSuccess)
				{
					return PropertiesResult;
				}
			}

			PreparedAllowedTypes.Add(KeyType);
		}
	}

	FBlackboardKeySelector* Selector = static_cast<FBlackboardKeySelector*>(ValuePtr);
	if (Json->HasField(TEXT("SelectedKeyName")))
	{
		FString SelectedKeyName;
		Json->TryGetStringField(TEXT("SelectedKeyName"), SelectedKeyName);
		Selector->SelectedKeyName = FName(*SelectedKeyName);
		Selector->InvalidateResolvedKey();
	}

	if (Json->HasField(TEXT("bNoneIsAllowedValue")))
	{
		bool bNoneIsAllowed = false;
		Json->TryGetBoolField(TEXT("bNoneIsAllowedValue"), bNoneIsAllowed);
		Selector->AllowNoneAsValue(bNoneIsAllowed);
	}

	if (bApplyAllowedTypes)
	{
		void* AllowedTypesPtr = AllowedTypesProperty->ContainerPtrToValuePtr<void>(ValuePtr);
		FScriptArrayHelper ArrayHelper(AllowedTypesProperty, AllowedTypesPtr);
		ArrayHelper.EmptyValues();
		for (UBlackboardKeyType* PreparedAllowedType : PreparedAllowedTypes)
		{
			const int32 Index = ArrayHelper.AddValue();
			void* ElementPtr = ArrayHelper.GetRawPtr(Index);
			InnerObjectProperty->SetObjectPropertyValue(ElementPtr, PreparedAllowedType);
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FAssetDocumentReflectedPropertyUtils::ExtractBlackboardKeySelector(
	FStructProperty* Property,
	const void* ValuePtr,
	TSharedPtr<FJsonValue>& OutValue,
	const FString& Path)
{
	if (!IsBlackboardKeySelectorProperty(Property) || !ValuePtr)
	{
		return Failure(Path, TEXT("InvalidProperty"), TEXT("FBlackboardKeySelector property and value are required"));
	}

	const FBlackboardKeySelector* Selector = static_cast<const FBlackboardKeySelector*>(ValuePtr);
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("SelectedKeyName"), Selector->SelectedKeyName.ToString());

	if (FBoolProperty* NoneAllowedProperty = FindFProperty<FBoolProperty>(FBlackboardKeySelector::StaticStruct(), TEXT("bNoneIsAllowedValue")))
	{
		const void* BoolPtr = NoneAllowedProperty->ContainerPtrToValuePtr<void>(ValuePtr);
		Json->SetBoolField(TEXT("bNoneIsAllowedValue"), NoneAllowedProperty->GetPropertyValue(BoolPtr));
	}

	if (FArrayProperty* AllowedTypesProperty = FindFProperty<FArrayProperty>(FBlackboardKeySelector::StaticStruct(), TEXT("AllowedTypes")))
	{
		const void* AllowedTypesPtr = AllowedTypesProperty->ContainerPtrToValuePtr<void>(ValuePtr);
		FScriptArrayHelper ArrayHelper(AllowedTypesProperty, AllowedTypesPtr);
		if (ArrayHelper.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> AllowedTypeRefs;
			FObjectPropertyBase* InnerObjectProperty = CastField<FObjectPropertyBase>(AllowedTypesProperty->Inner);
			for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
			{
				const void* ElementPtr = ArrayHelper.GetRawPtr(Index);
				UObject* AllowedType = InnerObjectProperty ? InnerObjectProperty->GetObjectPropertyValue(ElementPtr) : nullptr;
				if (AllowedType)
				{
					TSharedPtr<FJsonObject> AllowedTypeJson = MakeReferenceObject(TEXT("ClassRef"), AllowedType->GetClass()->GetPathName());
					TSharedRef<FJsonObject> AllowedTypeProperties = MakeShared<FJsonObject>();
					const FAssetDocumentCapabilityResult PropertiesResult =
						ExtractAuthoredProperties(AllowedType, AllowedTypeProperties, JoinPath(JoinPath(JoinPath(Path, TEXT("AllowedTypes")), FString::FromInt(Index)), TEXT("Properties")));
					if (!PropertiesResult.bSuccess)
					{
						return PropertiesResult;
					}
					if (AllowedTypeProperties->Values.Num() > 0)
					{
						AllowedTypeJson->SetObjectField(TEXT("Properties"), AllowedTypeProperties);
					}
					AllowedTypeRefs.Add(MakeShared<FJsonValueObject>(AllowedTypeJson));
				}
			}
			if (!AllowedTypeRefs.IsEmpty())
			{
				Json->SetArrayField(TEXT("AllowedTypes"), AllowedTypeRefs);
			}
		}
	}

	OutValue = MakeShared<FJsonValueObject>(Json);
	return FAssetDocumentCapabilityResult::Success();
}
