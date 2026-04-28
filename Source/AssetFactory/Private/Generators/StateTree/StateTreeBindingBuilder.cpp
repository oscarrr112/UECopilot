// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTree/StateTreeBindingBuilder.h"

#include "Generators/StateTree/StateTreeClassResolver.h"
#include "Generators/StateTree/StateTreeBindingResolver.h"
#include "Generators/StateTree/StateTreeJsonTypes.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreePropertyFunctionBase.h"
#include "StructUtils/InstancedStruct.h"

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

	struct FPreparedFunctionBinding;

	struct FPreparedFunctionInputBinding
	{
		FString InputName;
		FPropertyBindingPath SourcePath;
		TSharedPtr<FPreparedFunctionBinding> Function;
		TArray<FPropertyBindingPathSegment> TargetSegments;
		bool bHasSource = false;
		bool bHasFunction = false;
	};

	struct FPreparedFunctionBinding
	{
		UScriptStruct* FunctionStruct = nullptr;
		const UScriptStruct* InstanceStruct = nullptr;
		TArray<FPropertyBindingPathSegment> OutputSegments;
		TArray<FPreparedFunctionInputBinding> Inputs;
	};

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

	const UScriptStruct* ResolvePropertyFunctionInstanceStruct(UScriptStruct* FunctionStruct, const FString& FunctionType, FString& OutError)
	{
		FInstancedStruct FunctionNode;
		FunctionNode.InitializeAs(FunctionStruct);
		const FStateTreePropertyFunctionBase* Function = FunctionNode.GetPtr<FStateTreePropertyFunctionBase>();
		if (!Function)
		{
			OutError = FString::Printf(TEXT("StateTree property function type '%s' could not be instantiated"), *FunctionType);
			return nullptr;
		}

		const UScriptStruct* InstanceStruct = Cast<UScriptStruct>(Function->GetInstanceDataType());
		if (!InstanceStruct)
		{
			OutError = FString::Printf(TEXT("StateTree property function type '%s' has no struct instance data"), *FunctionType);
			return nullptr;
		}
		return InstanceStruct;
	}

	bool ValidateFunctionPath(
		const UScriptStruct* InstanceStruct,
		const TArray<FPropertyBindingPathSegment>& Segments,
		const FString& Label,
		TArray<FPropertyBindingPathSegment>& OutSegments,
		FString& OutError)
	{
		FPropertyBindingPath Path(FGuid::NewGuid(), Segments);
		FString PathError;
		if (!Path.UpdateSegments(InstanceStruct, &PathError))
		{
			OutError = PathError.IsEmpty()
				? FString::Printf(TEXT("%s path '%s' could not be resolved against '%s'"), *Label, *Path.ToString(), *InstanceStruct->GetPathName())
				: FString::Printf(TEXT("%s path '%s' could not be resolved: %s"), *Label, *Path.ToString(), *PathError);
			return false;
		}
		OutSegments.Reset();
		OutSegments.Append(Path.GetSegments().GetData(), Path.GetSegments().Num());
		return true;
	}

	bool ValidateFunctionInputName(const FString& InputName, FString& OutError)
	{
		if (InputName.IsEmpty())
		{
			OutError = TEXT("function input name must be non-empty");
			return false;
		}
		if (InputName.Contains(TEXT(".")) || InputName.Contains(TEXT("/")) || InputName.Contains(TEXT("\\")))
		{
			OutError = FString::Printf(TEXT("function input '%s' must be a single property name"), *InputName);
			return false;
		}
		return true;
	}

	bool PrepareFunctionBinding(
		const FAFStateTreeBindingIndex& Index,
		const FAFStateTreeBindingFunctionSpec& FunctionSpec,
		const FString& BindingLabel,
		FPreparedFunctionBinding& OutPrepared,
		FString& OutError)
	{
		OutPrepared = FPreparedFunctionBinding();

		UScriptStruct* FunctionStruct = ResolvePropertyFunctionStruct(FunctionSpec, OutError);
		if (!FunctionStruct)
		{
			return false;
		}
		OutPrepared.FunctionStruct = FunctionStruct;

		const UScriptStruct* InstanceStruct = ResolvePropertyFunctionInstanceStruct(FunctionStruct, FunctionSpec.Type, OutError);
		if (!InstanceStruct)
		{
			return false;
		}
		OutPrepared.InstanceStruct = InstanceStruct;

		TArray<FPropertyBindingPathSegment> OutputSegments = MakeBindingPathSegments(FunctionSpec.OutputPath, OutError);
		if (!OutError.IsEmpty())
		{
			OutError = FString::Printf(TEXT("StateTree %s function output could not be resolved: %s"), *BindingLabel, *OutError);
			return false;
		}
		if (!ValidateFunctionPath(InstanceStruct, OutputSegments, TEXT("function output"), OutPrepared.OutputSegments, OutError))
		{
			return false;
		}

		for (const TPair<FString, FAFStateTreeBindingFunctionInputSpec>& Pair : FunctionSpec.Inputs)
		{
			if (!ValidateFunctionInputName(Pair.Key, OutError))
			{
				return false;
			}

			FPreparedFunctionInputBinding PreparedInput;
			PreparedInput.InputName = Pair.Key;
			const TArray<FPropertyBindingPathSegment> InputSegments = { FPropertyBindingPathSegment(FName(*Pair.Key)) };
			if (!ValidateFunctionPath(
				InstanceStruct,
				InputSegments,
				FString::Printf(TEXT("function input '%s'"), *Pair.Key),
				PreparedInput.TargetSegments,
				OutError))
			{
				return false;
			}

			const FString InputLabel = FString::Printf(TEXT("%s function input '%s'"), *BindingLabel, *Pair.Key);
			if (Pair.Value.bHasSource)
			{
				if (!ResolveBindingEndpointPath(Index, Pair.Value.Source, PreparedInput.SourcePath, OutError))
				{
					OutError = FString::Printf(TEXT("StateTree %s source could not be resolved: %s"), *InputLabel, *OutError);
					return false;
				}
				PreparedInput.bHasSource = true;
			}
			else if (Pair.Value.bHasFunction && Pair.Value.Function.IsValid())
			{
				PreparedInput.Function = MakeShared<FPreparedFunctionBinding>();
				if (!PrepareFunctionBinding(Index, *Pair.Value.Function, InputLabel, *PreparedInput.Function, OutError))
				{
					OutError = FString::Printf(TEXT("StateTree %s function failed: %s"), *InputLabel, *OutError);
					return false;
				}
				PreparedInput.bHasFunction = true;
			}

			OutPrepared.Inputs.Add(MoveTemp(PreparedInput));
		}

		return true;
	}

	bool AddFunctionBinding(
		UStateTreeEditorData& EditorData,
		const FPreparedFunctionBinding& PreparedFunction,
		const FPropertyBindingPath& TargetPath,
		FString& OutError)
	{
		FStateTreeEditorPropertyBindings* EditorBindings = EditorData.GetPropertyEditorBindings();
		if (!EditorBindings)
		{
			OutError = TEXT("StateTree editor data has no property bindings");
			return false;
		}

		const FPropertyBindingPath FunctionOutputPath = EditorBindings->AddFunctionBinding(PreparedFunction.FunctionStruct, PreparedFunction.OutputSegments, TargetPath);
		const FGuid FunctionNodeID = FunctionOutputPath.GetStructID();
		for (const FPreparedFunctionInputBinding& Input : PreparedFunction.Inputs)
		{
			const FPropertyBindingPath InputTargetPath(FunctionNodeID, Input.TargetSegments);
			if (Input.bHasSource)
			{
				EditorData.AddPropertyBinding(Input.SourcePath, InputTargetPath);
				continue;
			}

			if (Input.bHasFunction && Input.Function.IsValid())
			{
				if (!AddFunctionBinding(EditorData, *Input.Function, InputTargetPath, OutError))
				{
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
				TEXT("StateTree %s targets duplicate path '%s'"),
				*BindingLabel,
				*TargetPath.ToString());
			return false;
		}
		TargetPaths.Add(TargetPathKey);

		if (Spec.bHasFunction)
		{
			FPreparedFunctionBinding PreparedFunction;
			if (!PrepareFunctionBinding(Index, Spec.Function, BindingLabel, PreparedFunction, OutError)
				|| !AddFunctionBinding(EditorData, PreparedFunction, TargetPath, OutError))
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
