// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/K2NodeAdapters/VariableGetSetNodeAdapter.h"

#include "Engine/Blueprint.h"
#include "Engine/MemberReference.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
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

bool VariableTryReadMemberRef(const TSharedPtr<FJsonObject>& Member, FString& OutOwnerClass, FString& OutName)
{
	if (!Member.IsValid())
	{
		return false;
	}

	FString Kind;
	return Member->TryGetStringField(TEXT("Kind"), Kind)
		&& Kind == TEXT("MemberRef")
		&& Member->TryGetStringField(TEXT("OwnerClass"), OutOwnerClass)
		&& !OutOwnerClass.IsEmpty()
		&& Member->TryGetStringField(TEXT("Name"), OutName)
		&& !OutName.IsEmpty();
}

UClass* VariableResolveClass(const FString& ClassPath)
{
	return ClassPath.IsEmpty() ? nullptr : StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath);
}

UClass* VariableResolveMemberOwnerClass(const UBlueprint* Blueprint, const FString& OwnerClassPath)
{
	if (OwnerClassPath == TEXT("Self"))
	{
		if (Blueprint && Blueprint->GeneratedClass)
		{
			return Blueprint->GeneratedClass;
		}
		return Blueprint ? Blueprint->ParentClass.Get() : nullptr;
	}
	return VariableResolveClass(OwnerClassPath);
}

FProperty* ResolveMemberProperty(const UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Member)
{
	FString OwnerClassPath;
	FString PropertyName;
	if (!VariableTryReadMemberRef(Member, OwnerClassPath, PropertyName))
	{
		return nullptr;
	}

	if (OwnerClassPath == TEXT("Self") && Blueprint)
	{
		const FName VariableName(*PropertyName);
		if (Blueprint->NewVariables.ContainsByPredicate([VariableName](const FBPVariableDescription& Variable)
		{
			return Variable.VarName == VariableName;
		}))
		{
			return FindFProperty<FProperty>(Blueprint->SkeletonGeneratedClass ? Blueprint->SkeletonGeneratedClass : Blueprint->GeneratedClass, VariableName);
		}
	}

	UClass* OwnerClass = VariableResolveMemberOwnerClass(Blueprint, OwnerClassPath);
	return OwnerClass ? FindFProperty<FProperty>(OwnerClass, FName(*PropertyName)) : nullptr;
}

FAssetDocumentCapabilityResult VariableMissingMemberFailure(const FAssetDocumentNodeApplyContext& Context, const FAssetDocumentNodeSpec& Node)
{
	return FAssetDocumentCapabilityResult::Failure(
		FString::Printf(TEXT("Graph node '%s' requires a reflected MemberRef"), *Node.Id),
		Context.NodePath / TEXT("Member"),
		TEXT("MissingGraphMemberReference"));
}

FAssetDocumentCapabilityResult VariableUnresolvedMemberFailure(const FAssetDocumentNodeApplyContext& Context, const FAssetDocumentNodeSpec& Node)
{
	return FAssetDocumentCapabilityResult::Failure(
		FString::Printf(TEXT("Graph node '%s' MemberRef could not be resolved"), *Node.Id),
		Context.NodePath / TEXT("Member"),
		TEXT("UnresolvedGraphMemberReference"));
}

template <typename NodeType>
FAssetDocumentCapabilityResult ConfigureVariableNodeForApply(
	const FAssetDocumentNodeApplyContext& Context,
	UEdGraphNode* Node,
	const FAssetDocumentNodeSpec& NodeSpec)
{
	NodeType* VariableNode = Cast<NodeType>(Node);
	if (!VariableNode)
	{
		return FAssetDocumentCapabilityResult::Failure(
			FString::Printf(TEXT("Graph node '%s' is not the expected K2 variable node"), *NodeSpec.Id),
			Context.NodePath,
			TEXT("UnsupportedGraphNodeClass"));
	}

	FString OwnerClassPath;
	FString PropertyName;
	if (!VariableTryReadMemberRef(NodeSpec.Member, OwnerClassPath, PropertyName))
	{
		return VariableMissingMemberFailure(Context, NodeSpec);
	}
	if (!ResolveMemberProperty(Context.Blueprint, NodeSpec.Member))
	{
		return VariableUnresolvedMemberFailure(Context, NodeSpec);
	}

	if (OwnerClassPath == TEXT("Self"))
	{
		VariableNode->VariableReference.SetSelfMember(FName(*PropertyName));
	}
	else
	{
		UClass* OwnerClass = VariableResolveMemberOwnerClass(Context.Blueprint, OwnerClassPath);
		VariableNode->VariableReference.SetExternalMember(FName(*PropertyName), OwnerClass);
	}
	return FAssetDocumentCapabilityResult::Success();
}

bool DoesVariableNodeMatchSpec(
	const UBlueprint*,
	const UEdGraphNode* Node,
	const FAssetDocumentNodeSpec& NodeSpec)
{
	const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node);
	if (!VariableNode || Node->GetClass()->GetPathName() != NodeSpec.Class)
	{
		return false;
	}

	FString OwnerClassPath;
	FString PropertyName;
	return VariableTryReadMemberRef(NodeSpec.Member, OwnerClassPath, PropertyName)
		&& VariableNode->GetVarName() == FName(*PropertyName);
}
}

FAssetDocumentCapabilityResult FAssetDocumentK2VariableGetNodeAdapter::ConfigureNodeForApply(
	const FAssetDocumentNodeApplyContext& Context,
	UEdGraphNode* Node,
	const FAssetDocumentNodeSpec& NodeSpec) const
{
	return ConfigureVariableNodeForApply<UK2Node_VariableGet>(Context, Node, NodeSpec);
}

bool FAssetDocumentK2VariableGetNodeAdapter::DoesNodeMatchSpec(
	const UBlueprint* Blueprint,
	const UEdGraphNode* Node,
	const FAssetDocumentNodeSpec& NodeSpec) const
{
	return DoesVariableNodeMatchSpec(Blueprint, Node, NodeSpec);
}

FAssetDocumentCapabilityResult FAssetDocumentK2VariableSetNodeAdapter::ConfigureNodeForApply(
	const FAssetDocumentNodeApplyContext& Context,
	UEdGraphNode* Node,
	const FAssetDocumentNodeSpec& NodeSpec) const
{
	return ConfigureVariableNodeForApply<UK2Node_VariableSet>(Context, Node, NodeSpec);
}

bool FAssetDocumentK2VariableSetNodeAdapter::DoesNodeMatchSpec(
	const UBlueprint* Blueprint,
	const UEdGraphNode* Node,
	const FAssetDocumentNodeSpec& NodeSpec) const
{
	return DoesVariableNodeMatchSpec(Blueprint, Node, NodeSpec);
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
