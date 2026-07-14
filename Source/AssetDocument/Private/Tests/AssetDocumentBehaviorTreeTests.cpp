// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentProfileRegistry.h"
#include "AssetDocumentService.h"
#include "AssetDocumentSidecar.h"
#include "AssetDocumentJsonRegionUtils.h"
#include "Profiles/BehaviorTreeAssetDocumentMaterializer.h"
#include "Profiles/BehaviorTreeAssetDocumentProfile.h"
#include "Regions/AssetDocumentReflectedPropertyUtils.h"
#include "AIGraphTypes.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "BehaviorTree/Services/BTService_DefaultFocus.h"
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"
#include "BehaviorTree/Tasks/BTTask_BlueprintBase.h"
#include "BehaviorTree/Tasks/BTTask_RunBehavior.h"
#include "BehaviorTree/Tasks/BTTask_SetKeyValue.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "BehaviorTree/Tasks/BTTask_WaitBlackboardTime.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BehaviorTreeGraphNode_Task.h"
#include "Animation/NodeMappingContainer.h"
#include "BlueprintEditorSettings.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "Engine/Blueprint.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "PackageTools.h"
#include "Sections/MovieSceneCVarSection.h"
#include "TestActorBase.h"
#include "TestDataAsset.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
const FAssetDocumentRegionPolicy* FindPolicyByRegionId(const TArray<FAssetDocumentRegionPolicy>& Policies, FName RegionId)
{
	return Policies.FindByPredicate([RegionId](const FAssetDocumentRegionPolicy& Policy)
	{
		return Policy.RegionId == RegionId;
	});
}

TSharedRef<FJsonObject> MakeObject()
{
	return MakeShared<FJsonObject>();
}

TSharedPtr<FJsonObject> MakeAssetRef(const FString& Path)
{
	TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
	Ref->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	Ref->SetStringField(TEXT("Path"), Path);
	return Ref;
}

TSharedPtr<FJsonValue> MakeObjectValue(TSharedPtr<FJsonObject> Object)
{
	return MakeShared<FJsonValueObject>(Object.ToSharedRef());
}

FString MakeObjectPathFromTarget(const FString& Target)
{
	return FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
}

UBlackboardData* MakeExistingBlackboardAsset(const FString& Target)
{
	UPackage* Package = CreatePackage(*Target);
	return NewObject<UBlackboardData>(
		Package,
		*FPackageName::GetLongPackageAssetName(Target),
		RF_Public | RF_Standalone | RF_Transactional);
}

template <typename TKeyType>
void AddBlackboardKey(UBlackboardData* Blackboard, FName Name)
{
	if (!Blackboard)
	{
		return;
	}

	FBlackboardEntry Entry;
	Entry.EntryName = Name;
	Entry.KeyType = NewObject<TKeyType>(Blackboard);
	Blackboard->Keys.Add(Entry);
}

void AddObjectBlackboardKey(UBlackboardData* Blackboard, FName Name, UClass* BaseClass = AActor::StaticClass())
{
	if (!Blackboard)
	{
		return;
	}

	FBlackboardEntry Entry;
	Entry.EntryName = Name;
	UBlackboardKeyType_Object* KeyType = NewObject<UBlackboardKeyType_Object>(Blackboard);
	KeyType->BaseClass = BaseClass;
	Entry.KeyType = KeyType;
	Blackboard->Keys.Add(Entry);
}

UBlackboardData* MakeTask7BlackboardAsset(const FString& Target)
{
	UBlackboardData* Blackboard = MakeExistingBlackboardAsset(Target);
	AddObjectBlackboardKey(Blackboard, TEXT("TargetActor"));
	AddObjectBlackboardKey(Blackboard, TEXT("OtherTargetActor"));
	return Blackboard;
}

UBehaviorTree* MakeExistingBehaviorTreeAsset(const FString& Target)
{
	UPackage* Package = CreatePackage(*Target);
	return NewObject<UBehaviorTree>(
		Package,
		*FPackageName::GetLongPackageAssetName(Target),
		RF_Public | RF_Standalone | RF_Transactional);
}

UBehaviorTree* LoadBehaviorTreeForTarget(const FString& Target)
{
	return LoadObject<UBehaviorTree>(nullptr, *MakeObjectPathFromTarget(Target));
}

FAssetDocumentApplyRequest MakeApplyRequest(TSharedPtr<FJsonObject> Document)
{
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;
	return Request;
}

FAssetDocumentDiffRequest MakeDiffRequest(TSharedPtr<FJsonObject> Document)
{
	FAssetDocumentDiffRequest Request;
	Request.Document = Document;
	return Request;
}

bool ResultHasDiagnostic(const FAssetDocumentResult& Result, const FString& ExpectedCode, const FString& ExpectedPath)
{
	return Result.Diagnostics.ContainsByPredicate([&ExpectedCode, &ExpectedPath](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == ExpectedCode && Diagnostic.Path == ExpectedPath;
	});
}

int32 CountBehaviorTreeNodeChildren(UBehaviorTree* BehaviorTree)
{
	int32 Count = 0;
	ForEachObjectWithOuter(BehaviorTree, [&Count](UObject* Object)
	{
		if (Object && Object->IsA<UBTNode>())
		{
			++Count;
		}
	}, true);
	return Count;
}

bool DiffPayloadHasEntry(const TSharedPtr<FJsonObject>& Payload, const FString& BucketName, const FString& ExpectedPath)
{
	if (!Payload.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Payload->TryGetArrayField(BucketName, Entries) || !Entries)
	{
		return false;
	}

	return Entries->ContainsByPredicate([&ExpectedPath](const TSharedPtr<FJsonValue>& EntryValue)
	{
		const TSharedPtr<FJsonObject> Entry = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
		FString Path;
		return Entry.IsValid() && Entry->TryGetStringField(TEXT("path"), Path) && Path == ExpectedPath;
	});
}

bool DiffPayloadHasAnyEntry(const TSharedPtr<FJsonObject>& Payload, const FString& ExpectedPath)
{
	return DiffPayloadHasEntry(Payload, TEXT("changed"), ExpectedPath)
		|| DiffPayloadHasEntry(Payload, TEXT("added"), ExpectedPath)
		|| DiffPayloadHasEntry(Payload, TEXT("removed"), ExpectedPath)
		|| DiffPayloadHasEntry(Payload, TEXT("unchanged"), ExpectedPath);
}

bool DiffPayloadHasNoChangedOrFailedEntries(const TSharedPtr<FJsonObject>& Payload)
{
	const TArray<TSharedPtr<FJsonValue>>* Changed = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Failed = nullptr;
	return Payload.IsValid()
		&& Payload->TryGetArrayField(TEXT("changed"), Changed)
		&& Payload->TryGetArrayField(TEXT("failed"), Failed)
		&& Changed
		&& Failed
		&& Changed->Num() == 0
		&& Failed->Num() == 0;
}

bool DiffPayloadBucketHasPathPrefix(const TSharedPtr<FJsonObject>& Payload, const FString& BucketName, const FString& ExpectedPrefix)
{
	if (!Payload.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Payload->TryGetArrayField(BucketName, Entries) || !Entries)
	{
		return false;
	}

	return Entries->ContainsByPredicate([&ExpectedPrefix](const TSharedPtr<FJsonValue>& EntryValue)
	{
		const TSharedPtr<FJsonObject> Entry = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
		FString Path;
		return Entry.IsValid() && Entry->TryGetStringField(TEXT("path"), Path) && Path.StartsWith(ExpectedPrefix);
	});
}

bool DiffPayloadHasChangedPathPrefix(const TSharedPtr<FJsonObject>& Payload, const FString& ExpectedPrefix)
{
	return DiffPayloadBucketHasPathPrefix(Payload, TEXT("changed"), ExpectedPrefix)
		|| DiffPayloadBucketHasPathPrefix(Payload, TEXT("added"), ExpectedPrefix)
		|| DiffPayloadBucketHasPathPrefix(Payload, TEXT("removed"), ExpectedPrefix);
}

void AddDiffPathsWithPrefix(FAutomationTestBase& Test, const TSharedPtr<FJsonObject>& Payload, const FString& ExpectedPrefix)
{
	if (!Payload.IsValid())
	{
		return;
	}

	for (const FString& BucketName : {FString(TEXT("changed")), FString(TEXT("added")), FString(TEXT("removed"))})
	{
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!Payload->TryGetArrayField(BucketName, Entries) || !Entries)
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& EntryValue : *Entries)
		{
			const TSharedPtr<FJsonObject> Entry = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
			FString Path;
			if (Entry.IsValid() && Entry->TryGetStringField(TEXT("path"), Path) && Path.StartsWith(ExpectedPrefix))
			{
				Test.AddError(FString::Printf(TEXT("Unexpected %s diff at %s"), *BucketName, *Path));
			}
		}
	}
}


void SwapArrayEntries(TSharedPtr<FJsonObject> Object, const FString& FieldName, int32 FirstIndex, int32 SecondIndex)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, Values) || !Values || !Values->IsValidIndex(FirstIndex) || !Values->IsValidIndex(SecondIndex))
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>> Swapped = *Values;
	Swapped.Swap(FirstIndex, SecondIndex);
	Object->SetArrayField(FieldName, Swapped);
}

TSharedPtr<FJsonObject> MakeBehaviorTreeBody(
	TSharedPtr<FJsonObject> Blackboard,
	TSharedPtr<FJsonObject> Tree = nullptr,
	TSharedPtr<FJsonObject> EditorLayout = nullptr)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	if (Blackboard.IsValid())
	{
		Body->SetObjectField(TEXT("Blackboard"), Blackboard);
	}
	else
	{
		Body->SetField(TEXT("Blackboard"), MakeShared<FJsonValueNull>());
	}

	if (Tree.IsValid())
	{
		Body->SetObjectField(TEXT("Tree"), Tree);
	}
	if (EditorLayout.IsValid())
	{
		Body->SetObjectField(TEXT("EditorLayout"), EditorLayout);
	}
	return Body;
}

TSharedPtr<FJsonObject> MakeEmptyBehaviorTree()
{
	TSharedPtr<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetArrayField(TEXT("RootDecorators"), TArray<TSharedPtr<FJsonValue>>());
	Tree->SetArrayField(TEXT("RootDecoratorLogic"), TArray<TSharedPtr<FJsonValue>>());
	Tree->SetObjectField(TEXT("Root"), MakeShared<FJsonObject>());
	return Tree;
}

TSharedPtr<FJsonObject> MakeMinimalSelectorBehaviorTree()
{
	TSharedPtr<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetArrayField(TEXT("RootDecorators"), TArray<TSharedPtr<FJsonValue>>());
	Tree->SetArrayField(TEXT("RootDecoratorLogic"), TArray<TSharedPtr<FJsonValue>>());
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("Id"), TEXT("Root"));
	Root->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BTComposite_Selector"));
	Root->SetArrayField(TEXT("Services"), TArray<TSharedPtr<FJsonValue>>());
	Root->SetArrayField(TEXT("Children"), TArray<TSharedPtr<FJsonValue>>());
	Tree->SetObjectField(TEXT("Root"), Root);
	return Tree;
}

TSharedPtr<FJsonObject> MakeTreeWithKeySelectorLikeProperty()
{
	TSharedPtr<FJsonObject> Selector = MakeShared<FJsonObject>();
	Selector->SetStringField(TEXT("Key"), TEXT("TargetActor"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetObjectField(TEXT("BlackboardKey"), Selector);

	TSharedPtr<FJsonObject> Task = MakeShared<FJsonObject>();
	Task->SetStringField(TEXT("Id"), TEXT("WaitForTarget"));
	Task->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BTTask_WaitBlackboardTime"));
	Task->SetObjectField(TEXT("Properties"), Properties);
	TSharedPtr<FJsonObject> Edge = MakeShared<FJsonObject>();
	Edge->SetObjectField(TEXT("Child"), Task);

	TSharedPtr<FJsonObject> Tree = MakeMinimalSelectorBehaviorTree();
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("Id"), TEXT("Root"));
	Root->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BTComposite_Selector"));
	Root->SetArrayField(TEXT("Services"), TArray<TSharedPtr<FJsonValue>>());
	Root->SetArrayField(TEXT("Children"), {MakeObjectValue(Edge)});
	Tree->SetObjectField(TEXT("Root"), Root);
	return Tree;
}

TSharedPtr<FJsonObject> MakeDecoratorLogicOp(const FString& Operation, int32 Number)
{
	TSharedPtr<FJsonObject> Logic = MakeShared<FJsonObject>();
	Logic->SetStringField(TEXT("Operation"), Operation);
	Logic->SetNumberField(TEXT("Number"), Number);
	return Logic;
}

TSharedPtr<FJsonObject> MakeSelectorProperty(const FString& KeyName)
{
	TSharedPtr<FJsonObject> Selector = MakeShared<FJsonObject>();
	Selector->SetStringField(TEXT("Key"), KeyName);
	return Selector;
}

TSharedPtr<FJsonObject> MakeBtNode(
	const FString& Id,
	const FString& Class,
	TSharedPtr<FJsonObject> Properties = nullptr)
{
	TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("Id"), Id);
	Node->SetStringField(TEXT("Class"), Class);
	if (Properties.IsValid())
	{
		Node->SetObjectField(TEXT("Properties"), Properties);
	}
	return Node;
}

TSharedPtr<FJsonObject> MakeDecoratorLogicTest(int32 Number = 0)
{
	TSharedPtr<FJsonObject> Logic = MakeShared<FJsonObject>();
	Logic->SetStringField(TEXT("Operation"), TEXT("Test"));
	Logic->SetNumberField(TEXT("Number"), Number);
	return Logic;
}

TSharedPtr<FJsonObject> MakeMoveToTreeWithKeys(const TArray<FString>& KeyNames)
{
	TArray<TSharedPtr<FJsonValue>> Children;
	for (const FString& KeyName : KeyNames)
	{
		TSharedPtr<FJsonObject> MoveToProperties = MakeShared<FJsonObject>();
		MoveToProperties->SetObjectField(TEXT("BlackboardKey"), MakeSelectorProperty(KeyName));
		const FString NodeId = KeyName == TEXT("TargetActor")
			? FString(TEXT("MoveToTarget"))
			: FString::Printf(TEXT("MoveTo_%s"), *KeyName);
		TSharedPtr<FJsonObject> MoveToNode = MakeBtNode(NodeId, TEXT("/Script/AIModule.BTTask_MoveTo"), MoveToProperties);

		TSharedPtr<FJsonObject> Edge = MakeShared<FJsonObject>();
		Edge->SetObjectField(TEXT("Child"), MoveToNode);
		Children.Add(MakeObjectValue(Edge));
	}

	TSharedPtr<FJsonObject> Root = MakeBtNode(TEXT("RootSelector"), TEXT("/Script/AIModule.BTComposite_Selector"));
	Root->SetArrayField(TEXT("Children"), Children);

	TSharedPtr<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetObjectField(TEXT("Root"), Root);
	Tree->SetArrayField(TEXT("RootDecorators"), TArray<TSharedPtr<FJsonValue>>());
	Tree->SetArrayField(TEXT("RootDecoratorLogic"), TArray<TSharedPtr<FJsonValue>>());
	return Tree;
}

TSharedPtr<FJsonObject> MakePositionObject(double X, double Y)
{
	TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
	Position->SetNumberField(TEXT("X"), X);
	Position->SetNumberField(TEXT("Y"), Y);
	return Position;
}

TSharedPtr<FJsonObject> MakeEditorLayoutNode(const FString& NodeId, double X, double Y)
{
	TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("NodeId"), NodeId);
	Node->SetObjectField(TEXT("Position"), MakePositionObject(X, Y));
	return Node;
}

TSharedPtr<FJsonObject> MakeEditorLayoutComment(
	const FString& Id,
	const FString& Text,
	double X,
	double Y,
	double Width,
	double Height,
	bool bIncludeColor = true)
{
	TSharedPtr<FJsonObject> Comment = MakeShared<FJsonObject>();
	Comment->SetStringField(TEXT("Id"), Id);
	Comment->SetStringField(TEXT("Text"), Text);
	Comment->SetObjectField(TEXT("Position"), MakePositionObject(X, Y));
	Comment->SetObjectField(TEXT("Size"), MakePositionObject(Width, Height));
	if (bIncludeColor)
	{
		TSharedPtr<FJsonObject> Color = MakeShared<FJsonObject>();
		Color->SetNumberField(TEXT("R"), 0.1);
		Color->SetNumberField(TEXT("G"), 0.2);
		Color->SetNumberField(TEXT("B"), 0.3);
		Color->SetNumberField(TEXT("A"), 0.4);
		Comment->SetObjectField(TEXT("Color"), Color);
	}
	return Comment;
}

TSharedPtr<FJsonObject> MakeEditorLayout(
	const TArray<TSharedPtr<FJsonValue>>& Nodes,
	const TArray<TSharedPtr<FJsonValue>>& Comments = TArray<TSharedPtr<FJsonValue>>())
{
	TSharedPtr<FJsonObject> Layout = MakeShared<FJsonObject>();
	Layout->SetArrayField(TEXT("Nodes"), Nodes);
	Layout->SetArrayField(TEXT("Comments"), Comments);
	return Layout;
}

UEdGraphNode_Comment* FindEditorLayoutComment(UBehaviorTreeGraph* Graph, const FString& Id)
{
	FGuid Guid;
	if (!Graph || !FGuid::Parse(Id, Guid))
	{
		return nullptr;
	}

	for (UEdGraphNode* GraphNode : Graph->Nodes)
	{
		UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(GraphNode);
		if (CommentNode && CommentNode->NodeGuid == Guid)
		{
			return CommentNode;
		}
	}
	return nullptr;
}

TSharedPtr<FJsonObject> MakeRunBehaviorTree(const FString& SubtreeTarget)
{
	TSharedPtr<FJsonObject> RunSubtreeProperties = MakeShared<FJsonObject>();
	RunSubtreeProperties->SetObjectField(TEXT("BehaviorAsset"), MakeAssetRef(MakeObjectPathFromTarget(SubtreeTarget)));
	TSharedPtr<FJsonObject> RunSubtreeNode = MakeBtNode(
		TEXT("RunSubtree"),
		TEXT("/Script/AIModule.BTTask_RunBehavior"),
		RunSubtreeProperties);

	TSharedPtr<FJsonObject> Edge = MakeShared<FJsonObject>();
	Edge->SetObjectField(TEXT("Child"), RunSubtreeNode);

	TArray<TSharedPtr<FJsonValue>> Children;
	Children.Add(MakeObjectValue(Edge));

	TSharedPtr<FJsonObject> Root = MakeBtNode(TEXT("RootSelector"), TEXT("/Script/AIModule.BTComposite_Selector"));
	Root->SetArrayField(TEXT("Children"), Children);

	TSharedPtr<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetObjectField(TEXT("Root"), Root);
	Tree->SetArrayField(TEXT("RootDecorators"), TArray<TSharedPtr<FJsonValue>>());
	Tree->SetArrayField(TEXT("RootDecoratorLogic"), TArray<TSharedPtr<FJsonValue>>());
	return Tree;
}

TSharedPtr<FJsonObject> MakeTreeWithSelectorTask(
	TSharedPtr<FJsonObject> Selector,
	const FString& TaskClass = TEXT("/Script/AIModule.BTTask_MoveTo"))
{
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetObjectField(TEXT("BlackboardKey"), Selector);
	TSharedPtr<FJsonObject> MoveToNode = MakeBtNode(TEXT("SelectorTask"), TaskClass, Properties);

	TSharedPtr<FJsonObject> Edge = MakeShared<FJsonObject>();
	Edge->SetObjectField(TEXT("Child"), MoveToNode);

	TSharedPtr<FJsonObject> Root = MakeBtNode(TEXT("RootSelector"), TEXT("/Script/AIModule.BTComposite_Selector"));
	Root->SetArrayField(TEXT("Children"), {MakeObjectValue(Edge)});

	TSharedPtr<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetObjectField(TEXT("Root"), Root);
	Tree->SetArrayField(TEXT("RootDecorators"), TArray<TSharedPtr<FJsonValue>>());
	Tree->SetArrayField(TEXT("RootDecoratorLogic"), TArray<TSharedPtr<FJsonValue>>());
	return Tree;
}

TSharedPtr<FJsonObject> MakeTask7BehaviorTree(
	const FString& SubtreeTarget,
	bool bIncludeRootDecorator,
	bool bUseShortClassNames)
{
	const FString SelectorClass = bUseShortClassNames ? TEXT("BTComposite_Selector") : TEXT("/Script/AIModule.BTComposite_Selector");
	const FString ServiceClass = bUseShortClassNames ? TEXT("BTService_DefaultFocus") : TEXT("/Script/AIModule.BTService_DefaultFocus");
	const FString DecoratorClass = bUseShortClassNames ? TEXT("BTDecorator_Blackboard") : TEXT("/Script/AIModule.BTDecorator_Blackboard");
	const FString MoveToClass = bUseShortClassNames ? TEXT("BTTask_MoveTo") : TEXT("/Script/AIModule.BTTask_MoveTo");
	const FString RunBehaviorClass = bUseShortClassNames ? TEXT("BTTask_RunBehavior") : TEXT("/Script/AIModule.BTTask_RunBehavior");

	TSharedPtr<FJsonObject> ServiceProperties = MakeShared<FJsonObject>();
	ServiceProperties->SetNumberField(TEXT("Interval"), 1.25);
	TArray<TSharedPtr<FJsonValue>> Services;
	Services.Add(MakeObjectValue(MakeBtNode(TEXT("FocusService"), ServiceClass, ServiceProperties)));

	TSharedPtr<FJsonObject> DecoratorProperties = MakeShared<FJsonObject>();
	DecoratorProperties->SetObjectField(TEXT("BlackboardKey"), MakeSelectorProperty(TEXT("TargetActor")));
	TArray<TSharedPtr<FJsonValue>> EdgeDecorators;
	EdgeDecorators.Add(MakeObjectValue(MakeBtNode(TEXT("HasTarget"), DecoratorClass, DecoratorProperties)));

	TArray<TSharedPtr<FJsonValue>> EdgeLogic;
	EdgeLogic.Add(MakeObjectValue(MakeDecoratorLogicTest()));

	TSharedPtr<FJsonObject> MoveToProperties = MakeShared<FJsonObject>();
	MoveToProperties->SetObjectField(TEXT("BlackboardKey"), MakeSelectorProperty(TEXT("TargetActor")));
	TSharedPtr<FJsonObject> MoveToNode = MakeBtNode(TEXT("MoveToTarget"), MoveToClass, MoveToProperties);

	TSharedPtr<FJsonObject> MoveEdge = MakeShared<FJsonObject>();
	MoveEdge->SetObjectField(TEXT("Child"), MoveToNode);
	MoveEdge->SetArrayField(TEXT("Decorators"), EdgeDecorators);
	MoveEdge->SetArrayField(TEXT("DecoratorLogic"), EdgeLogic);

	TSharedPtr<FJsonObject> RunSubtreeProperties = MakeShared<FJsonObject>();
	RunSubtreeProperties->SetObjectField(TEXT("BehaviorAsset"), MakeAssetRef(MakeObjectPathFromTarget(SubtreeTarget)));
	TSharedPtr<FJsonObject> RunSubtreeNode = MakeBtNode(TEXT("RunSubtree"), RunBehaviorClass, RunSubtreeProperties);

	TSharedPtr<FJsonObject> RunSubtreeEdge = MakeShared<FJsonObject>();
	RunSubtreeEdge->SetObjectField(TEXT("Child"), RunSubtreeNode);

	TArray<TSharedPtr<FJsonValue>> Children;
	Children.Add(MakeObjectValue(MoveEdge));
	Children.Add(MakeObjectValue(RunSubtreeEdge));

	TSharedPtr<FJsonObject> Root = MakeBtNode(TEXT("RootSelector"), SelectorClass);
	Root->SetArrayField(TEXT("Services"), Services);
	Root->SetArrayField(TEXT("Children"), Children);

	TSharedPtr<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetObjectField(TEXT("Root"), Root);

	TArray<TSharedPtr<FJsonValue>> RootDecorators;
	if (bIncludeRootDecorator)
	{
		TSharedPtr<FJsonObject> RootDecoratorProperties = MakeShared<FJsonObject>();
		RootDecoratorProperties->SetObjectField(TEXT("BlackboardKey"), MakeSelectorProperty(TEXT("TargetActor")));
		RootDecorators.Add(MakeObjectValue(MakeBtNode(TEXT("RootHasTarget"), DecoratorClass, RootDecoratorProperties)));
	}
	Tree->SetArrayField(TEXT("RootDecorators"), RootDecorators);

	TArray<TSharedPtr<FJsonValue>> RootLogic;
	if (bIncludeRootDecorator)
	{
		RootLogic.Add(MakeObjectValue(MakeDecoratorLogicTest()));
	}
	Tree->SetArrayField(TEXT("RootDecoratorLogic"), RootLogic);
	return Tree;
}

TSharedPtr<FJsonObject> MakeBehaviorTreeDocument(const FString& Target, TSharedPtr<FJsonObject> Body)
{
	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BehaviorTree"));
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Body"), Body);
	return Document;
}

TSharedRef<FJsonObject> MakeBlackboardKeyJson(
	const FString& Name,
	const FString& Type,
	const FString& BaseClass = TEXT(""),
	const FString& Enum = TEXT(""),
	const FString& KeyTypeClass = TEXT(""),
	const FString& Description = TEXT(""))
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("Name"), Name);
	if (!Type.IsEmpty())
	{
		Json->SetStringField(TEXT("Type"), Type);
	}
	if (!BaseClass.IsEmpty())
	{
		Json->SetStringField(TEXT("BaseClass"), BaseClass);
	}
	if (!Enum.IsEmpty())
	{
		Json->SetStringField(TEXT("Enum"), Enum);
	}
	if (!KeyTypeClass.IsEmpty())
	{
		Json->SetStringField(TEXT("KeyTypeClass"), KeyTypeClass);
	}
	if (!Description.IsEmpty())
	{
		Json->SetStringField(TEXT("Description"), Description);
	}
	return Json;
}

TSharedPtr<FJsonObject> MakeBlackboardDataBody(TSharedPtr<FJsonObject> Parent, TArray<TSharedRef<FJsonObject>> Keys)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	if (Parent.IsValid())
	{
		Body->SetObjectField(TEXT("Parent"), Parent);
	}
	else
	{
		Body->SetField(TEXT("Parent"), MakeShared<FJsonValueNull>());
	}

	TArray<TSharedPtr<FJsonValue>> KeyValues;
	KeyValues.Reserve(Keys.Num());
	for (const TSharedRef<FJsonObject>& Key : Keys)
	{
		KeyValues.Add(MakeShared<FJsonValueObject>(Key));
	}
	Body->SetArrayField(TEXT("Keys"), MoveTemp(KeyValues));
	return Body;
}

TSharedPtr<FJsonObject> MakeBlackboardDataDocument(const FString& Target, TSharedPtr<FJsonObject> Body)
{
	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BlackboardData"));
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Body"), Body);
	return Document;
}

TSharedPtr<FJsonObject> MakeClassRef(const FString& Path)
{
	TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
	Ref->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	Ref->SetStringField(TEXT("Path"), Path);
	return Ref;
}

bool HasDiagnostic(const FAssetDocumentCapabilityResult& Result, const FString& Path, const FString& Code)
{
	return Result.Diagnostics.ContainsByPredicate([&Path, &Code](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Path == Path && Diagnostic.Code == Code;
	});
}

TSharedPtr<FJsonObject> GetObjectField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
{
	const TSharedPtr<FJsonObject>* FieldObject = nullptr;
	return Object.IsValid() && Object->TryGetObjectField(FieldName, FieldObject) && FieldObject ? *FieldObject : nullptr;
}

TSharedPtr<FJsonObject> GetObjectFromValue(const TSharedPtr<FJsonValue>& Value)
{
	const TSharedPtr<FJsonObject>* Object = nullptr;
	return Value.IsValid() && Value->TryGetObject(Object) && Object ? *Object : nullptr;
}

void AddSecondDecoratorSet(TSharedPtr<FJsonObject> Tree)
{
	TSharedPtr<FJsonObject> OtherDecoratorProperties = MakeShared<FJsonObject>();
	OtherDecoratorProperties->SetObjectField(TEXT("BlackboardKey"), MakeSelectorProperty(TEXT("OtherTargetActor")));
	TSharedPtr<FJsonObject> OtherDecorator = MakeBtNode(TEXT("HasOtherTarget"), TEXT("/Script/AIModule.BTDecorator_Blackboard"), OtherDecoratorProperties);

	TSharedPtr<FJsonObject> Root = GetObjectField(Tree, TEXT("Root"));
	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (Root.IsValid() && Root->TryGetArrayField(TEXT("Children"), Children) && Children && Children->Num() > 0)
	{
		TSharedPtr<FJsonObject> FirstEdge = GetObjectFromValue((*Children)[0]);
		const TArray<TSharedPtr<FJsonValue>>* EdgeDecorators = nullptr;
		TArray<TSharedPtr<FJsonValue>> UpdatedDecorators;
		if (FirstEdge.IsValid() && FirstEdge->TryGetArrayField(TEXT("Decorators"), EdgeDecorators) && EdgeDecorators)
		{
			UpdatedDecorators = *EdgeDecorators;
		}
		UpdatedDecorators.Add(MakeObjectValue(OtherDecorator));
		FirstEdge->SetArrayField(TEXT("Decorators"), UpdatedDecorators);
	}

	TSharedPtr<FJsonObject> OtherRootDecoratorProperties = MakeShared<FJsonObject>();
	OtherRootDecoratorProperties->SetObjectField(TEXT("BlackboardKey"), MakeSelectorProperty(TEXT("OtherTargetActor")));
	TSharedPtr<FJsonObject> OtherRootDecorator = MakeBtNode(TEXT("RootHasOtherTarget"), TEXT("/Script/AIModule.BTDecorator_Blackboard"), OtherRootDecoratorProperties);
	const TArray<TSharedPtr<FJsonValue>>* RootDecorators = nullptr;
	TArray<TSharedPtr<FJsonValue>> UpdatedRootDecorators;
	if (Tree.IsValid() && Tree->TryGetArrayField(TEXT("RootDecorators"), RootDecorators) && RootDecorators)
	{
		UpdatedRootDecorators = *RootDecorators;
	}
	UpdatedRootDecorators.Add(MakeObjectValue(OtherRootDecorator));
	Tree->SetArrayField(TEXT("RootDecorators"), UpdatedRootDecorators);
}

bool ApplyTask7TreeFixture(
	FAutomationTestBase& Test,
	FAssetDocumentService& Service,
	const FString& Target,
	bool bIncludeRootDecorator = true,
	bool bUseShortClassNames = false)
{
	const FString BlackboardTarget = Target + TEXT("_BB");
	const FString SubtreeTarget = Target + TEXT("_Subtree");
	UBlackboardData* Blackboard = MakeTask7BlackboardAsset(BlackboardTarget);
	Test.TestNotNull(TEXT("Task7 fixture blackboard exists"), Blackboard);
	if (!Blackboard)
	{
		return false;
	}

	UBehaviorTree* Subtree = MakeExistingBehaviorTreeAsset(SubtreeTarget);
	Test.TestNotNull(TEXT("Task7 fixture subtree exists"), Subtree);
	if (!Subtree)
	{
		return false;
	}

	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		MakeTask7BehaviorTree(SubtreeTarget, bIncludeRootDecorator, bUseShortClassNames),
		MakeShared<FJsonObject>());
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, Body)));
	Test.TestTrue(TEXT("Task7 fixture apply succeeds"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		Test.AddError(ApplyResult.Message);
	}
	return ApplyResult.IsSuccess();
}

bool CollectIdsFromAttachmentObject(const TSharedPtr<FJsonObject>& Object, TSet<FString>& SeenIds, TArray<FString>& OutIds)
{
	if (!Object.IsValid())
	{
		return true;
	}

	FString Id;
	if (!Object->TryGetStringField(TEXT("Id"), Id))
	{
		return false;
	}
	if (SeenIds.Contains(Id))
	{
		return false;
	}

	SeenIds.Add(Id);
	OutIds.Add(Id);
	return true;
}

bool CollectIdsFromTreeNode(const TSharedPtr<FJsonObject>& Node, TSet<FString>& SeenIds, TArray<FString>& OutIds)
{
	if (!CollectIdsFromAttachmentObject(Node, SeenIds, OutIds))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Services = nullptr;
	if (Node.IsValid() && Node->TryGetArrayField(TEXT("Services"), Services) && Services)
	{
		for (const TSharedPtr<FJsonValue>& ServiceValue : *Services)
		{
			if (!CollectIdsFromAttachmentObject(GetObjectFromValue(ServiceValue), SeenIds, OutIds))
			{
				return false;
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (Node.IsValid() && Node->TryGetArrayField(TEXT("Children"), Children) && Children)
	{
		for (const TSharedPtr<FJsonValue>& ChildValue : *Children)
		{
			TSharedPtr<FJsonObject> Edge = GetObjectFromValue(ChildValue);
			const TArray<TSharedPtr<FJsonValue>>* Decorators = nullptr;
			if (Edge.IsValid() && Edge->TryGetArrayField(TEXT("Decorators"), Decorators) && Decorators)
			{
				for (const TSharedPtr<FJsonValue>& DecoratorValue : *Decorators)
				{
					if (!CollectIdsFromAttachmentObject(GetObjectFromValue(DecoratorValue), SeenIds, OutIds))
					{
						return false;
					}
				}
			}

			if (!CollectIdsFromTreeNode(GetObjectField(Edge, TEXT("Child")), SeenIds, OutIds))
			{
				return false;
			}
		}
	}

	return true;
}

bool CollectAllTreeIds(const TSharedPtr<FJsonObject>& Tree, TArray<FString>& OutIds)
{
	OutIds.Reset();
	TSet<FString> SeenIds;
	if (!CollectIdsFromTreeNode(GetObjectField(Tree, TEXT("Root")), SeenIds, OutIds))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* RootDecorators = nullptr;
	if (Tree.IsValid() && Tree->TryGetArrayField(TEXT("RootDecorators"), RootDecorators) && RootDecorators)
	{
		for (const TSharedPtr<FJsonValue>& DecoratorValue : *RootDecorators)
		{
			if (!CollectIdsFromAttachmentObject(GetObjectFromValue(DecoratorValue), SeenIds, OutIds))
			{
				return false;
			}
		}
	}

	return true;
}

bool JsonArrayContainsString(const TArray<TSharedPtr<FJsonValue>>* Values, const FString& Expected)
{
	if (!Values)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		if (Value.IsValid() && Value->Type == EJson::String && Value->AsString() == Expected)
		{
			return true;
		}
	}
	return false;
}

bool RegionPoliciesContain(const TArray<TSharedPtr<FJsonValue>>* Values, const FString& ExpectedRegionId)
{
	if (!Values)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid() && Value->Type == EJson::Object
			? Value->AsObject()
			: nullptr;
		FString RegionId;
		if (Object.IsValid() && Object->TryGetStringField(TEXT("RegionId"), RegionId) && RegionId == ExpectedRegionId)
		{
			return true;
		}
	}
	return false;
}

bool WriteSidecarJson(FAutomationTestBase* Test, const FString& SidecarPath, const TSharedPtr<FJsonObject>& Document)
{
	FString Error;
	const bool bWrote = FAssetDocumentSidecar::WriteJsonFile(SidecarPath, Document, Error);
	Test->TestTrue(FString::Printf(TEXT("Writes sidecar JSON '%s'"), *SidecarPath), bWrote);
	if (!bWrote)
	{
		Test->AddError(Error);
	}
	return bWrote;
}

bool LoadSidecarJson(FAutomationTestBase* Test, const FString& SidecarPath, TSharedPtr<FJsonObject>& OutDocument)
{
	FString Error;
	const bool bLoaded = FAssetDocumentSidecar::LoadJsonFile(SidecarPath, OutDocument, Error);
	Test->TestTrue(FString::Printf(TEXT("Loads sidecar JSON '%s'"), *SidecarPath), bLoaded);
	if (!bLoaded)
	{
		Test->AddError(Error);
	}
	return bLoaded;
}

bool ExpectSyncRegions(
	FAutomationTestBase* Test,
	const TSharedPtr<FJsonObject>& Document,
	const TArray<FString>& ExpectedRegionIds)
{
	const TSharedPtr<FJsonObject>* Meta = nullptr;
	const TSharedPtr<FJsonObject>* Sync = nullptr;
	const TSharedPtr<FJsonObject>* Regions = nullptr;
	Test->TestTrue(TEXT("ApplyFile sidecar includes _meta"), Document.IsValid() && Document->TryGetObjectField(TEXT("_meta"), Meta));
	Test->TestTrue(TEXT("ApplyFile sidecar includes _meta.sync"), Meta && Meta->IsValid() && (*Meta)->TryGetObjectField(TEXT("sync"), Sync));
	Test->TestTrue(TEXT("ApplyFile sidecar includes _meta.sync.regions"), Sync && Sync->IsValid() && (*Sync)->TryGetObjectField(TEXT("regions"), Regions));
	if (!Regions || !Regions->IsValid())
	{
		return false;
	}

	for (const FString& RegionId : ExpectedRegionIds)
	{
		const TSharedPtr<FJsonObject>* Region = nullptr;
		Test->TestTrue(FString::Printf(TEXT("Sync state includes %s"), *RegionId), (*Regions)->TryGetObjectField(RegionId, Region));
		if (!Region || !Region->IsValid())
		{
			continue;
		}

		FString SidecarHash;
		FString AssetEvidenceHash;
		Test->TestTrue(FString::Printf(TEXT("%s sidecarHash exists"), *RegionId), (*Region)->TryGetStringField(TEXT("sidecarHash"), SidecarHash));
		Test->TestTrue(FString::Printf(TEXT("%s assetEvidenceHash exists"), *RegionId), (*Region)->TryGetStringField(TEXT("assetEvidenceHash"), AssetEvidenceHash));
		Test->TestFalse(FString::Printf(TEXT("%s sidecarHash is non-empty"), *RegionId), SidecarHash.IsEmpty());
		Test->TestFalse(FString::Printf(TEXT("%s assetEvidenceHash is non-empty"), *RegionId), AssetEvidenceHash.IsEmpty());
		Test->TestEqual(FString::Printf(TEXT("%s sync hashes match"), *RegionId), AssetEvidenceHash, SidecarHash);
	}
	return true;
}

bool ApplyTask10BlackboardFixture(
	FAutomationTestBase& Test,
	FAssetDocumentService& Service,
	const FString& ParentBlackboardTarget,
	const FString& BlackboardTarget)
{
	const FAssetDocumentResult ParentApply = Service.Apply(MakeApplyRequest(MakeBlackboardDataDocument(
		ParentBlackboardTarget,
		MakeBlackboardDataBody(nullptr, {
			MakeBlackboardKeyJson(TEXT("TargetActor"), TEXT("Object"), AActor::StaticClass()->GetPathName(), TEXT(""), TEXT(""), TEXT("Inherited target")),
			MakeBlackboardKeyJson(TEXT("HasTarget"), TEXT("Bool"),
				TEXT(""),
				TEXT(""),
				TEXT(""),
				TEXT("Inherited visibility gate")),
		}))));
	Test.TestTrue(TEXT("Task10 parent blackboard apply succeeds"), ParentApply.IsSuccess());
	if (!ParentApply.IsSuccess())
	{
		Test.AddError(ParentApply.Message);
		return false;
	}

	TSharedPtr<FJsonObject> LocalBody = MakeBlackboardDataBody(MakeAssetRef(MakeObjectPathFromTarget(ParentBlackboardTarget)), {
		MakeBlackboardKeyJson(TEXT("OtherTargetActor"), TEXT("Object"), AActor::StaticClass()->GetPathName()),
		MakeBlackboardKeyJson(TEXT("MoveLocation"), TEXT("Vector")),
	});
	const FAssetDocumentResult LocalApply = Service.Apply(MakeApplyRequest(MakeBlackboardDataDocument(BlackboardTarget, LocalBody)));
	Test.TestTrue(TEXT("Task10 local blackboard apply succeeds"), LocalApply.IsSuccess());
	if (!LocalApply.IsSuccess())
	{
		Test.AddError(LocalApply.Message);
	}
	return LocalApply.IsSuccess();
}

TSharedPtr<FJsonObject> MakeTask10EditorLayout()
{
	return MakeEditorLayout(
		{
			MakeObjectValue(MakeEditorLayoutNode(TEXT("RootSelector"), 0, 0)),
			MakeObjectValue(MakeEditorLayoutNode(TEXT("RootHasTarget"), -180, -160)),
			MakeObjectValue(MakeEditorLayoutNode(TEXT("FocusService"), -240, 120)),
			MakeObjectValue(MakeEditorLayoutNode(TEXT("HasTarget"), -120, 320)),
			MakeObjectValue(MakeEditorLayoutNode(TEXT("MoveToTarget"), 80, 420)),
			MakeObjectValue(MakeEditorLayoutNode(TEXT("RunSubtree"), 420, 420)),
		},
		{
			MakeObjectValue(MakeEditorLayoutComment(
				TEXT("55555555-6666-7777-8888-999999999999"),
				TEXT("Full BT+BB roundtrip"),
				-320,
				-220,
				900,
				760)),
		});
}

TSharedPtr<FJsonObject> MakeReflectedTask10NodeProperties(UClass* NodeClass, const TSharedRef<FJsonObject>& SparseProperties)
{
	UObject* Node = NodeClass ? NewObject<UObject>(GetTransientPackage(), NodeClass) : nullptr;
	if (!Node)
	{
		return SparseProperties;
	}

	const FAssetDocumentCapabilityResult ApplyResult =
		FAssetDocumentReflectedPropertyUtils::ApplyProperties(Node, SparseProperties, TEXT("/Properties"));
	if (!ApplyResult.bSuccess)
	{
		return SparseProperties;
	}

	TSharedRef<FJsonObject> ExtractedProperties = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ExtractResult =
		FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(Node, ExtractedProperties, TEXT("/Properties"));
	return ExtractResult.bSuccess ? ExtractedProperties : SparseProperties;
}

TSharedRef<FJsonObject> MakeTask10SparseNodeProperties(const FString& NodeName)
{
	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetStringField(TEXT("NodeName"), NodeName);
	return Properties;
}

TSharedPtr<FJsonObject> MakeTask10CompositeProperties(const FString& NodeName)
{
	return MakeReflectedTask10NodeProperties(UBTComposite_Selector::StaticClass(), MakeTask10SparseNodeProperties(NodeName));
}

TSharedPtr<FJsonObject> MakeTask10DefaultFocusProperties(const FString& NodeName)
{
	TSharedRef<FJsonObject> Properties = MakeTask10SparseNodeProperties(NodeName);
	Properties->SetNumberField(TEXT("Interval"), 1.25);
	Properties->SetObjectField(TEXT("BlackboardKey"), MakeSelectorProperty(TEXT("TargetActor")));
	return MakeReflectedTask10NodeProperties(UBTService_DefaultFocus::StaticClass(), Properties);
}

TSharedPtr<FJsonObject> MakeTask10BlackboardDecoratorProperties(const FString& NodeName)
{
	TSharedRef<FJsonObject> Properties = MakeTask10SparseNodeProperties(NodeName);
	Properties->SetObjectField(TEXT("BlackboardKey"), MakeSelectorProperty(TEXT("TargetActor")));
	return MakeReflectedTask10NodeProperties(UBTDecorator_Blackboard::StaticClass(), Properties);
}

TSharedPtr<FJsonObject> MakeTask10MoveToProperties(const FString& NodeName)
{
	TSharedRef<FJsonObject> Properties = MakeTask10SparseNodeProperties(NodeName);
	Properties->SetObjectField(TEXT("BlackboardKey"), MakeSelectorProperty(TEXT("TargetActor")));
	return MakeReflectedTask10NodeProperties(UBTTask_MoveTo::StaticClass(), Properties);
}

TSharedPtr<FJsonObject> MakeTask10RunBehaviorProperties(const FString& NodeName, const FString& SubtreeTarget)
{
	TSharedRef<FJsonObject> Properties = MakeTask10SparseNodeProperties(NodeName);
	Properties->SetObjectField(TEXT("BehaviorAsset"), MakeAssetRef(MakeObjectPathFromTarget(SubtreeTarget)));
	return MakeReflectedTask10NodeProperties(UBTTask_RunBehavior::StaticClass(), Properties);
}

TSharedPtr<FJsonObject> MakeTask10BehaviorTree(const FString& SubtreeTarget)
{
	TArray<TSharedPtr<FJsonValue>> Services;
	Services.Add(MakeObjectValue(MakeBtNode(
		TEXT("FocusService"),
		TEXT("/Script/AIModule.BTService_DefaultFocus"),
		MakeTask10DefaultFocusProperties(TEXT("FocusService")))));

	TArray<TSharedPtr<FJsonValue>> EdgeDecorators;
	EdgeDecorators.Add(MakeObjectValue(MakeBtNode(
		TEXT("HasTarget"),
		TEXT("/Script/AIModule.BTDecorator_Blackboard"),
		MakeTask10BlackboardDecoratorProperties(TEXT("HasTarget")))));

	TArray<TSharedPtr<FJsonValue>> EdgeLogic;
	EdgeLogic.Add(MakeObjectValue(MakeDecoratorLogicTest()));

	TSharedPtr<FJsonObject> MoveEdge = MakeShared<FJsonObject>();
	MoveEdge->SetObjectField(TEXT("Child"), MakeBtNode(
		TEXT("MoveToTarget"),
		TEXT("/Script/AIModule.BTTask_MoveTo"),
		MakeTask10MoveToProperties(TEXT("MoveToTarget"))));
	MoveEdge->SetArrayField(TEXT("Decorators"), EdgeDecorators);
	MoveEdge->SetArrayField(TEXT("DecoratorLogic"), EdgeLogic);

	TSharedPtr<FJsonObject> RunSubtreeEdge = MakeShared<FJsonObject>();
	RunSubtreeEdge->SetObjectField(TEXT("Child"), MakeBtNode(
		TEXT("RunSubtree"),
		TEXT("/Script/AIModule.BTTask_RunBehavior"),
		MakeTask10RunBehaviorProperties(TEXT("RunSubtree"), SubtreeTarget)));

	TArray<TSharedPtr<FJsonValue>> Children;
	Children.Add(MakeObjectValue(MoveEdge));
	Children.Add(MakeObjectValue(RunSubtreeEdge));

	TSharedPtr<FJsonObject> Root = MakeBtNode(
		TEXT("RootSelector"),
		TEXT("/Script/AIModule.BTComposite_Selector"),
		MakeTask10CompositeProperties(TEXT("RootSelector")));
	Root->SetArrayField(TEXT("Services"), Services);
	Root->SetArrayField(TEXT("Children"), Children);

	TArray<TSharedPtr<FJsonValue>> RootDecorators;
	RootDecorators.Add(MakeObjectValue(MakeBtNode(
		TEXT("RootHasTarget"),
		TEXT("/Script/AIModule.BTDecorator_Blackboard"),
		MakeTask10BlackboardDecoratorProperties(TEXT("RootHasTarget")))));

	TArray<TSharedPtr<FJsonValue>> RootLogic;
	RootLogic.Add(MakeObjectValue(MakeDecoratorLogicTest()));

	TSharedPtr<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetObjectField(TEXT("Root"), Root);
	Tree->SetArrayField(TEXT("RootDecorators"), RootDecorators);
	Tree->SetArrayField(TEXT("RootDecoratorLogic"), RootLogic);
	return Tree;
}

TSharedPtr<FJsonObject> MakeTask10BehaviorTreeBody(
	const FString& BlackboardTarget,
	const FString& SubtreeTarget)
{
	return MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		MakeTask10BehaviorTree(SubtreeTarget),
		MakeTask10EditorLayout());
}

bool PrepareTask10BehaviorTreeFixture(
	FAutomationTestBase& Test,
	FAssetDocumentService& Service,
	const FString& Target,
	FString& OutParentBlackboardTarget,
	FString& OutBlackboardTarget,
	FString& OutSubtreeTarget)
{
	OutParentBlackboardTarget = Target + TEXT("_ParentBB");
	OutBlackboardTarget = Target + TEXT("_BB");
	OutSubtreeTarget = Target + TEXT("_Subtree");
	if (!ApplyTask10BlackboardFixture(Test, Service, OutParentBlackboardTarget, OutBlackboardTarget))
	{
		return false;
	}

	UBehaviorTree* Subtree = MakeExistingBehaviorTreeAsset(OutSubtreeTarget);
	Test.TestNotNull(TEXT("Task10 subtree asset exists"), Subtree);
	if (!Subtree)
	{
		return false;
	}

	Subtree->BlackboardAsset = LoadObject<UBlackboardData>(nullptr, *MakeObjectPathFromTarget(OutBlackboardTarget));
	Test.TestNotNull(TEXT("Task10 subtree blackboard resolves"), Subtree->BlackboardAsset.Get());
	return Subtree->BlackboardAsset != nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeProfileShapeTest,
	"AssetFactory.AssetDocument.BehaviorTree.ProfileShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeProfileShapeTest::RunTest(const FString&)
{
	TSharedPtr<IAssetDocumentProfile> RegisteredProfile = FAssetDocumentService::GetProfileRegistry().FindForClass(UBehaviorTree::StaticClass());
	TestTrue(TEXT("BehaviorTree profile is registered"), RegisteredProfile.IsValid());
	if (!RegisteredProfile.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("Exact class is UBehaviorTree"), RegisteredProfile->GetExactClass(), UBehaviorTree::StaticClass());

	FAssetDocumentTemplateContext TemplateContext;
	TemplateContext.Target = TEXT("/Game/AssetDocumentTests/BT_ProfileShape");
	TemplateContext.ClassPath = TEXT("/Script/AIModule.BehaviorTree");
	const TSharedRef<FJsonObject> Template = RegisteredProfile->CreateTemplate(TemplateContext);
	TestEqual(TEXT("Template class is BehaviorTree"), Template->GetStringField(TEXT("Class")), FString(TEXT("/Script/AIModule.BehaviorTree")));

	const TSharedPtr<FJsonObject>* Body = nullptr;
	TestTrue(TEXT("Template contains Body"), Template->TryGetObjectField(TEXT("Body"), Body) && Body && Body->IsValid());
	if (Body && Body->IsValid())
	{
		TestTrue(TEXT("Body contains Blackboard"), (*Body)->HasField(TEXT("Blackboard")));
		TestTrue(TEXT("Body contains Tree"), (*Body)->HasField(TEXT("Tree")));
		TestTrue(TEXT("Body contains EditorLayout"), (*Body)->HasField(TEXT("EditorLayout")));
		TestFalse(TEXT("Body does not contain BlackboardInline"), (*Body)->HasField(TEXT("BlackboardInline")));

		const TSharedPtr<FJsonObject>* Tree = nullptr;
		TestTrue(TEXT("Tree is an object"), (*Body)->TryGetObjectField(TEXT("Tree"), Tree) && Tree && Tree->IsValid());
		if (Tree && Tree->IsValid())
		{
			TestTrue(TEXT("Tree contains RootDecorators"), (*Tree)->HasField(TEXT("RootDecorators")));
			TestTrue(TEXT("Tree contains RootDecoratorLogic"), (*Tree)->HasField(TEXT("RootDecoratorLogic")));
			TestTrue(TEXT("Tree contains Root"), (*Tree)->HasField(TEXT("Root")));
		}
	}

	const TArray<FName> BodyKeys = RegisteredProfile->GetBodyKeys();
	TestTrue(TEXT("Blackboard body key is registered"), BodyKeys.Contains(TEXT("Blackboard")));
	TestTrue(TEXT("Tree body key is registered"), BodyKeys.Contains(TEXT("Tree")));
	TestTrue(TEXT("EditorLayout body key is registered"), BodyKeys.Contains(TEXT("EditorLayout")));
	TestFalse(TEXT("BlackboardInline body key is not registered"), BodyKeys.Contains(TEXT("BlackboardInline")));
	TestNotNull(TEXT("Body root resolves adapter"), RegisteredProfile->ResolveBodyAdapter(TEXT("Body")));
	TestNotNull(TEXT("Blackboard resolves adapter"), RegisteredProfile->ResolveBodyAdapter(TEXT("Blackboard")));
	TestNotNull(TEXT("Tree resolves adapter"), RegisteredProfile->ResolveBodyAdapter(TEXT("Tree")));
	TestNotNull(TEXT("EditorLayout resolves adapter"), RegisteredProfile->ResolveBodyAdapter(TEXT("EditorLayout")));
	TestNull(TEXT("BlackboardInline does not resolve adapter"), RegisteredProfile->ResolveBodyAdapter(TEXT("BlackboardInline")));

	const TArray<FAssetDocumentRegionPolicy> Policies = RegisteredProfile->GetRegionPolicies();
	TestNotNull(TEXT("Policy includes Body.Blackboard"), FindPolicyByRegionId(Policies, TEXT("Body.Blackboard")));
	TestNotNull(TEXT("Policy includes Body.Tree"), FindPolicyByRegionId(Policies, TEXT("Body.Tree")));
	TestNotNull(TEXT("Policy includes Body.EditorLayout"), FindPolicyByRegionId(Policies, TEXT("Body.EditorLayout")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeBlackboardReferenceTest,
	"AssetFactory.AssetDocument.BehaviorTree.BlackboardReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeBlackboardReferenceTest::RunTest(const FString&)
{
	const FString BlackboardTarget = TEXT("/Game/AssetDocumentTests/BB_BT_BlackboardReference");
	const FString BehaviorTreeTarget = TEXT("/Game/AssetDocumentTests/BT_AD_BlackboardReference");
	UBlackboardData* Blackboard = MakeExistingBlackboardAsset(BlackboardTarget);
	TestNotNull(TEXT("Referenced blackboard asset exists"), Blackboard);
	if (!Blackboard)
	{
		return false;
	}

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)), MakeEmptyBehaviorTree(), MakeShared<FJsonObject>());
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(BehaviorTreeTarget, Body)));
	TestTrue(TEXT("BehaviorTree apply with Blackboard AssetRef succeeds"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(BehaviorTreeTarget);
	TestNotNull(TEXT("BehaviorTree lifecycle creates an asset"), BehaviorTree);
	if (!BehaviorTree)
	{
		return false;
	}
	TestTrue(TEXT("BehaviorTree has exact class"), BehaviorTree->GetClass() == UBehaviorTree::StaticClass());
	TestTrue(TEXT("BlackboardAsset is assigned"), BehaviorTree->BlackboardAsset == Blackboard);

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = BehaviorTreeTarget;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("BehaviorTree extract succeeds"), ExtractResult.IsSuccess());
	const TSharedPtr<FJsonObject>* ExtractedBody = nullptr;
	const TSharedPtr<FJsonObject>* ExtractedBlackboard = nullptr;
	TestTrue(TEXT("Extracted payload contains Body"), ExtractResult.Payload.IsValid() && ExtractResult.Payload->TryGetObjectField(TEXT("Body"), ExtractedBody) && ExtractedBody && ExtractedBody->IsValid());
	TestTrue(TEXT("Extracted Body contains Blackboard AssetRef"), ExtractedBody && (*ExtractedBody)->TryGetObjectField(TEXT("Blackboard"), ExtractedBlackboard) && ExtractedBlackboard && ExtractedBlackboard->IsValid());
	if (ExtractedBlackboard && ExtractedBlackboard->IsValid())
	{
		TestEqual(TEXT("Extracted Blackboard Kind"), (*ExtractedBlackboard)->GetStringField(TEXT("Kind")), FString(TEXT("AssetRef")));
		TestEqual(TEXT("Extracted Blackboard Path"), (*ExtractedBlackboard)->GetStringField(TEXT("Path")), Blackboard->GetPathName());
	}

	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(MakeBehaviorTreeDocument(BehaviorTreeTarget, Body)));
	TestTrue(TEXT("BehaviorTree diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("Blackboard diff path is stable"), DiffPayloadHasEntry(DiffResult.Payload, TEXT("unchanged"), TEXT("/Body/Blackboard")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeRejectsBlackboardInlineTest,
	"AssetFactory.AssetDocument.BehaviorTree.RejectsBlackboardInline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeRejectsBlackboardInlineTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(nullptr, MakeEmptyBehaviorTree(), MakeShared<FJsonObject>());
	Body->SetObjectField(TEXT("BlackboardInline"), MakeShared<FJsonObject>());

	FAssetDocumentValidateRequest Request;
	Request.Document = MakeBehaviorTreeDocument(TEXT("/Game/AssetDocumentTests/BT_AD_RejectsBlackboardInline"), Body);
	const FAssetDocumentResult Result = FAssetDocumentService().Validate(Request);
	TestFalse(TEXT("BehaviorTree rejects BlackboardInline"), Result.IsSuccess());
	TestTrue(TEXT("BlackboardInline reports unknown region"), ResultHasDiagnostic(Result, TEXT("UnknownBodyKey"), TEXT("/Body/BlackboardInline")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeRejectsMissingBlackboardForKeySelectorsTest,
	"AssetFactory.AssetDocument.BehaviorTree.RejectsMissingBlackboardForKeySelectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeRejectsMissingBlackboardForKeySelectorsTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(nullptr, MakeTreeWithKeySelectorLikeProperty(), MakeShared<FJsonObject>());

	FAssetDocumentValidateRequest Request;
	Request.Document = MakeBehaviorTreeDocument(TEXT("/Game/AssetDocumentTests/BT_AD_RejectsMissingBlackboardForKeySelectors"), Body);
	const FAssetDocumentResult Result = FAssetDocumentService().Validate(Request);
	TestFalse(TEXT("BehaviorTree rejects key selectors without Blackboard"), Result.IsSuccess());
	TestTrue(TEXT("Missing blackboard diagnostic is stable"), ResultHasDiagnostic(Result, TEXT("MissingBehaviorTreeBlackboard"), TEXT("/Body/Blackboard")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeReflectedPropertiesTest,
	"AssetFactory.AssetDocument.BehaviorTree.ReflectedProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeReflectedPropertiesTest::RunTest(const FString&)
{
	UTestDataAsset* ScalarObject = NewObject<UTestDataAsset>(GetTransientPackage());
	TSharedRef<FJsonObject> ScalarProperties = MakeObject();
	ScalarProperties->SetStringField(TEXT("TestString"), TEXT("Scout"));
	ScalarProperties->SetNumberField(TEXT("TestInt"), 7);
	ScalarProperties->SetNumberField(TEXT("TestFloat"), 2.5);
	ScalarProperties->SetBoolField(TEXT("bTestBool"), true);
	ScalarProperties->SetStringField(TEXT("TestName"), TEXT("PatrolKey"));
	ScalarProperties->SetStringField(TEXT("TestText"), TEXT("Visible text"));

	FAssetDocumentCapabilityResult ScalarApply = FAssetDocumentReflectedPropertyUtils::ApplyProperties(ScalarObject, ScalarProperties, TEXT("/Properties"));
	TestTrue(TEXT("scalar reflected apply succeeds"), ScalarApply.bSuccess);
	TestEqual(TEXT("string applied"), ScalarObject->TestString, FString(TEXT("Scout")));
	TestEqual(TEXT("int applied"), ScalarObject->TestInt, 7);
	TestTrue(TEXT("float applied"), FMath::IsNearlyEqual(ScalarObject->TestFloat, 2.5f));
	TestTrue(TEXT("bool applied"), ScalarObject->bTestBool);
	TestEqual(TEXT("name applied"), ScalarObject->TestName, FName(TEXT("PatrolKey")));
	TestEqual(TEXT("text applied"), ScalarObject->TestText.ToString(), FString(TEXT("Visible text")));

	TSharedRef<FJsonObject> ExtractedScalars = MakeObject();
	FAssetDocumentCapabilityResult ScalarExtract = FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(ScalarObject, ExtractedScalars, TEXT("/Properties"));
	TestTrue(TEXT("scalar reflected extract succeeds"), ScalarExtract.bSuccess);
	TestEqual(TEXT("string extracted"), ExtractedScalars->GetStringField(TEXT("TestString")), FString(TEXT("Scout")));
	TestEqual(TEXT("name extracted"), ExtractedScalars->GetStringField(TEXT("TestName")), FString(TEXT("PatrolKey")));
	TestEqual(TEXT("text extracted"), ExtractedScalars->GetStringField(TEXT("TestText")), FString(TEXT("Visible text")));

	UBTDecorator_Blackboard* Decorator = NewObject<UBTDecorator_Blackboard>(GetTransientPackage());
	TSharedRef<FJsonObject> EnumProperties = MakeObject();
	EnumProperties->SetStringField(TEXT("FlowAbortMode"), TEXT("Both"));
	FAssetDocumentCapabilityResult EnumApply = FAssetDocumentReflectedPropertyUtils::ApplyProperties(Decorator, EnumProperties, TEXT("/Properties"));
	TestTrue(TEXT("enum reflected apply succeeds"), EnumApply.bSuccess);
	TSharedRef<FJsonObject> ExtractedEnum = MakeObject();
	TestTrue(TEXT("enum reflected extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(Decorator, ExtractedEnum, TEXT("/Properties")).bSuccess);
	TestEqual(TEXT("enum extracted"), ExtractedEnum->GetStringField(TEXT("FlowAbortMode")), FString(TEXT("Both")));

	UBehaviorTree* ReferencedTree = NewObject<UBehaviorTree>(GetTransientPackage(), TEXT("BT_ReflectedPropertyReference"));
	UBTTask_RunBehavior* ObjectRefTask = NewObject<UBTTask_RunBehavior>(GetTransientPackage());
	TSharedRef<FJsonObject> ObjectRefProperties = MakeObject();
	ObjectRefProperties->SetObjectField(TEXT("BehaviorAsset"), MakeAssetRef(ReferencedTree->GetPathName()));
	TestTrue(TEXT("AssetRef object apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(ObjectRefTask, ObjectRefProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedObjectRef = MakeObject();
	TestTrue(TEXT("AssetRef object extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(ObjectRefTask, ExtractedObjectRef, TEXT("/Properties")).bSuccess);
	TSharedPtr<FJsonObject> ExtractedBehaviorAsset = GetObjectField(ExtractedObjectRef, TEXT("BehaviorAsset"));
	TestTrue(TEXT("object reference extracts as AssetRef"), ExtractedBehaviorAsset.IsValid() && ExtractedBehaviorAsset->GetStringField(TEXT("Kind")) == TEXT("AssetRef"));

	TSharedRef<FJsonObject> ExtraAssetRefProperties = MakeObject();
	TSharedPtr<FJsonObject> ExtraAssetRef = MakeAssetRef(ReferencedTree->GetPathName());
	ExtraAssetRef->SetStringField(TEXT("Extra"), TEXT("invalid"));
	ExtraAssetRefProperties->SetObjectField(TEXT("BehaviorAsset"), ExtraAssetRef);
	FAssetDocumentCapabilityResult ExtraAssetRefResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(ObjectRefTask, ExtraAssetRefProperties, TEXT("/Properties"));
	TestFalse(TEXT("AssetRef rejects unknown object fields"), ExtraAssetRefResult.bSuccess);
	TestTrue(TEXT("AssetRef unknown field path/code is exact"), HasDiagnostic(ExtraAssetRefResult, TEXT("/Properties/BehaviorAsset/Extra"), TEXT("UnknownField")));

	UBTTask_RunBehavior* RawObjectRefTask = NewObject<UBTTask_RunBehavior>(GetTransientPackage());
	TSharedRef<FJsonObject> RawObjectRefProperties = MakeObject();
	RawObjectRefProperties->SetStringField(TEXT("BehaviorAsset"), ReferencedTree->GetPathName());
	TestTrue(TEXT("raw object path apply succeeds through shared setter"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(RawObjectRefTask, RawObjectRefProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedRawObjectRef = MakeObject();
	TestTrue(TEXT("raw object path extracts canonically"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(RawObjectRefTask, ExtractedRawObjectRef, TEXT("/Properties")).bSuccess);
	TestTrue(TEXT("raw object path canonicalizes to AssetRef"), GetObjectField(ExtractedRawObjectRef, TEXT("BehaviorAsset")).IsValid());

	UBTTask_SetKeyValueClass* ClassRefTask = NewObject<UBTTask_SetKeyValueClass>(GetTransientPackage());
	TSharedRef<FJsonObject> ClassRefProperties = MakeObject();
	ClassRefProperties->SetObjectField(TEXT("BaseClass"), MakeClassRef(TEXT("/Script/Engine.Actor")));
	TestTrue(TEXT("ClassRef apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(ClassRefTask, ClassRefProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedClassRef = MakeObject();
	TestTrue(TEXT("ClassRef extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(ClassRefTask, ExtractedClassRef, TEXT("/Properties")).bSuccess);
	TSharedPtr<FJsonObject> ExtractedBaseClass = GetObjectField(ExtractedClassRef, TEXT("BaseClass"));
	TestTrue(TEXT("class reference extracts as ClassRef"), ExtractedBaseClass.IsValid() && ExtractedBaseClass->GetStringField(TEXT("Kind")) == TEXT("ClassRef"));

	TSharedRef<FJsonObject> ExtraClassRefProperties = MakeObject();
	TSharedPtr<FJsonObject> ExtraClassRef = MakeClassRef(TEXT("/Script/Engine.Actor"));
	ExtraClassRef->SetStringField(TEXT("Extra"), TEXT("invalid"));
	ExtraClassRefProperties->SetObjectField(TEXT("BaseClass"), ExtraClassRef);
	FAssetDocumentCapabilityResult ExtraClassRefResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(ClassRefTask, ExtraClassRefProperties, TEXT("/Properties"));
	TestFalse(TEXT("ClassRef rejects unknown object fields"), ExtraClassRefResult.bSuccess);
	TestTrue(TEXT("ClassRef unknown field path/code is exact"), HasDiagnostic(ExtraClassRefResult, TEXT("/Properties/BaseClass/Extra"), TEXT("UnknownField")));

	UBTTask_SetKeyValueClass* NestedClassRefTask = NewObject<UBTTask_SetKeyValueClass>(GetTransientPackage());
	TSharedRef<FJsonObject> NestedClassRefProperties = MakeObject();
	TSharedPtr<FJsonObject> NestedValue = MakeShared<FJsonObject>();
	NestedValue->SetStringField(TEXT("DefaultValue"), TEXT("/Script/Engine.Actor"));
	NestedValue->SetStringField(TEXT("BaseClass"), TEXT("/Script/Engine.Actor"));
	NestedClassRefProperties->SetObjectField(TEXT("Value"), NestedValue);
	TestTrue(TEXT("nested struct raw class refs apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(NestedClassRefTask, NestedClassRefProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedNestedClassRef = MakeObject();
	TestTrue(TEXT("nested struct class refs extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(NestedClassRefTask, ExtractedNestedClassRef, TEXT("/Properties")).bSuccess);
	TSharedPtr<FJsonObject> ExtractedNestedValue = GetObjectField(ExtractedNestedClassRef, TEXT("Value"));
	TSharedPtr<FJsonObject> ExtractedNestedDefaultClass = GetObjectField(ExtractedNestedValue, TEXT("DefaultValue"));
	TSharedPtr<FJsonObject> ExtractedNestedBaseClass = GetObjectField(ExtractedNestedValue, TEXT("BaseClass"));
	TestTrue(TEXT("nested struct default class reference extracts as ClassRef"), ExtractedNestedDefaultClass.IsValid() && ExtractedNestedDefaultClass->GetStringField(TEXT("Kind")) == TEXT("ClassRef"));
	TestTrue(TEXT("nested struct base class reference extracts as ClassRef"), ExtractedNestedBaseClass.IsValid() && ExtractedNestedBaseClass->GetStringField(TEXT("Kind")) == TEXT("ClassRef"));
	UBTTask_SetKeyValueClass* NestedClassRefRoundtripTask = NewObject<UBTTask_SetKeyValueClass>(GetTransientPackage());
	TestTrue(TEXT("nested struct ClassRef object roundtrip apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(NestedClassRefRoundtripTask, ExtractedNestedClassRef, TEXT("/Properties")).bSuccess);

	UBTTask_SetKeyValueObject* NestedObjectRefTask = NewObject<UBTTask_SetKeyValueObject>(GetTransientPackage());
	TSharedRef<FJsonObject> NestedObjectRefProperties = MakeObject();
	TSharedPtr<FJsonObject> NestedObjectValue = MakeShared<FJsonObject>();
	NestedObjectValue->SetStringField(TEXT("DefaultValue"), ReferencedTree->GetPathName());
	NestedObjectValue->SetStringField(TEXT("BaseClass"), UBehaviorTree::StaticClass()->GetPathName());
	NestedObjectRefProperties->SetObjectField(TEXT("Value"), NestedObjectValue);
	TestTrue(TEXT("nested struct raw object refs apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(NestedObjectRefTask, NestedObjectRefProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedNestedObjectRef = MakeObject();
	TestTrue(TEXT("nested struct object refs extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(NestedObjectRefTask, ExtractedNestedObjectRef, TEXT("/Properties")).bSuccess);
	TSharedPtr<FJsonObject> ExtractedNestedObjectValue = GetObjectField(ExtractedNestedObjectRef, TEXT("Value"));
	TSharedPtr<FJsonObject> ExtractedNestedDefaultObject = GetObjectField(ExtractedNestedObjectValue, TEXT("DefaultValue"));
	TSharedPtr<FJsonObject> ExtractedNestedObjectBaseClass = GetObjectField(ExtractedNestedObjectValue, TEXT("BaseClass"));
	TestTrue(TEXT("nested struct default object reference extracts as AssetRef"), ExtractedNestedDefaultObject.IsValid() && ExtractedNestedDefaultObject->GetStringField(TEXT("Kind")) == TEXT("AssetRef"));
	TestTrue(TEXT("nested struct object base class reference extracts as ClassRef"), ExtractedNestedObjectBaseClass.IsValid() && ExtractedNestedObjectBaseClass->GetStringField(TEXT("Kind")) == TEXT("ClassRef"));
	UBTTask_SetKeyValueObject* NestedObjectRefRoundtripTask = NewObject<UBTTask_SetKeyValueObject>(GetTransientPackage());
	TestTrue(TEXT("nested struct AssetRef object roundtrip apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(NestedObjectRefRoundtripTask, ExtractedNestedObjectRef, TEXT("/Properties")).bSuccess);

	UBTTask_SetKeyValueStruct* StructValueTask = NewObject<UBTTask_SetKeyValueStruct>(GetTransientPackage());
	TSharedRef<FJsonObject> ExtractedStructValueTask = MakeObject();
	FAssetDocumentCapabilityResult InstancedStructExtractResult = FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(StructValueTask, ExtractedStructValueTask, TEXT("/Properties"));
	TestTrue(TEXT("empty FInstancedStruct extraction succeeds"), InstancedStructExtractResult.bSuccess);
	TSharedPtr<FJsonObject> ExtractedEmptyStructValue = GetObjectField(ExtractedStructValueTask, TEXT("Value"));
	const TSharedPtr<FJsonValue> ExtractedEmptyDefaultValue = ExtractedEmptyStructValue.IsValid()
		? ExtractedEmptyStructValue->TryGetField(TEXT("DefaultValue"))
		: nullptr;
	TestTrue(TEXT("empty FInstancedStruct extracts canonically as null"), ExtractedEmptyDefaultValue.IsValid() && ExtractedEmptyDefaultValue->Type == EJson::Null);

	TSharedRef<FJsonObject> InstancedStructProperties = MakeObject();
	TSharedPtr<FJsonObject> InstancedStructValue = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> InstancedStructDefaultValue = MakeShared<FJsonObject>();
	InstancedStructDefaultValue->SetStringField(TEXT("Struct"), FBlackboardEntry::StaticStruct()->GetPathName());
	TSharedPtr<FJsonObject> InstancedStructDefaultProperties = MakeShared<FJsonObject>();
	InstancedStructDefaultProperties->SetStringField(TEXT("EntryName"), TEXT("TargetActor"));
	InstancedStructDefaultProperties->SetBoolField(TEXT("bInstanceSynced"), true);
	InstancedStructDefaultValue->SetObjectField(TEXT("Properties"), InstancedStructDefaultProperties);
	InstancedStructValue->SetObjectField(TEXT("DefaultValue"), InstancedStructDefaultValue);
	InstancedStructProperties->SetObjectField(TEXT("Value"), InstancedStructValue);
	FAssetDocumentCapabilityResult InstancedStructValidateResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(StructValueTask, InstancedStructProperties, TEXT("/Properties"));
	TestTrue(TEXT("FInstancedStruct authored input validates"), InstancedStructValidateResult.bSuccess);
	TestTrue(TEXT("FInstancedStruct authored input applies"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(StructValueTask, InstancedStructProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedAppliedStructValueTask = MakeObject();
	TestTrue(TEXT("applied FInstancedStruct extracts"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(StructValueTask, ExtractedAppliedStructValueTask, TEXT("/Properties")).bSuccess);
	TSharedPtr<FJsonObject> ExtractedAppliedStructValue = GetObjectField(ExtractedAppliedStructValueTask, TEXT("Value"));
	TSharedPtr<FJsonObject> ExtractedAppliedDefaultValue = GetObjectField(ExtractedAppliedStructValue, TEXT("DefaultValue"));
	TestTrue(TEXT("FInstancedStruct preserves selected script struct"), ExtractedAppliedDefaultValue.IsValid() && ExtractedAppliedDefaultValue->GetStringField(TEXT("Struct")) == FBlackboardEntry::StaticStruct()->GetPathName());

	UBTTask_WaitBlackboardTime* SelectorTask = NewObject<UBTTask_WaitBlackboardTime>(GetTransientPackage());
	FStructProperty* SelectorProperty = FindFProperty<FStructProperty>(SelectorTask->GetClass(), TEXT("BlackboardKey"));
	TestNotNull(TEXT("BlackboardKey selector property exists"), SelectorProperty);
	if (SelectorProperty)
	{
		TSharedRef<FJsonObject> SelectorJson = MakeObject();
		SelectorJson->SetStringField(TEXT("Key"), TEXT("TargetActor"));
		void* SelectorPtr = SelectorProperty->ContainerPtrToValuePtr<void>(SelectorTask);
		TestTrue(TEXT("blackboard selector apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyBlackboardKeySelector(SelectorProperty, SelectorPtr, SelectorJson, TEXT("/Properties/BlackboardKey")).bSuccess);

		FBlackboardKeySelector* Selector = static_cast<FBlackboardKeySelector*>(SelectorPtr);
		UBlackboardKeyType_Object* ObjectFilter = NewObject<UBlackboardKeyType_Object>(SelectorTask);
		ObjectFilter->BaseClass = AActor::StaticClass();
		Selector->AllowedTypes = { ObjectFilter };
		FBoolProperty* NoneAllowedProperty = FindFProperty<FBoolProperty>(FBlackboardKeySelector::StaticStruct(), TEXT("bNoneIsAllowedValue"));
		const bool bNonePolicyBeforeRejectedWrite = NoneAllowedProperty
			? NoneAllowedProperty->GetPropertyValue(NoneAllowedProperty->ContainerPtrToValuePtr<void>(SelectorPtr))
			: false;

		TSharedPtr<FJsonValue> ExtractedSelectorValue;
		TestTrue(TEXT("blackboard selector extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractBlackboardKeySelector(SelectorProperty, SelectorPtr, ExtractedSelectorValue, TEXT("/Properties/BlackboardKey")).bSuccess);
		TSharedPtr<FJsonObject> ExtractedSelector = ExtractedSelectorValue.IsValid() ? ExtractedSelectorValue->AsObject() : nullptr;
		TestTrue(TEXT("blackboard selector extracts object"), ExtractedSelector.IsValid());
		if (ExtractedSelector.IsValid())
		{
			TestEqual(TEXT("blackboard selector key name"), ExtractedSelector->GetStringField(TEXT("Key")), FString(TEXT("TargetActor")));
			TestEqual(TEXT("blackboard selector extracts only authored Key"), ExtractedSelector->Values.Num(), 1);
		}

		TSharedRef<FJsonObject> LegacySelectorJson = MakeObject();
		LegacySelectorJson->SetStringField(TEXT("SelectedKeyName"), TEXT("OtherTargetActor"));
		FAssetDocumentCapabilityResult LegacySelectorResult = FAssetDocumentReflectedPropertyUtils::ApplyBlackboardKeySelector(SelectorProperty, SelectorPtr, LegacySelectorJson, TEXT("/Properties/BlackboardKey"));
		TestFalse(TEXT("legacy SelectedKeyName write is rejected"), LegacySelectorResult.bSuccess);
		TestTrue(TEXT("legacy SelectedKeyName reports read-only authored boundary"), HasDiagnostic(LegacySelectorResult, TEXT("/Properties/BlackboardKey/SelectedKeyName"), TEXT("NonAuthoredProperty")));

		TSharedRef<FJsonObject> InvalidFilterSelector = MakeObject();
		InvalidFilterSelector->SetStringField(TEXT("Key"), TEXT("TargetActor"));
		InvalidFilterSelector->SetArrayField(TEXT("AllowedTypes"), {});
		FAssetDocumentCapabilityResult InvalidFilterResult = FAssetDocumentReflectedPropertyUtils::ApplyBlackboardKeySelector(SelectorProperty, SelectorPtr, InvalidFilterSelector, TEXT("/Properties/BlackboardKey"));
		TestFalse(TEXT("selector AllowedTypes write is rejected"), InvalidFilterResult.bSuccess);
		TestTrue(TEXT("selector AllowedTypes reports read-only authored boundary"), HasDiagnostic(InvalidFilterResult, TEXT("/Properties/BlackboardKey/AllowedTypes"), TEXT("NonAuthoredProperty")));
		TestEqual(TEXT("rejected selector write preserves allowed type policy"), Selector->AllowedTypes.Num(), 1);
		TestEqual(
			TEXT("rejected selector write preserves None policy"),
			NoneAllowedProperty ? NoneAllowedProperty->GetPropertyValue(NoneAllowedProperty->ContainerPtrToValuePtr<void>(SelectorPtr)) : false,
			bNonePolicyBeforeRejectedWrite);
	}

	ATestActorBase* ContainerObject = NewObject<ATestActorBase>(GetTransientPackage());
	TSharedRef<FJsonObject> ContainerProperties = MakeObject();
	TSharedPtr<FJsonObject> ConfigJson = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Entries;
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("Name"), TEXT("Damage"));
	Entry->SetNumberField(TEXT("Value"), 12.0);
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
	ConfigJson->SetArrayField(TEXT("Entries"), Entries);
	ContainerProperties->SetObjectField(TEXT("Config"), ConfigJson);
	TestTrue(TEXT("array property apply succeeds through shared setter"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(ContainerObject, ContainerProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedContainer = MakeObject();
	TestTrue(TEXT("array property extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(ContainerObject, ExtractedContainer, TEXT("/Properties")).bSuccess);
	TestTrue(TEXT("array property extracted"), GetObjectField(ExtractedContainer, TEXT("Config")).IsValid());

	UNodeMappingContainer* MapObject = NewObject<UNodeMappingContainer>(GetTransientPackage());
	TSharedRef<FJsonObject> MapProperties = MakeObject();
	TSharedPtr<FJsonObject> SourceToTarget = MakeShared<FJsonObject>();
	SourceToTarget->SetStringField(TEXT("SourceBone"), TEXT("TargetBone"));
	MapProperties->SetObjectField(TEXT("SourceToTarget"), SourceToTarget);
	TestTrue(TEXT("map property apply succeeds through shared setter"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(MapObject, MapProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedMap = MakeObject();
	TestTrue(TEXT("map property extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(MapObject, ExtractedMap, TEXT("/Properties")).bSuccess);
	TestTrue(TEXT("map property extracted"), GetObjectField(ExtractedMap, TEXT("SourceToTarget")).IsValid());

	UClass* PropertyEditorTestClass = LoadClass<UObject>(nullptr, TEXT("/Script/UnrealEd.PropertyEditorTestObject"));
	TestNotNull(TEXT("PropertyEditorTestObject class exists"), PropertyEditorTestClass);
	if (PropertyEditorTestClass)
	{
		UObject* UnsupportedMapKeyObject = NewObject<UObject>(GetTransientPackage(), PropertyEditorTestClass);
		TSharedRef<FJsonObject> UnsupportedMapKeyProperties = MakeObject();
		TSharedPtr<FJsonObject> UnsupportedMapKeyValue = MakeShared<FJsonObject>();
		UnsupportedMapKeyValue->SetStringField(TEXT("NotAStableKey"), TEXT("Value"));
		UnsupportedMapKeyProperties->SetObjectField(TEXT("LinearColorToStringMap"), UnsupportedMapKeyValue);
		FAssetDocumentCapabilityResult UnsupportedMapKeyResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(UnsupportedMapKeyObject, UnsupportedMapKeyProperties, TEXT("/Properties"));
		TestFalse(TEXT("unsupported map key type rejected"), UnsupportedMapKeyResult.bSuccess);
		TestTrue(TEXT("unsupported map key type path/code is exact"), HasDiagnostic(UnsupportedMapKeyResult, TEXT("/Properties/LinearColorToStringMap"), TEXT("UnsupportedProperty")));
	}

	UBlueprintEditorSettings* SetSettings = NewObject<UBlueprintEditorSettings>(GetTransientPackage());
	TSharedRef<FJsonObject> SetProperties = MakeObject();
	TArray<TSharedPtr<FJsonValue>> SetValues;
	SetValues.Add(MakeShared<FJsonValueString>(TEXT("Zulu")));
	SetValues.Add(MakeShared<FJsonValueString>(TEXT("Alpha")));
	SetProperties->SetArrayField(TEXT("TypePromotionPinDenyList"), SetValues);
	TestTrue(TEXT("set property apply succeeds through shared setter"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(SetSettings, SetProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedSetProperties = MakeObject();
	TestTrue(TEXT("set property extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(SetSettings, ExtractedSetProperties, TEXT("/Properties")).bSuccess);
	const TArray<TSharedPtr<FJsonValue>>* ExtractedSetValues = nullptr;
	TestTrue(TEXT("set property extracts stable array"), ExtractedSetProperties->TryGetArrayField(TEXT("TypePromotionPinDenyList"), ExtractedSetValues) && ExtractedSetValues && ExtractedSetValues->Num() == 2);
	if (ExtractedSetValues && ExtractedSetValues->Num() == 2)
	{
		FString FirstSetValue;
		FString SecondSetValue;
		(*ExtractedSetValues)[0]->TryGetString(FirstSetValue);
		(*ExtractedSetValues)[1]->TryGetString(SecondSetValue);
		TestEqual(TEXT("set extraction is canonically sorted first"), FirstSetValue, FString(TEXT("Alpha")));
		TestEqual(TEXT("set extraction is canonically sorted second"), SecondSetValue, FString(TEXT("Zulu")));
	}

	TSharedRef<FJsonObject> SameSetDifferentOrder = MakeObject();
	TArray<TSharedPtr<FJsonValue>> ReorderedSetValues;
	ReorderedSetValues.Add(MakeShared<FJsonValueString>(TEXT("Alpha")));
	ReorderedSetValues.Add(MakeShared<FJsonValueString>(TEXT("Zulu")));
	SameSetDifferentOrder->SetArrayField(TEXT("TypePromotionPinDenyList"), ReorderedSetValues);
	TArray<TSharedPtr<FJsonValue>> SetDiffEntries;
	TestTrue(TEXT("set diff succeeds"), FAssetDocumentReflectedPropertyUtils::DiffProperties(SetSettings, SameSetDifferentOrder, TEXT("/Properties"), SetDiffEntries).bSuccess);
	TestEqual(TEXT("set diff ignores element order"), SetDiffEntries.Num(), 0);

	TSharedRef<FJsonObject> UnknownProperties = MakeObject();
	UnknownProperties->SetStringField(TEXT("DoesNotExist"), TEXT("value"));
	FAssetDocumentCapabilityResult UnknownResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(ScalarObject, UnknownProperties, TEXT("/Properties"));
	TestFalse(TEXT("unknown property rejected"), UnknownResult.bSuccess);
	TestTrue(TEXT("unknown property path/code is exact"), HasDiagnostic(UnknownResult, TEXT("/Properties/DoesNotExist"), TEXT("UnknownProperty")));

	TSharedRef<FJsonObject> RuntimeProperties = MakeObject();
	RuntimeProperties->SetStringField(TEXT("ParentNode"), TEXT("invalid"));
	FAssetDocumentCapabilityResult RuntimeResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(Decorator, RuntimeProperties, TEXT("/Properties"));
	TestFalse(TEXT("non-authored property rejected"), RuntimeResult.bSuccess);
	TestTrue(TEXT("non-authored property path/code is exact"), HasDiagnostic(RuntimeResult, TEXT("/Properties/ParentNode"), TEXT("NonAuthoredProperty")));

	UMovieSceneCVarSection* UnsupportedObject = NewObject<UMovieSceneCVarSection>(GetTransientPackage());
	TSharedRef<FJsonObject> UnsupportedProperties = MakeObject();
	TArray<TSharedPtr<FJsonValue>> ConsoleVariableCollections;
	TSharedPtr<FJsonObject> UnsupportedCollection = MakeShared<FJsonObject>();
	UnsupportedCollection->SetField(TEXT("Interface"), MakeShared<FJsonValueNull>());
	ConsoleVariableCollections.Add(MakeShared<FJsonValueObject>(UnsupportedCollection));
	UnsupportedProperties->SetArrayField(TEXT("ConsoleVariableCollections"), ConsoleVariableCollections);
	FAssetDocumentCapabilityResult UnsupportedResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(UnsupportedObject, UnsupportedProperties, TEXT("/Properties"));
	TestFalse(TEXT("unsupported authored property rejected"), UnsupportedResult.bSuccess);
	TestTrue(TEXT("unsupported authored property path/code is exact"), HasDiagnostic(UnsupportedResult, TEXT("/Properties/ConsoleVariableCollections/0/Interface"), TEXT("UnsupportedProperty")));

	UBehaviorTree* TreeObject = NewObject<UBehaviorTree>(GetTransientPackage());
	TSharedRef<FJsonObject> TreeOwnedProperties = MakeObject();
	TreeOwnedProperties->SetField(TEXT("RootNode"), MakeShared<FJsonValueNull>());
	FAssetDocumentCapabilityResult TreeOwnedResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(TreeObject, TreeOwnedProperties, TEXT("/Properties"));
	TestFalse(TEXT("Body.Tree-owned property rejected"), TreeOwnedResult.bSuccess);
	TestTrue(TEXT("Body.Tree-owned property path/code is exact"), HasDiagnostic(TreeOwnedResult, TEXT("/Properties/RootNode"), TEXT("BodyTreeProperty")));

	TSharedRef<FJsonObject> DiffProperties = MakeObject();
	DiffProperties->SetStringField(TEXT("TestString"), TEXT("Changed"));
	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	TestTrue(TEXT("reflected diff succeeds"), FAssetDocumentReflectedPropertyUtils::DiffProperties(ScalarObject, DiffProperties, TEXT("/Properties"), DiffEntries).bSuccess);
	TestTrue(TEXT("reflected diff reports changed entry"), DiffEntries.Num() == 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeTreeTest,
	"AssetFactory.AssetDocument.BehaviorTree.Tree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeTreeTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_Tree");
	FAssetDocumentService Service;
	if (!ApplyTask7TreeFixture(*this, Service, Target))
	{
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	TestNotNull(TEXT("BehaviorTree exists after semantic tree apply"), BehaviorTree);
	UBTCompositeNode* Root = BehaviorTree ? BehaviorTree->RootNode : nullptr;
	TestNotNull(TEXT("semantic RootNode is materialized"), Root);
	if (!Root)
	{
		return false;
	}

	TestTrue(TEXT("root is selector class"), Root->GetClass()->GetPathName().Contains(TEXT("BTComposite_Selector")));
	TestEqual(TEXT("root services materialized"), Root->Services.Num(), 1);
	TestEqual(TEXT("root children materialized"), Root->Children.Num(), 2);
	UBTTaskNode* MoveToTask = Root->Children.IsValidIndex(0) ? Root->Children[0].ChildTask : nullptr;
	UBTTaskNode* RunSubtreeTask = Root->Children.IsValidIndex(1) ? Root->Children[1].ChildTask : nullptr;
	TestTrue(TEXT("first child is BTTask_MoveTo"), MoveToTask && MoveToTask->GetClass()->IsChildOf(UBTTask_MoveTo::StaticClass()));
	TestTrue(TEXT("second child is BTTask_RunBehavior"), RunSubtreeTask && RunSubtreeTask->GetClass()->IsChildOf(UBTTask_RunBehavior::StaticClass()));
	TestEqual(TEXT("edge decorator materialized"), Root->Children[0].Decorators.Num(), 1);

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("semantic tree extract succeeds"), ExtractResult.IsSuccess());
	const TSharedPtr<FJsonObject>* Body = nullptr;
	const TSharedPtr<FJsonObject>* Tree = nullptr;
	const TSharedPtr<FJsonObject>* ExtractedRoot = nullptr;
	TestTrue(TEXT("extract payload contains Body.Tree.Root"), ExtractResult.Payload.IsValid()
		&& ExtractResult.Payload->TryGetObjectField(TEXT("Body"), Body)
		&& Body && (*Body)->TryGetObjectField(TEXT("Tree"), Tree)
		&& Tree && (*Tree)->TryGetObjectField(TEXT("Root"), ExtractedRoot));
	if (ExtractedRoot && ExtractedRoot->IsValid())
	{
		TestEqual(TEXT("root id extracted"), (*ExtractedRoot)->GetStringField(TEXT("Id")), FString(TEXT("RootSelector")));
		TestEqual(TEXT("root class extracted as full path"), (*ExtractedRoot)->GetStringField(TEXT("Class")), FString(TEXT("/Script/AIModule.BTComposite_Selector")));
		const TArray<TSharedPtr<FJsonValue>>* ExtractedChildren = nullptr;
		TestTrue(TEXT("children extracted"), (*ExtractedRoot)->TryGetArrayField(TEXT("Children"), ExtractedChildren) && ExtractedChildren && ExtractedChildren->Num() == 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeEmptyTreeExtractTest,
	"AssetFactory.AssetDocument.BehaviorTree.EmptyTreeExtract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeEmptyTreeExtractTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_EmptyExtract");
	UBehaviorTree* EmptyTree = NewObject<UBehaviorTree>(
		CreatePackage(*Target),
		*FPackageName::GetLongPackageAssetName(Target),
		RF_Public | RF_Standalone | RF_Transactional);
	TestNotNull(TEXT("empty BehaviorTree asset exists"), EmptyTree);
	if (!EmptyTree)
	{
		return false;
	}

	FAssetDocumentService Service;
	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("empty BehaviorTree extract succeeds"), ExtractResult.IsSuccess());

	const TSharedPtr<FJsonObject>* Body = nullptr;
	const TSharedPtr<FJsonObject>* Tree = nullptr;
	const TSharedPtr<FJsonObject>* Root = nullptr;
	TestTrue(TEXT("empty extract payload contains Body.Tree.Root"), ExtractResult.Payload.IsValid()
		&& ExtractResult.Payload->TryGetObjectField(TEXT("Body"), Body)
		&& Body && (*Body)->TryGetObjectField(TEXT("Tree"), Tree)
		&& Tree && (*Tree)->TryGetObjectField(TEXT("Root"), Root));
	TestEqual(TEXT("empty root is strict empty object"), Root && Root->IsValid() ? (*Root)->Values.Num() : -1, 0);

	TSharedPtr<FJsonObject> BodyDocument = MakeBehaviorTreeBody(nullptr, MakeEmptyBehaviorTree(), MakeShared<FJsonObject>());
	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(MakeBehaviorTreeDocument(Target, BodyDocument)));
	TestTrue(TEXT("empty BehaviorTree diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("empty tree diff path is stable"), DiffPayloadHasAnyEntry(DiffResult.Payload, TEXT("/Body/Tree")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeRootDecoratorsTest,
	"AssetFactory.AssetDocument.BehaviorTree.RootDecorators",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeRootDecoratorsTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_RootDecorators");
	FAssetDocumentService Service;
	if (!ApplyTask7TreeFixture(*this, Service, Target, true))
	{
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	TestNotNull(TEXT("BehaviorTree exists"), BehaviorTree);
	TestEqual(TEXT("root decorators materialized"), BehaviorTree ? BehaviorTree->RootDecorators.Num() : 0, 1);
	TestEqual(TEXT("root decorator logic materialized"), BehaviorTree ? BehaviorTree->RootDecoratorOps.Num() : 0, 1);

	TSharedPtr<FJsonObject> DesiredBody = MakeBehaviorTreeBody(
		MakeAssetRef(BehaviorTree->BlackboardAsset->GetPathName()),
		MakeTask7BehaviorTree(Target + TEXT("_Subtree"), true, false),
		MakeShared<FJsonObject>());
	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(MakeBehaviorTreeDocument(Target, DesiredBody)));
	TestTrue(TEXT("root decorator diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("root decorator semantic path is emitted"), DiffPayloadHasAnyEntry(DiffResult.Payload, TEXT("/Body/Tree/RootDecorators/RootHasTarget")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeDecoratorLogicTest,
	"AssetFactory.AssetDocument.BehaviorTree.DecoratorLogic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeDecoratorLogicTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_DecoratorLogic");
	FAssetDocumentService Service;
	if (!ApplyTask7TreeFixture(*this, Service, Target, true))
	{
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	UBTCompositeNode* Root = BehaviorTree ? BehaviorTree->RootNode : nullptr;
	TestTrue(TEXT("edge decorator logic has Test op"), Root && Root->Children.Num() > 0 && Root->Children[0].DecoratorOps.Num() == 1 && Root->Children[0].DecoratorOps[0].Operation == EBTDecoratorLogic::Test);
	TestTrue(TEXT("root decorator logic has Test op"), BehaviorTree && BehaviorTree->RootDecoratorOps.Num() == 1 && BehaviorTree->RootDecoratorOps[0].Operation == EBTDecoratorLogic::Test);

	TSharedPtr<FJsonObject> DesiredTree = MakeTask7BehaviorTree(Target + TEXT("_Subtree"), true, false);
	TSharedPtr<FJsonObject> RootObject = GetObjectField(DesiredTree, TEXT("Root"));
	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (RootObject.IsValid() && RootObject->TryGetArrayField(TEXT("Children"), Children) && Children && Children->Num() > 0)
	{
		TSharedPtr<FJsonObject> FirstEdge = GetObjectFromValue((*Children)[0]);
		FirstEdge->SetArrayField(TEXT("DecoratorLogic"), TArray<TSharedPtr<FJsonValue>>());
	}

	TSharedPtr<FJsonObject> DesiredBody = MakeBehaviorTreeBody(
		MakeAssetRef(BehaviorTree->BlackboardAsset->GetPathName()),
		DesiredTree,
		MakeShared<FJsonObject>());
	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(MakeBehaviorTreeDocument(Target, DesiredBody)));
	TestTrue(TEXT("decorator logic diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("edge decorator logic path uses semantic node ids and index"), DiffPayloadHasAnyEntry(DiffResult.Payload, TEXT("/Body/Tree/RootSelector/Children/MoveToTarget/DecoratorLogic/0")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeDecoratorLogicRejectsInvalidShapeTest,
	"AssetFactory.AssetDocument.BehaviorTree.DecoratorLogicRejectsInvalidShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeDecoratorLogicRejectsInvalidShapeTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_InvalidDecoratorLogic");
	const FString BlackboardTarget = Target + TEXT("_BB");
	const FString SubtreeTarget = Target + TEXT("_Subtree");
	MakeTask7BlackboardAsset(BlackboardTarget);
	MakeExistingBehaviorTreeAsset(SubtreeTarget);

	TSharedPtr<FJsonObject> InvalidTree = MakeTask7BehaviorTree(SubtreeTarget, true, false);
	TSharedPtr<FJsonObject> RootObject = GetObjectField(InvalidTree, TEXT("Root"));
	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (RootObject.IsValid() && RootObject->TryGetArrayField(TEXT("Children"), Children) && Children && Children->Num() > 0)
	{
		TSharedPtr<FJsonObject> FirstEdge = GetObjectFromValue((*Children)[0]);
		TArray<TSharedPtr<FJsonValue>> InvalidLogic;
		InvalidLogic.Add(MakeObjectValue(MakeDecoratorLogicOp(TEXT("And"), 2)));
		FirstEdge->SetArrayField(TEXT("DecoratorLogic"), InvalidLogic);
	}

	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		InvalidTree,
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest AndRequest;
	AndRequest.Document = MakeBehaviorTreeDocument(Target, Body);
	const FAssetDocumentResult AndResult = FAssetDocumentService().Validate(AndRequest);
	TestFalse(TEXT("And without two child expressions is rejected"), AndResult.IsSuccess());
	TestTrue(TEXT("invalid And logic reports exact edge path"), ResultHasDiagnostic(AndResult, TEXT("InvalidBehaviorTreeDecoratorLogicShape"), TEXT("/Body/Tree/Root/Children/0/DecoratorLogic")));

	TSharedPtr<FJsonObject> OutOfBoundsTree = MakeTask7BehaviorTree(SubtreeTarget, true, false);
	TSharedPtr<FJsonObject> OutOfBoundsRoot = GetObjectField(OutOfBoundsTree, TEXT("Root"));
	if (OutOfBoundsRoot.IsValid() && OutOfBoundsRoot->TryGetArrayField(TEXT("Children"), Children) && Children && Children->Num() > 0)
	{
		TSharedPtr<FJsonObject> FirstEdge = GetObjectFromValue((*Children)[0]);
		TArray<TSharedPtr<FJsonValue>> InvalidLogic;
		InvalidLogic.Add(MakeObjectValue(MakeDecoratorLogicTest(1)));
		FirstEdge->SetArrayField(TEXT("DecoratorLogic"), InvalidLogic);
	}

	Body = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		OutOfBoundsTree,
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest TestRequest;
	TestRequest.Document = MakeBehaviorTreeDocument(Target, Body);
	const FAssetDocumentResult TestResult = FAssetDocumentService().Validate(TestRequest);
	TestFalse(TEXT("Test out of decorator bounds is rejected"), TestResult.IsSuccess());
	TestTrue(TEXT("invalid Test logic reports exact entry path"), ResultHasDiagnostic(TestResult, TEXT("InvalidBehaviorTreeDecoratorLogicShape"), TEXT("/Body/Tree/Root/Children/0/DecoratorLogic/0")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeRejectsNodeLevelDecoratorsTest,
	"AssetFactory.AssetDocument.BehaviorTree.RejectsNodeLevelDecorators",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeRejectsNodeLevelDecoratorsTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_NodeLevelDecorators");
	const FString BlackboardTarget = Target + TEXT("_BB");
	const FString SubtreeTarget = Target + TEXT("_Subtree");
	MakeTask7BlackboardAsset(BlackboardTarget);
	MakeExistingBehaviorTreeAsset(SubtreeTarget);

	TSharedPtr<FJsonObject> Tree = MakeTask7BehaviorTree(SubtreeTarget, false, false);
	TSharedPtr<FJsonObject> Root = GetObjectField(Tree, TEXT("Root"));
	if (Root.IsValid())
	{
		Root->SetArrayField(TEXT("Decorators"), TArray<TSharedPtr<FJsonValue>>());
	}
	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		Tree,
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest DecoratorsRequest;
	DecoratorsRequest.Document = MakeBehaviorTreeDocument(Target, Body);
	const FAssetDocumentResult DecoratorsResult = FAssetDocumentService().Validate(DecoratorsRequest);
	TestFalse(TEXT("node-level Decorators are rejected instead of ignored"), DecoratorsResult.IsSuccess());
	TestTrue(TEXT("node-level Decorators diagnostic is exact"), ResultHasDiagnostic(DecoratorsResult, TEXT("UnsupportedBehaviorTreeNodeDecorators"), TEXT("/Body/Tree/Root/Decorators")));

	Tree = MakeTask7BehaviorTree(SubtreeTarget, false, false);
	Root = GetObjectField(Tree, TEXT("Root"));
	if (Root.IsValid())
	{
		Root->SetArrayField(TEXT("DecoratorLogic"), TArray<TSharedPtr<FJsonValue>>());
	}
	Body = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		Tree,
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest LogicRequest;
	LogicRequest.Document = MakeBehaviorTreeDocument(Target, Body);
	const FAssetDocumentResult LogicResult = FAssetDocumentService().Validate(LogicRequest);
	TestFalse(TEXT("node-level DecoratorLogic is rejected instead of ignored"), LogicResult.IsSuccess());
	TestTrue(TEXT("node-level DecoratorLogic diagnostic is exact"), ResultHasDiagnostic(LogicResult, TEXT("UnsupportedBehaviorTreeNodeDecorators"), TEXT("/Body/Tree/Root/DecoratorLogic")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeSemanticDiffPathsTest,
	"AssetFactory.AssetDocument.BehaviorTree.SemanticDiffPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeSemanticDiffPathsTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_SemanticDiffPaths");
	FAssetDocumentService Service;
	if (!ApplyTask7TreeFixture(*this, Service, Target, true))
	{
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	TestNotNull(TEXT("BehaviorTree exists"), BehaviorTree);
	if (!BehaviorTree)
	{
		return false;
	}

	TSharedPtr<FJsonObject> DesiredTree = MakeTask7BehaviorTree(Target + TEXT("_Subtree"), true, false);
	TSharedPtr<FJsonObject> Root = GetObjectField(DesiredTree, TEXT("Root"));
	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (Root.IsValid() && Root->TryGetArrayField(TEXT("Children"), Children) && Children && Children->Num() > 0)
	{
		TSharedPtr<FJsonObject> FirstEdge = GetObjectFromValue((*Children)[0]);
		const TArray<TSharedPtr<FJsonValue>>* Decorators = nullptr;
		if (FirstEdge.IsValid() && FirstEdge->TryGetArrayField(TEXT("Decorators"), Decorators) && Decorators && Decorators->Num() > 0)
		{
			TSharedPtr<FJsonObject> Decorator = GetObjectFromValue((*Decorators)[0]);
			TSharedPtr<FJsonObject> Properties = GetObjectField(Decorator, TEXT("Properties"));
			TSharedPtr<FJsonObject> BlackboardKey = GetObjectField(Properties, TEXT("BlackboardKey"));
			if (BlackboardKey.IsValid())
			{
				BlackboardKey->SetStringField(TEXT("Key"), TEXT("OtherTargetActor"));
			}
		}
	}

	TSharedPtr<FJsonObject> DesiredBody = MakeBehaviorTreeBody(
		MakeAssetRef(BehaviorTree->BlackboardAsset->GetPathName()),
		DesiredTree,
		MakeShared<FJsonObject>());
	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(MakeBehaviorTreeDocument(Target, DesiredBody)));
	TestTrue(TEXT("semantic diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("edge decorator change path is precise"), DiffPayloadHasEntry(DiffResult.Payload, TEXT("changed"), TEXT("/Body/Tree/RootSelector/Children/MoveToTarget/Decorators/HasTarget")));
	TestFalse(TEXT("edge decorator change does not dirty parent node path"), DiffPayloadHasEntry(DiffResult.Payload, TEXT("changed"), TEXT("/Body/Tree/RootSelector")));
	TestFalse(TEXT("edge decorator change does not dirty edge path"), DiffPayloadHasEntry(DiffResult.Payload, TEXT("changed"), TEXT("/Body/Tree/RootSelector/Children/MoveToTarget")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeOrderDiffPathsTest,
	"AssetFactory.AssetDocument.BehaviorTree.OrderDiffPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeOrderDiffPathsTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_OrderDiffPaths");
	const FString BlackboardTarget = Target + TEXT("_BB");
	const FString SubtreeTarget = Target + TEXT("_Subtree");
	MakeTask7BlackboardAsset(BlackboardTarget);
	MakeExistingBehaviorTreeAsset(SubtreeTarget);

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> AppliedTree = MakeTask7BehaviorTree(SubtreeTarget, true, false);
	AddSecondDecoratorSet(AppliedTree);
	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		AppliedTree,
		MakeShared<FJsonObject>());
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, Body)));
	TestTrue(TEXT("order fixture apply succeeds"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	TSharedPtr<FJsonObject> DesiredTree = MakeTask7BehaviorTree(SubtreeTarget, true, false);
	AddSecondDecoratorSet(DesiredTree);
	TSharedPtr<FJsonObject> Root = GetObjectField(DesiredTree, TEXT("Root"));
	SwapArrayEntries(Root, TEXT("Children"), 0, 1);
	if (Root.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (Root->TryGetArrayField(TEXT("Children"), Children) && Children && Children->Num() > 1)
		{
			TSharedPtr<FJsonObject> MoveEdge = GetObjectFromValue((*Children)[1]);
			SwapArrayEntries(MoveEdge, TEXT("Decorators"), 0, 1);
		}
	}
	SwapArrayEntries(DesiredTree, TEXT("RootDecorators"), 0, 1);

	TSharedPtr<FJsonObject> DesiredBody = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		DesiredTree,
		MakeShared<FJsonObject>());
	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(MakeBehaviorTreeDocument(Target, DesiredBody)));
	TestTrue(TEXT("order diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("child order change is reported"), DiffPayloadHasEntry(DiffResult.Payload, TEXT("changed"), TEXT("/Body/Tree/RootSelector/Children")));
	TestTrue(TEXT("edge decorator order change is reported"), DiffPayloadHasEntry(DiffResult.Payload, TEXT("changed"), TEXT("/Body/Tree/RootSelector/Children/MoveToTarget/Decorators")));
	TestTrue(TEXT("root decorator order change is reported"), DiffPayloadHasEntry(DiffResult.Payload, TEXT("changed"), TEXT("/Body/Tree/RootDecorators")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeExtractsUniqueIdsForDuplicateDisplayNamesTest,
	"AssetFactory.AssetDocument.BehaviorTree.ExtractsUniqueIdsForDuplicateDisplayNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeExtractsUniqueIdsForDuplicateDisplayNamesTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_DuplicateDisplayNames");
	UBehaviorTree* BehaviorTree = MakeExistingBehaviorTreeAsset(Target);
	BehaviorTree->BlackboardAsset = MakeExistingBlackboardAsset(Target + TEXT("_BB"));
	TestNotNull(TEXT("manual BehaviorTree exists"), BehaviorTree);
	if (!BehaviorTree)
	{
		return false;
	}

	UClass* SelectorClass = LoadClass<UBTCompositeNode>(nullptr, TEXT("/Script/AIModule.BTComposite_Selector"));
	TestNotNull(TEXT("selector class exists"), SelectorClass);
	if (!SelectorClass)
	{
		return false;
	}

	UBTCompositeNode* Root = NewObject<UBTCompositeNode>(BehaviorTree, SelectorClass, NAME_None, RF_Transactional);
	Root->NodeName = TEXT("Duplicate");
	UBTService* FirstService = NewObject<UBTService>(BehaviorTree, UBTService_DefaultFocus::StaticClass(), NAME_None, RF_Transactional);
	UBTService* SecondService = NewObject<UBTService>(BehaviorTree, UBTService_DefaultFocus::StaticClass(), NAME_None, RF_Transactional);
	FirstService->NodeName = TEXT("Duplicate");
	SecondService->NodeName = TEXT("Duplicate");
	Root->Services = { FirstService, SecondService };

	UBTTaskNode* FirstTask = NewObject<UBTTaskNode>(BehaviorTree, UBTTask_MoveTo::StaticClass(), NAME_None, RF_Transactional);
	UBTTaskNode* SecondTask = NewObject<UBTTaskNode>(BehaviorTree, UBTTask_MoveTo::StaticClass(), NAME_None, RF_Transactional);
	FirstTask->NodeName = TEXT("Duplicate");
	SecondTask->NodeName = TEXT("Duplicate");

	FBTCompositeChild FirstChild;
	FirstChild.ChildTask = FirstTask;
	UBTDecorator* FirstDecorator = NewObject<UBTDecorator>(BehaviorTree, UBTDecorator_Blackboard::StaticClass(), NAME_None, RF_Transactional);
	UBTDecorator* SecondDecorator = NewObject<UBTDecorator>(BehaviorTree, UBTDecorator_Blackboard::StaticClass(), NAME_None, RF_Transactional);
	FirstDecorator->NodeName = TEXT("Duplicate");
	SecondDecorator->NodeName = TEXT("Duplicate");
	FirstChild.Decorators = { FirstDecorator, SecondDecorator };

	FBTCompositeChild SecondChild;
	SecondChild.ChildTask = SecondTask;
	Root->Children = { FirstChild, SecondChild };

	UBTDecorator* FirstRootDecorator = NewObject<UBTDecorator>(BehaviorTree, UBTDecorator_Blackboard::StaticClass(), NAME_None, RF_Transactional);
	UBTDecorator* SecondRootDecorator = NewObject<UBTDecorator>(BehaviorTree, UBTDecorator_Blackboard::StaticClass(), NAME_None, RF_Transactional);
	FirstRootDecorator->NodeName = TEXT("Duplicate");
	SecondRootDecorator->NodeName = TEXT("Duplicate");
	BehaviorTree->RootDecorators = { FirstRootDecorator, SecondRootDecorator };
	BehaviorTree->RootNode = Root;

	FAssetDocumentService Service;
	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult FirstExtract = Service.Extract(ExtractRequest);
	TestTrue(TEXT("duplicate display name extract succeeds"), FirstExtract.IsSuccess());
	const FAssetDocumentResult SecondExtract = Service.Extract(ExtractRequest);
	TestTrue(TEXT("duplicate display name extract is repeatable"), SecondExtract.IsSuccess());
	if (!FirstExtract.IsSuccess() || !SecondExtract.IsSuccess())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* FirstBody = nullptr;
	const TSharedPtr<FJsonObject>* FirstTree = nullptr;
	TestTrue(TEXT("first extract contains tree"), FirstExtract.Payload->TryGetObjectField(TEXT("Body"), FirstBody) && FirstBody && (*FirstBody)->TryGetObjectField(TEXT("Tree"), FirstTree));
	TArray<FString> FirstIds;
	TestTrue(TEXT("all extracted ids are unique"), CollectAllTreeIds(FirstTree ? *FirstTree : nullptr, FirstIds));
	TestTrue(TEXT("duplicate display names produce multiple ids"), FirstIds.Num() >= 9);

	const TSharedPtr<FJsonObject>* SecondBody = nullptr;
	const TSharedPtr<FJsonObject>* SecondTree = nullptr;
	TestTrue(TEXT("second extract contains tree"), SecondExtract.Payload->TryGetObjectField(TEXT("Body"), SecondBody) && SecondBody && (*SecondBody)->TryGetObjectField(TEXT("Tree"), SecondTree));
	const FString FirstTreeJson = FAssetDocumentJsonRegionUtils::JsonValueToComparableString(MakeShared<FJsonValueObject>(*FirstTree));
	const FString SecondTreeJson = FAssetDocumentJsonRegionUtils::JsonValueToComparableString(MakeShared<FJsonValueObject>(*SecondTree));
	TestEqual(TEXT("duplicate display name ids are stable across extracts"), SecondTreeJson, FirstTreeJson);

	TSharedPtr<FJsonObject> DiffBody = MakeBehaviorTreeBody(
		MakeAssetRef(BehaviorTree->BlackboardAsset->GetPathName()),
		*FirstTree,
		MakeShared<FJsonObject>());
	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(MakeBehaviorTreeDocument(Target, DiffBody)));
	TestTrue(TEXT("diff with extracted duplicate-display tree succeeds"), DiffResult.IsSuccess());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeEditorLayoutTest,
	"AssetFactory.AssetDocument.BehaviorTree.EditorLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeEditorLayoutTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task9_EditorLayout");
	const FString BlackboardTarget = Target + TEXT("_BB");
	const FString SubtreeTarget = Target + TEXT("_Subtree");
	MakeTask7BlackboardAsset(BlackboardTarget);
	MakeExistingBehaviorTreeAsset(SubtreeTarget);

	const FString CommentId = TEXT("11111111-2222-3333-4444-555555555555");
	TSharedPtr<FJsonObject> Layout = MakeEditorLayout(
		{
			MakeObjectValue(MakeEditorLayoutNode(TEXT("RootSelector"), 100, 200)),
			MakeObjectValue(MakeEditorLayoutNode(TEXT("MoveToTarget"), 300, 520)),
		},
		{
			MakeObjectValue(MakeEditorLayoutComment(CommentId, TEXT("Primary movement branch"), -40, 80, 640, 220)),
		});

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		MakeTask7BehaviorTree(SubtreeTarget, true, false),
		Layout);
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, Body)));
	TestTrue(TEXT("semantic tree and editor layout apply succeeds"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	UBehaviorTreeGraph* Graph = BehaviorTree ? Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph) : nullptr;
	TestNotNull(TEXT("BTGraph exists after layout apply"), Graph);
	if (!Graph)
	{
		return false;
	}

	bool bFoundRoot = false;
	bool bFoundMoveTo = false;
	bool bFoundComment = false;
	for (UEdGraphNode* GraphNode : Graph->Nodes)
	{
		if (UBehaviorTreeGraphNode* BTGraphNode = Cast<UBehaviorTreeGraphNode>(GraphNode))
		{
			const UBTNode* NodeInstance = Cast<UBTNode>(BTGraphNode->NodeInstance);
			if (NodeInstance && NodeInstance->NodeName == TEXT("RootSelector"))
			{
				bFoundRoot = true;
				TestEqual(TEXT("root X applied to graph node"), BTGraphNode->NodePosX, 100);
				TestEqual(TEXT("root Y applied to graph node"), BTGraphNode->NodePosY, 200);
			}
			if (NodeInstance && NodeInstance->NodeName == TEXT("MoveToTarget"))
			{
				bFoundMoveTo = true;
				TestEqual(TEXT("MoveTo X applied to graph node"), BTGraphNode->NodePosX, 300);
				TestEqual(TEXT("MoveTo Y applied to graph node"), BTGraphNode->NodePosY, 520);
			}
		}
		else if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(GraphNode))
		{
			bFoundComment = CommentNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens).Equals(CommentId, ESearchCase::IgnoreCase);
			if (bFoundComment)
			{
				TestEqual(TEXT("comment text applied"), CommentNode->NodeComment, FString(TEXT("Primary movement branch")));
				TestEqual(TEXT("comment X applied"), CommentNode->NodePosX, -40);
				TestEqual(TEXT("comment Y applied"), CommentNode->NodePosY, 80);
				TestEqual(TEXT("comment width applied"), CommentNode->NodeWidth, 640);
				TestEqual(TEXT("comment height applied"), CommentNode->NodeHeight, 220);
			}
		}
	}
	TestTrue(TEXT("RootSelector graph node found"), bFoundRoot);
	TestTrue(TEXT("MoveToTarget graph node found"), bFoundMoveTo);
	TestTrue(TEXT("layout comment graph node found"), bFoundComment);

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("editor layout extract succeeds"), ExtractResult.IsSuccess());

	const TSharedPtr<FJsonObject>* ExtractedBody = nullptr;
	const TSharedPtr<FJsonObject>* ExtractedLayout = nullptr;
	TestTrue(TEXT("extract payload contains Body.EditorLayout"), ExtractResult.Payload.IsValid()
		&& ExtractResult.Payload->TryGetObjectField(TEXT("Body"), ExtractedBody)
		&& ExtractedBody && (*ExtractedBody)->TryGetObjectField(TEXT("EditorLayout"), ExtractedLayout));
	if (ExtractedLayout && ExtractedLayout->IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* ExtractedNodes = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* ExtractedComments = nullptr;
		TestTrue(TEXT("layout nodes extracted"), (*ExtractedLayout)->TryGetArrayField(TEXT("Nodes"), ExtractedNodes) && ExtractedNodes && ExtractedNodes->Num() >= 2);
		TestTrue(TEXT("layout comments extracted"), (*ExtractedLayout)->TryGetArrayField(TEXT("Comments"), ExtractedComments) && ExtractedComments && ExtractedComments->Num() == 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeEditorLayoutSparsePreservesCommentsTest,
	"AssetFactory.AssetDocument.BehaviorTree.EditorLayoutSparsePreservesComments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeEditorLayoutSparsePreservesCommentsTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task9_SparseLayoutComments");
	const FString BlackboardTarget = Target + TEXT("_BB");
	const FString SubtreeTarget = Target + TEXT("_Subtree");
	MakeTask7BlackboardAsset(BlackboardTarget);
	MakeExistingBehaviorTreeAsset(SubtreeTarget);

	const FString CommentId = TEXT("22222222-3333-4444-5555-666666666666");
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> InitialBody = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		MakeTask7BehaviorTree(SubtreeTarget, true, false),
		MakeEditorLayout(
			{
				MakeObjectValue(MakeEditorLayoutNode(TEXT("RootSelector"), 100, 200)),
				MakeObjectValue(MakeEditorLayoutNode(TEXT("MoveToTarget"), 300, 520)),
			},
			{
				MakeObjectValue(MakeEditorLayoutComment(CommentId, TEXT("Persist me"), -80, 40, 420, 160)),
			}));
	const FAssetDocumentResult InitialApply = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, InitialBody)));
	TestTrue(TEXT("initial editor layout with comment applies"), InitialApply.IsSuccess());
	if (!InitialApply.IsSuccess())
	{
		AddError(InitialApply.Message);
		return false;
	}

	TSharedPtr<FJsonObject> SparseBody = MakeShared<FJsonObject>();
	SparseBody->SetObjectField(TEXT("EditorLayout"), MakeShared<FJsonObject>());
	const FAssetDocumentResult SparseApply = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, SparseBody)));
	TestTrue(TEXT("sparse EditorLayout apply succeeds"), SparseApply.IsSuccess());
	if (!SparseApply.IsSuccess())
	{
		AddError(SparseApply.Message);
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	UBehaviorTreeGraph* Graph = BehaviorTree ? Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph) : nullptr;
	UEdGraphNode_Comment* Comment = FindEditorLayoutComment(Graph, CommentId);
	TestNotNull(TEXT("sparse EditorLayout preserves existing comments"), Comment);
	if (Comment)
	{
		TestEqual(TEXT("preserved comment text"), Comment->NodeComment, FString(TEXT("Persist me")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeEditorLayoutExplicitEmptyCommentsDeletesTest,
	"AssetFactory.AssetDocument.BehaviorTree.EditorLayoutExplicitEmptyCommentsDeletes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeEditorLayoutExplicitEmptyCommentsDeletesTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task9_DeleteLayoutComments");
	const FString BlackboardTarget = Target + TEXT("_BB");
	const FString SubtreeTarget = Target + TEXT("_Subtree");
	MakeTask7BlackboardAsset(BlackboardTarget);
	MakeExistingBehaviorTreeAsset(SubtreeTarget);

	const FString CommentId = TEXT("44444444-5555-6666-7777-888888888888");
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> InitialBody = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		MakeTask7BehaviorTree(SubtreeTarget, true, false),
		MakeEditorLayout(
			{
				MakeObjectValue(MakeEditorLayoutNode(TEXT("RootSelector"), 100, 200)),
				MakeObjectValue(MakeEditorLayoutNode(TEXT("MoveToTarget"), 300, 520)),
			},
			{
				MakeObjectValue(MakeEditorLayoutComment(CommentId, TEXT("Delete me"), -40, 80, 640, 220)),
			}));
	const FAssetDocumentResult InitialApply = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, InitialBody)));
	TestTrue(TEXT("initial editor layout comment applies before explicit delete"), InitialApply.IsSuccess());
	if (!InitialApply.IsSuccess())
	{
		AddError(InitialApply.Message);
		return false;
	}

	TSharedPtr<FJsonObject> DeleteLayout = MakeShared<FJsonObject>();
	DeleteLayout->SetArrayField(TEXT("Comments"), TArray<TSharedPtr<FJsonValue>>());
	TSharedPtr<FJsonObject> DeleteBody = MakeShared<FJsonObject>();
	DeleteBody->SetObjectField(TEXT("EditorLayout"), DeleteLayout);
	const FAssetDocumentResult DeleteApply = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, DeleteBody)));
	TestTrue(TEXT("explicit empty Comments apply succeeds"), DeleteApply.IsSuccess());
	if (!DeleteApply.IsSuccess())
	{
		AddError(DeleteApply.Message);
		return false;
	}

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("extract after explicit comment delete succeeds"), ExtractResult.IsSuccess());

	const TSharedPtr<FJsonObject>* ExtractedBody = nullptr;
	const TSharedPtr<FJsonObject>* ExtractedLayout = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* ExtractedComments = nullptr;
	TestTrue(TEXT("extract contains empty Body.EditorLayout.Comments"), ExtractResult.Payload.IsValid()
		&& ExtractResult.Payload->TryGetObjectField(TEXT("Body"), ExtractedBody)
		&& ExtractedBody
		&& (*ExtractedBody)->TryGetObjectField(TEXT("EditorLayout"), ExtractedLayout)
		&& ExtractedLayout
		&& (*ExtractedLayout)->TryGetArrayField(TEXT("Comments"), ExtractedComments)
		&& ExtractedComments
		&& ExtractedComments->Num() == 0);

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	UBehaviorTreeGraph* Graph = BehaviorTree ? Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph) : nullptr;
	TestNull(TEXT("explicit empty Comments removes existing comment graph node"), FindEditorLayoutComment(Graph, CommentId));

	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(MakeBehaviorTreeDocument(Target, DeleteBody)));
	TestTrue(TEXT("diff after explicit empty Comments succeeds"), DiffResult.IsSuccess());
	if (!DiffResult.IsSuccess())
	{
		AddError(DiffResult.Message);
		return false;
	}
	TestFalse(TEXT("explicit comment deletion does not emit changed semantic tree entries"), DiffPayloadHasChangedPathPrefix(DiffResult.Payload, TEXT("/Body/Tree")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeEditorLayoutCommentColorOptionalDiffTest,
	"AssetFactory.AssetDocument.BehaviorTree.EditorLayoutCommentColorOptionalDiff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeEditorLayoutCommentColorOptionalDiffTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task9_OptionalCommentColor");
	const FString BlackboardTarget = Target + TEXT("_BB");
	const FString SubtreeTarget = Target + TEXT("_Subtree");
	MakeTask7BlackboardAsset(BlackboardTarget);
	MakeExistingBehaviorTreeAsset(SubtreeTarget);

	const FString CommentId = TEXT("33333333-4444-5555-6666-777777777777");
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Tree = MakeTask7BehaviorTree(SubtreeTarget, true, false);
	TSharedPtr<FJsonObject> LayoutWithoutColor = MakeEditorLayout(
		{
			MakeObjectValue(MakeEditorLayoutNode(TEXT("RootSelector"), 100, 200)),
			MakeObjectValue(MakeEditorLayoutNode(TEXT("MoveToTarget"), 300, 520)),
		},
		{
			MakeObjectValue(MakeEditorLayoutComment(CommentId, TEXT("No authored color"), -120, 60, 500, 180, false)),
		});
	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		Tree,
		LayoutWithoutColor);
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, Body)));
	TestTrue(TEXT("editor layout comment without Color applies"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	TSharedPtr<FJsonObject> DesiredBody = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		MakeTask7BehaviorTree(SubtreeTarget, true, false),
		LayoutWithoutColor);
	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(MakeBehaviorTreeDocument(Target, DesiredBody)));
	TestTrue(TEXT("diff for comment without Color succeeds"), DiffResult.IsSuccess());
	if (!DiffResult.IsSuccess())
	{
		AddError(DiffResult.Message);
		return false;
	}

	const FString CommentPath = FString::Printf(TEXT("/Body/EditorLayout/Comments/%s"), *CommentId);
	TestFalse(TEXT("omitted Color does not keep comment changed"), DiffPayloadHasChangedPathPrefix(DiffResult.Payload, CommentPath));
	TestFalse(TEXT("optional comment Color diff does not emit changed semantic tree entries"), DiffPayloadHasChangedPathPrefix(DiffResult.Payload, TEXT("/Body/Tree")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeEditorLayoutRejectsDanglingNodeTest,
	"AssetFactory.AssetDocument.BehaviorTree.EditorLayoutRejectsDanglingNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeEditorLayoutRejectsDanglingNodeTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task9_DanglingLayout");
	const FString BlackboardTarget = Target + TEXT("_BB");
	const FString SubtreeTarget = Target + TEXT("_Subtree");
	MakeTask7BlackboardAsset(BlackboardTarget);
	MakeExistingBehaviorTreeAsset(SubtreeTarget);

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		MakeTask7BehaviorTree(SubtreeTarget, true, false),
		MakeEditorLayout({MakeObjectValue(MakeEditorLayoutNode(TEXT("MissingNode"), 10, 20))}));
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, Body)));
	TestFalse(TEXT("dangling layout node apply is rejected"), ApplyResult.IsSuccess());
	TestTrue(TEXT("dangling layout diagnostic is exact"), ResultHasDiagnostic(ApplyResult, TEXT("UnknownEditorLayoutNode"), TEXT("/Body/EditorLayout/Nodes/MissingNode")));
	TestNull(TEXT("failed layout preflight does not create target BehaviorTree"), LoadBehaviorTreeForTarget(Target));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeEditorLayoutOnlyDiffTest,
	"AssetFactory.AssetDocument.BehaviorTree.EditorLayoutOnlyDiff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeEditorLayoutOnlyDiffTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task9_LayoutOnlyDiff");
	const FString BlackboardTarget = Target + TEXT("_BB");
	const FString SubtreeTarget = Target + TEXT("_Subtree");
	MakeTask7BlackboardAsset(BlackboardTarget);
	MakeExistingBehaviorTreeAsset(SubtreeTarget);

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> AppliedTree = MakeTask7BehaviorTree(SubtreeTarget, true, false);
	TSharedPtr<FJsonObject> AppliedLayout = MakeEditorLayout(
		{
			MakeObjectValue(MakeEditorLayoutNode(TEXT("RootSelector"), 100, 200)),
			MakeObjectValue(MakeEditorLayoutNode(TEXT("MoveToTarget"), 300, 520)),
		});
	TSharedPtr<FJsonObject> AppliedBody = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		AppliedTree,
		AppliedLayout);
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, AppliedBody)));
	TestTrue(TEXT("layout diff fixture apply succeeds"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	TSharedPtr<FJsonObject> DesiredTree = MakeTask7BehaviorTree(SubtreeTarget, true, false);
	TSharedPtr<FJsonObject> DesiredLayout = MakeEditorLayout(
		{
			MakeObjectValue(MakeEditorLayoutNode(TEXT("RootSelector"), 180, 260)),
			MakeObjectValue(MakeEditorLayoutNode(TEXT("MoveToTarget"), 300, 520)),
		});
	TSharedPtr<FJsonObject> DesiredBody = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		DesiredTree,
		DesiredLayout);
	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(MakeBehaviorTreeDocument(Target, DesiredBody)));
	TestTrue(TEXT("layout-only diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("layout node path changed"), DiffPayloadHasEntry(DiffResult.Payload, TEXT("changed"), TEXT("/Body/EditorLayout/Nodes/RootSelector")));
	if (DiffPayloadHasChangedPathPrefix(DiffResult.Payload, TEXT("/Body/Tree")))
	{
		AddDiffPathsWithPrefix(*this, DiffResult.Payload, TEXT("/Body/Tree"));
	}
	TestFalse(TEXT("layout-only diff does not emit changed semantic tree entries"), DiffPayloadHasChangedPathPrefix(DiffResult.Payload, TEXT("/Body/Tree")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeDynamicNodeClassesTest,
	"AssetFactory.AssetDocument.BehaviorTree.DynamicNodeClasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeDynamicNodeClassesTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_DynamicClasses");
	FAssetDocumentService Service;
	if (!ApplyTask7TreeFixture(*this, Service, Target, true, true))
	{
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	UBTCompositeNode* Root = BehaviorTree ? BehaviorTree->RootNode : nullptr;
	TestTrue(TEXT("short selector class resolved dynamically"), Root && Root->GetClass()->GetPathName() == TEXT("/Script/AIModule.BTComposite_Selector"));
	TestTrue(TEXT("short service class resolved dynamically"), Root && Root->Services.Num() == 1 && Root->Services[0]->GetClass()->IsChildOf(UBTService_DefaultFocus::StaticClass()));
	TestTrue(TEXT("short edge decorator class resolved dynamically"), Root && Root->Children.Num() > 0 && Root->Children[0].Decorators.Num() == 1 && Root->Children[0].Decorators[0]->GetClass()->IsChildOf(UBTDecorator_Blackboard::StaticClass()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeApplyFailureDoesNotMutateExistingTest,
	"AssetFactory.AssetDocument.BehaviorTree.ApplyFailureDoesNotMutateExisting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeApplyFailureDoesNotMutateExistingTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_FailureAtomicity");
	FAssetDocumentService Service;
	if (!ApplyTask7TreeFixture(*this, Service, Target, false))
	{
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	UBTCompositeNode* OriginalRoot = BehaviorTree ? BehaviorTree->RootNode : nullptr;
	const int32 OriginalChildren = OriginalRoot ? OriginalRoot->Children.Num() : -1;
	const int32 OriginalServices = OriginalRoot ? OriginalRoot->Services.Num() : -1;
	const int32 OriginalDecorators = OriginalRoot && OriginalRoot->Children.Num() > 0 ? OriginalRoot->Children[0].Decorators.Num() : -1;
	UBlackboardData* OriginalBlackboard = BehaviorTree ? BehaviorTree->BlackboardAsset : nullptr;
	TestNotNull(TEXT("original root exists"), OriginalRoot);

	TSharedPtr<FJsonObject> InvalidTree = MakeTask7BehaviorTree(Target + TEXT("_Subtree"), false, false);
	TSharedPtr<FJsonObject> InvalidRoot = GetObjectField(InvalidTree, TEXT("Root"));
	if (InvalidRoot.IsValid())
	{
		InvalidRoot->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BTTask_MoveTo"));
	}

	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(
		MakeAssetRef(BehaviorTree->BlackboardAsset->GetPathName()),
		InvalidTree,
		MakeShared<FJsonObject>());
	const FAssetDocumentResult FailedApply = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, Body)));
	TestFalse(TEXT("invalid root task apply fails"), FailedApply.IsSuccess());

	TestTrue(TEXT("RootNode pointer survives failed apply"), BehaviorTree->RootNode == OriginalRoot);
	TestEqual(TEXT("existing children survive failed apply"), OriginalRoot ? OriginalRoot->Children.Num() : -1, OriginalChildren);

	TSharedPtr<FJsonObject> LateInvalidTree = MakeTask7BehaviorTree(Target + TEXT("_Subtree"), false, false);
	TSharedPtr<FJsonObject> LateInvalidRoot = GetObjectField(LateInvalidTree, TEXT("Root"));
	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (LateInvalidRoot.IsValid() && LateInvalidRoot->TryGetArrayField(TEXT("Children"), Children) && Children && Children->Num() > 1)
	{
		TSharedPtr<FJsonObject> SecondEdge = GetObjectFromValue((*Children)[1]);
		TSharedPtr<FJsonObject> SecondChild = GetObjectField(SecondEdge, TEXT("Child"));
		TSharedPtr<FJsonObject> Properties = GetObjectField(SecondChild, TEXT("Properties"));
		if (!Properties.IsValid())
		{
			Properties = MakeShared<FJsonObject>();
			SecondChild->SetObjectField(TEXT("Properties"), Properties);
		}
		Properties->SetStringField(TEXT("ParentNode"), TEXT("invalid"));
	}

	TSharedPtr<FJsonObject> LateInvalidBody = MakeBehaviorTreeBody(
		nullptr,
		LateInvalidTree,
		MakeShared<FJsonObject>());
	const FAssetDocumentResult LateFailedApply = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, LateInvalidBody)));
	TestFalse(TEXT("late reflected property apply fails"), LateFailedApply.IsSuccess());
	TestTrue(TEXT("RootNode pointer survives late failed apply"), BehaviorTree->RootNode == OriginalRoot);
	TestEqual(TEXT("children survive late failed apply"), OriginalRoot ? OriginalRoot->Children.Num() : -1, OriginalChildren);
	TestEqual(TEXT("services survive late failed apply"), OriginalRoot ? OriginalRoot->Services.Num() : -1, OriginalServices);
	TestEqual(TEXT("decorators survive late failed apply"), OriginalRoot && OriginalRoot->Children.Num() > 0 ? OriginalRoot->Children[0].Decorators.Num() : -1, OriginalDecorators);
	TestTrue(TEXT("BlackboardAsset survives late failed apply"), BehaviorTree->BlackboardAsset == OriginalBlackboard);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeCrossRegionValidationUsesTransientOuterTest,
	"AssetFactory.AssetDocument.BehaviorTree.CrossRegionValidationUsesTransientOuter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeCrossRegionValidationUsesTransientOuterTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_CrossRegionTransientOuter");
	FAssetDocumentService Service;
	if (!ApplyTask7TreeFixture(*this, Service, Target, false))
	{
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	TestNotNull(TEXT("BehaviorTree exists before cross-region validation"), BehaviorTree);
	if (!BehaviorTree)
	{
		return false;
	}

	const int32 OriginalNodeChildren = CountBehaviorTreeNodeChildren(BehaviorTree);
	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(
		MakeAssetRef(BehaviorTree->BlackboardAsset->GetPathName()),
		MakeMoveToTreeWithKeys({TEXT("TargetActor")}),
		MakeShared<FJsonObject>());

	FAssetDocumentCapabilityContext Context;
	Context.Asset = BehaviorTree;
	Context.AssetClass = UBehaviorTree::StaticClass();
	Context.TargetAssetPath = Target;
	Context.bIsDryRun = true;
	const FAssetDocumentCapabilityResult ValidateResult =
		FBehaviorTreeAssetDocumentMaterializer::ValidateBodyCrossRegion(Context, Body.ToSharedRef());
	TestTrue(TEXT("cross-region validation succeeds"), ValidateResult.bSuccess);
	TestEqual(TEXT("cross-region validation does not create preview BT nodes under real asset"), CountBehaviorTreeNodeChildren(BehaviorTree), OriginalNodeChildren);

	TSharedPtr<FJsonObject> InvalidBody = MakeBehaviorTreeBody(
		MakeAssetRef(BehaviorTree->BlackboardAsset->GetPathName()),
		MakeMoveToTreeWithKeys({TEXT("MissingTarget")}),
		MakeShared<FJsonObject>());
	const FAssetDocumentResult FailedApply = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, InvalidBody)));
	TestFalse(TEXT("invalid key apply preflight fails"), FailedApply.IsSuccess());
	TestEqual(TEXT("failed apply preflight does not create preview BT nodes under real asset"), CountBehaviorTreeNodeChildren(BehaviorTree), OriginalNodeChildren);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeGraphFailureRollsBackSemanticTreeTest,
	"AssetFactory.AssetDocument.BehaviorTree.GraphFailureRollsBackSemanticTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeGraphFailureRollsBackSemanticTreeTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_GraphFailureRollback");
	FAssetDocumentService Service;
	if (!ApplyTask7TreeFixture(*this, Service, Target, false))
	{
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	UBTCompositeNode* OriginalRoot = BehaviorTree ? BehaviorTree->RootNode : nullptr;
	UBehaviorTreeGraph* OriginalGraph = BehaviorTree ? Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph) : nullptr;
	const int32 OriginalNodeChildren = CountBehaviorTreeNodeChildren(BehaviorTree);
	const int32 OriginalChildren = OriginalRoot ? OriginalRoot->Children.Num() : -1;
	const int32 OriginalServices = OriginalRoot ? OriginalRoot->Services.Num() : -1;
	TestNotNull(TEXT("original root exists before forced graph failure"), OriginalRoot);

	TSharedPtr<FJsonObject> ChangedBody = MakeBehaviorTreeBody(
		MakeAssetRef(BehaviorTree->BlackboardAsset->GetPathName()),
		MakeMoveToTreeWithKeys({TEXT("TargetActor")}),
		MakeShared<FJsonObject>());
	FBehaviorTreeAssetDocumentMaterializer::FailNextEditorGraphRebuildForTest();
	const FAssetDocumentResult FailedApply = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, ChangedBody)));
	TestFalse(TEXT("forced graph rebuild apply fails"), FailedApply.IsSuccess());
	TestTrue(TEXT("forced graph rebuild diagnostic is exact"), ResultHasDiagnostic(FailedApply, TEXT("ForcedBehaviorTreeEditorGraphRebuildFailure"), TEXT("/Body/EditorLayout")));
	TestTrue(TEXT("graph failure preserves root pointer"), BehaviorTree && BehaviorTree->RootNode == OriginalRoot);
	TestTrue(TEXT("graph failure preserves editor graph pointer"), BehaviorTree && Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph) == OriginalGraph);
	TestEqual(TEXT("graph failure preserves child count"), OriginalRoot ? OriginalRoot->Children.Num() : -1, OriginalChildren);
	TestEqual(TEXT("graph failure preserves service count"), OriginalRoot ? OriginalRoot->Services.Num() : -1, OriginalServices);
	TestEqual(TEXT("graph failure does not leave replacement BT nodes under real asset"), CountBehaviorTreeNodeChildren(BehaviorTree), OriginalNodeChildren);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeBlackboardKeySelectorsTest,
	"AssetFactory.AssetDocument.BehaviorTree.BlackboardKeySelectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeBlackboardKeySelectorsTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task8_KeySelectors");
	const FString ParentBlackboardTarget = Target + TEXT("_ParentBB");
	const FString BlackboardTarget = Target + TEXT("_BB");
	UBlackboardData* ParentBlackboard = MakeExistingBlackboardAsset(ParentBlackboardTarget);
	UBlackboardData* Blackboard = MakeExistingBlackboardAsset(BlackboardTarget);
	TestNotNull(TEXT("parent blackboard exists"), ParentBlackboard);
	TestNotNull(TEXT("child blackboard exists"), Blackboard);
	if (!ParentBlackboard || !Blackboard)
	{
		return false;
	}

	AddObjectBlackboardKey(ParentBlackboard, TEXT("TargetActor"));
	AddBlackboardKey<UBlackboardKeyType_Vector>(Blackboard, TEXT("MoveLocation"));
	Blackboard->Parent = ParentBlackboard;

	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		MakeMoveToTreeWithKeys({TEXT("TargetActor"), TEXT("MoveLocation")}),
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest Request;
	Request.Document = MakeBehaviorTreeDocument(Target, Body);
	const FAssetDocumentResult Result = FAssetDocumentService().Validate(Request);
	TestTrue(TEXT("selector keys resolve through local and parent blackboards"), Result.IsSuccess());
	if (!Result.IsSuccess())
	{
		AddError(Result.Message);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeSubtreeBlackboardCompatibilityTest,
	"AssetFactory.AssetDocument.BehaviorTree.SubtreeBlackboardCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeSubtreeBlackboardCompatibilityTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task8_SubtreeCompatibility");
	const FString ParentBlackboardTarget = Target + TEXT("_ParentBB");
	const FString ChildBlackboardTarget = Target + TEXT("_ChildBB");
	const FString IncompatibleBlackboardTarget = Target + TEXT("_IncompatibleBB");
	const FString ParentSubtreeTarget = Target + TEXT("_ParentSubtree");
	const FString SameSubtreeTarget = Target + TEXT("_SameSubtree");
	const FString ChildSubtreeTarget = Target + TEXT("_ChildSubtree");
	const FString IncompatibleSubtreeTarget = Target + TEXT("_IncompatibleSubtree");

	UBlackboardData* ParentBlackboard = MakeExistingBlackboardAsset(ParentBlackboardTarget);
	UBlackboardData* ChildBlackboard = MakeExistingBlackboardAsset(ChildBlackboardTarget);
	UBlackboardData* IncompatibleBlackboard = MakeExistingBlackboardAsset(IncompatibleBlackboardTarget);
	TestNotNull(TEXT("parent blackboard exists"), ParentBlackboard);
	TestNotNull(TEXT("child blackboard exists"), ChildBlackboard);
	TestNotNull(TEXT("incompatible blackboard exists"), IncompatibleBlackboard);
	if (!ParentBlackboard || !ChildBlackboard || !IncompatibleBlackboard)
	{
		return false;
	}

	AddObjectBlackboardKey(ParentBlackboard, TEXT("TargetActor"));
	AddBlackboardKey<UBlackboardKeyType_Vector>(ChildBlackboard, TEXT("MoveLocation"));
	AddBlackboardKey<UBlackboardKeyType_Bool>(IncompatibleBlackboard, TEXT("HasTarget"));
	ChildBlackboard->Parent = ParentBlackboard;

	UBehaviorTree* ParentSubtree = MakeExistingBehaviorTreeAsset(ParentSubtreeTarget);
	UBehaviorTree* SameSubtree = MakeExistingBehaviorTreeAsset(SameSubtreeTarget);
	UBehaviorTree* ChildSubtree = MakeExistingBehaviorTreeAsset(ChildSubtreeTarget);
	UBehaviorTree* IncompatibleSubtree = MakeExistingBehaviorTreeAsset(IncompatibleSubtreeTarget);
	TestNotNull(TEXT("parent subtree exists"), ParentSubtree);
	TestNotNull(TEXT("same subtree exists"), SameSubtree);
	TestNotNull(TEXT("child subtree exists"), ChildSubtree);
	TestNotNull(TEXT("incompatible subtree exists"), IncompatibleSubtree);
	if (!ParentSubtree || !SameSubtree || !ChildSubtree || !IncompatibleSubtree)
	{
		return false;
	}
	ParentSubtree->BlackboardAsset = ParentBlackboard;
	SameSubtree->BlackboardAsset = ParentBlackboard;
	ChildSubtree->BlackboardAsset = ChildBlackboard;
	IncompatibleSubtree->BlackboardAsset = IncompatibleBlackboard;

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> ParentUsesSameSubtreeBody = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(ParentBlackboardTarget)),
		MakeRunBehaviorTree(SameSubtreeTarget),
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest ParentUsesSameSubtreeRequest;
	ParentUsesSameSubtreeRequest.Document = MakeBehaviorTreeDocument(Target, ParentUsesSameSubtreeBody);
	const FAssetDocumentResult ParentUsesSameSubtreeResult = Service.Validate(ParentUsesSameSubtreeRequest);
	TestTrue(TEXT("parent blackboard can run same-blackboard subtree"), ParentUsesSameSubtreeResult.IsSuccess());
	if (!ParentUsesSameSubtreeResult.IsSuccess())
	{
		AddError(ParentUsesSameSubtreeResult.Message);
	}

	TSharedPtr<FJsonObject> ChildUsesParentSubtreeBody = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(ChildBlackboardTarget)),
		MakeRunBehaviorTree(ParentSubtreeTarget),
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest ChildUsesParentSubtreeRequest;
	ChildUsesParentSubtreeRequest.Document = MakeBehaviorTreeDocument(Target, ChildUsesParentSubtreeBody);
	const FAssetDocumentResult ChildUsesParentSubtreeResult = Service.Validate(ChildUsesParentSubtreeRequest);
	TestTrue(TEXT("child blackboard can run parent-blackboard subtree"), ChildUsesParentSubtreeResult.IsSuccess());
	if (!ChildUsesParentSubtreeResult.IsSuccess())
	{
		AddError(ChildUsesParentSubtreeResult.Message);
	}

	TSharedPtr<FJsonObject> ChildUsesSameSubtreeBody = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(ChildBlackboardTarget)),
		MakeRunBehaviorTree(ChildSubtreeTarget),
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest ChildUsesSameSubtreeRequest;
	ChildUsesSameSubtreeRequest.Document = MakeBehaviorTreeDocument(Target, ChildUsesSameSubtreeBody);
	const FAssetDocumentResult ChildUsesSameSubtreeResult = Service.Validate(ChildUsesSameSubtreeRequest);
	TestTrue(TEXT("child blackboard can run same child-blackboard subtree"), ChildUsesSameSubtreeResult.IsSuccess());
	if (!ChildUsesSameSubtreeResult.IsSuccess())
	{
		AddError(ChildUsesSameSubtreeResult.Message);
	}

	TSharedPtr<FJsonObject> ParentUsesChildSubtreeBody = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(ParentBlackboardTarget)),
		MakeRunBehaviorTree(ChildSubtreeTarget),
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest ParentUsesChildSubtreeRequest;
	ParentUsesChildSubtreeRequest.Document = MakeBehaviorTreeDocument(Target, ParentUsesChildSubtreeBody);
	const FAssetDocumentResult ParentUsesChildSubtreeResult = Service.Validate(ParentUsesChildSubtreeRequest);
	TestFalse(TEXT("parent blackboard cannot run child-blackboard subtree"), ParentUsesChildSubtreeResult.IsSuccess());
	TestTrue(TEXT("child subtree mismatch diagnostic is exact"), ResultHasDiagnostic(ParentUsesChildSubtreeResult, TEXT("IncompatibleBehaviorTreeBlackboard"), TEXT("/Body/Tree/RunSubtree/Properties/BehaviorAsset")));

	TSharedPtr<FJsonObject> IncompatibleBody = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(ParentBlackboardTarget)),
		MakeRunBehaviorTree(IncompatibleSubtreeTarget),
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest IncompatibleRequest;
	IncompatibleRequest.Document = MakeBehaviorTreeDocument(Target, IncompatibleBody);
	const FAssetDocumentResult IncompatibleResult = Service.Validate(IncompatibleRequest);
	TestFalse(TEXT("subtree with unrelated blackboard is rejected"), IncompatibleResult.IsSuccess());
	TestTrue(TEXT("subtree mismatch diagnostic is exact"), ResultHasDiagnostic(IncompatibleResult, TEXT("IncompatibleBehaviorTreeBlackboard"), TEXT("/Body/Tree/RunSubtree/Properties/BehaviorAsset")));

	const FAssetDocumentResult ApplyCompatible = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, ParentUsesSameSubtreeBody)));
	TestTrue(TEXT("compatible subtree fixture apply succeeds"), ApplyCompatible.IsSuccess());
	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	UBTCompositeNode* OriginalRoot = BehaviorTree ? BehaviorTree->RootNode : nullptr;
	UBlackboardData* OriginalBlackboard = BehaviorTree ? BehaviorTree->BlackboardAsset : nullptr;
	TestNotNull(TEXT("applied root exists before missing blackboard check"), OriginalRoot);

	TSharedPtr<FJsonObject> MissingBlackboardBody = MakeBehaviorTreeBody(
		nullptr,
		MakeRunBehaviorTree(SameSubtreeTarget),
		MakeShared<FJsonObject>());
	const FAssetDocumentResult MissingBlackboardApply = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, MissingBlackboardBody)));
	TestFalse(TEXT("missing Body.Blackboard for subtree validation is rejected"), MissingBlackboardApply.IsSuccess());
	TestTrue(TEXT("missing blackboard diagnostic is exact"), ResultHasDiagnostic(MissingBlackboardApply, TEXT("MissingBehaviorTreeBlackboard"), TEXT("/Body/Blackboard")));
	TestTrue(TEXT("failed missing-blackboard apply does not replace root"), BehaviorTree && BehaviorTree->RootNode == OriginalRoot);
	TestTrue(TEXT("failed missing-blackboard apply does not replace blackboard"), BehaviorTree && BehaviorTree->BlackboardAsset == OriginalBlackboard);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeUnknownBlackboardKeyRejectsTest,
	"AssetFactory.AssetDocument.BehaviorTree.UnknownBlackboardKeyRejects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeUnknownBlackboardKeyRejectsTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task8_UnknownKey");
	const FString BlackboardTarget = Target + TEXT("_BB");
	UBlackboardData* Blackboard = MakeExistingBlackboardAsset(BlackboardTarget);
	TestNotNull(TEXT("blackboard exists"), Blackboard);
	if (!Blackboard)
	{
		return false;
	}
	AddObjectBlackboardKey(Blackboard, TEXT("TargetActor"));

	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		MakeMoveToTreeWithKeys({TEXT("MissingTarget")}),
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest Request;
	Request.Document = MakeBehaviorTreeDocument(Target, Body);
	const FAssetDocumentResult Result = FAssetDocumentService().Validate(Request);
	TestFalse(TEXT("unknown blackboard selector key is rejected"), Result.IsSuccess());
	TestTrue(TEXT("unknown key diagnostic is exact"), ResultHasDiagnostic(Result, TEXT("UnknownBlackboardKey"), TEXT("/Body/Tree/MoveTo_MissingTarget/Properties/BlackboardKey/Key")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeDuplicateNodeIdDiagnosticTest,
	"AssetFactory.AssetDocument.BehaviorTree.DuplicateNodeIdDiagnostic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeDuplicateNodeIdDiagnosticTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_DuplicateNodeIdDiagnostic");
	TSharedPtr<FJsonObject> Child = MakeBtNode(
		TEXT("Root"),
		TEXT("/Script/AIModule.BTTask_WaitBlackboardTime"),
		MakeShared<FJsonObject>());
	TSharedPtr<FJsonObject> Edge = MakeShared<FJsonObject>();
	Edge->SetObjectField(TEXT("Child"), Child);

	TSharedPtr<FJsonObject> Root = MakeBtNode(TEXT("Root"), TEXT("/Script/AIModule.BTComposite_Selector"));
	Root->SetArrayField(TEXT("Children"), {MakeObjectValue(Edge)});

	TSharedPtr<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetObjectField(TEXT("Root"), Root);
	Tree->SetArrayField(TEXT("RootDecorators"), TArray<TSharedPtr<FJsonValue>>());
	Tree->SetArrayField(TEXT("RootDecoratorLogic"), TArray<TSharedPtr<FJsonValue>>());

	FAssetDocumentValidateRequest Request;
	Request.Document = MakeBehaviorTreeDocument(
		Target,
		MakeBehaviorTreeBody(nullptr, Tree, MakeShared<FJsonObject>()));
	const FAssetDocumentResult Result = FAssetDocumentService().Validate(Request);
	TestFalse(TEXT("duplicate behavior tree node id is rejected"), Result.IsSuccess());
	TestTrue(TEXT("duplicate behavior tree node id diagnostic is exact"), ResultHasDiagnostic(Result, TEXT("DuplicateBehaviorTreeNodeId"), TEXT("/Body/Tree/Root")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeIncompatibleBlackboardKeyTypeRejectsTest,
	"AssetFactory.AssetDocument.BehaviorTree.IncompatibleBlackboardKeyTypeRejects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeIncompatibleBlackboardKeyTypeRejectsTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task8_KeyTypeMismatch");
	const FString BlackboardTarget = Target + TEXT("_BB");
	UBlackboardData* Blackboard = MakeExistingBlackboardAsset(BlackboardTarget);
	TestNotNull(TEXT("blackboard exists"), Blackboard);
	if (!Blackboard)
	{
		return false;
	}
	AddBlackboardKey<UBlackboardKeyType_Bool>(Blackboard, TEXT("HasTarget"));
	AddBlackboardKey<UBlackboardKeyType_Vector>(Blackboard, TEXT("MoveLocation"));

	TSharedPtr<FJsonObject> Selector = MakeSelectorProperty(TEXT("HasTarget"));
	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		MakeTreeWithSelectorTask(Selector),
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest Request;
	Request.Document = MakeBehaviorTreeDocument(Target, Body);
	const FAssetDocumentResult Result = FAssetDocumentService().Validate(Request);
	TestFalse(TEXT("selector key type mismatch is rejected"), Result.IsSuccess());
	TestTrue(TEXT("key type mismatch diagnostic is exact"), ResultHasDiagnostic(Result, TEXT("IncompatibleBlackboardKeyType"), TEXT("/Body/Tree/SelectorTask/Properties/BlackboardKey/Key")));

	TSharedPtr<FJsonObject> CompatibleMoveToBody = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		MakeTreeWithSelectorTask(MakeSelectorProperty(TEXT("MoveLocation"))),
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest CompatibleMoveToRequest;
	CompatibleMoveToRequest.Document = MakeBehaviorTreeDocument(Target, CompatibleMoveToBody);
	const FAssetDocumentResult CompatibleMoveToResult = FAssetDocumentService().Validate(CompatibleMoveToRequest);
	TestTrue(TEXT("MoveTo class policy accepts a vector key"), CompatibleMoveToResult.IsSuccess());
	if (!CompatibleMoveToResult.IsSuccess())
	{
		AddError(CompatibleMoveToResult.Message);
	}

	TSharedPtr<FJsonObject> CompatibleBoolBody = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		MakeTreeWithSelectorTask(
			MakeSelectorProperty(TEXT("HasTarget")),
			TEXT("/Script/AIModule.BTTask_SetKeyValueBool")),
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest CompatibleBoolRequest;
	CompatibleBoolRequest.Document = MakeBehaviorTreeDocument(Target, CompatibleBoolBody);
	const FAssetDocumentResult CompatibleBoolResult = FAssetDocumentService().Validate(CompatibleBoolRequest);
	TestTrue(TEXT("SetKeyValueBool class policy accepts a bool key"), CompatibleBoolResult.IsSuccess());
	if (!CompatibleBoolResult.IsSuccess())
	{
		AddError(CompatibleBoolResult.Message);
	}

	TSharedPtr<FJsonObject> IncompatibleBoolBody = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		MakeTreeWithSelectorTask(
			MakeSelectorProperty(TEXT("MoveLocation")),
			TEXT("/Script/AIModule.BTTask_SetKeyValueBool")),
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest IncompatibleBoolRequest;
	IncompatibleBoolRequest.Document = MakeBehaviorTreeDocument(Target, IncompatibleBoolBody);
	const FAssetDocumentResult IncompatibleBoolResult = FAssetDocumentService().Validate(IncompatibleBoolRequest);
	TestFalse(TEXT("SetKeyValueBool class policy rejects a vector key"), IncompatibleBoolResult.IsSuccess());
	TestTrue(TEXT("bool policy mismatch diagnostic is exact"), ResultHasDiagnostic(IncompatibleBoolResult, TEXT("IncompatibleBlackboardKeyType"), TEXT("/Body/Tree/SelectorTask/Properties/BlackboardKey/Key")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeFullApplyExtractDiffTest,
	"AssetFactory.AssetDocument.BehaviorTree.FullApplyExtractDiff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeFullApplyExtractDiffTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task10_FullApplyExtractDiff");
	FAssetDocumentService Service;
	FString ParentBlackboardTarget;
	FString BlackboardTarget;
	FString SubtreeTarget;
	if (!PrepareTask10BehaviorTreeFixture(*this, Service, Target, ParentBlackboardTarget, BlackboardTarget, SubtreeTarget))
	{
		return false;
	}

	TSharedPtr<FJsonObject> Body = MakeTask10BehaviorTreeBody(BlackboardTarget, SubtreeTarget);
	TSharedPtr<FJsonObject> Document = MakeBehaviorTreeDocument(Target, Body);
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyRequest(Document));
	TestTrue(TEXT("full BehaviorTree apply succeeds"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	UBlackboardData* Blackboard = LoadObject<UBlackboardData>(nullptr, *MakeObjectPathFromTarget(BlackboardTarget));
	TestTrue(TEXT("local blackboard has parent"), Blackboard && Blackboard->Parent.Get() == LoadObject<UBlackboardData>(nullptr, *MakeObjectPathFromTarget(ParentBlackboardTarget)));
	TestEqual(TEXT("local blackboard keeps local key count"), Blackboard ? Blackboard->Keys.Num() : -1, 2);

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	UBTCompositeNode* Root = BehaviorTree ? BehaviorTree->RootNode : nullptr;
	TestTrue(TEXT("BehaviorTree references authored blackboard"), BehaviorTree && BehaviorTree->BlackboardAsset == Blackboard);
	TestNotNull(TEXT("root composite exists"), Root);
	TestEqual(TEXT("root decorator count"), BehaviorTree ? BehaviorTree->RootDecorators.Num() : -1, 1);
	TestEqual(TEXT("root decorator logic count"), BehaviorTree ? BehaviorTree->RootDecoratorOps.Num() : -1, 1);
	TestEqual(TEXT("root service count"), Root ? Root->Services.Num() : -1, 1);
	TestEqual(TEXT("root child count"), Root ? Root->Children.Num() : -1, 2);
	if (Root && Root->Children.Num() >= 2)
	{
		TestEqual(TEXT("first edge decorator count"), Root->Children[0].Decorators.Num(), 1);
		TestEqual(TEXT("first edge decorator logic count"), Root->Children[0].DecoratorOps.Num(), 1);
		TestNotNull(TEXT("first child task exists"), Root->Children[0].ChildTask.Get());
		UBTTask_RunBehavior* RunSubtreeTask = Cast<UBTTask_RunBehavior>(Root->Children[1].ChildTask);
		TestNotNull(TEXT("subtree task exists"), RunSubtreeTask);
		TestTrue(TEXT("second child is BTTask_RunBehavior"), RunSubtreeTask && RunSubtreeTask->GetClass()->IsChildOf(UBTTask_RunBehavior::StaticClass()));
	}

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("full BehaviorTree extract succeeds"), ExtractResult.IsSuccess());
	TestTrue(TEXT("full BehaviorTree extract returns payload"), ExtractResult.Payload.IsValid());
	if (!ExtractResult.IsSuccess() || !ExtractResult.Payload.IsValid())
	{
		AddError(ExtractResult.Message);
		return false;
	}

	const TSharedPtr<FJsonObject>* ExtractedBody = nullptr;
	const TSharedPtr<FJsonObject>* ExtractedTree = nullptr;
	const TSharedPtr<FJsonObject>* ExtractedLayout = nullptr;
	TestTrue(TEXT("extract contains Body"), ExtractResult.Payload->TryGetObjectField(TEXT("Body"), ExtractedBody));
	TestTrue(TEXT("extract contains Body.Tree"), ExtractedBody && ExtractedBody->IsValid() && (*ExtractedBody)->TryGetObjectField(TEXT("Tree"), ExtractedTree));
	TestTrue(TEXT("extract contains Body.EditorLayout"), ExtractedBody && ExtractedBody->IsValid() && (*ExtractedBody)->TryGetObjectField(TEXT("EditorLayout"), ExtractedLayout));
	if (ExtractedTree && ExtractedTree->IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* RootDecorators = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* RootLogic = nullptr;
		TestTrue(TEXT("extract includes root decorators"), (*ExtractedTree)->TryGetArrayField(TEXT("RootDecorators"), RootDecorators) && RootDecorators && RootDecorators->Num() == 1);
		TestTrue(TEXT("extract includes root decorator logic"), (*ExtractedTree)->TryGetArrayField(TEXT("RootDecoratorLogic"), RootLogic) && RootLogic && RootLogic->Num() == 1);
	}
	if (ExtractedLayout && ExtractedLayout->IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		TestTrue(TEXT("extract includes editor layout nodes"), (*ExtractedLayout)->TryGetArrayField(TEXT("Nodes"), Nodes) && Nodes && Nodes->Num() >= 2);
	}

	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(Document));
	TestTrue(TEXT("full BehaviorTree diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("full BehaviorTree roundtrip has no changed or failed diff entries"), DiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeApplyFileCanonicalWritebackTest,
	"AssetFactory.AssetDocument.BehaviorTree.ApplyFileCanonicalWriteback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeApplyFileCanonicalWritebackTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task10_ApplyFileWriteback");
	FAssetDocumentService Service;
	FString ParentBlackboardTarget;
	FString BlackboardTarget;
	FString SubtreeTarget;
	if (!PrepareTask10BehaviorTreeFixture(*this, Service, Target, ParentBlackboardTarget, BlackboardTarget, SubtreeTarget))
	{
		return false;
	}

	const FString SidecarPath = FAssetDocumentSidecar::ResolveSidecarPathFromObjectPath(Target);
	TSharedPtr<FJsonObject> Document = MakeBehaviorTreeDocument(Target, MakeTask10BehaviorTreeBody(BlackboardTarget, SubtreeTarget));
	if (!WriteSidecarJson(this, SidecarPath, Document))
	{
		return false;
	}

	FAssetDocumentValidateRequest ValidateRequest;
	ValidateRequest.FilePath = SidecarPath;
	const FAssetDocumentResult ValidateResult = Service.Validate(ValidateRequest);
	TestTrue(TEXT("BehaviorTree ApplyFile sidecar validates"), ValidateResult.IsSuccess());
	if (!ValidateResult.IsSuccess())
	{
		AddError(ValidateResult.Message);
		return false;
	}

	FAssetDocumentApplyFileRequest ApplyFileRequest;
	ApplyFileRequest.FilePath = SidecarPath;
	ApplyFileRequest.bSaveAsset = true;
	const FAssetDocumentResult ApplyFileResult = Service.ApplyFile(ApplyFileRequest);
	TestTrue(TEXT("BehaviorTree ApplyFile succeeds"), ApplyFileResult.IsSuccess());
	TestTrue(TEXT("BehaviorTree ApplyFile writes sidecar sync state"), ApplyFileResult.bWroteSidecar);
	if (ApplyFileResult.Payload.IsValid())
	{
		FString SkipReason;
		if (ApplyFileResult.Payload->TryGetStringField(TEXT("sidecar_sync_update_skip_reason"), SkipReason))
		{
			AddError(FString::Printf(TEXT("BehaviorTree sidecar sync update skip reason: %s"), *SkipReason));
		}
		TestFalse(TEXT("BehaviorTree ApplyFile does not skip sync update"), ApplyFileResult.Payload->HasField(TEXT("sidecar_sync_update_skipped")));
	}
	if (!ApplyFileResult.IsSuccess())
	{
		AddError(ApplyFileResult.Message);
		return false;
	}

	TSharedPtr<FJsonObject> ReloadedSidecar;
	if (!LoadSidecarJson(this, SidecarPath, ReloadedSidecar))
	{
		return false;
	}
	ExpectSyncRegions(this, ReloadedSidecar, {
		TEXT("Body.Blackboard"),
		TEXT("Body.Tree"),
		TEXT("Body.EditorLayout"),
	});

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.FilePath = SidecarPath;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("BehaviorTree ApplyFile diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("BehaviorTree ApplyFile diff has no changed or failed entries"), DiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));

	TSharedPtr<FJsonObject> DerivedAllowedTypesDocument = MakeBehaviorTreeDocument(Target, MakeTask10BehaviorTreeBody(BlackboardTarget, SubtreeTarget));
	TSharedPtr<FJsonObject> DerivedAllowedTypesBody = GetObjectField(DerivedAllowedTypesDocument, TEXT("Body"));
	TSharedPtr<FJsonObject> DerivedAllowedTypesTree = GetObjectField(DerivedAllowedTypesBody, TEXT("Tree"));
	TSharedPtr<FJsonObject> DerivedAllowedTypesRoot = GetObjectField(DerivedAllowedTypesTree, TEXT("Root"));
	const TArray<TSharedPtr<FJsonValue>>* DerivedAllowedTypesChildren = nullptr;
	if (DerivedAllowedTypesRoot.IsValid() && DerivedAllowedTypesRoot->TryGetArrayField(TEXT("Children"), DerivedAllowedTypesChildren) && DerivedAllowedTypesChildren && DerivedAllowedTypesChildren->Num() > 0)
	{
		TSharedPtr<FJsonObject> MoveEdge = GetObjectFromValue((*DerivedAllowedTypesChildren)[0]);
		TSharedPtr<FJsonObject> MoveToNode = GetObjectField(MoveEdge, TEXT("Child"));
		TSharedPtr<FJsonObject> MoveToProperties = GetObjectField(MoveToNode, TEXT("Properties"));
		TSharedPtr<FJsonObject> MoveToBlackboardKey = GetObjectField(MoveToProperties, TEXT("BlackboardKey"));
		if (MoveToBlackboardKey.IsValid())
		{
			MoveToBlackboardKey->SetArrayField(TEXT("AllowedTypes"), {});
		}
	}
	const FAssetDocumentResult AllowedTypesDiffResult = Service.Diff(MakeDiffRequest(DerivedAllowedTypesDocument));
	TestFalse(TEXT("BehaviorTree rejects authored selector AllowedTypes"), AllowedTypesDiffResult.IsSuccess());
	TestTrue(TEXT("BehaviorTree selector AllowedTypes diagnostic is exact"), ResultHasDiagnostic(AllowedTypesDiffResult, TEXT("NonAuthoredProperty"), TEXT("/Body/Tree/Root/Children/0/Child/Properties/BlackboardKey/AllowedTypes")));

	TSharedPtr<FJsonObject> ChangedServiceSelectorDocument = MakeBehaviorTreeDocument(Target, MakeTask10BehaviorTreeBody(BlackboardTarget, SubtreeTarget));
	TSharedPtr<FJsonObject> ChangedBody = GetObjectField(ChangedServiceSelectorDocument, TEXT("Body"));
	TSharedPtr<FJsonObject> ChangedTree = GetObjectField(ChangedBody, TEXT("Tree"));
	TSharedPtr<FJsonObject> ChangedRoot = GetObjectField(ChangedTree, TEXT("Root"));
	const TArray<TSharedPtr<FJsonValue>>* Services = nullptr;
	if (ChangedRoot.IsValid() && ChangedRoot->TryGetArrayField(TEXT("Services"), Services) && Services && Services->Num() > 0)
	{
		TSharedPtr<FJsonObject> ServiceNode = GetObjectFromValue((*Services)[0]);
		TSharedPtr<FJsonObject> ServiceProperties = GetObjectField(ServiceNode, TEXT("Properties"));
		TSharedPtr<FJsonObject> ServiceBlackboardKey = GetObjectField(ServiceProperties, TEXT("BlackboardKey"));
		if (ServiceBlackboardKey.IsValid())
		{
			ServiceBlackboardKey->SetBoolField(TEXT("bNoneIsAllowedValue"), true);
		}
	}
	const FAssetDocumentResult ServiceSelectorDiffResult = Service.Diff(MakeDiffRequest(ChangedServiceSelectorDocument));
	TestFalse(TEXT("BehaviorTree rejects authored selector None policy"), ServiceSelectorDiffResult.IsSuccess());
	TestTrue(TEXT("BehaviorTree selector None policy diagnostic is exact"), ResultHasDiagnostic(ServiceSelectorDiffResult, TEXT("NonAuthoredProperty"), TEXT("/Body/Tree/Root/Services/0/Properties/BlackboardKey/bNoneIsAllowedValue")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeApplyFailureRollsBackTest,
	"AssetFactory.AssetDocument.BehaviorTree.ApplyFailureRollsBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeApplyFailureRollsBackTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task10_FailureRollback");
	FAssetDocumentService Service;
	FString ParentBlackboardTarget;
	FString BlackboardTarget;
	FString SubtreeTarget;
	if (!PrepareTask10BehaviorTreeFixture(*this, Service, Target, ParentBlackboardTarget, BlackboardTarget, SubtreeTarget))
	{
		return false;
	}

	TSharedPtr<FJsonObject> InitialDocument = MakeBehaviorTreeDocument(Target, MakeTask10BehaviorTreeBody(BlackboardTarget, SubtreeTarget));
	const FAssetDocumentResult InitialApply = Service.Apply(MakeApplyRequest(InitialDocument));
	TestTrue(TEXT("initial full BehaviorTree apply succeeds"), InitialApply.IsSuccess());
	if (!InitialApply.IsSuccess())
	{
		AddError(InitialApply.Message);
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	UBTCompositeNode* OriginalRoot = BehaviorTree ? BehaviorTree->RootNode : nullptr;
	UBlackboardData* OriginalBlackboard = BehaviorTree ? BehaviorTree->BlackboardAsset : nullptr;
	const int32 OriginalRootDecorators = BehaviorTree ? BehaviorTree->RootDecorators.Num() : -1;
	const int32 OriginalRootLogic = BehaviorTree ? BehaviorTree->RootDecoratorOps.Num() : -1;
	const int32 OriginalServices = OriginalRoot ? OriginalRoot->Services.Num() : -1;
	const int32 OriginalChildren = OriginalRoot ? OriginalRoot->Children.Num() : -1;
	const int32 OriginalEdgeDecorators = OriginalRoot && OriginalRoot->Children.Num() > 0 ? OriginalRoot->Children[0].Decorators.Num() : -1;
	UBehaviorTreeGraph* OriginalGraph = BehaviorTree ? Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph) : nullptr;
	TestNotNull(TEXT("original full tree root exists"), OriginalRoot);

	TSharedPtr<FJsonObject> InvalidBody = MakeTask10BehaviorTreeBody(BlackboardTarget, SubtreeTarget);
	TSharedPtr<FJsonObject> InvalidTree = GetObjectField(InvalidBody, TEXT("Tree"));
	TSharedPtr<FJsonObject> InvalidRoot = GetObjectField(InvalidTree, TEXT("Root"));
	if (InvalidRoot.IsValid())
	{
		InvalidRoot->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BTTask_MoveTo"));
	}

	const FAssetDocumentResult FailedApply = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, InvalidBody)));
	TestFalse(TEXT("invalid full BehaviorTree apply fails"), FailedApply.IsSuccess());
	TestTrue(TEXT("failure reports invalid root diagnostic"), ResultHasDiagnostic(FailedApply, TEXT("InvalidBehaviorTreeNodeClass"), TEXT("/Body/Tree/Root/Class")));
	TestTrue(TEXT("failed apply preserves blackboard"), BehaviorTree && BehaviorTree->BlackboardAsset == OriginalBlackboard);
	TestTrue(TEXT("failed apply preserves root pointer"), BehaviorTree && BehaviorTree->RootNode == OriginalRoot);
	TestEqual(TEXT("failed apply preserves root decorators"), BehaviorTree ? BehaviorTree->RootDecorators.Num() : -1, OriginalRootDecorators);
	TestEqual(TEXT("failed apply preserves root decorator logic"), BehaviorTree ? BehaviorTree->RootDecoratorOps.Num() : -1, OriginalRootLogic);
	TestEqual(TEXT("failed apply preserves services"), OriginalRoot ? OriginalRoot->Services.Num() : -1, OriginalServices);
	TestEqual(TEXT("failed apply preserves children"), OriginalRoot ? OriginalRoot->Children.Num() : -1, OriginalChildren);
	TestEqual(TEXT("failed apply preserves edge decorators"), OriginalRoot && OriginalRoot->Children.Num() > 0 ? OriginalRoot->Children[0].Decorators.Num() : -1, OriginalEdgeDecorators);
	TestTrue(TEXT("failed apply preserves editor graph"), BehaviorTree && Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph) == OriginalGraph);

	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(InitialDocument));
	TestTrue(TEXT("post-failure original document diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("post-failure original document remains unchanged"), DiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeProfileInspectionListsAllRegionsTest,
	"AssetFactory.AssetDocument.BehaviorTree.ProfileInspectionListsAllRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeProfileInspectionListsAllRegionsTest::RunTest(const FString&)
{
	FAssetDocumentService Service;
	FAssetDocumentProfileRequest Request;
	Request.ClassOrAsset = TEXT("/Script/AIModule.BehaviorTree");
	const FAssetDocumentResult Result = Service.InspectProfile(Request);
	TestTrue(TEXT("BehaviorTree profile inspection succeeds"), Result.IsSuccess());
	TestTrue(TEXT("BehaviorTree profile payload exists"), Result.Payload.IsValid());
	if (!Result.Payload.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("Profile class is BehaviorTree"), Result.Payload->GetStringField(TEXT("Class")), FString(TEXT("/Script/AIModule.BehaviorTree")));
	const TArray<TSharedPtr<FJsonValue>>* BodySections = nullptr;
	TestTrue(TEXT("BehaviorTree profile exposes BodySections"), Result.Payload->TryGetArrayField(TEXT("BodySections"), BodySections));
	for (const FName& BodyKey : FBehaviorTreeAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		TestTrue(
			FString::Printf(TEXT("BehaviorTree BodySections contains %s"), *BodyKey.ToString()),
			JsonArrayContainsString(BodySections, BodyKey.ToString()));
	}
	TestFalse(TEXT("BehaviorTree BodySections omits BlackboardInline"), JsonArrayContainsString(BodySections, TEXT("BlackboardInline")));

	const TArray<TSharedPtr<FJsonValue>>* RegionPolicies = nullptr;
	TestTrue(TEXT("BehaviorTree profile exposes RegionPolicies"), Result.Payload->TryGetArrayField(TEXT("RegionPolicies"), RegionPolicies));
	for (const FString& RegionId : {
		TEXT("Body.Blackboard"),
		TEXT("Body.Tree"),
		TEXT("Body.EditorLayout"),
	})
	{
		TestTrue(FString::Printf(TEXT("BehaviorTree RegionPolicies contains %s"), *RegionId), RegionPoliciesContain(RegionPolicies, RegionId));
	}

	const TArray<TSharedPtr<FJsonValue>>* InternalAdapters = nullptr;
	TestTrue(TEXT("BehaviorTree profile exposes InternalAdapters"), Result.Payload->TryGetArrayField(TEXT("InternalAdapters"), InternalAdapters));
	TestTrue(TEXT("BehaviorTree profile lists blackboard adapter"), JsonArrayContainsString(InternalAdapters, FBehaviorTreeAssetDocumentProfile::BlackboardRegionAdapterName().ToString()));
	TestTrue(TEXT("BehaviorTree profile lists tree adapter"), JsonArrayContainsString(InternalAdapters, FBehaviorTreeAssetDocumentProfile::TreeRegionAdapterName().ToString()));
	TestTrue(TEXT("BehaviorTree profile lists editor layout adapter"), JsonArrayContainsString(InternalAdapters, FBehaviorTreeAssetDocumentProfile::EditorLayoutRegionAdapterName().ToString()));
	return true;
}

namespace Task4GraphSourceTests
{
// Task 4 contract fixtures intentionally author graph identity, topology, and layout together.
constexpr const TCHAR* GraphGuid = TEXT("0102030405060708090A0B0C0D0E0F10");
constexpr const TCHAR* RootGuid = TEXT("11111111222222223333333344444444");
constexpr const TCHAR* FirstTaskGuid = TEXT("AAAAAAAA11111111BBBBBBBB22222222");
constexpr const TCHAR* SecondTaskGuid = TEXT("CCCCCCCC33333333DDDDDDDD44444444");
constexpr const TCHAR* NestedCompositeGuid = TEXT("55555555666666667777777788888888");
constexpr const TCHAR* RootServiceGuid = TEXT("99999999AAAABBBBCCCCDDDDEEEEFFFF");

TSharedPtr<FJsonObject> MakePosition(double X, double Y)
{
	TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
	Position->SetNumberField(TEXT("X"), X);
	Position->SetNumberField(TEXT("Y"), Y);
	return Position;
}

TSharedPtr<FJsonObject> MakeEditor(double X, double Y)
{
	TSharedPtr<FJsonObject> Editor = MakeShared<FJsonObject>();
	Editor->SetObjectField(TEXT("Position"), MakePosition(X, Y));
	return Editor;
}

TSharedPtr<FJsonObject> MakeGraphNode(
	const FString& Id,
	const FString& ClassPath,
	const FString& NodeName,
	double X,
	double Y,
	TArray<TSharedPtr<FJsonValue>> Children = {})
{
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetStringField(TEXT("NodeName"), NodeName);
	if (ClassPath.Contains(TEXT("BTComposite_")))
	{
		Properties->SetBoolField(TEXT("bApplyDecoratorScope"), true);
	}
	if (ClassPath.Contains(TEXT("BTTask_")) || ClassPath.Contains(TEXT("BTTaskBlueprint")))
	{
		Properties->SetBoolField(TEXT("bIgnoreRestartSelf"), true);
	}

	TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("Id"), Id);
	Node->SetStringField(TEXT("Class"), ClassPath);
	Node->SetObjectField(TEXT("Properties"), Properties);
	Node->SetArrayField(TEXT("Decorators"), {});
	Node->SetArrayField(TEXT("Services"), {});
	Node->SetArrayField(TEXT("Children"), Children);
	Node->SetObjectField(TEXT("Editor"), MakeEditor(X, Y));
	return Node;
}

TSharedPtr<FJsonObject> MakeGraphService()
{
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetStringField(TEXT("NodeName"), TEXT("Authored focus service"));
	Properties->SetNumberField(TEXT("Interval"), 0.75);
	Properties->SetNumberField(TEXT("RandomDeviation"), 0.125);
	Properties->SetBoolField(TEXT("bCallTickOnSearchStart"), true);
	Properties->SetBoolField(TEXT("bRestartTimerOnEachActivation"), false);

	TSharedPtr<FJsonObject> Service = MakeShared<FJsonObject>();
	Service->SetStringField(TEXT("Id"), RootServiceGuid);
	Service->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BTService_DefaultFocus"));
	Service->SetObjectField(TEXT("Properties"), Properties);
	return Service;
}

TSharedPtr<FJsonObject> MakeGraphSourceTree(
	const FString& FirstNodeName = TEXT("Shared visible label"),
	double FirstX = 100.0,
	double SecondX = 300.0,
	const FString& FirstClass = TEXT("/Script/AIModule.BTTask_Wait"))
{
	TArray<TSharedPtr<FJsonValue>> Children;
	Children.Add(MakeObjectValue(MakeGraphNode(
		FirstTaskGuid,
		FirstClass,
		FirstNodeName,
		FirstX,
		300.0)));
	Children.Add(MakeObjectValue(MakeGraphNode(
		SecondTaskGuid,
		TEXT("/Script/AIModule.BTTask_Wait"),
		TEXT("Shared visible label"),
		SecondX,
		300.0)));

	TSharedPtr<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetStringField(TEXT("GraphGuid"), GraphGuid);
	TSharedPtr<FJsonObject> Root = MakeGraphNode(
		RootGuid,
		TEXT("/Script/AIModule.BTComposite_Selector"),
		TEXT("Authored selector label"),
		200.0,
		0.0,
		Children);
	Root->SetArrayField(TEXT("Services"), {MakeObjectValue(MakeGraphService())});
	Tree->SetObjectField(TEXT("Root"), Root);
	Tree->SetArrayField(TEXT("Comments"), {});
	return Tree;
}

FAssetDocumentRegionContext MakeTreeContext(UBehaviorTree* BehaviorTree)
{
	FAssetDocumentRegionContext Context;
	Context.Asset = BehaviorTree;
	Context.AssetClass = UBehaviorTree::StaticClass();
	Context.BodyPath = TEXT("Body.Tree");
	Context.JsonPointer = TEXT("/Body/Tree");
	return Context;
}

FAssetDocumentCapabilityResult ApplyTree(UBehaviorTree* BehaviorTree, const TSharedPtr<FJsonObject>& Tree, bool& bOutChanged)
{
	FAssetDocumentRegionContext Context = MakeTreeContext(BehaviorTree);
	return FBehaviorTreeAssetDocumentMaterializer::ApplyTree(Context, Tree.ToSharedRef(), bOutChanged);
}

UBehaviorTreeGraphNode* FindGraphNodeByGuid(UBehaviorTreeGraph* Graph, const FString& GuidString)
{
	if (!Graph)
	{
		return nullptr;
	}
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UBehaviorTreeGraphNode* BehaviorNode = Cast<UBehaviorTreeGraphNode>(Node);
		if (BehaviorNode && BehaviorNode->NodeGuid.ToString(EGuidFormats::Digits).Equals(GuidString, ESearchCase::IgnoreCase))
		{
			return BehaviorNode;
		}
		if (BehaviorNode)
		{
			for (UBehaviorTreeGraphNode* Service : BehaviorNode->Services)
			{
				if (Service && Service->NodeGuid.ToString(EGuidFormats::Digits).Equals(GuidString, ESearchCase::IgnoreCase))
				{
					return Service;
				}
			}
		}
	}
	return nullptr;
}

UEdGraphPin* FindGraphPin(UBehaviorTreeGraphNode* Node, EEdGraphPinDirection Direction)
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

TSharedPtr<FJsonObject> FindExtractedNodeById(const TSharedPtr<FJsonObject>& Node, const FString& Id)
{
	if (!Node.IsValid())
	{
		return nullptr;
	}
	FString CandidateId;
	if (Node->TryGetStringField(TEXT("Id"), CandidateId) && CandidateId == Id)
	{
		return Node;
	}

	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (Node->TryGetArrayField(TEXT("Children"), Children) && Children)
	{
		for (const TSharedPtr<FJsonValue>& ChildValue : *Children)
		{
			TSharedPtr<FJsonObject> Found = FindExtractedNodeById(ChildValue.IsValid() ? ChildValue->AsObject() : nullptr, Id);
			if (Found.IsValid())
			{
				return Found;
			}
		}
	}
	return nullptr;
}

TSharedPtr<FJsonObject> ExtractTree(FAutomationTestBase& Test, UBehaviorTree* BehaviorTree)
{
	FAssetDocumentRegionContext Context = MakeTreeContext(BehaviorTree);
	TSharedRef<FJsonObject> Extracted = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult Result = FBehaviorTreeAssetDocumentMaterializer::ExtractTree(Context, Extracted);
	Test.TestTrue(TEXT("graph-source extraction succeeds"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		Test.AddError(Result.Message);
		return nullptr;
	}
	return Extracted;
}

TSharedPtr<FJsonObject> MakeGraphSourceBody(const TSharedPtr<FJsonObject>& Tree)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetField(TEXT("BlackboardAsset"), MakeShared<FJsonValueNull>());
	Body->SetObjectField(TEXT("Tree"), Tree);
	return Body;
}

void RemoveEditorLayoutRecursively(const TSharedPtr<FJsonObject>& Node)
{
	if (!Node.IsValid())
	{
		return;
	}
	Node->RemoveField(TEXT("Editor"));
	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (Node->TryGetArrayField(TEXT("Children"), Children) && Children)
	{
		for (const TSharedPtr<FJsonValue>& Child : *Children)
		{
			RemoveEditorLayoutRecursively(Child.IsValid() ? Child->AsObject() : nullptr);
		}
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeGraphSourceIdentityTest,
	"AssetFactory.AssetDocument.BehaviorTree.GraphSource.IdentityAndNodeName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeGraphSourceIdentityTest::RunTest(const FString&)
{
	using namespace Task4GraphSourceTests;
	UBehaviorTree* BehaviorTree = NewObject<UBehaviorTree>(GetTransientPackage(), NAME_None, RF_Transient);
	bool bChanged = false;
	FAssetDocumentCapabilityResult Result = ApplyTree(BehaviorTree, MakeGraphSourceTree(), bChanged);
	TestTrue(TEXT("graph-source apply succeeds"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Message);
		return false;
	}
	TestTrue(TEXT("initial graph-source apply reports change"), bChanged);

	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph);
	TestNotNull(TEXT("UBehaviorTree owns a production UBehaviorTreeGraph"), Graph);
	TestEqual(TEXT("authored GraphGuid is preserved"), Graph ? Graph->GraphGuid.ToString(EGuidFormats::Digits) : FString(), FString(GraphGuid));

	UBehaviorTreeGraphNode* RootNode = FindGraphNodeByGuid(Graph, RootGuid);
	UBehaviorTreeGraphNode* FirstTask = FindGraphNodeByGuid(Graph, FirstTaskGuid);
	UBehaviorTreeGraphNode* SecondTask = FindGraphNodeByGuid(Graph, SecondTaskGuid);
	UBehaviorTreeGraphNode* RootService = FindGraphNodeByGuid(Graph, RootServiceGuid);
	TestTrue(TEXT("explicit root NodeGuid becomes graph wrapper identity"), RootNode && RootNode->NodeGuid.ToString(EGuidFormats::Digits) == RootGuid);
	TestTrue(TEXT("explicit first task NodeGuid becomes graph wrapper identity"), FirstTask && FirstTask->NodeGuid.ToString(EGuidFormats::Digits) == FirstTaskGuid);
	TestTrue(TEXT("explicit second task NodeGuid becomes graph wrapper identity"), SecondTask && SecondTask->NodeGuid.ToString(EGuidFormats::Digits) == SecondTaskGuid);
	TestTrue(TEXT("explicit service NodeGuid becomes graph subnode identity"), RootService && RootService->NodeGuid.ToString(EGuidFormats::Digits) == RootServiceGuid);
	TestTrue(TEXT("duplicate visible labels do not collapse graph identity"), FirstTask && SecondTask && FirstTask != SecondTask);
	TestEqual(TEXT("NodeName is an independent authored property"), FirstTask && FirstTask->NodeInstance ? CastChecked<UBTNode>(FirstTask->NodeInstance)->NodeName : FString(), FString(TEXT("Shared visible label")));

	TSharedPtr<FJsonObject> Extracted = ExtractTree(*this, BehaviorTree);
	TSharedPtr<FJsonObject> ExtractedRoot = Extracted.IsValid() ? GetObjectField(Extracted, TEXT("Root")) : nullptr;
	TSharedPtr<FJsonObject> ExtractedFirst = FindExtractedNodeById(ExtractedRoot, FirstTaskGuid);
	TestTrue(TEXT("extract reads first NodeGuid from graph"), ExtractedFirst.IsValid());
	TestEqual(TEXT("extract keeps canonical graph identity"), ExtractedFirst.IsValid() ? ExtractedFirst->GetStringField(TEXT("Id")) : FString(), FString(FirstTaskGuid));
	const TSharedPtr<FJsonObject> ExtractedRootProperties = ExtractedRoot.IsValid() ? GetObjectField(ExtractedRoot, TEXT("Properties")) : nullptr;
	TestTrue(TEXT("base composite editable property round-trips"), ExtractedRootProperties.IsValid() && ExtractedRootProperties->GetBoolField(TEXT("bApplyDecoratorScope")));
	const TSharedPtr<FJsonObject> ExtractedTaskProperties = ExtractedFirst.IsValid() ? GetObjectField(ExtractedFirst, TEXT("Properties")) : nullptr;
	TestTrue(TEXT("base task editable property round-trips"), ExtractedTaskProperties.IsValid() && ExtractedTaskProperties->GetBoolField(TEXT("bIgnoreRestartSelf")));
	const TArray<TSharedPtr<FJsonValue>>* ExtractedServices = nullptr;
	TestTrue(TEXT("root service order and identity extract from graph subnodes"), ExtractedRoot.IsValid() && ExtractedRoot->TryGetArrayField(TEXT("Services"), ExtractedServices) && ExtractedServices && ExtractedServices->Num() == 1 && (*ExtractedServices)[0]->AsObject()->GetStringField(TEXT("Id")) == RootServiceGuid);
	if (ExtractedServices && ExtractedServices->Num() == 1)
	{
		const TSharedPtr<FJsonObject> ServiceProperties = GetObjectField((*ExtractedServices)[0]->AsObject(), TEXT("Properties"));
		TestEqual(TEXT("concrete service float property round-trips"), ServiceProperties.IsValid() ? ServiceProperties->GetNumberField(TEXT("Interval")) : -1.0, 0.75);
		TestTrue(TEXT("concrete service bool property round-trips"), ServiceProperties.IsValid() && ServiceProperties->GetBoolField(TEXT("bCallTickOnSearchStart")));
	}

	TSharedPtr<FJsonObject> UpdatedTree = MakeGraphSourceTree(TEXT("Renamed without identity change"));
	Result = ApplyTree(BehaviorTree, UpdatedTree, bChanged);
	TestTrue(TEXT("graph-source update succeeds"), Result.bSuccess);
	TestTrue(TEXT("NodeName-only update reports change"), bChanged);
	Graph = Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph);
	FirstTask = FindGraphNodeByGuid(Graph, FirstTaskGuid);
	TestEqual(TEXT("NodeGuid survives update"), FirstTask ? FirstTask->NodeGuid.ToString(EGuidFormats::Digits) : FString(), FString(FirstTaskGuid));
	TestEqual(TEXT("NodeName update does not rewrite identity"), FirstTask && FirstTask->NodeInstance ? CastChecked<UBTNode>(FirstTask->NodeInstance)->NodeName : FString(), FString(TEXT("Renamed without identity change")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeGraphSourceTopologyAndOrderTest,
	"AssetFactory.AssetDocument.BehaviorTree.GraphSource.TopologyOrderAndRuntimeMirror",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeGraphSourceTopologyAndOrderTest::RunTest(const FString&)
{
	using namespace Task4GraphSourceTests;
	UBehaviorTree* BehaviorTree = NewObject<UBehaviorTree>(GetTransientPackage(), NAME_None, RF_Transient);
	bool bChanged = false;
	const FAssetDocumentCapabilityResult ApplyResult = ApplyTree(BehaviorTree, MakeGraphSourceTree(), bChanged);
	TestTrue(TEXT("ordered graph apply succeeds"), ApplyResult.bSuccess);
	if (!ApplyResult.bSuccess)
	{
		AddError(ApplyResult.Message);
		return false;
	}

	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph);
	UBehaviorTreeGraphNode* RootGraphNode = FindGraphNodeByGuid(Graph, RootGuid);
	UBehaviorTreeGraphNode* FirstGraphTask = FindGraphNodeByGuid(Graph, FirstTaskGuid);
	UBehaviorTreeGraphNode* SecondGraphTask = FindGraphNodeByGuid(Graph, SecondTaskGuid);
	UBTCompositeNode* RuntimeRoot = BehaviorTree->RootNode;
	TestTrue(TEXT("semantic child order has strictly increasing X"), FirstGraphTask && SecondGraphTask && FirstGraphTask->NodePosX < SecondGraphTask->NodePosX);
	TestTrue(TEXT("standard UpdateAsset sets runtime root from graph wrapper"), RootGraphNode && RuntimeRoot == RootGraphNode->NodeInstance);
	TestEqual(TEXT("standard UpdateAsset creates two runtime children"), RuntimeRoot ? RuntimeRoot->Children.Num() : -1, 2);
	TestTrue(TEXT("standard UpdateAsset mirrors ordered service subnodes"), RuntimeRoot && RuntimeRoot->Services.Num() == 1 && RootGraphNode->Services.Num() == 1 && RuntimeRoot->Services[0] == RootGraphNode->Services[0]->NodeInstance);
	if (RuntimeRoot && RuntimeRoot->Children.Num() == 2)
	{
		TestTrue(TEXT("runtime first child mirrors graph X order"), RuntimeRoot->Children[0].ChildTask == FirstGraphTask->NodeInstance);
		TestTrue(TEXT("runtime second child mirrors graph X order"), RuntimeRoot->Children[1].ChildTask == SecondGraphTask->NodeInstance);

		RuntimeRoot->Children.Swap(0, 1);
		TSharedPtr<FJsonObject> Extracted = ExtractTree(*this, BehaviorTree);
		TSharedPtr<FJsonObject> ExtractedRoot = Extracted.IsValid() ? GetObjectField(Extracted, TEXT("Root")) : nullptr;
		const TArray<TSharedPtr<FJsonValue>>* ExtractedChildren = nullptr;
		TestTrue(TEXT("extract has graph children"), ExtractedRoot.IsValid() && ExtractedRoot->TryGetArrayField(TEXT("Children"), ExtractedChildren) && ExtractedChildren && ExtractedChildren->Num() == 2);
		if (ExtractedChildren && ExtractedChildren->Num() == 2)
		{
			TestEqual(TEXT("extract ignores corrupted runtime order and reads graph first"), (*ExtractedChildren)[0]->AsObject()->GetStringField(TEXT("Id")), FString(FirstTaskGuid));
			TestEqual(TEXT("extract ignores corrupted runtime order and reads graph second"), (*ExtractedChildren)[1]->AsObject()->GetStringField(TEXT("Id")), FString(SecondTaskGuid));
		}

		Graph->UpdateAsset();
		RuntimeRoot = BehaviorTree->RootNode;
		TestTrue(TEXT("standard graph rebuild restores runtime first child"), RuntimeRoot && RuntimeRoot->Children[0].ChildTask == FirstGraphTask->NodeInstance);
		TestTrue(TEXT("standard graph rebuild restores runtime second child"), RuntimeRoot && RuntimeRoot->Children[1].ChildTask == SecondGraphTask->NodeInstance);
	}

	TSharedPtr<FJsonObject> GeneratedLayoutTree = MakeGraphSourceTree();
	RemoveEditorLayoutRecursively(GetObjectField(GeneratedLayoutTree, TEXT("Root")));
	UBehaviorTree* GeneratedLayoutAsset = NewObject<UBehaviorTree>(GetTransientPackage(), NAME_None, RF_Transient);
	const FAssetDocumentCapabilityResult GeneratedLayoutResult = ApplyTree(GeneratedLayoutAsset, GeneratedLayoutTree, bChanged);
	TestTrue(TEXT("omitted layout generates deterministic graph coordinates"), GeneratedLayoutResult.bSuccess);
	UBehaviorTreeGraph* GeneratedLayoutGraph = Cast<UBehaviorTreeGraph>(GeneratedLayoutAsset->BTGraph);
	UBehaviorTreeGraphNode* GeneratedFirst = FindGraphNodeByGuid(GeneratedLayoutGraph, FirstTaskGuid);
	UBehaviorTreeGraphNode* GeneratedSecond = FindGraphNodeByGuid(GeneratedLayoutGraph, SecondTaskGuid);
	TestTrue(TEXT("generated sibling coordinates preserve semantic order"), GeneratedFirst && GeneratedSecond && GeneratedFirst->NodePosX < GeneratedSecond->NodePosX);
	TSharedPtr<FJsonObject> GeneratedExtract = ExtractTree(*this, GeneratedLayoutAsset);
	TSharedPtr<FJsonObject> GeneratedRoot = GeneratedExtract.IsValid() ? GetObjectField(GeneratedExtract, TEXT("Root")) : nullptr;
	const TArray<TSharedPtr<FJsonValue>>* GeneratedChildren = nullptr;
	TestTrue(TEXT("generated layout is emitted canonically"), GeneratedRoot.IsValid() && GeneratedRoot->TryGetArrayField(TEXT("Children"), GeneratedChildren) && GeneratedChildren && GeneratedChildren->Num() == 2 && GetObjectField((*GeneratedChildren)[0]->AsObject(), TEXT("Editor")).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeGraphSourceValidationTest,
	"AssetFactory.AssetDocument.BehaviorTree.GraphSource.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeGraphSourceValidationTest::RunTest(const FString&)
{
	using namespace Task4GraphSourceTests;
	UBehaviorTree* BehaviorTree = NewObject<UBehaviorTree>(GetTransientPackage(), NAME_None, RF_Transient);
	FAssetDocumentRegionContext Context = MakeTreeContext(BehaviorTree);

	TSharedPtr<FJsonObject> NameIdTree = MakeGraphSourceTree();
	GetObjectField(NameIdTree, TEXT("Root"))->SetStringField(TEXT("Id"), TEXT("RootByName"));
	FAssetDocumentCapabilityResult Result = FBehaviorTreeAssetDocumentMaterializer::ValidateTree(Context, NameIdTree.ToSharedRef());
	TestFalse(TEXT("legacy name identity is rejected"), Result.bSuccess);
	TestTrue(TEXT("legacy name identity diagnostic is exact"), HasDiagnostic(Result, TEXT("/Body/Tree/Root/Id"), TEXT("InvalidBehaviorTreeNodeGuid")));

	TSharedPtr<FJsonObject> DuplicateGuidTree = MakeGraphSourceTree();
	TSharedPtr<FJsonObject> DuplicateRoot = GetObjectField(DuplicateGuidTree, TEXT("Root"));
	const TArray<TSharedPtr<FJsonValue>>* DuplicateChildren = nullptr;
	if (DuplicateRoot->TryGetArrayField(TEXT("Children"), DuplicateChildren) && DuplicateChildren && DuplicateChildren->Num() == 2)
	{
		(*DuplicateChildren)[1]->AsObject()->SetStringField(TEXT("Id"), FirstTaskGuid);
	}
	Result = FBehaviorTreeAssetDocumentMaterializer::ValidateTree(Context, DuplicateGuidTree.ToSharedRef());
	TestFalse(TEXT("duplicate NodeGuid is rejected"), Result.bSuccess);
	TestTrue(TEXT("duplicate NodeGuid diagnostic is exact"), HasDiagnostic(Result, TEXT("/Body/Tree/Root/Children/1/Id"), TEXT("DuplicateBehaviorTreeNodeGuid")));

	TSharedPtr<FJsonObject> OrderConflictTree = MakeGraphSourceTree(TEXT("first"), 400.0, 100.0);
	Result = FBehaviorTreeAssetDocumentMaterializer::ValidateTree(Context, OrderConflictTree.ToSharedRef());
	TestFalse(TEXT("semantic child order conflicting with X order is rejected"), Result.bSuccess);
	TestTrue(TEXT("layout order conflict diagnostic is exact"), HasDiagnostic(Result, TEXT("/Body/Tree/Root/Children/1/Editor/Position/X"), TEXT("BehaviorTreeLayoutOrderConflict")));

	TSharedPtr<FJsonObject> DuplicateXTree = MakeGraphSourceTree(TEXT("first"), 100.0, 100.0);
	Result = FBehaviorTreeAssetDocumentMaterializer::ValidateTree(Context, DuplicateXTree.ToSharedRef());
	TestFalse(TEXT("sibling X tie is rejected"), Result.bSuccess);
	TestTrue(TEXT("sibling X tie diagnostic is exact"), HasDiagnostic(Result, TEXT("/Body/Tree/Root/Children/1/Editor/Position/X"), TEXT("DuplicateBehaviorTreeSiblingCoordinate")));

	TSharedPtr<FJsonObject> WrongRootClassTree = MakeGraphSourceTree();
	GetObjectField(WrongRootClassTree, TEXT("Root"))->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BTTask_Wait"));
	Result = FBehaviorTreeAssetDocumentMaterializer::ValidateTree(Context, WrongRootClassTree.ToSharedRef());
	TestFalse(TEXT("task class cannot be the root composite"), Result.bSuccess);
	TestTrue(TEXT("root class legality diagnostic is exact"), HasDiagnostic(Result, TEXT("/Body/Tree/Root/Class"), TEXT("InvalidBehaviorTreeNodeClass")));

	TSharedPtr<FJsonObject> AbstractClassTree = MakeGraphSourceTree(TEXT("first"), 100.0, 300.0, TEXT("/Script/AIModule.BTTaskNode"));
	Result = FBehaviorTreeAssetDocumentMaterializer::ValidateTree(Context, AbstractClassTree.ToSharedRef());
	TestFalse(TEXT("abstract task class is rejected"), Result.bSuccess);
	TestTrue(TEXT("abstract class diagnostic is exact"), HasDiagnostic(Result, TEXT("/Body/Tree/Root/Children/0/Class"), TEXT("AbstractBehaviorTreeNodeClass")));

	UBehaviorTree* MultipleRootsTree = NewObject<UBehaviorTree>(GetTransientPackage(), NAME_None, RF_Transient);
	bool bChanged = false;
	Result = ApplyTree(MultipleRootsTree, MakeGraphSourceTree(), bChanged);
	TestTrue(TEXT("multiple-root graph fixture applies before corruption"), Result.bSuccess);
	if (UBehaviorTreeGraph* MultipleRootsGraph = Cast<UBehaviorTreeGraph>(MultipleRootsTree->BTGraph))
	{
		UBehaviorTreeGraphNode_Root* ExtraRoot = NewObject<UBehaviorTreeGraphNode_Root>(MultipleRootsGraph);
		MultipleRootsGraph->AddNode(ExtraRoot, false, false);
		ExtraRoot->CreateNewGuid();
		ExtraRoot->AllocateDefaultPins();
		TSharedRef<FJsonObject> Ignored = MakeShared<FJsonObject>();
		Result = FBehaviorTreeAssetDocumentMaterializer::ExtractTree(MakeTreeContext(MultipleRootsTree), Ignored);
		TestFalse(TEXT("extract rejects a graph with multiple synthetic roots"), Result.bSuccess);
		TestTrue(TEXT("multiple-root graph diagnostic is exact"), HasDiagnostic(Result, TEXT("/Body/Tree/Root"), TEXT("MultipleBehaviorTreeRoots")));
	}

	UBehaviorTree* OrphanTree = NewObject<UBehaviorTree>(GetTransientPackage(), NAME_None, RF_Transient);
	Result = ApplyTree(OrphanTree, MakeGraphSourceTree(), bChanged);
	TestTrue(TEXT("orphan graph fixture applies before corruption"), Result.bSuccess);
	if (UBehaviorTreeGraph* OrphanGraph = Cast<UBehaviorTreeGraph>(OrphanTree->BTGraph))
	{
		UBehaviorTreeGraphNode_Task* Orphan = NewObject<UBehaviorTreeGraphNode_Task>(OrphanGraph);
		Orphan->ClassData = FGraphNodeClassData(UBTTask_Wait::StaticClass(), FString());
		OrphanGraph->AddNode(Orphan, false, false);
		Orphan->CreateNewGuid();
		Orphan->PostPlacedNewNode();
		Orphan->AllocateDefaultPins();
		TSharedRef<FJsonObject> Ignored = MakeShared<FJsonObject>();
		Result = FBehaviorTreeAssetDocumentMaterializer::ExtractTree(MakeTreeContext(OrphanTree), Ignored);
		TestFalse(TEXT("extract rejects an unreachable graph node"), Result.bSuccess);
		TestTrue(TEXT("unreachable graph diagnostic is exact"), HasDiagnostic(Result, TEXT("/Body/Tree/Nodes"), TEXT("UnreachableBehaviorTreeGraphNode")));
	}

	UBehaviorTree* CycleTree = NewObject<UBehaviorTree>(GetTransientPackage(), NAME_None, RF_Transient);
	Result = ApplyTree(CycleTree, MakeGraphSourceTree(), bChanged);
	TestTrue(TEXT("cycle graph fixture applies before corruption"), Result.bSuccess);
	if (UBehaviorTreeGraph* CycleGraph = Cast<UBehaviorTreeGraph>(CycleTree->BTGraph))
	{
		UBehaviorTreeGraphNode* RootGraphNode = FindGraphNodeByGuid(CycleGraph, RootGuid);
		UEdGraphPin* RootOutput = FindGraphPin(RootGraphNode, EGPD_Output);
		UEdGraphPin* RootInput = FindGraphPin(RootGraphNode, EGPD_Input);
		if (RootOutput && RootInput)
		{
			RootGraphNode->NodePosX = -100;
			RootOutput->MakeLinkTo(RootInput);
		}
		TSharedRef<FJsonObject> Ignored = MakeShared<FJsonObject>();
		Result = FBehaviorTreeAssetDocumentMaterializer::ExtractTree(MakeTreeContext(CycleTree), Ignored);
		TestFalse(TEXT("extract rejects a graph cycle"), Result.bSuccess);
		TestTrue(TEXT("cycle graph diagnostic is exact"), HasDiagnostic(Result, TEXT("/Body/Tree/Root/Children/0"), TEXT("BehaviorTreeGraphCycle")));
	}

	UBehaviorTree* IllegalPinTree = NewObject<UBehaviorTree>(GetTransientPackage(), NAME_None, RF_Transient);
	Result = ApplyTree(IllegalPinTree, MakeGraphSourceTree(), bChanged);
	TestTrue(TEXT("illegal-pin graph fixture applies before corruption"), Result.bSuccess);
	if (UBehaviorTreeGraph* IllegalPinGraph = Cast<UBehaviorTreeGraph>(IllegalPinTree->BTGraph))
	{
		UEdGraphPin* TaskInput = FindGraphPin(FindGraphNodeByGuid(IllegalPinGraph, FirstTaskGuid), EGPD_Input);
		if (TaskInput)
		{
			TaskInput->PinType.PinCategory = FName(TEXT("SingleTask"));
		}
		TSharedRef<FJsonObject> Ignored = MakeShared<FJsonObject>();
		Result = FBehaviorTreeAssetDocumentMaterializer::ExtractTree(MakeTreeContext(IllegalPinTree), Ignored);
		TestFalse(TEXT("extract rejects a schema-illegal pin connection"), Result.bSuccess);
		TestTrue(TEXT("illegal pin diagnostic is exact"), HasDiagnostic(Result, TEXT("/Body/Tree/Root/Children/0"), TEXT("InvalidBehaviorTreeGraphConnection")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeGraphSourceDynamicBlueprintClassTest,
	"AssetFactory.AssetDocument.BehaviorTree.GraphSource.DynamicBlueprintClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeGraphSourceDynamicBlueprintClassTest::RunTest(const FString&)
{
	using namespace Task4GraphSourceTests;
	const FString PackageName = TEXT("/Game/AssetDocumentTests/BTTaskBlueprint_Task4");
	UPackage* BlueprintPackage = CreatePackage(*PackageName);
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		UBTTask_BlueprintBase::StaticClass(),
		BlueprintPackage,
		TEXT("BTTaskBlueprint_Task4"),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		TEXT("AssetDocumentBehaviorTreeTask4"));
	TestNotNull(TEXT("dynamic Blueprint task class fixture is created"), Blueprint);
	if (!Blueprint)
	{
		return false;
	}
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	UClass* GeneratedClass = Blueprint->GeneratedClass;
	TestTrue(TEXT("generated Blueprint class is concrete UBTTaskNode"), GeneratedClass && GeneratedClass->IsChildOf(UBTTaskNode::StaticClass()) && !GeneratedClass->HasAnyClassFlags(CLASS_Abstract));
	if (!GeneratedClass)
	{
		return false;
	}

	TSharedPtr<FJsonObject> Tree = MakeGraphSourceTree(TEXT("Blueprint task label"), 100.0, 300.0, GeneratedClass->GetPathName());
	UBehaviorTree* BehaviorTree = NewObject<UBehaviorTree>(GetTransientPackage(), NAME_None, RF_Transient);
	bool bChanged = false;
	const FAssetDocumentCapabilityResult Result = ApplyTree(BehaviorTree, Tree, bChanged);
	TestTrue(TEXT("dynamically loaded Blueprint node class applies"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Message);
		return false;
	}
	UBehaviorTreeGraphNode* GraphNode = FindGraphNodeByGuid(Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph), FirstTaskGuid);
	TestTrue(TEXT("graph wrapper owns generated Blueprint NodeInstance"), GraphNode && GraphNode->NodeInstance && GraphNode->NodeInstance->GetClass() == GeneratedClass);
	TSharedPtr<FJsonObject> Extracted = ExtractTree(*this, BehaviorTree);
	TSharedPtr<FJsonObject> ExtractedNode = FindExtractedNodeById(Extracted.IsValid() ? GetObjectField(Extracted, TEXT("Root")) : nullptr, FirstTaskGuid);
	TestEqual(TEXT("dynamic class extracts as canonical concrete path"), ExtractedNode.IsValid() ? ExtractedNode->GetStringField(TEXT("Class")) : FString(), GeneratedClass->GetPathName());

	UClass* AngelscriptTaskClass = nullptr;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Candidate = *It;
		if (Candidate
			&& Candidate->bIsScriptClass
			&& Candidate->IsChildOf(UBTTaskNode::StaticClass())
			&& !Candidate->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			AngelscriptTaskClass = Candidate;
			break;
		}
	}
	if (!AngelscriptTaskClass)
	{
		AddWarning(TEXT("No concrete Angelscript UBTTaskNode is loaded in this host; dynamic Angelscript graph coverage was skipped"));
		return true;
	}

	TSharedPtr<FJsonObject> AngelscriptTree = MakeGraphSourceTree(
		TEXT("Angelscript task label"),
		100.0,
		300.0,
		AngelscriptTaskClass->GetPathName());
	UBehaviorTree* AngelscriptBehaviorTree = NewObject<UBehaviorTree>(GetTransientPackage(), NAME_None, RF_Transient);
	bChanged = false;
	const FAssetDocumentCapabilityResult AngelscriptResult = ApplyTree(AngelscriptBehaviorTree, AngelscriptTree, bChanged);
	TestTrue(TEXT("dynamically loaded Angelscript node class applies"), AngelscriptResult.bSuccess);
	if (!AngelscriptResult.bSuccess)
	{
		AddError(AngelscriptResult.Message);
		return false;
	}
	UBehaviorTreeGraphNode* AngelscriptGraphNode = FindGraphNodeByGuid(
		Cast<UBehaviorTreeGraph>(AngelscriptBehaviorTree->BTGraph),
		FirstTaskGuid);
	TestTrue(
		TEXT("graph wrapper owns Angelscript NodeInstance"),
		AngelscriptGraphNode && AngelscriptGraphNode->NodeInstance && AngelscriptGraphNode->NodeInstance->GetClass() == AngelscriptTaskClass);
	TSharedPtr<FJsonObject> AngelscriptExtracted = ExtractTree(*this, AngelscriptBehaviorTree);
	TSharedPtr<FJsonObject> AngelscriptExtractedNode = FindExtractedNodeById(
		AngelscriptExtracted.IsValid() ? GetObjectField(AngelscriptExtracted, TEXT("Root")) : nullptr,
		FirstTaskGuid);
	TestEqual(
		TEXT("Angelscript class extracts as canonical concrete path"),
		AngelscriptExtractedNode.IsValid() ? AngelscriptExtractedNode->GetStringField(TEXT("Class")) : FString(),
		AngelscriptTaskClass->GetPathName());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeGraphSourceSaveReloadTest,
	"AssetFactory.AssetDocument.BehaviorTree.GraphSource.SaveReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeGraphSourceSaveReloadTest::RunTest(const FString&)
{
	using namespace Task4GraphSourceTests;
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_GraphSource_Task4_Reload");
	TSharedPtr<FJsonObject> Document = MakeBehaviorTreeDocument(Target, MakeGraphSourceBody(MakeGraphSourceTree()));
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = true;
	FAssetDocumentService Service;
	const FAssetDocumentResult ApplyResult = Service.Apply(Request);
	TestTrue(TEXT("saved graph-source apply succeeds"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	UBehaviorTree* BeforeReload = LoadBehaviorTreeForTarget(Target);
	TestNotNull(TEXT("saved BehaviorTree loads before explicit unload"), BeforeReload);
	UPackage* Package = BeforeReload ? BeforeReload->GetOutermost() : nullptr;
	TestNotNull(TEXT("saved BehaviorTree package exists"), Package);
	if (!Package)
	{
		return false;
	}

	const bool bUnloaded = UPackageTools::UnloadPackages({Package});
	TestTrue(TEXT("saved BehaviorTree package unloads"), bUnloaded);
	UBehaviorTree* Reloaded = LoadBehaviorTreeForTarget(Target);
	TestNotNull(TEXT("BehaviorTree fresh reload succeeds"), Reloaded);
	UBehaviorTreeGraph* ReloadedGraph = Reloaded ? Cast<UBehaviorTreeGraph>(Reloaded->BTGraph) : nullptr;
	TestEqual(TEXT("GraphGuid survives save and fresh reload"), ReloadedGraph ? ReloadedGraph->GraphGuid.ToString(EGuidFormats::Digits) : FString(), FString(GraphGuid));
	TestNotNull(TEXT("root NodeGuid survives save and fresh reload"), FindGraphNodeByGuid(ReloadedGraph, RootGuid));
	TestNotNull(TEXT("first task NodeGuid survives save and fresh reload"), FindGraphNodeByGuid(ReloadedGraph, FirstTaskGuid));
	TestNotNull(TEXT("second task NodeGuid survives save and fresh reload"), FindGraphNodeByGuid(ReloadedGraph, SecondTaskGuid));
	TestNotNull(TEXT("service NodeGuid survives save and fresh reload"), FindGraphNodeByGuid(ReloadedGraph, RootServiceGuid));
	return true;
}

#endif
