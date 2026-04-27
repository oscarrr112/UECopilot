// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTree/StateTreeClassResolver.h"

#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "Blueprint/StateTreeConsiderationBlueprintBase.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "StateTreeConditionBase.h"
#include "StateTreeConsiderationBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "Utils/ClassFinderUtils.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectIterator.h"

namespace
{
	FString NormalizeStructSearchName(const FString& TypeName)
	{
		FString SearchName = FPackageName::ObjectPathToObjectName(TypeName);
		if (SearchName.StartsWith(TEXT("F")) && SearchName.Len() > 1)
		{
			SearchName.RightChopInline(1);
		}
		return SearchName;
	}
}

UScriptStruct* UE::AssetFactory::StateTree::ResolveScriptStruct(const FString& TypeName)
{
	if (TypeName.IsEmpty())
	{
		return nullptr;
	}

	if (TypeName.StartsWith(TEXT("/")))
	{
		if (UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *TypeName))
		{
			return Struct;
		}
		if (UScriptStruct* Struct = Cast<UScriptStruct>(StaticLoadObject(UScriptStruct::StaticClass(), nullptr, *TypeName)))
		{
			return Struct;
		}
	}

	const FString SearchName = NormalizeStructSearchName(TypeName);
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;
		if (!Struct)
		{
			continue;
		}

		const FString StructName = Struct->GetName();
		if (StructName.Equals(TypeName, ESearchCase::CaseSensitive)
			|| StructName.Equals(SearchName, ESearchCase::CaseSensitive)
			|| (FString(TEXT("F")) + StructName).Equals(TypeName, ESearchCase::CaseSensitive)
			|| Struct->GetPathName().Equals(TypeName, ESearchCase::CaseSensitive))
		{
			return Struct;
		}
	}

	return nullptr;
}

UClass* UE::AssetFactory::StateTree::ResolveNodeClass(const FString& TypeName)
{
	if (TypeName.IsEmpty())
	{
		return nullptr;
	}

	if (TypeName.StartsWith(TEXT("/")))
	{
		if (UClass* Class = FindObject<UClass>(nullptr, *TypeName))
		{
			return Class;
		}
		if (UClass* Class = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), nullptr, *TypeName)))
		{
			return Class;
		}
		if (UClass* Class = StaticLoadClass(UObject::StaticClass(), nullptr, *TypeName))
		{
			return Class;
		}
	}

	return FClassFinderUtils::FindClassByName(TypeName, UObject::StaticClass());
}

const UScriptStruct* UE::AssetFactory::StateTree::GetExpectedBaseStruct(EAFStateTreeNodeKind Kind)
{
	switch (Kind)
	{
	case EAFStateTreeNodeKind::Evaluator:
		return FStateTreeEvaluatorBase::StaticStruct();
	case EAFStateTreeNodeKind::GlobalTask:
	case EAFStateTreeNodeKind::Task:
		return FStateTreeTaskBase::StaticStruct();
	case EAFStateTreeNodeKind::EnterCondition:
	case EAFStateTreeNodeKind::TransitionCondition:
		return FStateTreeConditionBase::StaticStruct();
	case EAFStateTreeNodeKind::Consideration:
		return FStateTreeConsiderationBase::StaticStruct();
	default:
		return nullptr;
	}
}

UClass* UE::AssetFactory::StateTree::GetExpectedBlueprintBaseClass(EAFStateTreeNodeKind Kind)
{
	switch (Kind)
	{
	case EAFStateTreeNodeKind::Evaluator:
		return UStateTreeEvaluatorBlueprintBase::StaticClass();
	case EAFStateTreeNodeKind::GlobalTask:
	case EAFStateTreeNodeKind::Task:
		return UStateTreeTaskBlueprintBase::StaticClass();
	case EAFStateTreeNodeKind::EnterCondition:
	case EAFStateTreeNodeKind::TransitionCondition:
		return UStateTreeConditionBlueprintBase::StaticClass();
	case EAFStateTreeNodeKind::Consideration:
		return UStateTreeConsiderationBlueprintBase::StaticClass();
	default:
		return nullptr;
	}
}

EAFStateTreeNodeKind UE::AssetFactory::StateTree::GetStructNodeKind(const UScriptStruct* Struct)
{
	if (Struct && Struct->IsChildOf(FStateTreeEvaluatorBase::StaticStruct()))
	{
		return EAFStateTreeNodeKind::Evaluator;
	}
	if (Struct && Struct->IsChildOf(FStateTreeTaskBase::StaticStruct()))
	{
		return EAFStateTreeNodeKind::Task;
	}
	if (Struct && Struct->IsChildOf(FStateTreeConditionBase::StaticStruct()))
	{
		return EAFStateTreeNodeKind::EnterCondition;
	}
	if (Struct && Struct->IsChildOf(FStateTreeConsiderationBase::StaticStruct()))
	{
		return EAFStateTreeNodeKind::Consideration;
	}
	return EAFStateTreeNodeKind::Task;
}

EAFStateTreeNodeKind UE::AssetFactory::StateTree::GetClassNodeKind(const UClass* Class)
{
	if (Class && Class->IsChildOf(UStateTreeEvaluatorBlueprintBase::StaticClass()))
	{
		return EAFStateTreeNodeKind::Evaluator;
	}
	if (Class && Class->IsChildOf(UStateTreeTaskBlueprintBase::StaticClass()))
	{
		return EAFStateTreeNodeKind::Task;
	}
	if (Class && Class->IsChildOf(UStateTreeConditionBlueprintBase::StaticClass()))
	{
		return EAFStateTreeNodeKind::EnterCondition;
	}
	if (Class && Class->IsChildOf(UStateTreeConsiderationBlueprintBase::StaticClass()))
	{
		return EAFStateTreeNodeKind::Consideration;
	}
	return EAFStateTreeNodeKind::Task;
}

FString UE::AssetFactory::StateTree::DescribeNodeKindForType(const UScriptStruct* Struct, const UClass* Class)
{
	if (Struct)
	{
		return NodeKindToString(GetStructNodeKind(Struct));
	}
	if (Class)
	{
		return NodeKindToString(GetClassNodeKind(Class));
	}
	return TEXT("unknown");
}
