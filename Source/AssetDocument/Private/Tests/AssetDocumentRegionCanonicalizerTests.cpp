// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentRegionCanonicalizer.h"
#include "AssetDocumentCanonicalJson.h"
#include "AssetDocumentPolicy.h"
#include "Tests/AssetDocumentReflectedPropertyTestTypes.h"

#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "Animation/AnimSequence.h"
#include "AnimationUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
TSharedPtr<FJsonValue> RegionCanonicalizerTestMakeObjectValue(TSharedRef<FJsonObject> Object)
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

TSharedPtr<FJsonObject> MakeGraphMemberWithClassAndGuid(const TCHAR* Class, const TCHAR* Name, const TCHAR* Guid)
{
	TSharedPtr<FJsonObject> Member = MakeShared<FJsonObject>();
	Member->SetStringField(TEXT("Kind"), TEXT("Function"));
	Member->SetStringField(TEXT("Class"), Class);
	Member->SetStringField(TEXT("Name"), Name);
	if (Guid)
	{
		Member->SetStringField(TEXT("Guid"), Guid);
	}
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

TSharedPtr<FJsonObject> RegionCanonicalizerTestMakeGraphNode(
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
	PinOverrides.Add(RegionCanonicalizerTestMakeObjectValue(MakeGraphPinOverride(TEXT("Message"), TEXT("Input"), TEXT("Hello")).ToSharedRef()));
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
	Nodes.Add(RegionCanonicalizerTestMakeObjectValue(RegionCanonicalizerTestMakeGraphNode(
		EventNodeId,
		TEXT("/Script/BlueprintGraph.K2Node_Event"),
		MakeGraphMember(TEXT("Event"), TEXT("/Script/Engine.Actor"), TEXT("ReceiveBeginPlay")),
		bIncludeGeneratedMetadata ? TEXT("E0B14B7C4E0F4F0BA0E5E4D600000002") : nullptr,
		bIncludeGeneratedMetadata ? TEXT("Event") : nullptr).ToSharedRef()));
	Nodes.Add(RegionCanonicalizerTestMakeObjectValue(RegionCanonicalizerTestMakeGraphNode(
		CallNodeId,
		TEXT("/Script/BlueprintGraph.K2Node_CallFunction"),
		MakeGraphMember(TEXT("Function"), TEXT("/Script/Engine.KismetSystemLibrary"), TEXT("PrintString")),
		bIncludeGeneratedMetadata ? TEXT("E0B14B7C4E0F4F0BA0E5E4D600000003") : nullptr,
		bIncludeGeneratedMetadata ? TEXT("CallFunction") : nullptr).ToSharedRef()));
	Graph->SetArrayField(TEXT("Nodes"), MoveTemp(Nodes));

	TArray<TSharedPtr<FJsonValue>> Links;
	Links.Add(RegionCanonicalizerTestMakeObjectValue(MakeGraphLink(EventNodeId, TEXT("Then"), CallNodeId, TEXT("execute")).ToSharedRef()));
	Graph->SetArrayField(TEXT("Links"), MoveTemp(Links));

	TArray<TSharedPtr<FJsonValue>> Graphs;
	Graphs.Add(RegionCanonicalizerTestMakeObjectValue(Graph.ToSharedRef()));
	return MakeShared<FJsonValueArray>(MoveTemp(Graphs));
}

TSharedPtr<FJsonValue> MakeSingleNodeGraphRegionValue(TSharedPtr<FJsonObject> Node)
{
	TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();
	Graph->SetStringField(TEXT("Name"), TEXT("EventGraph"));
	Graph->SetStringField(TEXT("Schema"), TEXT("K2"));

	TArray<TSharedPtr<FJsonValue>> Nodes;
	Nodes.Add(RegionCanonicalizerTestMakeObjectValue(Node.ToSharedRef()));
	Graph->SetArrayField(TEXT("Nodes"), MoveTemp(Nodes));
	Graph->SetArrayField(TEXT("Links"), TArray<TSharedPtr<FJsonValue>>());

	TArray<TSharedPtr<FJsonValue>> Graphs;
	Graphs.Add(RegionCanonicalizerTestMakeObjectValue(Graph.ToSharedRef()));
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
	const TSharedPtr<FJsonValue>& Value,
	UClass* AssetClass = nullptr)
{
	FAssetDocumentRegionCanonicalizeContext Context;
	Context.Policy = &Policy;
	Context.Source = EAssetDocumentRegionCanonicalizeSource::AssetEvidence;
	Context.AssetClass = AssetClass;
	return FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, Value);
}

FAssetDocumentRegionPolicy MakeBehaviorTreePostApplyPolicy()
{
	FAssetDocumentRegionPolicy Policy;
	Policy.RegionId = TEXT("Body.Tree");
	Policy.BodyPath = TEXT("Body.Tree");
	Policy.CanonicalizerHookName = TEXT("BehaviorTreePostApply");
	return Policy;
}

FString HashBehaviorTreePostApplyRegion(
	const TSharedPtr<FJsonValue>& Value,
	EAssetDocumentRegionCanonicalizeSource Source = EAssetDocumentRegionCanonicalizeSource::AssetEvidence)
{
	const FAssetDocumentRegionPolicy Policy = MakeBehaviorTreePostApplyPolicy();
	FAssetDocumentRegionCanonicalizeContext Context;
	Context.Policy = &Policy;
	Context.Source = Source;
	return FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, Value);
}

TSharedPtr<FJsonObject> MakeBehaviorTreeComment(
	const TCHAR* Id,
	bool bExplicitExtractDefaults,
	bool bExtractedColorPrecision,
	double AuthoredColorR,
	int32 FontSize = 18)
{
	TSharedPtr<FJsonObject> Comment = MakeShared<FJsonObject>();
	Comment->SetStringField(TEXT("Id"), Id);
	if (bExplicitExtractDefaults)
	{
		Comment->SetStringField(TEXT("Text"), TEXT(""));
		Comment->SetObjectField(TEXT("Position"), MakeGraphPosition(0.0, 0.0));
		TSharedPtr<FJsonObject> Size = MakeShared<FJsonObject>();
		Size->SetNumberField(TEXT("Width"), 400);
		Size->SetNumberField(TEXT("Height"), 100);
		Comment->SetObjectField(TEXT("Size"), Size);
		Comment->SetNumberField(TEXT("CommentDepth"), -1);
		Comment->SetStringField(TEXT("MoveMode"), TEXT("GroupMovement"));
		Comment->SetStringField(TEXT("NodeDetails"), TEXT(""));
		Comment->SetBoolField(TEXT("bCommentBubblePinned"), true);
		Comment->SetBoolField(TEXT("bCommentBubbleVisible"), true);
		Comment->SetBoolField(TEXT("bCommentBubbleVisible_InDetailsPanel"), true);
		Comment->SetBoolField(TEXT("bColorCommentBubble"), false);
	}
	if (bExplicitExtractDefaults || FontSize != 18)
	{
		Comment->SetNumberField(TEXT("FontSize"), FontSize);
	}
	if (bExplicitExtractDefaults || AuthoredColorR != 1.0)
	{
		TSharedPtr<FJsonObject> Color = MakeShared<FJsonObject>();
		const auto AtStoragePrecision = [bExtractedColorPrecision](double Value)
		{
			return bExtractedColorPrecision ? static_cast<double>(static_cast<float>(Value)) : Value;
		};
		Color->SetNumberField(TEXT("R"), AtStoragePrecision(AuthoredColorR));
		if (bExplicitExtractDefaults)
		{
			Color->SetNumberField(TEXT("G"), AtStoragePrecision(1.0));
			Color->SetNumberField(TEXT("B"), AtStoragePrecision(1.0));
			Color->SetNumberField(TEXT("A"), AtStoragePrecision(1.0));
		}
		Comment->SetObjectField(TEXT("Color"), Color);
	}
	return Comment;
}

TSharedPtr<FJsonValue> MakeBehaviorTreePostApplyRegionValue(
	bool bExplicitExtractDefaults,
	bool bExtractedColorPrecision,
	const TCHAR* NodeComment = TEXT(""),
	int32 CommentFontSize = 18,
	double AuthoredColorR = 0.1)
{
	TSharedPtr<FJsonObject> Editor = MakeShared<FJsonObject>();
	Editor->SetObjectField(TEXT("Position"), MakeGraphPosition(0.0, 0.0));
	if (bExplicitExtractDefaults)
	{
		Editor->SetStringField(TEXT("NodeComment"), TEXT(""));
		Editor->SetBoolField(TEXT("bCommentBubblePinned"), false);
		Editor->SetBoolField(TEXT("bCommentBubbleVisible"), false);
	}
	if (FCString::Strlen(NodeComment) > 0)
	{
		Editor->SetStringField(TEXT("NodeComment"), NodeComment);
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("Id"), TEXT("11111111111111111111111111111111"));
	Root->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BTComposite_Selector"));
	Root->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Root->SetObjectField(TEXT("Editor"), Editor);
	if (bExplicitExtractDefaults)
	{
		Root->SetArrayField(TEXT("Decorators"), {});
		Root->SetArrayField(TEXT("Services"), {});
		Root->SetArrayField(TEXT("Children"), {});
	}

	TArray<TSharedPtr<FJsonValue>> Comments;
	Comments.Add(RegionCanonicalizerTestMakeObjectValue(MakeBehaviorTreeComment(
		TEXT("22222222222222222222222222222222"),
		bExplicitExtractDefaults,
		bExtractedColorPrecision,
		1.0).ToSharedRef()));
	Comments.Add(RegionCanonicalizerTestMakeObjectValue(MakeBehaviorTreeComment(
		TEXT("33333333333333333333333333333333"),
		bExplicitExtractDefaults,
		bExtractedColorPrecision,
		AuthoredColorR,
		CommentFontSize).ToSharedRef()));

	TSharedPtr<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetStringField(TEXT("GraphGuid"), TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
	Tree->SetObjectField(TEXT("Root"), Root);
	Tree->SetArrayField(TEXT("Comments"), MoveTemp(Comments));
	return RegionCanonicalizerTestMakeObjectValue(Tree.ToSharedRef());
}

TSharedRef<FJsonObject> MakeAssetRefObject(const UObject* Object)
{
	TSharedRef<FJsonObject> AssetRef = MakeShared<FJsonObject>();
	AssetRef->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	AssetRef->SetStringField(TEXT("Path"), Object ? Object->GetPathName() : TEXT(""));
	return AssetRef;
}

TSharedPtr<FJsonValue> MakeCurveObjectValue(const TCHAR* Name, const TCHAR* InterpMode, double Value)
{
	TSharedRef<FJsonObject> Key = MakeShared<FJsonObject>();
	Key->SetNumberField(TEXT("Time"), 0.5);
	Key->SetNumberField(TEXT("Value"), Value);
	Key->SetStringField(TEXT("InterpMode"), InterpMode);

	TSharedRef<FJsonObject> Curve = MakeShared<FJsonObject>();
	Curve->SetStringField(TEXT("Name"), Name);
	Curve->SetStringField(TEXT("CurveType"), TEXT("Float"));
	Curve->SetArrayField(TEXT("Keys"), {RegionCanonicalizerTestMakeObjectValue(Key)});
	return RegionCanonicalizerTestMakeObjectValue(Curve);
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
		FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, RegionCanonicalizerTestMakeObjectValue(Object)),
		FAssetDocumentCanonicalJson::HashJsonValue(RegionCanonicalizerTestMakeObjectValue(Object), &Policy));
	TestEqual(
		TEXT("Identity canonicalizer ignores extract-only fields"),
		FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, RegionCanonicalizerTestMakeObjectValue(Object)),
		FAssetDocumentCanonicalJson::HashJsonValue(RegionCanonicalizerTestMakeObjectValue(BaselineObject), &Policy));

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
	Object->SetObjectField(TEXT("_Skipped"), MakeShared<FJsonObject>());

	FAssetDocumentRegionCanonicalizeContext Context;
	Context.Policy = &Policy;
	Context.Source = EAssetDocumentRegionCanonicalizeSource::AssetEvidence;

	const TSharedPtr<FJsonValue> Writeback = FAssetDocumentRegionCanonicalizer::CanonicalizeForSidecarWriteback(Context, RegionCanonicalizerTestMakeObjectValue(Object));
	TestTrue(TEXT("Writeback remains object"), Writeback.IsValid() && Writeback->Type == EJson::Object);
	if (Writeback.IsValid() && Writeback->Type == EJson::Object)
	{
		TestFalse(TEXT("Writeback returns cloned object"), Writeback->AsObject().Get() == &Object.Get());
		TestTrue(TEXT("Writeback preserves ordinary value"), Writeback->AsObject()->HasField(TEXT("Value")));
		TestEqual(TEXT("Writeback preserves authored value"), Writeback->AsObject()->GetNumberField(TEXT("Value")), 1.0);
		TestTrue(TEXT("Writeback preserves authored _meta"), Writeback->AsObject()->HasField(TEXT("_meta")));
		TestEqual(TEXT("Writeback preserves authored _meta value"), Writeback->AsObject()->GetStringField(TEXT("_meta")), FString(TEXT("diagnostic")));
		TestTrue(TEXT("Writeback preserves authored _Skipped"), Writeback->AsObject()->HasTypedField<EJson::Object>(TEXT("_Skipped")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerBehaviorTreePostApplyNormalizesExtractDefaultsTest,
	"AssetDocument.RegionCanonicalizer.BehaviorTreePostApply.NormalizesExtractDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerBehaviorTreePostApplyNormalizesExtractDefaultsTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonValue> SparseAuthored = MakeBehaviorTreePostApplyRegionValue(false, false);
	const TSharedPtr<FJsonValue> ExplicitEvidence = MakeBehaviorTreePostApplyRegionValue(true, true);
	TestEqual(
		TEXT("omitted BehaviorTree editor/comment defaults and FLinearColor storage precision hash equally"),
		HashBehaviorTreePostApplyRegion(SparseAuthored, EAssetDocumentRegionCanonicalizeSource::SidecarAuthored),
		HashBehaviorTreePostApplyRegion(ExplicitEvidence, EAssetDocumentRegionCanonicalizeSource::AssetEvidence));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerBehaviorTreePostApplyNormalizesNodeClassDefaultsTest,
	"AssetDocument.RegionCanonicalizer.BehaviorTreePostApply.NormalizesNodeClassDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerBehaviorTreePostApplyNormalizesNodeClassDefaultsTest::RunTest(const FString& Parameters)
{
	const auto MakeWaitTree = [](
		bool bIncludeExtractedClassDefaults,
		double WaitDefaultValue = 5.0,
		const TCHAR* ClassPath = TEXT("/Script/AIModule.BTTask_Wait"))
	{
		TSharedPtr<FJsonObject> WaitProperties = MakeShared<FJsonObject>();
		WaitProperties->SetStringField(TEXT("NodeName"), TEXT("Subtree wait"));
		WaitProperties->SetBoolField(TEXT("bIgnoreRestartSelf"), true);
		if (bIncludeExtractedClassDefaults)
		{
			TSharedPtr<FJsonObject> WaitTime = MakeShared<FJsonObject>();
			WaitTime->SetNumberField(TEXT("DefaultValue"), WaitDefaultValue);
			WaitTime->SetStringField(TEXT("Key"), TEXT("None"));
			WaitProperties->SetObjectField(TEXT("WaitTime"), WaitTime);

			TSharedPtr<FJsonObject> RandomDeviation = MakeShared<FJsonObject>();
			RandomDeviation->SetNumberField(TEXT("DefaultValue"), 0.0);
			RandomDeviation->SetStringField(TEXT("Key"), TEXT("None"));
			WaitProperties->SetObjectField(TEXT("RandomDeviation"), RandomDeviation);
		}

		TSharedPtr<FJsonObject> WaitNode = MakeShared<FJsonObject>();
		WaitNode->SetStringField(TEXT("Id"), TEXT("44444444444444444444444444444444"));
		WaitNode->SetStringField(TEXT("Class"), ClassPath);
		WaitNode->SetObjectField(TEXT("Properties"), WaitProperties);

		TSharedPtr<FJsonValue> TreeValue = MakeBehaviorTreePostApplyRegionValue(false, false);
		TreeValue->AsObject()
			->GetObjectField(TEXT("Root"))
			->SetArrayField(TEXT("Children"), {RegionCanonicalizerTestMakeObjectValue(WaitNode.ToSharedRef())});
		return TreeValue;
	};

	const FString SparseHash = HashBehaviorTreePostApplyRegion(
		MakeWaitTree(false),
		EAssetDocumentRegionCanonicalizeSource::SidecarAuthored);
	TestEqual(
		TEXT("omitted BehaviorTree node CDO defaults and extracted defaults hash equally"),
		SparseHash,
		HashBehaviorTreePostApplyRegion(MakeWaitTree(true), EAssetDocumentRegionCanonicalizeSource::AssetEvidence));
	TestNotEqual(
		TEXT("non-default BehaviorTree node property remains hash-significant"),
		SparseHash,
		HashBehaviorTreePostApplyRegion(MakeWaitTree(true, 7.5), EAssetDocumentRegionCanonicalizeSource::AssetEvidence));
	TestNotEqual(
		TEXT("unresolved BehaviorTree node class leaves default-looking properties hash-significant"),
		HashBehaviorTreePostApplyRegion(
			MakeWaitTree(false, 5.0, TEXT("/Script/AIModule.DoesNotExist")),
			EAssetDocumentRegionCanonicalizeSource::SidecarAuthored),
		HashBehaviorTreePostApplyRegion(
			MakeWaitTree(true, 5.0, TEXT("/Script/AIModule.DoesNotExist")),
			EAssetDocumentRegionCanonicalizeSource::AssetEvidence));
	TestNotEqual(
		TEXT("non-BehaviorTree class leaves default-looking properties hash-significant"),
		HashBehaviorTreePostApplyRegion(
			MakeWaitTree(false, 5.0, TEXT("/Script/CoreUObject.Object")),
			EAssetDocumentRegionCanonicalizeSource::SidecarAuthored),
		HashBehaviorTreePostApplyRegion(
			MakeWaitTree(true, 5.0, TEXT("/Script/CoreUObject.Object")),
			EAssetDocumentRegionCanonicalizeSource::AssetEvidence));
	TestNotEqual(
		TEXT("valid BehaviorTree class with an unsupported CDO property leaves authored properties hash-significant"),
		HashBehaviorTreePostApplyRegion(
			MakeWaitTree(
				false,
				5.0,
				*UAssetDocumentUnsupportedExtractTaskTestNode::StaticClass()->GetPathName()),
			EAssetDocumentRegionCanonicalizeSource::SidecarAuthored),
		HashBehaviorTreePostApplyRegion(
			MakeWaitTree(
				true,
				5.0,
				*UAssetDocumentUnsupportedExtractTaskTestNode::StaticClass()->GetPathName()),
			EAssetDocumentRegionCanonicalizeSource::AssetEvidence));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerBehaviorTreePostApplyKeepsAuthoredPropertyShapesOpaqueTest,
	"AssetDocument.RegionCanonicalizer.BehaviorTreePostApply.KeepsAuthoredPropertyShapesOpaque",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerBehaviorTreePostApplyKeepsAuthoredPropertyShapesOpaqueTest::RunTest(const FString& Parameters)
{
	const auto MakeClassShapeTree = [](bool bIncludeDefaultLookingValue)
	{
		TSharedPtr<FJsonObject> NestedProperties = MakeShared<FJsonObject>();
		if (bIncludeDefaultLookingValue)
		{
			TSharedPtr<FJsonObject> WaitTime = MakeShared<FJsonObject>();
			WaitTime->SetNumberField(TEXT("DefaultValue"), 5.0);
			WaitTime->SetStringField(TEXT("Key"), TEXT("None"));
			NestedProperties->SetObjectField(TEXT("WaitTime"), WaitTime);
		}

		TSharedPtr<FJsonObject> ClassShape = MakeShared<FJsonObject>();
		ClassShape->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BTTask_Wait"));
		ClassShape->SetObjectField(TEXT("Properties"), NestedProperties);

		TSharedPtr<FJsonValue> TreeValue = MakeBehaviorTreePostApplyRegionValue(false, false);
		TreeValue->AsObject()
			->GetObjectField(TEXT("Root"))
			->GetObjectField(TEXT("Properties"))
			->SetObjectField(TEXT("ClassShape"), ClassShape);
		return TreeValue;
	};

	const auto MakeBoundGraphShapeTree = [](bool bPermuteNodes)
	{
		const auto MakeNode = [](const TCHAR* Id)
		{
			TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
			Node->SetStringField(TEXT("Id"), Id);
			return RegionCanonicalizerTestMakeObjectValue(Node.ToSharedRef());
		};

		TArray<TSharedPtr<FJsonValue>> Nodes = {
			MakeNode(TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA1")),
			MakeNode(TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA2")),
		};
		if (bPermuteNodes)
		{
			Nodes.Swap(0, 1);
		}

		TSharedPtr<FJsonObject> BoundGraph = MakeShared<FJsonObject>();
		BoundGraph->SetStringField(TEXT("GraphGuid"), TEXT("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"));
		BoundGraph->SetArrayField(TEXT("Nodes"), MoveTemp(Nodes));

		TSharedPtr<FJsonObject> GraphShape = MakeShared<FJsonObject>();
		GraphShape->SetObjectField(TEXT("BoundGraph"), BoundGraph);

		TSharedPtr<FJsonValue> TreeValue = MakeBehaviorTreePostApplyRegionValue(false, false);
		TreeValue->AsObject()
			->GetObjectField(TEXT("Root"))
			->GetObjectField(TEXT("Properties"))
			->SetObjectField(TEXT("GraphShape"), GraphShape);
		return TreeValue;
	};

	TestNotEqual(
		TEXT("Class and Properties fields inside authored properties are ordinary hash-significant data"),
		HashBehaviorTreePostApplyRegion(MakeClassShapeTree(false)),
		HashBehaviorTreePostApplyRegion(MakeClassShapeTree(true)));
	TestNotEqual(
		TEXT("BoundGraph-shaped data inside authored properties keeps authored array order"),
		HashBehaviorTreePostApplyRegion(MakeBoundGraphShapeTree(false)),
		HashBehaviorTreePostApplyRegion(MakeBoundGraphShapeTree(true)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerBehaviorTreePostApplyNormalizesStorageOrderTest,
	"AssetDocument.RegionCanonicalizer.BehaviorTreePostApply.NormalizesStorageOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerBehaviorTreePostApplyNormalizesStorageOrderTest::RunTest(const FString& Parameters)
{
	const auto MakeStorageOrderTree = [](bool bPermuteStorageOrder, bool bChangeLinkInput)
	{
		const auto MakeBoundNode = [](const TCHAR* Id, const TCHAR* Kind)
		{
			TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
			Node->SetStringField(TEXT("Id"), Id);
			Node->SetStringField(TEXT("Kind"), Kind);
			return RegionCanonicalizerTestMakeObjectValue(Node.ToSharedRef());
		};
		const auto MakeBoundLink = [](const TCHAR* From, const TCHAR* To, int32 ToInput)
		{
			TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
			Link->SetStringField(TEXT("From"), From);
			Link->SetStringField(TEXT("To"), To);
			Link->SetNumberField(TEXT("ToInput"), ToInput);
			return RegionCanonicalizerTestMakeObjectValue(Link.ToSharedRef());
		};

		TArray<TSharedPtr<FJsonValue>> Nodes = {
			MakeBoundNode(TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA1"), TEXT("Sink")),
			MakeBoundNode(TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA2"), TEXT("Not")),
			MakeBoundNode(TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA3"), TEXT("Not")),
		};
		TArray<TSharedPtr<FJsonValue>> Links = {
			MakeBoundLink(TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA2"), TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA1"), 0),
			MakeBoundLink(TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA3"), TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA2"), bChangeLinkInput ? 1 : 0),
		};
		if (bPermuteStorageOrder)
		{
			Nodes.Swap(0, 2);
			Links.Swap(0, 1);
		}

		TSharedPtr<FJsonObject> BoundGraph = MakeShared<FJsonObject>();
		BoundGraph->SetStringField(TEXT("GraphGuid"), TEXT("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"));
		BoundGraph->SetArrayField(TEXT("Nodes"), MoveTemp(Nodes));
		BoundGraph->SetArrayField(TEXT("Links"), MoveTemp(Links));

		TSharedPtr<FJsonObject> CompositeDecorator = MakeShared<FJsonObject>();
		CompositeDecorator->SetStringField(TEXT("Id"), TEXT("CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC"));
		CompositeDecorator->SetStringField(TEXT("Kind"), TEXT("Composite"));
		CompositeDecorator->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
		CompositeDecorator->SetObjectField(TEXT("BoundGraph"), BoundGraph);

		TSharedPtr<FJsonValue> TreeValue = MakeBehaviorTreePostApplyRegionValue(false, false);
		const TSharedPtr<FJsonObject> Tree = TreeValue->AsObject();
		Tree->GetObjectField(TEXT("Root"))->SetArrayField(
			TEXT("Decorators"),
			{RegionCanonicalizerTestMakeObjectValue(CompositeDecorator.ToSharedRef())});
		if (bPermuteStorageOrder)
		{
			const TArray<TSharedPtr<FJsonValue>>* ExistingComments = nullptr;
			Tree->TryGetArrayField(TEXT("Comments"), ExistingComments);
			TArray<TSharedPtr<FJsonValue>> Comments = ExistingComments ? *ExistingComments : TArray<TSharedPtr<FJsonValue>>();
			if (Comments.Num() == 2)
			{
				Comments.Swap(0, 1);
			}
			Tree->SetArrayField(TEXT("Comments"), MoveTemp(Comments));
		}
		return TreeValue;
	};

	const TSharedPtr<FJsonValue> PermutedAuthored = MakeStorageOrderTree(true, false);
	const TSharedPtr<FJsonValue> ExtractOrderEvidence = MakeStorageOrderTree(false, false);
	TestEqual(
		TEXT("BehaviorTree comment and BoundGraph node/link storage order do not affect the hash"),
		HashBehaviorTreePostApplyRegion(PermutedAuthored, EAssetDocumentRegionCanonicalizeSource::SidecarAuthored),
		HashBehaviorTreePostApplyRegion(ExtractOrderEvidence, EAssetDocumentRegionCanonicalizeSource::AssetEvidence));
	TestNotEqual(
		TEXT("BehaviorTree BoundGraph link input remains hash-significant"),
		HashBehaviorTreePostApplyRegion(ExtractOrderEvidence, EAssetDocumentRegionCanonicalizeSource::AssetEvidence),
		HashBehaviorTreePostApplyRegion(MakeStorageOrderTree(false, true), EAssetDocumentRegionCanonicalizeSource::AssetEvidence));

	const FAssetDocumentRegionPolicy Policy = MakeBehaviorTreePostApplyPolicy();
	FAssetDocumentRegionCanonicalizeContext Context;
	Context.Policy = &Policy;
	Context.Source = EAssetDocumentRegionCanonicalizeSource::SidecarAuthored;
	const TSharedPtr<FJsonValue> Writeback =
		FAssetDocumentRegionCanonicalizer::CanonicalizeForSidecarWriteback(Context, PermutedAuthored);
	const TSharedPtr<FJsonObject> WritebackTree = Writeback->AsObject();
	const TArray<TSharedPtr<FJsonValue>>* WritebackComments = nullptr;
	WritebackTree->TryGetArrayField(TEXT("Comments"), WritebackComments);
	TestEqual(
		TEXT("writeback preserves authored comment order"),
		WritebackComments && WritebackComments->Num() > 0
			? (*WritebackComments)[0]->AsObject()->GetStringField(TEXT("Id"))
			: FString(),
		FString(TEXT("33333333333333333333333333333333")));

	const TArray<TSharedPtr<FJsonValue>>* WritebackDecorators = nullptr;
	WritebackTree->GetObjectField(TEXT("Root"))->TryGetArrayField(TEXT("Decorators"), WritebackDecorators);
	const TSharedPtr<FJsonObject> WritebackBoundGraph =
		WritebackDecorators && WritebackDecorators->Num() > 0
			? (*WritebackDecorators)[0]->AsObject()->GetObjectField(TEXT("BoundGraph"))
			: nullptr;
	const TArray<TSharedPtr<FJsonValue>>* WritebackNodes = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* WritebackLinks = nullptr;
	if (WritebackBoundGraph.IsValid())
	{
		WritebackBoundGraph->TryGetArrayField(TEXT("Nodes"), WritebackNodes);
		WritebackBoundGraph->TryGetArrayField(TEXT("Links"), WritebackLinks);
	}
	TestEqual(
		TEXT("writeback preserves authored BoundGraph node order"),
		WritebackNodes && WritebackNodes->Num() > 0
			? (*WritebackNodes)[0]->AsObject()->GetStringField(TEXT("Id"))
			: FString(),
		FString(TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA3")));
	TestEqual(
		TEXT("writeback preserves authored BoundGraph link order"),
		WritebackLinks && WritebackLinks->Num() > 0
			? (*WritebackLinks)[0]->AsObject()->GetStringField(TEXT("From"))
			: FString(),
		FString(TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA3")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerBehaviorTreePostApplyKeepsSemanticArrayOrderTest,
	"AssetDocument.RegionCanonicalizer.BehaviorTreePostApply.KeepsSemanticArrayOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerBehaviorTreePostApplyKeepsSemanticArrayOrderTest::RunTest(const FString& Parameters)
{
	const auto MakeOrderedTree = [](const TCHAR* FieldName, bool bSwap)
	{
		const auto MakeEntry = [](const TCHAR* Id)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("Id"), Id);
			return RegionCanonicalizerTestMakeObjectValue(Entry.ToSharedRef());
		};

		TArray<TSharedPtr<FJsonValue>> Entries = {
			MakeEntry(TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA1")),
			MakeEntry(TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA2")),
		};
		if (bSwap)
		{
			Entries.Swap(0, 1);
		}

		TSharedPtr<FJsonValue> TreeValue = MakeBehaviorTreePostApplyRegionValue(false, false);
		TreeValue->AsObject()
			->GetObjectField(TEXT("Root"))
			->SetArrayField(FieldName, MoveTemp(Entries));
		return TreeValue;
	};

	for (const TCHAR* FieldName : {TEXT("Children"), TEXT("Services"), TEXT("Decorators")})
	{
		TestNotEqual(
			*FString::Printf(TEXT("%s semantic order remains hash-significant"), FieldName),
			HashBehaviorTreePostApplyRegion(MakeOrderedTree(FieldName, false)),
			HashBehaviorTreePostApplyRegion(MakeOrderedTree(FieldName, true)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerBehaviorTreePostApplyKeepsAuthoredSemanticsTest,
	"AssetDocument.RegionCanonicalizer.BehaviorTreePostApply.KeepsAuthoredSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerBehaviorTreePostApplyKeepsAuthoredSemanticsTest::RunTest(const FString& Parameters)
{
	const FString BaselineHash = HashBehaviorTreePostApplyRegion(MakeBehaviorTreePostApplyRegionValue(false, false));
	TestNotEqual(
		TEXT("non-default NodeComment remains hash-significant"),
		BaselineHash,
		HashBehaviorTreePostApplyRegion(MakeBehaviorTreePostApplyRegionValue(false, false, TEXT("authored note"))));
	TestNotEqual(
		TEXT("non-default comment FontSize remains hash-significant"),
		BaselineHash,
		HashBehaviorTreePostApplyRegion(MakeBehaviorTreePostApplyRegionValue(false, false, TEXT(""), 24)));
	TestNotEqual(
		TEXT("non-default comment Color remains hash-significant"),
		BaselineHash,
		HashBehaviorTreePostApplyRegion(MakeBehaviorTreePostApplyRegionValue(false, false, TEXT(""), 18, 0.25)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerBehaviorTreePostApplyKeepsAuthoredEmptyArraysTest,
	"AssetDocument.RegionCanonicalizer.BehaviorTreePostApply.KeepsAuthoredEmptyArrays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerBehaviorTreePostApplyKeepsAuthoredEmptyArraysTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonValue> MissingAuthoredArrays = MakeBehaviorTreePostApplyRegionValue(false, false);
	const TSharedPtr<FJsonValue> WithAuthoredArrays = MakeBehaviorTreePostApplyRegionValue(false, false);
	const TSharedPtr<FJsonObject> AuthoredProperties = WithAuthoredArrays->AsObject()
		->GetObjectField(TEXT("Root"))
		->GetObjectField(TEXT("Properties"));
	const TArray<FString> AuthoredArrayNames = {
		TEXT("Services"),
		TEXT("Children"),
		TEXT("Decorators"),
		TEXT("DecoratorLogic"),
		TEXT("RootDecorators"),
		TEXT("RootDecoratorLogic"),
	};
	for (const FString& FieldName : AuthoredArrayNames)
	{
		AuthoredProperties->SetArrayField(FieldName, {});
	}

	TestNotEqual(
		TEXT("authored empty arrays under Properties remain hash-significant"),
		HashBehaviorTreePostApplyRegion(MissingAuthoredArrays, EAssetDocumentRegionCanonicalizeSource::SidecarAuthored),
		HashBehaviorTreePostApplyRegion(WithAuthoredArrays, EAssetDocumentRegionCanonicalizeSource::SidecarAuthored));

	const FAssetDocumentRegionPolicy Policy = MakeBehaviorTreePostApplyPolicy();
	FAssetDocumentRegionCanonicalizeContext Context;
	Context.Policy = &Policy;
	Context.Source = EAssetDocumentRegionCanonicalizeSource::SidecarAuthored;
	const TSharedPtr<FJsonValue> Writeback = FAssetDocumentRegionCanonicalizer::CanonicalizeForSidecarWriteback(
		Context,
		WithAuthoredArrays);
	const TSharedPtr<FJsonObject> WritebackProperties = Writeback->AsObject()
		->GetObjectField(TEXT("Root"))
		->GetObjectField(TEXT("Properties"));
	for (const FString& FieldName : AuthoredArrayNames)
	{
		const TArray<TSharedPtr<FJsonValue>>* EmptyArray = nullptr;
		TestTrue(
			*FString::Printf(TEXT("writeback preserves authored empty %s array"), *FieldName),
			WritebackProperties->TryGetArrayField(FieldName, EmptyArray)
				&& EmptyArray
				&& EmptyArray->Num() == 0);
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
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(First)),
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(Second)));

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
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(Baseline)),
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(WithGeneratedContainers)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerAnimSequencePostApplyNormalizesManagedFloatPrecisionTest,
	"AssetDocument.RegionCanonicalizer.AnimSequencePostApply.NormalizesManagedFloatPrecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerAnimSequencePostApplyNormalizesManagedFloatPrecisionTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionPolicy Policy = MakeAnimSequencePostApplyPolicy();
	Policy.RegionId = TEXT("Body.Compression");
	Policy.BodyPath = TEXT("Body.Compression");
	Policy.ManagedUePropertyPaths = {TEXT("CompressionErrorThresholdScale")};

	TSharedRef<FJsonObject> Authored = MakeShared<FJsonObject>();
	Authored->SetNumberField(TEXT("CompressionErrorThresholdScale"), 0.42);

	TSharedRef<FJsonObject> Extracted = MakeShared<FJsonObject>();
	Extracted->SetNumberField(TEXT("CompressionErrorThresholdScale"), static_cast<float>(0.42));

	TestEqual(
		TEXT("Managed float fields hash at reflected property precision"),
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(Authored), UAnimSequence::StaticClass()),
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(Extracted), UAnimSequence::StaticClass()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerAnimSequencePostApplyRemovesProjectDefaultAssetRefsTest,
	"AssetDocument.RegionCanonicalizer.AnimSequencePostApply.RemovesProjectDefaultAssetRefs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerAnimSequencePostApplyRemovesProjectDefaultAssetRefsTest::RunTest(const FString& Parameters)
{
	const UObject* DefaultBoneCompressionSettings = FAnimationUtils::GetDefaultAnimationBoneCompressionSettings();
	const UObject* DefaultCurveCompressionSettings = FAnimationUtils::GetDefaultAnimationCurveCompressionSettings();
	TestNotNull(TEXT("Default bone compression settings exist"), DefaultBoneCompressionSettings);
	TestNotNull(TEXT("Default curve compression settings exist"), DefaultCurveCompressionSettings);
	if (!DefaultBoneCompressionSettings || !DefaultCurveCompressionSettings)
	{
		return false;
	}

	FAssetDocumentRegionPolicy Policy = MakeAnimSequencePostApplyPolicy();
	Policy.RegionId = TEXT("Body.Compression");
	Policy.BodyPath = TEXT("Body.Compression");
	Policy.ManagedUePropertyPaths = {TEXT("BoneCompressionSettings"), TEXT("CurveCompressionSettings")};

	TSharedRef<FJsonObject> Authored = MakeShared<FJsonObject>();
	Authored->SetBoolField(TEXT("bDoNotOverrideCompression"), true);

	TSharedRef<FJsonObject> Extracted = MakeShared<FJsonObject>();
	Extracted->SetBoolField(TEXT("bDoNotOverrideCompression"), true);
	Extracted->SetObjectField(TEXT("BoneCompressionSettings"), MakeAssetRefObject(DefaultBoneCompressionSettings));
	Extracted->SetObjectField(TEXT("CurveCompressionSettings"), MakeAssetRefObject(DefaultCurveCompressionSettings));

	TestEqual(
		TEXT("Project default compression asset refs hash like omitted default-diff fields"),
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(Authored), UAnimSequence::StaticClass()),
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(Extracted), UAnimSequence::StaticClass()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerAnimSequencePostApplyKeepsExtendedDefaultAssetRefsSemanticTest,
	"AssetDocument.RegionCanonicalizer.AnimSequencePostApply.KeepsExtendedDefaultAssetRefsSemantic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerAnimSequencePostApplyKeepsExtendedDefaultAssetRefsSemanticTest::RunTest(const FString& Parameters)
{
	const UObject* DefaultBoneCompressionSettings = FAnimationUtils::GetDefaultAnimationBoneCompressionSettings();
	TestNotNull(TEXT("Default bone compression settings exist"), DefaultBoneCompressionSettings);
	if (!DefaultBoneCompressionSettings)
	{
		return false;
	}

	FAssetDocumentRegionPolicy Policy = MakeAnimSequencePostApplyPolicy();
	Policy.RegionId = TEXT("Body.Compression");
	Policy.BodyPath = TEXT("Body.Compression");
	Policy.ManagedUePropertyPaths = {TEXT("BoneCompressionSettings")};

	TSharedRef<FJsonObject> Authored = MakeShared<FJsonObject>();
	Authored->SetBoolField(TEXT("bDoNotOverrideCompression"), true);

	TSharedRef<FJsonObject> ExtendedAssetRef = MakeAssetRefObject(DefaultBoneCompressionSettings);
	ExtendedAssetRef->SetStringField(TEXT("Note"), TEXT("AuthoredMetadata"));

	TSharedRef<FJsonObject> Extracted = MakeShared<FJsonObject>();
	Extracted->SetBoolField(TEXT("bDoNotOverrideCompression"), true);
	Extracted->SetObjectField(TEXT("BoneCompressionSettings"), ExtendedAssetRef);

	TestNotEqual(
		TEXT("Default AssetRef objects with authored extension fields remain semantic"),
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(Authored), UAnimSequence::StaticClass()),
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(Extracted), UAnimSequence::StaticClass()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerAnimSequencePostApplyNormalizesCurveArraysTest,
	"AssetDocument.RegionCanonicalizer.AnimSequencePostApply.NormalizesCurveArrays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerAnimSequencePostApplyNormalizesCurveArraysTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionPolicy Policy = MakeAnimSequencePostApplyPolicy();
	Policy.RegionId = TEXT("Body.Curves");
	Policy.BodyPath = TEXT("Body.Curves");

	TArray<TSharedPtr<FJsonValue>> AuthoredCurves;
	AuthoredCurves.Add(MakeCurveObjectValue(TEXT("Speed"), TEXT("Linear"), 100.0));
	AuthoredCurves.Add(MakeCurveObjectValue(TEXT("Lean"), TEXT("Linear"), 1.0));

	TArray<TSharedPtr<FJsonValue>> ExtractedCurves;
	ExtractedCurves.Add(MakeCurveObjectValue(TEXT("lean"), TEXT("RCIM_Linear"), 1.0));
	ExtractedCurves.Add(MakeCurveObjectValue(TEXT("speed"), TEXT("RCIM_Linear"), 100.0));

	TestEqual(
		TEXT("Curve name case, rich-curve enum prefix, and array order are normalized"),
		HashAnimSequencePostApplyRegion(Policy, MakeShared<FJsonValueArray>(AuthoredCurves), UAnimSequence::StaticClass()),
		HashAnimSequencePostApplyRegion(Policy, MakeShared<FJsonValueArray>(ExtractedCurves), UAnimSequence::StaticClass()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerAnimSequencePostApplyNormalizesTimelineFloatFieldsTest,
	"AssetDocument.RegionCanonicalizer.AnimSequencePostApply.NormalizesTimelineFloatFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerAnimSequencePostApplyNormalizesTimelineFloatFieldsTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionPolicy Policy = MakeAnimSequencePostApplyPolicy();
	Policy.RegionId = TEXT("Body.NotifyStates");
	Policy.BodyPath = TEXT("Body.NotifyStates");
	Policy.RegionKind = EAssetDocumentRegionKind::Timeline;

	TSharedRef<FJsonObject> AuthoredNotifyState = MakeShared<FJsonObject>();
	AuthoredNotifyState->SetStringField(TEXT("Name"), TEXT("Window"));
	AuthoredNotifyState->SetNumberField(TEXT("Time"), 0.2);
	AuthoredNotifyState->SetNumberField(TEXT("Duration"), 0.3);

	TSharedRef<FJsonObject> ExtractedNotifyState = MakeShared<FJsonObject>();
	ExtractedNotifyState->SetStringField(TEXT("Name"), TEXT("Window"));
	ExtractedNotifyState->SetNumberField(TEXT("Time"), static_cast<float>(0.2));
	ExtractedNotifyState->SetNumberField(TEXT("Duration"), static_cast<float>(0.3));

	TestEqual(
		TEXT("Timeline Time and Duration fields hash at reflected float precision"),
		HashAnimSequencePostApplyRegion(Policy, MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{RegionCanonicalizerTestMakeObjectValue(AuthoredNotifyState)}), UAnimSequence::StaticClass()),
		HashAnimSequencePostApplyRegion(Policy, MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{RegionCanonicalizerTestMakeObjectValue(ExtractedNotifyState)}), UAnimSequence::StaticClass()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerAnimSequencePostApplyNormalizesTimelineArrayOrderTest,
	"AssetDocument.RegionCanonicalizer.AnimSequencePostApply.NormalizesTimelineArrayOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerAnimSequencePostApplyNormalizesTimelineArrayOrderTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionPolicy Policy = MakeAnimSequencePostApplyPolicy();
	Policy.RegionId = TEXT("Body.SyncMarkers");
	Policy.BodyPath = TEXT("Body.SyncMarkers");
	Policy.RegionKind = EAssetDocumentRegionKind::Timeline;

	TSharedRef<FJsonObject> AuthoredRight = MakeShared<FJsonObject>();
	AuthoredRight->SetStringField(TEXT("Name"), TEXT("RightFoot"));
	AuthoredRight->SetNumberField(TEXT("Time"), 0.6);
	TSharedRef<FJsonObject> AuthoredLeft = MakeShared<FJsonObject>();
	AuthoredLeft->SetStringField(TEXT("Name"), TEXT("LeftFoot"));
	AuthoredLeft->SetNumberField(TEXT("Time"), 0.1);

	TSharedRef<FJsonObject> ExtractedLeft = MakeShared<FJsonObject>();
	ExtractedLeft->SetStringField(TEXT("Name"), TEXT("LeftFoot"));
	ExtractedLeft->SetNumberField(TEXT("Time"), static_cast<float>(0.1));
	TSharedRef<FJsonObject> ExtractedRight = MakeShared<FJsonObject>();
	ExtractedRight->SetStringField(TEXT("Name"), TEXT("RightFoot"));
	ExtractedRight->SetNumberField(TEXT("Time"), static_cast<float>(0.6));

	TestEqual(
		TEXT("Timeline arrays hash independently of post-apply sort order"),
		HashAnimSequencePostApplyRegion(Policy, MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{RegionCanonicalizerTestMakeObjectValue(AuthoredRight), RegionCanonicalizerTestMakeObjectValue(AuthoredLeft)}), UAnimSequence::StaticClass()),
		HashAnimSequencePostApplyRegion(Policy, MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{RegionCanonicalizerTestMakeObjectValue(ExtractedLeft), RegionCanonicalizerTestMakeObjectValue(ExtractedRight)}), UAnimSequence::StaticClass()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerAnimSequencePostApplyNormalizesTimelineArrayTieBreakerTest,
	"AssetDocument.RegionCanonicalizer.AnimSequencePostApply.NormalizesTimelineArrayTieBreaker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerAnimSequencePostApplyNormalizesTimelineArrayTieBreakerTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionPolicy Policy = MakeAnimSequencePostApplyPolicy();
	Policy.RegionId = TEXT("Body.NotifyStates");
	Policy.BodyPath = TEXT("Body.NotifyStates");
	Policy.RegionKind = EAssetDocumentRegionKind::Timeline;

	TSharedRef<FJsonObject> FirstShort = MakeShared<FJsonObject>();
	FirstShort->SetStringField(TEXT("Name"), TEXT("Window"));
	FirstShort->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimNotifyState"));
	FirstShort->SetNumberField(TEXT("Time"), 0.25);
	FirstShort->SetNumberField(TEXT("Duration"), 0.1);
	TSharedRef<FJsonObject> FirstLong = MakeShared<FJsonObject>();
	FirstLong->SetStringField(TEXT("Name"), TEXT("Window"));
	FirstLong->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimNotifyState"));
	FirstLong->SetNumberField(TEXT("Time"), 0.25);
	FirstLong->SetNumberField(TEXT("Duration"), 0.5);

	TSharedRef<FJsonObject> SecondLong = MakeShared<FJsonObject>();
	SecondLong->SetStringField(TEXT("Name"), TEXT("Window"));
	SecondLong->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimNotifyState"));
	SecondLong->SetNumberField(TEXT("Time"), static_cast<float>(0.25));
	SecondLong->SetNumberField(TEXT("Duration"), static_cast<float>(0.5));
	TSharedRef<FJsonObject> SecondShort = MakeShared<FJsonObject>();
	SecondShort->SetStringField(TEXT("Name"), TEXT("Window"));
	SecondShort->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimNotifyState"));
	SecondShort->SetNumberField(TEXT("Time"), static_cast<float>(0.25));
	SecondShort->SetNumberField(TEXT("Duration"), static_cast<float>(0.1));

	TestEqual(
		TEXT("Timeline sort uses a deterministic semantic tie-breaker"),
		HashAnimSequencePostApplyRegion(Policy, MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{RegionCanonicalizerTestMakeObjectValue(FirstShort), RegionCanonicalizerTestMakeObjectValue(FirstLong)}), UAnimSequence::StaticClass()),
		HashAnimSequencePostApplyRegion(Policy, MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{RegionCanonicalizerTestMakeObjectValue(SecondLong), RegionCanonicalizerTestMakeObjectValue(SecondShort)}), UAnimSequence::StaticClass()));

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
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(First)),
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(Second)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerAnimSequencePostApplyKeepsPropertiesTimeSemanticTest,
	"AssetDocument.RegionCanonicalizer.AnimSequencePostApply.KeepsPropertiesTimeSemantic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerAnimSequencePostApplyKeepsPropertiesTimeSemanticTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionPolicy Policy = MakeAnimSequencePostApplyPolicy();
	Policy.RegionId = TEXT("Body.Metadata");
	Policy.BodyPath = TEXT("Body.Metadata");
	Policy.RegionKind = EAssetDocumentRegionKind::Array;

	TSharedRef<FJsonObject> FirstProperties = MakeShared<FJsonObject>();
	FirstProperties->SetNumberField(TEXT("Time"), 1.00000001);
	FirstProperties->SetNumberField(TEXT("Duration"), 2.00000001);
	TSharedRef<FJsonObject> First = MakeShared<FJsonObject>();
	First->SetStringField(TEXT("Name"), TEXT("AuthoredMetadata"));
	First->SetObjectField(TEXT("Properties"), FirstProperties);

	TSharedRef<FJsonObject> SecondProperties = MakeShared<FJsonObject>();
	SecondProperties->SetNumberField(TEXT("Time"), 1.00000002);
	SecondProperties->SetNumberField(TEXT("Duration"), 2.00000002);
	TSharedRef<FJsonObject> Second = MakeShared<FJsonObject>();
	Second->SetStringField(TEXT("Name"), TEXT("AuthoredMetadata"));
	Second->SetObjectField(TEXT("Properties"), SecondProperties);

	TestNotEqual(
		TEXT("Non-timeline Properties.Time and Properties.Duration remain semantic"),
		HashAnimSequencePostApplyRegion(Policy, MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{RegionCanonicalizerTestMakeObjectValue(First)}), UAnimSequence::StaticClass()),
		HashAnimSequencePostApplyRegion(Policy, MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{RegionCanonicalizerTestMakeObjectValue(Second)}), UAnimSequence::StaticClass()));

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
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(WithoutDiagnostics)),
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(WithDiagnostics)));

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
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(First)),
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(Second)));

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
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(First)),
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(Second)));

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
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(First)),
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(Second)));

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
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(FirstName)),
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(SecondName)));

	TSharedRef<FJsonObject> FirstClass = MakeShared<FJsonObject>();
	FirstClass->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimNotify"));
	TSharedRef<FJsonObject> SecondClass = MakeShared<FJsonObject>();
	SecondClass->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimNotifyState"));
	TestNotEqual(
		TEXT("Class remains hash-significant"),
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(FirstClass)),
		HashAnimSequencePostApplyRegion(Policy, RegionCanonicalizerTestMakeObjectValue(SecondClass)));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerGraphIgnoresMemberGuidTest,
	"AssetDocument.RegionCanonicalizer.Graph.IgnoresMemberGuid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerGraphIgnoresMemberGuidTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionPolicy Policy;
	Policy.RegionId = TEXT("Body.UbergraphPages");
	Policy.BodyPath = TEXT("Body.UbergraphPages");
	Policy.RegionKind = EAssetDocumentRegionKind::Graph;
	Policy.CanonicalizerHookName = TEXT("UBlueprintGraph");

	FAssetDocumentRegionCanonicalizeContext Context;
	Context.Policy = &Policy;
	Context.Source = EAssetDocumentRegionCanonicalizeSource::AssetEvidence;

	TSharedPtr<FJsonObject> WithoutGuidNode = RegionCanonicalizerTestMakeGraphNode(
		TEXT("print_string"),
		TEXT("/Script/BlueprintGraph.K2Node_CallFunction"),
		MakeGraphMemberWithClassAndGuid(TEXT("/Script/Engine.KismetSystemLibrary"), TEXT("PrintString"), nullptr));
	TSharedPtr<FJsonObject> WithGuidNode = RegionCanonicalizerTestMakeGraphNode(
		TEXT("print_string"),
		TEXT("/Script/BlueprintGraph.K2Node_CallFunction"),
		MakeGraphMemberWithClassAndGuid(TEXT("/Script/Engine.KismetSystemLibrary"), TEXT("PrintString"), TEXT("11111111111111111111111111111111")));
	TSharedPtr<FJsonObject> WithDifferentGuidNode = RegionCanonicalizerTestMakeGraphNode(
		TEXT("print_string"),
		TEXT("/Script/BlueprintGraph.K2Node_CallFunction"),
		MakeGraphMemberWithClassAndGuid(TEXT("/Script/Engine.KismetSystemLibrary"), TEXT("PrintString"), TEXT("22222222222222222222222222222222")));

	TestEqual(
		TEXT("Member.Guid omission hashes equally"),
		FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, MakeSingleNodeGraphRegionValue(WithoutGuidNode)),
		FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, MakeSingleNodeGraphRegionValue(WithGuidNode)));
	TestEqual(
		TEXT("Member.Guid differences hash equally"),
		FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, MakeSingleNodeGraphRegionValue(WithGuidNode)),
		FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, MakeSingleNodeGraphRegionValue(WithDifferentGuidNode)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerGraphKeepsMemberSemanticsTest,
	"AssetDocument.RegionCanonicalizer.Graph.KeepsMemberSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerGraphKeepsMemberSemanticsTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionPolicy Policy;
	Policy.RegionId = TEXT("Body.UbergraphPages");
	Policy.BodyPath = TEXT("Body.UbergraphPages");
	Policy.RegionKind = EAssetDocumentRegionKind::Graph;
	Policy.CanonicalizerHookName = TEXT("UBlueprintGraph");

	FAssetDocumentRegionCanonicalizeContext Context;
	Context.Policy = &Policy;
	Context.Source = EAssetDocumentRegionCanonicalizeSource::AssetEvidence;

	TSharedPtr<FJsonObject> BaselineNode = RegionCanonicalizerTestMakeGraphNode(
		TEXT("print_string"),
		TEXT("/Script/BlueprintGraph.K2Node_CallFunction"),
		MakeGraphMemberWithClassAndGuid(TEXT("/Script/Engine.KismetSystemLibrary"), TEXT("PrintString"), TEXT("11111111111111111111111111111111")));
	TSharedPtr<FJsonObject> DifferentNameNode = RegionCanonicalizerTestMakeGraphNode(
		TEXT("print_string"),
		TEXT("/Script/BlueprintGraph.K2Node_CallFunction"),
		MakeGraphMemberWithClassAndGuid(TEXT("/Script/Engine.KismetSystemLibrary"), TEXT("Delay"), TEXT("11111111111111111111111111111111")));
	TSharedPtr<FJsonObject> DifferentClassNode = RegionCanonicalizerTestMakeGraphNode(
		TEXT("print_string"),
		TEXT("/Script/BlueprintGraph.K2Node_CallFunction"),
		MakeGraphMemberWithClassAndGuid(TEXT("/Script/Engine.GameplayStatics"), TEXT("PrintString"), TEXT("11111111111111111111111111111111")));

	TestNotEqual(
		TEXT("Member.Name remains hash-significant"),
		FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, MakeSingleNodeGraphRegionValue(BaselineNode)),
		FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, MakeSingleNodeGraphRegionValue(DifferentNameNode)));
	TestNotEqual(
		TEXT("Member.Class remains hash-significant"),
		FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, MakeSingleNodeGraphRegionValue(BaselineNode)),
		FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, MakeSingleNodeGraphRegionValue(DifferentClassNode)));

	return true;
}

#endif
