// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "StructUtils/PropertyBag.h"

struct FAFStateTreeParameterSpec
{
	FString Name;
	FString Type;
	FGuid ID;
	bool bHasExplicitID = false;
	bool bHasValue = false;
	bool bOverridden = false;
	TSharedPtr<FJsonValue> Value;
};

struct FAFStateTreeParameterBagSpec
{
	bool bSpecified = false;
	TArray<FAFStateTreeParameterSpec> Parameters;
};

namespace UE::AssetFactory::StateTree
{
	bool ParseParameterBagSpec(
		const TSharedPtr<FJsonObject>& ParametersObject,
		const FString& ScopeLabel,
		FAFStateTreeParameterBagSpec& OutSpec,
		FString& OutError,
		bool bRequireType = true);

	bool ApplyParameterBagSpec(
		FInstancedPropertyBag& Bag,
		const FAFStateTreeParameterBagSpec& Spec,
		const FString& AssetPath,
		const FString& ScopeLabel,
		FString& OutError);

	TSharedPtr<FJsonObject> ExtractParameterBag(
		const FInstancedPropertyBag& Bag,
		bool bDiffOnly);
}
