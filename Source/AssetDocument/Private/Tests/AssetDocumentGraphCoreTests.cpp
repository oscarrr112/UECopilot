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

#endif
