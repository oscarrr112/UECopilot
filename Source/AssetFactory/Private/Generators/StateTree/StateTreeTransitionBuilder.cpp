// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTree/StateTreeTransitionBuilder.h"

#include "Generators/StateTree/StateTreeLinkResolver.h"
#include "Generators/StateTree/StateTreeNodeBuilder.h"
#include "Generators/StateTree/StateTreeStructureTypes.h"
#include "StateTreeEditorNode.h"
#include "StateTreeSchema.h"
#include "StateTreeState.h"

namespace
{
	FGuid MakeTransitionGuid(const FAFStateTreeTransitionSpec& Spec, const FString& StatePath, int32 Index)
	{
		if (!Spec.Id.IsEmpty())
		{
			FGuid ParsedGuid;
			if (FGuid::Parse(Spec.Id, ParsedGuid))
			{
				return ParsedGuid;
			}
			return FGuid::NewDeterministicGuid(TEXT("AssetFactory.StateTree.Transition.") + Spec.Id);
		}
		return FGuid::NewDeterministicGuid(FString::Printf(TEXT("AssetFactory.StateTree.Transition.%s.%d"), *StatePath, Index));
	}
}

bool UE::AssetFactory::StateTree::BuildTransitionsForState(
	UObject* Outer,
	const UStateTreeSchema& Schema,
	const FAFStateTreeStateIndex& Index,
	const FAFStateTreeStateSpec& Spec,
	UStateTreeState& State,
	FString& OutError)
{
	State.Transitions.Reset();

	for (int32 TransitionIndex = 0; TransitionIndex < Spec.Transitions.Num(); ++TransitionIndex)
	{
		const FAFStateTreeTransitionSpec& TransitionSpec = Spec.Transitions[TransitionIndex];
		UStateTreeState* TargetState = nullptr;
		if (TransitionSpec.Type == EStateTreeTransitionType::GotoState)
		{
			if (!ResolveStateReference(Index, TransitionSpec.Target, TargetState, OutError))
			{
				OutError = FString::Printf(
					TEXT("StateTree transition in state '%s' has invalid target: %s"),
					*Spec.CanonicalPath,
					*OutError);
				return false;
			}
		}
		else if (!TransitionSpec.Target.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("StateTree transition in state '%s' defines target '%s' but type is not GotoState"),
				*Spec.CanonicalPath,
				*TransitionSpec.Target);
			return false;
		}

		if (TransitionSpec.Trigger == EStateTreeTransitionTrigger::OnEvent && !TransitionSpec.RequiredEventTag.IsValid())
		{
			OutError = FString::Printf(
				TEXT("StateTree transition in state '%s' with trigger OnEvent must define requiredEvent.tag"),
				*Spec.CanonicalPath);
			return false;
		}

		FStateTreeTransition& Transition = State.Transitions.AddDefaulted_GetRef();
		Transition.Trigger = TransitionSpec.Trigger;
		Transition.RequiredEvent = FStateTreeEventDesc(TransitionSpec.RequiredEventTag);
		Transition.State = MakeStateLink(TransitionSpec.Type, TargetState);
		Transition.ID = MakeTransitionGuid(TransitionSpec, Spec.CanonicalPath, TransitionIndex);
		Transition.Priority = TransitionSpec.Priority;
		Transition.bDelayTransition = TransitionSpec.Delay.bEnabled;
		Transition.DelayDuration = TransitionSpec.Delay.Duration;
		Transition.DelayRandomVariance = TransitionSpec.Delay.RandomVariance;
		Transition.bTransitionEnabled = TransitionSpec.bEnabled;
		Transition.Conditions.Reset();

		for (const FAFStateTreeNodeSpec& ConditionSpec : TransitionSpec.Conditions)
		{
			FStateTreeEditorNode ConditionNode;
			if (!BuildEditorNode(Outer, Schema, ConditionSpec, ConditionNode, OutError))
			{
				OutError = FString::Printf(
					TEXT("Failed to build transition condition in state '%s': %s"),
					*Spec.CanonicalPath,
					*OutError);
				return false;
			}
			Transition.Conditions.Add(MoveTemp(ConditionNode));
		}
	}

	return true;
}
