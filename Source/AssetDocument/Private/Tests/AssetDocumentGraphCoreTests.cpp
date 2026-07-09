// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/AssetDocumentGraphParser.h"
#include "Graphs/AssetDocumentGraphDefinitionResolver.h"
#include "Graphs/AssetDocumentGraphDiff.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
TSharedPtr<FJsonObject> ParseJsonObject(const FString& Json)
{
	TSharedPtr<FJsonObject> Object;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	FJsonSerializer::Deserialize(Reader, Object);
	return Object;
}

TSharedPtr<FJsonObject> MakeValidEventGraph()
{
	return ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    {
      "Id": "BeginPlay",
      "Class": "/Script/BlueprintGraph.K2Node_Event",
      "Member": {
        "Kind": "MemberRef",
        "OwnerClass": "/Script/Engine.Actor",
        "Name": "ReceiveBeginPlay"
      },
      "Position": { "X": 0, "Y": 0 }
    }
  ],
  "Links": []
}
)JSON"));
}

FAssetDocumentGraphParseResult ParseGraphs(TArray<TSharedPtr<FJsonValue>> Values)
{
	FAssetDocumentGraphParseOptions Options;
	Options.Path = TEXT("/Body/UbergraphPages");
	return FAssetDocumentGraphParser::ParseGraphArray(Values, Options);
}

bool HasDiagnosticCode(const FAssetDocumentGraphParseResult& Result, const FString& Code)
{
	return Result.Diagnostics.ContainsByPredicate(
		[&Code](const FAssetDocumentGraphDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == Code;
		});
}

bool HasDiagnosticCodeAtPath(const FAssetDocumentGraphParseResult& Result, const FString& Code, const FString& Path)
{
	return Result.Diagnostics.ContainsByPredicate(
		[&Code, &Path](const FAssetDocumentGraphDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == Code && Diagnostic.Path == Path;
		});
}

bool HasDiagnosticCode(const FAssetDocumentGraphDefinitionResolveResult& Result, const FString& Code)
{
	return Result.Diagnostics.ContainsByPredicate(
		[&Code](const FAssetDocumentGraphDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == Code;
		});
}

bool HasDiagnosticCodeAtPath(const FAssetDocumentGraphDefinitionResolveResult& Result, const FString& Code, const FString& Path)
{
	return Result.Diagnostics.ContainsByPredicate(
		[&Code, &Path](const FAssetDocumentGraphDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == Code && Diagnostic.Path == Path;
		});
}

TSharedPtr<FJsonObject> MakeDefinitions()
{
	return ParseJsonObject(TEXT(R"JSON(
{
  "Func.KismetSystemLibrary.PrintString": {
    "Kind": "MemberRef",
    "OwnerClass": "/Script/Engine.KismetSystemLibrary",
    "Name": "PrintString"
  }
}
)JSON"));
}

FAssetDocumentGraphDefinitionResolveResult ResolveGraphs(
	const TArray<FAssetDocumentGraphSpec>& Graphs,
	const TSharedPtr<FJsonObject>& Definitions)
{
	FAssetDocumentGraphDefinitionResolveOptions Options;
	Options.DefinitionsPath = TEXT("/Definitions");
	Options.GraphsPath = TEXT("/Body/UbergraphPages");
	return FAssetDocumentGraphDefinitionResolver::ResolveGraphArray(Graphs, Definitions, Options);
}

bool HasDiffStatusAtPath(
	const TArray<FAssetDocumentGraphDiffEntry>& Entries,
	const FString& Path,
	const FString& Status)
{
	return Entries.ContainsByPredicate(
		[&Path, &Status](const FAssetDocumentGraphDiffEntry& Entry)
		{
			return Entry.Path == Path && Entry.Status == Status;
		});
}

TSharedPtr<FJsonObject> MakeRecursiveGraphRegion()
{
	return ParseJsonObject(TEXT(R"JSON(
{
  "Graphs": [
    {
      "Id": "Locomotion",
      "Kind": "StateMachine",
      "Owner": { "StateMachine": "Locomotion" },
      "OwnerNodeId": "StateMachineNode",
      "OwnerPin": "OutputPose",
      "EntryPins": ["Entry"],
      "ResultPins": ["Result"],
      "Position": { "X": 10, "Y": 20 },
      "Metadata": { "EditorCategory": "Movement" },
      "Diagnostics": [],
      "_Skipped": { "Unsupported": [] },
      "Evidence": { "GraphGuid": "root-guid" },
      "Nodes": [
        {
          "Id": "IdleState",
          "Class": "/Script/AnimGraph.AnimStateNode",
          "Kind": "State",
          "Spawner": { "ActionKey": "AnimState" },
          "Fields": { "DisplayName": "Idle" },
          "Pins": { "Pose": { "Direction": "Output" } },
          "SubgraphRefs": { "StatePose": "IdlePose" },
          "Position": { "X": 120, "Y": 80 },
          "Evidence": { "NodeGuid": "idle-node-guid" }
        }
      ],
      "Links": [],
      "Subgraphs": [
        {
          "Id": "IdleToRunRule",
          "Kind": "TransitionRule",
          "Owner": { "StateMachine": "Locomotion", "Transition": "IdleToRun" },
          "Nodes": [
            {
              "Id": "SpeedCheck",
              "Class": "/Script/BlueprintGraph.K2Node_VariableGet",
              "Fields": { "Variable": "Speed" },
              "Position": { "X": 320, "Y": 160 }
            }
          ],
          "Links": [],
          "Subgraphs": []
        }
      ]
    }
  ]
}
)JSON"));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreParseValidEventGraphTest,
	"AssetFactory.AssetDocument.GraphCore.ParseValidEventGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreParseValidEventGraphTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentGraphParseResult Result = ParseGraphs({
		MakeShared<FJsonValueObject>(MakeValidEventGraph().ToSharedRef())
	});

	TestTrue(TEXT("Valid graph parses"), Result.IsValid());
	TestEqual(TEXT("One graph parsed"), Result.Graphs.Num(), 1);
	if (Result.Graphs.Num() == 1)
	{
		TestEqual(TEXT("Graph name is preserved"), Result.Graphs[0].Name, FString(TEXT("EventGraph")));
		TestEqual(TEXT("Node id is preserved"), Result.Graphs[0].Nodes[0].Id, FString(TEXT("BeginPlay")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreGraphArrayPreservesSharedGraphFieldsTest,
	"AssetFactory.AssetDocument.GraphCore.GraphArrayPreservesSharedGraphFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreGraphArrayPreservesSharedGraphFieldsTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Graph = MakeValidEventGraph();
	Graph->SetStringField(TEXT("OwnerNodeId"), TEXT("OwnerNode"));
	Graph->SetStringField(TEXT("OwnerPin"), TEXT("Output"));
	Graph->SetArrayField(TEXT("EntryPins"), { MakeShared<FJsonValueString>(TEXT("Entry")) });
	Graph->SetArrayField(TEXT("ResultPins"), { MakeShared<FJsonValueString>(TEXT("Result")) });
	Graph->SetObjectField(TEXT("Metadata"), ParseJsonObject(TEXT(R"JSON({ "Category": "Shared" })JSON")));
	Graph->SetArrayField(TEXT("Diagnostics"), {});
	Graph->SetObjectField(TEXT("_Skipped"), ParseJsonObject(TEXT(R"JSON({ "Unsupported": [] })JSON")));

	const FAssetDocumentGraphParseResult Result = ParseGraphs({
		MakeShared<FJsonValueObject>(Graph.ToSharedRef())
	});

	TestTrue(TEXT("Graph array with shared graph fields parses"), Result.IsValid());
	TestEqual(TEXT("One graph parsed"), Result.Graphs.Num(), 1);
	if (Result.Graphs.Num() == 1)
	{
		const FAssetDocumentGraphSpec& ParsedGraph = Result.Graphs[0];
		TestEqual(TEXT("OwnerNodeId is preserved"), ParsedGraph.OwnerNodeId, FString(TEXT("OwnerNode")));
		TestEqual(TEXT("OwnerPin is preserved"), ParsedGraph.OwnerPin, FString(TEXT("Output")));
		TestTrue(TEXT("EntryPins is preserved"), ParsedGraph.EntryPins.IsValid());
		TestTrue(TEXT("ResultPins is preserved"), ParsedGraph.ResultPins.IsValid());
		TestTrue(TEXT("Metadata is preserved"), ParsedGraph.Metadata.IsValid());
		TestTrue(TEXT("Diagnostics is preserved"), ParsedGraph.Diagnostics.IsValid());
		TestTrue(TEXT("_Skipped is preserved"), ParsedGraph.UnderscoreSkipped.IsValid());
	}

	const TSharedRef<FJsonValue> Canonical = FAssetDocumentGraphParser::WriteCanonicalGraphArray(Result.Graphs);
	const TSharedPtr<FJsonObject> CanonicalGraph = Canonical->AsArray()[0]->AsObject();
	TestEqual(TEXT("Canonical OwnerNodeId is written"), CanonicalGraph->GetStringField(TEXT("OwnerNodeId")), FString(TEXT("OwnerNode")));
	TestTrue(TEXT("Canonical EntryPins is written"), CanonicalGraph->HasTypedField<EJson::Array>(TEXT("EntryPins")));
	TestTrue(TEXT("Canonical Metadata is written"), CanonicalGraph->HasTypedField<EJson::Object>(TEXT("Metadata")));
	TestTrue(TEXT("Canonical _Skipped is written"), CanonicalGraph->HasTypedField<EJson::Object>(TEXT("_Skipped")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreRejectDuplicateGraphNamesTest,
	"AssetFactory.AssetDocument.GraphCore.RejectDuplicateGraphNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreRejectDuplicateGraphNamesTest::RunTest(const FString& Parameters)
{
	const FAssetDocumentGraphParseResult Result = ParseGraphs({
		MakeShared<FJsonValueObject>(MakeValidEventGraph().ToSharedRef()),
		MakeShared<FJsonValueObject>(MakeValidEventGraph().ToSharedRef())
	});

	TestFalse(TEXT("Duplicate graph names fail"), Result.IsValid());
	TestTrue(TEXT("DuplicateGraphName diagnostic is emitted"), HasDiagnosticCode(Result, TEXT("DuplicateGraphName")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreRejectDuplicateNodeIdsTest,
	"AssetFactory.AssetDocument.GraphCore.RejectDuplicateNodeIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreRejectDuplicateNodeIdsTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Graph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    { "Id": "BeginPlay", "Class": "/Script/BlueprintGraph.K2Node_Event" },
    { "Id": "BeginPlay", "Class": "/Script/BlueprintGraph.K2Node_Event" }
  ],
  "Links": []
}
)JSON"));

	const FAssetDocumentGraphParseResult Result = ParseGraphs({ MakeShared<FJsonValueObject>(Graph.ToSharedRef()) });

	TestFalse(TEXT("Duplicate node ids fail"), Result.IsValid());
	TestTrue(TEXT("DuplicateGraphNodeId diagnostic is emitted"), HasDiagnosticCode(Result, TEXT("DuplicateGraphNodeId")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreRejectInvalidNodeIdTest,
	"AssetFactory.AssetDocument.GraphCore.RejectInvalidNodeId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreRejectInvalidNodeIdTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Graph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    { "Id": "1-BeginPlay", "Class": "/Script/BlueprintGraph.K2Node_Event" }
  ],
  "Links": []
}
)JSON"));

	const FAssetDocumentGraphParseResult Result = ParseGraphs({ MakeShared<FJsonValueObject>(Graph.ToSharedRef()) });

	TestFalse(TEXT("Invalid node id fails"), Result.IsValid());
	TestTrue(TEXT("InvalidGraphNodeId diagnostic is emitted"), HasDiagnosticCode(Result, TEXT("InvalidGraphNodeId")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreRejectDuplicateLinksTest,
	"AssetFactory.AssetDocument.GraphCore.RejectDuplicateLinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreRejectDuplicateLinksTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Graph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    { "Id": "BeginPlay", "Class": "/Script/BlueprintGraph.K2Node_Event" },
    { "Id": "Print", "Class": "/Script/BlueprintGraph.K2Node_CallFunction" }
  ],
  "Links": [
    { "From": { "Node": "BeginPlay", "Pin": "then" }, "To": { "Node": "Print", "Pin": "execute" } },
    { "From": { "Node": "BeginPlay", "Pin": "then" }, "To": { "Node": "Print", "Pin": "execute" } }
  ]
}
)JSON"));

	const FAssetDocumentGraphParseResult Result = ParseGraphs({ MakeShared<FJsonValueObject>(Graph.ToSharedRef()) });

	TestFalse(TEXT("Duplicate links fail"), Result.IsValid());
	TestTrue(TEXT("DuplicateGraphLink diagnostic is emitted"), HasDiagnosticCode(Result, TEXT("DuplicateGraphLink")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreAcceptCompactLinkInputAsSugarTest,
	"AssetFactory.AssetDocument.GraphCore.AcceptCompactLinkInputAsSugar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreAcceptCompactLinkInputAsSugarTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Graph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    { "Id": "BeginPlay", "Class": "/Script/BlueprintGraph.K2Node_Event" },
    { "Id": "Print", "Class": "/Script/BlueprintGraph.K2Node_CallFunction" }
  ],
  "Links": [
    { "From": "BeginPlay.then", "To": "Print.execute" }
  ]
}
)JSON"));

	const FAssetDocumentGraphParseResult Result = ParseGraphs({ MakeShared<FJsonValueObject>(Graph.ToSharedRef()) });
	TestTrue(TEXT("Compact link parses"), Result.IsValid());
	TestEqual(TEXT("One link parsed"), Result.Graphs[0].Links.Num(), 1);
	TestEqual(TEXT("Compact source node expands"), Result.Graphs[0].Links[0].From.Node, FString(TEXT("BeginPlay")));
	TestEqual(TEXT("Compact target pin expands"), Result.Graphs[0].Links[0].To.Pin, FString(TEXT("execute")));

	const TSharedRef<FJsonValue> Canonical = FAssetDocumentGraphParser::WriteCanonicalGraphArray(Result.Graphs);
	const TArray<TSharedPtr<FJsonValue>>& CanonicalGraphs = Canonical->AsArray();
	const TArray<TSharedPtr<FJsonValue>>& CanonicalLinks =
		CanonicalGraphs[0]->AsObject()->GetArrayField(TEXT("Links"));
	TestTrue(TEXT("Canonical link From is expanded object"), CanonicalLinks[0]->AsObject()->GetField<EJson::Object>(TEXT("From")).IsValid());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreRejectInvalidLinkEndpointIdsTest,
	"AssetFactory.AssetDocument.GraphCore.RejectInvalidLinkEndpointIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreRejectInvalidLinkEndpointIdsTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> ExpandedInvalidNodeGraph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    { "Id": "BeginPlay", "Class": "/Script/BlueprintGraph.K2Node_Event" },
    { "Id": "Print", "Class": "/Script/BlueprintGraph.K2Node_CallFunction" }
  ],
  "Links": [
    { "From": { "Node": "1BeginPlay", "Pin": "then" }, "To": { "Node": "Print", "Pin": "execute" } }
  ]
}
)JSON"));
	const FAssetDocumentGraphParseResult ExpandedInvalidNodeResult =
		ParseGraphs({ MakeShared<FJsonValueObject>(ExpandedInvalidNodeGraph.ToSharedRef()) });
	TestFalse(TEXT("Expanded endpoint with invalid node token fails"), ExpandedInvalidNodeResult.IsValid());
	TestTrue(
		TEXT("Expanded endpoint invalid node token uses InvalidGraphNodeId"),
		HasDiagnosticCode(ExpandedInvalidNodeResult, TEXT("InvalidGraphNodeId")));

	const TSharedPtr<FJsonObject> ExpandedInvalidPinGraph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    { "Id": "BeginPlay", "Class": "/Script/BlueprintGraph.K2Node_Event" },
    { "Id": "Print", "Class": "/Script/BlueprintGraph.K2Node_CallFunction" }
  ],
  "Links": [
    { "From": { "Node": "BeginPlay", "Pin": "1then" }, "To": { "Node": "Print", "Pin": "execute" } }
  ]
}
)JSON"));
	const FAssetDocumentGraphParseResult ExpandedInvalidPinResult =
		ParseGraphs({ MakeShared<FJsonValueObject>(ExpandedInvalidPinGraph.ToSharedRef()) });
	TestFalse(TEXT("Expanded endpoint with invalid pin token fails"), ExpandedInvalidPinResult.IsValid());
	TestTrue(
		TEXT("Expanded endpoint invalid pin token uses InvalidGraphPinId"),
		HasDiagnosticCode(ExpandedInvalidPinResult, TEXT("InvalidGraphPinId")));

	const TSharedPtr<FJsonObject> CompactInvalidNodeGraph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    { "Id": "BeginPlay", "Class": "/Script/BlueprintGraph.K2Node_Event" },
    { "Id": "Print", "Class": "/Script/BlueprintGraph.K2Node_CallFunction" }
  ],
  "Links": [
    { "From": "1BeginPlay.then", "To": "Print.execute" }
  ]
}
)JSON"));
	const FAssetDocumentGraphParseResult CompactInvalidNodeResult =
		ParseGraphs({ MakeShared<FJsonValueObject>(CompactInvalidNodeGraph.ToSharedRef()) });
	TestFalse(TEXT("Compact endpoint with invalid node token fails"), CompactInvalidNodeResult.IsValid());
	TestTrue(
		TEXT("Compact endpoint invalid node token uses InvalidGraphNodeId"),
		HasDiagnosticCode(CompactInvalidNodeResult, TEXT("InvalidGraphNodeId")));

	const TSharedPtr<FJsonObject> CompactInvalidPinGraph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    { "Id": "BeginPlay", "Class": "/Script/BlueprintGraph.K2Node_Event" },
    { "Id": "Print", "Class": "/Script/BlueprintGraph.K2Node_CallFunction" }
  ],
  "Links": [
    { "From": "BeginPlay.1then", "To": "Print.execute" }
  ]
}
)JSON"));
	const FAssetDocumentGraphParseResult CompactInvalidPinResult =
		ParseGraphs({ MakeShared<FJsonValueObject>(CompactInvalidPinGraph.ToSharedRef()) });
	TestFalse(TEXT("Compact endpoint with invalid pin token fails"), CompactInvalidPinResult.IsValid());
	TestTrue(
		TEXT("Compact endpoint invalid pin token uses InvalidGraphPinId"),
		HasDiagnosticCode(CompactInvalidPinResult, TEXT("InvalidGraphPinId")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreRejectUnknownGraphFieldTest,
	"AssetFactory.AssetDocument.GraphCore.RejectUnknownGraphField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreRejectUnknownGraphFieldTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Graph = MakeValidEventGraph();
	Graph->SetStringField(TEXT("Unexpected"), TEXT("nope"));

	const FAssetDocumentGraphParseResult Result = ParseGraphs({ MakeShared<FJsonValueObject>(Graph.ToSharedRef()) });

	TestFalse(TEXT("Unknown graph fields fail"), Result.IsValid());
	TestTrue(TEXT("UnknownGraphField diagnostic is emitted"), HasDiagnosticCode(Result, TEXT("UnknownGraphField")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreRejectInvalidNestedFieldTypesTest,
	"AssetFactory.AssetDocument.GraphCore.RejectInvalidNestedFieldTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreRejectInvalidNestedFieldTypesTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> InvalidSignatureGraph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Signature": "not-an-object",
  "Nodes": [],
  "Links": []
}
)JSON"));
	const FAssetDocumentGraphParseResult InvalidSignatureResult =
		ParseGraphs({ MakeShared<FJsonValueObject>(InvalidSignatureGraph.ToSharedRef()) });
	TestFalse(TEXT("Graph Signature with invalid type fails"), InvalidSignatureResult.IsValid());
	TestTrue(
		TEXT("InvalidGraphSignature diagnostic is emitted"),
		HasDiagnosticCode(InvalidSignatureResult, TEXT("InvalidGraphSignature")));

	const TSharedPtr<FJsonObject> InvalidMemberGraph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    {
      "Id": "BeginPlay",
      "Class": "/Script/BlueprintGraph.K2Node_Event",
      "Member": "not-an-object"
    }
  ],
  "Links": []
}
)JSON"));
	const FAssetDocumentGraphParseResult InvalidMemberResult =
		ParseGraphs({ MakeShared<FJsonValueObject>(InvalidMemberGraph.ToSharedRef()) });
	TestFalse(TEXT("Node Member with invalid type fails"), InvalidMemberResult.IsValid());
	TestTrue(
		TEXT("InvalidGraphMemberReference diagnostic is emitted"),
		HasDiagnosticCode(InvalidMemberResult, TEXT("InvalidGraphMemberReference")));

	const TSharedPtr<FJsonObject> InvalidPositionGraph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    {
      "Id": "BeginPlay",
      "Class": "/Script/BlueprintGraph.K2Node_Event",
      "Position": "not-an-object"
    }
  ],
  "Links": []
}
)JSON"));
	const FAssetDocumentGraphParseResult InvalidPositionResult =
		ParseGraphs({ MakeShared<FJsonValueObject>(InvalidPositionGraph.ToSharedRef()) });
	TestFalse(TEXT("Node Position with invalid type fails"), InvalidPositionResult.IsValid());
	TestTrue(
		TEXT("InvalidGraphPosition diagnostic is emitted"),
		HasDiagnosticCode(InvalidPositionResult, TEXT("InvalidGraphPosition")));

	const TSharedPtr<FJsonObject> InvalidPinOverridesGraph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    {
      "Id": "BeginPlay",
      "Class": "/Script/BlueprintGraph.K2Node_Event",
      "PinOverrides": "not-an-array"
    }
  ],
  "Links": []
}
)JSON"));
	const FAssetDocumentGraphParseResult InvalidPinOverridesResult =
		ParseGraphs({ MakeShared<FJsonValueObject>(InvalidPinOverridesGraph.ToSharedRef()) });
	TestFalse(TEXT("Node PinOverrides with invalid type fails"), InvalidPinOverridesResult.IsValid());
	TestTrue(
		TEXT("InvalidGraphPin diagnostic is emitted for invalid PinOverrides"),
		HasDiagnosticCode(InvalidPinOverridesResult, TEXT("InvalidGraphPin")));

	const TSharedPtr<FJsonObject> InvalidPinTypeGraph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    {
      "Id": "BeginPlay",
      "Class": "/Script/BlueprintGraph.K2Node_Event",
      "PinOverrides": [
        { "Pin": "then", "Type": "not-an-object" }
      ]
    }
  ],
  "Links": []
}
)JSON"));
	const FAssetDocumentGraphParseResult InvalidPinTypeResult =
		ParseGraphs({ MakeShared<FJsonValueObject>(InvalidPinTypeGraph.ToSharedRef()) });
	TestFalse(TEXT("Pin override Type with invalid type fails"), InvalidPinTypeResult.IsValid());
	TestTrue(
		TEXT("InvalidGraphPin diagnostic is emitted for invalid pin Type"),
		HasDiagnosticCode(InvalidPinTypeResult, TEXT("InvalidGraphPin")));

	const TSharedPtr<FJsonObject> InvalidPinDirectionGraph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    {
      "Id": "BeginPlay",
      "Class": "/Script/BlueprintGraph.K2Node_Event",
      "PinOverrides": [
        { "Pin": "then", "Direction": 42 }
      ]
    }
  ],
  "Links": []
}
)JSON"));
	const FAssetDocumentGraphParseResult InvalidPinDirectionResult =
		ParseGraphs({ MakeShared<FJsonValueObject>(InvalidPinDirectionGraph.ToSharedRef()) });
	TestFalse(TEXT("Pin override Direction with invalid type fails"), InvalidPinDirectionResult.IsValid());
	TestTrue(
		TEXT("InvalidGraphPin diagnostic is emitted for invalid pin Direction"),
		HasDiagnosticCode(InvalidPinDirectionResult, TEXT("InvalidGraphPin")));

	const TSharedPtr<FJsonObject> InvalidPinHiddenGraph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    {
      "Id": "BeginPlay",
      "Class": "/Script/BlueprintGraph.K2Node_Event",
      "PinOverrides": [
        { "Pin": "then", "Hidden": "yes" }
      ]
    }
  ],
  "Links": []
}
)JSON"));
	const FAssetDocumentGraphParseResult InvalidPinHiddenResult =
		ParseGraphs({ MakeShared<FJsonValueObject>(InvalidPinHiddenGraph.ToSharedRef()) });
	TestFalse(TEXT("Pin override Hidden with invalid type fails"), InvalidPinHiddenResult.IsValid());
	TestTrue(
		TEXT("InvalidGraphPin diagnostic is emitted for invalid pin Hidden"),
		HasDiagnosticCode(InvalidPinHiddenResult, TEXT("InvalidGraphPin")));

	const TSharedPtr<FJsonObject> InvalidPinAdvancedViewGraph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    {
      "Id": "BeginPlay",
      "Class": "/Script/BlueprintGraph.K2Node_Event",
      "PinOverrides": [
        { "Pin": "then", "AdvancedView": "yes" }
      ]
    }
  ],
  "Links": []
}
)JSON"));
	const FAssetDocumentGraphParseResult InvalidPinAdvancedViewResult =
		ParseGraphs({ MakeShared<FJsonValueObject>(InvalidPinAdvancedViewGraph.ToSharedRef()) });
	TestFalse(TEXT("Pin override AdvancedView with invalid type fails"), InvalidPinAdvancedViewResult.IsValid());
	TestTrue(
		TEXT("InvalidGraphPin diagnostic is emitted for invalid pin AdvancedView"),
		HasDiagnosticCode(InvalidPinAdvancedViewResult, TEXT("InvalidGraphPin")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreRejectUnknownLinkFieldTest,
	"AssetFactory.AssetDocument.GraphCore.RejectUnknownLinkField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreRejectUnknownLinkFieldTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Graph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    { "Id": "BeginPlay", "Class": "/Script/BlueprintGraph.K2Node_Event" },
    { "Id": "Print", "Class": "/Script/BlueprintGraph.K2Node_CallFunction" }
  ],
  "Links": [
    {
      "From": { "Node": "BeginPlay", "Pin": "then" },
      "To": { "Node": "Print", "Pin": "execute" },
      "Comment": "canonical serializer must not drop this"
    }
  ]
}
)JSON"));

	const FAssetDocumentGraphParseResult Result = ParseGraphs({ MakeShared<FJsonValueObject>(Graph.ToSharedRef()) });

	TestFalse(TEXT("Unknown link fields fail"), Result.IsValid());
	TestTrue(TEXT("UnknownGraphLinkField diagnostic is emitted"), HasDiagnosticCode(Result, TEXT("UnknownGraphLinkField")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreRejectInvalidLinkEndpointTypeTest,
	"AssetFactory.AssetDocument.GraphCore.RejectInvalidLinkEndpointType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreRejectInvalidLinkEndpointTypeTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Graph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    { "Id": "BeginPlay", "Class": "/Script/BlueprintGraph.K2Node_Event" },
    { "Id": "Print", "Class": "/Script/BlueprintGraph.K2Node_CallFunction" }
  ],
  "Links": [
    {
      "From": 42,
      "To": { "Node": "Print", "Pin": "execute" }
    }
  ]
}
)JSON"));

	const FAssetDocumentGraphParseResult Result = ParseGraphs({ MakeShared<FJsonValueObject>(Graph.ToSharedRef()) });

	TestFalse(TEXT("Invalid endpoint value type fails"), Result.IsValid());
	TestTrue(
		TEXT("InvalidGraphLinkEndpointSyntax diagnostic is emitted"),
		HasDiagnosticCode(Result, TEXT("InvalidGraphLinkEndpointSyntax")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreRejectUnknownEndpointObjectFieldTest,
	"AssetFactory.AssetDocument.GraphCore.RejectUnknownEndpointObjectField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreRejectUnknownEndpointObjectFieldTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Graph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    { "Id": "BeginPlay", "Class": "/Script/BlueprintGraph.K2Node_Event" },
    { "Id": "Print", "Class": "/Script/BlueprintGraph.K2Node_CallFunction" }
  ],
  "Links": [
    {
      "From": { "Node": "BeginPlay", "Pin": "then", "Role": "source" },
      "To": { "Node": "Print", "Pin": "execute" }
    }
  ]
}
)JSON"));

	const FAssetDocumentGraphParseResult Result = ParseGraphs({ MakeShared<FJsonValueObject>(Graph.ToSharedRef()) });

	TestFalse(TEXT("Unknown endpoint object fields fail"), Result.IsValid());
	TestTrue(
		TEXT("UnknownGraphLinkEndpointField diagnostic is emitted"),
		HasDiagnosticCode(Result, TEXT("UnknownGraphLinkEndpointField")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreResolveDefinitionRefsTest,
	"AssetFactory.AssetDocument.GraphCore.ResolveDefinitionRefs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreResolveDefinitionRefsTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Graph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    {
      "Id": "Print",
      "Class": "/Script/BlueprintGraph.K2Node_CallFunction",
      "Member": { "Kind": "DefinitionRef", "Id": "Func.KismetSystemLibrary.PrintString" }
    }
  ],
  "Links": []
}
)JSON"));
	const FAssetDocumentGraphParseResult ParseResult = ParseGraphs({ MakeShared<FJsonValueObject>(Graph.ToSharedRef()) });
	TestTrue(TEXT("Graph with DefinitionRef parses"), ParseResult.IsValid());

	const FAssetDocumentGraphDefinitionResolveResult ResolveResult = ResolveGraphs(ParseResult.Graphs, MakeDefinitions());
	TestTrue(TEXT("DefinitionRef resolves"), ResolveResult.IsValid());
	TestEqual(
		TEXT("Resolved member kind"),
		ResolveResult.Graphs[0].Nodes[0].Member->GetStringField(TEXT("Kind")),
		FString(TEXT("MemberRef")));
	TestEqual(
		TEXT("Resolved member name"),
		ResolveResult.Graphs[0].Nodes[0].Member->GetStringField(TEXT("Name")),
		FString(TEXT("PrintString")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreRejectCircularDefinitionRefsTest,
	"AssetFactory.AssetDocument.GraphCore.RejectCircularDefinitionRefs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreRejectCircularDefinitionRefsTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Definitions = ParseJsonObject(TEXT(R"JSON(
{
  "A": { "Kind": "DefinitionRef", "Id": "B" },
  "B": { "Kind": "DefinitionRef", "Id": "A" }
}
)JSON"));
	const TSharedPtr<FJsonObject> Graph = MakeValidEventGraph();
	Graph->GetArrayField(TEXT("Nodes"))[0]->AsObject()->SetObjectField(
		TEXT("Member"),
		ParseJsonObject(TEXT(R"JSON({ "Kind": "DefinitionRef", "Id": "A" })JSON")));

	const FAssetDocumentGraphParseResult ParseResult = ParseGraphs({ MakeShared<FJsonValueObject>(Graph.ToSharedRef()) });
	const FAssetDocumentGraphDefinitionResolveResult ResolveResult = ResolveGraphs(ParseResult.Graphs, Definitions);

	TestFalse(TEXT("Circular DefinitionRefs fail"), ResolveResult.IsValid());
	TestTrue(
		TEXT("CircularDefinitionReference diagnostic is emitted"),
		HasDiagnosticCode(ResolveResult, TEXT("CircularDefinitionReference")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreRejectUnresolvedDefinitionRefTest,
	"AssetFactory.AssetDocument.GraphCore.RejectUnresolvedDefinitionRef",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreRejectUnresolvedDefinitionRefTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Graph = MakeValidEventGraph();
	Graph->GetArrayField(TEXT("Nodes"))[0]->AsObject()->SetObjectField(
		TEXT("Member"),
		ParseJsonObject(TEXT(R"JSON({ "Kind": "DefinitionRef", "Id": "Func.Missing" })JSON")));

	const FAssetDocumentGraphParseResult ParseResult = ParseGraphs({ MakeShared<FJsonValueObject>(Graph.ToSharedRef()) });
	const FAssetDocumentGraphDefinitionResolveResult ResolveResult = ResolveGraphs(ParseResult.Graphs, MakeDefinitions());

	TestFalse(TEXT("Unresolved DefinitionRef fails"), ResolveResult.IsValid());
	TestTrue(
		TEXT("UnresolvedDefinitionReference diagnostic is emitted"),
		HasDiagnosticCode(ResolveResult, TEXT("UnresolvedDefinitionReference")));

	const TSharedPtr<FJsonObject> UnknownKindDefinitions = ParseJsonObject(TEXT(R"JSON(
{
  "Broken.Definition": { "Kind": "MysteryRef", "Value": "nope" }
}
)JSON"));
	const FAssetDocumentGraphDefinitionResolveResult UnknownKindResult =
		ResolveGraphs(ParseResult.Graphs, UnknownKindDefinitions);
	TestFalse(TEXT("Unknown definition kind fails"), UnknownKindResult.IsValid());
	TestTrue(
		TEXT("UnknownDefinitionKind diagnostic is emitted"),
		HasDiagnosticCode(UnknownKindResult, TEXT("UnknownDefinitionKind")));

	const TSharedPtr<FJsonObject> EscapedPathDefinitions = ParseJsonObject(TEXT(R"JSON(
{
  "Bad/Definition~Name": { "Kind": "MysteryRef", "Value": "nope" }
}
)JSON"));
	const FAssetDocumentGraphDefinitionResolveResult EscapedPathResult =
		ResolveGraphs(ParseResult.Graphs, EscapedPathDefinitions);
	TestFalse(TEXT("Definition path escaping case fails"), EscapedPathResult.IsValid());
	TestTrue(
		TEXT("Definition diagnostic path uses JSON Pointer escaping"),
		HasDiagnosticCodeAtPath(
			EscapedPathResult,
			TEXT("InvalidDefinitionId"),
			TEXT("/Definitions/Bad~1Definition~0Name")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreCompareInlineAndDefinitionRefAsEqualTest,
	"AssetFactory.AssetDocument.GraphCore.CompareInlineAndDefinitionRefAsEqual",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreCompareInlineAndDefinitionRefAsEqualTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> InlineGraph = MakeValidEventGraph();
	InlineGraph->GetArrayField(TEXT("Nodes"))[0]->AsObject()->SetStringField(
		TEXT("Class"),
		TEXT("/Script/BlueprintGraph.K2Node_CallFunction"));
	InlineGraph->GetArrayField(TEXT("Nodes"))[0]->AsObject()->SetStringField(TEXT("Id"), TEXT("Print"));
	InlineGraph->GetArrayField(TEXT("Nodes"))[0]->AsObject()->SetObjectField(
		TEXT("Member"),
		MakeDefinitions()->GetObjectField(TEXT("Func.KismetSystemLibrary.PrintString")));

	const TSharedPtr<FJsonObject> RefGraph = MakeValidEventGraph();
	RefGraph->GetArrayField(TEXT("Nodes"))[0]->AsObject()->SetStringField(
		TEXT("Class"),
		TEXT("/Script/BlueprintGraph.K2Node_CallFunction"));
	RefGraph->GetArrayField(TEXT("Nodes"))[0]->AsObject()->SetStringField(TEXT("Id"), TEXT("Print"));
	RefGraph->GetArrayField(TEXT("Nodes"))[0]->AsObject()->SetObjectField(
		TEXT("Member"),
		ParseJsonObject(TEXT(R"JSON({ "Kind": "DefinitionRef", "Id": "Func.KismetSystemLibrary.PrintString" })JSON")));

	const FAssetDocumentGraphParseResult InlineParseResult =
		ParseGraphs({ MakeShared<FJsonValueObject>(InlineGraph.ToSharedRef()) });
	const FAssetDocumentGraphParseResult RefParseResult =
		ParseGraphs({ MakeShared<FJsonValueObject>(RefGraph.ToSharedRef()) });
	const TArray<FAssetDocumentGraphDiffEntry> Entries = FAssetDocumentGraphDiff::CompareUbergraphPages(
		RefParseResult.Graphs,
		InlineParseResult.Graphs,
		MakeDefinitions());

	TestTrue(
		TEXT("Inline MemberRef and equivalent DefinitionRef compare as unchanged"),
		HasDiffStatusAtPath(Entries, TEXT("/Body/UbergraphPages/EventGraph/Nodes/Print"), TEXT("unchanged")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreReportMissingExtraChangedGraphDiffsTest,
	"AssetFactory.AssetDocument.GraphCore.ReportMissingExtraChangedGraphDiffs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreReportMissingExtraChangedGraphDiffsTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> DesiredGraph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    {
      "Id": "Print",
      "Class": "/Script/BlueprintGraph.K2Node_CallFunction",
      "Member": { "Kind": "DefinitionRef", "Id": "Func.KismetSystemLibrary.PrintString" },
      "PinOverrides": [
        { "Pin": "InString", "DefaultValue": "Desired" }
      ]
    },
    { "Id": "MissingInCurrent", "Class": "/Script/BlueprintGraph.K2Node_Self" }
  ],
  "Links": [
    { "From": "MissingInCurrent.self", "To": "Print.self" }
  ]
}
)JSON"));
	const TSharedPtr<FJsonObject> CurrentGraph = ParseJsonObject(TEXT(R"JSON(
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [
    {
      "Id": "Print",
      "Class": "/Script/BlueprintGraph.K2Node_CallFunction",
      "Member": {
        "Kind": "MemberRef",
        "OwnerClass": "/Script/Engine.KismetSystemLibrary",
        "Name": "PrintString"
      },
      "PinOverrides": [
        { "Pin": "InString", "DefaultValue": "Current" },
        { "Pin": "WorldContextObject", "DefaultValue": "Self" }
      ]
    },
    { "Id": "ExtraInCurrent", "Class": "/Script/BlueprintGraph.K2Node_Self" }
  ],
  "Links": [
    { "From": "ExtraInCurrent.self", "To": "Print.self" }
  ]
}
)JSON"));

	const FAssetDocumentGraphParseResult DesiredParseResult =
		ParseGraphs({ MakeShared<FJsonValueObject>(DesiredGraph.ToSharedRef()) });
	const FAssetDocumentGraphParseResult CurrentParseResult =
		ParseGraphs({ MakeShared<FJsonValueObject>(CurrentGraph.ToSharedRef()) });
	const TArray<FAssetDocumentGraphDiffEntry> Entries = FAssetDocumentGraphDiff::CompareUbergraphPages(
		DesiredParseResult.Graphs,
		CurrentParseResult.Graphs,
		MakeDefinitions());

	TestTrue(
		TEXT("Changed node is reported"),
		HasDiffStatusAtPath(Entries, TEXT("/Body/UbergraphPages/EventGraph/Nodes/Print"), TEXT("changed")));
	TestTrue(
		TEXT("Missing node is reported"),
		HasDiffStatusAtPath(Entries, TEXT("/Body/UbergraphPages/EventGraph/Nodes/MissingInCurrent"), TEXT("missing")));
	TestTrue(
		TEXT("Extra node is reported"),
		HasDiffStatusAtPath(Entries, TEXT("/Body/UbergraphPages/EventGraph/Nodes/ExtraInCurrent"), TEXT("extra")));
	TestTrue(
		TEXT("Changed pin is reported"),
		HasDiffStatusAtPath(
			Entries,
			TEXT("/Body/UbergraphPages/EventGraph/Nodes/Print/PinOverrides/InString"),
			TEXT("changed")));
	TestTrue(
		TEXT("Extra pin is reported"),
		HasDiffStatusAtPath(
			Entries,
			TEXT("/Body/UbergraphPages/EventGraph/Nodes/Print/PinOverrides/WorldContextObject"),
			TEXT("extra")));
	TestTrue(
		TEXT("Missing link is reported"),
		HasDiffStatusAtPath(
			Entries,
			TEXT("/Body/UbergraphPages/EventGraph/Links/MissingInCurrent:self->Print:self"),
			TEXT("missing")));
	TestTrue(
		TEXT("Extra link is reported"),
		HasDiffStatusAtPath(
			Entries,
			TEXT("/Body/UbergraphPages/EventGraph/Links/ExtraInCurrent:self->Print:self"),
			TEXT("extra")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreRecursiveGraphParserTest,
	"AssetFactory.AssetDocument.GraphCore.RecursiveGraphParser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreRecursiveGraphParserTest::RunTest(const FString& Parameters)
{
	FAssetDocumentGraphParseOptions Options;
	Options.Path = TEXT("/Body/StateMachines");
	const FAssetDocumentGraphParseResult Result =
		FAssetDocumentGraphParser::ParseGraphRegion(MakeRecursiveGraphRegion().ToSharedRef(), Options);

	TestTrue(TEXT("Recursive graph region parses"), Result.IsValid());
	TestEqual(TEXT("One root graph"), Result.Graphs.Num(), 1);
	if (Result.Graphs.Num() == 1)
	{
		const FAssetDocumentGraphSpec& RootGraph = Result.Graphs[0];
		TestEqual(TEXT("Root graph id"), RootGraph.Id, FString(TEXT("Locomotion")));
		TestEqual(TEXT("Root graph kind"), RootGraph.Kind, FString(TEXT("StateMachine")));
		TestTrue(TEXT("Root owner is preserved"), RootGraph.Owner.IsValid());
		TestEqual(TEXT("Owner node id is preserved"), RootGraph.OwnerNodeId, FString(TEXT("StateMachineNode")));
		TestEqual(TEXT("Owner pin is preserved"), RootGraph.OwnerPin, FString(TEXT("OutputPose")));
		TestTrue(TEXT("Entry pins are preserved"), RootGraph.EntryPins.IsValid());
		TestTrue(TEXT("Result pins are preserved"), RootGraph.ResultPins.IsValid());
		TestTrue(TEXT("Root position is preserved"), RootGraph.Position.IsValid());
		TestTrue(TEXT("Metadata is preserved"), RootGraph.Metadata.IsValid());
		TestTrue(TEXT("Diagnostics is preserved"), RootGraph.Diagnostics.IsValid());
		TestTrue(TEXT("_Skipped is preserved"), RootGraph.UnderscoreSkipped.IsValid());
		TestTrue(TEXT("Root evidence is preserved"), RootGraph.Evidence.IsValid());
		TestEqual(TEXT("Root node count"), RootGraph.Nodes.Num(), 1);
		TestEqual(TEXT("Nested graph count"), RootGraph.Subgraphs.Num(), 1);

		if (RootGraph.Nodes.Num() == 1)
		{
			const FAssetDocumentNodeSpec& Node = RootGraph.Nodes[0];
			TestEqual(TEXT("Node kind is preserved"), Node.Kind, FString(TEXT("State")));
			TestTrue(TEXT("Node spawner is preserved"), Node.Spawner.IsValid());
			TestTrue(TEXT("Node fields are preserved"), Node.Fields.IsValid());
			TestTrue(TEXT("Node pins are preserved"), Node.Pins.IsValid());
			TestTrue(TEXT("Node subgraph refs are preserved"), Node.SubgraphRefs.IsValid());
			TestTrue(TEXT("Node evidence is preserved"), Node.Evidence.IsValid());
		}

		if (RootGraph.Subgraphs.Num() == 1)
		{
			TestEqual(TEXT("Nested graph id"), RootGraph.Subgraphs[0].Id, FString(TEXT("IdleToRunRule")));
			TestEqual(TEXT("Nested graph kind"), RootGraph.Subgraphs[0].Kind, FString(TEXT("TransitionRule")));
			TestEqual(TEXT("Nested graph node id"), RootGraph.Subgraphs[0].Nodes[0].Id, FString(TEXT("SpeedCheck")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreWriteCanonicalRecursiveGraphRegionTest,
	"AssetFactory.AssetDocument.GraphCore.WriteCanonicalRecursiveGraphRegion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreWriteCanonicalRecursiveGraphRegionTest::RunTest(const FString& Parameters)
{
	FAssetDocumentGraphParseOptions Options;
	Options.Path = TEXT("/Body/StateMachines");
	const FAssetDocumentGraphParseResult Result =
		FAssetDocumentGraphParser::ParseGraphRegion(MakeRecursiveGraphRegion().ToSharedRef(), Options);
	TestTrue(TEXT("Recursive graph region parses before write"), Result.IsValid());

	const TSharedRef<FJsonObject> Canonical =
		FAssetDocumentGraphParser::WriteCanonicalGraphRegion(Result.Graphs);
	const TArray<TSharedPtr<FJsonValue>>& Graphs = Canonical->GetArrayField(TEXT("Graphs"));
	TestEqual(TEXT("Canonical root graph count"), Graphs.Num(), 1);

	const TSharedPtr<FJsonObject> RootGraph = Graphs[0]->AsObject();
	TestEqual(TEXT("Canonical root graph id"), RootGraph->GetStringField(TEXT("Id")), FString(TEXT("Locomotion")));
	TestEqual(TEXT("Canonical owner node id is written"), RootGraph->GetStringField(TEXT("OwnerNodeId")), FString(TEXT("StateMachineNode")));
	TestEqual(TEXT("Canonical owner pin is written"), RootGraph->GetStringField(TEXT("OwnerPin")), FString(TEXT("OutputPose")));
	TestTrue(TEXT("Canonical entry pins are written"), RootGraph->HasTypedField<EJson::Array>(TEXT("EntryPins")));
	TestTrue(TEXT("Canonical result pins are written"), RootGraph->HasTypedField<EJson::Array>(TEXT("ResultPins")));
	TestTrue(TEXT("Canonical root position is written"), RootGraph->HasTypedField<EJson::Object>(TEXT("Position")));
	TestTrue(TEXT("Canonical metadata is written"), RootGraph->HasTypedField<EJson::Object>(TEXT("Metadata")));
	TestTrue(TEXT("Canonical diagnostics is written"), RootGraph->HasTypedField<EJson::Array>(TEXT("Diagnostics")));
	TestTrue(TEXT("Canonical _Skipped is written"), RootGraph->HasTypedField<EJson::Object>(TEXT("_Skipped")));
	TestTrue(TEXT("Canonical root evidence is written"), RootGraph->HasTypedField<EJson::Object>(TEXT("Evidence")));
	TestTrue(TEXT("Canonical subgraphs are written"), RootGraph->HasTypedField<EJson::Array>(TEXT("Subgraphs")));

	const TSharedPtr<FJsonObject> RootNode = RootGraph->GetArrayField(TEXT("Nodes"))[0]->AsObject();
	TestTrue(TEXT("Canonical node fields are written"), RootNode->HasTypedField<EJson::Object>(TEXT("Fields")));
	TestTrue(TEXT("Canonical node pins are written"), RootNode->HasTypedField<EJson::Object>(TEXT("Pins")));
	TestTrue(TEXT("Canonical node subgraph refs are written"), RootNode->HasTypedField<EJson::Object>(TEXT("SubgraphRefs")));
	TestTrue(TEXT("Canonical node evidence is written"), RootNode->HasTypedField<EJson::Object>(TEXT("Evidence")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreRejectInvalidRecursiveGraphRegionTest,
	"AssetFactory.AssetDocument.GraphCore.RejectInvalidRecursiveGraphRegion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreRejectInvalidRecursiveGraphRegionTest::RunTest(const FString& Parameters)
{
	FAssetDocumentGraphParseOptions Options;
	Options.Path = TEXT("/Body/StateMachines");

	const TSharedPtr<FJsonObject> DuplicateGraphs = ParseJsonObject(TEXT(R"JSON(
{
  "Graphs": [
    { "Id": "Locomotion", "Kind": "StateMachine", "Nodes": [], "Links": [], "Subgraphs": [] },
    { "Id": "Locomotion", "Kind": "StateMachine", "Nodes": [], "Links": [], "Subgraphs": [] }
  ]
}
)JSON"));
	const FAssetDocumentGraphParseResult DuplicateGraphResult =
		FAssetDocumentGraphParser::ParseGraphRegion(DuplicateGraphs.ToSharedRef(), Options);
	TestFalse(TEXT("Duplicate sibling graph ids fail"), DuplicateGraphResult.IsValid());
	TestTrue(
		TEXT("DuplicateGraphId diagnostic is emitted"),
		HasDiagnosticCode(DuplicateGraphResult, TEXT("DuplicateGraphId")));

	const TSharedPtr<FJsonObject> DuplicateNodes = ParseJsonObject(TEXT(R"JSON(
{
  "Graphs": [
    {
      "Id": "Locomotion",
      "Kind": "StateMachine",
      "Nodes": [
        { "Id": "SpeedCheck", "Class": "/Script/BlueprintGraph.K2Node_VariableGet" },
        { "Id": "SpeedCheck", "Class": "/Script/BlueprintGraph.K2Node_VariableGet" }
      ],
      "Links": [],
      "Subgraphs": []
    }
  ]
}
)JSON"));
	const FAssetDocumentGraphParseResult DuplicateNodeResult =
		FAssetDocumentGraphParser::ParseGraphRegion(DuplicateNodes.ToSharedRef(), Options);
	TestFalse(TEXT("Duplicate node ids fail"), DuplicateNodeResult.IsValid());
	TestTrue(
		TEXT("DuplicateGraphNodeId diagnostic is emitted"),
		HasDiagnosticCode(DuplicateNodeResult, TEXT("DuplicateGraphNodeId")));

	const TSharedPtr<FJsonObject> InvalidSubgraphs = ParseJsonObject(TEXT(R"JSON(
{
  "Graphs": [
    { "Id": "Locomotion", "Kind": "StateMachine", "Nodes": [], "Links": [], "Subgraphs": "nope" }
  ]
}
)JSON"));
	const FAssetDocumentGraphParseResult InvalidSubgraphsResult =
		FAssetDocumentGraphParser::ParseGraphRegion(InvalidSubgraphs.ToSharedRef(), Options);
	TestFalse(TEXT("Invalid Subgraphs shape fails"), InvalidSubgraphsResult.IsValid());
	TestTrue(
		TEXT("InvalidGraphRegionType diagnostic is emitted for Subgraphs"),
		HasDiagnosticCode(InvalidSubgraphsResult, TEXT("InvalidGraphRegionType")));

	const TSharedPtr<FJsonObject> InvalidOwner = ParseJsonObject(TEXT(R"JSON(
{
  "Graphs": [
    { "Id": "Locomotion", "Kind": "StateMachine", "Owner": "nope", "Nodes": [], "Links": [], "Subgraphs": [] }
  ]
}
)JSON"));
	const FAssetDocumentGraphParseResult InvalidOwnerResult =
		FAssetDocumentGraphParser::ParseGraphRegion(InvalidOwner.ToSharedRef(), Options);
	TestFalse(TEXT("Invalid Owner shape fails"), InvalidOwnerResult.IsValid());
	TestTrue(
		TEXT("InvalidGraphOwner diagnostic is emitted"),
		HasDiagnosticCode(InvalidOwnerResult, TEXT("InvalidGraphOwner")));

	const TSharedPtr<FJsonObject> InvalidGraphId = ParseJsonObject(TEXT(R"JSON(
{
  "Graphs": [
    { "Id": "1 Locomotion", "Kind": "StateMachine", "Nodes": [], "Links": [], "Subgraphs": [] }
  ]
}
)JSON"));
	const FAssetDocumentGraphParseResult InvalidGraphIdResult =
		FAssetDocumentGraphParser::ParseGraphRegion(InvalidGraphId.ToSharedRef(), Options);
	TestFalse(TEXT("Invalid graph ids fail"), InvalidGraphIdResult.IsValid());
	TestTrue(
		TEXT("InvalidGraphId diagnostic is emitted"),
		HasDiagnosticCode(InvalidGraphIdResult, TEXT("InvalidGraphId")));

	const TSharedPtr<FJsonObject> UnknownGraphKind = ParseJsonObject(TEXT(R"JSON(
{
  "Graphs": [
    { "Id": "Locomotion", "Kind": "FutureGraph", "Nodes": [], "Links": [], "Subgraphs": [] }
  ]
}
)JSON"));
	const FAssetDocumentGraphParseResult UnknownGraphKindResult =
		FAssetDocumentGraphParser::ParseGraphRegion(UnknownGraphKind.ToSharedRef(), Options);
	TestFalse(TEXT("Unknown graph kind fails"), UnknownGraphKindResult.IsValid());
	TestTrue(
		TEXT("UnknownGraphKind diagnostic is emitted"),
		HasDiagnosticCode(UnknownGraphKindResult, TEXT("UnknownGraphKind")));

	const TSharedPtr<FJsonObject> UnknownOwnerReference = ParseJsonObject(TEXT(R"JSON(
{
  "Graphs": [
    {
      "Id": "Locomotion",
      "Kind": "StateMachine",
      "Owner": { "StateMachine": "Missing" },
      "Nodes": [],
      "Links": [],
      "Subgraphs": []
    }
  ]
}
)JSON"));
	const FAssetDocumentGraphParseResult UnknownOwnerReferenceResult =
		FAssetDocumentGraphParser::ParseGraphRegion(UnknownOwnerReference.ToSharedRef(), Options);
	TestFalse(TEXT("Unknown owner reference fails"), UnknownOwnerReferenceResult.IsValid());
	TestTrue(
		TEXT("UnknownGraphOwnerReference diagnostic is emitted"),
		HasDiagnosticCode(UnknownOwnerReferenceResult, TEXT("UnknownGraphOwnerReference")));

	const TSharedPtr<FJsonObject> DuplicatePinOverrides = ParseJsonObject(TEXT(R"JSON(
{
  "Graphs": [
    {
      "Id": "Locomotion",
      "Kind": "StateMachine",
      "Nodes": [
        {
          "Id": "Blend",
          "Class": "/Script/AnimGraph.AnimGraphNode_BlendListByBool",
          "PinOverrides": [
            { "Pin": "Pose" },
            { "Pin": "Pose" }
          ]
        }
      ],
      "Links": [],
      "Subgraphs": []
    }
  ]
}
)JSON"));
	const FAssetDocumentGraphParseResult DuplicatePinOverridesResult =
		FAssetDocumentGraphParser::ParseGraphRegion(DuplicatePinOverrides.ToSharedRef(), Options);
	TestFalse(TEXT("Duplicate pin override ids fail"), DuplicatePinOverridesResult.IsValid());
	TestTrue(
		TEXT("DuplicateGraphPinId diagnostic is emitted"),
		HasDiagnosticCode(DuplicatePinOverridesResult, TEXT("DuplicateGraphPinId")));

	const TSharedPtr<FJsonObject> UnknownEscapedField = ParseJsonObject(TEXT(R"JSON(
{
  "Graphs": [
    {
      "Id": "Locomotion",
      "Kind": "StateMachine",
      "Nodes": [],
      "Links": [],
      "Subgraphs": [],
      "Bad/Field~Name": true
    }
  ]
}
)JSON"));
	const FAssetDocumentGraphParseResult UnknownEscapedFieldResult =
		FAssetDocumentGraphParser::ParseGraphRegion(UnknownEscapedField.ToSharedRef(), Options);
	TestFalse(TEXT("Unknown escaped field fails"), UnknownEscapedFieldResult.IsValid());
	TestTrue(
		TEXT("Unknown field diagnostic path uses JSON Pointer escaping"),
		HasDiagnosticCodeAtPath(
			UnknownEscapedFieldResult,
			TEXT("UnknownGraphField"),
			TEXT("/Body/StateMachines/Graphs/0/Bad~1Field~0Name")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreCompareRecursiveGraphRegionTest,
	"AssetFactory.AssetDocument.GraphCore.CompareRecursiveGraphRegion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreCompareRecursiveGraphRegionTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> DesiredRegion = MakeRecursiveGraphRegion();
	const TSharedPtr<FJsonObject> CurrentRegion = MakeRecursiveGraphRegion();
	CurrentRegion->GetArrayField(TEXT("Graphs"))[0]
		->AsObject()
		->GetArrayField(TEXT("Subgraphs"))[0]
		->AsObject()
		->GetArrayField(TEXT("Nodes"))[0]
		->AsObject()
		->GetObjectField(TEXT("Fields"))
		->SetStringField(TEXT("Variable"), TEXT("Velocity"));
	CurrentRegion->GetArrayField(TEXT("Graphs"))[0]
		->AsObject()
		->GetArrayField(TEXT("Subgraphs"))[0]
		->AsObject()
		->GetArrayField(TEXT("Nodes"))[0]
		->AsObject()
		->GetObjectField(TEXT("Position"))
		->SetNumberField(TEXT("X"), 640);

	FAssetDocumentGraphParseOptions Options;
	Options.Path = TEXT("/Body/StateMachines");
	const FAssetDocumentGraphParseResult DesiredParseResult =
		FAssetDocumentGraphParser::ParseGraphRegion(DesiredRegion.ToSharedRef(), Options);
	const FAssetDocumentGraphParseResult CurrentParseResult =
		FAssetDocumentGraphParser::ParseGraphRegion(CurrentRegion.ToSharedRef(), Options);
	TestTrue(TEXT("Desired recursive region parses"), DesiredParseResult.IsValid());
	TestTrue(TEXT("Current recursive region parses"), CurrentParseResult.IsValid());

	const TArray<FAssetDocumentGraphDiffEntry> Entries = FAssetDocumentGraphDiff::CompareGraphRegion(
		DesiredParseResult.Graphs,
		CurrentParseResult.Graphs,
		TEXT("/Body/StateMachines"));

	TestTrue(
		TEXT("Changed nested field uses semantic graph and node path"),
		HasDiffStatusAtPath(
			Entries,
			TEXT("/Body/StateMachines/Graphs/Locomotion/Subgraphs/IdleToRunRule/Nodes/SpeedCheck/Fields/Variable"),
			TEXT("changed")));
	TestTrue(
		TEXT("Changed nested layout uses semantic graph and node path"),
		HasDiffStatusAtPath(
			Entries,
			TEXT("/Body/StateMachines/Graphs/Locomotion/Subgraphs/IdleToRunRule/Nodes/SpeedCheck/Position"),
			TEXT("changed")));

	return true;
}

#endif
