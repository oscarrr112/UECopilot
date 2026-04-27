// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UStateTreeSchema;
class UStateTreeState;
struct FAFStateTreeStateIndex;
struct FAFStateTreeStateSpec;

namespace UE::AssetFactory::StateTree
{
	bool BuildTransitionsForState(
		UObject* Outer,
		const UStateTreeSchema& Schema,
		const FAFStateTreeStateIndex& Index,
		const FAFStateTreeStateSpec& Spec,
		UStateTreeState& State,
		FString& OutError);
}
