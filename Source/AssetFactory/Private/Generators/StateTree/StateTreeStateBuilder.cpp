// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTree/StateTreeStateBuilder.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Generators/StateTree/StateTreeJsonTypes.h"
#include "Generators/StateTree/StateTreeNodeBuilder.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeSchema.h"
#include "StateTreeState.h"
#include "StateTreeTasksStatus.h"

namespace
{
	bool TryGetArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, const TArray<TSharedPtr<FJsonValue>>*& OutArray)
	{
		OutArray = nullptr;
		return Object.IsValid() && Object->TryGetArrayField(FieldName, OutArray) && OutArray;
	}

	bool TryGetNamedArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* LowerName, const TCHAR* UpperName, const TArray<TSharedPtr<FJsonValue>>*& OutArray)
	{
		return TryGetArray(Object, LowerName, OutArray) || TryGetArray(Object, UpperName, OutArray);
	}

	FGuid MakeStateGuid(const FString& Id, const FString& Name)
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
		return FGuid::NewDeterministicGuid(TEXT("AssetFactory.StateTree.State.") + Name);
	}

	bool ParseEnumByName(const FString& Value, UEnum* Enum, int64& OutValue)
	{
		if (!Enum)
		{
			return false;
		}
		for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
		{
			if (Enum->HasMetaData(TEXT("Hidden"), Index))
			{
				continue;
			}

			const FString NameString = Enum->GetNameStringByIndex(Index);
			if (NameString.Equals(Value, ESearchCase::IgnoreCase))
			{
				OutValue = Enum->GetValueByIndex(Index);
				return true;
			}
		}
		return false;
	}

	bool ParseStateType(const TSharedPtr<FJsonObject>& StateJson, EStateTreeStateType& OutType, FString& OutError)
	{
		OutType = EStateTreeStateType::State;
		FString TypeString;
		if (!StateJson->TryGetStringField(TEXT("type"), TypeString) && !StateJson->TryGetStringField(TEXT("Type"), TypeString))
		{
			return true;
		}

		int64 RawValue = 0;
		if (!ParseEnumByName(TypeString, StaticEnum<EStateTreeStateType>(), RawValue))
		{
			OutError = FString::Printf(TEXT("Unknown StateTree state type: %s"), *TypeString);
			return false;
		}
		OutType = static_cast<EStateTreeStateType>(RawValue);
		return true;
	}

	bool ParseTaskCompletion(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, EStateTreeTaskCompletionType& OutValue, FString& OutError)
	{
		FString ValueString;
		if (!Object->TryGetStringField(FieldName, ValueString))
		{
			return true;
		}

		int64 RawValue = 0;
		if (!ParseEnumByName(ValueString, StaticEnum<EStateTreeTaskCompletionType>(), RawValue))
		{
			OutError = FString::Printf(TEXT("Unknown StateTree task completion value: %s"), *ValueString);
			return false;
		}
		OutValue = static_cast<EStateTreeTaskCompletionType>(RawValue);
		return true;
	}

	bool ParseNodeArray(
		UObject* Outer,
		const UStateTreeSchema& Schema,
		const TSharedPtr<FJsonObject>& OwnerJson,
		const TCHAR* LowerFieldName,
		const TCHAR* UpperFieldName,
		EAFStateTreeNodeKind Kind,
		TArray<FStateTreeEditorNode>& OutNodes,
		FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!TryGetNamedArray(OwnerJson, LowerFieldName, UpperFieldName, Array))
		{
			return true;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Array)
		{
			TSharedPtr<FJsonObject> NodeObject = Value.IsValid() ? Value->AsObject() : nullptr;
			FAFStateTreeNodeSpec Spec;
			if (!UE::AssetFactory::StateTree::ParseNodeSpec(NodeObject, Kind, Spec, OutError))
			{
				return false;
			}

			FStateTreeEditorNode EditorNode;
			if (!UE::AssetFactory::StateTree::BuildEditorNode(Outer, Schema, Spec, EditorNode, OutError))
			{
				return false;
			}
			OutNodes.Add(MoveTemp(EditorNode));
		}

		return true;
	}

	bool ContainsUnsupportedStructure(const TSharedPtr<FJsonObject>& StateJson, FString& OutError)
	{
		if (StateJson->HasField(TEXT("transitions")) || StateJson->HasField(TEXT("Transitions")))
		{
			OutError = TEXT("StateTree transitions input is not supported by the dynamic nodes spec");
			return true;
		}
		if (StateJson->HasField(TEXT("linkedState")) || StateJson->HasField(TEXT("LinkedState"))
			|| StateJson->HasField(TEXT("linkedSubtree")) || StateJson->HasField(TEXT("LinkedSubtree"))
			|| StateJson->HasField(TEXT("linkedAsset")) || StateJson->HasField(TEXT("LinkedAsset")))
		{
			OutError = TEXT("StateTree linked states/assets input is not supported by the dynamic nodes spec");
			return true;
		}
		return false;
	}

	bool ApplyStateProperties(UStateTreeState& State, const TSharedPtr<FJsonObject>& StateJson, FString& OutError)
	{
		FString Name;
		if (!StateJson->TryGetStringField(TEXT("name"), Name) && !StateJson->TryGetStringField(TEXT("Name"), Name))
		{
			OutError = TEXT("StateTree state is missing required field 'name'");
			return false;
		}
		if (Name.IsEmpty())
		{
			OutError = TEXT("StateTree state field 'name' must be non-empty");
			return false;
		}

		FString Id;
		StateJson->TryGetStringField(TEXT("id"), Id);
		StateJson->TryGetStringField(TEXT("ID"), Id);

		EStateTreeStateType StateType = EStateTreeStateType::State;
		if (!ParseStateType(StateJson, StateType, OutError))
		{
			return false;
		}

		State.Name = FName(*Name);
		State.ID = MakeStateGuid(Id, Name);
		State.Type = StateType;

		return ParseTaskCompletion(StateJson, TEXT("tasksCompletion"), State.TasksCompletion, OutError)
			&& ParseTaskCompletion(StateJson, TEXT("TasksCompletion"), State.TasksCompletion, OutError);
	}

	bool BuildStateRecursive(
		UStateTreeEditorData& EditorData,
		UStateTreeState& State,
		const TSharedPtr<FJsonObject>& StateJson,
		FString& OutError)
	{
		if (!StateJson.IsValid())
		{
			OutError = TEXT("StateTree state entry must be a JSON object");
			return false;
		}
		if (ContainsUnsupportedStructure(StateJson, OutError))
		{
			return false;
		}
		if (!EditorData.Schema)
		{
			OutError = TEXT("StateTree editor data has no schema instance");
			return false;
		}

		if (!ApplyStateProperties(State, StateJson, OutError))
		{
			return false;
		}

		TArray<FStateTreeEditorNode> Tasks;
		if (!ParseNodeArray(&State, *EditorData.Schema, StateJson, TEXT("tasks"), TEXT("Tasks"), EAFStateTreeNodeKind::Task, Tasks, OutError))
		{
			return false;
		}

		if (!EditorData.Schema->AllowMultipleTasks())
		{
			if (Tasks.Num() > 1)
			{
				OutError = FString::Printf(TEXT("StateTree schema '%s' does not allow multiple tasks in state '%s'"), *EditorData.Schema->GetClass()->GetPathName(), *State.Name.ToString());
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

		State.EnterConditions.Reset();
		if (!ParseNodeArray(&State, *EditorData.Schema, StateJson, TEXT("enterConditions"), TEXT("EnterConditions"), EAFStateTreeNodeKind::EnterCondition, State.EnterConditions, OutError))
		{
			return false;
		}

		State.Considerations.Reset();
		if (!ParseNodeArray(&State, *EditorData.Schema, StateJson, TEXT("considerations"), TEXT("Considerations"), EAFStateTreeNodeKind::Consideration, State.Considerations, OutError))
		{
			return false;
		}

		State.Children.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (TryGetNamedArray(StateJson, TEXT("children"), TEXT("Children"), Children))
		{
			for (const TSharedPtr<FJsonValue>& ChildValue : *Children)
			{
				TSharedPtr<FJsonObject> ChildJson = ChildValue.IsValid() ? ChildValue->AsObject() : nullptr;
				FString ChildName;
				if (!ChildJson.IsValid() || (!ChildJson->TryGetStringField(TEXT("name"), ChildName) && !ChildJson->TryGetStringField(TEXT("Name"), ChildName)))
				{
					OutError = FString::Printf(TEXT("StateTree child of state '%s' must be an object with a non-empty name"), *State.Name.ToString());
					return false;
				}
				UStateTreeState& ChildState = State.AddChildState(FName(*ChildName));
				if (!BuildStateRecursive(EditorData, ChildState, ChildJson, OutError))
				{
					return false;
				}
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

	EditorData.Evaluators.Reset();
	if (!ParseNodeArray(&EditorData, *EditorData.Schema, Config, TEXT("evaluators"), TEXT("Evaluators"), EAFStateTreeNodeKind::Evaluator, EditorData.Evaluators, OutError))
	{
		return false;
	}

	EditorData.GlobalTasks.Reset();
	if (!ParseNodeArray(&EditorData, *EditorData.Schema, Config, TEXT("globalTasks"), TEXT("GlobalTasks"), EAFStateTreeNodeKind::GlobalTask, EditorData.GlobalTasks, OutError))
	{
		return false;
	}

	if (!ParseTaskCompletion(Config, TEXT("GlobalTasksCompletion"), EditorData.GlobalTasksCompletion, OutError)
		|| !ParseTaskCompletion(Config, TEXT("globalTasksCompletion"), EditorData.GlobalTasksCompletion, OutError))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* SubTrees = nullptr;
	if (!TryGetNamedArray(Config, TEXT("subTrees"), TEXT("SubTrees"), SubTrees))
	{
		return true;
	}

	EditorData.SubTrees.Reset();
	for (const TSharedPtr<FJsonValue>& SubTreeValue : *SubTrees)
	{
		TSharedPtr<FJsonObject> SubTreeJson = SubTreeValue.IsValid() ? SubTreeValue->AsObject() : nullptr;
		FString SubTreeName;
		if (!SubTreeJson.IsValid() || (!SubTreeJson->TryGetStringField(TEXT("name"), SubTreeName) && !SubTreeJson->TryGetStringField(TEXT("Name"), SubTreeName)) || SubTreeName.IsEmpty())
		{
			OutError = TEXT("SubTrees must be an array of state objects with non-empty names");
			return false;
		}

		UStateTreeState& SubTree = EditorData.AddSubTree(FName(*SubTreeName));
		if (!BuildStateRecursive(EditorData, SubTree, SubTreeJson, OutError))
		{
			return false;
		}
	}

	return true;
}
