// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeJsonTypes.h"

class UStateTreeSchema;
struct FStateTreeEditorNode;

namespace UE::AssetFactory::StateTree
{
	bool BuildEditorNode(
		UObject* Outer,
		const UStateTreeSchema& Schema,
		const FAFStateTreeNodeSpec& Spec,
		FStateTreeEditorNode& OutNode,
		FString& OutError);
}
