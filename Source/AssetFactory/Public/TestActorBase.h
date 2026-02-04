// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TestActorBase.generated.h"

/**
 * Test Actor base class for validating Blueprint generation with default properties
 */
UCLASS(Blueprintable)
class ASSETFACTORY_API ATestActorBase : public AActor
{
	GENERATED_BODY()

public:
	ATestActorBase();

	/** Test health property */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test|Stats")
	float MaxHealth = 100.0f;

	/** Test damage property */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test|Stats")
	float BaseDamage = 10.0f;

	/** Test speed property */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test|Stats")
	float MoveSpeed = 600.0f;

	/** Test name property */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test|Info")
	FString DisplayName;

	/** Test enabled flag */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test|Info")
	bool bIsEnabled = true;

	/** Test level property */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test|Info")
	int32 Level = 1;
};
