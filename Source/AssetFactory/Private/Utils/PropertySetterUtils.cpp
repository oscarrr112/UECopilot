// Copyright ProjectRPG. All Rights Reserved.

#include "Utils/PropertySetterUtils.h"
#include "AssetFactoryModule.h"

#include "UObject/EnumProperty.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
#include "Engine/Texture2D.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Margin.h"
#include "Widgets/Layout/Anchors.h"

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
