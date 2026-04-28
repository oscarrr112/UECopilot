// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTree/StateTreeBindingBuilder.h"

#include "Generators/StateTree/StateTreeBindingResolver.h"
#include "Generators/StateTree/StateTreeJsonTypes.h"
#include "StateTreeEditorData.h"

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
		if (Spec.bHasFunction)
		{
			continue;
		}

		const FString BindingLabel = MakeBindingLabel(Spec);
		FPropertyBindingPath SourcePath;
		FPropertyBindingPath TargetPath;
		if (!ResolveBindingEndpointPath(Index, Spec.Source, SourcePath, OutError))
		{
			OutError = FString::Printf(TEXT("StateTree %s source could not be resolved: %s"), *BindingLabel, *OutError);
			return false;
		}
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

		EditorData.AddPropertyBinding(SourcePath, TargetPath);
	}

	return true;
}
