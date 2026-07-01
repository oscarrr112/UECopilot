// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/AssetDocumentAnimationGraphRuntime.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "AssetDocumentPropertyAdapter.h"

#include "EdGraph/EdGraphNode.h"
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
	const FAssetDocumentAnimationGraphContext& Context) const
{
	for (const FAssetDocumentNodeSpec& NodeSpec : GraphSpec.Nodes)
	{
		FAssetDocumentAnimationGraphNodeSpawnCandidate Candidate;
		FAssetDocumentCapabilityResult CandidateResult = ResolveCandidate(GraphSpec, NodeSpec, Context, Candidate);
		if (!CandidateResult.bSuccess)
		{
			return CandidateResult;
		}

		UEdGraphNode* SpawnedNode = nullptr;
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

	const FAssetDocumentCapabilityResult MaterializeResult = MaterializeGraphNodes(GraphSpec, Context);
	if (!MaterializeResult.bSuccess)
	{
		return MaterializeResult;
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
	TSharedRef<FJsonObject> Skipped = MakeShared<FJsonObject>();
	Skipped->SetStringField(TEXT("Reason"), TEXT("AnimationGraphExtractionNotMaterialized"));
	Skipped->SetStringField(TEXT("Message"), TEXT("Animation graph extraction is not materialized in the runtime shell."));
	OutGraph.UnderscoreSkipped = MakeShared<FJsonValueObject>(Skipped);
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
