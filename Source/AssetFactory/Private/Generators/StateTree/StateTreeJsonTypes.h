// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Generators/StateTree/StateTreeBindingTypes.h"
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
	inline bool ParseBindingSpecsFromConfig(
		const TSharedPtr<FJsonObject>& Config,
		TArray<FAFStateTreeBindingSpec>& OutSpecs,
		FString& OutError);

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

	inline bool TryParseBindingEndpointKind(const FString& Value, EAFStateTreeBindingEndpointKind& OutKind)
	{
		if (Value.Equals(TEXT("rootParameter"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("root_parameter"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::RootParameter;
			return true;
		}
		if (Value.Equals(TEXT("stateParameter"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("state_parameter"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::StateParameter;
			return true;
		}
		if (Value.Equals(TEXT("context"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::Context;
			return true;
		}
		if (Value.Equals(TEXT("evaluator"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::Evaluator;
			return true;
		}
		if (Value.Equals(TEXT("globalTask"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("global_task"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::GlobalTask;
			return true;
		}
		if (Value.Equals(TEXT("task"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::Task;
			return true;
		}
		if (Value.Equals(TEXT("enterCondition"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("enter_condition"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::EnterCondition;
			return true;
		}
		if (Value.Equals(TEXT("transitionCondition"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("transition_condition"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::TransitionCondition;
			return true;
		}
		if (Value.Equals(TEXT("consideration"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::Consideration;
			return true;
		}
		if (Value.Equals(TEXT("function"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::Function;
			return true;
		}
		if (Value.Equals(TEXT("node"), ESearchCase::IgnoreCase))
		{
			OutKind = EAFStateTreeBindingEndpointKind::Node;
			return true;
		}
		return false;
	}

	inline bool TryParseBindingDataSection(const FString& Value, EAFStateTreeBindingDataSection& OutSection)
	{
		if (Value.IsEmpty() || Value.Equals(TEXT("instance"), ESearchCase::IgnoreCase))
		{
			OutSection = EAFStateTreeBindingDataSection::Instance;
			return true;
		}
		if (Value.Equals(TEXT("node"), ESearchCase::IgnoreCase))
		{
			OutSection = EAFStateTreeBindingDataSection::Node;
			return true;
		}
		if (Value.Equals(TEXT("executionRuntimeData"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("execution_runtime_data"), ESearchCase::IgnoreCase))
		{
			OutSection = EAFStateTreeBindingDataSection::ExecutionRuntimeData;
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

	inline bool HasBindingField(const TSharedPtr<FJsonObject>& Object, const TCHAR* LowerName, const TCHAR* UpperName)
	{
		return Object.IsValid() && (Object->HasField(LowerName) || Object->HasField(UpperName));
	}

	inline bool TryGetBindingStringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* LowerName, const TCHAR* UpperName, FString& OutValue)
	{
		return Object.IsValid() && (Object->TryGetStringField(LowerName, OutValue) || Object->TryGetStringField(UpperName, OutValue));
	}

	inline bool TryGetBindingObjectField(const TSharedPtr<FJsonObject>& Object, const TCHAR* LowerName, const TCHAR* UpperName, TSharedPtr<FJsonObject>& OutValue)
	{
		OutValue.Reset();
		if (!Object.IsValid())
		{
			return false;
		}
		if (Object->HasTypedField<EJson::Object>(LowerName))
		{
			OutValue = Object->GetObjectField(LowerName);
			return OutValue.IsValid();
		}
		if (Object->HasTypedField<EJson::Object>(UpperName))
		{
			OutValue = Object->GetObjectField(UpperName);
			return OutValue.IsValid();
		}
		return false;
	}

	inline bool TryGetBindingArrayField(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* LowerName,
		const TCHAR* UpperName,
		const TArray<TSharedPtr<FJsonValue>>*& OutArray)
	{
		OutArray = nullptr;
		return Object.IsValid()
			&& ((Object->TryGetArrayField(LowerName, OutArray) && OutArray)
				|| (Object->TryGetArrayField(UpperName, OutArray) && OutArray));
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

	inline bool ParseBindingPathSegments(
		const TArray<TSharedPtr<FJsonValue>>& PathValues,
		const FString& Label,
		TArray<FAFStateTreeBindingPathSegmentSpec>& OutPath,
		FString& OutError)
	{
		OutPath.Reset();
		for (int32 Index = 0; Index < PathValues.Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Value = PathValues[Index];
			FAFStateTreeBindingPathSegmentSpec Segment;
			if (!Value.IsValid())
			{
				OutError = FString::Printf(TEXT("StateTree binding %s path segment %d is null"), *Label, Index);
				return false;
			}
			if (Value->Type == EJson::String)
			{
				Segment.Name = Value->AsString();
			}
			else if (Value->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject>* SegmentObject = nullptr;
				if (!Value->TryGetObject(SegmentObject) || !SegmentObject || !SegmentObject->IsValid())
				{
					OutError = FString::Printf(TEXT("StateTree binding %s path segment %d must be an object"), *Label, Index);
					return false;
				}
				(*SegmentObject)->TryGetStringField(TEXT("name"), Segment.Name);
				(*SegmentObject)->TryGetStringField(TEXT("Name"), Segment.Name);
				double ArrayIndex = static_cast<double>(INDEX_NONE);
				if ((*SegmentObject)->TryGetNumberField(TEXT("arrayIndex"), ArrayIndex) || (*SegmentObject)->TryGetNumberField(TEXT("ArrayIndex"), ArrayIndex))
				{
					Segment.ArrayIndex = static_cast<int32>(ArrayIndex);
				}
				FString GuidString;
				if ((*SegmentObject)->TryGetStringField(TEXT("guid"), GuidString) || (*SegmentObject)->TryGetStringField(TEXT("Guid"), GuidString))
				{
					if (!FGuid::Parse(GuidString, Segment.Guid))
					{
						OutError = FString::Printf(TEXT("StateTree binding %s path segment %d has invalid guid '%s'"), *Label, Index, *GuidString);
						return false;
					}
					Segment.bHasGuid = true;
				}
				(*SegmentObject)->TryGetStringField(TEXT("instanceStruct"), Segment.InstanceStruct);
				(*SegmentObject)->TryGetStringField(TEXT("InstanceStruct"), Segment.InstanceStruct);
				(*SegmentObject)->TryGetStringField(TEXT("access"), Segment.Access);
				(*SegmentObject)->TryGetStringField(TEXT("Access"), Segment.Access);
			}
			else
			{
				OutError = FString::Printf(TEXT("StateTree binding %s path segment %d must be a string or object"), *Label, Index);
				return false;
			}
			if (Segment.Name.IsEmpty())
			{
				OutError = FString::Printf(TEXT("StateTree binding %s path segment %d is missing name"), *Label, Index);
				return false;
			}
			OutPath.Add(MoveTemp(Segment));
		}
		return true;
	}

	inline bool ParseBindingEndpointSpec(
		const TSharedPtr<FJsonObject>& EndpointObject,
		const FString& Label,
		FAFStateTreeBindingEndpointSpec& OutSpec,
		FString& OutError)
	{
		OutSpec = FAFStateTreeBindingEndpointSpec();
		if (!EndpointObject.IsValid())
		{
			OutError = FString::Printf(TEXT("StateTree binding %s endpoint must be a JSON object"), *Label);
			return false;
		}

		if (!HasBindingField(EndpointObject, TEXT("kind"), TEXT("Kind")))
		{
			OutError = FString::Printf(TEXT("StateTree binding %s endpoint is missing required field 'kind'"), *Label);
			return false;
		}

		FString KindString;
		if (!TryGetBindingStringField(EndpointObject, TEXT("kind"), TEXT("Kind"), KindString) || !TryParseBindingEndpointKind(KindString, OutSpec.Kind))
		{
			OutError = FString::Printf(TEXT("StateTree binding %s endpoint has unknown kind '%s'"), *Label, *KindString);
			return false;
		}

		TryGetBindingStringField(EndpointObject, TEXT("state"), TEXT("State"), OutSpec.State);
		TryGetBindingStringField(EndpointObject, TEXT("transition"), TEXT("Transition"), OutSpec.Transition);
		TryGetBindingStringField(EndpointObject, TEXT("node"), TEXT("Node"), OutSpec.Node);
		TryGetBindingStringField(EndpointObject, TEXT("name"), TEXT("Name"), OutSpec.Name);
		TryGetBindingStringField(EndpointObject, TEXT("class"), TEXT("Class"), OutSpec.Class);

		if (HasBindingField(EndpointObject, TEXT("section"), TEXT("Section")))
		{
			FString SectionString;
			if (!TryGetBindingStringField(EndpointObject, TEXT("section"), TEXT("Section"), SectionString)
				|| !TryParseBindingDataSection(SectionString, OutSpec.Section))
			{
				OutError = FString::Printf(TEXT("StateTree binding %s endpoint has unknown section '%s'"), *Label, *SectionString);
				return false;
			}
		}

		if (HasBindingField(EndpointObject, TEXT("path"), TEXT("Path")))
		{
			const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
			if (!TryGetBindingArrayField(EndpointObject, TEXT("path"), TEXT("Path"), PathValues))
			{
				OutError = FString::Printf(TEXT("StateTree binding %s endpoint path must be an array"), *Label);
				return false;
			}
			if (!ParseBindingPathSegments(*PathValues, Label, OutSpec.Path, OutError))
			{
				return false;
			}
		}

		return true;
	}

	inline bool ParseBindingFunctionSpec(
		const TSharedPtr<FJsonObject>& FunctionObject,
		int32 BindingIndex,
		const FString& Label,
		FAFStateTreeBindingFunctionSpec& OutSpec,
		FString& OutError);

	inline bool ParseBindingFunctionInputSpec(
		const TSharedPtr<FJsonObject>& InputObject,
		int32 BindingIndex,
		const FString& InputName,
		const FString& Label,
		FAFStateTreeBindingFunctionInputSpec& OutSpec,
		FString& OutError)
	{
		OutSpec = FAFStateTreeBindingFunctionInputSpec();
		if (!InputObject.IsValid())
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] function input '%s' must be a JSON object"), BindingIndex, *InputName);
			return false;
		}

		const bool bHasSourceField = HasBindingField(InputObject, TEXT("source"), TEXT("Source"));
		const bool bHasFunctionField = HasBindingField(InputObject, TEXT("function"), TEXT("Function"));
		if (bHasSourceField == bHasFunctionField)
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] function input '%s' must define exactly one of 'source' or 'function'"), BindingIndex, *InputName);
			return false;
		}

		if (bHasSourceField)
		{
			TSharedPtr<FJsonObject> SourceObject;
			if (!TryGetBindingObjectField(InputObject, TEXT("source"), TEXT("Source"), SourceObject))
			{
				OutError = FString::Printf(TEXT("StateTree binding[%d] function input '%s' source must be a JSON object"), BindingIndex, *InputName);
				return false;
			}
			if (!ParseBindingEndpointSpec(SourceObject, Label + TEXT(".source"), OutSpec.Source, OutError))
			{
				return false;
			}
			OutSpec.bHasSource = true;
			return true;
		}

		TSharedPtr<FJsonObject> FunctionObject;
		if (!TryGetBindingObjectField(InputObject, TEXT("function"), TEXT("Function"), FunctionObject))
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] function input '%s' function must be a JSON object"), BindingIndex, *InputName);
			return false;
		}

		OutSpec.Function = MakeShared<FAFStateTreeBindingFunctionSpec>();
		if (!ParseBindingFunctionSpec(FunctionObject, BindingIndex, Label + TEXT(".function"), *OutSpec.Function, OutError))
		{
			return false;
		}
		OutSpec.bHasFunction = true;
		return true;
	}

	inline bool ParseBindingFunctionSpec(
		const TSharedPtr<FJsonObject>& FunctionObject,
		int32 BindingIndex,
		const FString& Label,
		FAFStateTreeBindingFunctionSpec& OutSpec,
		FString& OutError)
	{
		OutSpec = FAFStateTreeBindingFunctionSpec();
		if (!FunctionObject.IsValid())
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] function must be a JSON object"), BindingIndex);
			return false;
		}

		TryGetBindingStringField(FunctionObject, TEXT("type"), TEXT("Type"), OutSpec.Type);

		if (HasBindingField(FunctionObject, TEXT("output"), TEXT("Output")))
		{
			const TArray<TSharedPtr<FJsonValue>>* OutputValues = nullptr;
			if (!TryGetBindingArrayField(FunctionObject, TEXT("output"), TEXT("Output"), OutputValues))
			{
				OutError = FString::Printf(TEXT("StateTree binding[%d] function output must be an array"), BindingIndex);
				return false;
			}
			if (!ParseBindingPathSegments(*OutputValues, Label + TEXT(".output"), OutSpec.OutputPath, OutError))
			{
				return false;
			}
		}

		if (HasBindingField(FunctionObject, TEXT("inputs"), TEXT("Inputs")))
		{
			TSharedPtr<FJsonObject> InputsObject;
			if (!TryGetBindingObjectField(FunctionObject, TEXT("inputs"), TEXT("Inputs"), InputsObject))
			{
				OutError = FString::Printf(TEXT("StateTree binding[%d] function inputs must be a JSON object"), BindingIndex);
				return false;
			}

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : InputsObject->Values)
			{
				const TSharedPtr<FJsonObject>* InputObject = nullptr;
				if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(InputObject) || !InputObject || !InputObject->IsValid())
				{
					OutError = FString::Printf(TEXT("StateTree binding[%d] function input '%s' must be a JSON object"), BindingIndex, *Pair.Key);
					return false;
				}

				FAFStateTreeBindingFunctionInputSpec InputSpec;
				if (!ParseBindingFunctionInputSpec(*InputObject, BindingIndex, Pair.Key, Label + TEXT(".inputs.") + Pair.Key, InputSpec, OutError))
				{
					return false;
				}
				OutSpec.Inputs.Add(Pair.Key, MoveTemp(InputSpec));
			}
		}

		return true;
	}

	inline bool ParseBindingSpec(
		const TSharedPtr<FJsonObject>& BindingObject,
		int32 BindingIndex,
		FAFStateTreeBindingSpec& OutSpec,
		FString& OutError)
	{
		OutSpec = FAFStateTreeBindingSpec();
		OutSpec.SourceIndex = BindingIndex;

		if (!BindingObject.IsValid())
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] must be a JSON object"), BindingIndex);
			return false;
		}

		TryGetBindingStringField(BindingObject, TEXT("id"), TEXT("ID"), OutSpec.Id);

		if (HasBindingField(BindingObject, TEXT("source"), TEXT("Source")))
		{
			TSharedPtr<FJsonObject> SourceObject;
			if (!TryGetBindingObjectField(BindingObject, TEXT("source"), TEXT("Source"), SourceObject))
			{
				OutError = FString::Printf(TEXT("StateTree binding[%d] source must be a JSON object"), BindingIndex);
				return false;
			}
			if (!ParseBindingEndpointSpec(SourceObject, FString::Printf(TEXT("[%d].source"), BindingIndex), OutSpec.Source, OutError))
			{
				return false;
			}
			OutSpec.bHasSource = true;
		}

		if (HasBindingField(BindingObject, TEXT("target"), TEXT("Target")))
		{
			TSharedPtr<FJsonObject> TargetObject;
			if (!TryGetBindingObjectField(BindingObject, TEXT("target"), TEXT("Target"), TargetObject))
			{
				OutError = FString::Printf(TEXT("StateTree binding[%d] target must be a JSON object"), BindingIndex);
				return false;
			}
			if (!ParseBindingEndpointSpec(TargetObject, FString::Printf(TEXT("[%d].target"), BindingIndex), OutSpec.Target, OutError))
			{
				return false;
			}
			OutSpec.bHasTarget = true;
		}

		if (HasBindingField(BindingObject, TEXT("function"), TEXT("Function")))
		{
			TSharedPtr<FJsonObject> FunctionObject;
			if (!TryGetBindingObjectField(BindingObject, TEXT("function"), TEXT("Function"), FunctionObject))
			{
				OutError = FString::Printf(TEXT("StateTree binding[%d] function must be a JSON object"), BindingIndex);
				return false;
			}
			if (!ParseBindingFunctionSpec(FunctionObject, BindingIndex, FString::Printf(TEXT("[%d].function"), BindingIndex), OutSpec.Function, OutError))
			{
				return false;
			}
			OutSpec.bHasFunction = true;
		}

		if (OutSpec.bHasFunction)
		{
			if (!OutSpec.bHasTarget)
			{
				OutError = FString::Printf(TEXT("StateTree binding[%d] is missing required field 'target'"), BindingIndex);
				return false;
			}
			return true;
		}

		if (!OutSpec.bHasSource)
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] is missing required field 'source'"), BindingIndex);
			return false;
		}
		if (!OutSpec.bHasTarget)
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] is missing required field 'target'"), BindingIndex);
			return false;
		}

		return true;
	}

	inline bool ParseBindingSpecsFromConfig(
		const TSharedPtr<FJsonObject>& Config,
		TArray<FAFStateTreeBindingSpec>& OutSpecs,
		FString& OutError)
	{
		OutSpecs.Reset();
		if (!Config.IsValid())
		{
			OutError = TEXT("Invalid StateTree configuration object");
			return false;
		}

		if (!HasBindingField(Config, TEXT("bindings"), TEXT("Bindings")))
		{
			return true;
		}

		const TArray<TSharedPtr<FJsonValue>>* BindingValues = nullptr;
		if (!TryGetBindingArrayField(Config, TEXT("bindings"), TEXT("Bindings"), BindingValues))
		{
			OutError = TEXT("StateTree Bindings must be an array");
			return false;
		}

		for (int32 BindingIndex = 0; BindingIndex < BindingValues->Num(); ++BindingIndex)
		{
			const TSharedPtr<FJsonValue>& BindingValue = (*BindingValues)[BindingIndex];
			const TSharedPtr<FJsonObject>* BindingObject = nullptr;
			if (!BindingValue.IsValid() || !BindingValue->TryGetObject(BindingObject) || !BindingObject || !BindingObject->IsValid())
			{
				OutError = FString::Printf(TEXT("StateTree binding[%d] must be a JSON object"), BindingIndex);
				return false;
			}

			FAFStateTreeBindingSpec Spec;
			if (!ParseBindingSpec(*BindingObject, BindingIndex, Spec, OutError))
			{
				return false;
			}
			OutSpecs.Add(MoveTemp(Spec));
		}

		return true;
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
