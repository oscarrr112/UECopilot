// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentAnimGraphRegionAdapter.h"

#include "AssetDocumentJsonRegionUtils.h"
#include "Graphs/AssetDocumentAnimationGraphRuntime.h"
#include "Graphs/AssetDocumentGraphDiff.h"
#include "Graphs/AssetDocumentGraphParser.h"

#include "Dom/JsonValue.h"

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
};

class FAnimGraphStructuralHook final : public IAssetDocumentAnimationGraphStructuralHook
{
public:
	explicit FAnimGraphStructuralHook(const FAssetDocumentRegionContext& InRegionContext)
		: RegionContext(InRegionContext)
	{
	}

	virtual FAssetDocumentCapabilityResult LocateOrCreateGraph(
		const FAssetDocumentGraphSpec& GraphSpec,
		FAssetDocumentAnimationGraphContext& InOutContext) override
	{
		InOutContext.Asset = RegionContext.Asset;
		InOutContext.GraphKind = GraphSpec.Kind;
		InOutContext.GraphPath = TEXT("/Body/AnimGraph/Graphs/AnimGraph");
		return FAssetDocumentCapabilityResult::Success();
	}

	virtual FAssetDocumentCapabilityResult RepairAfterApply(
		const FAssetDocumentGraphSpec& GraphSpec,
		const FAssetDocumentAnimationGraphContext& Context) override
	{
		return FAssetDocumentCapabilityResult::Success();
	}

private:
	const FAssetDocumentRegionContext& RegionContext;
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
	RuntimeContext.Asset = RegionContext ? RegionContext->Asset : nullptr;
	RuntimeContext.GraphPath = TEXT("/Body/AnimGraph/Graphs/AnimGraph");
	RuntimeContext.GraphKind = CanonicalGraphKind;
	const FAssetDocumentAnimationGraphRuntime Runtime(MakeShared<FEmptyAnimGraphCandidateProvider>());
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
	Entry->SetField(TEXT("current"), AssetDocumentGraphJson::CloneJsonValue(GraphEntry.Current));
	Entry->SetField(TEXT("desired"), AssetDocumentGraphJson::CloneJsonValue(GraphEntry.Desired));
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

	FAssetDocumentAnimationGraphRuntime Runtime(MakeShared<FEmptyAnimGraphCandidateProvider>());
	FAssetDocumentAnimationGraphContext RuntimeContext;
	FAnimGraphStructuralHook Hook(Context);
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
