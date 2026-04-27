// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeJsonTypes.h"

class UStateTreeSchema;

namespace UE::AssetFactory::StateTree
{
	bool ValidateNodeSpec(
		const UStateTreeSchema& Schema,
		const FAFStateTreeNodeSpec& Spec,
		FString& OutError);
}
