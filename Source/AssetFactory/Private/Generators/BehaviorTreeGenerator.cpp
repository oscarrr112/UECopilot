// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/BehaviorTreeGenerator.h"

#include "AssetFactoryModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#if WITH_EDITORONLY_DATA
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "EdGraphSchema_BehaviorTree.h"
#include "Kismet2/BlueprintEditorUtils.h"
#endif
#include "Generators/BlackboardDataGenerator.h"
#include "Misc/PackageName.h"
#include "Utils/ClassFinderUtils.h"
#include "Utils/PropertySetterUtils.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace
{
	FString NormalizeAssetObjectPath(const FString& AssetPath)
	{
		FString NormalizedPath = AssetPath;
		if (!NormalizedPath.Contains(TEXT(".")))
		{
			const FString AssetName = FPaths::GetBaseFilename(NormalizedPath);
			if (!AssetName.IsEmpty())
			{
				NormalizedPath += TEXT(".") + AssetName;
			}
		}

		return NormalizedPath;
	}

	FString NormalizeBlackboardPath(const FString& BlackboardPath)
	{
		return NormalizeAssetObjectPath(BlackboardPath);
	}

	UBlackboardData* LoadBlackboardDataFromPath(const FString& BlackboardPath)
	{
		if (BlackboardPath.IsEmpty())
		{
			return nullptr;
		}

		return LoadObject<UBlackboardData>(nullptr, *NormalizeBlackboardPath(BlackboardPath));
	}

	UBehaviorTree* LoadBehaviorTreeFromPath(const FString& BehaviorTreePath)
	{
		if (BehaviorTreePath.IsEmpty())
		{
			return nullptr;
		}

		return LoadObject<UBehaviorTree>(nullptr, *NormalizeAssetObjectPath(BehaviorTreePath));
	}

	bool IsBlackboardCompatibleWithSubtree(const UBlackboardData* ParentBlackboard, const UBlackboardData* ChildBlackboard)
	{
		if (!ChildBlackboard)
		{
			return true;
		}

		if (!ParentBlackboard)
		{
			return false;
		}

		for (const UBlackboardData* Current = ParentBlackboard; Current; Current = Current->Parent)
		{
			if (Current == ChildBlackboard || &Current->Keys == &ChildBlackboard->Keys)
			{
				return true;
			}
		}

		return false;
	}

	TSharedPtr<FJsonObject> BuildInlineBlackboardValidationConfig(
		TSharedPtr<FJsonObject> InlineBlackboard,
		const FString& BTName,
		const FString& BTPath)
	{
		TSharedPtr<FJsonObject> ValidationConfig = MakeShared<FJsonObject>(*InlineBlackboard);
		ValidationConfig->SetStringField(TEXT("AssetType"), TEXT("BlackboardData"));

		FString InlineName;
		if (!ValidationConfig->TryGetStringField(TEXT("Name"), InlineName) || InlineName.IsEmpty())
		{
			ValidationConfig->SetStringField(TEXT("Name"), FString::Printf(TEXT("BB_%s"), *BTName));
		}

		FString InlinePath;
		if (!ValidationConfig->TryGetStringField(TEXT("Path"), InlinePath) || InlinePath.IsEmpty())
		{
			ValidationConfig->SetStringField(TEXT("Path"), BTPath);
		}

		return ValidationConfig;
	}

	void CollectBlackboardKeysFromData(UBlackboardData* Blackboard, TSet<FName>& OutKeys)
	{
		for (UBlackboardData* Current = Blackboard; Current; Current = Current->Parent)
		{
			for (const FBlackboardEntry& Entry : Current->Keys)
			{
				OutKeys.Add(Entry.EntryName);
			}
		}
	}

	TSet<FName> CollectAllowedKeys(TSharedPtr<FJsonObject> Config)
	{
		TSet<FName> OutKeys;

		if (!Config.IsValid())
		{
			return OutKeys;
		}

		FString BlackboardPath;
		if (Config->TryGetStringField(TEXT("Blackboard"), BlackboardPath))
		{
			if (UBlackboardData* Blackboard = LoadBlackboardDataFromPath(BlackboardPath))
			{
				CollectBlackboardKeysFromData(Blackboard, OutKeys);
			}
		}

		const TSharedPtr<FJsonObject> BlackboardInline = Config->HasTypedField<EJson::Object>(TEXT("BlackboardInline"))
			? Config->GetObjectField(TEXT("BlackboardInline"))
			: nullptr;
		if (BlackboardInline.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* KeysArray = nullptr;
			if (BlackboardInline->TryGetArrayField(TEXT("Keys"), KeysArray) && KeysArray)
			{
				for (const TSharedPtr<FJsonValue>& KeyValue : *KeysArray)
				{
					const TSharedPtr<FJsonObject> KeyJson = KeyValue.IsValid() ? KeyValue->AsObject() : nullptr;
					if (!KeyJson.IsValid())
					{
						continue;
					}

					FString KeyName;
					if (KeyJson->TryGetStringField(TEXT("Name"), KeyName) && !KeyName.IsEmpty())
					{
						OutKeys.Add(FName(*KeyName));
					}
				}
			}

			FString ParentPath;
			if (BlackboardInline->TryGetStringField(TEXT("Parent"), ParentPath))
			{
				if (UBlackboardData* ParentBlackboard = LoadBlackboardDataFromPath(ParentPath))
				{
					CollectBlackboardKeysFromData(ParentBlackboard, OutKeys);
				}
			}
		}

		return OutKeys;
	}

	FString GetBTNodeClassExportName(const UBTNode* Node)
	{
		UClass* NodeClass = Node ? Node->GetClass() : nullptr;
		if (!NodeClass)
		{
			return FString();
		}

		const FString ClassName = NodeClass->GetName();
		int32 MatchingLoadedClasses = 0;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* CandidateClass = *It;
			if (CandidateClass && CandidateClass->IsChildOf(UBTNode::StaticClass()) && CandidateClass->GetName() == ClassName)
			{
				++MatchingLoadedClasses;
				if (MatchingLoadedClasses > 1)
				{
					break;
				}
			}
		}

		return MatchingLoadedClasses > 1 ? NodeClass->GetPathName() : ClassName;
	}

	void BuildNodeIndexMap(const UBTNode* Node, int32& InOutIndex, TMap<const UBTNode*, int32>& OutNodeIndices)
	{
		if (!Node)
		{
			return;
		}

		OutNodeIndices.Add(Node, InOutIndex++);

		const UBTCompositeNode* Composite = Cast<const UBTCompositeNode>(Node);
		if (!Composite)
		{
			return;
		}

		for (const UBTService* Service : Composite->Services)
		{
			if (Service)
			{
				OutNodeIndices.Add(Service, InOutIndex++);
			}
		}

		for (const FBTCompositeChild& Child : Composite->Children)
		{
			for (const UBTDecorator* Decorator : Child.Decorators)
			{
				if (Decorator)
				{
					OutNodeIndices.Add(Decorator, InOutIndex++);
				}
			}

			const UBTNode* ChildNode = Child.ChildComposite
				? Cast<const UBTNode>(Child.ChildComposite)
				: Cast<const UBTNode>(Child.ChildTask);
			BuildNodeIndexMap(ChildNode, InOutIndex, OutNodeIndices);
		}
	}

	void InitializeBTNodeForAsset(UBehaviorTree& TreeAsset, UBTCompositeNode* ParentNode, UBTNode* Node, uint8 TreeDepth, uint16& ExecutionIndex)
	{
		if (!Node)
		{
			return;
		}

		Node->InitializeNode(ParentNode, ExecutionIndex, 0, TreeDepth);
		Node->InitializeFromAsset(TreeAsset);
		ExecutionIndex++;

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

			Service->InitializeNode(Composite, ExecutionIndex, 0, TreeDepth);
			Service->InitializeFromAsset(TreeAsset);
			ExecutionIndex++;
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

				Decorator->InitializeNode(Composite, ExecutionIndex, 0, TreeDepth);
				Decorator->InitializeFromAsset(TreeAsset);
				Decorator->InitializeParentLink(ParentLinkIndex);
				Decorator->UpdateFlowAbortMode();
				ExecutionIndex++;
			}

			UBTNode* ChildNode = Child.ChildComposite
				? Cast<UBTNode>(Child.ChildComposite)
				: Cast<UBTNode>(Child.ChildTask);

			if (UBTTaskNode* ChildTask = Cast<UBTTaskNode>(ChildNode))
			{
				for (UBTService* Service : ChildTask->Services)
				{
					if (!Service)
					{
						continue;
					}

					Service->InitializeNode(Composite, ExecutionIndex, 0, TreeDepth);
					Service->InitializeFromAsset(TreeAsset);
					Service->InitializeParentLink(ParentLinkIndex);
					ExecutionIndex++;
				}
			}

			InitializeBTNodeForAsset(TreeAsset, Composite, ChildNode, TreeDepth + 1, ExecutionIndex);
		}

		Composite->InitializeComposite(ExecutionIndex - 1);
	}

#if WITH_EDITORONLY_DATA
	namespace BTEditorGraphLayout
	{
		constexpr int32 RootY = 0;
		constexpr int32 FirstTreeNodeY = 180;
		constexpr int32 VerticalSpacing = 220;
		constexpr int32 HorizontalSpacing = 100;
		constexpr int32 MinimumSubtreeWidth = 240;
		constexpr int32 NodePadding = 32;
		constexpr int32 BaseNodeWidth = 220;
		constexpr int32 BaseNodeHeight = 90;
		constexpr int32 RootNodeWidth = 180;
		constexpr int32 RootNodeHeight = 80;
		constexpr int32 SubNodeHeight = 42;
		constexpr int32 MaxOverlapPasses = 8;

		struct FEstimatedGraphNodeSize
		{
			float Width = BaseNodeWidth;
			float Height = BaseNodeHeight;
		};

		struct FLayoutTreeNode
		{
			UBTNode* BTNode = nullptr;
			UBehaviorTreeGraphNode* GraphNode = nullptr;
			TArray<FLayoutTreeNode> Children;
			FEstimatedGraphNodeSize Size;
			float SubtreeWidth = MinimumSubtreeWidth;
		};

		struct FPlacedGraphNode
		{
			UBehaviorTreeGraphNode* GraphNode = nullptr;
			FEstimatedGraphNodeSize Size;
		};

		FString GetBTNodeDisplayName(const UBTNode* Node)
		{
			if (!Node)
			{
				return FString();
			}

			if (!Node->NodeName.IsEmpty())
			{
				return Node->NodeName;
			}

			UClass* NodeClass = Node->GetClass();
			return NodeClass ? NodeClass->GetName() : FString();
		}

		FEstimatedGraphNodeSize EstimatePrimaryGraphNodeSize(const UBehaviorTreeGraphNode* GraphNode)
		{
			FEstimatedGraphNodeSize Result;

			const UBTNode* NodeInstance = GraphNode ? Cast<UBTNode>(GraphNode->NodeInstance) : nullptr;
			const FString DisplayName = GetBTNodeDisplayName(NodeInstance);
			const int32 TitleWidth = DisplayName.Len() * 8 + 80;
			const int32 SubNodeCount = GraphNode ? GraphNode->Decorators.Num() + GraphNode->Services.Num() : 0;

			Result.Width = static_cast<float>(FMath::Clamp(FMath::Max(BaseNodeWidth, TitleWidth), BaseNodeWidth, 520));
			Result.Height = static_cast<float>(FMath::Clamp(BaseNodeHeight + SubNodeCount * SubNodeHeight, BaseNodeHeight, 640));
			return Result;
		}

		FEstimatedGraphNodeSize EstimateRootGraphNodeSize()
		{
			FEstimatedGraphNodeSize Result;
			Result.Width = RootNodeWidth;
			Result.Height = RootNodeHeight;
			return Result;
		}

		void RenameGraphOutOfAsset(UEdGraph* Graph)
		{
			if (!Graph)
			{
				return;
			}

			Graph->SetFlags(RF_Transient);
			Graph->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
			Graph->MarkAsGarbage();
		}

		UBehaviorTreeGraphNode_Root* FindRootGraphNode(UBehaviorTreeGraph* Graph)
		{
			if (!Graph)
			{
				return nullptr;
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UBehaviorTreeGraphNode_Root* RootGraphNode = Cast<UBehaviorTreeGraphNode_Root>(Node))
				{
					return RootGraphNode;
				}
			}

			return nullptr;
		}

		void SyncRootGraphBlackboard(UBehaviorTree* BT, UBehaviorTreeGraph* Graph, UBlackboardData* DesiredBlackboard)
		{
			if (!BT || !Graph)
			{
				return;
			}

			if (UBehaviorTreeGraphNode_Root* RootGraphNode = FindRootGraphNode(Graph))
			{
				RootGraphNode->Modify();
				RootGraphNode->BlackboardAsset = DesiredBlackboard;
				RootGraphNode->UpdateBlackboard();
			}

			BT->BlackboardAsset = DesiredBlackboard;
		}

		void BuildGraphNodeMap(UBehaviorTreeGraph* Graph, TMap<UBTNode*, UBehaviorTreeGraphNode*>& OutGraphNodes)
		{
			OutGraphNodes.Reset();
			if (!Graph)
			{
				return;
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UBehaviorTreeGraphNode* BTGraphNode = Cast<UBehaviorTreeGraphNode>(Node);
				UBTNode* BTNode = BTGraphNode ? Cast<UBTNode>(BTGraphNode->NodeInstance) : nullptr;
				if (BTNode)
				{
					OutGraphNodes.Add(BTNode, BTGraphNode);
				}
			}
		}

		bool BuildLayoutTree(UBTNode* BTNode, const TMap<UBTNode*, UBehaviorTreeGraphNode*>& GraphNodes, FLayoutTreeNode& OutTree)
		{
			if (!BTNode)
			{
				UE_LOG(LogAssetFactory, Error, TEXT("BehaviorTree graph layout failed: null runtime BT node"));
				return false;
			}

			UBehaviorTreeGraphNode* const* GraphNodePtr = GraphNodes.Find(BTNode);
			if (!GraphNodePtr || !*GraphNodePtr)
			{
				UE_LOG(LogAssetFactory, Error, TEXT("BehaviorTree graph layout failed: missing graph node for runtime node '%s'"), *BTNode->GetPathName());
				return false;
			}

			OutTree.BTNode = BTNode;
			OutTree.GraphNode = *GraphNodePtr;
			OutTree.Size = EstimatePrimaryGraphNodeSize(OutTree.GraphNode);
			OutTree.Children.Reset();

			if (UBTCompositeNode* Composite = Cast<UBTCompositeNode>(BTNode))
			{
				for (int32 ChildIndex = 0; ChildIndex < Composite->Children.Num(); ++ChildIndex)
				{
					UBTNode* ChildNode = Composite->GetChildNode(ChildIndex);
					if (!ChildNode)
					{
						UE_LOG(LogAssetFactory, Error, TEXT("BehaviorTree graph layout failed: composite '%s' has null child at index %d"), *Composite->GetPathName(), ChildIndex);
						return false;
					}

					FLayoutTreeNode ChildTree;
					if (!BuildLayoutTree(ChildNode, GraphNodes, ChildTree))
					{
						UE_LOG(LogAssetFactory, Error, TEXT("BehaviorTree graph layout failed: could not build layout child %d for composite '%s'"), ChildIndex, *Composite->GetPathName());
						return false;
					}

					OutTree.Children.Add(MoveTemp(ChildTree));
				}
			}

			return true;
		}

		float ComputeChildrenWidth(const FLayoutTreeNode& Tree)
		{
			float ChildrenWidth = 0.0f;
			for (int32 ChildIndex = 0; ChildIndex < Tree.Children.Num(); ++ChildIndex)
			{
				ChildrenWidth += Tree.Children[ChildIndex].SubtreeWidth;
				if (ChildIndex > 0)
				{
					ChildrenWidth += HorizontalSpacing;
				}
			}

			return ChildrenWidth;
		}

		float ComputeSubtreeWidth(FLayoutTreeNode& Tree)
		{
			for (int32 ChildIndex = 0; ChildIndex < Tree.Children.Num(); ++ChildIndex)
			{
				ComputeSubtreeWidth(Tree.Children[ChildIndex]);
			}

			const float ChildrenWidth = ComputeChildrenWidth(Tree);
			const float OwnWidth = FMath::Max(static_cast<float>(MinimumSubtreeWidth), Tree.Size.Width + NodePadding);
			Tree.SubtreeWidth = Tree.Children.Num() > 0 ? FMath::Max(OwnWidth, ChildrenWidth) : OwnWidth;
			return Tree.SubtreeWidth;
		}

		void PositionLayoutTree(FLayoutTreeNode& Tree, float CenterX, float PosY, TArray<FPlacedGraphNode>& OutPlacedNodes)
		{
			if (Tree.GraphNode)
			{
				Tree.GraphNode->Modify();
				Tree.GraphNode->NodePosX = FMath::RoundToInt(CenterX - Tree.Size.Width * 0.5f);
				Tree.GraphNode->NodePosY = FMath::RoundToInt(PosY);

				FPlacedGraphNode Placement;
				Placement.GraphNode = Tree.GraphNode;
				Placement.Size = Tree.Size;
				OutPlacedNodes.Add(Placement);
			}

			const float ChildrenWidth = ComputeChildrenWidth(Tree);
			float ChildLeft = CenterX - ChildrenWidth * 0.5f;
			for (FLayoutTreeNode& Child : Tree.Children)
			{
				const float ChildCenterX = ChildLeft + Child.SubtreeWidth * 0.5f;
				PositionLayoutTree(Child, ChildCenterX, PosY + VerticalSpacing, OutPlacedNodes);
				ChildLeft += Child.SubtreeWidth + HorizontalSpacing;
			}
		}

		bool DoPlacedNodesOverlap(const FPlacedGraphNode& A, const FPlacedGraphNode& B, float Padding)
		{
			if (!A.GraphNode || !B.GraphNode || A.GraphNode == B.GraphNode)
			{
				return false;
			}

			const float ALeft = static_cast<float>(A.GraphNode->NodePosX);
			const float ATop = static_cast<float>(A.GraphNode->NodePosY);
			const float BLeft = static_cast<float>(B.GraphNode->NodePosX);
			const float BTop = static_cast<float>(B.GraphNode->NodePosY);

			return !(ALeft + A.Size.Width + Padding <= BLeft ||
				BLeft + B.Size.Width + Padding <= ALeft ||
				ATop + A.Size.Height + Padding <= BTop ||
				BTop + B.Size.Height + Padding <= ATop);
		}

		void ResolveOverlaps(TArray<FPlacedGraphNode>& PlacedNodes)
		{
			PlacedNodes.Sort([](const FPlacedGraphNode& A, const FPlacedGraphNode& B)
			{
				const int32 AY = A.GraphNode ? A.GraphNode->NodePosY : 0;
				const int32 BY = B.GraphNode ? B.GraphNode->NodePosY : 0;
				if (AY == BY)
				{
					const int32 AX = A.GraphNode ? A.GraphNode->NodePosX : 0;
					const int32 BX = B.GraphNode ? B.GraphNode->NodePosX : 0;
					return AX < BX;
				}
				return AY < BY;
			});

			for (int32 Pass = 0; Pass < MaxOverlapPasses; ++Pass)
			{
				bool bMovedAny = false;
				for (int32 Index = 0; Index < PlacedNodes.Num(); ++Index)
				{
					FPlacedGraphNode& Current = PlacedNodes[Index];
					if (!Current.GraphNode)
					{
						continue;
					}

					for (int32 PreviousIndex = 0; PreviousIndex < Index; ++PreviousIndex)
					{
						const FPlacedGraphNode& Previous = PlacedNodes[PreviousIndex];
						if (!Previous.GraphNode || !DoPlacedNodesOverlap(Previous, Current, NodePadding))
						{
							continue;
						}

						const int32 NewY = FMath::RoundToInt(static_cast<float>(Previous.GraphNode->NodePosY) + Previous.Size.Height + NodePadding);
						if (NewY > Current.GraphNode->NodePosY)
						{
							Current.GraphNode->Modify();
							Current.GraphNode->NodePosY = NewY;
							bMovedAny = true;
						}
					}
				}

				if (!bMovedAny)
				{
					break;
				}
			}
		}

		bool LayoutBehaviorTreeGraph(UBehaviorTree* BT, UBehaviorTreeGraph* Graph)
		{
			if (!BT || !BT->RootNode || !Graph)
			{
				UE_LOG(LogAssetFactory, Error, TEXT("BehaviorTree graph layout failed: invalid behavior tree or graph"));
				return false;
			}

			TMap<UBTNode*, UBehaviorTreeGraphNode*> GraphNodesByInstance;
			BuildGraphNodeMap(Graph, GraphNodesByInstance);

			FLayoutTreeNode LayoutRoot;
			if (!BuildLayoutTree(BT->RootNode, GraphNodesByInstance, LayoutRoot))
			{
				return false;
			}

			ComputeSubtreeWidth(LayoutRoot);

			TArray<FPlacedGraphNode> PlacedNodes;
			UBehaviorTreeGraphNode_Root* RootGraphNode = FindRootGraphNode(Graph);
			if (!RootGraphNode)
			{
				UE_LOG(LogAssetFactory, Error, TEXT("BehaviorTree graph layout failed: missing root graph node for '%s'"), *BT->GetPathName());
				return false;
			}

			{
				const FEstimatedGraphNodeSize RootSize = EstimateRootGraphNodeSize();
				RootGraphNode->Modify();
				RootGraphNode->NodePosX = FMath::RoundToInt(-RootSize.Width * 0.5f);
				RootGraphNode->NodePosY = RootY;

				FPlacedGraphNode RootPlacement;
				RootPlacement.GraphNode = RootGraphNode;
				RootPlacement.Size = RootSize;
				PlacedNodes.Add(RootPlacement);
			}

			PositionLayoutTree(LayoutRoot, 0.0f, FirstTreeNodeY, PlacedNodes);
			ResolveOverlaps(PlacedNodes);
			return true;
		}

		UBehaviorTreeGraph* RebuildEditorGraph(UBehaviorTree* BT)
		{
			if (!BT)
			{
				return nullptr;
			}

			UEdGraph* PreviousGraph = BT->BTGraph;
			UBlackboardData* DesiredBlackboard = BT->BlackboardAsset;
			const FName TemporaryGraphName = MakeUniqueObjectName(BT, UBehaviorTreeGraph::StaticClass(), TEXT("Behavior Tree Rebuild"));
			UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
				BT,
				TemporaryGraphName,
				UBehaviorTreeGraph::StaticClass(),
				UEdGraphSchema_BehaviorTree::StaticClass());

			UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(NewGraph);
			if (!Graph)
			{
				BT->BTGraph = PreviousGraph;
				BT->BlackboardAsset = DesiredBlackboard;
				return nullptr;
			}

			BT->BTGraph = Graph;
			Graph->LockUpdates();
			if (const UEdGraphSchema* Schema = Graph->GetSchema())
			{
				Schema->CreateDefaultNodesForGraph(*Graph);
			}
			SyncRootGraphBlackboard(BT, Graph, DesiredBlackboard);
			Graph->OnCreated();
			Graph->Initialize();
			Graph->UnlockUpdates();
			SyncRootGraphBlackboard(BT, Graph, DesiredBlackboard);

			if (!LayoutBehaviorTreeGraph(BT, Graph))
			{
				UE_LOG(LogAssetFactory, Error, TEXT("Failed to rebuild BehaviorTree editor graph layout for '%s'"), *BT->GetPathName());
				RenameGraphOutOfAsset(Graph);
				BT->BTGraph = PreviousGraph;
				BT->BlackboardAsset = DesiredBlackboard;
				return nullptr;
			}

			Graph->UpdateClassData();
			SyncRootGraphBlackboard(BT, Graph, DesiredBlackboard);
			Graph->UpdateAsset(UBehaviorTreeGraph::ClearDebuggerFlags | UBehaviorTreeGraph::KeepRebuildCounter);
			SyncRootGraphBlackboard(BT, Graph, DesiredBlackboard);

			RenameGraphOutOfAsset(PreviousGraph);
			Graph->Rename(TEXT("Behavior Tree"), BT, REN_DontCreateRedirectors | REN_NonTransactional);
			BT->BTGraph = Graph;
			Graph->NotifyGraphChanged();
			Graph->MarkPackageDirty();

			return Graph;
		}
	}
#endif

	void AddExtractedBTProperties(TSharedPtr<FJsonObject> OutJson, const UBTNode* Node, bool bDiffOnly)
	{
		if (!OutJson.IsValid() || !Node)
		{
			return;
		}

		TSharedPtr<FJsonObject> Properties = FPropertySetterUtils::ExtractPropertiesToJson(const_cast<UBTNode*>(Node), true, bDiffOnly);
		if (!Properties.IsValid())
		{
			return;
		}

		Properties->RemoveField(TEXT("NodeName"));
		if (Properties->Values.Num() > 0)
		{
			OutJson->SetObjectField(TEXT("Properties"), Properties);
		}
	}

	TSharedPtr<FJsonObject> ExtractAttachmentNodeToJson(
		const UBTNode* Node,
		bool bDiffOnly,
		const TMap<const UBTNode*, int32>& NodeIndices)
	{
		if (!Node)
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> OutJson = MakeShared<FJsonObject>();
		if (const int32* NodeIndex = NodeIndices.Find(Node))
		{
			OutJson->SetNumberField(TEXT("NodeIndex"), *NodeIndex);
		}
		OutJson->SetStringField(TEXT("Type"), GetBTNodeClassExportName(Node));
		AddExtractedBTProperties(OutJson, Node, bDiffOnly);
		return OutJson;
	}

	TSharedPtr<FJsonObject> ExtractBTNodeToJson(
		const UBTNode* Node,
		bool bDiffOnly,
		const TMap<const UBTNode*, int32>& NodeIndices)
	{
		if (!Node)
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> OutJson = MakeShared<FJsonObject>();
		if (const int32* NodeIndex = NodeIndices.Find(Node))
		{
			OutJson->SetNumberField(TEXT("NodeIndex"), *NodeIndex);
		}
		OutJson->SetStringField(TEXT("Node"), GetBTNodeClassExportName(Node));

		if (!Node->NodeName.IsEmpty())
		{
			OutJson->SetStringField(TEXT("InstanceName"), Node->NodeName);
		}

		AddExtractedBTProperties(OutJson, Node, bDiffOnly);

		const UBTCompositeNode* Composite = Cast<const UBTCompositeNode>(Node);
		if (!Composite)
		{
			return OutJson;
		}

		if (Composite->Services.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Services;
			for (const UBTService* Service : Composite->Services)
			{
				TSharedPtr<FJsonObject> ServiceJson = ExtractAttachmentNodeToJson(Service, bDiffOnly, NodeIndices);
				if (ServiceJson.IsValid())
				{
					Services.Add(MakeShared<FJsonValueObject>(ServiceJson));
				}
			}

			if (Services.Num() > 0)
			{
				OutJson->SetArrayField(TEXT("Services"), Services);
			}
		}

		TArray<TSharedPtr<FJsonValue>> Children;
		for (const FBTCompositeChild& Child : Composite->Children)
		{
			const UBTNode* ChildNode = Child.ChildComposite
				? Cast<const UBTNode>(Child.ChildComposite)
				: Cast<const UBTNode>(Child.ChildTask);
			TSharedPtr<FJsonObject> ChildJson = ExtractBTNodeToJson(ChildNode, bDiffOnly, NodeIndices);
			if (!ChildJson.IsValid())
			{
				continue;
			}

			if (Child.Decorators.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> Decorators;
				for (const UBTDecorator* Decorator : Child.Decorators)
				{
					TSharedPtr<FJsonObject> DecoratorJson = ExtractAttachmentNodeToJson(Decorator, bDiffOnly, NodeIndices);
					if (DecoratorJson.IsValid())
					{
						Decorators.Add(MakeShared<FJsonValueObject>(DecoratorJson));
					}
				}

				if (Decorators.Num() > 0)
				{
					ChildJson->SetArrayField(TEXT("Decorators"), Decorators);
				}
			}

			Children.Add(MakeShared<FJsonValueObject>(ChildJson));
		}

		if (Children.Num() > 0)
		{
			OutJson->SetArrayField(TEXT("Children"), Children);
		}

		return OutJson;
	}

	template<typename NodeType>
	NodeType* CreateBTAttachmentNode(
		const TCHAR* AttachmentKind,
		TSharedPtr<FJsonObject> AttachmentJson,
		TFunctionRef<UClass*(const FString&)> ResolveNodeClass,
		UBehaviorTree* OuterBT,
		TArray<FString>& OutWarnings)
	{
		if (!AttachmentJson.IsValid() || !OuterBT)
		{
			OutWarnings.Add(FString::Printf(TEXT("%s attachment is invalid; skipped"), AttachmentKind));
			return nullptr;
		}

		FString TypeName;
		if (!AttachmentJson->TryGetStringField(TEXT("Type"), TypeName) || TypeName.IsEmpty())
		{
			OutWarnings.Add(FString::Printf(TEXT("%s attachment is missing Type; skipped"), AttachmentKind));
			return nullptr;
		}

		UClass* AttachmentClass = ResolveNodeClass(TypeName);
		if (!AttachmentClass || !AttachmentClass->IsChildOf(NodeType::StaticClass()))
		{
			OutWarnings.Add(FString::Printf(TEXT("%s attachment class '%s' could not be resolved; skipped"), AttachmentKind, *TypeName));
			return nullptr;
		}

		NodeType* Attachment = NewObject<NodeType>(OuterBT, AttachmentClass);
		if (!Attachment)
		{
			OutWarnings.Add(FString::Printf(TEXT("%s attachment '%s' could not be created; skipped"), AttachmentKind, *TypeName));
			return nullptr;
		}

		const bool bHasPropertiesField = AttachmentJson->Values.Contains(TEXT("Properties"));
		const TSharedPtr<FJsonObject> PropsJson = AttachmentJson->HasTypedField<EJson::Object>(TEXT("Properties"))
			? AttachmentJson->GetObjectField(TEXT("Properties"))
			: nullptr;
		if (bHasPropertiesField && !PropsJson.IsValid())
		{
			OutWarnings.Add(FString::Printf(TEXT("%s attachment '%s': Properties must be an object"), AttachmentKind, *TypeName));
		}
		else if (PropsJson.IsValid() && !FPropertySetterUtils::SetPropertiesFromJson(Attachment, PropsJson))
		{
			OutWarnings.Add(FString::Printf(TEXT("%s attachment '%s': failed to apply Properties"), AttachmentKind, *TypeName));
		}

		return Attachment;
	}

	TOptional<FString> ValidateNodeStructure(
		const TFunction<UClass*(const FString&)>& ResolveNodeClass,
		TSharedPtr<FJsonObject> NodeJson,
		const FString& NodePath,
		bool bRequireComposite)
	{
		if (!NodeJson.IsValid())
		{
			return FString::Printf(TEXT("%s: Invalid node object"), *NodePath);
		}

		FString NodeName;
		if (!NodeJson->TryGetStringField(TEXT("Node"), NodeName) || NodeName.IsEmpty())
		{
			return FString::Printf(TEXT("%s: Missing or empty Node field"), *NodePath);
		}

		UClass* NodeClass = ResolveNodeClass(NodeName);
		if (!NodeClass)
		{
			return FString::Printf(TEXT("%s: Unknown BT node class '%s'"), *NodePath, *NodeName);
		}

		if (!NodeClass->IsChildOf(UBTNode::StaticClass()))
		{
			return FString::Printf(TEXT("%s: Resolved class '%s' is not a UBTNode subclass"), *NodePath, *NodeName);
		}

		const bool bIsComposite = NodeClass->IsChildOf(UBTCompositeNode::StaticClass());
		const bool bIsTask = NodeClass->IsChildOf(UBTTaskNode::StaticClass());
		const bool bIsDecorator = NodeClass->IsChildOf(UBTDecorator::StaticClass());
		const bool bIsService = NodeClass->IsChildOf(UBTService::StaticClass());

		if (bRequireComposite && !bIsComposite)
		{
			return FString::Printf(TEXT("%s: Root node '%s' must be a UBTCompositeNode subclass"), *NodePath, *NodeName);
		}

		if (!bIsComposite && !bIsTask)
		{
			if (bIsDecorator)
			{
				return FString::Printf(TEXT("%s: Decorator '%s' cannot be used as a tree node"), *NodePath, *NodeName);
			}

			if (bIsService)
			{
				return FString::Printf(TEXT("%s: Service '%s' cannot be used as a tree node"), *NodePath, *NodeName);
			}

			return FString::Printf(TEXT("%s: Node '%s' must be a UBTCompositeNode or UBTTaskNode subclass"), *NodePath, *NodeName);
		}

		const bool bHasChildrenField = NodeJson->Values.Contains(TEXT("Children"));
		const TArray<TSharedPtr<FJsonValue>>* ChildrenArray = nullptr;
		const bool bHasChildrenArray = NodeJson->TryGetArrayField(TEXT("Children"), ChildrenArray) && ChildrenArray;
		if (bHasChildrenField && !bHasChildrenArray)
		{
			return FString::Printf(TEXT("%s: Children must be an array"), *NodePath);
		}

		if (ChildrenArray)
		{
			if (!bIsComposite)
			{
				return FString::Printf(TEXT("%s: Non-composite node '%s' cannot have 'Children'"), *NodePath, *NodeName);
			}

			if (ChildrenArray->Num() == 0)
			{
				return FString::Printf(TEXT("%s: Composite node '%s' must have at least one child"), *NodePath, *NodeName);
			}
		}
		else if (bIsComposite)
		{
			return FString::Printf(TEXT("%s: Composite nodes must define Children"), *NodePath);
		}

		const TArray<TSharedPtr<FJsonValue>>* DecoratorsArray = nullptr;
		if (NodeJson->TryGetArrayField(TEXT("Decorators"), DecoratorsArray) && DecoratorsArray)
		{
			// fall through
		}
		else if (NodeJson->Values.Contains(TEXT("Decorators")))
		{
			return FString::Printf(TEXT("%s: Decorators must be an array"), *NodePath);
		}

		if (DecoratorsArray)
		{
			for (int32 Index = 0; Index < DecoratorsArray->Num(); ++Index)
			{
				const TSharedPtr<FJsonValue>& Value = (*DecoratorsArray)[Index];
				if (!Value.IsValid() || Value->Type != EJson::Object)
				{
					return FString::Printf(TEXT("%s.Decorators[%d]: Invalid decorator object"), *NodePath, Index);
				}

				const TSharedPtr<FJsonObject> DecoratorJson = Value->AsObject();
				FString DecoratorName;
				if (!DecoratorJson.IsValid() || !DecoratorJson->TryGetStringField(TEXT("Type"), DecoratorName) || DecoratorName.IsEmpty())
				{
					return FString::Printf(TEXT("%s.Decorators[%d]: Missing or empty Type field"), *NodePath, Index);
				}

				UClass* DecoratorClass = ResolveNodeClass(DecoratorName);
				if (!DecoratorClass || !DecoratorClass->IsChildOf(UBTDecorator::StaticClass()))
				{
					return FString::Printf(TEXT("%s.Decorators[%d]: Resolved class '%s' is not a UBTDecorator subclass"), *NodePath, Index, *DecoratorName);
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* ServicesArray = nullptr;
		if (NodeJson->TryGetArrayField(TEXT("Services"), ServicesArray) && ServicesArray)
		{
			// fall through
		}
		else if (NodeJson->Values.Contains(TEXT("Services")))
		{
			return FString::Printf(TEXT("%s: Services must be an array"), *NodePath);
		}

		if (ServicesArray)
		{
			if (!bIsComposite)
			{
				return FString::Printf(TEXT("%s: Only composite nodes can define Services"), *NodePath);
			}

			for (int32 Index = 0; Index < ServicesArray->Num(); ++Index)
			{
				const TSharedPtr<FJsonValue>& Value = (*ServicesArray)[Index];
				if (!Value.IsValid() || Value->Type != EJson::Object)
				{
					return FString::Printf(TEXT("%s.Services[%d]: Invalid service object"), *NodePath, Index);
				}

				const TSharedPtr<FJsonObject> ServiceJson = Value->AsObject();
				FString ServiceName;
				if (!ServiceJson.IsValid() || !ServiceJson->TryGetStringField(TEXT("Type"), ServiceName) || ServiceName.IsEmpty())
				{
					return FString::Printf(TEXT("%s.Services[%d]: Missing or empty Type field"), *NodePath, Index);
				}

				UClass* ServiceClass = ResolveNodeClass(ServiceName);
				if (!ServiceClass || !ServiceClass->IsChildOf(UBTService::StaticClass()))
				{
					return FString::Printf(TEXT("%s.Services[%d]: Resolved class '%s' is not a UBTService subclass"), *NodePath, Index, *ServiceName);
				}
			}
		}

		if (ChildrenArray)
		{
			for (int32 Index = 0; Index < ChildrenArray->Num(); ++Index)
			{
				const TSharedPtr<FJsonValue>& Value = (*ChildrenArray)[Index];
				if (!Value.IsValid() || Value->Type != EJson::Object)
				{
					return FString::Printf(TEXT("%s.Children[%d]: Invalid child node object"), *NodePath, Index);
				}

				const TSharedPtr<FJsonObject> ChildJson = Value->AsObject();
				const TOptional<FString> ChildError = ValidateNodeStructure(ResolveNodeClass, ChildJson, FString::Printf(TEXT("%s.Children[%d]"), *NodePath, Index), false);
				if (ChildError.IsSet())
				{
					return ChildError;
				}
			}
		}

		return TOptional<FString>();
	}
}

FGenerationResult FBehaviorTreeGenerator::Generate(
	const FString& Name,
	const FString& Path,
	EGenerationAction Action,
	TSharedPtr<FJsonObject> Config)
{
	if (!Config.IsValid())
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Invalid configuration object"));
	}

	const bool bExists = DoesAssetExist(Path, Name);
	if (Action == EGenerationAction::Create && bExists)
	{
		return FGenerationResult::MakeSkipped(GetAssetType(), Name, Path, TEXT("Asset already exists"));
	}

	if (Action == EGenerationAction::Update && !bExists)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Asset does not exist for update"));
	}

	UBlackboardData* Blackboard = ResolveBlackboard(Config, Name, Path);
	if (!Blackboard)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to resolve Blackboard"));
	}

	UBehaviorTree* BehaviorTree = nullptr;
	UPackage* Package = nullptr;
	if (bExists)
	{
		BehaviorTree = Cast<UBehaviorTree>(LoadExistingAsset(Path, Name));
		if (!BehaviorTree)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to load BT for update"));
		}

		Package = BehaviorTree->GetOutermost();
	}
	else
	{
		FString FullPath = Path / Name;
		if (!FullPath.StartsWith(TEXT("/")))
		{
			FullPath = TEXT("/") + FullPath;
		}

		Package = CreatePackage(*FullPath);
		if (!Package)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create package"));
		}

		BehaviorTree = NewObject<UBehaviorTree>(Package, UBehaviorTree::StaticClass(), *Name, RF_Public | RF_Standalone);
		if (!BehaviorTree)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create behavior tree asset"));
		}
	}

	TArray<FString> Warnings;
	TSharedPtr<FJsonObject> RootJson = GetObjectField(Config, TEXT("Root"));
	int32 BTAssetIdx = 0;
	if (TOptional<FString> BTAssetError = ValidateBTAssetReferences(RootJson, Blackboard, BTAssetIdx, true))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, BTAssetError.GetValue());
	}

	UBTNode* Root = BuildNode(RootJson, BehaviorTree, nullptr, Warnings);
	if (!Root)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to build root node"));
	}

	UBTCompositeNode* NewRootNode = Cast<UBTCompositeNode>(Root);
	if (!NewRootNode)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Root node must be a Composite"));
	}

	BehaviorTree->Modify();
	BehaviorTree->BlackboardAsset = Blackboard;
	BehaviorTree->RootNode = NewRootNode;
	BehaviorTree->RootDecorators.Reset();
	BehaviorTree->RootDecoratorOps.Reset();
#if WITH_EDITORONLY_DATA
	BehaviorTree->LastEditedDocuments.Reset();
#endif

	if (!bExists)
	{
		FAssetRegistryModule::AssetCreated(BehaviorTree);
	}

	if (!FinalizeBT(BehaviorTree))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to rebuild behavior tree editor graph"));
	}

	if (!Package)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to resolve behavior tree package"));
	}

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, BehaviorTree, *PackageFileName, SaveArgs))
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to save behavior tree package"));
	}

	FGenerationResult Result = bExists
		? FGenerationResult::MakeUpdated(GetAssetType(), Name, Path, BehaviorTree)
		: FGenerationResult::MakeSuccess(GetAssetType(), Name, Path, BehaviorTree);
	if (Warnings.Num() > 0)
	{
		Result.Message += FString::Printf(TEXT(" | Warnings: %s"), *FString::Join(Warnings, TEXT("; ")));
	}

	return Result;
}

TOptional<FString> FBehaviorTreeGenerator::ValidateConfig(TSharedPtr<FJsonObject> Config, EGenerationAction Action) const
{
	(void)Action;

	if (!Config.IsValid())
	{
		return FString(TEXT("Invalid configuration object"));
	}

	FString AssetType;
	if (!Config->TryGetStringField(TEXT("AssetType"), AssetType) || !AssetType.Equals(TEXT("BehaviorTree"), ESearchCase::CaseSensitive))
	{
		return FString(TEXT("AssetType must be 'BehaviorTree'"));
	}

	const bool bHasBlackboard = Config->Values.Contains(TEXT("Blackboard"));
	const bool bHasBlackboardInline = Config->Values.Contains(TEXT("BlackboardInline"));
	if (bHasBlackboard && bHasBlackboardInline)
	{
		return FString(TEXT("Blackboard and BlackboardInline are mutually exclusive"));
	}

	UBlackboardData* ValidationBlackboard = nullptr;
	if (bHasBlackboard)
	{
		FString BlackboardPath;
		if (!Config->TryGetStringField(TEXT("Blackboard"), BlackboardPath) || BlackboardPath.IsEmpty())
		{
			return FString(TEXT("Blackboard must be a non-empty string"));
		}

		FString NormalizedPath = BlackboardPath;
		if (!NormalizedPath.Contains(TEXT(".")))
		{
			const FString AssetName = FPaths::GetBaseFilename(NormalizedPath);
			if (!AssetName.IsEmpty())
			{
				NormalizedPath += TEXT(".") + AssetName;
			}
		}

		UBlackboardData* Blackboard = LoadObject<UBlackboardData>(nullptr, *NormalizedPath);
		if (!Blackboard)
		{
			return FString::Printf(TEXT("Blackboard asset could not be loaded: %s"), *BlackboardPath);
		}
		ValidationBlackboard = Blackboard;
	}
	else if (bHasBlackboardInline)
	{
		if (!Config->HasTypedField<EJson::Object>(TEXT("BlackboardInline")))
		{
			return FString(TEXT("BlackboardInline must be a JSON object"));
		}

		FString BTName;
		Config->TryGetStringField(TEXT("Name"), BTName);
		FString BTPath;
		Config->TryGetStringField(TEXT("Path"), BTPath);

		const TSharedPtr<FJsonObject> InlineBlackboard = Config->GetObjectField(TEXT("BlackboardInline"));
		FBlackboardDataGenerator BlackboardGenerator;
		if (TOptional<FString> InlineError = BlackboardGenerator.ValidateConfig(
			BuildInlineBlackboardValidationConfig(InlineBlackboard, BTName, BTPath),
			EGenerationAction::CreateOrUpdate);
			InlineError.IsSet())
		{
			return FString::Printf(TEXT("BlackboardInline validation failed: %s"), *InlineError.GetValue());
		}

	}

	TSharedPtr<FJsonObject> RootJson = GetObjectField(Config, TEXT("Root"));
	if (!RootJson.IsValid())
	{
		return FString(TEXT("Root must exist and be a JSON object"));
	}

	const TOptional<FString> StructureError = ValidateNodeStructure(
		[this](const FString& NodeName) -> UClass*
		{
			return ResolveNodeClass(NodeName);
		},
		RootJson,
		TEXT("Root"),
		true);

	if (StructureError.IsSet())
	{
		return StructureError;
	}

	TSet<FName> Allowed = CollectAllowedKeys(Config);
	int32 Idx = 0;
	if (TOptional<FString> BBKeyError = ValidateBBKeyReferences(RootJson, Allowed, Idx))
	{
		return BBKeyError;
	}

	int32 BTAssetIdx = 0;
	if (TOptional<FString> BTAssetError = ValidateBTAssetReferences(RootJson, ValidationBlackboard, BTAssetIdx, !bHasBlackboardInline))
	{
		return BTAssetError;
	}

	return TOptional<FString>();
}

bool FBehaviorTreeGenerator::CanExtract(UObject* Asset) const
{
	return Asset && Asset->IsA<UBehaviorTree>();
}

TSharedPtr<FJsonObject> FBehaviorTreeGenerator::Extract(UObject* Asset, bool bDiffOnly) const
{
	UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(Asset);
	if (!BehaviorTree)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("AssetType"), TEXT("BehaviorTree"));
	OutJson->SetStringField(TEXT("Name"), BehaviorTree->GetName());
	if (UPackage* Package = BehaviorTree->GetOutermost())
	{
		OutJson->SetStringField(TEXT("Path"), FPackageName::GetLongPackagePath(Package->GetName()));
	}
	if (BehaviorTree->BlackboardAsset)
	{
		OutJson->SetStringField(TEXT("Blackboard"), BehaviorTree->BlackboardAsset->GetPathName());
	}

	if (BehaviorTree->RootNode)
	{
		TMap<const UBTNode*, int32> NodeIndices;
		int32 NextNodeIndex = 0;
		BuildNodeIndexMap(BehaviorTree->RootNode, NextNodeIndex, NodeIndices);
		OutJson->SetObjectField(TEXT("Root"), ExtractBTNodeToJson(BehaviorTree->RootNode, bDiffOnly, NodeIndices));
	}

	return OutJson;
}

UClass* FBehaviorTreeGenerator::ResolveNodeClass(const FString& NodeName) const
{
	if (NodeName.IsEmpty())
	{
		return nullptr;
	}

	if (NodeName.StartsWith(TEXT("/")))
	{
		UClass* LoadedClass = LoadObject<UClass>(nullptr, *NodeName);
		return LoadedClass && LoadedClass->IsChildOf(UBTNode::StaticClass()) ? LoadedClass : nullptr;
	}

	return FClassFinderUtils::FindClassByName(NodeName, UBTNode::StaticClass(), true);
}

UBlackboardData* FBehaviorTreeGenerator::ResolveBlackboard(TSharedPtr<FJsonObject> Config, const FString& BTName, const FString& BTPath)
{
	if (!Config.IsValid())
	{
		return nullptr;
	}

	FString BBPath;
	if (Config->TryGetStringField(TEXT("Blackboard"), BBPath) && !BBPath.IsEmpty())
	{
		return LoadBlackboardDataFromPath(BBPath);
	}

	if (Config->HasTypedField<EJson::Object>(TEXT("BlackboardInline")))
	{
		const TSharedPtr<FJsonObject> InlineConfig = Config->GetObjectField(TEXT("BlackboardInline"));
		if (!InlineConfig.IsValid())
		{
			return nullptr;
		}

		FString InlineName;
		InlineConfig->TryGetStringField(TEXT("Name"), InlineName);
		if (InlineName.IsEmpty())
		{
			InlineName = FString::Printf(TEXT("BB_%s"), *BTName);
		}

		FString InlinePath;
		InlineConfig->TryGetStringField(TEXT("Path"), InlinePath);
		if (InlinePath.IsEmpty())
		{
			InlinePath = BTPath;
		}

		FBlackboardDataGenerator BlackboardGenerator;
		const FGenerationResult Result = BlackboardGenerator.Generate(InlineName, InlinePath, EGenerationAction::CreateOrUpdate, InlineConfig);
		if (!Result.IsSuccess())
		{
			UE_LOG(LogAssetFactory, Error, TEXT("Inline BB generation failed: %s"), *Result.Message);
			return nullptr;
		}

		return Cast<UBlackboardData>(Result.GeneratedAsset);
	}

	return nullptr;
}

UBTNode* FBehaviorTreeGenerator::BuildNode(TSharedPtr<FJsonObject> NodeJson, UBehaviorTree* OuterBT, UBTCompositeNode* Parent, TArray<FString>& OutWarnings)
{
	if (!NodeJson.IsValid() || !OuterBT)
	{
		return nullptr;
	}

	if (!Parent)
	{
		const TArray<TSharedPtr<FJsonValue>>* RootDecorators = nullptr;
		if (NodeJson->TryGetArrayField(TEXT("Decorators"), RootDecorators) && RootDecorators && RootDecorators->Num() > 0)
		{
			OutWarnings.Add(TEXT("Root node decorators are not supported by UE BT model; ignored"));
		}
	}

	FString NodeName;
	if (!NodeJson->TryGetStringField(TEXT("Node"), NodeName) || NodeName.IsEmpty())
	{
		return nullptr;
	}

	UClass* NodeClass = ResolveNodeClass(NodeName);
	if (!NodeClass || (!NodeClass->IsChildOf(UBTCompositeNode::StaticClass()) && !NodeClass->IsChildOf(UBTTaskNode::StaticClass())))
	{
		return nullptr;
	}

	UBTNode* Node = NewObject<UBTNode>(OuterBT, NodeClass);
	if (!Node)
	{
		return nullptr;
	}

	FString InstanceName;
	if (NodeJson->TryGetStringField(TEXT("InstanceName"), InstanceName) && !InstanceName.IsEmpty())
	{
		Node->NodeName = InstanceName;
	}

	const bool bHasPropertiesField = NodeJson->Values.Contains(TEXT("Properties"));
	const TSharedPtr<FJsonObject> PropsJson = NodeJson->HasTypedField<EJson::Object>(TEXT("Properties"))
		? NodeJson->GetObjectField(TEXT("Properties"))
		: nullptr;
	if (bHasPropertiesField && !PropsJson.IsValid())
	{
		const FString WarningName = Node->NodeName.IsEmpty() ? NodeName : Node->NodeName;
		OutWarnings.Add(FString::Printf(TEXT("Node '%s': Properties must be an object"), *WarningName));
	}
	else if (PropsJson.IsValid() && !FPropertySetterUtils::SetPropertiesFromJson(Node, PropsJson))
	{
		const FString WarningName = Node->NodeName.IsEmpty() ? Node->GetClass()->GetName() : Node->NodeName;
		OutWarnings.Add(FString::Printf(TEXT("Node '%s': failed to apply Properties"), *WarningName));
	}

	if (UBTCompositeNode* Composite = Cast<UBTCompositeNode>(Node))
	{
		const TArray<TSharedPtr<FJsonValue>>* ServicesArray = nullptr;
		if (NodeJson->TryGetArrayField(TEXT("Services"), ServicesArray))
		{
			AttachServices(Composite, ServicesArray, OuterBT, OutWarnings);
		}

		const TArray<TSharedPtr<FJsonValue>>* ChildrenArray = nullptr;
		if (NodeJson->TryGetArrayField(TEXT("Children"), ChildrenArray) && ChildrenArray)
		{
			for (const TSharedPtr<FJsonValue>& ChildValue : *ChildrenArray)
			{
				const TSharedPtr<FJsonObject> ChildJson = ChildValue.IsValid() ? ChildValue->AsObject() : nullptr;
				UBTNode* ChildNode = BuildNode(ChildJson, OuterBT, Composite, OutWarnings);
				if (!ChildNode)
				{
					return nullptr;
				}

				FBTCompositeChild NewChild;
				if (UBTCompositeNode* ChildComposite = Cast<UBTCompositeNode>(ChildNode))
				{
					NewChild.ChildComposite = ChildComposite;
				}
				else if (UBTTaskNode* ChildTask = Cast<UBTTaskNode>(ChildNode))
				{
					NewChild.ChildTask = ChildTask;
				}
				else
				{
					return nullptr;
				}

				const TArray<TSharedPtr<FJsonValue>>* DecoratorsArray = nullptr;
				if (ChildJson->TryGetArrayField(TEXT("Decorators"), DecoratorsArray) && DecoratorsArray)
				{
					for (const TSharedPtr<FJsonValue>& DecoratorValue : *DecoratorsArray)
					{
						const TSharedPtr<FJsonObject> DecoratorJson = DecoratorValue.IsValid() ? DecoratorValue->AsObject() : nullptr;
						UBTDecorator* Decorator = CreateBTAttachmentNode<UBTDecorator>(
							TEXT("Decorator"),
							DecoratorJson,
							[this](const FString& TypeName) -> UClass*
							{
								return ResolveNodeClass(TypeName);
							},
							OuterBT,
							OutWarnings);
						if (Decorator)
						{
							NewChild.Decorators.Add(Decorator);
						}
					}
				}

				Composite->Children.Add(NewChild);
			}
		}
	}

	return Node;
}

void FBehaviorTreeGenerator::AttachDecorators(UBTNode* Target, const TArray<TSharedPtr<FJsonValue>>* DecoArray, UBehaviorTree* OuterBT, TArray<FString>& OutWarnings)
{
	if (!Target || !DecoArray || !OuterBT)
	{
		return;
	}

	UBTCompositeNode* ParentNode = Target->GetParentNode();
	if (!ParentNode)
	{
		OutWarnings.Add(FString::Printf(TEXT("Decorators for node '%s' could not be attached because the node has no parent composite"), *Target->GetName()));
		return;
	}

	for (FBTCompositeChild& Child : ParentNode->Children)
	{
		if (Child.ChildComposite != Target && Child.ChildTask != Target)
		{
			continue;
		}

		for (const TSharedPtr<FJsonValue>& DecoratorValue : *DecoArray)
		{
			const TSharedPtr<FJsonObject> DecoratorJson = DecoratorValue.IsValid() ? DecoratorValue->AsObject() : nullptr;
			UBTDecorator* Decorator = CreateBTAttachmentNode<UBTDecorator>(
				TEXT("Decorator"),
				DecoratorJson,
				[this](const FString& TypeName) -> UClass*
				{
					return ResolveNodeClass(TypeName);
				},
				OuterBT,
				OutWarnings);
			if (Decorator)
			{
				Child.Decorators.Add(Decorator);
			}
		}
		return;
	}

	OutWarnings.Add(FString::Printf(TEXT("Decorators for node '%s' could not be attached because the parent link was not found"), *Target->GetName()));
}

void FBehaviorTreeGenerator::AttachServices(UBTCompositeNode* Composite, const TArray<TSharedPtr<FJsonValue>>* SvcArray, UBehaviorTree* OuterBT, TArray<FString>& OutWarnings)
{
	if (!Composite || !SvcArray || !OuterBT)
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& ServiceValue : *SvcArray)
	{
		const TSharedPtr<FJsonObject> ServiceJson = ServiceValue.IsValid() ? ServiceValue->AsObject() : nullptr;
		UBTService* Service = CreateBTAttachmentNode<UBTService>(
			TEXT("Service"),
			ServiceJson,
			[this](const FString& TypeName) -> UClass*
			{
				return ResolveNodeClass(TypeName);
			},
			OuterBT,
			OutWarnings);
		if (Service)
		{
			Composite->Services.Add(Service);
		}
	}
}

bool FBehaviorTreeGenerator::FinalizeBT(UBehaviorTree* BT)
{
	if (!BT)
	{
		return false;
	}

	uint16 ExecutionIndex = 0;
	InitializeBTNodeForAsset(*BT, nullptr, BT->RootNode, 0, ExecutionIndex);

#if WITH_EDITORONLY_DATA
	if (!BTEditorGraphLayout::RebuildEditorGraph(BT))
	{
		UE_LOG(LogAssetFactory, Error, TEXT("BehaviorTree finalization failed to rebuild editor graph for '%s'"), *BT->GetPathName());
		return false;
	}
#endif

	BT->PostEditChange();
	BT->MarkPackageDirty();
	return true;
}

TSharedPtr<FJsonObject> FBehaviorTreeGenerator::ExtractNode(const UBTNode* Node, bool bDiffOnly) const
{
	if (!Node)
	{
		return nullptr;
	}

	TMap<const UBTNode*, int32> NodeIndices;
	int32 NextNodeIndex = 0;
	BuildNodeIndexMap(Node, NextNodeIndex, NodeIndices);
	return ExtractBTNodeToJson(Node, bDiffOnly, NodeIndices);
}

TOptional<FString> FBehaviorTreeGenerator::ValidateBBKeyReferences(TSharedPtr<FJsonObject> NodeJson, const TSet<FName>& AllowedKeys, int32& InOutFakeIndex) const
{
	const int32 CurrentIndex = InOutFakeIndex++;

	auto ValidateSelectorProperties = [&](TSharedPtr<FJsonObject> JsonObject, const FString& TypeFieldName, int32 ItemIndex) -> TOptional<FString>
	{
		if (!JsonObject.IsValid())
		{
			return TOptional<FString>();
		}

		FString ClassName;
		if (!JsonObject->TryGetStringField(TypeFieldName, ClassName) || ClassName.IsEmpty())
		{
			return TOptional<FString>();
		}

		UClass* NodeClass = ResolveNodeClass(ClassName);
		if (!NodeClass)
		{
			return TOptional<FString>();
		}

		const TSharedPtr<FJsonObject> Properties = GetObjectField(JsonObject, TEXT("Properties"));
		if (!Properties.IsValid())
		{
			return TOptional<FString>();
		}

		UScriptStruct* SelectorStruct = TBaseStructure<FBlackboardKeySelector>::Get();
		for (TFieldIterator<FStructProperty> It(NodeClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FStructProperty* StructProperty = *It;
			if (!StructProperty || StructProperty->Struct != SelectorStruct)
			{
				continue;
			}

			const FString PropertyName = StructProperty->GetName();
			FString KeyName;
			if (!Properties->TryGetStringField(PropertyName, KeyName))
			{
				const TSharedPtr<FJsonObject> SelectorObject = GetObjectField(Properties, PropertyName);
				if (SelectorObject.IsValid())
				{
					SelectorObject->TryGetStringField(TEXT("SelectedKeyName"), KeyName);
				}
			}

			if (KeyName.IsEmpty())
			{
				continue;
			}

			if (!AllowedKeys.Contains(FName(*KeyName)))
			{
				TArray<FString> AvailableKeys;
				for (const FName& AllowedKey : AllowedKeys)
				{
					AvailableKeys.Add(AllowedKey.ToString());
				}
				AvailableKeys.Sort();

				return FString::Printf(
					TEXT("BB key reference error: node #%d (%s).%s references key \"%s\" not present in blackboard. Available keys: [%s]"),
					ItemIndex,
					*ClassName,
					*PropertyName,
					*KeyName,
					*FString::Join(AvailableKeys, TEXT(", ")));
			}
		}

		return TOptional<FString>();
	};

	if (!NodeJson.IsValid())
	{
		return FString::Printf(TEXT("Node #%d: Invalid node object"), CurrentIndex);
	}

	if (TOptional<FString> NodeError = ValidateSelectorProperties(NodeJson, TEXT("Node"), CurrentIndex); NodeError.IsSet())
	{
		return NodeError;
	}

	const TArray<TSharedPtr<FJsonValue>>* DecoratorsArray = nullptr;
	if (NodeJson->TryGetArrayField(TEXT("Decorators"), DecoratorsArray) && DecoratorsArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *DecoratorsArray)
		{
			const int32 DecoratorIndex = InOutFakeIndex++;
			const TSharedPtr<FJsonObject> DecoratorJson = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!DecoratorJson.IsValid())
			{
				return FString::Printf(TEXT("Decorator #%d: Invalid decorator object"), DecoratorIndex);
			}

			if (TOptional<FString> DecoratorError = ValidateSelectorProperties(DecoratorJson, TEXT("Type"), DecoratorIndex); DecoratorError.IsSet())
			{
				return DecoratorError;
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ServicesArray = nullptr;
	if (NodeJson->TryGetArrayField(TEXT("Services"), ServicesArray) && ServicesArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *ServicesArray)
		{
			const int32 ServiceIndex = InOutFakeIndex++;
			const TSharedPtr<FJsonObject> ServiceJson = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!ServiceJson.IsValid())
			{
				return FString::Printf(TEXT("Service #%d: Invalid service object"), ServiceIndex);
			}

			if (TOptional<FString> ServiceError = ValidateSelectorProperties(ServiceJson, TEXT("Type"), ServiceIndex); ServiceError.IsSet())
			{
				return ServiceError;
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ChildrenArray = nullptr;
	if (NodeJson->TryGetArrayField(TEXT("Children"), ChildrenArray) && ChildrenArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *ChildrenArray)
		{
			const TSharedPtr<FJsonObject> ChildJson = Value.IsValid() ? Value->AsObject() : nullptr;
			if (TOptional<FString> ChildError = ValidateBBKeyReferences(ChildJson, AllowedKeys, InOutFakeIndex); ChildError.IsSet())
			{
				return ChildError;
			}
		}
	}

	return TOptional<FString>();
}

TOptional<FString> FBehaviorTreeGenerator::ValidateBTAssetReferences(
	TSharedPtr<FJsonObject> NodeJson,
	const UBlackboardData* ParentBlackboard,
	int32& InOutFakeIndex,
	bool bValidateBlackboardCompatibility) const
{
	const int32 CurrentIndex = InOutFakeIndex++;

	auto ValidateBehaviorTreeProperties = [&](TSharedPtr<FJsonObject> JsonObject, const FString& TypeFieldName, int32 ItemIndex) -> TOptional<FString>
	{
		if (!JsonObject.IsValid())
		{
			return TOptional<FString>();
		}

		FString ClassName;
		if (!JsonObject->TryGetStringField(TypeFieldName, ClassName) || ClassName.IsEmpty())
		{
			return TOptional<FString>();
		}

		UClass* NodeClass = ResolveNodeClass(ClassName);
		if (!NodeClass)
		{
			return TOptional<FString>();
		}

		for (TFieldIterator<FObjectPropertyBase> It(NodeClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FObjectPropertyBase* ObjectProperty = *It;
			if (!ObjectProperty ||
				!ObjectProperty->PropertyClass ||
				!ObjectProperty->PropertyClass->IsChildOf(UBehaviorTree::StaticClass()) ||
				!ObjectProperty->HasAnyPropertyFlags(CPF_Edit))
			{
				continue;
			}

			const FString PropertyName = ObjectProperty->GetName();
			const TSharedPtr<FJsonObject> Properties = GetObjectField(JsonObject, TEXT("Properties"));
			UBehaviorTree* ChildBehaviorTree = nullptr;
			FString BehaviorAssetPath;
			if (Properties.IsValid() && Properties->Values.Contains(PropertyName))
			{
				if (!Properties->TryGetStringField(PropertyName, BehaviorAssetPath) || BehaviorAssetPath.IsEmpty())
				{
					return FString::Printf(
						TEXT("BT asset reference error: node #%d (%s).%s must be a non-empty BehaviorTree asset path"),
						ItemIndex,
						*ClassName,
						*PropertyName);
				}

				ChildBehaviorTree = LoadBehaviorTreeFromPath(BehaviorAssetPath);
				if (!ChildBehaviorTree)
				{
					return FString::Printf(
						TEXT("BT asset reference error: node #%d (%s).%s could not load BehaviorTree asset '%s'"),
						ItemIndex,
						*ClassName,
						*PropertyName,
						*BehaviorAssetPath);
				}
			}
			else
			{
				const UObject* DefaultObject = NodeClass->GetDefaultObject();
				ChildBehaviorTree = DefaultObject
					? Cast<UBehaviorTree>(ObjectProperty->GetObjectPropertyValue_InContainer(DefaultObject))
					: nullptr;

				if (!ChildBehaviorTree)
				{
					return FString::Printf(
						TEXT("BT asset reference error: node #%d (%s).%s must define a BehaviorTree asset path"),
						ItemIndex,
						*ClassName,
						*PropertyName);
				}

				BehaviorAssetPath = ChildBehaviorTree->GetPathName();
			}

			if (bValidateBlackboardCompatibility && !IsBlackboardCompatibleWithSubtree(ParentBlackboard, ChildBehaviorTree->BlackboardAsset))
			{
				const FString ParentBlackboardName = ParentBlackboard ? ParentBlackboard->GetPathName() : TEXT("<none>");
				const FString ChildBlackboardName = ChildBehaviorTree->BlackboardAsset ? ChildBehaviorTree->BlackboardAsset->GetPathName() : TEXT("<none>");
				return FString::Printf(
					TEXT("BT asset reference error: node #%d (%s).%s references BehaviorTree '%s' with incompatible blackboard '%s'; parent blackboard is '%s'"),
					ItemIndex,
					*ClassName,
					*PropertyName,
					*BehaviorAssetPath,
					*ChildBlackboardName,
					*ParentBlackboardName);
			}
		}

		return TOptional<FString>();
	};

	if (!NodeJson.IsValid())
	{
		return FString::Printf(TEXT("Node #%d: Invalid node object"), CurrentIndex);
	}

	if (TOptional<FString> NodeError = ValidateBehaviorTreeProperties(NodeJson, TEXT("Node"), CurrentIndex); NodeError.IsSet())
	{
		return NodeError;
	}

	const TArray<TSharedPtr<FJsonValue>>* DecoratorsArray = nullptr;
	if (NodeJson->TryGetArrayField(TEXT("Decorators"), DecoratorsArray) && DecoratorsArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *DecoratorsArray)
		{
			const int32 DecoratorIndex = InOutFakeIndex++;
			const TSharedPtr<FJsonObject> DecoratorJson = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!DecoratorJson.IsValid())
			{
				return FString::Printf(TEXT("Decorator #%d: Invalid decorator object"), DecoratorIndex);
			}

			if (TOptional<FString> DecoratorError = ValidateBehaviorTreeProperties(DecoratorJson, TEXT("Type"), DecoratorIndex); DecoratorError.IsSet())
			{
				return DecoratorError;
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ServicesArray = nullptr;
	if (NodeJson->TryGetArrayField(TEXT("Services"), ServicesArray) && ServicesArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *ServicesArray)
		{
			const int32 ServiceIndex = InOutFakeIndex++;
			const TSharedPtr<FJsonObject> ServiceJson = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!ServiceJson.IsValid())
			{
				return FString::Printf(TEXT("Service #%d: Invalid service object"), ServiceIndex);
			}

			if (TOptional<FString> ServiceError = ValidateBehaviorTreeProperties(ServiceJson, TEXT("Type"), ServiceIndex); ServiceError.IsSet())
			{
				return ServiceError;
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ChildrenArray = nullptr;
	if (NodeJson->TryGetArrayField(TEXT("Children"), ChildrenArray) && ChildrenArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *ChildrenArray)
		{
			const TSharedPtr<FJsonObject> ChildJson = Value.IsValid() ? Value->AsObject() : nullptr;
			if (TOptional<FString> ChildError = ValidateBTAssetReferences(ChildJson, ParentBlackboard, InOutFakeIndex, bValidateBlackboardCompatibility); ChildError.IsSet())
			{
				return ChildError;
			}
		}
	}

	return TOptional<FString>();
}
