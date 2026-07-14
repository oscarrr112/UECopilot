// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "StructUtils/InstancedStruct.h"

#include "AssetDocumentReflectedPropertyTestTypes.generated.h"

USTRUCT()
struct FAssetDocumentReflectedPropertyNestedTestValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "AssetDocumentTest")
	FName Identity = NAME_None;

	UPROPERTY(EditAnywhere, Category = "AssetDocumentTest")
	FString PreservedText;

	UPROPERTY(EditAnywhere, Category = "AssetDocumentTest")
	FBlackboardKeySelector Selector;

	UPROPERTY(EditAnywhere, Category = "AssetDocumentTest")
	FInstancedStruct NestedValue;

	bool operator==(const FAssetDocumentReflectedPropertyNestedTestValue& Other) const
	{
		return Identity == Other.Identity;
	}
};

FORCEINLINE uint32 GetTypeHash(const FAssetDocumentReflectedPropertyNestedTestValue& Value)
{
	return GetTypeHash(Value.Identity);
}

template<>
struct TStructOpsTypeTraits<FAssetDocumentReflectedPropertyNestedTestValue>
	: public TStructOpsTypeTraitsBase2<FAssetDocumentReflectedPropertyNestedTestValue>
{
	enum
	{
		WithIdenticalViaEquality = true,
		WithGetTypeHash = true
	};
};

UCLASS()
class UAssetDocumentReflectedPropertyTestObject : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "AssetDocumentTest")
	FInstancedStruct DynamicValue;

	UPROPERTY(EditAnywhere, Category = "AssetDocumentTest", meta = (BaseStruct = "/Script/AIModule.BlackboardEntry"))
	FInstancedStruct ConstrainedValue;

	UPROPERTY(EditAnywhere, Category = "AssetDocumentTest", meta = (BaseStruct = "/Script/AssetDocument.DoesNotExist"))
	FInstancedStruct InvalidConstraintValue;

	UPROPERTY(EditAnywhere, Category = "AssetDocumentTest")
	TArray<FAssetDocumentReflectedPropertyNestedTestValue> ArrayValues;

	UPROPERTY(EditAnywhere, Category = "AssetDocumentTest")
	TMap<FName, FAssetDocumentReflectedPropertyNestedTestValue> MapValues;

	UPROPERTY(EditAnywhere, Category = "AssetDocumentTest")
	TMap<uint8, FAssetDocumentReflectedPropertyNestedTestValue> ByteMapValues;

	UPROPERTY(EditAnywhere, Category = "AssetDocumentTest")
	TMap<uint8, float> PlainByteMapValues;

	UPROPERTY(EditAnywhere, Category = "AssetDocumentTest")
	TSet<FAssetDocumentReflectedPropertyNestedTestValue> SetValues;

	UPROPERTY(EditAnywhere, Category = "AssetDocumentTest")
	TSet<FName> PlainNameSetValues;
};
