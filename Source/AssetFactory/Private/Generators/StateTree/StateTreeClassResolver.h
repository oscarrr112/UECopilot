// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeJsonTypes.h"

class UStateTreeSchema;

namespace UE::AssetFactory::StateTree
{
	UScriptStruct* ResolveScriptStruct(const FString& TypeName);
	UClass* ResolveNodeClass(const FString& TypeName);

	const UScriptStruct* GetExpectedBaseStruct(EAFStateTreeNodeKind Kind);
	UClass* GetExpectedBlueprintBaseClass(EAFStateTreeNodeKind Kind);

	EAFStateTreeNodeKind GetStructNodeKind(const UScriptStruct* Struct);
	EAFStateTreeNodeKind GetClassNodeKind(const UClass* Class);
	FString DescribeNodeKindForType(const UScriptStruct* Struct, const UClass* Class);
}
