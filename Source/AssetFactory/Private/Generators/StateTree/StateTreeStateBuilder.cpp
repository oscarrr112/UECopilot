// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTree/StateTreeStateBuilder.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Generators/StateTree/StateTreeBindingBuilder.h"
#include "Generators/StateTree/StateTreeLinkResolver.h"
#include "Generators/StateTree/StateTreeNodeBuilder.h"
#include "Generators/StateTree/StateTreeStructureTypes.h"
#include "Generators/StateTree/StateTreeTransitionBuilder.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTree.h"
#include "StateTreeSchema.h"
#include "StateTreeState.h"

namespace
{
	FGuid MakeStateGuid(const FString& Id, const FString& CanonicalPath)
	{
		if (!Id.IsEmpty())
		{
			FGuid ParsedGuid;
			if (FGuid::Parse(Id, ParsedGuid))
			{
				return ParsedGuid;
			}
			return FGuid::NewDeterministicGuid(TEXT("AssetFactory.StateTree.State.") + Id);
		}
		return FGuid::NewDeterministicGuid(TEXT("AssetFactory.StateTree.State.") + CanonicalPath);
	}

	bool ParseNodeSpecsFromConfig(
		const TSharedPtr<FJsonObject>& Config,
		const TCHAR* LowerFieldName,
		const TCHAR* UpperFieldName,
		EAFStateTreeNodeKind Kind,
		TArray<FAFStateTreeNodeSpec>& OutSpecs,
		FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!UE::AssetFactory::StateTree::StructureJson::ReadOptionalArrayField(Config, LowerFieldName, UpperFieldName, Array, OutError))
		{
			return false;
		}
		if (!Array)
		{
			return true;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Array)
		{
			FAFStateTreeNodeSpec Spec;
			if (!UE::AssetFactory::StateTree::ParseNodeSpec(Value.IsValid() ? Value->AsObject() : nullptr, Kind, Spec, OutError))
			{
				return false;
			}
			OutSpecs.Add(MoveTemp(Spec));
		}
		return true;
	}

	bool BuildEditorNodes(
		UObject* Outer,
		const UStateTreeSchema& Schema,
		const TArray<FAFStateTreeNodeSpec>& Specs,
		TArray<FStateTreeEditorNode>& OutNodes,
		FString& OutError,
		FAFStateTreeStateIndex* Index = nullptr)
	{
		OutNodes.Reset();
		for (const FAFStateTreeNodeSpec& Spec : Specs)
		{
			FStateTreeEditorNode EditorNode;
			if (!UE::AssetFactory::StateTree::BuildEditorNode(Outer, Schema, Spec, EditorNode, OutError))
			{
				return false;
			}
			OutNodes.Add(MoveTemp(EditorNode));
			if (Index && !UE::AssetFactory::StateTree::RegisterNodeAlias(*Index, Spec.Id, OutNodes.Last(), OutError))
			{
				return false;
			}
		}
		return true;
	}

	bool ApplyStateProperties(UStateTreeState& State, const FAFStateTreeStateSpec& Spec)
	{
		State.Name = FName(*Spec.Name);
		State.ID = MakeStateGuid(Spec.Id, Spec.CanonicalPath);
		State.Type = Spec.Type;
		State.Parameters.bFixedLayout = Spec.Type == EStateTreeStateType::Linked
			|| Spec.Type == EStateTreeStateType::LinkedAsset;
		State.Description = Spec.Description;
		State.Tag = Spec.Tag;
		if (Spec.SelectionBehavior.IsSet())
		{
			State.SelectionBehavior = Spec.SelectionBehavior.GetValue();
		}
		if (Spec.TasksCompletion.IsSet())
		{
			State.TasksCompletion = Spec.TasksCompletion.GetValue();
		}
		if (Spec.bEnabled.IsSet())
		{
			State.bEnabled = Spec.bEnabled.GetValue();
		}
		if (Spec.CustomTickRate.bTouched)
		{
			State.bHasCustomTickRate = Spec.CustomTickRate.bEnabled;
			State.CustomTickRate = Spec.CustomTickRate.Value;
		}
		return true;
	}

	bool ApplyInlineParameterOverrideFlags(
		FStateTreeStateParameters& Parameters,
		const FAFStateTreeParameterBagSpec& ParameterSpec,
		const FString& ScopeLabel,
		FString& OutError)
	{
		FAFStateTreeParameterBagSpec OverrideFlags;
		OverrideFlags.bSpecified = ParameterSpec.bSpecified;
		for (const FAFStateTreeParameterSpec& Parameter : ParameterSpec.Parameters)
		{
			if (!Parameter.bOverridden)
			{
				continue;
			}

			FAFStateTreeParameterSpec Override = Parameter;
			Override.bHasValue = false;
			Override.Value.Reset();
			OverrideFlags.Parameters.Add(MoveTemp(Override));
		}

		return OverrideFlags.Parameters.IsEmpty()
			|| UE::AssetFactory::StateTree::ApplyParameterOverrides(Parameters, OverrideFlags, ScopeLabel, OutError);
	}

	bool BuildStateRecursive(
		UStateTreeEditorData& EditorData,
		const FAFStateTreeStateSpec& Spec,
		UStateTreeState& State,
		FAFStateTreeStateIndex& Index,
		FString& OutError)
	{
		if (!EditorData.Schema)
		{
			OutError = TEXT("StateTree editor data has no schema instance");
			return false;
		}

		ApplyStateProperties(State, Spec);
		if (!UE::AssetFactory::StateTree::RegisterStateReference(Index, Spec.Id, Spec.CanonicalPath, State, OutError))
		{
			return false;
		}
		if (Spec.Parameters.bSpecified)
		{
			State.Parameters.ResetParametersAndOverrides();
			if (!UE::AssetFactory::StateTree::ApplyParameterBagSpec(
				State.Parameters.Parameters,
				Spec.Parameters,
				TEXT(""),
				Spec.CanonicalPath,
				OutError))
			{
				return false;
			}
			if (!ApplyInlineParameterOverrideFlags(State.Parameters, Spec.Parameters, Spec.CanonicalPath, OutError))
			{
				return false;
			}
		}
		if (Spec.Type != EStateTreeStateType::Linked
			&& Spec.Type != EStateTreeStateType::LinkedAsset
			&& Spec.ParameterOverrides.bSpecified)
		{
			if (!UE::AssetFactory::StateTree::ApplyParameterOverrides(
				State.Parameters,
				Spec.ParameterOverrides,
				Spec.CanonicalPath,
				OutError))
			{
				return false;
			}
		}

		TArray<FStateTreeEditorNode> Tasks;
		if (!BuildEditorNodes(&State, *EditorData.Schema, Spec.Tasks, Tasks, OutError, &Index))
		{
			return false;
		}
		if (!EditorData.Schema->AllowMultipleTasks())
		{
			if (Tasks.Num() > 1)
			{
				OutError = FString::Printf(TEXT("StateTree schema '%s' does not allow multiple tasks in state '%s'"), *EditorData.Schema->GetClass()->GetPathName(), *Spec.CanonicalPath);
				return false;
			}
			State.Tasks.Reset();
			State.SingleTask.Reset();
			if (Tasks.Num() == 1)
			{
				State.SingleTask = MoveTemp(Tasks[0]);
			}
		}
		else
		{
			State.SingleTask.Reset();
			State.Tasks = MoveTemp(Tasks);
		}

		if (!BuildEditorNodes(&State, *EditorData.Schema, Spec.EnterConditions, State.EnterConditions, OutError, &Index)
			|| !BuildEditorNodes(&State, *EditorData.Schema, Spec.Considerations, State.Considerations, OutError, &Index))
		{
			return false;
		}

		State.Children.Reset();
		for (const FAFStateTreeStateSpec& ChildSpec : Spec.Children)
		{
			UStateTreeState& ChildState = State.AddChildState(FName(*ChildSpec.Name), ChildSpec.Type);
			if (!BuildStateRecursive(EditorData, ChildSpec, ChildState, Index, OutError))
			{
				return false;
			}
		}

		return true;
	}

	bool ParseSubTreeSpecs(
		TSharedPtr<FJsonObject> Config,
		TArray<FAFStateTreeStateSpec>& OutSpecs,
		FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* SubTrees = nullptr;
		if (!UE::AssetFactory::StateTree::StructureJson::ReadOptionalArrayField(Config, TEXT("subTrees"), TEXT("SubTrees"), SubTrees, OutError))
		{
			return false;
		}
		if (!SubTrees)
		{
			return true;
		}

		for (const TSharedPtr<FJsonValue>& SubTreeValue : *SubTrees)
		{
			FAFStateTreeStateSpec SubTreeSpec;
			if (!UE::AssetFactory::StateTree::ParseStateSpec(SubTreeValue.IsValid() ? SubTreeValue->AsObject() : nullptr, FString(), SubTreeSpec, OutError))
			{
				return false;
			}
			OutSpecs.Add(MoveTemp(SubTreeSpec));
		}
		return true;
	}

	bool ApplyLinkedState(
		UStateTreeState& State,
		const FAFStateTreeStateSpec& Spec,
		const FAFStateTreeStateIndex& Index,
		FString& OutError)
	{
		if (!Spec.LinkedState.IsEmpty() || !Spec.LinkedSubtree.IsEmpty())
		{
			if (Spec.Type != EStateTreeStateType::Linked)
			{
				OutError = FString::Printf(TEXT("StateTree state '%s' defines linkedState/linkedSubtree but is not type Linked"), *Spec.CanonicalPath);
				return false;
			}
			if (!Spec.LinkedState.IsEmpty() && !Spec.LinkedSubtree.IsEmpty())
			{
				OutError = FString::Printf(TEXT("StateTree state '%s' cannot define both linkedState and linkedSubtree"), *Spec.CanonicalPath);
				return false;
			}

			const FString& Reference = Spec.LinkedSubtree.IsEmpty() ? Spec.LinkedState : Spec.LinkedSubtree;
			UStateTreeState* LinkedState = nullptr;
			if (!UE::AssetFactory::StateTree::ResolveStateReference(Index, Reference, LinkedState, OutError))
			{
				OutError = FString::Printf(TEXT("StateTree linked state '%s' has invalid target: %s"), *Spec.CanonicalPath, *OutError);
				return false;
			}
			if (LinkedState == &State)
			{
				OutError = FString::Printf(TEXT("StateTree linked state '%s' cannot link to itself"), *Spec.CanonicalPath);
				return false;
			}
			if (LinkedState->Type != EStateTreeStateType::Subtree)
			{
				OutError = FString::Printf(TEXT("StateTree linked target '%s' must be type Subtree"), *Reference);
				return false;
			}
			State.SetLinkedState(LinkedState->GetLinkToState());
		}

		if (!Spec.LinkedAsset.IsEmpty())
		{
			if (Spec.Type != EStateTreeStateType::LinkedAsset)
			{
				OutError = FString::Printf(TEXT("StateTree state '%s' defines linkedAsset but is not type LinkedAsset"), *Spec.CanonicalPath);
				return false;
			}

			UStateTree* LinkedAsset = LoadObject<UStateTree>(nullptr, *Spec.LinkedAsset);
			if (!LinkedAsset)
			{
				OutError = FString::Printf(TEXT("StateTree linkedAsset '%s' could not be loaded"), *Spec.LinkedAsset);
				return false;
			}
			State.SetLinkedStateAsset(LinkedAsset);
		}

		if (Spec.Type == EStateTreeStateType::Linked && Spec.LinkedState.IsEmpty() && Spec.LinkedSubtree.IsEmpty())
		{
			OutError = FString::Printf(TEXT("StateTree linked state '%s' must define linkedState or linkedSubtree"), *Spec.CanonicalPath);
			return false;
		}
		if (Spec.Type == EStateTreeStateType::LinkedAsset && Spec.LinkedAsset.IsEmpty())
		{
			OutError = FString::Printf(TEXT("StateTree linked asset state '%s' must define linkedAsset"), *Spec.CanonicalPath);
			return false;
		}

		return true;
	}

	bool FinalizeStateRecursive(
		UStateTreeEditorData& EditorData,
		const FAFStateTreeStateSpec& Spec,
		UStateTreeState& State,
		const FAFStateTreeStateIndex& Index,
		FString& OutError)
	{
		if (!ApplyLinkedState(State, Spec, Index, OutError))
		{
			return false;
		}
		if (Spec.Type == EStateTreeStateType::Linked
			|| Spec.Type == EStateTreeStateType::LinkedAsset)
		{
			State.UpdateParametersFromLinkedSubtree();
			if (Spec.ParameterOverrides.bSpecified
				&& !UE::AssetFactory::StateTree::ApplyParameterOverrides(
					State.Parameters,
					Spec.ParameterOverrides,
					Spec.CanonicalPath,
					OutError))
			{
				return false;
			}
		}
		if (!UE::AssetFactory::StateTree::BuildTransitionsForState(&State, *EditorData.Schema, Index, Spec, State, OutError))
		{
			return false;
		}

		if (Spec.Children.Num() != State.Children.Num())
		{
			OutError = FString::Printf(TEXT("StateTree state '%s' child count mismatch during finalize"), *Spec.CanonicalPath);
			return false;
		}
		for (int32 ChildIndex = 0; ChildIndex < Spec.Children.Num(); ++ChildIndex)
		{
			if (!State.Children[ChildIndex] || !FinalizeStateRecursive(EditorData, Spec.Children[ChildIndex], *State.Children[ChildIndex], Index, OutError))
			{
				return false;
			}
		}

		return true;
	}
}

bool UE::AssetFactory::StateTree::ApplyStateTreeConfig(
	UStateTreeEditorData& EditorData,
	TSharedPtr<FJsonObject> Config,
	FString& OutError)
{
	if (!Config.IsValid())
	{
		OutError = TEXT("Invalid StateTree configuration object");
		return false;
	}
	if (!EditorData.Schema)
	{
		OutError = TEXT("StateTree editor data has no schema instance");
		return false;
	}

	TArray<FAFStateTreeNodeSpec> EvaluatorSpecs;
	TArray<FAFStateTreeNodeSpec> GlobalTaskSpecs;
	TArray<FAFStateTreeStateSpec> SubTreeSpecs;
	TSharedPtr<FJsonObject> RootParametersObject;
	FAFStateTreeParameterBagSpec RootParametersSpec;
	if (!UE::AssetFactory::StateTree::StructureJson::ReadOptionalObjectField(Config, TEXT("rootParameters"), TEXT("RootParameters"), RootParametersObject, OutError))
	{
		return false;
	}
	if (!RootParametersObject.IsValid()
		&& !UE::AssetFactory::StateTree::StructureJson::ReadOptionalObjectField(Config, TEXT("parameters"), TEXT("Parameters"), RootParametersObject, OutError))
	{
		return false;
	}
	if (RootParametersObject.IsValid())
	{
		if (!UE::AssetFactory::StateTree::ParseParameterBagSpec(RootParametersObject, TEXT("root"), RootParametersSpec, OutError))
		{
			return false;
		}
	}
	if (!ParseNodeSpecsFromConfig(Config, TEXT("evaluators"), TEXT("Evaluators"), EAFStateTreeNodeKind::Evaluator, EvaluatorSpecs, OutError)
		|| !ParseNodeSpecsFromConfig(Config, TEXT("globalTasks"), TEXT("GlobalTasks"), EAFStateTreeNodeKind::GlobalTask, GlobalTaskSpecs, OutError)
		|| !ParseSubTreeSpecs(Config, SubTreeSpecs, OutError))
	{
		return false;
	}

	FString CompletionString;
	if (UE::AssetFactory::StateTree::StructureJson::TryGetStringField(Config, TEXT("globalTasksCompletion"), TEXT("GlobalTasksCompletion"), CompletionString))
	{
		EStateTreeTaskCompletionType CompletionType = EStateTreeTaskCompletionType::Any;
		if (!UE::AssetFactory::StateTree::TryParseTaskCompletionType(CompletionString, CompletionType))
		{
			OutError = FString::Printf(TEXT("Unknown StateTree global task completion value: %s"), *CompletionString);
			return false;
		}
		EditorData.GlobalTasksCompletion = CompletionType;
	}

	FAFStateTreeStateIndex Index;
	if (!BuildEditorNodes(&EditorData, *EditorData.Schema, EvaluatorSpecs, EditorData.Evaluators, OutError, &Index)
		|| !BuildEditorNodes(&EditorData, *EditorData.Schema, GlobalTaskSpecs, EditorData.GlobalTasks, OutError, &Index))
	{
		return false;
	}
	if (RootParametersObject.IsValid())
	{
		FInstancedPropertyBag& RootParameterBag = const_cast<FInstancedPropertyBag&>(EditorData.GetRootParametersPropertyBag());
		if (!UE::AssetFactory::StateTree::ApplyParameterBagSpec(
			RootParameterBag,
			RootParametersSpec,
			TEXT(""),
			TEXT("root"),
			OutError))
		{
			return false;
		}
	}

	EditorData.SubTrees.Reset();
	for (const FAFStateTreeStateSpec& SubTreeSpec : SubTreeSpecs)
	{
		UStateTreeState& SubTree = EditorData.AddSubTree(FName(*SubTreeSpec.Name));
		if (!BuildStateRecursive(EditorData, SubTreeSpec, SubTree, Index, OutError))
		{
			return false;
		}
	}

	if (SubTreeSpecs.Num() != EditorData.SubTrees.Num())
	{
		OutError = TEXT("StateTree subtree count mismatch during finalize");
		return false;
	}
	for (int32 SubTreeIndex = 0; SubTreeIndex < SubTreeSpecs.Num(); ++SubTreeIndex)
	{
		if (!EditorData.SubTrees[SubTreeIndex] || !FinalizeStateRecursive(EditorData, SubTreeSpecs[SubTreeIndex], *EditorData.SubTrees[SubTreeIndex], Index, OutError))
		{
			return false;
		}
	}
	if (!UE::AssetFactory::StateTree::ApplyPropertyBindings(EditorData, Index, Config, OutError))
	{
		return false;
	}

	return true;
}
