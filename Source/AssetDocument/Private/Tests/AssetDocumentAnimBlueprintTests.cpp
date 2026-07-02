// Copyright ProjectRPG. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetDocumentService.h"
#include "Graphs/AssetDocumentAnimationGraphRuntime.h"
#include "Graphs/AssetDocumentGraphTypes.h"
#include "Profiles/AnimBlueprintAssetDocumentProfile.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"

namespace
{
bool HasBodyKey(const TArray<FName>& BodyKeys, const TCHAR* Name)
{
	return BodyKeys.Contains(FName(Name));
}

TSharedRef<FJsonValue> MakeEmptyBodyValue()
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithNullRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetField(RegionName, MakeShared<FJsonValueNull>());
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithEmptyArrayRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(RegionName, TArray<TSharedPtr<FJsonValue>>());
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithEmptyObjectRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(RegionName, MakeShared<FJsonObject>());
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithNonEmptyDeferredRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> AuthoredNode = MakeShared<FJsonObject>();
	AuthoredNode->SetStringField(TEXT("Node"), TEXT("Bad"));
	TArray<TSharedPtr<FJsonValue>> RegionEntries;
	RegionEntries.Add(MakeShared<FJsonValueObject>(AuthoredNode));
	Body->SetArrayField(RegionName, MoveTemp(RegionEntries));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonObject> MakeCanonicalAnimGraphObject()
{
	TSharedRef<FJsonObject> Graph = MakeShared<FJsonObject>();
	Graph->SetStringField(TEXT("Id"), TEXT("AnimGraph"));
	Graph->SetStringField(TEXT("Kind"), TEXT("AnimGraph"));
	Graph->SetField(TEXT("Owner"), MakeShared<FJsonValueNull>());
	Graph->SetArrayField(TEXT("Nodes"), {});
	Graph->SetArrayField(TEXT("Links"), {});
	Graph->SetArrayField(TEXT("Subgraphs"), {});

	TSharedRef<FJsonObject> Region = MakeShared<FJsonObject>();
	Region->SetArrayField(TEXT("Graphs"), {MakeShared<FJsonValueObject>(Graph)});
	return Region;
}

TSharedRef<FJsonObject> MakeEmptyGraphRegionObject()
{
	TSharedRef<FJsonObject> Region = MakeShared<FJsonObject>();
	Region->SetArrayField(TEXT("Graphs"), {});
	return Region;
}

TSharedRef<FJsonObject> MakeAnimLayerGraph(const TCHAR* LayerName)
{
	TSharedRef<FJsonObject> Graph = MakeShared<FJsonObject>();
	Graph->SetStringField(TEXT("Id"), LayerName);
	Graph->SetStringField(TEXT("Name"), LayerName);
	Graph->SetStringField(TEXT("Kind"), TEXT("AnimLayer"));
	Graph->SetArrayField(TEXT("Nodes"), {});
	Graph->SetArrayField(TEXT("Links"), {});
	Graph->SetArrayField(TEXT("Subgraphs"), {});
	return Graph;
}

TSharedRef<FJsonObject> MakeAnimLayerGraphRegion(std::initializer_list<TSharedRef<FJsonObject>> Graphs)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const TSharedRef<FJsonObject>& Graph : Graphs)
	{
		Values.Add(MakeShared<FJsonValueObject>(Graph));
	}
	TSharedRef<FJsonObject> Region = MakeShared<FJsonObject>();
	Region->SetArrayField(TEXT("Graphs"), Values);
	return Region;
}

TSharedRef<FJsonValue> MakeBodyWithAnimLayerGraphs(std::initializer_list<TSharedRef<FJsonObject>> Graphs)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("AnimLayers"), MakeAnimLayerGraphRegion(Graphs));
	return MakeShared<FJsonValueObject>(Body);
}

TArray<TSharedPtr<FJsonValue>> MakeLegacyAnimGraphArray()
{
	TSharedRef<FJsonObject> Graph = MakeShared<FJsonObject>();
	Graph->SetStringField(TEXT("Name"), TEXT("AnimGraph"));
	Graph->SetArrayField(TEXT("Nodes"), {});
	return {MakeShared<FJsonValueObject>(Graph)};
}

TSharedRef<FJsonValue> MakeBodyWithCanonicalAnimGraph()
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("AnimGraph"), MakeCanonicalAnimGraphObject());
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithLegacyAnimGraphArray()
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("AnimGraph"), MakeLegacyAnimGraphArray());
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithUnsupportedAnimGraphNode()
{
	TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("Id"), TEXT("IdlePlayer"));
	Node->SetStringField(TEXT("Kind"), TEXT("SequencePlayer"));
	Node->SetStringField(TEXT("Class"), TEXT("/Script/AnimGraph.AnimGraphNode_DoesNotExist"));

	TSharedRef<FJsonObject> OutputPose = MakeShared<FJsonObject>();
	OutputPose->SetStringField(TEXT("Node"), TEXT("IdlePlayer"));
	OutputPose->SetStringField(TEXT("Pin"), TEXT("Pose"));
	TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
	Metadata->SetObjectField(TEXT("OutputPose"), OutputPose);

	TSharedRef<FJsonObject> Graph = MakeShared<FJsonObject>();
	Graph->SetStringField(TEXT("Id"), TEXT("AnimGraph"));
	Graph->SetStringField(TEXT("Kind"), TEXT("AnimGraph"));
	Graph->SetField(TEXT("Owner"), MakeShared<FJsonValueNull>());
	Graph->SetObjectField(TEXT("Metadata"), Metadata);
	Graph->SetArrayField(TEXT("Nodes"), {MakeShared<FJsonValueObject>(Node)});
	Graph->SetArrayField(TEXT("Links"), {});
	Graph->SetArrayField(TEXT("Subgraphs"), {});

	TSharedRef<FJsonObject> Region = MakeShared<FJsonObject>();
	Region->SetArrayField(TEXT("Graphs"), {MakeShared<FJsonValueObject>(Graph)});

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("AnimGraph"), Region);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithAnimGraphSubgraph()
{
	TSharedRef<FJsonObject> Region = MakeCanonicalAnimGraphObject();
	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	Region->TryGetArrayField(TEXT("Graphs"), Graphs);
	TSharedPtr<FJsonObject> RootGraph = Graphs && Graphs->Num() == 1 && (*Graphs)[0].IsValid()
		? (*Graphs)[0]->AsObject()
		: nullptr;

	TSharedRef<FJsonObject> Subgraph = MakeShared<FJsonObject>();
	Subgraph->SetStringField(TEXT("Id"), TEXT("NestedPose"));
	Subgraph->SetStringField(TEXT("Kind"), TEXT("StatePose"));
	Subgraph->SetField(TEXT("Owner"), MakeShared<FJsonValueNull>());
	Subgraph->SetArrayField(TEXT("Nodes"), {});
	Subgraph->SetArrayField(TEXT("Links"), {});
	Subgraph->SetArrayField(TEXT("Subgraphs"), {});
	if (RootGraph.IsValid())
	{
		RootGraph->SetArrayField(TEXT("Subgraphs"), {MakeShared<FJsonValueObject>(Subgraph)});
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("AnimGraph"), Region);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonObject> MakeAnimGraphWithSequencePlayer(
	const TCHAR* NodeId,
	bool bIncludeOutputPose = true,
	const FString& SequenceAssetPath = FString(),
	bool bIncludeKind = true)
{
	TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("Id"), NodeId);
	if (bIncludeKind)
	{
		Node->SetStringField(TEXT("Kind"), TEXT("SequencePlayer"));
	}
	Node->SetStringField(TEXT("Class"), TEXT("/Script/AnimGraph.AnimGraphNode_SequencePlayer"));
	if (!SequenceAssetPath.IsEmpty())
	{
		TSharedRef<FJsonObject> AnimNodeFields = MakeShared<FJsonObject>();
		AnimNodeFields->SetStringField(TEXT("Sequence"), SequenceAssetPath);
		TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
		Fields->SetObjectField(TEXT("Node"), AnimNodeFields);
		Node->SetObjectField(TEXT("Fields"), Fields);
	}
	TSharedRef<FJsonObject> Position = MakeShared<FJsonObject>();
	Position->SetNumberField(TEXT("X"), 120.0);
	Position->SetNumberField(TEXT("Y"), 40.0);
	Node->SetObjectField(TEXT("Position"), Position);

	TSharedRef<FJsonObject> Graph = MakeShared<FJsonObject>();
	Graph->SetStringField(TEXT("Id"), TEXT("AnimGraph"));
	Graph->SetStringField(TEXT("Kind"), TEXT("AnimGraph"));
	Graph->SetField(TEXT("Owner"), MakeShared<FJsonValueNull>());
	if (bIncludeOutputPose)
	{
		TSharedRef<FJsonObject> OutputPose = MakeShared<FJsonObject>();
		OutputPose->SetStringField(TEXT("Node"), NodeId);
		OutputPose->SetStringField(TEXT("Pin"), TEXT("Pose"));
		TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
		Metadata->SetObjectField(TEXT("OutputPose"), OutputPose);
		Graph->SetObjectField(TEXT("Metadata"), Metadata);
	}
	Graph->SetArrayField(TEXT("Nodes"), {MakeShared<FJsonValueObject>(Node)});
	Graph->SetArrayField(TEXT("Links"), {});
	Graph->SetArrayField(TEXT("Subgraphs"), {});

	TSharedRef<FJsonObject> Region = MakeShared<FJsonObject>();
	Region->SetArrayField(TEXT("Graphs"), {MakeShared<FJsonValueObject>(Graph)});
	return Region;
}

TSharedRef<FJsonObject> MakeAnimGraphWithTwoSequencePlayers(
	const TCHAR* FirstNodeId,
	const TCHAR* SecondNodeId,
	const TCHAR* OutputPoseNodeId,
	const FString& SequenceAssetPath = FString())
{
	TSharedRef<FJsonObject> Region = MakeAnimGraphWithSequencePlayer(FirstNodeId, true, SequenceAssetPath);

	const TSharedPtr<FJsonObject> Graph = Region->GetArrayField(TEXT("Graphs"))[0]->AsObject();
	TArray<TSharedPtr<FJsonValue>> Nodes = Graph->GetArrayField(TEXT("Nodes"));
	const TSharedRef<FJsonObject> SecondNode = MakeShared<FJsonObject>();
	SecondNode->SetStringField(TEXT("Id"), SecondNodeId);
	SecondNode->SetStringField(TEXT("Class"), TEXT("/Script/AnimGraph.AnimGraphNode_SequencePlayer"));
	if (!SequenceAssetPath.IsEmpty())
	{
		TSharedRef<FJsonObject> AnimNodeFields = MakeShared<FJsonObject>();
		AnimNodeFields->SetStringField(TEXT("Sequence"), SequenceAssetPath);
		TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
		Fields->SetObjectField(TEXT("Node"), AnimNodeFields);
		SecondNode->SetObjectField(TEXT("Fields"), Fields);
	}
	TSharedRef<FJsonObject> Position = MakeShared<FJsonObject>();
	Position->SetNumberField(TEXT("X"), 360.0);
	Position->SetNumberField(TEXT("Y"), 40.0);
	SecondNode->SetObjectField(TEXT("Position"), Position);
	Nodes.Add(MakeShared<FJsonValueObject>(SecondNode));
	Graph->SetArrayField(TEXT("Nodes"), Nodes);

	TSharedPtr<FJsonObject> Metadata = Graph->GetObjectField(TEXT("Metadata"));
	TSharedPtr<FJsonObject> OutputPose = Metadata->GetObjectField(TEXT("OutputPose"));
	OutputPose->SetStringField(TEXT("Node"), OutputPoseNodeId);
	return Region;
}

TSharedRef<FJsonObject> MakeAnimGraphWithOutputPosePin(
	const TCHAR* NodeId,
	const TCHAR* PinName)
{
	TSharedRef<FJsonObject> Region = MakeAnimGraphWithSequencePlayer(NodeId);
	TSharedPtr<FJsonObject> Graph = Region->GetArrayField(TEXT("Graphs"))[0]->AsObject();
	TSharedPtr<FJsonObject> Metadata = Graph->GetObjectField(TEXT("Metadata"));
	TSharedPtr<FJsonObject> OutputPose = Metadata->GetObjectField(TEXT("OutputPose"));
	OutputPose->SetStringField(TEXT("Pin"), PinName);
	return Region;
}

TSharedRef<FJsonValue> MakeBodyWithAnimGraphSequencePlayerMissingOutputPose()
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("AnimGraph"), MakeAnimGraphWithSequencePlayer(TEXT("IdlePlayer"), false));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedPtr<FJsonObject> MakeStateMachineState(const TCHAR* Id)
{
	TSharedPtr<FJsonObject> State = MakeShared<FJsonObject>();
	State->SetStringField(TEXT("Id"), Id);
	return State;
}

TSharedPtr<FJsonObject> MakeStateMachineTransition(const TCHAR* Id, const TCHAR* From, const TCHAR* To)
{
	TSharedPtr<FJsonObject> Transition = MakeShared<FJsonObject>();
	Transition->SetStringField(TEXT("Id"), Id);
	Transition->SetStringField(TEXT("From"), From);
	Transition->SetStringField(TEXT("To"), To);
	Transition->SetStringField(TEXT("Rule"), Id);
	return Transition;
}

TSharedPtr<FJsonObject> MakeStateMachine(
	const TCHAR* Name,
	std::initializer_list<TSharedPtr<FJsonObject>> States,
	std::initializer_list<TSharedPtr<FJsonObject>> Transitions)
{
	TSharedPtr<FJsonObject> Machine = MakeShared<FJsonObject>();
	Machine->SetStringField(TEXT("Name"), Name);
	Machine->SetStringField(TEXT("EntryState"), States.size() > 0 ? (*States.begin())->GetStringField(TEXT("Id")) : FString());

	TArray<TSharedPtr<FJsonValue>> StateValues;
	for (const TSharedPtr<FJsonObject>& State : States)
	{
		StateValues.Add(MakeShared<FJsonValueObject>(State.ToSharedRef()));
	}
	Machine->SetArrayField(TEXT("States"), StateValues);

	TArray<TSharedPtr<FJsonValue>> TransitionValues;
	for (const TSharedPtr<FJsonObject>& Transition : Transitions)
	{
		TransitionValues.Add(MakeShared<FJsonValueObject>(Transition.ToSharedRef()));
	}
	Machine->SetArrayField(TEXT("Transitions"), TransitionValues);
	return Machine;
}

TArray<TSharedPtr<FJsonValue>> MakeStateMachineArray(std::initializer_list<TSharedPtr<FJsonObject>> Machines)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const TSharedPtr<FJsonObject>& Machine : Machines)
	{
		Values.Add(MakeShared<FJsonValueObject>(Machine.ToSharedRef()));
	}
	return Values;
}

TSharedPtr<FJsonObject> MakeTransitionGraph(const TCHAR* StateMachine, const TCHAR* Transition, bool bWithUnsupportedNode = false)
{
	TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();
	Graph->SetStringField(TEXT("StateMachine"), StateMachine);
	Graph->SetStringField(TEXT("Transition"), Transition);

	TArray<TSharedPtr<FJsonValue>> Nodes;
	if (bWithUnsupportedNode)
	{
		TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
		Node->SetStringField(TEXT("Id"), TEXT("CanEnter"));
		Node->SetStringField(TEXT("Kind"), TEXT("BoolLiteral"));
		Nodes.Add(MakeShared<FJsonValueObject>(Node));
	}
	Graph->SetArrayField(TEXT("Nodes"), Nodes);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetField(TEXT("Node"), MakeShared<FJsonValueNull>());
	Result->SetStringField(TEXT("Pin"), TEXT("CanEnterTransition"));
	Graph->SetObjectField(TEXT("Result"), Result);
	return Graph;
}

TArray<TSharedPtr<FJsonValue>> MakeTransitionGraphArray(std::initializer_list<TSharedPtr<FJsonObject>> Graphs)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const TSharedPtr<FJsonObject>& Graph : Graphs)
	{
		Values.Add(MakeShared<FJsonValueObject>(Graph.ToSharedRef()));
	}
	return Values;
}

TSharedRef<FJsonValue> MakeBodyWithStateMachines(std::initializer_list<TSharedPtr<FJsonObject>> Machines)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("StateMachines"), MakeStateMachineArray(Machines));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonObject> MakeStateMachineGraph(
	const TCHAR* Id,
	bool bDuplicateState = false,
	bool bUnknownEndpoint = false)
{
	TSharedRef<FJsonObject> Graph = MakeShared<FJsonObject>();
	Graph->SetStringField(TEXT("Id"), Id);
	Graph->SetStringField(TEXT("Kind"), TEXT("StateMachine"));
	Graph->SetField(TEXT("Owner"), MakeShared<FJsonValueNull>());
	TSharedRef<FJsonObject> GraphPosition = MakeShared<FJsonObject>();
	GraphPosition->SetNumberField(TEXT("X"), 0.0);
	GraphPosition->SetNumberField(TEXT("Y"), 0.0);
	Graph->SetObjectField(TEXT("Position"), GraphPosition);

	TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
	Metadata->SetStringField(TEXT("EntryState"), TEXT("Idle"));
	Graph->SetObjectField(TEXT("Metadata"), Metadata);

	auto MakeNode = [](const TCHAR* NodeId, const TCHAR* Kind, const TCHAR* ClassPath, double X, double Y)
	{
		TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
		Node->SetStringField(TEXT("Id"), NodeId);
		Node->SetStringField(TEXT("Kind"), Kind);
		Node->SetStringField(TEXT("Class"), ClassPath);
		TSharedRef<FJsonObject> Position = MakeShared<FJsonObject>();
		Position->SetNumberField(TEXT("X"), X);
		Position->SetNumberField(TEXT("Y"), Y);
		Node->SetObjectField(TEXT("Position"), Position);
		return Node;
	};

	TArray<TSharedPtr<FJsonValue>> Nodes;
	Nodes.Add(MakeShared<FJsonValueObject>(MakeNode(TEXT("Idle"), TEXT("State"), TEXT("/Script/AnimGraph.AnimStateNode"), 0, 0)));
	Nodes.Add(MakeShared<FJsonValueObject>(MakeNode(bDuplicateState ? TEXT("Idle") : TEXT("Run"), TEXT("State"), TEXT("/Script/AnimGraph.AnimStateNode"), 240, 0)));
	Nodes.Add(MakeShared<FJsonValueObject>(MakeNode(TEXT("IdleToRun"), TEXT("Transition"), TEXT("/Script/AnimGraph.AnimStateTransitionNode"), 120, 0)));
	Graph->SetArrayField(TEXT("Nodes"), Nodes);

	auto MakeLink = [](const TCHAR* FromNode, const TCHAR* FromPin, const TCHAR* ToNode, const TCHAR* ToPin)
	{
		TSharedRef<FJsonObject> From = MakeShared<FJsonObject>();
		From->SetStringField(TEXT("Node"), FromNode);
		From->SetStringField(TEXT("Pin"), FromPin);
		TSharedRef<FJsonObject> To = MakeShared<FJsonObject>();
		To->SetStringField(TEXT("Node"), ToNode);
		To->SetStringField(TEXT("Pin"), ToPin);
		TSharedRef<FJsonObject> Link = MakeShared<FJsonObject>();
		Link->SetObjectField(TEXT("From"), From);
		Link->SetObjectField(TEXT("To"), To);
		return Link;
	};

	TArray<TSharedPtr<FJsonValue>> Links;
	Links.Add(MakeShared<FJsonValueObject>(MakeLink(TEXT("Idle"), TEXT("Out"), TEXT("IdleToRun"), TEXT("In"))));
	Links.Add(MakeShared<FJsonValueObject>(MakeLink(TEXT("IdleToRun"), TEXT("Out"), bUnknownEndpoint ? TEXT("Missing") : TEXT("Run"), TEXT("In"))));
	Graph->SetArrayField(TEXT("Links"), Links);

	TSharedRef<FJsonObject> RuleGraph = MakeShared<FJsonObject>();
	RuleGraph->SetStringField(TEXT("Id"), TEXT("CanStartRunning"));
	RuleGraph->SetStringField(TEXT("Kind"), TEXT("TransitionRule"));
	TSharedRef<FJsonObject> Owner = MakeShared<FJsonObject>();
	Owner->SetStringField(TEXT("Transition"), TEXT("IdleToRun"));
	RuleGraph->SetObjectField(TEXT("Owner"), Owner);
	TSharedRef<FJsonObject> RuleResult = MakeShared<FJsonObject>();
	RuleResult->SetField(TEXT("Node"), MakeShared<FJsonValueNull>());
	RuleResult->SetStringField(TEXT("Pin"), TEXT("CanEnterTransition"));
	TSharedRef<FJsonObject> RuleMetadata = MakeShared<FJsonObject>();
	RuleMetadata->SetObjectField(TEXT("Result"), RuleResult);
	RuleGraph->SetObjectField(TEXT("Metadata"), RuleMetadata);
	RuleGraph->SetArrayField(TEXT("Nodes"), {});
	RuleGraph->SetArrayField(TEXT("Links"), {});
	RuleGraph->SetArrayField(TEXT("Subgraphs"), {});
	TSharedRef<FJsonObject> StatePoseGraph = MakeShared<FJsonObject>();
	StatePoseGraph->SetStringField(TEXT("Id"), TEXT("IdlePose"));
	StatePoseGraph->SetStringField(TEXT("Kind"), TEXT("StatePose"));
	TSharedRef<FJsonObject> StatePoseOwner = MakeShared<FJsonObject>();
	StatePoseOwner->SetStringField(TEXT("State"), TEXT("Idle"));
	StatePoseGraph->SetObjectField(TEXT("Owner"), StatePoseOwner);
	StatePoseGraph->SetArrayField(TEXT("Nodes"), {});
	StatePoseGraph->SetArrayField(TEXT("Links"), {});
	StatePoseGraph->SetArrayField(TEXT("Subgraphs"), {});

	Graph->SetArrayField(TEXT("Subgraphs"), {MakeShared<FJsonValueObject>(StatePoseGraph), MakeShared<FJsonValueObject>(RuleGraph)});
	return Graph;
}

TSharedRef<FJsonObject> MakeStateMachineGraphWithBrokenStatePoseField(const TCHAR* Id)
{
	TSharedRef<FJsonObject> Graph = MakeStateMachineGraph(Id);
	Graph->GetObjectField(TEXT("Metadata"))->SetStringField(TEXT("EntryState"), TEXT("Run"));

	const TArray<TSharedPtr<FJsonValue>>* Subgraphs = nullptr;
	if (Graph->TryGetArrayField(TEXT("Subgraphs"), Subgraphs) && Subgraphs)
	{
		for (const TSharedPtr<FJsonValue>& SubgraphValue : *Subgraphs)
		{
			const TSharedPtr<FJsonObject> Subgraph = SubgraphValue.IsValid() ? SubgraphValue->AsObject() : nullptr;
			FString Kind;
			if (!Subgraph.IsValid() || !Subgraph->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("StatePose"))
			{
				continue;
			}

			Subgraph->SetStringField(TEXT("Id"), TEXT("BrokenPose"));
			TSharedRef<FJsonObject> BadNode = MakeShared<FJsonObject>();
			BadNode->SetStringField(TEXT("Id"), TEXT("BrokenPosePlayer"));
			BadNode->SetStringField(TEXT("Kind"), TEXT("SequencePlayer"));
			BadNode->SetStringField(TEXT("Class"), TEXT("/Script/AnimGraph.AnimGraphNode_SequencePlayer"));
			TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
			Fields->SetBoolField(TEXT("DefinitelyMissingProperty"), true);
			BadNode->SetObjectField(TEXT("Fields"), Fields);
			Subgraph->SetArrayField(TEXT("Nodes"), {MakeShared<FJsonValueObject>(BadNode)});
			break;
		}
	}

	return Graph;
}

TSharedRef<FJsonObject> MakeStateMachinesGraphRegion(std::initializer_list<TSharedRef<FJsonObject>> Graphs)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const TSharedRef<FJsonObject>& Graph : Graphs)
	{
		Values.Add(MakeShared<FJsonValueObject>(Graph));
	}
	TSharedRef<FJsonObject> Region = MakeShared<FJsonObject>();
	Region->SetArrayField(TEXT("Graphs"), Values);
	return Region;
}

TSharedRef<FJsonValue> MakeBodyWithStateMachineGraphs(std::initializer_list<TSharedRef<FJsonObject>> Graphs)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("StateMachines"), MakeStateMachinesGraphRegion(Graphs));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithTransitionGraphs(std::initializer_list<TSharedPtr<FJsonObject>> Graphs)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("TransitionGraphs"), MakeTransitionGraphArray(Graphs));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithStateMachineAndTransitionGraph()
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(
		TEXT("StateMachines"),
		MakeStateMachineArray({
			MakeStateMachine(
				TEXT("Locomotion"),
				{MakeStateMachineState(TEXT("Idle")), MakeStateMachineState(TEXT("Run"))},
				{MakeStateMachineTransition(TEXT("IdleToRun"), TEXT("Idle"), TEXT("Run"))})}));
	Body->SetArrayField(
		TEXT("TransitionGraphs"),
		MakeTransitionGraphArray({MakeTransitionGraph(TEXT("Locomotion"), TEXT("IdleToRun"))}));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedPtr<FJsonObject> MakeParentAssetOverride(const FString& ParentNodeGuid, const FString& AssetPath)
{
	TSharedPtr<FJsonObject> Override = MakeShared<FJsonObject>();
	Override->SetStringField(TEXT("ParentNodeGuid"), ParentNodeGuid);
	TSharedRef<FJsonObject> AssetRef = MakeShared<FJsonObject>();
	AssetRef->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	AssetRef->SetStringField(TEXT("Path"), AssetPath);
	Override->SetObjectField(TEXT("NewAsset"), AssetRef);
	return Override;
}

TSharedPtr<FJsonObject> MakeParentAssetOverrideByNode(const FString& Node, const FString& AssetPath)
{
	TSharedPtr<FJsonObject> Override = MakeShared<FJsonObject>();
	Override->SetStringField(TEXT("Node"), Node);
	TSharedRef<FJsonObject> AssetRef = MakeShared<FJsonObject>();
	AssetRef->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	AssetRef->SetStringField(TEXT("Path"), AssetPath);
	Override->SetObjectField(TEXT("NewAsset"), AssetRef);
	return Override;
}

TArray<TSharedPtr<FJsonValue>> MakeParentAssetOverrideArray(std::initializer_list<TSharedPtr<FJsonObject>> Overrides)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const TSharedPtr<FJsonObject>& Override : Overrides)
	{
		Values.Add(MakeShared<FJsonValueObject>(Override.ToSharedRef()));
	}
	return Values;
}

TSharedRef<FJsonValue> MakeBodyWithParentAssetOverrides(std::initializer_list<TSharedPtr<FJsonObject>> Overrides)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("ParentAssetOverrides"), MakeParentAssetOverrideArray(Overrides));
	return MakeShared<FJsonValueObject>(Body);
}

FGuid MakeExpectedManagedAnimGraphNodeGuid(const FString& NodeId)
{
	FAssetDocumentGraphSpec Graph;
	Graph.Id = TEXT("AnimGraph");
	Graph.Kind = TEXT("AnimGraph");
	FAssetDocumentNodeSpec Node;
	Node.Id = NodeId;
	return FAssetDocumentAnimationGraphRuntime::MakeManagedNodeGuid(Graph, Node);
}

TSharedRef<FJsonValue> MakeBodyWithNonEmptyDeferredObjectRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> RegionObject = MakeShared<FJsonObject>();
	RegionObject->SetStringField(TEXT("Node"), TEXT("Bad"));
	Body->SetObjectField(RegionName, RegionObject);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithScalarDeferredRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(RegionName, TEXT("unsupported"));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithUnknownKey(const TCHAR* UnknownKey = TEXT("UnexpectedGraph"))
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(UnknownKey, TEXT("unsupported"));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonObject> MakeClassRef(const FString& ClassPath)
{
	TSharedRef<FJsonObject> ClassRef = MakeShared<FJsonObject>();
	ClassRef->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ClassRef->SetStringField(TEXT("Class"), ClassPath);
	return ClassRef;
}

TSharedRef<FJsonObject> MakeAssetRef(const FString& AssetPath)
{
	TSharedRef<FJsonObject> AssetRef = MakeShared<FJsonObject>();
	AssetRef->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	AssetRef->SetStringField(TEXT("Path"), AssetPath);
	return AssetRef;
}

TSharedRef<FJsonValue> MakeBodyWithMissingParentClass()
{
	TSharedRef<FJsonObject> ParentClass = MakeShared<FJsonObject>();
	ParentClass->SetStringField(TEXT("Kind"), TEXT("ClassRef"));

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), ParentClass);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithParentClass(const FString& ClassPath)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), MakeClassRef(ClassPath));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithTemplateAndTargetSkeleton()
{
	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetBoolField(TEXT("bIsTemplate"), true);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("Template"), Template);
	Body->SetObjectField(
		TEXT("TargetSkeleton"),
		MakeAssetRef(TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton")));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithTargetSkeleton(
	const FString& TargetSkeletonPath = TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh_Skeleton.DefaultSkeletalMesh_Skeleton"))
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("TargetSkeleton"), MakeAssetRef(TargetSkeletonPath));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithNonTemplateMissingTargetSkeleton()
{
	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetBoolField(TEXT("bIsTemplate"), false);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("Template"), Template);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithTargetSkeletonAndPreviewApplicationMethod(const FString& Method)
{
	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintApplicationMethod"), Method);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(
		TEXT("TargetSkeleton"),
		MakeAssetRef(TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh_Skeleton.DefaultSkeletalMesh_Skeleton")));
	Body->SetObjectField(TEXT("Preview"), Preview);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithSparseDefaultCoreObjects()
{
	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetBoolField(TEXT("bIsTemplate"), false);

	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintApplicationMethod"), TEXT("LinkedLayers"));

	TSharedRef<FJsonObject> Optimization = MakeShared<FJsonObject>();
	Optimization->SetBoolField(TEXT("bUseMultiThreadedAnimationUpdate"), true);
	Optimization->SetBoolField(TEXT("bWarnAboutBlueprintUsage"), false);
	Optimization->SetBoolField(TEXT("bEnableLinkedAnimLayerInstanceSharing"), false);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("Template"), Template);
	Body->SetObjectField(TEXT("Preview"), Preview);
	Body->SetObjectField(TEXT("Optimization"), Optimization);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithSparseChangedPreview()
{
	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintApplicationMethod"), TEXT("LinkedAnimGraph"));

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("Preview"), Preview);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithMismatchedPreviewSkeleton()
{
	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetObjectField(
		TEXT("PreviewSkeletalMesh"),
		MakeAssetRef(TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh.DefaultSkeletalMesh")));

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(
		TEXT("TargetSkeleton"),
		MakeAssetRef(TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton")));
	Body->SetObjectField(TEXT("Preview"), Preview);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithPreviewApplicationMethod(const FString& Method)
{
	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintApplicationMethod"), Method);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("Preview"), Preview);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithUnknownOptimizationField()
{
	TSharedRef<FJsonObject> Optimization = MakeShared<FJsonObject>();
	Optimization->SetBoolField(TEXT("bUseMultiThreadedAnimationUpdate"), true);
	Optimization->SetBoolField(TEXT("bUnknownOptimizationFlag"), true);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("Optimization"), Optimization);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonObject> MakeAnimBlueprintApplyDocument(
	const FString& Target,
	const FString& ParentClassPath = TEXT("/Script/Engine.AnimInstance"),
	const FString& TargetSkeletonPath = TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh_Skeleton.DefaultSkeletalMesh_Skeleton"),
	const FString& PreviewMeshPath = TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh.DefaultSkeletalMesh"),
	bool bIsTemplate = false)
{
	TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimBlueprint"));
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), MakeClassRef(ParentClassPath));
	if (bIsTemplate)
	{
		Body->SetField(TEXT("TargetSkeleton"), MakeShared<FJsonValueNull>());
	}
	else
	{
		Body->SetObjectField(TEXT("TargetSkeleton"), MakeAssetRef(TargetSkeletonPath));
	}

	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetBoolField(TEXT("bIsTemplate"), bIsTemplate);
	Body->SetObjectField(TEXT("Template"), Template);

	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetObjectField(TEXT("PreviewSkeletalMesh"), MakeAssetRef(PreviewMeshPath));
	Preview->SetField(TEXT("PreviewAnimationBlueprint"), MakeShared<FJsonValueNull>());
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintApplicationMethod"), TEXT("LinkedLayers"));
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintTag"), TEXT(""));
	Body->SetObjectField(TEXT("Preview"), Preview);

	TSharedRef<FJsonObject> Optimization = MakeShared<FJsonObject>();
	Optimization->SetBoolField(TEXT("bUseMultiThreadedAnimationUpdate"), true);
	Optimization->SetBoolField(TEXT("bWarnAboutBlueprintUsage"), false);
	Optimization->SetBoolField(TEXT("bEnableLinkedAnimLayerInstanceSharing"), false);
	Body->SetObjectField(TEXT("Optimization"), Optimization);

	Body->SetArrayField(TEXT("SyncGroups"), {});
	Body->SetArrayField(TEXT("ImplementedInterfaces"), {});
	Body->SetArrayField(TEXT("Variables"), {});
	Body->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
	Body->SetArrayField(TEXT("UbergraphPages"), {});
	Body->SetArrayField(TEXT("FunctionGraphs"), {});
	Body->SetArrayField(TEXT("MacroGraphs"), {});
	Body->SetObjectField(TEXT("AnimGraph"), MakeCanonicalAnimGraphObject());
	Body->SetObjectField(TEXT("StateMachines"), MakeEmptyGraphRegionObject());
	Body->SetArrayField(TEXT("TransitionGraphs"), {});
	Body->SetObjectField(TEXT("AnimLayers"), MakeEmptyGraphRegionObject());
	Body->SetArrayField(TEXT("ParentAssetOverrides"), {});
	Document->SetObjectField(TEXT("Body"), Body);
	return Document;
}

TArray<TSharedPtr<FJsonValue>> MakeSyncGroupArray(std::initializer_list<const TCHAR*> Names)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const TCHAR* Name : Names)
	{
		TSharedRef<FJsonObject> Group = MakeShared<FJsonObject>();
		Group->SetStringField(TEXT("Name"), Name);
		Values.Add(MakeShared<FJsonValueObject>(Group));
	}
	return Values;
}

void SetSyncGroups(const TSharedRef<FJsonObject>& Document, std::initializer_list<const TCHAR*> Names)
{
	TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));
	Body->SetArrayField(TEXT("SyncGroups"), MakeSyncGroupArray(Names));
}

TSharedPtr<FJsonObject> MakeFloatVariable(const TCHAR* Name, const TCHAR* DefaultValue)
{
	TSharedPtr<FJsonObject> Variable = MakeShared<FJsonObject>();
	Variable->SetStringField(TEXT("Name"), Name);
	TSharedPtr<FJsonObject> Type = MakeShared<FJsonObject>();
	Type->SetStringField(TEXT("PinCategory"), TEXT("real"));
	Type->SetStringField(TEXT("PinSubCategory"), TEXT("float"));
	Variable->SetObjectField(TEXT("Type"), Type);
	Variable->SetStringField(TEXT("DefaultValue"), DefaultValue);
	return Variable;
}

TArray<TSharedPtr<FJsonValue>> MakeVariableArray(std::initializer_list<TSharedPtr<FJsonObject>> Variables)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const TSharedPtr<FJsonObject>& Variable : Variables)
	{
		Values.Add(MakeShared<FJsonValueObject>(Variable.ToSharedRef()));
	}
	return Values;
}

TSharedPtr<FJsonObject> MakeImplementedInterface(const FString& InterfacePath)
{
	TSharedPtr<FJsonObject> InterfaceEntry = MakeShared<FJsonObject>();
	InterfaceEntry->SetObjectField(TEXT("Interface"), MakeClassRef(InterfacePath));
	return InterfaceEntry;
}

void SetImplementedInterfaces(const TSharedRef<FJsonObject>& Document, std::initializer_list<TSharedPtr<FJsonObject>> Interfaces)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const TSharedPtr<FJsonObject>& Interface : Interfaces)
	{
		Values.Add(MakeShared<FJsonValueObject>(Interface.ToSharedRef()));
	}
	Document->GetObjectField(TEXT("Body"))->SetArrayField(TEXT("ImplementedInterfaces"), Values);
}

bool HasBlueprintVariable(const UBlueprint* Blueprint, FName Name)
{
	return Blueprint && Blueprint->NewVariables.ContainsByPredicate([Name](const FBPVariableDescription& Variable)
	{
		return Variable.VarName == Name;
	});
}

TSharedRef<FJsonValue> MakeBodyWithSyncGroups(std::initializer_list<const TCHAR*> Names)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("SyncGroups"), MakeSyncGroupArray(Names));
	return MakeShared<FJsonValueObject>(Body);
}

bool HasDiagnostic(const FAssetDocumentCapabilityResult& Result, const FString& Path, const FString& Code)
{
	return Result.Diagnostics.ContainsByPredicate([&Path, &Code](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Path == Path && Diagnostic.Code == Code;
	});
}

bool HasDiagnostic(const FAssetDocumentResult& Result, const FString& Path, const FString& Code)
{
	return Result.Diagnostics.ContainsByPredicate([&Path, &Code](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Path == Path && Diagnostic.Code == Code;
	});
}

bool HasDiffPath(const TArray<TSharedPtr<FJsonValue>>& DiffEntries, const FString& ExpectedPath)
{
	return DiffEntries.ContainsByPredicate([&ExpectedPath](const TSharedPtr<FJsonValue>& EntryValue)
	{
		const TSharedPtr<FJsonObject> Entry = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
		FString Path;
		return Entry.IsValid() && Entry->TryGetStringField(TEXT("path"), Path) && Path == ExpectedPath;
	});
}

bool HasDiffStatus(const TArray<TSharedPtr<FJsonValue>>& DiffEntries, const FString& ExpectedStatus)
{
	return DiffEntries.ContainsByPredicate([&ExpectedStatus](const TSharedPtr<FJsonValue>& EntryValue)
	{
		const TSharedPtr<FJsonObject> Entry = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
		FString Status;
		return Entry.IsValid() && Entry->TryGetStringField(TEXT("status"), Status) && Status == ExpectedStatus;
	});
}

bool HasDiffNullField(const TArray<TSharedPtr<FJsonValue>>& DiffEntries, const FString& ExpectedPath, const FString& FieldName)
{
	return DiffEntries.ContainsByPredicate([&ExpectedPath, &FieldName](const TSharedPtr<FJsonValue>& EntryValue)
	{
		const TSharedPtr<FJsonObject> Entry = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
		FString Path;
		const TSharedPtr<FJsonValue> FieldValue = Entry.IsValid() ? Entry->TryGetField(FieldName) : nullptr;
		return Entry.IsValid()
			&& Entry->TryGetStringField(TEXT("path"), Path)
			&& Path == ExpectedPath
			&& FieldValue.IsValid()
			&& FieldValue->Type == EJson::Null;
	});
}

bool HasNumericSyncGroupDiffPath(const TArray<TSharedPtr<FJsonValue>>& DiffEntries)
{
	return DiffEntries.ContainsByPredicate([](const TSharedPtr<FJsonValue>& EntryValue)
	{
		const TSharedPtr<FJsonObject> Entry = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
		FString Path;
		return Entry.IsValid()
			&& Entry->TryGetStringField(TEXT("path"), Path)
			&& Path.StartsWith(TEXT("/Body/SyncGroups/"))
			&& Path.Len() > FString(TEXT("/Body/SyncGroups/")).Len()
			&& FChar::IsDigit(Path[FString(TEXT("/Body/SyncGroups/")).Len()]);
	});
}

bool HasArrayFieldCount(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, int32 ExpectedCount)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	return Object.IsValid() && Object->TryGetArrayField(FieldName, Values) && Values && Values->Num() == ExpectedCount;
}

bool HasCanonicalAnimGraphObject(const TSharedPtr<FJsonObject>& Body)
{
	const TSharedPtr<FJsonObject>* AnimGraph = nullptr;
	if (!Body.IsValid() || !Body->TryGetObjectField(TEXT("AnimGraph"), AnimGraph) || !AnimGraph || !AnimGraph->IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	if (!(*AnimGraph)->TryGetArrayField(TEXT("Graphs"), Graphs) || !Graphs || Graphs->Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject> RootGraph = (*Graphs)[0].IsValid() ? (*Graphs)[0]->AsObject() : nullptr;
	FString Id;
	FString Kind;
	return RootGraph.IsValid()
		&& RootGraph->TryGetStringField(TEXT("Id"), Id)
		&& RootGraph->TryGetStringField(TEXT("Kind"), Kind)
		&& Id == TEXT("AnimGraph")
		&& Kind == TEXT("AnimGraph");
}

bool HasAnimGraphNode(const TSharedPtr<FJsonObject>& Body, const FString& ExpectedNodeId)
{
	const TSharedPtr<FJsonObject>* AnimGraph = nullptr;
	if (!Body.IsValid() || !Body->TryGetObjectField(TEXT("AnimGraph"), AnimGraph) || !AnimGraph || !AnimGraph->IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	if (!(*AnimGraph)->TryGetArrayField(TEXT("Graphs"), Graphs) || !Graphs)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& GraphValue : *Graphs)
	{
		const TSharedPtr<FJsonObject> Graph = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!Graph.IsValid() || !Graph->TryGetArrayField(TEXT("Nodes"), Nodes) || !Nodes)
		{
			continue;
		}

		for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
		{
			const TSharedPtr<FJsonObject> Node = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
			FString NodeId;
			if (Node.IsValid()
				&& Node->TryGetStringField(TEXT("Id"), NodeId)
				&& NodeId == ExpectedNodeId)
			{
				return true;
			}
		}
	}

	return false;
}

bool HasAnimGraphNodePosition(const TSharedPtr<FJsonObject>& Body, const FString& ExpectedNodeId, double ExpectedX, double ExpectedY)
{
	const TSharedPtr<FJsonObject>* AnimGraph = nullptr;
	if (!Body.IsValid() || !Body->TryGetObjectField(TEXT("AnimGraph"), AnimGraph) || !AnimGraph || !AnimGraph->IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	if (!(*AnimGraph)->TryGetArrayField(TEXT("Graphs"), Graphs) || !Graphs)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& GraphValue : *Graphs)
	{
		const TSharedPtr<FJsonObject> Graph = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!Graph.IsValid() || !Graph->TryGetArrayField(TEXT("Nodes"), Nodes) || !Nodes)
		{
			continue;
		}

		for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
		{
			const TSharedPtr<FJsonObject> Node = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
			const TSharedPtr<FJsonObject>* Position = nullptr;
			FString NodeId;
			double X = 0.0;
			double Y = 0.0;
			if (Node.IsValid()
				&& Node->TryGetStringField(TEXT("Id"), NodeId)
				&& NodeId == ExpectedNodeId
				&& Node->TryGetObjectField(TEXT("Position"), Position)
				&& Position
				&& (*Position)->TryGetNumberField(TEXT("X"), X)
				&& (*Position)->TryGetNumberField(TEXT("Y"), Y)
				&& FMath::IsNearlyEqual(X, ExpectedX)
				&& FMath::IsNearlyEqual(Y, ExpectedY))
			{
				return true;
			}
		}
	}

	return false;
}

TSharedPtr<FJsonObject> GetExtractedRootAnimGraph(const TSharedPtr<FJsonObject>& Body)
{
	const TSharedPtr<FJsonObject>* AnimGraph = nullptr;
	if (!Body.IsValid() || !Body->TryGetObjectField(TEXT("AnimGraph"), AnimGraph) || !AnimGraph || !AnimGraph->IsValid())
	{
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	if (!(*AnimGraph)->TryGetArrayField(TEXT("Graphs"), Graphs) || !Graphs)
	{
		return nullptr;
	}

	for (const TSharedPtr<FJsonValue>& GraphValue : *Graphs)
	{
		TSharedPtr<FJsonObject> Graph = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
		FString Id;
		if (Graph.IsValid() && Graph->TryGetStringField(TEXT("Id"), Id) && Id == TEXT("AnimGraph"))
		{
			return Graph;
		}
	}
	return nullptr;
}

bool HasAnimGraphOutputPose(const TSharedPtr<FJsonObject>& Body, const FString& ExpectedNodeId, const FString& ExpectedPin)
{
	const TSharedPtr<FJsonObject> Graph = GetExtractedRootAnimGraph(Body);
	const TSharedPtr<FJsonObject>* Metadata = nullptr;
	const TSharedPtr<FJsonObject>* OutputPose = nullptr;
	FString NodeId;
	FString Pin;
	return Graph.IsValid()
		&& Graph->TryGetObjectField(TEXT("Metadata"), Metadata)
		&& Metadata
		&& (*Metadata)->TryGetObjectField(TEXT("OutputPose"), OutputPose)
		&& OutputPose
		&& (*OutputPose)->TryGetStringField(TEXT("Node"), NodeId)
		&& (*OutputPose)->TryGetStringField(TEXT("Pin"), Pin)
		&& NodeId == ExpectedNodeId
		&& Pin == ExpectedPin;
}

bool HasAnimGraphSkippedOutputPose(const TSharedPtr<FJsonObject>& Body)
{
	const TSharedPtr<FJsonObject> Graph = GetExtractedRootAnimGraph(Body);
	const TSharedPtr<FJsonObject>* Skipped = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* OutputPose = nullptr;
	return Graph.IsValid()
		&& Graph->TryGetObjectField(TEXT("_Skipped"), Skipped)
		&& Skipped
		&& (*Skipped)->TryGetArrayField(TEXT("OutputPose"), OutputPose)
		&& OutputPose
		&& OutputPose->Num() > 0;
}

TSharedPtr<FJsonObject> GetExtractedStateMachineGraph(const TSharedPtr<FJsonObject>& Body, const FString& ExpectedGraphId)
{
	const TSharedPtr<FJsonObject>* StateMachines = nullptr;
	if (!Body.IsValid() || !Body->TryGetObjectField(TEXT("StateMachines"), StateMachines) || !StateMachines || !StateMachines->IsValid())
	{
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	if (!(*StateMachines)->TryGetArrayField(TEXT("Graphs"), Graphs) || !Graphs)
	{
		return nullptr;
	}

	for (const TSharedPtr<FJsonValue>& GraphValue : *Graphs)
	{
		TSharedPtr<FJsonObject> Graph = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
		FString Id;
		if (Graph.IsValid() && Graph->TryGetStringField(TEXT("Id"), Id) && Id == ExpectedGraphId)
		{
			return Graph;
		}
	}
	return nullptr;
}

bool HasStateMachineEntryState(const TSharedPtr<FJsonObject>& Body, const FString& ExpectedGraphId, const FString& ExpectedEntryState)
{
	const TSharedPtr<FJsonObject> Graph = GetExtractedStateMachineGraph(Body, ExpectedGraphId);
	const TSharedPtr<FJsonObject>* Metadata = nullptr;
	FString EntryState;
	return Graph.IsValid()
		&& Graph->TryGetObjectField(TEXT("Metadata"), Metadata)
		&& Metadata
		&& (*Metadata)->TryGetStringField(TEXT("EntryState"), EntryState)
		&& EntryState == ExpectedEntryState;
}

bool HasStateMachineNodePosition(
	const TSharedPtr<FJsonObject>& Body,
	const FString& ExpectedGraphId,
	const FString& ExpectedNodeId,
	double ExpectedX,
	double ExpectedY)
{
	const TSharedPtr<FJsonObject> Graph = GetExtractedStateMachineGraph(Body, ExpectedGraphId);
	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (!Graph.IsValid() || !Graph->TryGetArrayField(TEXT("Nodes"), Nodes) || !Nodes)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
	{
		const TSharedPtr<FJsonObject> Node = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
		const TSharedPtr<FJsonObject>* Position = nullptr;
		FString NodeId;
		double X = 0.0;
		double Y = 0.0;
		if (Node.IsValid()
			&& Node->TryGetStringField(TEXT("Id"), NodeId)
			&& NodeId == ExpectedNodeId
			&& Node->TryGetObjectField(TEXT("Position"), Position)
			&& Position
			&& (*Position)->TryGetNumberField(TEXT("X"), X)
			&& (*Position)->TryGetNumberField(TEXT("Y"), Y)
			&& FMath::IsNearlyEqual(X, ExpectedX)
			&& FMath::IsNearlyEqual(Y, ExpectedY))
		{
			return true;
		}
	}

	return false;
}

bool HasStateMachineSubgraph(
	const TSharedPtr<FJsonObject>& Body,
	const FString& ExpectedGraphId,
	const FString& ExpectedSubgraphId,
	const FString& ExpectedKind,
	const FString& OwnerField,
	const FString& ExpectedOwner)
{
	const TSharedPtr<FJsonObject> Graph = GetExtractedStateMachineGraph(Body, ExpectedGraphId);
	const TArray<TSharedPtr<FJsonValue>>* Subgraphs = nullptr;
	if (!Graph.IsValid() || !Graph->TryGetArrayField(TEXT("Subgraphs"), Subgraphs) || !Subgraphs)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& SubgraphValue : *Subgraphs)
	{
		const TSharedPtr<FJsonObject> Subgraph = SubgraphValue.IsValid() ? SubgraphValue->AsObject() : nullptr;
		const TSharedPtr<FJsonObject>* Owner = nullptr;
		FString Id;
		FString Kind;
		FString OwnerValue;
		if (Subgraph.IsValid()
			&& Subgraph->TryGetStringField(TEXT("Id"), Id)
			&& Subgraph->TryGetStringField(TEXT("Kind"), Kind)
			&& Id == ExpectedSubgraphId
			&& Kind == ExpectedKind
			&& Subgraph->TryGetObjectField(TEXT("Owner"), Owner)
			&& Owner
			&& (*Owner)->TryGetStringField(OwnerField, OwnerValue)
			&& OwnerValue == ExpectedOwner)
		{
			return true;
		}
	}

	return false;
}

bool HasStateMachineSubgraphMetadataResultPin(
	const TSharedPtr<FJsonObject>& Body,
	const FString& ExpectedGraphId,
	const FString& ExpectedSubgraphId,
	const FString& ExpectedPin)
{
	const TSharedPtr<FJsonObject> Graph = GetExtractedStateMachineGraph(Body, ExpectedGraphId);
	const TArray<TSharedPtr<FJsonValue>>* Subgraphs = nullptr;
	if (!Graph.IsValid() || !Graph->TryGetArrayField(TEXT("Subgraphs"), Subgraphs) || !Subgraphs)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& SubgraphValue : *Subgraphs)
	{
		const TSharedPtr<FJsonObject> Subgraph = SubgraphValue.IsValid() ? SubgraphValue->AsObject() : nullptr;
		const TSharedPtr<FJsonObject>* Metadata = nullptr;
		const TSharedPtr<FJsonObject>* Result = nullptr;
		FString Id;
		FString Pin;
		if (Subgraph.IsValid()
			&& Subgraph->TryGetStringField(TEXT("Id"), Id)
			&& Id == ExpectedSubgraphId
			&& Subgraph->TryGetObjectField(TEXT("Metadata"), Metadata)
			&& Metadata
			&& (*Metadata)->TryGetObjectField(TEXT("Result"), Result)
			&& Result
			&& (*Result)->TryGetStringField(TEXT("Pin"), Pin)
			&& Pin == ExpectedPin)
		{
			return true;
		}
	}

	return false;
}

UEdGraph* FindAnimBlueprintAnimGraph(UAnimBlueprint* AnimBlueprint)
{
	if (!AnimBlueprint)
	{
		return nullptr;
	}
	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FName(TEXT("AnimGraph")))
		{
			return Graph;
		}
	}
	return FindObject<UEdGraph>(AnimBlueprint, TEXT("AnimGraph"));
}

UAnimationStateMachineGraph* FindStateMachineGraph(
	UAnimBlueprint* AnimBlueprint,
	const FString& ExpectedGraphId)
{
	UEdGraph* RootGraph = FindAnimBlueprintAnimGraph(AnimBlueprint);
	if (!RootGraph)
	{
		return nullptr;
	}

	TArray<UAnimGraphNode_StateMachineBase*> StateMachineNodes;
	RootGraph->GetNodesOfClass<UAnimGraphNode_StateMachineBase>(StateMachineNodes);
	for (UAnimGraphNode_StateMachineBase* StateMachineNode : StateMachineNodes)
	{
		UAnimationStateMachineGraph* StateMachineGraph =
			StateMachineNode ? StateMachineNode->EditorStateMachineGraph : nullptr;
		if (StateMachineGraph && StateMachineGraph->GetName() == ExpectedGraphId)
		{
			return StateMachineGraph;
		}
	}
	return nullptr;
}

UAnimationTransitionGraph* FindTransitionRuleGraph(
	UAnimBlueprint* AnimBlueprint,
	const FString& ExpectedStateMachineId,
	const FString& ExpectedTransitionId)
{
	UAnimationStateMachineGraph* StateMachineGraph =
		FindStateMachineGraph(AnimBlueprint, ExpectedStateMachineId);
	if (!StateMachineGraph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : StateMachineGraph->Nodes)
	{
		UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node);
		if (TransitionNode && TransitionNode->GetStateName() == ExpectedTransitionId)
		{
			return Cast<UAnimationTransitionGraph>(TransitionNode->GetBoundGraph());
		}
	}
	return nullptr;
}

int32 CountTransitionResultNodes(UAnimationTransitionGraph* TransitionGraph)
{
	int32 Count = 0;
	if (!TransitionGraph)
	{
		return Count;
	}

	for (UEdGraphNode* Node : TransitionGraph->Nodes)
	{
		if (Cast<UAnimGraphNode_TransitionResult>(Node))
		{
			++Count;
		}
	}
	return Count;
}

UEdGraphNode* FindManagedAnimGraphNode(UAnimBlueprint* AnimBlueprint, const FString& NodeId)
{
	UEdGraph* Graph = FindAnimBlueprintAnimGraph(AnimBlueprint);
	if (!Graph)
	{
		return nullptr;
	}
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		FString ParsedNodeId;
		if (Node && FAssetDocumentAnimationGraphRuntime::TryParseManagedNodeObjectName(Node->GetFName(), ParsedNodeId) && ParsedNodeId == NodeId)
		{
			return Node;
		}
	}
	return nullptr;
}

UEdGraphPin* FindNamedPin(UEdGraphNode* Node, const TCHAR* PinName, EEdGraphPinDirection Direction)
{
	if (!Node)
	{
		return nullptr;
	}
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == Direction && Pin->PinName == PinName)
		{
			return Pin;
		}
	}
	return nullptr;
}

UEdGraphPin* FindFirstPin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
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

UEdGraphPin* FindResultPosePin(UAnimBlueprint* AnimBlueprint)
{
	UEdGraph* Graph = FindAnimBlueprintAnimGraph(AnimBlueprint);
	UClass* ResultClass = LoadClass<UEdGraphNode>(nullptr, TEXT("/Script/AnimGraph.AnimGraphNode_Root"));
	if (!Graph || !ResultClass)
	{
		return nullptr;
	}
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->IsA(ResultClass))
		{
			return FindNamedPin(Node, TEXT("Result"), EGPD_Input);
		}
	}
	return nullptr;
}

UEdGraphPin* FindManagedPoseOutputPin(UAnimBlueprint* AnimBlueprint, const FString& NodeId)
{
	return FindNamedPin(FindManagedAnimGraphNode(AnimBlueprint, NodeId), TEXT("Pose"), EGPD_Output);
}

bool IsResultLinkedToManagedNode(UAnimBlueprint* AnimBlueprint, const FString& NodeId)
{
	UEdGraphPin* OutputPin = FindManagedPoseOutputPin(AnimBlueprint, NodeId);
	UEdGraphPin* ResultPin = FindResultPosePin(AnimBlueprint);
	return OutputPin && ResultPin && OutputPin->LinkedTo.Contains(ResultPin) && ResultPin->LinkedTo.Contains(OutputPin);
}

int32 CountResultManagedOutputLinks(UAnimBlueprint* AnimBlueprint)
{
	int32 Count = 0;
	UEdGraphPin* ResultPin = FindResultPosePin(AnimBlueprint);
	if (!ResultPin)
	{
		return Count;
	}
	for (UEdGraphPin* LinkedPin : ResultPin->LinkedTo)
	{
		const UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
		FString ParsedNodeId;
		if (LinkedNode
			&& LinkedPin->Direction == EGPD_Output
			&& FAssetDocumentAnimationGraphRuntime::TryParseManagedNodeObjectName(LinkedNode->GetFName(), ParsedNodeId))
		{
			++Count;
		}
	}
	return Count;
}

bool AddDirectResultPoseLink(UAnimBlueprint* AnimBlueprint, const FString& NodeId)
{
	UEdGraphNode* Node = FindManagedAnimGraphNode(AnimBlueprint, NodeId);
	UEdGraphPin* OutputPin = FindNamedPin(Node, TEXT("Pose"), EGPD_Output);
	UEdGraphPin* ResultPin = FindResultPosePin(AnimBlueprint);
	if (!OutputPin || !ResultPin)
	{
		return false;
	}
	if (!OutputPin->LinkedTo.Contains(ResultPin))
	{
		OutputPin->MakeLinkTo(ResultPin);
	}
	return OutputPin->LinkedTo.Contains(ResultPin);
}

bool AddSyntheticPoseInputLink(UAnimBlueprint* AnimBlueprint, const FString& NodeId, UEdGraphPin*& OutInputPin)
{
	OutInputPin = nullptr;
	UEdGraph* Graph = FindAnimBlueprintAnimGraph(AnimBlueprint);
	UEdGraphPin* OutputPin = FindManagedPoseOutputPin(AnimBlueprint, NodeId);
	UClass* ConsumerClass = LoadClass<UEdGraphNode>(nullptr, TEXT("/Script/AnimGraph.AnimGraphNode_SaveCachedPose"));
	if (!Graph || !OutputPin || !ConsumerClass)
	{
		return false;
	}

	UEdGraphNode* SyntheticNode = NewObject<UEdGraphNode>(Graph, ConsumerClass, NAME_None, RF_Transactional);
	if (!SyntheticNode)
	{
		return false;
	}
	SyntheticNode->CreateNewGuid();
	Graph->AddNode(SyntheticNode, false, false);
	SyntheticNode->AllocateDefaultPins();
	OutInputPin = FindNamedPin(SyntheticNode, TEXT("Pose"), EGPD_Input);
	if (!OutInputPin)
	{
		OutInputPin = FindFirstPin(SyntheticNode, EGPD_Input);
	}
	if (!OutInputPin)
	{
		return false;
	}
	if (!OutputPin->LinkedTo.Contains(OutInputPin))
	{
		OutputPin->MakeLinkTo(OutInputPin);
	}
	return OutputPin->LinkedTo.Contains(OutInputPin) && OutInputPin->LinkedTo.Contains(OutputPin);
}

bool HasPoseOutputLink(UAnimBlueprint* AnimBlueprint, const FString& NodeId, const UEdGraphPin* ExpectedLinkedPin)
{
	const UEdGraphPin* OutputPin = FindManagedPoseOutputPin(AnimBlueprint, NodeId);
	return OutputPin && ExpectedLinkedPin && OutputPin->LinkedTo.Contains(ExpectedLinkedPin);
}

bool AllDiffEntriesUnchanged(const TArray<TSharedPtr<FJsonValue>>& DiffEntries)
{
	if (DiffEntries.IsEmpty())
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& EntryValue : DiffEntries)
	{
		const TSharedPtr<FJsonObject> Entry = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
		FString Status;
		if (!Entry.IsValid() || !Entry->TryGetStringField(TEXT("status"), Status) || Status != TEXT("unchanged"))
		{
			return false;
		}
	}
	return true;
}

const FAssetDocumentRegionPolicy* FindPolicy(const TArray<FAssetDocumentRegionPolicy>& Policies, const TCHAR* RegionId)
{
	return Policies.FindByPredicate([RegionId](const FAssetDocumentRegionPolicy& Policy)
	{
		return Policy.RegionId == FName(RegionId);
	});
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintProfileShapeTest,
	"AssetFactory.AssetDocument.AnimBlueprint.ProfileShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintProfileShapeTest::RunTest(const FString&)
{
	const FAnimBlueprintAssetDocumentProfile Profile;
	TestEqual(TEXT("Exact class is UAnimBlueprint"), Profile.GetExactClass(), UAnimBlueprint::StaticClass());

	FAssetDocumentTemplateContext TemplateContext;
	TemplateContext.Target = TEXT("/Game/AssetDocumentSmoke/ABP_AssetDocumentSmoke");
	const TSharedRef<FJsonObject> Template = Profile.CreateTemplate(TemplateContext);
	TestEqual(TEXT("Template class is AnimBlueprint"), Template->GetStringField(TEXT("Class")), FString(TEXT("/Script/Engine.AnimBlueprint")));

	const TSharedPtr<FJsonObject> Body = Template->GetObjectField(TEXT("Body"));
	TestTrue(TEXT("Template includes Body object"), Body.IsValid());
	if (Body.IsValid())
	{
		const TArray<FName> ExpectedTemplateBodyKeys = FAnimBlueprintAssetDocumentCapability::GetCanonicalBodyKeys();
		TestEqual(TEXT("Template Body only includes canonical keys"), Body->Values.Num(), ExpectedTemplateBodyKeys.Num());
		for (const FName& ExpectedKey : ExpectedTemplateBodyKeys)
		{
			TestTrue(
				FString::Printf(TEXT("Template includes Body.%s"), *ExpectedKey.ToString()),
				Body->HasField(ExpectedKey.ToString()));
		}
	}

	const TArray<FName> BodyKeys = Profile.GetBodyKeys();
	TestTrue(TEXT("Body keys include ParentClass"), HasBodyKey(BodyKeys, TEXT("ParentClass")));
	TestTrue(TEXT("Body keys include TargetSkeleton"), HasBodyKey(BodyKeys, TEXT("TargetSkeleton")));
	TestTrue(TEXT("Body keys include Template"), HasBodyKey(BodyKeys, TEXT("Template")));
	TestTrue(TEXT("Body keys include Preview"), HasBodyKey(BodyKeys, TEXT("Preview")));
	TestTrue(TEXT("Body keys include Optimization"), HasBodyKey(BodyKeys, TEXT("Optimization")));
	TestTrue(TEXT("Body keys include SyncGroups"), HasBodyKey(BodyKeys, TEXT("SyncGroups")));
	TestTrue(TEXT("Body keys include ImplementedInterfaces"), HasBodyKey(BodyKeys, TEXT("ImplementedInterfaces")));
	TestTrue(TEXT("Body keys include Variables"), HasBodyKey(BodyKeys, TEXT("Variables")));
	TestTrue(TEXT("Body keys include ClassDefaults"), HasBodyKey(BodyKeys, TEXT("ClassDefaults")));
	TestTrue(TEXT("Body keys include UbergraphPages"), HasBodyKey(BodyKeys, TEXT("UbergraphPages")));
	TestTrue(TEXT("Body keys include FunctionGraphs"), HasBodyKey(BodyKeys, TEXT("FunctionGraphs")));
	TestTrue(TEXT("Body keys include MacroGraphs"), HasBodyKey(BodyKeys, TEXT("MacroGraphs")));
	TestTrue(TEXT("Body keys include AnimGraph"), HasBodyKey(BodyKeys, TEXT("AnimGraph")));
	TestTrue(TEXT("Body keys include StateMachines"), HasBodyKey(BodyKeys, TEXT("StateMachines")));
	TestTrue(TEXT("Body keys include TransitionGraphs"), HasBodyKey(BodyKeys, TEXT("TransitionGraphs")));
	TestTrue(TEXT("Body keys include AnimLayers"), HasBodyKey(BodyKeys, TEXT("AnimLayers")));
	TestTrue(TEXT("Body keys include ParentAssetOverrides"), HasBodyKey(BodyKeys, TEXT("ParentAssetOverrides")));
	TestTrue(TEXT("Body root resolves adapter"), Profile.ResolveBodyAdapter(TEXT("Body")) != nullptr);
	TestTrue(TEXT("ParentClass resolves adapter"), Profile.ResolveBodyAdapter(TEXT("ParentClass")) != nullptr);

	const TArray<FAssetDocumentRegionPolicy> Policies = Profile.GetRegionPolicies();
	TestNotNull(TEXT("Policy includes Body.ParentClass"), FindPolicy(Policies, TEXT("Body.ParentClass")));
	TestNotNull(TEXT("Policy includes Body.TargetSkeleton"), FindPolicy(Policies, TEXT("Body.TargetSkeleton")));
	TestNotNull(TEXT("Policy includes Body.Template"), FindPolicy(Policies, TEXT("Body.Template")));
	TestNotNull(TEXT("Policy includes Body.Preview"), FindPolicy(Policies, TEXT("Body.Preview")));
	TestNotNull(TEXT("Policy includes Body.Optimization"), FindPolicy(Policies, TEXT("Body.Optimization")));
	TestNotNull(TEXT("Policy includes Body.SyncGroups"), FindPolicy(Policies, TEXT("Body.SyncGroups")));
	TestNotNull(TEXT("Policy includes Body.ImplementedInterfaces"), FindPolicy(Policies, TEXT("Body.ImplementedInterfaces")));
	TestNotNull(TEXT("Policy includes Body.Variables"), FindPolicy(Policies, TEXT("Body.Variables")));
	TestNotNull(TEXT("Policy includes Body.ClassDefaults"), FindPolicy(Policies, TEXT("Body.ClassDefaults")));
	TestNotNull(TEXT("Policy includes Body.UbergraphPages"), FindPolicy(Policies, TEXT("Body.UbergraphPages")));
	const FAssetDocumentRegionPolicy* FunctionGraphsPolicy = FindPolicy(Policies, TEXT("Body.FunctionGraphs"));
	const FAssetDocumentRegionPolicy* MacroGraphsPolicy = FindPolicy(Policies, TEXT("Body.MacroGraphs"));
	const FAssetDocumentRegionPolicy* AnimGraphPolicy = FindPolicy(Policies, TEXT("Body.AnimGraph"));
	const FAssetDocumentRegionPolicy* StateMachinesPolicy = FindPolicy(Policies, TEXT("Body.StateMachines"));
	const FAssetDocumentRegionPolicy* TransitionGraphsPolicy = FindPolicy(Policies, TEXT("Body.TransitionGraphs"));
	const FAssetDocumentRegionPolicy* AnimLayersPolicy = FindPolicy(Policies, TEXT("Body.AnimLayers"));
	const FAssetDocumentRegionPolicy* ParentAssetOverridesPolicy = FindPolicy(Policies, TEXT("Body.ParentAssetOverrides"));
	TestNotNull(TEXT("Policy includes Body.AnimGraph"), AnimGraphPolicy);
	TestNotNull(TEXT("Policy includes Body.FunctionGraphs"), FunctionGraphsPolicy);
	TestNotNull(TEXT("Policy includes Body.MacroGraphs"), MacroGraphsPolicy);
	TestNotNull(TEXT("Policy includes Body.StateMachines"), StateMachinesPolicy);
	TestNotNull(TEXT("Policy includes Body.TransitionGraphs"), TransitionGraphsPolicy);
	TestNotNull(TEXT("Policy includes Body.AnimLayers"), AnimLayersPolicy);
	TestNotNull(TEXT("Policy includes Body.ParentAssetOverrides"), ParentAssetOverridesPolicy);
	if (AnimGraphPolicy)
	{
		TestEqual(TEXT("AnimGraph policy is no longer deferred/null-gated"), AnimGraphPolicy->ExplicitDeleteValues.Num(), 0);
	}
	if (FunctionGraphsPolicy)
	{
		TestEqual(TEXT("FunctionGraphs policy is no longer deferred/null-gated"), FunctionGraphsPolicy->ExplicitDeleteValues.Num(), 0);
	}
	if (MacroGraphsPolicy)
	{
		TestEqual(TEXT("MacroGraphs policy is no longer deferred/null-gated"), MacroGraphsPolicy->ExplicitDeleteValues.Num(), 0);
	}
	if (StateMachinesPolicy)
	{
		TestEqual(TEXT("StateMachines policy is no longer deferred/null-gated"), StateMachinesPolicy->ExplicitDeleteValues.Num(), 0);
	}
	if (TransitionGraphsPolicy)
	{
		TestEqual(TEXT("TransitionGraphs policy is no longer deferred/null-gated"), TransitionGraphsPolicy->ExplicitDeleteValues.Num(), 0);
	}
	if (AnimLayersPolicy)
	{
		TestEqual(TEXT("AnimLayers policy is no longer deferred/null-gated"), AnimLayersPolicy->ExplicitDeleteValues.Num(), 0);
	}
	if (ParentAssetOverridesPolicy)
	{
		TestEqual(TEXT("ParentAssetOverrides policy is no longer deferred/null-gated"), ParentAssetOverridesPolicy->ExplicitDeleteValues.Num(), 0);
	}

	const IAssetDocumentCapability* BodyAdapter = Profile.ResolveBodyAdapter(TEXT("Body"));
	TestNotNull(TEXT("Body adapter resolves for validation"), BodyAdapter);
	if (BodyAdapter)
	{
		FAssetDocumentCapabilityContext Context;
		Context.AssetClass = UAnimBlueprint::StaticClass();
		TestTrue(TEXT("Empty Body validates"), BodyAdapter->Validate(Context, MakeEmptyBodyValue()).bSuccess);

		TestTrue(TEXT("Empty AnimLayers graph region validates"),
			BodyAdapter->Validate(Context, MakeBodyWithAnimLayerGraphs({})).bSuccess);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintDeferredGraphGatesTest,
	"AssetFactory.AssetDocument.AnimBlueprint.DeferredGraphGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintDeferredGraphGatesTest::RunTest(const FString&)
{
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UAnimBlueprint::StaticClass();
	const FAnimBlueprintAssetDocumentCapability Capability;

	const FAssetDocumentCapabilityResult LegacyAnimGraphResult =
		Capability.Validate(Context, MakeBodyWithLegacyAnimGraphArray());
	TestFalse(TEXT("Body.AnimGraph rejects legacy array shape"), LegacyAnimGraphResult.bSuccess);
	TestTrue(
		TEXT("Legacy AnimGraph array reports recursive schema diagnostic"),
		HasDiagnostic(LegacyAnimGraphResult, TEXT("/Body/AnimGraph"), TEXT("InvalidAnimGraphRegionType")));

	TestTrue(
		TEXT("Body.StateMachines accepts non-empty recursive graph schema"),
		Capability.Validate(Context, MakeBodyWithStateMachineGraphs({MakeStateMachineGraph(TEXT("Locomotion"))})).bSuccess);

	TestTrue(
		TEXT("Body.AnimLayers accepts non-empty recursive graph region"),
		Capability.Validate(Context, MakeBodyWithAnimLayerGraphs({MakeAnimLayerGraph(TEXT("UpperBodyLayer"))})).bSuccess);

	TestFalse(
		TEXT("Body.AnimLayers rejects null because it is no longer deferred"),
		Capability.Validate(Context, MakeBodyWithNullRegion(TEXT("AnimLayers"))).bSuccess);
	TestFalse(
		TEXT("Body.AnimLayers rejects legacy empty array because it is a graph object region"),
		Capability.Validate(Context, MakeBodyWithEmptyArrayRegion(TEXT("AnimLayers"))).bSuccess);
	TestFalse(
		TEXT("Body.AnimLayers rejects scalar values"),
		Capability.Validate(Context, MakeBodyWithScalarDeferredRegion(TEXT("AnimLayers"))).bSuccess);

	const FAssetDocumentCapabilityResult ObsoleteTransitionGraphsResult =
		Capability.Validate(Context, MakeBodyWithTransitionGraphs({MakeTransitionGraph(TEXT("Locomotion"), TEXT("IdleToRun"))}));
	TestFalse(TEXT("Body.TransitionGraphs rejects obsolete authored side-list shape"), ObsoleteTransitionGraphsResult.bSuccess);
	TestTrue(
		TEXT("Obsolete TransitionGraphs side-list reports explicit boundary"),
		HasDiagnostic(ObsoleteTransitionGraphsResult, TEXT("/Body/TransitionGraphs"), TEXT("UnsupportedAnimBlueprintRegion")));

	const FAssetDocumentCapabilityResult UnknownKeyResult = Capability.Validate(Context, MakeBodyWithUnknownKey());
	TestFalse(TEXT("Unknown Body key rejects"), UnknownKeyResult.bSuccess);
	TestTrue(
		TEXT("Unknown Body key reports UnknownBodyKey"),
		HasDiagnostic(UnknownKeyResult, TEXT("/Body/UnexpectedGraph"), TEXT("UnknownBodyKey")));
	TestTrue(TEXT("FunctionGraphs empty region validates through Blueprint common graph adapter"),
		Capability.Validate(Context, MakeBodyWithEmptyArrayRegion(TEXT("FunctionGraphs"))).bSuccess);
	TestTrue(TEXT("MacroGraphs empty region validates through Blueprint common graph adapter"),
		Capability.Validate(Context, MakeBodyWithEmptyArrayRegion(TEXT("MacroGraphs"))).bSuccess);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintAnimGraphTest,
	"AssetFactory.AssetDocument.AnimBlueprint.AnimGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintAnimGraphTest::RunTest(const FString&)
{
	FAssetDocumentCapabilityContext ValidationContext;
	ValidationContext.AssetClass = UAnimBlueprint::StaticClass();
	const FAnimBlueprintAssetDocumentCapability Capability;

	TestTrue(TEXT("Canonical recursive AnimGraph validates"), Capability.Validate(ValidationContext, MakeBodyWithCanonicalAnimGraph()).bSuccess);

	const FAssetDocumentCapabilityResult LegacyArrayResult =
		Capability.Validate(ValidationContext, MakeBodyWithLegacyAnimGraphArray());
	TestFalse(TEXT("Legacy AnimGraph array rejects under recursive schema"), LegacyArrayResult.bSuccess);
	TestTrue(
		TEXT("Legacy array diagnostic uses AnimGraph region path"),
		HasDiagnostic(LegacyArrayResult, TEXT("/Body/AnimGraph"), TEXT("InvalidAnimGraphRegionType")));

	const FAssetDocumentCapabilityResult UnsupportedNodeResult =
		Capability.Validate(ValidationContext, MakeBodyWithUnsupportedAnimGraphNode());
	TestFalse(TEXT("Unsupported AnimGraph node rejects"), UnsupportedNodeResult.bSuccess);
	TestTrue(
		TEXT("Unsupported node diagnostic uses semantic AnimGraph path"),
		HasDiagnostic(
			UnsupportedNodeResult,
			TEXT("/Body/AnimGraph/Graphs/AnimGraph/Nodes/IdlePlayer/Class"),
			TEXT("UnspawnableGraphNodeClass")));

	const FAssetDocumentCapabilityResult MissingOutputPoseResult =
		Capability.Validate(ValidationContext, MakeBodyWithAnimGraphSequencePlayerMissingOutputPose());
	TestFalse(TEXT("Managed AnimGraph node requires explicit output pose"), MissingOutputPoseResult.bSuccess);
	TestTrue(
		TEXT("Missing output pose diagnostic uses root graph path"),
		HasDiagnostic(
			MissingOutputPoseResult,
			TEXT("/Body/AnimGraph/Graphs/AnimGraph"),
			TEXT("MissingAnimGraphOutputPose")));

	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_AnimGraph_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
	FAssetDocumentService Service;

	const FString BadTarget = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_AnimGraph_Bad_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	TSharedRef<FJsonObject> BadDocument = MakeAnimBlueprintApplyDocument(BadTarget);
	BadDocument->GetObjectField(TEXT("Body"))->SetObjectField(
		TEXT("AnimGraph"),
		MakeBodyWithUnsupportedAnimGraphNode()->AsObject()->GetObjectField(TEXT("AnimGraph")));
	FAssetDocumentApplyRequest BadRequest;
	BadRequest.Document = BadDocument;
	BadRequest.bSaveAsset = false;
	const FAssetDocumentResult BadApplyResult = Service.Apply(BadRequest);
	TestFalse(TEXT("Unsupported AnimGraph node apply rejects before mutation"), BadApplyResult.IsSuccess());
	TestTrue(
		TEXT("Unsupported apply diagnostic keeps Body.AnimGraph semantic path"),
		HasDiagnostic(
			BadApplyResult,
			TEXT("/Body/AnimGraph/Graphs/AnimGraph/Nodes/IdlePlayer/Class"),
			TEXT("UnspawnableGraphNodeClass")));

	TSharedRef<FJsonObject> Document = MakeAnimBlueprintApplyDocument(Target);
	Document->GetObjectField(TEXT("Body"))->SetObjectField(TEXT("AnimGraph"), MakeCanonicalAnimGraphObject());

	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;
	const FAssetDocumentResult ApplyResult = Service.Apply(Request);
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("AnimGraph recursive apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("Recursive AnimGraph apply succeeds"), ApplyResult.IsSuccess());

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
	TestNotNull(TEXT("Created AnimBlueprint loads"), AnimBlueprint);
	FAssetDocumentCapabilityContext Context;
	Context.Asset = AnimBlueprint;
	Context.AssetClass = UAnimBlueprint::StaticClass();

	TSharedRef<FJsonObject> ExtractedBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ExtractResult = Capability.Extract(Context, ExtractedBody);
	TestTrue(TEXT("AnimGraph extract succeeds"), ExtractResult.bSuccess);
	TestTrue(TEXT("Extract includes canonical recursive AnimGraph"), HasCanonicalAnimGraphObject(ExtractedBody));

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult =
		Capability.Diff(Context, MakeBodyWithCanonicalAnimGraph(), DiffEntries);
	TestTrue(TEXT("AnimGraph diff succeeds"), DiffResult.bSuccess);
	TestTrue(TEXT("AnimGraph diff uses semantic graph path"), HasDiffPath(DiffEntries, TEXT("/Body/AnimGraph/Graphs/AnimGraph")));

	DiffEntries.Reset();
	const FAssetDocumentCapabilityResult SubgraphDiffResult =
		Capability.Diff(Context, MakeBodyWithAnimGraphSubgraph(), DiffEntries);
	TestTrue(TEXT("AnimGraph subgraph diff succeeds"), SubgraphDiffResult.bSuccess);
	TestTrue(
		TEXT("AnimGraph subgraph diff uses semantic subgraph path"),
		HasDiffPath(DiffEntries, TEXT("/Body/AnimGraph/Graphs/AnimGraph/Subgraphs/NestedPose")));
	TestTrue(
		TEXT("AnimGraph subgraph missing current is serialized as null"),
		HasDiffNullField(DiffEntries, TEXT("/Body/AnimGraph/Graphs/AnimGraph/Subgraphs/NestedPose"), TEXT("current")));

	const FString TutorialSkeletonPath = TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton");
	const FString TutorialPreviewMeshPath = TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP");
	const FString TutorialIdleAnimationPath = TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/Tutorial_Idle.Tutorial_Idle");
	const FString MissingOutputPoseTarget = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_AnimGraph_MissingOutput_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString MissingOutputPoseObjectPath = FString::Printf(TEXT("%s.%s"), *MissingOutputPoseTarget, *FPackageName::GetLongPackageAssetName(MissingOutputPoseTarget));
	TSharedRef<FJsonObject> MissingOutputPoseDocument = MakeAnimBlueprintApplyDocument(
		MissingOutputPoseTarget,
		TEXT("/Script/Engine.AnimInstance"),
		TutorialSkeletonPath,
		TutorialPreviewMeshPath);
	MissingOutputPoseDocument->GetObjectField(TEXT("Body"))->SetObjectField(
		TEXT("AnimGraph"),
		MakeAnimGraphWithSequencePlayer(TEXT("IdlePlayer"), false, TutorialIdleAnimationPath));
	FAssetDocumentApplyRequest MissingOutputPoseRequest;
	MissingOutputPoseRequest.Document = MissingOutputPoseDocument;
	MissingOutputPoseRequest.bSaveAsset = false;
	const FAssetDocumentResult MissingOutputPoseApplyResult = Service.Apply(MissingOutputPoseRequest);
	TestFalse(TEXT("Missing OutputPose apply rejects before graph mutation"), MissingOutputPoseApplyResult.IsSuccess());
	TestTrue(
		TEXT("Missing OutputPose apply reports AnimGraph path"),
		HasDiagnostic(
			MissingOutputPoseApplyResult,
			TEXT("/Body/AnimGraph/Graphs/AnimGraph"),
			TEXT("MissingAnimGraphOutputPose")));
	if (UAnimBlueprint* MissingOutputPoseAnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *MissingOutputPoseObjectPath))
	{
		TestNull(
			TEXT("Missing OutputPose apply does not materialize managed node"),
			FindManagedAnimGraphNode(MissingOutputPoseAnimBlueprint, TEXT("IdlePlayer")));
	}

	const FString ManagedNodeTarget = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_AnimGraphNode_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString ManagedNodeObjectPath = FString::Printf(TEXT("%s.%s"), *ManagedNodeTarget, *FPackageName::GetLongPackageAssetName(ManagedNodeTarget));
	TSharedRef<FJsonObject> ManagedNodeDocument = MakeAnimBlueprintApplyDocument(
		ManagedNodeTarget,
		TEXT("/Script/Engine.AnimInstance"),
		TutorialSkeletonPath,
		TutorialPreviewMeshPath);
	ManagedNodeDocument->GetObjectField(TEXT("Body"))->SetObjectField(
		TEXT("AnimGraph"),
		MakeAnimGraphWithSequencePlayer(TEXT("IdlePlayer"), true, TutorialIdleAnimationPath));

	FAssetDocumentApplyRequest ManagedNodeRequest;
	ManagedNodeRequest.Document = ManagedNodeDocument;
	ManagedNodeRequest.bSaveAsset = false;
	const FAssetDocumentResult ManagedNodeApplyResult = Service.Apply(ManagedNodeRequest);
	if (!ManagedNodeApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Managed AnimGraph node apply failed: %s"), *ManagedNodeApplyResult.Message));
	}
	TestTrue(TEXT("Managed AnimGraph node apply succeeds"), ManagedNodeApplyResult.IsSuccess());

	UAnimBlueprint* ManagedNodeAnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *ManagedNodeObjectPath);
	TestNotNull(TEXT("Created AnimBlueprint with managed AnimGraph node loads"), ManagedNodeAnimBlueprint);
	FAssetDocumentCapabilityContext ManagedNodeContext;
	ManagedNodeContext.Asset = ManagedNodeAnimBlueprint;
	ManagedNodeContext.AssetClass = UAnimBlueprint::StaticClass();

	TSharedRef<FJsonObject> ManagedNodeExtractedBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ManagedNodeExtractResult = Capability.Extract(ManagedNodeContext, ManagedNodeExtractedBody);
	TestTrue(TEXT("Managed AnimGraph node extract succeeds"), ManagedNodeExtractResult.bSuccess);
	TestTrue(TEXT("Managed AnimGraph node extracts by authored identity"), HasAnimGraphNode(ManagedNodeExtractedBody, TEXT("IdlePlayer")));
	TestTrue(
		TEXT("Managed AnimGraph node extracts authored position"),
		HasAnimGraphNodePosition(ManagedNodeExtractedBody, TEXT("IdlePlayer"), 120.0, 40.0));
	TestTrue(
		TEXT("Managed AnimGraph node extracts output pose"),
		HasAnimGraphOutputPose(ManagedNodeExtractedBody, TEXT("IdlePlayer"), TEXT("Pose")));

	TArray<TSharedPtr<FJsonValue>> ManagedNodeDiffEntries;
	TSharedRef<FJsonObject> ManagedNodeDiffBody = MakeShared<FJsonObject>();
	ManagedNodeDiffBody->SetObjectField(
		TEXT("AnimGraph"),
		MakeAnimGraphWithSequencePlayer(TEXT("IdlePlayer"), true, FString(), false));
	const FAssetDocumentCapabilityResult ManagedNodeDiffResult =
		Capability.Diff(
			ManagedNodeContext,
			MakeShared<FJsonValueObject>(ManagedNodeDiffBody),
			ManagedNodeDiffEntries);
	if (!ManagedNodeDiffResult.bSuccess)
	{
		AddError(FString::Printf(TEXT("Managed AnimGraph node diff failed: %s"), *ManagedNodeDiffResult.Message));
	}
	TestTrue(TEXT("Managed AnimGraph node diff succeeds"), ManagedNodeDiffResult.bSuccess);
	TestTrue(TEXT("Managed AnimGraph node diff is unchanged after apply/extract"), AllDiffEntriesUnchanged(ManagedNodeDiffEntries));

	TArray<TSharedPtr<FJsonValue>> ManagedNodeFieldDiffEntries;
	TSharedRef<FJsonObject> ManagedNodeFieldDiffBody = MakeShared<FJsonObject>();
	ManagedNodeFieldDiffBody->SetObjectField(
		TEXT("AnimGraph"),
		MakeAnimGraphWithSequencePlayer(
			TEXT("IdlePlayer"),
			true,
			TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/Tutorial_Walk_Fwd.Tutorial_Walk_Fwd"),
			false));
	const FAssetDocumentCapabilityResult ManagedNodeFieldDiffResult =
		Capability.Diff(
			ManagedNodeContext,
			MakeShared<FJsonValueObject>(ManagedNodeFieldDiffBody),
			ManagedNodeFieldDiffEntries);
	if (!ManagedNodeFieldDiffResult.bSuccess)
	{
		AddError(FString::Printf(TEXT("Managed AnimGraph node field diff failed: %s"), *ManagedNodeFieldDiffResult.Message));
	}
	TestTrue(TEXT("Managed AnimGraph node field diff succeeds"), ManagedNodeFieldDiffResult.bSuccess);
	TestFalse(TEXT("Managed AnimGraph node field diff is not normalized away"), AllDiffEntriesUnchanged(ManagedNodeFieldDiffEntries));

	TSharedRef<FJsonObject> InvalidPinDocument = MakeAnimBlueprintApplyDocument(
		ManagedNodeTarget,
		TEXT("/Script/Engine.AnimInstance"),
		TutorialSkeletonPath,
		TutorialPreviewMeshPath);
	InvalidPinDocument->GetObjectField(TEXT("Body"))->SetObjectField(
		TEXT("AnimGraph"),
		MakeAnimGraphWithOutputPosePin(TEXT("IdlePlayer"), TEXT("MissingPose")));
	FAssetDocumentApplyRequest InvalidPinRequest;
	InvalidPinRequest.Document = InvalidPinDocument;
	InvalidPinRequest.bSaveAsset = false;
	const FAssetDocumentResult InvalidPinApplyResult = Service.Apply(InvalidPinRequest);
	TestFalse(TEXT("Invalid OutputPose pin apply rejects"), InvalidPinApplyResult.IsSuccess());
	TestEqual(TEXT("Invalid OutputPose pin keeps one managed result link"), CountResultManagedOutputLinks(ManagedNodeAnimBlueprint), 1);
	TSharedRef<FJsonObject> InvalidPinExtractedBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult InvalidPinExtractResult = Capability.Extract(ManagedNodeContext, InvalidPinExtractedBody);
	TestTrue(TEXT("Invalid OutputPose pin rollback extract succeeds"), InvalidPinExtractResult.bSuccess);
	TestTrue(
		TEXT("Invalid OutputPose pin rollback preserves previous output pose"),
		HasAnimGraphOutputPose(InvalidPinExtractedBody, TEXT("IdlePlayer"), TEXT("Pose")));

	TSharedRef<FJsonObject> ReplacementDocument = MakeAnimBlueprintApplyDocument(
		ManagedNodeTarget,
		TEXT("/Script/Engine.AnimInstance"),
		TutorialSkeletonPath,
		TutorialPreviewMeshPath);
	ReplacementDocument->GetObjectField(TEXT("Body"))->SetObjectField(
		TEXT("AnimGraph"),
		MakeAnimGraphWithSequencePlayer(TEXT("WalkPlayer"), true, TutorialIdleAnimationPath, false));
	FAssetDocumentApplyRequest ReplacementRequest;
	ReplacementRequest.Document = ReplacementDocument;
	ReplacementRequest.bSaveAsset = false;
	const FAssetDocumentResult ReplacementApplyResult = Service.Apply(ReplacementRequest);
	if (!ReplacementApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Replacement OutputPose apply failed: %s"), *ReplacementApplyResult.Message));
	}
	TestTrue(TEXT("Replacement OutputPose apply succeeds"), ReplacementApplyResult.IsSuccess());
	TestEqual(TEXT("Replacement OutputPose keeps one managed result link"), CountResultManagedOutputLinks(ManagedNodeAnimBlueprint), 1);
	TSharedRef<FJsonObject> ReplacementExtractedBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ReplacementExtractResult = Capability.Extract(ManagedNodeContext, ReplacementExtractedBody);
	TestTrue(TEXT("Replacement OutputPose extract succeeds"), ReplacementExtractResult.bSuccess);
	TestTrue(
		TEXT("Replacement OutputPose is extracted from the new managed node"),
		HasAnimGraphOutputPose(ReplacementExtractedBody, TEXT("WalkPlayer"), TEXT("Pose")));

	const FString RollbackMutationTarget = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_AnimGraph_RollbackMutation_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString RollbackMutationObjectPath = FString::Printf(TEXT("%s.%s"), *RollbackMutationTarget, *FPackageName::GetLongPackageAssetName(RollbackMutationTarget));
	TSharedRef<FJsonObject> RollbackMutationDocument = MakeAnimBlueprintApplyDocument(
		RollbackMutationTarget,
		TEXT("/Script/Engine.AnimInstance"),
		TutorialSkeletonPath,
		TutorialPreviewMeshPath);
	RollbackMutationDocument->GetObjectField(TEXT("Body"))->SetObjectField(
		TEXT("AnimGraph"),
		MakeAnimGraphWithTwoSequencePlayers(TEXT("IdlePlayer"), TEXT("WalkPlayer"), TEXT("WalkPlayer"), TutorialIdleAnimationPath));
	FAssetDocumentApplyRequest RollbackMutationRequest;
	RollbackMutationRequest.Document = RollbackMutationDocument;
	RollbackMutationRequest.bSaveAsset = false;
	const FAssetDocumentResult RollbackMutationApplyResult = Service.Apply(RollbackMutationRequest);
	if (!RollbackMutationApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Rollback mutation setup apply failed: %s"), *RollbackMutationApplyResult.Message));
	}
	TestTrue(TEXT("Rollback mutation setup apply succeeds"), RollbackMutationApplyResult.IsSuccess());
	UAnimBlueprint* RollbackMutationAnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *RollbackMutationObjectPath);
	TestNotNull(TEXT("Rollback mutation AnimBlueprint loads"), RollbackMutationAnimBlueprint);
	TestTrue(
		TEXT("Rollback mutation starts with old result link"),
		IsResultLinkedToManagedNode(RollbackMutationAnimBlueprint, TEXT("WalkPlayer")));
	UEdGraphPin* ExtraInputPin = nullptr;
	TestTrue(
		TEXT("Rollback mutation setup creates extra authored output link"),
		AddSyntheticPoseInputLink(RollbackMutationAnimBlueprint, TEXT("IdlePlayer"), ExtraInputPin));
	TestTrue(
		TEXT("Rollback mutation setup keeps extra authored output link"),
		HasPoseOutputLink(RollbackMutationAnimBlueprint, TEXT("IdlePlayer"), ExtraInputPin));

	TSharedRef<FJsonObject> RollbackMutationSwitchDocument = MakeAnimBlueprintApplyDocument(
		RollbackMutationTarget,
		TEXT("/Script/Engine.AnimInstance"),
		TutorialSkeletonPath,
		TutorialPreviewMeshPath);
	RollbackMutationSwitchDocument->GetObjectField(TEXT("Body"))->SetObjectField(
		TEXT("AnimGraph"),
		MakeAnimGraphWithTwoSequencePlayers(TEXT("IdlePlayer"), TEXT("WalkPlayer"), TEXT("IdlePlayer"), TutorialIdleAnimationPath));
	FAssetDocumentApplyRequest RollbackMutationSwitchRequest;
	RollbackMutationSwitchRequest.Document = RollbackMutationSwitchDocument;
	RollbackMutationSwitchRequest.bSaveAsset = false;
	const FAssetDocumentResult RollbackMutationSwitchResult = Service.Apply(RollbackMutationSwitchRequest);
	TestFalse(
		TEXT("OutputPose repair rejects schema side effect on output links"),
		RollbackMutationSwitchResult.IsSuccess());
	TestTrue(
		TEXT("OutputPose repair rollback preserves old result link after mutation failure"),
		IsResultLinkedToManagedNode(RollbackMutationAnimBlueprint, TEXT("WalkPlayer")));
	TestFalse(
		TEXT("OutputPose repair rollback does not keep requested failed result link"),
		IsResultLinkedToManagedNode(RollbackMutationAnimBlueprint, TEXT("IdlePlayer")));
	TestTrue(
		TEXT("OutputPose repair rollback preserves extra authored output link"),
		HasPoseOutputLink(RollbackMutationAnimBlueprint, TEXT("IdlePlayer"), ExtraInputPin));

	const FString MultiLinkTarget = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_AnimGraph_MultiLink_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString MultiLinkObjectPath = FString::Printf(TEXT("%s.%s"), *MultiLinkTarget, *FPackageName::GetLongPackageAssetName(MultiLinkTarget));
	TSharedRef<FJsonObject> MultiLinkDocument = MakeAnimBlueprintApplyDocument(
		MultiLinkTarget,
		TEXT("/Script/Engine.AnimInstance"),
		TutorialSkeletonPath,
		TutorialPreviewMeshPath);
	MultiLinkDocument->GetObjectField(TEXT("Body"))->SetObjectField(
		TEXT("AnimGraph"),
		MakeAnimGraphWithTwoSequencePlayers(TEXT("IdlePlayer"), TEXT("RunPlayer"), TEXT("IdlePlayer"), TutorialIdleAnimationPath));
	FAssetDocumentApplyRequest MultiLinkRequest;
	MultiLinkRequest.Document = MultiLinkDocument;
	MultiLinkRequest.bSaveAsset = false;
	const FAssetDocumentResult MultiLinkApplyResult = Service.Apply(MultiLinkRequest);
	if (!MultiLinkApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Multi-link AnimGraph apply failed: %s"), *MultiLinkApplyResult.Message));
	}
	TestTrue(TEXT("Multi-link AnimGraph setup apply succeeds"), MultiLinkApplyResult.IsSuccess());
	UAnimBlueprint* MultiLinkAnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *MultiLinkObjectPath);
	TestNotNull(TEXT("Multi-link AnimBlueprint loads"), MultiLinkAnimBlueprint);
	TestTrue(TEXT("Second managed result link can be injected"), AddDirectResultPoseLink(MultiLinkAnimBlueprint, TEXT("RunPlayer")));
	TestTrue(TEXT("Injected graph has multiple managed result links"), CountResultManagedOutputLinks(MultiLinkAnimBlueprint) > 1);

	FAssetDocumentCapabilityContext MultiLinkContext;
	MultiLinkContext.Asset = MultiLinkAnimBlueprint;
	MultiLinkContext.AssetClass = UAnimBlueprint::StaticClass();
	TSharedRef<FJsonObject> MultiLinkExtractedBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult MultiLinkExtractResult = Capability.Extract(MultiLinkContext, MultiLinkExtractedBody);
	TestTrue(TEXT("Multi result link AnimGraph extract succeeds"), MultiLinkExtractResult.bSuccess);
	TestFalse(
		TEXT("Multi result link extract does not randomly choose OutputPose"),
		HasAnimGraphOutputPose(MultiLinkExtractedBody, TEXT("IdlePlayer"), TEXT("Pose"))
			|| HasAnimGraphOutputPose(MultiLinkExtractedBody, TEXT("RunPlayer"), TEXT("Pose")));
	TestTrue(
		TEXT("Multi result link extract reports skipped OutputPose"),
		HasAnimGraphSkippedOutputPose(MultiLinkExtractedBody));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintStateMachinesTest,
	"AssetFactory.AssetDocument.AnimBlueprint.StateMachines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintStateMachinesTest::RunTest(const FString&)
{
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UAnimBlueprint::StaticClass();
	const FAnimBlueprintAssetDocumentCapability Capability;

	TestTrue(
		TEXT("StateMachines accepts empty recursive graph region"),
		Capability.Validate(Context, MakeBodyWithStateMachineGraphs({})).bSuccess);

	const FAssetDocumentCapabilityResult AuthoredStateMachineResult =
		Capability.Validate(
			Context,
			MakeBodyWithStateMachineGraphs({MakeStateMachineGraph(TEXT("Locomotion"))}));
	TestTrue(TEXT("StateMachines validates authored recursive graph data"), AuthoredStateMachineResult.bSuccess);

	const FAssetDocumentCapabilityResult DuplicateStateResult =
		Capability.Validate(
			Context,
			MakeBodyWithStateMachineGraphs({MakeStateMachineGraph(TEXT("Locomotion"), true)}));
	TestFalse(TEXT("StateMachines rejects duplicate graph node identities"), DuplicateStateResult.bSuccess);
	TestTrue(TEXT("Duplicate state is not silently accepted"), HasDiagnostic(DuplicateStateResult, TEXT("/Body/StateMachines/Graphs/0/Nodes/1/Id"), TEXT("DuplicateGraphNodeId")));

	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_StateMachines_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
	TSharedRef<FJsonObject> Document = MakeAnimBlueprintApplyDocument(Target);
	Document->GetObjectField(TEXT("Body"))->SetObjectField(
		TEXT("StateMachines"),
		MakeStateMachinesGraphRegion({MakeStateMachineGraph(TEXT("Locomotion"))}));
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;
	FAssetDocumentService Service;
	const FAssetDocumentResult ApplyResult = Service.Apply(Request);
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("StateMachines recursive apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("StateMachines recursive apply succeeds"), ApplyResult.IsSuccess());

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
	TestNotNull(TEXT("Created AnimBlueprint with state machine loads"), AnimBlueprint);
	FAssetDocumentCapabilityContext AssetContext;
	AssetContext.Asset = AnimBlueprint;
	AssetContext.AssetClass = UAnimBlueprint::StaticClass();

	TSharedRef<FJsonObject> ExtractedBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ExtractResult = Capability.Extract(AssetContext, ExtractedBody);
	TestTrue(TEXT("StateMachines extract succeeds"), ExtractResult.bSuccess);
	const TSharedPtr<FJsonObject>* ExtractedStateMachines = nullptr;
	TestTrue(
		TEXT("Extract includes recursive StateMachines object"),
		ExtractedBody->TryGetObjectField(TEXT("StateMachines"), ExtractedStateMachines)
			&& ExtractedStateMachines
			&& (*ExtractedStateMachines)->HasTypedField<EJson::Array>(TEXT("Graphs")));
	TestTrue(
		TEXT("StateMachines extract preserves EntryState metadata"),
		HasStateMachineEntryState(ExtractedBody, TEXT("Locomotion"), TEXT("Idle")));
	TestTrue(
		TEXT("StateMachines extract preserves state position"),
		HasStateMachineNodePosition(ExtractedBody, TEXT("Locomotion"), TEXT("Idle"), 0.0, 0.0));
	TestTrue(
		TEXT("StateMachines extract preserves transition position"),
		HasStateMachineNodePosition(ExtractedBody, TEXT("Locomotion"), TEXT("IdleToRun"), 120.0, 0.0));
	TestTrue(
		TEXT("StateMachines extract includes state-owned StatePose subgraph"),
		HasStateMachineSubgraph(ExtractedBody, TEXT("Locomotion"), TEXT("IdlePose"), TEXT("StatePose"), TEXT("State"), TEXT("Idle")));
	TestTrue(
		TEXT("StateMachines extract includes transition-owned TransitionRule subgraph"),
		HasStateMachineSubgraph(ExtractedBody, TEXT("Locomotion"), TEXT("CanStartRunning"), TEXT("TransitionRule"), TEXT("Transition"), TEXT("IdleToRun")));
	TestTrue(
		TEXT("TransitionRule extract includes result pin metadata"),
		HasStateMachineSubgraphMetadataResultPin(ExtractedBody, TEXT("Locomotion"), TEXT("CanStartRunning"), TEXT("CanEnterTransition")));

	UAnimationTransitionGraph* IdleToRunGraph =
		FindTransitionRuleGraph(AnimBlueprint, TEXT("Locomotion"), TEXT("IdleToRun"));
	TestNotNull(TEXT("Applied transition rule graph exists"), IdleToRunGraph);
	const int32 TransitionResultCountBeforeExtract = CountTransitionResultNodes(IdleToRunGraph);
	TestTrue(TEXT("Applied transition rule has a result node"), TransitionResultCountBeforeExtract > 0);
	if (IdleToRunGraph)
	{
		IdleToRunGraph->MyResultNode = nullptr;
	}
	TSharedRef<FJsonObject> StaleResultExtractedBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult StaleResultExtract =
		Capability.Extract(AssetContext, StaleResultExtractedBody);
	TestTrue(TEXT("StateMachines extract with stale transition result pointer succeeds"), StaleResultExtract.bSuccess);
	TestEqual(
		TEXT("StateMachines extract does not create duplicate transition result nodes"),
		CountTransitionResultNodes(IdleToRunGraph),
		TransitionResultCountBeforeExtract);
	TestNull(
		TEXT("StateMachines extract does not repair transition result pointer"),
		IdleToRunGraph ? IdleToRunGraph->MyResultNode.Get() : nullptr);

	TSharedRef<FJsonObject> BrokenSubgraphDocument = MakeAnimBlueprintApplyDocument(Target);
	BrokenSubgraphDocument->GetObjectField(TEXT("Body"))->SetObjectField(
		TEXT("StateMachines"),
		MakeStateMachinesGraphRegion({MakeStateMachineGraphWithBrokenStatePoseField(TEXT("Locomotion"))}));
	FAssetDocumentApplyRequest BrokenSubgraphRequest;
	BrokenSubgraphRequest.Document = BrokenSubgraphDocument;
	BrokenSubgraphRequest.bSaveAsset = false;
	const FAssetDocumentResult BrokenSubgraphResult = Service.Apply(BrokenSubgraphRequest);
	TestFalse(TEXT("StateMachines apply rejects invalid subgraph field on existing asset"), BrokenSubgraphResult.IsSuccess());
	TSharedRef<FJsonObject> BrokenRollbackExtractedBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult BrokenRollbackExtract =
		Capability.Extract(AssetContext, BrokenRollbackExtractedBody);
	TestTrue(TEXT("StateMachines extract after failed subgraph apply succeeds"), BrokenRollbackExtract.bSuccess);
	TestTrue(
		TEXT("Failed subgraph apply preserves previous EntryState"),
		HasStateMachineEntryState(BrokenRollbackExtractedBody, TEXT("Locomotion"), TEXT("Idle")));
	TestTrue(
		TEXT("Failed subgraph apply preserves previous StatePose subgraph identity"),
		HasStateMachineSubgraph(BrokenRollbackExtractedBody, TEXT("Locomotion"), TEXT("IdlePose"), TEXT("StatePose"), TEXT("State"), TEXT("Idle")));
	TestFalse(
		TEXT("Failed subgraph apply does not leave new StatePose subgraph identity"),
		HasStateMachineSubgraph(BrokenRollbackExtractedBody, TEXT("Locomotion"), TEXT("BrokenPose"), TEXT("StatePose"), TEXT("State"), TEXT("Idle")));

	const FString MissingTargetTarget = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_StateMachines_MissingTarget_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	TSharedRef<FJsonObject> MissingTargetDocument = MakeAnimBlueprintApplyDocument(MissingTargetTarget);
	MissingTargetDocument->GetObjectField(TEXT("Body"))->SetObjectField(
		TEXT("StateMachines"),
		MakeStateMachinesGraphRegion({MakeStateMachineGraph(TEXT("Locomotion"), false, true)}));
	FAssetDocumentApplyRequest MissingTargetRequest;
	MissingTargetRequest.Document = MissingTargetDocument;
	MissingTargetRequest.bSaveAsset = false;
	const FAssetDocumentResult MissingTargetResult = Service.Apply(MissingTargetRequest);
	TestFalse(TEXT("StateMachines apply rejects missing transition target"), MissingTargetResult.IsSuccess());
	TestTrue(
		TEXT("Missing transition target reports exact link endpoint path"),
		HasDiagnostic(
			MissingTargetResult,
			TEXT("/Body/StateMachines/Graphs/Locomotion/Links/IdleToRun.Out->Missing.In/To/Node"),
			TEXT("UnknownStateMachineLinkEndpoint")));

	TestTrue(
		TEXT("TransitionGraphs accepts empty compatibility value"),
		Capability.Validate(Context, MakeBodyWithEmptyArrayRegion(TEXT("TransitionGraphs"))).bSuccess);

	const FAssetDocumentCapabilityResult AuthoredTransitionGraphResult =
		Capability.Validate(Context, MakeBodyWithTransitionGraphs({MakeTransitionGraph(TEXT("Locomotion"), TEXT("IdleToRun"))}));
	TestFalse(TEXT("TransitionGraphs rejects non-empty root-only data until materialization exists"), AuthoredTransitionGraphResult.bSuccess);
	TestTrue(
		TEXT("TransitionGraphs non-empty diagnostic uses region path"),
		HasDiagnostic(AuthoredTransitionGraphResult, TEXT("/Body/TransitionGraphs"), TEXT("UnsupportedAnimBlueprintRegion")));

	const FAssetDocumentCapabilityResult DuplicateTransitionGraphResult =
		Capability.Validate(
			Context,
			MakeBodyWithTransitionGraphs({
				MakeTransitionGraph(TEXT("Locomotion"), TEXT("IdleToRun")),
				MakeTransitionGraph(TEXT("locomotion"), TEXT("idletorun"))}));
	TestFalse(TEXT("TransitionGraphs rejects duplicate authored input through deferred boundary"), DuplicateTransitionGraphResult.bSuccess);
	TestTrue(
		TEXT("Duplicate transition graph is not silently accepted"),
		HasDiagnostic(DuplicateTransitionGraphResult, TEXT("/Body/TransitionGraphs"), TEXT("UnsupportedAnimBlueprintRegion")));

	const FAssetDocumentCapabilityResult UnsupportedRuleNodeResult =
		Capability.Validate(
			Context,
			MakeBodyWithTransitionGraphs({MakeTransitionGraph(TEXT("Locomotion"), TEXT("IdleToRun"), true)}));
	TestFalse(TEXT("TransitionGraphs rejects authored rule nodes while materialization is deferred"), UnsupportedRuleNodeResult.bSuccess);
	TestTrue(
		TEXT("Unsupported transition graph node is not silently accepted"),
		HasDiagnostic(
			UnsupportedRuleNodeResult,
			TEXT("/Body/TransitionGraphs"),
			TEXT("UnsupportedAnimBlueprintRegion")));

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult =
		Capability.Diff(AssetContext, MakeBodyWithStateMachineGraphs({MakeStateMachineGraph(TEXT("Locomotion"))}), DiffEntries);
	TestTrue(TEXT("StateMachines and TransitionGraphs diff succeeds"), DiffResult.bSuccess);
	TestFalse(TEXT("StateMachines diff does not report changed entries after apply/extract"), HasDiffStatus(DiffEntries, TEXT("changed")));
	TestFalse(TEXT("StateMachines diff does not report extra entries after apply/extract"), HasDiffStatus(DiffEntries, TEXT("extra")));
	TestFalse(TEXT("StateMachines diff does not report missing entries after apply/extract"), HasDiffStatus(DiffEntries, TEXT("missing")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintAnimLayersAndParentAssetOverridesTest,
	"AssetFactory.AssetDocument.AnimBlueprint.AnimLayersAndParentAssetOverrides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintAnimLayersAndParentAssetOverridesTest::RunTest(const FString&)
{
	const FString AnimationAssetPath = TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/Tutorial_Idle.Tutorial_Idle");
	const FString ParentGuidA = TEXT("01234567-89ab-cdef-0123-456789abcdef");
	const FString ParentGuidB = TEXT("11111111-2222-3333-4444-555555555555");

	FAssetDocumentCapabilityContext ValidationContext;
	ValidationContext.AssetClass = UAnimBlueprint::StaticClass();
	const FAnimBlueprintAssetDocumentCapability Capability;

	TestTrue(
		TEXT("AnimLayers validates a non-empty recursive graph region"),
		Capability.Validate(
			ValidationContext,
			MakeBodyWithAnimLayerGraphs({MakeAnimLayerGraph(TEXT("UpperBodyLayer"))})).bSuccess);

	TestTrue(
		TEXT("ParentAssetOverrides validates stable guid identity array"),
		Capability.Validate(
			ValidationContext,
			MakeBodyWithParentAssetOverrides({MakeParentAssetOverride(ParentGuidA, AnimationAssetPath)})).bSuccess);

	const FAssetDocumentCapabilityResult DuplicateGuidResult =
		Capability.Validate(
			ValidationContext,
			MakeBodyWithParentAssetOverrides({
				MakeParentAssetOverride(ParentGuidA, AnimationAssetPath),
				MakeParentAssetOverride(ParentGuidA, AnimationAssetPath)}));
	TestFalse(TEXT("ParentAssetOverrides rejects duplicate ParentNodeGuid"), DuplicateGuidResult.bSuccess);
	TestTrue(
		TEXT("Duplicate ParentNodeGuid diagnostic uses authored index"),
		HasDiagnostic(DuplicateGuidResult, TEXT("/Body/ParentAssetOverrides/1/ParentNodeGuid"), TEXT("DuplicateParentAssetOverrideGuid")));

	TSharedPtr<FJsonObject> MissingPathOverride = MakeParentAssetOverride(ParentGuidA, AnimationAssetPath);
	const TSharedPtr<FJsonObject>* MissingPathAssetRef = nullptr;
	if (MissingPathOverride->TryGetObjectField(TEXT("NewAsset"), MissingPathAssetRef) && MissingPathAssetRef && MissingPathAssetRef->IsValid())
	{
		(*MissingPathAssetRef)->RemoveField(TEXT("Path"));
	}
	const FAssetDocumentCapabilityResult MissingAssetPathResult =
		Capability.Validate(ValidationContext, MakeBodyWithParentAssetOverrides({MissingPathOverride}));
	TestFalse(TEXT("ParentAssetOverrides rejects missing animation asset path"), MissingAssetPathResult.bSuccess);
	TestTrue(
		TEXT("Missing asset path diagnostic uses guid identity path"),
		HasDiagnostic(
			MissingAssetPathResult,
			TEXT("/Body/ParentAssetOverrides/01234567-89ab-cdef-0123-456789abcdef/NewAsset/Path"),
			TEXT("MissingParentAssetOverrideAssetPath")));

	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_Overrides_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
	TSharedRef<FJsonObject> Document = MakeAnimBlueprintApplyDocument(Target);
	Document->GetObjectField(TEXT("Body"))->SetArrayField(
		TEXT("ParentAssetOverrides"),
		MakeParentAssetOverrideArray({MakeParentAssetOverride(ParentGuidA, AnimationAssetPath)}));

	FAssetDocumentService Service;
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;
	const FAssetDocumentResult ApplyResult = Service.Apply(Request);
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("ParentAssetOverrides apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("ParentAssetOverrides apply succeeds"), ApplyResult.IsSuccess());

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
	TestNotNull(TEXT("Created AnimBlueprint loads"), AnimBlueprint);
	UAnimationAsset* ExpectedAsset = LoadObject<UAnimationAsset>(nullptr, *AnimationAssetPath);
	TestNotNull(TEXT("Expected animation asset loads"), ExpectedAsset);
	if (AnimBlueprint && ExpectedAsset)
	{
		TestEqual(TEXT("One parent asset override applied"), AnimBlueprint->ParentAssetOverrides.Num(), 1);
		if (AnimBlueprint->ParentAssetOverrides.Num() == 1)
		{
			TestEqual(TEXT("ParentNodeGuid is preserved"), AnimBlueprint->ParentAssetOverrides[0].ParentNodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower), ParentGuidA);
			TestEqual(TEXT("NewAsset is applied"), AnimBlueprint->ParentAssetOverrides[0].NewAsset.Get(), ExpectedAsset);
		}
	}

	FAssetDocumentCapabilityContext Context;
	Context.Asset = AnimBlueprint;
	Context.AssetClass = UAnimBlueprint::StaticClass();
	TSharedRef<FJsonObject> ExtractedBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ExtractResult = Capability.Extract(Context, ExtractedBody);
	TestTrue(TEXT("ParentAssetOverrides extract succeeds"), ExtractResult.bSuccess);
	TestTrue(TEXT("Extract includes one parent asset override"), HasArrayFieldCount(ExtractedBody, TEXT("ParentAssetOverrides"), 1));
	const TArray<TSharedPtr<FJsonValue>>* ExtractedOverrides = nullptr;
	if (ExtractedBody->TryGetArrayField(TEXT("ParentAssetOverrides"), ExtractedOverrides) && ExtractedOverrides && ExtractedOverrides->Num() == 1)
	{
		const TSharedPtr<FJsonObject> ExtractedOverride = (*ExtractedOverrides)[0]->AsObject();
		TestTrue(TEXT("Extracted parent override is object"), ExtractedOverride.IsValid());
		if (ExtractedOverride.IsValid())
		{
			FString ExtractedGuid;
			TestTrue(TEXT("Extracted ParentNodeGuid is present"), ExtractedOverride->TryGetStringField(TEXT("ParentNodeGuid"), ExtractedGuid));
			TestEqual(TEXT("Extracted ParentNodeGuid shape is stable"), ExtractedGuid, ParentGuidA);
			const TSharedPtr<FJsonObject>* ExtractedAssetRef = nullptr;
			TestTrue(TEXT("Extracted NewAsset is object"), ExtractedOverride->TryGetObjectField(TEXT("NewAsset"), ExtractedAssetRef));
			if (ExtractedAssetRef && ExtractedAssetRef->IsValid())
			{
				FString ExtractedKind;
				FString ExtractedPath;
				TestTrue(TEXT("Extracted NewAsset.Kind is present"), (*ExtractedAssetRef)->TryGetStringField(TEXT("Kind"), ExtractedKind));
				TestEqual(TEXT("Extracted NewAsset.Kind is AssetRef"), ExtractedKind, FString(TEXT("AssetRef")));
				TestTrue(TEXT("Extracted NewAsset.Path is present"), (*ExtractedAssetRef)->TryGetStringField(TEXT("Path"), ExtractedPath));
				if (ExpectedAsset)
				{
					TestEqual(TEXT("Extracted NewAsset.Path is stable"), ExtractedPath, ExpectedAsset->GetPathName());
				}
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult =
		Capability.Diff(
			Context,
			MakeBodyWithParentAssetOverrides({
				MakeParentAssetOverride(ParentGuidA, AnimationAssetPath),
				MakeParentAssetOverride(ParentGuidB, AnimationAssetPath)}),
			DiffEntries);
	TestTrue(TEXT("ParentAssetOverrides diff succeeds"), DiffResult.bSuccess);
	TestTrue(
		TEXT("ParentAssetOverrides diff uses guid identity path"),
		HasDiffPath(DiffEntries, TEXT("/Body/ParentAssetOverrides/11111111-2222-3333-4444-555555555555")));

	const FString AliasTarget = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_OverrideAlias_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString AliasObjectPath = FString::Printf(TEXT("%s.%s"), *AliasTarget, *FPackageName::GetLongPackageAssetName(AliasTarget));
	TSharedRef<FJsonObject> AliasDocument = MakeAnimBlueprintApplyDocument(AliasTarget);
	AliasDocument->GetObjectField(TEXT("Body"))->SetObjectField(TEXT("AnimGraph"), MakeAnimGraphWithSequencePlayer(TEXT("IdlePlayer")));
	AliasDocument->GetObjectField(TEXT("Body"))->SetArrayField(
		TEXT("ParentAssetOverrides"),
		MakeParentAssetOverrideArray({MakeParentAssetOverrideByNode(TEXT("IdlePlayer"), AnimationAssetPath)}));

	FAssetDocumentApplyRequest AliasRequest;
	AliasRequest.Document = AliasDocument;
	AliasRequest.bSaveAsset = false;
	const FAssetDocumentResult AliasApplyResult = Service.Apply(AliasRequest);
	if (!AliasApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("ParentAssetOverrides node alias apply failed: %s"), *AliasApplyResult.Message));
	}
	TestTrue(TEXT("ParentAssetOverrides node alias apply succeeds"), AliasApplyResult.IsSuccess());

	UAnimBlueprint* AliasAnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *AliasObjectPath);
	TestNotNull(TEXT("Alias AnimBlueprint loads"), AliasAnimBlueprint);
	const FGuid ExpectedIdlePlayerGuid = MakeExpectedManagedAnimGraphNodeGuid(TEXT("IdlePlayer"));
	if (AliasAnimBlueprint && ExpectedAsset)
	{
		TestEqual(TEXT("One node alias parent asset override applied"), AliasAnimBlueprint->ParentAssetOverrides.Num(), 1);
		if (AliasAnimBlueprint->ParentAssetOverrides.Num() == 1)
		{
			TestEqual(TEXT("Node alias resolves to managed AnimGraph node guid"), AliasAnimBlueprint->ParentAssetOverrides[0].ParentNodeGuid, ExpectedIdlePlayerGuid);
			TestEqual(TEXT("Node alias NewAsset is applied"), AliasAnimBlueprint->ParentAssetOverrides[0].NewAsset.Get(), ExpectedAsset);
		}
	}

	FAssetDocumentCapabilityContext AliasContext;
	AliasContext.Asset = AliasAnimBlueprint;
	AliasContext.AssetClass = UAnimBlueprint::StaticClass();
	TSharedRef<FJsonObject> AliasExtractedBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult AliasExtractResult = Capability.Extract(AliasContext, AliasExtractedBody);
	TestTrue(TEXT("ParentAssetOverrides alias extract succeeds"), AliasExtractResult.bSuccess);
	TestTrue(TEXT("Alias extract includes one parent asset override"), HasArrayFieldCount(AliasExtractedBody, TEXT("ParentAssetOverrides"), 1));
	const TArray<TSharedPtr<FJsonValue>>* AliasExtractedOverrides = nullptr;
	if (AliasExtractedBody->TryGetArrayField(TEXT("ParentAssetOverrides"), AliasExtractedOverrides) && AliasExtractedOverrides && AliasExtractedOverrides->Num() == 1)
	{
		const TSharedPtr<FJsonObject> AliasExtractedOverride = (*AliasExtractedOverrides)[0]->AsObject();
		TestTrue(TEXT("Alias extracted parent override is object"), AliasExtractedOverride.IsValid());
		if (AliasExtractedOverride.IsValid())
		{
			FString ExtractedNode;
			TestTrue(TEXT("Extracted parent override Node is present"), AliasExtractedOverride->TryGetStringField(TEXT("Node"), ExtractedNode));
			TestEqual(TEXT("Extracted parent override Node roundtrips"), ExtractedNode, FString(TEXT("IdlePlayer")));
			const TSharedPtr<FJsonObject>* Evidence = nullptr;
			TestTrue(TEXT("Extracted parent override Evidence is present"), AliasExtractedOverride->TryGetObjectField(TEXT("Evidence"), Evidence));
			if (Evidence && Evidence->IsValid())
			{
				FString EvidenceGuid;
				TestTrue(TEXT("Extracted Evidence.ParentNodeGuid is present"), (*Evidence)->TryGetStringField(TEXT("ParentNodeGuid"), EvidenceGuid));
				TestEqual(TEXT("Extracted Evidence.ParentNodeGuid is stable"), EvidenceGuid, ExpectedIdlePlayerGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> UnknownAliasDiffEntries;
	const FAssetDocumentCapabilityResult UnknownAliasResult =
		Capability.Diff(
			AliasContext,
			MakeBodyWithParentAssetOverrides({MakeParentAssetOverrideByNode(TEXT("MissingPlayer"), AnimationAssetPath)}),
			UnknownAliasDiffEntries);
	TestFalse(TEXT("ParentAssetOverrides rejects unresolved node alias"), UnknownAliasResult.bSuccess);
	TestTrue(
		TEXT("Unresolved node alias diagnostic uses semantic alias path"),
		HasDiagnostic(UnknownAliasResult, TEXT("/Body/ParentAssetOverrides/MissingPlayer/Node"), TEXT("UnknownParentOverrideNode")));

	TArray<TSharedPtr<FJsonValue>> AliasAssetDiffEntries;
	const FString AlternateAnimationAssetPath = TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/Tutorial_Walk_Fwd.Tutorial_Walk_Fwd");
	const FAssetDocumentCapabilityResult AliasAssetDiffResult =
		Capability.Diff(
			AliasContext,
			MakeBodyWithParentAssetOverrides({MakeParentAssetOverrideByNode(TEXT("IdlePlayer"), AlternateAnimationAssetPath)}),
			AliasAssetDiffEntries);
	TestTrue(TEXT("ParentAssetOverrides alias diff succeeds"), AliasAssetDiffResult.bSuccess);
	TestTrue(
		TEXT("ParentAssetOverrides diff uses node identity path for asset changes"),
		HasDiffPath(AliasAssetDiffEntries, TEXT("/Body/ParentAssetOverrides/IdlePlayer/NewAsset")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintCoreObjectRegionsTest,
	"AssetFactory.AssetDocument.AnimBlueprint.CoreObjectRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintCoreObjectRegionsTest::RunTest(const FString&)
{
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UAnimBlueprint::StaticClass();
	const FAnimBlueprintAssetDocumentCapability Capability;

	const FAssetDocumentCapabilityResult MissingParentClassResult =
		Capability.Validate(Context, MakeBodyWithMissingParentClass());
	TestFalse(TEXT("ParentClass missing Class rejects"), MissingParentClassResult.bSuccess);
	TestTrue(
		TEXT("ParentClass missing Class reports MissingParentClass"),
		HasDiagnostic(MissingParentClassResult, TEXT("/Body/ParentClass/Class"), TEXT("MissingParentClass")));

	const FAssetDocumentCapabilityResult InvalidParentClassResult =
		Capability.Validate(Context, MakeBodyWithParentClass(TEXT("/Script/Engine.Actor")));
	TestFalse(TEXT("ParentClass must be an AnimInstance child"), InvalidParentClassResult.bSuccess);
	TestTrue(
		TEXT("ParentClass reports InvalidAnimBlueprintParentClass"),
		HasDiagnostic(InvalidParentClassResult, TEXT("/Body/ParentClass/Class"), TEXT("InvalidAnimBlueprintParentClass")));

	const FAssetDocumentCapabilityResult InvalidTemplateSkeletonResult =
		Capability.Validate(Context, MakeBodyWithTemplateAndTargetSkeleton());
	TestFalse(TEXT("Template AnimBlueprint cannot author TargetSkeleton"), InvalidTemplateSkeletonResult.bSuccess);
	TestTrue(
		TEXT("Template skeleton conflict reports InvalidTemplateSkeleton"),
		HasDiagnostic(InvalidTemplateSkeletonResult, TEXT("/Body/TargetSkeleton"), TEXT("InvalidTemplateSkeleton"))
			|| HasDiagnostic(InvalidTemplateSkeletonResult, TEXT("/Body/Template/bIsTemplate"), TEXT("InvalidTemplateSkeleton")));

	const FAssetDocumentCapabilityResult MissingCreateSkeletonResult =
		Capability.Validate(Context, MakeBodyWithNonTemplateMissingTargetSkeleton());
	TestFalse(TEXT("Non-template AnimBlueprint create requires TargetSkeleton"), MissingCreateSkeletonResult.bSuccess);
	TestTrue(
		TEXT("Missing create skeleton diagnostic uses TargetSkeleton path"),
		HasDiagnostic(MissingCreateSkeletonResult, TEXT("/Body/TargetSkeleton"), TEXT("MissingTargetSkeleton")));

	const FAssetDocumentCapabilityResult MismatchedPreviewSkeletonResult =
		Capability.Validate(Context, MakeBodyWithMismatchedPreviewSkeleton());
	TestFalse(TEXT("Preview skeletal mesh skeleton must match TargetSkeleton"), MismatchedPreviewSkeletonResult.bSuccess);
	TestTrue(
		TEXT("Preview skeleton mismatch diagnostic uses PreviewSkeletalMesh path"),
		HasDiagnostic(
			MismatchedPreviewSkeletonResult,
			TEXT("/Body/Preview/PreviewSkeletalMesh"),
			TEXT("MismatchedPreviewSkeletalMeshSkeleton")));

	const FAssetDocumentCapabilityResult InvalidPreviewMethodResult =
		Capability.Validate(Context, MakeBodyWithPreviewApplicationMethod(TEXT("Bogus")));
	TestFalse(TEXT("Preview rejects unknown application method"), InvalidPreviewMethodResult.bSuccess);
	TestTrue(
		TEXT("Preview method reports InvalidPreviewAnimationBlueprintApplicationMethod"),
		HasDiagnostic(
			InvalidPreviewMethodResult,
			TEXT("/Body/Preview/PreviewAnimationBlueprintApplicationMethod"),
			TEXT("InvalidPreviewAnimationBlueprintApplicationMethod")));

	const FAssetDocumentCapabilityResult UnknownOptimizationFieldResult =
		Capability.Validate(Context, MakeBodyWithUnknownOptimizationField());
	TestFalse(TEXT("Optimization rejects unknown object fields"), UnknownOptimizationFieldResult.bSuccess);
	TestTrue(
		TEXT("Optimization unknown field reports schema diagnostic"),
		HasDiagnostic(UnknownOptimizationFieldResult, TEXT("/Body/Optimization/bUnknownOptimizationFlag"), TEXT("UnknownObjectField")));

	const FAssetDocumentCapabilityResult UnknownBodyKeyResult = Capability.Validate(Context, MakeBodyWithUnknownKey());
	TestFalse(TEXT("Dispatcher still rejects unknown Body key"), UnknownBodyKeyResult.bSuccess);
	TestTrue(
		TEXT("Dispatcher preserves UnknownBodyKey code"),
		HasDiagnostic(UnknownBodyKeyResult, TEXT("/Body/UnexpectedGraph"), TEXT("UnknownBodyKey")));

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult ObjectDiffResult =
		Capability.Diff(Context, MakeBodyWithTargetSkeletonAndPreviewApplicationMethod(TEXT("LinkedAnimGraph")), DiffEntries);
	TestTrue(TEXT("Preview object diff succeeds"), ObjectDiffResult.bSuccess);
	TestTrue(TEXT("Preview object diff reports region path"), HasDiffPath(DiffEntries, TEXT("/Body/Preview")));

	FAssetDocumentCapabilityContext SparseDiffContext;
	SparseDiffContext.Asset = NewObject<UAnimBlueprint>(GetTransientPackage());
	SparseDiffContext.AssetClass = UAnimBlueprint::StaticClass();

	DiffEntries.Reset();
	const FAssetDocumentCapabilityResult SparseNoChangeDiffResult =
		Capability.Diff(SparseDiffContext, MakeBodyWithSparseDefaultCoreObjects(), DiffEntries);
	TestTrue(TEXT("Sparse default object diff succeeds"), SparseNoChangeDiffResult.bSuccess);
	TestEqual(TEXT("Sparse default object diff does not report omitted defaults"), DiffEntries.Num(), 0);

	DiffEntries.Reset();
	const FAssetDocumentCapabilityResult SparseChangedDiffResult =
		Capability.Diff(SparseDiffContext, MakeBodyWithSparseChangedPreview(), DiffEntries);
	TestTrue(TEXT("Sparse changed object diff succeeds"), SparseChangedDiffResult.bSuccess);
	TestTrue(TEXT("Sparse changed object diff reports Preview"), HasDiffPath(DiffEntries, TEXT("/Body/Preview")));

	DiffEntries.Reset();
	const FAssetDocumentCapabilityResult SkeletonDiffResult =
		Capability.Diff(
			Context,
			MakeBodyWithTargetSkeleton(),
			DiffEntries);
	TestTrue(TEXT("TargetSkeleton diff succeeds"), SkeletonDiffResult.bSuccess);
	TestTrue(TEXT("TargetSkeleton diff reports region path"), HasDiffPath(DiffEntries, TEXT("/Body/TargetSkeleton")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintCreateUpdateLifecycleTest,
	"AssetFactory.AssetDocument.AnimBlueprint.CreateUpdateLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintCreateUpdateLifecycleTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_Lifecycle_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
	const FString SkeletonPath = TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh_Skeleton.DefaultSkeletalMesh_Skeleton");
	const FString PreviewMeshPath = TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh.DefaultSkeletalMesh");

	FAssetDocumentService Service;
	FAssetDocumentApplyRequest Request;
	Request.Document = MakeAnimBlueprintApplyDocument(Target, TEXT("/Script/Engine.AnimInstance"), SkeletonPath, PreviewMeshPath, false);
	Request.bSaveAsset = false;

	const FAssetDocumentResult Result = Service.Apply(Request);
	if (!Result.IsSuccess())
	{
		AddError(FString::Printf(TEXT("AnimBlueprint apply failed: %s"), *Result.Message));
	}
	TestTrue(TEXT("AnimBlueprint apply succeeds"), Result.IsSuccess());

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
	TestNotNull(TEXT("Created asset is UAnimBlueprint"), AnimBlueprint);
	USkeleton* ExpectedSkeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	USkeletalMesh* ExpectedPreviewMesh = LoadObject<USkeletalMesh>(nullptr, *PreviewMeshPath);
	TestNotNull(TEXT("Expected skeleton asset loads"), ExpectedSkeleton);
	TestNotNull(TEXT("Expected preview mesh asset loads"), ExpectedPreviewMesh);
	if (AnimBlueprint)
	{
		TestEqual(TEXT("Parent class is AnimInstance"), AnimBlueprint->ParentClass.Get(), UAnimInstance::StaticClass());
		TestFalse(TEXT("Created AnimBlueprint is not a template"), AnimBlueprint->bIsTemplate);
		TestEqual(TEXT("TargetSkeleton is authored skeleton"), AnimBlueprint->TargetSkeleton.Get(), ExpectedSkeleton);
		TestEqual(TEXT("Preview mesh is authored mesh"), AnimBlueprint->GetPreviewMesh(), ExpectedPreviewMesh);
	}

	FAssetDocumentApplyRequest TemplateUpdateRequest;
	TemplateUpdateRequest.Document = MakeAnimBlueprintApplyDocument(Target, TEXT("/Script/Engine.AnimInstance"), SkeletonPath, PreviewMeshPath, true);
	TemplateUpdateRequest.bSaveAsset = false;
	const FAssetDocumentResult TemplateUpdateResult = Service.Apply(TemplateUpdateRequest);
	if (!TemplateUpdateResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("AnimBlueprint template update failed: %s"), *TemplateUpdateResult.Message));
	}
	TestTrue(TEXT("AnimBlueprint update succeeds"), TemplateUpdateResult.IsSuccess());
	if (AnimBlueprint)
	{
		TestTrue(TEXT("Updated AnimBlueprint is template"), AnimBlueprint->bIsTemplate);
		TestNull(TEXT("Template update clears TargetSkeleton"), AnimBlueprint->TargetSkeleton.Get());
	}

	const FString BadTarget = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_Invalid_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FAssetDocumentApplyRequest BadRequest;
	BadRequest.Document = MakeAnimBlueprintApplyDocument(BadTarget, TEXT("/Script/Engine.Actor"), SkeletonPath, PreviewMeshPath, false);
	BadRequest.bSaveAsset = false;
	const FAssetDocumentResult BadResult = Service.Apply(BadRequest);
	TestFalse(TEXT("Invalid AnimBlueprint create preflight fails"), BadResult.IsSuccess());

	UObject* BadAsset = FindObject<UObject>(
		nullptr,
		*FString::Printf(TEXT("%s.%s"), *BadTarget, *FPackageName::GetLongPackageAssetName(BadTarget)));
	TestNull(TEXT("Invalid create does not leave a loadable asset"), BadAsset);

	const FString MissingSkeletonTarget = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_MissingSkeleton_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FAssetDocumentApplyRequest MissingSkeletonRequest;
	MissingSkeletonRequest.Document = MakeAnimBlueprintApplyDocument(MissingSkeletonTarget, TEXT("/Script/Engine.AnimInstance"), SkeletonPath, PreviewMeshPath, false);
	MissingSkeletonRequest.Document->GetObjectField(TEXT("Body"))->RemoveField(TEXT("TargetSkeleton"));
	MissingSkeletonRequest.bSaveAsset = false;
	const FAssetDocumentResult MissingSkeletonResult = Service.Apply(MissingSkeletonRequest);
	TestFalse(TEXT("Service create rejects non-template AnimBlueprint without TargetSkeleton"), MissingSkeletonResult.IsSuccess());
	TestTrue(
		TEXT("Missing skeleton create failure mentions TargetSkeleton"),
		MissingSkeletonResult.Message.Contains(TEXT("TargetSkeleton")) || MissingSkeletonResult.Message.Contains(TEXT("MissingTargetSkeleton")));
	UObject* MissingSkeletonAsset = FindObject<UObject>(
		nullptr,
		*FString::Printf(TEXT("%s.%s"), *MissingSkeletonTarget, *FPackageName::GetLongPackageAssetName(MissingSkeletonTarget)));
	TestNull(TEXT("Missing skeleton create does not leave a loadable asset"), MissingSkeletonAsset);

	const FString MismatchedPreviewTarget = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_UpdatePreview_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString MismatchedPreviewMeshPath = TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP");
	FAssetDocumentApplyRequest PreviewBaseRequest;
	PreviewBaseRequest.Document = MakeAnimBlueprintApplyDocument(MismatchedPreviewTarget, TEXT("/Script/Engine.AnimInstance"), SkeletonPath, PreviewMeshPath, false);
	PreviewBaseRequest.bSaveAsset = false;
	const FAssetDocumentResult PreviewBaseResult = Service.Apply(PreviewBaseRequest);
	if (!PreviewBaseResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("AnimBlueprint preview mismatch base apply failed: %s"), *PreviewBaseResult.Message));
	}
	TestTrue(TEXT("Preview mismatch base AnimBlueprint apply succeeds"), PreviewBaseResult.IsSuccess());

	TSharedRef<FJsonObject> PreviewUpdateDocument = MakeAnimBlueprintApplyDocument(MismatchedPreviewTarget, TEXT("/Script/Engine.AnimInstance"), SkeletonPath, PreviewMeshPath, false);
	TSharedPtr<FJsonObject> PreviewUpdateBody = PreviewUpdateDocument->GetObjectField(TEXT("Body"));
	PreviewUpdateBody->Values.Empty();
	TSharedRef<FJsonObject> PreviewUpdate = MakeShared<FJsonObject>();
	PreviewUpdate->SetObjectField(TEXT("PreviewSkeletalMesh"), MakeAssetRef(MismatchedPreviewMeshPath));
	PreviewUpdateBody->SetObjectField(TEXT("Preview"), PreviewUpdate);

	FAssetDocumentApplyRequest PreviewUpdateRequest;
	PreviewUpdateRequest.Document = PreviewUpdateDocument;
	PreviewUpdateRequest.bSaveAsset = false;
	const FAssetDocumentResult PreviewUpdateResult = Service.Apply(PreviewUpdateRequest);
	TestFalse(TEXT("Update-only PreviewSkeletalMesh rejects skeleton mismatch against existing TargetSkeleton"), PreviewUpdateResult.IsSuccess());
	TestTrue(
		TEXT("Update-only preview mismatch reports PreviewSkeletalMesh diagnostic"),
		PreviewUpdateResult.Diagnostics.ContainsByPredicate([](const FAssetDocumentDiagnostic& Diagnostic)
		{
			return Diagnostic.Path == TEXT("/Body/Preview/PreviewSkeletalMesh")
				&& Diagnostic.Code == TEXT("MismatchedPreviewSkeletalMeshSkeleton");
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintBlueprintCommonRegionsTest,
	"AssetFactory.AssetDocument.AnimBlueprint.BlueprintCommonRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintBlueprintCommonRegionsTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_Common_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
	const FString InterfacePath = TEXT("/Script/Engine.ActorSoundParameterInterface");

	TSharedRef<FJsonObject> Document = MakeAnimBlueprintApplyDocument(Target);
	TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));
	SetImplementedInterfaces(Document, {MakeImplementedInterface(InterfacePath)});
	Body->SetArrayField(TEXT("Variables"), MakeVariableArray({MakeFloatVariable(TEXT("Speed"), TEXT("123.0"))}));
	Body->GetObjectField(TEXT("ClassDefaults"))->SetBoolField(TEXT("bUseMainInstanceMontageEvaluationData"), true);
	Body->SetArrayField(TEXT("UbergraphPages"), {});

	FAssetDocumentService Service;
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;
	const FAssetDocumentResult ApplyResult = Service.Apply(Request);
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("AnimBlueprint common region apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("Blueprint common regions apply succeeds"), ApplyResult.IsSuccess());

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
	TestNotNull(TEXT("Created AnimBlueprint loads"), AnimBlueprint);
	UClass* InterfaceClass = LoadObject<UClass>(nullptr, *InterfacePath);
	TestNotNull(TEXT("Interface class loads"), InterfaceClass);
	if (AnimBlueprint && InterfaceClass)
	{
		TestTrue(
			TEXT("AnimBlueprint implements authored interface"),
			AnimBlueprint->ImplementedInterfaces.ContainsByPredicate([InterfaceClass](const FBPInterfaceDescription& Interface)
			{
				return Interface.Interface == InterfaceClass;
			}));
		TestTrue(TEXT("AnimBlueprint has authored variable"), HasBlueprintVariable(AnimBlueprint, TEXT("Speed")));

		UObject* GeneratedCDO = AnimBlueprint->GeneratedClass ? AnimBlueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
		FBoolProperty* MontageDataProperty = GeneratedCDO
			? FindFProperty<FBoolProperty>(GeneratedCDO->GetClass(), TEXT("bUseMainInstanceMontageEvaluationData"))
			: nullptr;
		TestNotNull(TEXT("ClassDefaults property resolves on generated CDO"), MontageDataProperty);
		if (GeneratedCDO && MontageDataProperty)
		{
			TestTrue(TEXT("ClassDefaults bool is applied"), MontageDataProperty->GetPropertyValue_InContainer(GeneratedCDO));
		}
	}

	FAssetDocumentCapabilityContext Context;
	Context.Asset = AnimBlueprint;
	Context.AssetClass = UAnimBlueprint::StaticClass();
	const FAnimBlueprintAssetDocumentCapability Capability;
	TSharedRef<FJsonObject> ExtractedBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ExtractResult = Capability.Extract(Context, ExtractedBody);
	TestTrue(TEXT("Extract succeeds"), ExtractResult.bSuccess);
	TestTrue(TEXT("Extract includes one implemented interface"), HasArrayFieldCount(ExtractedBody, TEXT("ImplementedInterfaces"), 1));
	TestTrue(TEXT("Extract includes one variable"), HasArrayFieldCount(ExtractedBody, TEXT("Variables"), 1));
	TestTrue(TEXT("Extract includes empty UbergraphPages"), HasArrayFieldCount(ExtractedBody, TEXT("UbergraphPages"), 0));

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult =
		Capability.Diff(Context, MakeShared<FJsonValueObject>(Body.ToSharedRef()), DiffEntries);
	TestTrue(TEXT("Diff succeeds for Blueprint common regions"), DiffResult.bSuccess);
	TestTrue(TEXT("Diff reports implemented interface semantic path"), HasDiffPath(DiffEntries, FString::Printf(TEXT("/Body/ImplementedInterfaces/%s"), *InterfacePath)));
	TestTrue(TEXT("Diff reports variable semantic path"), HasDiffPath(DiffEntries, TEXT("/Body/Variables/Speed")));

	FAssetDocumentCapabilityContext ValidationContext;
	ValidationContext.AssetClass = UAnimBlueprint::StaticClass();
	TSharedRef<FJsonObject> InvalidInterfaceBody = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> InvalidInterfaces;
	InvalidInterfaces.Add(MakeShared<FJsonValueObject>(MakeImplementedInterface(TEXT("/Script/Engine.Actor")).ToSharedRef()));
	InvalidInterfaceBody->SetArrayField(TEXT("ImplementedInterfaces"), InvalidInterfaces);
	const FAssetDocumentCapabilityResult InvalidInterfaceResult =
		Capability.Validate(ValidationContext, MakeShared<FJsonValueObject>(InvalidInterfaceBody));
	TestFalse(TEXT("ImplementedInterfaces validates interface classes"), InvalidInterfaceResult.bSuccess);
	TestTrue(
		TEXT("Invalid interface reports exact common diagnostic"),
		HasDiagnostic(InvalidInterfaceResult, TEXT("/Body/ImplementedInterfaces/0/Interface/Class"), TEXT("InvalidInterfaceClass")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintSyncGroupsTest,
	"AssetFactory.AssetDocument.AnimBlueprint.SyncGroups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintSyncGroupsTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_SyncGroups_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));

	TSharedRef<FJsonObject> Document = MakeAnimBlueprintApplyDocument(Target);
	SetSyncGroups(Document, {TEXT("Locomotion"), TEXT("UpperBody")});

	FAssetDocumentService Service;
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;
	const FAssetDocumentResult Result = Service.Apply(Request);
	if (!Result.IsSuccess())
	{
		AddError(FString::Printf(TEXT("AnimBlueprint SyncGroups apply failed: %s"), *Result.Message));
	}
	TestTrue(TEXT("SyncGroups apply succeeds"), Result.IsSuccess());

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
	TestNotNull(TEXT("Created AnimBlueprint loads"), AnimBlueprint);
	if (AnimBlueprint)
	{
		TestEqual(TEXT("Two sync groups applied"), AnimBlueprint->Groups.Num(), 2);
		if (AnimBlueprint->Groups.Num() == 2)
		{
			TestEqual(TEXT("First sync group preserves authored order"), AnimBlueprint->Groups[0].Name, FName(TEXT("Locomotion")));
			TestEqual(TEXT("Second sync group preserves authored order"), AnimBlueprint->Groups[1].Name, FName(TEXT("UpperBody")));
		}
	}

	FAssetDocumentCapabilityContext Context;
	Context.Asset = AnimBlueprint;
	Context.AssetClass = UAnimBlueprint::StaticClass();
	const FAnimBlueprintAssetDocumentCapability Capability;
	TSharedRef<FJsonObject> ExtractedBody = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult ExtractResult = Capability.Extract(Context, ExtractedBody);
	TestTrue(TEXT("Extract succeeds"), ExtractResult.bSuccess);
	const TArray<TSharedPtr<FJsonValue>>* ExtractedGroups = nullptr;
	TestTrue(TEXT("Extract includes SyncGroups array"), ExtractedBody->TryGetArrayField(TEXT("SyncGroups"), ExtractedGroups));
	if (ExtractedGroups)
	{
		TestEqual(TEXT("Extracted SyncGroups count"), ExtractedGroups->Num(), 2);
	}

	FAssetDocumentCapabilityContext ValidationContext;
	ValidationContext.AssetClass = UAnimBlueprint::StaticClass();
	const FAssetDocumentCapabilityResult DuplicateResult =
		Capability.Validate(ValidationContext, MakeBodyWithSyncGroups({TEXT("Locomotion"), TEXT("locomotion")}));
	TestFalse(TEXT("Duplicate sync group names reject case-insensitively"), DuplicateResult.bSuccess);
	TestTrue(
		TEXT("Duplicate sync group reports exact index diagnostic"),
		HasDiagnostic(DuplicateResult, TEXT("/Body/SyncGroups/1/Name"), TEXT("DuplicateSyncGroupName")));

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult =
		Capability.Diff(Context, MakeBodyWithSyncGroups({TEXT("Locomotion"), TEXT("AimOffset")}), DiffEntries);
	TestTrue(TEXT("SyncGroups diff succeeds"), DiffResult.bSuccess);
	TestTrue(TEXT("Diff uses stable added identity path"), HasDiffPath(DiffEntries, TEXT("/Body/SyncGroups/AimOffset")));
	TestTrue(TEXT("Diff uses stable removed identity path"), HasDiffPath(DiffEntries, TEXT("/Body/SyncGroups/UpperBody")));
	TestFalse(TEXT("Diff does not use numeric SyncGroups paths"), HasNumericSyncGroupDiffPath(DiffEntries));

	return true;
}

#endif
