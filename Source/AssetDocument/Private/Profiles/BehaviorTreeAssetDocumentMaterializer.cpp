// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/BehaviorTreeAssetDocumentMaterializer.h"

#include "AIGraphSchema.h"
#include "AIGraphTypes.h"
#include "AssetDocumentJsonRegionUtils.h"
#include "Regions/AssetDocumentReflectedPropertyUtils.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_NativeEnum.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "BehaviorTree/Tasks/BTTask_RunBehavior.h"
#include "BehaviorTree/Tasks/BTTask_RunBehaviorDynamic.h"
#include "BehaviorTree/ValueOrBBKey.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "BehaviorTreeGraphNode_CompositeDecorator.h"
#include "BehaviorTreeGraphNode_Decorator.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BehaviorTreeGraphNode_Service.h"
#include "BehaviorTreeGraphNode_SimpleParallel.h"
#include "BehaviorTreeGraphNode_SubtreeTask.h"
#include "BehaviorTreeGraphNode_Task.h"
#include "BehaviorTreeDecoratorGraph.h"
#include "BehaviorTreeDecoratorGraphNode.h"
#include "BehaviorTreeDecoratorGraphNode_Decorator.h"
#include "BehaviorTreeDecoratorGraphNode_Logic.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_BehaviorTree.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/FieldIterator.h"
#include "UObject/Package.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
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
constexpr const TCHAR* KindField = TEXT("Kind");
constexpr const TCHAR* BoundGraphField = TEXT("BoundGraph");
constexpr const TCHAR* NodesField = TEXT("Nodes");
constexpr const TCHAR* LinksField = TEXT("Links");
constexpr const TCHAR* FromField = TEXT("From");
constexpr const TCHAR* ToField = TEXT("To");
constexpr const TCHAR* ToInputField = TEXT("ToInput");
constexpr const TCHAR* CompositeNameField = TEXT("CompositeName");
constexpr const TCHAR* ShowOperationsField = TEXT("bShowOperations");
constexpr const TCHAR* TextField = TEXT("Text");
constexpr const TCHAR* SizeField = TEXT("Size");
constexpr const TCHAR* WidthField = TEXT("Width");
constexpr const TCHAR* HeightField = TEXT("Height");
constexpr const TCHAR* ColorField = TEXT("Color");
constexpr const TCHAR* RField = TEXT("R");
constexpr const TCHAR* GField = TEXT("G");
constexpr const TCHAR* BField = TEXT("B");
constexpr const TCHAR* AField = TEXT("A");
constexpr const TCHAR* CommentDepthField = TEXT("CommentDepth");
constexpr const TCHAR* FontSizeField = TEXT("FontSize");
constexpr const TCHAR* MoveModeField = TEXT("MoveMode");
constexpr const TCHAR* NodeDetailsField = TEXT("NodeDetails");
constexpr const TCHAR* CommentBubbleVisibleInDetailsField = TEXT("bCommentBubbleVisible_InDetailsPanel");
constexpr const TCHAR* ColorCommentBubbleField = TEXT("bColorCommentBubble");

#if WITH_DEV_AUTOMATION_TESTS
bool bFailNextEditorGraphRebuildForTest = false;
bool bFailNextTreeGraphSwapForTest = false;
bool bFailNextPreviousTreeGraphCleanupForTest = false;
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
	FBehaviorTreeEditorSpec Editor;
};

struct FBehaviorTreeDecoratorGraphNodeSpec
{
	FGuid Guid;
	FString Id;
	FString JsonPath;
	FString Kind;
	UClass* NodeClass = nullptr;
	TSharedPtr<FJsonObject> Properties;
	FBehaviorTreeEditorSpec Editor;
};

struct FBehaviorTreeDecoratorGraphLinkSpec
{
	FGuid From;
	FGuid To;
	int32 ToInput = 0;
	FString JsonPath;
};

struct FBehaviorTreeDecoratorGraphSpec
{
	FGuid GraphGuid;
	TArray<FBehaviorTreeDecoratorGraphNodeSpec> Nodes;
	TArray<FBehaviorTreeDecoratorGraphLinkSpec> Links;
};

struct FBehaviorTreeDecoratorSpec
{
	FGuid Guid;
	FString Id;
	FString JsonPath;
	bool bComposite = false;
	UClass* NodeClass = nullptr;
	TSharedPtr<FJsonObject> Properties;
	FBehaviorTreeEditorSpec Editor;
	FString CompositeName;
	bool bShowOperations = true;
	FBehaviorTreeDecoratorGraphSpec BoundGraph;
};

struct FBehaviorTreeCommentSpec
{
	FGuid Guid;
	FString Id;
	FString Text;
	int32 X = 0;
	int32 Y = 0;
	int32 Width = 400;
	int32 Height = 100;
	FLinearColor Color = FLinearColor::White;
	int32 CommentDepth = -1;
	int32 FontSize = 18;
	ECommentBoxMode::Type MoveMode = ECommentBoxMode::GroupMovement;
	FText NodeDetails;
	bool bCommentBubblePinned = true;
	bool bCommentBubbleVisible = true;
	bool bCommentBubbleVisibleInDetails = true;
	bool bColorCommentBubble = false;
};

struct FBehaviorTreeNodeSpec
{
	FGuid Guid;
	FString Id;
	FString JsonPath;
	UClass* NodeClass = nullptr;
	TSharedPtr<FJsonObject> Properties;
	TArray<FBehaviorTreeDecoratorSpec> Decorators;
	TArray<FBehaviorTreeServiceSpec> Services;
	TArray<TSharedPtr<FBehaviorTreeNodeSpec>> Children;
	FBehaviorTreeEditorSpec Editor;
};

struct FBehaviorTreeGraphSpec
{
	FGuid GraphGuid;
	bool bGraphGuidWasAuthored = false;
	TSharedPtr<FBehaviorTreeNodeSpec> Root;
	TArray<FBehaviorTreeCommentSpec> Comments;
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

FAssetDocumentCapabilityResult ReadCanonicalGuidString(
	const TSharedRef<FJsonObject>& Object,
	const FString& Field,
	const FString& Path,
	FGuid& OutGuid)
{
	FString Value;
	if (!Object->TryGetStringField(Field, Value)
		|| !FGuid::ParseExact(Value, EGuidFormats::Digits, OutGuid)
		|| !OutGuid.IsValid())
	{
		return Failure(Path, TEXT("InvalidBehaviorTreeNodeGuid"), TEXT("BehaviorTree GUID references must be canonical 32-hex values"));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadFiniteNumber(
	const TSharedRef<FJsonObject>& Object,
	const FString& Field,
	const FString& Path,
	double& OutValue)
{
	if (!Object->TryGetNumberField(Field, OutValue) || !FMath::IsFinite(OutValue))
	{
		return Failure(Path, TEXT("InvalidBehaviorTreeNumber"), TEXT("BehaviorTree numeric fields must be finite numbers"));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseDecoratorGraph(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	FBehaviorTreeParseState& State,
	FBehaviorTreeDecoratorGraphSpec& OutGraph)
{
	FAssetDocumentCapabilityResult Result = CheckKnownFields(Object, Path, {GraphGuidField, NodesField, LinksField});
	if (!Result.bSuccess)
	{
		return Result;
	}
	FString GraphGuidString;
	if (!Object->TryGetStringField(GraphGuidField, GraphGuidString)
		|| !FGuid::ParseExact(GraphGuidString, EGuidFormats::Digits, OutGraph.GraphGuid)
		|| !OutGraph.GraphGuid.IsValid())
	{
		return Failure(JoinPath(Path, GraphGuidField), TEXT("InvalidBehaviorTreeGraphGuid"), TEXT("BoundGraph GraphGuid must be a valid canonical 32-hex GUID"));
	}

	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	Result = ReadArray(Object, NodesField, JoinPath(Path, NodesField), Nodes, false);
	if (!Result.bSuccess)
	{
		return Result;
	}
	for (int32 Index = 0; Nodes && Index < Nodes->Num(); ++Index)
	{
		const FString NodePath = JoinPath(JoinPath(Path, NodesField), Index);
		TSharedPtr<FJsonObject> NodeObject;
		Result = FAssetDocumentJsonRegionUtils::RequireObjectValue((*Nodes)[Index], NodePath, NodeObject);
		if (!Result.bSuccess)
		{
			return Failure(NodePath, TEXT("InvalidBehaviorTreeDecoratorNode"), TEXT("BoundGraph Nodes entries must be objects"));
		}
		Result = CheckKnownFields(NodeObject.ToSharedRef(), NodePath, {IdField, KindField, ClassField, PropertiesField, EditorField});
		if (!Result.bSuccess)
		{
			return Result;
		}
		FBehaviorTreeDecoratorGraphNodeSpec Node;
		Node.JsonPath = NodePath;
		Result = ReadGuid(NodeObject.ToSharedRef(), NodePath, State, Node.Guid, Node.Id);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (!NodeObject->TryGetStringField(KindField, Node.Kind)
			|| (Node.Kind != TEXT("Sink") && Node.Kind != TEXT("Test") && Node.Kind != TEXT("And") && Node.Kind != TEXT("Or") && Node.Kind != TEXT("Not")))
		{
			return Failure(JoinPath(NodePath, KindField), TEXT("InvalidBehaviorTreeDecoratorKind"), TEXT("BoundGraph Kind must be Sink, Test, And, Or, or Not"));
		}
		if (Node.Kind == TEXT("Test"))
		{
			Result = ResolveConcreteNodeClass(NodeObject.ToSharedRef(), NodePath, UBTDecorator::StaticClass(), Node.NodeClass);
			if (!Result.bSuccess)
			{
				return Result;
			}
			Result = ReadProperties(NodeObject.ToSharedRef(), NodePath, Node.Properties);
			if (!Result.bSuccess)
			{
				return Result;
			}
		}
		else if (NodeObject->HasField(ClassField) || NodeObject->HasField(PropertiesField))
		{
			return Failure(
				JoinPath(NodePath, NodeObject->HasField(ClassField) ? ClassField : PropertiesField),
				TEXT("InvalidBehaviorTreeDecoratorNode"),
				TEXT("Only Test BoundGraph nodes may declare Class or Properties"));
		}
		Result = ParseEditor(NodeObject.ToSharedRef(), NodePath, State, Node.Editor);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutGraph.Nodes.Add(MoveTemp(Node));
	}

	const TArray<TSharedPtr<FJsonValue>>* Links = nullptr;
	Result = ReadArray(Object, LinksField, JoinPath(Path, LinksField), Links, false);
	if (!Result.bSuccess)
	{
		return Result;
	}
	TMap<FGuid, int32> NodeIndices;
	for (int32 Index = 0; Index < OutGraph.Nodes.Num(); ++Index)
	{
		NodeIndices.Add(OutGraph.Nodes[Index].Guid, Index);
	}
	TMap<FGuid, int32> OutgoingLinkIndices;
	TSet<FString> TargetInputs;
	for (int32 Index = 0; Links && Index < Links->Num(); ++Index)
	{
		const FString LinkPath = JoinPath(JoinPath(Path, LinksField), Index);
		TSharedPtr<FJsonObject> LinkObject;
		Result = FAssetDocumentJsonRegionUtils::RequireObjectValue((*Links)[Index], LinkPath, LinkObject);
		if (!Result.bSuccess)
		{
			return Failure(LinkPath, TEXT("InvalidBehaviorTreeDecoratorLink"), TEXT("BoundGraph Links entries must be objects"));
		}
		Result = CheckKnownFields(LinkObject.ToSharedRef(), LinkPath, {FromField, ToField, ToInputField});
		if (!Result.bSuccess)
		{
			return Result;
		}
		FBehaviorTreeDecoratorGraphLinkSpec Link;
		Link.JsonPath = LinkPath;
		Result = ReadCanonicalGuidString(LinkObject.ToSharedRef(), FromField, JoinPath(LinkPath, FromField), Link.From);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = ReadCanonicalGuidString(LinkObject.ToSharedRef(), ToField, JoinPath(LinkPath, ToField), Link.To);
		if (!Result.bSuccess)
		{
			return Result;
		}
		double ToInputNumber = 0.0;
		if (!LinkObject->TryGetNumberField(ToInputField, ToInputNumber)
			|| ToInputNumber < 0.0
			|| ToInputNumber > MAX_int32
			|| static_cast<double>(static_cast<int32>(ToInputNumber)) != ToInputNumber)
		{
			return Failure(JoinPath(LinkPath, ToInputField), TEXT("InvalidBehaviorTreeDecoratorInput"), TEXT("ToInput must be a non-negative int32"));
		}
		Link.ToInput = static_cast<int32>(ToInputNumber);
		if (!NodeIndices.Contains(Link.From))
		{
			return Failure(JoinPath(LinkPath, FromField), TEXT("DanglingBehaviorTreeDecoratorLink"), TEXT("BoundGraph link From does not reference a node"));
		}
		if (!NodeIndices.Contains(Link.To))
		{
			return Failure(JoinPath(LinkPath, ToField), TEXT("DanglingBehaviorTreeDecoratorLink"), TEXT("BoundGraph link To does not reference a node"));
		}
		if (OutgoingLinkIndices.Contains(Link.From))
		{
			return Failure(JoinPath(LinkPath, FromField), TEXT("MultipleBehaviorTreeDecoratorLinks"), TEXT("BoundGraph nodes may have only one outgoing link"));
		}
		OutgoingLinkIndices.Add(Link.From, Index);
		const FString TargetInput = FString::Printf(TEXT("%s:%d"), *Link.To.ToString(EGuidFormats::Digits), Link.ToInput);
		if (TargetInputs.Contains(TargetInput))
		{
			return Failure(JoinPath(LinkPath, ToInputField), TEXT("MultipleBehaviorTreeDecoratorLinks"), TEXT("BoundGraph input pins may have only one incoming link"));
		}
		TargetInputs.Add(TargetInput);
		OutGraph.Links.Add(Link);
	}

	// Reject cycles before structural arity checks so the closing link is the diagnostic anchor.
	TMap<FGuid, const FBehaviorTreeDecoratorGraphLinkSpec*> Outgoing;
	for (const FBehaviorTreeDecoratorGraphLinkSpec& Link : OutGraph.Links)
	{
		Outgoing.Add(Link.From, &Link);
	}
	TSet<FGuid> Visited;
	TSet<FGuid> Active;
	TFunction<FAssetDocumentCapabilityResult(const FGuid&)> Visit;
	Visit = [&Visit, &Visited, &Active, &Outgoing](const FGuid& NodeId) -> FAssetDocumentCapabilityResult
	{
		if (Visited.Contains(NodeId))
		{
			return FAssetDocumentCapabilityResult::Success();
		}
		Active.Add(NodeId);
		if (const FBehaviorTreeDecoratorGraphLinkSpec* const* LinkPtr = Outgoing.Find(NodeId))
		{
			const FBehaviorTreeDecoratorGraphLinkSpec* Link = *LinkPtr;
			if (Active.Contains(Link->To))
			{
				return Failure(Link->JsonPath, TEXT("BehaviorTreeDecoratorGraphCycle"), TEXT("BoundGraph contains a cycle"));
			}
			FAssetDocumentCapabilityResult VisitResult = Visit(Link->To);
			if (!VisitResult.bSuccess)
			{
				return VisitResult;
			}
		}
		Active.Remove(NodeId);
		Visited.Add(NodeId);
		return FAssetDocumentCapabilityResult::Success();
	};
	for (const FBehaviorTreeDecoratorGraphNodeSpec& Node : OutGraph.Nodes)
	{
		Result = Visit(Node.Guid);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	int32 SinkCount = 0;
	FGuid SinkGuid;
	for (const FBehaviorTreeDecoratorGraphNodeSpec& Node : OutGraph.Nodes)
	{
		int32 IncomingCount = 0;
		for (const FBehaviorTreeDecoratorGraphLinkSpec& Link : OutGraph.Links)
		{
			IncomingCount += Link.To == Node.Guid ? 1 : 0;
		}
		const bool bVariadicLogic = Node.Kind == TEXT("And") || Node.Kind == TEXT("Or");
		const int32 ExpectedInputs = bVariadicLogic
			? IncomingCount
			: (Node.Kind == TEXT("Sink") || Node.Kind == TEXT("Not") ? 1 : 0);
		bool bInputsCanonical = IncomingCount == ExpectedInputs;
		if (bVariadicLogic && ExpectedInputs < 2)
		{
			bInputsCanonical = false;
		}
		for (int32 InputIndex = 0; bInputsCanonical && InputIndex < ExpectedInputs; ++InputIndex)
		{
			bInputsCanonical = OutGraph.Links.ContainsByPredicate([&Node, InputIndex](const FBehaviorTreeDecoratorGraphLinkSpec& Link)
			{
				return Link.To == Node.Guid && Link.ToInput == InputIndex;
			});
		}
		if (!bInputsCanonical || OutGraph.Links.ContainsByPredicate([&Node, ExpectedInputs](const FBehaviorTreeDecoratorGraphLinkSpec& Link)
			{ return Link.To == Node.Guid && Link.ToInput >= ExpectedInputs; }))
		{
			return Failure(Node.JsonPath, TEXT("InvalidBehaviorTreeDecoratorArity"), TEXT("BoundGraph node input arity does not match its Kind"));
		}
		const bool bHasOutgoing = Outgoing.Contains(Node.Guid);
		if (Node.Kind == TEXT("Sink"))
		{
			++SinkCount;
			SinkGuid = Node.Guid;
			if (bHasOutgoing)
			{
				return Failure(Node.JsonPath, TEXT("InvalidBehaviorTreeDecoratorArity"), TEXT("BoundGraph Sink cannot have an outgoing link"));
			}
		}
		else if (!bHasOutgoing)
		{
			return Failure(Node.JsonPath, TEXT("UnreachableBehaviorTreeDecoratorNode"), TEXT("BoundGraph node does not reach the Sink"));
		}
	}
	if (SinkCount != 1)
	{
		return Failure(JoinPath(Path, NodesField), TEXT("InvalidBehaviorTreeDecoratorSink"), TEXT("BoundGraph must contain exactly one Sink"));
	}
	for (const FBehaviorTreeDecoratorGraphNodeSpec& Node : OutGraph.Nodes)
	{
		FGuid Current = Node.Guid;
		TSet<FGuid> Chain;
		while (Current != SinkGuid && !Chain.Contains(Current))
		{
			Chain.Add(Current);
			const FBehaviorTreeDecoratorGraphLinkSpec* const* Link = Outgoing.Find(Current);
			if (!Link)
			{
				return Failure(Node.JsonPath, TEXT("UnreachableBehaviorTreeDecoratorNode"), TEXT("BoundGraph node does not reach the Sink"));
			}
			Current = (*Link)->To;
		}
		if (Current != SinkGuid)
		{
			return Failure(Node.JsonPath, TEXT("UnreachableBehaviorTreeDecoratorNode"), TEXT("BoundGraph node does not reach the Sink"));
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseDecorator(
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	FBehaviorTreeParseState& State,
	FBehaviorTreeDecoratorSpec& OutDecorator)
{
	TSharedPtr<FJsonObject> Object;
	FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(Value, Path, Object);
	if (!Result.bSuccess)
	{
		return Failure(Path, TEXT("InvalidBehaviorTreeDecorator"), TEXT("BehaviorTree Decorators entries must be objects"));
	}
	FString Kind;
	const bool bHasKind = Object->TryGetStringField(KindField, Kind);
	OutDecorator.bComposite = bHasKind && Kind == TEXT("Composite");
	if (Object->HasField(KindField) && !OutDecorator.bComposite)
	{
		return Failure(JoinPath(Path, KindField), TEXT("InvalidBehaviorTreeDecoratorKind"), TEXT("Decorator Kind must be Composite when present"));
	}
	Result = OutDecorator.bComposite
		? CheckKnownFields(Object.ToSharedRef(), Path, {IdField, KindField, PropertiesField, EditorField, BoundGraphField})
		: CheckKnownFields(Object.ToSharedRef(), Path, {IdField, ClassField, PropertiesField, EditorField});
	if (!Result.bSuccess)
	{
		return Result;
	}
	OutDecorator.JsonPath = Path;
	Result = ReadGuid(Object.ToSharedRef(), Path, State, OutDecorator.Guid, OutDecorator.Id);
	if (!Result.bSuccess)
	{
		return Result;
	}
	Result = ParseEditor(Object.ToSharedRef(), Path, State, OutDecorator.Editor);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (!OutDecorator.bComposite)
	{
		Result = ResolveConcreteNodeClass(Object.ToSharedRef(), Path, UBTDecorator::StaticClass(), OutDecorator.NodeClass);
		if (!Result.bSuccess)
		{
			return Result;
		}
		return ReadProperties(Object.ToSharedRef(), Path, OutDecorator.Properties);
	}

	TSharedPtr<FJsonObject> Properties;
	Result = RequireObject(Object.ToSharedRef(), PropertiesField, JoinPath(Path, PropertiesField), Properties);
	if (!Result.bSuccess)
	{
		return Result;
	}
	Result = CheckKnownFields(Properties.ToSharedRef(), JoinPath(Path, PropertiesField), {CompositeNameField, ShowOperationsField});
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (Properties->HasField(CompositeNameField) && !Properties->TryGetStringField(CompositeNameField, OutDecorator.CompositeName))
	{
		return Failure(JoinPath(JoinPath(Path, PropertiesField), CompositeNameField), TEXT("InvalidBehaviorTreeDecoratorProperty"), TEXT("CompositeName must be a string"));
	}
	if (Properties->HasField(ShowOperationsField) && !Properties->TryGetBoolField(ShowOperationsField, OutDecorator.bShowOperations))
	{
		return Failure(JoinPath(JoinPath(Path, PropertiesField), ShowOperationsField), TEXT("InvalidBehaviorTreeDecoratorProperty"), TEXT("bShowOperations must be a boolean"));
	}
	TSharedPtr<FJsonObject> BoundGraph;
	Result = RequireObject(Object.ToSharedRef(), BoundGraphField, JoinPath(Path, BoundGraphField), BoundGraph);
	return Result.bSuccess
		? ParseDecoratorGraph(BoundGraph.ToSharedRef(), JoinPath(Path, BoundGraphField), State, OutDecorator.BoundGraph)
		: Result;
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
	Result = CheckKnownFields(Object.ToSharedRef(), Path, {IdField, ClassField, PropertiesField, EditorField});
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
	Result = ReadProperties(Object.ToSharedRef(), Path, OutService.Properties);
	return Result.bSuccess ? ParseEditor(Object.ToSharedRef(), Path, State, OutService.Editor) : Result;
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
		for (int32 Index = 0; Index < Decorators->Num(); ++Index)
		{
			FBehaviorTreeDecoratorSpec Decorator;
			Result = ParseDecorator((*Decorators)[Index], JoinPath(JoinPath(Path, DecoratorsField), Index), State, Decorator);
			if (!Result.bSuccess)
			{
				return Result;
			}
			OutNode.Decorators.Add(MoveTemp(Decorator));
		}
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
	const bool bPinSemanticChildren = OutNode.NodeClass->IsChildOf(UBTComposite_SimpleParallel::StaticClass());
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
			if (!bPinSemanticChildren && Index > 0 && Child->Editor.X == PreviousX)
			{
				return Failure(
					JoinPath(JoinPath(JoinPath(JoinPath(JoinPath(Path, ChildrenField), Index), EditorField), PositionField), XField),
					TEXT("DuplicateBehaviorTreeSiblingCoordinate"),
					TEXT("BehaviorTree siblings cannot share X coordinates"));
			}
			if (!bPinSemanticChildren && Index > 0 && Child->Editor.X < PreviousX)
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

UEdGraphNode_Comment* FindExistingComment(const FAssetDocumentRegionContext& Context, const FGuid& Guid)
{
	const UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(Context.Asset);
	const UBehaviorTreeGraph* Graph = BehaviorTree ? Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph) : nullptr;
	if (!Graph)
	{
		return nullptr;
	}
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node); Comment && Comment->NodeGuid == Guid)
		{
			return Comment;
		}
	}
	return nullptr;
}

FAssetDocumentCapabilityResult ReadOptionalIntField(
	const TSharedRef<FJsonObject>& Object,
	const FString& Field,
	const FString& Path,
	int32& OutValue)
{
	if (!Object->HasField(Field))
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	return ReadIntCoordinate(Object, Field, Path, OutValue);
}

FAssetDocumentCapabilityResult ReadOptionalColorField(
	const TSharedRef<FJsonObject>& Object,
	const FString& Field,
	const FString& Path,
	float& OutValue)
{
	if (!Object->HasField(Field))
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	double Number = 0.0;
	FAssetDocumentCapabilityResult Result = ReadFiniteNumber(Object, Field, Path, Number);
	if (!Result.bSuccess)
	{
		return Result;
	}
	OutValue = static_cast<float>(Number);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseComment(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	FBehaviorTreeParseState& State,
	FBehaviorTreeCommentSpec& OutComment)
{
	TSharedPtr<FJsonObject> Object;
	FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(Value, Path, Object);
	if (!Result.bSuccess)
	{
		return Failure(Path, TEXT("InvalidBehaviorTreeComment"), TEXT("BehaviorTree Comments entries must be objects"));
	}
	Result = CheckKnownFields(Object.ToSharedRef(), Path, {
		IdField, TextField, PositionField, SizeField, ColorField, CommentDepthField, FontSizeField,
		MoveModeField, NodeDetailsField, CommentBubblePinnedField, CommentBubbleVisibleField,
		CommentBubbleVisibleInDetailsField, ColorCommentBubbleField});
	if (!Result.bSuccess)
	{
		return Result;
	}
	Result = ReadGuid(Object.ToSharedRef(), Path, State, OutComment.Guid, OutComment.Id);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (const UEdGraphNode_Comment* Existing = FindExistingComment(Context, OutComment.Guid))
	{
		OutComment.Text = Existing->NodeComment;
		OutComment.X = Existing->NodePosX;
		OutComment.Y = Existing->NodePosY;
		OutComment.Width = Existing->NodeWidth;
		OutComment.Height = Existing->NodeHeight;
		OutComment.Color = Existing->CommentColor;
		OutComment.CommentDepth = Existing->CommentDepth;
		OutComment.FontSize = Existing->FontSize;
		OutComment.MoveMode = Existing->MoveMode.GetValue();
		OutComment.NodeDetails = Existing->NodeDetails;
		OutComment.bCommentBubblePinned = Existing->bCommentBubblePinned;
		OutComment.bCommentBubbleVisible = Existing->bCommentBubbleVisible;
		OutComment.bCommentBubbleVisibleInDetails = Existing->bCommentBubbleVisible_InDetailsPanel;
		OutComment.bColorCommentBubble = Existing->bColorCommentBubble;
	}
	if (Object->HasField(TextField) && !Object->TryGetStringField(TextField, OutComment.Text))
	{
		return Failure(JoinPath(Path, TextField), TEXT("InvalidBehaviorTreeCommentField"), TEXT("Comment Text must be a string"));
	}
	TSharedPtr<FJsonObject> Position;
	Result = RequireObject(Object.ToSharedRef(), PositionField, JoinPath(Path, PositionField), Position, true);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (Position)
	{
		Result = CheckKnownFields(Position.ToSharedRef(), JoinPath(Path, PositionField), {XField, YField});
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = ReadOptionalIntField(Position.ToSharedRef(), XField, JoinPath(JoinPath(Path, PositionField), XField), OutComment.X);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = ReadOptionalIntField(Position.ToSharedRef(), YField, JoinPath(JoinPath(Path, PositionField), YField), OutComment.Y);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}
	TSharedPtr<FJsonObject> Size;
	Result = RequireObject(Object.ToSharedRef(), SizeField, JoinPath(Path, SizeField), Size, true);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (Size)
	{
		Result = CheckKnownFields(Size.ToSharedRef(), JoinPath(Path, SizeField), {WidthField, HeightField});
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = ReadOptionalIntField(Size.ToSharedRef(), WidthField, JoinPath(JoinPath(Path, SizeField), WidthField), OutComment.Width);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = ReadOptionalIntField(Size.ToSharedRef(), HeightField, JoinPath(JoinPath(Path, SizeField), HeightField), OutComment.Height);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (OutComment.Width <= 0 || OutComment.Height <= 0)
		{
			return Failure(JoinPath(Path, SizeField), TEXT("InvalidBehaviorTreeCommentSize"), TEXT("Comment Width and Height must be positive"));
		}
	}
	TSharedPtr<FJsonObject> Color;
	Result = RequireObject(Object.ToSharedRef(), ColorField, JoinPath(Path, ColorField), Color, true);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (Color)
	{
		Result = CheckKnownFields(Color.ToSharedRef(), JoinPath(Path, ColorField), {RField, GField, BField, AField});
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (!(Result = ReadOptionalColorField(Color.ToSharedRef(), RField, JoinPath(JoinPath(Path, ColorField), RField), OutComment.Color.R)).bSuccess
			|| !(Result = ReadOptionalColorField(Color.ToSharedRef(), GField, JoinPath(JoinPath(Path, ColorField), GField), OutComment.Color.G)).bSuccess
			|| !(Result = ReadOptionalColorField(Color.ToSharedRef(), BField, JoinPath(JoinPath(Path, ColorField), BField), OutComment.Color.B)).bSuccess
			|| !(Result = ReadOptionalColorField(Color.ToSharedRef(), AField, JoinPath(JoinPath(Path, ColorField), AField), OutComment.Color.A)).bSuccess)
		{
			return Result;
		}
	}
	if (!(Result = ReadOptionalIntField(Object.ToSharedRef(), CommentDepthField, JoinPath(Path, CommentDepthField), OutComment.CommentDepth)).bSuccess
		|| !(Result = ReadOptionalIntField(Object.ToSharedRef(), FontSizeField, JoinPath(Path, FontSizeField), OutComment.FontSize)).bSuccess)
	{
		return Result;
	}
	if (OutComment.FontSize < 1)
	{
		return Failure(JoinPath(Path, FontSizeField), TEXT("InvalidBehaviorTreeCommentField"), TEXT("Comment FontSize must be positive"));
	}
	FString MoveMode;
	if (Object->HasField(MoveModeField))
	{
		if (!Object->TryGetStringField(MoveModeField, MoveMode)
			|| (MoveMode != TEXT("GroupMovement") && MoveMode != TEXT("NoGroupMovement")))
		{
			return Failure(JoinPath(Path, MoveModeField), TEXT("InvalidBehaviorTreeCommentField"), TEXT("MoveMode must be GroupMovement or NoGroupMovement"));
		}
		OutComment.MoveMode = MoveMode == TEXT("NoGroupMovement") ? ECommentBoxMode::NoGroupMovement : ECommentBoxMode::GroupMovement;
	}
	FString Details;
	if (Object->HasField(NodeDetailsField))
	{
		if (!Object->TryGetStringField(NodeDetailsField, Details))
		{
			return Failure(JoinPath(Path, NodeDetailsField), TEXT("InvalidBehaviorTreeCommentField"), TEXT("NodeDetails must be a string"));
		}
		OutComment.NodeDetails = FText::FromString(Details);
	}
	const auto ReadOptionalBool = [&Object, &Path](const FString& Field, bool& OutValue) -> FAssetDocumentCapabilityResult
	{
		if (Object->HasField(Field) && !Object->TryGetBoolField(Field, OutValue))
		{
			return Failure(JoinPath(Path, Field), TEXT("InvalidBehaviorTreeCommentField"), FString::Printf(TEXT("%s must be a boolean"), *Field));
		}
		return FAssetDocumentCapabilityResult::Success();
	};
	if (!(Result = ReadOptionalBool(CommentBubblePinnedField, OutComment.bCommentBubblePinned)).bSuccess
		|| !(Result = ReadOptionalBool(CommentBubbleVisibleField, OutComment.bCommentBubbleVisible)).bSuccess
		|| !(Result = ReadOptionalBool(CommentBubbleVisibleInDetailsField, OutComment.bCommentBubbleVisibleInDetails)).bSuccess
		|| !(Result = ReadOptionalBool(ColorCommentBubbleField, OutComment.bColorCommentBubble)).bSuccess)
	{
		return Result;
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

bool HasExistingGraphGuid(const FAssetDocumentRegionContext& Context)
{
	const UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(Context.Asset);
	const UBehaviorTreeGraph* Graph = BehaviorTree ? Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph) : nullptr;
	return Graph && Graph->GraphGuid.IsValid();
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
	const UBehaviorTree* ExistingBehaviorTree = Cast<UBehaviorTree>(Context.Asset);
	const UBehaviorTreeGraph* ExistingGraph = ExistingBehaviorTree
		? Cast<UBehaviorTreeGraph>(ExistingBehaviorTree->BTGraph)
		: nullptr;
	FString GraphGuidString;
	if (Tree->TryGetStringField(GraphGuidField, GraphGuidString))
	{
		if (!FGuid::ParseExact(GraphGuidString, EGuidFormats::Digits, OutSpec.GraphGuid) || !OutSpec.GraphGuid.IsValid())
		{
			return Failure(JoinPath(Path, GraphGuidField), TEXT("InvalidBehaviorTreeGraphGuid"), TEXT("GraphGuid must be a valid canonical 32-hex GUID"));
		}
		OutSpec.bGraphGuidWasAuthored = true;
		if (ExistingGraph && ExistingGraph->GraphGuid.IsValid() && OutSpec.GraphGuid != ExistingGraph->GraphGuid)
		{
			return Failure(
				JoinPath(Path, GraphGuidField),
				TEXT("BehaviorTreeGraphGuidReplacementRejected"),
				TEXT("Existing BehaviorTree graph identity cannot be replaced through Tree apply"));
		}
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
	TSharedPtr<FJsonObject> Root;
	Result = RequireObject(Tree, RootField, JoinPath(Path, RootField), Root);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (!OutSpec.bGraphGuidWasAuthored && !HasExistingGraphGuid(Context))
	{
		return Failure(JoinPath(Path, GraphGuidField), TEXT("MissingBehaviorTreeGraphGuid"), TEXT("A new BehaviorTree graph requires an authored GraphGuid"));
	}

	FBehaviorTreeParseState State;
	if (Comments)
	{
		for (int32 Index = 0; Index < Comments->Num(); ++Index)
		{
			FBehaviorTreeCommentSpec Comment;
			Result = ParseComment(Context, (*Comments)[Index], JoinPath(JoinPath(Path, CommentsField), Index), State, Comment);
			if (!Result.bSuccess)
			{
				return Result;
			}
			OutSpec.Comments.Add(MoveTemp(Comment));
		}
	}
	if (Root->Values.Num() == 0)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Parsed empty BehaviorTree graph source"));
	}

	OutSpec.Root = MakeShared<FBehaviorTreeNodeSpec>();
	return ParseNode(Root.ToSharedRef(), JoinPath(Path, RootField), true, 0, State, *OutSpec.Root);
}

FAssetDocumentCapabilityResult ValidateSelector(
	FBlackboardKeySelector& Selector,
	UBlackboardData* Blackboard,
	const FString& SelectorPath,
	bool bTreatPropertiesAsAuthored)
{
	const FString KeyPath = JoinPath(SelectorPath, TEXT("Key"));
	if (Selector.SelectedKeyName.IsNone())
	{
		Selector.InvalidateResolvedKey();
		Selector.SelectedKeyType = nullptr;
		return Selector.IsNone() || !bTreatPropertiesAsAuthored
			? FAssetDocumentCapabilityResult::Success()
			: Failure(KeyPath, TEXT("MissingBlackboardKey"), TEXT("Blackboard selector Key is required by this node policy"));
	}
	if (!Blackboard)
	{
		return Failure(TEXT("/Body/BlackboardAsset"), TEXT("MissingBehaviorTreeBlackboard"), TEXT("BehaviorTree selectors require an effective BlackboardAsset"));
	}

	const FBlackboard::FKey KeyId = Blackboard->GetKeyID(Selector.SelectedKeyName);
	const FBlackboardEntry* Entry = Blackboard->GetKey(KeyId);
	if (KeyId == FBlackboard::InvalidKey || !Entry || !Entry->KeyType)
	{
		Selector.InvalidateResolvedKey();
		Selector.SelectedKeyType = nullptr;
		return Failure(
			KeyPath,
			TEXT("UnknownBlackboardKey"),
			FString::Printf(TEXT("Blackboard key '%s' does not exist in the effective BlackboardAsset"), *Selector.SelectedKeyName.ToString()));
	}

	bool bAllowed = Selector.AllowedTypes.IsEmpty();
	for (UBlackboardKeyType* Filter : Selector.AllowedTypes)
	{
		if (Filter && Entry->KeyType->IsAllowedByFilter(Filter))
		{
			bAllowed = true;
			break;
		}
	}
	if (!bAllowed)
	{
		Selector.InvalidateResolvedKey();
		Selector.SelectedKeyType = nullptr;
		return Failure(
			KeyPath,
			TEXT("IncompatibleBlackboardKeyType"),
			FString::Printf(TEXT("Blackboard key '%s' is incompatible with the selector filter policy"), *Selector.SelectedKeyName.ToString()));
	}

	Selector.ResolveSelectedKey(*Blackboard);
	return FAssetDocumentCapabilityResult::Success();
}

FString ExportContainerKey(FProperty* Property, const void* ValuePtr)
{
	FString Value;
	if (Property && ValuePtr)
	{
		Property->ExportTextItem_Direct(Value, ValuePtr, nullptr, nullptr, PPF_None);
	}
	return Value;
}

FAssetDocumentCapabilityResult ValidateSelectorsInValue(
	FProperty* Property,
	void* ValuePtr,
	UBlackboardData* Blackboard,
	const FString& Path,
	bool bTreatPropertiesAsAuthored,
	const TSharedPtr<FJsonValue>& AuthoredValue,
	bool* bOutDerivedCacheChanged = nullptr);

void ResetValueOrBlackboardKeyDerivedCache(UScriptStruct* Struct, void* StructValue)
{
	FStructOnScope AuthoredSnapshot(Struct);
	void* SnapshotValue = AuthoredSnapshot.GetStructMemory();
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		FProperty* Property = *It;
		Property->CopyCompleteValue(
			Property->ContainerPtrToValuePtr<void>(SnapshotValue),
			Property->ContainerPtrToValuePtr<void>(StructValue));
	}

	// KeyId is protected, mutable, and deliberately not a UPROPERTY. Rebuilding
	// the live struct resets that cache while the reflected authored surface is
	// restored exactly from the initialized snapshot above.
	Struct->DestroyStruct(StructValue);
	Struct->InitializeStruct(StructValue);
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		FProperty* Property = *It;
		Property->CopyCompleteValue(
			Property->ContainerPtrToValuePtr<void>(StructValue),
			Property->ContainerPtrToValuePtr<void>(SnapshotValue));
	}
}

FAssetDocumentCapabilityResult ValidateSelectorsInStruct(
	UScriptStruct* Struct,
	void* StructValue,
	UBlackboardData* Blackboard,
	const FString& Path,
	bool bTreatPropertiesAsAuthored,
	const TSharedPtr<FJsonObject>& AuthoredObject,
	bool* bOutDerivedCacheChanged = nullptr)
{
	if (!Struct || !StructValue)
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		FProperty* Property = *It;
		const TSharedPtr<FJsonValue> FieldValue = AuthoredObject.IsValid()
			? AuthoredObject->TryGetField(Property->GetName())
			: nullptr;
		bool bPropertyCacheChanged = false;
		FAssetDocumentCapabilityResult Result = ValidateSelectorsInValue(
			Property,
			Property->ContainerPtrToValuePtr<void>(StructValue),
			Blackboard,
			JoinPath(Path, Property->GetName()),
			bTreatPropertiesAsAuthored,
			FieldValue,
			&bPropertyCacheChanged);
		if (bPropertyCacheChanged && bOutDerivedCacheChanged)
		{
			*bOutDerivedCacheChanged = true;
		}
		if (!Result.bSuccess)
		{
			return Result;
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateSelectorsInValue(
	FProperty* Property,
	void* ValuePtr,
	UBlackboardData* Blackboard,
	const FString& Path,
	bool bTreatPropertiesAsAuthored,
	const TSharedPtr<FJsonValue>& AuthoredValue,
	bool* bOutDerivedCacheChanged)
{
	if (!Property || !ValuePtr)
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (StructProperty->Struct == FBlackboardKeySelector::StaticStruct())
		{
			if (bOutDerivedCacheChanged)
			{
				*bOutDerivedCacheChanged = true;
			}
			return ValidateSelector(
				*static_cast<FBlackboardKeySelector*>(ValuePtr),
				Blackboard,
				Path,
				bTreatPropertiesAsAuthored);
		}
		if (StructProperty->Struct->IsChildOf(FValueOrBlackboardKeyBase::StaticStruct()))
		{
			ResetValueOrBlackboardKeyDerivedCache(StructProperty->Struct, ValuePtr);
			if (bOutDerivedCacheChanged)
			{
				*bOutDerivedCacheChanged = true;
			}
			FValueOrBlackboardKeyBase* ValueOrBlackboardKey =
				static_cast<FValueOrBlackboardKeyBase*>(ValuePtr);
			const FName KeyName = ValueOrBlackboardKey->GetKey();
			if (!KeyName.IsNone())
			{
				if (!Blackboard)
				{
					return Failure(
						TEXT("/Body/BlackboardAsset"),
						TEXT("MissingBehaviorTreeBlackboard"),
						TEXT("A bound ValueOrBlackboardKey requires an effective BlackboardAsset"));
				}
				const FBlackboard::FKey KeyId = Blackboard->GetKeyID(KeyName);
				const FBlackboardEntry* Entry = Blackboard->GetKey(KeyId);
				if (!Entry || !Entry->KeyType)
				{
					return Failure(
						JoinPath(Path, TEXT("Key")),
						TEXT("UnknownValueOrBlackboardKey"),
						FString::Printf(TEXT("ValueOrBlackboardKey '%s' does not exist in the effective BlackboardAsset"), *KeyName.ToString()));
				}
#if WITH_EDITOR
				if (!ValueOrBlackboardKey->IsCompatibleType(Entry->KeyType))
				{
					return Failure(
						JoinPath(Path, TEXT("Key")),
						TEXT("IncompatibleValueOrBlackboardKeyType"),
						FString::Printf(TEXT("Blackboard key '%s' is incompatible with this ValueOrBlackboardKey type"), *KeyName.ToString()));
				}
#endif
			}
			return ValidateSelectorsInStruct(
				StructProperty->Struct,
				ValuePtr,
				Blackboard,
				Path,
				bTreatPropertiesAsAuthored,
				AuthoredValue.IsValid() ? AuthoredValue->AsObject() : nullptr);
		}
		if (StructProperty->Struct == FInstancedStruct::StaticStruct())
		{
			FInstancedStruct* Instanced = static_cast<FInstancedStruct*>(ValuePtr);
			const TSharedPtr<FJsonObject> InstancedObject = AuthoredValue.IsValid() ? AuthoredValue->AsObject() : nullptr;
			const TSharedPtr<FJsonObject>* AuthoredProperties = nullptr;
			if (InstancedObject.IsValid())
			{
				InstancedObject->TryGetObjectField(TEXT("Properties"), AuthoredProperties);
			}
			bool bInstancedCacheChanged = false;
			FAssetDocumentCapabilityResult Result = Instanced->IsValid()
				? ValidateSelectorsInStruct(
					const_cast<UScriptStruct*>(Instanced->GetScriptStruct()),
					Instanced->GetMutableMemory(),
					Blackboard,
					JoinPath(Path, TEXT("Properties")),
					bTreatPropertiesAsAuthored,
					AuthoredProperties ? *AuthoredProperties : nullptr,
					&bInstancedCacheChanged)
				: FAssetDocumentCapabilityResult::Success();
			if (bInstancedCacheChanged && bOutDerivedCacheChanged)
			{
				*bOutDerivedCacheChanged = true;
			}
			return Result;
		}
		return ValidateSelectorsInStruct(
			StructProperty->Struct,
			ValuePtr,
			Blackboard,
			Path,
			bTreatPropertiesAsAuthored,
			AuthoredValue.IsValid() ? AuthoredValue->AsObject() : nullptr,
			bOutDerivedCacheChanged);
	}
	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
		const TArray<TSharedPtr<FJsonValue>>* AuthoredArray = nullptr;
		if (AuthoredValue.IsValid())
		{
			AuthoredValue->TryGetArray(AuthoredArray);
		}
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			bool bElementCacheChanged = false;
			FAssetDocumentCapabilityResult Result = ValidateSelectorsInValue(
				ArrayProperty->Inner,
				Helper.GetRawPtr(Index),
				Blackboard,
				JoinPath(Path, Index),
				bTreatPropertiesAsAuthored,
				AuthoredArray && AuthoredArray->IsValidIndex(Index) ? (*AuthoredArray)[Index] : nullptr,
				&bElementCacheChanged);
			if (bElementCacheChanged && bOutDerivedCacheChanged)
			{
				*bOutDerivedCacheChanged = true;
			}
			if (!Result.bSuccess)
			{
				return Result;
			}
		}
	}
	else if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		FScriptMapHelper Helper(MapProperty, ValuePtr);
		const TSharedPtr<FJsonObject> AuthoredMap = AuthoredValue.IsValid() ? AuthoredValue->AsObject() : nullptr;
		bool bMapKeyCacheChanged = false;
		for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
		{
			if (!Helper.IsValidIndex(Index))
			{
				continue;
			}
			const FString Key = ExportContainerKey(MapProperty->KeyProp, Helper.GetKeyPtr(Index));
			bool bKeyCacheChanged = false;
			FAssetDocumentCapabilityResult Result = ValidateSelectorsInValue(
				MapProperty->KeyProp,
				Helper.GetKeyPtr(Index),
				Blackboard,
				JoinPath(JoinPath(Path, Key), TEXT("Key")),
				false,
				nullptr,
				&bKeyCacheChanged);
			bMapKeyCacheChanged |= bKeyCacheChanged;
			if (!Result.bSuccess)
			{
				if (bMapKeyCacheChanged)
				{
					Helper.Rehash();
				}
				return Result;
			}
			bool bValueCacheChanged = false;
			Result = ValidateSelectorsInValue(
				MapProperty->ValueProp,
				Helper.GetValuePtr(Index),
				Blackboard,
				JoinPath(Path, Key),
				bTreatPropertiesAsAuthored,
				AuthoredMap.IsValid() ? AuthoredMap->TryGetField(Key) : nullptr,
				&bValueCacheChanged);
			if ((bKeyCacheChanged || bValueCacheChanged) && bOutDerivedCacheChanged)
			{
				*bOutDerivedCacheChanged = true;
			}
			if (!Result.bSuccess)
			{
				if (bMapKeyCacheChanged)
				{
					Helper.Rehash();
				}
				return Result;
			}
		}
		if (bMapKeyCacheChanged)
		{
			Helper.Rehash();
		}
	}
	else if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		FScriptSetHelper Helper(SetProperty, ValuePtr);
		const TArray<TSharedPtr<FJsonValue>>* AuthoredSet = nullptr;
		if (AuthoredValue.IsValid())
		{
			AuthoredValue->TryGetArray(AuthoredSet);
		}
		int32 LogicalIndex = 0;
		bool bSetElementCacheChanged = false;
		for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
		{
			if (!Helper.IsValidIndex(Index))
			{
				continue;
			}
			bool bElementCacheChanged = false;
			FAssetDocumentCapabilityResult Result = ValidateSelectorsInValue(
				SetProperty->ElementProp,
				Helper.GetElementPtr(Index),
				Blackboard,
				JoinPath(Path, LogicalIndex),
				bTreatPropertiesAsAuthored,
				AuthoredSet && AuthoredSet->IsValidIndex(LogicalIndex) ? (*AuthoredSet)[LogicalIndex] : nullptr,
				&bElementCacheChanged);
			bSetElementCacheChanged |= bElementCacheChanged;
			if (bElementCacheChanged && bOutDerivedCacheChanged)
			{
				*bOutDerivedCacheChanged = true;
			}
			++LogicalIndex;
			if (!Result.bSuccess)
			{
				if (bSetElementCacheChanged)
				{
					Helper.Rehash();
				}
				return Result;
			}
		}
		if (bSetElementCacheChanged)
		{
			Helper.Rehash();
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateSubtreeReference(
	UObject* Instance,
	UBlackboardData* Blackboard,
	const FString& PropertiesPath)
{
	UBehaviorTree* Subtree = nullptr;
	FString PropertyName;
	bool bDynamic = false;
	if (const UBTTask_RunBehavior* StaticTask = Cast<UBTTask_RunBehavior>(Instance))
	{
		Subtree = StaticTask->GetSubtreeAsset();
		PropertyName = TEXT("BehaviorAsset");
	}
	else if (const UBTTask_RunBehaviorDynamic* DynamicTask = Cast<UBTTask_RunBehaviorDynamic>(Instance))
	{
		Subtree = DynamicTask->GetDefaultBehaviorAsset();
		PropertyName = TEXT("DefaultBehaviorAsset");
		bDynamic = true;
	}
	else
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const FString Path = JoinPath(PropertiesPath, PropertyName);
	if (!Subtree)
	{
		return bDynamic
			? FAssetDocumentCapabilityResult::Success()
			: Failure(Path, TEXT("IncompatibleBehaviorTreeBlackboard"), TEXT("Static subtree tasks require a BehaviorAsset with a BlackboardAsset"));
	}
	if (!Blackboard)
	{
		return Failure(TEXT("/Body/BlackboardAsset"), TEXT("MissingBehaviorTreeBlackboard"), TEXT("BehaviorTree subtree references require an effective BlackboardAsset"));
	}
	if (!Subtree->BlackboardAsset
		|| (Blackboard != Subtree->BlackboardAsset && !Blackboard->IsChildOf(*Subtree->BlackboardAsset)))
	{
		return Failure(
			Path,
			TEXT("IncompatibleBehaviorTreeBlackboard"),
			TEXT("The effective BlackboardAsset must equal or derive from the referenced subtree BlackboardAsset"));
	}
	if (bDynamic && Subtree->RootDecorators.Num() > 0)
	{
		return Failure(
			Path,
			TEXT("DynamicSubtreeRootDecoratorsUnsupported"),
			TEXT("Dynamic subtree defaults cannot reference a tree with root decorators"));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateBlackboardDecoratorEnumValue(
	UObject* Instance,
	UBlackboardData* Blackboard,
	const FString& PropertiesPath)
{
	UBTDecorator_Blackboard* Decorator = Cast<UBTDecorator_Blackboard>(Instance);
	if (!Decorator || !Blackboard)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FStructProperty* SelectorProperty = FindFProperty<FStructProperty>(
		Decorator->GetClass(),
		TEXT("BlackboardKey"));
	FBlackboardKeySelector* Selector = SelectorProperty
		&& SelectorProperty->Struct == FBlackboardKeySelector::StaticStruct()
		? SelectorProperty->ContainerPtrToValuePtr<FBlackboardKeySelector>(Decorator)
		: nullptr;
	const FBlackboardEntry* Entry = Selector && !Selector->SelectedKeyName.IsNone()
		? Blackboard->GetKey(Blackboard->GetKeyID(Selector->SelectedKeyName))
		: nullptr;
	if (!Entry || !Entry->KeyType)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const UEnum* Enum = nullptr;
	if (const UBlackboardKeyType_Enum* EnumKey = Cast<UBlackboardKeyType_Enum>(Entry->KeyType))
	{
		Enum = EnumKey->EnumType;
	}
	else if (const UBlackboardKeyType_NativeEnum* NativeEnumKey = Cast<UBlackboardKeyType_NativeEnum>(Entry->KeyType))
	{
		Enum = NativeEnumKey->EnumType;
	}
	if (!Enum)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FStrProperty* StringValueProperty = FindFProperty<FStrProperty>(Decorator->GetClass(), TEXT("StringValue"));
	FIntProperty* IntValueProperty = FindFProperty<FIntProperty>(Decorator->GetClass(), TEXT("IntValue"));
	if (!StringValueProperty || !IntValueProperty)
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	const FString StringValue = StringValueProperty->GetPropertyValue_InContainer(Decorator);
	if (StringValue.IsEmpty())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const int64 MappedValue = Enum->GetValueByName(FName(*StringValue));
	if (MappedValue == INDEX_NONE)
	{
		return Failure(
			JoinPath(PropertiesPath, TEXT("StringValue")),
			TEXT("UnknownBehaviorTreeEnumDecoratorValue"),
			FString::Printf(
				TEXT("Enum decorator StringValue '%s' does not exist in the effective Blackboard enum; author Body.Tree together with this Blackboard change"),
				*StringValue));
	}
	const int32 IntValue = IntValueProperty->GetPropertyValue_InContainer(Decorator);
	if (MappedValue != IntValue)
	{
		return Failure(
			JoinPath(PropertiesPath, TEXT("IntValue")),
			TEXT("BehaviorTreeEnumDecoratorValueOutOfSync"),
			FString::Printf(
				TEXT("Enum decorator StringValue '%s' maps to %lld in the effective Blackboard enum, but authored IntValue is %d; author Body.Tree together with this Blackboard change"),
				*StringValue,
				static_cast<long long>(MappedValue),
				IntValue));
	}
	return FAssetDocumentCapabilityResult::Success();
}

void RefreshBlackboardDecoratorDerivedOperation(
	UObject* Instance,
	UBlackboardData* Blackboard)
{
#if WITH_EDITORONLY_DATA
	UBTDecorator_Blackboard* Decorator = Cast<UBTDecorator_Blackboard>(Instance);
	if (!Decorator)
	{
		return;
	}

	FStructProperty* SelectorProperty = FindFProperty<FStructProperty>(
		Decorator->GetClass(),
		TEXT("BlackboardKey"));
	FBlackboardKeySelector* Selector = SelectorProperty
		&& SelectorProperty->Struct == FBlackboardKeySelector::StaticStruct()
		? SelectorProperty->ContainerPtrToValuePtr<FBlackboardKeySelector>(Decorator)
		: nullptr;
	const FBlackboardEntry* Entry = Blackboard && Selector && !Selector->SelectedKeyName.IsNone()
		? Blackboard->GetKey(Blackboard->GetKeyID(Selector->SelectedKeyName))
		: nullptr;

	FByteProperty* OperationTypeProperty = FindFProperty<FByteProperty>(
		Decorator->GetClass(),
		TEXT("OperationType"));
	FStrProperty* CachedDescriptionProperty = FindFProperty<FStrProperty>(
		Decorator->GetClass(),
		TEXT("CachedDescription"));
	if (!OperationTypeProperty || !CachedDescriptionProperty)
	{
		return;
	}
	if (!Entry || !Entry->KeyType || Entry->KeyType->GetClass() != Selector->SelectedKeyType)
	{
		CachedDescriptionProperty->SetPropertyValue_InContainer(Decorator, TEXT("invalid"));
		return;
	}

	const EBlackboardKeyOperation::Type Operation = Entry->KeyType->GetTestOperation();
	const TCHAR* AuthoredOperationPropertyName = nullptr;
	switch (Operation)
	{
	case EBlackboardKeyOperation::Basic:
		AuthoredOperationPropertyName = TEXT("BasicOperation");
		break;
	case EBlackboardKeyOperation::Arithmetic:
		AuthoredOperationPropertyName = TEXT("ArithmeticOperation");
		break;
	case EBlackboardKeyOperation::Text:
		AuthoredOperationPropertyName = TEXT("TextOperation");
		break;
	default:
		CachedDescriptionProperty->SetPropertyValue_InContainer(Decorator, TEXT("invalid"));
		return;
	}
	FByteProperty* AuthoredOperationProperty = FindFProperty<FByteProperty>(
		Decorator->GetClass(),
		AuthoredOperationPropertyName);
	if (!AuthoredOperationProperty)
	{
		return;
	}
	const uint8 OperationType = AuthoredOperationProperty->GetPropertyValue_InContainer(Decorator);
	OperationTypeProperty->SetPropertyValue_InContainer(Decorator, OperationType);

	FString Description = TEXT("invalid");
	const FString KeyName = Entry->EntryName.ToString();
	if (Operation == EBlackboardKeyOperation::Basic)
	{
		const UEnum* OperationEnum = StaticEnum<EBasicKeyOperation::Type>();
		if (OperationEnum)
		{
			Description = FString::Printf(
				TEXT("%s is %s"),
				*KeyName,
				*OperationEnum->GetDisplayNameTextByValue(OperationType).ToString());
		}
	}
	else if (Operation == EBlackboardKeyOperation::Arithmetic)
	{
		const UEnum* OperationEnum = StaticEnum<EArithmeticKeyOperation::Type>();
		FIntProperty* IntValueProperty = FindFProperty<FIntProperty>(Decorator->GetClass(), TEXT("IntValue"));
		FFloatProperty* FloatValueProperty = FindFProperty<FFloatProperty>(Decorator->GetClass(), TEXT("FloatValue"));
		if (OperationEnum && IntValueProperty && FloatValueProperty)
		{
			Description = FString::Printf(
				TEXT("%s %s %s"),
				*KeyName,
				*OperationEnum->GetDisplayNameTextByValue(OperationType).ToString(),
				*Entry->KeyType->DescribeArithmeticParam(
					IntValueProperty->GetPropertyValue_InContainer(Decorator),
					FloatValueProperty->GetPropertyValue_InContainer(Decorator)));
		}
	}
	else
	{
		const UEnum* OperationEnum = StaticEnum<ETextKeyOperation::Type>();
		FStrProperty* StringValueProperty = FindFProperty<FStrProperty>(Decorator->GetClass(), TEXT("StringValue"));
		if (OperationEnum && StringValueProperty)
		{
			Description = FString::Printf(
				TEXT("%s %s [%s]"),
				*KeyName,
				*OperationEnum->GetDisplayNameTextByValue(OperationType).ToString(),
				*StringValueProperty->GetPropertyValue_InContainer(Decorator));
		}
	}
	CachedDescriptionProperty->SetPropertyValue_InContainer(Decorator, Description);
#endif
}

FAssetDocumentCapabilityResult ValidateInstanceSemantics(
	UObject* Instance,
	UBlackboardData* Blackboard,
	const FString& PropertiesPath,
	bool bTreatPropertiesAsAuthored,
	const TSharedPtr<FJsonObject>& AuthoredProperties)
{
	for (TFieldIterator<FProperty> It(Instance ? Instance->GetClass() : nullptr); It; ++It)
	{
		FProperty* Property = *It;
		const TSharedPtr<FJsonValue> AuthoredValue = AuthoredProperties.IsValid()
			? AuthoredProperties->TryGetField(Property->GetName())
			: nullptr;
		FAssetDocumentCapabilityResult Result = ValidateSelectorsInValue(
			Property,
			Property->ContainerPtrToValuePtr<void>(Instance),
			Blackboard,
			JoinPath(PropertiesPath, Property->GetName()),
			bTreatPropertiesAsAuthored,
			AuthoredValue);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}
	FAssetDocumentCapabilityResult Result = ValidateBlackboardDecoratorEnumValue(
		Instance,
		Blackboard,
		PropertiesPath);
	if (!Result.bSuccess)
	{
		return Result;
	}
	RefreshBlackboardDecoratorDerivedOperation(Instance, Blackboard);
	return ValidateSubtreeReference(Instance, Blackboard, PropertiesPath);
}

FAssetDocumentCapabilityResult ResolveEffectiveBlackboard(
	const UObject* Asset,
	const TSharedRef<FJsonObject>& Body,
	UBlackboardData*& OutBlackboard)
{
	OutBlackboard = Cast<UBehaviorTree>(Asset)
		? Cast<UBehaviorTree>(Asset)->BlackboardAsset
		: nullptr;
	const TSharedPtr<FJsonValue> Desired = Body->TryGetField(TEXT("BlackboardAsset"));
	if (!Desired.IsValid())
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (Desired->Type == EJson::Null)
	{
		OutBlackboard = nullptr;
		return FAssetDocumentCapabilityResult::Success();
	}

	FString ObjectPath;
	if (!Desired->TryGetString(ObjectPath))
	{
		const TSharedPtr<FJsonObject> Ref = Desired->AsObject();
		if (!Ref.IsValid() || !Ref->TryGetStringField(TEXT("Path"), ObjectPath))
		{
			return Failure(TEXT("/Body/BlackboardAsset"), TEXT("InvalidBehaviorTreeBlackboardRef"), TEXT("BlackboardAsset must be null, an object path, or AssetRef"));
		}
	}
	ObjectPath.TrimStartAndEndInline();
	if (ObjectPath.StartsWith(TEXT("/")) && !ObjectPath.Contains(TEXT(".")))
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(ObjectPath);
		ObjectPath = FString::Printf(TEXT("%s.%s"), *ObjectPath, *AssetName);
	}
	OutBlackboard = LoadObject<UBlackboardData>(nullptr, *ObjectPath);
	return OutBlackboard
		? FAssetDocumentCapabilityResult::Success()
		: Failure(TEXT("/Body/BlackboardAsset"), TEXT("UnresolvedBehaviorTreeBlackboard"), TEXT("BlackboardAsset did not resolve to UBlackboardData"));
}

FAssetDocumentCapabilityResult ValidateSpecProperties(
	const FBehaviorTreeGraphSpec& Spec,
	UObject* Outer,
	UBlackboardData* Blackboard,
	bool bValidateSemantics,
	bool bTreatPropertiesAsAuthored = true)
{
	TFunction<FAssetDocumentCapabilityResult(const FBehaviorTreeNodeSpec&)> ValidateNode;
	ValidateNode = [&ValidateNode, Outer, Blackboard, bValidateSemantics, bTreatPropertiesAsAuthored](const FBehaviorTreeNodeSpec& Node) -> FAssetDocumentCapabilityResult
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
		Result = FAssetDocumentReflectedPropertyUtils::ApplyProperties(
			Preview,
			Node.Properties.ToSharedRef(),
			JoinPath(Node.JsonPath, PropertiesField));
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (bValidateSemantics)
		{
			Result = ValidateInstanceSemantics(
				Preview,
				Blackboard,
				JoinPath(Node.JsonPath, PropertiesField),
				bTreatPropertiesAsAuthored,
				bTreatPropertiesAsAuthored ? Node.Properties : nullptr);
			if (!Result.bSuccess)
			{
				return Result;
			}
		}
		for (const FBehaviorTreeDecoratorSpec& Decorator : Node.Decorators)
		{
			if (!Decorator.bComposite)
			{
				UBTDecorator* PreviewDecorator = NewObject<UBTDecorator>(Outer, Decorator.NodeClass, NAME_None, RF_Transient);
				if (!PreviewDecorator)
				{
					return Failure(Decorator.JsonPath, TEXT("InvalidBehaviorTreeNodeClass"), TEXT("Failed to instantiate BehaviorTree decorator"));
				}
				Result = FAssetDocumentReflectedPropertyUtils::ValidateProperties(
					PreviewDecorator,
					Decorator.Properties.ToSharedRef(),
					JoinPath(Decorator.JsonPath, PropertiesField));
				if (!Result.bSuccess)
				{
					return Result;
				}
				Result = FAssetDocumentReflectedPropertyUtils::ApplyProperties(
					PreviewDecorator,
					Decorator.Properties.ToSharedRef(),
					JoinPath(Decorator.JsonPath, PropertiesField));
				if (!Result.bSuccess)
				{
					return Result;
				}
				if (bValidateSemantics)
				{
					Result = ValidateInstanceSemantics(
						PreviewDecorator,
						Blackboard,
						JoinPath(Decorator.JsonPath, PropertiesField),
						bTreatPropertiesAsAuthored,
						bTreatPropertiesAsAuthored ? Decorator.Properties : nullptr);
					if (!Result.bSuccess)
					{
						return Result;
					}
				}
			}
			else
			{
				for (const FBehaviorTreeDecoratorGraphNodeSpec& BoundNode : Decorator.BoundGraph.Nodes)
				{
					if (BoundNode.Kind != TEXT("Test"))
					{
						continue;
					}
					UBTDecorator* PreviewDecorator = NewObject<UBTDecorator>(Outer, BoundNode.NodeClass, NAME_None, RF_Transient);
					if (!PreviewDecorator)
					{
						return Failure(BoundNode.JsonPath, TEXT("InvalidBehaviorTreeNodeClass"), TEXT("Failed to instantiate BoundGraph decorator"));
					}
					Result = FAssetDocumentReflectedPropertyUtils::ValidateProperties(
						PreviewDecorator,
						BoundNode.Properties.ToSharedRef(),
						JoinPath(BoundNode.JsonPath, PropertiesField));
					if (!Result.bSuccess)
					{
						return Result;
					}
					Result = FAssetDocumentReflectedPropertyUtils::ApplyProperties(
						PreviewDecorator,
						BoundNode.Properties.ToSharedRef(),
						JoinPath(BoundNode.JsonPath, PropertiesField));
					if (!Result.bSuccess)
					{
						return Result;
					}
					if (bValidateSemantics)
					{
						Result = ValidateInstanceSemantics(
							PreviewDecorator,
							Blackboard,
							JoinPath(BoundNode.JsonPath, PropertiesField),
							bTreatPropertiesAsAuthored,
							bTreatPropertiesAsAuthored ? BoundNode.Properties : nullptr);
						if (!Result.bSuccess)
						{
							return Result;
						}
					}
				}
			}
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
			Result = FAssetDocumentReflectedPropertyUtils::ApplyProperties(
				PreviewService,
				Service.Properties.ToSharedRef(),
				JoinPath(Service.JsonPath, PropertiesField));
			if (!Result.bSuccess)
			{
				return Result;
			}
			if (bValidateSemantics)
			{
				Result = ValidateInstanceSemantics(
					PreviewService,
					Blackboard,
					JoinPath(Service.JsonPath, PropertiesField),
					bTreatPropertiesAsAuthored,
					bTreatPropertiesAsAuthored ? Service.Properties : nullptr);
				if (!Result.bSuccess)
				{
					return Result;
				}
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

TArray<UEdGraphPin*> FindPins(UEdGraphNode* Node, EEdGraphPinDirection Direction)
{
	TArray<UEdGraphPin*> Pins;
	if (Node)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction)
			{
				Pins.Add(Pin);
			}
		}
	}
	return Pins;
}

UEdGraphPin* FindNamedPin(UEdGraphNode* Node, EEdGraphPinDirection Direction, const TCHAR* Name)
{
	for (UEdGraphPin* Pin : FindPins(Node, Direction))
	{
		if (Pin->PinName == FName(Name))
		{
			return Pin;
		}
	}
	return nullptr;
}

void ApplyEditorFields(UEdGraphNode* Node, const FBehaviorTreeEditorSpec& Editor)
{
	Node->NodePosX = Editor.X;
	Node->NodePosY = Editor.Y;
	Node->NodeComment = Editor.NodeComment;
	Node->bCommentBubblePinned = Editor.bCommentBubblePinned;
	Node->bCommentBubbleVisible = Editor.bCommentBubbleVisible;
}

FAssetDocumentCapabilityResult ApplyInstanceProperties(
	UObject* Instance,
	const TSharedRef<FJsonObject>& Properties,
	const FString& Path)
{
	return Instance
		? FAssetDocumentReflectedPropertyUtils::ApplyProperties(Instance, Properties, JoinPath(Path, PropertiesField))
		: Failure(Path, TEXT("MissingBehaviorTreeNodeInstance"), TEXT("BehaviorTree graph wrapper failed to create its NodeInstance"));
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
	return ApplyInstanceProperties(NodeInstance, Properties, Path);
}

FAssetDocumentCapabilityResult BuildCompositeBoundGraph(
	UBehaviorTreeGraphNode_CompositeDecorator* Composite,
	const FBehaviorTreeDecoratorSpec& Spec)
{
	UBehaviorTreeDecoratorGraph* BoundGraph = Composite ? Cast<UBehaviorTreeDecoratorGraph>(Composite->BoundGraph) : nullptr;
	if (!BoundGraph)
	{
		return Failure(Spec.JsonPath, TEXT("MissingBehaviorTreeDecoratorGraph"), TEXT("Composite decorator failed to create a UBehaviorTreeDecoratorGraph"));
	}
	BoundGraph->Nodes.Reset();
	BoundGraph->GraphGuid = Spec.BoundGraph.GraphGuid;
	const UEdGraphSchema* Schema = BoundGraph->GetSchema();
	if (!Schema)
	{
		return Failure(JoinPath(Spec.JsonPath, BoundGraphField), TEXT("MissingBehaviorTreeDecoratorGraphSchema"), TEXT("BoundGraph has no decorator schema"));
	}
	TMap<FGuid, UBehaviorTreeDecoratorGraphNode*> Nodes;
	for (const FBehaviorTreeDecoratorGraphNodeSpec& NodeSpec : Spec.BoundGraph.Nodes)
	{
		UBehaviorTreeDecoratorGraphNode* Created = nullptr;
		if (NodeSpec.Kind == TEXT("Test"))
		{
			FGraphNodeCreator<UBehaviorTreeDecoratorGraphNode_Decorator> NodeCreator(*BoundGraph);
			UBehaviorTreeDecoratorGraphNode_Decorator* Template = NodeCreator.CreateNode(false);
			Template->ClassData = FGraphNodeClassData(NodeSpec.NodeClass, FGraphNodeClassHelper::GetDeprecationMessage(NodeSpec.NodeClass));
			NodeCreator.Finalize();
			Created = Template;
			UBehaviorTreeDecoratorGraphNode_Decorator* TestNode = Cast<UBehaviorTreeDecoratorGraphNode_Decorator>(Created);
			FAssetDocumentCapabilityResult Result = ApplyInstanceProperties(TestNode ? TestNode->NodeInstance : nullptr, NodeSpec.Properties.ToSharedRef(), NodeSpec.JsonPath);
			if (!Result.bSuccess)
			{
				return Result;
			}
		}
		else
		{
			FGraphNodeCreator<UBehaviorTreeDecoratorGraphNode_Logic> NodeCreator(*BoundGraph);
			UBehaviorTreeDecoratorGraphNode_Logic* Template = NodeCreator.CreateNode(false);
			Template->LogicMode = NodeSpec.Kind == TEXT("Sink") ? EDecoratorLogicMode::Sink
				: NodeSpec.Kind == TEXT("And") ? EDecoratorLogicMode::And
				: NodeSpec.Kind == TEXT("Or") ? EDecoratorLogicMode::Or
				: EDecoratorLogicMode::Not;
			NodeCreator.Finalize();
			if (NodeSpec.Kind == TEXT("And") || NodeSpec.Kind == TEXT("Or"))
			{
				int32 AuthoredInputCount = 0;
				for (const FBehaviorTreeDecoratorGraphLinkSpec& Link : Spec.BoundGraph.Links)
				{
					AuthoredInputCount += Link.To == NodeSpec.Guid ? 1 : 0;
				}
				while (FindPins(Template, EGPD_Input).Num() < AuthoredInputCount)
				{
					// AddInputPin is not exported by BehaviorTreeEditor, so reproduce its
					// exact UE 5.7 pin shape through the exported UEdGraphNode API.
					Template->CreatePin(EGPD_Input, TEXT("Transition"), TEXT("In"));
				}
			}
			Created = Template;
		}
		if (!Created)
		{
			return Failure(NodeSpec.JsonPath, TEXT("BehaviorTreeDecoratorNodeCreationFailed"), TEXT("Failed to create BoundGraph node"));
		}
		Created->NodeGuid = NodeSpec.Guid;
		ApplyEditorFields(Created, NodeSpec.Editor);
		Nodes.Add(NodeSpec.Guid, Created);
	}
	for (const FBehaviorTreeDecoratorGraphLinkSpec& Link : Spec.BoundGraph.Links)
	{
		UBehaviorTreeDecoratorGraphNode* const* FromNode = Nodes.Find(Link.From);
		UBehaviorTreeDecoratorGraphNode* const* ToNode = Nodes.Find(Link.To);
		TArray<UEdGraphPin*> Inputs = ToNode ? FindPins(*ToNode, EGPD_Input) : TArray<UEdGraphPin*>();
		UEdGraphPin* Output = FromNode ? FindPin(*FromNode, EGPD_Output) : nullptr;
		if (!Output || !Inputs.IsValidIndex(Link.ToInput) || !Schema->TryCreateConnection(Output, Inputs[Link.ToInput]))
		{
			return Failure(Link.JsonPath, TEXT("InvalidBehaviorTreeDecoratorGraphConnection"), TEXT("UE decorator schema rejected the authored BoundGraph link"));
		}
	}
	BoundGraph->NotifyGraphChanged();
	Composite->OnInnerGraphChanged();
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult CreateDecoratorSubNode(
	UBehaviorTreeGraph* Graph,
	UBehaviorTreeGraphNode* Parent,
	const FBehaviorTreeDecoratorSpec& Spec)
{
	UBehaviorTreeGraphNode* Decorator = nullptr;
	if (Spec.bComposite)
	{
		Decorator = NewObject<UBehaviorTreeGraphNode_CompositeDecorator>(Graph);
	}
	else
	{
		Decorator = NewObject<UBehaviorTreeGraphNode_Decorator>(Graph);
		Decorator->ClassData = FGraphNodeClassData(Spec.NodeClass, FGraphNodeClassHelper::GetDeprecationMessage(Spec.NodeClass));
	}
	FAISchemaAction_NewSubNode Action;
	Action.ParentNode = Parent;
	Action.NodeTemplate = Decorator;
	Action.PerformAction(Graph, nullptr, FVector2f(Spec.Editor.X, Spec.Editor.Y), false);
	Decorator->NodeGuid = Spec.Guid;
	ApplyEditorFields(Decorator, Spec.Editor);
	if (!Spec.bComposite)
	{
		return ApplyNodeProperties(Decorator, Spec.Properties.ToSharedRef(), Spec.JsonPath);
	}
	UBehaviorTreeGraphNode_CompositeDecorator* Composite = CastChecked<UBehaviorTreeGraphNode_CompositeDecorator>(Decorator);
	Composite->CompositeName = Spec.CompositeName;
	Composite->bShowOperations = Spec.bShowOperations;
	return BuildCompositeBoundGraph(Composite, Spec);
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
	ApplyEditorFields(OutNode, Spec.Editor);
	FAssetDocumentCapabilityResult Result = ApplyNodeProperties(OutNode, Spec.Properties.ToSharedRef(), Spec.JsonPath);
	if (!Result.bSuccess)
	{
		return Result;
	}

	for (const FBehaviorTreeDecoratorSpec& DecoratorSpec : Spec.Decorators)
	{
		Result = CreateDecoratorSubNode(Graph, OutNode, DecoratorSpec);
		if (!Result.bSuccess)
		{
			return Result;
		}
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
		ApplyEditorFields(ServiceNode, ServiceSpec.Editor);
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
	for (int32 ChildIndex = 0; ChildIndex < Spec.Children.Num(); ++ChildIndex)
	{
		const TSharedPtr<FBehaviorTreeNodeSpec>& ChildSpec = Spec.Children[ChildIndex];
		UBehaviorTreeGraphNode* ChildNode = nullptr;
		Result = BuildNodeSubtree(Graph, Schema, *ChildSpec, ChildNode);
		if (!Result.bSuccess)
		{
			return Result;
		}
		UEdGraphPin* ParentOutput = nullptr;
		if (OutNode->IsA<UBehaviorTreeGraphNode_SimpleParallel>())
		{
			ParentOutput = ChildIndex == 0
				? FindNamedPin(OutNode, EGPD_Output, TEXT("Task"))
				: ChildIndex == 1 ? FindNamedPin(OutNode, EGPD_Output, TEXT("Out")) : nullptr;
		}
		else
		{
			ParentOutput = FindPin(OutNode, EGPD_Output);
		}
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
	const auto AddLinkedChildren = [&Children](UEdGraphPin* Output)
	{
		if (!Output)
		{
			return;
		}
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
	};
	if (Node && Node->IsA<UBehaviorTreeGraphNode_SimpleParallel>())
	{
		AddLinkedChildren(FindNamedPin(Node, EGPD_Output, TEXT("Task")));
		AddLinkedChildren(FindNamedPin(Node, EGPD_Output, TEXT("Out")));
		return Children;
	}
	for (UEdGraphPin* Output : FindPins(Node, EGPD_Output))
	{
		AddLinkedChildren(Output);
	}
	Children.Sort([](const UBehaviorTreeGraphNode& A, const UBehaviorTreeGraphNode& B)
	{
		return A.NodePosX == B.NodePosX ? A.NodePosY < B.NodePosY : A.NodePosX < B.NodePosX;
	});
	return Children;
}

void CollectGraphDecoratorData(
	const UBehaviorTreeGraphNode* Node,
	TArray<UBTDecorator*>& OutInstances,
	TArray<FBTDecoratorLogic>& OutOperations)
{
	int32 WrapperCount = 0;
	bool bHasCompositeWrapper = false;
	for (UBehaviorTreeGraphNode* Decorator : Node->Decorators)
	{
		if (const UBehaviorTreeGraphNode_Decorator* Ordinary = Cast<UBehaviorTreeGraphNode_Decorator>(Decorator))
		{
			Ordinary->CollectDecoratorData(OutInstances, OutOperations);
			++WrapperCount;
		}
		else if (const UBehaviorTreeGraphNode_CompositeDecorator* Composite = Cast<UBehaviorTreeGraphNode_CompositeDecorator>(Decorator))
		{
			Composite->CollectDecoratorData(OutInstances, OutOperations);
			++WrapperCount;
			bHasCompositeWrapper = true;
		}
	}
	// UE intentionally omits a logic program for ordinary-only wrappers. When at
	// least one composite wrapper exists, multiple wrappers are implicitly ANDed.
	if (!bHasCompositeWrapper)
	{
		OutOperations.Reset();
	}
	else if (WrapperCount > 1)
	{
		OutOperations.Insert(FBTDecoratorLogic(EBTDecoratorLogic::And, IntCastChecked<uint16>(WrapperCount)), 0);
	}
}

FAssetDocumentCapabilityResult AssertDecoratorMirror(
	const UBehaviorTreeGraphNode* GraphNode,
	const TArray<TObjectPtr<UBTDecorator>>& RuntimeDecorators,
	const TArray<FBTDecoratorLogic>& RuntimeOperations,
	const FString& Path)
{
	TArray<UBTDecorator*> GraphDecorators;
	TArray<FBTDecoratorLogic> GraphOperations;
	CollectGraphDecoratorData(GraphNode, GraphDecorators, GraphOperations);
	if (GraphDecorators.Num() != RuntimeDecorators.Num())
	{
		return Failure(Path, TEXT("BehaviorTreeRuntimeMirrorMismatch"), TEXT("Runtime decorator instances do not mirror graph decorator subnodes"));
	}
	for (int32 Index = 0; Index < GraphDecorators.Num(); ++Index)
	{
		if (GraphDecorators[Index] != RuntimeDecorators[Index])
		{
			return Failure(Path, TEXT("BehaviorTreeRuntimeMirrorMismatch"), TEXT("Runtime decorator order does not mirror graph decorator subnodes"));
		}
	}
	if (GraphOperations.Num() != RuntimeOperations.Num())
	{
		return Failure(Path, TEXT("BehaviorTreeRuntimeMirrorMismatch"), TEXT("Runtime decorator operation count does not mirror graph decorator logic"));
	}
	for (int32 Index = 0; Index < GraphOperations.Num(); ++Index)
	{
		if (GraphOperations[Index].Operation != RuntimeOperations[Index].Operation
			|| GraphOperations[Index].Number != RuntimeOperations[Index].Number)
		{
			return Failure(Path, TEXT("BehaviorTreeRuntimeMirrorMismatch"), TEXT("Runtime decorator operation/type/number does not mirror graph decorator logic"));
		}
	}
	return FAssetDocumentCapabilityResult::Success();
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
			FAssetDocumentCapabilityResult Result = AssertDecoratorMirror(
				Children[Index],
				Composite->Children[Index].Decorators,
				Composite->Children[Index].DecoratorOps,
				JoinPath(Path, Index));
			if (!Result.bSuccess)
			{
				return Result;
			}
			const UBTNode* RuntimeChild = Composite->Children[Index].ChildComposite
				? static_cast<const UBTNode*>(Composite->Children[Index].ChildComposite.Get())
				: static_cast<const UBTNode*>(Composite->Children[Index].ChildTask.Get());
			if (RuntimeChild != Children[Index]->NodeInstance)
			{
				return Failure(Path, TEXT("BehaviorTreeRuntimeMirrorMismatch"), TEXT("Runtime child order does not mirror graph X order"));
			}
			Result = AssertRuntimeMirrorNode(Children[Index], JoinPath(Path, Index));
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
	FAssetDocumentCapabilityResult DecoratorResult = AssertDecoratorMirror(
		GraphRoot,
		BehaviorTree->RootDecorators,
		BehaviorTree->RootDecoratorOps,
		JoinPath(Path, RootField));
	if (!DecoratorResult.bSuccess)
	{
		return DecoratorResult;
	}
	return AssertRuntimeMirrorNode(GraphRoot, JoinPath(Path, RootField));
}

void BuildComments(UBehaviorTreeGraph* Graph, const FBehaviorTreeGraphSpec& Spec)
{
	for (const FBehaviorTreeCommentSpec& CommentSpec : Spec.Comments)
	{
		UEdGraphNode_Comment* Comment = NewObject<UEdGraphNode_Comment>(Graph);
		Comment->NodeGuid = CommentSpec.Guid;
		Comment->NodeComment = CommentSpec.Text;
		Comment->NodePosX = CommentSpec.X;
		Comment->NodePosY = CommentSpec.Y;
		Comment->NodeWidth = CommentSpec.Width;
		Comment->NodeHeight = CommentSpec.Height;
		Comment->CommentColor = CommentSpec.Color;
		Comment->CommentDepth = CommentSpec.CommentDepth;
		Comment->FontSize = CommentSpec.FontSize;
		Comment->MoveMode = CommentSpec.MoveMode;
		Comment->NodeDetails = CommentSpec.NodeDetails;
		Comment->bCommentBubblePinned = CommentSpec.bCommentBubblePinned;
		Comment->bCommentBubbleVisible = CommentSpec.bCommentBubbleVisible;
		Comment->bCommentBubbleVisible_InDetailsPanel = CommentSpec.bCommentBubbleVisibleInDetails;
		Comment->bColorCommentBubble = CommentSpec.bColorCommentBubble;
		Graph->AddNode(Comment, false, false);
	}
}

void CollectGraphRuntimeNodeInstances(const UBehaviorTreeGraph* Graph, TArray<UObject*>& OutInstances);

FAssetDocumentCapabilityResult BuildGraph(
	UBehaviorTree* BehaviorTree,
	const FBehaviorTreeGraphSpec& Spec,
	const FString& Path,
	UBehaviorTreeGraph*& OutGraph,
	bool bValidateSemantics)
{
	OutGraph = nullptr;
#if WITH_EDITOR
	// CreateDefaultNodesForGraph calls the synthetic root's PostPlacedNewNode,
	// which guesses the first loaded Blackboard and writes it back to the tree.
	// Preserve the document's effective cross-region value across that editor UI
	// convenience path; staging must never depend on global load order.
	UBlackboardData* EffectiveBlackboard = BehaviorTree ? BehaviorTree->BlackboardAsset : nullptr;
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
	SyntheticRoot->BlackboardAsset = EffectiveBlackboard;
	BehaviorTree->BlackboardAsset = EffectiveBlackboard;

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
	BuildComments(OutGraph, Spec);
	OutGraph->UnlockUpdates();
	OutGraph->UpdateClassData();
	if (bValidateSemantics)
	{
		// Selector semantics have already been checked against the effective
		// Blackboard above. Only semantic materialization may enter UE's runtime
		// compilation lifecycle, because UpdateAsset calls InitializeFromAsset and
		// would otherwise emit warnings for a document that cross-region preflight
		// is about to reject.
		OutGraph->UpdateAsset(UBehaviorTreeGraph::ClearDebuggerFlags | UBehaviorTreeGraph::KeepRebuildCounter);
		TArray<UObject*> RuntimeInstances;
		CollectGraphRuntimeNodeInstances(OutGraph, RuntimeInstances);
		for (UObject* Instance : RuntimeInstances)
		{
			FAssetDocumentCapabilityResult SemanticResult = ValidateInstanceSemantics(
				Instance,
				BehaviorTree->BlackboardAsset,
				Path,
				false,
				nullptr);
			if (!SemanticResult.bSuccess)
			{
				return SemanticResult;
			}
		}
		FAssetDocumentCapabilityResult MirrorResult = AssertRuntimeMirror(BehaviorTree, OutGraph, Path);
		if (!MirrorResult.bSuccess)
		{
			return MirrorResult;
		}
	}
	if (bValidateSemantics)
	{
		OutGraph->NotifyGraphChanged();
	}
	return FAssetDocumentCapabilityResult::Success(bValidateSemantics
		? TEXT("Built BehaviorTree graph source and runtime mirror")
		: TEXT("Built BehaviorTree graph source"));
#else
	return Failure(Path, TEXT("EditorOnlyRegionUnavailable"), TEXT("BehaviorTree graph source requires WITH_EDITOR"));
#endif
}

bool RenameObjectToTransient(UObject* Object)
{
	if (!Object || Object->GetOuter() == GetTransientPackage())
	{
		return true;
	}
	const FName NewName = MakeUniqueObjectName(GetTransientPackage(), Object->GetClass(), Object->GetFName());
	return Object->Rename(*NewName.ToString(), GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
}

void CollectGraphRuntimeNodeInstances(const UBehaviorTreeGraph* Graph, TArray<UObject*>& OutInstances)
{
	if (!Graph)
	{
		return;
	}
	for (const UEdGraphNode* NodeObject : Graph->Nodes)
	{
		if (const UBehaviorTreeGraphNode* Node = Cast<UBehaviorTreeGraphNode>(NodeObject))
		{
			if (Node->NodeInstance)
			{
				OutInstances.Add(Node->NodeInstance);
			}
			for (const TObjectPtr<UAIGraphNode>& SubNode : Node->SubNodes)
			{
				if (SubNode && SubNode->NodeInstance)
				{
					OutInstances.Add(SubNode->NodeInstance);
				}
				if (const UBehaviorTreeGraphNode_CompositeDecorator* Composite = Cast<UBehaviorTreeGraphNode_CompositeDecorator>(SubNode))
				{
					if (const UBehaviorTreeDecoratorGraph* BoundGraph = Cast<UBehaviorTreeDecoratorGraph>(Composite->BoundGraph))
					{
						for (const UEdGraphNode* BoundNode : BoundGraph->Nodes)
						{
							if (const UBehaviorTreeDecoratorGraphNode_Decorator* Test = Cast<UBehaviorTreeDecoratorGraphNode_Decorator>(BoundNode))
							{
								if (Test->NodeInstance)
								{
									OutInstances.Add(Test->NodeInstance);
								}
							}
						}
					}
				}
			}
		}
	}
}

bool RestoreGraphRuntimeNodeOwnership(UBehaviorTreeGraph* Graph, UBehaviorTree* BehaviorTree)
{
	if (!Graph)
	{
		return true;
	}
	if (!BehaviorTree)
	{
		return false;
	}
	TArray<UObject*> Instances;
	CollectGraphRuntimeNodeInstances(Graph, Instances);
	bool bSuccess = true;
	for (UObject* Instance : Instances)
	{
		if (!Instance || Instance->GetOuter() == BehaviorTree)
		{
			continue;
		}
		const FName RestoredName = MakeUniqueObjectName(BehaviorTree, Instance->GetClass(), Instance->GetFName());
		bSuccess = Instance->Rename(
			*RestoredName.ToString(),
			BehaviorTree,
			REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty) && bSuccess;
	}
	for (const UObject* Instance : Instances)
	{
		bSuccess = Instance && Instance->GetOuter() == BehaviorTree && bSuccess;
	}
	return bSuccess;
}

bool MoveGraphToTransient(UBehaviorTreeGraph* Graph);

bool MoveGraphRuntimeNodeInstancesToTransient(UBehaviorTreeGraph* Graph)
{
	TArray<UObject*> Instances;
	CollectGraphRuntimeNodeInstances(Graph, Instances);
	bool bSuccess = true;
	for (UObject* Instance : Instances)
	{
		bSuccess = RenameObjectToTransient(Instance) && bSuccess;
	}
	return bSuccess;
}

bool CleanupRejectedGraphAndRestorePreviousOwnership(
	UBehaviorTreeGraph* RejectedGraph,
	UBehaviorTreeGraph* PreviousGraph,
	UBehaviorTree* BehaviorTree,
	bool& bOutCleanupSucceeded)
{
	// UpdateAsset can orphan every instance referenced by the previous graph before
	// the replacement graph is installed. Clean the rejected graph first so the
	// previous instances can reclaim BehaviorTree ownership without name conflicts.
	bOutCleanupSucceeded = MoveGraphToTransient(RejectedGraph);
	return RestoreGraphRuntimeNodeOwnership(PreviousGraph, BehaviorTree);
}

bool MoveGraphToTransient(UBehaviorTreeGraph* Graph)
{
	if (!Graph)
	{
		return true;
	}
	const bool bInstancesMoved = MoveGraphRuntimeNodeInstancesToTransient(Graph);
	return RenameObjectToTransient(Graph) && bInstancesMoved;
}

bool WrapperMatchesInstance(const UBehaviorTreeGraphNode* Node)
{
	if (!Node)
	{
		return false;
	}
	if (const UBehaviorTreeGraphNode_CompositeDecorator* Composite = Cast<UBehaviorTreeGraphNode_CompositeDecorator>(Node))
	{
		return Composite->BoundGraph && Composite->BoundGraph->IsA<UBehaviorTreeDecoratorGraph>();
	}
	if (!Node->NodeInstance)
	{
		return false;
	}
	if (Node->IsA<UBehaviorTreeGraphNode_Decorator>())
	{
		return Node->NodeInstance->IsA<UBTDecorator>();
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

FAssetDocumentCapabilityResult ValidateGraphDecorator(
	UBehaviorTreeGraphNode* Decorator,
	const FString& Path,
	TSet<FGuid>& SeenGuids)
{
	if (!Decorator || !WrapperMatchesInstance(Decorator)
		|| (!Decorator->IsA<UBehaviorTreeGraphNode_Decorator>() && !Decorator->IsA<UBehaviorTreeGraphNode_CompositeDecorator>()))
	{
		return Failure(Path, TEXT("InvalidBehaviorTreeGraphDecorator"), TEXT("BehaviorTree decorator subnode wrapper/class is invalid"));
	}
	if (!Decorator->NodeGuid.IsValid())
	{
		return Failure(JoinPath(Path, IdField), TEXT("InvalidBehaviorTreeNodeGuid"), TEXT("BehaviorTree decorator has no persistent NodeGuid"));
	}
	if (SeenGuids.Contains(Decorator->NodeGuid))
	{
		return Failure(JoinPath(Path, IdField), TEXT("DuplicateBehaviorTreeNodeGuid"), TEXT("BehaviorTree graph contains duplicate NodeGuid identities"));
	}
	SeenGuids.Add(Decorator->NodeGuid);
	if (UBehaviorTreeGraphNode_CompositeDecorator* Composite = Cast<UBehaviorTreeGraphNode_CompositeDecorator>(Decorator))
	{
		UBehaviorTreeDecoratorGraph* BoundGraph = Cast<UBehaviorTreeDecoratorGraph>(Composite->BoundGraph);
		if (!BoundGraph || !BoundGraph->GraphGuid.IsValid())
		{
			return Failure(JoinPath(JoinPath(Path, BoundGraphField), GraphGuidField), TEXT("InvalidBehaviorTreeGraphGuid"), TEXT("Composite decorator BoundGraph has no persistent GraphGuid"));
		}
		for (int32 Index = 0; Index < BoundGraph->Nodes.Num(); ++Index)
		{
			UEdGraphNode* BoundNode = BoundGraph->Nodes[Index];
			if (!BoundNode || !BoundNode->NodeGuid.IsValid())
			{
				return Failure(JoinPath(JoinPath(JoinPath(Path, BoundGraphField), NodesField), Index), TEXT("InvalidBehaviorTreeNodeGuid"), TEXT("BoundGraph node has no persistent NodeGuid"));
			}
			if (SeenGuids.Contains(BoundNode->NodeGuid))
			{
				return Failure(JoinPath(JoinPath(JoinPath(JoinPath(Path, BoundGraphField), NodesField), Index), IdField), TEXT("DuplicateBehaviorTreeNodeGuid"), TEXT("BehaviorTree graph contains duplicate NodeGuid identities"));
			}
			SeenGuids.Add(BoundNode->NodeGuid);
			if (UBehaviorTreeDecoratorGraphNode_Decorator* TestNode = Cast<UBehaviorTreeDecoratorGraphNode_Decorator>(BoundNode);
				TestNode && (!TestNode->NodeInstance || !TestNode->NodeInstance->IsA<UBTDecorator>()))
			{
				return Failure(JoinPath(JoinPath(JoinPath(Path, BoundGraphField), NodesField), Index), TEXT("InvalidBehaviorTreeGraphDecorator"), TEXT("BoundGraph Test node has no decorator instance"));
			}
		}
	}
	return FAssetDocumentCapabilityResult::Success();
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
	for (int32 Index = 0; Index < Node->Decorators.Num(); ++Index)
	{
		FAssetDocumentCapabilityResult Result = ValidateGraphDecorator(Node->Decorators[Index], JoinPath(JoinPath(Path, DecoratorsField), Index), SeenGuids);
		if (!Result.bSuccess)
		{
			return Result;
		}
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
	TArray<UEdGraphNode_Comment*> Comments;
	for (UEdGraphNode* NodeObject : Graph->Nodes)
	{
		if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(NodeObject))
		{
			Comments.Add(Comment);
			continue;
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
	for (int32 Index = 0; Index < Comments.Num(); ++Index)
	{
		if (!Comments[Index]->NodeGuid.IsValid())
		{
			return Failure(JoinPath(JoinPath(JoinPath(Path, CommentsField), Index), IdField), TEXT("InvalidBehaviorTreeNodeGuid"), TEXT("BehaviorTree comment has no persistent NodeGuid"));
		}
		if (SeenGuids.Contains(Comments[Index]->NodeGuid))
		{
			return Failure(JoinPath(JoinPath(JoinPath(Path, CommentsField), Index), IdField), TEXT("DuplicateBehaviorTreeNodeGuid"), TEXT("BehaviorTree graph contains duplicate NodeGuid identities"));
		}
		SeenGuids.Add(Comments[Index]->NodeGuid);
	}
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
		const TArray<UEdGraphPin*> Outputs = FindPins(Node, EGPD_Output);
		if (Outputs.Num() == 0 && Node->NodeInstance->IsA<UBTCompositeNode>())
		{
			return Failure(NodePath, TEXT("InvalidBehaviorTreeGraphConnection"), TEXT("BehaviorTree graph node has no output pin"));
		}
		for (UEdGraphPin* Output : Outputs)
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
			if (!Node->IsA<UBehaviorTreeGraphNode_SimpleParallel>()
				&& Index > 0
				&& Children[Index - 1]->NodePosX == Children[Index]->NodePosX)
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
			UEdGraphPin* ActualOutput = Input && Input->LinkedTo.Num() == 1 ? Input->LinkedTo[0] : nullptr;
			if (!ActualOutput || !Outputs.Contains(ActualOutput) || Schema->CanCreateConnection(ActualOutput, Input).Response == CONNECT_RESPONSE_DISALLOW)
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

TSharedPtr<FJsonObject> MakeEditorJson(const UEdGraphNode* Node)
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

FString BoundNodeKind(const UEdGraphNode* Node)
{
	if (Cast<UBehaviorTreeDecoratorGraphNode_Decorator>(Node))
	{
		return TEXT("Test");
	}
	if (const UBehaviorTreeDecoratorGraphNode_Logic* Logic = Cast<UBehaviorTreeDecoratorGraphNode_Logic>(Node))
	{
		switch (Logic->LogicMode)
		{
		case EDecoratorLogicMode::Sink: return TEXT("Sink");
		case EDecoratorLogicMode::And: return TEXT("And");
		case EDecoratorLogicMode::Or: return TEXT("Or");
		case EDecoratorLogicMode::Not: return TEXT("Not");
		default: break;
		}
	}
	return FString();
}

FAssetDocumentCapabilityResult ExtractBoundGraph(
	UBehaviorTreeGraphNode_CompositeDecorator* Composite,
	const FString& Path,
	TSharedPtr<FJsonObject>& OutGraph)
{
	UBehaviorTreeDecoratorGraph* Graph = Composite ? Cast<UBehaviorTreeDecoratorGraph>(Composite->BoundGraph) : nullptr;
	if (!Graph || !Graph->GraphGuid.IsValid())
	{
		return Failure(JoinPath(Path, GraphGuidField), TEXT("InvalidBehaviorTreeGraphGuid"), TEXT("Composite decorator BoundGraph has no persistent GraphGuid"));
	}
	OutGraph = MakeShared<FJsonObject>();
	OutGraph->SetStringField(GraphGuidField, Graph->GraphGuid.ToString(EGuidFormats::Digits));
	TArray<TSharedPtr<FJsonValue>> Nodes;
	TMap<const UEdGraphNode*, FString> NodeIds;
	for (int32 Index = 0; Index < Graph->Nodes.Num(); ++Index)
	{
		UEdGraphNode* Node = Graph->Nodes[Index];
		const FString Kind = BoundNodeKind(Node);
		if (!Node || Kind.IsEmpty())
		{
			return Failure(JoinPath(JoinPath(Path, NodesField), Index), TEXT("InvalidBehaviorTreeDecoratorNode"), TEXT("BoundGraph contains an unsupported node"));
		}
		TSharedPtr<FJsonObject> NodeJson = MakeShared<FJsonObject>();
		const FString Id = Node->NodeGuid.ToString(EGuidFormats::Digits);
		NodeIds.Add(Node, Id);
		NodeJson->SetStringField(IdField, Id);
		NodeJson->SetStringField(KindField, Kind);
		if (UBehaviorTreeDecoratorGraphNode_Decorator* TestNode = Cast<UBehaviorTreeDecoratorGraphNode_Decorator>(Node))
		{
			NodeJson->SetStringField(ClassField, TestNode->NodeInstance->GetClass()->GetPathName());
			TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
			FAssetDocumentCapabilityResult Result = FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(
				TestNode->NodeInstance,
				Properties,
				JoinPath(JoinPath(JoinPath(Path, NodesField), Index), PropertiesField));
			if (!Result.bSuccess)
			{
				return Result;
			}
			NodeJson->SetObjectField(PropertiesField, Properties);
		}
		NodeJson->SetObjectField(EditorField, MakeEditorJson(Node));
		Nodes.Add(MakeShared<FJsonValueObject>(NodeJson));
	}
	Nodes.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
	{
		return Left->AsObject()->GetStringField(IdField) < Right->AsObject()->GetStringField(IdField);
	});
	OutGraph->SetArrayField(NodesField, Nodes);

	TArray<TSharedPtr<FJsonValue>> Links;
	for (UEdGraphNode* FromNode : Graph->Nodes)
	{
		for (UEdGraphPin* Output : FindPins(FromNode, EGPD_Output))
		{
			for (UEdGraphPin* Linked : Output->LinkedTo)
			{
				UEdGraphNode* ToNode = Linked ? Linked->GetOwningNode() : nullptr;
				const FString* FromId = NodeIds.Find(FromNode);
				const FString* ToId = NodeIds.Find(ToNode);
				if (!FromId || !ToId)
				{
					return Failure(Path, TEXT("InvalidBehaviorTreeDecoratorGraphConnection"), TEXT("BoundGraph contains a dangling connection"));
				}
				const int32 ToInput = FindPins(ToNode, EGPD_Input).IndexOfByKey(Linked);
				if (ToInput == INDEX_NONE)
				{
					return Failure(Path, TEXT("InvalidBehaviorTreeDecoratorGraphConnection"), TEXT("BoundGraph link does not target an input pin"));
				}
				TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
				Link->SetStringField(FromField, *FromId);
				Link->SetStringField(ToField, *ToId);
				Link->SetNumberField(ToInputField, ToInput);
				Links.Add(MakeShared<FJsonValueObject>(Link));
			}
		}
	}
	Links.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
	{
		const TSharedPtr<FJsonObject> LeftObject = Left->AsObject();
		const TSharedPtr<FJsonObject> RightObject = Right->AsObject();
		const FString LeftFrom = LeftObject->GetStringField(FromField);
		const FString RightFrom = RightObject->GetStringField(FromField);
		if (LeftFrom != RightFrom)
		{
			return LeftFrom < RightFrom;
		}
		const FString LeftTo = LeftObject->GetStringField(ToField);
		const FString RightTo = RightObject->GetStringField(ToField);
		return LeftTo == RightTo
			? LeftObject->GetNumberField(ToInputField) < RightObject->GetNumberField(ToInputField)
			: LeftTo < RightTo;
	});
	OutGraph->SetArrayField(LinksField, Links);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractDecorator(
	UBehaviorTreeGraphNode* Decorator,
	const FString& Path,
	TSharedPtr<FJsonObject>& OutDecorator)
{
	OutDecorator = MakeShared<FJsonObject>();
	OutDecorator->SetStringField(IdField, Decorator->NodeGuid.ToString(EGuidFormats::Digits));
	if (UBehaviorTreeGraphNode_CompositeDecorator* Composite = Cast<UBehaviorTreeGraphNode_CompositeDecorator>(Decorator))
	{
		OutDecorator->SetStringField(KindField, TEXT("Composite"));
		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
		Properties->SetStringField(CompositeNameField, Composite->CompositeName);
		Properties->SetBoolField(ShowOperationsField, Composite->bShowOperations);
		OutDecorator->SetObjectField(PropertiesField, Properties);
		TSharedPtr<FJsonObject> BoundGraph;
		FAssetDocumentCapabilityResult Result = ExtractBoundGraph(Composite, JoinPath(Path, BoundGraphField), BoundGraph);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutDecorator->SetObjectField(BoundGraphField, BoundGraph);
	}
	else
	{
		OutDecorator->SetStringField(ClassField, Decorator->NodeInstance->GetClass()->GetPathName());
		TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
		FAssetDocumentCapabilityResult Result = FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(
			Decorator->NodeInstance,
			Properties,
			JoinPath(Path, PropertiesField));
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutDecorator->SetObjectField(PropertiesField, Properties);
	}
	OutDecorator->SetObjectField(EditorField, MakeEditorJson(Decorator));
	return FAssetDocumentCapabilityResult::Success();
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
	OutService->SetObjectField(EditorField, MakeEditorJson(Service));
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
	TArray<TSharedPtr<FJsonValue>> Decorators;
	for (int32 Index = 0; Index < Node->Decorators.Num(); ++Index)
	{
		TSharedPtr<FJsonObject> Decorator;
		Result = ExtractDecorator(Node->Decorators[Index], JoinPath(JoinPath(Path, DecoratorsField), Index), Decorator);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Decorators.Add(MakeShared<FJsonValueObject>(Decorator));
	}
	OutNode->SetArrayField(DecoratorsField, Decorators);

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

TSharedPtr<FJsonObject> ExtractComment(const UEdGraphNode_Comment* Comment)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(IdField, Comment->NodeGuid.ToString(EGuidFormats::Digits));
	Json->SetStringField(TextField, Comment->NodeComment);
	TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
	Position->SetNumberField(XField, Comment->NodePosX);
	Position->SetNumberField(YField, Comment->NodePosY);
	Json->SetObjectField(PositionField, Position);
	TSharedPtr<FJsonObject> Size = MakeShared<FJsonObject>();
	Size->SetNumberField(WidthField, Comment->NodeWidth);
	Size->SetNumberField(HeightField, Comment->NodeHeight);
	Json->SetObjectField(SizeField, Size);
	TSharedPtr<FJsonObject> Color = MakeShared<FJsonObject>();
	Color->SetNumberField(RField, Comment->CommentColor.R);
	Color->SetNumberField(GField, Comment->CommentColor.G);
	Color->SetNumberField(BField, Comment->CommentColor.B);
	Color->SetNumberField(AField, Comment->CommentColor.A);
	Json->SetObjectField(ColorField, Color);
	Json->SetNumberField(CommentDepthField, Comment->CommentDepth);
	Json->SetNumberField(FontSizeField, Comment->FontSize);
	Json->SetStringField(MoveModeField, Comment->MoveMode.GetValue() == ECommentBoxMode::NoGroupMovement ? TEXT("NoGroupMovement") : TEXT("GroupMovement"));
	Json->SetStringField(NodeDetailsField, Comment->NodeDetails.ToString());
	Json->SetBoolField(CommentBubblePinnedField, Comment->bCommentBubblePinned);
	Json->SetBoolField(CommentBubbleVisibleField, Comment->bCommentBubbleVisible);
	Json->SetBoolField(CommentBubbleVisibleInDetailsField, Comment->bCommentBubbleVisible_InDetailsPanel);
	Json->SetBoolField(ColorCommentBubbleField, Comment->bColorCommentBubble);
	return Json;
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
	TArray<TSharedPtr<FJsonValue>> Comments;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
		{
			Comments.Add(MakeShared<FJsonValueObject>(ExtractComment(Comment)));
		}
	}
	Comments.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
	{
		return Left->AsObject()->GetStringField(IdField) < Right->AsObject()->GetStringField(IdField);
	});
	OutTree->SetArrayField(CommentsField, Comments);
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

void FlattenAttachment(
	const TSharedPtr<FJsonObject>& Attachment,
	const FString& TreePath,
	const FString& Collection,
	TMap<FString, TSharedPtr<FJsonValue>>& OutValues)
{
	if (!Attachment.IsValid())
	{
		return;
	}
	const FString Id = Attachment->GetStringField(IdField);
	const FString Path = JoinPath(JoinPath(TreePath, Collection), Id);
	if (Attachment->HasField(ClassField))
	{
		OutValues.Add(JoinPath(Path, ClassField), Attachment->TryGetField(ClassField));
	}
	if (Attachment->HasField(KindField))
	{
		OutValues.Add(JoinPath(Path, KindField), Attachment->TryGetField(KindField));
	}
	if (const TSharedPtr<FJsonObject>* Properties = nullptr; Attachment->TryGetObjectField(PropertiesField, Properties) && Properties && Properties->IsValid())
	{
		FlattenObject(*Properties, JoinPath(Path, PropertiesField), OutValues);
	}
	if (const TSharedPtr<FJsonObject>* Editor = nullptr; Attachment->TryGetObjectField(EditorField, Editor) && Editor && Editor->IsValid())
	{
		FlattenObject(*Editor, JoinPath(Path, EditorField), OutValues);
	}
	const TSharedPtr<FJsonObject>* BoundGraph = nullptr;
	if (Attachment->TryGetObjectField(BoundGraphField, BoundGraph) && BoundGraph && BoundGraph->IsValid())
	{
		const FString BoundPath = JoinPath(Path, BoundGraphField);
		OutValues.Add(JoinPath(BoundPath, GraphGuidField), (*BoundGraph)->TryGetField(GraphGuidField));
		TArray<FString> BoundNodeIds;
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if ((*BoundGraph)->TryGetArrayField(NodesField, Nodes) && Nodes)
		{
			for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
			{
				TSharedPtr<FJsonObject> Node = NodeValue->AsObject();
				if (!Node)
				{
					continue;
				}
				const FString NodeId = Node->GetStringField(IdField);
				BoundNodeIds.Add(NodeId);
				const FString NodePath = JoinPath(JoinPath(BoundPath, NodesField), NodeId);
				OutValues.Add(JoinPath(NodePath, KindField), Node->TryGetField(KindField));
				if (Node->HasField(ClassField))
				{
					OutValues.Add(JoinPath(NodePath, ClassField), Node->TryGetField(ClassField));
				}
				if (const TSharedPtr<FJsonObject>* NodeProperties = nullptr; Node->TryGetObjectField(PropertiesField, NodeProperties) && NodeProperties && NodeProperties->IsValid())
				{
					FlattenObject(*NodeProperties, JoinPath(NodePath, PropertiesField), OutValues);
				}
				if (const TSharedPtr<FJsonObject>* NodeEditor = nullptr; Node->TryGetObjectField(EditorField, NodeEditor) && NodeEditor && NodeEditor->IsValid())
				{
					FlattenObject(*NodeEditor, JoinPath(NodePath, EditorField), OutValues);
				}
			}
		}
		BoundNodeIds.Sort();
		OutValues.Add(JoinPath(BoundPath, NodesField), StringArrayValue(BoundNodeIds));
		const TArray<TSharedPtr<FJsonValue>>* BoundLinks = nullptr;
		if ((*BoundGraph)->TryGetArrayField(LinksField, BoundLinks) && BoundLinks)
		{
			TArray<TSharedPtr<FJsonValue>> SortedLinks = *BoundLinks;
			SortedLinks.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
			{
				const TSharedPtr<FJsonObject> LeftObject = Left->AsObject();
				const TSharedPtr<FJsonObject> RightObject = Right->AsObject();
				const FString LeftFrom = LeftObject->GetStringField(FromField);
				const FString RightFrom = RightObject->GetStringField(FromField);
				if (LeftFrom != RightFrom)
				{
					return LeftFrom < RightFrom;
				}
				const FString LeftTo = LeftObject->GetStringField(ToField);
				const FString RightTo = RightObject->GetStringField(ToField);
				return LeftTo == RightTo
					? LeftObject->GetNumberField(ToInputField) < RightObject->GetNumberField(ToInputField)
					: LeftTo < RightTo;
			});
			OutValues.Add(JoinPath(BoundPath, LinksField), MakeShared<FJsonValueArray>(SortedLinks));
		}
		else
		{
			OutValues.Add(JoinPath(BoundPath, LinksField), (*BoundGraph)->TryGetField(LinksField));
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

	TArray<FString> DecoratorIds;
	const TArray<TSharedPtr<FJsonValue>>* Decorators = nullptr;
	if (Node->TryGetArrayField(DecoratorsField, Decorators) && Decorators)
	{
		for (const TSharedPtr<FJsonValue>& DecoratorValue : *Decorators)
		{
			TSharedPtr<FJsonObject> Decorator = DecoratorValue->AsObject();
			DecoratorIds.Add(Decorator->GetStringField(IdField));
			FlattenAttachment(Decorator, TreePath, DecoratorsField, OutValues);
		}
	}
	OutValues.Add(JoinPath(NodePath, DecoratorsField), StringArrayValue(DecoratorIds));

	TArray<FString> ServiceIds;
	const TArray<TSharedPtr<FJsonValue>>* Services = nullptr;
	if (Node->TryGetArrayField(ServicesField, Services) && Services)
	{
		for (const TSharedPtr<FJsonValue>& ServiceValue : *Services)
		{
			TSharedPtr<FJsonObject> Service = ServiceValue->AsObject();
			ServiceIds.Add(Service->GetStringField(IdField));
			FlattenAttachment(Service, TreePath, ServicesField, OutValues);
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
	TArray<FString> CommentIds;
	const TArray<TSharedPtr<FJsonValue>>* Comments = nullptr;
	if (Tree->TryGetArrayField(CommentsField, Comments) && Comments)
	{
		for (const TSharedPtr<FJsonValue>& CommentValue : *Comments)
		{
			TSharedPtr<FJsonObject> Comment = CommentValue->AsObject();
			if (!Comment)
			{
				continue;
			}
			const FString Id = Comment->GetStringField(IdField);
			CommentIds.Add(Id);
			const FString CommentPath = JoinPath(JoinPath(Path, CommentsField), Id);
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Comment->Values)
			{
				if (Pair.Key == IdField)
				{
					continue;
				}
				if (Pair.Value.IsValid() && Pair.Value->Type == EJson::Object)
				{
					FlattenObject(Pair.Value->AsObject(), JoinPath(CommentPath, Pair.Key), OutValues);
				}
				else
				{
					OutValues.Add(JoinPath(CommentPath, Pair.Key), Pair.Value);
				}
			}
		}
	}
	CommentIds.Sort();
	OutValues.Add(JoinPath(Path, CommentsField), StringArrayValue(CommentIds));
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
	for (const FBehaviorTreeDecoratorSpec& Decorator : Node.Decorators)
	{
		OutIds.Add(Decorator.Id);
		if (Decorator.bComposite)
		{
			for (const FBehaviorTreeDecoratorGraphNodeSpec& BoundNode : Decorator.BoundGraph.Nodes)
			{
				OutIds.Add(BoundNode.Id);
			}
		}
	}
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
	// Region-local validation is structural. Selector semantics are checked by
	// ValidateBodyCrossRegion against the desired effective BlackboardAsset.
	// Leaving the structural preview unbound also prevents editor graph wrappers
	// from resolving an invalid authored selector (and logging) before that
	// explicit cross-region validator can return its deterministic diagnostic.
	Result = ValidateSpecProperties(Spec, GetTransientPackage(), nullptr, false);
	if (!Result.bSuccess)
	{
		return Result;
	}
	UBehaviorTreeGraph* StagingGraph = nullptr;
	return BuildGraph(Preview, Spec, RegionPath(Context), StagingGraph, false);
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentMaterializer::ValidateBodyCrossRegion(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& Body)
{
	const TSharedPtr<FJsonValue> TreeValue = Body->TryGetField(TEXT("Tree"));
	TSharedPtr<FJsonObject> EffectiveTree;
	if (TreeValue.IsValid())
	{
		if (TreeValue->Type != EJson::Object || !TreeValue->AsObject().IsValid())
		{
			return Failure(TEXT("/Body/Tree"), TEXT("InvalidBodySectionType"), TEXT("Body.Tree must be an object"));
		}
		EffectiveTree = TreeValue->AsObject();
	}
	else
	{
		const UBehaviorTree* Existing = Cast<UBehaviorTree>(Context.Asset);
		if (!Existing || !Existing->BTGraph)
		{
			return FAssetDocumentCapabilityResult::Success(TEXT("No retained BehaviorTree graph requires cross-region validation"));
		}
		TSharedRef<FJsonObject> ExtractedTree = MakeShared<FJsonObject>();
		FAssetDocumentCapabilityResult ExtractResult = ExtractGraphTree(Existing, TEXT("/Body/Tree"), ExtractedTree);
		if (!ExtractResult.bSuccess)
		{
			return ExtractResult;
		}
		EffectiveTree = ExtractedTree;
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
	UBlackboardData* EffectiveBlackboard = nullptr;
	FAssetDocumentCapabilityResult Result = ResolveEffectiveBlackboard(Context.Asset, Body, EffectiveBlackboard);
	if (!Result.bSuccess)
	{
		return Result;
	}
	FBehaviorTreeGraphSpec Spec;
	Result = ParseTreeSpec(TreeContext, EffectiveTree.ToSharedRef(), Spec);
	if (!Result.bSuccess)
	{
		return Result;
	}
	UBehaviorTree* Preview = NewObject<UBehaviorTree>(GetTransientPackage(), NAME_None, RF_Transient);
	Preview->BlackboardAsset = EffectiveBlackboard;
	Result = ValidateSpecProperties(Spec, Preview, EffectiveBlackboard, true, TreeValue.IsValid());
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (!TreeValue.IsValid())
	{
		// ValidateSpecProperties has already reconstructed every retained node,
		// decorator, service, selector, ValueOrBlackboardKey, enum contract, and
		// subtree reference against the desired Blackboard. Building a second
		// graph here would enter UpdateAsset/InitializeFromAsset on arbitrary
		// project node classes during Validate, Diff, and Apply preflight.
		return FAssetDocumentCapabilityResult::Success(
			TEXT("Validated retained BehaviorTree semantics against the desired BlackboardAsset"));
	}
	UBehaviorTreeGraph* StagingGraph = nullptr;
	return BuildGraph(Preview, Spec, RegionPath(TreeContext), StagingGraph, true);
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
	Result = ValidateSpecProperties(Spec, Preview, Preview->BlackboardAsset, true);
	if (!Result.bSuccess)
	{
		return Result;
	}
	UBehaviorTreeGraph* StagingGraph = nullptr;
	Result = BuildGraph(Preview, Spec, RegionPath(Context), StagingGraph, true);
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
	UPackage* Package = BehaviorTree->GetOutermost();
	const bool bWasPackageDirty = Package && Package->IsDirty();
	UBehaviorTreeGraph* NewGraph = nullptr;
	BehaviorTree->Modify();
	Result = BuildGraph(BehaviorTree, Spec, RegionPath(Context), NewGraph, true);
	if (!Result.bSuccess)
	{
		bOutChanged = false;
		BehaviorTree->BTGraph = PreviousGraph;
		BehaviorTree->RootNode = PreviousRoot;
		BehaviorTree->RootDecorators = PreviousRootDecorators;
		BehaviorTree->RootDecoratorOps = PreviousRootOps;
		BehaviorTree->BlackboardAsset = PreviousBlackboard;
		bool bCleanupSucceeded = false;
		const bool bOwnershipRestored = CleanupRejectedGraphAndRestorePreviousOwnership(
			NewGraph,
			PreviousGraph,
			BehaviorTree,
			bCleanupSucceeded);
		if (!bOwnershipRestored)
		{
			Result = Failure(RegionPath(Context), TEXT("BehaviorTreeGraphRollbackFailed"), TEXT("Failed to restore all previous BehaviorTree runtime node ownership after rejected graph build"));
		}
		else if (!bCleanupSucceeded)
		{
			Result = Failure(RegionPath(Context), TEXT("BehaviorTreeGraphCleanupFailed"), TEXT("Failed to move the rejected BehaviorTree staging graph to transient ownership"));
		}
		if (Package)
		{
			Package->SetDirtyFlag(bWasPackageDirty);
		}
		return Result;
	}
#if WITH_DEV_AUTOMATION_TESTS
	if (BehaviorTree->GetOutermost() != GetTransientPackage()
		&& !BehaviorTree->HasAnyFlags(RF_Transient)
		&& bFailNextTreeGraphSwapForTest)
	{
		bFailNextTreeGraphSwapForTest = false;
		bOutChanged = false;
		BehaviorTree->BTGraph = PreviousGraph;
		BehaviorTree->RootNode = PreviousRoot;
		BehaviorTree->RootDecorators = PreviousRootDecorators;
		BehaviorTree->RootDecoratorOps = PreviousRootOps;
		BehaviorTree->BlackboardAsset = PreviousBlackboard;
		bool bCleanupSucceeded = false;
		const bool bOwnershipRestored = CleanupRejectedGraphAndRestorePreviousOwnership(
			NewGraph,
			PreviousGraph,
			BehaviorTree,
			bCleanupSucceeded);
		if (Package)
		{
			Package->SetDirtyFlag(bWasPackageDirty);
		}
		if (!bOwnershipRestored)
		{
			return Failure(RegionPath(Context), TEXT("BehaviorTreeGraphRollbackFailed"), TEXT("Forced graph swap failure also failed to restore all previous BehaviorTree runtime node ownership"));
		}
		return bCleanupSucceeded
			? Failure(RegionPath(Context), TEXT("ForcedBehaviorTreeGraphSwapFailure"), TEXT("Forced BehaviorTree graph swap failure for automation coverage"))
			: Failure(RegionPath(Context), TEXT("BehaviorTreeGraphCleanupFailed"), TEXT("Forced graph swap failure also failed to clean the rejected graph"));
	}
#endif
	BehaviorTree->BlackboardAsset = PreviousBlackboard;
	const FName PreviousName = PreviousGraph ? PreviousGraph->GetFName() : NAME_None;
	if (PreviousGraph)
	{
		const FName BackupName = MakeUniqueObjectName(BehaviorTree, UBehaviorTreeGraph::StaticClass(), TEXT("Behavior Tree Previous"));
		if (!PreviousGraph->Rename(*BackupName.ToString(), BehaviorTree, REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty))
		{
			bOutChanged = false;
			BehaviorTree->BTGraph = PreviousGraph;
			BehaviorTree->RootNode = PreviousRoot;
			BehaviorTree->RootDecorators = PreviousRootDecorators;
			BehaviorTree->RootDecoratorOps = PreviousRootOps;
			bool bCleanupSucceeded = false;
			const bool bOwnershipRestored = CleanupRejectedGraphAndRestorePreviousOwnership(
				NewGraph,
				PreviousGraph,
				BehaviorTree,
				bCleanupSucceeded);
			if (Package)
			{
				Package->SetDirtyFlag(bWasPackageDirty);
			}
			if (!bOwnershipRestored)
			{
				return Failure(RegionPath(Context), TEXT("BehaviorTreeGraphRollbackFailed"), TEXT("Graph swap failure could not restore all previous BehaviorTree runtime node ownership"));
			}
			return bCleanupSucceeded
				? Failure(RegionPath(Context), TEXT("BehaviorTreeGraphSwapFailed"), TEXT("Failed to reserve the canonical BehaviorTree graph name"))
				: Failure(RegionPath(Context), TEXT("BehaviorTreeGraphCleanupFailed"), TEXT("Graph swap failed and rejected graph cleanup also failed"));
		}
	}
	if (!NewGraph
		|| !NewGraph->Rename(TEXT("Behavior Tree"), BehaviorTree, REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty))
	{
		bOutChanged = false;
		bool bPreviousNameRestored = true;
		if (PreviousGraph)
		{
			bPreviousNameRestored = PreviousGraph->Rename(*PreviousName.ToString(), BehaviorTree, REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
		}
		BehaviorTree->BTGraph = PreviousGraph;
		BehaviorTree->RootNode = PreviousRoot;
		BehaviorTree->RootDecorators = PreviousRootDecorators;
		BehaviorTree->RootDecoratorOps = PreviousRootOps;
		bool bCleanupSucceeded = false;
		const bool bOwnershipRestored = CleanupRejectedGraphAndRestorePreviousOwnership(
			NewGraph,
			PreviousGraph,
			BehaviorTree,
			bCleanupSucceeded);
		if (Package)
		{
			Package->SetDirtyFlag(bWasPackageDirty);
		}
		if (!bPreviousNameRestored || !bOwnershipRestored)
		{
			return Failure(RegionPath(Context), TEXT("BehaviorTreeGraphRollbackFailed"), TEXT("Graph swap failed and previous graph ownership restoration was incomplete"));
		}
		return bCleanupSucceeded
			? Failure(RegionPath(Context), TEXT("BehaviorTreeGraphSwapFailed"), TEXT("Failed to install the replacement BehaviorTree graph"))
			: Failure(RegionPath(Context), TEXT("BehaviorTreeGraphCleanupFailed"), TEXT("Graph swap failed and rejected graph cleanup was incomplete"));
	}
	BehaviorTree->BTGraph = NewGraph;
	bool bPreviousCleanupSucceeded = true;
#if WITH_DEV_AUTOMATION_TESTS
	if (BehaviorTree->GetOutermost() != GetTransientPackage()
		&& !BehaviorTree->HasAnyFlags(RF_Transient)
		&& bFailNextPreviousTreeGraphCleanupForTest
		&& PreviousGraph)
	{
		bFailNextPreviousTreeGraphCleanupForTest = false;
		// Exercise a real partial cleanup: old runtime instances move first, then
		// the graph Rename itself is reported as failed.
		MoveGraphRuntimeNodeInstancesToTransient(PreviousGraph);
		bPreviousCleanupSucceeded = false;
	}
	else
#endif
	{
		bPreviousCleanupSucceeded = MoveGraphToTransient(PreviousGraph);
	}
	if (!bPreviousCleanupSucceeded)
	{
		bOutChanged = false;
		BehaviorTree->BTGraph = PreviousGraph;
		BehaviorTree->RootNode = PreviousRoot;
		BehaviorTree->RootDecorators = PreviousRootDecorators;
		BehaviorTree->RootDecoratorOps = PreviousRootOps;
		BehaviorTree->BlackboardAsset = PreviousBlackboard;

		const bool bRejectedCleanupSucceeded = MoveGraphToTransient(NewGraph);
		bool bPreviousGraphRestored = true;
		if (PreviousGraph
			&& (PreviousGraph->GetOuter() != BehaviorTree || PreviousGraph->GetFName() != PreviousName))
		{
			bPreviousGraphRestored = PreviousGraph->Rename(
				*PreviousName.ToString(),
				BehaviorTree,
				REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
		}
		bPreviousGraphRestored = bPreviousGraphRestored
			&& (!PreviousGraph
				|| (PreviousGraph->GetOuter() == BehaviorTree && PreviousGraph->GetFName() == PreviousName));
		const bool bPreviousInstancesRestored = RestoreGraphRuntimeNodeOwnership(PreviousGraph, BehaviorTree);
		if (Package)
		{
			Package->SetDirtyFlag(bWasPackageDirty);
		}
		if (!bRejectedCleanupSucceeded)
		{
			return Failure(
				RegionPath(Context),
				TEXT("BehaviorTreeGraphRollbackCleanupFailed"),
				TEXT("Previous graph cleanup failed and rollback could not clean the installed replacement graph"));
		}
		if (!bPreviousGraphRestored || !bPreviousInstancesRestored)
		{
			return Failure(
				RegionPath(Context),
				TEXT("BehaviorTreeGraphRollbackFailed"),
				TEXT("Previous graph cleanup failed and rollback could not restore previous graph ownership"));
		}
		return Failure(
			RegionPath(Context),
			TEXT("BehaviorTreeGraphCleanupFailed"),
			TEXT("Failed to release previous BehaviorTree graph ownership; replacement was rolled back"));
	}
	NewGraph->MarkPackageDirty();
	BehaviorTree->MarkPackageDirty();
	return FAssetDocumentCapabilityResult::Success(TEXT("Applied BehaviorTree authored graph source"));
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentMaterializer::ExtractTree(
	const FAssetDocumentRegionContext& Context,
	TSharedRef<FJsonObject>& OutTree)
{
	return ExtractGraphTree(Cast<UBehaviorTree>(Context.Asset), RegionPath(Context), OutTree);
}

FAssetDocumentCapabilityResult FBehaviorTreeAssetDocumentMaterializer::RefreshDerivedSelectorCaches(
	UBehaviorTree& BehaviorTree)
{
#if WITH_EDITOR
	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(BehaviorTree.BTGraph);
	if (!Graph)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("No BehaviorTree editor graph requires selector cache refresh"));
	}

	TArray<UObject*> Instances;
	CollectGraphRuntimeNodeInstances(Graph, Instances);
	for (UObject* Instance : Instances)
	{
		FAssetDocumentCapabilityResult Result = ValidateInstanceSemantics(
			Instance,
			BehaviorTree.BlackboardAsset,
			TEXT("/Body/Tree/DerivedSelectorCaches"),
			false,
			nullptr);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Refreshed derived BehaviorTree selector and ValueOrBlackboardKey caches"));
#else
	return Failure(TEXT("/Body/Tree"), TEXT("EditorOnlyRegionUnavailable"), TEXT("BehaviorTree selector cache refresh requires WITH_EDITOR"));
#endif
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
	UBlackboardData* EffectiveBlackboard = nullptr;
	if (Context.DesiredBody.IsValid())
	{
		Result = ResolveEffectiveBlackboard(Context.Asset, Context.DesiredBody.ToSharedRef(), EffectiveBlackboard);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}
	else if (const UBehaviorTree* Existing = Cast<UBehaviorTree>(Context.Asset))
	{
		EffectiveBlackboard = Existing->BlackboardAsset;
	}
	Preview->BlackboardAsset = EffectiveBlackboard;
	Result = ValidateSpecProperties(Spec, Preview, EffectiveBlackboard, true);
	if (!Result.bSuccess)
	{
		return Result;
	}
	UBehaviorTreeGraph* StagingGraph = nullptr;
	Result = BuildGraph(Preview, Spec, RegionPath(Context), StagingGraph, true);
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
	for (const FBehaviorTreeCommentSpec& Comment : Spec.Comments)
	{
		OutIds.Add(Comment.Id);
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
	if (Context.Asset
		&& Context.Asset->GetOutermost() != GetTransientPackage()
		&& !Context.Asset->HasAnyFlags(RF_Transient)
		&& bFailNextEditorGraphRebuildForTest)
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
		for (UBehaviorTreeGraphNode* Decorator : Node->Decorators)
		{
			OutNodesById.Add(Decorator->NodeGuid.ToString(EGuidFormats::Digits), Decorator);
			if (UBehaviorTreeGraphNode_CompositeDecorator* Composite = Cast<UBehaviorTreeGraphNode_CompositeDecorator>(Decorator))
			{
				if (UBehaviorTreeDecoratorGraph* BoundGraph = Cast<UBehaviorTreeDecoratorGraph>(Composite->BoundGraph))
				{
					for (UEdGraphNode* BoundNode : BoundGraph->Nodes)
					{
						OutNodesById.Add(BoundNode->NodeGuid.ToString(EGuidFormats::Digits), BoundNode);
					}
				}
			}
		}
		for (UBehaviorTreeGraphNode* Service : Node->Services)
		{
			OutNodesById.Add(Service->NodeGuid.ToString(EGuidFormats::Digits), Service);
		}
	}
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
		{
			OutNodesById.Add(Comment->NodeGuid.ToString(EGuidFormats::Digits), Comment);
		}
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Collected BehaviorTree graph wrappers by NodeGuid"));
}

#if WITH_DEV_AUTOMATION_TESTS
void FBehaviorTreeAssetDocumentMaterializer::FailNextEditorGraphRebuildForTest()
{
	bFailNextEditorGraphRebuildForTest = true;
}

void FBehaviorTreeAssetDocumentMaterializer::FailNextTreeGraphSwapForTest()
{
	bFailNextTreeGraphSwapForTest = true;
}

void FBehaviorTreeAssetDocumentMaterializer::FailNextPreviousTreeGraphCleanupForTest()
{
	bFailNextPreviousTreeGraphCleanupForTest = true;
}
#endif
