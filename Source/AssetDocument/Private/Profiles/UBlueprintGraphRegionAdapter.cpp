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
constexpr const TCHAR* UbergraphPagesPath = TEXT("/Body/UbergraphPages");
constexpr const TCHAR* K2NodeCallFunctionClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");

FAssetDocumentCapabilityResult GraphFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FAssetDocumentCapabilityResult InvalidRegionTypeFailure(const FString& RegionName)
{
	return GraphFailure(
		FString::Printf(TEXT("Body.%s must be an array when authored"), *RegionName),
		FString::Printf(TEXT("/Body/%s"), *RegionName),
		TEXT("InvalidGraphRegionType"));
}

FAssetDocumentCapabilityResult GraphDiagnosticsFailure(const TArray<FAssetDocumentGraphDiagnostic>& Diagnostics)
{
	if (Diagnostics.IsEmpty())
	{
		return GraphFailure(TEXT("Graph region validation failed"), UbergraphPagesPath, TEXT("InvalidGraphRegion"));
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

FString NodePath(int32 GraphIndex, int32 NodeIndex)
{
	return FString::Printf(TEXT("%s/%d/Nodes/%d"), UbergraphPagesPath, GraphIndex, NodeIndex);
}

UClass* ResolveClass(const FString& ClassPath)
{
	return ClassPath.IsEmpty() ? nullptr : StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath);
}

struct FMemberFunctionResolutionResult
{
	bool bResolved = false;
	FString Code;
	FString Reason;
	FString SuggestedAction;
};

FMemberFunctionResolutionResult ResolveMemberFunction(const TSharedPtr<FJsonObject>& Member)
{
	FMemberFunctionResolutionResult Result;
	if (!Member.IsValid())
	{
		Result.Code = TEXT("InvalidGraphMemberReference");
		Result.Reason = TEXT("K2Node_CallFunction requires a MemberRef object with OwnerClass and Name");
		Result.SuggestedAction = TEXT("add a valid MemberRef for the function or remove the call function node from the managed graph");
		return Result;
	}

	FString Kind;
	if (!Member->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("MemberRef"))
	{
		Result.Code = TEXT("InvalidGraphMemberReference");
		Result.Reason = TEXT("K2Node_CallFunction Member must be a MemberRef");
		Result.SuggestedAction = TEXT("change Member.Kind to MemberRef and provide OwnerClass plus Name");
		return Result;
	}

	FString OwnerClassPath;
	FString FunctionName;
	if (!Member->TryGetStringField(TEXT("OwnerClass"), OwnerClassPath)
		|| OwnerClassPath.IsEmpty()
		|| !Member->TryGetStringField(TEXT("Name"), FunctionName)
		|| FunctionName.IsEmpty())
	{
		Result.Code = TEXT("InvalidGraphMemberReference");
		Result.Reason = TEXT("K2Node_CallFunction MemberRef must include non-empty OwnerClass and Name");
		Result.SuggestedAction = TEXT("fix the MemberRef OwnerClass and Name fields before graph apply validation");
		return Result;
	}

	if (OwnerClassPath == TEXT("Self"))
	{
		Result.Code = TEXT("UnresolvedGraphFunction");
		Result.Reason = TEXT("K2Node_CallFunction MemberRef OwnerClass 'Self' cannot be resolved in the current graph validation tier");
		Result.SuggestedAction = TEXT("use an explicit reflected OwnerClass in MemberRef or wait for staged Self resolution support");
		return Result;
	}

	UClass* OwnerClass = ResolveClass(OwnerClassPath);
	if (!OwnerClass)
	{
		Result.Code = TEXT("UnresolvedGraphFunction");
		Result.Reason = FString::Printf(TEXT("K2Node_CallFunction MemberRef OwnerClass '%s' could not be loaded"), *OwnerClassPath);
		Result.SuggestedAction = TEXT("fix the MemberRef OwnerClass path or remove the function node from the managed graph");
		return Result;
	}

	if (!OwnerClass->FindFunctionByName(FName(*FunctionName)))
	{
		Result.Code = TEXT("UnresolvedGraphFunction");
		Result.Reason = FString::Printf(TEXT("K2Node_CallFunction MemberRef '%s.%s' does not resolve to a reflected UFunction"), *OwnerClassPath, *FunctionName);
		Result.SuggestedAction = TEXT("fix the MemberRef function Name or remove the function node from the managed graph");
		return Result;
	}

	Result.bResolved = true;
	return Result;
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

FAssetDocumentCapabilityResult UnsupportedGraphFailure(const TArray<FAssetDocumentUnsupportedNodeDiagnostic>& UnsupportedDiagnostics)
{
	if (UnsupportedDiagnostics.IsEmpty())
	{
		return GraphFailure(TEXT("Graph region validation failed"), UbergraphPagesPath, TEXT("InvalidGraphRegion"));
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

TSharedRef<FJsonObject> MakeCapabilityDiffEntry(const FAssetDocumentGraphDiffEntry& GraphEntry)
{
	const bool bUnsupported = GraphEntry.Status == TEXT("unsupported");
	const bool bUnchanged = GraphEntry.Status == TEXT("unchanged");
	const FString PublicStatus = bUnsupported ? FString(TEXT("skipped")) : (bUnchanged ? FString(TEXT("unchanged")) : FString(TEXT("changed")));
	const FString Change = bUnchanged ? FString() : GraphEntry.Status;
	return MakeCapabilityDiffEntry(
		GraphEntry.Path,
		PublicStatus,
		GraphEntry.Current,
		GraphEntry.Desired,
		Change,
		GraphEntry.Message,
		bUnsupported ? FString(TEXT("UnsupportedGraphDiff")) : FString());
}

FString GraphPathFromSkippedNode(const TSharedPtr<FJsonObject>& SkippedNode)
{
	FString GraphName;
	if (SkippedNode.IsValid() && SkippedNode->TryGetStringField(TEXT("Graph"), GraphName) && !GraphName.IsEmpty())
	{
		FString EscapedName = GraphName;
		EscapedName.ReplaceInline(TEXT("~"), TEXT("~0"));
		EscapedName.ReplaceInline(TEXT("/"), TEXT("~1"));
		return FString::Printf(TEXT("%s/%s"), UbergraphPagesPath, *EscapedName);
	}
	return UbergraphPagesPath;
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
			GraphPathFromSkippedNode(SkippedNode),
			TEXT("skipped"),
			SkippedValue,
			nullptr,
			TEXT("unsupported"),
			SkippedNodeMessage(SkippedNode),
			Code)));
	}
}

FAssetDocumentCapabilityResult ParseDesiredUbergraphPages(
	const TSharedRef<FJsonObject>& DesiredBody,
	TArray<FAssetDocumentGraphSpec>& OutGraphs)
{
	OutGraphs.Reset();
	const TSharedPtr<FJsonValue>* UbergraphPagesValue = DesiredBody->Values.Find(TEXT("UbergraphPages"));
	if (!UbergraphPagesValue || !UbergraphPagesValue->IsValid() || (*UbergraphPagesValue)->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	if ((*UbergraphPagesValue)->Type != EJson::Array)
	{
		return InvalidRegionTypeFailure(TEXT("UbergraphPages"));
	}

	FAssetDocumentGraphParseOptions ParseOptions;
	ParseOptions.Path = UbergraphPagesPath;
	FAssetDocumentGraphParseResult ParseResult =
		FAssetDocumentGraphParser::ParseGraphArray((*UbergraphPagesValue)->AsArray(), ParseOptions);
	if (!ParseResult.IsValid())
	{
		return GraphDiagnosticsFailure(ParseResult.Diagnostics);
	}

	OutGraphs = MoveTemp(ParseResult.Graphs);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseAndResolveDesiredUbergraphPages(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& DesiredBody,
	TArray<FAssetDocumentGraphSpec>& OutGraphs)
{
	TArray<FAssetDocumentGraphSpec> ParsedGraphs;
	const FAssetDocumentCapabilityResult ParseResult = ParseDesiredUbergraphPages(DesiredBody, ParsedGraphs);
	if (!ParseResult.bSuccess)
	{
		return ParseResult;
	}

	FAssetDocumentGraphDefinitionResolveOptions ResolveOptions;
	ResolveOptions.GraphsPath = UbergraphPagesPath;
	const TSharedPtr<FJsonObject> Definitions = Context.Definitions ? *Context.Definitions : nullptr;
	FAssetDocumentGraphDefinitionResolveResult ResolveResult =
		FAssetDocumentGraphDefinitionResolver::ResolveGraphArray(ParsedGraphs, Definitions, ResolveOptions);
	if (!ResolveResult.IsValid())
	{
		return GraphDiagnosticsFailure(ResolveResult.Diagnostics);
	}

	OutGraphs = MoveTemp(ResolveResult.Graphs);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateUbergraphPages(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (Value->Type != EJson::Array)
	{
		return InvalidRegionTypeFailure(TEXT("UbergraphPages"));
	}

	const TArray<TSharedPtr<FJsonValue>>& GraphValues = Value->AsArray();
	if (GraphValues.IsEmpty())
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	FAssetDocumentGraphParseOptions ParseOptions;
	ParseOptions.Path = UbergraphPagesPath;
	FAssetDocumentGraphParseResult ParseResult = FAssetDocumentGraphParser::ParseGraphArray(GraphValues, ParseOptions);
	if (!ParseResult.IsValid())
	{
		return GraphDiagnosticsFailure(ParseResult.Diagnostics);
	}

	FAssetDocumentGraphDefinitionResolveOptions ResolveOptions;
	ResolveOptions.GraphsPath = UbergraphPagesPath;
	const TSharedPtr<FJsonObject> Definitions = Context.Definitions ? *Context.Definitions : nullptr;
	FAssetDocumentGraphDefinitionResolveResult ResolveResult =
		FAssetDocumentGraphDefinitionResolver::ResolveGraphArray(ParseResult.Graphs, Definitions, ResolveOptions);
	if (!ResolveResult.IsValid())
	{
		return GraphDiagnosticsFailure(ResolveResult.Diagnostics);
	}

	const FAssetDocumentNodeAdapterRegistry CurrentTierRegistry = FAssetDocumentK2GraphAdapter::CreateTier1NodeAdapterRegistry();
	TArray<FAssetDocumentUnsupportedNodeDiagnostic> UnsupportedDiagnostics;
	for (int32 GraphIndex = 0; GraphIndex < ResolveResult.Graphs.Num(); ++GraphIndex)
	{
		const FAssetDocumentGraphSpec& Graph = ResolveResult.Graphs[GraphIndex];
		for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
		{
			const FAssetDocumentNodeSpec& Node = Graph.Nodes[NodeIndex];
			const FString Path = NodePath(GraphIndex, NodeIndex);
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

			if (Node.Class == K2NodeCallFunctionClassPath)
			{
				const FMemberFunctionResolutionResult FunctionResolution = ResolveMemberFunction(Node.Member);
				if (FunctionResolution.bResolved)
				{
					if (CurrentTierRegistry.FindAdapter(NodeClass).IsValid())
					{
						continue;
					}
					UnsupportedDiagnostics.Add(MakeUnsupportedNodeDiagnostic(
						Node,
						Path,
						TEXT("UnsupportedGraphFunction"),
						TEXT("reflected function resolves, but the current tier has no function adapter for graph apply validation"),
						TEXT("add a thin K2Node_CallFunction adapter for this function pattern or remove the function node from the managed graph")));
				}
				else
				{
					UnsupportedDiagnostics.Add(MakeUnsupportedNodeDiagnostic(
						Node,
						Path,
						FunctionResolution.Code,
						FunctionResolution.Reason,
						FunctionResolution.SuggestedAction));
				}
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
		return UnsupportedGraphFailure(UnsupportedDiagnostics);
	}

	return FAssetDocumentCapabilityResult::Success();
}
}

FAssetDocumentCapabilityResult FUBlueprintGraphRegionAdapter::ValidateRegions(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& BodyObject) const
{
	const TSharedPtr<FJsonValue>* UbergraphPagesValue = BodyObject->Values.Find(TEXT("UbergraphPages"));
	return ValidateUbergraphPages(Context, UbergraphPagesValue ? *UbergraphPagesValue : TSharedPtr<FJsonValue>());
}

FAssetDocumentCapabilityResult FUBlueprintGraphRegionAdapter::ExtractRegions(
	const FAssetDocumentCapabilityContext& Context,
	TSharedRef<FJsonObject>& OutBodyJson) const
{
	const UBlueprint* Blueprint = Cast<UBlueprint>(Context.Asset);
	if (!Blueprint)
	{
		OutBodyJson->SetArrayField(TEXT("UbergraphPages"), {});
		return FAssetDocumentCapabilityResult::Success();
	}

	const FAssetDocumentK2GraphAdapter K2GraphAdapter;
	const FAssetDocumentK2GraphExtractResult ExtractResult = K2GraphAdapter.ExtractUbergraphPages(Blueprint);
	OutBodyJson->SetField(TEXT("UbergraphPages"), FAssetDocumentGraphParser::WriteCanonicalGraphArray(ExtractResult.Graphs));
	MergeSkippedGraphEvidence(OutBodyJson, ExtractResult.SkippedNodes);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult FUBlueprintGraphRegionAdapter::DiffRegions(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonObject>& DesiredBody,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	TArray<FAssetDocumentGraphSpec> DesiredGraphs;
	const FAssetDocumentCapabilityResult DesiredParseResult = ParseDesiredUbergraphPages(DesiredBody, DesiredGraphs);
	if (!DesiredParseResult.bSuccess)
	{
		return DesiredParseResult;
	}

	const UBlueprint* Blueprint = Cast<UBlueprint>(Context.Asset);
	if (!Blueprint)
	{
		return GraphFailure(TEXT("UBlueprint graph diff requires exact UBlueprint asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
	}

	const FAssetDocumentK2GraphAdapter K2GraphAdapter;
	const FAssetDocumentK2GraphExtractResult CurrentExtract = K2GraphAdapter.ExtractUbergraphPages(Blueprint);
	const TSharedPtr<FJsonObject> Definitions = Context.Definitions ? *Context.Definitions : nullptr;
	const TArray<FAssetDocumentGraphDiffEntry> GraphEntries =
		FAssetDocumentGraphDiff::CompareUbergraphPages(DesiredGraphs, CurrentExtract.Graphs, Definitions);
	for (const FAssetDocumentGraphDiffEntry& GraphEntry : GraphEntries)
	{
		OutDiffEntries.Add(MakeShared<FJsonValueObject>(MakeCapabilityDiffEntry(GraphEntry)));
	}

	AppendSkippedGraphDiffEntries(CurrentExtract.SkippedNodes, OutDiffEntries);
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

	TArray<FAssetDocumentGraphSpec> DesiredGraphs;
	const FAssetDocumentCapabilityResult DesiredParseResult =
		ParseAndResolveDesiredUbergraphPages(Context, DesiredBody, DesiredGraphs);
	if (!DesiredParseResult.bSuccess)
	{
		return DesiredParseResult;
	}

	const FAssetDocumentK2GraphAdapter K2GraphAdapter;
	const FAssetDocumentK2GraphApplyResult ApplyResult = K2GraphAdapter.ApplyUbergraphPages(Blueprint, DesiredGraphs);
	bOutChanged = ApplyResult.bChanged;
	return ApplyResult.Result;
}
