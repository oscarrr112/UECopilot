// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTree/StateTreeExtract.h"

#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "Blueprint/StateTreeConsiderationBlueprintBase.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Generators/StateTree/StateTreeJsonTypes.h"
#include "Generators/StateTree/StateTreePropertyBagAdapter.h"
#include "JsonObjectConverter.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeState.h"
#include "StateTreeTasksStatus.h"
#include "Utils/PropertySetterUtils.h"

namespace
{
	struct FStateTreeExtractContext
	{
		TMap<FGuid, FString> PathById;
	};

	template<typename TEnum>
	FString EnumToString(TEnum Value)
	{
		if (const UEnum* Enum = StaticEnum<TEnum>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(Value));
		}
		return FString();
	}

	FString StatePath(const FString& ParentPath, const UStateTreeState& State)
	{
		const FString Name = State.Name.ToString();
		return ParentPath.IsEmpty() ? Name : ParentPath + TEXT("/") + Name;
	}

	void CollectStatePaths(const UStateTreeState& State, const FString& ParentPath, FStateTreeExtractContext& Context)
	{
		const FString Path = StatePath(ParentPath, State);
		Context.PathById.Add(State.ID, Path);
		for (const TObjectPtr<UStateTreeState>& Child : State.Children)
		{
			if (Child)
			{
				CollectStatePaths(*Child, Path, Context);
			}
		}
	}

	TSharedPtr<FJsonObject> ExtractStructProperties(const FInstancedStruct& StructData, bool bDiffOnly)
	{
		if (!StructData.IsValid())
		{
			return MakeShared<FJsonObject>();
		}

		TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
		const int64 CheckFlags = CPF_Edit;
		const int64 SkipFlags = CPF_Transient | CPF_Deprecated;
		FJsonObjectConverter::UStructToJsonObject(StructData.GetScriptStruct(), StructData.GetMemory(), Properties, CheckFlags, SkipFlags);

		if (bDiffOnly)
		{
			FInstancedStruct DefaultData;
			DefaultData.InitializeAs(StructData.GetScriptStruct());
			TArray<FString> RemoveKeys;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
			{
				const FProperty* Property = StructData.GetScriptStruct()->FindPropertyByName(FName(*Pair.Key));
				if (Property)
				{
					const void* CurrentPtr = Property->ContainerPtrToValuePtr<void>(StructData.GetMemory());
					const void* DefaultPtr = Property->ContainerPtrToValuePtr<void>(DefaultData.GetMemory());
					if (Property->Identical(CurrentPtr, DefaultPtr))
					{
						RemoveKeys.Add(Pair.Key);
					}
				}
			}
			for (const FString& Key : RemoveKeys)
			{
				Properties->RemoveField(Key);
			}
		}

		return Properties;
	}

	TSharedPtr<FJsonObject> ExtractStructProperties(const FInstancedStruct& StructData)
	{
		return ExtractStructProperties(StructData, false);
	}

	TSharedPtr<FJsonObject> ExtractDataSection(const FInstancedStruct& StructData, UObject* ObjectData, bool bDiffOnly)
	{
		TSharedPtr<FJsonObject> Section = MakeShared<FJsonObject>();
		if (StructData.IsValid())
		{
			Section->SetStringField(TEXT("type"), StructData.GetScriptStruct()->GetPathName());
			Section->SetObjectField(TEXT("properties"), ExtractStructProperties(StructData, bDiffOnly));
		}
		else if (ObjectData)
		{
			Section->SetStringField(TEXT("type"), ObjectData->GetClass()->GetPathName());
			TSharedPtr<FJsonObject> Properties = FPropertySetterUtils::ExtractPropertiesToJson(ObjectData, true, bDiffOnly);
			Section->SetObjectField(TEXT("properties"), Properties.IsValid() ? Properties : MakeShared<FJsonObject>());
		}
		else
		{
			Section->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
		}
		return Section;
	}

	FString ResolveNodeType(const FStateTreeEditorNode& Node)
	{
		if (const FStateTreeBlueprintTaskWrapper* Task = Node.Node.GetPtr<FStateTreeBlueprintTaskWrapper>())
		{
			return Task->TaskClass ? Task->TaskClass->GetPathName() : FString();
		}
		if (const FStateTreeBlueprintEvaluatorWrapper* Evaluator = Node.Node.GetPtr<FStateTreeBlueprintEvaluatorWrapper>())
		{
			return Evaluator->EvaluatorClass ? Evaluator->EvaluatorClass->GetPathName() : FString();
		}
		if (const FStateTreeBlueprintConditionWrapper* Condition = Node.Node.GetPtr<FStateTreeBlueprintConditionWrapper>())
		{
			return Condition->ConditionClass ? Condition->ConditionClass->GetPathName() : FString();
		}
		if (const FStateTreeBlueprintConsiderationWrapper* Consideration = Node.Node.GetPtr<FStateTreeBlueprintConsiderationWrapper>())
		{
			return Consideration->ConsiderationClass ? Consideration->ConsiderationClass->GetPathName() : FString();
		}
		return Node.Node.IsValid() ? Node.Node.GetScriptStruct()->GetPathName() : FString();
	}

	TSharedPtr<FJsonObject> ExtractNode(const FStateTreeEditorNode& Node, const FString& Kind, bool bDiffOnly)
	{
		TSharedPtr<FJsonObject> NodeJson = MakeShared<FJsonObject>();
		NodeJson->SetStringField(TEXT("id"), Node.ID.ToString(EGuidFormats::DigitsWithHyphensLower));
		NodeJson->SetStringField(TEXT("kind"), Kind);
		NodeJson->SetStringField(TEXT("type"), ResolveNodeType(Node));

		TSharedPtr<FJsonObject> NodeSection = MakeShared<FJsonObject>();
		NodeSection->SetObjectField(TEXT("properties"), ExtractStructProperties(Node.Node, bDiffOnly));
		NodeJson->SetObjectField(TEXT("node"), NodeSection);
		NodeJson->SetObjectField(TEXT("instance"), ExtractDataSection(Node.Instance, Node.InstanceObject, bDiffOnly));
		NodeJson->SetObjectField(TEXT("executionRuntimeData"), ExtractDataSection(Node.ExecutionRuntimeData, Node.ExecutionRuntimeDataObject, bDiffOnly));

		if (Kind.Equals(TEXT("enterCondition"), ESearchCase::CaseSensitive)
			|| Kind.Equals(TEXT("transitionCondition"), ESearchCase::CaseSensitive)
			|| Kind.Equals(TEXT("consideration"), ESearchCase::CaseSensitive))
		{
			TSharedPtr<FJsonObject> Expression = MakeShared<FJsonObject>();
			Expression->SetNumberField(TEXT("indent"), Node.ExpressionIndent);
			Expression->SetStringField(TEXT("operand"), UE::AssetFactory::StateTree::ExpressionOperandToString(Node.ExpressionOperand));
			NodeJson->SetObjectField(TEXT("expression"), Expression);
		}

		return NodeJson;
	}

	TArray<TSharedPtr<FJsonValue>> ExtractTransitions(const UStateTreeState& State, const FStateTreeExtractContext& Context, bool bDiffOnly)
	{
		TArray<TSharedPtr<FJsonValue>> Transitions;
		for (const FStateTreeTransition& Transition : State.Transitions)
		{
			TSharedPtr<FJsonObject> TransitionJson = MakeShared<FJsonObject>();
			TransitionJson->SetStringField(TEXT("id"), Transition.ID.ToString(EGuidFormats::DigitsWithHyphensLower));
			TransitionJson->SetStringField(TEXT("trigger"), EnumToString(Transition.Trigger));
			TransitionJson->SetStringField(TEXT("type"), EnumToString(Transition.State.LinkType));
			TransitionJson->SetStringField(TEXT("priority"), EnumToString(Transition.Priority));
			TransitionJson->SetBoolField(TEXT("enabled"), Transition.bTransitionEnabled);

		if (Transition.State.LinkType == EStateTreeTransitionType::GotoState)
		{
			if (const FString* TargetPath = Context.PathById.Find(Transition.State.ID))
			{
				TransitionJson->SetStringField(TEXT("target"), *TargetPath);
			}
			else if (Transition.State.ID.IsValid())
			{
				TransitionJson->SetStringField(TEXT("target"), Transition.State.ID.ToString(EGuidFormats::DigitsWithHyphensLower));
			}
		}
			if (Transition.RequiredEvent.Tag.IsValid())
			{
				TSharedPtr<FJsonObject> RequiredEvent = MakeShared<FJsonObject>();
				RequiredEvent->SetStringField(TEXT("tag"), Transition.RequiredEvent.Tag.ToString());
				TransitionJson->SetObjectField(TEXT("requiredEvent"), RequiredEvent);
			}
			if (Transition.bDelayTransition || Transition.DelayDuration != 0.0f || Transition.DelayRandomVariance != 0.0f)
			{
				TSharedPtr<FJsonObject> Delay = MakeShared<FJsonObject>();
				Delay->SetBoolField(TEXT("enabled"), Transition.bDelayTransition);
				Delay->SetNumberField(TEXT("duration"), Transition.DelayDuration);
				Delay->SetNumberField(TEXT("randomVariance"), Transition.DelayRandomVariance);
				TransitionJson->SetObjectField(TEXT("delay"), Delay);
			}
			TransitionJson->SetArrayField(TEXT("conditions"), UE::AssetFactory::StateTree::ExtractEditorNodes(Transition.Conditions, TEXT("transitionCondition"), bDiffOnly));
			Transitions.Add(MakeShared<FJsonValueObject>(TransitionJson));
		}
		return Transitions;
	}

	TSharedPtr<FJsonObject> ExtractState(const UStateTreeState& State, const FString& ParentPath, const FStateTreeExtractContext& Context, bool bDiffOnly)
	{
		TSharedPtr<FJsonObject> StateJson = MakeShared<FJsonObject>();
		StateJson->SetStringField(TEXT("id"), State.ID.ToString(EGuidFormats::DigitsWithHyphensLower));
		StateJson->SetStringField(TEXT("name"), State.Name.ToString());
		StateJson->SetStringField(TEXT("type"), EnumToString(State.Type));
		StateJson->SetStringField(TEXT("selectionBehavior"), EnumToString(State.SelectionBehavior));
		StateJson->SetStringField(TEXT("tasksCompletion"), EnumToString(State.TasksCompletion));
		StateJson->SetStringField(TEXT("description"), State.Description);
		StateJson->SetBoolField(TEXT("enabled"), State.bEnabled);
		if (State.Tag.IsValid())
		{
			StateJson->SetStringField(TEXT("tag"), State.Tag.ToString());
		}
		if (State.bHasCustomTickRate || State.CustomTickRate != 0.0f)
		{
			TSharedPtr<FJsonObject> CustomTickRate = MakeShared<FJsonObject>();
			CustomTickRate->SetBoolField(TEXT("enabled"), State.bHasCustomTickRate);
			CustomTickRate->SetNumberField(TEXT("value"), State.CustomTickRate);
			StateJson->SetObjectField(TEXT("customTickRate"), CustomTickRate);
		}
		TSharedPtr<FJsonObject> Parameters = UE::AssetFactory::StateTree::ExtractParameterBag(State.Parameters.Parameters, bDiffOnly);
		if (Parameters.IsValid() && Parameters->Values.Num() > 0)
		{
			StateJson->SetObjectField(TEXT("parameters"), Parameters);
		}
		if (State.Type == EStateTreeStateType::Linked)
		{
			if (const FString* LinkedPath = Context.PathById.Find(State.LinkedSubtree.ID))
			{
				StateJson->SetStringField(TEXT("linkedSubtree"), *LinkedPath);
			}
		}
		if (State.Type == EStateTreeStateType::LinkedAsset && State.LinkedAsset)
		{
			StateJson->SetStringField(TEXT("linkedAsset"), State.LinkedAsset->GetPathName());
		}

		TArray<TSharedPtr<FJsonValue>> Tasks = UE::AssetFactory::StateTree::ExtractEditorNodes(State.Tasks, TEXT("task"), bDiffOnly);
		if (State.SingleTask.Node.IsValid())
		{
			Tasks.Add(MakeShared<FJsonValueObject>(ExtractNode(State.SingleTask, TEXT("task"), bDiffOnly)));
		}
		StateJson->SetArrayField(TEXT("tasks"), Tasks);
		StateJson->SetArrayField(TEXT("enterConditions"), UE::AssetFactory::StateTree::ExtractEditorNodes(State.EnterConditions, TEXT("enterCondition"), bDiffOnly));
		StateJson->SetArrayField(TEXT("considerations"), UE::AssetFactory::StateTree::ExtractEditorNodes(State.Considerations, TEXT("consideration"), bDiffOnly));
		StateJson->SetArrayField(TEXT("transitions"), ExtractTransitions(State, Context, bDiffOnly));

		TArray<TSharedPtr<FJsonValue>> Children;
		const FString CurrentPath = StatePath(ParentPath, State);
		for (const TObjectPtr<UStateTreeState>& Child : State.Children)
		{
			if (Child)
			{
				Children.Add(MakeShared<FJsonValueObject>(ExtractState(*Child, CurrentPath, Context, bDiffOnly)));
			}
		}
		StateJson->SetArrayField(TEXT("children"), Children);

		return StateJson;
	}
}

TArray<TSharedPtr<FJsonValue>> UE::AssetFactory::StateTree::ExtractEditorNodes(
	const TArray<FStateTreeEditorNode>& Nodes,
	const FString& Kind,
	bool bDiffOnly)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	for (const FStateTreeEditorNode& Node : Nodes)
	{
		if (Node.Node.IsValid())
		{
			Result.Add(MakeShared<FJsonValueObject>(ExtractNode(Node, Kind, bDiffOnly)));
		}
	}
	return Result;
}

TArray<TSharedPtr<FJsonValue>> UE::AssetFactory::StateTree::ExtractSubTrees(
	const UStateTreeEditorData* EditorData,
	bool bDiffOnly)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	if (!EditorData)
	{
		return Result;
	}

	FStateTreeExtractContext Context;
	for (const TObjectPtr<UStateTreeState>& SubTree : EditorData->SubTrees)
	{
		if (SubTree)
		{
			CollectStatePaths(*SubTree, FString(), Context);
		}
	}

	for (const TObjectPtr<UStateTreeState>& SubTree : EditorData->SubTrees)
	{
		if (SubTree)
		{
			Result.Add(MakeShared<FJsonValueObject>(ExtractState(*SubTree, FString(), Context, bDiffOnly)));
		}
	}

	return Result;
}
