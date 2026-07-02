// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentAnimGraphRegionAdapter.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "Animation/AnimBlueprint.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "Graphs/AssetDocumentAnimationGraphNodeActionProvider.h"
#include "Graphs/AssetDocumentAnimationGraphRuntime.h"
#include "Graphs/AssetDocumentGraphDiff.h"
#include "Graphs/AssetDocumentGraphParser.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"

namespace
{
constexpr const TCHAR* AnimGraphPath = TEXT("/Body/AnimGraph");
constexpr const TCHAR* CanonicalGraphId = TEXT("AnimGraph");
constexpr const TCHAR* CanonicalGraphKind = TEXT("AnimGraph");
constexpr const TCHAR* CanonicalGraphPath = TEXT("/Body/AnimGraph/Graphs/AnimGraph");
constexpr const TCHAR* OutputPoseField = TEXT("OutputPose");
constexpr const TCHAR* ResultNodeClassPath = TEXT("/Script/AnimGraph.AnimGraphNode_Root");

struct FAnimGraphOutputPoseSpec
{
	FString Node;
	FString Pin;
};

struct FAnimGraphOutputPoseLink
{
	FString Node;
	FString Pin;
};

struct FPinLinkSnapshotEntry
{
	UEdGraphPin* Pin = nullptr;
	TArray<UEdGraphPin*> LinkedPins;
};

FAssetDocumentCapabilityResult Failure(const FString& Path, const FString& Code, const FString& Message)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FAssetDocumentCapabilityResult FromGraphDiagnostics(const TArray<FAssetDocumentGraphDiagnostic>& Diagnostics)
{
	if (Diagnostics.IsEmpty())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FAssetDocumentCapabilityResult Result;
	Result.bSuccess = false;
	Result.Message = Diagnostics[0].Message;
	for (const FAssetDocumentGraphDiagnostic& GraphDiagnostic : Diagnostics)
	{
		FAssetDocumentDiagnostic Diagnostic;
		Diagnostic.Path = GraphDiagnostic.Path;
		Diagnostic.Code = GraphDiagnostic.Code;
		Diagnostic.Message = GraphDiagnostic.Message;
		Result.Diagnostics.Add(MoveTemp(Diagnostic));
	}
	return Result;
}

FAssetDocumentGraphSpec MakeCanonicalGraphSpec()
{
	FAssetDocumentGraphSpec Graph;
	Graph.Id = CanonicalGraphId;
	Graph.Kind = CanonicalGraphKind;
	return Graph;
}

TSharedRef<FJsonObject> MakeCanonicalRegionObject()
{
	TArray<TSharedPtr<FJsonValue>> Graphs;
	Graphs.Add(MakeShared<FJsonValueObject>(MakeCanonicalGraphSpec().ToJsonObject()));

	TSharedRef<FJsonObject> Region = MakeShared<FJsonObject>();
	Region->SetArrayField(TEXT("Graphs"), MoveTemp(Graphs));
	return Region;
}

UAnimBlueprint* ResolveAnimBlueprint(const FAssetDocumentRegionContext& Context)
{
	return Cast<UAnimBlueprint>(Context.Asset);
}

UAnimationGraph* FindAnimGraph(UAnimBlueprint* AnimBlueprint)
{
	if (!AnimBlueprint)
	{
		return nullptr;
	}

	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		UAnimationGraph* AnimGraph = Cast<UAnimationGraph>(Graph);
		if (AnimGraph && AnimGraph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
		{
			return AnimGraph;
		}
	}

	return Cast<UAnimationGraph>(
		FindObject<UEdGraph>(AnimBlueprint, *UEdGraphSchema_K2::GN_AnimGraph.ToString()));
}

FString JoinPath(const FString& BasePath, const FString& Segment)
{
	const FString EscapedSegment = FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Segment);
	if (BasePath.IsEmpty())
	{
		return FString::Printf(TEXT("/%s"), *EscapedSegment);
	}
	return FString::Printf(TEXT("%s/%s"), *BasePath, *EscapedSegment);
}

FString OutputPosePath()
{
	return JoinPath(JoinPath(CanonicalGraphPath, TEXT("Metadata")), OutputPoseField);
}

bool HasAuthoredNodesOrLinks(const FAssetDocumentGraphSpec& Graph)
{
	return !Graph.Nodes.IsEmpty() || !Graph.Links.IsEmpty();
}

FAssetDocumentCapabilityResult TryParseOutputPose(
	const FAssetDocumentGraphSpec& Graph,
	bool bRequireOutputPose,
	TOptional<FAnimGraphOutputPoseSpec>& OutOutputPose)
{
	OutOutputPose.Reset();

	TSharedPtr<FJsonObject> MetadataObject;
	if (Graph.Metadata.IsValid())
	{
		if (Graph.Metadata->Type != EJson::Object)
		{
			return Failure(
				JoinPath(CanonicalGraphPath, TEXT("Metadata")),
				TEXT("InvalidAnimGraphMetadata"),
				TEXT("Body.AnimGraph root graph Metadata must be an object when present."));
		}
		MetadataObject = Graph.Metadata->AsObject();
	}

	const TSharedPtr<FJsonValue>* OutputPoseValue = MetadataObject.IsValid()
		? MetadataObject->Values.Find(OutputPoseField)
		: nullptr;
	if (!OutputPoseValue)
	{
		if (bRequireOutputPose)
		{
			return Failure(
				CanonicalGraphPath,
				TEXT("MissingAnimGraphOutputPose"),
				TEXT("Body.AnimGraph root graph requires Metadata.OutputPose when authored nodes or links are present."));
		}
		return FAssetDocumentCapabilityResult::Success();
	}

	if (!OutputPoseValue->IsValid() || (*OutputPoseValue)->Type != EJson::Object)
	{
		return Failure(
			OutputPosePath(),
			TEXT("InvalidAnimGraphOutputPose"),
			TEXT("Body.AnimGraph root graph Metadata.OutputPose must be an object."));
	}

	const TSharedPtr<FJsonObject> OutputPoseObject = (*OutputPoseValue)->AsObject();
	FAnimGraphOutputPoseSpec OutputPose;
	if (!OutputPoseObject.IsValid()
		|| !OutputPoseObject->TryGetStringField(TEXT("Node"), OutputPose.Node)
		|| OutputPose.Node.IsEmpty()
		|| !OutputPoseObject->TryGetStringField(TEXT("Pin"), OutputPose.Pin)
		|| OutputPose.Pin.IsEmpty())
	{
		return Failure(
			OutputPosePath(),
			TEXT("InvalidAnimGraphOutputPose"),
			TEXT("Body.AnimGraph root graph Metadata.OutputPose requires non-empty Node and Pin fields."));
	}

	const bool bNodeExists = Graph.Nodes.ContainsByPredicate(
		[&OutputPose](const FAssetDocumentNodeSpec& Node)
		{
			return Node.Id == OutputPose.Node;
		});
	if (!bNodeExists)
	{
		return Failure(
			JoinPath(OutputPosePath(), TEXT("Node")),
			TEXT("UnknownAnimGraphOutputPoseNode"),
			FString::Printf(TEXT("Body.AnimGraph OutputPose node '%s' does not exist in the root graph."), *OutputPose.Node));
	}

	OutOutputPose = OutputPose;
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateOutputPose(const FAssetDocumentGraphSpec& Graph)
{
	TOptional<FAnimGraphOutputPoseSpec> Ignored;
	return TryParseOutputPose(Graph, HasAuthoredNodesOrLinks(Graph), Ignored);
}

UEdGraphNode* FindManagedNodeById(const FString& NodeId, const FAssetDocumentAnimationGraphContext& Context)
{
	if (!Context.Graph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : Context.Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		FString ParsedNodeId;
		if (FAssetDocumentAnimationGraphRuntime::TryParseManagedNodeObjectName(Node->GetFName(), ParsedNodeId)
			&& ParsedNodeId == NodeId)
		{
			return Node;
		}
	}
	return nullptr;
}

bool IsAnimGraphResultNode(const UEdGraphNode* Node)
{
	if (!Node || !Node->GetClass())
	{
		return false;
	}

	if (Node->GetClass()->GetPathName() == ResultNodeClassPath)
	{
		return true;
	}

	UClass* ResultNodeClass = StaticLoadClass(UEdGraphNode::StaticClass(), nullptr, ResultNodeClassPath);
	return ResultNodeClass && Node->IsA(ResultNodeClass);
}

FAssetDocumentCapabilityResult FindResultNode(const FAssetDocumentAnimationGraphContext& Context, UEdGraphNode*& OutNode)
{
	OutNode = nullptr;
	if (!Context.Graph)
	{
		return Failure(CanonicalGraphPath, TEXT("MissingAnimGraph"), TEXT("Body.AnimGraph root graph is missing."));
	}

	TArray<UEdGraphNode*> ResultNodes;
	for (UEdGraphNode* Node : Context.Graph->Nodes)
	{
		if (IsAnimGraphResultNode(Node))
		{
			ResultNodes.Add(Node);
		}
	}

	if (ResultNodes.Num() == 0)
	{
		return Failure(
			CanonicalGraphPath,
			TEXT("MissingAnimGraphResultNode"),
			TEXT("Body.AnimGraph could not find the official AnimGraph result node."));
	}
	if (ResultNodes.Num() > 1)
	{
		return Failure(
			CanonicalGraphPath,
			TEXT("AmbiguousAnimGraphResultNode"),
			TEXT("Body.AnimGraph found multiple official AnimGraph result nodes."));
	}

	OutNode = ResultNodes[0];
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FindUniquePin(
	UEdGraphNode* Node,
	const FString& PinName,
	EEdGraphPinDirection Direction,
	const FString& Path,
	const FString& MissingCode,
	const FString& AmbiguousCode,
	UEdGraphPin*& OutPin)
{
	OutPin = nullptr;
	if (!Node)
	{
		return Failure(Path, MissingCode, TEXT("Graph node is missing while resolving a pose pin."));
	}

	TArray<UEdGraphPin*> Matches;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == Direction && Pin->PinName.ToString() == PinName)
		{
			Matches.Add(Pin);
		}
	}

	if (Matches.Num() == 0)
	{
		return Failure(
			Path,
			MissingCode,
			FString::Printf(TEXT("Graph node pin '%s' could not be resolved."), *PinName));
	}
	if (Matches.Num() > 1)
	{
		return Failure(
			Path,
			AmbiguousCode,
			FString::Printf(TEXT("Graph node pin '%s' is ambiguous."), *PinName));
	}

	OutPin = Matches[0];
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FindResultPoseInputPin(UEdGraphNode* ResultNode, UEdGraphPin*& OutPin)
{
	OutPin = nullptr;
	if (!ResultNode)
	{
		return Failure(CanonicalGraphPath, TEXT("MissingAnimGraphResultNode"), TEXT("Body.AnimGraph result node is missing."));
	}

	FAssetDocumentCapabilityResult ResultPinResult;
	UEdGraphPin* ResultPin = nullptr;
	ResultPinResult = FindUniquePin(
		ResultNode,
		TEXT("Result"),
		EGPD_Input,
		CanonicalGraphPath,
		TEXT("MissingAnimGraphResultPosePin"),
		TEXT("AmbiguousAnimGraphResultPosePin"),
		ResultPin);
	if (ResultPinResult.bSuccess)
	{
		OutPin = ResultPin;
		return FAssetDocumentCapabilityResult::Success();
	}

	TArray<UEdGraphPin*> InputPins;
	for (UEdGraphPin* Pin : ResultNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input)
		{
			InputPins.Add(Pin);
		}
	}

	if (InputPins.Num() == 1)
	{
		OutPin = InputPins[0];
		return FAssetDocumentCapabilityResult::Success();
	}
	return ResultPinResult;
}

bool BreakExistingLinks(UEdGraphPin* Pin, UEdGraphPin* PreservePin)
{
	if (!Pin)
	{
		return false;
	}

	bool bChanged = false;
	TArray<UEdGraphPin*> LinkedPins = Pin->LinkedTo;
	for (UEdGraphPin* LinkedPin : LinkedPins)
	{
		if (LinkedPin && LinkedPin != PreservePin)
		{
			Pin->BreakLinkTo(LinkedPin);
			bChanged = true;
		}
	}
	return bChanged;
}

TArray<FPinLinkSnapshotEntry> SnapshotPinLinks(UEdGraphPin* FirstPin, UEdGraphPin* SecondPin)
{
	TArray<UEdGraphPin*> Pins;
	if (FirstPin)
	{
		Pins.AddUnique(FirstPin);
		for (UEdGraphPin* LinkedPin : FirstPin->LinkedTo)
		{
			Pins.AddUnique(LinkedPin);
		}
	}
	if (SecondPin)
	{
		Pins.AddUnique(SecondPin);
		for (UEdGraphPin* LinkedPin : SecondPin->LinkedTo)
		{
			Pins.AddUnique(LinkedPin);
		}
	}

	TArray<FPinLinkSnapshotEntry> Snapshot;
	for (UEdGraphPin* Pin : Pins)
	{
		if (!Pin)
		{
			continue;
		}
		FPinLinkSnapshotEntry Entry;
		Entry.Pin = Pin;
		Entry.LinkedPins = Pin->LinkedTo;
		Snapshot.Add(MoveTemp(Entry));
	}
	return Snapshot;
}

void RestorePinLinks(const TArray<FPinLinkSnapshotEntry>& Snapshot)
{
	for (const FPinLinkSnapshotEntry& Entry : Snapshot)
	{
		if (!Entry.Pin)
		{
			continue;
		}
		TArray<UEdGraphPin*> LinkedPins = Entry.Pin->LinkedTo;
		for (UEdGraphPin* LinkedPin : LinkedPins)
		{
			if (LinkedPin)
			{
				Entry.Pin->BreakLinkTo(LinkedPin);
			}
		}
	}

	for (const FPinLinkSnapshotEntry& Entry : Snapshot)
	{
		if (!Entry.Pin)
		{
			continue;
		}
		for (UEdGraphPin* LinkedPin : Entry.LinkedPins)
		{
			if (LinkedPin && !Entry.Pin->LinkedTo.Contains(LinkedPin))
			{
				Entry.Pin->MakeLinkTo(LinkedPin);
			}
		}
	}
}

TArray<UEdGraphPin*> GetLinksToPreserveOnOutputPin(UEdGraphPin* OutputPin, UEdGraphPin* ResultPin)
{
	TArray<UEdGraphPin*> PreservedLinks;
	if (!OutputPin)
	{
		return PreservedLinks;
	}
	for (UEdGraphPin* LinkedPin : OutputPin->LinkedTo)
	{
		if (LinkedPin && LinkedPin != ResultPin)
		{
			PreservedLinks.AddUnique(LinkedPin);
		}
	}
	return PreservedLinks;
}

bool AreOutputLinksPreserved(UEdGraphPin* OutputPin, const TArray<UEdGraphPin*>& PreservedLinks)
{
	if (!OutputPin)
	{
		return PreservedLinks.IsEmpty();
	}
	for (UEdGraphPin* PreservedLink : PreservedLinks)
	{
		if (PreservedLink && (!OutputPin->LinkedTo.Contains(PreservedLink) || !PreservedLink->LinkedTo.Contains(OutputPin)))
		{
			return false;
		}
	}
	return true;
}

FAssetDocumentCapabilityResult ConnectOutputPoseToResult(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentAnimationGraphContext& Context,
	bool& bOutChanged)
{
	TOptional<FAnimGraphOutputPoseSpec> OutputPose;
	const FAssetDocumentCapabilityResult ParseResult =
		TryParseOutputPose(GraphSpec, HasAuthoredNodesOrLinks(GraphSpec), OutputPose);
	if (!ParseResult.bSuccess || !OutputPose.IsSet())
	{
		return ParseResult;
	}

	UEdGraphNode* OutputNode = FindManagedNodeById(OutputPose->Node, Context);
	if (!OutputNode)
	{
		return Failure(
			JoinPath(OutputPosePath(), TEXT("Node")),
			TEXT("UnresolvedAnimGraphOutputPoseNode"),
			FString::Printf(TEXT("Body.AnimGraph OutputPose node '%s' was not materialized."), *OutputPose->Node));
	}

	UEdGraphPin* OutputPin = nullptr;
	const FAssetDocumentCapabilityResult OutputPinResult = FindUniquePin(
		OutputNode,
		OutputPose->Pin,
		EGPD_Output,
		JoinPath(OutputPosePath(), TEXT("Pin")),
		TEXT("UnresolvedAnimGraphOutputPosePin"),
		TEXT("AmbiguousAnimGraphOutputPosePin"),
		OutputPin);
	if (!OutputPinResult.bSuccess)
	{
		return OutputPinResult;
	}

	UEdGraphNode* ResultNode = nullptr;
	const FAssetDocumentCapabilityResult ResultNodeResult = FindResultNode(Context, ResultNode);
	if (!ResultNodeResult.bSuccess)
	{
		return ResultNodeResult;
	}

	UEdGraphPin* ResultPin = nullptr;
	const FAssetDocumentCapabilityResult ResultPinResult = FindResultPoseInputPin(ResultNode, ResultPin);
	if (!ResultPinResult.bSuccess)
	{
		return ResultPinResult;
	}

	const bool bAlreadyLinked = OutputPin && ResultPin && OutputPin->LinkedTo.Contains(ResultPin);
	const TArray<FPinLinkSnapshotEntry> LinkSnapshot = SnapshotPinLinks(ResultPin, OutputPin);
	const TArray<UEdGraphPin*> PreservedOutputLinks = GetLinksToPreserveOnOutputPin(OutputPin, ResultPin);
	bool bLinkTopologyChanged = false;
	if (const UEdGraphSchema* Schema = Context.Graph ? Context.Graph->GetSchema() : nullptr)
	{
		const FPinConnectionResponse Response = Schema->CanCreateConnection(OutputPin, ResultPin);
		if (!bAlreadyLinked && Response.Response == CONNECT_RESPONSE_DISALLOW)
		{
			return Failure(
				CanonicalGraphPath,
				TEXT("InvalidAnimGraphOutputPoseLink"),
				FString::Printf(TEXT("AnimGraph result pose rejected OutputPose link: %s"), *Response.Message.ToString()));
		}
		bLinkTopologyChanged |= BreakExistingLinks(ResultPin, OutputPin);
		if (!bAlreadyLinked && !Schema->TryCreateConnection(OutputPin, ResultPin))
		{
			RestorePinLinks(LinkSnapshot);
			return Failure(
				CanonicalGraphPath,
				TEXT("InvalidAnimGraphOutputPoseLink"),
				TEXT("AnimGraph result pose rejected OutputPose link."));
		}
		if (!AreOutputLinksPreserved(OutputPin, PreservedOutputLinks))
		{
			RestorePinLinks(LinkSnapshot);
			return Failure(
				CanonicalGraphPath,
				TEXT("InvalidAnimGraphOutputPoseLink"),
				TEXT("AnimGraph result pose would break existing OutputPose output links."));
		}
		if (!OutputPin->LinkedTo.Contains(ResultPin) || !ResultPin->LinkedTo.Contains(OutputPin))
		{
			RestorePinLinks(LinkSnapshot);
			return Failure(
				CanonicalGraphPath,
				TEXT("InvalidAnimGraphOutputPoseLink"),
				TEXT("AnimGraph result pose did not retain the requested OutputPose link."));
		}
	}
	else if (!bAlreadyLinked && OutputPin && ResultPin)
	{
		bLinkTopologyChanged |= BreakExistingLinks(ResultPin, OutputPin);
		OutputPin->MakeLinkTo(ResultPin);
		if (!AreOutputLinksPreserved(OutputPin, PreservedOutputLinks))
		{
			RestorePinLinks(LinkSnapshot);
			return Failure(
				CanonicalGraphPath,
				TEXT("InvalidAnimGraphOutputPoseLink"),
				TEXT("AnimGraph result pose would break existing OutputPose output links."));
		}
	}
	else
	{
		bLinkTopologyChanged |= BreakExistingLinks(ResultPin, OutputPin);
	}

	if (!bAlreadyLinked || bLinkTopologyChanged)
	{
		bOutChanged = true;
	}
	return FAssetDocumentCapabilityResult::Success();
}

void SetOutputPoseMetadata(FAssetDocumentGraphSpec& Graph, const FString& NodeId, const FString& PinName)
{
	TSharedRef<FJsonObject> OutputPose = MakeShared<FJsonObject>();
	OutputPose->SetStringField(TEXT("Node"), NodeId);
	OutputPose->SetStringField(TEXT("Pin"), PinName);

	TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
	Metadata->SetObjectField(OutputPoseField, OutputPose);
	Graph.Metadata = MakeShared<FJsonValueObject>(Metadata);
}

void AddSkippedOutputPose(FAssetDocumentGraphSpec& Graph, TArray<FAnimGraphOutputPoseLink> Links)
{
	Links.Sort([](const FAnimGraphOutputPoseLink& Left, const FAnimGraphOutputPoseLink& Right)
	{
		const int32 NodeCompare = Left.Node.Compare(Right.Node, ESearchCase::CaseSensitive);
		return NodeCompare == 0
			? Left.Pin.Compare(Right.Pin, ESearchCase::CaseSensitive) < 0
			: NodeCompare < 0;
	});

	TArray<TSharedPtr<FJsonValue>> Entries;
	for (const FAnimGraphOutputPoseLink& Link : Links)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Reason"), TEXT("AmbiguousAnimGraphOutputPoseLinks"));
		Entry->SetStringField(TEXT("Node"), Link.Node);
		Entry->SetStringField(TEXT("Pin"), Link.Pin);
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> SkippedObject;
	if (Graph.UnderscoreSkipped.IsValid() && Graph.UnderscoreSkipped->Type == EJson::Object)
	{
		SkippedObject = Graph.UnderscoreSkipped->AsObject();
	}
	if (!SkippedObject.IsValid())
	{
		SkippedObject = MakeShared<FJsonObject>();
	}
	SkippedObject->SetArrayField(OutputPoseField, MoveTemp(Entries));
	Graph.UnderscoreSkipped = MakeShared<FJsonValueObject>(SkippedObject.ToSharedRef());
	Graph.Metadata.Reset();
}

void ExtractOutputPoseFromResultLink(const FAssetDocumentAnimationGraphContext& Context, FAssetDocumentGraphSpec& Graph)
{
	UEdGraphNode* ResultNode = nullptr;
	if (!FindResultNode(Context, ResultNode).bSuccess)
	{
		return;
	}

	UEdGraphPin* ResultPin = nullptr;
	if (!FindResultPoseInputPin(ResultNode, ResultPin).bSuccess || !ResultPin)
	{
		return;
	}

	TArray<FAnimGraphOutputPoseLink> ManagedOutputLinks;
	for (UEdGraphPin* LinkedPin : ResultPin->LinkedTo)
	{
		const UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
		FString NodeId;
		if (LinkedNode
			&& FAssetDocumentAnimationGraphRuntime::TryParseManagedNodeObjectName(LinkedNode->GetFName(), NodeId)
			&& LinkedPin->Direction == EGPD_Output
			&& !LinkedPin->PinName.IsNone())
		{
			ManagedOutputLinks.Add({NodeId, LinkedPin->PinName.ToString()});
		}
	}

	if (ManagedOutputLinks.Num() == 1)
	{
		SetOutputPoseMetadata(Graph, ManagedOutputLinks[0].Node, ManagedOutputLinks[0].Pin);
	}
	else if (ManagedOutputLinks.Num() > 1)
	{
		AddSkippedOutputPose(Graph, MoveTemp(ManagedOutputLinks));
	}
}

void NormalizeGraphForAnimGraphDiff(FAssetDocumentGraphSpec& Graph)
{
	Graph.Diagnostics.Reset();
	Graph.Skipped.Reset();
	Graph.UnderscoreSkipped.Reset();
	Graph.Evidence.Reset();
	for (FAssetDocumentNodeSpec& Node : Graph.Nodes)
	{
		Node.Evidence.Reset();
	}
	for (FAssetDocumentGraphSpec& Subgraph : Graph.Subgraphs)
	{
		NormalizeGraphForAnimGraphDiff(Subgraph);
	}
}

TArray<FAssetDocumentGraphSpec> NormalizeGraphsForAnimGraphDiff(TArray<FAssetDocumentGraphSpec> Graphs)
{
	for (FAssetDocumentGraphSpec& Graph : Graphs)
	{
		NormalizeGraphForAnimGraphDiff(Graph);
	}
	return Graphs;
}

FAssetDocumentCapabilityResult InitializeRuntimeContextFromAsset(
	const FAssetDocumentRegionContext& RegionContext,
	FAssetDocumentAnimationGraphContext& InOutContext,
	bool bCreateIfMissing,
	bool& bOutChanged)
{
	UAnimBlueprint* AnimBlueprint = ResolveAnimBlueprint(RegionContext);
	if (!AnimBlueprint)
	{
		return Failure(
			AnimGraphPath,
			TEXT("InvalidAnimBlueprintAsset"),
			TEXT("Body.AnimGraph requires a UAnimBlueprint asset."));
	}

	InOutContext.Asset = AnimBlueprint;
	InOutContext.Blueprint = AnimBlueprint;

	UAnimationGraph* AnimGraph = FindAnimGraph(AnimBlueprint);
	if (!AnimGraph && bCreateIfMissing)
	{
		if (AnimBlueprint->BlueprintType == BPTYPE_Interface || UAnimBlueprint::FindRootAnimBlueprint(AnimBlueprint) != nullptr)
		{
			return Failure(
				AnimGraphPath,
				TEXT("AnimGraphNotWritable"),
				TEXT("This Animation Blueprint cannot own a writable root AnimGraph."));
		}

		UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
			AnimBlueprint,
			UEdGraphSchema_K2::GN_AnimGraph,
			UAnimationGraph::StaticClass(),
			UAnimationGraphSchema::StaticClass());
		FBlueprintEditorUtils::AddDomainSpecificGraph(AnimBlueprint, NewGraph);
		AnimBlueprint->LastEditedDocuments.Add(NewGraph);
		NewGraph->bAllowDeletion = false;
		if (const UEdGraphSchema* Schema = NewGraph->GetSchema())
		{
			Schema->CreateDefaultNodesForGraph(*NewGraph);
		}
		AnimGraph = Cast<UAnimationGraph>(NewGraph);
		bOutChanged = true;
	}

	InOutContext.Graph = AnimGraph;
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseAnimGraphRegion(
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<FAssetDocumentGraphSpec>& OutGraphs)
{
	if (!DesiredValue.IsValid() || DesiredValue->Type != EJson::Object)
	{
		return Failure(
			AnimGraphPath,
			TEXT("InvalidAnimGraphRegionType"),
			TEXT("Body.AnimGraph must be an object with a Graphs array."));
	}

	const TSharedPtr<FJsonObject> RegionObject = DesiredValue->AsObject();
	if (!RegionObject.IsValid())
	{
		return Failure(
			AnimGraphPath,
			TEXT("InvalidAnimGraphRegionType"),
			TEXT("Body.AnimGraph must be an object with a Graphs array."));
	}

	FAssetDocumentGraphParseOptions Options;
	Options.Path = AnimGraphPath;
	const FAssetDocumentGraphParseResult ParseResult =
		FAssetDocumentGraphParser::ParseGraphRegion(RegionObject.ToSharedRef(), Options);
	if (!ParseResult.IsValid())
	{
		return FromGraphDiagnostics(ParseResult.Diagnostics);
	}

	if (ParseResult.Graphs.Num() != 1)
	{
		return Failure(
			AnimGraphPath,
			TEXT("InvalidAnimGraphCount"),
			TEXT("Body.AnimGraph requires exactly one root AnimGraph graph."));
	}

	const FAssetDocumentGraphSpec& RootGraph = ParseResult.Graphs[0];
	if (RootGraph.Id != CanonicalGraphId || RootGraph.Kind != CanonicalGraphKind)
	{
		return Failure(
			CanonicalGraphPath,
			TEXT("InvalidAnimGraphRoot"),
			TEXT("Body.AnimGraph root graph must use Id='AnimGraph' and Kind='AnimGraph'."));
	}

	OutGraphs = ParseResult.Graphs;
	return FAssetDocumentCapabilityResult::Success();
}

class FEmptyAnimGraphCandidateProvider final : public IAssetDocumentAnimationGraphCandidateProvider
{
public:
	virtual TArray<FAssetDocumentAnimationGraphNodeSpawnCandidate> FindCandidates(
		const FAssetDocumentNodeSpec& NodeSpec,
		const FAssetDocumentAnimationGraphContext& Context) const override
	{
		return {};
	}

	virtual FAssetDocumentCapabilityResult SpawnNode(
		const FAssetDocumentGraphSpec& GraphSpec,
		const FAssetDocumentNodeSpec& NodeSpec,
		const FAssetDocumentAnimationGraphContext& Context,
		const FAssetDocumentAnimationGraphNodeSpawnCandidate& Candidate,
		UEdGraphNode*& OutNode) const override
	{
		OutNode = nullptr;
		return FAssetDocumentCapabilityResult::Success();
	}
};

class FAnimGraphStructuralHook final : public IAssetDocumentAnimationGraphStructuralHook
{
public:
	FAnimGraphStructuralHook(const FAssetDocumentRegionContext& InRegionContext, bool& bInOutChanged)
		: RegionContext(InRegionContext)
		, bOutChanged(bInOutChanged)
	{
	}

	virtual FAssetDocumentCapabilityResult LocateOrCreateGraph(
		const FAssetDocumentGraphSpec& GraphSpec,
		FAssetDocumentAnimationGraphContext& InOutContext) override
	{
		InOutContext.GraphKind = GraphSpec.Kind;
		InOutContext.GraphPath = CanonicalGraphPath;
		return InitializeRuntimeContextFromAsset(RegionContext, InOutContext, true, bOutChanged);
	}

	virtual FAssetDocumentCapabilityResult RepairAfterApply(
		const FAssetDocumentGraphSpec& GraphSpec,
		const FAssetDocumentAnimationGraphContext& Context) override
	{
		return ConnectOutputPoseToResult(GraphSpec, Context, bOutChanged);
	}

private:
	const FAssetDocumentRegionContext& RegionContext;
	bool& bOutChanged;
};

FAssetDocumentCapabilityResult ValidateAnimGraphValue(
	const TSharedPtr<FJsonValue>& DesiredValue,
	const FAssetDocumentRegionContext* RegionContext = nullptr)
{
	TArray<FAssetDocumentGraphSpec> Graphs;
	const FAssetDocumentCapabilityResult ParseResult = ParseAnimGraphRegion(DesiredValue, Graphs);
	if (!ParseResult.bSuccess)
	{
		return ParseResult;
	}
	const FAssetDocumentCapabilityResult OutputPoseResult = ValidateOutputPose(Graphs[0]);
	if (!OutputPoseResult.bSuccess)
	{
		return OutputPoseResult;
	}

	FAssetDocumentAnimationGraphContext RuntimeContext;
	bool bChanged = false;
	if (RegionContext && RegionContext->Asset)
	{
		const FAssetDocumentCapabilityResult ContextResult =
			InitializeRuntimeContextFromAsset(*RegionContext, RuntimeContext, false, bChanged);
		if (!ContextResult.bSuccess)
		{
			return ContextResult;
		}
	}
	else
	{
		RuntimeContext.Asset = RegionContext ? RegionContext->Asset : nullptr;
	}
	RuntimeContext.GraphPath = CanonicalGraphPath;
	RuntimeContext.GraphKind = CanonicalGraphKind;
	TSharedPtr<IAssetDocumentAnimationGraphCandidateProvider> Provider;
	if (RegionContext && RegionContext->Asset)
	{
		Provider = MakeShared<FAssetDocumentAnimationGraphNodeActionProvider>();
	}
	else
	{
		Provider = MakeShared<FEmptyAnimGraphCandidateProvider>();
	}
	const FAssetDocumentAnimationGraphRuntime Runtime(Provider);
	return Runtime.ValidateGraph(Graphs[0], RuntimeContext);
}

TSharedPtr<FJsonObject> GraphDiffEntryToBodyDiffEntry(const FAssetDocumentGraphDiffEntry& GraphEntry)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("path"), GraphEntry.Path);
	Entry->SetStringField(TEXT("status"), GraphEntry.Status);
	if (!GraphEntry.Message.IsEmpty())
	{
		Entry->SetStringField(TEXT("message"), GraphEntry.Message);
	}
	Entry->SetField(
		TEXT("current"),
		GraphEntry.Current.IsValid()
			? AssetDocumentGraphJson::CloneJsonValue(GraphEntry.Current)
			: MakeShared<FJsonValueNull>());
	Entry->SetField(
		TEXT("desired"),
		GraphEntry.Desired.IsValid()
			? AssetDocumentGraphJson::CloneJsonValue(GraphEntry.Desired)
			: MakeShared<FJsonValueNull>());
	return Entry;
}
}

FAssetDocumentAnimGraphRegionAdapter::FAssetDocumentAnimGraphRegionAdapter(FName InAdapterName)
	: AdapterName(InAdapterName)
{
}

FName FAssetDocumentAnimGraphRegionAdapter::GetName() const
{
	return AdapterName;
}

bool FAssetDocumentAnimGraphRegionAdapter::SupportsRegion(const FAssetDocumentRegionContext& Context) const
{
	return Context.RegionId == TEXT("Body.AnimGraph");
}

TSharedRef<FJsonObject> FAssetDocumentAnimGraphRegionAdapter::GetSchemaHint(const FAssetDocumentRegionContext&) const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Kind"), TEXT("AnimGraphRecursiveGraphRegion"));
	Schema->SetStringField(TEXT("Shape"), TEXT("{Graphs:[{Id:'AnimGraph', Kind:'AnimGraph', Metadata:{OutputPose:{Node,Pin}}, Nodes:[], Links:[], Subgraphs:[]}]}"));
	return Schema;
}

FAssetDocumentCapabilityResult FAssetDocumentAnimGraphRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	return ValidateAnimGraphValue(DesiredValue, &Context);
}

FAssetDocumentCapabilityResult FAssetDocumentAnimGraphRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	bOutChanged = false;

	TArray<FAssetDocumentGraphSpec> Graphs;
	const FAssetDocumentCapabilityResult ParseResult = ParseAnimGraphRegion(DesiredValue, Graphs);
	if (!ParseResult.bSuccess)
	{
		return ParseResult;
	}
	const FAssetDocumentCapabilityResult OutputPoseResult = ValidateOutputPose(Graphs[0]);
	if (!OutputPoseResult.bSuccess)
	{
		return OutputPoseResult;
	}

	FAssetDocumentAnimationGraphRuntime Runtime(MakeShared<FAssetDocumentAnimationGraphNodeActionProvider>());
	FAssetDocumentAnimationGraphContext RuntimeContext;
	RuntimeContext.Asset = Context.Asset;
	RuntimeContext.GraphKind = CanonicalGraphKind;
	RuntimeContext.GraphPath = CanonicalGraphPath;
	FAnimGraphStructuralHook Hook(Context, bOutChanged);
	return Runtime.ApplyGraph(Graphs[0], RuntimeContext, Hook);
}

FAssetDocumentCapabilityResult FAssetDocumentAnimGraphRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	FAssetDocumentAnimationGraphContext RuntimeContext;
	bool bChanged = false;
	const FAssetDocumentCapabilityResult ContextResult =
		InitializeRuntimeContextFromAsset(Context, RuntimeContext, false, bChanged);
	if (!ContextResult.bSuccess)
	{
		return ContextResult;
	}
	RuntimeContext.GraphPath = CanonicalGraphPath;
	RuntimeContext.GraphKind = CanonicalGraphKind;

	FAssetDocumentGraphSpec CurrentGraph;
	const FAssetDocumentAnimationGraphRuntime Runtime;
	const FAssetDocumentCapabilityResult ExtractResult = Runtime.ExtractGraph(RuntimeContext, CurrentGraph);
	if (!ExtractResult.bSuccess)
	{
		return ExtractResult;
	}
	ExtractOutputPoseFromResultLink(RuntimeContext, CurrentGraph);

	OutCurrentValue = MakeShared<FJsonValueObject>(
		FAssetDocumentGraphParser::WriteCanonicalGraphRegion({CurrentGraph}));
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FAssetDocumentAnimGraphRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	TArray<FAssetDocumentGraphSpec> DesiredGraphs;
	const FAssetDocumentCapabilityResult ParseResult = ParseAnimGraphRegion(DesiredValue, DesiredGraphs);
	if (!ParseResult.bSuccess)
	{
		return ParseResult;
	}

	const FAssetDocumentCapabilityResult ValidateResult = ValidateAnimGraphValue(DesiredValue, &Context);
	if (!ValidateResult.bSuccess)
	{
		return ValidateResult;
	}

	TArray<FAssetDocumentGraphSpec> CurrentGraphs;
	TSharedPtr<FJsonValue> CurrentValue;
	const FAssetDocumentCapabilityResult ExtractResult = ExtractRegion(Context, CurrentValue);
	if (!ExtractResult.bSuccess)
	{
		return ExtractResult;
	}
	if (!CurrentValue.IsValid() || CurrentValue->Type != EJson::Object)
	{
		return Failure(
			AnimGraphPath,
			TEXT("InvalidCurrentAnimGraphRegion"),
			TEXT("Current Body.AnimGraph extraction did not produce an object."));
	}
	const FAssetDocumentCapabilityResult CurrentParseResult = ParseAnimGraphRegion(CurrentValue, CurrentGraphs);
	if (!CurrentParseResult.bSuccess)
	{
		return CurrentParseResult;
	}

	const TArray<FAssetDocumentGraphDiffEntry> GraphEntries =
		FAssetDocumentGraphDiff::CompareGraphRegion(
			NormalizeGraphsForAnimGraphDiff(DesiredGraphs),
			NormalizeGraphsForAnimGraphDiff(CurrentGraphs),
			TEXT("/Body/AnimGraph"));

	for (const FAssetDocumentGraphDiffEntry& GraphEntry : GraphEntries)
	{
		OutDiffEntries.Add(MakeShared<FJsonValueObject>(GraphDiffEntryToBodyDiffEntry(GraphEntry)));
	}

	return FAssetDocumentCapabilityResult::Success();
}
