// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "StateTreeTypes.h"

enum class EAFStateTreeNodeKind : uint8
{
	Evaluator,
	GlobalTask,
	Task,
	EnterCondition,
	TransitionCondition,
	Consideration,
};

struct FAFStateTreeNodeSpec
{
	FString Id;
	EAFStateTreeNodeKind Kind = EAFStateTreeNodeKind::Task;
	FString Type;
	TSharedPtr<FJsonObject> NodeProperties;
	TSharedPtr<FJsonObject> InstanceProperties;
	TSharedPtr<FJsonObject> ExecutionRuntimeDataProperties;
	int32 ExpressionIndent = 0;
	FString ExpressionOperand = TEXT("And");
};

namespace UE::AssetFactory::StateTree
{
	inline bool TryParseNodeKind(const FString& Value, EAFStateTreeNodeKind& OutKind)
	{
		if (Value.Equals(TEXT("evaluator"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeNodeKind::Evaluator;
			return true;
		}
		if (Value.Equals(TEXT("globalTask"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("global_task"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeNodeKind::GlobalTask;
			return true;
		}
		if (Value.Equals(TEXT("task"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeNodeKind::Task;
			return true;
		}
		if (Value.Equals(TEXT("enterCondition"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("enter_condition"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeNodeKind::EnterCondition;
			return true;
		}
		if (Value.Equals(TEXT("transitionCondition"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("transition_condition"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeNodeKind::TransitionCondition;
			return true;
		}
		if (Value.Equals(TEXT("consideration"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeNodeKind::Consideration;
			return true;
		}
		return false;
	}

	inline FString NodeKindToString(EAFStateTreeNodeKind Kind)
	{
		switch (Kind)
		{
		case EAFStateTreeNodeKind::Evaluator:
			return TEXT("evaluator");
		case EAFStateTreeNodeKind::GlobalTask:
			return TEXT("globalTask");
		case EAFStateTreeNodeKind::Task:
			return TEXT("task");
		case EAFStateTreeNodeKind::EnterCondition:
			return TEXT("enterCondition");
		case EAFStateTreeNodeKind::TransitionCondition:
			return TEXT("transitionCondition");
		case EAFStateTreeNodeKind::Consideration:
			return TEXT("consideration");
		default:
			return TEXT("unknown");
		}
	}

	inline bool TryGetObject(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, TSharedPtr<FJsonObject>& OutObject)
	{
		OutObject.Reset();
		if (!Object.IsValid() || !Object->HasTypedField<EJson::Object>(FieldName))
		{
			return false;
		}
		OutObject = Object->GetObjectField(FieldName);
		return OutObject.IsValid();
	}

	inline TSharedPtr<FJsonObject> ReadPropertiesObject(const TSharedPtr<FJsonObject>& Object)
	{
		TSharedPtr<FJsonObject> Properties;
		if (TryGetObject(Object, TEXT("properties"), Properties) || TryGetObject(Object, TEXT("Properties"), Properties))
		{
			return Properties;
		}
		return nullptr;
	}

	inline bool ParseNodeSpec(
		TSharedPtr<FJsonObject> NodeObject,
		EAFStateTreeNodeKind ExpectedKind,
		FAFStateTreeNodeSpec& OutSpec,
		FString& OutError)
	{
		OutSpec = FAFStateTreeNodeSpec();
		OutSpec.Kind = ExpectedKind;

		if (!NodeObject.IsValid())
		{
			OutError = TEXT("StateTree node entry must be a JSON object");
			return false;
		}

		if (NodeObject->HasField(TEXT("id")))
		{
			NodeObject->TryGetStringField(TEXT("id"), OutSpec.Id);
		}
		else if (NodeObject->HasField(TEXT("ID")))
		{
			NodeObject->TryGetStringField(TEXT("ID"), OutSpec.Id);
		}

		if (NodeObject->HasField(TEXT("kind")))
		{
			FString KindString;
			if (!NodeObject->TryGetStringField(TEXT("kind"), KindString) || !TryParseNodeKind(KindString, OutSpec.Kind))
			{
				OutError = FString::Printf(TEXT("Unknown StateTree node kind: %s"), *KindString);
				return false;
			}
			if (OutSpec.Kind != ExpectedKind)
			{
				OutError = FString::Printf(TEXT("StateTree node kind '%s' cannot be used in '%s' slot"), *NodeKindToString(OutSpec.Kind), *NodeKindToString(ExpectedKind));
				return false;
			}
		}

		if (!NodeObject->TryGetStringField(TEXT("type"), OutSpec.Type) && !NodeObject->TryGetStringField(TEXT("Type"), OutSpec.Type))
		{
			OutError = TEXT("StateTree node is missing required field 'type'");
			return false;
		}
		if (OutSpec.Type.IsEmpty())
		{
			OutError = TEXT("StateTree node field 'type' must be non-empty");
			return false;
		}

		TSharedPtr<FJsonObject> NodeSection;
		if (TryGetObject(NodeObject, TEXT("node"), NodeSection) || TryGetObject(NodeObject, TEXT("Node"), NodeSection))
		{
			OutSpec.NodeProperties = ReadPropertiesObject(NodeSection);
		}

		TSharedPtr<FJsonObject> InstanceSection;
		if (TryGetObject(NodeObject, TEXT("instance"), InstanceSection) || TryGetObject(NodeObject, TEXT("Instance"), InstanceSection))
		{
			OutSpec.InstanceProperties = ReadPropertiesObject(InstanceSection);
		}

		TSharedPtr<FJsonObject> RuntimeSection;
		if (TryGetObject(NodeObject, TEXT("executionRuntimeData"), RuntimeSection) || TryGetObject(NodeObject, TEXT("ExecutionRuntimeData"), RuntimeSection))
		{
			OutSpec.ExecutionRuntimeDataProperties = ReadPropertiesObject(RuntimeSection);
		}

		TSharedPtr<FJsonObject> AliasProperties;
		if (TryGetObject(NodeObject, TEXT("properties"), AliasProperties) || TryGetObject(NodeObject, TEXT("Properties"), AliasProperties))
		{
			if (OutSpec.InstanceProperties.IsValid())
			{
				OutError = TEXT("StateTree node cannot define both 'properties' and 'instance.properties'");
				return false;
			}
			OutSpec.InstanceProperties = AliasProperties;
		}

		TSharedPtr<FJsonObject> ExpressionObject;
		if (TryGetObject(NodeObject, TEXT("expression"), ExpressionObject) || TryGetObject(NodeObject, TEXT("Expression"), ExpressionObject))
		{
			double Indent = 0.0;
			if (ExpressionObject->TryGetNumberField(TEXT("indent"), Indent) || ExpressionObject->TryGetNumberField(TEXT("Indent"), Indent))
			{
				OutSpec.ExpressionIndent = static_cast<int32>(Indent);
			}
			ExpressionObject->TryGetStringField(TEXT("operand"), OutSpec.ExpressionOperand);
			ExpressionObject->TryGetStringField(TEXT("Operand"), OutSpec.ExpressionOperand);
		}

		return true;
	}

	inline bool TryParseExpressionOperand(const FString& Value, EStateTreeExpressionOperand& OutOperand)
	{
		if (Value.Equals(TEXT("Copy"), ESearchCase::IgnoreCase))
		{
			OutOperand = EStateTreeExpressionOperand::Copy;
			return true;
		}
		if (Value.IsEmpty() || Value.Equals(TEXT("And"), ESearchCase::IgnoreCase))
		{
			OutOperand = EStateTreeExpressionOperand::And;
			return true;
		}
		if (Value.Equals(TEXT("Or"), ESearchCase::IgnoreCase))
		{
			OutOperand = EStateTreeExpressionOperand::Or;
			return true;
		}
		if (Value.Equals(TEXT("Multiply"), ESearchCase::IgnoreCase))
		{
			OutOperand = EStateTreeExpressionOperand::Multiply;
			return true;
		}
		return false;
	}

	inline FString ExpressionOperandToString(EStateTreeExpressionOperand Operand)
	{
		switch (Operand)
		{
		case EStateTreeExpressionOperand::Copy:
			return TEXT("Copy");
		case EStateTreeExpressionOperand::And:
			return TEXT("And");
		case EStateTreeExpressionOperand::Or:
			return TEXT("Or");
		case EStateTreeExpressionOperand::Multiply:
			return TEXT("Multiply");
		default:
			return TEXT("And");
		}
	}
}
