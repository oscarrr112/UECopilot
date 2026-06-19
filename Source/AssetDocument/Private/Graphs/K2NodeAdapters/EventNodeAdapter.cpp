// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/K2NodeAdapters/EventNodeAdapter.h"

#include "Engine/Blueprint.h"
#include "Engine/MemberReference.h"
#include "K2Node_Event.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

namespace
{
FString GetClassPath(const UClass* Class)
{
	return Class ? Class->GetPathName() : FString();
}

TSharedRef<FJsonObject> MakeMemberRef(const UClass* OwnerClass, FName Name, const FGuid& Guid)
{
	TSharedRef<FJsonObject> Member = MakeShared<FJsonObject>();
	Member->SetStringField(TEXT("Kind"), TEXT("MemberRef"));
	Member->SetStringField(TEXT("OwnerClass"), GetClassPath(OwnerClass));
	Member->SetStringField(TEXT("Name"), Name.ToString());
	if (Guid.IsValid())
	{
		Member->SetStringField(TEXT("Guid"), Guid.ToString(EGuidFormats::Digits));
	}
	return Member;
}

bool TryReadMemberRef(const TSharedPtr<FJsonObject>& Member, FString& OutOwnerClass, FString& OutName)
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

UClass* ResolveClass(const FString& ClassPath)
{
	return ClassPath.IsEmpty() ? nullptr : StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath);
}

UClass* ResolveMemberOwnerClass(const UBlueprint* Blueprint, const FString& OwnerClassPath)
{
	if (OwnerClassPath == TEXT("Self"))
	{
		if (Blueprint && Blueprint->GeneratedClass)
		{
			return Blueprint->GeneratedClass;
		}
		return Blueprint ? Blueprint->ParentClass.Get() : nullptr;
	}
	return ResolveClass(OwnerClassPath);
}

FAssetDocumentCapabilityResult MissingMemberFailure(const FAssetDocumentNodeApplyContext& Context, const FAssetDocumentNodeSpec& Node)
{
	return FAssetDocumentCapabilityResult::Failure(
		FString::Printf(TEXT("Graph node '%s' requires a reflected MemberRef"), *Node.Id),
		Context.NodePath / TEXT("Member"),
		TEXT("MissingGraphMemberReference"));
}

FAssetDocumentCapabilityResult UnresolvedMemberFailure(const FAssetDocumentNodeApplyContext& Context, const FAssetDocumentNodeSpec& Node)
{
	return FAssetDocumentCapabilityResult::Failure(
		FString::Printf(TEXT("Graph node '%s' MemberRef could not be resolved"), *Node.Id),
		Context.NodePath / TEXT("Member"),
		TEXT("UnresolvedGraphMemberReference"));
}
}

FAssetDocumentCapabilityResult FAssetDocumentK2EventNodeAdapter::ConfigureNodeForApply(
	const FAssetDocumentNodeApplyContext& Context,
	UEdGraphNode* Node,
	const FAssetDocumentNodeSpec& NodeSpec) const
{
	UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
	if (!EventNode)
	{
		return FAssetDocumentCapabilityResult::Failure(
			FString::Printf(TEXT("Graph node '%s' is not a K2 event node"), *NodeSpec.Id),
			Context.NodePath,
			TEXT("UnsupportedGraphNodeClass"));
	}

	FString OwnerClassPath;
	FString FunctionName;
	if (!TryReadMemberRef(NodeSpec.Member, OwnerClassPath, FunctionName))
	{
		return MissingMemberFailure(Context, NodeSpec);
	}

	UClass* OwnerClass = ResolveMemberOwnerClass(Context.Blueprint, OwnerClassPath);
	UFunction* Function = OwnerClass ? OwnerClass->FindFunctionByName(FName(*FunctionName)) : nullptr;
	if (!OwnerClass || !Function)
	{
		return UnresolvedMemberFailure(Context, NodeSpec);
	}

	EventNode->EventReference.SetExternalMember(FName(*FunctionName), OwnerClass);
	EventNode->bOverrideFunction = true;
	return FAssetDocumentCapabilityResult::Success();
}

bool FAssetDocumentK2EventNodeAdapter::DoesNodeMatchSpec(
	const UBlueprint*,
	const UEdGraphNode* Node,
	const FAssetDocumentNodeSpec& NodeSpec) const
{
	const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
	if (!EventNode || Node->GetClass()->GetPathName() != NodeSpec.Class)
	{
		return false;
	}

	FString OwnerClassPath;
	FString FunctionName;
	return TryReadMemberRef(NodeSpec.Member, OwnerClassPath, FunctionName)
		&& EventNode->GetFunctionName() == FName(*FunctionName);
}

bool FAssetDocumentK2EventNodeAdapter::ExtractNode(const UBlueprint* Blueprint, const UK2Node_Event* Node, FAssetDocumentNodeSpec& OutNode) const
{
	if (!Node)
	{
		return false;
	}

	UFunction* Function = Node->FindEventSignatureFunction();
	const UClass* OwnerClass = Node->EventReference.GetMemberParentClass(Blueprint ? Blueprint->SkeletonGeneratedClass : nullptr);
	if (!OwnerClass && Function)
	{
		OwnerClass = Function->GetOwnerClass();
	}

	const FName FunctionName = Node->GetFunctionName();
	if (!OwnerClass || FunctionName.IsNone())
	{
		return false;
	}

	OutNode.Capability = GetCapability();
	OutNode.Member = MakeMemberRef(OwnerClass, FunctionName, Node->EventReference.GetMemberGuid());
	return true;
}
