// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentEditorLayoutRegionAdapter.h"

#include "AssetDocumentJsonRegionUtils.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphNode_Comment.h"

namespace
{
constexpr const TCHAR* NodesField = TEXT("Nodes");
constexpr const TCHAR* CommentsField = TEXT("Comments");
constexpr const TCHAR* NodeIdField = TEXT("NodeId");
constexpr const TCHAR* IdField = TEXT("Id");
constexpr const TCHAR* TextField = TEXT("Text");
constexpr const TCHAR* PositionField = TEXT("Position");
constexpr const TCHAR* SizeField = TEXT("Size");
constexpr const TCHAR* ColorField = TEXT("Color");
constexpr const TCHAR* XField = TEXT("X");
constexpr const TCHAR* YField = TEXT("Y");

struct FEditorLayoutNodeSpec
{
	FString NodeId;
	FString Path;
	int32 X = 0;
	int32 Y = 0;
};

struct FEditorLayoutCommentSpec
{
	FString Id;
	FString Path;
	FGuid Guid;
	FString Text;
	int32 X = 0;
	int32 Y = 0;
	int32 Width = 0;
	int32 Height = 0;
	bool bHasColor = false;
	FLinearColor Color = FLinearColor::White;
};

struct FEditorLayoutSpec
{
	bool bHasNodes = false;
	TArray<FEditorLayoutNodeSpec> Nodes;
	bool bHasComments = false;
	TArray<FEditorLayoutCommentSpec> Comments;
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

FString NodePath(const FString& LayoutPath, const FString& NodeId)
{
	return JoinPath(JoinPath(LayoutPath, NodesField), NodeId);
}

FString CommentPath(const FString& LayoutPath, const FString& Id)
{
	return JoinPath(JoinPath(LayoutPath, CommentsField), Id);
}

FAssetDocumentCapabilityResult Failure(const FString& Path, const FString& Code, const FString& Message)
{
	return FAssetDocumentJsonRegionUtils::Failure(Path, Code, Message);
}

bool IsIntegralFinite(double Value)
{
	return FMath::IsFinite(Value)
		&& Value >= static_cast<double>(TNumericLimits<int32>::Min())
		&& Value <= static_cast<double>(TNumericLimits<int32>::Max())
		&& FMath::TruncToDouble(Value) == Value;
}

FAssetDocumentCapabilityResult ReadIntegerField(
	const TSharedRef<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	const FString& Code,
	int32& OutValue)
{
	double Number = 0.0;
	if (!Object->TryGetNumberField(FieldName, Number) || !IsIntegralFinite(Number))
	{
		return Failure(Path, Code, FString::Printf(TEXT("%s must be an int32 number"), *FieldName));
	}
	OutValue = static_cast<int32>(Number);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadVector2Object(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	const FString& Code,
	int32& OutX,
	int32& OutY)
{
	FAssetDocumentCapabilityResult Result = ReadIntegerField(Object, XField, JoinPath(Path, XField), Code, OutX);
	if (!Result.bSuccess)
	{
		return Result;
	}
	return ReadIntegerField(Object, YField, JoinPath(Path, YField), Code, OutY);
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

FAssetDocumentCapabilityResult ReadColor(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	FLinearColor& OutColor)
{
	double R = 0.0;
	double G = 0.0;
	double B = 0.0;
	double A = 1.0;
	if (!Object->TryGetNumberField(TEXT("R"), R) || !Object->TryGetNumberField(TEXT("G"), G) || !Object->TryGetNumberField(TEXT("B"), B))
	{
		return Failure(Path, TEXT("InvalidEditorLayoutCommentColor"), TEXT("Comment Color requires numeric R, G, and B fields"));
	}
	if (Object->HasField(TEXT("A")) && !Object->TryGetNumberField(TEXT("A"), A))
	{
		return Failure(JoinPath(Path, TEXT("A")), TEXT("InvalidEditorLayoutCommentColor"), TEXT("Comment Color.A must be numeric"));
	}
	if (!FMath::IsFinite(R) || !FMath::IsFinite(G) || !FMath::IsFinite(B) || !FMath::IsFinite(A))
	{
		return Failure(Path, TEXT("InvalidEditorLayoutCommentColor"), TEXT("Comment Color fields must be finite"));
	}
	OutColor = FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), static_cast<float>(A));
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult RejectUnknownFields(
	const TSharedRef<FJsonObject>& Object,
	const FString& Path,
	const TSet<FString>& AllowedFields,
	const FString& Code)
{
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
	{
		if (!AllowedFields.Contains(Field.Key))
		{
			return Failure(
				JoinPath(Path, Field.Key),
				Code,
				FString::Printf(TEXT("Unsupported editor layout field %s"), *Field.Key));
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseLayoutNode(
	const TSharedPtr<FJsonValue>& Value,
	const FString& IndexPath,
	const FString& LayoutPath,
	FEditorLayoutNodeSpec& OutSpec)
{
	TSharedPtr<FJsonObject> Object;
	FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(Value, IndexPath, Object);
	if (!Result.bSuccess)
	{
		return Failure(IndexPath, TEXT("InvalidEditorLayoutNode"), TEXT("EditorLayout Nodes entries must be objects"));
	}

	FString NodeId;
	Result = FAssetDocumentJsonRegionUtils::RequireStringField(Object, NodeIdField, JoinPath(IndexPath, NodeIdField), NodeId, TEXT("MissingEditorLayoutNodeId"));
	if (!Result.bSuccess)
	{
		return Result;
	}
	NodeId.TrimStartAndEndInline();
	if (NodeId.IsEmpty())
	{
		return Failure(JoinPath(IndexPath, NodeIdField), TEXT("MissingEditorLayoutNodeId"), TEXT("EditorLayout NodeId must not be empty"));
	}

	OutSpec.NodeId = NodeId;
	OutSpec.Path = NodePath(LayoutPath, NodeId);

	Result = RejectUnknownFields(Object.ToSharedRef(), OutSpec.Path, {NodeIdField, PositionField}, TEXT("UnsupportedEditorLayoutField"));
	if (!Result.bSuccess)
	{
		return Result;
	}

	TSharedPtr<FJsonObject> Position;
	Result = RequireObjectField(Object.ToSharedRef(), PositionField, JoinPath(OutSpec.Path, PositionField), Position, TEXT("InvalidEditorLayoutPosition"));
	if (!Result.bSuccess)
	{
		return Result;
	}
	return ReadVector2Object(Position.ToSharedRef(), JoinPath(OutSpec.Path, PositionField), TEXT("InvalidEditorLayoutPosition"), OutSpec.X, OutSpec.Y);
}

FAssetDocumentCapabilityResult ParseLayoutComment(
	const TSharedPtr<FJsonValue>& Value,
	const FString& IndexPath,
	const FString& LayoutPath,
	FEditorLayoutCommentSpec& OutSpec)
{
	TSharedPtr<FJsonObject> Object;
	FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(Value, IndexPath, Object);
	if (!Result.bSuccess)
	{
		return Failure(IndexPath, TEXT("InvalidEditorLayoutComment"), TEXT("EditorLayout Comments entries must be objects"));
	}

	FString Id;
	Result = FAssetDocumentJsonRegionUtils::RequireStringField(Object, IdField, JoinPath(IndexPath, IdField), Id, TEXT("MissingEditorLayoutCommentId"));
	if (!Result.bSuccess)
	{
		return Result;
	}
	Id.TrimStartAndEndInline();
	if (Id.IsEmpty())
	{
		return Failure(JoinPath(IndexPath, IdField), TEXT("MissingEditorLayoutCommentId"), TEXT("EditorLayout Comment Id must not be empty"));
	}

	OutSpec.Id = Id;
	OutSpec.Path = CommentPath(LayoutPath, Id);
	if (!FGuid::Parse(Id, OutSpec.Guid))
	{
		return Failure(JoinPath(OutSpec.Path, IdField), TEXT("InvalidEditorLayoutCommentId"), TEXT("EditorLayout Comment Id must be a stable GUID string"));
	}

	Result = RejectUnknownFields(Object.ToSharedRef(), OutSpec.Path, {IdField, TextField, PositionField, SizeField, ColorField}, TEXT("UnsupportedEditorLayoutCommentField"));
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = FAssetDocumentJsonRegionUtils::RequireStringField(Object, TextField, JoinPath(OutSpec.Path, TextField), OutSpec.Text, TEXT("InvalidEditorLayoutComment"));
	if (!Result.bSuccess)
	{
		return Result;
	}

	TSharedPtr<FJsonObject> Position;
	Result = RequireObjectField(Object.ToSharedRef(), PositionField, JoinPath(OutSpec.Path, PositionField), Position, TEXT("InvalidEditorLayoutCommentPosition"));
	if (!Result.bSuccess)
	{
		return Result;
	}
	Result = ReadVector2Object(Position.ToSharedRef(), JoinPath(OutSpec.Path, PositionField), TEXT("InvalidEditorLayoutCommentPosition"), OutSpec.X, OutSpec.Y);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TSharedPtr<FJsonObject> Size;
	Result = RequireObjectField(Object.ToSharedRef(), SizeField, JoinPath(OutSpec.Path, SizeField), Size, TEXT("InvalidEditorLayoutCommentSize"));
	if (!Result.bSuccess)
	{
		return Result;
	}
	Result = ReadVector2Object(Size.ToSharedRef(), JoinPath(OutSpec.Path, SizeField), TEXT("InvalidEditorLayoutCommentSize"), OutSpec.Width, OutSpec.Height);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (OutSpec.Width <= 0 || OutSpec.Height <= 0)
	{
		return Failure(JoinPath(OutSpec.Path, SizeField), TEXT("InvalidEditorLayoutCommentSize"), TEXT("EditorLayout Comment Size must be positive"));
	}

	if (Object->HasField(ColorField))
	{
		TSharedPtr<FJsonObject> Color;
		Result = RequireObjectField(Object.ToSharedRef(), ColorField, JoinPath(OutSpec.Path, ColorField), Color, TEXT("InvalidEditorLayoutCommentColor"));
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = ReadColor(Color.ToSharedRef(), JoinPath(OutSpec.Path, ColorField), OutSpec.Color);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutSpec.bHasColor = true;
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseLayout(
	const TSharedPtr<FJsonValue>& DesiredValue,
	const FString& LayoutPath,
	FEditorLayoutSpec& OutSpec)
{
	OutSpec = FEditorLayoutSpec();

	TSharedPtr<FJsonObject> Object;
	FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(DesiredValue, LayoutPath, Object);
	if (!Result.bSuccess)
	{
		return Result;
	}

	Result = RejectUnknownFields(Object.ToSharedRef(), LayoutPath, {NodesField, CommentsField}, TEXT("UnsupportedEditorLayoutField"));
	if (!Result.bSuccess)
	{
		return Result;
	}

	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	OutSpec.bHasNodes = Object->HasField(NodesField);
	if (OutSpec.bHasNodes && Object->TryGetArrayField(NodesField, Nodes) && Nodes)
	{
		TSet<FString> SeenNodeIds;
		for (int32 Index = 0; Index < Nodes->Num(); ++Index)
		{
			FEditorLayoutNodeSpec NodeSpec;
			Result = ParseLayoutNode((*Nodes)[Index], JoinPath(JoinPath(LayoutPath, NodesField), Index), LayoutPath, NodeSpec);
			if (!Result.bSuccess)
			{
				return Result;
			}
			if (SeenNodeIds.Contains(NodeSpec.NodeId))
			{
				return Failure(NodeSpec.Path, TEXT("DuplicateEditorLayoutNode"), TEXT("Duplicate EditorLayout node id"));
			}
			SeenNodeIds.Add(NodeSpec.NodeId);
			OutSpec.Nodes.Add(MoveTemp(NodeSpec));
		}
	}
	else if (OutSpec.bHasNodes)
	{
		return Failure(JoinPath(LayoutPath, NodesField), TEXT("InvalidEditorLayoutNodes"), TEXT("EditorLayout Nodes must be an array"));
	}

	const TArray<TSharedPtr<FJsonValue>>* Comments = nullptr;
	OutSpec.bHasComments = Object->HasField(CommentsField);
	if (OutSpec.bHasComments && Object->TryGetArrayField(CommentsField, Comments) && Comments)
	{
		TSet<FString> SeenCommentIds;
		for (int32 Index = 0; Index < Comments->Num(); ++Index)
		{
			FEditorLayoutCommentSpec CommentSpec;
			Result = ParseLayoutComment((*Comments)[Index], JoinPath(JoinPath(LayoutPath, CommentsField), Index), LayoutPath, CommentSpec);
			if (!Result.bSuccess)
			{
				return Result;
			}
			if (SeenCommentIds.Contains(CommentSpec.Id))
			{
				return Failure(CommentSpec.Path, TEXT("DuplicateEditorLayoutComment"), TEXT("Duplicate EditorLayout comment id"));
			}
			SeenCommentIds.Add(CommentSpec.Id);
			OutSpec.Comments.Add(MoveTemp(CommentSpec));
		}
	}
	else if (OutSpec.bHasComments)
	{
		return Failure(JoinPath(LayoutPath, CommentsField), TEXT("InvalidEditorLayoutComments"), TEXT("EditorLayout Comments must be an array"));
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Parsed EditorLayout"));
}

const TArray<TSharedPtr<FJsonValue>>* GetLayoutArray(const TSharedPtr<FJsonValue>& LayoutValue, const FString& FieldName)
{
	const TSharedPtr<FJsonObject> Layout = LayoutValue.IsValid() && LayoutValue->Type == EJson::Object ? LayoutValue->AsObject() : nullptr;
	if (!Layout.IsValid())
	{
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	return Layout->TryGetArrayField(FieldName, Values) ? Values : nullptr;
}

TSharedPtr<FJsonObject> FindObjectByStringId(
	const TArray<TSharedPtr<FJsonValue>>* Values,
	const FString& FieldName,
	const FString& Id)
{
	if (!Values)
	{
		return nullptr;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
		FString CandidateId;
		if (Object.IsValid() && Object->TryGetStringField(FieldName, CandidateId) && CandidateId == Id)
		{
			return Object;
		}
	}
	return nullptr;
}

TSharedPtr<FJsonObject> MakePosition(int32 X, int32 Y)
{
	TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
	Position->SetNumberField(XField, X);
	Position->SetNumberField(YField, Y);
	return Position;
}

TSharedPtr<FJsonValue> NodeSpecValue(const FEditorLayoutNodeSpec& Spec)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(NodeIdField, Spec.NodeId);
	Object->SetObjectField(PositionField, MakePosition(Spec.X, Spec.Y));
	return MakeShared<FJsonValueObject>(Object);
}

TSharedPtr<FJsonValue> CommentSpecValue(const FEditorLayoutCommentSpec& Spec)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(IdField, Spec.Id);
	Object->SetStringField(TextField, Spec.Text);
	Object->SetObjectField(PositionField, MakePosition(Spec.X, Spec.Y));
	Object->SetObjectField(SizeField, MakePosition(Spec.Width, Spec.Height));
	if (Spec.bHasColor)
	{
		TSharedPtr<FJsonObject> Color = MakeShared<FJsonObject>();
		Color->SetNumberField(TEXT("R"), Spec.Color.R);
		Color->SetNumberField(TEXT("G"), Spec.Color.G);
		Color->SetNumberField(TEXT("B"), Spec.Color.B);
		Color->SetNumberField(TEXT("A"), Spec.Color.A);
		Object->SetObjectField(ColorField, Color);
	}
	return MakeShared<FJsonValueObject>(Object);
}

TSharedPtr<FJsonValue> ExtractLayoutValue(const FAssetDocumentEditorLayoutGraphState& GraphState)
{
	TSharedPtr<FJsonObject> Layout = MakeShared<FJsonObject>();

	TArray<FString> NodeIds;
	GraphState.NodesById.GetKeys(NodeIds);
	NodeIds.Sort([](const FString& Left, const FString& Right)
	{
		return Left < Right;
	});

	TArray<TSharedPtr<FJsonValue>> Nodes;
	for (const FString& NodeId : NodeIds)
	{
		const UEdGraphNode* GraphNode = GraphState.NodesById.FindRef(NodeId);
		if (!GraphNode)
		{
			continue;
		}
		FEditorLayoutNodeSpec Spec;
		Spec.NodeId = NodeId;
		Spec.X = GraphNode->NodePosX;
		Spec.Y = GraphNode->NodePosY;
		Nodes.Add(NodeSpecValue(Spec));
	}
	Layout->SetArrayField(NodesField, Nodes);

	TArray<TSharedPtr<FJsonValue>> Comments;
	if (GraphState.Graph)
	{
		TArray<UEdGraphNode_Comment*> CommentNodes;
		for (UEdGraphNode* GraphNode : GraphState.Graph->Nodes)
		{
			if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(GraphNode))
			{
				CommentNodes.Add(CommentNode);
			}
		}
		CommentNodes.Sort([](const UEdGraphNode_Comment& Left, const UEdGraphNode_Comment& Right)
		{
			return Left.NodeGuid.ToString(EGuidFormats::DigitsWithHyphens) < Right.NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
		});

		for (const UEdGraphNode_Comment* CommentNode : CommentNodes)
		{
			FEditorLayoutCommentSpec Spec;
			Spec.Id = CommentNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
			Spec.Text = CommentNode->NodeComment;
			Spec.X = CommentNode->NodePosX;
			Spec.Y = CommentNode->NodePosY;
			Spec.Width = CommentNode->NodeWidth;
			Spec.Height = CommentNode->NodeHeight;
			Spec.bHasColor = true;
			Spec.Color = CommentNode->CommentColor;
			Comments.Add(CommentSpecValue(Spec));
		}
	}
	Layout->SetArrayField(CommentsField, Comments);
	return MakeShared<FJsonValueObject>(Layout);
}

void CollectLayoutDiffValues(
	const TSharedPtr<FJsonValue>& LayoutValue,
	const FString& LayoutPath,
	TMap<FString, TSharedPtr<FJsonValue>>& OutValues)
{
	const TSharedPtr<FJsonObject> Layout = LayoutValue.IsValid() && LayoutValue->Type == EJson::Object
		? LayoutValue->AsObject()
		: nullptr;
	if (!Layout.IsValid())
	{
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (Layout->TryGetArrayField(NodesField, Nodes) && Nodes)
	{
		for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
		{
			const TSharedPtr<FJsonObject> Node = NodeValue.IsValid() && NodeValue->Type == EJson::Object ? NodeValue->AsObject() : nullptr;
			FString NodeId;
			if (Node.IsValid() && Node->TryGetStringField(NodeIdField, NodeId))
			{
				OutValues.Add(NodePath(LayoutPath, NodeId), NodeValue);
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Comments = nullptr;
	if (Layout->TryGetArrayField(CommentsField, Comments) && Comments)
	{
		for (const TSharedPtr<FJsonValue>& CommentValue : *Comments)
		{
			const TSharedPtr<FJsonObject> Comment = CommentValue.IsValid() && CommentValue->Type == EJson::Object ? CommentValue->AsObject() : nullptr;
			FString Id;
			if (Comment.IsValid() && Comment->TryGetStringField(IdField, Id))
			{
				OutValues.Add(CommentPath(LayoutPath, Id), CommentValue);
			}
		}
	}
}

bool JsonEqual(const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
{
	return FAssetDocumentJsonRegionUtils::JsonValueToComparableString(Left.IsValid() ? Left : MakeShared<FJsonValueNull>())
		== FAssetDocumentJsonRegionUtils::JsonValueToComparableString(Right.IsValid() ? Right : MakeShared<FJsonValueNull>());
}

void AddMapDiffs(
	const TMap<FString, TSharedPtr<FJsonValue>>& Current,
	const TMap<FString, TSharedPtr<FJsonValue>>& Desired,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
{
	TSet<FString> Paths;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Current)
	{
		Paths.Add(Entry.Key);
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Desired)
	{
		Paths.Add(Entry.Key);
	}

	TArray<FString> SortedPaths = Paths.Array();
	SortedPaths.Sort([](const FString& Left, const FString& Right)
	{
		return Left < Right;
	});

	for (const FString& Path : SortedPaths)
	{
		const TSharedPtr<FJsonValue> CurrentValue = Current.FindRef(Path);
		const TSharedPtr<FJsonValue> DesiredValue = Desired.FindRef(Path);
		const FString Status = CurrentValue.IsValid()
			? (DesiredValue.IsValid() ? (JsonEqual(CurrentValue, DesiredValue) ? TEXT("unchanged") : TEXT("changed")) : TEXT("removed"))
			: TEXT("added");
		FAssetDocumentJsonRegionUtils::AddDiffEntry(OutDiffEntries, Path, Status, CurrentValue, DesiredValue);
	}
}
}

FAssetDocumentEditorLayoutRegionAdapter::FAssetDocumentEditorLayoutRegionAdapter(
	FAssetDocumentEditorLayoutRegionAdapterConfig InConfig,
	FAssetDocumentEditorLayoutRegionAdapterHooks InHooks)
	: Config(MoveTemp(InConfig))
	, Hooks(MoveTemp(InHooks))
{
}

FAssetDocumentCapabilityResult FAssetDocumentEditorLayoutRegionAdapter::ValidateSemanticReferences(
	const TSharedPtr<FJsonValue>& DesiredValue,
	const FString& JsonPointer,
	const TSet<FString>& SemanticNodeIds)
{
	FEditorLayoutSpec Spec;
	FAssetDocumentCapabilityResult Result = ParseLayout(DesiredValue, JsonPointer, Spec);
	if (!Result.bSuccess)
	{
		return Result;
	}

	for (const FEditorLayoutNodeSpec& Node : Spec.Nodes)
	{
		if (!SemanticNodeIds.Contains(Node.NodeId))
		{
			return Failure(Node.Path, TEXT("UnknownEditorLayoutNode"), FString::Printf(TEXT("EditorLayout node '%s' does not reference an existing semantic node"), *Node.NodeId));
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated EditorLayout semantic references"));
}

FName FAssetDocumentEditorLayoutRegionAdapter::GetName() const
{
	return Config.Name;
}

bool FAssetDocumentEditorLayoutRegionAdapter::SupportsRegion(const FAssetDocumentRegionContext& Context) const
{
	return !Context.Policy || Context.Policy->RegionKind == EAssetDocumentRegionKind::Object;
}

TSharedRef<FJsonObject> FAssetDocumentEditorLayoutRegionAdapter::GetSchemaHint(const FAssetDocumentRegionContext&) const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Adapter"), GetName().ToString());
	Schema->SetStringField(TEXT("Nodes"), TEXT("array<{NodeId, Position:{X,Y}}>"));
	Schema->SetStringField(TEXT("Comments"), TEXT("array<{Id, Text, Position:{X,Y}, Size:{X,Y}, Color?}>"));
	return Schema;
}

FAssetDocumentCapabilityResult FAssetDocumentEditorLayoutRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	FEditorLayoutSpec Spec;
	return ParseLayout(DesiredValue, RegionPath(Context), Spec);
}

FAssetDocumentCapabilityResult FAssetDocumentEditorLayoutRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	bOutChanged = false;

	FEditorLayoutSpec Spec;
	FAssetDocumentCapabilityResult Result = ParseLayout(DesiredValue, RegionPath(Context), Spec);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (!Spec.bHasNodes && !Spec.bHasComments)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Sparse EditorLayout had no managed fields to apply"));
	}

	FAssetDocumentEditorLayoutGraphState GraphState;
	if (!Hooks.PrepareGraphForApply)
	{
		return Failure(RegionPath(Context), TEXT("InvalidEditorLayoutAdapterConfig"), TEXT("EditorLayout adapter requires a graph apply hook"));
	}
	Result = Hooks.PrepareGraphForApply(Context, GraphState);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (!GraphState.Graph)
	{
		return Failure(RegionPath(Context), TEXT("MissingEditorLayoutGraph"), TEXT("EditorLayout apply requires an editor graph"));
	}

	for (const FEditorLayoutNodeSpec& Node : Spec.Nodes)
	{
		UEdGraphNode* GraphNode = GraphState.NodesById.FindRef(Node.NodeId);
		if (!GraphNode)
		{
			return Failure(Node.Path, TEXT("UnknownEditorLayoutNode"), FString::Printf(TEXT("EditorLayout node '%s' does not reference an existing semantic node"), *Node.NodeId));
		}
		if (GraphNode->NodePosX != Node.X || GraphNode->NodePosY != Node.Y)
		{
			bOutChanged = true;
		}
		if (!Context.bIsDryRun)
		{
			GraphNode->Modify();
			GraphNode->NodePosX = Node.X;
			GraphNode->NodePosY = Node.Y;
		}
	}

	if (Spec.bHasComments)
	{
		TMap<FGuid, UEdGraphNode_Comment*> ExistingCommentsByGuid;
		for (UEdGraphNode* GraphNode : GraphState.Graph->Nodes)
		{
			if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(GraphNode))
			{
				ExistingCommentsByGuid.Add(CommentNode->NodeGuid, CommentNode);
			}
		}

		TSet<FGuid> DesiredCommentGuids;
		for (const FEditorLayoutCommentSpec& Comment : Spec.Comments)
		{
			DesiredCommentGuids.Add(Comment.Guid);
			UEdGraphNode_Comment* CommentNode = ExistingCommentsByGuid.FindRef(Comment.Guid);
			if (!CommentNode && !Context.bIsDryRun)
			{
				CommentNode = NewObject<UEdGraphNode_Comment>(GraphState.Graph);
				CommentNode->NodeGuid = Comment.Guid;
				GraphState.Graph->AddNode(CommentNode, false, false);
				bOutChanged = true;
			}
			if (!CommentNode)
			{
				bOutChanged = true;
				continue;
			}

			const bool bCommentChanged =
				CommentNode->NodeComment != Comment.Text
				|| CommentNode->NodePosX != Comment.X
				|| CommentNode->NodePosY != Comment.Y
				|| CommentNode->NodeWidth != Comment.Width
				|| CommentNode->NodeHeight != Comment.Height
				|| (Comment.bHasColor && CommentNode->CommentColor != Comment.Color);
			bOutChanged = bOutChanged || bCommentChanged;
			if (!Context.bIsDryRun)
			{
				CommentNode->Modify();
				CommentNode->NodeGuid = Comment.Guid;
				CommentNode->NodeComment = Comment.Text;
				CommentNode->NodePosX = Comment.X;
				CommentNode->NodePosY = Comment.Y;
				CommentNode->NodeWidth = Comment.Width;
				CommentNode->NodeHeight = Comment.Height;
				if (Comment.bHasColor)
				{
					CommentNode->CommentColor = Comment.Color;
				}
			}
		}

		for (const TPair<FGuid, UEdGraphNode_Comment*>& Existing : ExistingCommentsByGuid)
		{
			if (!DesiredCommentGuids.Contains(Existing.Key))
			{
				bOutChanged = true;
				if (!Context.bIsDryRun && Existing.Value)
				{
					GraphState.Graph->RemoveNode(Existing.Value);
				}
			}
		}
	}

	if (bOutChanged && !Context.bIsDryRun)
	{
		GraphState.Graph->Modify();
		GraphState.Graph->MarkPackageDirty();
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Applied EditorLayout"));
}

FAssetDocumentCapabilityResult FAssetDocumentEditorLayoutRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	FAssetDocumentEditorLayoutGraphState GraphState;
	if (Hooks.CollectGraphForExtract)
	{
		const FAssetDocumentCapabilityResult Result = Hooks.CollectGraphForExtract(Context, GraphState);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}
	OutCurrentValue = ExtractLayoutValue(GraphState);
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted EditorLayout"));
}

FAssetDocumentCapabilityResult FAssetDocumentEditorLayoutRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	FEditorLayoutSpec DesiredSpec;
	FAssetDocumentCapabilityResult Result = ParseLayout(DesiredValue, RegionPath(Context), DesiredSpec);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (!DesiredSpec.bHasNodes && !DesiredSpec.bHasComments)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Sparse EditorLayout had no managed fields to diff"));
	}

	if (Hooks.CollectSemanticNodeIds)
	{
		TSet<FString> SemanticIds;
		Result = Hooks.CollectSemanticNodeIds(Context, SemanticIds);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Result = ValidateSemanticReferences(DesiredValue, RegionPath(Context), SemanticIds);
		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	TSharedPtr<FJsonValue> CurrentValue;
	Result = ExtractRegion(Context, CurrentValue);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TSharedPtr<FJsonObject> CurrentObject = MakeShared<FJsonObject>();
	const TArray<TSharedPtr<FJsonValue>>* CurrentNodes = GetLayoutArray(CurrentValue, NodesField);
	if (DesiredSpec.bHasNodes)
	{
		CurrentObject->SetArrayField(NodesField, CurrentNodes ? *CurrentNodes : TArray<TSharedPtr<FJsonValue>>());
	}
	const TArray<TSharedPtr<FJsonValue>>* CurrentComments = GetLayoutArray(CurrentValue, CommentsField);
	if (DesiredSpec.bHasComments)
	{
		CurrentObject->SetArrayField(CommentsField, CurrentComments ? *CurrentComments : TArray<TSharedPtr<FJsonValue>>());
	}

	TSharedPtr<FJsonObject> DesiredObject = MakeShared<FJsonObject>();
	if (DesiredSpec.bHasNodes)
	{
		TArray<TSharedPtr<FJsonValue>> DesiredNodes;
		for (const FEditorLayoutNodeSpec& Node : DesiredSpec.Nodes)
		{
			DesiredNodes.Add(NodeSpecValue(Node));
		}
		DesiredObject->SetArrayField(NodesField, DesiredNodes);
	}

	if (DesiredSpec.bHasComments)
	{
		TArray<TSharedPtr<FJsonValue>> DesiredComments;
		for (const FEditorLayoutCommentSpec& Comment : DesiredSpec.Comments)
		{
			const TSharedPtr<FJsonValue> DesiredCommentValue = CommentSpecValue(Comment);
			const TSharedPtr<FJsonObject> DesiredCommentObject = DesiredCommentValue.IsValid() ? DesiredCommentValue->AsObject() : nullptr;
			const TSharedPtr<FJsonObject> CurrentCommentObject = FindObjectByStringId(CurrentComments, IdField, Comment.Id);
			if (!Comment.bHasColor && DesiredCommentObject.IsValid() && CurrentCommentObject.IsValid())
			{
				const TSharedPtr<FJsonValue> CurrentColor = CurrentCommentObject->TryGetField(ColorField);
				if (CurrentColor.IsValid())
				{
					DesiredCommentObject->SetField(ColorField, CurrentColor);
				}
			}
			DesiredComments.Add(DesiredCommentValue);
		}
		DesiredObject->SetArrayField(CommentsField, DesiredComments);
	}

	TMap<FString, TSharedPtr<FJsonValue>> CurrentValues;
	TMap<FString, TSharedPtr<FJsonValue>> DesiredValues;
	CollectLayoutDiffValues(MakeShared<FJsonValueObject>(CurrentObject), RegionPath(Context), CurrentValues);
	CollectLayoutDiffValues(MakeShared<FJsonValueObject>(DesiredObject), RegionPath(Context), DesiredValues);
	AddMapDiffs(CurrentValues, DesiredValues, OutDiffEntries);
	return FAssetDocumentCapabilityResult::Success(TEXT("Diffed EditorLayout"));
}
