// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "UObject/UnrealType.h"
#include "Layout/Margin.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Layout/Anchors.h"

/**
 * Utility class for setting UObject properties from JSON values.
 * Consolidates property setting logic from DataAssetGenerator, BlueprintGenerator,
 * and WidgetBlueprintGenerator.
 *
 * Supported property types:
 * - Numeric (int8-64, uint8-64, float, double)
 * - Bool
 * - String / FName / FText
 * - Enum / ByteEnum
 * - Struct (FLinearColor, FColor, FVector, FVector2D, FMargin, FSlateColor, FSlateFontInfo, FSlateBrush, FAnchors + generic)
 * - Object Reference / SoftObject / Class
 * - Array / Map
 */
class ASSETFACTORY_API FPropertySetterUtils
{
public:
	/**
	 * Set a single property from a JSON value
	 * @param Object - The UObject containing the property
	 * @param Property - The property to set
	 * @param JsonValue - The JSON value to use
	 * @return true if the property was set successfully
	 */
	static bool SetPropertyFromJson(UObject* Object, FProperty* Property, TSharedPtr<FJsonValue> JsonValue);

	/**
	 * Set multiple properties from a JSON object
	 * @param Object - The UObject to set properties on
	 * @param Properties - JSON object containing property name/value pairs
	 */
	static void SetPropertiesFromJson(UObject* Object, TSharedPtr<FJsonObject> Properties);

	/**
	 * Set a struct property from a JSON value
	 * @param StructProp - The struct property
	 * @param ValuePtr - Pointer to the struct value
	 * @param JsonValue - The JSON value to use
	 * @return true if the property was set successfully
	 */
	static bool SetStructPropertyFromJson(FStructProperty* StructProp, void* ValuePtr, TSharedPtr<FJsonValue> JsonValue);

	/**
	 * Set an array property from JSON array values
	 * @param Object - The UObject containing the property
	 * @param ArrayProp - The array property
	 * @param ArrayValues - Array of JSON values
	 * @return true if the property was set successfully
	 */
	static bool SetArrayProperty(UObject* Object, FArrayProperty* ArrayProp, const TArray<TSharedPtr<FJsonValue>>& ArrayValues);

	/**
	 * Set a map property from a JSON object
	 * @param Object - The UObject containing the property
	 * @param MapProp - The map property
	 * @param MapConfig - JSON object representing the map
	 * @return true if the property was set successfully
	 */
	static bool SetMapProperty(UObject* Object, FMapProperty* MapProp, TSharedPtr<FJsonObject> MapConfig);

	/**
	 * Set an object reference property from an asset path
	 * @param ObjProp - The object property
	 * @param ValuePtr - Pointer to the property value
	 * @param ObjectPath - Asset path string
	 * @return true if the property was set successfully
	 */
	static bool SetObjectReferenceFromPath(FObjectPropertyBase* ObjProp, void* ValuePtr, const FString& ObjectPath);

	/**
	 * Set a soft object property from an asset path
	 * @param Object - The UObject containing the property
	 * @param Property - The soft object property
	 * @param AssetPath - Asset path string
	 * @return true if the property was set successfully
	 */
	static bool SetSoftObjectProperty(UObject* Object, FSoftObjectProperty* Property, const FString& AssetPath);

	/**
	 * Set a class property from a class path
	 * @param Object - The UObject containing the property
	 * @param Property - The class property
	 * @param ClassPath - Class path string
	 * @return true if the property was set successfully
	 */
	static bool SetClassProperty(UObject* Object, FClassProperty* Property, const FString& ClassPath);

	//~ Parse Helpers

	/**
	 * Parse a color from a JSON value
	 * Supports: string (hex "#RRGGBBAA" or named), array [R,G,B,A], object {R,G,B,A}
	 */
	static FLinearColor ParseColor(TSharedPtr<FJsonValue> Value);

	/**
	 * Parse a 2D vector from a JSON array [X, Y]
	 */
	static FVector2D ParseVector2D(const TArray<TSharedPtr<FJsonValue>>& Array);

	/**
	 * Parse a 3D vector from a JSON array [X, Y, Z]
	 */
	static FVector ParseVector(const TArray<TSharedPtr<FJsonValue>>& Array);

	/**
	 * Parse margins from a JSON value
	 * Supports: number (uniform), array [L,T,R,B] or [H,V], object {Left,Top,Right,Bottom}
	 */
	static FMargin ParseMargin(TSharedPtr<FJsonValue> Value);

	/**
	 * Parse a font info from a JSON object
	 */
	static FSlateFontInfo ParseFont(TSharedPtr<FJsonObject> FontConfig);

	/**
	 * Parse a slate brush from a JSON object
	 */
	static FSlateBrush ParseBrush(TSharedPtr<FJsonObject> BrushConfig);

	/**
	 * Parse anchors from a JSON object
	 */
	static FAnchors ParseAnchors(TSharedPtr<FJsonObject> AnchorsConfig);

private:
	/**
	 * Internal property value setter with void* pointer
	 */
	static bool SetPropertyValueInternal(UObject* Object, FProperty* Property, void* ValuePtr, TSharedPtr<FJsonValue> JsonValue);
};
