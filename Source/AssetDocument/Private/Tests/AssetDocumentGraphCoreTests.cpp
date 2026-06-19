// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/AssetDocumentGraphParser.h"

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

#endif
