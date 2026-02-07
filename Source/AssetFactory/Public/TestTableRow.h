// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "TestTableRow.generated.h"

/**
 * Test row struct for validating DataTable generator functionality
 */
USTRUCT(BlueprintType)
struct ASSETFACTORY_API FTestTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	int32 MaxHealth = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	float AttackPower = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	bool bIsRanged = false;
};
