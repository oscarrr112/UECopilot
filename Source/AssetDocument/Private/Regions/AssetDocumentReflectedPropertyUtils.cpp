// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentReflectedPropertyUtils.h"

#include "AssetDocumentCanonicalJson.h"
#include "AssetDocumentJsonRegionUtils.h"
#include "Utils/PropertySetterUtils.h"

#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Dom/JsonObject.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/StructOnScope.h"
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

FString ReflectedPropertyJoinPath(const FString& Path, const FString& Token)
{
	return FString::Printf(TEXT("%s/%s"), *Path, *FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Token));
}

FAssetDocumentCapabilityResult ReflectedPropertyFailure(const FString& Path, const FString& Code, const FString& Message)
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

bool TryGetJsonObject(const TSharedPtr<FJsonValue>& Value, TSharedPtr<FJsonObject>& OutObject);
bool IsBlackboardKeySelectorProperty(FStructProperty* Property);
bool IsInstancedStructProperty(FStructProperty* Property);
bool IsSupportedMapKeyProperty(FProperty* KeyProperty);

FAssetDocumentCapabilityResult ValidateReferenceValue(const TSharedPtr<FJsonValue>& Value, const FString& ExpectedKind, const FString& Path)
{
	if (!Value.IsValid())
	{
		return ReflectedPropertyFailure(Path, TEXT("InvalidPropertyValue"), TEXT("Reference value is required"));
	}
	if (Value->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FString UnusedString;
	if (Value->TryGetString(UnusedString))
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TSharedPtr<FJsonObject> Object;
	if (!TryGetJsonObject(Value, Object))
	{
		return ReflectedPropertyFailure(Path, TEXT("InvalidPropertyValue"), TEXT("Reference value must be a string, null, or reference object"));
	}

	static const TSet<FString> SupportedFields =
	{
		TEXT("Kind"),
		TEXT("Path")
	};

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		if (!SupportedFields.Contains(Pair.Key))
		{
			return ReflectedPropertyFailure(ReflectedPropertyJoinPath(Path, Pair.Key), TEXT("UnknownField"), FString::Printf(TEXT("Unknown reference field '%s'"), *Pair.Key));
		}
	}

	FString Kind;
	if (!Object->TryGetStringField(TEXT("Kind"), Kind) || Kind != ExpectedKind)
	{
		return ReflectedPropertyFailure(ReflectedPropertyJoinPath(Path, TEXT("Kind")), TEXT("InvalidPropertyValue"), FString::Printf(TEXT("Reference Kind must be '%s'"), *ExpectedKind));
	}

	FString ReferencePath;
	if (!Object->TryGetStringField(TEXT("Path"), ReferencePath))
	{
		return ReflectedPropertyFailure(ReflectedPropertyJoinPath(Path, TEXT("Path")), TEXT("InvalidPropertyValue"), TEXT("Reference Path must be a string"));
	}

	return FAssetDocumentCapabilityResult::Success();
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

TSharedPtr<FJsonValue> NormalizeAuthoredValueForSetter(FProperty* Property, const TSharedPtr<FJsonValue>& Value)
{
	if (!Property || !Value.IsValid())
	{
		return Value;
	}

	if (IsClassReferenceProperty(Property) || IsObjectReferenceProperty(Property))
	{
		return NormalizeInputValueForSetter(Property, Value);
	}

	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (IsBlackboardKeySelectorProperty(StructProperty))
		{
			return Value;
		}

		const TSharedPtr<FJsonObject>* StructObject = nullptr;
		if (!Value->TryGetObject(StructObject) || !StructObject || !StructObject->IsValid())
		{
			return Value;
		}

		TSharedPtr<FJsonObject> NormalizedStruct = MakeShared<FJsonObject>();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*StructObject)->Values)
		{
			FProperty* FieldProperty = FindFProperty<FProperty>(StructProperty->Struct, *Pair.Key);
			NormalizedStruct->SetField(Pair.Key, FieldProperty ? NormalizeAuthoredValueForSetter(FieldProperty, Pair.Value) : Pair.Value);
		}
		return MakeShared<FJsonValueObject>(NormalizedStruct);
	}

	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		const TArray<TSharedPtr<FJsonValue>>* ArrayValues = nullptr;
		if (!Value->TryGetArray(ArrayValues))
		{
			return Value;
		}

		TArray<TSharedPtr<FJsonValue>> NormalizedArray;
		NormalizedArray.Reserve(ArrayValues->Num());
		for (const TSharedPtr<FJsonValue>& ArrayValue : *ArrayValues)
		{
			NormalizedArray.Add(NormalizeAuthoredValueForSetter(ArrayProperty->Inner, ArrayValue));
		}
		return MakeShared<FJsonValueArray>(NormalizedArray);
	}

	if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		const TSharedPtr<FJsonObject>* MapObject = nullptr;
		if (!Value->TryGetObject(MapObject) || !MapObject || !MapObject->IsValid())
		{
			return Value;
		}

		TSharedPtr<FJsonObject> NormalizedMap = MakeShared<FJsonObject>();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*MapObject)->Values)
		{
			NormalizedMap->SetField(Pair.Key, NormalizeAuthoredValueForSetter(MapProperty->ValueProp, Pair.Value));
		}
		return MakeShared<FJsonValueObject>(NormalizedMap);
	}

	if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		const TArray<TSharedPtr<FJsonValue>>* SetValues = nullptr;
		if (!Value->TryGetArray(SetValues))
		{
			return Value;
		}

		TArray<TSharedPtr<FJsonValue>> NormalizedSet;
		NormalizedSet.Reserve(SetValues->Num());
		for (const TSharedPtr<FJsonValue>& SetValue : *SetValues)
		{
			NormalizedSet.Add(NormalizeAuthoredValueForSetter(SetProperty->ElementProp, SetValue));
		}
		return MakeShared<FJsonValueArray>(NormalizedSet);
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

bool IsInstancedStructProperty(FStructProperty* Property)
{
	return Property
		&& Property->Struct
		&& Property->Struct == FInstancedStruct::StaticStruct();
}

bool IsSupportedMapKeyProperty(FProperty* KeyProperty)
{
	return CastField<FEnumProperty>(KeyProperty)
		|| CastField<FByteProperty>(KeyProperty)
		|| CastField<FStrProperty>(KeyProperty)
		|| CastField<FNameProperty>(KeyProperty);
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

FAssetDocumentCapabilityResult ValidateBlackboardKeySelectorJson(const TSharedRef<FJsonObject>& Json, const FString& Path)
{
	static const TSet<FString> DerivedFields =
	{
		TEXT("SelectedKeyName"),
		TEXT("AllowedTypes"),
		TEXT("SelectedKeyType"),
		TEXT("SelectedKeyID"),
		TEXT("bNoneIsAllowedValue")
	};

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Json->Values)
	{
		const FString FieldPath = ReflectedPropertyJoinPath(Path, Pair.Key);
		if (DerivedFields.Contains(Pair.Key))
		{
			return ReflectedPropertyFailure(
				FieldPath,
				TEXT("NonAuthoredProperty"),
				FString::Printf(TEXT("BlackboardKeySelector field '%s' is derived class policy or runtime cache and is read-only"), *Pair.Key));
		}
		if (Pair.Key != TEXT("Key"))
		{
			return ReflectedPropertyFailure(FieldPath, TEXT("UnknownProperty"), FString::Printf(TEXT("Unknown BlackboardKeySelector field '%s'"), *Pair.Key));
		}

		FString Unused;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetString(Unused))
		{
			return ReflectedPropertyFailure(FieldPath, TEXT("InvalidPropertyValue"), TEXT("Key must be a string"));
		}
	}

	if (!Json->HasField(TEXT("Key")))
	{
		return ReflectedPropertyFailure(ReflectedPropertyJoinPath(Path, TEXT("Key")), TEXT("MissingProperty"), TEXT("BlackboardKeySelector requires authored field 'Key'"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractAuthoredPropertyValue(FProperty* Property, const void* ValuePtr, TSharedPtr<FJsonValue>& OutValue, const FString& Path);
FAssetDocumentCapabilityResult ValidateAuthoredPropertyValue(FProperty* Property, const TSharedPtr<FJsonValue>& Value, const FString& Path);
bool RequiresCustomStructApply(FProperty* Property, const TSharedPtr<FJsonValue>& Value);
FAssetDocumentCapabilityResult ApplyPropertyValueWithCustomFields(
	FProperty* Property,
	void* ValuePtr,
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path);
FAssetDocumentCapabilityResult ApplyScriptStructPropertiesWithCustomFields(
	UScriptStruct* ScriptStruct,
	void* ValuePtr,
	const TSharedRef<FJsonObject>& Properties,
	const FString& Path);

UScriptStruct* ResolveInstancedScriptStruct(const FString& StructPath)
{
	FString NormalizedPath = StructPath;
	NormalizedPath.TrimStartAndEndInline();
	if (NormalizedPath.IsEmpty())
	{
		return nullptr;
	}

	if (UScriptStruct* LoadedStruct = LoadObject<UScriptStruct>(nullptr, *NormalizedPath))
	{
		return LoadedStruct;
	}
	return FindObject<UScriptStruct>(nullptr, *NormalizedPath);
}

FAssetDocumentCapabilityResult ParseInstancedStructValue(
	FStructProperty* Property,
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	UScriptStruct*& OutScriptStruct,
	TSharedPtr<FJsonObject>& OutProperties)
{
	OutScriptStruct = nullptr;
	OutProperties.Reset();
	if (!Property || !IsInstancedStructProperty(Property) || !Value.IsValid())
	{
		return ReflectedPropertyFailure(Path, TEXT("InvalidPropertyValue"), TEXT("FInstancedStruct property and value are required"));
	}
	if (Value->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TSharedPtr<FJsonObject> ValueObject;
	if (!TryGetJsonObject(Value, ValueObject))
	{
		return ReflectedPropertyFailure(Path, TEXT("InvalidPropertyValue"), TEXT("FInstancedStruct value must be null or an object with Struct and Properties"));
	}

	static const TSet<FString> SupportedFields =
	{
		TEXT("Struct"),
		TEXT("Properties")
	};
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ValueObject->Values)
	{
		if (!SupportedFields.Contains(Pair.Key))
		{
			return ReflectedPropertyFailure(
				ReflectedPropertyJoinPath(Path, Pair.Key),
				TEXT("UnknownProperty"),
				FString::Printf(TEXT("Unknown FInstancedStruct field '%s'"), *Pair.Key));
		}
	}

	FString StructPath;
	if (!ValueObject->TryGetStringField(TEXT("Struct"), StructPath) || StructPath.TrimStartAndEnd().IsEmpty())
	{
		return ReflectedPropertyFailure(ReflectedPropertyJoinPath(Path, TEXT("Struct")), TEXT("InvalidStructRef"), TEXT("FInstancedStruct Struct must be a non-empty script struct path"));
	}
	OutScriptStruct = ResolveInstancedScriptStruct(StructPath);
	if (!OutScriptStruct)
	{
		return ReflectedPropertyFailure(
			ReflectedPropertyJoinPath(Path, TEXT("Struct")),
			TEXT("InvalidStructRef"),
			FString::Printf(TEXT("Could not resolve script struct '%s'"), *StructPath));
	}

	const FString BaseStructPath = Property->GetMetaData(TEXT("BaseStruct"));
	if (!BaseStructPath.IsEmpty())
	{
		UScriptStruct* BaseStruct = ResolveInstancedScriptStruct(BaseStructPath);
		if (!BaseStruct)
		{
			return ReflectedPropertyFailure(
				ReflectedPropertyJoinPath(Path, TEXT("Struct")),
				TEXT("InvalidStructConstraint"),
				FString::Printf(TEXT("FInstancedStruct BaseStruct metadata '%s' could not be resolved"), *BaseStructPath));
		}
		if (!OutScriptStruct->IsChildOf(BaseStruct))
		{
			return ReflectedPropertyFailure(
				ReflectedPropertyJoinPath(Path, TEXT("Struct")),
				TEXT("IncompatibleStructType"),
				FString::Printf(TEXT("Script struct '%s' is not derived from required base '%s'"), *OutScriptStruct->GetPathName(), *BaseStruct->GetPathName()));
		}
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	if (!ValueObject->TryGetObjectField(TEXT("Properties"), PropertiesObject) || !PropertiesObject || !PropertiesObject->IsValid())
	{
		return ReflectedPropertyFailure(ReflectedPropertyJoinPath(Path, TEXT("Properties")), TEXT("InvalidPropertyValue"), TEXT("FInstancedStruct Properties must be an object"));
	}
	OutProperties = *PropertiesObject;

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : OutProperties->Values)
	{
		const FString FieldPath = ReflectedPropertyJoinPath(ReflectedPropertyJoinPath(Path, TEXT("Properties")), Pair.Key);
		FProperty* FieldProperty = FindFProperty<FProperty>(OutScriptStruct, *Pair.Key);
		if (!FieldProperty)
		{
			return ReflectedPropertyFailure(FieldPath, TEXT("UnknownProperty"), FString::Printf(TEXT("Struct field '%s' does not exist"), *Pair.Key));
		}
		if (!IsAuthoredEditableProperty(FieldProperty))
		{
			return ReflectedPropertyFailure(FieldPath, TEXT("NonAuthoredProperty"), FString::Printf(TEXT("Struct field '%s' is not authored: %s"), *Pair.Key, *GetNonAuthoredReason(FieldProperty)));
		}

		const FAssetDocumentCapabilityResult FieldResult = ValidateAuthoredPropertyValue(FieldProperty, Pair.Value, FieldPath);
		if (!FieldResult.bSuccess)
		{
			return FieldResult;
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
		return ReflectedPropertyFailure(Path, TEXT("InvalidProperty"), TEXT("Property is required"));
	}

	if (!IsPropertyRuntimeSupported(Property))
	{
		return ReflectedPropertyFailure(Path, TEXT("UnsupportedProperty"), FString::Printf(TEXT("Property '%s' is not supported"), *Property->GetName()));
	}

	if (IsClassReferenceProperty(Property) || IsObjectReferenceProperty(Property))
	{
		return ValidateReferenceValue(Value, IsClassReferenceProperty(Property) ? TEXT("ClassRef") : TEXT("AssetRef"), Path);
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

		if (IsInstancedStructProperty(StructProperty))
		{
			UScriptStruct* ScriptStruct = nullptr;
			TSharedPtr<FJsonObject> StructProperties;
			return ParseInstancedStructValue(StructProperty, Value, Path, ScriptStruct, StructProperties);
		}

		const TSharedPtr<FJsonObject>* StructObject = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(StructObject) || !StructObject || !StructObject->IsValid())
		{
			return FAssetDocumentCapabilityResult::Success();
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*StructObject)->Values)
		{
			const FString FieldPath = ReflectedPropertyJoinPath(Path, Pair.Key);
			FProperty* FieldProperty = FindFProperty<FProperty>(StructProperty->Struct, *Pair.Key);
			if (!FieldProperty)
			{
				return ReflectedPropertyFailure(FieldPath, TEXT("UnknownProperty"), FString::Printf(TEXT("Struct field '%s' does not exist"), *Pair.Key));
			}
			if (!IsAuthoredEditableProperty(FieldProperty))
			{
				return ReflectedPropertyFailure(FieldPath, TEXT("NonAuthoredProperty"), FString::Printf(TEXT("Struct field '%s' is not authored: %s"), *Pair.Key, *GetNonAuthoredReason(FieldProperty)));
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
				ValidateAuthoredPropertyValue(ArrayProperty->Inner, (*ArrayValues)[Index], ReflectedPropertyJoinPath(Path, FString::FromInt(Index)));
			if (!ElementResult.bSuccess)
			{
				return ElementResult;
			}
		}
		return FAssetDocumentCapabilityResult::Success();
	}

	if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		if (!IsSupportedMapKeyProperty(MapProperty->KeyProp))
		{
			const FString KeyTypeName = MapProperty->KeyProp ? MapProperty->KeyProp->GetClass()->GetName() : TEXT("None");
			return ReflectedPropertyFailure(Path, TEXT("UnsupportedProperty"), FString::Printf(TEXT("Map property '%s' has unsupported key type '%s'"), *Property->GetName(), *KeyTypeName));
		}

		const TSharedPtr<FJsonObject>* MapObject = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(MapObject) || !MapObject || !MapObject->IsValid())
		{
			return FAssetDocumentCapabilityResult::Success();
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*MapObject)->Values)
		{
			const FAssetDocumentCapabilityResult ValueResult =
				ValidateAuthoredPropertyValue(MapProperty->ValueProp, Pair.Value, ReflectedPropertyJoinPath(Path, Pair.Key));
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
				ValidateAuthoredPropertyValue(SetProperty->ElementProp, (*SetValues)[Index], ReflectedPropertyJoinPath(Path, FString::FromInt(Index)));
			if (!ElementResult.bSuccess)
			{
				return ElementResult;
			}
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyInstancedStructValue(
	FStructProperty* Property,
	void* ValuePtr,
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path)
{
	if (!Property || !IsInstancedStructProperty(Property) || !ValuePtr)
	{
		return ReflectedPropertyFailure(Path, TEXT("InvalidProperty"), TEXT("FInstancedStruct property and destination are required"));
	}

	UScriptStruct* ScriptStruct = nullptr;
	TSharedPtr<FJsonObject> StructProperties;
	const FAssetDocumentCapabilityResult ParseResult = ParseInstancedStructValue(Property, Value, Path, ScriptStruct, StructProperties);
	if (!ParseResult.bSuccess)
	{
		return ParseResult;
	}

	FInstancedStruct* Destination = static_cast<FInstancedStruct*>(ValuePtr);
	if (!ScriptStruct)
	{
		Destination->Reset();
		return FAssetDocumentCapabilityResult::Success();
	}

	FInstancedStruct Prepared;
	if (Destination->GetScriptStruct() == ScriptStruct)
	{
		Prepared = *Destination;
	}
	else
	{
		Prepared.InitializeAs(ScriptStruct);
	}

	const FString PropertiesPath = ReflectedPropertyJoinPath(Path, TEXT("Properties"));
	const FAssetDocumentCapabilityResult ApplyResult = ApplyScriptStructPropertiesWithCustomFields(
		ScriptStruct,
		Prepared.GetMutableMemory(),
		StructProperties.ToSharedRef(),
		PropertiesPath);
	if (!ApplyResult.bSuccess)
	{
		return ApplyResult;
	}

	*Destination = MoveTemp(Prepared);
	return FAssetDocumentCapabilityResult::Success();
}

bool RequiresCustomStructApply(FProperty* Property, const TSharedPtr<FJsonValue>& Value)
{
	if (!Property || !Value.IsValid())
	{
		return false;
	}

	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (IsBlackboardKeySelectorProperty(StructProperty) || IsInstancedStructProperty(StructProperty))
		{
			return true;
		}

		const TSharedPtr<FJsonObject>* StructObject = nullptr;
		if (!Value->TryGetObject(StructObject) || !StructObject || !StructObject->IsValid())
		{
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*StructObject)->Values)
		{
			if (FProperty* FieldProperty = FindFProperty<FProperty>(StructProperty->Struct, *Pair.Key);
				RequiresCustomStructApply(FieldProperty, Pair.Value))
			{
				return true;
			}
		}
		return false;
	}

	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		const TArray<TSharedPtr<FJsonValue>>* ArrayValues = nullptr;
		if (!Value->TryGetArray(ArrayValues))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& ElementValue : *ArrayValues)
		{
			if (RequiresCustomStructApply(ArrayProperty->Inner, ElementValue))
			{
				return true;
			}
		}
		return false;
	}

	if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		const TSharedPtr<FJsonObject>* MapObject = nullptr;
		if (!Value->TryGetObject(MapObject) || !MapObject || !MapObject->IsValid())
		{
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*MapObject)->Values)
		{
			if (RequiresCustomStructApply(MapProperty->ValueProp, Pair.Value))
			{
				return true;
			}
		}
		return false;
	}

	if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		const TArray<TSharedPtr<FJsonValue>>* SetValues = nullptr;
		if (!Value->TryGetArray(SetValues))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& ElementValue : *SetValues)
		{
			if (RequiresCustomStructApply(SetProperty->ElementProp, ElementValue))
			{
				return true;
			}
		}
	}

	return false;
}

class FScopedPropertyValue
{
public:
	explicit FScopedPropertyValue(FProperty* InProperty)
		: Property(InProperty)
	{
		if (Property)
		{
			Data = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
			Property->InitializeValue(Data);
		}
	}

	~FScopedPropertyValue()
	{
		if (Property && Data)
		{
			Property->DestroyValue(Data);
			FMemory::Free(Data);
		}
	}

	void* Get() const { return Data; }

private:
	FProperty* Property = nullptr;
	void* Data = nullptr;
};

bool TrySetMapKeyFromString(FProperty* KeyProperty, void* KeyPtr, const FString& KeyString)
{
	if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(KeyProperty))
	{
		UEnum* Enum = EnumProperty->GetEnum();
		int64 EnumValue = Enum ? Enum->GetValueByNameString(KeyString) : INDEX_NONE;
		if (Enum && EnumValue == INDEX_NONE)
		{
			EnumValue = Enum->GetValueByNameString(Enum->GetName() + TEXT("::") + KeyString);
		}
		if (EnumValue != INDEX_NONE)
		{
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(KeyPtr, EnumValue);
			return true;
		}
		return false;
	}
	if (FByteProperty* ByteProperty = CastField<FByteProperty>(KeyProperty))
	{
		if (UEnum* Enum = ByteProperty->Enum)
		{
			int64 EnumValue = Enum->GetValueByNameString(KeyString);
			if (EnumValue == INDEX_NONE)
			{
				EnumValue = Enum->GetValueByNameString(Enum->GetName() + TEXT("::") + KeyString);
			}
			if (EnumValue == INDEX_NONE)
			{
				return false;
			}
			ByteProperty->SetIntPropertyValue(KeyPtr, EnumValue);
			return true;
		}

		if (KeyString.IsEmpty() || KeyString.TrimStartAndEnd() != KeyString)
		{
			return false;
		}
		TCHAR* End = nullptr;
		const TCHAR* Start = *KeyString;
		const int64 ParsedValue = FCString::Strtoi64(Start, &End, 10);
		if (End == Start || !End || *End != TEXT('\0') || ParsedValue < 0 || ParsedValue > MAX_uint8)
		{
			return false;
		}
		ByteProperty->SetIntPropertyValue(KeyPtr, ParsedValue);
		return true;
	}
	if (FStrProperty* StringProperty = CastField<FStrProperty>(KeyProperty))
	{
		StringProperty->SetPropertyValue(KeyPtr, KeyString);
		return true;
	}
	if (FNameProperty* NameProperty = CastField<FNameProperty>(KeyProperty))
	{
		NameProperty->SetPropertyValue(KeyPtr, FName(*KeyString));
		return true;
	}
	return false;
}

FAssetDocumentCapabilityResult ApplyArrayValueWithCustomFields(
	FArrayProperty* ArrayProperty,
	void* ValuePtr,
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path)
{
	const TArray<TSharedPtr<FJsonValue>>* ArrayValues = nullptr;
	if (!ArrayProperty || !ValuePtr || !Value.IsValid() || !Value->TryGetArray(ArrayValues))
	{
		return ReflectedPropertyFailure(Path, TEXT("InvalidPropertyValue"), TEXT("Array value must be an array"));
	}

	FScopedPropertyValue Prepared(ArrayProperty);
	FScriptArrayHelper ArrayHelper(ArrayProperty, Prepared.Get());
	ArrayHelper.AddValues(ArrayValues->Num());
	for (int32 Index = 0; Index < ArrayValues->Num(); ++Index)
	{
		const FAssetDocumentCapabilityResult ElementResult = ApplyPropertyValueWithCustomFields(
			ArrayProperty->Inner,
			ArrayHelper.GetRawPtr(Index),
			(*ArrayValues)[Index],
			ReflectedPropertyJoinPath(Path, FString::FromInt(Index)));
		if (!ElementResult.bSuccess)
		{
			return ElementResult;
		}
	}

	ArrayProperty->CopyCompleteValue(ValuePtr, Prepared.Get());
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyMapValueWithCustomFields(
	FMapProperty* MapProperty,
	void* ValuePtr,
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path)
{
	const TSharedPtr<FJsonObject>* MapObject = nullptr;
	if (!MapProperty || !ValuePtr || !Value.IsValid()
		|| !Value->TryGetObject(MapObject) || !MapObject || !MapObject->IsValid())
	{
		return ReflectedPropertyFailure(Path, TEXT("InvalidPropertyValue"), TEXT("Map value must be an object"));
	}

	FScopedPropertyValue Prepared(MapProperty);
	FScriptMapHelper MapHelper(MapProperty, Prepared.Get());
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*MapObject)->Values)
	{
		const int32 Index = MapHelper.AddDefaultValue_Invalid_NeedsRehash();
		if (!TrySetMapKeyFromString(MapProperty->KeyProp, MapHelper.GetKeyPtr(Index), Pair.Key))
		{
			return ReflectedPropertyFailure(
				ReflectedPropertyJoinPath(Path, Pair.Key),
				TEXT("InvalidMapKey"),
				FString::Printf(TEXT("Map key '%s' is invalid for property '%s'"), *Pair.Key, *MapProperty->GetName()));
		}
		// This staged map starts empty and only appends, so all earlier indices are valid even
		// before Rehash() makes the hash table queryable.
		for (int32 ExistingIndex = 0; ExistingIndex < Index; ++ExistingIndex)
		{
			if (MapProperty->KeyProp->Identical(
					MapHelper.GetKeyPtr(ExistingIndex),
					MapHelper.GetKeyPtr(Index),
					PPF_None))
			{
				return ReflectedPropertyFailure(
					ReflectedPropertyJoinPath(Path, Pair.Key),
					TEXT("DuplicateMapKey"),
					FString::Printf(TEXT("Map key '%s' collides with another authored key"), *Pair.Key));
			}
		}

		const FAssetDocumentCapabilityResult ValueResult = ApplyPropertyValueWithCustomFields(
			MapProperty->ValueProp,
			MapHelper.GetValuePtr(Index),
			Pair.Value,
			ReflectedPropertyJoinPath(Path, Pair.Key));
		if (!ValueResult.bSuccess)
		{
			return ValueResult;
		}
	}
	MapHelper.Rehash();

	MapProperty->CopyCompleteValue(ValuePtr, Prepared.Get());
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplySetValueWithCustomFields(
	FSetProperty* SetProperty,
	void* ValuePtr,
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path)
{
	const TArray<TSharedPtr<FJsonValue>>* SetValues = nullptr;
	if (!SetProperty || !ValuePtr || !Value.IsValid() || !Value->TryGetArray(SetValues))
	{
		return ReflectedPropertyFailure(Path, TEXT("InvalidPropertyValue"), TEXT("Set value must be an array"));
	}

	FScopedPropertyValue Prepared(SetProperty);
	FScriptSetHelper SetHelper(SetProperty, Prepared.Get());
	for (int32 Index = 0; Index < SetValues->Num(); ++Index)
	{
		const int32 SetIndex = SetHelper.AddDefaultValue_Invalid_NeedsRehash();
		const FAssetDocumentCapabilityResult ElementResult = ApplyPropertyValueWithCustomFields(
			SetProperty->ElementProp,
			SetHelper.GetElementPtr(SetIndex),
			(*SetValues)[Index],
			ReflectedPropertyJoinPath(Path, FString::FromInt(Index)));
		if (!ElementResult.bSuccess)
		{
			return ElementResult;
		}
		// This staged set starts empty and only appends, so all earlier indices are valid even
		// before Rehash() makes the hash table queryable.
		for (int32 ExistingIndex = 0; ExistingIndex < SetIndex; ++ExistingIndex)
		{
			if (SetProperty->ElementProp->Identical(
					SetHelper.GetElementPtr(ExistingIndex),
					SetHelper.GetElementPtr(SetIndex),
					PPF_None))
			{
				return ReflectedPropertyFailure(
					ReflectedPropertyJoinPath(Path, FString::FromInt(Index)),
					TEXT("DuplicateSetElement"),
					TEXT("Set element duplicates another authored element"));
			}
		}
	}
	SetHelper.Rehash();

	SetProperty->CopyCompleteValue(ValuePtr, Prepared.Get());
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyScriptStructPropertiesWithCustomFields(
	UScriptStruct* ScriptStruct,
	void* ValuePtr,
	const TSharedRef<FJsonObject>& Properties,
	const FString& Path)
{
	if (!ScriptStruct || !ValuePtr)
	{
		return ReflectedPropertyFailure(Path, TEXT("InvalidPropertyValue"), TEXT("Script struct and destination are required"));
	}

	FStructOnScope Prepared(ScriptStruct);
	ScriptStruct->CopyScriptStruct(Prepared.GetStructMemory(), ValuePtr);
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
	{
		FProperty* FieldProperty = FindFProperty<FProperty>(ScriptStruct, *Pair.Key);
		if (!FieldProperty)
		{
			return ReflectedPropertyFailure(
				ReflectedPropertyJoinPath(Path, Pair.Key),
				TEXT("UnknownProperty"),
				FString::Printf(TEXT("Struct field '%s' does not exist"), *Pair.Key));
		}

		const FString FieldPath = ReflectedPropertyJoinPath(Path, Pair.Key);
		if (RequiresCustomStructApply(FieldProperty, Pair.Value))
		{
			const FAssetDocumentCapabilityResult FieldResult = ApplyPropertyValueWithCustomFields(
				FieldProperty,
				FieldProperty->ContainerPtrToValuePtr<void>(Prepared.GetStructMemory()),
				Pair.Value,
				FieldPath);
			if (!FieldResult.bSuccess)
			{
				return FieldResult;
			}
			continue;
		}

		TSharedPtr<FJsonObject> SingleField = MakeShared<FJsonObject>();
		SingleField->SetField(Pair.Key, NormalizeAuthoredValueForSetter(FieldProperty, Pair.Value));
		if (!FPropertySetterUtils::SetStructFromJson(
			ScriptStruct,
			Prepared.GetStructMemory(),
			MakeShared<FJsonValueObject>(SingleField)))
		{
			return ReflectedPropertyFailure(
				FieldPath,
				TEXT("InvalidPropertyValue"),
				FString::Printf(TEXT("Failed to set struct field '%s'"), *Pair.Key));
		}
	}

	ScriptStruct->CopyScriptStruct(ValuePtr, Prepared.GetStructMemory());
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ApplyStructValueWithCustomFields(
	FStructProperty* StructProperty,
	void* ValuePtr,
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path)
{
	if (!StructProperty || !ValuePtr || !Value.IsValid())
	{
		return ReflectedPropertyFailure(Path, TEXT("InvalidPropertyValue"), TEXT("Struct property, destination, and value are required"));
	}
	if (IsInstancedStructProperty(StructProperty))
	{
		return ApplyInstancedStructValue(StructProperty, ValuePtr, Value, Path);
	}

	const TSharedPtr<FJsonObject>* StructObject = nullptr;
	if (!Value->TryGetObject(StructObject) || !StructObject || !StructObject->IsValid())
	{
		return ReflectedPropertyFailure(Path, TEXT("InvalidPropertyValue"), TEXT("Struct value must be an object"));
	}
	if (IsBlackboardKeySelectorProperty(StructProperty))
	{
		return FAssetDocumentReflectedPropertyUtils::ApplyBlackboardKeySelector(
			StructProperty,
			ValuePtr,
			(*StructObject).ToSharedRef(),
			Path);
	}

	return ApplyScriptStructPropertiesWithCustomFields(
		StructProperty->Struct,
		ValuePtr,
		(*StructObject).ToSharedRef(),
		Path);
}

FAssetDocumentCapabilityResult ApplyPropertyValueWithCustomFields(
	FProperty* Property,
	void* ValuePtr,
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path)
{
	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		return ApplyStructValueWithCustomFields(StructProperty, ValuePtr, Value, Path);
	}
	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		return ApplyArrayValueWithCustomFields(ArrayProperty, ValuePtr, Value, Path);
	}
	if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		return ApplyMapValueWithCustomFields(MapProperty, ValuePtr, Value, Path);
	}
	if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		return ApplySetValueWithCustomFields(SetProperty, ValuePtr, Value, Path);
	}
	return ReflectedPropertyFailure(
		Path,
		TEXT("InvalidProperty"),
		TEXT("Custom reflected-property apply requires a struct or container property"));
}

FAssetDocumentCapabilityResult ExtractInstancedStructValue(
	FStructProperty* Property,
	const void* ValuePtr,
	TSharedPtr<FJsonValue>& OutValue,
	const FString& Path)
{
	if (!Property || !IsInstancedStructProperty(Property) || !ValuePtr)
	{
		return ReflectedPropertyFailure(Path, TEXT("InvalidProperty"), TEXT("FInstancedStruct property and value are required"));
	}

	const FInstancedStruct* InstancedStruct = static_cast<const FInstancedStruct*>(ValuePtr);
	if (!InstancedStruct->IsValid())
	{
		OutValue = MakeShared<FJsonValueNull>();
		return FAssetDocumentCapabilityResult::Success();
	}

	UScriptStruct* ScriptStruct = const_cast<UScriptStruct*>(InstancedStruct->GetScriptStruct());
	TSharedPtr<FJsonObject> StructProperties = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(ScriptStruct); It; ++It)
	{
		FProperty* FieldProperty = *It;
		if (!IsAuthoredEditableProperty(FieldProperty))
		{
			continue;
		}

		TSharedPtr<FJsonValue> FieldValue;
		const FString FieldPath = ReflectedPropertyJoinPath(ReflectedPropertyJoinPath(Path, TEXT("Properties")), FieldProperty->GetName());
		const FAssetDocumentCapabilityResult FieldResult = ExtractAuthoredPropertyValue(
			FieldProperty,
			FieldProperty->ContainerPtrToValuePtr<void>(InstancedStruct->GetMemory()),
			FieldValue,
			FieldPath);
		if (!FieldResult.bSuccess)
		{
			return FieldResult;
		}
		if (FieldValue.IsValid())
		{
			StructProperties->SetField(FieldProperty->GetName(), FieldValue);
		}
	}

	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("Struct"), ScriptStruct->GetPathName());
	Json->SetObjectField(TEXT("Properties"), StructProperties);
	OutValue = MakeShared<FJsonValueObject>(Json);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractStructAuthoredProperties(FStructProperty* StructProperty, const void* ValuePtr, TSharedPtr<FJsonValue>& OutValue, const FString& Path)
{
	if (!StructProperty || !ValuePtr)
	{
		return ReflectedPropertyFailure(Path, TEXT("InvalidProperty"), TEXT("Struct property and value are required"));
	}
	if (IsInstancedStructProperty(StructProperty))
	{
		return ExtractInstancedStructValue(StructProperty, ValuePtr, OutValue, Path);
	}

	TSharedPtr<FJsonObject> StructJson = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
	{
		FProperty* FieldProperty = *It;
		if (!IsAuthoredEditableProperty(FieldProperty))
		{
			continue;
		}

		TSharedPtr<FJsonValue> FieldValue;
		const FString FieldPath = ReflectedPropertyJoinPath(Path, FieldProperty->GetName());
		const FAssetDocumentCapabilityResult FieldResult =
			ExtractAuthoredPropertyValue(FieldProperty, FieldProperty->ContainerPtrToValuePtr<void>(ValuePtr), FieldValue, FieldPath);
		if (!FieldResult.bSuccess)
		{
			return FieldResult;
		}
		if (FieldValue.IsValid())
		{
			StructJson->SetField(FieldProperty->GetName(), FieldValue);
		}
	}

	OutValue = MakeShared<FJsonValueObject>(StructJson);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractArrayAuthoredValues(FArrayProperty* ArrayProperty, const void* ValuePtr, TSharedPtr<FJsonValue>& OutValue, const FString& Path)
{
	if (!ArrayProperty || !ValuePtr)
	{
		return ReflectedPropertyFailure(Path, TEXT("InvalidProperty"), TEXT("Array property and value are required"));
	}

	FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
	TArray<TSharedPtr<FJsonValue>> ArrayJson;
	ArrayJson.Reserve(ArrayHelper.Num());
	for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
	{
		TSharedPtr<FJsonValue> ElementValue;
		const FAssetDocumentCapabilityResult ElementResult =
			ExtractAuthoredPropertyValue(ArrayProperty->Inner, ArrayHelper.GetRawPtr(Index), ElementValue, ReflectedPropertyJoinPath(Path, FString::FromInt(Index)));
		if (!ElementResult.bSuccess)
		{
			return ElementResult;
		}
		if (ElementValue.IsValid())
		{
			ArrayJson.Add(ElementValue);
		}
	}

	OutValue = MakeShared<FJsonValueArray>(ArrayJson);
	return FAssetDocumentCapabilityResult::Success();
}

bool TryExtractMapKeyToString(FProperty* KeyProperty, const void* KeyPtr, FString& OutKey)
{
	if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(KeyProperty))
	{
		OutKey = EnumProperty->GetEnum()->GetNameStringByValue(EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(KeyPtr));
		return true;
	}
	if (FByteProperty* ByteProperty = CastField<FByteProperty>(KeyProperty))
	{
		OutKey = ByteProperty->Enum
			? ByteProperty->Enum->GetNameStringByValue(ByteProperty->GetPropertyValue(KeyPtr))
			: FString::FromInt(ByteProperty->GetPropertyValue(KeyPtr));
		return true;
	}
	if (FStrProperty* StringProperty = CastField<FStrProperty>(KeyProperty))
	{
		OutKey = StringProperty->GetPropertyValue(KeyPtr);
		return true;
	}
	if (FNameProperty* NameProperty = CastField<FNameProperty>(KeyProperty))
	{
		OutKey = NameProperty->GetPropertyValue(KeyPtr).ToString();
		return true;
	}
	return false;
}

FAssetDocumentCapabilityResult ExtractMapAuthoredValues(FMapProperty* MapProperty, const void* ValuePtr, TSharedPtr<FJsonValue>& OutValue, const FString& Path)
{
	if (!MapProperty || !ValuePtr)
	{
		return ReflectedPropertyFailure(Path, TEXT("InvalidProperty"), TEXT("Map property and value are required"));
	}
	if (!IsSupportedMapKeyProperty(MapProperty->KeyProp))
	{
		const FString KeyTypeName = MapProperty->KeyProp ? MapProperty->KeyProp->GetClass()->GetName() : TEXT("None");
		return ReflectedPropertyFailure(Path, TEXT("UnsupportedProperty"), FString::Printf(TEXT("Map property '%s' has unsupported key type '%s'"), *MapProperty->GetName(), *KeyTypeName));
	}

	FScriptMapHelper MapHelper(MapProperty, ValuePtr);
	TSharedPtr<FJsonObject> MapJson = MakeShared<FJsonObject>();
	for (int32 Index = 0; Index < MapHelper.Num(); ++Index)
	{
		if (!MapHelper.IsValidIndex(Index))
		{
			continue;
		}

		FString KeyString;
		if (!TryExtractMapKeyToString(MapProperty->KeyProp, MapHelper.GetKeyPtr(Index), KeyString))
		{
			const FString KeyTypeName = MapProperty->KeyProp ? MapProperty->KeyProp->GetClass()->GetName() : TEXT("None");
			return ReflectedPropertyFailure(Path, TEXT("UnsupportedProperty"), FString::Printf(TEXT("Map property '%s' has unsupported key type '%s'"), *MapProperty->GetName(), *KeyTypeName));
		}

		TSharedPtr<FJsonValue> ValueJson;
		const FAssetDocumentCapabilityResult ValueResult =
			ExtractAuthoredPropertyValue(MapProperty->ValueProp, MapHelper.GetValuePtr(Index), ValueJson, ReflectedPropertyJoinPath(Path, KeyString));
		if (!ValueResult.bSuccess)
		{
			return ValueResult;
		}
		if (ValueJson.IsValid())
		{
			MapJson->SetField(KeyString, ValueJson);
		}
	}

	OutValue = MakeShared<FJsonValueObject>(MapJson);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractSetAuthoredValues(FSetProperty* SetProperty, const void* ValuePtr, TSharedPtr<FJsonValue>& OutValue, const FString& Path)
{
	if (!SetProperty || !ValuePtr)
	{
		return ReflectedPropertyFailure(Path, TEXT("InvalidProperty"), TEXT("Set property and value are required"));
	}

	FScriptSetHelper SetHelper(SetProperty, ValuePtr);
	TArray<TSharedPtr<FJsonValue>> SetJson;
	for (int32 Index = 0; Index < SetHelper.Num(); ++Index)
	{
		if (!SetHelper.IsValidIndex(Index))
		{
			continue;
		}

		TSharedPtr<FJsonValue> ElementJson;
		const FAssetDocumentCapabilityResult ElementResult =
			ExtractAuthoredPropertyValue(SetProperty->ElementProp, SetHelper.GetElementPtr(Index), ElementJson, ReflectedPropertyJoinPath(Path, FString::FromInt(Index)));
		if (!ElementResult.bSuccess)
		{
			return ElementResult;
		}
		if (ElementJson.IsValid())
		{
			SetJson.Add(ElementJson);
		}
	}

	SetJson.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
	{
		return FAssetDocumentCanonicalJson::WriteCanonicalJson(Left) < FAssetDocumentCanonicalJson::WriteCanonicalJson(Right);
	});

	OutValue = MakeShared<FJsonValueArray>(SetJson);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractAuthoredPropertyValue(FProperty* Property, const void* ValuePtr, TSharedPtr<FJsonValue>& OutValue, const FString& Path)
{
	if (!Property || !ValuePtr)
	{
		return ReflectedPropertyFailure(Path, TEXT("InvalidProperty"), TEXT("Property and value are required"));
	}

	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		return IsBlackboardKeySelectorProperty(StructProperty)
			? FAssetDocumentReflectedPropertyUtils::ExtractBlackboardKeySelector(StructProperty, ValuePtr, OutValue, Path)
			: ExtractStructAuthoredProperties(StructProperty, ValuePtr, OutValue, Path);
	}

	if (IsClassReferenceProperty(Property) || IsObjectReferenceProperty(Property))
	{
		OutValue = ExtractReferenceProperty(Property, ValuePtr);
		return OutValue.IsValid()
			? FAssetDocumentCapabilityResult::Success()
			: ReflectedPropertyFailure(Path, TEXT("UnsupportedProperty"), FString::Printf(TEXT("Property '%s' could not be extracted"), *Property->GetName()));
	}

	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		return ExtractArrayAuthoredValues(ArrayProperty, ValuePtr, OutValue, Path);
	}

	if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		return ExtractMapAuthoredValues(MapProperty, ValuePtr, OutValue, Path);
	}

	if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		return ExtractSetAuthoredValues(SetProperty, ValuePtr, OutValue, Path);
	}

	OutValue = FPropertySetterUtils::ExtractPropertyToJson(Property, ValuePtr);
	return OutValue.IsValid()
		? FAssetDocumentCapabilityResult::Success()
		: ReflectedPropertyFailure(Path, TEXT("UnsupportedProperty"), FString::Printf(TEXT("Property '%s' is not supported"), *Property->GetName()));
}

FAssetDocumentCapabilityResult ExtractSingleProperty(FProperty* Property, const void* ValuePtr, TSharedPtr<FJsonValue>& OutValue, const FString& Path)
{
	return ExtractAuthoredPropertyValue(Property, ValuePtr, OutValue, Path);
}

FAssetDocumentCapabilityResult ProcessProperties(UObject* Object, const TSharedRef<FJsonObject>& Properties, const FString& Path, bool bApply)
{
	if (!Object)
	{
		return ReflectedPropertyFailure(Path, TEXT("InvalidObject"), TEXT("Object is required"));
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
	{
		const FString PropertyPath = ReflectedPropertyJoinPath(Path, Pair.Key);
		FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), *Pair.Key);
		if (!Property)
		{
			return ReflectedPropertyFailure(PropertyPath, TEXT("UnknownProperty"), FString::Printf(TEXT("Property '%s' does not exist"), *Pair.Key));
		}

		if (IsBodyTreeOwnedProperty(Property))
		{
			return ReflectedPropertyFailure(PropertyPath, TEXT("BodyTreeProperty"), FString::Printf(TEXT("Property '%s' is owned by Body.Tree"), *Pair.Key));
		}

		if (!IsAuthoredEditableProperty(Property))
		{
			return ReflectedPropertyFailure(PropertyPath, TEXT("NonAuthoredProperty"), FString::Printf(TEXT("Property '%s' is not authored: %s"), *Pair.Key, *GetNonAuthoredReason(Property)));
		}

		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property); IsBlackboardKeySelectorProperty(StructProperty))
		{
			const TSharedPtr<FJsonObject>* SelectorObject = nullptr;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(SelectorObject) || !SelectorObject || !SelectorObject->IsValid())
			{
				return ReflectedPropertyFailure(PropertyPath, TEXT("InvalidPropertyValue"), TEXT("BlackboardKeySelector must be an object"));
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

		if (RequiresCustomStructApply(Property, Pair.Value))
		{
			UObject* TargetObject = Object;
			TStrongObjectPtr<UObject> Duplicate;
			if (!bApply)
			{
				Duplicate.Reset(DuplicateObject<UObject>(Object, GetTransientPackage()));
				TargetObject = Duplicate.Get();
				if (!TargetObject)
				{
					return ReflectedPropertyFailure(Path, TEXT("PreflightFailed"), TEXT("Failed to create property validation duplicate"));
				}
				Property = FindFProperty<FProperty>(TargetObject->GetClass(), *Pair.Key);
			}

			const FAssetDocumentCapabilityResult ApplyResult = ApplyPropertyValueWithCustomFields(
				Property,
				Property->ContainerPtrToValuePtr<void>(TargetObject),
				Pair.Value,
				PropertyPath);
			if (!ApplyResult.bSuccess)
			{
				return ApplyResult;
			}
			continue;
		}

		const TSharedPtr<FJsonValue> ValueToApply = NormalizeAuthoredValueForSetter(Property, Pair.Value);
		if (!ValueToApply.IsValid())
		{
			return ReflectedPropertyFailure(PropertyPath, TEXT("InvalidPropertyValue"), FString::Printf(TEXT("Property '%s' value is invalid"), *Pair.Key));
		}

		UObject* TargetObject = Object;
		TStrongObjectPtr<UObject> Duplicate;
		if (!bApply)
		{
			Duplicate.Reset(DuplicateObject<UObject>(Object, GetTransientPackage()));
			TargetObject = Duplicate.Get();
			if (!TargetObject)
			{
				return ReflectedPropertyFailure(Path, TEXT("PreflightFailed"), TEXT("Failed to create property validation duplicate"));
			}
			Property = FindFProperty<FProperty>(TargetObject->GetClass(), *Pair.Key);
		}

		if (!FPropertySetterUtils::SetPropertyFromJson(TargetObject, Property, ValueToApply))
		{
			return ReflectedPropertyFailure(PropertyPath, TEXT("InvalidPropertyValue"), FString::Printf(TEXT("Failed to set property '%s'"), *Pair.Key));
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
		return ReflectedPropertyFailure(Path, TEXT("InvalidObject"), TEXT("Object is required"));
	}

	for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
	{
		FProperty* Property = *It;
		if (IsBodyTreeOwnedProperty(Property) || !IsAuthoredEditableProperty(Property))
		{
			continue;
		}

		TSharedPtr<FJsonValue> ExtractedValue;
		const FString PropertyPath = ReflectedPropertyJoinPath(Path, Property->GetName());
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
		return ReflectedPropertyFailure(Path, TEXT("InvalidObject"), TEXT("Object is required"));
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
		return ReflectedPropertyFailure(Path, TEXT("PreflightFailed"), TEXT("Failed to create property diff duplicate"));
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
		const FString PropertyPath = ReflectedPropertyJoinPath(Path, Pair.Key);
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
		return ReflectedPropertyFailure(Path, TEXT("InvalidProperty"), TEXT("FBlackboardKeySelector property and value are required"));
	}

	const FAssetDocumentCapabilityResult ValidateResult = ValidateBlackboardKeySelectorJson(Json, Path);
	if (!ValidateResult.bSuccess)
	{
		return ValidateResult;
	}

	FBlackboardKeySelector* Selector = static_cast<FBlackboardKeySelector*>(ValuePtr);
	FString SelectedKeyName;
	if (Json->TryGetStringField(TEXT("Key"), SelectedKeyName))
	{
		Selector->SelectedKeyName = FName(*SelectedKeyName);
		Selector->InvalidateResolvedKey();
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
		return ReflectedPropertyFailure(Path, TEXT("InvalidProperty"), TEXT("FBlackboardKeySelector property and value are required"));
	}

	const FBlackboardKeySelector* Selector = static_cast<const FBlackboardKeySelector*>(ValuePtr);
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("Key"), Selector->SelectedKeyName.ToString());

	OutValue = MakeShared<FJsonValueObject>(Json);
	return FAssetDocumentCapabilityResult::Success();
}
