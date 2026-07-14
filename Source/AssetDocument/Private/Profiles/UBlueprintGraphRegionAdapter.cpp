// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/UBlueprintGraphRegionAdapter.h"

#include "Graphs/AssetDocumentGraphDefinitionResolver.h"
#include "Graphs/AssetDocumentGraphDiff.h"
#include "Graphs/AssetDocumentGraphParser.h"
#include "Graphs/AssetDocumentNodeAdapter.h"
#include "Graphs/K2GraphAdapter.h"

#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

namespace
{
struct FUBlueprintGraphRegion
{
	FString Name;
	FString Path;
	EAssetDocumentK2GraphRegion K2Region = EAssetDocumentK2GraphRegion::UbergraphPages;
};

const TArray<FUBlueprintGraphRegion>& GraphRegions()
{
	static const TArray<FUBlueprintGraphRegion> Regions = {
		{TEXT("UbergraphPages"), TEXT("/Body/UbergraphPages"), EAssetDocumentK2GraphRegion::UbergraphPages},
		{TEXT("FunctionGraphs"), TEXT("/Body/FunctionGraphs"), EAssetDocumentK2GraphRegion::FunctionGraphs},
		{TEXT("MacroGraphs"), TEXT("/Body/MacroGraphs"), EAssetDocumentK2GraphRegion::MacroGraphs},
	};
	return Regions;
}

FAssetDocumentCapabilityResult GraphFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FAssetDocumentCapabilityResult InvalidRegionTypeFailure(const FUBlueprintGraphRegion& Region)
{
	return GraphFailure(
		FString::Printf(TEXT("Body.%s must be an array when authored"), *Region.Name),
		Region.Path,
		TEXT("InvalidGraphRegionType"));
}

FAssetDocumentCapabilityResult GraphDiagnosticsFailure(const TArray<FAssetDocumentGraphDiagnostic>& Diagnostics, const FString& FallbackPath)
{
	if (Diagnostics.IsEmpty())
	{
		return GraphFailure(TEXT("Graph region validation failed"), FallbackPath, TEXT("InvalidGraphRegion"));
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

TSharedPtr<FJsonObject> CloneJsonObject(const TSharedPtr<FJsonObject>& Object)
{
	return AssetDocumentGraphJson::CloneJsonObject(Object);
}

FString UBlueprintGraphNodePath(const FUBlueprintGraphRegion& Region, int32 GraphIndex, int32 NodeIndex)
{
	return FString::Printf(TEXT("%s/%d/Nodes/%d"), *Region.Path, GraphIndex, NodeIndex);
}

UClass* ResolveClass(const FString& ClassPath)
{
	return ClassPath.IsEmpty() ? nullptr : StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath);
}

FAssetDocumentUnsupportedNodeDiagnostic MakeUnsupportedNodeDiagnostic(
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
	Diagnostic.Member = CloneJsonObject(Node.Member);
	Diagnostic.Reason = Reason;
	Diagnostic.SuggestedAction = SuggestedAction;
	return Diagnostic;
}

FAssetDocumentCapabilityResult UnsupportedGraphFailure(const FString& FallbackPath, const TArray<FAssetDocumentUnsupportedNodeDiagnostic>& UnsupportedDiagnostics)
{
	if (UnsupportedDiagnostics.IsEmpty())
	{
		return GraphFailure(TEXT("Graph region validation failed"), FallbackPath, TEXT("InvalidGraphRegion"));
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

void MergeSkippedGraphEvidence(TSharedRef<FJsonObject>& OutBodyJson, const TArray<TSharedPtr<FJsonValue>>& SkippedNodes)
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

TSharedRef<FJsonObject> MakeCapabilityDiffEntry(
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

FString RewriteGraphDiffPath(const FString& Path, const FUBlueprintGraphRegion& Region)
{
	const FString UBlueprintPrefix = TEXT("/Body/UbergraphPages");
	if (Region.Path == UBlueprintPrefix || !Path.StartsWith(UBlueprintPrefix))
	{
		return Path;
	}
	return Region.Path + Path.RightChop(FCString::Strlen(*UBlueprintPrefix));
}

TSharedRef<FJsonObject> MakeCapabilityDiffEntry(const FUBlueprintGraphRegion& Region, const FAssetDocumentGraphDiffEntry& GraphEntry)
{
	const bool bUnsupported = GraphEntry.Status == TEXT("unsupported");
	const bool bUnchanged = GraphEntry.Status == TEXT("unchanged");
	const FString PublicStatus = bUnsupported ? FString(TEXT("skipped")) : (bUnchanged ? FString(TEXT("unchanged")) : FString(TEXT("changed")));
	const FString Change = bUnchanged ? FString() : GraphEntry.Status;
	return MakeCapabilityDiffEntry(
		RewriteGraphDiffPath(GraphEntry.Path, Region),
		PublicStatus,
		GraphEntry.Current,
		GraphEntry.Desired,
		Change,
		GraphEntry.Message,
		bUnsupported ? FString(TEXT("UnsupportedGraphDiff")) : FString());
}

FString GraphPathFromSkippedNode(const FUBlueprintGraphRegion& Region, const TSharedPtr<FJsonObject>& SkippedNode)
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

FString SkippedNodeMessage(const TSharedPtr<FJsonObject>& SkippedNode)
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

void AppendSkippedGraphDiffEntries(
	const FUBlueprintGraphRegion& Region,
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
		OutDiffEntries.Add(MakeShared<FJsonValueObject>(MakeCapabilityDiffEntry(
			GraphPathFromSkippedNode(Region, SkippedNode),
			TEXT("skipped"),
			SkippedValue,
			nullptr,
			TEXT("unsupported"),
			SkippedNodeMessage(SkippedNode),
			Code)));
	}
}

FAssetDocumentCapabilityResult ParseDesiredGraphRegion(
	const TSharedRef<FJsonObject>& DesiredBody,
	const FUBlueprintGraphRegion& Region,
	TArray<FAssetDocumentGraphSpec>& OutGraphs)
{
	OutGraphs.Reset();
	const TSharedPtr<FJsonValue>* RegionValue = DesiredBody->Values.Find(Region.Name);
	if (!RegionValue || !RegionValue->IsValid() || (*RegionValue)->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	if ((*RegionValue)->Type != EJson::Array)
	{
		return InvalidRegionTypeFailure(Region);
	}

	FAssetDocumentGraphParseOptions ParseOptions;
	ParseOptions.Path = Region.Path;
	FAssetDocumentGraphParseResult ParseResult =
		FAssetDocumentGraphParser::ParseGraphArray((*RegionValue)->AsArray(), ParseOptions);
	if (!ParseResult.IsValid())
	{
		return GraphDiagnosticsFailure(ParseResult.Diagnostics, Region.Path);
	}

	OutGraphs = MoveTemp(ParseResult.Graphs);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseAndResolveDesiredGraphRegion(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& DesiredBody,
	const FUBlueprintGraphRegion& Region,
	TArray<FAssetDocumentGraphSpec>& OutGraphs)
{
	TArray<FAssetDocumentGraphSpec> ParsedGraphs;
	const FAssetDocumentCapabilityResult ParseResult = ParseDesiredGraphRegion(DesiredBody, Region, ParsedGraphs);
	if (!ParseResult.bSuccess)
	{
		return ParseResult;
	}

	FAssetDocumentGraphDefinitionResolveOptions ResolveOptions;
	ResolveOptions.GraphsPath = Region.Path;
	const TSharedPtr<FJsonObject> Definitions = Context.Definitions ? *Context.Definitions : nullptr;
	FAssetDocumentGraphDefinitionResolveResult ResolveResult =
		FAssetDocumentGraphDefinitionResolver::ResolveGraphArray(ParsedGraphs, Definitions, ResolveOptions);
	if (!ResolveResult.IsValid())
	{
		return GraphDiagnosticsFailure(ResolveResult.Diagnostics, Region.Path);
	}

	OutGraphs = MoveTemp(ResolveResult.Graphs);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateGraphRegion(
	const FAssetDocumentCapabilityContext& Context,
	const FUBlueprintGraphRegion& Region,
	const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (Value->Type != EJson::Array)
	{
		return InvalidRegionTypeFailure(Region);
	}

	const TArray<TSharedPtr<FJsonValue>>& GraphValues = Value->AsArray();
	if (GraphValues.IsEmpty())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FAssetDocumentGraphParseOptions ParseOptions;
	ParseOptions.Path = Region.Path;
	FAssetDocumentGraphParseResult ParseResult = FAssetDocumentGraphParser::ParseGraphArray(GraphValues, ParseOptions);
	if (!ParseResult.IsValid())
	{
		return GraphDiagnosticsFailure(ParseResult.Diagnostics, Region.Path);
	}

	FAssetDocumentGraphDefinitionResolveOptions ResolveOptions;
	ResolveOptions.GraphsPath = Region.Path;
	const TSharedPtr<FJsonObject> Definitions = Context.Definitions ? *Context.Definitions : nullptr;
	FAssetDocumentGraphDefinitionResolveResult ResolveResult =
		FAssetDocumentGraphDefinitionResolver::ResolveGraphArray(ParseResult.Graphs, Definitions, ResolveOptions);
	if (!ResolveResult.IsValid())
	{
		return GraphDiagnosticsFailure(ResolveResult.Diagnostics, Region.Path);
	}

	const FAssetDocumentNodeAdapterRegistry CurrentTierRegistry = FAssetDocumentK2GraphAdapter::CreateTier1NodeAdapterRegistry();
	TArray<FAssetDocumentUnsupportedNodeDiagnostic> UnsupportedDiagnostics;
	for (int32 GraphIndex = 0; GraphIndex < ResolveResult.Graphs.Num(); ++GraphIndex)
	{
			const FAssetDocumentGraphSpec& Graph = ResolveResult.Graphs[GraphIndex];
			for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
			{
				const FAssetDocumentNodeSpec& Node = Graph.Nodes[NodeIndex];
				const FString Path = UBlueprintGraphNodePath(Region, GraphIndex, NodeIndex);
			UClass* NodeClass = ResolveClass(Node.Class);
			if (!NodeClass)
			{
				UnsupportedDiagnostics.Add(MakeUnsupportedNodeDiagnostic(
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

			UnsupportedDiagnostics.Add(MakeUnsupportedNodeDiagnostic(
				Node,
				Path,
				TEXT("UnsupportedGraphNodeClass"),
				TEXT("node class has no registered AssetDocument adapter in the current tier"),
				TEXT("add a thin node adapter for this class or remove the node from the managed graph")));
		}
	}

	if (!UnsupportedDiagnostics.IsEmpty())
	{
		return UnsupportedGraphFailure(Region.Path, UnsupportedDiagnostics);
	}

	const FAssetDocumentK2GraphAdapter K2GraphAdapter;
	const FAssetDocumentCapabilityResult PreflightResult =
		K2GraphAdapter.PreflightGraphRegion(Cast<UBlueprint>(Context.Asset), Region.K2Region, ResolveResult.Graphs);
	if (!PreflightResult.bSuccess)
	{
		return PreflightResult;
	}

	return FAssetDocumentCapabilityResult::Success();
}
}

FAssetDocumentCapabilityResult FUBlueprintGraphRegionAdapter::ValidateRegions(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& BodyObject) const
{
	for (const FUBlueprintGraphRegion& Region : GraphRegions())
	{
		const TSharedPtr<FJsonValue>* RegionValue = BodyObject->Values.Find(Region.Name);
		const FAssetDocumentCapabilityResult Result =
			ValidateGraphRegion(Context, Region, RegionValue ? *RegionValue : TSharedPtr<FJsonValue>());
		if (!Result.bSuccess)
		{
			return Result;
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FUBlueprintGraphRegionAdapter::ExtractRegions(
	const FAssetDocumentCapabilityContext& Context,
	TSharedRef<FJsonObject>& OutBodyJson) const
{
	const UBlueprint* Blueprint = Cast<UBlueprint>(Context.Asset);
	if (!Blueprint)
	{
		for (const FUBlueprintGraphRegion& Region : GraphRegions())
		{
			OutBodyJson->SetArrayField(Region.Name, {});
		}
		return FAssetDocumentCapabilityResult::Success();
	}

	const FAssetDocumentK2GraphAdapter K2GraphAdapter;
	for (const FUBlueprintGraphRegion& Region : GraphRegions())
	{
		const FAssetDocumentK2GraphExtractResult ExtractResult =
			K2GraphAdapter.ExtractGraphRegion(Blueprint, Region.K2Region);
		OutBodyJson->SetField(Region.Name, FAssetDocumentGraphParser::WriteCanonicalGraphArray(ExtractResult.Graphs));
		MergeSkippedGraphEvidence(OutBodyJson, ExtractResult.SkippedNodes);
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FUBlueprintGraphRegionAdapter::DiffRegions(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& DesiredBody,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	const UBlueprint* Blueprint = Cast<UBlueprint>(Context.Asset);
	if (!Blueprint)
	{
		return GraphFailure(TEXT("UBlueprint graph diff requires exact UBlueprint asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	const FAssetDocumentK2GraphAdapter K2GraphAdapter;
	const TSharedPtr<FJsonObject> Definitions = Context.Definitions ? *Context.Definitions : nullptr;
	for (const FUBlueprintGraphRegion& Region : GraphRegions())
	{
		TArray<FAssetDocumentGraphSpec> DesiredGraphs;
		const FAssetDocumentCapabilityResult DesiredParseResult = ParseDesiredGraphRegion(DesiredBody, Region, DesiredGraphs);
		if (!DesiredParseResult.bSuccess)
		{
			return DesiredParseResult;
		}
		if (DesiredGraphs.IsEmpty()
			&& (Region.K2Region == EAssetDocumentK2GraphRegion::FunctionGraphs
				|| Region.K2Region == EAssetDocumentK2GraphRegion::MacroGraphs))
		{
			continue;
		}

		const FAssetDocumentK2GraphExtractResult CurrentExtract =
			K2GraphAdapter.ExtractGraphRegion(Blueprint, Region.K2Region);
		const TArray<FAssetDocumentGraphDiffEntry> GraphEntries =
			FAssetDocumentGraphDiff::CompareUbergraphPages(DesiredGraphs, CurrentExtract.Graphs, Definitions);
		for (const FAssetDocumentGraphDiffEntry& GraphEntry : GraphEntries)
		{
			OutDiffEntries.Add(MakeShared<FJsonValueObject>(MakeCapabilityDiffEntry(Region, GraphEntry)));
		}

		AppendSkippedGraphDiffEntries(Region, CurrentExtract.SkippedNodes, OutDiffEntries);
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Diffed UBlueprint graph regions"));
}

FAssetDocumentCapabilityResult ApplyUBlueprintGraphRegions(
	FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& DesiredBody,
	bool& bOutChanged)
{
	bOutChanged = false;
	UBlueprint* Blueprint = Cast<UBlueprint>(Context.Asset);
	if (!Blueprint)
	{
		return GraphFailure(TEXT("UBlueprint graph apply requires exact UBlueprint asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	const FAssetDocumentK2GraphAdapter K2GraphAdapter;
	for (const FUBlueprintGraphRegion& Region : GraphRegions())
	{
		TArray<FAssetDocumentGraphSpec> DesiredGraphs;
		const FAssetDocumentCapabilityResult DesiredParseResult =
			ParseAndResolveDesiredGraphRegion(Context, DesiredBody, Region, DesiredGraphs);
		if (!DesiredParseResult.bSuccess)
		{
			return DesiredParseResult;
		}

		const FAssetDocumentK2GraphApplyResult ApplyResult =
			K2GraphAdapter.ApplyGraphRegion(Blueprint, Region.K2Region, DesiredGraphs);
		if (!ApplyResult.Result.bSuccess)
		{
			return ApplyResult.Result;
		}
		bOutChanged |= ApplyResult.bChanged;
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Applied UBlueprint graph regions"));
}
