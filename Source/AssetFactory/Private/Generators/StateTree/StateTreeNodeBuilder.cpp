// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTree/StateTreeNodeBuilder.h"

#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "Blueprint/StateTreeConsiderationBlueprintBase.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Dom/JsonValue.h"
#include "Generators/StateTree/StateTreeClassResolver.h"
#include "Generators/StateTree/StateTreeValidation.h"
#include "StateTreeEditorNode.h"
#include "StateTreeNodeBase.h"
#include "StateTreeSchema.h"
#include "Utils/PropertySetterUtils.h"

namespace
{
	FGuid MakeNodeGuid(const FAFStateTreeNodeSpec& Spec)
	{
		if (!Spec.Id.IsEmpty())
		{
			return FGuid::NewDeterministicGuid(FString::Printf(
				TEXT("AssetFactory.StateTree.Node.%s.%s.%s"),
				*UE::AssetFactory::StateTree::NodeKindToString(Spec.Kind),
				*Spec.Type,
				*Spec.Id));
		}
		return FGuid::NewGuid();
	}

	bool ApplyStructProperties(
		UScriptStruct* Struct,
		void* Memory,
		const TSharedPtr<FJsonObject>& Properties,
		const TCHAR* Label,
		FString& OutError)
	{
		if (!Properties.IsValid())
		{
			return true;
		}
		if (!Struct || !Memory)
		{
			OutError = FString::Printf(TEXT("StateTree %s has no struct memory for provided properties"), Label);
			return false;
		}
		if (!FPropertySetterUtils::SetStructFromJson(Struct, Memory, MakeShared<FJsonValueObject>(Properties)))
		{
			OutError = FString::Printf(TEXT("Failed to apply StateTree %s properties"), Label);
			return false;
		}
		return true;
	}

	bool ApplyObjectProperties(
		UObject* Object,
		const TSharedPtr<FJsonObject>& Properties,
		const TCHAR* Label,
		FString& OutError)
	{
		if (!Properties.IsValid())
		{
			return true;
		}
		if (!Object)
		{
			OutError = FString::Printf(TEXT("StateTree %s has no object instance for provided properties"), Label);
			return false;
		}
		if (!FPropertySetterUtils::SetPropertiesFromJson(Object, Properties))
		{
			OutError = FString::Printf(TEXT("Failed to apply StateTree %s properties"), Label);
			return false;
		}
		return true;
	}

	bool InitializeDataView(
		UObject* Outer,
		const UStruct* DataType,
		FInstancedStruct& StructData,
		TObjectPtr<UObject>& ObjectData,
		const TSharedPtr<FJsonObject>& Properties,
		const TCHAR* Label,
		FString& OutError)
	{
		if (const UScriptStruct* ScriptStruct = Cast<UScriptStruct>(DataType))
		{
			StructData.InitializeAs(ScriptStruct);
			return ApplyStructProperties(const_cast<UScriptStruct*>(ScriptStruct), StructData.GetMutableMemory(), Properties, Label, OutError);
		}
		if (const UClass* ObjectClass = Cast<UClass>(DataType))
		{
			ObjectData = NewObject<UObject>(Outer, ObjectClass, NAME_None, RF_Transactional);
			return ApplyObjectProperties(ObjectData, Properties, Label, OutError);
		}

		if (Properties.IsValid())
		{
			OutError = FString::Printf(TEXT("StateTree %s properties were provided, but the node does not expose %s data"), Label, Label);
			return false;
		}

		return true;
	}

	bool ApplyExpressionMetadata(const FAFStateTreeNodeSpec& Spec, FStateTreeEditorNode& OutNode, FString& OutError)
	{
		if (Spec.Kind != EAFStateTreeNodeKind::EnterCondition
			&& Spec.Kind != EAFStateTreeNodeKind::TransitionCondition
			&& Spec.Kind != EAFStateTreeNodeKind::Consideration)
		{
			return true;
		}

		EStateTreeExpressionOperand Operand = EStateTreeExpressionOperand::And;
		if (!UE::AssetFactory::StateTree::TryParseExpressionOperand(Spec.ExpressionOperand, Operand))
		{
			OutError = FString::Printf(TEXT("Unknown StateTree expression operand: %s"), *Spec.ExpressionOperand);
			return false;
		}

		OutNode.ExpressionIndent = static_cast<uint8>(FMath::Clamp(Spec.ExpressionIndent, 0, UE::StateTree::MaxExpressionIndent));
		OutNode.ExpressionOperand = Operand;
		return true;
	}

	UScriptStruct* GetWrapperStruct(EAFStateTreeNodeKind Kind)
	{
		switch (Kind)
		{
		case EAFStateTreeNodeKind::Evaluator:
			return FStateTreeBlueprintEvaluatorWrapper::StaticStruct();
		case EAFStateTreeNodeKind::GlobalTask:
		case EAFStateTreeNodeKind::Task:
			return FStateTreeBlueprintTaskWrapper::StaticStruct();
		case EAFStateTreeNodeKind::EnterCondition:
		case EAFStateTreeNodeKind::TransitionCondition:
			return FStateTreeBlueprintConditionWrapper::StaticStruct();
		case EAFStateTreeNodeKind::Consideration:
			return FStateTreeBlueprintConsiderationWrapper::StaticStruct();
		default:
			return nullptr;
		}
	}

	bool BuildStructNode(
		UObject* Outer,
		const FAFStateTreeNodeSpec& Spec,
		UScriptStruct* Struct,
		FStateTreeEditorNode& OutNode,
		FString& OutError)
	{
		OutNode.Reset();
		OutNode.ID = MakeNodeGuid(Spec);
		OutNode.Node.InitializeAs(Struct);

		if (!ApplyStructProperties(Struct, OutNode.Node.GetMutableMemory(), Spec.NodeProperties, TEXT("node"), OutError))
		{
			return false;
		}

		FStateTreeNodeBase& NodeBase = OutNode.Node.GetMutable<FStateTreeNodeBase>();
		if (!InitializeDataView(Outer, NodeBase.GetInstanceDataType(), OutNode.Instance, OutNode.InstanceObject, Spec.InstanceProperties, TEXT("instance"), OutError))
		{
			return false;
		}
		if (!InitializeDataView(Outer, NodeBase.GetExecutionRuntimeDataType(), OutNode.ExecutionRuntimeData, OutNode.ExecutionRuntimeDataObject, Spec.ExecutionRuntimeDataProperties, TEXT("executionRuntimeData"), OutError))
		{
			return false;
		}

		return ApplyExpressionMetadata(Spec, OutNode, OutError);
	}

	bool BuildBlueprintNode(
		UObject* Outer,
		const FAFStateTreeNodeSpec& Spec,
		UClass* NodeClass,
		FStateTreeEditorNode& OutNode,
		FString& OutError)
	{
		UScriptStruct* WrapperStruct = GetWrapperStruct(Spec.Kind);
		if (!WrapperStruct)
		{
			OutError = FString::Printf(TEXT("No Blueprint wrapper is available for StateTree node kind '%s'"), *UE::AssetFactory::StateTree::NodeKindToString(Spec.Kind));
			return false;
		}

		OutNode.Reset();
		OutNode.ID = MakeNodeGuid(Spec);
		OutNode.Node.InitializeAs(WrapperStruct);

		if (FStateTreeBlueprintTaskWrapper* Task = OutNode.Node.GetMutablePtr<FStateTreeBlueprintTaskWrapper>())
		{
			Task->TaskClass = NodeClass;
		}
		else if (FStateTreeBlueprintEvaluatorWrapper* Evaluator = OutNode.Node.GetMutablePtr<FStateTreeBlueprintEvaluatorWrapper>())
		{
			Evaluator->EvaluatorClass = NodeClass;
		}
		else if (FStateTreeBlueprintConditionWrapper* Condition = OutNode.Node.GetMutablePtr<FStateTreeBlueprintConditionWrapper>())
		{
			Condition->ConditionClass = NodeClass;
		}
		else if (FStateTreeBlueprintConsiderationWrapper* Consideration = OutNode.Node.GetMutablePtr<FStateTreeBlueprintConsiderationWrapper>())
		{
			Consideration->ConsiderationClass = NodeClass;
		}

		if (!ApplyStructProperties(WrapperStruct, OutNode.Node.GetMutableMemory(), Spec.NodeProperties, TEXT("node"), OutError))
		{
			return false;
		}

		OutNode.InstanceObject = NewObject<UObject>(Outer, NodeClass, NAME_None, RF_Transactional);
		if (!ApplyObjectProperties(OutNode.InstanceObject, Spec.InstanceProperties, TEXT("instance"), OutError))
		{
			return false;
		}

		FStateTreeNodeBase& NodeBase = OutNode.Node.GetMutable<FStateTreeNodeBase>();
		if (!InitializeDataView(Outer, NodeBase.GetExecutionRuntimeDataType(), OutNode.ExecutionRuntimeData, OutNode.ExecutionRuntimeDataObject, Spec.ExecutionRuntimeDataProperties, TEXT("executionRuntimeData"), OutError))
		{
			return false;
		}

		return ApplyExpressionMetadata(Spec, OutNode, OutError);
	}
}

bool UE::AssetFactory::StateTree::BuildEditorNode(
	UObject* Outer,
	const UStateTreeSchema& Schema,
	const FAFStateTreeNodeSpec& Spec,
	FStateTreeEditorNode& OutNode,
	FString& OutError)
{
	if (!Outer)
	{
		OutError = TEXT("StateTree editor node outer is null");
		return false;
	}

	if (!ValidateNodeSpec(Schema, Spec, OutError))
	{
		return false;
	}

	if (UScriptStruct* Struct = ResolveScriptStruct(Spec.Type))
	{
		return BuildStructNode(Outer, Spec, Struct, OutNode, OutError);
	}

	if (UClass* Class = ResolveNodeClass(Spec.Type))
	{
		return BuildBlueprintNode(Outer, Spec, Class, OutNode, OutError);
	}

	OutError = FString::Printf(TEXT("Unknown StateTree node type: %s"), *Spec.Type);
	return false;
}
