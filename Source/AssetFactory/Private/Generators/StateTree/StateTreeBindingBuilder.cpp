// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTree/StateTreeBindingBuilder.h"

#include "Generators/StateTree/StateTreeClassResolver.h"
#include "Generators/StateTree/StateTreeBindingResolver.h"
#include "Generators/StateTree/StateTreeJsonTypes.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreePropertyFunctionBase.h"

namespace
{
	using namespace UE::AssetFactory::StateTree;

	FString MakeBindingLabel(const FAFStateTreeBindingSpec& Spec)
	{
		if (!Spec.Id.IsEmpty())
		{
			return FString::Printf(TEXT("binding[%d] id '%s'"), Spec.SourceIndex, *Spec.Id);
		}
		return FString::Printf(TEXT("binding[%d]"), Spec.SourceIndex);
	}

	FString MakeTargetPathKey(const FPropertyBindingPath& TargetPath)
	{
		return FString::Printf(
			TEXT("%s:%s"),
			*TargetPath.GetStructID().ToString(EGuidFormats::DigitsWithHyphensLower),
			*TargetPath.ToString());
	}

	UScriptStruct* ResolvePropertyFunctionStruct(const FAFStateTreeBindingFunctionSpec& FunctionSpec, FString& OutError)
	{
		UScriptStruct* FunctionStruct = ResolveScriptStruct(FunctionSpec.Type);
		if (!FunctionStruct)
		{
			OutError = FString::Printf(TEXT("Unknown StateTree property function type '%s'"), *FunctionSpec.Type);
			return nullptr;
		}
		if (!FunctionStruct->IsChildOf(FStateTreePropertyFunctionBase::StaticStruct()))
		{
			OutError = FString::Printf(TEXT("StateTree property function type '%s' is not a FStateTreePropertyFunctionBase"), *FunctionSpec.Type);
			return nullptr;
		}
		return FunctionStruct;
	}

	bool AddFunctionBindingInputs(
		UStateTreeEditorData& EditorData,
		const FAFStateTreeBindingIndex& Index,
		const FAFStateTreeBindingFunctionSpec& FunctionSpec,
		const FGuid& FunctionNodeID,
		const FString& BindingLabel,
		FString& OutError);

	bool AddFunctionBinding(
		UStateTreeEditorData& EditorData,
		const FAFStateTreeBindingIndex& Index,
		const FAFStateTreeBindingFunctionSpec& FunctionSpec,
		const FPropertyBindingPath& TargetPath,
		const FString& BindingLabel,
		FString& OutError)
	{
		UScriptStruct* FunctionStruct = ResolvePropertyFunctionStruct(FunctionSpec, OutError);
		if (!FunctionStruct)
		{
			return false;
		}

		TArray<FPropertyBindingPathSegment> OutputSegments = MakeBindingPathSegments(FunctionSpec.OutputPath, OutError);
		if (!OutError.IsEmpty())
		{
			OutError = FString::Printf(TEXT("StateTree %s function output could not be resolved: %s"), *BindingLabel, *OutError);
			return false;
		}

		FStateTreeEditorPropertyBindings* EditorBindings = EditorData.GetPropertyEditorBindings();
		if (!EditorBindings)
		{
			OutError = TEXT("StateTree editor data has no property bindings");
			return false;
		}

		const FPropertyBindingPath FunctionOutputPath = EditorBindings->AddFunctionBinding(FunctionStruct, OutputSegments, TargetPath);
		return AddFunctionBindingInputs(EditorData, Index, FunctionSpec, FunctionOutputPath.GetStructID(), BindingLabel, OutError);
	}

	bool AddFunctionBindingInputs(
		UStateTreeEditorData& EditorData,
		const FAFStateTreeBindingIndex& Index,
		const FAFStateTreeBindingFunctionSpec& FunctionSpec,
		const FGuid& FunctionNodeID,
		const FString& BindingLabel,
		FString& OutError)
	{
		for (const TPair<FString, FAFStateTreeBindingFunctionInputSpec>& Pair : FunctionSpec.Inputs)
		{
			const FString InputLabel = FString::Printf(TEXT("%s function input '%s'"), *BindingLabel, *Pair.Key);
			const FPropertyBindingPath InputTargetPath(FunctionNodeID, FName(*Pair.Key));
			if (Pair.Value.bHasSource)
			{
				FPropertyBindingPath SourcePath;
				if (!ResolveBindingEndpointPath(Index, Pair.Value.Source, SourcePath, OutError))
				{
					OutError = FString::Printf(TEXT("StateTree %s source could not be resolved: %s"), *InputLabel, *OutError);
					return false;
				}

				EditorData.AddPropertyBinding(SourcePath, InputTargetPath);
				continue;
			}

			if (Pair.Value.bHasFunction && Pair.Value.Function.IsValid())
			{
				if (!AddFunctionBinding(EditorData, Index, *Pair.Value.Function, InputTargetPath, InputLabel, OutError))
				{
					OutError = FString::Printf(TEXT("StateTree %s function failed: %s"), *InputLabel, *OutError);
					return false;
				}
			}
		}

		return true;
	}
}

bool UE::AssetFactory::StateTree::ApplyPropertyBindings(
	UStateTreeEditorData& EditorData,
	const FAFStateTreeStateIndex& StateIndex,
	const TSharedPtr<FJsonObject>& Config,
	FString& OutError)
{
	TArray<FAFStateTreeBindingSpec> Specs;
	if (!ParseBindingSpecsFromConfig(Config, Specs, OutError))
	{
		return false;
	}

	if (FStateTreeEditorPropertyBindings* EditorBindings = EditorData.GetPropertyEditorBindings())
	{
		EditorBindings->RemoveBindings([](FPropertyBindingBinding&)
		{
			return true;
		});
	}

	if (Specs.IsEmpty())
	{
		return true;
	}

	FAFStateTreeBindingIndex Index;
	if (!BuildBindingIndex(EditorData, StateIndex, Index, OutError))
	{
		return false;
	}

	TSet<FString> TargetPaths;
	for (const FAFStateTreeBindingSpec& Spec : Specs)
	{
		const FString BindingLabel = MakeBindingLabel(Spec);
		FPropertyBindingPath TargetPath;
		if (!ResolveBindingEndpointPath(Index, Spec.Target, TargetPath, OutError))
		{
			OutError = FString::Printf(TEXT("StateTree %s target could not be resolved: %s"), *BindingLabel, *OutError);
			return false;
		}

		const FString TargetPathKey = MakeTargetPathKey(TargetPath);
		if (TargetPaths.Contains(TargetPathKey))
		{
			OutError = FString::Printf(
				TEXT("StateTree %s duplicates target binding path '%s'"),
				*BindingLabel,
				*TargetPath.ToString());
			return false;
		}
		TargetPaths.Add(TargetPathKey);

		if (Spec.bHasFunction)
		{
			if (!AddFunctionBinding(EditorData, Index, Spec.Function, TargetPath, BindingLabel, OutError))
			{
				OutError = FString::Printf(TEXT("StateTree %s function failed: %s"), *BindingLabel, *OutError);
				return false;
			}
			continue;
		}

		FPropertyBindingPath SourcePath;
		if (!ResolveBindingEndpointPath(Index, Spec.Source, SourcePath, OutError))
		{
			OutError = FString::Printf(TEXT("StateTree %s source could not be resolved: %s"), *BindingLabel, *OutError);
			return false;
		}

		EditorData.AddPropertyBinding(SourcePath, TargetPath);
	}

	return true;
}
