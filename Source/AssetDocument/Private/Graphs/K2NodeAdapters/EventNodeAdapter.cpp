// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/K2NodeAdapters/EventNodeAdapter.h"

#include "Engine/Blueprint.h"
#include "Engine/MemberReference.h"
#include "K2Node_Event.h"
#include "UObject/Class.h"

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
