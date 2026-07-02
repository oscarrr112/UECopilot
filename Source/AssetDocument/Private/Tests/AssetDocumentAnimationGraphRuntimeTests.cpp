// Copyright ProjectRPG. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Graphs/AssetDocumentGraphFieldRules.h"
#include "Graphs/AssetDocumentAnimationGraphRuntime.h"

#include "Animation/AnimationAsset.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
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

struct FFakeAnimationGraphPinSpec
{
	FString Name;
	EEdGraphPinDirection Direction = EGPD_Input;
};

class FFakeAnimationGraphCandidateProvider final : public IAssetDocumentAnimationGraphCandidateProvider
{
public:
	TArray<FAssetDocumentAnimationGraphNodeSpawnCandidate> Candidates;
	TMap<FString, TArray<FFakeAnimationGraphPinSpec>> PinsByNodeId;
	int32 QueryCount = 0;
	int32 SpawnCount = 0;
	UEdGraphNode* LastSpawnedNode = nullptr;

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
		UObject* Outer = Context.Graph ? static_cast<UObject*>(Context.Graph) : GetTransientPackage();
		OutNode = NewObject<UEdGraphNode>(Outer);
		if (Context.Graph)
		{
			Context.Graph->AddNode(OutNode, false, false);
		}
		if (const TArray<FFakeAnimationGraphPinSpec>* PinSpecs =
			const_cast<FFakeAnimationGraphCandidateProvider*>(this)->PinsByNodeId.Find(NodeSpec.Id))
		{
			for (const FFakeAnimationGraphPinSpec& PinSpec : *PinSpecs)
			{
				OutNode->CreatePin(PinSpec.Direction, TEXT("Wildcard"), FName(*PinSpec.Name));
			}
		}
		const_cast<FFakeAnimationGraphCandidateProvider*>(this)->LastSpawnedNode = OutNode;
		return FAssetDocumentCapabilityResult::Success();
	}
};

class FFakeAnimationGraphStructuralHook final : public IAssetDocumentAnimationGraphStructuralHook
{
public:
	UEdGraph* CreatedGraph = nullptr;
	int32 LocateCount = 0;
	int32 RepairCount = 0;

	virtual FAssetDocumentCapabilityResult LocateOrCreateGraph(
		const FAssetDocumentGraphSpec& GraphSpec,
		FAssetDocumentAnimationGraphContext& InOutContext) override
	{
		++LocateCount;
		if (!InOutContext.Graph)
		{
			CreatedGraph = NewObject<UEdGraph>(GetTransientPackage());
			InOutContext.Graph = CreatedGraph;
		}
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

FString FirstDiagnosticPath(const FAssetDocumentCapabilityResult& Result)
{
	return Result.Diagnostics.IsEmpty() ? FString() : Result.Diagnostics[0].Path;
}

FAssetDocumentLinkSpec MakeRuntimeLink(
	const FString& FromNode,
	const FString& FromPin,
	const FString& ToNode,
	const FString& ToPin)
{
	FAssetDocumentLinkSpec Link;
	Link.From.Node = FromNode;
	Link.From.Pin = FromPin;
	Link.To.Node = ToNode;
	Link.To.Pin = ToPin;
	return Link;
}

UEdGraphNode* AddManagedEditorNode(
	UEdGraph* Graph,
	const FAssetDocumentGraphSpec& GraphSpec,
	const FAssetDocumentNodeSpec& NodeSpec,
	int32 X,
	int32 Y)
{
	UEdGraphNode* Node = Graph
		? NewObject<UEdGraphNode>(Graph, FName(*FAssetDocumentAnimationGraphRuntime::MakeManagedNodeObjectName(NodeSpec.Id)))
		: nullptr;
	if (!Node)
	{
		return nullptr;
	}
	Graph->AddNode(Node, false, false);
	Node->NodeGuid = FAssetDocumentAnimationGraphRuntime::MakeManagedNodeGuid(GraphSpec, NodeSpec);
	Node->NodePosX = X;
	Node->NodePosY = Y;
	return Node;
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
	FAssetDocumentAnimationGraphRuntimeManagedNodeIdentityTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.ManagedNodeIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeManagedNodeIdentityTest::RunTest(const FString&)
{
	TSharedRef<FFakeAnimationGraphCandidateProvider> Provider = MakeShared<FFakeAnimationGraphCandidateProvider>();
	Provider->Candidates.Add(MakeCandidate(FakeGraphNodeClassPath()));

	FAssetDocumentAnimationGraphRuntime Runtime(Provider);
	FFakeAnimationGraphStructuralHook Hook;
	FAssetDocumentAnimationGraphContext Context;
	FAssetDocumentNodeSpec Node = MakeRuntimeNode(TEXT("IdlePlayer"), FakeGraphNodeClassPath());
	FAssetDocumentGraphSpec Graph = MakeRuntimeGraphWithNode(Node);
	Graph.Id = TEXT("AnimGraph");
	Graph.Kind = TEXT("AnimGraph");

	const FAssetDocumentCapabilityResult Result = Runtime.ApplyGraph(Graph, Context, Hook);
	TestTrue(TEXT("ApplyGraph succeeds"), Result.bSuccess);
	TestNotNull(TEXT("Runtime exposes the spawned node for identity assertions"), Provider->LastSpawnedNode);
	if (Provider->LastSpawnedNode)
	{
		const FGuid ExpectedGuid = FAssetDocumentAnimationGraphRuntime::MakeManagedNodeGuid(Graph, Node);
		TestEqual(TEXT("Runtime assigns deterministic managed NodeGuid"), Provider->LastSpawnedNode->NodeGuid, ExpectedGuid);

		FString ParsedNodeId;
		TestTrue(
			TEXT("Runtime stores recoverable sidecar node id in the node object name"),
			FAssetDocumentAnimationGraphRuntime::TryParseManagedNodeObjectName(Provider->LastSpawnedNode->GetFName(), ParsedNodeId));
		TestEqual(TEXT("Recovered sidecar node id roundtrips"), ParsedNodeId, FString(TEXT("IdlePlayer")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphRuntimeReusesManagedNodeIdentityTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.ReusesManagedNodeIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeReusesManagedNodeIdentityTest::RunTest(const FString&)
{
	TSharedRef<FFakeAnimationGraphCandidateProvider> Provider = MakeShared<FFakeAnimationGraphCandidateProvider>();
	Provider->Candidates.Add(MakeCandidate(FakeGraphNodeClassPath()));

	FAssetDocumentAnimationGraphRuntime Runtime(Provider);
	FFakeAnimationGraphStructuralHook Hook;
	FAssetDocumentAnimationGraphContext Context;
	FAssetDocumentNodeSpec Node = MakeRuntimeNode(TEXT("IdlePlayer"), FakeGraphNodeClassPath());
	FAssetDocumentGraphSpec Graph = MakeRuntimeGraphWithNode(Node);
	Graph.Id = TEXT("AnimGraph");
	Graph.Kind = TEXT("AnimGraph");

	const FAssetDocumentCapabilityResult FirstResult = Runtime.ApplyGraph(Graph, Context, Hook);
	TestTrue(TEXT("First apply succeeds"), FirstResult.bSuccess);
	const FAssetDocumentCapabilityResult SecondResult = Runtime.ApplyGraph(Graph, Context, Hook);
	TestTrue(TEXT("Second apply succeeds"), SecondResult.bSuccess);
	TestEqual(TEXT("Managed node is spawned once and reused by authored identity"), Provider->SpawnCount, 1);
	TestNotNull(TEXT("Structural hook created a real graph"), Hook.CreatedGraph);
	if (Hook.CreatedGraph)
	{
		TestEqual(TEXT("Graph still contains one managed node"), Hook.CreatedGraph->Nodes.Num(), 1);
	}
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
	FAssetDocumentAnimationGraphRuntimeRejectsMissingLinkPinTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.RejectsMissingLinkPin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeRejectsMissingLinkPinTest::RunTest(const FString&)
{
	TSharedRef<FFakeAnimationGraphCandidateProvider> Provider = MakeShared<FFakeAnimationGraphCandidateProvider>();
	Provider->Candidates.Add(MakeCandidate(FakeGraphNodeClassPath()));
	Provider->PinsByNodeId.Add(TEXT("Source"), { { TEXT("Out"), EGPD_Output } });
	Provider->PinsByNodeId.Add(TEXT("Target"), { { TEXT("In"), EGPD_Input } });

	FAssetDocumentAnimationGraphRuntime Runtime(Provider);
	FFakeAnimationGraphStructuralHook Hook;
	FAssetDocumentAnimationGraphContext Context;
	FAssetDocumentGraphSpec Graph;
	Graph.Id = TEXT("AnimGraph");
	Graph.Kind = TEXT("AnimGraph");
	Graph.Nodes.Add(MakeRuntimeNode(TEXT("Source"), FakeGraphNodeClassPath()));
	Graph.Nodes.Add(MakeRuntimeNode(TEXT("Target"), FakeGraphNodeClassPath()));
	Graph.Links.Add(MakeRuntimeLink(TEXT("Source"), TEXT("Out"), TEXT("Target"), TEXT("Missing")));

	const FAssetDocumentCapabilityResult Result = Runtime.ApplyGraph(Graph, Context, Hook);
	TestFalse(TEXT("Missing semantic link endpoint is rejected after node reconstruction"), Result.bSuccess);
	TestEqual(TEXT("Missing pin diagnostic code"), FirstDiagnosticCode(Result), FString(TEXT("UnresolvedGraphLinkEndpoint")));
	TestEqual(
		TEXT("Missing pin diagnostic path is semantic"),
		FirstDiagnosticPath(Result),
		FString(TEXT("/Body/AnimGraph/Graphs/AnimGraph/Links/Source.Out->Target.Missing")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphRuntimeRejectsAmbiguousLinkPinTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.RejectsAmbiguousLinkPin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeRejectsAmbiguousLinkPinTest::RunTest(const FString&)
{
	TSharedRef<FFakeAnimationGraphCandidateProvider> Provider = MakeShared<FFakeAnimationGraphCandidateProvider>();
	Provider->Candidates.Add(MakeCandidate(FakeGraphNodeClassPath()));
	Provider->PinsByNodeId.Add(TEXT("Source"), { { TEXT("Out"), EGPD_Output }, { TEXT("Out"), EGPD_Output } });
	Provider->PinsByNodeId.Add(TEXT("Target"), { { TEXT("In"), EGPD_Input } });

	FAssetDocumentAnimationGraphRuntime Runtime(Provider);
	FFakeAnimationGraphStructuralHook Hook;
	FAssetDocumentAnimationGraphContext Context;
	FAssetDocumentGraphSpec Graph;
	Graph.Id = TEXT("AnimGraph");
	Graph.Kind = TEXT("AnimGraph");
	Graph.Nodes.Add(MakeRuntimeNode(TEXT("Source"), FakeGraphNodeClassPath()));
	Graph.Nodes.Add(MakeRuntimeNode(TEXT("Target"), FakeGraphNodeClassPath()));
	Graph.Links.Add(MakeRuntimeLink(TEXT("Source"), TEXT("Out"), TEXT("Target"), TEXT("In")));

	const FAssetDocumentCapabilityResult Result = Runtime.ApplyGraph(Graph, Context, Hook);
	TestFalse(TEXT("Ambiguous semantic link endpoint is rejected after node reconstruction"), Result.bSuccess);
	TestEqual(TEXT("Ambiguous pin diagnostic code"), FirstDiagnosticCode(Result), FString(TEXT("AmbiguousGraphLinkEndpointPin")));
	TestEqual(
		TEXT("Ambiguous pin diagnostic path is semantic"),
		FirstDiagnosticPath(Result),
		FString(TEXT("/Body/AnimGraph/Graphs/AnimGraph/Links/Source.Out->Target.In")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphRuntimeExtractsManagedNodesAndLinksTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.ExtractsManagedNodesAndLinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeExtractsManagedNodesAndLinksTest::RunTest(const FString&)
{
	FAssetDocumentAnimationGraphRuntime Runtime;
	FAssetDocumentAnimationGraphContext Context;
	Context.GraphKind = TEXT("AnimGraph");
	Context.GraphPath = TEXT("/Body/AnimGraph/Graphs/AnimGraph");
	Context.Graph = NewObject<UEdGraph>(GetTransientPackage());

	FAssetDocumentGraphSpec SourceGraph;
	SourceGraph.Id = TEXT("AnimGraph");
	SourceGraph.Kind = TEXT("AnimGraph");
	FAssetDocumentNodeSpec SourceSpec = MakeRuntimeNode(TEXT("Source"), FakeGraphNodeClassPath());
	FAssetDocumentNodeSpec TargetSpec = MakeRuntimeNode(TEXT("Target"), FakeGraphNodeClassPath());
	UEdGraphNode* SourceNode = AddManagedEditorNode(Context.Graph, SourceGraph, SourceSpec, 120, 240);
	UEdGraphNode* TargetNode = AddManagedEditorNode(Context.Graph, SourceGraph, TargetSpec, 360, 240);
	UEdGraphPin* OutPin = SourceNode ? SourceNode->CreatePin(EGPD_Output, TEXT("Wildcard"), TEXT("Out")) : nullptr;
	UEdGraphPin* InPin = TargetNode ? TargetNode->CreatePin(EGPD_Input, TEXT("Wildcard"), TEXT("In")) : nullptr;
	if (OutPin && InPin)
	{
		OutPin->MakeLinkTo(InPin);
	}

	FAssetDocumentGraphSpec ExtractedGraph;
	const FAssetDocumentCapabilityResult Result = Runtime.ExtractGraph(Context, ExtractedGraph);

	TestTrue(TEXT("Managed graph extract succeeds"), Result.bSuccess);
	TestEqual(TEXT("Managed nodes are extracted"), ExtractedGraph.Nodes.Num(), 2);
	TestEqual(TEXT("Managed links are extracted"), ExtractedGraph.Links.Num(), 1);
	const FAssetDocumentNodeSpec* ExtractedSource = ExtractedGraph.Nodes.FindByPredicate(
		[](const FAssetDocumentNodeSpec& Node)
		{
			return Node.Id == TEXT("Source");
		});
	TestNotNull(TEXT("Source node is extracted by authored identity"), ExtractedSource);
	if (ExtractedSource)
	{
		TestEqual(TEXT("Class is extracted"), ExtractedSource->Class, FakeGraphNodeClassPath());
		double X = 0.0;
		double Y = 0.0;
		TestTrue(TEXT("Position is extracted"), ExtractedSource->Position.IsValid());
		if (ExtractedSource->Position.IsValid())
		{
			ExtractedSource->Position->TryGetNumberField(TEXT("X"), X);
			ExtractedSource->Position->TryGetNumberField(TEXT("Y"), Y);
		}
		TestEqual(TEXT("Position X roundtrips"), X, 120.0);
		TestEqual(TEXT("Position Y roundtrips"), Y, 240.0);
		TestTrue(TEXT("Evidence object is extracted"), ExtractedSource->Evidence.IsValid());
		if (ExtractedSource->Evidence.IsValid())
		{
			TestEqual(
				TEXT("Evidence.NodeGuid is extracted"),
				ExtractedSource->Evidence->GetStringField(TEXT("NodeGuid")),
				FAssetDocumentAnimationGraphRuntime::MakeManagedNodeGuid(SourceGraph, SourceSpec).ToString(EGuidFormats::Digits));
		}
	}
	if (ExtractedGraph.Links.Num() == 1)
	{
		TestEqual(TEXT("Link source node is semantic"), ExtractedGraph.Links[0].From.Node, FString(TEXT("Source")));
		TestEqual(TEXT("Link source pin is semantic"), ExtractedGraph.Links[0].From.Pin, FString(TEXT("Out")));
		TestEqual(TEXT("Link target node is semantic"), ExtractedGraph.Links[0].To.Node, FString(TEXT("Target")));
		TestEqual(TEXT("Link target pin is semantic"), ExtractedGraph.Links[0].To.Pin, FString(TEXT("In")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphRuntimeNoShellSkippedForManagedNodesTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.NoShellSkippedForManagedNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeNoShellSkippedForManagedNodesTest::RunTest(const FString&)
{
	FAssetDocumentAnimationGraphRuntime Runtime;
	FAssetDocumentAnimationGraphContext Context;
	Context.GraphKind = TEXT("AnimGraph");
	Context.Graph = NewObject<UEdGraph>(GetTransientPackage());

	FAssetDocumentGraphSpec SourceGraph;
	SourceGraph.Id = TEXT("AnimGraph");
	SourceGraph.Kind = TEXT("AnimGraph");
	FAssetDocumentNodeSpec NodeSpec = MakeRuntimeNode(TEXT("IdlePlayer"), FakeGraphNodeClassPath());
	AddManagedEditorNode(Context.Graph, SourceGraph, NodeSpec, 10, 20);

	FAssetDocumentGraphSpec ExtractedGraph;
	const FAssetDocumentCapabilityResult Result = Runtime.ExtractGraph(Context, ExtractedGraph);

	TestTrue(TEXT("Managed graph extract succeeds"), Result.bSuccess);
	TestEqual(TEXT("Managed node is extracted instead of shell skipped"), ExtractedGraph.Nodes.Num(), 1);
	TestFalse(TEXT("Managed-only graph does not emit shell _Skipped"), ExtractedGraph.UnderscoreSkipped.IsValid());
	return true;
}

#endif
