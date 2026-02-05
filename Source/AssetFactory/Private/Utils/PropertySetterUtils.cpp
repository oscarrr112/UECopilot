// Copyright ProjectRPG. All Rights Reserved.

#include "Utils/PropertySetterUtils.h"
#include "AssetFactoryModule.h"
#include "Utils/ClassFinderUtils.h"

#include "UObject/EnumProperty.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
#include "Engine/Texture2D.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Margin.h"
#include "Widgets/Layout/Anchors.h"

//////////////////////////////////////////////////////////////////////////
// New Typed Property System
//////////////////////////////////////////////////////////////////////////

FParsedTypeInfo FPropertySetterUtils::ParseTypeString(const FString& TypeString)
{
	FParsedTypeInfo Result;

	if (TypeString.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Type string is empty");
		return Result;
	}

	// Check for compound types with colon separator
	// Examples: "Object:UStaticMesh", "Array:Float", "Map:String:Int", "Enum:ECollisionChannel", "Struct:FMyStruct"
	TArray<FString> Parts;
	TypeString.ParseIntoArray(Parts, TEXT(":"), true);

	if (Parts.Num() == 0)
	{
		Result.ErrorMessage = TEXT("Failed to parse type string");
		return Result;
	}

	Result.BaseType = Parts[0];

	// Handle different base types
	if (Result.BaseType == TEXT("Object") || Result.BaseType == TEXT("SoftObject") || Result.BaseType == TEXT("Class"))
	{
		if (Parts.Num() < 2)
		{
			Result.ErrorMessage = FString::Printf(TEXT("%s type requires a class name (e.g., %s:UStaticMesh)"), *Result.BaseType, *Result.BaseType);
			return Result;
		}
		Result.SubType = Parts[1];
	}
	else if (Result.BaseType == TEXT("Array"))
	{
		if (Parts.Num() < 2)
		{
			Result.ErrorMessage = TEXT("Array type requires element type (e.g., Array:Float or Array:Object:UStaticMesh)");
			return Result;
		}
		// Element type could be compound like "Object:UStaticMesh"
		if (Parts.Num() >= 3)
		{
			Result.ElementType = Parts[1] + TEXT(":") + Parts[2];
		}
		else
		{
			Result.ElementType = Parts[1];
		}
	}
	else if (Result.BaseType == TEXT("Map"))
	{
		if (Parts.Num() < 3)
		{
			Result.ErrorMessage = TEXT("Map type requires key and value types (e.g., Map:String:Float)");
			return Result;
		}
		Result.KeyType = Parts[1];
		// Value type could be compound
		if (Parts.Num() >= 4)
		{
			Result.ValueType = Parts[2] + TEXT(":") + Parts[3];
		}
		else
		{
			Result.ValueType = Parts[2];
		}
	}
	else if (Result.BaseType == TEXT("Enum"))
	{
		if (Parts.Num() < 2)
		{
			Result.ErrorMessage = TEXT("Enum type requires enum name (e.g., Enum:ECollisionChannel)");
			return Result;
		}
		Result.SubType = Parts[1];
	}
	else if (Result.BaseType == TEXT("Struct"))
	{
		if (Parts.Num() < 2)
		{
			Result.ErrorMessage = TEXT("Struct type requires struct name (e.g., Struct:FMyStruct)");
			return Result;
		}
		Result.SubType = Parts[1];
	}
	// Basic types and built-in structs don't need additional parsing
	// Bool, Byte, Int, Int64, Float, Double, String, Name, Text
	// FVector, FVector2D, FRotator, FTransform, FLinearColor, FColor, FMargin

	Result.bIsValid = true;
	return Result;
}

FPropertyValidationResult FPropertySetterUtils::ValidateTypedValue(const FParsedTypeInfo& TypeInfo, TSharedPtr<FJsonValue> Value)
{
	if (!TypeInfo.bIsValid)
	{
		return FPropertyValidationResult::Failure(TypeInfo.ErrorMessage);
	}

	if (!Value.IsValid())
	{
		return FPropertyValidationResult::Failure(TEXT("Value is null"));
	}

	const FString& Type = TypeInfo.BaseType;

	// Boolean
	if (Type == TEXT("Bool"))
	{
		bool Temp;
		if (!Value->TryGetBool(Temp))
		{
			return FPropertyValidationResult::Failure(TEXT("Expected boolean value"));
		}
	}
	// Numeric types
	else if (Type == TEXT("Byte") || Type == TEXT("Int") || Type == TEXT("Int64") ||
			 Type == TEXT("Float") || Type == TEXT("Double"))
	{
		double Temp;
		if (!Value->TryGetNumber(Temp))
		{
			return FPropertyValidationResult::Failure(FString::Printf(TEXT("Expected numeric value for type %s"), *Type));
		}
	}
	// String types
	else if (Type == TEXT("String") || Type == TEXT("Name") || Type == TEXT("Text"))
	{
		FString Temp;
		if (!Value->TryGetString(Temp))
		{
			return FPropertyValidationResult::Failure(FString::Printf(TEXT("Expected string value for type %s"), *Type));
		}
	}
	// Vector types - expect array
	else if (Type == TEXT("FVector") || Type == TEXT("FRotator"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		if (!Value->TryGetArray(Arr) || Arr->Num() < 3)
		{
			return FPropertyValidationResult::Failure(FString::Printf(TEXT("Expected array with 3 elements for %s"), *Type));
		}
	}
	else if (Type == TEXT("FVector2D"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		if (!Value->TryGetArray(Arr) || Arr->Num() < 2)
		{
			return FPropertyValidationResult::Failure(TEXT("Expected array with 2 elements for FVector2D"));
		}
	}
	else if (Type == TEXT("FTransform"))
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (!Value->TryGetObject(Obj))
		{
			return FPropertyValidationResult::Failure(TEXT("Expected object with Location/Rotation/Scale for FTransform"));
		}
	}
	// Color types - accept array, string, or object
	else if (Type == TEXT("FLinearColor") || Type == TEXT("FColor"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		FString Str;
		const TSharedPtr<FJsonObject>* Obj;
		if (!Value->TryGetArray(Arr) && !Value->TryGetString(Str) && !Value->TryGetObject(Obj))
		{
			return FPropertyValidationResult::Failure(FString::Printf(TEXT("Expected array [R,G,B,A], hex string, or object for %s"), *Type));
		}
	}
	// Object/SoftObject/Class references - expect string path
	else if (Type == TEXT("Object") || Type == TEXT("SoftObject") || Type == TEXT("Class"))
	{
		FString Temp;
		if (!Value->TryGetString(Temp))
		{
			return FPropertyValidationResult::Failure(FString::Printf(TEXT("Expected asset path string for %s"), *Type));
		}
	}
	// Array - expect JSON array
	else if (Type == TEXT("Array"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		if (!Value->TryGetArray(Arr))
		{
			return FPropertyValidationResult::Failure(TEXT("Expected array value for Array type"));
		}
		// Optionally validate each element
		FParsedTypeInfo ElementType = ParseTypeString(TypeInfo.ElementType);
		if (ElementType.bIsValid)
		{
			for (int32 i = 0; i < Arr->Num(); ++i)
			{
				auto ElementResult = ValidateTypedValue(ElementType, (*Arr)[i]);
				if (!ElementResult.bIsValid)
				{
					return FPropertyValidationResult::Failure(FString::Printf(TEXT("Array element %d: %s"), i, *ElementResult.ErrorMessage));
				}
			}
		}
	}
	// Map - expect JSON object
	else if (Type == TEXT("Map"))
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (!Value->TryGetObject(Obj))
		{
			return FPropertyValidationResult::Failure(TEXT("Expected object value for Map type"));
		}
	}
	// Enum - expect string
	else if (Type == TEXT("Enum"))
	{
		FString Temp;
		if (!Value->TryGetString(Temp))
		{
			return FPropertyValidationResult::Failure(TEXT("Expected string value for Enum type"));
		}
	}
	// Struct - expect object with nested properties
	else if (Type == TEXT("Struct"))
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (!Value->TryGetObject(Obj))
		{
			return FPropertyValidationResult::Failure(TEXT("Expected object value for Struct type"));
		}
	}
	// FMargin - accept number, array, or object
	else if (Type == TEXT("FMargin"))
	{
		double Num;
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		const TSharedPtr<FJsonObject>* Obj;
		if (!Value->TryGetNumber(Num) && !Value->TryGetArray(Arr) && !Value->TryGetObject(Obj))
		{
			return FPropertyValidationResult::Failure(TEXT("Expected number, array, or object for FMargin"));
		}
	}

	return FPropertyValidationResult::Success();
}

bool FPropertySetterUtils::ValidateTypedProperties(TSharedPtr<FJsonObject> Properties, TArray<FString>& OutErrors)
{
	if (!Properties.IsValid())
	{
		OutErrors.Add(TEXT("Properties object is null"));
		return false;
	}

	bool bAllValid = true;

	for (const auto& Pair : Properties->Values)
	{
		const FString& PropName = Pair.Key;
		const TSharedPtr<FJsonValue>& PropValue = Pair.Value;

		const TSharedPtr<FJsonObject>* TypedObj;
		if (!PropValue->TryGetObject(TypedObj))
		{
			OutErrors.Add(FString::Printf(TEXT("Property '%s': expected object with 'type' and 'value' fields"), *PropName));
			bAllValid = false;
			continue;
		}

		FString TypeStr;
		if (!(*TypedObj)->TryGetStringField(TEXT("type"), TypeStr))
		{
			OutErrors.Add(FString::Printf(TEXT("Property '%s': missing 'type' field"), *PropName));
			bAllValid = false;
			continue;
		}

		TSharedPtr<FJsonValue> ValueField = (*TypedObj)->TryGetField(TEXT("value"));
		if (!ValueField.IsValid())
		{
			OutErrors.Add(FString::Printf(TEXT("Property '%s': missing 'value' field"), *PropName));
			bAllValid = false;
			continue;
		}

		FParsedTypeInfo TypeInfo = ParseTypeString(TypeStr);
		if (!TypeInfo.bIsValid)
		{
			OutErrors.Add(FString::Printf(TEXT("Property '%s': invalid type '%s' - %s"), *PropName, *TypeStr, *TypeInfo.ErrorMessage));
			bAllValid = false;
			continue;
		}

		auto ValidationResult = ValidateTypedValue(TypeInfo, ValueField);
		if (!ValidationResult.bIsValid)
		{
			OutErrors.Add(FString::Printf(TEXT("Property '%s': %s"), *PropName, *ValidationResult.ErrorMessage));
			bAllValid = false;
		}
	}

	return bAllValid;
}

UScriptStruct* FPropertySetterUtils::FindStructByName(const FString& StructName)
{
	// Handle common built-in structs first
	if (StructName == TEXT("FVector") || StructName == TEXT("Vector"))
	{
		return TBaseStructure<FVector>::Get();
	}
	if (StructName == TEXT("FVector2D") || StructName == TEXT("Vector2D"))
	{
		return TBaseStructure<FVector2D>::Get();
	}
	if (StructName == TEXT("FRotator") || StructName == TEXT("Rotator"))
	{
		return TBaseStructure<FRotator>::Get();
	}
	if (StructName == TEXT("FTransform") || StructName == TEXT("Transform"))
	{
		return TBaseStructure<FTransform>::Get();
	}
	if (StructName == TEXT("FLinearColor") || StructName == TEXT("LinearColor"))
	{
		return TBaseStructure<FLinearColor>::Get();
	}
	if (StructName == TEXT("FColor") || StructName == TEXT("Color"))
	{
		return TBaseStructure<FColor>::Get();
	}
	if (StructName == TEXT("FMargin") || StructName == TEXT("Margin"))
	{
		return TBaseStructure<FMargin>::Get();
	}
	if (StructName == TEXT("FSoftObjectPath") || StructName == TEXT("SoftObjectPath"))
	{
		return TBaseStructure<FSoftObjectPath>::Get();
	}

	// Dynamic lookup for other structs
	FString SearchName = StructName;
	if (!SearchName.StartsWith(TEXT("F")))
	{
		SearchName = TEXT("F") + SearchName;
	}

	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		if (It->GetName() == SearchName || It->GetName() == StructName)
		{
			return *It;
		}
	}

	UE_LOG(LogAssetFactory, Warning, TEXT("Struct '%s' not found"), *StructName);
	return nullptr;
}

UEnum* FPropertySetterUtils::FindEnumByName(const FString& EnumName)
{
	FString SearchName = EnumName;
	if (!SearchName.StartsWith(TEXT("E")))
	{
		SearchName = TEXT("E") + SearchName;
	}

	for (TObjectIterator<UEnum> It; It; ++It)
	{
		if (It->GetName() == SearchName || It->GetName() == EnumName)
		{
			return *It;
		}
	}

	UE_LOG(LogAssetFactory, Warning, TEXT("Enum '%s' not found"), *EnumName);
	return nullptr;
}

bool FPropertySetterUtils::SetValueFromTypedJson(void* ValuePtr, const FParsedTypeInfo& TypeInfo, TSharedPtr<FJsonValue> JsonValue)
{
	if (!ValuePtr || !TypeInfo.bIsValid || !JsonValue.IsValid())
	{
		return false;
	}

	const FString& Type = TypeInfo.BaseType;

	// Boolean
	if (Type == TEXT("Bool"))
	{
		bool Value = false;
		if (JsonValue->TryGetBool(Value))
		{
			*static_cast<bool*>(ValuePtr) = Value;
			return true;
		}
	}
	// Byte
	else if (Type == TEXT("Byte"))
	{
		double Value = 0;
		if (JsonValue->TryGetNumber(Value))
		{
			*static_cast<uint8*>(ValuePtr) = static_cast<uint8>(Value);
			return true;
		}
	}
	// Int
	else if (Type == TEXT("Int"))
	{
		double Value = 0;
		if (JsonValue->TryGetNumber(Value))
		{
			*static_cast<int32*>(ValuePtr) = static_cast<int32>(Value);
			return true;
		}
	}
	// Int64
	else if (Type == TEXT("Int64"))
	{
		double Value = 0;
		if (JsonValue->TryGetNumber(Value))
		{
			*static_cast<int64*>(ValuePtr) = static_cast<int64>(Value);
			return true;
		}
	}
	// Float
	else if (Type == TEXT("Float"))
	{
		double Value = 0;
		if (JsonValue->TryGetNumber(Value))
		{
			*static_cast<float*>(ValuePtr) = static_cast<float>(Value);
			return true;
		}
	}
	// Double
	else if (Type == TEXT("Double"))
	{
		double Value = 0;
		if (JsonValue->TryGetNumber(Value))
		{
			*static_cast<double*>(ValuePtr) = Value;
			return true;
		}
	}
	// String
	else if (Type == TEXT("String"))
	{
		FString Value;
		if (JsonValue->TryGetString(Value))
		{
			*static_cast<FString*>(ValuePtr) = Value;
			return true;
		}
	}
	// Name
	else if (Type == TEXT("Name"))
	{
		FString Value;
		if (JsonValue->TryGetString(Value))
		{
			*static_cast<FName*>(ValuePtr) = FName(*Value);
			return true;
		}
	}
	// Text
	else if (Type == TEXT("Text"))
	{
		FString Value;
		if (JsonValue->TryGetString(Value))
		{
			*static_cast<FText*>(ValuePtr) = FText::FromString(Value);
			return true;
		}
	}
	// FVector
	else if (Type == TEXT("FVector"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		if (JsonValue->TryGetArray(Arr) && Arr->Num() >= 3)
		{
			*static_cast<FVector*>(ValuePtr) = ParseVector(*Arr);
			return true;
		}
	}
	// FVector2D
	else if (Type == TEXT("FVector2D"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		if (JsonValue->TryGetArray(Arr) && Arr->Num() >= 2)
		{
			*static_cast<FVector2D*>(ValuePtr) = ParseVector2D(*Arr);
			return true;
		}
	}
	// FRotator
	else if (Type == TEXT("FRotator"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		if (JsonValue->TryGetArray(Arr) && Arr->Num() >= 3)
		{
			double Pitch = 0, Yaw = 0, Roll = 0;
			(*Arr)[0]->TryGetNumber(Pitch);
			(*Arr)[1]->TryGetNumber(Yaw);
			(*Arr)[2]->TryGetNumber(Roll);
			*static_cast<FRotator*>(ValuePtr) = FRotator(Pitch, Yaw, Roll);
			return true;
		}
	}
	// FTransform
	else if (Type == TEXT("FTransform"))
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (JsonValue->TryGetObject(Obj))
		{
			FTransform Transform;

			const TArray<TSharedPtr<FJsonValue>>* LocArr;
			if ((*Obj)->TryGetArrayField(TEXT("Location"), LocArr) && LocArr->Num() >= 3)
			{
				Transform.SetLocation(ParseVector(*LocArr));
			}

			const TArray<TSharedPtr<FJsonValue>>* RotArr;
			if ((*Obj)->TryGetArrayField(TEXT("Rotation"), RotArr) && RotArr->Num() >= 3)
			{
				double Pitch = 0, Yaw = 0, Roll = 0;
				(*RotArr)[0]->TryGetNumber(Pitch);
				(*RotArr)[1]->TryGetNumber(Yaw);
				(*RotArr)[2]->TryGetNumber(Roll);
				Transform.SetRotation(FRotator(Pitch, Yaw, Roll).Quaternion());
			}

			const TArray<TSharedPtr<FJsonValue>>* ScaleArr;
			if ((*Obj)->TryGetArrayField(TEXT("Scale"), ScaleArr) && ScaleArr->Num() >= 3)
			{
				Transform.SetScale3D(ParseVector(*ScaleArr));
			}

			*static_cast<FTransform*>(ValuePtr) = Transform;
			return true;
		}
	}
	// FLinearColor
	else if (Type == TEXT("FLinearColor"))
	{
		*static_cast<FLinearColor*>(ValuePtr) = ParseColor(JsonValue);
		return true;
	}
	// FColor
	else if (Type == TEXT("FColor"))
	{
		FLinearColor Linear = ParseColor(JsonValue);
		*static_cast<FColor*>(ValuePtr) = Linear.ToFColor(true);
		return true;
	}
	// FMargin
	else if (Type == TEXT("FMargin"))
	{
		*static_cast<FMargin*>(ValuePtr) = ParseMargin(JsonValue);
		return true;
	}
	// Object reference
	else if (Type == TEXT("Object"))
	{
		FString ObjectPath;
		if (JsonValue->TryGetString(ObjectPath))
		{
			UClass* ExpectedClass = FClassFinderUtils::FindClassByName(TypeInfo.SubType, UObject::StaticClass(), false);
			if (!ExpectedClass)
			{
				ExpectedClass = UObject::StaticClass();
			}

			UObject* LoadedObject = StaticLoadObject(ExpectedClass, nullptr, *ObjectPath);
			if (LoadedObject)
			{
				*static_cast<UObject**>(ValuePtr) = LoadedObject;
				return true;
			}
			UE_LOG(LogAssetFactory, Warning, TEXT("Failed to load object: %s"), *ObjectPath);
		}
	}
	// Soft object reference
	else if (Type == TEXT("SoftObject"))
	{
		FString ObjectPath;
		if (JsonValue->TryGetString(ObjectPath))
		{
			if (!ObjectPath.Contains(TEXT(".")))
			{
				FString AssetName = FPaths::GetBaseFilename(ObjectPath);
				ObjectPath = ObjectPath + TEXT(".") + AssetName;
			}
			FSoftObjectPath SoftPath(ObjectPath);
			*static_cast<FSoftObjectPath*>(ValuePtr) = SoftPath;
			return true;
		}
	}
	// Enum
	else if (Type == TEXT("Enum"))
	{
		FString EnumValueStr;
		if (JsonValue->TryGetString(EnumValueStr))
		{
			UEnum* Enum = FindEnumByName(TypeInfo.SubType);
			if (Enum)
			{
				int64 EnumValue = Enum->GetValueByNameString(EnumValueStr);
				if (EnumValue == INDEX_NONE)
				{
					EnumValue = Enum->GetValueByNameString(Enum->GetName() + TEXT("::") + EnumValueStr);
				}
				if (EnumValue != INDEX_NONE)
				{
					*static_cast<uint8*>(ValuePtr) = static_cast<uint8>(EnumValue);
					return true;
				}
			}
		}
	}
	// Struct - recursive handling
	else if (Type == TEXT("Struct"))
	{
		UScriptStruct* Struct = FindStructByName(TypeInfo.SubType);
		if (Struct)
		{
			return SetStructValueFromTypedJson(ValuePtr, Struct, JsonValue);
		}
	}

	return false;
}

bool FPropertySetterUtils::SetStructValueFromTypedJson(void* ValuePtr, UScriptStruct* Struct, TSharedPtr<FJsonValue> JsonValue)
{
	if (!ValuePtr || !Struct || !JsonValue.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* Obj;
	if (!JsonValue->TryGetObject(Obj))
	{
		return false;
	}

	for (const auto& Pair : (*Obj)->Values)
	{
		const FString& FieldName = Pair.Key;
		const TSharedPtr<FJsonValue>& FieldValue = Pair.Value;

		// Each field should be { "type": "...", "value": ... }
		const TSharedPtr<FJsonObject>* TypedFieldObj;
		if (!FieldValue->TryGetObject(TypedFieldObj))
		{
			continue;
		}

		FString TypeStr;
		if (!(*TypedFieldObj)->TryGetStringField(TEXT("type"), TypeStr))
		{
			continue;
		}

		TSharedPtr<FJsonValue> ValueField = (*TypedFieldObj)->TryGetField(TEXT("value"));
		if (!ValueField.IsValid())
		{
			continue;
		}

		FProperty* FieldProp = Struct->FindPropertyByName(*FieldName);
		if (!FieldProp)
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Struct field '%s' not found in %s"), *FieldName, *Struct->GetName());
			continue;
		}

		void* FieldPtr = FieldProp->ContainerPtrToValuePtr<void>(ValuePtr);
		FParsedTypeInfo TypeInfo = ParseTypeString(TypeStr);

		if (TypeInfo.bIsValid)
		{
			SetValueFromTypedJson(FieldPtr, TypeInfo, ValueField);
		}
	}

	return true;
}

bool FPropertySetterUtils::SetTypedPropertyFromJson(UObject* Object, const FString& PropertyName, TSharedPtr<FJsonObject> TypedValue)
{
	if (!Object || !TypedValue.IsValid())
	{
		return false;
	}

	FString TypeStr;
	if (!TypedValue->TryGetStringField(TEXT("type"), TypeStr))
	{
		UE_LOG(LogAssetFactory, Warning, TEXT("Property '%s': missing 'type' field"), *PropertyName);
		return false;
	}

	TSharedPtr<FJsonValue> ValueField = TypedValue->TryGetField(TEXT("value"));
	if (!ValueField.IsValid())
	{
		UE_LOG(LogAssetFactory, Warning, TEXT("Property '%s': missing 'value' field"), *PropertyName);
		return false;
	}

	FParsedTypeInfo TypeInfo = ParseTypeString(TypeStr);
	if (!TypeInfo.bIsValid)
	{
		UE_LOG(LogAssetFactory, Warning, TEXT("Property '%s': invalid type '%s' - %s"), *PropertyName, *TypeStr, *TypeInfo.ErrorMessage);
		return false;
	}

	UClass* ObjectClass = Object->GetClass();
	FProperty* Property = ObjectClass->FindPropertyByName(*PropertyName);
	if (!Property)
	{
		UE_LOG(LogAssetFactory, Warning, TEXT("Property '%s' not found on class '%s'"), *PropertyName, *ObjectClass->GetName());
		return false;
	}

	// Special handling for arrays - use reflection-based array handling
	if (TypeInfo.BaseType == TEXT("Array"))
	{
		FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property);
		if (ArrayProp)
		{
			const TArray<TSharedPtr<FJsonValue>>* ArrayValues;
			if (ValueField->TryGetArray(ArrayValues))
			{
				return SetArrayProperty(Object, ArrayProp, *ArrayValues);
			}
		}
		return false;
	}

	// Special handling for maps - use reflection-based map handling
	if (TypeInfo.BaseType == TEXT("Map"))
	{
		FMapProperty* MapProp = CastField<FMapProperty>(Property);
		if (MapProp)
		{
			const TSharedPtr<FJsonObject>* MapObj;
			if (ValueField->TryGetObject(MapObj))
			{
				return SetMapProperty(Object, MapProp, *MapObj);
			}
		}
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
	return SetValueFromTypedJson(ValuePtr, TypeInfo, ValueField);
}

void FPropertySetterUtils::SetTypedPropertiesFromJson(UObject* Object, TSharedPtr<FJsonObject> Properties)
{
	if (!Object || !Properties.IsValid())
	{
		return;
	}

	for (const auto& Pair : Properties->Values)
	{
		const FString& PropertyName = Pair.Key;
		const TSharedPtr<FJsonValue>& PropertyValue = Pair.Value;

		const TSharedPtr<FJsonObject>* TypedObj;
		if (!PropertyValue->TryGetObject(TypedObj))
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Property '%s': expected object with 'type' and 'value' fields"), *PropertyName);
			continue;
		}

		if (!SetTypedPropertyFromJson(Object, PropertyName, *TypedObj))
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Failed to set typed property '%s'"), *PropertyName);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Legacy Property System
//////////////////////////////////////////////////////////////////////////

bool FPropertySetterUtils::SetPropertyFromJson(UObject* Object, FProperty* Property, TSharedPtr<FJsonValue> JsonValue)
{
	if (!Object || !Property || !JsonValue.IsValid())
	{
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
	return SetPropertyValueInternal(Object, Property, ValuePtr, JsonValue);
}

void FPropertySetterUtils::SetPropertiesFromJson(UObject* Object, TSharedPtr<FJsonObject> Properties)
{
	if (!Object || !Properties.IsValid())
	{
		return;
	}

	UClass* ObjectClass = Object->GetClass();

	for (const auto& Pair : Properties->Values)
	{
		const FString& PropertyName = Pair.Key;
		const TSharedPtr<FJsonValue>& JsonValue = Pair.Value;

		FProperty* Property = ObjectClass->FindPropertyByName(*PropertyName);
		if (!Property)
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Property '%s' not found on class '%s'"),
				*PropertyName, *ObjectClass->GetName());
			continue;
		}

		if (!SetPropertyFromJson(Object, Property, JsonValue))
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Failed to set property '%s'"), *PropertyName);
		}
	}
}

bool FPropertySetterUtils::SetPropertyValueInternal(UObject* Object, FProperty* Property, void* ValuePtr, TSharedPtr<FJsonValue> JsonValue)
{
	if (!Property || !ValuePtr || !JsonValue.IsValid())
	{
		return false;
	}

	// Handle numeric types (int, float, double, etc.)
	if (FNumericProperty* NumericProp = CastField<FNumericProperty>(Property))
	{
		double Value = 0.0;
		if (JsonValue->TryGetNumber(Value))
		{
			if (NumericProp->IsFloatingPoint())
			{
				NumericProp->SetFloatingPointPropertyValue(ValuePtr, Value);
			}
			else
			{
				NumericProp->SetIntPropertyValue(ValuePtr, static_cast<int64>(Value));
			}
			return true;
		}
	}
	// Handle bool
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		bool Value = false;
		if (JsonValue->TryGetBool(Value))
		{
			BoolProp->SetPropertyValue(ValuePtr, Value);
			return true;
		}
	}
	// Handle FString
	else if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		FString Value;
		if (JsonValue->TryGetString(Value))
		{
			StrProp->SetPropertyValue(ValuePtr, Value);
			return true;
		}
	}
	// Handle FName
	else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		FString Value;
		if (JsonValue->TryGetString(Value))
		{
			NameProp->SetPropertyValue(ValuePtr, FName(*Value));
			return true;
		}
	}
	// Handle FText
	else if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		FString Value;
		if (JsonValue->TryGetString(Value))
		{
			TextProp->SetPropertyValue(ValuePtr, FText::FromString(Value));
			return true;
		}
	}
	// Handle Enum (FEnumProperty)
	else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		FString EnumValueStr;
		if (JsonValue->TryGetString(EnumValueStr))
		{
			UEnum* Enum = EnumProp->GetEnum();
			int64 EnumValue = Enum->GetValueByNameString(EnumValueStr);
			if (EnumValue == INDEX_NONE)
			{
				// Try with enum prefix
				EnumValue = Enum->GetValueByNameString(Enum->GetName() + TEXT("::") + EnumValueStr);
			}
			if (EnumValue != INDEX_NONE)
			{
				EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumValue);
				return true;
			}
			else
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("Invalid enum value '%s' for property '%s'"),
					*EnumValueStr, *Property->GetName());
			}
		}
	}
	// Handle ByteProperty (with or without enum)
	else if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (UEnum* Enum = ByteProp->Enum)
		{
			FString EnumValueStr;
			if (JsonValue->TryGetString(EnumValueStr))
			{
				int64 EnumValue = Enum->GetValueByNameString(EnumValueStr);
				if (EnumValue == INDEX_NONE)
				{
					EnumValue = Enum->GetValueByNameString(Enum->GetName() + TEXT("::") + EnumValueStr);
				}
				if (EnumValue != INDEX_NONE)
				{
					ByteProp->SetIntPropertyValue(ValuePtr, EnumValue);
					return true;
				}
			}
		}
		else
		{
			// Plain byte, treat as number
			double Value = 0.0;
			if (JsonValue->TryGetNumber(Value))
			{
				ByteProp->SetIntPropertyValue(ValuePtr, static_cast<int64>(Value));
				return true;
			}
		}
	}
	// Handle Struct types
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		return SetStructPropertyFromJson(StructProp, ValuePtr, JsonValue);
	}
	// Handle Object references (UObject*, TSoftObjectPtr, TSubclassOf)
	else if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
	{
		FString ObjectPath;
		if (JsonValue->TryGetString(ObjectPath))
		{
			return SetObjectReferenceFromPath(ObjProp, ValuePtr, ObjectPath);
		}
	}
	// Handle Soft Object Ptr
	else if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
	{
		FString ObjectPath;
		if (JsonValue->TryGetString(ObjectPath))
		{
			return SetSoftObjectProperty(Object, SoftObjProp, ObjectPath);
		}
	}
	// Handle TSubclassOf
	else if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
	{
		FString ClassPath;
		if (JsonValue->TryGetString(ClassPath))
		{
			return SetClassProperty(Object, ClassProp, ClassPath);
		}
	}
	// Handle TArray
	else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		const TArray<TSharedPtr<FJsonValue>>* ArrayValues;
		if (JsonValue->TryGetArray(ArrayValues))
		{
			return SetArrayProperty(Object, ArrayProp, *ArrayValues);
		}
	}
	// Handle TMap
	else if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		const TSharedPtr<FJsonObject>* MapObject;
		if (JsonValue->TryGetObject(MapObject))
		{
			return SetMapProperty(Object, MapProp, *MapObject);
		}
	}

	return false;
}

bool FPropertySetterUtils::SetStructPropertyFromJson(FStructProperty* StructProp, void* ValuePtr, TSharedPtr<FJsonValue> JsonValue)
{
	if (!StructProp || !ValuePtr || !JsonValue.IsValid())
	{
		return false;
	}

	UScriptStruct* Struct = StructProp->Struct;

	// FLinearColor - [R, G, B, A] array or {R, G, B, A} object or string
	if (Struct == TBaseStructure<FLinearColor>::Get())
	{
		*static_cast<FLinearColor*>(ValuePtr) = ParseColor(JsonValue);
		return true;
	}
	// FColor
	else if (Struct == TBaseStructure<FColor>::Get())
	{
		FLinearColor Color = ParseColor(JsonValue);
		*static_cast<FColor*>(ValuePtr) = Color.ToFColor(true);
		return true;
	}
	// FVector2D - [X, Y] array
	else if (Struct == TBaseStructure<FVector2D>::Get())
	{
		const TArray<TSharedPtr<FJsonValue>>* Array;
		if (JsonValue->TryGetArray(Array) && Array->Num() >= 2)
		{
			*static_cast<FVector2D*>(ValuePtr) = ParseVector2D(*Array);
			return true;
		}
	}
	// FVector - [X, Y, Z] array
	else if (Struct == TBaseStructure<FVector>::Get())
	{
		const TArray<TSharedPtr<FJsonValue>>* Array;
		if (JsonValue->TryGetArray(Array) && Array->Num() >= 3)
		{
			*static_cast<FVector*>(ValuePtr) = ParseVector(*Array);
			return true;
		}
	}
	// FMargin - number, [H, V], [L, T, R, B], or {Left, Top, Right, Bottom}
	else if (Struct == TBaseStructure<FMargin>::Get())
	{
		*static_cast<FMargin*>(ValuePtr) = ParseMargin(JsonValue);
		return true;
	}
	// FSlateColor
	else if (Struct->GetFName() == TEXT("SlateColor"))
	{
		FLinearColor Color = ParseColor(JsonValue);
		*static_cast<FSlateColor*>(ValuePtr) = FSlateColor(Color);
		return true;
	}
	// FSlateFontInfo
	else if (Struct->GetFName() == TEXT("SlateFontInfo"))
	{
		const TSharedPtr<FJsonObject>* FontObj;
		if (JsonValue->TryGetObject(FontObj))
		{
			*static_cast<FSlateFontInfo*>(ValuePtr) = ParseFont(*FontObj);
			return true;
		}
	}
	// FSlateBrush
	else if (Struct->GetFName() == TEXT("SlateBrush"))
	{
		const TSharedPtr<FJsonObject>* BrushObj;
		if (JsonValue->TryGetObject(BrushObj))
		{
			*static_cast<FSlateBrush*>(ValuePtr) = ParseBrush(*BrushObj);
			return true;
		}
	}
	// FAnchors
	else if (Struct->GetFName() == TEXT("Anchors"))
	{
		const TSharedPtr<FJsonObject>* AnchorsObj;
		if (JsonValue->TryGetObject(AnchorsObj))
		{
			*static_cast<FAnchors*>(ValuePtr) = ParseAnchors(*AnchorsObj);
			return true;
		}
	}
	// FSoftObjectPath
	else if (Struct == TBaseStructure<FSoftObjectPath>::Get())
	{
		FString PathStr;
		if (JsonValue->TryGetString(PathStr))
		{
			*static_cast<FSoftObjectPath*>(ValuePtr) = FSoftObjectPath(PathStr);
			return true;
		}
	}
	// Generic struct - try to set fields recursively
	else
	{
		const TSharedPtr<FJsonObject>* StructObj;
		if (JsonValue->TryGetObject(StructObj))
		{
			for (const auto& Pair : (*StructObj)->Values)
			{
				FProperty* FieldProp = Struct->FindPropertyByName(*Pair.Key);
				if (FieldProp)
				{
					void* FieldPtr = FieldProp->ContainerPtrToValuePtr<void>(ValuePtr);
					SetPropertyValueInternal(nullptr, FieldProp, FieldPtr, Pair.Value);
				}
			}
			return true;
		}
	}

	return false;
}

bool FPropertySetterUtils::SetArrayProperty(UObject* Object, FArrayProperty* Property, const TArray<TSharedPtr<FJsonValue>>& ArrayValues)
{
	if (!Object || !Property)
	{
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
	FScriptArrayHelper ArrayHelper(Property, ValuePtr);
	FProperty* InnerProp = Property->Inner;

	// Clear existing entries
	ArrayHelper.EmptyValues();

	for (int32 i = 0; i < ArrayValues.Num(); ++i)
	{
		const TSharedPtr<FJsonValue>& JsonValue = ArrayValues[i];

		int32 Index = ArrayHelper.AddValue();
		void* ElementPtr = ArrayHelper.GetRawPtr(Index);

		// Handle different inner property types
		if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(InnerProp))
		{
			FString AssetPath;
			if (JsonValue->TryGetString(AssetPath))
			{
				if (!AssetPath.Contains(TEXT(".")))
				{
					FString AssetName = FPaths::GetBaseFilename(AssetPath);
					AssetPath = AssetPath + TEXT(".") + AssetName;
				}
				FSoftObjectPath SoftPath(AssetPath);
				FSoftObjectPtr SoftPtr(SoftPath);
				SoftObjProp->SetPropertyValue(ElementPtr, SoftPtr);
			}
		}
		else if (FStrProperty* StrProp = CastField<FStrProperty>(InnerProp))
		{
			FString Value;
			if (JsonValue->TryGetString(Value))
			{
				StrProp->SetPropertyValue(ElementPtr, Value);
			}
		}
		else if (FNumericProperty* NumProp = CastField<FNumericProperty>(InnerProp))
		{
			double Value = 0;
			if (JsonValue->TryGetNumber(Value))
			{
				if (NumProp->IsFloatingPoint())
				{
					NumProp->SetFloatingPointPropertyValue(ElementPtr, Value);
				}
				else
				{
					NumProp->SetIntPropertyValue(ElementPtr, static_cast<int64>(Value));
				}
			}
		}
		else if (FStructProperty* StructProp = CastField<FStructProperty>(InnerProp))
		{
			SetStructPropertyFromJson(StructProp, ElementPtr, JsonValue);
		}
		else if (FNameProperty* NameProp = CastField<FNameProperty>(InnerProp))
		{
			FString Value;
			if (JsonValue->TryGetString(Value))
			{
				NameProp->SetPropertyValue(ElementPtr, FName(*Value));
			}
		}
	}

	UE_LOG(LogAssetFactory, Verbose, TEXT("Set TArray: %s with %d elements"), *Property->GetName(), ArrayHelper.Num());
	return true;
}

bool FPropertySetterUtils::SetMapProperty(UObject* Object, FMapProperty* Property, TSharedPtr<FJsonObject> MapConfig)
{
	if (!Object || !Property || !MapConfig.IsValid())
	{
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
	FScriptMapHelper MapHelper(Property, ValuePtr);

	FProperty* KeyProp = Property->KeyProp;
	FProperty* ValueProp = Property->ValueProp;

	// Clear existing entries
	MapHelper.EmptyValues();

	for (const auto& Pair : MapConfig->Values)
	{
		const FString& KeyStr = Pair.Key;
		const TSharedPtr<FJsonValue>& JsonValue = Pair.Value;

		// Add a new entry
		int32 Index = MapHelper.AddDefaultValue_Invalid_NeedsRehash();

		// Set the key
		void* KeyPtr = MapHelper.GetKeyPtr(Index);

		// Handle enum keys
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(KeyProp))
		{
			UEnum* Enum = EnumProp->GetEnum();
			int64 EnumValue = Enum->GetValueByNameString(KeyStr);
			if (EnumValue == INDEX_NONE)
			{
				FString FullName = Enum->GetName() + TEXT("::") + KeyStr;
				EnumValue = Enum->GetValueByNameString(FullName);
			}
			if (EnumValue != INDEX_NONE)
			{
				EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(KeyPtr, EnumValue);
			}
			else
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("Unknown enum value '%s' for enum '%s'"), *KeyStr, *Enum->GetName());
				MapHelper.RemoveAt(Index);
				continue;
			}
		}
		else if (FByteProperty* ByteProp = CastField<FByteProperty>(KeyProp))
		{
			if (UEnum* Enum = ByteProp->Enum)
			{
				int64 EnumValue = Enum->GetValueByNameString(KeyStr);
				if (EnumValue != INDEX_NONE)
				{
					ByteProp->SetIntPropertyValue(KeyPtr, EnumValue);
				}
				else
				{
					MapHelper.RemoveAt(Index);
					continue;
				}
			}
			else
			{
				ByteProp->SetIntPropertyValue(KeyPtr, static_cast<int64>(FCString::Atoi(*KeyStr)));
			}
		}
		else if (FStrProperty* StrProp = CastField<FStrProperty>(KeyProp))
		{
			StrProp->SetPropertyValue(KeyPtr, KeyStr);
		}
		else if (FNameProperty* NameProp = CastField<FNameProperty>(KeyProp))
		{
			NameProp->SetPropertyValue(KeyPtr, FName(*KeyStr));
		}
		else
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Unsupported map key type: %s"), *KeyProp->GetClass()->GetName());
			MapHelper.RemoveAt(Index);
			continue;
		}

		// Set the value
		void* ValPtr = MapHelper.GetValuePtr(Index);

		// Handle TSoftObjectPtr values
		if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(ValueProp))
		{
			FString AssetPath;
			if (JsonValue->TryGetString(AssetPath))
			{
				if (!AssetPath.Contains(TEXT(".")))
				{
					FString AssetName = FPaths::GetBaseFilename(AssetPath);
					AssetPath = AssetPath + TEXT(".") + AssetName;
				}
				FSoftObjectPath SoftPath(AssetPath);
				FSoftObjectPtr SoftPtr(SoftPath);
				SoftObjProp->SetPropertyValue(ValPtr, SoftPtr);
			}
		}
		// Handle FLinearColor values
		else if (FStructProperty* StructProp = CastField<FStructProperty>(ValueProp))
		{
			SetStructPropertyFromJson(StructProp, ValPtr, JsonValue);
		}
		// Handle string values
		else if (FStrProperty* StrValProp = CastField<FStrProperty>(ValueProp))
		{
			FString ValueStr;
			if (JsonValue->TryGetString(ValueStr))
			{
				StrValProp->SetPropertyValue(ValPtr, ValueStr);
			}
		}
		// Handle numeric values
		else if (FNumericProperty* NumProp = CastField<FNumericProperty>(ValueProp))
		{
			double ValueNum = 0;
			if (JsonValue->TryGetNumber(ValueNum))
			{
				if (NumProp->IsFloatingPoint())
				{
					NumProp->SetFloatingPointPropertyValue(ValPtr, ValueNum);
				}
				else
				{
					NumProp->SetIntPropertyValue(ValPtr, static_cast<int64>(ValueNum));
				}
			}
		}
	}

	MapHelper.Rehash();

	UE_LOG(LogAssetFactory, Verbose, TEXT("Set TMap: %s with %d entries"), *Property->GetName(), MapHelper.Num());
	return true;
}

bool FPropertySetterUtils::SetObjectReferenceFromPath(FObjectPropertyBase* ObjProp, void* ValuePtr, const FString& ObjectPath)
{
	if (!ObjProp || !ValuePtr)
	{
		return false;
	}

	if (ObjectPath.IsEmpty())
	{
		ObjProp->SetObjectPropertyValue(ValuePtr, nullptr);
		return true;
	}

	UObject* LoadedObject = StaticLoadObject(ObjProp->PropertyClass, nullptr, *ObjectPath);
	if (LoadedObject)
	{
		ObjProp->SetObjectPropertyValue(ValuePtr, LoadedObject);
		return true;
	}

	UE_LOG(LogAssetFactory, Warning, TEXT("Failed to load object: %s"), *ObjectPath);
	return false;
}

bool FPropertySetterUtils::SetSoftObjectProperty(UObject* Object, FSoftObjectProperty* Property, const FString& AssetPath)
{
	if (!Object || !Property || AssetPath.IsEmpty())
	{
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);

	// Normalize the asset path
	FString NormalizedPath = AssetPath;
	if (!NormalizedPath.Contains(TEXT(".")))
	{
		// Add asset name if not present (e.g., "/Game/UI/Mat" -> "/Game/UI/Mat.Mat")
		FString AssetName = FPaths::GetBaseFilename(NormalizedPath);
		NormalizedPath = NormalizedPath + TEXT(".") + AssetName;
	}

	FSoftObjectPath SoftPath(NormalizedPath);
	FSoftObjectPtr SoftPtr(SoftPath);
	Property->SetPropertyValue(ValuePtr, SoftPtr);

	UE_LOG(LogAssetFactory, Verbose, TEXT("Set TSoftObjectPtr: %s = %s"), *Property->GetName(), *NormalizedPath);
	return true;
}

bool FPropertySetterUtils::SetClassProperty(UObject* Object, FClassProperty* Property, const FString& ClassPath)
{
	if (!Object || !Property || ClassPath.IsEmpty())
	{
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);

	// Try to load the class
	FString FullClassPath = ClassPath;

	// If it's a Blueprint path, try to load the generated class
	if (!FullClassPath.EndsWith(TEXT("_C")))
	{
		FullClassPath += TEXT("_C");
	}

	UClass* LoadedClass = LoadClass<UObject>(nullptr, *FullClassPath);
	if (!LoadedClass)
	{
		// Try without _C suffix (native classes)
		LoadedClass = LoadClass<UObject>(nullptr, *ClassPath);
	}

	if (LoadedClass)
	{
		// Verify the class is compatible with the property's meta class
		UClass* MetaClass = Property->MetaClass;
		if (MetaClass && !LoadedClass->IsChildOf(MetaClass))
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Class '%s' is not a subclass of '%s'"),
				*LoadedClass->GetName(), *MetaClass->GetName());
			return false;
		}

		Property->SetPropertyValue(ValuePtr, LoadedClass);
		UE_LOG(LogAssetFactory, Verbose, TEXT("Set TSubclassOf: %s = %s"), *Property->GetName(), *LoadedClass->GetName());
		return true;
	}

	UE_LOG(LogAssetFactory, Warning, TEXT("Failed to load class: %s"), *ClassPath);
	return false;
}

//~ Parse Helpers

FLinearColor FPropertySetterUtils::ParseColor(TSharedPtr<FJsonValue> ColorValue)
{
	FLinearColor Result = FLinearColor::White;

	if (!ColorValue.IsValid())
	{
		return Result;
	}

	// String format - hex or named
	FString ColorStr;
	if (ColorValue->TryGetString(ColorStr))
	{
		// Hex format
		if (ColorStr.StartsWith(TEXT("#")))
		{
			FColor ParsedColor = FColor::FromHex(ColorStr);
			return FLinearColor(ParsedColor);
		}

		// Named colors
		if (ColorStr == TEXT("White")) return FLinearColor::White;
		if (ColorStr == TEXT("Black")) return FLinearColor::Black;
		if (ColorStr == TEXT("Red")) return FLinearColor::Red;
		if (ColorStr == TEXT("Green")) return FLinearColor::Green;
		if (ColorStr == TEXT("Blue")) return FLinearColor::Blue;
		if (ColorStr == TEXT("Yellow")) return FLinearColor::Yellow;
		if (ColorStr == TEXT("Transparent")) return FLinearColor::Transparent;
	}

	// Array format [R, G, B, A]
	const TArray<TSharedPtr<FJsonValue>>* ColorArray = nullptr;
	if (ColorValue->TryGetArray(ColorArray))
	{
		if (ColorArray->Num() >= 3)
		{
			double R = 1, G = 1, B = 1, A = 1;
			(*ColorArray)[0]->TryGetNumber(R);
			(*ColorArray)[1]->TryGetNumber(G);
			(*ColorArray)[2]->TryGetNumber(B);
			if (ColorArray->Num() >= 4)
			{
				(*ColorArray)[3]->TryGetNumber(A);
			}
			return FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), static_cast<float>(A));
		}
	}

	// Object format {R, G, B, A}
	const TSharedPtr<FJsonObject>* ColorObject = nullptr;
	if (ColorValue->TryGetObject(ColorObject))
	{
		double R = 1, G = 1, B = 1, A = 1;
		(*ColorObject)->TryGetNumberField(TEXT("R"), R);
		(*ColorObject)->TryGetNumberField(TEXT("G"), G);
		(*ColorObject)->TryGetNumberField(TEXT("B"), B);
		(*ColorObject)->TryGetNumberField(TEXT("A"), A);
		return FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), static_cast<float>(A));
	}

	return Result;
}

FVector2D FPropertySetterUtils::ParseVector2D(const TArray<TSharedPtr<FJsonValue>>& Array)
{
	FVector2D Result(0.0f, 0.0f);

	if (Array.Num() >= 2)
	{
		double X = 0, Y = 0;
		Array[0]->TryGetNumber(X);
		Array[1]->TryGetNumber(Y);
		Result = FVector2D(static_cast<float>(X), static_cast<float>(Y));
	}

	return Result;
}

FVector FPropertySetterUtils::ParseVector(const TArray<TSharedPtr<FJsonValue>>& Array)
{
	FVector Result(0.0f, 0.0f, 0.0f);

	if (Array.Num() >= 3)
	{
		double X = 0, Y = 0, Z = 0;
		Array[0]->TryGetNumber(X);
		Array[1]->TryGetNumber(Y);
		Array[2]->TryGetNumber(Z);
		Result = FVector(X, Y, Z);
	}

	return Result;
}

FMargin FPropertySetterUtils::ParseMargin(TSharedPtr<FJsonValue> MarginsValue)
{
	FMargin Result(0.0f);

	if (!MarginsValue.IsValid())
	{
		return Result;
	}

	// Single number - uniform margin
	double UniformValue = 0;
	if (MarginsValue->TryGetNumber(UniformValue))
	{
		return FMargin(static_cast<float>(UniformValue));
	}

	// Array format [Left, Top, Right, Bottom] or [H, V]
	const TArray<TSharedPtr<FJsonValue>>* MarginsArray = nullptr;
	if (MarginsValue->TryGetArray(MarginsArray))
	{
		if (MarginsArray->Num() >= 4)
		{
			double Left = 0, Top = 0, Right = 0, Bottom = 0;
			(*MarginsArray)[0]->TryGetNumber(Left);
			(*MarginsArray)[1]->TryGetNumber(Top);
			(*MarginsArray)[2]->TryGetNumber(Right);
			(*MarginsArray)[3]->TryGetNumber(Bottom);
			return FMargin(static_cast<float>(Left), static_cast<float>(Top), static_cast<float>(Right), static_cast<float>(Bottom));
		}
		else if (MarginsArray->Num() >= 2)
		{
			double H = 0, V = 0;
			(*MarginsArray)[0]->TryGetNumber(H);
			(*MarginsArray)[1]->TryGetNumber(V);
			return FMargin(static_cast<float>(H), static_cast<float>(V));
		}
	}

	// Object format {Left, Top, Right, Bottom}
	const TSharedPtr<FJsonObject>* MarginsObject = nullptr;
	if (MarginsValue->TryGetObject(MarginsObject))
	{
		double Left = 0, Top = 0, Right = 0, Bottom = 0;
		(*MarginsObject)->TryGetNumberField(TEXT("Left"), Left);
		(*MarginsObject)->TryGetNumberField(TEXT("Top"), Top);
		(*MarginsObject)->TryGetNumberField(TEXT("Right"), Right);
		(*MarginsObject)->TryGetNumberField(TEXT("Bottom"), Bottom);
		return FMargin(static_cast<float>(Left), static_cast<float>(Top), static_cast<float>(Right), static_cast<float>(Bottom));
	}

	return Result;
}

FSlateFontInfo FPropertySetterUtils::ParseFont(TSharedPtr<FJsonObject> FontConfig)
{
	FSlateFontInfo Font;

	if (FontConfig.IsValid())
	{
		double Size = 12;
		if (FontConfig->TryGetNumberField(TEXT("Size"), Size))
		{
			Font.Size = static_cast<int32>(Size);
		}

		// Font family handling would need font asset loading
		// For now, just use the default font with specified size
	}

	return Font;
}

FSlateBrush FPropertySetterUtils::ParseBrush(TSharedPtr<FJsonObject> BrushConfig)
{
	FSlateBrush Brush;

	if (!BrushConfig.IsValid())
	{
		return Brush;
	}

	// Load image texture
	FString ImagePath;
	if (BrushConfig->TryGetStringField(TEXT("Image"), ImagePath))
	{
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ImagePath);
		if (Texture)
		{
			Brush.SetResourceObject(Texture);
		}
		else
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Failed to load texture: %s"), *ImagePath);
		}
	}

	// Tint color
	if (BrushConfig->HasField(TEXT("Tint")))
	{
		FLinearColor Tint = ParseColor(BrushConfig->TryGetField(TEXT("Tint")));
		Brush.TintColor = FSlateColor(Tint);
	}

	// Draw type
	FString DrawAs;
	if (BrushConfig->TryGetStringField(TEXT("DrawAs"), DrawAs))
	{
		if (DrawAs == TEXT("Box")) Brush.DrawAs = ESlateBrushDrawType::Box;
		else if (DrawAs == TEXT("Image")) Brush.DrawAs = ESlateBrushDrawType::Image;
		else if (DrawAs == TEXT("Border")) Brush.DrawAs = ESlateBrushDrawType::Border;
		else if (DrawAs == TEXT("NoDrawType")) Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
	}

	return Brush;
}

FAnchors FPropertySetterUtils::ParseAnchors(TSharedPtr<FJsonObject> AnchorsConfig)
{
	FAnchors Result;

	if (!AnchorsConfig.IsValid())
	{
		return Result;
	}

	const TArray<TSharedPtr<FJsonValue>>* MinArray = nullptr;
	if (AnchorsConfig->TryGetArrayField(TEXT("Min"), MinArray) && MinArray->Num() >= 2)
	{
		Result.Minimum = ParseVector2D(*MinArray);
	}

	const TArray<TSharedPtr<FJsonValue>>* MaxArray = nullptr;
	if (AnchorsConfig->TryGetArrayField(TEXT("Max"), MaxArray) && MaxArray->Num() >= 2)
	{
		Result.Maximum = ParseVector2D(*MaxArray);
	}

	return Result;
}

//////////////////////////////////////////////////////////////////////////
// Extract Properties (Property → JSON)
//////////////////////////////////////////////////////////////////////////

TSharedPtr<FJsonValue> FPropertySetterUtils::ExtractPropertyToJson(FProperty* Property, const void* ValuePtr)
{
	if (!Property || !ValuePtr)
	{
		return nullptr;
	}

	// Numeric properties
	if (FNumericProperty* NumProp = CastField<FNumericProperty>(Property))
	{
		if (NumProp->IsFloatingPoint())
		{
			return MakeShared<FJsonValueNumber>(NumProp->GetFloatingPointPropertyValue(ValuePtr));
		}
		else
		{
			return MakeShared<FJsonValueNumber>(static_cast<double>(NumProp->GetSignedIntPropertyValue(ValuePtr)));
		}
	}

	// Bool
	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		return MakeShared<FJsonValueBoolean>(BoolProp->GetPropertyValue(ValuePtr));
	}

	// String
	if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		return MakeShared<FJsonValueString>(StrProp->GetPropertyValue(ValuePtr));
	}

	// Name
	if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		return MakeShared<FJsonValueString>(NameProp->GetPropertyValue(ValuePtr).ToString());
	}

	// Text
	if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		return MakeShared<FJsonValueString>(TextProp->GetPropertyValue(ValuePtr).ToString());
	}

	// Enum
	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		UEnum* Enum = EnumProp->GetEnum();
		FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
		int64 EnumValue = UnderlyingProp->GetSignedIntPropertyValue(ValuePtr);
		FString EnumName = Enum->GetNameStringByValue(EnumValue);
		return MakeShared<FJsonValueString>(EnumName);
	}

	// Byte (may be enum)
	if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (UEnum* Enum = ByteProp->Enum)
		{
			uint8 ByteValue = ByteProp->GetPropertyValue(ValuePtr);
			FString EnumName = Enum->GetNameStringByValue(ByteValue);
			return MakeShared<FJsonValueString>(EnumName);
		}
		else
		{
			return MakeShared<FJsonValueNumber>(ByteProp->GetPropertyValue(ValuePtr));
		}
	}

	// Object reference
	if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
	{
		UObject* ObjValue = ObjProp->GetObjectPropertyValue(ValuePtr);
		if (ObjValue)
		{
			return MakeShared<FJsonValueString>(ObjValue->GetPathName());
		}
		return MakeShared<FJsonValueNull>();
	}

	// Soft object reference
	if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
	{
		const FSoftObjectPtr* SoftPtr = static_cast<const FSoftObjectPtr*>(ValuePtr);
		if (!SoftPtr->IsNull())
		{
			return MakeShared<FJsonValueString>(SoftPtr->ToSoftObjectPath().ToString());
		}
		return MakeShared<FJsonValueNull>();
	}

	// Class reference
	if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
	{
		UClass* ClassValue = Cast<UClass>(ClassProp->GetObjectPropertyValue(ValuePtr));
		if (ClassValue)
		{
			return MakeShared<FJsonValueString>(ClassValue->GetPathName());
		}
		return MakeShared<FJsonValueNull>();
	}

	// Soft class reference
	if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Property))
	{
		const FSoftObjectPtr* SoftPtr = static_cast<const FSoftObjectPtr*>(ValuePtr);
		if (!SoftPtr->IsNull())
		{
			return MakeShared<FJsonValueString>(SoftPtr->ToSoftObjectPath().ToString());
		}
		return MakeShared<FJsonValueNull>();
	}

	// Struct
	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		return ExtractStructToJson(StructProp, ValuePtr);
	}

	// Array
	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		return ExtractArrayToJson(ArrayProp, ValuePtr);
	}

	// Map
	if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		return ExtractMapToJson(MapProp, ValuePtr);
	}

	// Set - treat as array
	if (FSetProperty* SetProp = CastField<FSetProperty>(Property))
	{
		FScriptSetHelper SetHelper(SetProp, ValuePtr);
		TArray<TSharedPtr<FJsonValue>> JsonArray;

		for (int32 i = 0; i < SetHelper.Num(); ++i)
		{
			if (SetHelper.IsValidIndex(i))
			{
				const void* ElemPtr = SetHelper.GetElementPtr(i);
				TSharedPtr<FJsonValue> ElemJson = ExtractPropertyToJson(SetProp->ElementProp, ElemPtr);
				if (ElemJson.IsValid())
				{
					JsonArray.Add(ElemJson);
				}
			}
		}

		return MakeShared<FJsonValueArray>(JsonArray);
	}

	UE_LOG(LogAssetFactory, Verbose, TEXT("Unsupported property type for extraction: %s (%s)"),
		*Property->GetName(), *Property->GetClass()->GetName());
	return nullptr;
}

TSharedPtr<FJsonObject> FPropertySetterUtils::ExtractPropertiesToJson(UObject* Object, bool bEditableOnly, bool bSkipDefaults)
{
	if (!Object)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> PropertiesJson = MakeShared<FJsonObject>();
	UClass* ObjectClass = Object->GetClass();
	UObject* CDO = bSkipDefaults ? ObjectClass->GetDefaultObject() : nullptr;

	for (TFieldIterator<FProperty> PropIt(ObjectClass); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		// Skip non-editable if requested
		if (bEditableOnly && !Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		// Skip deprecated properties
		if (Property->HasAnyPropertyFlags(CPF_Deprecated))
		{
			continue;
		}

		FString PropertyName = Property->GetName();
		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);

		// Skip if same as default
		if (bSkipDefaults && CDO)
		{
			const void* DefaultPtr = Property->ContainerPtrToValuePtr<void>(CDO);
			if (Property->Identical(ValuePtr, DefaultPtr))
			{
				continue;
			}
		}

		TSharedPtr<FJsonValue> JsonValue = ExtractPropertyToJson(Property, ValuePtr);
		if (JsonValue.IsValid())
		{
			PropertiesJson->SetField(PropertyName, JsonValue);
		}
	}

	return PropertiesJson;
}

TSharedPtr<FJsonValue> FPropertySetterUtils::ExtractStructToJson(FStructProperty* StructProp, const void* ValuePtr)
{
	if (!StructProp || !ValuePtr)
	{
		return nullptr;
	}

	UScriptStruct* Struct = StructProp->Struct;

	// Handle common struct types with array format for compactness
	if (Struct == TBaseStructure<FVector>::Get())
	{
		const FVector* Vec = static_cast<const FVector*>(ValuePtr);
		TArray<TSharedPtr<FJsonValue>> VecArray;
		VecArray.Add(MakeShared<FJsonValueNumber>(Vec->X));
		VecArray.Add(MakeShared<FJsonValueNumber>(Vec->Y));
		VecArray.Add(MakeShared<FJsonValueNumber>(Vec->Z));
		return MakeShared<FJsonValueArray>(VecArray);
	}

	if (Struct == TBaseStructure<FVector2D>::Get())
	{
		const FVector2D* Vec = static_cast<const FVector2D*>(ValuePtr);
		TArray<TSharedPtr<FJsonValue>> VecArray;
		VecArray.Add(MakeShared<FJsonValueNumber>(Vec->X));
		VecArray.Add(MakeShared<FJsonValueNumber>(Vec->Y));
		return MakeShared<FJsonValueArray>(VecArray);
	}

	if (Struct == TBaseStructure<FRotator>::Get())
	{
		const FRotator* Rot = static_cast<const FRotator*>(ValuePtr);
		TArray<TSharedPtr<FJsonValue>> RotArray;
		RotArray.Add(MakeShared<FJsonValueNumber>(Rot->Pitch));
		RotArray.Add(MakeShared<FJsonValueNumber>(Rot->Yaw));
		RotArray.Add(MakeShared<FJsonValueNumber>(Rot->Roll));
		return MakeShared<FJsonValueArray>(RotArray);
	}

	if (Struct == TBaseStructure<FLinearColor>::Get())
	{
		const FLinearColor* Color = static_cast<const FLinearColor*>(ValuePtr);
		TArray<TSharedPtr<FJsonValue>> ColorArray;
		ColorArray.Add(MakeShared<FJsonValueNumber>(Color->R));
		ColorArray.Add(MakeShared<FJsonValueNumber>(Color->G));
		ColorArray.Add(MakeShared<FJsonValueNumber>(Color->B));
		ColorArray.Add(MakeShared<FJsonValueNumber>(Color->A));
		return MakeShared<FJsonValueArray>(ColorArray);
	}

	if (Struct == TBaseStructure<FColor>::Get())
	{
		const FColor* Color = static_cast<const FColor*>(ValuePtr);
		TArray<TSharedPtr<FJsonValue>> ColorArray;
		ColorArray.Add(MakeShared<FJsonValueNumber>(Color->R));
		ColorArray.Add(MakeShared<FJsonValueNumber>(Color->G));
		ColorArray.Add(MakeShared<FJsonValueNumber>(Color->B));
		ColorArray.Add(MakeShared<FJsonValueNumber>(Color->A));
		return MakeShared<FJsonValueArray>(ColorArray);
	}

	if (Struct == TBaseStructure<FTransform>::Get())
	{
		const FTransform* Trans = static_cast<const FTransform*>(ValuePtr);
		TSharedPtr<FJsonObject> TransObj = MakeShared<FJsonObject>();

		// Location
		TArray<TSharedPtr<FJsonValue>> LocArray;
		LocArray.Add(MakeShared<FJsonValueNumber>(Trans->GetLocation().X));
		LocArray.Add(MakeShared<FJsonValueNumber>(Trans->GetLocation().Y));
		LocArray.Add(MakeShared<FJsonValueNumber>(Trans->GetLocation().Z));
		TransObj->SetArrayField(TEXT("Location"), LocArray);

		// Rotation
		FRotator Rot = Trans->Rotator();
		TArray<TSharedPtr<FJsonValue>> RotArray;
		RotArray.Add(MakeShared<FJsonValueNumber>(Rot.Pitch));
		RotArray.Add(MakeShared<FJsonValueNumber>(Rot.Yaw));
		RotArray.Add(MakeShared<FJsonValueNumber>(Rot.Roll));
		TransObj->SetArrayField(TEXT("Rotation"), RotArray);

		// Scale
		TArray<TSharedPtr<FJsonValue>> ScaleArray;
		ScaleArray.Add(MakeShared<FJsonValueNumber>(Trans->GetScale3D().X));
		ScaleArray.Add(MakeShared<FJsonValueNumber>(Trans->GetScale3D().Y));
		ScaleArray.Add(MakeShared<FJsonValueNumber>(Trans->GetScale3D().Z));
		TransObj->SetArrayField(TEXT("Scale"), ScaleArray);

		return MakeShared<FJsonValueObject>(TransObj);
	}

	// FMargin
	if (Struct->GetFName() == TEXT("Margin"))
	{
		const FMargin* Margin = static_cast<const FMargin*>(ValuePtr);
		TArray<TSharedPtr<FJsonValue>> MarginArray;
		MarginArray.Add(MakeShared<FJsonValueNumber>(Margin->Left));
		MarginArray.Add(MakeShared<FJsonValueNumber>(Margin->Top));
		MarginArray.Add(MakeShared<FJsonValueNumber>(Margin->Right));
		MarginArray.Add(MakeShared<FJsonValueNumber>(Margin->Bottom));
		return MakeShared<FJsonValueArray>(MarginArray);
	}

	// FAnchors
	if (Struct->GetFName() == TEXT("Anchors"))
	{
		const FAnchors* Anchors = static_cast<const FAnchors*>(ValuePtr);
		TSharedPtr<FJsonObject> AnchorsObj = MakeShared<FJsonObject>();

		TArray<TSharedPtr<FJsonValue>> MinArray;
		MinArray.Add(MakeShared<FJsonValueNumber>(Anchors->Minimum.X));
		MinArray.Add(MakeShared<FJsonValueNumber>(Anchors->Minimum.Y));
		AnchorsObj->SetArrayField(TEXT("Min"), MinArray);

		TArray<TSharedPtr<FJsonValue>> MaxArray;
		MaxArray.Add(MakeShared<FJsonValueNumber>(Anchors->Maximum.X));
		MaxArray.Add(MakeShared<FJsonValueNumber>(Anchors->Maximum.Y));
		AnchorsObj->SetArrayField(TEXT("Max"), MaxArray);

		return MakeShared<FJsonValueObject>(AnchorsObj);
	}

	// FSoftObjectPath
	if (Struct == TBaseStructure<FSoftObjectPath>::Get())
	{
		const FSoftObjectPath* SoftPath = static_cast<const FSoftObjectPath*>(ValuePtr);
		return MakeShared<FJsonValueString>(SoftPath->ToString());
	}

	// Generic struct - recursively extract fields
	TSharedPtr<FJsonObject> StructObj = MakeShared<FJsonObject>();

	for (TFieldIterator<FProperty> PropIt(Struct); PropIt; ++PropIt)
	{
		FProperty* FieldProp = *PropIt;
		const void* FieldPtr = FieldProp->ContainerPtrToValuePtr<void>(ValuePtr);

		TSharedPtr<FJsonValue> FieldJson = ExtractPropertyToJson(FieldProp, FieldPtr);
		if (FieldJson.IsValid())
		{
			StructObj->SetField(FieldProp->GetName(), FieldJson);
		}
	}

	return MakeShared<FJsonValueObject>(StructObj);
}

TSharedPtr<FJsonValue> FPropertySetterUtils::ExtractArrayToJson(FArrayProperty* ArrayProp, const void* ValuePtr)
{
	if (!ArrayProp || !ValuePtr)
	{
		return nullptr;
	}

	FScriptArrayHelper ArrayHelper(ArrayProp, ValuePtr);
	TArray<TSharedPtr<FJsonValue>> JsonArray;

	FProperty* InnerProp = ArrayProp->Inner;

	for (int32 i = 0; i < ArrayHelper.Num(); ++i)
	{
		const void* ElemPtr = ArrayHelper.GetRawPtr(i);
		TSharedPtr<FJsonValue> ElemJson = ExtractPropertyToJson(InnerProp, ElemPtr);
		if (ElemJson.IsValid())
		{
			JsonArray.Add(ElemJson);
		}
	}

	return MakeShared<FJsonValueArray>(JsonArray);
}

TSharedPtr<FJsonValue> FPropertySetterUtils::ExtractMapToJson(FMapProperty* MapProp, const void* ValuePtr)
{
	if (!MapProp || !ValuePtr)
	{
		return nullptr;
	}

	FScriptMapHelper MapHelper(MapProp, ValuePtr);
	TSharedPtr<FJsonObject> MapObj = MakeShared<FJsonObject>();

	for (int32 i = 0; i < MapHelper.Num(); ++i)
	{
		if (MapHelper.IsValidIndex(i))
		{
			const void* KeyPtr = MapHelper.GetKeyPtr(i);
			const void* ValPtr = MapHelper.GetValuePtr(i);

			// Get key as string
			FString KeyStr;
			FProperty* KeyProp = MapProp->KeyProp;

			if (FStrProperty* StrProp = CastField<FStrProperty>(KeyProp))
			{
				KeyStr = StrProp->GetPropertyValue(KeyPtr);
			}
			else if (FNameProperty* NameProp = CastField<FNameProperty>(KeyProp))
			{
				KeyStr = NameProp->GetPropertyValue(KeyPtr).ToString();
			}
			else if (FNumericProperty* NumProp = CastField<FNumericProperty>(KeyProp))
			{
				if (NumProp->IsFloatingPoint())
				{
					KeyStr = FString::SanitizeFloat(NumProp->GetFloatingPointPropertyValue(KeyPtr));
				}
				else
				{
					KeyStr = FString::FromInt(static_cast<int32>(NumProp->GetSignedIntPropertyValue(KeyPtr)));
				}
			}
			else
			{
				// Fallback: use index
				KeyStr = FString::FromInt(i);
			}

			TSharedPtr<FJsonValue> ValJson = ExtractPropertyToJson(MapProp->ValueProp, ValPtr);
			if (ValJson.IsValid())
			{
				MapObj->SetField(KeyStr, ValJson);
			}
		}
	}

	return MakeShared<FJsonValueObject>(MapObj);
}
