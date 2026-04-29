// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

enum class EAFStateTreeBindingEndpointKind : uint8
{
	RootParameter,
	StateParameter,
	Context,
	Evaluator,
	GlobalTask,
	Task,
	EnterCondition,
	TransitionCondition,
	Consideration,
	Function,
	Node,
};

enum class EAFStateTreeBindingDataSection : uint8
{
	Node,
	Instance,
	ExecutionRuntimeData,
};

struct FAFStateTreeBindingPathSegmentSpec
{
	FString Name;
	int32 ArrayIndex = INDEX_NONE;
	FGuid Guid;
	bool bHasGuid = false;
	FString InstanceStruct;
	FString Access;
};

struct FAFStateTreeBindingEndpointSpec
{
	EAFStateTreeBindingEndpointKind Kind = EAFStateTreeBindingEndpointKind::Node;
	EAFStateTreeBindingDataSection Section = EAFStateTreeBindingDataSection::Instance;
	FString State;
	FString Transition;
	FString Node;
	FString Name;
	FString Class;
	TArray<FAFStateTreeBindingPathSegmentSpec> Path;
};

struct FAFStateTreeBindingFunctionSpec;

struct FAFStateTreeBindingFunctionInputSpec
{
	TArray<FAFStateTreeBindingPathSegmentSpec> TargetPath;
	FAFStateTreeBindingEndpointSpec Source;
	TSharedPtr<FAFStateTreeBindingFunctionSpec> Function;
	FString SourceLabel;
	bool bHasSource = false;
	bool bHasFunction = false;
};

struct FAFStateTreeBindingFunctionSpec
{
	FString Type;
	TArray<FAFStateTreeBindingPathSegmentSpec> OutputPath;
	TArray<FAFStateTreeBindingFunctionInputSpec> Inputs;
};

struct FAFStateTreeBindingSpec
{
	FString Id;
	FAFStateTreeBindingEndpointSpec Source;
	FAFStateTreeBindingEndpointSpec Target;
	FAFStateTreeBindingFunctionSpec Function;
	bool bHasSource = false;
	bool bHasTarget = false;
	bool bHasFunction = false;
	int32 SourceIndex = INDEX_NONE;
};
