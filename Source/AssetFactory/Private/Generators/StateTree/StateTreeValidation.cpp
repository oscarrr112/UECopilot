// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTree/StateTreeValidation.h"

#include "Generators/StateTree/StateTreeClassResolver.h"
#include "StateTreeSchema.h"

namespace
{
	bool IsConditionSlot(EAFStateTreeNodeKind Kind)
	{
		return Kind == EAFStateTreeNodeKind::EnterCondition || Kind == EAFStateTreeNodeKind::TransitionCondition;
	}

	bool IsSameSlotFamily(EAFStateTreeNodeKind ActualKind, EAFStateTreeNodeKind ExpectedKind)
	{
		if (ActualKind == ExpectedKind)
		{
			return true;
		}
		if ((ActualKind == EAFStateTreeNodeKind::Task && ExpectedKind == EAFStateTreeNodeKind::GlobalTask)
			|| (ActualKind == EAFStateTreeNodeKind::GlobalTask && ExpectedKind == EAFStateTreeNodeKind::Task))
		{
			return true;
		}
		return IsConditionSlot(ActualKind) && IsConditionSlot(ExpectedKind);
	}
}

bool UE::AssetFactory::StateTree::ValidateNodeSpec(
	const UStateTreeSchema& Schema,
	const FAFStateTreeNodeSpec& Spec,
	FString& OutError)
{
	if (Spec.Kind == EAFStateTreeNodeKind::Evaluator && !Schema.AllowEvaluators())
	{
		OutError = FString::Printf(TEXT("StateTree schema '%s' does not allow evaluator nodes"), *Schema.GetClass()->GetPathName());
		return false;
	}
	if (Spec.Kind == EAFStateTreeNodeKind::EnterCondition && !Schema.AllowEnterConditions())
	{
		OutError = FString::Printf(TEXT("StateTree schema '%s' does not allow enter condition nodes"), *Schema.GetClass()->GetPathName());
		return false;
	}
	if (Spec.Kind == EAFStateTreeNodeKind::Consideration && !Schema.AllowUtilityConsiderations())
	{
		OutError = FString::Printf(TEXT("StateTree schema '%s' does not allow consideration nodes"), *Schema.GetClass()->GetPathName());
		return false;
	}

	if (UScriptStruct* Struct = ResolveScriptStruct(Spec.Type))
	{
		const UScriptStruct* ExpectedBase = GetExpectedBaseStruct(Spec.Kind);
		if (!ExpectedBase || !Struct->IsChildOf(ExpectedBase))
		{
			const FString ActualKind = DescribeNodeKindForType(Struct, nullptr);
			if (!IsSameSlotFamily(GetStructNodeKind(Struct), Spec.Kind))
			{
				OutError = FString::Printf(
					TEXT("StateTree node '%s' is a %s and cannot be used in a %s slot"),
					*Spec.Type,
					*ActualKind,
					*NodeKindToString(Spec.Kind));
			}
			else
			{
				OutError = FString::Printf(
					TEXT("StateTree node '%s' does not derive from expected base '%s'"),
					*Spec.Type,
					*GetNameSafe(ExpectedBase));
			}
			return false;
		}

		if (!Schema.IsStructAllowed(Struct))
		{
			OutError = FString::Printf(TEXT("StateTree schema '%s' does not allow node '%s'"), *Schema.GetClass()->GetPathName(), *Spec.Type);
			return false;
		}

		return true;
	}

	if (UClass* Class = ResolveNodeClass(Spec.Type))
	{
		UClass* ExpectedBase = GetExpectedBlueprintBaseClass(Spec.Kind);
		if (!ExpectedBase || !Class->IsChildOf(ExpectedBase))
		{
			const FString ActualKind = DescribeNodeKindForType(nullptr, Class);
			if (!IsSameSlotFamily(GetClassNodeKind(Class), Spec.Kind))
			{
				OutError = FString::Printf(
					TEXT("StateTree node '%s' is a %s and cannot be used in a %s slot"),
					*Spec.Type,
					*ActualKind,
					*NodeKindToString(Spec.Kind));
			}
			else
			{
				OutError = FString::Printf(
					TEXT("StateTree node class '%s' does not derive from expected base '%s'"),
					*Spec.Type,
					*GetNameSafe(ExpectedBase));
			}
			return false;
		}

		if (!Schema.IsClassAllowed(Class))
		{
			OutError = FString::Printf(TEXT("StateTree schema '%s' does not allow node '%s'"), *Schema.GetClass()->GetPathName(), *Spec.Type);
			return false;
		}

		return true;
	}

	OutError = FString::Printf(TEXT("Unknown StateTree node type: %s"), *Spec.Type);
	return false;
}
