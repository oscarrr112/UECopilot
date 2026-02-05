// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * Utility class for resolving property paths in JSON objects
 *
 * Supports path syntax:
 * - "PropertyName" - simple property access
 * - "Parent.Child" - nested property access
 * - "Array[0]" - array index access
 * - "Parent.Array[0].Property" - combined access
 * - "Array[*]" - all array elements (returns array)
 *
 * Examples:
 * - "RootWidget.Children[0].Properties.Text"
 * - "Components[*].Name"
 * - "Bindings.HealthBar.Percent"
 */
class ASSETFACTORY_API FPropertyPathResolver
{
public:
	/**
	 * Resolve a path in a JSON object and return the value
	 * @param Root - The root JSON object to search in
	 * @param Path - The property path (e.g., "RootWidget.Children[0].Name")
	 * @return The resolved value, or nullptr if path is invalid
	 */
	static TSharedPtr<FJsonValue> Resolve(TSharedPtr<FJsonObject> Root, const FString& Path);

	/**
	 * Resolve multiple paths and return results as a JSON object
	 * @param Root - The root JSON object to search in
	 * @param Paths - Array of property paths to resolve
	 * @return JSON object with path->value mappings
	 */
	static TSharedPtr<FJsonObject> ResolveMultiple(TSharedPtr<FJsonObject> Root, const TArray<FString>& Paths);

private:
	/** Parse path into segments */
	static TArray<FString> ParsePath(const FString& Path);

	/** Resolve a single segment (may be property name or array index) */
	static TSharedPtr<FJsonValue> ResolveSegment(TSharedPtr<FJsonValue> Current, const FString& Segment);

	/** Check if segment is an array access (e.g., "[0]" or "Array[0]") */
	static bool ParseArrayAccess(const FString& Segment, FString& OutPropertyName, int32& OutIndex, bool& bOutWildcard);
};
