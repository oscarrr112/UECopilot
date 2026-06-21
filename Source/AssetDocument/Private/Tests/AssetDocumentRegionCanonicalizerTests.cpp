// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentRegionCanonicalizer.h"
#include "AssetDocumentCanonicalJson.h"
#include "AssetDocumentPolicy.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
TSharedPtr<FJsonValue> MakeObjectValue(TSharedRef<FJsonObject> Object)
{
	return MakeShared<FJsonValueObject>(Object);
}

TSharedPtr<FJsonObject> MakeGraphMember(const TCHAR* Kind, const TCHAR* OwnerClass, const TCHAR* Name)
{
	TSharedPtr<FJsonObject> Member = MakeShared<FJsonObject>();
	Member->SetStringField(TEXT("Kind"), Kind);
	Member->SetStringField(TEXT("OwnerClass"), OwnerClass);
	Member->SetStringField(TEXT("Name"), Name);
	return Member;
}

TSharedPtr<FJsonObject> MakeGraphPosition(double X, double Y)
{
	TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
	Position->SetNumberField(TEXT("X"), X);
	Position->SetNumberField(TEXT("Y"), Y);
	return Position;
}

TSharedPtr<FJsonObject> MakeGraphPinOverride(const TCHAR* Pin, const TCHAR* Direction, const TCHAR* DefaultValue)
{
	TSharedPtr<FJsonObject> PinOverride = MakeShared<FJsonObject>();
	PinOverride->SetStringField(TEXT("Pin"), Pin);
	PinOverride->SetStringField(TEXT("Direction"), Direction);
	PinOverride->SetStringField(TEXT("DefaultValue"), DefaultValue);
	return PinOverride;
}

TSharedPtr<FJsonObject> MakeGraphNode(
	const TCHAR* Id,
	const TCHAR* Class,
	TSharedPtr<FJsonObject> Member,
	const TCHAR* NodeGuid = nullptr,
	const TCHAR* Capability = nullptr)
{
	TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("Id"), Id);
	Node->SetStringField(TEXT("Class"), Class);
	if (Member.IsValid())
	{
		Node->SetObjectField(TEXT("Member"), Member);
	}
	if (NodeGuid)
	{
		Node->SetStringField(TEXT("NodeGuid"), NodeGuid);
	}
	if (Capability)
	{
		Node->SetStringField(TEXT("Capability"), Capability);
	}
	Node->SetObjectField(TEXT("Position"), MakeGraphPosition(160.0, 320.0));
	Node->SetStringField(TEXT("Comment"), TEXT("keep authored comment"));

	TArray<TSharedPtr<FJsonValue>> PinOverrides;
	PinOverrides.Add(MakeObjectValue(MakeGraphPinOverride(TEXT("Message"), TEXT("Input"), TEXT("Hello")).ToSharedRef()));
	Node->SetArrayField(TEXT("PinOverrides"), MoveTemp(PinOverrides));
	return Node;
}

TSharedPtr<FJsonObject> MakeGraphEndpoint(const TCHAR* Node, const TCHAR* Pin)
{
	TSharedPtr<FJsonObject> Endpoint = MakeShared<FJsonObject>();
	Endpoint->SetStringField(TEXT("Node"), Node);
	Endpoint->SetStringField(TEXT("Pin"), Pin);
	return Endpoint;
}

TSharedPtr<FJsonObject> MakeGraphLink(const TCHAR* FromNode, const TCHAR* FromPin, const TCHAR* ToNode, const TCHAR* ToPin)
{
	TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
	Link->SetObjectField(TEXT("From"), MakeGraphEndpoint(FromNode, FromPin));
	Link->SetObjectField(TEXT("To"), MakeGraphEndpoint(ToNode, ToPin));
	return Link;
}

TSharedPtr<FJsonValue> MakeGraphRegionValue(
	const TCHAR* EventNodeId,
	const TCHAR* CallNodeId,
	bool bIncludeGeneratedMetadata)
{
	TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();
	Graph->SetStringField(TEXT("Name"), TEXT("EventGraph"));
	Graph->SetStringField(TEXT("Schema"), TEXT("K2"));
	if (bIncludeGeneratedMetadata)
	{
		Graph->SetStringField(TEXT("GraphGuid"), TEXT("E0B14B7C4E0F4F0BA0E5E4D600000001"));
	}

	TArray<TSharedPtr<FJsonValue>> Nodes;
	Nodes.Add(MakeObjectValue(MakeGraphNode(
		EventNodeId,
		TEXT("/Script/BlueprintGraph.K2Node_Event"),
		MakeGraphMember(TEXT("Event"), TEXT("/Script/Engine.Actor"), TEXT("ReceiveBeginPlay")),
		bIncludeGeneratedMetadata ? TEXT("E0B14B7C4E0F4F0BA0E5E4D600000002") : nullptr,
		bIncludeGeneratedMetadata ? TEXT("Event") : nullptr).ToSharedRef()));
	Nodes.Add(MakeObjectValue(MakeGraphNode(
		CallNodeId,
		TEXT("/Script/BlueprintGraph.K2Node_CallFunction"),
		MakeGraphMember(TEXT("Function"), TEXT("/Script/Engine.KismetSystemLibrary"), TEXT("PrintString")),
		bIncludeGeneratedMetadata ? TEXT("E0B14B7C4E0F4F0BA0E5E4D600000003") : nullptr,
		bIncludeGeneratedMetadata ? TEXT("CallFunction") : nullptr).ToSharedRef()));
	Graph->SetArrayField(TEXT("Nodes"), MoveTemp(Nodes));

	TArray<TSharedPtr<FJsonValue>> Links;
	Links.Add(MakeObjectValue(MakeGraphLink(EventNodeId, TEXT("Then"), CallNodeId, TEXT("execute")).ToSharedRef()));
	Graph->SetArrayField(TEXT("Links"), MoveTemp(Links));

	TArray<TSharedPtr<FJsonValue>> Graphs;
	Graphs.Add(MakeObjectValue(Graph.ToSharedRef()));
	return MakeShared<FJsonValueArray>(MoveTemp(Graphs));
}

FAssetDocumentRegionPolicy MakeAnimSequencePostApplyPolicy()
{
	FAssetDocumentRegionPolicy Policy;
	Policy.RegionId = TEXT("Body.Metadata");
	Policy.BodyPath = TEXT("Body.Metadata");
	Policy.CanonicalizerHookName = TEXT("AnimSequencePostApply");
	return Policy;
}

FString HashAnimSequencePostApplyRegion(
	const FAssetDocumentRegionPolicy& Policy,
	const TSharedPtr<FJsonValue>& Value)
{
	FAssetDocumentRegionCanonicalizeContext Context;
	Context.Policy = &Policy;
	Context.Source = EAssetDocumentRegionCanonicalizeSource::AssetEvidence;
	return FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, Value);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerIdentityHashMatchesCanonicalJsonTest,
	"AssetDocument.RegionCanonicalizer.IdentityHashMatchesCanonicalJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerIdentityHashMatchesCanonicalJsonTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionPolicy Policy;
	Policy.RegionId = TEXT("Body.Blend");
	Policy.BodyPath = TEXT("Body.Blend");
	Policy.ExtractOnlyFields.Add(TEXT("_ProjectionMetrics"));

	TSharedRef<FJsonObject> BaselineObject = MakeShared<FJsonObject>();
	BaselineObject->SetNumberField(TEXT("Value"), 1.0);

	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("Value"), 1.0);
	Object->SetStringField(TEXT("_ProjectionMetrics"), TEXT("diagnostic"));
	Object->SetStringField(TEXT("_meta"), TEXT("extract diagnostics"));

	FAssetDocumentRegionCanonicalizeContext Context;
	Context.Policy = &Policy;
	Context.Source = EAssetDocumentRegionCanonicalizeSource::SidecarAuthored;

	TestEqual(
		TEXT("Identity canonicalizer keeps canonical json hash"),
		FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, MakeObjectValue(Object)),
		FAssetDocumentCanonicalJson::HashJsonValue(MakeObjectValue(Object), &Policy));
	TestEqual(
		TEXT("Identity canonicalizer ignores extract-only fields"),
		FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, MakeObjectValue(Object)),
		FAssetDocumentCanonicalJson::HashJsonValue(MakeObjectValue(BaselineObject), &Policy));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerWritebackKeepsAuthoredShapeTest,
	"AssetDocument.RegionCanonicalizer.WritebackKeepsAuthoredShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerWritebackKeepsAuthoredShapeTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionPolicy Policy;
	Policy.RegionId = TEXT("Body.Blend");
	Policy.BodyPath = TEXT("Body.Blend");

	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("Value"), 1.0);
	Object->SetStringField(TEXT("_meta"), TEXT("diagnostic"));

	FAssetDocumentRegionCanonicalizeContext Context;
	Context.Policy = &Policy;
	Context.Source = EAssetDocumentRegionCanonicalizeSource::AssetEvidence;

	const TSharedPtr<FJsonValue> Writeback = FAssetDocumentRegionCanonicalizer::CanonicalizeForSidecarWriteback(Context, MakeObjectValue(Object));
	TestTrue(TEXT("Writeback remains object"), Writeback.IsValid() && Writeback->Type == EJson::Object);
	if (Writeback.IsValid() && Writeback->Type == EJson::Object)
	{
		TestTrue(TEXT("Writeback preserves ordinary value"), Writeback->AsObject()->HasField(TEXT("Value")));
		TestEqual(TEXT("Writeback preserves authored value"), Writeback->AsObject()->GetNumberField(TEXT("Value")), 1.0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerAnimSequencePostApplyNormalizesGeneratedIdentityTest,
	"AssetDocument.RegionCanonicalizer.AnimSequencePostApply.NormalizesGeneratedIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerAnimSequencePostApplyNormalizesGeneratedIdentityTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentRegionPolicy Policy = MakeAnimSequencePostApplyPolicy();

	TSharedRef<FJsonObject> First = MakeShared<FJsonObject>();
	First->SetStringField(TEXT("ObjectPath"), TEXT("/Engine/Transient.REINST_TestAnimSequence_C_1"));
	First->SetStringField(TEXT("Kind"), TEXT("GeneratedDiagnostic"));

	TSharedRef<FJsonObject> Second = MakeShared<FJsonObject>();
	Second->SetStringField(TEXT("ObjectPath"), TEXT("/Engine/Transient.REINST_TestAnimSequence_C_2"));
	Second->SetStringField(TEXT("Kind"), TEXT("GeneratedDiagnostic"));

	TestEqual(
		TEXT("Generated object identity fields hash equally"),
		HashAnimSequencePostApplyRegion(Policy, MakeObjectValue(First)),
		HashAnimSequencePostApplyRegion(Policy, MakeObjectValue(Second)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerAnimSequencePostApplyRemovesGeneratedEmptyContainersTest,
	"AssetDocument.RegionCanonicalizer.AnimSequencePostApply.RemovesGeneratedEmptyContainers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerAnimSequencePostApplyRemovesGeneratedEmptyContainersTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentRegionPolicy Policy = MakeAnimSequencePostApplyPolicy();

	TSharedRef<FJsonObject> Baseline = MakeShared<FJsonObject>();
	Baseline->SetStringField(TEXT("Name"), TEXT("GeneratedContainerProbe"));

	TSharedRef<FJsonObject> WithGeneratedContainers = MakeShared<FJsonObject>();
	WithGeneratedContainers->SetStringField(TEXT("Name"), TEXT("GeneratedContainerProbe"));
	WithGeneratedContainers->SetObjectField(TEXT("_ProjectionMetrics"), MakeShared<FJsonObject>());
	WithGeneratedContainers->SetArrayField(TEXT("_Generated"), TArray<TSharedPtr<FJsonValue>>());

	TestEqual(
		TEXT("Empty generated diagnostic containers hash equally"),
		HashAnimSequencePostApplyRegion(Policy, MakeObjectValue(Baseline)),
		HashAnimSequencePostApplyRegion(Policy, MakeObjectValue(WithGeneratedContainers)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerAnimSequencePostApplyKeepsPropertiesObjectPathSemanticTest,
	"AssetDocument.RegionCanonicalizer.AnimSequencePostApply.KeepsPropertiesObjectPathSemantic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerAnimSequencePostApplyKeepsPropertiesObjectPathSemanticTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentRegionPolicy Policy = MakeAnimSequencePostApplyPolicy();

	TSharedRef<FJsonObject> FirstProperties = MakeShared<FJsonObject>();
	FirstProperties->SetStringField(TEXT("ObjectPath"), TEXT("/Engine/Transient.REINST_AuthoredPath_A"));
	TSharedRef<FJsonObject> First = MakeShared<FJsonObject>();
	First->SetObjectField(TEXT("Properties"), FirstProperties);

	TSharedRef<FJsonObject> SecondProperties = MakeShared<FJsonObject>();
	SecondProperties->SetStringField(TEXT("ObjectPath"), TEXT("/Engine/Transient.REINST_AuthoredPath_B"));
	TSharedRef<FJsonObject> Second = MakeShared<FJsonObject>();
	Second->SetObjectField(TEXT("Properties"), SecondProperties);

	TestNotEqual(
		TEXT("Properties.ObjectPath remains semantic"),
		HashAnimSequencePostApplyRegion(Policy, MakeObjectValue(First)),
		HashAnimSequencePostApplyRegion(Policy, MakeObjectValue(Second)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerAnimSequencePostApplyKeepsPropertiesDiagnosticsSemanticTest,
	"AssetDocument.RegionCanonicalizer.AnimSequencePostApply.KeepsPropertiesDiagnosticsSemantic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerAnimSequencePostApplyKeepsPropertiesDiagnosticsSemanticTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentRegionPolicy Policy = MakeAnimSequencePostApplyPolicy();

	TSharedRef<FJsonObject> WithoutDiagnosticsProperties = MakeShared<FJsonObject>();
	WithoutDiagnosticsProperties->SetStringField(TEXT("Value"), TEXT("Authored"));
	TSharedRef<FJsonObject> WithoutDiagnostics = MakeShared<FJsonObject>();
	WithoutDiagnostics->SetObjectField(TEXT("Properties"), WithoutDiagnosticsProperties);

	TSharedRef<FJsonObject> WithDiagnosticsProperties = MakeShared<FJsonObject>();
	WithDiagnosticsProperties->SetStringField(TEXT("Value"), TEXT("Authored"));
	WithDiagnosticsProperties->SetArrayField(TEXT("Diagnostics"), TArray<TSharedPtr<FJsonValue>>());
	TSharedRef<FJsonObject> WithDiagnostics = MakeShared<FJsonObject>();
	WithDiagnostics->SetObjectField(TEXT("Properties"), WithDiagnosticsProperties);

	TestNotEqual(
		TEXT("Properties.Diagnostics remains semantic"),
		HashAnimSequencePostApplyRegion(Policy, MakeObjectValue(WithoutDiagnostics)),
		HashAnimSequencePostApplyRegion(Policy, MakeObjectValue(WithDiagnostics)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerAnimSequencePostApplyKeepsGameTestObjectPathSemanticTest,
	"AssetDocument.RegionCanonicalizer.AnimSequencePostApply.KeepsGameTestObjectPathSemantic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerAnimSequencePostApplyKeepsGameTestObjectPathSemanticTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentRegionPolicy Policy = MakeAnimSequencePostApplyPolicy();

	TSharedRef<FJsonObject> First = MakeShared<FJsonObject>();
	First->SetStringField(TEXT("ObjectPath"), TEXT("/Game/Test/SKEL_Foo.SKEL_Foo"));

	TSharedRef<FJsonObject> Second = MakeShared<FJsonObject>();
	Second->SetStringField(TEXT("ObjectPath"), TEXT("/Game/Test/SKEL_Bar.SKEL_Bar"));

	TestNotEqual(
		TEXT("/Game/Test ObjectPath remains hash-significant"),
		HashAnimSequencePostApplyRegion(Policy, MakeObjectValue(First)),
		HashAnimSequencePostApplyRegion(Policy, MakeObjectValue(Second)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerAnimSequencePostApplyKeepsGameAssetDocumentTestsObjectPathSemanticTest,
	"AssetDocument.RegionCanonicalizer.AnimSequencePostApply.KeepsGameAssetDocumentTestsObjectPathSemantic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerAnimSequencePostApplyKeepsGameAssetDocumentTestsObjectPathSemanticTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentRegionPolicy Policy = MakeAnimSequencePostApplyPolicy();

	TSharedRef<FJsonObject> First = MakeShared<FJsonObject>();
	First->SetStringField(TEXT("ObjectPath"), TEXT("/Game/AssetDocumentTests/REINST_Foo.REINST_Foo"));

	TSharedRef<FJsonObject> Second = MakeShared<FJsonObject>();
	Second->SetStringField(TEXT("ObjectPath"), TEXT("/Game/AssetDocumentTests/REINST_Bar.REINST_Bar"));

	TestNotEqual(
		TEXT("/Game/AssetDocumentTests ObjectPath remains hash-significant"),
		HashAnimSequencePostApplyRegion(Policy, MakeObjectValue(First)),
		HashAnimSequencePostApplyRegion(Policy, MakeObjectValue(Second)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerAnimSequencePostApplyKeepsAssetRefLikeFieldsSemanticTest,
	"AssetDocument.RegionCanonicalizer.AnimSequencePostApply.KeepsAssetRefLikeFieldsSemantic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerAnimSequencePostApplyKeepsAssetRefLikeFieldsSemanticTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentRegionPolicy Policy = MakeAnimSequencePostApplyPolicy();

	TSharedRef<FJsonObject> First = MakeShared<FJsonObject>();
	First->SetStringField(TEXT("RefPoseSeq"), TEXT("/Game/AssetDocumentTests/REINST_RefPoseA.REINST_RefPoseA"));

	TSharedRef<FJsonObject> Second = MakeShared<FJsonObject>();
	Second->SetStringField(TEXT("RefPoseSeq"), TEXT("/Game/AssetDocumentTests/REINST_RefPoseB.REINST_RefPoseB"));

	TestNotEqual(
		TEXT("Asset-ref-like fields remain hash-significant"),
		HashAnimSequencePostApplyRegion(Policy, MakeObjectValue(First)),
		HashAnimSequencePostApplyRegion(Policy, MakeObjectValue(Second)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerAnimSequencePostApplyKeepsNameAndClassSemanticTest,
	"AssetDocument.RegionCanonicalizer.AnimSequencePostApply.KeepsNameAndClassSemantic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerAnimSequencePostApplyKeepsNameAndClassSemanticTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentRegionPolicy Policy = MakeAnimSequencePostApplyPolicy();

	TSharedRef<FJsonObject> FirstName = MakeShared<FJsonObject>();
	FirstName->SetStringField(TEXT("Name"), TEXT("AuthoredNameA"));
	TSharedRef<FJsonObject> SecondName = MakeShared<FJsonObject>();
	SecondName->SetStringField(TEXT("Name"), TEXT("AuthoredNameB"));
	TestNotEqual(
		TEXT("Name remains hash-significant"),
		HashAnimSequencePostApplyRegion(Policy, MakeObjectValue(FirstName)),
		HashAnimSequencePostApplyRegion(Policy, MakeObjectValue(SecondName)));

	TSharedRef<FJsonObject> FirstClass = MakeShared<FJsonObject>();
	FirstClass->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimNotify"));
	TSharedRef<FJsonObject> SecondClass = MakeShared<FJsonObject>();
	SecondClass->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimNotifyState"));
	TestNotEqual(
		TEXT("Class remains hash-significant"),
		HashAnimSequencePostApplyRegion(Policy, MakeObjectValue(FirstClass)),
		HashAnimSequencePostApplyRegion(Policy, MakeObjectValue(SecondClass)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerGraphIgnoresGeneratedMetadataTest,
	"AssetDocument.RegionCanonicalizer.Graph.IgnoresGeneratedMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerGraphIgnoresGeneratedMetadataTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionPolicy Policy;
	Policy.RegionId = TEXT("Body.UbergraphPages");
	Policy.BodyPath = TEXT("Body.UbergraphPages");
	Policy.RegionKind = EAssetDocumentRegionKind::Graph;
	Policy.CanonicalizerHookName = TEXT("UBlueprintGraph");

	FAssetDocumentRegionCanonicalizeContext SidecarContext;
	SidecarContext.Policy = &Policy;
	SidecarContext.Source = EAssetDocumentRegionCanonicalizeSource::SidecarAuthored;

	FAssetDocumentRegionCanonicalizeContext EvidenceContext;
	EvidenceContext.Policy = &Policy;
	EvidenceContext.Source = EAssetDocumentRegionCanonicalizeSource::AssetEvidence;

	const TSharedPtr<FJsonValue> SidecarGraph = MakeGraphRegionValue(
		TEXT("event_beginplay"),
		TEXT("print_string"),
		false);
	const TSharedPtr<FJsonValue> EvidenceGraph = MakeGraphRegionValue(
		TEXT("K2Node_Event_0"),
		TEXT("K2Node_CallFunction_0"),
		true);

	TestEqual(
		TEXT("Generated graph metadata and semantic node ids hash equally"),
		FAssetDocumentRegionCanonicalizer::HashRegionValue(SidecarContext, SidecarGraph),
		FAssetDocumentRegionCanonicalizer::HashRegionValue(EvidenceContext, EvidenceGraph));

	return true;
}

#endif
