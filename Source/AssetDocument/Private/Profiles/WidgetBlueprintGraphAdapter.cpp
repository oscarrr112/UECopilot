// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/WidgetBlueprintGraphAdapter.h"

#include "Graphs/AssetDocumentGraphDefinitionResolver.h"
#include "Graphs/AssetDocumentGraphDiff.h"
#include "Graphs/AssetDocumentGraphParser.h"
#include "Graphs/AssetDocumentNodeAdapter.h"
#include "Graphs/K2GraphAdapter.h"

#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "WidgetBlueprint.h"

namespace
{
struct FWidgetBlueprintGraphRegion
{
	FString Name;
	FString Path;
	EAssetDocumentK2GraphRegion K2Region = EAssetDocumentK2GraphRegion::UbergraphPages;
};

const TArray<FWidgetBlueprintGraphRegion>& WidgetBlueprintGraphRegions()
{
	static const TArray<FWidgetBlueprintGraphRegion> WidgetBlueprintGraphRegionDefinitions = {
		{TEXT("UbergraphPages"), TEXT("/Body/UbergraphPages"), EAssetDocumentK2GraphRegion::UbergraphPages},
		{TEXT("FunctionGraphs"), TEXT("/Body/FunctionGraphs"), EAssetDocumentK2GraphRegion::FunctionGraphs},
		{TEXT("MacroGraphs"), TEXT("/Body/MacroGraphs"), EAssetDocumentK2GraphRegion::MacroGraphs},
	};
	return WidgetBlueprintGraphRegionDefinitions;
}

bool WidgetBlueprintGraphIsEmptyFunctionOrMacroNoop(const FWidgetBlueprintGraphRegion& Region, const TArray<FAssetDocumentGraphSpec>& DesiredGraphs)
{
	return DesiredGraphs.IsEmpty()
		&& (Region.K2Region == EAssetDocumentK2GraphRegion::FunctionGraphs
			|| Region.K2Region == EAssetDocumentK2GraphRegion::MacroGraphs);
}

FAssetDocumentCapabilityResult WidgetBlueprintGraphFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FAssetDocumentCapabilityResult WidgetBlueprintGraphInvalidRegionTypeFailure(const FWidgetBlueprintGraphRegion& Region)
{
	return WidgetBlueprintGraphFailure(
		FString::Printf(TEXT("Body.%s must be an array when authored"), *Region.Name),
		Region.Path,
		TEXT("InvalidGraphRegionType"));
}

FAssetDocumentCapabilityResult WidgetBlueprintGraphDiagnosticsFailure(const TArray<FAssetDocumentGraphDiagnostic>& Diagnostics, const FString& FallbackPath)
{
	if (Diagnostics.IsEmpty())
	{
		return WidgetBlueprintGraphFailure(TEXT("Graph region validation failed"), FallbackPath, TEXT("InvalidGraphRegion"));
	}

	FAssetDocumentCapabilityResult Result = FAssetDocumentCapabilityResult::Failure(
		Diagnostics[0].Message,
		Diagnostics[0].Path,
		Diagnostics[0].Code);
	Result.Diagnostics.Reset();
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

TSharedPtr<FJsonObject> WidgetBlueprintGraphCloneJsonObject(const TSharedPtr<FJsonObject>& Object)
{
	return AssetDocumentGraphJson::CloneJsonObject(Object);
}

FString WidgetBlueprintGraphNodePath(const FWidgetBlueprintGraphRegion& Region, int32 GraphIndex, int32 NodeIndex)
{
	return FString::Printf(TEXT("%s/%d/Nodes/%d"), *Region.Path, GraphIndex, NodeIndex);
}

UClass* WidgetBlueprintGraphResolveClass(const FString& ClassPath)
{
	return ClassPath.IsEmpty() ? nullptr : StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath);
}

FAssetDocumentUnsupportedNodeDiagnostic WidgetBlueprintGraphMakeUnsupportedNodeDiagnostic(
	const FAssetDocumentNodeSpec& Node,
	const FString& Path,
	const FString& Code,
	const FString& Reason,
	const FString& SuggestedAction)
{
	FAssetDocumentUnsupportedNodeDiagnostic Diagnostic;
	Diagnostic.Code = Code;
	Diagnostic.Path = Path;
	Diagnostic.Class = Node.Class;
	Diagnostic.Capability = Node.Capability;
	Diagnostic.Member = WidgetBlueprintGraphCloneJsonObject(Node.Member);
	Diagnostic.Reason = Reason;
	Diagnostic.SuggestedAction = SuggestedAction;
	return Diagnostic;
}

FAssetDocumentCapabilityResult WidgetBlueprintGraphUnsupportedGraphFailure(const FString& FallbackPath, const TArray<FAssetDocumentUnsupportedNodeDiagnostic>& UnsupportedDiagnostics)
{
	if (UnsupportedDiagnostics.IsEmpty())
	{
		return WidgetBlueprintGraphFailure(TEXT("Graph region validation failed"), FallbackPath, TEXT("InvalidGraphRegion"));
	}

	const FAssetDocumentUnsupportedNodeDiagnostic& FirstUnsupported = UnsupportedDiagnostics[0];
	FAssetDocumentCapabilityResult Result = FAssetDocumentCapabilityResult::Failure(
		FString::Printf(TEXT("%s at %s: %s"), *FirstUnsupported.Code, *FirstUnsupported.Path, *FirstUnsupported.Reason),
		FirstUnsupported.Path,
		FirstUnsupported.Code);
	Result.Diagnostics.Reset();

	TArray<TSharedPtr<FJsonValue>> UnsupportedPayload;
	for (const FAssetDocumentUnsupportedNodeDiagnostic& Unsupported : UnsupportedDiagnostics)
	{
		FAssetDocumentDiagnostic Diagnostic;
		Diagnostic.Path = Unsupported.Path;
		Diagnostic.Code = Unsupported.Code;
		Diagnostic.Message = FString::Printf(TEXT("%s at %s: %s"), *Unsupported.Code, *Unsupported.Path, *Unsupported.Reason);
		Result.Diagnostics.Add(MoveTemp(Diagnostic));
		UnsupportedPayload.Add(MakeShared<FJsonValueObject>(Unsupported.ToJsonObject()));
	}
	Result.Payload = MakeShared<FJsonObject>();
	Result.Payload->SetArrayField(TEXT("UnsupportedGraphDiagnostics"), MoveTemp(UnsupportedPayload));
	return Result;
}

FAssetDocumentCapabilityResult WidgetBlueprintGraphParseAndResolveRegion(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& BodyObject,
	const FWidgetBlueprintGraphRegion& Region,
	TArray<FAssetDocumentGraphSpec>& OutGraphs)
{
	OutGraphs.Reset();
	const TSharedPtr<FJsonValue>* RegionValue = BodyObject->Values.Find(Region.Name);
	if (!RegionValue || !RegionValue->IsValid() || (*RegionValue)->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if ((*RegionValue)->Type != EJson::Array)
	{
		return WidgetBlueprintGraphInvalidRegionTypeFailure(Region);
	}

	FAssetDocumentGraphParseOptions ParseOptions;
	ParseOptions.Path = Region.Path;
	const FAssetDocumentGraphParseResult ParseResult =
		FAssetDocumentGraphParser::ParseGraphArray((*RegionValue)->AsArray(), ParseOptions);
	if (!ParseResult.IsValid())
	{
		return WidgetBlueprintGraphDiagnosticsFailure(ParseResult.Diagnostics, Region.Path);
	}

	FAssetDocumentGraphDefinitionResolveOptions ResolveOptions;
	ResolveOptions.GraphsPath = Region.Path;
	const TSharedPtr<FJsonObject> Definitions = Context.Definitions ? *Context.Definitions : nullptr;
	FAssetDocumentGraphDefinitionResolveResult ResolveResult =
		FAssetDocumentGraphDefinitionResolver::ResolveGraphArray(ParseResult.Graphs, Definitions, ResolveOptions);
	if (!ResolveResult.IsValid())
	{
		return WidgetBlueprintGraphDiagnosticsFailure(ResolveResult.Diagnostics, Region.Path);
	}

	OutGraphs = MoveTemp(ResolveResult.Graphs);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult WidgetBlueprintGraphValidateSupportedNodeClasses(
	const FWidgetBlueprintGraphRegion& Region,
	const TArray<FAssetDocumentGraphSpec>& Graphs)
{
	const FAssetDocumentNodeAdapterRegistry CurrentTierRegistry = FAssetDocumentK2GraphAdapter::CreateTier1NodeAdapterRegistry();
	TArray<FAssetDocumentUnsupportedNodeDiagnostic> UnsupportedDiagnostics;
	for (int32 GraphIndex = 0; GraphIndex < Graphs.Num(); ++GraphIndex)
	{
		const FAssetDocumentGraphSpec& Graph = Graphs[GraphIndex];
		for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
		{
			const FAssetDocumentNodeSpec& Node = Graph.Nodes[NodeIndex];
			const FString Path = WidgetBlueprintGraphNodePath(Region, GraphIndex, NodeIndex);
			UClass* NodeClass = WidgetBlueprintGraphResolveClass(Node.Class);
			if (!NodeClass)
			{
				UnsupportedDiagnostics.Add(WidgetBlueprintGraphMakeUnsupportedNodeDiagnostic(
					Node,
					Path,
					TEXT("UnresolvedGraphNodeClass"),
					FString::Printf(TEXT("graph node class '%s' could not be loaded"), *Node.Class),
					TEXT("fix the node Class path or remove the node from the managed graph")));
				continue;
			}

			if (CurrentTierRegistry.FindAdapter(NodeClass).IsValid())
			{
				continue;
			}

			UnsupportedDiagnostics.Add(WidgetBlueprintGraphMakeUnsupportedNodeDiagnostic(
				Node,
				Path,
				TEXT("UnsupportedGraphNodeClass"),
				TEXT("node class has no registered AssetDocument adapter in the current tier"),
				TEXT("add a thin node adapter for this class or remove the node from the managed graph")));
		}
	}

	return UnsupportedDiagnostics.IsEmpty()
		? FAssetDocumentCapabilityResult::Success()
		: WidgetBlueprintGraphUnsupportedGraphFailure(Region.Path, UnsupportedDiagnostics);
}

void WidgetBlueprintGraphMergeSkippedGraphEvidence(TSharedRef<FJsonObject>& OutBodyJson, const TArray<TSharedPtr<FJsonValue>>& SkippedNodes)
{
	if (SkippedNodes.IsEmpty())
	{
		return;
	}

	TSharedPtr<FJsonObject> Skipped;
	const TSharedPtr<FJsonObject>* ExistingSkipped = nullptr;
	if (OutBodyJson->TryGetObjectField(TEXT("_Skipped"), ExistingSkipped) && ExistingSkipped && ExistingSkipped->IsValid())
	{
		Skipped = *ExistingSkipped;
	}
	else
	{
		Skipped = MakeShared<FJsonObject>();
		OutBodyJson->SetObjectField(TEXT("_Skipped"), Skipped);
	}

	TSharedPtr<FJsonObject> Graphs;
	const TSharedPtr<FJsonObject>* ExistingGraphs = nullptr;
	if (Skipped->TryGetObjectField(TEXT("Graphs"), ExistingGraphs) && ExistingGraphs && ExistingGraphs->IsValid())
	{
		Graphs = *ExistingGraphs;
	}
	else
	{
		Graphs = MakeShared<FJsonObject>();
		Skipped->SetObjectField(TEXT("Graphs"), Graphs);
	}

	TArray<TSharedPtr<FJsonValue>> Nodes = SkippedNodes;
	const TArray<TSharedPtr<FJsonValue>>* ExistingNodes = nullptr;
	if (Graphs->TryGetArrayField(TEXT("Nodes"), ExistingNodes) && ExistingNodes)
	{
		Nodes.Append(*ExistingNodes);
	}
	Graphs->SetStringField(TEXT("Reason"), TEXT("UnsupportedGraphNodeClass"));
	Graphs->SetNumberField(TEXT("Count"), Nodes.Num());
	Graphs->SetArrayField(TEXT("Nodes"), MoveTemp(Nodes));
}

void WidgetBlueprintGraphNormalizeMemberIdentityForDiff(FAssetDocumentNodeSpec& Node, const UBlueprint*)
{
	if (!Node.Member.IsValid())
	{
		return;
	}

	Node.Member->RemoveField(TEXT("Guid"));
	Node.Member->RemoveField(TEXT("SelfContext"));
}

void WidgetBlueprintGraphNormalizeIdentityForDiff(TArray<FAssetDocumentGraphSpec>& Graphs, const UBlueprint* Blueprint)
{
	for (FAssetDocumentGraphSpec& Graph : Graphs)
	{
		Graph.GraphGuid.Reset();
		if (Graph.Schema == TEXT("/Script/UMGEditor.WidgetGraphSchema"))
		{
			Graph.Schema = TEXT("/Script/BlueprintGraph.EdGraphSchema_K2");
		}
		for (FAssetDocumentNodeSpec& Node : Graph.Nodes)
		{
			Node.NodeGuid.Reset();
			Node.Capability.Reset();
			WidgetBlueprintGraphNormalizeMemberIdentityForDiff(Node, Blueprint);
		}
	}
}

FString WidgetBlueprintGraphRewriteDiffPath(const FString& Path, const FWidgetBlueprintGraphRegion& Region)
{
	const FString UBlueprintPrefix = TEXT("/Body/UbergraphPages");
	if (Region.Path == UBlueprintPrefix || !Path.StartsWith(UBlueprintPrefix))
	{
		return Path;
	}
	return Region.Path + Path.RightChop(FCString::Strlen(*UBlueprintPrefix));
}

TSharedRef<FJsonObject> WidgetBlueprintGraphMakeCapabilityDiffEntry(
	const FString& Path,
	const FString& Status,
	const TSharedPtr<FJsonValue>& Current,
	const TSharedPtr<FJsonValue>& Desired,
	const FString& Change = FString(),
	const FString& Message = FString(),
	const FString& Code = FString())
{
	TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("path"), Path);
	Entry->SetStringField(TEXT("status"), Status);
	if (!Change.IsEmpty())
	{
		Entry->SetStringField(TEXT("change"), Change);
	}
	if (!Message.IsEmpty())
	{
		Entry->SetStringField(TEXT("message"), Message);
	}
	if (!Code.IsEmpty())
	{
		Entry->SetStringField(TEXT("code"), Code);
	}
	Entry->SetField(TEXT("current"), Current.IsValid() ? AssetDocumentGraphJson::CloneJsonValue(Current) : MakeShared<FJsonValueNull>());
	Entry->SetField(TEXT("desired"), Desired.IsValid() ? AssetDocumentGraphJson::CloneJsonValue(Desired) : MakeShared<FJsonValueNull>());
	return Entry;
}

TSharedRef<FJsonObject> WidgetBlueprintGraphMakeCapabilityDiffEntry(const FWidgetBlueprintGraphRegion& Region, const FAssetDocumentGraphDiffEntry& GraphEntry)
{
	const bool bUnsupported = GraphEntry.Status == TEXT("unsupported");
	const bool bUnchanged = GraphEntry.Status == TEXT("unchanged");
	const FString PublicStatus = bUnsupported ? FString(TEXT("skipped")) : (bUnchanged ? FString(TEXT("unchanged")) : FString(TEXT("changed")));
	const FString Change = bUnchanged ? FString() : GraphEntry.Status;
	return WidgetBlueprintGraphMakeCapabilityDiffEntry(
		WidgetBlueprintGraphRewriteDiffPath(GraphEntry.Path, Region),
		PublicStatus,
		GraphEntry.Current,
		GraphEntry.Desired,
		Change,
		GraphEntry.Message,
		bUnsupported ? FString(TEXT("UnsupportedGraphDiff")) : FString());
}

FString WidgetBlueprintGraphPathFromSkippedNode(const FWidgetBlueprintGraphRegion& Region, const TSharedPtr<FJsonObject>& SkippedNode)
{
	FString GraphName;
	if (SkippedNode.IsValid() && SkippedNode->TryGetStringField(TEXT("Graph"), GraphName) && !GraphName.IsEmpty())
	{
		FString EscapedName = GraphName;
		EscapedName.ReplaceInline(TEXT("~"), TEXT("~0"));
		EscapedName.ReplaceInline(TEXT("/"), TEXT("~1"));
		return FString::Printf(TEXT("%s/%s"), *Region.Path, *EscapedName);
	}
	return Region.Path;
}

FString WidgetBlueprintGraphSkippedNodeMessage(const TSharedPtr<FJsonObject>& SkippedNode)
{
	FString ClassPath;
	FString NodeTitle;
	if (SkippedNode.IsValid())
	{
		SkippedNode->TryGetStringField(TEXT("Class"), ClassPath);
		SkippedNode->TryGetStringField(TEXT("NodeTitle"), NodeTitle);
	}
	return FString::Printf(
		TEXT("current asset contains unsupported graph node%s%s; graph content cannot be fully canonicalized"),
		ClassPath.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" class '%s'"), *ClassPath),
		NodeTitle.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" titled '%s'"), *NodeTitle));
}

void WidgetBlueprintGraphAppendSkippedDiffEntries(
	const FWidgetBlueprintGraphRegion& Region,
	const TArray<TSharedPtr<FJsonValue>>& SkippedNodes,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
{
	for (const TSharedPtr<FJsonValue>& SkippedValue : SkippedNodes)
	{
		const TSharedPtr<FJsonObject> SkippedNode = SkippedValue.IsValid() ? SkippedValue->AsObject() : nullptr;
		FString Code = TEXT("UnsupportedGraphNodeClass");
		if (SkippedNode.IsValid())
		{
			SkippedNode->TryGetStringField(TEXT("Reason"), Code);
		}
		OutDiffEntries.Add(MakeShared<FJsonValueObject>(WidgetBlueprintGraphMakeCapabilityDiffEntry(
			WidgetBlueprintGraphPathFromSkippedNode(Region, SkippedNode),
			TEXT("skipped"),
			SkippedValue,
			nullptr,
			TEXT("unsupported"),
			WidgetBlueprintGraphSkippedNodeMessage(SkippedNode),
			Code)));
	}
}
}

FAssetDocumentCapabilityResult FWidgetBlueprintGraphAdapter::ValidateRegions(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& BodyObject) const
{
	for (const FWidgetBlueprintGraphRegion& Region : WidgetBlueprintGraphRegions())
	{
		TArray<FAssetDocumentGraphSpec> Graphs;
		const FAssetDocumentCapabilityResult ParseResult = WidgetBlueprintGraphParseAndResolveRegion(Context, BodyObject, Region, Graphs);
		if (!ParseResult.bSuccess)
		{
			return ParseResult;
		}

		const FAssetDocumentCapabilityResult NodeClassResult = WidgetBlueprintGraphValidateSupportedNodeClasses(Region, Graphs);
		if (!NodeClassResult.bSuccess)
		{
			return NodeClassResult;
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FWidgetBlueprintGraphAdapter::PreflightRegions(
	FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& BodyObject) const
{
	return PreflightRegions(Context, BodyObject, Cast<UBlueprint>(Context.Asset));
}

FAssetDocumentCapabilityResult FWidgetBlueprintGraphAdapter::PreflightRegions(
	FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& BodyObject,
	UBlueprint* DesiredStateBlueprint) const
{
	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Context.Asset);
	const FAssetDocumentK2GraphAdapter K2GraphAdapter;
	for (const FWidgetBlueprintGraphRegion& Region : WidgetBlueprintGraphRegions())
	{
		TArray<FAssetDocumentGraphSpec> Graphs;
		const FAssetDocumentCapabilityResult ParseResult = WidgetBlueprintGraphParseAndResolveRegion(Context, BodyObject, Region, Graphs);
		if (!ParseResult.bSuccess)
		{
			return ParseResult;
		}

		const FAssetDocumentCapabilityResult PreflightResult =
			K2GraphAdapter.PreflightGraphRegion(Cast<UBlueprint>(WidgetBlueprint), DesiredStateBlueprint, Region.K2Region, Graphs);
		if (!PreflightResult.bSuccess)
		{
			return PreflightResult;
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FWidgetBlueprintGraphAdapter::ApplyRegions(
	FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& DesiredBody,
	bool& bOutChanged) const
{
	bOutChanged = false;
	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Context.Asset);
	if (!WidgetBlueprint)
	{
		return WidgetBlueprintGraphFailure(TEXT("WidgetBlueprint graph apply requires exact UWidgetBlueprint asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	const FAssetDocumentK2GraphAdapter K2GraphAdapter;
	for (const FWidgetBlueprintGraphRegion& Region : WidgetBlueprintGraphRegions())
	{
		TArray<FAssetDocumentGraphSpec> Graphs;
		const FAssetDocumentCapabilityResult ParseResult = WidgetBlueprintGraphParseAndResolveRegion(Context, DesiredBody, Region, Graphs);
		if (!ParseResult.bSuccess)
		{
			return ParseResult;
		}

		const FAssetDocumentK2GraphApplyResult ApplyResult =
			K2GraphAdapter.ApplyGraphRegion(Cast<UBlueprint>(WidgetBlueprint), Region.K2Region, Graphs);
		if (!ApplyResult.Result.bSuccess)
		{
			return ApplyResult.Result;
		}
		bOutChanged |= ApplyResult.bChanged;
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Applied WidgetBlueprint graph regions"));
}

FAssetDocumentCapabilityResult FWidgetBlueprintGraphAdapter::ExtractRegions(
	const FAssetDocumentCapabilityContext& Context,
	TSharedRef<FJsonObject>& OutBodyJson) const
{
	const UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Context.Asset);
	const FAssetDocumentK2GraphAdapter K2GraphAdapter;
	for (const FWidgetBlueprintGraphRegion& Region : WidgetBlueprintGraphRegions())
	{
		const FAssetDocumentK2GraphExtractResult ExtractResult =
			K2GraphAdapter.ExtractGraphRegion(Cast<UBlueprint>(const_cast<UWidgetBlueprint*>(WidgetBlueprint)), Region.K2Region);
		OutBodyJson->SetField(Region.Name, FAssetDocumentGraphParser::WriteCanonicalGraphArray(ExtractResult.Graphs));
		WidgetBlueprintGraphMergeSkippedGraphEvidence(OutBodyJson, ExtractResult.SkippedNodes);
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted WidgetBlueprint graph regions"));
}

FAssetDocumentCapabilityResult FWidgetBlueprintGraphAdapter::DiffRegions(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& DesiredBody,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	const UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Context.Asset);
	if (!WidgetBlueprint)
	{
		return WidgetBlueprintGraphFailure(TEXT("WidgetBlueprint graph diff requires exact UWidgetBlueprint asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	const FAssetDocumentK2GraphAdapter K2GraphAdapter;
	const TSharedPtr<FJsonObject> Definitions = Context.Definitions ? *Context.Definitions : nullptr;
	for (const FWidgetBlueprintGraphRegion& Region : WidgetBlueprintGraphRegions())
	{
		TArray<FAssetDocumentGraphSpec> DesiredGraphs;
		const FAssetDocumentCapabilityResult DesiredParseResult = WidgetBlueprintGraphParseAndResolveRegion(Context, DesiredBody, Region, DesiredGraphs);
		if (!DesiredParseResult.bSuccess)
		{
			return DesiredParseResult;
		}
		if (WidgetBlueprintGraphIsEmptyFunctionOrMacroNoop(Region, DesiredGraphs))
		{
			continue;
		}

		const FAssetDocumentK2GraphExtractResult CurrentExtract =
			K2GraphAdapter.ExtractGraphRegion(Cast<UBlueprint>(const_cast<UWidgetBlueprint*>(WidgetBlueprint)), Region.K2Region);
		TArray<FAssetDocumentGraphSpec> CurrentGraphs = CurrentExtract.Graphs;
		WidgetBlueprintGraphNormalizeIdentityForDiff(DesiredGraphs, WidgetBlueprint);
		WidgetBlueprintGraphNormalizeIdentityForDiff(CurrentGraphs, WidgetBlueprint);

		const TArray<FAssetDocumentGraphDiffEntry> GraphEntries =
			FAssetDocumentGraphDiff::CompareUbergraphPages(DesiredGraphs, CurrentGraphs, Definitions);
		for (const FAssetDocumentGraphDiffEntry& GraphEntry : GraphEntries)
		{
			OutDiffEntries.Add(MakeShared<FJsonValueObject>(WidgetBlueprintGraphMakeCapabilityDiffEntry(Region, GraphEntry)));
		}

		WidgetBlueprintGraphAppendSkippedDiffEntries(Region, CurrentExtract.SkippedNodes, OutDiffEntries);
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Diffed WidgetBlueprint graph regions"));
}
