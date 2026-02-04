// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JSON/BlueprintJSONSchema.h"
#include "BlueprintJSONParser.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogBlueprintJSON, Log, All);

/**
 * Parse result with error info
 */
USTRUCT(BlueprintType)
struct ASSETFACTORYAI_API FBlueprintParseResult
{
	GENERATED_BODY()

	/** Was parsing successful? */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bSuccess = false;

	/** Error message if failed */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString ErrorMessage;

	/** Line number where error occurred */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 ErrorLine = -1;

	/** Parsed blueprint data */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FBlueprintData BlueprintData;
};

/**
 * Blueprint JSON Parser - Parses JSON into intermediate representation
 */
UCLASS()
class ASSETFACTORYAI_API UBlueprintJSONParser : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Parse JSON string into blueprint data
	 * @param JSONString - The JSON string to parse
	 * @return Parse result with blueprint data or error info
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint JSON")
	static FBlueprintParseResult ParseBlueprintJSON(const FString& JSONString);

	/**
	 * Extract JSON from AI response (handles markdown code blocks)
	 * @param AIResponse - Raw AI response that may contain markdown
	 * @return Extracted JSON string
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint JSON")
	static FString ExtractJSONFromResponse(const FString& AIResponse);

	/**
	 * Validate blueprint data for completeness
	 * @param Data - Blueprint data to validate
	 * @param OutErrors - Array to receive validation errors
	 * @return True if valid
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint JSON")
	static bool ValidateBlueprintData(const FBlueprintData& Data, TArray<FString>& OutErrors);

	/**
	 * Serialize blueprint data to JSON
	 * @param Data - Blueprint data to serialize
	 * @return JSON string
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint JSON")
	static FString SerializeBlueprintData(const FBlueprintData& Data);

private:
	/** Parse blueprint object from JSON */
	static bool ParseBlueprintObject(const TSharedPtr<FJsonObject>& JsonObject, FBlueprintData& OutData, FString& OutError);

	/** Parse variable from JSON */
	static bool ParseVariable(const TSharedPtr<FJsonObject>& JsonObject, FBlueprintVariableData& OutData, FString& OutError);

	/** Parse graph from JSON */
	static bool ParseGraph(const TSharedPtr<FJsonObject>& JsonObject, FBlueprintGraphData& OutData, FString& OutError);

	/** Parse node from JSON */
	static bool ParseNode(const TSharedPtr<FJsonObject>& JsonObject, FBlueprintNodeData& OutData, FString& OutError);

	/** Parse pin from JSON */
	static bool ParsePin(const TSharedPtr<FJsonObject>& JsonObject, FBlueprintPinData& OutData, FString& OutError);

	/** Parse pin connection from JSON */
	static bool ParseConnection(const TSharedPtr<FJsonObject>& JsonObject, FBlueprintPinConnection& OutData, FString& OutError);

	/** Convert string to node type enum */
	static EBlueprintNodeType StringToNodeType(const FString& TypeString);

	/** Convert string to variable type enum */
	static EBlueprintVarType StringToVarType(const FString& TypeString);

	/** Convert node type enum to string */
	static FString NodeTypeToString(EBlueprintNodeType Type);

	/** Convert variable type enum to string */
	static FString VarTypeToString(EBlueprintVarType Type);
};
