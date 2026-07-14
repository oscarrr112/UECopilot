// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentAnimLayerRegionAdapter.h"

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

namespace
{
constexpr const TCHAR* AnimLayersPath = TEXT("/Body/AnimLayers");
constexpr const TCHAR* AnimLayerKind = TEXT("AnimLayer");

FAssetDocumentCapabilityResult AnimLayerFailure(const FString& Path, const FString& Code, const FString& Message)
{
	return FAssetDocumentJsonRegionUtils::Failure(Path, Code, Message);
}

FAssetDocumentCapabilityResult AnimLayerFromGraphDiagnostics(const TArray<FAssetDocumentGraphDiagnostic>& Diagnostics)
{
	if (Diagnostics.IsEmpty())
	{
		return AnimLayerFailure(AnimLayersPath, TEXT("InvalidAnimLayerRegion"), TEXT("Body.AnimLayers graph region is invalid."));
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

FString GraphIdentity(const FAssetDocumentGraphSpec& Graph)
{
	return !Graph.Name.IsEmpty() ? Graph.Name : Graph.Id;
}

TSharedRef<FJsonObject> MakeEmptyAnimLayerRegionObject()
{
	TSharedRef<FJsonObject> Region = MakeShared<FJsonObject>();
	Region->SetArrayField(TEXT("Graphs"), {});
	return Region;
}

FAssetDocumentCapabilityResult ParseAnimLayerRegion(
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<FAssetDocumentGraphSpec>& OutGraphs)
{
	OutGraphs.Reset();
	if (!DesiredValue.IsValid() || DesiredValue->Type != EJson::Object)
	{
		return AnimLayerFailure(
			AnimLayersPath,
			TEXT("InvalidAnimLayerRegionType"),
			TEXT("Body.AnimLayers must be an object with a Graphs array."));
	}

	const TSharedPtr<FJsonObject> RegionObject = DesiredValue->AsObject();
	if (!RegionObject.IsValid())
	{
		return AnimLayerFailure(
			AnimLayersPath,
			TEXT("InvalidAnimLayerRegionType"),
			TEXT("Body.AnimLayers must be an object with a Graphs array."));
	}

	FAssetDocumentGraphParseOptions Options;
	Options.Path = AnimLayersPath;
	const FAssetDocumentGraphParseResult ParseResult =
		FAssetDocumentGraphParser::ParseGraphRegion(RegionObject.ToSharedRef(), Options);
	if (!ParseResult.IsValid())
	{
		return AnimLayerFromGraphDiagnostics(ParseResult.Diagnostics);
	}

	TSet<FString> Identities;
	for (const FAssetDocumentGraphSpec& Graph : ParseResult.Graphs)
	{
		if (Graph.Kind != AnimLayerKind)
		{
			return AnimLayerFailure(
				FString::Printf(TEXT("%s/Graphs/%s/Kind"), AnimLayersPath, *FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Graph.Id)),
				TEXT("InvalidAnimLayerGraphKind"),
				TEXT("Body.AnimLayers graphs must use Kind='AnimLayer'."));
		}

		const FString Identity = GraphIdentity(Graph);
		if (Identity.IsEmpty())
		{
			return AnimLayerFailure(
				FString::Printf(TEXT("%s/Graphs/%s/Name"), AnimLayersPath, *FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Graph.Id)),
				TEXT("MissingAnimLayerGraphName"),
				TEXT("AnimLayer graph requires Id or Name as the layer function name."));
		}
		if (Identities.Contains(Identity))
		{
			return AnimLayerFailure(
				FString::Printf(TEXT("%s/Graphs/%s"), AnimLayersPath, *FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(Identity)),
				TEXT("DuplicateAnimLayerGraph"),
				FString::Printf(TEXT("Duplicate AnimLayer graph '%s'."), *Identity));
		}
		Identities.Add(Identity);
	}

	OutGraphs = ParseResult.Graphs;
	return FAssetDocumentCapabilityResult::Success();
}

UAnimationGraph* FindSelfAnimLayerGraph(UAnimBlueprint* AnimBlueprint, const FString& LayerName)
{
	if (!AnimBlueprint || LayerName.IsEmpty())
	{
		return nullptr;
	}

	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		UAnimationGraph* AnimGraph = Cast<UAnimationGraph>(Graph);
		if (AnimGraph && AnimGraph->GetName() == LayerName)
		{
			return AnimGraph;
		}
	}
	return Cast<UAnimationGraph>(FindObject<UEdGraph>(AnimBlueprint, *LayerName));
}

UAnimationGraph* FindOrCreateSelfAnimLayerGraph(
	UAnimBlueprint* AnimBlueprint,
	const FAssetDocumentGraphSpec& GraphSpec,
	bool& bOutChanged)
{
	const FString LayerName = GraphIdentity(GraphSpec);
	UAnimationGraph* AnimGraph = FindSelfAnimLayerGraph(AnimBlueprint, LayerName);
	if (AnimGraph)
	{
		return AnimGraph;
	}

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		AnimBlueprint,
		FName(*LayerName),
		UAnimationGraph::StaticClass(),
		UAnimationGraphSchema::StaticClass());
	FBlueprintEditorUtils::AddDomainSpecificGraph(AnimBlueprint, NewGraph);
	AnimBlueprint->LastEditedDocuments.Add(NewGraph);
	if (const UEdGraphSchema* Schema = NewGraph->GetSchema())
	{
		Schema->CreateDefaultNodesForGraph(*NewGraph);
	}
	bOutChanged = true;
	return Cast<UAnimationGraph>(NewGraph);
}

FAssetDocumentGraphSpec ExtractSelfAnimLayerGraphSpec(const UAnimationGraph* Graph)
{
	FAssetDocumentGraphSpec Spec;
	Spec.Id = Graph ? Graph->GetName() : FString();
	Spec.Name = Spec.Id;
	Spec.Kind = AnimLayerKind;
	Spec.Schema = UAnimationGraphSchema::StaticClass()->GetPathName();
	if (Graph && Graph->GraphGuid.IsValid())
	{
		Spec.GraphGuid = Graph->GraphGuid.ToString(EGuidFormats::Digits);
	}
	return Spec;
}

TArray<FAssetDocumentGraphSpec> ExtractSelfAnimLayerGraphs(const UAnimBlueprint* AnimBlueprint)
{
	TArray<FAssetDocumentGraphSpec> Graphs;
	if (!AnimBlueprint)
	{
		return Graphs;
	}

	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		const UAnimationGraph* AnimGraph = Cast<UAnimationGraph>(Graph);
		if (!AnimGraph || AnimGraph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph || AnimGraph->InterfaceGuid.IsValid())
		{
			continue;
		}
		Graphs.Add(ExtractSelfAnimLayerGraphSpec(AnimGraph));
	}
	return Graphs;
}

TSharedPtr<FJsonValue> WriteAnimLayerRegion(const TArray<FAssetDocumentGraphSpec>& Graphs)
{
	TSharedRef<FJsonObject> Region = MakeShared<FJsonObject>();
	Region->SetField(TEXT("Graphs"), FAssetDocumentGraphParser::WriteCanonicalGraphArray(Graphs));
	return MakeShared<FJsonValueObject>(Region);
}

TSharedPtr<FJsonObject> AnimLayerGraphDiffEntryToBodyDiffEntry(const FAssetDocumentGraphDiffEntry& GraphEntry)
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

class FAnimLayerStructuralHook final : public IAssetDocumentAnimationGraphStructuralHook
{
public:
	FAnimLayerStructuralHook(const FAssetDocumentRegionContext& InRegionContext, bool& bInOutChanged)
		: RegionContext(InRegionContext)
		, bOutChanged(bInOutChanged)
	{
	}

	virtual FAssetDocumentCapabilityResult LocateOrCreateGraph(
		const FAssetDocumentGraphSpec& GraphSpec,
		FAssetDocumentAnimationGraphContext& InOutContext) override
	{
		UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(RegionContext.Asset);
		if (!AnimBlueprint)
		{
			return AnimLayerFailure(AnimLayersPath, TEXT("InvalidAnimBlueprintAsset"), TEXT("Body.AnimLayers requires a UAnimBlueprint asset."));
		}

		UAnimationGraph* Graph = FindOrCreateSelfAnimLayerGraph(AnimBlueprint, GraphSpec, bOutChanged);
		if (!Graph)
		{
			return AnimLayerFailure(AnimLayersPath, TEXT("AnimLayerGraphCreateFailed"), TEXT("Failed to create AnimLayer graph."));
		}

		InOutContext.Asset = AnimBlueprint;
		InOutContext.Blueprint = AnimBlueprint;
		InOutContext.Graph = Graph;
		InOutContext.GraphKind = GraphSpec.Kind;
		InOutContext.GraphPath = FString::Printf(
			TEXT("%s/Graphs/%s"),
			AnimLayersPath,
			*FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(GraphIdentity(GraphSpec)));
		return FAssetDocumentCapabilityResult::Success();
	}

	virtual FAssetDocumentCapabilityResult RepairAfterApply(
		const FAssetDocumentGraphSpec&,
		const FAssetDocumentAnimationGraphContext&) override
	{
		return FAssetDocumentCapabilityResult::Success();
	}

private:
	const FAssetDocumentRegionContext& RegionContext;
	bool& bOutChanged;
};

FAssetDocumentCapabilityResult ValidateAnimLayerValue(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue)
{
	TArray<FAssetDocumentGraphSpec> Graphs;
	const FAssetDocumentCapabilityResult ParseResult = ParseAnimLayerRegion(DesiredValue, Graphs);
	if (!ParseResult.bSuccess)
	{
		return ParseResult;
	}

	FAssetDocumentAnimationGraphContext RuntimeContext;
	RuntimeContext.Asset = Context.Asset;
	RuntimeContext.Blueprint = Cast<UAnimBlueprint>(Context.Asset);
	RuntimeContext.GraphKind = AnimLayerKind;
	RuntimeContext.GraphPath = AnimLayersPath;
	const FAssetDocumentAnimationGraphRuntime Runtime(MakeShared<FAssetDocumentAnimationGraphNodeActionProvider>());
	for (const FAssetDocumentGraphSpec& Graph : Graphs)
	{
		FAssetDocumentAnimationGraphContext GraphContext = RuntimeContext;
		GraphContext.GraphPath = FString::Printf(
			TEXT("%s/Graphs/%s"),
			AnimLayersPath,
			*FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(GraphIdentity(Graph)));
		const FAssetDocumentCapabilityResult GraphResult = Runtime.ValidateGraph(Graph, GraphContext);
		if (!GraphResult.bSuccess)
		{
			return GraphResult;
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}
}

FAssetDocumentAnimLayerRegionAdapter::FAssetDocumentAnimLayerRegionAdapter(FName InAdapterName)
	: AdapterName(InAdapterName)
{
}

FName FAssetDocumentAnimLayerRegionAdapter::GetName() const
{
	return AdapterName;
}

bool FAssetDocumentAnimLayerRegionAdapter::SupportsRegion(const FAssetDocumentRegionContext& Context) const
{
	return Context.RegionId == TEXT("Body.AnimLayers");
}

TSharedRef<FJsonObject> FAssetDocumentAnimLayerRegionAdapter::GetSchemaHint(const FAssetDocumentRegionContext&) const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("Kind"), TEXT("AnimLayerRecursiveGraphRegion"));
	Schema->SetStringField(TEXT("Shape"), TEXT("{Graphs:[{Id, Name, Kind:'AnimLayer', Nodes:[], Links:[], Subgraphs:[]}]}"));
	return Schema;
}

FAssetDocumentCapabilityResult FAssetDocumentAnimLayerRegionAdapter::ValidateRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue) const
{
	return ValidateAnimLayerValue(Context, DesiredValue);
}

FAssetDocumentCapabilityResult FAssetDocumentAnimLayerRegionAdapter::ApplyRegion(
	FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	bool& bOutChanged)
{
	bOutChanged = false;
	TArray<FAssetDocumentGraphSpec> Graphs;
	const FAssetDocumentCapabilityResult ParseResult = ParseAnimLayerRegion(DesiredValue, Graphs);
	if (!ParseResult.bSuccess)
	{
		return ParseResult;
	}

	FAssetDocumentAnimationGraphRuntime Runtime(MakeShared<FAssetDocumentAnimationGraphNodeActionProvider>());
	FAnimLayerStructuralHook Hook(Context, bOutChanged);
	for (const FAssetDocumentGraphSpec& Graph : Graphs)
	{
		FAssetDocumentAnimationGraphContext RuntimeContext;
		RuntimeContext.Asset = Context.Asset;
		RuntimeContext.Blueprint = Cast<UAnimBlueprint>(Context.Asset);
		RuntimeContext.GraphKind = AnimLayerKind;
		RuntimeContext.GraphPath = AnimLayersPath;
		const FAssetDocumentCapabilityResult ApplyResult = Runtime.ApplyGraph(Graph, RuntimeContext, Hook);
		if (!ApplyResult.bSuccess)
		{
			return ApplyResult;
		}
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Applied AnimBlueprint AnimLayers"));
}

FAssetDocumentCapabilityResult FAssetDocumentAnimLayerRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext& Context,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	OutCurrentValue = WriteAnimLayerRegion(ExtractSelfAnimLayerGraphs(Cast<UAnimBlueprint>(Context.Asset)));
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted AnimBlueprint AnimLayers"));
}

FAssetDocumentCapabilityResult FAssetDocumentAnimLayerRegionAdapter::DiffRegion(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	TArray<FAssetDocumentGraphSpec> DesiredGraphs;
	const FAssetDocumentCapabilityResult ParseResult = ParseAnimLayerRegion(DesiredValue, DesiredGraphs);
	if (!ParseResult.bSuccess)
	{
		return ParseResult;
	}

	const FAssetDocumentCapabilityResult ValidateResult = ValidateAnimLayerValue(Context, DesiredValue);
	if (!ValidateResult.bSuccess)
	{
		return ValidateResult;
	}

	const TArray<FAssetDocumentGraphSpec> CurrentGraphs =
		ExtractSelfAnimLayerGraphs(Cast<UAnimBlueprint>(Context.Asset));
	const TArray<FAssetDocumentGraphDiffEntry> GraphEntries =
		FAssetDocumentGraphDiff::CompareGraphRegion(DesiredGraphs, CurrentGraphs, AnimLayersPath);
	for (const FAssetDocumentGraphDiffEntry& GraphEntry : GraphEntries)
	{
		OutDiffEntries.Add(MakeShared<FJsonValueObject>(AnimLayerGraphDiffEntryToBodyDiffEntry(GraphEntry)));
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Diffed AnimBlueprint AnimLayers"));
}
