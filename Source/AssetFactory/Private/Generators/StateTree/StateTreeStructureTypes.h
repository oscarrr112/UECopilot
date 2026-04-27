// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Generators/StateTree/StateTreeJsonTypes.h"
#include "GameplayTagContainer.h"
#include "StateTreeState.h"
#include "StateTreeTasksStatus.h"
#include "StateTreeTypes.h"

struct FAFStateTreeTransitionDelaySpec
{
	bool bEnabled = false;
	float Duration = 0.0f;
	float RandomVariance = 0.0f;
};

struct FAFStateTreeTransitionSpec
{
	FString Id;
	EStateTreeTransitionTrigger Trigger = EStateTreeTransitionTrigger::OnStateCompleted;
	EStateTreeTransitionType Type = EStateTreeTransitionType::Succeeded;
	FString Target;
	EStateTreeTransitionPriority Priority = EStateTreeTransitionPriority::Normal;
	bool bEnabled = true;
	FAFStateTreeTransitionDelaySpec Delay;
	FGameplayTag RequiredEventTag;
	TArray<FAFStateTreeNodeSpec> Conditions;
};

struct FAFStateTreeCustomTickRateSpec
{
	bool bTouched = false;
	bool bEnabled = false;
	float Value = 0.0f;
};

struct FAFStateTreeStateSpec
{
	FString Id;
	FString Name;
	FString CanonicalPath;
	EStateTreeStateType Type = EStateTreeStateType::State;
	TOptional<EStateTreeStateSelectionBehavior> SelectionBehavior;
	TOptional<EStateTreeTaskCompletionType> TasksCompletion;
	FGameplayTag Tag;
	FString Description;
	TOptional<bool> bEnabled;
	FAFStateTreeCustomTickRateSpec CustomTickRate;
	FString LinkedState;
	FString LinkedSubtree;
	FString LinkedAsset;
	TArray<FAFStateTreeNodeSpec> Tasks;
	TArray<FAFStateTreeNodeSpec> EnterConditions;
	TArray<FAFStateTreeNodeSpec> Considerations;
	TArray<FAFStateTreeTransitionSpec> Transitions;
	TArray<FAFStateTreeStateSpec> Children;
};

namespace UE::AssetFactory::StateTree
{
	namespace StructureJson
	{
		inline bool TryGetStringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* LowerName, const TCHAR* UpperName, FString& OutValue)
		{
			return Object.IsValid() && (Object->TryGetStringField(LowerName, OutValue) || Object->TryGetStringField(UpperName, OutValue));
		}

		inline bool TryGetBoolField(const TSharedPtr<FJsonObject>& Object, const TCHAR* LowerName, const TCHAR* UpperName, bool& OutValue)
		{
			return Object.IsValid() && (Object->TryGetBoolField(LowerName, OutValue) || Object->TryGetBoolField(UpperName, OutValue));
		}

		inline bool TryGetNumberField(const TSharedPtr<FJsonObject>& Object, const TCHAR* LowerName, const TCHAR* UpperName, double& OutValue)
		{
			return Object.IsValid() && (Object->TryGetNumberField(LowerName, OutValue) || Object->TryGetNumberField(UpperName, OutValue));
		}

		inline bool TryGetObjectField(const TSharedPtr<FJsonObject>& Object, const TCHAR* LowerName, const TCHAR* UpperName, TSharedPtr<FJsonObject>& OutValue)
		{
			return TryGetObject(Object, LowerName, OutValue) || TryGetObject(Object, UpperName, OutValue);
		}

		inline bool TryGetArrayField(const TSharedPtr<FJsonObject>& Object, const TCHAR* LowerName, const TCHAR* UpperName, const TArray<TSharedPtr<FJsonValue>>*& OutArray)
		{
			OutArray = nullptr;
			return Object.IsValid()
				&& ((Object->TryGetArrayField(LowerName, OutArray) && OutArray)
					|| (Object->TryGetArrayField(UpperName, OutArray) && OutArray));
		}

		inline bool ReadOptionalArrayField(
			const TSharedPtr<FJsonObject>& Object,
			const TCHAR* LowerName,
			const TCHAR* UpperName,
			const TArray<TSharedPtr<FJsonValue>>*& OutArray,
			FString& OutError)
		{
			OutArray = nullptr;
			if (!Object.IsValid())
			{
				return true;
			}
			if (!Object->HasField(LowerName) && !Object->HasField(UpperName))
			{
				return true;
			}
			if (TryGetArrayField(Object, LowerName, UpperName, OutArray))
			{
				return true;
			}
			OutError = FString::Printf(TEXT("StateTree field '%s' must be an array"), LowerName);
			return false;
		}

		inline bool TryParseTag(const FString& TagString, FGameplayTag& OutTag)
		{
			OutTag = FGameplayTag();
			if (TagString.IsEmpty())
			{
				return true;
			}
			OutTag = FGameplayTag::RequestGameplayTag(FName(*TagString), false);
			return OutTag.IsValid();
		}
	}

	inline bool TryParseStateType(const FString& Value, EStateTreeStateType& OutType)
	{
		if (Value.Equals(TEXT("State"), ESearchCase::IgnoreCase))
		{
			OutType = EStateTreeStateType::State;
			return true;
		}
		if (Value.Equals(TEXT("Group"), ESearchCase::IgnoreCase))
		{
			OutType = EStateTreeStateType::Group;
			return true;
		}
		if (Value.Equals(TEXT("Linked"), ESearchCase::IgnoreCase))
		{
			OutType = EStateTreeStateType::Linked;
			return true;
		}
		if (Value.Equals(TEXT("LinkedAsset"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("LinkedAssetState"), ESearchCase::IgnoreCase))
		{
			OutType = EStateTreeStateType::LinkedAsset;
			return true;
		}
		if (Value.Equals(TEXT("Subtree"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("SubTree"), ESearchCase::IgnoreCase))
		{
			OutType = EStateTreeStateType::Subtree;
			return true;
		}
		return false;
	}

	inline bool TryParseSelectionBehavior(const FString& Value, EStateTreeStateSelectionBehavior& OutValue)
	{
		if (Value.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeStateSelectionBehavior::None;
			return true;
		}
		if (Value.Equals(TEXT("TryEnterState"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("TryEnter"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeStateSelectionBehavior::TryEnterState;
			return true;
		}
		if (Value.Equals(TEXT("TrySelectChildrenInOrder"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("ChildrenInOrder"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
			return true;
		}
		if (Value.Equals(TEXT("TrySelectChildrenAtRandom"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("ChildrenAtRandom"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandom;
			return true;
		}
		if (Value.Equals(TEXT("TrySelectChildrenWithHighestUtility"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("HighestUtility"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility;
			return true;
		}
		if (Value.Equals(TEXT("TrySelectChildrenAtRandomWeightedByUtility"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("WeightedRandom"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandomWeightedByUtility;
			return true;
		}
		if (Value.Equals(TEXT("TryFollowTransitions"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("FollowTransitions"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeStateSelectionBehavior::TryFollowTransitions;
			return true;
		}
		return false;
	}

	inline bool TryParseTaskCompletionType(const FString& Value, EStateTreeTaskCompletionType& OutValue)
	{
		if (Value.Equals(TEXT("Any"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTaskCompletionType::Any;
			return true;
		}
		if (Value.Equals(TEXT("All"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTaskCompletionType::All;
			return true;
		}
		return false;
	}

	inline bool TryParseTransitionTrigger(const FString& Value, EStateTreeTransitionTrigger& OutValue)
	{
		if (Value.Equals(TEXT("OnStateCompleted"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("Completed"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTransitionTrigger::OnStateCompleted;
			return true;
		}
		if (Value.Equals(TEXT("OnStateSucceeded"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("Succeeded"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTransitionTrigger::OnStateSucceeded;
			return true;
		}
		if (Value.Equals(TEXT("OnStateFailed"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("Failed"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTransitionTrigger::OnStateFailed;
			return true;
		}
		if (Value.Equals(TEXT("OnTick"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("Tick"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTransitionTrigger::OnTick;
			return true;
		}
		if (Value.Equals(TEXT("OnEvent"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTransitionTrigger::OnEvent;
			return true;
		}
		if (Value.Equals(TEXT("OnDelegate"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("Delegate"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTransitionTrigger::OnDelegate;
			return true;
		}
		return false;
	}

	inline bool TryParseTransitionType(const FString& Value, EStateTreeTransitionType& OutValue)
	{
		if (Value.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTransitionType::None;
			return true;
		}
		if (Value.Equals(TEXT("Succeeded"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("Success"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTransitionType::Succeeded;
			return true;
		}
		if (Value.Equals(TEXT("Failed"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("Failure"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTransitionType::Failed;
			return true;
		}
		if (Value.Equals(TEXT("GotoState"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("Goto"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTransitionType::GotoState;
			return true;
		}
		if (Value.Equals(TEXT("NextState"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTransitionType::NextState;
			return true;
		}
		if (Value.Equals(TEXT("NextSelectableState"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTransitionType::NextSelectableState;
			return true;
		}
		return false;
	}

	inline bool TryParseTransitionPriority(const FString& Value, EStateTreeTransitionPriority& OutValue)
	{
		if (Value.Equals(TEXT("Low"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTransitionPriority::Low;
			return true;
		}
		if (Value.Equals(TEXT("Normal"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTransitionPriority::Normal;
			return true;
		}
		if (Value.Equals(TEXT("Medium"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTransitionPriority::Medium;
			return true;
		}
		if (Value.Equals(TEXT("High"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTransitionPriority::High;
			return true;
		}
		if (Value.Equals(TEXT("Critical"), ESearchCase::IgnoreCase))
		{
			OutValue = EStateTreeTransitionPriority::Critical;
			return true;
		}
		return false;
	}

	inline bool ParseTransitionDelay(const TSharedPtr<FJsonObject>& TransitionObject, FAFStateTreeTransitionDelaySpec& OutDelay, FString& OutError)
	{
		OutDelay = FAFStateTreeTransitionDelaySpec();
		const TSharedPtr<FJsonValue> DelayValue = TransitionObject->TryGetField(TEXT("delay")).IsValid()
			? TransitionObject->TryGetField(TEXT("delay"))
			: TransitionObject->TryGetField(TEXT("Delay"));
		if (!DelayValue.IsValid() || DelayValue->Type == EJson::Null)
		{
			return true;
		}
		if (DelayValue->Type == EJson::Number)
		{
			OutDelay.bEnabled = true;
			OutDelay.Duration = static_cast<float>(DelayValue->AsNumber());
			return true;
		}
		if (DelayValue->Type != EJson::Object)
		{
			OutError = TEXT("StateTree transition field 'delay' must be a number or object");
			return false;
		}

		TSharedPtr<FJsonObject> DelayObject = DelayValue->AsObject();
		double NumberValue = 0.0;
		bool bBoolValue = false;
		if (StructureJson::TryGetBoolField(DelayObject, TEXT("enabled"), TEXT("Enabled"), bBoolValue))
		{
			OutDelay.bEnabled = bBoolValue;
		}
		if (StructureJson::TryGetNumberField(DelayObject, TEXT("duration"), TEXT("Duration"), NumberValue))
		{
			OutDelay.Duration = static_cast<float>(NumberValue);
			if (!DelayObject->HasField(TEXT("enabled")) && !DelayObject->HasField(TEXT("Enabled")))
			{
				OutDelay.bEnabled = OutDelay.Duration > 0.0f;
			}
		}
		if (StructureJson::TryGetNumberField(DelayObject, TEXT("randomVariance"), TEXT("RandomVariance"), NumberValue))
		{
			OutDelay.RandomVariance = static_cast<float>(NumberValue);
		}
		return true;
	}

	inline bool ParseRequiredEvent(const TSharedPtr<FJsonObject>& TransitionObject, FAFStateTreeTransitionSpec& OutSpec, FString& OutError)
	{
		const TSharedPtr<FJsonValue> EventValue = TransitionObject->TryGetField(TEXT("requiredEvent")).IsValid()
			? TransitionObject->TryGetField(TEXT("requiredEvent"))
			: TransitionObject->TryGetField(TEXT("RequiredEvent"));
		if (!EventValue.IsValid() || EventValue->Type == EJson::Null)
		{
			return true;
		}

		FString TagString;
		if (EventValue->Type == EJson::String)
		{
			TagString = EventValue->AsString();
		}
		else if (EventValue->Type == EJson::Object)
		{
			TSharedPtr<FJsonObject> EventObject = EventValue->AsObject();
			StructureJson::TryGetStringField(EventObject, TEXT("tag"), TEXT("Tag"), TagString);
		}
		else
		{
			OutError = TEXT("StateTree transition field 'requiredEvent' must be a string or object");
			return false;
		}

		if (TagString.IsEmpty())
		{
			OutError = TEXT("StateTree transition requiredEvent must include a non-empty tag");
			return false;
		}
		if (!StructureJson::TryParseTag(TagString, OutSpec.RequiredEventTag))
		{
			OutError = FString::Printf(TEXT("Unknown StateTree transition requiredEvent tag: %s"), *TagString);
			return false;
		}
		return true;
	}

	inline bool ParseTransitionSpec(TSharedPtr<FJsonObject> TransitionObject, FAFStateTreeTransitionSpec& OutSpec, FString& OutError)
	{
		OutSpec = FAFStateTreeTransitionSpec();
		if (!TransitionObject.IsValid())
		{
			OutError = TEXT("StateTree transition entry must be a JSON object");
			return false;
		}

		StructureJson::TryGetStringField(TransitionObject, TEXT("id"), TEXT("ID"), OutSpec.Id);
		StructureJson::TryGetStringField(TransitionObject, TEXT("target"), TEXT("Target"), OutSpec.Target);

		FString TriggerString;
		if (StructureJson::TryGetStringField(TransitionObject, TEXT("trigger"), TEXT("Trigger"), TriggerString))
		{
			if (!TryParseTransitionTrigger(TriggerString, OutSpec.Trigger))
			{
				OutError = FString::Printf(TEXT("Unknown StateTree transition trigger: %s"), *TriggerString);
				return false;
			}
			if (OutSpec.Trigger == EStateTreeTransitionTrigger::OnDelegate)
			{
				OutError = TEXT("StateTree transition trigger 'OnDelegate' is not supported by this spec");
				return false;
			}
		}

		FString TypeString;
		if (StructureJson::TryGetStringField(TransitionObject, TEXT("type"), TEXT("Type"), TypeString))
		{
			if (!TryParseTransitionType(TypeString, OutSpec.Type))
			{
				OutError = FString::Printf(TEXT("Unknown StateTree transition type: %s"), *TypeString);
				return false;
			}
		}
		else if (!OutSpec.Target.IsEmpty())
		{
			OutSpec.Type = EStateTreeTransitionType::GotoState;
		}

		FString PriorityString;
		if (StructureJson::TryGetStringField(TransitionObject, TEXT("priority"), TEXT("Priority"), PriorityString)
			&& !TryParseTransitionPriority(PriorityString, OutSpec.Priority))
		{
			OutError = FString::Printf(TEXT("Unknown StateTree transition priority: %s"), *PriorityString);
			return false;
		}

		bool bEnabled = true;
		if (StructureJson::TryGetBoolField(TransitionObject, TEXT("enabled"), TEXT("Enabled"), bEnabled))
		{
			OutSpec.bEnabled = bEnabled;
		}

		if (!ParseTransitionDelay(TransitionObject, OutSpec.Delay, OutError) || !ParseRequiredEvent(TransitionObject, OutSpec, OutError))
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Conditions = nullptr;
		if (!StructureJson::ReadOptionalArrayField(TransitionObject, TEXT("conditions"), TEXT("Conditions"), Conditions, OutError))
		{
			return false;
		}
		if (Conditions)
		{
			for (const TSharedPtr<FJsonValue>& ConditionValue : *Conditions)
			{
				FAFStateTreeNodeSpec ConditionSpec;
				if (!ParseNodeSpec(ConditionValue.IsValid() ? ConditionValue->AsObject() : nullptr, EAFStateTreeNodeKind::TransitionCondition, ConditionSpec, OutError))
				{
					return false;
				}
				OutSpec.Conditions.Add(MoveTemp(ConditionSpec));
			}
		}

		return true;
	}

	inline bool ParseNodeSpecArray(
		const TSharedPtr<FJsonObject>& OwnerObject,
		const TCHAR* LowerName,
		const TCHAR* UpperName,
		EAFStateTreeNodeKind ExpectedKind,
		TArray<FAFStateTreeNodeSpec>& OutSpecs,
		FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!StructureJson::ReadOptionalArrayField(OwnerObject, LowerName, UpperName, Values, OutError))
		{
			return false;
		}
		if (!Values)
		{
			return true;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FAFStateTreeNodeSpec NodeSpec;
			if (!ParseNodeSpec(Value.IsValid() ? Value->AsObject() : nullptr, ExpectedKind, NodeSpec, OutError))
			{
				return false;
			}
			OutSpecs.Add(MoveTemp(NodeSpec));
		}
		return true;
	}

	inline bool ParseStateSpec(TSharedPtr<FJsonObject> StateObject, const FString& ParentPath, FAFStateTreeStateSpec& OutSpec, FString& OutError)
	{
		OutSpec = FAFStateTreeStateSpec();
		if (!StateObject.IsValid())
		{
			OutError = TEXT("StateTree state entry must be a JSON object");
			return false;
		}

		if (!StructureJson::TryGetStringField(StateObject, TEXT("name"), TEXT("Name"), OutSpec.Name) || OutSpec.Name.IsEmpty())
		{
			OutError = TEXT("StateTree state is missing required non-empty field 'name'");
			return false;
		}
		OutSpec.CanonicalPath = ParentPath.IsEmpty() ? OutSpec.Name : ParentPath + TEXT("/") + OutSpec.Name;
		StructureJson::TryGetStringField(StateObject, TEXT("id"), TEXT("ID"), OutSpec.Id);
		StructureJson::TryGetStringField(StateObject, TEXT("description"), TEXT("Description"), OutSpec.Description);
		StructureJson::TryGetStringField(StateObject, TEXT("linkedState"), TEXT("LinkedState"), OutSpec.LinkedState);
		StructureJson::TryGetStringField(StateObject, TEXT("linkedSubtree"), TEXT("LinkedSubtree"), OutSpec.LinkedSubtree);
		StructureJson::TryGetStringField(StateObject, TEXT("linkedAsset"), TEXT("LinkedAsset"), OutSpec.LinkedAsset);

		FString StringValue;
		if (StructureJson::TryGetStringField(StateObject, TEXT("type"), TEXT("Type"), StringValue)
			&& !TryParseStateType(StringValue, OutSpec.Type))
		{
			OutError = FString::Printf(TEXT("Unknown StateTree state type: %s"), *StringValue);
			return false;
		}
		if (StructureJson::TryGetStringField(StateObject, TEXT("selectionBehavior"), TEXT("SelectionBehavior"), StringValue))
		{
			EStateTreeStateSelectionBehavior SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
			if (!TryParseSelectionBehavior(StringValue, SelectionBehavior))
			{
				OutError = FString::Printf(TEXT("Unknown StateTree state selection behavior: %s"), *StringValue);
				return false;
			}
			OutSpec.SelectionBehavior = SelectionBehavior;
		}
		if (StructureJson::TryGetStringField(StateObject, TEXT("tasksCompletion"), TEXT("TasksCompletion"), StringValue))
		{
			EStateTreeTaskCompletionType CompletionType = EStateTreeTaskCompletionType::Any;
			if (!TryParseTaskCompletionType(StringValue, CompletionType))
			{
				OutError = FString::Printf(TEXT("Unknown StateTree task completion value: %s"), *StringValue);
				return false;
			}
			OutSpec.TasksCompletion = CompletionType;
		}
		if (StructureJson::TryGetStringField(StateObject, TEXT("tag"), TEXT("Tag"), StringValue)
			&& !StructureJson::TryParseTag(StringValue, OutSpec.Tag))
		{
			OutError = FString::Printf(TEXT("Unknown StateTree state tag: %s"), *StringValue);
			return false;
		}
		bool bBoolValue = false;
		if (StructureJson::TryGetBoolField(StateObject, TEXT("enabled"), TEXT("Enabled"), bBoolValue))
		{
			OutSpec.bEnabled = bBoolValue;
		}

		const TSharedPtr<FJsonValue> TickRateValue = StateObject->TryGetField(TEXT("customTickRate")).IsValid()
			? StateObject->TryGetField(TEXT("customTickRate"))
			: StateObject->TryGetField(TEXT("CustomTickRate"));
		if (TickRateValue.IsValid() && TickRateValue->Type != EJson::Null)
		{
			OutSpec.CustomTickRate.bTouched = true;
			if (TickRateValue->Type == EJson::Number)
			{
				OutSpec.CustomTickRate.bEnabled = true;
				OutSpec.CustomTickRate.Value = static_cast<float>(TickRateValue->AsNumber());
			}
			else if (TickRateValue->Type == EJson::Object)
			{
				TSharedPtr<FJsonObject> TickRateObject = TickRateValue->AsObject();
				double NumberValue = 0.0;
				if (StructureJson::TryGetBoolField(TickRateObject, TEXT("enabled"), TEXT("Enabled"), bBoolValue))
				{
					OutSpec.CustomTickRate.bEnabled = bBoolValue;
				}
				if (StructureJson::TryGetNumberField(TickRateObject, TEXT("value"), TEXT("Value"), NumberValue))
				{
					OutSpec.CustomTickRate.Value = static_cast<float>(NumberValue);
					if (!TickRateObject->HasField(TEXT("enabled")) && !TickRateObject->HasField(TEXT("Enabled")))
					{
						OutSpec.CustomTickRate.bEnabled = true;
					}
				}
			}
			else
			{
				OutError = TEXT("StateTree state field 'customTickRate' must be a number or object");
				return false;
			}
		}

		if (!ParseNodeSpecArray(StateObject, TEXT("tasks"), TEXT("Tasks"), EAFStateTreeNodeKind::Task, OutSpec.Tasks, OutError)
			|| !ParseNodeSpecArray(StateObject, TEXT("enterConditions"), TEXT("EnterConditions"), EAFStateTreeNodeKind::EnterCondition, OutSpec.EnterConditions, OutError)
			|| !ParseNodeSpecArray(StateObject, TEXT("considerations"), TEXT("Considerations"), EAFStateTreeNodeKind::Consideration, OutSpec.Considerations, OutError))
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Transitions = nullptr;
		if (!StructureJson::ReadOptionalArrayField(StateObject, TEXT("transitions"), TEXT("Transitions"), Transitions, OutError))
		{
			return false;
		}
		if (Transitions)
		{
			for (const TSharedPtr<FJsonValue>& TransitionValue : *Transitions)
			{
				FAFStateTreeTransitionSpec TransitionSpec;
				if (!ParseTransitionSpec(TransitionValue.IsValid() ? TransitionValue->AsObject() : nullptr, TransitionSpec, OutError))
				{
					return false;
				}
				OutSpec.Transitions.Add(MoveTemp(TransitionSpec));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (!StructureJson::ReadOptionalArrayField(StateObject, TEXT("children"), TEXT("Children"), Children, OutError))
		{
			return false;
		}
		if (Children)
		{
			for (const TSharedPtr<FJsonValue>& ChildValue : *Children)
			{
				FAFStateTreeStateSpec ChildSpec;
				if (!ParseStateSpec(ChildValue.IsValid() ? ChildValue->AsObject() : nullptr, OutSpec.CanonicalPath, ChildSpec, OutError))
				{
					return false;
				}
				OutSpec.Children.Add(MoveTemp(ChildSpec));
			}
		}

		return true;
	}
}
