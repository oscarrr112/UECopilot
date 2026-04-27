// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

class UStateTreeEditorData;
class UStateTreeState;
struct FStateTreeEditorNode;

namespace UE::AssetFactory::StateTree
{
	TArray<TSharedPtr<FJsonValue>> ExtractSubTrees(const UStateTreeEditorData* EditorData, bool bDiffOnly);
	TArray<TSharedPtr<FJsonValue>> ExtractEditorNodes(const TArray<FStateTreeEditorNode>& Nodes, const FString& Kind, bool bDiffOnly);
}
