// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Generators/StateTree/StateTreeBindingTypes.h"
#include "Generators/StateTree/StateTreeLinkResolver.h"
#include "PropertyBindingPath.h"

class UStateTreeEditorData;
class UStateTreeState;
struct FStateTreeEditorNode;

namespace UE::AssetFactory::StateTree
{
	struct FAFStateTreeBindingIndex
	{
		const UStateTreeEditorData* EditorData = nullptr;
		const FAFStateTreeStateIndex* StateIndex = nullptr;
		TMap<FGuid, const FStateTreeEditorNode*> NodeByGuid;
		TMap<FString, const FStateTreeEditorNode*> GlobalNodeById;
		TMap<FString, const FStateTreeEditorNode*> GlobalTaskById;
		TMap<FString, const FStateTreeEditorNode*> EvaluatorById;
		TMap<const UStateTreeState*, TMap<FString, const FStateTreeEditorNode*>> StateNodeById;
		TMap<const UStateTreeState*, TMap<FString, const FStateTreeEditorNode*>> StateTaskById;
		TMap<const UStateTreeState*, TMap<FString, const FStateTreeEditorNode*>> StateEnterConditionById;
		TMap<const UStateTreeState*, TMap<FString, const FStateTreeEditorNode*>> StateConsiderationById;
		TMap<const UStateTreeState*, TMap<FString, TMap<FString, const FStateTreeEditorNode*>>> StateTransitionConditionByTransitionId;
	};

	bool BuildBindingIndex(
		const UStateTreeEditorData& EditorData,
		const FAFStateTreeStateIndex& StateIndex,
		FAFStateTreeBindingIndex& OutIndex,
		FString& OutError);

	bool ResolveBindingEndpointPath(
		const FAFStateTreeBindingIndex& Index,
		const FAFStateTreeBindingEndpointSpec& Endpoint,
		FPropertyBindingPath& OutPath,
		FString& OutError);

	TArray<FPropertyBindingPathSegment> MakeBindingPathSegments(
		const TArray<FAFStateTreeBindingPathSegmentSpec>& SegmentSpecs,
		FString& OutError);
}
