// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTypes.h"

class UStateTreeState;
struct FStateTreeEditorNode;

struct FAFStateTreeStateIndex
{
	TMap<FString, UStateTreeState*> ById;
	TMap<FString, UStateTreeState*> ByPath;
	TMap<FGuid, UStateTreeState*> ByGuid;
	TMap<FString, TArray<UStateTreeState*>> ByName;
	TMap<const UStateTreeState*, FString> PathByState;
	TMap<FGuid, FString> NodeAliasByGuid;
};

namespace UE::AssetFactory::StateTree
{
	bool RegisterStateReference(
		FAFStateTreeStateIndex& Index,
		const FString& Id,
		const FString& CanonicalPath,
		UStateTreeState& State,
		FString& OutError);

	bool RegisterNodeAlias(
		FAFStateTreeStateIndex& Index,
		const FString& Id,
		const FStateTreeEditorNode& Node,
		FString& OutError);

	bool ResolveStateReference(
		const FAFStateTreeStateIndex& Index,
		const FString& Reference,
		UStateTreeState*& OutState,
		FString& OutError);

	FStateTreeStateLink MakeStateLink(EStateTreeTransitionType Type, const UStateTreeState* State);
}
