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

namespace
{
constexpr const TCHAR* AnimGraphPath = TEXT("/Body/AnimGraph");
constexpr const TCHAR* CanonicalGraphId = TEXT("AnimGraph");
constexpr const TCHAR* CanonicalGraphKind = TEXT("AnimGraph");

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
			TEXT("/Body/AnimGraph/Graphs/AnimGraph"),
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
		InOutContext.GraphPath = TEXT("/Body/AnimGraph/Graphs/AnimGraph");
		return InitializeRuntimeContextFromAsset(RegionContext, InOutContext, true, bOutChanged);
	}

	virtual FAssetDocumentCapabilityResult RepairAfterApply(
		const FAssetDocumentGraphSpec& GraphSpec,
		const FAssetDocumentAnimationGraphContext& Context) override
	{
		return FAssetDocumentCapabilityResult::Success();
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
	RuntimeContext.GraphPath = TEXT("/Body/AnimGraph/Graphs/AnimGraph");
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

TSharedPtr<FJsonValue> MakeCanonicalValue()
{
	return MakeShared<FJsonValueObject>(MakeCanonicalRegionObject());
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
	Schema->SetStringField(TEXT("Shape"), TEXT("{Graphs:[{Id:'AnimGraph', Kind:'AnimGraph', Nodes:[], Links:[], Subgraphs:[]}]}"));
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

	FAssetDocumentAnimationGraphRuntime Runtime(MakeShared<FAssetDocumentAnimationGraphNodeActionProvider>());
	FAssetDocumentAnimationGraphContext RuntimeContext;
	RuntimeContext.Asset = Context.Asset;
	RuntimeContext.GraphKind = CanonicalGraphKind;
	RuntimeContext.GraphPath = TEXT("/Body/AnimGraph/Graphs/AnimGraph");
	FAnimGraphStructuralHook Hook(Context, bOutChanged);
	return Runtime.ApplyGraph(Graphs[0], RuntimeContext, Hook);
}

FAssetDocumentCapabilityResult FAssetDocumentAnimGraphRegionAdapter::ExtractRegion(
	const FAssetDocumentRegionContext&,
	TSharedPtr<FJsonValue>& OutCurrentValue) const
{
	OutCurrentValue = MakeCanonicalValue();
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
	CurrentGraphs.Add(MakeCanonicalGraphSpec());
	const TArray<FAssetDocumentGraphDiffEntry> GraphEntries =
		FAssetDocumentGraphDiff::CompareGraphRegion(DesiredGraphs, CurrentGraphs, TEXT("/Body/AnimGraph"));

	for (const FAssetDocumentGraphDiffEntry& GraphEntry : GraphEntries)
	{
		OutDiffEntries.Add(MakeShared<FJsonValueObject>(GraphDiffEntryToBodyDiffEntry(GraphEntry)));
	}

	return FAssetDocumentCapabilityResult::Success();
}
