// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTypes.h"

class UStateTreeState;

struct FAFStateTreeStateIndex
{
	TMap<FString, UStateTreeState*> ById;
	TMap<FString, UStateTreeState*> ByPath;
	TMap<FString, TArray<UStateTreeState*>> ByName;
	TMap<const UStateTreeState*, FString> PathByState;
};

namespace UE::AssetFactory::StateTree
{
	bool RegisterStateReference(
		FAFStateTreeStateIndex& Index,
		const FString& Id,
		const FString& CanonicalPath,
		UStateTreeState& State,
		FString& OutError);

	bool ResolveStateReference(
		const FAFStateTreeStateIndex& Index,
		const FString& Reference,
		UStateTreeState*& OutState,
		FString& OutError);

	FStateTreeStateLink MakeStateLink(EStateTreeTransitionType Type, const UStateTreeState* State);
}
