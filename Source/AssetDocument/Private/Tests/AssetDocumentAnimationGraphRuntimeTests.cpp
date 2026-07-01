// Copyright ProjectRPG. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Graphs/AssetDocumentGraphFieldRules.h"
#include "Graphs/AssetDocumentAnimationGraphRuntime.h"

#include "Animation/AnimationAsset.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraphNode.h"
#include "Misc/AutomationTest.h"

namespace
{
FAssetDocumentGraphFieldRuleContext MakeFieldRuleContext(const FString& FieldPath)
{
	FAssetDocumentGraphFieldRuleContext Context;
	Context.JsonPath = FAssetDocumentGraphFieldRules::MakeFieldJsonPath(
		TEXT("/Body/AnimGraph/Graphs/AnimGraph/Nodes/IdlePlayer/Fields"),
		FieldPath);
	Context.FieldPath = FieldPath;
	Context.OwnerGraphKind = TEXT("AnimGraph");
	Context.RequiredObjectClass = UAnimationAsset::StaticClass();
	return Context;
}

TSharedRef<FJsonObject> MakeAssetRef(const FString& Path)
{
	TSharedRef<FJsonObject> AssetRef = MakeShared<FJsonObject>();
	AssetRef->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	AssetRef->SetStringField(TEXT("Path"), Path);
	return AssetRef;
}

TSharedRef<FJsonObject> MakeClassRef(const FString& ClassPath)
{
	TSharedRef<FJsonObject> ClassRef = MakeShared<FJsonObject>();
	ClassRef->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ClassRef->SetStringField(TEXT("Path"), ClassPath);
	return ClassRef;
}

TSharedPtr<FJsonObject> MakeSpawner(const FString& ActionKey)
{
	TSharedRef<FJsonObject> Spawner = MakeShared<FJsonObject>();
	Spawner->SetStringField(TEXT("ActionKey"), ActionKey);
	return Spawner;
}

FAssetDocumentGraphSpec MakeRuntimeGraphWithNode(const FAssetDocumentNodeSpec& Node)
{
	FAssetDocumentGraphSpec Graph;
	Graph.Id = TEXT("AnimGraph");
	Graph.Kind = TEXT("AnimGraph");
	Graph.Nodes.Add(Node);
	return Graph;
}

FAssetDocumentNodeSpec MakeRuntimeNode(const FString& Id, const FString& ClassPath)
{
	FAssetDocumentNodeSpec Node;
	Node.Id = Id;
	Node.Class = ClassPath;
	return Node;
}

class FFakeAnimationGraphCandidateProvider final : public IAssetDocumentAnimationGraphCandidateProvider
{
public:
	TArray<FAssetDocumentAnimationGraphNodeSpawnCandidate> Candidates;
	int32 QueryCount = 0;
	int32 SpawnCount = 0;

	virtual TArray<FAssetDocumentAnimationGraphNodeSpawnCandidate> FindCandidates(
		const FAssetDocumentNodeSpec& NodeSpec,
		const FAssetDocumentAnimationGraphContext& Context) const override
	{
		++const_cast<FFakeAnimationGraphCandidateProvider*>(this)->QueryCount;
		return Candidates;
	}

	virtual FAssetDocumentCapabilityResult SpawnNode(
		const FAssetDocumentGraphSpec& GraphSpec,
		const FAssetDocumentNodeSpec& NodeSpec,
		const FAssetDocumentAnimationGraphContext& Context,
		const FAssetDocumentAnimationGraphNodeSpawnCandidate& Candidate,
		UEdGraphNode*& OutNode) const override
	{
		++const_cast<FFakeAnimationGraphCandidateProvider*>(this)->SpawnCount;
		OutNode = NewObject<UEdGraphNode>(GetTransientPackage());
		return FAssetDocumentCapabilityResult::Success();
	}
};

class FFakeAnimationGraphStructuralHook final : public IAssetDocumentAnimationGraphStructuralHook
{
public:
	int32 LocateCount = 0;
	int32 RepairCount = 0;

	virtual FAssetDocumentCapabilityResult LocateOrCreateGraph(
		const FAssetDocumentGraphSpec& GraphSpec,
		FAssetDocumentAnimationGraphContext& InOutContext) override
	{
		++LocateCount;
		InOutContext.GraphKind = GraphSpec.Kind;
		InOutContext.GraphPath = TEXT("/Body/AnimGraph/Graphs/AnimGraph");
		return FAssetDocumentCapabilityResult::Success();
	}

	virtual FAssetDocumentCapabilityResult RepairAfterApply(
		const FAssetDocumentGraphSpec& GraphSpec,
		const FAssetDocumentAnimationGraphContext& Context) override
	{
		++RepairCount;
		return FAssetDocumentCapabilityResult::Success();
	}
};

FAssetDocumentAnimationGraphNodeSpawnCandidate MakeCandidate(
	const FString& ClassPath,
	const FString& ActionKey = FString(),
	bool bSpawnable = true)
{
	FAssetDocumentAnimationGraphNodeSpawnCandidate Candidate;
	Candidate.ClassPath = ClassPath;
	Candidate.ActionKey = ActionKey;
	Candidate.MenuName = ActionKey;
	Candidate.bSpawnable = bSpawnable;
	if (!ActionKey.IsEmpty())
	{
		Candidate.Spawner = MakeSpawner(ActionKey);
	}
	return Candidate;
}

FString FakeGraphNodeClassPath()
{
	return UEdGraphNode::StaticClass()->GetPathName();
}

FString FirstDiagnosticCode(const FAssetDocumentCapabilityResult& Result)
{
	return Result.Diagnostics.IsEmpty() ? FString() : Result.Diagnostics[0].Code;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphFieldRulesTraitResolutionTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.FieldRules.TraitResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphFieldRulesTraitResolutionTest::RunTest(const FString&)
{
	const FAssetDocumentGraphFieldRuleContext Context = MakeFieldRuleContext(TEXT("Node.Sequence"));

	const FAssetDocumentGraphFieldRuleResult AssetResult =
		FAssetDocumentGraphFieldRules::ValidateTraitShape(
			Context,
			EAssetDocumentGraphFieldTrait::AssetRef,
			MakeShared<FJsonValueObject>(MakeAssetRef(TEXT("/Game/DoesNotNeedToExistForShape"))));

	TestTrue(TEXT("AssetRef trait shape validates"), AssetResult.bSuccess);

	const FAssetDocumentGraphFieldRuleResult ClassResult =
		FAssetDocumentGraphFieldRules::ValidateTraitShape(
			Context,
			EAssetDocumentGraphFieldTrait::ClassRef,
			MakeShared<FJsonValueObject>(MakeClassRef(TEXT("/Script/Engine.AnimInstance"))));

	TestTrue(TEXT("ClassRef trait shape validates"), ClassResult.bSuccess);

	EAssetDocumentGraphFieldTrait ResolvedTrait = EAssetDocumentGraphFieldTrait::Raw;
	const FAssetDocumentGraphFieldRuleResult ResolveResult =
		FAssetDocumentGraphFieldRules::ResolveTrait(
			Context,
			{ EAssetDocumentGraphFieldTrait::Raw, EAssetDocumentGraphFieldTrait::AssetRef },
			MakeShared<FJsonValueObject>(MakeAssetRef(TEXT("/Game/DoesNotNeedToExistForShape"))),
			ResolvedTrait);
	TestTrue(TEXT("Trait resolution succeeds"), ResolveResult.bSuccess);
	TestEqual(TEXT("AssetRef trait is selected"), ResolvedTrait, EAssetDocumentGraphFieldTrait::AssetRef);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphFieldRulesAmbiguousTraitTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.FieldRules.AmbiguousTrait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphFieldRulesAmbiguousTraitTest::RunTest(const FString&)
{
	const FAssetDocumentGraphFieldRuleContext Context = MakeFieldRuleContext(TEXT("Node.Sequence"));
	const FAssetDocumentGraphFieldRuleResult Result =
		FAssetDocumentGraphFieldRules::ValidateTraitShape(
			Context,
			EAssetDocumentGraphFieldTrait::Raw,
			MakeShared<FJsonValueObject>(MakeAssetRef(TEXT("/Game/Idle"))));

	TestFalse(TEXT("Semantic object cannot silently fall back to raw"), Result.bSuccess);
	TestEqual(TEXT("Ambiguous trait code is explicit"), Result.Code, FString(TEXT("AmbiguousGraphFieldTrait")));
	TestEqual(TEXT("Ambiguous trait path points at Kind"), Result.Path, FString(TEXT("/Body/AnimGraph/Graphs/AnimGraph/Nodes/IdlePlayer/Fields/Node.Sequence/Kind")));

	EAssetDocumentGraphFieldTrait ResolvedTrait = EAssetDocumentGraphFieldTrait::Raw;
	const FAssetDocumentGraphFieldRuleResult AmbiguousResolveResult =
		FAssetDocumentGraphFieldRules::ResolveTrait(
			Context,
			{ EAssetDocumentGraphFieldTrait::Name, EAssetDocumentGraphFieldTrait::Enum },
			MakeShared<FJsonValueString>(TEXT("Idle")),
			ResolvedTrait);
	TestFalse(TEXT("Multiple matching candidate traits are rejected"), AmbiguousResolveResult.bSuccess);
	TestEqual(
		TEXT("Ambiguous candidate trait code is explicit"),
		AmbiguousResolveResult.Code,
		FString(TEXT("AmbiguousGraphFieldTrait")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphFieldRulesAssetRefRejectsRawStringTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.FieldRules.AssetRefRejectsRawString",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphFieldRulesAssetRefRejectsRawStringTest::RunTest(const FString&)
{
	const FAssetDocumentGraphFieldRuleContext Context = MakeFieldRuleContext(TEXT("Node.Sequence"));
	const FAssetDocumentGraphFieldRuleResult Result =
		FAssetDocumentGraphFieldRules::ValidateTraitShape(
			Context,
			EAssetDocumentGraphFieldTrait::AssetRef,
			MakeShared<FJsonValueString>(TEXT("/Game/Idle")));

	TestFalse(TEXT("AssetRef rejects raw string paths"), Result.bSuccess);
	TestEqual(TEXT("AssetRef raw string failure code"), Result.Code, FString(TEXT("InvalidGraphFieldAssetRef")));
	TestEqual(TEXT("AssetRef raw string failure path"), Result.Path, Context.JsonPath);

	FAssetDocumentGraphFieldRuleContext NullableContext = Context;
	NullableContext.bAllowNull = true;
	const FAssetDocumentGraphFieldRuleResult NullableResult =
		FAssetDocumentGraphFieldRules::ValidateTraitShape(
			NullableContext,
			EAssetDocumentGraphFieldTrait::AssetRef,
			MakeShared<FJsonValueNull>());
	TestTrue(TEXT("Nullable AssetRef accepts explicit null"), NullableResult.bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphFieldRulesClassRefRequiresPathTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.FieldRules.ClassRefRequiresPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphFieldRulesClassRefRequiresPathTest::RunTest(const FString&)
{
	const FAssetDocumentGraphFieldRuleContext Context = MakeFieldRuleContext(TEXT("Node.InstanceClass"));
	TSharedRef<FJsonObject> ClassRef = MakeShared<FJsonObject>();
	ClassRef->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ClassRef->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimInstance"));

	const FAssetDocumentGraphFieldRuleResult Result =
		FAssetDocumentGraphFieldRules::ValidateTraitShape(
			Context,
			EAssetDocumentGraphFieldTrait::ClassRef,
			MakeShared<FJsonValueObject>(ClassRef));

	TestFalse(TEXT("ClassRef.Class-only shape is rejected"), Result.bSuccess);
	TestEqual(TEXT("ClassRef requires canonical Path"), Result.Code, FString(TEXT("MissingGraphFieldClassRefPath")));
	TestEqual(
		TEXT("ClassRef diagnostic points at Path"),
		Result.Path,
		FString(TEXT("/Body/AnimGraph/Graphs/AnimGraph/Nodes/IdlePlayer/Fields/Node.InstanceClass/Path")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphFieldRulesDefaultOmissionTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.FieldRules.DefaultOmission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphFieldRulesDefaultOmissionTest::RunTest(const FString&)
{
	TestTrue(
		TEXT("Primitive default values can be omitted"),
		FAssetDocumentGraphFieldRules::ShouldOmitDefaultField(
			MakeShared<FJsonValueNumber>(1.0),
			MakeShared<FJsonValueNumber>(1.0),
			false));

	TestFalse(
		TEXT("Identity or pin-affecting defaults stay authored"),
		FAssetDocumentGraphFieldRules::ShouldOmitDefaultField(
			MakeShared<FJsonValueString>(TEXT("Pose")),
			MakeShared<FJsonValueString>(TEXT("Pose")),
			true));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphFieldRulesJsonPointerEscapingTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.FieldRules.JsonPointerEscaping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphFieldRulesJsonPointerEscapingTest::RunTest(const FString&)
{
	FAssetDocumentGraphFieldRuleContext Context = MakeFieldRuleContext(TEXT("Node.Groups/A~B"));

	TSharedRef<FJsonObject> AssetRef = MakeAssetRef(TEXT("/Game/Idle"));
	AssetRef->RemoveField(TEXT("Path"));
	const FAssetDocumentGraphFieldRuleResult Result =
		FAssetDocumentGraphFieldRules::ValidateTraitShape(
			Context,
			EAssetDocumentGraphFieldTrait::AssetRef,
			MakeShared<FJsonValueObject>(AssetRef));

	TestFalse(TEXT("Missing AssetRef path fails"), Result.bSuccess);
	TestEqual(
		TEXT("Diagnostic path uses JSON pointer escaping"),
		Result.Path,
		FString(TEXT("/Body/AnimGraph/Graphs/AnimGraph/Nodes/IdlePlayer/Fields/Node.Groups~1A~0B/Path")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphFieldRulesStagedApplyOrderTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.FieldRules.StagedApplyOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphFieldRulesStagedApplyOrderTest::RunTest(const FString&)
{
	const TArray<EAssetDocumentGraphFieldApplyStage> Stages = FAssetDocumentGraphFieldRules::GetStagedApplyOrder();

	TestEqual(TEXT("Full apply stage barriers are exposed"), Stages.Num(), 9);
	if (Stages.Num() == 9)
	{
		TestEqual(TEXT("Validation happens first"), Stages[0], EAssetDocumentGraphFieldApplyStage::Validate);
		TestEqual(TEXT("Identity and pin-affecting fields apply second"), Stages[1], EAssetDocumentGraphFieldApplyStage::IdentityAndPins);
		TestEqual(TEXT("Dynamic pins reconstruct after identity fields"), Stages[2], EAssetDocumentGraphFieldApplyStage::ReconstructDynamicPins);
		TestEqual(TEXT("Regular reflected fields apply after reconstruction"), Stages[3], EAssetDocumentGraphFieldApplyStage::Fields);
		TestEqual(TEXT("Pin defaults apply after reflected fields"), Stages[4], EAssetDocumentGraphFieldApplyStage::PinDefaults);
		TestEqual(TEXT("Layout applies after pin defaults"), Stages[5], EAssetDocumentGraphFieldApplyStage::Layout);
		TestEqual(TEXT("Links apply after layout"), Stages[6], EAssetDocumentGraphFieldApplyStage::Links);
		TestEqual(TEXT("Repair runs after links"), Stages[7], EAssetDocumentGraphFieldApplyStage::Repair);
		TestEqual(TEXT("Post-apply evidence is last"), Stages[8], EAssetDocumentGraphFieldApplyStage::PostApplyEvidence);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphRuntimeUniqueCandidateTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.UniqueCandidate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeUniqueCandidateTest::RunTest(const FString&)
{
	TSharedRef<FFakeAnimationGraphCandidateProvider> Provider = MakeShared<FFakeAnimationGraphCandidateProvider>();
	Provider->Candidates.Add(MakeCandidate(FakeGraphNodeClassPath()));

	const FAssetDocumentAnimationGraphRuntime Runtime(Provider);
	FAssetDocumentAnimationGraphContext Context;
	Context.GraphPath = TEXT("/Body/AnimGraph/Graphs/AnimGraph");
	const FAssetDocumentGraphSpec Graph =
		MakeRuntimeGraphWithNode(MakeRuntimeNode(TEXT("IdlePlayer"), FakeGraphNodeClassPath()));

	const FAssetDocumentCapabilityResult Result = Runtime.ValidateGraph(Graph, Context);
	TestTrue(TEXT("Single candidate validates without explicit spawner"), Result.bSuccess);
	TestEqual(TEXT("Candidate provider is queried once"), Provider->QueryCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphRuntimeDuplicateCandidateRequiresSpawnerTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.DuplicateCandidateRequiresSpawner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeDuplicateCandidateRequiresSpawnerTest::RunTest(const FString&)
{
	TSharedRef<FFakeAnimationGraphCandidateProvider> Provider = MakeShared<FFakeAnimationGraphCandidateProvider>();
	Provider->Candidates.Add(MakeCandidate(FakeGraphNodeClassPath(), TEXT("BlendSpace")));
	Provider->Candidates.Add(MakeCandidate(FakeGraphNodeClassPath(), TEXT("AimOffset")));

	const FAssetDocumentAnimationGraphRuntime Runtime(Provider);
	FAssetDocumentAnimationGraphContext Context;
	Context.GraphPath = TEXT("/Body/AnimGraph/Graphs/AnimGraph");
	const FAssetDocumentGraphSpec AmbiguousGraph =
		MakeRuntimeGraphWithNode(MakeRuntimeNode(TEXT("MovePlayer"), FakeGraphNodeClassPath()));

	const FAssetDocumentCapabilityResult AmbiguousResult = Runtime.ValidateGraph(AmbiguousGraph, Context);
	TestFalse(TEXT("Duplicate candidates require Node.Spawner"), AmbiguousResult.bSuccess);
	TestEqual(TEXT("Duplicate candidate code"), FirstDiagnosticCode(AmbiguousResult), FString(TEXT("AmbiguousGraphNodeSpawner")));

	FAssetDocumentNodeSpec DisambiguatedNode =
		MakeRuntimeNode(TEXT("MovePlayer"), FakeGraphNodeClassPath());
	DisambiguatedNode.Spawner = MakeSpawner(TEXT("BlendSpace"));
	const FAssetDocumentCapabilityResult DisambiguatedResult =
		Runtime.ValidateGraph(MakeRuntimeGraphWithNode(DisambiguatedNode), Context);
	TestTrue(TEXT("Explicit spawner disambiguates candidates"), DisambiguatedResult.bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphRuntimeUnspawnableClassTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.UnspawnableClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeUnspawnableClassTest::RunTest(const FString&)
{
	TSharedRef<FFakeAnimationGraphCandidateProvider> Provider = MakeShared<FFakeAnimationGraphCandidateProvider>();
	Provider->Candidates.Add(MakeCandidate(FakeGraphNodeClassPath(), FString(), false));

	const FAssetDocumentAnimationGraphRuntime Runtime(Provider);
	FAssetDocumentAnimationGraphContext Context;
	Context.GraphPath = TEXT("/Body/AnimGraph/Graphs/AnimGraph");
	const FAssetDocumentGraphSpec Graph =
		MakeRuntimeGraphWithNode(MakeRuntimeNode(TEXT("DebugOnly"), FakeGraphNodeClassPath()));

	const FAssetDocumentCapabilityResult Result = Runtime.ValidateGraph(Graph, Context);
	TestFalse(TEXT("Unspawnable candidate fails preflight"), Result.bSuccess);
	TestEqual(TEXT("Unspawnable diagnostic code"), FirstDiagnosticCode(Result), FString(TEXT("UnspawnableGraphNodeClass")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphRuntimeApplyPreflightBeforeHookTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.ApplyPreflightBeforeHook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeApplyPreflightBeforeHookTest::RunTest(const FString&)
{
	TSharedRef<FFakeAnimationGraphCandidateProvider> Provider = MakeShared<FFakeAnimationGraphCandidateProvider>();
	Provider->Candidates.Add(MakeCandidate(FakeGraphNodeClassPath(), FString(), false));

	const FAssetDocumentAnimationGraphRuntime Runtime(Provider);
	FFakeAnimationGraphStructuralHook Hook;
	FAssetDocumentAnimationGraphContext Context;
	Context.GraphPath = TEXT("/Body/AnimGraph/Graphs/AnimGraph");
	const FAssetDocumentGraphSpec Graph =
		MakeRuntimeGraphWithNode(MakeRuntimeNode(TEXT("DebugOnly"), FakeGraphNodeClassPath()));

	const FAssetDocumentCapabilityResult Result = Runtime.ApplyGraph(Graph, Context, Hook);
	TestFalse(TEXT("ApplyGraph rejects unspawnable node"), Result.bSuccess);
	TestEqual(TEXT("Failed preflight does not locate graph"), Hook.LocateCount, 0);
	TestEqual(TEXT("Failed preflight does not repair graph"), Hook.RepairCount, 0);
	TestEqual(TEXT("Failed preflight does not spawn nodes"), Provider->SpawnCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphRuntimeStructuralHookBoundaryTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.StructuralHookBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeStructuralHookBoundaryTest::RunTest(const FString&)
{
	TSharedRef<FFakeAnimationGraphCandidateProvider> Provider = MakeShared<FFakeAnimationGraphCandidateProvider>();
	Provider->Candidates.Add(MakeCandidate(FakeGraphNodeClassPath()));

	FAssetDocumentAnimationGraphRuntime Runtime(Provider);
	FFakeAnimationGraphStructuralHook Hook;
	FAssetDocumentAnimationGraphContext Context;
	FAssetDocumentGraphSpec Graph =
		MakeRuntimeGraphWithNode(MakeRuntimeNode(TEXT("IdlePlayer"), FakeGraphNodeClassPath()));
	Graph.Id = TEXT("AnimGraph");

	const FAssetDocumentCapabilityResult Result = Runtime.ApplyGraph(Graph, Context, Hook);
	TestTrue(TEXT("ApplyGraph succeeds with fake structural hook"), Result.bSuccess);
	TestEqual(TEXT("Structural hook locates graph once"), Hook.LocateCount, 1);
	TestEqual(TEXT("Structural hook repairs graph once"), Hook.RepairCount, 1);
	TestEqual(TEXT("Candidate provider preflights and materializes node"), Provider->QueryCount, 2);
	TestEqual(TEXT("Candidate provider spawns node once"), Provider->SpawnCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphRuntimeStructuralHookCoversSubgraphsTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.StructuralHookCoversSubgraphs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeStructuralHookCoversSubgraphsTest::RunTest(const FString&)
{
	TSharedRef<FFakeAnimationGraphCandidateProvider> Provider = MakeShared<FFakeAnimationGraphCandidateProvider>();
	Provider->Candidates.Add(MakeCandidate(FakeGraphNodeClassPath()));

	FAssetDocumentAnimationGraphRuntime Runtime(Provider);
	FFakeAnimationGraphStructuralHook Hook;
	FAssetDocumentAnimationGraphContext Context;
	Context.GraphPath = TEXT("/Body/AnimGraph/Graphs/AnimGraph");

	FAssetDocumentGraphSpec Graph =
		MakeRuntimeGraphWithNode(MakeRuntimeNode(TEXT("IdlePlayer"), FakeGraphNodeClassPath()));
	Graph.Id = TEXT("AnimGraph");
	FAssetDocumentGraphSpec Subgraph =
		MakeRuntimeGraphWithNode(MakeRuntimeNode(TEXT("NestedPlayer"), FakeGraphNodeClassPath()));
	Subgraph.Id = TEXT("AnimGraph.Nested");
	Subgraph.Kind = TEXT("PoseSubgraph");
	Graph.Subgraphs.Add(Subgraph);

	const FAssetDocumentCapabilityResult Result = Runtime.ApplyGraph(Graph, Context, Hook);
	TestTrue(TEXT("ApplyGraph succeeds with a subgraph"), Result.bSuccess);
	TestEqual(TEXT("Structural hook locates root and subgraph"), Hook.LocateCount, 2);
	TestEqual(TEXT("Structural hook repairs root and subgraph"), Hook.RepairCount, 2);
	TestEqual(TEXT("Candidate provider validates and materializes root and subgraph nodes"), Provider->QueryCount, 4);
	TestEqual(TEXT("Candidate provider spawns root and subgraph nodes"), Provider->SpawnCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphRuntimeExtractSkippedEvidenceTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.ExtractSkippedEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeExtractSkippedEvidenceTest::RunTest(const FString&)
{
	FAssetDocumentAnimationGraphRuntime Runtime;
	FAssetDocumentAnimationGraphContext Context;
	Context.GraphKind = TEXT("AnimGraph");

	FAssetDocumentGraphSpec ExtractedGraph;
	const FAssetDocumentCapabilityResult Result = Runtime.ExtractGraph(Context, ExtractedGraph);

	TestTrue(TEXT("Runtime shell extract succeeds"), Result.bSuccess);
	TestFalse(TEXT("Runtime shell does not put skipped data in authored Evidence"), ExtractedGraph.Evidence.IsValid());
	TestTrue(TEXT("Runtime shell emits extract-only graph _Skipped"), ExtractedGraph.UnderscoreSkipped.IsValid());
	return true;
}

#endif
