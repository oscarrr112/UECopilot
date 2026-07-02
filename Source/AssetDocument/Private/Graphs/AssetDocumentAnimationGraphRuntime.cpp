// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/AssetDocumentAnimationGraphRuntime.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "AssetDocumentPropertyAdapter.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"

namespace
{
constexpr const TCHAR* ManagedNodeNamePrefix = TEXT("ADNode_");

FString JoinPath(const FString& BasePath, const FString& Segment)
{
	const FString EscapedSegment = FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Segment);
	if (BasePath.IsEmpty())
	{
		return FString::Printf(TEXT("/%s"), *EscapedSegment);
	}
	return FString::Printf(TEXT("%s/%s"), *BasePath, *EscapedSegment);
}

FString GraphPath(const FAssetDocumentGraphSpec& GraphSpec, const FAssetDocumentAnimationGraphContext& Context)
{
	if (!Context.GraphPath.IsEmpty())
	{
		return Context.GraphPath;
	}
	return FString::Printf(TEXT("/Graphs/%s"), *FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(GraphSpec.Id));
}

FString NodePath(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentNodeSpec& NodeSpec,
	const FAssetDocumentAnimationGraphContext& Context)
{
	return JoinPath(JoinPath(GraphPath(GraphSpec, Context), TEXT("Nodes")), NodeSpec.Id);
}

FString LinkPath(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentLinkSpec& Link,
	const FAssetDocumentAnimationGraphContext& Context)
{
	return JoinPath(JoinPath(GraphPath(GraphSpec, Context), TEXT("Links")), Link.ToKey());
}

FAssetDocumentCapabilityResult RuntimeFailure(
	const FString& Path,
	const FString& Code,
	const FString& Message)
{
	return FAssetDocumentJsonRegionUtils::Failure(Path, Code, Message);
}

bool HasSpawnerDescriptor(const FAssetDocumentNodeSpec& NodeSpec)
{
	return NodeSpec.Spawner.IsValid() && !NodeSpec.Spawner->Values.IsEmpty();
}

bool SpawnerMatches(
	const TSharedPtr<FJsonObject>& DesiredSpawner,
	const TSharedPtr<FJsonObject>& CandidateSpawner)
{
	if (!DesiredSpawner.IsValid())
	{
		return true;
	}
	if (!CandidateSpawner.IsValid())
	{
		return false;
	}
	return AssetDocumentGraphJson::AreJsonObjectsEqual(DesiredSpawner, CandidateSpawner);
}

FAssetDocumentCapabilityResult PropertyApplyFailure(
	const FString& Path,
	const FAssetDocumentPropertyApplyResult& ApplyResult)
{
	FAssetDocumentCapabilityResult Result;
	Result.bSuccess = false;
	Result.Message = ApplyResult.Message;
	if (ApplyResult.Diagnostics.IsEmpty())
	{
		Result.Diagnostics.Add({Path, TEXT("GraphNodeFieldApplyFailed"), ApplyResult.Message});
		return Result;
	}

	for (const FAssetDocumentDiagnostic& Diagnostic : ApplyResult.Diagnostics)
	{
		FAssetDocumentDiagnostic Mapped;
		Mapped.Path = Diagnostic.Path.IsEmpty() ? Path : JoinPath(Path, Diagnostic.Path);
		Mapped.Code = Diagnostic.Code.IsEmpty() ? TEXT("GraphNodeFieldApplyFailed") : Diagnostic.Code;
		Mapped.Message = Diagnostic.Message;
		Result.Diagnostics.Add(MoveTemp(Mapped));
	}
	return Result;
}

TSharedRef<FJsonObject> MakePositionObject(const UEdGraphNode* Node)
{
	TSharedRef<FJsonObject> Position = MakeShared<FJsonObject>();
	Position->SetNumberField(TEXT("X"), Node ? Node->NodePosX : 0);
	Position->SetNumberField(TEXT("Y"), Node ? Node->NodePosY : 0);
	return Position;
}

TSharedRef<FJsonObject> MakeNodeEvidenceObject(const UEdGraphNode* Node)
{
	TSharedRef<FJsonObject> Evidence = MakeShared<FJsonObject>();
	Evidence->SetStringField(TEXT("NodeGuid"), Node ? Node->NodeGuid.ToString(EGuidFormats::Digits) : FString());
	return Evidence;
}

TSharedRef<FJsonObject> MakeSkippedNodeObject(const UEdGraphNode* Node)
{
	TSharedRef<FJsonObject> Skipped = MakeShared<FJsonObject>();
	Skipped->SetStringField(TEXT("Reason"), TEXT("UnsupportedAnimationGraphNodeIdentity"));
	Skipped->SetStringField(TEXT("Class"), Node && Node->GetClass() ? Node->GetClass()->GetPathName() : FString());
	Skipped->SetStringField(TEXT("NodeTitle"), Node ? Node->GetNodeTitle(ENodeTitleType::ListView).ToString() : FString());
	Skipped->SetStringField(TEXT("NodeGuid"), Node ? Node->NodeGuid.ToString(EGuidFormats::Digits) : FString());
	return Skipped;
}

bool IsPinNameRepresentable(const UEdGraphPin* Pin)
{
	return Pin && !Pin->PinName.IsNone() && !Pin->PinName.ToString().IsEmpty();
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

UEdGraphPin* FindUniquePinByName(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentLinkSpec& Link,
	const FAssetDocumentAnimationGraphContext& Context,
	const FAssetDocumentGraphEndpoint& Endpoint,
	UEdGraphNode* Node,
	EEdGraphPinDirection ExpectedDirection,
	FAssetDocumentCapabilityResult& OutResult)
{
	OutResult = FAssetDocumentCapabilityResult::Success();
	if (!Node)
	{
		OutResult = RuntimeFailure(
			LinkPath(GraphSpec, Link, Context),
			TEXT("UnresolvedGraphLinkEndpoint"),
			FString::Printf(TEXT("Graph link endpoint node '%s' could not be resolved."), *Endpoint.Node));
		return nullptr;
	}

	TArray<UEdGraphPin*> Matches;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->PinName.ToString() == Endpoint.Pin)
		{
			Matches.Add(Pin);
		}
	}
	if (Matches.IsEmpty())
	{
		OutResult = RuntimeFailure(
			LinkPath(GraphSpec, Link, Context),
			TEXT("UnresolvedGraphLinkEndpoint"),
			FString::Printf(TEXT("Graph link endpoint pin '%s.%s' could not be resolved after node reconstruction."), *Endpoint.Node, *Endpoint.Pin));
		return nullptr;
	}
	if (Matches.Num() > 1)
	{
		OutResult = RuntimeFailure(
			LinkPath(GraphSpec, Link, Context),
			TEXT("AmbiguousGraphLinkEndpointPin"),
			FString::Printf(TEXT("Graph link endpoint pin '%s.%s' is ambiguous after node reconstruction."), *Endpoint.Node, *Endpoint.Pin));
		return nullptr;
	}
	if (Matches[0]->Direction != ExpectedDirection)
	{
		OutResult = RuntimeFailure(
			LinkPath(GraphSpec, Link, Context),
			TEXT("InvalidGraphLinkType"),
			FString::Printf(TEXT("Graph link endpoint '%s.%s' has the wrong pin direction."), *Endpoint.Node, *Endpoint.Pin));
		return nullptr;
	}
	return Matches[0];
}

void BreakAllManagedNodeLinks(const TMap<FString, UEdGraphNode*>& NodesById)
{
	for (const TPair<FString, UEdGraphNode*>& Pair : NodesById)
	{
		if (!Pair.Value)
		{
			continue;
		}
		for (UEdGraphPin* Pin : Pair.Value->Pins)
		{
			if (Pin)
			{
				Pin->BreakAllPinLinks();
			}
		}
	}
}

void AddExtractedManagedLinks(
	const TArray<UEdGraphNode*>& GraphNodes,
	const TMap<const UEdGraphNode*, FString>& NodeIds,
	FAssetDocumentGraphSpec& OutGraph)
{
	TSet<FString> LinkKeys;
	for (UEdGraphNode* Node : GraphNodes)
	{
		const FString* FromNodeId = NodeIds.Find(Node);
		if (!FromNodeId)
		{
			continue;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output || !IsPinNameRepresentable(Pin))
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				const UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
				const FString* ToNodeId = NodeIds.Find(LinkedNode);
				if (!ToNodeId || !IsPinNameRepresentable(LinkedPin))
				{
					continue;
				}

				FAssetDocumentLinkSpec Link;
				Link.From.Node = *FromNodeId;
				Link.From.Pin = Pin->PinName.ToString();
				Link.To.Node = *ToNodeId;
				Link.To.Pin = LinkedPin->PinName.ToString();
				if (!LinkKeys.Contains(Link.ToKey()))
				{
					LinkKeys.Add(Link.ToKey());
					OutGraph.Links.Add(MoveTemp(Link));
				}
			}
		}
	}
}

TCHAR ToHexDigit(uint8 Value)
{
	return Value < 10 ? static_cast<TCHAR>(TEXT('0') + Value) : static_cast<TCHAR>(TEXT('A') + (Value - 10));
}

bool FromHexDigit(TCHAR Digit, uint8& OutValue)
{
	if (Digit >= TEXT('0') && Digit <= TEXT('9'))
	{
		OutValue = static_cast<uint8>(Digit - TEXT('0'));
		return true;
	}
	if (Digit >= TEXT('A') && Digit <= TEXT('F'))
	{
		OutValue = static_cast<uint8>(10 + Digit - TEXT('A'));
		return true;
	}
	if (Digit >= TEXT('a') && Digit <= TEXT('f'))
	{
		OutValue = static_cast<uint8>(10 + Digit - TEXT('a'));
		return true;
	}
	return false;
}

FString MakeManagedNodeGuidKey(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentNodeSpec& NodeSpec)
{
	return FString::Printf(
		TEXT("AssetDocument.AnimationGraph.Node|%s|%s|%s"),
		*GraphSpec.Kind,
		*GraphSpec.Id,
		*NodeSpec.Id);
}

FAssetDocumentCapabilityResult AssignManagedIdentity(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentNodeSpec& NodeSpec,
	const FAssetDocumentAnimationGraphContext& Context,
	UEdGraphNode& Node)
{
	FGuid DesiredGuid;
	if (!NodeSpec.NodeGuid.IsEmpty())
	{
		if (!FGuid::Parse(NodeSpec.NodeGuid, DesiredGuid))
		{
			return RuntimeFailure(
				JoinPath(NodePath(GraphSpec, NodeSpec, Context), TEXT("NodeGuid")),
				TEXT("InvalidGraphNodeGuid"),
				TEXT("Node.NodeGuid must be a GUID string."));
		}
	}
	else
	{
		DesiredGuid = FAssetDocumentAnimationGraphRuntime::MakeManagedNodeGuid(GraphSpec, NodeSpec);
	}
	Node.NodeGuid = DesiredGuid;

	FString DesiredName = FAssetDocumentAnimationGraphRuntime::MakeManagedNodeObjectName(NodeSpec.Id);
	if (!DesiredName.IsEmpty() && Node.GetName() != DesiredName)
	{
		if (UObject* ExistingObject = FindObject<UObject>(Node.GetOuter(), *DesiredName))
		{
			if (ExistingObject != &Node)
			{
				DesiredName = MakeUniqueObjectName(Node.GetOuter(), Node.GetClass(), FName(*DesiredName)).ToString();
			}
		}
		if (!Node.Rename(*DesiredName, Node.GetOuter(), REN_DontCreateRedirectors | REN_NonTransactional))
		{
			return RuntimeFailure(
				NodePath(GraphSpec, NodeSpec, Context),
				TEXT("GraphNodeIdentityRenameFailed"),
				FString::Printf(TEXT("Graph node '%s' could not be assigned a stable object identity."), *NodeSpec.Id));
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

}

FAssetDocumentAnimationGraphRuntime::FAssetDocumentAnimationGraphRuntime(
	TSharedPtr<IAssetDocumentAnimationGraphCandidateProvider> InCandidateProvider)
	: CandidateProvider(MoveTemp(InCandidateProvider))
{
}

FAssetDocumentCapabilityResult FAssetDocumentAnimationGraphRuntime::ResolveCandidate(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentNodeSpec& NodeSpec,
	const FAssetDocumentAnimationGraphContext& Context,
	FAssetDocumentAnimationGraphNodeSpawnCandidate& OutCandidate) const
{
	const FString CurrentNodePath = NodePath(GraphSpec, NodeSpec, Context);
	if (!CandidateProvider.IsValid())
	{
		return RuntimeFailure(
			JoinPath(CurrentNodePath, TEXT("Class")),
			TEXT("MissingGraphNodeCandidateProvider"),
			TEXT("Animation graph runtime has no node spawn candidate provider."));
	}

	TArray<FAssetDocumentAnimationGraphNodeSpawnCandidate> Candidates =
		CandidateProvider->FindCandidates(NodeSpec, Context);
	Candidates.RemoveAll(
		[&NodeSpec](const FAssetDocumentAnimationGraphNodeSpawnCandidate& Candidate)
		{
			return !NodeSpec.Class.IsEmpty() && Candidate.ClassPath != NodeSpec.Class;
		});

	if (HasSpawnerDescriptor(NodeSpec))
	{
		Candidates.RemoveAll(
			[&NodeSpec](const FAssetDocumentAnimationGraphNodeSpawnCandidate& Candidate)
			{
				return !SpawnerMatches(NodeSpec.Spawner, Candidate.Spawner);
			});
	}

	if (Candidates.IsEmpty())
	{
		return RuntimeFailure(
			JoinPath(CurrentNodePath, TEXT("Class")),
			TEXT("UnspawnableGraphNodeClass"),
			FString::Printf(TEXT("No spawn candidate exists for graph node '%s'."), *NodeSpec.Id));
	}

	if (!HasSpawnerDescriptor(NodeSpec) && Candidates.Num() > 1)
	{
		return RuntimeFailure(
			JoinPath(CurrentNodePath, TEXT("Spawner")),
			TEXT("AmbiguousGraphNodeSpawner"),
			FString::Printf(TEXT("Graph node '%s' requires Node.Spawner to disambiguate candidates."), *NodeSpec.Id));
	}

	OutCandidate = Candidates[0];
	if (!OutCandidate.bSpawnable)
	{
		return RuntimeFailure(
			JoinPath(CurrentNodePath, TEXT("Class")),
			TEXT("UnspawnableGraphNodeClass"),
			FString::Printf(TEXT("Graph node '%s' is not spawnable in this graph context."), *NodeSpec.Id));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FAssetDocumentAnimationGraphRuntime::ValidateGraph(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentAnimationGraphContext& Context) const
{
	for (const FAssetDocumentNodeSpec& NodeSpec : GraphSpec.Nodes)
	{
		FAssetDocumentAnimationGraphNodeSpawnCandidate Candidate;
		const FAssetDocumentCapabilityResult CandidateResult =
			ResolveCandidate(GraphSpec, NodeSpec, Context, Candidate);
		if (!CandidateResult.bSuccess)
		{
			return CandidateResult;
		}
	}

	for (const FAssetDocumentGraphSpec& Subgraph : GraphSpec.Subgraphs)
	{
		FAssetDocumentAnimationGraphContext SubgraphContext = Context;
		SubgraphContext.GraphPath = JoinPath(JoinPath(GraphPath(GraphSpec, Context), TEXT("Subgraphs")), Subgraph.Id);
		SubgraphContext.GraphKind = Subgraph.Kind;
		const FAssetDocumentCapabilityResult SubgraphResult = ValidateGraph(Subgraph, SubgraphContext);
		if (!SubgraphResult.bSuccess)
		{
			return SubgraphResult;
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FAssetDocumentAnimationGraphRuntime::MaterializeGraphNodes(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentAnimationGraphContext& Context,
	TMap<FString, UEdGraphNode*>& OutNodesById) const
{
	OutNodesById.Reset();
	for (const FAssetDocumentNodeSpec& NodeSpec : GraphSpec.Nodes)
	{
		UEdGraphNode* SpawnedNode = FindManagedNodeById(NodeSpec.Id, Context);
		if (!SpawnedNode)
		{
			FAssetDocumentAnimationGraphNodeSpawnCandidate Candidate;
			FAssetDocumentCapabilityResult CandidateResult = ResolveCandidate(GraphSpec, NodeSpec, Context, Candidate);
			if (!CandidateResult.bSuccess)
			{
				return CandidateResult;
			}

			CandidateResult = CandidateProvider->SpawnNode(GraphSpec, NodeSpec, Context, Candidate, SpawnedNode);
			if (!CandidateResult.bSuccess)
			{
				return CandidateResult;
			}
			if (!SpawnedNode)
			{
				return RuntimeFailure(
					JoinPath(NodePath(GraphSpec, NodeSpec, Context), TEXT("Class")),
					TEXT("GraphNodeSpawnFailed"),
					FString::Printf(TEXT("Graph node '%s' did not produce a UE graph node."), *NodeSpec.Id));
			}
			if (Context.Graph && !Context.Graph->Nodes.Contains(SpawnedNode))
			{
				Context.Graph->AddNode(SpawnedNode, false, false);
			}
		}
		if (!NodeSpec.Class.IsEmpty() && SpawnedNode->GetClass()->GetPathName() != NodeSpec.Class)
		{
			return RuntimeFailure(
				JoinPath(NodePath(GraphSpec, NodeSpec, Context), TEXT("Class")),
				TEXT("GraphNodeSpawnClassMismatch"),
				FString::Printf(
					TEXT("Graph node '%s' spawned '%s' instead of '%s'."),
					*NodeSpec.Id,
					*SpawnedNode->GetClass()->GetPathName(),
					*NodeSpec.Class));
		}

		const FAssetDocumentCapabilityResult IdentityResult =
			AssignManagedIdentity(GraphSpec, NodeSpec, Context, *SpawnedNode);
		if (!IdentityResult.bSuccess)
		{
			return IdentityResult;
		}

		if (NodeSpec.Fields.IsValid() && !NodeSpec.Fields->Values.IsEmpty())
		{
			const FString FieldsPath = JoinPath(NodePath(GraphSpec, NodeSpec, Context), TEXT("Fields"));
			const FAssetDocumentPropertyApplyResult FieldResult =
				FAssetDocumentPropertyAdapter::ApplyProperties(SpawnedNode, NodeSpec.Fields);
			if (!FieldResult.bSuccess)
			{
				return PropertyApplyFailure(FieldsPath, FieldResult);
			}
		}

		SpawnedNode->NodePosX = 0;
		SpawnedNode->NodePosY = 0;
		if (NodeSpec.Position.IsValid())
		{
			double X = 0.0;
			double Y = 0.0;
			if (NodeSpec.Position->TryGetNumberField(TEXT("X"), X))
			{
				SpawnedNode->NodePosX = static_cast<int32>(X);
			}
			if (NodeSpec.Position->TryGetNumberField(TEXT("Y"), Y))
			{
				SpawnedNode->NodePosY = static_cast<int32>(Y);
			}
		}
		SpawnedNode->ReconstructNode();
		OutNodesById.Add(NodeSpec.Id, SpawnedNode);
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FAssetDocumentAnimationGraphRuntime::MaterializeGraphLinks(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentAnimationGraphContext& Context,
	const TMap<FString, UEdGraphNode*>& NodesById) const
{
	BreakAllManagedNodeLinks(NodesById);
	const UEdGraphSchema* Schema = Context.Graph ? Context.Graph->GetSchema() : nullptr;

	for (const FAssetDocumentLinkSpec& Link : GraphSpec.Links)
	{
		UEdGraphNode* const* FromNode = NodesById.Find(Link.From.Node);
		UEdGraphNode* const* ToNode = NodesById.Find(Link.To.Node);

		FAssetDocumentCapabilityResult PinResult;
		UEdGraphPin* FromPin = FindUniquePinByName(
			GraphSpec,
			Link,
			Context,
			Link.From,
			FromNode ? *FromNode : nullptr,
			EGPD_Output,
			PinResult);
		if (!PinResult.bSuccess)
		{
			return PinResult;
		}

		UEdGraphPin* ToPin = FindUniquePinByName(
			GraphSpec,
			Link,
			Context,
			Link.To,
			ToNode ? *ToNode : nullptr,
			EGPD_Input,
			PinResult);
		if (!PinResult.bSuccess)
		{
			return PinResult;
		}

		if (!FromPin || !ToPin)
		{
			return RuntimeFailure(
				LinkPath(GraphSpec, Link, Context),
				TEXT("UnresolvedGraphLinkEndpoint"),
				FString::Printf(TEXT("Graph link endpoint '%s' could not be resolved after node reconstruction."), *Link.ToKey()));
		}

		if (Schema)
		{
			if (!Schema->TryCreateConnection(FromPin, ToPin))
			{
				return RuntimeFailure(
					LinkPath(GraphSpec, Link, Context),
					TEXT("InvalidGraphLinkType"),
					FString::Printf(TEXT("Graph schema rejected link '%s'."), *Link.ToKey()));
			}
		}
		else if (!FromPin->LinkedTo.Contains(ToPin))
		{
			FromPin->MakeLinkTo(ToPin);
		}
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FAssetDocumentAnimationGraphRuntime::ApplyGraphAfterPreflight(
	const FAssetDocumentGraphSpec& GraphSpec,
	FAssetDocumentAnimationGraphContext& Context,
	IAssetDocumentAnimationGraphStructuralHook& Hook) const
{
	const FAssetDocumentCapabilityResult LocateResult = Hook.LocateOrCreateGraph(GraphSpec, Context);
	if (!LocateResult.bSuccess)
	{
		return LocateResult;
	}

	TMap<FString, UEdGraphNode*> NodesById;
	const FAssetDocumentCapabilityResult MaterializeResult = MaterializeGraphNodes(GraphSpec, Context, NodesById);
	if (!MaterializeResult.bSuccess)
	{
		return MaterializeResult;
	}

	const FAssetDocumentCapabilityResult LinkResult = MaterializeGraphLinks(GraphSpec, Context, NodesById);
	if (!LinkResult.bSuccess)
	{
		return LinkResult;
	}

	for (const FAssetDocumentGraphSpec& Subgraph : GraphSpec.Subgraphs)
	{
		FAssetDocumentAnimationGraphContext SubgraphContext = Context;
		SubgraphContext.GraphPath = JoinPath(JoinPath(GraphPath(GraphSpec, Context), TEXT("Subgraphs")), Subgraph.Id);
		SubgraphContext.GraphKind = Subgraph.Kind;
		const FAssetDocumentCapabilityResult SubgraphResult =
			ApplyGraphAfterPreflight(Subgraph, SubgraphContext, Hook);
		if (!SubgraphResult.bSuccess)
		{
			return SubgraphResult;
		}
	}

	return Hook.RepairAfterApply(GraphSpec, Context);
}

FAssetDocumentCapabilityResult FAssetDocumentAnimationGraphRuntime::ApplyGraph(
	const FAssetDocumentGraphSpec& GraphSpec,
	FAssetDocumentAnimationGraphContext& Context,
	IAssetDocumentAnimationGraphStructuralHook& Hook) const
{
	const FAssetDocumentCapabilityResult ValidateResult = ValidateGraph(GraphSpec, Context);
	if (!ValidateResult.bSuccess)
	{
		return ValidateResult;
	}

	return ApplyGraphAfterPreflight(GraphSpec, Context, Hook);
}

FAssetDocumentCapabilityResult FAssetDocumentAnimationGraphRuntime::ExtractGraph(
	const FAssetDocumentAnimationGraphContext& Context,
	FAssetDocumentGraphSpec& OutGraph) const
{
	OutGraph.Id = Context.GraphKind.IsEmpty() ? TEXT("AnimGraph") : Context.GraphKind;
	OutGraph.Kind = Context.GraphKind;
	OutGraph.Nodes.Reset();
	OutGraph.Links.Reset();
	OutGraph.UnderscoreSkipped.Reset();

	if (!Context.Graph)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	TArray<TSharedPtr<FJsonValue>> SkippedNodes;
	TMap<const UEdGraphNode*, FString> NodeIds;
	for (UEdGraphNode* Node : Context.Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		FString NodeId;
		if (!TryParseManagedNodeObjectName(Node->GetFName(), NodeId))
		{
			SkippedNodes.Add(MakeShared<FJsonValueObject>(MakeSkippedNodeObject(Node)));
			continue;
		}

		FAssetDocumentNodeSpec NodeSpec;
		NodeSpec.Id = NodeId;
		NodeSpec.Class = Node->GetClass() ? Node->GetClass()->GetPathName() : FString();
		NodeSpec.Position = MakePositionObject(Node);
		NodeSpec.Evidence = MakeNodeEvidenceObject(Node);
		NodeIds.Add(Node, NodeId);
		OutGraph.Nodes.Add(MoveTemp(NodeSpec));
	}

	AddExtractedManagedLinks(Context.Graph->Nodes, NodeIds, OutGraph);

	if (!SkippedNodes.IsEmpty())
	{
		TSharedRef<FJsonObject> Skipped = MakeShared<FJsonObject>();
		Skipped->SetArrayField(TEXT("Nodes"), MoveTemp(SkippedNodes));
		OutGraph.UnderscoreSkipped = MakeShared<FJsonValueObject>(Skipped);
	}
	return FAssetDocumentCapabilityResult::Success();
}

FGuid FAssetDocumentAnimationGraphRuntime::MakeManagedNodeGuid(
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentNodeSpec& NodeSpec)
{
	return FGuid::NewDeterministicGuid(MakeManagedNodeGuidKey(GraphSpec, NodeSpec));
}

FString FAssetDocumentAnimationGraphRuntime::MakeManagedNodeObjectName(const FString& NodeId)
{
	FString Encoded;
	Encoded.Reserve(FCString::Strlen(ManagedNodeNamePrefix) + NodeId.Len() * 2);
	Encoded += ManagedNodeNamePrefix;
	for (TCHAR Character : NodeId)
	{
		const uint8 Byte = static_cast<uint8>(Character);
		Encoded.AppendChar(ToHexDigit(Byte >> 4));
		Encoded.AppendChar(ToHexDigit(Byte & 0x0F));
	}
	return Encoded;
}

bool FAssetDocumentAnimationGraphRuntime::TryParseManagedNodeObjectName(
	const FName& ObjectName,
	FString& OutNodeId)
{
	const FString Name = ObjectName.ToString();
	if (!Name.StartsWith(ManagedNodeNamePrefix))
	{
		return false;
	}

	const FString EncodedWithOptionalSuffix = Name.Mid(FCString::Strlen(ManagedNodeNamePrefix));
	int32 EncodedLength = 0;
	while (EncodedLength < EncodedWithOptionalSuffix.Len())
	{
		uint8 Ignored = 0;
		if (!FromHexDigit(EncodedWithOptionalSuffix[EncodedLength], Ignored))
		{
			break;
		}
		++EncodedLength;
	}
	if (EncodedLength <= 0 || EncodedLength % 2 != 0)
	{
		return false;
	}
	const FString Encoded = EncodedWithOptionalSuffix.Left(EncodedLength);

	FString Decoded;
	Decoded.Reserve(Encoded.Len() / 2);
	for (int32 Index = 0; Index < Encoded.Len(); Index += 2)
	{
		uint8 High = 0;
		uint8 Low = 0;
		if (!FromHexDigit(Encoded[Index], High) || !FromHexDigit(Encoded[Index + 1], Low))
		{
			return false;
		}
		Decoded.AppendChar(static_cast<TCHAR>((High << 4) | Low));
	}

	OutNodeId = MoveTemp(Decoded);
	return !OutNodeId.IsEmpty();
}
