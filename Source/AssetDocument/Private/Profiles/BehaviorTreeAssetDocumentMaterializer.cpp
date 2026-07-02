// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/BehaviorTreeAssetDocumentMaterializer.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "Regions/AssetDocumentReflectedPropertyUtils.h"
#include "Utils/ClassFinderUtils.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Dom/JsonValue.h"

namespace
{
constexpr const TCHAR* IdField = TEXT("Id");
constexpr const TCHAR* ClassField = TEXT("Class");
constexpr const TCHAR* PropertiesField = TEXT("Properties");
constexpr const TCHAR* RootField = TEXT("Root");
constexpr const TCHAR* RootDecoratorsField = TEXT("RootDecorators");
constexpr const TCHAR* RootDecoratorLogicField = TEXT("RootDecoratorLogic");
constexpr const TCHAR* ChildrenField = TEXT("Children");
constexpr const TCHAR* ChildField = TEXT("Child");
constexpr const TCHAR* DecoratorsField = TEXT("Decorators");
constexpr const TCHAR* DecoratorLogicField = TEXT("DecoratorLogic");
constexpr const TCHAR* ServicesField = TEXT("Services");
constexpr const TCHAR* OperationField = TEXT("Operation");
constexpr const TCHAR* NumberField = TEXT("Number");

enum class EBehaviorTreeNodeRole
{
	RootComposite,
	ChildComposite,
	LeafTask,
};

struct FBehaviorTreeAttachmentSpec
{
	FString Id;
	FString ClassPath;
	FString JsonPath;
	UClass* NodeClass = nullptr;
	TSharedPtr<FJsonObject> Properties;
};

struct FBehaviorTreeDecoratorLogicSpec
{
	EBTDecoratorLogic::Type Operation = EBTDecoratorLogic::Invalid;
	uint16 Number = 0;
};

struct FBehaviorTreeChildSpec;

struct FBehaviorTreeNodeSpec
{
	FString Id;
	FString ClassPath;
	FString JsonPath;
	UClass* NodeClass = nullptr;
	EBehaviorTreeNodeRole Role = EBehaviorTreeNodeRole::LeafTask;
	TSharedPtr<FJsonObject> Properties;
	TArray<FBehaviorTreeAttachmentSpec> Services;
	TArray<TSharedPtr<FBehaviorTreeChildSpec>> Children;
};

struct FBehaviorTreeChildSpec
{
	FBehaviorTreeNodeSpec Child;
	TArray<FBehaviorTreeAttachmentSpec> Decorators;
	TArray<FBehaviorTreeDecoratorLogicSpec> DecoratorLogic;
};

struct FBehaviorTreeSpec
{
	FBehaviorTreeNodeSpec Root;
	TArray<FBehaviorTreeAttachmentSpec> RootDecorators;
	TArray<FBehaviorTreeDecoratorLogicSpec> RootDecoratorLogic;
	bool bHasSemanticRoot = false;
};

FString RegionPath(const FAssetDocumentRegionContext& Context)
{
	return Context.JsonPointer.IsEmpty() ? Context.BodyPath : Context.JsonPointer;
}

FString JoinPath(const FString& Path, const FString& Token)
{
	return FString::Printf(TEXT("%s/%s"), *Path, *FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Token));
}

FString JoinPath(const FString& Path, int32 Index)
{
	return FString::Printf(TEXT("%s/%d"), *Path, Index);
}

FAssetDocumentCapabilityResult Failure(const FString& Path, const FString& Code, const FString& Message)
{
	return FAssetDocumentJsonRegionUtils::Failure(Path, Code, Message);
}

FAssetDocumentCapabilityResult RequireObjectField(
	const TSharedRef<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	TSharedPtr<FJsonObject>& OutObject,
	const FString& Code)
{
	const TSharedPtr<FJsonValue> FieldValue = Object->TryGetField(FieldName);
	if (!FieldValue.IsValid() || FieldValue->Type != EJson::Object || !FieldValue->AsObject().IsValid())
	{
		return Failure(Path, Code, FString::Printf(TEXT("%s must be a JSON object"), *FieldName));
	}

	OutObject = FieldValue->AsObject();
	return FAssetDocumentCapabilityResult::Success();
}

FString NodePath(const FAssetDocumentRegionContext& Context, const FString& NodeId)
{
	return JoinPath(RegionPath(Context), NodeId);
}

FString RootDecoratorPath(const FAssetDocumentRegionContext& Context, const FString& DecoratorId)
{
	return JoinPath(JoinPath(RegionPath(Context), RootDecoratorsField), DecoratorId);
}

FString ChildEdgePath(const FAssetDocumentRegionContext& Context, const FString& ParentId, const FString& ChildId)
{
	return JoinPath(JoinPath(NodePath(Context, ParentId), ChildrenField), ChildId);
}

FString EdgeDecoratorPath(const FAssetDocumentRegionContext& Context, const FString& ParentId, const FString& ChildId, const FString& DecoratorId)
{
	return JoinPath(JoinPath(ChildEdgePath(Context, ParentId, ChildId), DecoratorsField), DecoratorId);
}

FString ServicePath(const FAssetDocumentRegionContext& Context, const FString& OwnerId, const FString& ServiceId)
{
	return JoinPath(JoinPath(NodePath(Context, OwnerId), ServicesField), ServiceId);
}

FString ClassPathForOutput(const UClass* Class)
{
	return Class ? Class->GetPathName() : FString();
}

bool IsEmptyRootObject(const TSharedRef<FJsonObject>& Tree)
{
	const TSharedPtr<FJsonObject>* RootObject = nullptr;
	return Tree->TryGetObjectField(RootField, RootObject)
		&& RootObject
		&& RootObject->IsValid()
		&& (*RootObject)->Values.Num() == 0;
}

TSharedRef<FJsonObject> MakeEmptyTree()
{
	TSharedRef<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetArrayField(RootDecoratorsField, {});
	Tree->SetArrayField(RootDecoratorLogicField, {});
	Tree->SetObjectField(RootField, MakeShared<FJsonObject>());
	return Tree;
}

UClass* ResolveNodeClass(const FString& ClassPath, UClass* RequiredBaseClass)
{
	const FString TrimmedClassPath = ClassPath.TrimStartAndEnd();
	if (TrimmedClassPath.IsEmpty())
	{
		return nullptr;
	}

	if (UClass* LoadedClass = StaticLoadClass(RequiredBaseClass, nullptr, *TrimmedClassPath))
	{
		return LoadedClass->IsChildOf(RequiredBaseClass) ? LoadedClass : nullptr;
	}

	return FClassFinderUtils::FindClassByName(TrimmedClassPath, RequiredBaseClass, true);
}

FAssetDocumentCapabilityResult RequireClass(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	UClass* RequiredBaseClass,
	const FString& DiagnosticCode,
	FString& OutClassPath,
	UClass*& OutClass)
{
	OutClassPath.Reset();
	OutClass = nullptr;

	FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireStringField(
		Object,
		ClassField,
		JoinPath(Path, ClassField),
		OutClassPath,
		DiagnosticCode);
	if (!Result.bSuccess)
	{
		return Result;
	}

	OutClass = ResolveNodeClass(OutClassPath, RequiredBaseClass);
	if (!OutClass)
	{
		return Failure(
			JoinPath(Path, ClassField),
			DiagnosticCode,
			FString::Printf(TEXT("BehaviorTree class '%s' did not resolve to %s"), *OutClassPath, *RequiredBaseClass->GetName()));
	}
	if (OutClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return Failure(
			JoinPath(Path, ClassField),
			DiagnosticCode,
			FString::Printf(TEXT("BehaviorTree class '%s' is abstract"), *OutClassPath));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadProperties(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	TSharedPtr<FJsonObject>& OutProperties)
{
	OutProperties = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonValue> PropertiesValue = Object->TryGetField(PropertiesField);
	if (!PropertiesValue.IsValid())
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (PropertiesValue->Type != EJson::Object || !PropertiesValue->AsObject().IsValid())
	{
		return Failure(JoinPath(Path, PropertiesField), TEXT("InvalidTreeProperties"), TEXT("BehaviorTree node Properties must be an object"));
	}

	OutProperties = PropertiesValue->AsObject();
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateAttachmentProperties(
	const FBehaviorTreeAttachmentSpec& Spec,
	UClass* RequiredBaseClass,
	UObject* Outer)
{
	UBTNode* PreviewNode = NewObject<UBTNode>(Outer, Spec.NodeClass, NAME_None, RF_Transactional);
	if (!PreviewNode || !PreviewNode->GetClass()->IsChildOf(RequiredBaseClass))
	{
		return Failure(Spec.JsonPath, TEXT("InvalidBehaviorTreeNodeClass"), TEXT("Failed to instantiate BehaviorTree attachment node"));
	}
	return FAssetDocumentReflectedPropertyUtils::ValidateProperties(
		PreviewNode,
		Spec.Properties.ToSharedRef(),
		JoinPath(Spec.JsonPath, PropertiesField));
}

FAssetDocumentCapabilityResult ValidateNodeProperties(const FBehaviorTreeNodeSpec& Spec, UObject* Outer)
{
	UBTNode* PreviewNode = NewObject<UBTNode>(Outer, Spec.NodeClass, NAME_None, RF_Transactional);
	if (!PreviewNode)
	{
		return Failure(Spec.JsonPath, TEXT("InvalidBehaviorTreeNodeClass"), TEXT("Failed to instantiate BehaviorTree node"));
	}
	return FAssetDocumentReflectedPropertyUtils::ValidateProperties(
		PreviewNode,
		Spec.Properties.ToSharedRef(),
		JoinPath(Spec.JsonPath, PropertiesField));
}

FAssetDocumentCapabilityResult ParseAttachment(
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	UClass* RequiredBaseClass,
	const FString& ClassDiagnosticCode,
	FBehaviorTreeAttachmentSpec& OutSpec)
{
	TSharedPtr<FJsonObject> Object;
	FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(Value, Path, Object);
	if (!Result.bSuccess)
	{
		return Failure(Path, TEXT("InvalidTreeNode"), TEXT("BehaviorTree attachment entries must be objects"));
	}

	Result = FAssetDocumentJsonRegionUtils::RequireStringField(Object, IdField, JoinPath(Path, IdField), OutSpec.Id, TEXT("MissingTreeNodeId"));
	if (!Result.bSuccess)
	{
		return Result;
	}

	OutSpec.JsonPath = Path;
	Result = RequireClass(Object.ToSharedRef(), Path, RequiredBaseClass, ClassDiagnosticCode, OutSpec.ClassPath, OutSpec.NodeClass);
	if (!Result.bSuccess)
	{
		return Result;
	}

	return ReadProperties(Object.ToSharedRef(), Path, OutSpec.Properties);
}

bool TryReadLogicOperation(const FString& Operation, EBTDecoratorLogic::Type& OutOperation)
{
	if (Operation == TEXT("Test"))
	{
		OutOperation = EBTDecoratorLogic::Test;
		return true;
	}
	if (Operation == TEXT("And"))
	{
		OutOperation = EBTDecoratorLogic::And;
		return true;
	}
	if (Operation == TEXT("Or"))
	{
		OutOperation = EBTDecoratorLogic::Or;
		return true;
	}
	if (Operation == TEXT("Not"))
	{
		OutOperation = EBTDecoratorLogic::Not;
		return true;
	}
	return false;
}

FAssetDocumentCapabilityResult ParseDecoratorLogic(
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	FBehaviorTreeDecoratorLogicSpec& OutSpec)
{
	TSharedPtr<FJsonObject> Object;
	FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(Value, Path, Object);
	if (!Result.bSuccess)
	{
		return Failure(Path, TEXT("InvalidBehaviorTreeDecoratorLogic"), TEXT("Decorator logic entries must be objects"));
	}

	FString OperationString;
	Result = FAssetDocumentJsonRegionUtils::RequireStringField(
		Object,
		OperationField,
		JoinPath(Path, OperationField),
		OperationString,
		TEXT("InvalidBehaviorTreeDecoratorLogic"));
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (!TryReadLogicOperation(OperationString, OutSpec.Operation))
	{
		return Failure(JoinPath(Path, OperationField), TEXT("InvalidBehaviorTreeDecoratorLogic"), FString::Printf(TEXT("Unsupported decorator logic operation '%s'"), *OperationString));
	}

	double Number = 0.0;
	if (Object->HasField(NumberField))
	{
		if (!Object->TryGetNumberField(NumberField, Number) || Number < 0.0 || Number > TNumericLimits<uint16>::Max() || FMath::TruncToDouble(Number) != Number)
		{
			return Failure(JoinPath(Path, NumberField), TEXT("InvalidBehaviorTreeDecoratorLogic"), TEXT("Decorator logic Number must be a uint16 integer"));
		}
	}
	OutSpec.Number = static_cast<uint16>(Number);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateDecoratorLogicShape(
	const TArray<FBehaviorTreeAttachmentSpec>& Decorators,
	const TArray<FBehaviorTreeDecoratorLogicSpec>& Logic,
	const FString& LogicPath)
{
	if (Logic.Num() == 0)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TFunction<FAssetDocumentCapabilityResult(int32&)> ConsumeExpression;
	ConsumeExpression = [&](int32& Index) -> FAssetDocumentCapabilityResult
	{
		if (!Logic.IsValidIndex(Index))
		{
			return Failure(LogicPath, TEXT("InvalidBehaviorTreeDecoratorLogicShape"), TEXT("Decorator logic expression does not contain enough operands"));
		}

		const FBehaviorTreeDecoratorLogicSpec& Entry = Logic[Index];
		const FString EntryPath = JoinPath(LogicPath, Index);
		++Index;

		if (Entry.Operation == EBTDecoratorLogic::Test)
		{
			if (Entry.Number >= Decorators.Num())
			{
				return Failure(EntryPath, TEXT("InvalidBehaviorTreeDecoratorLogicShape"), TEXT("Decorator logic Test references a missing decorator index"));
			}
			return FAssetDocumentCapabilityResult::Success();
		}

		if (Entry.Operation == EBTDecoratorLogic::Not)
		{
			if (Entry.Number != 1)
			{
				return Failure(EntryPath, TEXT("InvalidBehaviorTreeDecoratorLogicShape"), TEXT("Not decorator logic must span exactly one entry"));
			}
			return ConsumeExpression(Index);
		}

		if (Entry.Operation == EBTDecoratorLogic::And || Entry.Operation == EBTDecoratorLogic::Or)
		{
			if (Entry.Number < 2)
			{
				return Failure(EntryPath, TEXT("InvalidBehaviorTreeDecoratorLogicShape"), TEXT("And/Or decorator logic must span at least two entries"));
			}
			for (uint16 OperandIndex = 0; OperandIndex < Entry.Number; ++OperandIndex)
			{
				FAssetDocumentCapabilityResult Result = ConsumeExpression(Index);
				if (!Result.bSuccess)
				{
					return Result;
				}
			}
			return FAssetDocumentCapabilityResult::Success();
		}

		return Failure(EntryPath, TEXT("InvalidBehaviorTreeDecoratorLogicShape"), TEXT("Unsupported decorator logic operation"));
	};

	int32 ConsumedIndex = 0;
	FAssetDocumentCapabilityResult Result = ConsumeExpression(ConsumedIndex);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (ConsumedIndex != Logic.Num())
	{
		return Failure(JoinPath(LogicPath, ConsumedIndex), TEXT("InvalidBehaviorTreeDecoratorLogicShape"), TEXT("Decorator logic expression has unconsumed entries"));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseDecoratorLogicArray(
	const TSharedRef<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	TArray<FBehaviorTreeDecoratorLogicSpec>& OutLogic)
{
	OutLogic.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Object->TryGetArrayField(FieldName, Values) || !Values)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	for (int32 Index = 0; Index < Values->Num(); ++Index)
	{
		FBehaviorTreeDecoratorLogicSpec Spec;
		const FAssetDocumentCapabilityResult Result = ParseDecoratorLogic((*Values)[Index], JoinPath(JoinPath(Path, FieldName), Index), Spec);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutLogic.Add(Spec);
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseAttachmentArray(
	const TSharedRef<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	UClass* RequiredBaseClass,
	const FString& ClassDiagnosticCode,
	TArray<FBehaviorTreeAttachmentSpec>& OutSpecs)
{
	OutSpecs.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Object->TryGetArrayField(FieldName, Values) || !Values)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	for (int32 Index = 0; Index < Values->Num(); ++Index)
	{
		FBehaviorTreeAttachmentSpec Spec;
		const FAssetDocumentCapabilityResult Result = ParseAttachment(
			(*Values)[Index],
			JoinPath(JoinPath(Path, FieldName), Index),
			RequiredBaseClass,
			ClassDiagnosticCode,
			Spec);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutSpecs.Add(Spec);
	}
	return FAssetDocumentCapabilityResult::Success();
}

bool ShouldParseAsComposite(const TSharedRef<FJsonObject>& Object, bool bIsRoot)
{
	return bIsRoot || Object->HasField(ChildrenField) || Object->HasField(ServicesField);
}

FAssetDocumentCapabilityResult ParseNode(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	bool bIsRoot,
	FBehaviorTreeNodeSpec& OutSpec);

FAssetDocumentCapabilityResult ParseChildEdge(
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	FBehaviorTreeChildSpec& OutSpec)
{
	TSharedPtr<FJsonObject> EdgeObject;
	FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(Value, Path, EdgeObject);
	if (!Result.bSuccess)
	{
		return Failure(Path, TEXT("InvalidBehaviorTreeChildEdge"), TEXT("BehaviorTree child edge must be an object"));
	}

	TSharedPtr<FJsonObject> ChildObject;
	Result = RequireObjectField(EdgeObject.ToSharedRef(), ChildField, JoinPath(Path, ChildField), ChildObject, TEXT("InvalidBehaviorTreeChildEdge"));
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ParseNode(ChildObject.ToSharedRef(), JoinPath(Path, ChildField), false, OutSpec.Child);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ParseAttachmentArray(EdgeObject.ToSharedRef(), DecoratorsField, Path, UBTDecorator::StaticClass(), TEXT("InvalidBehaviorTreeDecoratorClass"), OutSpec.Decorators);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ParseDecoratorLogicArray(EdgeObject.ToSharedRef(), DecoratorLogicField, Path, OutSpec.DecoratorLogic);
	if (!Result.bSuccess)
	{
		return Result;
	}

	return ValidateDecoratorLogicShape(OutSpec.Decorators, OutSpec.DecoratorLogic, JoinPath(Path, DecoratorLogicField));
}

FAssetDocumentCapabilityResult ParseNode(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	bool bIsRoot,
	FBehaviorTreeNodeSpec& OutSpec)
{
	if (Object->HasField(DecoratorsField))
	{
		return Failure(JoinPath(Path, DecoratorsField), TEXT("UnsupportedBehaviorTreeNodeDecorators"), TEXT("BehaviorTree node-level Decorators are not part of the authored schema; use root or child-edge decorators"));
	}
	if (Object->HasField(DecoratorLogicField))
	{
		return Failure(JoinPath(Path, DecoratorLogicField), TEXT("UnsupportedBehaviorTreeNodeDecorators"), TEXT("BehaviorTree node-level DecoratorLogic is not part of the authored schema; use root or child-edge decorator logic"));
	}

	FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireStringField(Object, IdField, JoinPath(Path, IdField), OutSpec.Id, TEXT("MissingTreeNodeId"));
	if (!Result.bSuccess)
	{
		return Result;
	}

	OutSpec.JsonPath = Path;
	OutSpec.Role = ShouldParseAsComposite(Object, bIsRoot)
		? (bIsRoot ? EBehaviorTreeNodeRole::RootComposite : EBehaviorTreeNodeRole::ChildComposite)
		: EBehaviorTreeNodeRole::LeafTask;
	UClass* RequiredClass = (OutSpec.Role == EBehaviorTreeNodeRole::LeafTask)
		? UBTTaskNode::StaticClass()
		: UBTCompositeNode::StaticClass();
	Result = RequireClass(Object, Path, RequiredClass, TEXT("InvalidBehaviorTreeNodeClass"), OutSpec.ClassPath, OutSpec.NodeClass);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ReadProperties(Object, Path, OutSpec.Properties);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ParseAttachmentArray(Object, ServicesField, Path, UBTService::StaticClass(), TEXT("InvalidBehaviorTreeServiceClass"), OutSpec.Services);
	if (!Result.bSuccess)
	{
		return Result;
	}

	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (Object->TryGetArrayField(ChildrenField, Children) && Children)
	{
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			TSharedPtr<FBehaviorTreeChildSpec> ChildSpec = MakeShared<FBehaviorTreeChildSpec>();
			Result = ParseChildEdge((*Children)[Index], JoinPath(JoinPath(Path, ChildrenField), Index), *ChildSpec);
			if (!Result.bSuccess)
			{
				return Result;
			}
			OutSpec.Children.Add(ChildSpec);
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseTreeSpec(
	const FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& Tree,
	FBehaviorTreeSpec& OutSpec)
{
	if (IsEmptyRootObject(Tree))
	{
		OutSpec.bHasSemanticRoot = false;
		return FAssetDocumentCapabilityResult::Success();
	}

	TSharedPtr<FJsonObject> RootObject;
	FAssetDocumentCapabilityResult Result = RequireObjectField(
		Tree,
		RootField,
		JoinPath(RegionPath(Context), RootField),
		RootObject,
		TEXT("MissingTreeRoot"));
	if (!Result.bSuccess)
	{
		return Result;
	}

	OutSpec.bHasSemanticRoot = true;
	Result = ParseNode(RootObject.ToSharedRef(), JoinPath(RegionPath(Context), RootField), true, OutSpec.Root);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ParseAttachmentArray(Tree, RootDecoratorsField, RegionPath(Context), UBTDecorator::StaticClass(), TEXT("InvalidBehaviorTreeRootDecoratorClass"), OutSpec.RootDecorators);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = ParseDecoratorLogicArray(Tree, RootDecoratorLogicField, RegionPath(Context), OutSpec.RootDecoratorLogic);
	if (!Result.bSuccess)
	{
		return Result;
	}

	return ValidateDecoratorLogicShape(OutSpec.RootDecorators, OutSpec.RootDecoratorLogic, JoinPath(RegionPath(Context), RootDecoratorLogicField));
}

FAssetDocumentCapabilityResult ValidateSpecProperties(const FBehaviorTreeNodeSpec& Spec, UObject* Outer)
{
	FAssetDocumentCapabilityResult Result = ValidateNodeProperties(Spec, Outer);
	if (!Result.bSuccess)
	{
		return Result;
	}

	for (const FBehaviorTreeAttachmentSpec& Service : Spec.Services)
	{
		Result = ValidateAttachmentProperties(Service, UBTService::StaticClass(), Outer);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	for (const TSharedPtr<FBehaviorTreeChildSpec>& Child : Spec.Children)
	{
		if (!Child.IsValid())
		{
			continue;
		}
		for (const FBehaviorTreeAttachmentSpec& Decorator : Child->Decorators)
		{
			Result = ValidateAttachmentProperties(Decorator, UBTDecorator::StaticClass(), Outer);
			if (!Result.bSuccess)
			{
				return Result;
			}
		}
		Result = ValidateSpecProperties(Child->Child, Outer);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateSpec(const FBehaviorTreeSpec& Spec, UObject* Outer)
{
	if (!Spec.bHasSemanticRoot)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	for (const FBehaviorTreeAttachmentSpec& RootDecorator : Spec.RootDecorators)
	{
		const FAssetDocumentCapabilityResult Result = ValidateAttachmentProperties(RootDecorator, UBTDecorator::StaticClass(), Outer);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	return ValidateSpecProperties(Spec.Root, Outer);
}

FAssetDocumentCapabilityResult ApplyPropertiesToNode(UBTNode* Node, const TSharedRef<FJsonObject>& Properties, const FString& Path)
{
	return FAssetDocumentReflectedPropertyUtils::ApplyProperties(Node, Properties, JoinPath(Path, PropertiesField));
}

UBTNode* CreateBtNode(UBehaviorTree* BehaviorTree, UClass* NodeClass, const FString& Id)
{
	UBTNode* Node = NewObject<UBTNode>(BehaviorTree, NodeClass, NAME_None, RF_Transactional);
	if (Node)
	{
		Node->NodeName = Id;
	}
	return Node;
}

FAssetDocumentCapabilityResult MaterializeDecorators(
	UBehaviorTree* BehaviorTree,
	const TArray<FBehaviorTreeAttachmentSpec>& Specs,
	TArray<TObjectPtr<UBTDecorator>>& OutDecorators)
{
	OutDecorators.Reset();
	for (const FBehaviorTreeAttachmentSpec& Spec : Specs)
	{
		UBTNode* Node = CreateBtNode(BehaviorTree, Spec.NodeClass, Spec.Id);
		UBTDecorator* Decorator = Cast<UBTDecorator>(Node);
		if (!Decorator)
		{
			return Failure(Spec.JsonPath, TEXT("InvalidBehaviorTreeDecoratorClass"), TEXT("Resolved class is not a UBTDecorator"));
		}

		const FAssetDocumentCapabilityResult PropertiesResult = ApplyPropertiesToNode(Decorator, Spec.Properties.ToSharedRef(), Spec.JsonPath);
		if (!PropertiesResult.bSuccess)
		{
			return PropertiesResult;
		}
		OutDecorators.Add(Decorator);
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult MaterializeServices(
	UBehaviorTree* BehaviorTree,
	const TArray<FBehaviorTreeAttachmentSpec>& Specs,
	TArray<TObjectPtr<UBTService>>& OutServices)
{
	OutServices.Reset();
	for (const FBehaviorTreeAttachmentSpec& Spec : Specs)
	{
		UBTNode* Node = CreateBtNode(BehaviorTree, Spec.NodeClass, Spec.Id);
		UBTService* Service = Cast<UBTService>(Node);
		if (!Service)
		{
			return Failure(Spec.JsonPath, TEXT("InvalidBehaviorTreeServiceClass"), TEXT("Resolved class is not a UBTService"));
		}

		const FAssetDocumentCapabilityResult PropertiesResult = ApplyPropertiesToNode(Service, Spec.Properties.ToSharedRef(), Spec.JsonPath);
		if (!PropertiesResult.bSuccess)
		{
			return PropertiesResult;
		}
		OutServices.Add(Service);
	}
	return FAssetDocumentCapabilityResult::Success();
}

TArray<FBTDecoratorLogic> MakeRuntimeDecoratorLogic(const TArray<FBehaviorTreeDecoratorLogicSpec>& Specs)
{
	TArray<FBTDecoratorLogic> Logic;
	Logic.Reserve(Specs.Num());
	for (const FBehaviorTreeDecoratorLogicSpec& Spec : Specs)
	{
		Logic.Add(FBTDecoratorLogic(static_cast<uint8>(Spec.Operation), Spec.Number));
	}
	return Logic;
}

FAssetDocumentCapabilityResult MaterializeNode(UBehaviorTree* BehaviorTree, const FBehaviorTreeNodeSpec& Spec, UBTNode*& OutNode)
{
	OutNode = CreateBtNode(BehaviorTree, Spec.NodeClass, Spec.Id);
	if (!OutNode)
	{
		return Failure(Spec.JsonPath, TEXT("InvalidBehaviorTreeNodeClass"), TEXT("Failed to create BehaviorTree node"));
	}

	const FAssetDocumentCapabilityResult PropertiesResult = ApplyPropertiesToNode(OutNode, Spec.Properties.ToSharedRef(), Spec.JsonPath);
	if (!PropertiesResult.bSuccess)
	{
		return PropertiesResult;
	}

	if (Spec.Role == EBehaviorTreeNodeRole::LeafTask)
	{
		if (!Cast<UBTTaskNode>(OutNode))
		{
			return Failure(Spec.JsonPath, TEXT("InvalidBehaviorTreeNodeClass"), TEXT("Leaf BehaviorTree node must be a UBTTaskNode"));
		}
		return FAssetDocumentCapabilityResult::Success();
	}

	UBTCompositeNode* Composite = Cast<UBTCompositeNode>(OutNode);
	if (!Composite)
	{
		return Failure(Spec.JsonPath, TEXT("InvalidBehaviorTreeNodeClass"), TEXT("Composite BehaviorTree node must be a UBTCompositeNode"));
	}

	FAssetDocumentCapabilityResult Result = MaterializeServices(BehaviorTree, Spec.Services, Composite->Services);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Composite->Children.Reset();
	for (const TSharedPtr<FBehaviorTreeChildSpec>& ChildSpec : Spec.Children)
	{
		if (!ChildSpec.IsValid())
		{
			continue;
		}

		UBTNode* ChildNode = nullptr;
		Result = MaterializeNode(BehaviorTree, ChildSpec->Child, ChildNode);
		if (!Result.bSuccess)
		{
			return Result;
		}

		FBTCompositeChild RuntimeChild;
		if (UBTCompositeNode* ChildComposite = Cast<UBTCompositeNode>(ChildNode))
		{
			RuntimeChild.ChildComposite = ChildComposite;
		}
		else if (UBTTaskNode* ChildTask = Cast<UBTTaskNode>(ChildNode))
		{
			RuntimeChild.ChildTask = ChildTask;
		}
		else
		{
			return Failure(ChildSpec->Child.JsonPath, TEXT("InvalidBehaviorTreeChildEdge"), TEXT("Child edge must target a composite or task node"));
		}

		Result = MaterializeDecorators(BehaviorTree, ChildSpec->Decorators, RuntimeChild.Decorators);
		if (!Result.bSuccess)
		{
			return Result;
		}
		RuntimeChild.DecoratorOps = MakeRuntimeDecoratorLogic(ChildSpec->DecoratorLogic);
		Composite->Children.Add(MoveTemp(RuntimeChild));
	}

	return FAssetDocumentCapabilityResult::Success();
}

void InitializeNodeRecursive(UBehaviorTree& BehaviorTree, UBTCompositeNode* Parent, UBTNode* Node, uint8 Depth, uint16& ExecutionIndex)
{
	if (!Node)
	{
		return;
	}

	Node->InitializeNode(Parent, ExecutionIndex, 0, Depth);
	Node->InitializeFromAsset(BehaviorTree);
	++ExecutionIndex;

	UBTCompositeNode* Composite = Cast<UBTCompositeNode>(Node);
	if (!Composite)
	{
		return;
	}

	for (UBTService* Service : Composite->Services)
	{
		if (!Service)
		{
			continue;
		}
		Service->InitializeNode(Composite, ExecutionIndex, 0, Depth);
		Service->InitializeFromAsset(BehaviorTree);
		++ExecutionIndex;
	}

	for (int32 ChildIndex = 0; ChildIndex < Composite->Children.Num(); ++ChildIndex)
	{
		FBTCompositeChild& Child = Composite->Children[ChildIndex];
		const uint8 ParentLinkIndex = IntCastChecked<uint8>(ChildIndex);
		for (UBTDecorator* Decorator : Child.Decorators)
		{
			if (!Decorator)
			{
				continue;
			}
			Decorator->InitializeNode(Composite, ExecutionIndex, 0, Depth);
			Decorator->InitializeFromAsset(BehaviorTree);
			Decorator->InitializeParentLink(ParentLinkIndex);
			Decorator->UpdateFlowAbortMode();
			++ExecutionIndex;
		}

		InitializeNodeRecursive(BehaviorTree, Composite, Composite->GetChildNode(ChildIndex), Depth + 1, ExecutionIndex);
	}

	Composite->InitializeComposite(ExecutionIndex - 1);
}

void InitializeRootDecorators(UBehaviorTree& BehaviorTree, uint16& ExecutionIndex)
{
	for (UBTDecorator* Decorator : BehaviorTree.RootDecorators)
	{
		if (!Decorator)
		{
			continue;
		}
		Decorator->InitializeNode(nullptr, ExecutionIndex, 0, 0);
		Decorator->InitializeFromAsset(BehaviorTree);
		Decorator->UpdateFlowAbortMode();
		++ExecutionIndex;
	}
}

FAssetDocumentCapabilityResult MaterializeTree(UBehaviorTree* BehaviorTree, const FBehaviorTreeSpec& Spec)
{
	if (!BehaviorTree)
	{
		return Failure(TEXT("/Body/Tree"), TEXT("UnsupportedAsset"), TEXT("BehaviorTree Tree apply requires UBehaviorTree asset"));
	}

	BehaviorTree->RootNode = nullptr;
	BehaviorTree->RootDecorators.Reset();
	BehaviorTree->RootDecoratorOps.Reset();
	if (!Spec.bHasSemanticRoot)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	UBTNode* RootNode = nullptr;
	FAssetDocumentCapabilityResult Result = MaterializeNode(BehaviorTree, Spec.Root, RootNode);
	if (!Result.bSuccess)
	{
		return Result;
	}

	UBTCompositeNode* RootComposite = Cast<UBTCompositeNode>(RootNode);
	if (!RootComposite)
	{
		return Failure(Spec.Root.JsonPath, TEXT("InvalidBehaviorTreeNodeClass"), TEXT("BehaviorTree root must be a composite node"));
	}

	Result = MaterializeDecorators(BehaviorTree, Spec.RootDecorators, BehaviorTree->RootDecorators);
	if (!Result.bSuccess)
	{
		return Result;
	}
	BehaviorTree->RootDecoratorOps = MakeRuntimeDecoratorLogic(Spec.RootDecoratorLogic);
	BehaviorTree->RootNode = RootComposite;

	uint16 ExecutionIndex = 0;
	InitializeRootDecorators(*BehaviorTree, ExecutionIndex);
	InitializeNodeRecursive(*BehaviorTree, nullptr, BehaviorTree->RootNode, 0, ExecutionIndex);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractProperties(UBTNode* Node, TSharedRef<FJsonObject>& OutProperties, const FString& Path)
{
	return FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(Node, OutProperties, JoinPath(Path, PropertiesField));
}

TSharedPtr<FJsonObject> MakeLogicJson(const FBTDecoratorLogic& Logic)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	switch (Logic.Operation)
	{
	case EBTDecoratorLogic::Test:
		Json->SetStringField(OperationField, TEXT("Test"));
		break;
	case EBTDecoratorLogic::And:
		Json->SetStringField(OperationField, TEXT("And"));
		break;
	case EBTDecoratorLogic::Or:
		Json->SetStringField(OperationField, TEXT("Or"));
		break;
	case EBTDecoratorLogic::Not:
		Json->SetStringField(OperationField, TEXT("Not"));
		break;
	default:
		Json->SetStringField(OperationField, TEXT("Invalid"));
		break;
	}
	Json->SetNumberField(NumberField, Logic.Number);
	return Json;
}

TArray<TSharedPtr<FJsonValue>> ExtractLogicArray(const TArray<FBTDecoratorLogic>& Logic)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Logic.Num());
	for (const FBTDecoratorLogic& Entry : Logic)
	{
		Values.Add(MakeShared<FJsonValueObject>(MakeLogicJson(Entry)));
	}
	return Values;
}

struct FBehaviorTreeExtractContext
{
	TMap<const UBTNode*, FString> IdsByNode;
};

FString DisplayNameForNode(const UBTNode* Node)
{
	return Node ? Node->NodeName.TrimStartAndEnd() : FString();
}

void CountDisplayName(const UBTNode* Node, TMap<FString, int32>& InOutCounts)
{
	const FString DisplayName = DisplayNameForNode(Node);
	if (!DisplayName.IsEmpty())
	{
		++InOutCounts.FindOrAdd(DisplayName);
	}
}

const UBTNode* ChildNodeFromCompositeChild(const FBTCompositeChild& Child)
{
	return Child.ChildComposite ? Cast<UBTNode>(Child.ChildComposite) : Cast<UBTNode>(Child.ChildTask);
}

void GatherNodeDisplayNames(const UBTNode* Node, TMap<FString, int32>& InOutCounts)
{
	if (!Node)
	{
		return;
	}

	CountDisplayName(Node, InOutCounts);
	const UBTCompositeNode* Composite = Cast<UBTCompositeNode>(Node);
	if (!Composite)
	{
		return;
	}

	for (const UBTService* Service : Composite->Services)
	{
		CountDisplayName(Service, InOutCounts);
	}

	for (const FBTCompositeChild& Child : Composite->Children)
	{
		for (const UBTDecorator* Decorator : Child.Decorators)
		{
			CountDisplayName(Decorator, InOutCounts);
		}
		GatherNodeDisplayNames(ChildNodeFromCompositeChild(Child), InOutCounts);
	}
}

FString UniqueObjectIdForNode(const UBTNode* Node)
{
	if (!Node)
	{
		return FString();
	}

	const FString ObjectName = Node->GetName();
	if (!ObjectName.IsEmpty())
	{
		return ObjectName;
	}

	return Node->GetClass() ? Node->GetClass()->GetName() : FString(TEXT("BTNode"));
}

FString StableExtractIdForNode(const UBTNode* Node, const TMap<FString, int32>& DisplayNameCounts)
{
	const FString DisplayName = DisplayNameForNode(Node);
	if (!DisplayName.IsEmpty() && DisplayNameCounts.FindRef(DisplayName) == 1)
	{
		return DisplayName;
	}
	return UniqueObjectIdForNode(Node);
}

void AssignExtractIds(const UBTNode* Node, const TMap<FString, int32>& DisplayNameCounts, FBehaviorTreeExtractContext& InOutContext)
{
	if (!Node)
	{
		return;
	}

	InOutContext.IdsByNode.Add(Node, StableExtractIdForNode(Node, DisplayNameCounts));
	const UBTCompositeNode* Composite = Cast<UBTCompositeNode>(Node);
	if (!Composite)
	{
		return;
	}

	for (const UBTService* Service : Composite->Services)
	{
		if (Service)
		{
			InOutContext.IdsByNode.Add(Service, StableExtractIdForNode(Service, DisplayNameCounts));
		}
	}

	for (const FBTCompositeChild& Child : Composite->Children)
	{
		for (const UBTDecorator* Decorator : Child.Decorators)
		{
			if (Decorator)
			{
				InOutContext.IdsByNode.Add(Decorator, StableExtractIdForNode(Decorator, DisplayNameCounts));
			}
		}
		AssignExtractIds(ChildNodeFromCompositeChild(Child), DisplayNameCounts, InOutContext);
	}
}

FBehaviorTreeExtractContext BuildExtractContext(const UBehaviorTree* BehaviorTree)
{
	FBehaviorTreeExtractContext Context;
	if (!BehaviorTree)
	{
		return Context;
	}

	TMap<FString, int32> DisplayNameCounts;
	GatherNodeDisplayNames(BehaviorTree->RootNode, DisplayNameCounts);
	for (const UBTDecorator* Decorator : BehaviorTree->RootDecorators)
	{
		CountDisplayName(Decorator, DisplayNameCounts);
	}

	AssignExtractIds(BehaviorTree->RootNode, DisplayNameCounts, Context);
	for (const UBTDecorator* Decorator : BehaviorTree->RootDecorators)
	{
		if (Decorator)
		{
			Context.IdsByNode.Add(Decorator, StableExtractIdForNode(Decorator, DisplayNameCounts));
		}
	}
	return Context;
}

FString IdForNode(const UBTNode* Node, const FBehaviorTreeExtractContext& Context)
{
	const FString* Id = Context.IdsByNode.Find(Node);
	return Id ? *Id : StableExtractIdForNode(Node, {});
}

FAssetDocumentCapabilityResult ExtractAttachment(
	UBTNode* Node,
	const FString& Path,
	const FBehaviorTreeExtractContext& ExtractContext,
	TSharedPtr<FJsonObject>& OutJson)
{
	OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(IdField, IdForNode(Node, ExtractContext));
	OutJson->SetStringField(ClassField, ClassPathForOutput(Node ? Node->GetClass() : nullptr));

	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult PropertiesResult = ExtractProperties(Node, Properties, Path);
	if (!PropertiesResult.bSuccess)
	{
		return PropertiesResult;
	}
	if (Properties->Values.Num() > 0)
	{
		OutJson->SetObjectField(PropertiesField, Properties);
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractNode(
	UBTNode* Node,
	const FString& Path,
	const FBehaviorTreeExtractContext& ExtractContext,
	TSharedPtr<FJsonObject>& OutJson)
{
	FAssetDocumentCapabilityResult Result = ExtractAttachment(Node, Path, ExtractContext, OutJson);
	if (!Result.bSuccess)
	{
		return Result;
	}

	UBTCompositeNode* Composite = Cast<UBTCompositeNode>(Node);
	if (!Composite)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TArray<TSharedPtr<FJsonValue>> Services;
	for (UBTService* Service : Composite->Services)
	{
		if (!Service)
		{
			continue;
		}
		TSharedPtr<FJsonObject> ServiceJson;
		Result = ExtractAttachment(Service, JoinPath(JoinPath(Path, ServicesField), IdForNode(Service, ExtractContext)), ExtractContext, ServiceJson);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Services.Add(MakeShared<FJsonValueObject>(ServiceJson));
	}
	OutJson->SetArrayField(ServicesField, Services);

	TArray<TSharedPtr<FJsonValue>> Children;
	for (const FBTCompositeChild& Child : Composite->Children)
	{
		UBTNode* ChildNode = Child.ChildComposite ? Cast<UBTNode>(Child.ChildComposite) : Cast<UBTNode>(Child.ChildTask);
		if (!ChildNode)
		{
			continue;
		}

		TSharedPtr<FJsonObject> ChildNodeJson;
		Result = ExtractNode(ChildNode, JoinPath(JoinPath(Path, ChildrenField), IdForNode(ChildNode, ExtractContext)), ExtractContext, ChildNodeJson);
		if (!Result.bSuccess)
		{
			return Result;
		}

		TSharedPtr<FJsonObject> EdgeJson = MakeShared<FJsonObject>();
		EdgeJson->SetObjectField(ChildField, ChildNodeJson);

		TArray<TSharedPtr<FJsonValue>> Decorators;
		for (UBTDecorator* Decorator : Child.Decorators)
		{
			if (!Decorator)
			{
				continue;
			}
			TSharedPtr<FJsonObject> DecoratorJson;
			Result = ExtractAttachment(Decorator, JoinPath(JoinPath(JoinPath(Path, ChildrenField), IdForNode(ChildNode, ExtractContext)), DecoratorsField), ExtractContext, DecoratorJson);
			if (!Result.bSuccess)
			{
				return Result;
			}
			Decorators.Add(MakeShared<FJsonValueObject>(DecoratorJson));
		}
		EdgeJson->SetArrayField(DecoratorsField, Decorators);
		EdgeJson->SetArrayField(DecoratorLogicField, ExtractLogicArray(Child.DecoratorOps));
		Children.Add(MakeShared<FJsonValueObject>(EdgeJson));
	}
	OutJson->SetArrayField(ChildrenField, Children);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractTreeObject(const UBehaviorTree* BehaviorTree, TSharedRef<FJsonObject>& OutTree)
{
	if (!BehaviorTree || !BehaviorTree->RootNode)
	{
		OutTree = MakeEmptyTree();
		return FAssetDocumentCapabilityResult::Success();
	}

	const FBehaviorTreeExtractContext ExtractContext = BuildExtractContext(BehaviorTree);
	TSharedPtr<FJsonObject> RootJson;
	FAssetDocumentCapabilityResult Result = ExtractNode(BehaviorTree->RootNode, JoinPath(TEXT("/Body/Tree"), RootField), ExtractContext, RootJson);
	if (!Result.bSuccess)
	{
		return Result;
	}
	OutTree->SetObjectField(RootField, RootJson);

	TArray<TSharedPtr<FJsonValue>> RootDecorators;
	for (UBTDecorator* Decorator : BehaviorTree->RootDecorators)
	{
		if (!Decorator)
		{
			continue;
		}
		TSharedPtr<FJsonObject> DecoratorJson;
		Result = ExtractAttachment(Decorator, JoinPath(JoinPath(TEXT("/Body/Tree"), RootDecoratorsField), IdForNode(Decorator, ExtractContext)), ExtractContext, DecoratorJson);
		if (!Result.bSuccess)
		{
			return Result;
		}
		RootDecorators.Add(MakeShared<FJsonValueObject>(DecoratorJson));
	}
	OutTree->SetArrayField(RootDecoratorsField, RootDecorators);
	OutTree->SetArrayField(RootDecoratorLogicField, ExtractLogicArray(BehaviorTree->RootDecoratorOps));
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult BuildCanonicalDesiredTree(
	const FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& DesiredTree,
	TSharedRef<FJsonObject>& OutCanonicalTree)
{
	FBehaviorTreeSpec Spec;
	FAssetDocumentCapabilityResult Result = ParseTreeSpec(Context, DesiredTree, Spec);
	if (!Result.bSuccess)
	{
		return Result;
	}

	UBehaviorTree* PreviewTree = NewObject<UBehaviorTree>(GetTransientPackage());
	Result = ValidateSpec(Spec, PreviewTree);
	if (!Result.bSuccess)
	{
		return Result;
	}
	Result = MaterializeTree(PreviewTree, Spec);
	if (!Result.bSuccess)
	{
		return Result;
	}
	return ExtractTreeObject(PreviewTree, OutCanonicalTree);
}

bool JsonEqual(const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
{
	return FAssetDocumentJsonRegionUtils::JsonValueToComparableString(Left.IsValid() ? Left : MakeShared<FJsonValueNull>())
		== FAssetDocumentJsonRegionUtils::JsonValueToComparableString(Right.IsValid() ? Right : MakeShared<FJsonValueNull>());
}

void AddComparableDiff(
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries,
	const FString& Path,
	const TSharedPtr<FJsonValue>& CurrentValue,
	const TSharedPtr<FJsonValue>& DesiredValue)
{
	const FString Status = CurrentValue.IsValid()
		? (DesiredValue.IsValid() ? (JsonEqual(CurrentValue, DesiredValue) ? TEXT("unchanged") : TEXT("changed")) : TEXT("changed"))
		: TEXT("changed");
	FAssetDocumentJsonRegionUtils::AddDiffEntry(OutDiffEntries, Path, Status, CurrentValue, DesiredValue);
}

TSharedPtr<FJsonValue> ObjectValue(const TSharedPtr<FJsonObject>& Object)
{
	if (!Object.IsValid())
	{
		return nullptr;
	}
	return MakeShared<FJsonValueObject>(Object.ToSharedRef());
}

TSharedPtr<FJsonObject> ObjectFromValue(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
}

TSharedPtr<FJsonValue> StringArrayValue(const TArray<FString>& Values)
{
	TArray<TSharedPtr<FJsonValue>> JsonValues;
	JsonValues.Reserve(Values.Num());
	for (const FString& Value : Values)
	{
		JsonValues.Add(MakeShared<FJsonValueString>(Value));
	}
	return MakeShared<FJsonValueArray>(JsonValues);
}

void AddMapDiffs(
	const TMap<FString, TSharedPtr<FJsonValue>>& Current,
	const TMap<FString, TSharedPtr<FJsonValue>>& Desired,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
{
	TSet<FString> PathSet;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Current)
	{
		PathSet.Add(Entry.Key);
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Desired)
	{
		PathSet.Add(Entry.Key);
	}
	TArray<FString> Paths = PathSet.Array();
	Paths.Sort([](const FString& Left, const FString& Right)
	{
		return Left < Right;
	});

	for (const FString& Path : Paths)
	{
		AddComparableDiff(OutDiffEntries, Path, Current.FindRef(Path), Desired.FindRef(Path));
	}
}

void CollectAttachmentDiffValue(
	const TSharedPtr<FJsonObject>& Object,
	const FString& Path,
	TMap<FString, TSharedPtr<FJsonValue>>& OutMap)
{
	if (!Object.IsValid())
	{
		return;
	}

	TSharedPtr<FJsonObject> OwnObject = MakeShared<FJsonObject>();
	if (Object->HasField(IdField))
	{
		OwnObject->SetField(IdField, Object->TryGetField(IdField));
	}
	if (Object->HasField(ClassField))
	{
		OwnObject->SetField(ClassField, Object->TryGetField(ClassField));
	}
	if (Object->HasField(PropertiesField))
	{
		OwnObject->SetField(PropertiesField, Object->TryGetField(PropertiesField));
	}
	OutMap.Add(Path, ObjectValue(OwnObject));
}

void CollectNodeDiffValues(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonObject>& Node,
	TMap<FString, TSharedPtr<FJsonValue>>& OutMap)
{
	if (!Node.IsValid())
	{
		return;
	}

	const FString Id = Node->GetStringField(IdField);
	CollectAttachmentDiffValue(Node, NodePath(Context, Id), OutMap);

	const TArray<TSharedPtr<FJsonValue>>* Services = nullptr;
	if (Node->TryGetArrayField(ServicesField, Services) && Services)
	{
		TArray<FString> ServiceOrder;
		for (const TSharedPtr<FJsonValue>& ServiceValue : *Services)
		{
			TSharedPtr<FJsonObject> Service = ObjectFromValue(ServiceValue);
			if (Service.IsValid())
			{
				const FString ServiceId = Service->GetStringField(IdField);
				ServiceOrder.Add(ServiceId);
				CollectAttachmentDiffValue(Service, ServicePath(Context, Id, ServiceId), OutMap);
			}
		}
		OutMap.Add(JoinPath(NodePath(Context, Id), ServicesField), StringArrayValue(ServiceOrder));
	}

	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (!Node->TryGetArrayField(ChildrenField, Children) || !Children)
	{
		return;
	}

	TArray<FString> ChildOrder;
	for (const TSharedPtr<FJsonValue>& EdgeValue : *Children)
	{
		TSharedPtr<FJsonObject> Edge = ObjectFromValue(EdgeValue);
		TSharedPtr<FJsonObject> Child = Edge.IsValid() ? Edge->GetObjectField(ChildField) : nullptr;
		if (!Child.IsValid())
		{
			continue;
		}

		const FString ChildId = Child->GetStringField(IdField);
		ChildOrder.Add(ChildId);
		const FString EdgePath = ChildEdgePath(Context, Id, ChildId);
		TSharedPtr<FJsonObject> EdgeIdentity = MakeShared<FJsonObject>();
		EdgeIdentity->SetStringField(ChildField, ChildId);
		OutMap.Add(EdgePath, ObjectValue(EdgeIdentity));

		const TArray<TSharedPtr<FJsonValue>>* Decorators = nullptr;
		if (Edge->TryGetArrayField(DecoratorsField, Decorators) && Decorators)
		{
			TArray<FString> DecoratorOrder;
			for (const TSharedPtr<FJsonValue>& DecoratorValue : *Decorators)
			{
				TSharedPtr<FJsonObject> Decorator = ObjectFromValue(DecoratorValue);
				if (Decorator.IsValid())
				{
					const FString DecoratorId = Decorator->GetStringField(IdField);
					DecoratorOrder.Add(DecoratorId);
					CollectAttachmentDiffValue(Decorator, EdgeDecoratorPath(Context, Id, ChildId, DecoratorId), OutMap);
				}
			}
			OutMap.Add(JoinPath(EdgePath, DecoratorsField), StringArrayValue(DecoratorOrder));
		}

		const TArray<TSharedPtr<FJsonValue>>* Logic = nullptr;
		if (Edge->TryGetArrayField(DecoratorLogicField, Logic) && Logic)
		{
			for (int32 Index = 0; Index < Logic->Num(); ++Index)
			{
				OutMap.Add(JoinPath(JoinPath(EdgePath, DecoratorLogicField), Index), (*Logic)[Index]);
			}
		}

		CollectNodeDiffValues(Context, Child, OutMap);
	}
	OutMap.Add(JoinPath(NodePath(Context, Id), ChildrenField), StringArrayValue(ChildOrder));
}

void CollectTreeDiffValues(
	const FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& Tree,
	TMap<FString, TSharedPtr<FJsonValue>>& OutMap)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedPtr<FJsonObject>* RootObject = nullptr;
	if (Tree->TryGetObjectField(RootField, RootObject) && RootObject && RootObject->IsValid())
	{
		Root = *RootObject;
		if (Root->Values.Num() > 0)
		{
			CollectNodeDiffValues(Context, Root, OutMap);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* RootDecorators = nullptr;
	if (Tree->TryGetArrayField(RootDecoratorsField, RootDecorators) && RootDecorators)
	{
		TArray<FString> RootDecoratorOrder;
		for (const TSharedPtr<FJsonValue>& DecoratorValue : *RootDecorators)
		{
			TSharedPtr<FJsonObject> Decorator = ObjectFromValue(DecoratorValue);
			if (Decorator.IsValid())
			{
				const FString DecoratorId = Decorator->GetStringField(IdField);
				RootDecoratorOrder.Add(DecoratorId);
				CollectAttachmentDiffValue(Decorator, RootDecoratorPath(Context, DecoratorId), OutMap);
			}
		}
		OutMap.Add(JoinPath(RegionPath(Context), RootDecoratorsField), StringArrayValue(RootDecoratorOrder));
	}

	const TArray<TSharedPtr<FJsonValue>>* RootLogic = nullptr;
	if (Tree->TryGetArrayField(RootDecoratorLogicField, RootLogic) && RootLogic)
	{
		for (int32 Index = 0; Index < RootLogic->Num(); ++Index)
		{
			OutMap.Add(JoinPath(JoinPath(RegionPath(Context), RootDecoratorLogicField), Index), (*RootLogic)[Index]);
		}
	}
}
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentMaterializer::ValidateTree(
	const FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& Tree)
{
	FBehaviorTreeSpec Spec;
	FAssetDocumentCapabilityResult Result = ParseTreeSpec(Context, Tree, Spec);
	if (!Result.bSuccess)
	{
		return Result;
	}

	UBehaviorTree* PreviewTree = NewObject<UBehaviorTree>(GetTransientPackage());
	return ValidateSpec(Spec, PreviewTree);
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentMaterializer::ApplyTree(
	FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& Tree,
	bool& bOutChanged)
{
	bOutChanged = false;
	UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(Context.Asset);
	if (!BehaviorTree)
	{
		return Failure(RegionPath(Context), TEXT("UnsupportedAsset"), TEXT("BehaviorTree Tree apply requires UBehaviorTree asset"));
	}

	FBehaviorTreeSpec Spec;
	FAssetDocumentCapabilityResult Result = ParseTreeSpec(Context, Tree, Spec);
	if (!Result.bSuccess)
	{
		return Result;
	}

	UBehaviorTree* PreviewTree = NewObject<UBehaviorTree>(GetTransientPackage());
	Result = ValidateSpec(Spec, PreviewTree);
	if (!Result.bSuccess)
	{
		return Result;
	}
	Result = MaterializeTree(PreviewTree, Spec);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TSharedRef<FJsonObject> CurrentTree = MakeShared<FJsonObject>();
	Result = ExtractTreeObject(BehaviorTree, CurrentTree);
	if (!Result.bSuccess)
	{
		return Result;
	}
	TSharedRef<FJsonObject> DesiredCanonicalTree = MakeShared<FJsonObject>();
	Result = ExtractTreeObject(PreviewTree, DesiredCanonicalTree);
	if (!Result.bSuccess)
	{
		return Result;
	}

	bOutChanged = !JsonEqual(MakeShared<FJsonValueObject>(CurrentTree), MakeShared<FJsonValueObject>(DesiredCanonicalTree));
	if (Context.bIsDryRun || !bOutChanged)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("BehaviorTree semantic tree unchanged"));
	}

	BehaviorTree->Modify();
	Result = MaterializeTree(BehaviorTree, Spec);
	if (!Result.bSuccess)
	{
		return Result;
	}
	BehaviorTree->MarkPackageDirty();
	return FAssetDocumentCapabilityResult::Success(TEXT("Applied BehaviorTree semantic tree"));
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentMaterializer::ExtractTree(
	const FAssetDocumentRegionContext& Context,
	TSharedRef<FJsonObject>& OutTree)
{
	const UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(Context.Asset);
	return ExtractTreeObject(BehaviorTree, OutTree);
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentMaterializer::DiffTree(
	const FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& DesiredTree,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
{
	const UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(Context.Asset);
	TSharedRef<FJsonObject> CurrentTree = MakeShared<FJsonObject>();
	FAssetDocumentCapabilityResult Result = ExtractTreeObject(BehaviorTree, CurrentTree);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TSharedRef<FJsonObject> DesiredCanonicalTree = MakeShared<FJsonObject>();
	Result = BuildCanonicalDesiredTree(Context, DesiredTree, DesiredCanonicalTree);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TMap<FString, TSharedPtr<FJsonValue>> CurrentValues;
	TMap<FString, TSharedPtr<FJsonValue>> DesiredValues;
	CollectTreeDiffValues(Context, CurrentTree, CurrentValues);
	CollectTreeDiffValues(Context, DesiredCanonicalTree, DesiredValues);
	AddMapDiffs(CurrentValues, DesiredValues, OutDiffEntries);
	return FAssetDocumentCapabilityResult::Success(TEXT("Diffed BehaviorTree semantic tree"));
}
