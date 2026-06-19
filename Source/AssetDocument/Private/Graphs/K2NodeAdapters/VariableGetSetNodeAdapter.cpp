// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/K2NodeAdapters/VariableGetSetNodeAdapter.h"

#include "Engine/Blueprint.h"
#include "Engine/MemberReference.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace
{
bool IsBlueprintVariable(const UBlueprint* Blueprint, FName VariableName)
{
	return Blueprint && Blueprint->NewVariables.ContainsByPredicate([VariableName](const FBPVariableDescription& Variable)
	{
		return Variable.VarName == VariableName;
	});
}

FString GetPropertyOwnerPath(const FProperty* Property)
{
	const UClass* OwnerClass = Property ? Property->GetOwnerClass() : nullptr;
	return OwnerClass ? OwnerClass->GetPathName() : FString();
}

bool TryMakeVariableMemberRef(const UBlueprint* Blueprint, const UK2Node_Variable* Node, TSharedPtr<FJsonObject>& OutMember)
{
	const FName VariableName = Node ? Node->GetVarName() : NAME_None;
	const FProperty* Property = Node ? Node->GetPropertyForVariable() : nullptr;
	const bool bBlueprintVariable = IsBlueprintVariable(Blueprint, VariableName);
	if (!bBlueprintVariable && !Property)
	{
		return false;
	}

	TSharedRef<FJsonObject> Member = MakeShared<FJsonObject>();
	Member->SetStringField(TEXT("Kind"), TEXT("MemberRef"));
	Member->SetStringField(TEXT("OwnerClass"), bBlueprintVariable ? FString(TEXT("Self")) : GetPropertyOwnerPath(Property));
	Member->SetStringField(TEXT("Name"), VariableName.ToString());
	const FGuid Guid = Node ? Node->VariableReference.GetMemberGuid() : FGuid();
	if (Guid.IsValid())
	{
		Member->SetStringField(TEXT("Guid"), Guid.ToString(EGuidFormats::Digits));
	}
	if (bBlueprintVariable)
	{
		Member->SetBoolField(TEXT("SelfContext"), true);
	}
	OutMember = Member;
	return true;
}
}

bool FAssetDocumentK2VariableGetNodeAdapter::ExtractNode(const UBlueprint* Blueprint, const UK2Node_VariableGet* Node, FAssetDocumentNodeSpec& OutNode) const
{
	if (!Node || Node->GetVarName().IsNone())
	{
		return false;
	}

	OutNode.Capability = GetCapability();
	return TryMakeVariableMemberRef(Blueprint, Node, OutNode.Member);
}

bool FAssetDocumentK2VariableSetNodeAdapter::ExtractNode(const UBlueprint* Blueprint, const UK2Node_VariableSet* Node, FAssetDocumentNodeSpec& OutNode) const
{
	if (!Node || Node->GetVarName().IsNone())
	{
		return false;
	}

	OutNode.Capability = GetCapability();
	return TryMakeVariableMemberRef(Blueprint, Node, OutNode.Member);
}
