// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreePropertyFunctionBase.h"
#include "StructUtils/InstancedStruct.h"
#include "StateTreeRichBindingTestTypes.generated.h"

struct FStateTreeExecutionContext;

USTRUCT()
struct FAFStateTreeRichBindingPayload
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	float Value = 0.f;
};

USTRUCT()
struct FAFStateTreeRichBindingInputPropertyFunctionInstanceData
{
	GENERATED_BODY()

	FAFStateTreeRichBindingInputPropertyFunctionInstanceData()
	{
		DynamicInput.InitializeAs(FAFStateTreeRichBindingPayload::StaticStruct());
	}

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (BaseStruct = "/Script/AssetFactory.AFStateTreeRichBindingPayload"))
	FInstancedStruct DynamicInput;

	UPROPERTY(EditAnywhere, Category = Output)
	float Result = 0.f;
};

USTRUCT(meta = (DisplayName = "AF Rich Binding Input", Category = "AssetFactory|Test"))
struct FAFStateTreeRichBindingInputPropertyFunction : public FStateTreePropertyFunctionBase
{
	GENERATED_BODY()

	using FInstanceDataType = FAFStateTreeRichBindingInputPropertyFunctionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual void Execute(FStateTreeExecutionContext& Context) const override;
};
