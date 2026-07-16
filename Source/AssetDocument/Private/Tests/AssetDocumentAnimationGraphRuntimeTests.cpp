// Copyright ProjectRPG. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Graphs/AssetDocumentGraphFieldRules.h"
#include "Graphs/AssetDocumentAnimationGraphNodeActionProvider.h"
#include "Graphs/AssetDocumentAnimationGraphRuntime.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationAsset.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectHash.h"

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

FAssetDocumentAnimationGraphNodeSpawnCandidate MakeCandidate(
	const FString& ClassPath,
	const FString& ActionKey,
	bool bSpawnable);
UEdGraphNode* AddUnmanagedPoseNode(
	UEdGraph* Graph,
	const FString& PinName,
	EEdGraphPinDirection PinDirection,
	int32 X = 0,
	int32 Y = 0);
UEdGraphPin* FindPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction);

class FFakeAnimationGraphCandidateProvider final : public IAssetDocumentAnimationGraphCandidateProvider
{
public:
	TArray<FAssetDocumentAnimationGraphNodeSpawnCandidate> Candidates;
	TMap<FString, TArray<FFakeAnimationGraphPinSpec>> PinsByNodeId;
	TMap<FString, UEdGraphNode*> SpawnedNodesById;
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
		const_cast<FFakeAnimationGraphCandidateProvider*>(this)->SpawnedNodesById.Add(NodeSpec.Id, OutNode);
		const_cast<FFakeAnimationGraphCandidateProvider*>(this)->LastSpawnedNode = OutNode;
		return FAssetDocumentCapabilityResult::Success();
	}
};

class FRealAnimGraphSideEffectCandidateProvider final : public IAssetDocumentAnimationGraphCandidateProvider
{
public:
	explicit FRealAnimGraphSideEffectCandidateProvider(UClass* InNodeClass)
		: NodeClass(InNodeClass)
	{
	}

	mutable TMap<FString, UEdGraphNode*> SpawnedNodesById;
	mutable UEdGraphPin* ManagedSourcePin = nullptr;
	mutable UEdGraphPin* FrameworkPin = nullptr;

	virtual TArray<FAssetDocumentAnimationGraphNodeSpawnCandidate> FindCandidates(
		const FAssetDocumentNodeSpec& NodeSpec,
		const FAssetDocumentAnimationGraphContext& Context) const override
	{
		TArray<FAssetDocumentAnimationGraphNodeSpawnCandidate> Candidates;
		if (NodeClass && NodeSpec.Class == NodeClass->GetPathName())
		{
			Candidates.Add(MakeCandidate(NodeClass->GetPathName(), FString(), true));
		}
		return Candidates;
	}

	virtual FAssetDocumentCapabilityResult SpawnNode(
		const FAssetDocumentGraphSpec& GraphSpec,
		const FAssetDocumentNodeSpec& NodeSpec,
		const FAssetDocumentAnimationGraphContext& Context,
		const FAssetDocumentAnimationGraphNodeSpawnCandidate& Candidate,
		UEdGraphNode*& OutNode) const override
	{
		UObject* Outer = Context.Graph ? static_cast<UObject*>(Context.Graph) : GetTransientPackage();
		OutNode = NodeClass ? NewObject<UEdGraphNode>(Outer, NodeClass) : nullptr;
		if (!OutNode)
		{
			return FAssetDocumentCapabilityResult::Failure(
				TEXT("Test provider could not spawn a real animation graph node."),
				TEXT("/Test"),
				TEXT("TestAnimGraphNodeSpawnFailed"));
		}
		if (Context.Graph)
		{
			Context.Graph->AddNode(OutNode, false, false);
		}
		SpawnedNodesById.Add(NodeSpec.Id, OutNode);

		if (NodeSpec.Id == TEXT("Target") && Context.Graph)
		{
			UEdGraphNode* const* SourceNode = SpawnedNodesById.Find(TEXT("Source"));
			ManagedSourcePin = SourceNode ? FindPosePin(*SourceNode, EGPD_Output) : nullptr;
			UEdGraphNode* FrameworkNode = AddUnmanagedPoseNode(Context.Graph, TEXT("FrameworkIn"), EGPD_Input, 800, 0);
			FrameworkPin = FrameworkNode ? FrameworkNode->FindPin(TEXT("FrameworkIn")) : nullptr;
			if (ManagedSourcePin && FrameworkPin)
			{
				ManagedSourcePin->MakeLinkTo(FrameworkPin);
			}
		}
		return FAssetDocumentCapabilityResult::Success();
	}

private:
	UClass* NodeClass = nullptr;
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
	int32 Y,
	UClass* NodeClass = UEdGraphNode::StaticClass())
{
	UEdGraphNode* Node = Graph
		? NewObject<UEdGraphNode>(
			Graph,
			NodeClass ? NodeClass : UEdGraphNode::StaticClass(),
			FName(*FAssetDocumentAnimationGraphRuntime::MakeManagedNodeObjectName(NodeSpec.Id)))
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

UAnimationGraph* CreateTransientAnimGraph(UAnimBlueprint*& OutAnimBlueprint)
{
	OutAnimBlueprint = NewObject<UAnimBlueprint>(GetTransientPackage());
	if (!OutAnimBlueprint)
	{
		return nullptr;
	}

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		OutAnimBlueprint,
		UEdGraphSchema_K2::GN_AnimGraph,
		UAnimationGraph::StaticClass(),
		UAnimationGraphSchema::StaticClass());
	FBlueprintEditorUtils::AddDomainSpecificGraph(OutAnimBlueprint, NewGraph);
	return Cast<UAnimationGraph>(NewGraph);
}

UEdGraph* CreateSchemaBackedGraph(TSubclassOf<UEdGraphSchema> SchemaClass)
{
	UEdGraph* Graph = NewObject<UEdGraph>(GetTransientPackage());
	if (Graph)
	{
		Graph->Schema = SchemaClass;
	}
	return Graph;
}

UEdGraphPin* FindFakePin(
	const TSharedRef<FFakeAnimationGraphCandidateProvider>& Provider,
	const FString& NodeId,
	const FString& PinName)
{
	UEdGraphNode** Node = Provider->SpawnedNodesById.Find(NodeId);
	return Node && *Node ? (*Node)->FindPin(FName(*PinName)) : nullptr;
}

UEdGraphPin* FindFakePinByDirection(
	const TSharedRef<FFakeAnimationGraphCandidateProvider>& Provider,
	const FString& NodeId,
	const FString& PinName,
	EEdGraphPinDirection Direction)
{
	UEdGraphNode** Node = Provider->SpawnedNodesById.Find(NodeId);
	if (!Node || !*Node)
	{
		return nullptr;
	}
	for (UEdGraphPin* Pin : (*Node)->Pins)
	{
		if (Pin && Pin->PinName.ToString() == PinName && Pin->Direction == Direction)
		{
			return Pin;
		}
	}
	return nullptr;
}

UEdGraphNode* AddUnmanagedNode(
	UEdGraph* Graph,
	const FString& PinName,
	EEdGraphPinDirection PinDirection,
	int32 X = 0,
	int32 Y = 0)
{
	UEdGraphNode* Node = Graph ? NewObject<UEdGraphNode>(Graph) : nullptr;
	if (!Node)
	{
		return nullptr;
	}
	Graph->AddNode(Node, false, false);
	Node->NodePosX = X;
	Node->NodePosY = Y;
	Node->CreatePin(PinDirection, TEXT("Wildcard"), FName(*PinName));
	return Node;
}

UEdGraphNode* AddUnmanagedPoseNode(
	UEdGraph* Graph,
	const FString& PinName,
	EEdGraphPinDirection PinDirection,
	int32 X,
	int32 Y)
{
	UEdGraphNode* Node = Graph ? NewObject<UEdGraphNode>(Graph) : nullptr;
	if (!Node)
	{
		return nullptr;
	}
	Graph->AddNode(Node, false, false);
	Node->NodePosX = X;
	Node->NodePosY = Y;
	Node->CreatePin(PinDirection, UAnimationGraphSchema::MakeLocalSpacePosePin(), FName(*PinName));
	return Node;
}

UEdGraphPin* FindPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
{
	if (!Node)
	{
		return nullptr;
	}
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == Direction && UAnimationGraphSchema::IsPosePin(Pin->PinType))
		{
			return Pin;
		}
	}
	return nullptr;
}

const TArray<TSharedPtr<FJsonValue>>* GetSkippedArray(
	const FAssetDocumentGraphSpec& Graph,
	const FString& FieldName)
{
	if (!Graph.UnderscoreSkipped.IsValid() || Graph.UnderscoreSkipped->Type != EJson::Object)
	{
		return nullptr;
	}

	const TSharedPtr<FJsonObject> SkippedObject = Graph.UnderscoreSkipped->AsObject();
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	return SkippedObject.IsValid() && SkippedObject->TryGetArrayField(FieldName, Values) ? Values : nullptr;
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
	FAssetDocumentAnimationGraphRuntimeMaterializesAuthoredLinksTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.MaterializesAuthoredLinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeMaterializesAuthoredLinksTest::RunTest(const FString&)
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
	Graph.Links.Add(MakeRuntimeLink(TEXT("Source"), TEXT("Out"), TEXT("Target"), TEXT("In")));

	const FAssetDocumentCapabilityResult Result = Runtime.ApplyGraph(Graph, Context, Hook);
	TestTrue(TEXT("ApplyGraph succeeds with authored link"), Result.bSuccess);

	UEdGraphPin* OutPin = FindFakePin(Provider, TEXT("Source"), TEXT("Out"));
	UEdGraphPin* InPin = FindFakePin(Provider, TEXT("Target"), TEXT("In"));
	TestNotNull(TEXT("Source output pin exists"), OutPin);
	TestNotNull(TEXT("Target input pin exists"), InPin);
	if (OutPin && InPin)
	{
		TestTrue(TEXT("Authored link is materialized on output pin"), OutPin->LinkedTo.Contains(InPin));
		TestTrue(TEXT("Authored link is materialized on input pin"), InPin->LinkedTo.Contains(OutPin));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphRuntimeFailedLinkApplyPreservesExistingLinksTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.FailedLinkApplyPreservesExistingLinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeFailedLinkApplyPreservesExistingLinksTest::RunTest(const FString&)
{
	TSharedRef<FFakeAnimationGraphCandidateProvider> Provider = MakeShared<FFakeAnimationGraphCandidateProvider>();
	Provider->Candidates.Add(MakeCandidate(FakeGraphNodeClassPath()));
	Provider->PinsByNodeId.Add(TEXT("Source"), { { TEXT("Out"), EGPD_Output } });
	Provider->PinsByNodeId.Add(TEXT("Target"), { { TEXT("In"), EGPD_Input } });

	FAssetDocumentAnimationGraphRuntime Runtime(Provider);
	FFakeAnimationGraphStructuralHook Hook;
	FAssetDocumentAnimationGraphContext Context;
	FAssetDocumentGraphSpec BaseGraph;
	BaseGraph.Id = TEXT("AnimGraph");
	BaseGraph.Kind = TEXT("AnimGraph");
	BaseGraph.Nodes.Add(MakeRuntimeNode(TEXT("Source"), FakeGraphNodeClassPath()));
	BaseGraph.Nodes.Add(MakeRuntimeNode(TEXT("Target"), FakeGraphNodeClassPath()));
	BaseGraph.Links.Add(MakeRuntimeLink(TEXT("Source"), TEXT("Out"), TEXT("Target"), TEXT("In")));

	const FAssetDocumentCapabilityResult BaseResult = Runtime.ApplyGraph(BaseGraph, Context, Hook);
	TestTrue(TEXT("Base apply succeeds"), BaseResult.bSuccess);
	UEdGraphPin* OutPin = FindFakePin(Provider, TEXT("Source"), TEXT("Out"));
	UEdGraphPin* InPin = FindFakePin(Provider, TEXT("Target"), TEXT("In"));
	TestTrue(TEXT("Base link exists before bad apply"), OutPin && InPin && OutPin->LinkedTo.Contains(InPin));
	UEdGraphNode* UnmanagedNode = AddUnmanagedNode(Hook.CreatedGraph, TEXT("FrameworkIn"), EGPD_Input);
	UEdGraphPin* FrameworkIn = UnmanagedNode ? UnmanagedNode->FindPin(TEXT("FrameworkIn")) : nullptr;
	if (OutPin && FrameworkIn)
	{
		OutPin->MakeLinkTo(FrameworkIn);
	}
	TestTrue(TEXT("Managed-to-unmanaged link exists before bad apply"), OutPin && FrameworkIn && OutPin->LinkedTo.Contains(FrameworkIn));

	FAssetDocumentGraphSpec BadGraph = BaseGraph;
	BadGraph.Links.Reset();
	BadGraph.Links.Add(MakeRuntimeLink(TEXT("Source"), TEXT("Out"), TEXT("Target"), TEXT("Missing")));
	const FAssetDocumentCapabilityResult BadResult = Runtime.ApplyGraph(BadGraph, Context, Hook);
	TestFalse(TEXT("Bad link apply fails"), BadResult.bSuccess);
	TestEqual(TEXT("Bad apply fails on unresolved endpoint"), FirstDiagnosticCode(BadResult), FString(TEXT("UnresolvedGraphLinkEndpoint")));
	TestTrue(TEXT("Failed link apply preserves existing output link"), OutPin && InPin && OutPin->LinkedTo.Contains(InPin));
	TestTrue(TEXT("Failed link apply preserves existing input link"), OutPin && InPin && InPin->LinkedTo.Contains(OutPin));
	TestTrue(TEXT("Failed link apply preserves managed-to-unmanaged output link"), OutPin && FrameworkIn && OutPin->LinkedTo.Contains(FrameworkIn));
	TestTrue(TEXT("Failed link apply preserves managed-to-unmanaged input link"), OutPin && FrameworkIn && FrameworkIn->LinkedTo.Contains(OutPin));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphRuntimeApplyPreservesManagedToUnmanagedLinksTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.ApplyPreservesManagedToUnmanagedLinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeApplyPreservesManagedToUnmanagedLinksTest::RunTest(const FString&)
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

	const FAssetDocumentCapabilityResult BaseResult = Runtime.ApplyGraph(Graph, Context, Hook);
	TestTrue(TEXT("Base apply creates managed nodes"), BaseResult.bSuccess);

	UEdGraphPin* SourceOut = FindFakePin(Provider, TEXT("Source"), TEXT("Out"));
	UEdGraphNode* UnmanagedNode = AddUnmanagedNode(Hook.CreatedGraph, TEXT("FrameworkIn"), EGPD_Input);
	UEdGraphPin* FrameworkIn = UnmanagedNode ? UnmanagedNode->FindPin(TEXT("FrameworkIn")) : nullptr;
	if (SourceOut && FrameworkIn)
	{
		SourceOut->MakeLinkTo(FrameworkIn);
	}
	TestTrue(TEXT("Managed-to-unmanaged link exists before managed apply"), SourceOut && FrameworkIn && SourceOut->LinkedTo.Contains(FrameworkIn));

	Graph.Links.Add(MakeRuntimeLink(TEXT("Source"), TEXT("Out"), TEXT("Target"), TEXT("In")));
	const FAssetDocumentCapabilityResult ApplyResult = Runtime.ApplyGraph(Graph, Context, Hook);
	TestTrue(TEXT("Managed-managed apply succeeds"), ApplyResult.bSuccess);
	TestTrue(TEXT("Managed-to-unmanaged output link is preserved"), SourceOut && FrameworkIn && SourceOut->LinkedTo.Contains(FrameworkIn));
	TestTrue(TEXT("Managed-to-unmanaged input link is preserved"), SourceOut && FrameworkIn && FrameworkIn->LinkedTo.Contains(SourceOut));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphRuntimeExtractSkipsManagedToUnmanagedLinksTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.ExtractSkipsManagedToUnmanagedLinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeExtractSkipsManagedToUnmanagedLinksTest::RunTest(const FString&)
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
	UEdGraphNode* SourceNode = AddManagedEditorNode(Context.Graph, SourceGraph, SourceSpec, 0, 0);
	UEdGraphPin* SourceOut = SourceNode ? SourceNode->CreatePin(EGPD_Output, TEXT("Wildcard"), TEXT("Out")) : nullptr;
	UEdGraphNode* UnmanagedNode = AddUnmanagedNode(Context.Graph, TEXT("FrameworkIn"), EGPD_Input, 200, 0);
	UEdGraphPin* FrameworkIn = UnmanagedNode ? UnmanagedNode->FindPin(TEXT("FrameworkIn")) : nullptr;
	if (SourceOut && FrameworkIn)
	{
		SourceOut->MakeLinkTo(FrameworkIn);
	}

	FAssetDocumentGraphSpec ExtractedGraph;
	const FAssetDocumentCapabilityResult Result = Runtime.ExtractGraph(Context, ExtractedGraph);
	TestTrue(TEXT("Extract succeeds with managed-to-unmanaged link"), Result.bSuccess);
	TestEqual(TEXT("Managed-to-unmanaged link is not emitted as authored link"), ExtractedGraph.Links.Num(), 0);
	const TArray<TSharedPtr<FJsonValue>>* SkippedLinks = GetSkippedArray(ExtractedGraph, TEXT("Links"));
	TestNotNull(TEXT("Managed-to-unmanaged link emits _Skipped.Links"), SkippedLinks);
	if (SkippedLinks && SkippedLinks->Num() == 1)
	{
		const TSharedPtr<FJsonObject> SkippedLink = (*SkippedLinks)[0]->AsObject();
		TestEqual(TEXT("Skipped link reason is explicit"), SkippedLink->GetStringField(TEXT("Reason")), FString(TEXT("UnmanagedGraphLinkEndpoint")));
		TestEqual(TEXT("Skipped link records managed node"), SkippedLink->GetStringField(TEXT("ManagedNode")), FString(TEXT("Source")));
		TestEqual(TEXT("Skipped link records unmanaged pin"), SkippedLink->GetStringField(TEXT("UnmanagedPin")), FString(TEXT("FrameworkIn")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphRuntimeExtractSkipsAmbiguousManagedPinLinksTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.ExtractSkipsAmbiguousManagedPinLinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeExtractSkipsAmbiguousManagedPinLinksTest::RunTest(const FString&)
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
	UEdGraphNode* SourceNode = AddManagedEditorNode(Context.Graph, SourceGraph, SourceSpec, 0, 0);
	UEdGraphNode* TargetNode = AddManagedEditorNode(Context.Graph, SourceGraph, TargetSpec, 200, 0);
	UEdGraphPin* AmbiguousOut = SourceNode ? SourceNode->CreatePin(EGPD_Output, TEXT("Wildcard"), TEXT("Pose")) : nullptr;
	if (SourceNode)
	{
		SourceNode->CreatePin(EGPD_Output, TEXT("Wildcard"), TEXT("Pose"));
	}
	UEdGraphPin* TargetIn = TargetNode ? TargetNode->CreatePin(EGPD_Input, TEXT("Wildcard"), TEXT("In")) : nullptr;
	if (AmbiguousOut && TargetIn)
	{
		AmbiguousOut->MakeLinkTo(TargetIn);
	}

	FAssetDocumentGraphSpec ExtractedGraph;
	const FAssetDocumentCapabilityResult Result = Runtime.ExtractGraph(Context, ExtractedGraph);
	TestTrue(TEXT("Extract succeeds with ambiguous managed pin"), Result.bSuccess);
	TestEqual(TEXT("Ambiguous managed pin link is not emitted as authored link"), ExtractedGraph.Links.Num(), 0);
	const TArray<TSharedPtr<FJsonValue>>* SkippedLinks = GetSkippedArray(ExtractedGraph, TEXT("Links"));
	TestNotNull(TEXT("Ambiguous managed pin link emits _Skipped.Links"), SkippedLinks);
	if (SkippedLinks && SkippedLinks->Num() == 1)
	{
		const TSharedPtr<FJsonObject> SkippedLink = (*SkippedLinks)[0]->AsObject();
		TestEqual(TEXT("Skipped ambiguous pin reason is explicit"), SkippedLink->GetStringField(TEXT("Reason")), FString(TEXT("AmbiguousManagedGraphPin")));
		TestEqual(TEXT("Skipped ambiguous pin records node"), SkippedLink->GetStringField(TEXT("ManagedNode")), FString(TEXT("Source")));
		TestEqual(TEXT("Skipped ambiguous pin records pin"), SkippedLink->GetStringField(TEXT("ManagedPin")), FString(TEXT("Pose")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphRuntimeExtractedSameNameOppositeDirectionPinsApplyRoundtripTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.ExtractedSameNameOppositeDirectionPinsApplyRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeExtractedSameNameOppositeDirectionPinsApplyRoundtripTest::RunTest(const FString&)
{
	FAssetDocumentAnimationGraphRuntime ExtractRuntime;
	FAssetDocumentAnimationGraphContext ExtractContext;
	ExtractContext.GraphKind = TEXT("AnimGraph");
	ExtractContext.GraphPath = TEXT("/Body/AnimGraph/Graphs/AnimGraph");
	ExtractContext.Graph = NewObject<UEdGraph>(GetTransientPackage());

	FAssetDocumentGraphSpec SourceGraph;
	SourceGraph.Id = TEXT("AnimGraph");
	SourceGraph.Kind = TEXT("AnimGraph");
	FAssetDocumentNodeSpec SourceSpec = MakeRuntimeNode(TEXT("Source"), FakeGraphNodeClassPath());
	FAssetDocumentNodeSpec TargetSpec = MakeRuntimeNode(TEXT("Target"), FakeGraphNodeClassPath());
	UEdGraphNode* SourceNode = AddManagedEditorNode(ExtractContext.Graph, SourceGraph, SourceSpec, 0, 0);
	UEdGraphNode* TargetNode = AddManagedEditorNode(ExtractContext.Graph, SourceGraph, TargetSpec, 200, 0);
	if (SourceNode)
	{
		SourceNode->CreatePin(EGPD_Input, TEXT("Wildcard"), TEXT("Pose"));
	}
	UEdGraphPin* SourceOut = SourceNode ? SourceNode->CreatePin(EGPD_Output, TEXT("Wildcard"), TEXT("Pose")) : nullptr;
	UEdGraphPin* TargetIn = TargetNode ? TargetNode->CreatePin(EGPD_Input, TEXT("Wildcard"), TEXT("Pose")) : nullptr;
	if (TargetNode)
	{
		TargetNode->CreatePin(EGPD_Output, TEXT("Wildcard"), TEXT("Pose"));
	}
	if (SourceOut && TargetIn)
	{
		SourceOut->MakeLinkTo(TargetIn);
	}

	FAssetDocumentGraphSpec ExtractedGraph;
	const FAssetDocumentCapabilityResult ExtractResult = ExtractRuntime.ExtractGraph(ExtractContext, ExtractedGraph);
	TestTrue(TEXT("Extract succeeds with same-name opposite-direction pins"), ExtractResult.bSuccess);
	TestEqual(TEXT("Direction-unique same-name pin link is emitted"), ExtractedGraph.Links.Num(), 1);
	if (ExtractedGraph.Links.Num() == 1)
	{
		TestEqual(TEXT("Extracted source pin keeps semantic name"), ExtractedGraph.Links[0].From.Pin, FString(TEXT("Pose")));
		TestEqual(TEXT("Extracted target pin keeps semantic name"), ExtractedGraph.Links[0].To.Pin, FString(TEXT("Pose")));
	}

	TSharedRef<FFakeAnimationGraphCandidateProvider> Provider = MakeShared<FFakeAnimationGraphCandidateProvider>();
	Provider->Candidates.Add(MakeCandidate(FakeGraphNodeClassPath()));
	Provider->PinsByNodeId.Add(TEXT("Source"), { { TEXT("Pose"), EGPD_Input }, { TEXT("Pose"), EGPD_Output } });
	Provider->PinsByNodeId.Add(TEXT("Target"), { { TEXT("Pose"), EGPD_Input }, { TEXT("Pose"), EGPD_Output } });

	FAssetDocumentAnimationGraphRuntime ApplyRuntime(Provider);
	FFakeAnimationGraphStructuralHook Hook;
	FAssetDocumentAnimationGraphContext ApplyContext;
	const FAssetDocumentCapabilityResult ApplyResult = ApplyRuntime.ApplyGraph(ExtractedGraph, ApplyContext, Hook);
	TestTrue(TEXT("Apply accepts extract-authored same-name opposite-direction pin link"), ApplyResult.bSuccess);
	TestNotEqual(TEXT("Apply does not report ambiguous pin"), FirstDiagnosticCode(ApplyResult), FString(TEXT("AmbiguousGraphLinkEndpointPin")));

	UEdGraphPin* AppliedSourceOut = FindFakePinByDirection(Provider, TEXT("Source"), TEXT("Pose"), EGPD_Output);
	UEdGraphPin* AppliedTargetIn = FindFakePinByDirection(Provider, TEXT("Target"), TEXT("Pose"), EGPD_Input);
	TestTrue(
		TEXT("Apply materializes direction-resolved same-name link"),
		AppliedSourceOut && AppliedTargetIn && AppliedSourceOut->LinkedTo.Contains(AppliedTargetIn));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphRuntimeSchemaBackedGraphRejectRollbackPreservesLinksTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.SchemaBackedGraphRejectRollbackPreservesLinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeSchemaBackedGraphRejectRollbackPreservesLinksTest::RunTest(const FString&)
{
	TSharedRef<FFakeAnimationGraphCandidateProvider> Provider = MakeShared<FFakeAnimationGraphCandidateProvider>();
	Provider->Candidates.Add(MakeCandidate(FakeGraphNodeClassPath()));
	Provider->PinsByNodeId.Add(TEXT("Source"), { { TEXT("Out"), EGPD_Output } });
	Provider->PinsByNodeId.Add(TEXT("Target"), { { TEXT("In"), EGPD_Input } });

	UEdGraph* GraphObject = CreateSchemaBackedGraph(UEdGraphSchema::StaticClass());
	TestNotNull(TEXT("Schema-backed graph is created"), GraphObject);
	TestNotNull(TEXT("Schema-backed graph resolves schema"), GraphObject ? GraphObject->GetSchema() : nullptr);

	FAssetDocumentAnimationGraphRuntime Runtime(Provider);
	FFakeAnimationGraphStructuralHook Hook;
	FAssetDocumentAnimationGraphContext Context;
	Context.Graph = GraphObject;
	FAssetDocumentGraphSpec Graph;
	Graph.Id = TEXT("AnimGraph");
	Graph.Kind = TEXT("AnimGraph");
	Graph.Nodes.Add(MakeRuntimeNode(TEXT("Source"), FakeGraphNodeClassPath()));
	Graph.Nodes.Add(MakeRuntimeNode(TEXT("Target"), FakeGraphNodeClassPath()));

	const FAssetDocumentCapabilityResult BaseResult = Runtime.ApplyGraph(Graph, Context, Hook);
	TestTrue(TEXT("Schema-backed base apply succeeds"), BaseResult.bSuccess);
	UEdGraphPin* SourceOut = FindFakePin(Provider, TEXT("Source"), TEXT("Out"));
	UEdGraphPin* TargetIn = FindFakePin(Provider, TEXT("Target"), TEXT("In"));
	if (SourceOut && TargetIn)
	{
		SourceOut->MakeLinkTo(TargetIn);
	}
	TestTrue(TEXT("Existing schema-backed managed link is present before reapply"), SourceOut && TargetIn && SourceOut->LinkedTo.Contains(TargetIn));

	FAssetDocumentGraphSpec ReapplyGraph = Graph;
	ReapplyGraph.Links.Add(MakeRuntimeLink(TEXT("Source"), TEXT("Out"), TEXT("Target"), TEXT("In")));
	const FAssetDocumentCapabilityResult Result = Runtime.ApplyGraph(ReapplyGraph, Context, Hook);
	TestFalse(TEXT("Schema-backed authored link is rejected by schema"), Result.bSuccess);
	TestEqual(TEXT("Schema-backed rejection reports invalid link type"), FirstDiagnosticCode(Result), FString(TEXT("InvalidGraphLinkType")));
	TestTrue(TEXT("Schema-backed rollback preserves output link"), SourceOut && TargetIn && SourceOut->LinkedTo.Contains(TargetIn));
	TestTrue(TEXT("Schema-backed rollback preserves input link"), SourceOut && TargetIn && TargetIn->LinkedTo.Contains(SourceOut));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphRuntimeAnimSchemaRejectsLinksThatWouldBreakUnmanagedEndpointTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.AnimSchemaRejectsLinksThatWouldBreakUnmanagedEndpoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeAnimSchemaRejectsLinksThatWouldBreakUnmanagedEndpointTest::RunTest(const FString&)
{
	UAnimBlueprint* AnimBlueprint = nullptr;
	UAnimationGraph* AnimGraph = CreateTransientAnimGraph(AnimBlueprint);
	TestNotNull(TEXT("Transient AnimBlueprint is created"), AnimBlueprint);
	TestNotNull(TEXT("Real animation editor graph is created"), AnimGraph);
	TestNotNull(TEXT("Animation graph resolves real schema"), AnimGraph ? AnimGraph->GetSchema() : nullptr);

	UClass* AnimGraphNodeClass = StaticLoadClass(
		UEdGraphNode::StaticClass(),
		nullptr,
		TEXT("/Script/AnimGraph.AnimGraphNode_BlendListByBool"));
	TestNotNull(TEXT("Real AnimGraphNode class loads dynamically"), AnimGraphNodeClass);
	if (!AnimBlueprint || !AnimGraph || !AnimGraphNodeClass)
	{
		return false;
	}

	FAssetDocumentGraphSpec Graph;
	Graph.Id = TEXT("AnimGraph");
	Graph.Kind = TEXT("AnimGraph");
	FAssetDocumentNodeSpec SourceSpec = MakeRuntimeNode(TEXT("Source"), AnimGraphNodeClass->GetPathName());
	FAssetDocumentNodeSpec TargetSpec = MakeRuntimeNode(TEXT("Target"), AnimGraphNodeClass->GetPathName());
	Graph.Nodes.Add(SourceSpec);
	Graph.Nodes.Add(TargetSpec);

	UAnimBlueprint* TemplateBlueprint = nullptr;
	UAnimationGraph* TemplateGraph = CreateTransientAnimGraph(TemplateBlueprint);
	TestNotNull(TEXT("Template animation editor graph is created"), TemplateGraph);
	if (!TemplateGraph)
	{
		return false;
	}
	UEdGraphNode* SourceTemplate = NewObject<UEdGraphNode>(TemplateGraph, AnimGraphNodeClass);
	UEdGraphNode* TargetTemplate = NewObject<UEdGraphNode>(TemplateGraph, AnimGraphNodeClass);
	TestNotNull(TEXT("Source template real anim node is created"), SourceTemplate);
	TestNotNull(TEXT("Target template real anim node is created"), TargetTemplate);
	if (SourceTemplate)
	{
		TemplateGraph->AddNode(SourceTemplate, false, false);
		SourceTemplate->AllocateDefaultPins();
	}
	if (TargetTemplate)
	{
		TemplateGraph->AddNode(TargetTemplate, false, false);
		TargetTemplate->AllocateDefaultPins();
	}
	UEdGraphPin* SourceTemplateOut = FindPosePin(SourceTemplate, EGPD_Output);
	UEdGraphPin* TargetTemplateIn = FindPosePin(TargetTemplate, EGPD_Input);
	TestNotNull(TEXT("Source template has pose output"), SourceTemplateOut);
	TestNotNull(TEXT("Target template has pose input"), TargetTemplateIn);
	if (!SourceTemplateOut || !TargetTemplateIn)
	{
		return false;
	}
	Graph.Links.Add(MakeRuntimeLink(
		TEXT("Source"),
		SourceTemplateOut->PinName.ToString(),
		TEXT("Target"),
		TargetTemplateIn->PinName.ToString()));

	TSharedRef<FRealAnimGraphSideEffectCandidateProvider> Provider =
		MakeShared<FRealAnimGraphSideEffectCandidateProvider>(AnimGraphNodeClass);
	FAssetDocumentAnimationGraphRuntime Runtime(Provider);
	FFakeAnimationGraphStructuralHook Hook;
	FAssetDocumentAnimationGraphContext Context;
	Context.Asset = AnimBlueprint;
	Context.Blueprint = AnimBlueprint;
	Context.Graph = AnimGraph;
	Context.GraphKind = TEXT("AnimGraph");
	Context.GraphPath = TEXT("/Body/AnimGraph/Graphs/AnimGraph");

	const FAssetDocumentCapabilityResult Result = Runtime.ApplyGraph(Graph, Context, Hook);
	UEdGraphNode* const* SpawnedTargetNode = Provider->SpawnedNodesById.Find(TEXT("Target"));
	UEdGraphPin* SourceOut = Provider->ManagedSourcePin;
	UEdGraphPin* TargetIn = SpawnedTargetNode ? FindPosePin(*SpawnedTargetNode, EGPD_Input) : nullptr;
	UEdGraphPin* FrameworkIn = Provider->FrameworkPin;
	TestNotNull(TEXT("Source real anim node has pose output"), SourceOut);
	TestNotNull(TEXT("Target real anim node has pose input"), TargetIn);
	TestNotNull(TEXT("Provider attached unmanaged framework endpoint"), FrameworkIn);
	if (!SourceOut || !TargetIn)
	{
		return false;
	}

	TestFalse(TEXT("Schema-backed apply rejects authored link that would break unmanaged endpoint"), Result.bSuccess);
	TestEqual(
		TEXT("Break-unmanaged rejection diagnostic code is explicit"),
		FirstDiagnosticCode(Result),
		FString(TEXT("GraphLinkWouldBreakUnmanagedEndpoint")));
	TestEqual(
		TEXT("Break-unmanaged rejection diagnostic path is the authored link"),
		FirstDiagnosticPath(Result),
		FString::Printf(
			TEXT("/Body/AnimGraph/Graphs/AnimGraph/Links/Source.%s->Target.%s"),
			*SourceTemplateOut->PinName.ToString(),
			*TargetTemplateIn->PinName.ToString()));
	TestTrue(
		TEXT("Rejected schema apply preserves managed-to-unmanaged output link"),
		SourceOut->LinkedTo.Contains(FrameworkIn));
	TestTrue(
		TEXT("Rejected schema apply preserves unmanaged-to-managed input link"),
		FrameworkIn && FrameworkIn->LinkedTo.Contains(SourceOut));
	TestFalse(
		TEXT("Rejected schema apply does not create authored managed-managed link"),
		SourceOut->LinkedTo.Contains(TargetIn));
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
	FAssetDocumentAnimationGraphRuntimeExtractsRealAnimGraphNodePositionTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.ExtractsRealAnimGraphNodePosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphRuntimeExtractsRealAnimGraphNodePositionTest::RunTest(const FString&)
{
	UAnimBlueprint* AnimBlueprint = nullptr;
	UAnimationGraph* AnimGraph = CreateTransientAnimGraph(AnimBlueprint);
	TestNotNull(TEXT("Transient AnimBlueprint is created"), AnimBlueprint);
	TestNotNull(TEXT("Real animation editor graph is created"), AnimGraph);

	UClass* AnimGraphNodeClass = StaticLoadClass(
		UEdGraphNode::StaticClass(),
		nullptr,
		TEXT("/Script/AnimGraph.AnimGraphNode_BlendListByBool"));
	TestNotNull(TEXT("Real AnimGraphNode class loads dynamically"), AnimGraphNodeClass);
	if (!AnimBlueprint || !AnimGraph || !AnimGraphNodeClass)
	{
		return false;
	}

	FAssetDocumentGraphSpec SourceGraph;
	SourceGraph.Id = TEXT("AnimGraph");
	SourceGraph.Kind = TEXT("AnimGraph");
	FAssetDocumentNodeSpec NodeSpec = MakeRuntimeNode(TEXT("BlendByBool"), AnimGraphNodeClass->GetPathName());
	UEdGraphNode* Node = AddManagedEditorNode(AnimGraph, SourceGraph, NodeSpec, 512, -128, AnimGraphNodeClass);
	TestNotNull(TEXT("Real AnimGraphNode instance is created"), Node);
	if (Node)
	{
		Node->AllocateDefaultPins();
	}

	FAssetDocumentAnimationGraphContext Context;
	Context.Asset = AnimBlueprint;
	Context.Blueprint = AnimBlueprint;
	Context.Graph = AnimGraph;
	Context.GraphKind = TEXT("AnimGraph");
	Context.GraphPath = TEXT("/Body/AnimGraph/Graphs/AnimGraph");

	FAssetDocumentAnimationGraphRuntime Runtime;
	FAssetDocumentGraphSpec ExtractedGraph;
	const FAssetDocumentCapabilityResult Result = Runtime.ExtractGraph(Context, ExtractedGraph);

	TestTrue(TEXT("Real anim graph extract succeeds"), Result.bSuccess);
	TestEqual(TEXT("One managed real anim node is extracted"), ExtractedGraph.Nodes.Num(), 1);
	if (ExtractedGraph.Nodes.Num() == 1)
	{
		const FAssetDocumentNodeSpec& ExtractedNode = ExtractedGraph.Nodes[0];
		TestEqual(TEXT("Managed id is extracted from real anim node"), ExtractedNode.Id, FString(TEXT("BlendByBool")));
		TestEqual(TEXT("Real anim node class is extracted"), ExtractedNode.Class, AnimGraphNodeClass->GetPathName());
		double X = 0.0;
		double Y = 0.0;
		TestTrue(TEXT("Real anim node position is extracted"), ExtractedNode.Position.IsValid());
		if (ExtractedNode.Position.IsValid())
		{
			ExtractedNode.Position->TryGetNumberField(TEXT("X"), X);
			ExtractedNode.Position->TryGetNumberField(TEXT("Y"), Y);
		}
		TestEqual(TEXT("Real anim node position X roundtrips"), X, 512.0);
		TestEqual(TEXT("Real anim node position Y roundtrips"), Y, -128.0);
		TestTrue(TEXT("Real anim node evidence is extracted"), ExtractedNode.Evidence.IsValid());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimationGraphNodeActionProviderTransientGraphFallbackTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules.TransientGraphUsesClassFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimationGraphNodeActionProviderTransientGraphFallbackTest::RunTest(const FString&)
{
	const FName BlueprintName = MakeUniqueObjectName(
		GetTransientPackage(),
		UAnimBlueprint::StaticClass(),
		TEXT("AssetDocumentTransientActionProviderTest"));
	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		UAnimInstance::StaticClass(),
		GetTransientPackage(),
		BlueprintName,
		BPTYPE_Normal,
		UAnimBlueprint::StaticClass(),
		UAnimBlueprintGeneratedClass::StaticClass()));
	UAnimationGraph* AnimGraph = nullptr;
	if (AnimBlueprint)
	{
		UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
			AnimBlueprint,
			UEdGraphSchema_K2::GN_AnimGraph,
			UAnimationGraph::StaticClass(),
			UAnimationGraphSchema::StaticClass());
		FBlueprintEditorUtils::AddDomainSpecificGraph(AnimBlueprint, NewGraph);
		AnimGraph = Cast<UAnimationGraph>(NewGraph);
		FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
	}
	TestNotNull(TEXT("Transient AnimBlueprint is created"), AnimBlueprint);
	TestNotNull(TEXT("Transient AnimGraph is created"), AnimGraph);
	if (!AnimBlueprint || !AnimGraph)
	{
		return false;
	}

	AnimBlueprint->SetFlags(RF_Transient);
	TArray<UObject*> OwnedObjects;
	GetObjectsWithOuter(AnimBlueprint, OwnedObjects, true);
	for (UObject* OwnedObject : OwnedObjects)
	{
		if (OwnedObject)
		{
			OwnedObject->SetFlags(RF_Transient);
		}
	}

	UClass* SequencePlayerClass = StaticLoadClass(
		UEdGraphNode::StaticClass(),
		nullptr,
		TEXT("/Script/AnimGraph.AnimGraphNode_SequencePlayer"));
	TestNotNull(TEXT("Sequence player class loads"), SequencePlayerClass);
	if (!SequencePlayerClass)
	{
		return false;
	}

	FAssetDocumentAnimationGraphContext Context;
	Context.Asset = AnimBlueprint;
	Context.Blueprint = AnimBlueprint;
	Context.Graph = AnimGraph;
	Context.GraphKind = TEXT("AnimGraph");
	Context.GraphPath = TEXT("/Body/AnimGraph/Graphs/AnimGraph");

	const FAssetDocumentNodeSpec NodeSpec = MakeRuntimeNode(TEXT("IdlePlayer"), SequencePlayerClass->GetPathName());
	const FAssetDocumentAnimationGraphNodeActionProvider Provider;
	const TArray<FAssetDocumentAnimationGraphNodeActionCandidate> Candidates =
		Provider.FindActionCandidates(NodeSpec, Context);

	TestEqual(TEXT("Transient graph resolves one class fallback candidate"), Candidates.Num(), 1);
	if (Candidates.Num() == 1)
	{
		TestTrue(TEXT("Transient graph candidate is the class fallback"), Candidates[0].bFallback);
		TestEqual(TEXT("Fallback candidate preserves class path"), Candidates[0].Candidate.ClassPath, NodeSpec.Class);
		TestTrue(TEXT("Fallback candidate remains spawnable"), Candidates[0].Candidate.bSpawnable);

		FAssetDocumentGraphSpec GraphSpec;
		GraphSpec.Id = TEXT("AnimGraph");
		GraphSpec.Kind = TEXT("AnimGraph");
		UEdGraphNode* SpawnedNode = nullptr;
		const FAssetDocumentCapabilityResult SpawnResult =
			Provider.SpawnNode(GraphSpec, NodeSpec, Context, Candidates[0].Candidate, SpawnedNode);
		TestTrue(TEXT("Fallback candidate spawns in transient graph"), SpawnResult.bSuccess);
		TestNotNull(TEXT("Fallback candidate produces an AnimGraph node"), SpawnedNode);
	}
	return true;
}

#endif
