// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "AssetFactoryNestedDefaultsVerificationCommandlet.generated.h"

/**
 * Verifies the Blueprint DefaultProperties nested CDO regression fixture in a fresh editor process.
 */
UCLASS()
class UAssetFactoryNestedDefaultsVerificationCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UAssetFactoryNestedDefaultsVerificationCommandlet();

	virtual int32 Main(const FString& Params) override;

private:
	bool ParseParameters(const FString& Params, FString& OutAssetPath) const;
	void PrintHelp() const;
};
