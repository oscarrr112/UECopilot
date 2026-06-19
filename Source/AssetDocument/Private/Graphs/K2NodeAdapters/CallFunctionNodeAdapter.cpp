// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/K2NodeAdapters/CallFunctionNodeAdapter.h"

#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/MemberReference.h"
#include "K2Node_CallFunction.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace
{
FString GetClassPath(const UClass* Class)
{
	return Class ? Class->GetPathName() : FString();
}

TSharedRef<FJsonObject> MakeMemberRef(const UFunction* Function, const FGuid& Guid)
{
	TSharedRef<FJsonObject> Member = MakeShared<FJsonObject>();
	Member->SetStringField(TEXT("Kind"), TEXT("MemberRef"));
	Member->SetStringField(TEXT("OwnerClass"), GetClassPath(Function ? Function->GetOwnerClass() : nullptr));
	Member->SetStringField(TEXT("Name"), Function ? Function->GetName() : FString());
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

UFunction* ResolveMemberFunction(const UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Member)
{
	FString OwnerClassPath;
	FString FunctionName;
	if (!TryReadMemberRef(Member, OwnerClassPath, FunctionName))
	{
		return nullptr;
	}

	UClass* OwnerClass = ResolveMemberOwnerClass(Blueprint, OwnerClassPath);
	return OwnerClass ? OwnerClass->FindFunctionByName(FName(*FunctionName)) : nullptr;
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
		TEXT("UnresolvedGraphFunction"));
}

bool HasAuthoredDefault(const UEdGraphPin* Pin)
{
	return Pin && (!Pin->DefaultValue.IsEmpty() || Pin->DefaultObject || !Pin->DefaultTextValue.IsEmpty());
}

bool ShouldExtractInputDefault(const UEdGraphPin* Pin)
{
	return Pin
		&& Pin->Direction == EGPD_Input
		&& Pin->LinkedTo.IsEmpty()
		&& Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec
		&& Pin->PinName != UEdGraphSchema_K2::PN_Self
		&& HasAuthoredDefault(Pin);
}

bool IsTruthyString(const FString& Value)
{
	return Value.Equals(TEXT("true"), ESearchCase::IgnoreCase)
		|| Value.Equals(TEXT("1"), ESearchCase::IgnoreCase);
}

bool IsFalseyString(const FString& Value)
{
	return Value.Equals(TEXT("false"), ESearchCase::IgnoreCase)
		|| Value.Equals(TEXT("0"), ESearchCase::IgnoreCase);
}

bool LexicalDefaultsMatch(const FString& Left, const FString& Right)
{
	if (Left == Right)
	{
		return true;
	}

	double LeftNumber = 0.0;
	double RightNumber = 0.0;
	if (LexTryParseString(LeftNumber, *Left) && LexTryParseString(RightNumber, *Right))
	{
		return FMath::IsNearlyEqual(LeftNumber, RightNumber);
	}

	return (IsTruthyString(Left) && IsTruthyString(Right))
		|| (IsFalseyString(Left) && IsFalseyString(Right));
}

bool IsBaselineFunctionPinDefault(const UEdGraphPin* Pin, const UFunction* Function)
{
	if (!Pin || !Function)
	{
		return false;
	}

	if (Pin->DefaultObject)
	{
		return false;
	}

	const FString PinName = Pin->PinName.ToString();
	const FString CppDefaultKey = FString::Printf(TEXT("CPP_Default_%s"), *PinName);
	if (Function->HasMetaData(*CppDefaultKey))
	{
		const FString MetadataDefault = Function->GetMetaData(*CppDefaultKey);
		if (Pin->DefaultValue.IsEmpty() || LexicalDefaultsMatch(Pin->DefaultValue, MetadataDefault))
		{
			return Pin->DefaultTextValue.IsEmpty();
		}
		if (!Pin->DefaultTextValue.IsEmpty() && LexicalDefaultsMatch(Pin->DefaultTextValue.ToString(), MetadataDefault))
		{
			return Pin->DefaultValue.IsEmpty();
		}
		return false;
	}

	return Pin->DefaultValue == TEXT("None") && Pin->DefaultTextValue.IsEmpty();
}
}

FAssetDocumentCapabilityResult FAssetDocumentK2CallFunctionNodeAdapter::ConfigureNodeForApply(
	const FAssetDocumentNodeApplyContext& Context,
	UEdGraphNode* Node,
	const FAssetDocumentNodeSpec& NodeSpec) const
{
	UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
	if (!CallNode)
	{
		return FAssetDocumentCapabilityResult::Failure(
			FString::Printf(TEXT("Graph node '%s' is not a K2 call function node"), *NodeSpec.Id),
			Context.NodePath,
			TEXT("UnsupportedGraphNodeClass"));
	}

	UFunction* Function = ResolveMemberFunction(Context.Blueprint, NodeSpec.Member);
	if (!Function)
	{
		return NodeSpec.Member.IsValid()
			? UnresolvedMemberFailure(Context, NodeSpec)
			: MissingMemberFailure(Context, NodeSpec);
	}

	CallNode->SetFromFunction(Function);
	return FAssetDocumentCapabilityResult::Success();
}

bool FAssetDocumentK2CallFunctionNodeAdapter::DoesNodeMatchSpec(
	const UBlueprint* Blueprint,
	const UEdGraphNode* Node,
	const FAssetDocumentNodeSpec& NodeSpec) const
{
	const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
	if (!CallNode || Node->GetClass()->GetPathName() != NodeSpec.Class)
	{
		return false;
	}

	UFunction* DesiredFunction = ResolveMemberFunction(Blueprint, NodeSpec.Member);
	UFunction* CurrentFunction = CallNode->GetTargetFunction();
	return DesiredFunction && CurrentFunction == DesiredFunction;
}

bool FAssetDocumentK2CallFunctionNodeAdapter::ExtractNode(const UBlueprint* Blueprint, const UK2Node_CallFunction* Node, FAssetDocumentNodeSpec& OutNode) const
{
	if (!Node)
	{
		return false;
	}

	UFunction* Function = Node->GetTargetFunction();
	if (!Function)
	{
		Function = Node->FunctionReference.ResolveMember<UFunction>(Blueprint ? Blueprint->SkeletonGeneratedClass : nullptr, true);
	}
	if (!Function)
	{
		return false;
	}

	OutNode.Capability = GetCapability();
	OutNode.Member = MakeMemberRef(Function, Node->FunctionReference.GetMemberGuid());

	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (!ShouldExtractInputDefault(Pin))
		{
			continue;
		}
		if (IsBaselineFunctionPinDefault(Pin, Function))
		{
			continue;
		}

		FAssetDocumentPinOverrideSpec Override;
		Override.Pin = Pin->PinName.ToString();
		if (!Pin->DefaultValue.IsEmpty())
		{
			Override.DefaultValue = MakeShared<FJsonValueString>(Pin->DefaultValue);
		}
		if (Pin->DefaultObject)
		{
			Override.DefaultObject = MakeShared<FJsonValueString>(Pin->DefaultObject->GetPathName());
		}
		if (!Pin->DefaultTextValue.IsEmpty())
		{
			Override.DefaultTextValue = MakeShared<FJsonValueString>(Pin->DefaultTextValue.ToString());
		}
		OutNode.PinOverrides.Add(MoveTemp(Override));
	}
	return true;
}
