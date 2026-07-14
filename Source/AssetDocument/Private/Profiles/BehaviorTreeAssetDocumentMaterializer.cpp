// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/BehaviorTreeAssetDocumentMaterializer.h"

#include "AIGraphSchema.h"
#include "AIGraphTypes.h"
#include "AssetDocumentJsonRegionUtils.h"
#include "Regions/AssetDocumentReflectedPropertyUtils.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"
#include "BehaviorTree/Tasks/BTTask_RunBehavior.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BehaviorTreeGraphNode_Service.h"
#include "BehaviorTreeGraphNode_SimpleParallel.h"
#include "BehaviorTreeGraphNode_SubtreeTask.h"
#include "BehaviorTreeGraphNode_Task.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_BehaviorTree.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#include <initializer_list>

namespace
{
constexpr const TCHAR* GraphGuidField = TEXT("GraphGuid");
constexpr const TCHAR* RootField = TEXT("Root");
constexpr const TCHAR* CommentsField = TEXT("Comments");
constexpr const TCHAR* IdField = TEXT("Id");
constexpr const TCHAR* ClassField = TEXT("Class");
constexpr const TCHAR* PropertiesField = TEXT("Properties");
constexpr const TCHAR* DecoratorsField = TEXT("Decorators");
constexpr const TCHAR* ServicesField = TEXT("Services");
constexpr const TCHAR* ChildrenField = TEXT("Children");
constexpr const TCHAR* EditorField = TEXT("Editor");
constexpr const TCHAR* PositionField = TEXT("Position");
constexpr const TCHAR* XField = TEXT("X");
constexpr const TCHAR* YField = TEXT("Y");
constexpr const TCHAR* NodeCommentField = TEXT("NodeComment");
constexpr const TCHAR* CommentBubblePinnedField = TEXT("bCommentBubblePinned");
constexpr const TCHAR* CommentBubbleVisibleField = TEXT("bCommentBubbleVisible");

#if WITH_DEV_AUTOMATION_TESTS
bool bFailNextEditorGraphRebuildForTest = false;
#endif

struct FBehaviorTreeEditorSpec
{
	int32 X = 0;
	int32 Y = 0;
	bool bHasExplicitPosition = false;
	FString NodeComment;
	bool bCommentBubblePinned = false;
	bool bCommentBubbleVisible = false;
};

struct FBehaviorTreeServiceSpec
{
	FGuid Guid;
	FString Id;
	FString JsonPath;
	UClass* NodeClass = nullptr;
	TSharedPtr<FJsonObject> Properties;
};

struct FBehaviorTreeNodeSpec
{
	FGuid Guid;
	FString Id;
	FString JsonPath;
	UClass* NodeClass = nullptr;
	TSharedPtr<FJsonObject> Properties;
	TArray<FBehaviorTreeServiceSpec> Services;
	TArray<TSharedPtr<FBehaviorTreeNodeSpec>> Children;
	FBehaviorTreeEditorSpec Editor;
};

struct FBehaviorTreeGraphSpec
{
	FGuid GraphGuid;
	bool bGraphGuidWasAuthored = false;
	TSharedPtr<FBehaviorTreeNodeSpec> Root;
};

struct FBehaviorTreeParseState
{
	TSet<FGuid> Guids;
	TSet<FString> Coordinates;
	int32 NextGeneratedOrdinal = 0;
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

FAssetDocumentCapabilityResult CheckKnownFields(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	std::initializer_list<const TCHAR*> KnownFields)
{
	TSet<FString> Known;
	for (const TCHAR* Field : KnownFields)
	{
		Known.Add(Field);
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		if (!Known.Contains(Pair.Key))
		{
			return Failure(
				JoinPath(Path, Pair.Key),
				TEXT("UnknownField"),
				FString::Printf(TEXT("Unknown BehaviorTree graph-source field '%s'"), *Pair.Key));
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult RequireObject(
	const TSharedRef<FJsonObject>& Owner,
	const FString& Field,
	const FString& Path,
	TSharedPtr<FJsonObject>& OutObject,
	bool bOptional = false)
{
	OutObject.Reset();
	const TSharedPtr<FJsonValue> Value = Owner->TryGetField(Field);
	if (!Value.IsValid() && bOptional)
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (!Value.IsValid() || Value->Type != EJson::Object || !Value->AsObject().IsValid())
	{
		return Failure(Path, TEXT("InvalidBehaviorTreeObject"), FString::Printf(TEXT("%s must be an object"), *Field));
	}
	OutObject = Value->AsObject();
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadArray(
	const TSharedRef<FJsonObject>& Owner,
	const FString& Field,
	const FString& Path,
	const TArray<TSharedPtr<FJsonValue>>*& OutArray,
	bool bOptional = true)
{
	OutArray = nullptr;
	const TSharedPtr<FJsonValue> Value = Owner->TryGetField(Field);
	if (!Value.IsValid() && bOptional)
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (!Value.IsValid() || Value->Type != EJson::Array)
	{
		return Failure(Path, TEXT("InvalidBehaviorTreeArray"), FString::Printf(TEXT("%s must be an array"), *Field));
	}
	OutArray = &Value->AsArray();
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadGuid(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	FBehaviorTreeParseState& State,
	FGuid& OutGuid,
	FString& OutId)
{
	OutId.Reset();
	if (!Object->TryGetStringField(IdField, OutId) || !FGuid::ParseExact(OutId, EGuidFormats::Digits, OutGuid) || !OutGuid.IsValid())
	{
		return Failure(
			JoinPath(Path, IdField),
			TEXT("InvalidBehaviorTreeNodeGuid"),
			TEXT("BehaviorTree Id must be a valid canonical 32-hex NodeGuid"));
	}
	OutId = OutGuid.ToString(EGuidFormats::Digits);
	if (State.Guids.Contains(OutGuid))
	{
		return Failure(
			JoinPath(Path, IdField),
			TEXT("DuplicateBehaviorTreeNodeGuid"),
			FString::Printf(TEXT("BehaviorTree NodeGuid %s is duplicated"), *OutId));
	}
	State.Guids.Add(OutGuid);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ResolveConcreteNodeClass(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	UClass* RequiredFamily,
	UClass*& OutClass)
{
	OutClass = nullptr;
	FString ClassPath;
	if (!Object->TryGetStringField(ClassField, ClassPath) || ClassPath.TrimStartAndEnd().IsEmpty())
	{
		return Failure(JoinPath(Path, ClassField), TEXT("InvalidBehaviorTreeNodeClass"), TEXT("BehaviorTree Class is required"));
	}
	ClassPath.TrimStartAndEndInline();
	OutClass = StaticLoadClass(UBTNode::StaticClass(), nullptr, *ClassPath);
	if (!OutClass)
	{
		OutClass = FindObject<UClass>(nullptr, *ClassPath);
	}
	if (!OutClass || !OutClass->IsChildOf(RequiredFamily))
	{
		return Failure(
			JoinPath(Path, ClassField),
			TEXT("InvalidBehaviorTreeNodeClass"),
			FString::Printf(TEXT("BehaviorTree class '%s' is not a %s"), *ClassPath, *RequiredFamily->GetName()));
	}
	if (OutClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return Failure(
			JoinPath(Path, ClassField),
			TEXT("AbstractBehaviorTreeNodeClass"),
			FString::Printf(TEXT("BehaviorTree class '%s' is abstract"), *ClassPath));
	}
	if (OutClass->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return Failure(
			JoinPath(Path, ClassField),
			TEXT("DeprecatedBehaviorTreeNodeClass"),
			FString::Printf(TEXT("BehaviorTree class '%s' is deprecated for new authored input"), *ClassPath));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadProperties(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	TSharedPtr<FJsonObject>& OutProperties)
{
	OutProperties = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonValue> Value = Object->TryGetField(PropertiesField);
	if (!Value.IsValid())
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (Value->Type != EJson::Object || !Value->AsObject().IsValid())
	{
		return Failure(JoinPath(Path, PropertiesField), TEXT("InvalidTreeProperties"), TEXT("BehaviorTree Properties must be an object"));
	}
	OutProperties = Value->AsObject();
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadIntCoordinate(
	const TSharedRef<FJsonObject>& Position,
	const FString& Field,
	const FString& Path,
	int32& OutValue)
{
	double Number = 0.0;
	if (!Position->TryGetNumberField(Field, Number)
		|| !FMath::IsFinite(Number)
		|| Number < MIN_int32
		|| Number > MAX_int32)
	{
		return Failure(Path, TEXT("InvalidBehaviorTreeCoordinate"), TEXT("BehaviorTree coordinates must be finite int32 values"));
	}
	OutValue = static_cast<int32>(Number);
	if (static_cast<double>(OutValue) != Number)
	{
		return Failure(Path, TEXT("InvalidBehaviorTreeCoordinate"), TEXT("BehaviorTree coordinates must be finite int32 values"));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseEditor(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	FBehaviorTreeParseState& State,
	FBehaviorTreeEditorSpec& OutEditor)
{
	const int32 GeneratedOrdinal = State.NextGeneratedOrdinal++;
	OutEditor.X = GeneratedOrdinal * 300;
	OutEditor.Y = 0;

	TSharedPtr<FJsonObject> Editor;
	FAssetDocumentCapabilityResult Result = RequireObject(Object, EditorField, JoinPath(Path, EditorField), Editor, true);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (!Editor.IsValid())
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	Result = CheckKnownFields(
		Editor.ToSharedRef(),
		JoinPath(Path, EditorField),
		{PositionField, NodeCommentField, CommentBubblePinnedField, CommentBubbleVisibleField});
	if (!Result.bSuccess)
	{
		return Result;
	}

	TSharedPtr<FJsonObject> Position;
	Result = RequireObject(Editor.ToSharedRef(), PositionField, JoinPath(JoinPath(Path, EditorField), PositionField), Position, true);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (Position.IsValid())
	{
		Result = CheckKnownFields(Position.ToSharedRef(), JoinPath(JoinPath(Path, EditorField), PositionField), {XField, YField});
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = ReadIntCoordinate(Position.ToSharedRef(), XField, JoinPath(JoinPath(JoinPath(Path, EditorField), PositionField), XField), OutEditor.X);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = ReadIntCoordinate(Position.ToSharedRef(), YField, JoinPath(JoinPath(JoinPath(Path, EditorField), PositionField), YField), OutEditor.Y);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutEditor.bHasExplicitPosition = true;
	}

	if (Editor->HasField(NodeCommentField) && !Editor->TryGetStringField(NodeCommentField, OutEditor.NodeComment))
	{
		return Failure(JoinPath(JoinPath(Path, EditorField), NodeCommentField), TEXT("InvalidBehaviorTreeEditorField"), TEXT("NodeComment must be a string"));
	}
	if (Editor->HasField(CommentBubblePinnedField) && !Editor->TryGetBoolField(CommentBubblePinnedField, OutEditor.bCommentBubblePinned))
	{
		return Failure(JoinPath(JoinPath(Path, EditorField), CommentBubblePinnedField), TEXT("InvalidBehaviorTreeEditorField"), TEXT("bCommentBubblePinned must be a boolean"));
	}
	if (Editor->HasField(CommentBubbleVisibleField) && !Editor->TryGetBoolField(CommentBubbleVisibleField, OutEditor.bCommentBubbleVisible))
	{
		return Failure(JoinPath(JoinPath(Path, EditorField), CommentBubbleVisibleField), TEXT("InvalidBehaviorTreeEditorField"), TEXT("bCommentBubbleVisible must be a boolean"));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseService(
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	FBehaviorTreeParseState& State,
	FBehaviorTreeServiceSpec& OutService)
{
	TSharedPtr<FJsonObject> Object;
	FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(Value, Path, Object);
	if (!Result.bSuccess)
	{
		return Failure(Path, TEXT("InvalidBehaviorTreeService"), TEXT("BehaviorTree Services entries must be objects"));
	}
	Result = CheckKnownFields(Object.ToSharedRef(), Path, {IdField, ClassField, PropertiesField});
	if (!Result.bSuccess)
	{
		return Result;
	}
	OutService.JsonPath = Path;
	Result = ReadGuid(Object.ToSharedRef(), Path, State, OutService.Guid, OutService.Id);
	if (!Result.bSuccess)
	{
		return Result;
	}
	Result = ResolveConcreteNodeClass(Object.ToSharedRef(), Path, UBTService::StaticClass(), OutService.NodeClass);
	if (!Result.bSuccess)
	{
		return Result;
	}
	return ReadProperties(Object.ToSharedRef(), Path, OutService.Properties);
}

FAssetDocumentCapabilityResult ParseNode(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	bool bIsRoot,
	int32 Depth,
	FBehaviorTreeParseState& State,
	FBehaviorTreeNodeSpec& OutNode)
{
	FAssetDocumentCapabilityResult Result = CheckKnownFields(
		Object,
		Path,
		{IdField, ClassField, PropertiesField, DecoratorsField, ServicesField, ChildrenField, EditorField});
	if (!Result.bSuccess)
	{
		return Result;
	}
	OutNode.JsonPath = Path;
	Result = ReadGuid(Object, Path, State, OutNode.Guid, OutNode.Id);
	if (!Result.bSuccess)
	{
		return Result;
	}
	Result = ResolveConcreteNodeClass(Object, Path, UBTNode::StaticClass(), OutNode.NodeClass);
	if (!Result.bSuccess)
	{
		return Result;
	}
	const bool bComposite = OutNode.NodeClass->IsChildOf(UBTCompositeNode::StaticClass());
	const bool bTask = OutNode.NodeClass->IsChildOf(UBTTaskNode::StaticClass());
	if ((bIsRoot && !bComposite) || (!bIsRoot && !bComposite && !bTask))
	{
		return Failure(
			JoinPath(Path, ClassField),
			TEXT("InvalidBehaviorTreeNodeClass"),
			bIsRoot ? TEXT("BehaviorTree Root must be a concrete composite") : TEXT("BehaviorTree Children must be concrete composite or task nodes"));
	}
	Result = ReadProperties(Object, Path, OutNode.Properties);
	if (!Result.bSuccess)
	{
		return Result;
	}
	Result = ParseEditor(Object, Path, State, OutNode.Editor);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (!OutNode.Editor.bHasExplicitPosition)
	{
		OutNode.Editor.Y = Depth * 300;
	}
	const FString CoordinateKey = FString::Printf(TEXT("%d:%d"), OutNode.Editor.X, OutNode.Editor.Y);
	if (State.Coordinates.Contains(CoordinateKey))
	{
		return Failure(
			JoinPath(JoinPath(JoinPath(Path, EditorField), PositionField), XField),
			TEXT("DuplicateBehaviorTreeSiblingCoordinate"),
			TEXT("BehaviorTree graph nodes cannot share the same X/Y coordinate"));
	}
	State.Coordinates.Add(CoordinateKey);

	const TArray<TSharedPtr<FJsonValue>>* Decorators = nullptr;
	Result = ReadArray(Object, DecoratorsField, JoinPath(Path, DecoratorsField), Decorators);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (Decorators && Decorators->Num() > 0)
	{
		return Failure(JoinPath(Path, DecoratorsField), TEXT("UnsupportedBehaviorTreeDecoratorsTask5"), TEXT("Decorator authored graphs are integrated by Task 5"));
	}

	const TArray<TSharedPtr<FJsonValue>>* Services = nullptr;
	Result = ReadArray(Object, ServicesField, JoinPath(Path, ServicesField), Services);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (Services)
	{
		for (int32 Index = 0; Index < Services->Num(); ++Index)
		{
			FBehaviorTreeServiceSpec Service;
			Result = ParseService((*Services)[Index], JoinPath(JoinPath(Path, ServicesField), Index), State, Service);
			if (!Result.bSuccess)
			{
				return Result;
			}
			OutNode.Services.Add(MoveTemp(Service));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	Result = ReadArray(Object, ChildrenField, JoinPath(Path, ChildrenField), Children);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (bTask && Children && Children->Num() > 0)
	{
		return Failure(JoinPath(Path, ChildrenField), TEXT("InvalidBehaviorTreeParentClass"), TEXT("BehaviorTree task nodes cannot own Children"));
	}
	int32 PreviousX = MIN_int32;
	if (Children)
	{
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			TSharedPtr<FJsonObject> ChildObject;
			Result = FAssetDocumentJsonRegionUtils::RequireObjectValue((*Children)[Index], JoinPath(JoinPath(Path, ChildrenField), Index), ChildObject);
			if (!Result.bSuccess)
			{
				return Failure(JoinPath(JoinPath(Path, ChildrenField), Index), TEXT("InvalidBehaviorTreeNode"), TEXT("BehaviorTree Children entries must be node objects"));
			}
			TSharedPtr<FBehaviorTreeNodeSpec> Child = MakeShared<FBehaviorTreeNodeSpec>();
			Result = ParseNode(ChildObject.ToSharedRef(), JoinPath(JoinPath(Path, ChildrenField), Index), false, Depth + 1, State, *Child);
			if (!Result.bSuccess)
			{
				return Result;
			}
			if (Index > 0 && Child->Editor.X == PreviousX)
			{
				return Failure(
					JoinPath(JoinPath(JoinPath(JoinPath(JoinPath(Path, ChildrenField), Index), EditorField), PositionField), XField),
					TEXT("DuplicateBehaviorTreeSiblingCoordinate"),
					TEXT("BehaviorTree siblings cannot share X coordinates"));
			}
			if (Index > 0 && Child->Editor.X < PreviousX)
			{
				return Failure(
					JoinPath(JoinPath(JoinPath(JoinPath(JoinPath(Path, ChildrenField), Index), EditorField), PositionField), XField),
					TEXT("BehaviorTreeLayoutOrderConflict"),
					TEXT("BehaviorTree Children order must match strictly increasing NodePosX order"));
			}
			PreviousX = Child->Editor.X;
			OutNode.Children.Add(MoveTemp(Child));
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

FGuid ExistingOrNewGraphGuid(const FAssetDocumentRegionContext& Context)
{
	if (const UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(Context.Asset))
	{
		if (const UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph); Graph && Graph->GraphGuid.IsValid())
		{
			return Graph->GraphGuid;
		}
	}
	return FGuid::NewGuid();
}

FAssetDocumentCapabilityResult ParseTreeSpec(
	const FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& Tree,
	FBehaviorTreeGraphSpec& OutSpec)
{
	const FString Path = RegionPath(Context);
	FAssetDocumentCapabilityResult Result = CheckKnownFields(Tree, Path, {GraphGuidField, RootField, CommentsField});
	if (!Result.bSuccess)
	{
		return Result;
	}

	OutSpec.GraphGuid = ExistingOrNewGraphGuid(Context);
	FString GraphGuidString;
	if (Tree->TryGetStringField(GraphGuidField, GraphGuidString))
	{
		if (!FGuid::ParseExact(GraphGuidString, EGuidFormats::Digits, OutSpec.GraphGuid) || !OutSpec.GraphGuid.IsValid())
		{
			return Failure(JoinPath(Path, GraphGuidField), TEXT("InvalidBehaviorTreeGraphGuid"), TEXT("GraphGuid must be a valid canonical 32-hex GUID"));
		}
		OutSpec.bGraphGuidWasAuthored = true;
	}
	else if (Tree->HasField(GraphGuidField))
	{
		return Failure(JoinPath(Path, GraphGuidField), TEXT("InvalidBehaviorTreeGraphGuid"), TEXT("GraphGuid must be a string"));
	}

	const TArray<TSharedPtr<FJsonValue>>* Comments = nullptr;
	Result = ReadArray(Tree, CommentsField, JoinPath(Path, CommentsField), Comments);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (Comments && Comments->Num() > 0)
	{
		return Failure(JoinPath(Path, CommentsField), TEXT("UnsupportedBehaviorTreeCommentsTask5"), TEXT("BehaviorTree comment boxes are integrated by Task 5"));
	}

	TSharedPtr<FJsonObject> Root;
	Result = RequireObject(Tree, RootField, JoinPath(Path, RootField), Root);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (Root->Values.Num() == 0)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Parsed empty BehaviorTree graph source"));
	}

	FBehaviorTreeParseState State;
	OutSpec.Root = MakeShared<FBehaviorTreeNodeSpec>();
	return ParseNode(Root.ToSharedRef(), JoinPath(Path, RootField), true, 0, State, *OutSpec.Root);
}

FAssetDocumentCapabilityResult ValidateSpecProperties(const FBehaviorTreeGraphSpec& Spec, UObject* Outer)
{
	TFunction<FAssetDocumentCapabilityResult(const FBehaviorTreeNodeSpec&)> ValidateNode;
	ValidateNode = [&ValidateNode, Outer](const FBehaviorTreeNodeSpec& Node) -> FAssetDocumentCapabilityResult
	{
		UBTNode* Preview = NewObject<UBTNode>(Outer, Node.NodeClass, NAME_None, RF_Transient);
		if (!Preview)
		{
			return Failure(Node.JsonPath, TEXT("InvalidBehaviorTreeNodeClass"), TEXT("Failed to instantiate BehaviorTree node"));
		}
		FAssetDocumentCapabilityResult Result = FAssetDocumentReflectedPropertyUtils::ValidateProperties(
			Preview,
			Node.Properties.ToSharedRef(),
			JoinPath(Node.JsonPath, PropertiesField));
		if (!Result.bSuccess)
		{
			return Result;
		}
		for (const FBehaviorTreeServiceSpec& Service : Node.Services)
		{
			UBTService* PreviewService = NewObject<UBTService>(Outer, Service.NodeClass, NAME_None, RF_Transient);
			if (!PreviewService)
			{
				return Failure(Service.JsonPath, TEXT("InvalidBehaviorTreeNodeClass"), TEXT("Failed to instantiate BehaviorTree service"));
			}
			Result = FAssetDocumentReflectedPropertyUtils::ValidateProperties(
				PreviewService,
				Service.Properties.ToSharedRef(),
				JoinPath(Service.JsonPath, PropertiesField));
			if (!Result.bSuccess)
			{
				return Result;
			}
		}
		for (const TSharedPtr<FBehaviorTreeNodeSpec>& Child : Node.Children)
		{
			Result = ValidateNode(*Child);
			if (!Result.bSuccess)
			{
				return Result;
			}
		}
		return FAssetDocumentCapabilityResult::Success();
	};
	return Spec.Root.IsValid() ? ValidateNode(*Spec.Root) : FAssetDocumentCapabilityResult::Success();
}

UClass* WrapperClassForNode(UClass* NodeClass)
{
	if (NodeClass->IsChildOf(UBTComposite_SimpleParallel::StaticClass()))
	{
		return UBehaviorTreeGraphNode_SimpleParallel::StaticClass();
	}
	if (NodeClass->IsChildOf(UBTCompositeNode::StaticClass()))
	{
		return UBehaviorTreeGraphNode_Composite::StaticClass();
	}
	if (NodeClass->IsChildOf(UBTTask_RunBehavior::StaticClass()))
	{
		return UBehaviorTreeGraphNode_SubtreeTask::StaticClass();
	}
	return UBehaviorTreeGraphNode_Task::StaticClass();
}

UEdGraphPin* FindPin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
{
	if (!Node)
	{
		return nullptr;
	}
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == Direction)
		{
			return Pin;
		}
	}
	return nullptr;
}

FAssetDocumentCapabilityResult ApplyNodeProperties(
	UBehaviorTreeGraphNode* GraphNode,
	const TSharedRef<FJsonObject>& Properties,
	const FString& Path)
{
	UBTNode* NodeInstance = GraphNode ? Cast<UBTNode>(GraphNode->NodeInstance) : nullptr;
	if (!NodeInstance)
	{
		return Failure(Path, TEXT("MissingBehaviorTreeNodeInstance"), TEXT("BehaviorTree graph wrapper failed to create its NodeInstance"));
	}
	return FAssetDocumentReflectedPropertyUtils::ApplyProperties(NodeInstance, Properties, JoinPath(Path, PropertiesField));
}

FAssetDocumentCapabilityResult CreateGraphNode(
	UBehaviorTreeGraph* Graph,
	const FBehaviorTreeNodeSpec& Spec,
	UBehaviorTreeGraphNode*& OutNode)
{
	OutNode = nullptr;
	UBehaviorTreeGraphNode* Template = NewObject<UBehaviorTreeGraphNode>(Graph, WrapperClassForNode(Spec.NodeClass));
	Template->ClassData = FGraphNodeClassData(Spec.NodeClass, FGraphNodeClassHelper::GetDeprecationMessage(Spec.NodeClass));
	FAISchemaAction_NewNode Action;
	Action.NodeTemplate = Template;
	OutNode = Cast<UBehaviorTreeGraphNode>(Action.PerformAction(Graph, nullptr, FVector2f(Spec.Editor.X, Spec.Editor.Y), false));
	if (!OutNode)
	{
		return Failure(Spec.JsonPath, TEXT("BehaviorTreeGraphNodeCreationFailed"), TEXT("Failed to create BehaviorTree graph wrapper"));
	}
	OutNode->NodeGuid = Spec.Guid;
	OutNode->NodePosX = Spec.Editor.X;
	OutNode->NodePosY = Spec.Editor.Y;
	OutNode->NodeComment = Spec.Editor.NodeComment;
	OutNode->bCommentBubblePinned = Spec.Editor.bCommentBubblePinned;
	OutNode->bCommentBubbleVisible = Spec.Editor.bCommentBubbleVisible;
	FAssetDocumentCapabilityResult Result = ApplyNodeProperties(OutNode, Spec.Properties.ToSharedRef(), Spec.JsonPath);
	if (!Result.bSuccess)
	{
		return Result;
	}

	for (const FBehaviorTreeServiceSpec& ServiceSpec : Spec.Services)
	{
		UBehaviorTreeGraphNode_Service* ServiceNode = NewObject<UBehaviorTreeGraphNode_Service>(Graph);
		ServiceNode->ClassData = FGraphNodeClassData(ServiceSpec.NodeClass, FGraphNodeClassHelper::GetDeprecationMessage(ServiceSpec.NodeClass));
		FAISchemaAction_NewSubNode ServiceAction;
		ServiceAction.ParentNode = OutNode;
		ServiceAction.NodeTemplate = ServiceNode;
		ServiceAction.PerformAction(Graph, nullptr, FVector2f(0.0f, 0.0f), false);
		ServiceNode->NodeGuid = ServiceSpec.Guid;
		Result = ApplyNodeProperties(ServiceNode, ServiceSpec.Properties.ToSharedRef(), ServiceSpec.JsonPath);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult BuildNodeSubtree(
	UBehaviorTreeGraph* Graph,
	const UEdGraphSchema* Schema,
	const FBehaviorTreeNodeSpec& Spec,
	UBehaviorTreeGraphNode*& OutNode)
{
	FAssetDocumentCapabilityResult Result = CreateGraphNode(Graph, Spec, OutNode);
	if (!Result.bSuccess)
	{
		return Result;
	}
	for (const TSharedPtr<FBehaviorTreeNodeSpec>& ChildSpec : Spec.Children)
	{
		UBehaviorTreeGraphNode* ChildNode = nullptr;
		Result = BuildNodeSubtree(Graph, Schema, *ChildSpec, ChildNode);
		if (!Result.bSuccess)
		{
			return Result;
		}
		UEdGraphPin* ParentOutput = FindPin(OutNode, EGPD_Output);
		UEdGraphPin* ChildInput = FindPin(ChildNode, EGPD_Input);
		if (!ParentOutput || !ChildInput || !Schema->TryCreateConnection(ParentOutput, ChildInput))
		{
			return Failure(
				ChildSpec->JsonPath,
				TEXT("InvalidBehaviorTreeGraphConnection"),
				TEXT("UE BehaviorTree schema rejected the authored parent/child connection"));
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

TArray<UBehaviorTreeGraphNode*> SortedGraphChildren(UBehaviorTreeGraphNode* Node)
{
	TArray<UBehaviorTreeGraphNode*> Children;
	if (UEdGraphPin* Output = FindPin(Node, EGPD_Output))
	{
		for (UEdGraphPin* Linked : Output->LinkedTo)
		{
			if (Linked)
			{
				if (UBehaviorTreeGraphNode* Child = Cast<UBehaviorTreeGraphNode>(Linked->GetOwningNode()))
				{
					Children.Add(Child);
				}
			}
		}
	}
	Children.Sort([](const UBehaviorTreeGraphNode& A, const UBehaviorTreeGraphNode& B)
	{
		return A.NodePosX == B.NodePosX ? A.NodePosY < B.NodePosY : A.NodePosX < B.NodePosX;
	});
	return Children;
}

FAssetDocumentCapabilityResult AssertRuntimeMirrorNode(UBehaviorTreeGraphNode* GraphNode, const FString& Path)
{
	UBTNode* Instance = GraphNode ? Cast<UBTNode>(GraphNode->NodeInstance) : nullptr;
	if (!Instance)
	{
		return Failure(Path, TEXT("BehaviorTreeRuntimeMirrorMismatch"), TEXT("Graph wrapper has no NodeInstance after UpdateAsset"));
	}
	TArray<UBTService*> RuntimeServices;
	if (UBTCompositeNode* Composite = Cast<UBTCompositeNode>(Instance))
	{
		RuntimeServices.Reserve(Composite->Services.Num());
		for (UBTService* Service : Composite->Services)
		{
			RuntimeServices.Add(Service);
		}
	}
	else if (UBTTaskNode* Task = Cast<UBTTaskNode>(Instance))
	{
		RuntimeServices.Reserve(Task->Services.Num());
		for (UBTService* Service : Task->Services)
		{
			RuntimeServices.Add(Service);
		}
	}
	if (RuntimeServices.Num() != GraphNode->Services.Num())
	{
		return Failure(Path, TEXT("BehaviorTreeRuntimeMirrorMismatch"), TEXT("Runtime service count does not mirror graph service subnodes"));
	}
	for (int32 Index = 0; Index < RuntimeServices.Num(); ++Index)
	{
		if (RuntimeServices[Index] != GraphNode->Services[Index]->NodeInstance)
		{
			return Failure(Path, TEXT("BehaviorTreeRuntimeMirrorMismatch"), TEXT("Runtime service order does not mirror graph service subnodes"));
		}
	}

	const TArray<UBehaviorTreeGraphNode*> Children = SortedGraphChildren(GraphNode);
	if (UBTCompositeNode* Composite = Cast<UBTCompositeNode>(Instance))
	{
		if (Composite->Children.Num() != Children.Num())
		{
			return Failure(Path, TEXT("BehaviorTreeRuntimeMirrorMismatch"), TEXT("Runtime child count does not mirror graph topology"));
		}
		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			const UBTNode* RuntimeChild = Composite->Children[Index].ChildComposite
				? static_cast<const UBTNode*>(Composite->Children[Index].ChildComposite.Get())
				: static_cast<const UBTNode*>(Composite->Children[Index].ChildTask.Get());
			if (RuntimeChild != Children[Index]->NodeInstance)
			{
				return Failure(Path, TEXT("BehaviorTreeRuntimeMirrorMismatch"), TEXT("Runtime child order does not mirror graph X order"));
			}
			FAssetDocumentCapabilityResult Result = AssertRuntimeMirrorNode(Children[Index], JoinPath(Path, Index));
			if (!Result.bSuccess)
			{
				return Result;
			}
		}
	}
	else if (Children.Num() > 0)
	{
		return Failure(Path, TEXT("BehaviorTreeRuntimeMirrorMismatch"), TEXT("Task graph wrapper unexpectedly owns runtime children"));
	}
	return FAssetDocumentCapabilityResult::Success();
}

UBehaviorTreeGraphNode_Root* FindSyntheticRoot(UBehaviorTreeGraph* Graph)
{
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UBehaviorTreeGraphNode_Root* Root = Cast<UBehaviorTreeGraphNode_Root>(Node))
		{
			return Root;
		}
	}
	return nullptr;
}

FAssetDocumentCapabilityResult AssertRuntimeMirror(UBehaviorTree* BehaviorTree, UBehaviorTreeGraph* Graph, const FString& Path)
{
	UBehaviorTreeGraphNode_Root* SyntheticRoot = FindSyntheticRoot(Graph);
	UEdGraphPin* RootOutput = FindPin(SyntheticRoot, EGPD_Output);
	UBehaviorTreeGraphNode* GraphRoot = RootOutput && RootOutput->LinkedTo.Num() == 1
		? Cast<UBehaviorTreeGraphNode>(RootOutput->LinkedTo[0]->GetOwningNode())
		: nullptr;
	if (!GraphRoot)
	{
		return BehaviorTree->RootNode == nullptr
			? FAssetDocumentCapabilityResult::Success()
			: Failure(Path, TEXT("BehaviorTreeRuntimeMirrorMismatch"), TEXT("Empty graph produced a non-empty runtime root"));
	}
	if (BehaviorTree->RootNode != Cast<UBTCompositeNode>(GraphRoot->NodeInstance))
	{
		return Failure(Path, TEXT("BehaviorTreeRuntimeMirrorMismatch"), TEXT("Runtime RootNode does not mirror the authored graph root wrapper"));
	}
	return AssertRuntimeMirrorNode(GraphRoot, JoinPath(Path, RootField));
}

FAssetDocumentCapabilityResult BuildGraph(
	UBehaviorTree* BehaviorTree,
	const FBehaviorTreeGraphSpec& Spec,
	const FString& Path,
	UBehaviorTreeGraph*& OutGraph)
{
	OutGraph = nullptr;
#if WITH_EDITOR
	const FName GraphName = MakeUniqueObjectName(BehaviorTree, UBehaviorTreeGraph::StaticClass(), TEXT("Behavior Tree Staging"));
	OutGraph = Cast<UBehaviorTreeGraph>(FBlueprintEditorUtils::CreateNewGraph(
		BehaviorTree,
		GraphName,
		UBehaviorTreeGraph::StaticClass(),
		UEdGraphSchema_BehaviorTree::StaticClass()));
	if (!OutGraph)
	{
		return Failure(Path, TEXT("MissingBehaviorTreeEditorGraph"), TEXT("Failed to create UBehaviorTreeGraph"));
	}
	BehaviorTree->BTGraph = OutGraph;
	OutGraph->GraphGuid = Spec.GraphGuid;
	OutGraph->LockUpdates();
	const UEdGraphSchema* Schema = OutGraph->GetSchema();
	if (!Schema)
	{
		OutGraph->UnlockUpdates();
		return Failure(Path, TEXT("MissingBehaviorTreeGraphSchema"), TEXT("UBehaviorTreeGraph has no BehaviorTree schema"));
	}
	Schema->CreateDefaultNodesForGraph(*OutGraph);
	UBehaviorTreeGraphNode_Root* SyntheticRoot = FindSyntheticRoot(OutGraph);
	if (!SyntheticRoot)
	{
		OutGraph->UnlockUpdates();
		return Failure(Path, TEXT("MissingBehaviorTreeSyntheticRoot"), TEXT("BehaviorTree schema did not create its synthetic root wrapper"));
	}

	if (Spec.Root.IsValid())
	{
		UBehaviorTreeGraphNode* RootNode = nullptr;
		FAssetDocumentCapabilityResult Result = BuildNodeSubtree(OutGraph, Schema, *Spec.Root, RootNode);
		if (!Result.bSuccess)
		{
			OutGraph->UnlockUpdates();
			return Result;
		}
		UEdGraphPin* RootOutput = FindPin(SyntheticRoot, EGPD_Output);
		UEdGraphPin* SemanticInput = FindPin(RootNode, EGPD_Input);
		if (!RootOutput || !SemanticInput || !Schema->TryCreateConnection(RootOutput, SemanticInput))
		{
			OutGraph->UnlockUpdates();
			return Failure(JoinPath(Path, RootField), TEXT("InvalidBehaviorTreeGraphConnection"), TEXT("UE BehaviorTree schema rejected the authored root connection"));
		}
	}
	OutGraph->UnlockUpdates();
	OutGraph->UpdateClassData();
	OutGraph->UpdateAsset(UBehaviorTreeGraph::ClearDebuggerFlags | UBehaviorTreeGraph::KeepRebuildCounter);
	FAssetDocumentCapabilityResult MirrorResult = AssertRuntimeMirror(BehaviorTree, OutGraph, Path);
	if (!MirrorResult.bSuccess)
	{
		return MirrorResult;
	}
	OutGraph->NotifyGraphChanged();
	return FAssetDocumentCapabilityResult::Success(TEXT("Built BehaviorTree graph source and runtime mirror"));
#else
	return Failure(Path, TEXT("EditorOnlyRegionUnavailable"), TEXT("BehaviorTree graph source requires WITH_EDITOR"));
#endif
}

void RenameObjectToTransient(UObject* Object)
{
	if (!Object || Object->GetOuter() == GetTransientPackage())
	{
		return;
	}
	const FName NewName = MakeUniqueObjectName(GetTransientPackage(), Object->GetClass(), Object->GetFName());
	Object->Rename(*NewName.ToString(), GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
}

void MoveGraphToTransient(UBehaviorTreeGraph* Graph)
{
	if (!Graph)
	{
		return;
	}
	for (UEdGraphNode* NodeObject : Graph->Nodes)
	{
		if (UBehaviorTreeGraphNode* Node = Cast<UBehaviorTreeGraphNode>(NodeObject))
		{
			RenameObjectToTransient(Node->NodeInstance);
			for (const TObjectPtr<UAIGraphNode>& SubNode : Node->SubNodes)
			{
				RenameObjectToTransient(SubNode ? SubNode->NodeInstance : nullptr);
			}
		}
	}
	RenameObjectToTransient(Graph);
}

bool WrapperMatchesInstance(const UBehaviorTreeGraphNode* Node)
{
	if (!Node || !Node->NodeInstance)
	{
		return false;
	}
	if (Node->IsA<UBehaviorTreeGraphNode_Service>())
	{
		return Node->NodeInstance->IsA<UBTService>();
	}
	if (Node->IsA<UBehaviorTreeGraphNode_Composite>())
	{
		return Node->NodeInstance->IsA<UBTCompositeNode>();
	}
	if (Node->IsA<UBehaviorTreeGraphNode_Task>())
	{
		return Node->NodeInstance->IsA<UBTTaskNode>();
	}
	return false;
}

FAssetDocumentCapabilityResult ValidateGraphService(
	UBehaviorTreeGraphNode* Service,
	const FString& Path,
	TSet<FGuid>& SeenGuids)
{
	if (!Service || !Service->IsA<UBehaviorTreeGraphNode_Service>() || !WrapperMatchesInstance(Service))
	{
		return Failure(Path, TEXT("InvalidBehaviorTreeGraphService"), TEXT("BehaviorTree service subnode wrapper/class is invalid"));
	}
	if (!Service->NodeGuid.IsValid())
	{
		return Failure(JoinPath(Path, IdField), TEXT("InvalidBehaviorTreeNodeGuid"), TEXT("BehaviorTree service has no persistent NodeGuid"));
	}
	if (SeenGuids.Contains(Service->NodeGuid))
	{
		return Failure(JoinPath(Path, IdField), TEXT("DuplicateBehaviorTreeNodeGuid"), TEXT("BehaviorTree graph contains duplicate NodeGuid identities"));
	}
	SeenGuids.Add(Service->NodeGuid);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateGraphNodeShallow(
	UBehaviorTreeGraphNode* Node,
	const FString& Path,
	bool bIsRoot,
	TSet<FGuid>& SeenGuids,
	TSet<FString>& SeenCoordinates)
{
	if (!Node || !WrapperMatchesInstance(Node))
	{
		return Failure(Path, TEXT("InvalidBehaviorTreeGraphNode"), TEXT("BehaviorTree graph wrapper/class is invalid"));
	}
	if (bIsRoot && !Node->NodeInstance->IsA<UBTCompositeNode>())
	{
		return Failure(JoinPath(Path, ClassField), TEXT("InvalidBehaviorTreeNodeClass"), TEXT("BehaviorTree graph root must be a composite"));
	}
	if (!Node->NodeGuid.IsValid())
	{
		return Failure(JoinPath(Path, IdField), TEXT("InvalidBehaviorTreeNodeGuid"), TEXT("BehaviorTree graph node has no persistent NodeGuid"));
	}
	if (SeenGuids.Contains(Node->NodeGuid))
	{
		return Failure(JoinPath(Path, IdField), TEXT("DuplicateBehaviorTreeNodeGuid"), TEXT("BehaviorTree graph contains duplicate NodeGuid identities"));
	}
	SeenGuids.Add(Node->NodeGuid);
	const FString Coordinate = FString::Printf(TEXT("%d:%d"), Node->NodePosX, Node->NodePosY);
	if (SeenCoordinates.Contains(Coordinate))
	{
		return Failure(JoinPath(JoinPath(JoinPath(Path, EditorField), PositionField), XField), TEXT("DuplicateBehaviorTreeCoordinate"), TEXT("BehaviorTree graph nodes share X/Y coordinates"));
	}
	SeenCoordinates.Add(Coordinate);
	if (Node->Decorators.Num() > 0)
	{
		return Failure(JoinPath(Path, DecoratorsField), TEXT("UnsupportedBehaviorTreeDecoratorsTask5"), TEXT("Decorator authored graphs are integrated by Task 5"));
	}
	for (int32 Index = 0; Index < Node->Services.Num(); ++Index)
	{
		FAssetDocumentCapabilityResult Result = ValidateGraphService(Node->Services[Index], JoinPath(JoinPath(Path, ServicesField), Index), SeenGuids);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateGraphTopology(
	UBehaviorTreeGraph* Graph,
	const FString& Path,
	UBehaviorTreeGraphNode*& OutSemanticRoot)
{
	OutSemanticRoot = nullptr;
	TArray<UBehaviorTreeGraphNode_Root*> SyntheticRoots;
	TArray<UBehaviorTreeGraphNode*> SemanticNodes;
	for (UEdGraphNode* NodeObject : Graph->Nodes)
	{
		if (Cast<UEdGraphNode_Comment>(NodeObject))
		{
			return Failure(JoinPath(Path, CommentsField), TEXT("UnsupportedBehaviorTreeCommentsTask5"), TEXT("BehaviorTree comment boxes are integrated by Task 5"));
		}
		if (UBehaviorTreeGraphNode_Root* Root = Cast<UBehaviorTreeGraphNode_Root>(NodeObject))
		{
			SyntheticRoots.Add(Root);
		}
		else if (UBehaviorTreeGraphNode* Node = Cast<UBehaviorTreeGraphNode>(NodeObject))
		{
			SemanticNodes.Add(Node);
		}
	}
	if (SyntheticRoots.Num() != 1)
	{
		return Failure(JoinPath(Path, RootField), SyntheticRoots.Num() > 1 ? TEXT("MultipleBehaviorTreeRoots") : TEXT("MissingBehaviorTreeRoot"), TEXT("BehaviorTree graph must contain exactly one synthetic root"));
	}
	UEdGraphPin* RootOutput = FindPin(SyntheticRoots[0], EGPD_Output);
	if (!RootOutput || RootOutput->LinkedTo.Num() > 1)
	{
		return Failure(JoinPath(Path, RootField), TEXT("MultipleBehaviorTreeRoots"), TEXT("BehaviorTree synthetic root must have zero or one connection"));
	}
	if (RootOutput->LinkedTo.Num() == 0)
	{
		return SemanticNodes.Num() == 0
			? FAssetDocumentCapabilityResult::Success()
			: Failure(JoinPath(Path, TEXT("Nodes")), TEXT("UnreachableBehaviorTreeGraphNode"), TEXT("BehaviorTree graph contains nodes unreachable from its root"));
	}
	OutSemanticRoot = Cast<UBehaviorTreeGraphNode>(RootOutput->LinkedTo[0]->GetOwningNode());
	if (!OutSemanticRoot)
	{
		return Failure(JoinPath(Path, RootField), TEXT("InvalidBehaviorTreeGraphConnection"), TEXT("BehaviorTree synthetic root is linked to a non-BehaviorTree node"));
	}
	UEdGraphPin* SemanticRootInput = FindPin(OutSemanticRoot, EGPD_Input);
	if (!SemanticRootInput)
	{
		return Failure(
			JoinPath(Path, RootField),
			TEXT("InvalidBehaviorTreeGraphConnection"),
			TEXT("BehaviorTree semantic root has no input pin"));
	}
	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema)
	{
		return Failure(Path, TEXT("MissingBehaviorTreeGraphSchema"), TEXT("UBehaviorTreeGraph has no BehaviorTree schema"));
	}

	TSet<FGuid> SeenGuids;
	TSet<FString> SeenCoordinates;
	TSet<UBehaviorTreeGraphNode*> Visited;
	TSet<UBehaviorTreeGraphNode*> Active;
	TFunction<FAssetDocumentCapabilityResult(UBehaviorTreeGraphNode*, const FString&, bool)> Visit;
	Visit = [&](UBehaviorTreeGraphNode* Node, const FString& NodePath, bool bIsRoot) -> FAssetDocumentCapabilityResult
	{
		if (Active.Contains(Node))
		{
			return Failure(NodePath, TEXT("BehaviorTreeGraphCycle"), TEXT("BehaviorTree graph contains a cycle"));
		}
		if (Visited.Contains(Node))
		{
			return Failure(NodePath, TEXT("MultipleBehaviorTreeParents"), TEXT("BehaviorTree graph node has more than one parent"));
		}
		FAssetDocumentCapabilityResult Result = ValidateGraphNodeShallow(Node, NodePath, bIsRoot, SeenGuids, SeenCoordinates);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Visited.Add(Node);
		Active.Add(Node);
		UEdGraphPin* Output = FindPin(Node, EGPD_Output);
		if (!Output && Node->NodeInstance->IsA<UBTCompositeNode>())
		{
			return Failure(NodePath, TEXT("InvalidBehaviorTreeGraphConnection"), TEXT("BehaviorTree graph node has no output pin"));
		}
		if (Output)
		{
			for (UEdGraphPin* LinkedPin : Output->LinkedTo)
			{
				if (!LinkedPin || !Cast<UBehaviorTreeGraphNode>(LinkedPin->GetOwningNode()))
				{
					return Failure(NodePath, TEXT("InvalidBehaviorTreeGraphConnection"), TEXT("BehaviorTree graph output is linked to a non-BehaviorTree node"));
				}
			}
		}
		const TArray<UBehaviorTreeGraphNode*> Children = SortedGraphChildren(Node);
		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			if (Index > 0 && Children[Index - 1]->NodePosX == Children[Index]->NodePosX)
			{
				return Failure(
					JoinPath(JoinPath(JoinPath(JoinPath(JoinPath(NodePath, ChildrenField), Index), EditorField), PositionField), XField),
					TEXT("DuplicateBehaviorTreeSiblingCoordinate"),
					TEXT("BehaviorTree siblings cannot share X coordinates"));
			}
			Result = Visit(Children[Index], JoinPath(JoinPath(NodePath, ChildrenField), Index), false);
			if (!Result.bSuccess)
			{
				return Result;
			}
			UEdGraphPin* Input = FindPin(Children[Index], EGPD_Input);
			if (!Input || Input->LinkedTo.Num() != 1 || Input->LinkedTo[0]->GetOwningNode() != Node)
			{
				return Failure(JoinPath(JoinPath(NodePath, ChildrenField), Index), TEXT("MultipleBehaviorTreeParents"), TEXT("BehaviorTree child input must have exactly one parent"));
			}
			if (!Output || Schema->CanCreateConnection(Output, Input).Response == CONNECT_RESPONSE_DISALLOW)
			{
				return Failure(
					JoinPath(JoinPath(NodePath, ChildrenField), Index),
					TEXT("InvalidBehaviorTreeGraphConnection"),
					TEXT("BehaviorTree graph contains a parent/child pin connection rejected by its schema"));
			}
		}
		if (Children.Num() > 0 && !Node->NodeInstance->IsA<UBTCompositeNode>())
		{
			return Failure(JoinPath(NodePath, ChildrenField), TEXT("InvalidBehaviorTreeParentClass"), TEXT("BehaviorTree task graph node cannot own children"));
		}
		Active.Remove(Node);
		return FAssetDocumentCapabilityResult::Success();
	};

	FAssetDocumentCapabilityResult Result = Visit(OutSemanticRoot, JoinPath(Path, RootField), true);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (SemanticRootInput->LinkedTo.Num() != 1 || SemanticRootInput->LinkedTo[0]->GetOwningNode() != SyntheticRoots[0])
	{
		return Failure(
			JoinPath(Path, RootField),
			TEXT("MultipleBehaviorTreeParents"),
			TEXT("BehaviorTree semantic root input must connect only to the synthetic root"));
	}
	if (Schema->CanCreateConnection(RootOutput, SemanticRootInput).Response == CONNECT_RESPONSE_DISALLOW)
	{
		return Failure(
			JoinPath(Path, RootField),
			TEXT("InvalidBehaviorTreeGraphConnection"),
			TEXT("BehaviorTree graph contains a root pin connection rejected by its schema"));
	}
	if (Visited.Num() != SemanticNodes.Num())
	{
		return Failure(JoinPath(Path, TEXT("Nodes")), TEXT("UnreachableBehaviorTreeGraphNode"), TEXT("BehaviorTree graph contains nodes unreachable from its root"));
	}
	return FAssetDocumentCapabilityResult::Success();
}

TSharedPtr<FJsonObject> MakeEditorJson(const UBehaviorTreeGraphNode* Node)
{
	TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
	Position->SetNumberField(XField, Node->NodePosX);
	Position->SetNumberField(YField, Node->NodePosY);
	TSharedPtr<FJsonObject> Editor = MakeShared<FJsonObject>();
	Editor->SetObjectField(PositionField, Position);
	Editor->SetStringField(NodeCommentField, Node->NodeComment);
	Editor->SetBoolField(CommentBubblePinnedField, Node->bCommentBubblePinned);
	Editor->SetBoolField(CommentBubbleVisibleField, Node->bCommentBubbleVisible);
	return Editor;
}

FAssetDocumentCapabilityResult ExtractService(
	UBehaviorTreeGraphNode* Service,
	const FString& Path,
	TSharedPtr<FJsonObject>& OutService)
{
	OutService = MakeShared<FJsonObject>();
	OutService->SetStringField(IdField, Service->NodeGuid.ToString(EGuidFormats::Digits));
	OutService->SetStringField(ClassField, Service->NodeInstance->GetClass()->GetPathName());
	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	FAssetDocumentCapabilityResult Result = FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(
		Service->NodeInstance,
		Properties,
		JoinPath(Path, PropertiesField));
	if (!Result.bSuccess)
	{
		return Result;
	}
	OutService->SetObjectField(PropertiesField, Properties);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractNode(
	UBehaviorTreeGraphNode* Node,
	const FString& Path,
	TSharedPtr<FJsonObject>& OutNode)
{
	OutNode = MakeShared<FJsonObject>();
	OutNode->SetStringField(IdField, Node->NodeGuid.ToString(EGuidFormats::Digits));
	OutNode->SetStringField(ClassField, Node->NodeInstance->GetClass()->GetPathName());
	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	FAssetDocumentCapabilityResult Result = FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(
		Node->NodeInstance,
		Properties,
		JoinPath(Path, PropertiesField));
	if (!Result.bSuccess)
	{
		return Result;
	}
	OutNode->SetObjectField(PropertiesField, Properties);
	OutNode->SetArrayField(DecoratorsField, {});

	TArray<TSharedPtr<FJsonValue>> Services;
	for (int32 Index = 0; Index < Node->Services.Num(); ++Index)
	{
		TSharedPtr<FJsonObject> Service;
		Result = ExtractService(Node->Services[Index], JoinPath(JoinPath(Path, ServicesField), Index), Service);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Services.Add(MakeShared<FJsonValueObject>(Service));
	}
	OutNode->SetArrayField(ServicesField, Services);

	TArray<TSharedPtr<FJsonValue>> Children;
	const TArray<UBehaviorTreeGraphNode*> GraphChildren = SortedGraphChildren(Node);
	for (int32 Index = 0; Index < GraphChildren.Num(); ++Index)
	{
		TSharedPtr<FJsonObject> Child;
		Result = ExtractNode(GraphChildren[Index], JoinPath(JoinPath(Path, ChildrenField), Index), Child);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Children.Add(MakeShared<FJsonValueObject>(Child));
	}
	OutNode->SetArrayField(ChildrenField, Children);
	OutNode->SetObjectField(EditorField, MakeEditorJson(Node));
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractGraphTree(
	const UBehaviorTree* BehaviorTree,
	const FString& Path,
	TSharedRef<FJsonObject>& OutTree)
{
	OutTree->Values.Reset();
	OutTree->SetObjectField(RootField, MakeShared<FJsonObject>());
	OutTree->SetArrayField(CommentsField, {});
	if (!BehaviorTree)
	{
		return Failure(Path, TEXT("UnsupportedAsset"), TEXT("BehaviorTree graph extraction requires UBehaviorTree"));
	}
	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph);
	if (!Graph)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Extracted empty BehaviorTree without editor graph"));
	}
	if (!Graph->GraphGuid.IsValid())
	{
		return Failure(JoinPath(Path, GraphGuidField), TEXT("InvalidBehaviorTreeGraphGuid"), TEXT("UBehaviorTreeGraph has no persistent GraphGuid"));
	}
	UBehaviorTreeGraphNode* Root = nullptr;
	FAssetDocumentCapabilityResult Result = ValidateGraphTopology(Graph, Path, Root);
	if (!Result.bSuccess)
	{
		return Result;
	}
	OutTree->SetStringField(GraphGuidField, Graph->GraphGuid.ToString(EGuidFormats::Digits));
	if (Root)
	{
		TSharedPtr<FJsonObject> RootJson;
		Result = ExtractNode(Root, JoinPath(Path, RootField), RootJson);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutTree->SetObjectField(RootField, RootJson);
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted BehaviorTree authored graph source"));
}

bool JsonEqual(const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
{
	return FAssetDocumentJsonRegionUtils::JsonValueToComparableString(A)
		== FAssetDocumentJsonRegionUtils::JsonValueToComparableString(B);
}

TSharedPtr<FJsonValue> ObjectValue(const TSharedPtr<FJsonObject>& Object)
{
	return MakeShared<FJsonValueObject>(Object);
}

TSharedPtr<FJsonValue> StringArrayValue(const TArray<FString>& Strings)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FString& String : Strings)
	{
		Values.Add(MakeShared<FJsonValueString>(String));
	}
	return MakeShared<FJsonValueArray>(Values);
}

void FlattenObject(
	const TSharedPtr<FJsonObject>& Object,
	const FString& Path,
	TMap<FString, TSharedPtr<FJsonValue>>& OutValues)
{
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		const FString ChildPath = JoinPath(Path, Pair.Key);
		if (Pair.Value.IsValid() && Pair.Value->Type == EJson::Object)
		{
			FlattenObject(Pair.Value->AsObject(), ChildPath, OutValues);
		}
		else
		{
			OutValues.Add(ChildPath, Pair.Value);
		}
	}
}

void FlattenNode(
	const TSharedPtr<FJsonObject>& Node,
	const FString& TreePath,
	TMap<FString, TSharedPtr<FJsonValue>>& OutValues)
{
	if (!Node.IsValid() || Node->Values.Num() == 0)
	{
		return;
	}
	const FString Id = Node->GetStringField(IdField);
	const FString NodePath = JoinPath(JoinPath(TreePath, TEXT("Nodes")), Id);
	OutValues.Add(JoinPath(NodePath, ClassField), Node->TryGetField(ClassField));
	if (const TSharedPtr<FJsonObject>* Properties = nullptr; Node->TryGetObjectField(PropertiesField, Properties) && Properties && Properties->IsValid())
	{
		FlattenObject(*Properties, JoinPath(NodePath, PropertiesField), OutValues);
	}
	if (const TSharedPtr<FJsonObject>* Editor = nullptr; Node->TryGetObjectField(EditorField, Editor) && Editor && Editor->IsValid())
	{
		FlattenObject(*Editor, JoinPath(NodePath, EditorField), OutValues);
	}

	TArray<FString> ServiceIds;
	const TArray<TSharedPtr<FJsonValue>>* Services = nullptr;
	if (Node->TryGetArrayField(ServicesField, Services) && Services)
	{
		for (const TSharedPtr<FJsonValue>& ServiceValue : *Services)
		{
			TSharedPtr<FJsonObject> Service = ServiceValue->AsObject();
			ServiceIds.Add(Service->GetStringField(IdField));
			FlattenNode(Service, TreePath, OutValues);
		}
	}
	OutValues.Add(JoinPath(NodePath, ServicesField), StringArrayValue(ServiceIds));

	TArray<FString> ChildIds;
	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (Node->TryGetArrayField(ChildrenField, Children) && Children)
	{
		for (const TSharedPtr<FJsonValue>& ChildValue : *Children)
		{
			TSharedPtr<FJsonObject> Child = ChildValue->AsObject();
			ChildIds.Add(Child->GetStringField(IdField));
			FlattenNode(Child, TreePath, OutValues);
		}
	}
	OutValues.Add(JoinPath(NodePath, ChildrenField), StringArrayValue(ChildIds));
}

void FlattenTree(
	const TSharedRef<FJsonObject>& Tree,
	const FString& Path,
	TMap<FString, TSharedPtr<FJsonValue>>& OutValues)
{
	OutValues.Add(JoinPath(Path, GraphGuidField), Tree->TryGetField(GraphGuidField));
	OutValues.Add(JoinPath(Path, CommentsField), Tree->TryGetField(CommentsField));
	const TSharedPtr<FJsonObject>* Root = nullptr;
	if (Tree->TryGetObjectField(RootField, Root) && Root && Root->IsValid() && (*Root)->Values.Num() > 0)
	{
		OutValues.Add(JoinPath(Path, RootField), MakeShared<FJsonValueString>((*Root)->GetStringField(IdField)));
		FlattenNode(*Root, Path, OutValues);
	}
	else
	{
		OutValues.Add(JoinPath(Path, RootField), MakeShared<FJsonValueNull>());
	}
}

void AddMapDiffs(
	const TMap<FString, TSharedPtr<FJsonValue>>& Current,
	const TMap<FString, TSharedPtr<FJsonValue>>& Desired,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
{
	TSet<FString> Paths;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Current)
	{
		Paths.Add(Pair.Key);
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Desired)
	{
		Paths.Add(Pair.Key);
	}
	TArray<FString> SortedPaths = Paths.Array();
	SortedPaths.Sort();
	for (const FString& Path : SortedPaths)
	{
		const TSharedPtr<FJsonValue>* CurrentValue = Current.Find(Path);
		const TSharedPtr<FJsonValue>* DesiredValue = Desired.Find(Path);
		const TSharedPtr<FJsonValue> CurrentOrNull = CurrentValue && CurrentValue->IsValid() ? *CurrentValue : MakeShared<FJsonValueNull>();
		const TSharedPtr<FJsonValue> DesiredOrNull = DesiredValue && DesiredValue->IsValid() ? *DesiredValue : MakeShared<FJsonValueNull>();
		if (!JsonEqual(CurrentOrNull, DesiredOrNull))
		{
			FAssetDocumentJsonRegionUtils::AddDiffEntry(OutDiffEntries, Path, TEXT("changed"), CurrentOrNull, DesiredOrNull);
		}
	}
}

void CollectSpecIds(const FBehaviorTreeNodeSpec& Node, TSet<FString>& OutIds)
{
	OutIds.Add(Node.Id);
	for (const FBehaviorTreeServiceSpec& Service : Node.Services)
	{
		OutIds.Add(Service.Id);
	}
	for (const TSharedPtr<FBehaviorTreeNodeSpec>& Child : Node.Children)
	{
		CollectSpecIds(*Child, OutIds);
	}
}
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentMaterializer::ValidateTree(
	const FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& Tree)
{
	FBehaviorTreeGraphSpec Spec;
	FAssetDocumentCapabilityResult Result = ParseTreeSpec(Context, Tree, Spec);
	if (!Result.bSuccess)
	{
		return Result;
	}
	UBehaviorTree* Preview = NewObject<UBehaviorTree>(GetTransientPackage(), NAME_None, RF_Transient);
	Preview->BlackboardAsset = Cast<UBehaviorTree>(Context.Asset) ? Cast<UBehaviorTree>(Context.Asset)->BlackboardAsset : nullptr;
	Result = ValidateSpecProperties(Spec, Preview);
	if (!Result.bSuccess)
	{
		return Result;
	}
	UBehaviorTreeGraph* StagingGraph = nullptr;
	return BuildGraph(Preview, Spec, RegionPath(Context), StagingGraph);
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentMaterializer::ValidateBodyCrossRegion(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& Body)
{
	const TSharedPtr<FJsonValue> TreeValue = Body->TryGetField(TEXT("Tree"));
	if (!TreeValue.IsValid())
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (TreeValue->Type != EJson::Object || !TreeValue->AsObject().IsValid())
	{
		return Failure(TEXT("/Body/Tree"), TEXT("InvalidBodySectionType"), TEXT("Body.Tree must be an object"));
	}
	FAssetDocumentRegionContext TreeContext;
	TreeContext.Asset = Context.Asset;
	TreeContext.AssetClass = Context.AssetClass;
	TreeContext.TargetAssetPath = Context.TargetAssetPath;
	TreeContext.SourceDocumentPath = Context.SourceDocumentPath;
	TreeContext.Definitions = Context.Definitions;
	TreeContext.Result = Context.Result;
	TreeContext.bIsDryRun = Context.bIsDryRun;
	TreeContext.BodyPath = TEXT("Body.Tree");
	TreeContext.JsonPointer = TEXT("/Body/Tree");
	return ValidateTree(TreeContext, TreeValue->AsObject().ToSharedRef());
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
		return Failure(RegionPath(Context), TEXT("UnsupportedAsset"), TEXT("BehaviorTree graph apply requires UBehaviorTree"));
	}

	FBehaviorTreeGraphSpec Spec;
	FAssetDocumentCapabilityResult Result = ParseTreeSpec(Context, Tree, Spec);
	if (!Result.bSuccess)
	{
		return Result;
	}
	UBehaviorTree* Preview = NewObject<UBehaviorTree>(GetTransientPackage(), NAME_None, RF_Transient);
	Preview->BlackboardAsset = BehaviorTree->BlackboardAsset;
	Result = ValidateSpecProperties(Spec, Preview);
	if (!Result.bSuccess)
	{
		return Result;
	}
	UBehaviorTreeGraph* StagingGraph = nullptr;
	Result = BuildGraph(Preview, Spec, RegionPath(Context), StagingGraph);
	if (!Result.bSuccess)
	{
		return Result;
	}
	TSharedRef<FJsonObject> DesiredCanonical = MakeShared<FJsonObject>();
	Result = ExtractGraphTree(Preview, RegionPath(Context), DesiredCanonical);
	if (!Result.bSuccess)
	{
		return Result;
	}
	TSharedRef<FJsonObject> CurrentCanonical = MakeShared<FJsonObject>();
	Result = ExtractGraphTree(BehaviorTree, RegionPath(Context), CurrentCanonical);
	if (!Result.bSuccess)
	{
		return Result;
	}
	bOutChanged = !JsonEqual(ObjectValue(CurrentCanonical), ObjectValue(DesiredCanonical));
	if (Context.bIsDryRun || !bOutChanged)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("BehaviorTree authored graph source unchanged"));
	}

	UBehaviorTreeGraph* PreviousGraph = Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph);
	UBTCompositeNode* PreviousRoot = BehaviorTree->RootNode;
	const auto PreviousRootDecorators = BehaviorTree->RootDecorators;
	const auto PreviousRootOps = BehaviorTree->RootDecoratorOps;
	UBlackboardData* PreviousBlackboard = BehaviorTree->BlackboardAsset;
	UBehaviorTreeGraph* NewGraph = nullptr;
	BehaviorTree->Modify();
	Result = BuildGraph(BehaviorTree, Spec, RegionPath(Context), NewGraph);
	if (!Result.bSuccess)
	{
		BehaviorTree->BTGraph = PreviousGraph;
		BehaviorTree->RootNode = PreviousRoot;
		BehaviorTree->RootDecorators = PreviousRootDecorators;
		BehaviorTree->RootDecoratorOps = PreviousRootOps;
		BehaviorTree->BlackboardAsset = PreviousBlackboard;
		MoveGraphToTransient(NewGraph);
		return Result;
	}
	BehaviorTree->BlackboardAsset = PreviousBlackboard;
	MoveGraphToTransient(PreviousGraph);
	if (NewGraph)
	{
		NewGraph->Rename(TEXT("Behavior Tree"), BehaviorTree, REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
		BehaviorTree->BTGraph = NewGraph;
		NewGraph->MarkPackageDirty();
	}
	BehaviorTree->MarkPackageDirty();
	return FAssetDocumentCapabilityResult::Success(TEXT("Applied BehaviorTree authored graph source"));
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentMaterializer::ExtractTree(
	const FAssetDocumentRegionContext& Context,
	TSharedRef<FJsonObject>& OutTree)
{
	return ExtractGraphTree(Cast<UBehaviorTree>(Context.Asset), RegionPath(Context), OutTree);
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentMaterializer::DiffTree(
	const FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& DesiredTree,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
{
	FBehaviorTreeGraphSpec Spec;
	FAssetDocumentCapabilityResult Result = ParseTreeSpec(Context, DesiredTree, Spec);
	if (!Result.bSuccess)
	{
		return Result;
	}
	UBehaviorTree* Preview = NewObject<UBehaviorTree>(GetTransientPackage(), NAME_None, RF_Transient);
	if (const UBehaviorTree* Existing = Cast<UBehaviorTree>(Context.Asset))
	{
		Preview->BlackboardAsset = Existing->BlackboardAsset;
	}
	Result = ValidateSpecProperties(Spec, Preview);
	if (!Result.bSuccess)
	{
		return Result;
	}
	UBehaviorTreeGraph* StagingGraph = nullptr;
	Result = BuildGraph(Preview, Spec, RegionPath(Context), StagingGraph);
	if (!Result.bSuccess)
	{
		return Result;
	}
	TSharedRef<FJsonObject> Current = MakeShared<FJsonObject>();
	Result = ExtractGraphTree(Cast<UBehaviorTree>(Context.Asset), RegionPath(Context), Current);
	if (!Result.bSuccess)
	{
		return Result;
	}
	TSharedRef<FJsonObject> DesiredCanonical = MakeShared<FJsonObject>();
	Result = ExtractGraphTree(Preview, RegionPath(Context), DesiredCanonical);
	if (!Result.bSuccess)
	{
		return Result;
	}
	TMap<FString, TSharedPtr<FJsonValue>> CurrentValues;
	TMap<FString, TSharedPtr<FJsonValue>> DesiredValues;
	FlattenTree(Current, RegionPath(Context), CurrentValues);
	FlattenTree(DesiredCanonical, RegionPath(Context), DesiredValues);
	AddMapDiffs(CurrentValues, DesiredValues, OutDiffEntries);
	return FAssetDocumentCapabilityResult::Success(TEXT("Diffed BehaviorTree authored graph source"));
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentMaterializer::CollectSemanticNodeIdsFromTree(
	const FAssetDocumentRegionContext& Context,
	const TSharedRef<FJsonObject>& Tree,
	TSet<FString>& OutIds)
{
	OutIds.Reset();
	FBehaviorTreeGraphSpec Spec;
	FAssetDocumentCapabilityResult Result = ParseTreeSpec(Context, Tree, Spec);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (Spec.Root.IsValid())
	{
		CollectSpecIds(*Spec.Root, OutIds);
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Collected BehaviorTree graph-source identities"));
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentMaterializer::CollectSemanticNodeIds(
	const FAssetDocumentRegionContext& Context,
	TSet<FString>& OutIds)
{
	OutIds.Reset();
	UEdGraph* Graph = nullptr;
	TMap<FString, UEdGraphNode*> Nodes;
	FAssetDocumentCapabilityResult Result = CollectEditorGraphNodes(Context, Graph, Nodes);
	if (!Result.bSuccess)
	{
		return Result;
	}
	Nodes.GetKeys(OutIds);
	return FAssetDocumentCapabilityResult::Success(TEXT("Collected BehaviorTree graph NodeGuid identities"));
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentMaterializer::RebuildEditorGraph(
	FAssetDocumentRegionContext& Context,
	bool,
	bool& bOutChanged)
{
	bOutChanged = false;
#if WITH_DEV_AUTOMATION_TESTS
	if (bFailNextEditorGraphRebuildForTest)
	{
		bFailNextEditorGraphRebuildForTest = false;
		return Failure(RegionPath(Context), TEXT("ForcedBehaviorTreeEditorGraphRebuildFailure"), TEXT("Forced BehaviorTree graph rebuild failure for automation coverage"));
	}
#endif
	UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(Context.Asset);
	UBehaviorTreeGraph* Graph = BehaviorTree ? Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph) : nullptr;
	if (!BehaviorTree || !Graph)
	{
		return Failure(RegionPath(Context), TEXT("MissingBehaviorTreeEditorGraph"), TEXT("BehaviorTree has no authored graph to rebuild"));
	}
	TSharedRef<FJsonObject> Validated = MakeShared<FJsonObject>();
	FAssetDocumentCapabilityResult Result = ExtractGraphTree(BehaviorTree, RegionPath(Context), Validated);
	if (!Result.bSuccess)
	{
		return Result;
	}
	Graph->UpdateClassData();
	Graph->UpdateAsset(UBehaviorTreeGraph::ClearDebuggerFlags | UBehaviorTreeGraph::KeepRebuildCounter);
	return AssertRuntimeMirror(BehaviorTree, Graph, RegionPath(Context));
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentMaterializer::CollectEditorGraphNodes(
	const FAssetDocumentRegionContext& Context,
	UEdGraph*& OutGraph,
	TMap<FString, UEdGraphNode*>& OutNodesById)
{
	OutGraph = nullptr;
	OutNodesById.Reset();
	const UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(Context.Asset);
	UBehaviorTreeGraph* Graph = BehaviorTree ? Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph) : nullptr;
	if (!Graph)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("BehaviorTree has no editor graph"));
	}
	UBehaviorTreeGraphNode* Root = nullptr;
	FAssetDocumentCapabilityResult Result = ValidateGraphTopology(Graph, RegionPath(Context), Root);
	if (!Result.bSuccess)
	{
		return Result;
	}
	OutGraph = Graph;
	for (UEdGraphNode* NodeObject : Graph->Nodes)
	{
		UBehaviorTreeGraphNode* Node = Cast<UBehaviorTreeGraphNode>(NodeObject);
		if (!Node || Node->IsA<UBehaviorTreeGraphNode_Root>())
		{
			continue;
		}
		OutNodesById.Add(Node->NodeGuid.ToString(EGuidFormats::Digits), Node);
		for (UBehaviorTreeGraphNode* Service : Node->Services)
		{
			OutNodesById.Add(Service->NodeGuid.ToString(EGuidFormats::Digits), Service);
		}
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Collected BehaviorTree graph wrappers by NodeGuid"));
}

#if WITH_DEV_AUTOMATION_TESTS
void FBehaviorTreeAssetDocumentMaterializer::FailNextEditorGraphRebuildForTest()
{
	bFailNextEditorGraphRebuildForTest = true;
}
#endif
