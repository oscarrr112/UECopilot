// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/UBlueprintAssetDocumentCapability.h"

#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
TSharedRef<FJsonValue> MakeBodyValue(const TSharedRef<FJsonObject>& Body)
{
	return StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueObject>(Body));
}

TSharedRef<FJsonObject> MakeActorParentClassRef()
{
	TSharedRef<FJsonObject> ParentClass = MakeShared<FJsonObject>();
	ParentClass->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ParentClass->SetStringField(TEXT("Class"), TEXT("/Script/Engine.Actor"));
	return ParentClass;
}

TSharedRef<FJsonObject> MakeMemberRef(const TCHAR* OwnerClass, const TCHAR* Name)
{
	TSharedRef<FJsonObject> Member = MakeShared<FJsonObject>();
	Member->SetStringField(TEXT("Kind"), TEXT("MemberRef"));
	Member->SetStringField(TEXT("OwnerClass"), OwnerClass);
	Member->SetStringField(TEXT("Name"), Name);
	return Member;
}

TSharedRef<FJsonObject> MakeGraphNode(const TCHAR* Id, const TCHAR* ClassPath, TSharedPtr<FJsonObject> Member = nullptr)
{
	TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("Id"), Id);
	Node->SetStringField(TEXT("Class"), ClassPath);
	if (Member.IsValid())
	{
		Node->SetObjectField(TEXT("Member"), Member);
	}

	TSharedRef<FJsonObject> Position = MakeShared<FJsonObject>();
	Position->SetNumberField(TEXT("X"), 0.0);
	Position->SetNumberField(TEXT("Y"), 0.0);
	Node->SetObjectField(TEXT("Position"), Position);
	return Node;
}

TArray<TSharedPtr<FJsonValue>> MakeJsonArray(std::initializer_list<TSharedRef<FJsonObject>> Objects)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const TSharedRef<FJsonObject>& Object : Objects)
	{
		Values.Add(MakeShared<FJsonValueObject>(Object));
	}
	return Values;
}

TSharedRef<FJsonObject> MakeEventGraph(std::initializer_list<TSharedRef<FJsonObject>> Nodes)
{
	TSharedRef<FJsonObject> Graph = MakeShared<FJsonObject>();
	Graph->SetStringField(TEXT("Name"), TEXT("EventGraph"));
	Graph->SetStringField(TEXT("Schema"), TEXT("/Script/BlueprintGraph.EdGraphSchema_K2"));
	Graph->SetArrayField(TEXT("Nodes"), MakeJsonArray(Nodes));
	Graph->SetArrayField(TEXT("Links"), {});
	return Graph;
}

TSharedRef<FJsonObject> MakeBodyWithRegion(const FString& RegionName, std::initializer_list<TSharedRef<FJsonObject>> Entries)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), MakeActorParentClassRef());
	Body->SetArrayField(RegionName, MakeJsonArray(Entries));
	return Body;
}

bool ResultHasDiagnosticCode(const FAssetDocumentCapabilityResult& Result, const FString& Code)
{
	return Result.Diagnostics.ContainsByPredicate([&Code](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == Code;
	});
}

bool ResultHasDiagnostic(const FAssetDocumentCapabilityResult& Result, const FString& Path, const FString& Code)
{
	return Result.Diagnostics.ContainsByPredicate([&Path, &Code](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Path == Path && Diagnostic.Code == Code;
	});
}

TSharedPtr<FJsonObject> GetFirstUnsupportedGraphDiagnostic(const FAssetDocumentCapabilityResult& Result)
{
	if (!Result.Payload.IsValid())
	{
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
	if (!Result.Payload->TryGetArrayField(TEXT("UnsupportedGraphDiagnostics"), Diagnostics) || !Diagnostics || Diagnostics->IsEmpty())
	{
		return nullptr;
	}

	return (*Diagnostics)[0].IsValid() ? (*Diagnostics)[0]->AsObject() : nullptr;
}

bool FallbackHasActionableFields(const TSharedPtr<FJsonObject>& Fallback)
{
	if (!Fallback.IsValid())
	{
		return false;
	}

	return Fallback->HasTypedField<EJson::String>(TEXT("Code"))
		&& Fallback->HasTypedField<EJson::String>(TEXT("Path"))
		&& Fallback->HasTypedField<EJson::String>(TEXT("Class"))
		&& Fallback->HasField(TEXT("Capability"))
		&& Fallback->HasField(TEXT("Member"))
		&& Fallback->HasTypedField<EJson::String>(TEXT("Reason"))
		&& Fallback->HasTypedField<EJson::String>(TEXT("SuggestedAction"));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphValidationAcceptsTier1ShapeBeforeApplyTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphValidation.AcceptsTier1ShapeBeforeApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphValidationAcceptsTier1ShapeBeforeApplyTest::RunTest(const FString&)
{
	const FUBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UBlueprint::StaticClass();

	TSharedRef<FJsonObject> Body = MakeBodyWithRegion(
		TEXT("UbergraphPages"),
		{MakeEventGraph({
			MakeGraphNode(
				TEXT("BeginPlay"),
				TEXT("/Script/BlueprintGraph.K2Node_Event"),
				MakeMemberRef(TEXT("/Script/Engine.Actor"), TEXT("ReceiveBeginPlay")))
		})});

	const FAssetDocumentCapabilityResult Result = Capability.Validate(Context, MakeBodyValue(Body));
	TestFalse(TEXT("Valid graph shape reaches current-tier unsupported fallback"), Result.bSuccess);
	TestFalse(TEXT("Graph shape is not rejected by old protected-region guard"), ResultHasDiagnosticCode(Result, TEXT("UnsupportedUBlueprintRegion")));
	TestTrue(TEXT("Current tier has no node adapter"), ResultHasDiagnostic(Result, TEXT("/Body/UbergraphPages/0/Nodes/0"), TEXT("UnsupportedGraphNodeClass")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphValidationRejectsUnsupportedFunctionGraphsTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphValidation.RejectsUnsupportedFunctionGraphs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphValidationRejectsUnsupportedFunctionGraphsTest::RunTest(const FString&)
{
	const FUBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UBlueprint::StaticClass();

	const FAssetDocumentCapabilityResult Result = Capability.Validate(
		Context,
		MakeBodyValue(MakeBodyWithRegion(TEXT("FunctionGraphs"), {MakeShared<FJsonObject>()})));
	TestFalse(TEXT("FunctionGraphs remains unsupported"), Result.bSuccess);
	TestTrue(TEXT("FunctionGraphs diagnostic remains protected"), ResultHasDiagnostic(Result, TEXT("/Body/FunctionGraphs"), TEXT("UnsupportedUBlueprintRegion")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphValidationRejectsUnsupportedMacroGraphsTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphValidation.RejectsUnsupportedMacroGraphs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphValidationRejectsUnsupportedMacroGraphsTest::RunTest(const FString&)
{
	const FUBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UBlueprint::StaticClass();

	const FAssetDocumentCapabilityResult Result = Capability.Validate(
		Context,
		MakeBodyValue(MakeBodyWithRegion(TEXT("MacroGraphs"), {MakeShared<FJsonObject>()})));
	TestFalse(TEXT("MacroGraphs remains unsupported"), Result.bSuccess);
	TestTrue(TEXT("MacroGraphs diagnostic remains protected"), ResultHasDiagnostic(Result, TEXT("/Body/MacroGraphs"), TEXT("UnsupportedUBlueprintRegion")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphValidationRejectsTimelinesUntilImplementedTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphValidation.RejectsTimelinesUntilImplemented",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphValidationRejectsTimelinesUntilImplementedTest::RunTest(const FString&)
{
	const FUBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UBlueprint::StaticClass();

	const FAssetDocumentCapabilityResult Result = Capability.Validate(
		Context,
		MakeBodyValue(MakeBodyWithRegion(TEXT("Timelines"), {MakeShared<FJsonObject>()})));
	TestFalse(TEXT("Timelines remains unsupported"), Result.bSuccess);
	TestTrue(TEXT("Timelines diagnostic remains protected"), ResultHasDiagnostic(Result, TEXT("/Body/Timelines"), TEXT("UnsupportedUBlueprintRegion")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphValidationUnsupportedNodeHasActionableDiagnosticTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphValidation.UnsupportedNodeHasActionableDiagnostic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphValidationUnsupportedNodeHasActionableDiagnosticTest::RunTest(const FString&)
{
	const FUBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UBlueprint::StaticClass();

	TSharedRef<FJsonObject> Body = MakeBodyWithRegion(
		TEXT("UbergraphPages"),
		{MakeEventGraph({
			MakeGraphNode(TEXT("Branch"), TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"))
		})});

	const FAssetDocumentCapabilityResult Result = Capability.Validate(Context, MakeBodyValue(Body));
	TestFalse(TEXT("Unsupported node class fails validation"), Result.bSuccess);
	TestTrue(TEXT("Primary diagnostic uses unsupported node class code"), ResultHasDiagnostic(Result, TEXT("/Body/UbergraphPages/0/Nodes/0"), TEXT("UnsupportedGraphNodeClass")));

	const TSharedPtr<FJsonObject> Fallback = GetFirstUnsupportedGraphDiagnostic(Result);
	TestTrue(TEXT("Fallback payload has actionable fields"), FallbackHasActionableFields(Fallback));
	if (Fallback.IsValid())
	{
		TestEqual(TEXT("Fallback code"), Fallback->GetStringField(TEXT("Code")), FString(TEXT("UnsupportedGraphNodeClass")));
		TestEqual(TEXT("Fallback path"), Fallback->GetStringField(TEXT("Path")), FString(TEXT("/Body/UbergraphPages/0/Nodes/0")));
		TestEqual(TEXT("Fallback class"), Fallback->GetStringField(TEXT("Class")), FString(TEXT("/Script/BlueprintGraph.K2Node_IfThenElse")));
		TestTrue(TEXT("Fallback reason mentions adapter"), Fallback->GetStringField(TEXT("Reason")).Contains(TEXT("adapter")));
		TestFalse(TEXT("Fallback suggested action is not empty"), Fallback->GetStringField(TEXT("SuggestedAction")).IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphValidationUnsupportedFunctionHasActionableDiagnosticTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphValidation.UnsupportedFunctionHasActionableDiagnostic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphValidationUnsupportedFunctionHasActionableDiagnosticTest::RunTest(const FString&)
{
	const FUBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UBlueprint::StaticClass();

	TSharedRef<FJsonObject> Body = MakeBodyWithRegion(
		TEXT("UbergraphPages"),
		{MakeEventGraph({
			MakeGraphNode(
				TEXT("Print"),
				TEXT("/Script/BlueprintGraph.K2Node_CallFunction"),
				MakeMemberRef(TEXT("/Script/Engine.KismetSystemLibrary"), TEXT("PrintString")))
		})});

	const FAssetDocumentCapabilityResult Result = Capability.Validate(Context, MakeBodyValue(Body));
	TestFalse(TEXT("Unsupported reflected function fails validation"), Result.bSuccess);
	TestTrue(TEXT("Primary diagnostic uses unsupported function code"), ResultHasDiagnostic(Result, TEXT("/Body/UbergraphPages/0/Nodes/0"), TEXT("UnsupportedGraphFunction")));

	const TSharedPtr<FJsonObject> Fallback = GetFirstUnsupportedGraphDiagnostic(Result);
	TestTrue(TEXT("Function fallback payload has actionable fields"), FallbackHasActionableFields(Fallback));
	if (Fallback.IsValid())
	{
		TestEqual(TEXT("Fallback code"), Fallback->GetStringField(TEXT("Code")), FString(TEXT("UnsupportedGraphFunction")));
		TestEqual(TEXT("Fallback class"), Fallback->GetStringField(TEXT("Class")), FString(TEXT("/Script/BlueprintGraph.K2Node_CallFunction")));
		const TSharedPtr<FJsonObject>* Member = nullptr;
		TestTrue(TEXT("Fallback includes member object"), Fallback->TryGetObjectField(TEXT("Member"), Member) && Member && Member->IsValid());
		TestTrue(TEXT("Fallback reason mentions current tier"), Fallback->GetStringField(TEXT("Reason")).Contains(TEXT("current tier")));
		TestFalse(TEXT("Fallback suggested action is not empty"), Fallback->GetStringField(TEXT("SuggestedAction")).IsEmpty());
	}
	return true;
}

#endif
