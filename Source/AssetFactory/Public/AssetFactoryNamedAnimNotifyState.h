// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AssetFactoryNamedAnimNotifyState.generated.h"

UCLASS()
class ASSETFACTORY_API UAssetFactoryNamedAnimNotifyState : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual FString GetNotifyName_Implementation() const override;
};
