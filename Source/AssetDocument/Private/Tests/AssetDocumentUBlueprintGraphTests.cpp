// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/UBlueprintAssetDocumentCapability.h"

#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/MemberReference.h"
#include "GameFramework/Actor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
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

int32 GetUnsupportedGraphDiagnosticCount(const FAssetDocumentCapabilityResult& Result)
{
	if (!Result.Payload.IsValid())
	{
		return 0;
	}

	const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
	return Result.Payload->TryGetArrayField(TEXT("UnsupportedGraphDiagnostics"), Diagnostics) && Diagnostics
		? Diagnostics->Num()
		: 0;
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

UBlueprint* CreateTransientActorBlueprint(const TCHAR* NamePrefix)
{
	const FName BlueprintName(*FString::Printf(TEXT("%s_%s"), NamePrefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(),
		GetTransientPackage(),
		BlueprintName,
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass());
	if (Blueprint)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
	}
	return Blueprint;
}

UEdGraph* GetEventGraph(UBlueprint* Blueprint)
{
	return Blueprint ? FBlueprintEditorUtils::FindEventGraph(Blueprint) : nullptr;
}

template <typename NodeType>
NodeType* AddK2Node(UEdGraph* Graph, int32 X, int32 Y)
{
	NodeType* Node = Graph ? NewObject<NodeType>(Graph) : nullptr;
	if (!Node)
	{
		return nullptr;
	}

	Graph->AddNode(Node, true, false);
	Node->NodePosX = X;
	Node->NodePosY = Y;
	return Node;
}

UK2Node_Event* AddBeginPlayNode(UEdGraph* Graph, int32 X = 0, int32 Y = 0)
{
	UK2Node_Event* Node = AddK2Node<UK2Node_Event>(Graph, X, Y);
	if (!Node)
	{
		return nullptr;
	}

	Node->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
	Node->bOverrideFunction = true;
	Node->AllocateDefaultPins();
	return Node;
}

UK2Node_CallFunction* AddPrintStringNode(UEdGraph* Graph, int32 X = 320, int32 Y = 0, const FString& InString = FString())
{
	UK2Node_CallFunction* Node = AddK2Node<UK2Node_CallFunction>(Graph, X, Y);
	UFunction* Function = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString"));
	if (!Node || !Function)
	{
		return nullptr;
	}

	Node->SetFromFunction(Function);
	Node->AllocateDefaultPins();
	if (!InString.IsEmpty())
	{
		if (UEdGraphPin* InStringPin = Node->FindPin(TEXT("InString")))
		{
			InStringPin->DefaultValue = InString;
		}
	}
	return Node;
}

UK2Node_VariableGet* AddVariableGetNode(UBlueprint* Blueprint, UEdGraph* Graph, FName VariableName, int32 X, int32 Y)
{
	UK2Node_VariableGet* Node = AddK2Node<UK2Node_VariableGet>(Graph, X, Y);
	if (!Node || !Blueprint)
	{
		return nullptr;
	}

	Node->VariableReference.SetSelfMember(VariableName);
	Node->AllocateDefaultPins();
	return Node;
}

UK2Node_VariableSet* AddVariableSetNode(UBlueprint* Blueprint, UEdGraph* Graph, FName VariableName, int32 X, int32 Y)
{
	UK2Node_VariableSet* Node = AddK2Node<UK2Node_VariableSet>(Graph, X, Y);
	if (!Node || !Blueprint)
	{
		return nullptr;
	}

	Node->VariableReference.SetSelfMember(VariableName);
	Node->AllocateDefaultPins();
	return Node;
}

UK2Node_Self* AddSelfNode(UEdGraph* Graph, int32 X, int32 Y)
{
	UK2Node_Self* Node = AddK2Node<UK2Node_Self>(Graph, X, Y);
	if (Node)
	{
		Node->AllocateDefaultPins();
	}
	return Node;
}

UK2Node_IfThenElse* AddUnsupportedBranchNode(UEdGraph* Graph, int32 X, int32 Y)
{
	UK2Node_IfThenElse* Node = AddK2Node<UK2Node_IfThenElse>(Graph, X, Y);
	if (Node)
	{
		Node->AllocateDefaultPins();
	}
	return Node;
}

bool LinkPins(UEdGraphPin* From, UEdGraphPin* To)
{
	if (!From || !To)
	{
		return false;
	}
	From->MakeLinkTo(To);
	return true;
}

TSharedRef<FJsonObject> ExtractBlueprintBody(UBlueprint* Blueprint, FAssetDocumentCapabilityResult& OutResult)
{
	const FUBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.Asset = Blueprint;
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	OutResult = Capability.Extract(Context, Body);
	return Body;
}

const TArray<TSharedPtr<FJsonValue>>* GetUbergraphPages(const TSharedRef<FJsonObject>& Body)
{
	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	return Body->TryGetArrayField(TEXT("UbergraphPages"), Graphs) ? Graphs : nullptr;
}

TSharedPtr<FJsonObject> FindNodeByClass(const TArray<TSharedPtr<FJsonValue>>& Nodes, const FString& ClassPath)
{
	for (const TSharedPtr<FJsonValue>& Value : Nodes)
	{
		const TSharedPtr<FJsonObject> Node = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Node.IsValid())
		{
			continue;
		}
		FString Class;
		if (Node->TryGetStringField(TEXT("Class"), Class) && Class == ClassPath)
		{
			return Node;
		}
	}
	return nullptr;
}

TSharedPtr<FJsonObject> FindNodeByMemberName(const TArray<TSharedPtr<FJsonValue>>& Nodes, const FString& MemberName)
{
	for (const TSharedPtr<FJsonValue>& Value : Nodes)
	{
		const TSharedPtr<FJsonObject> Node = Value.IsValid() ? Value->AsObject() : nullptr;
		const TSharedPtr<FJsonObject>* Member = nullptr;
		if (!Node.IsValid() || !Node->TryGetObjectField(TEXT("Member"), Member) || !Member || !Member->IsValid())
		{
			continue;
		}

		FString Name;
		if ((*Member)->TryGetStringField(TEXT("Name"), Name) && Name == MemberName)
		{
			return Node;
		}
	}
	return nullptr;
}

bool NodeHasSparsePinOverride(const TSharedPtr<FJsonObject>& Node, const FString& PinName, const FString& ExpectedDefault)
{
	if (!Node.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* PinOverrides = nullptr;
	if (!Node->TryGetArrayField(TEXT("PinOverrides"), PinOverrides) || !PinOverrides)
	{
		return false;
	}

	return PinOverrides->ContainsByPredicate([&](const TSharedPtr<FJsonValue>& Value)
	{
		const TSharedPtr<FJsonObject> Pin = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Pin.IsValid())
		{
			return false;
		}
		FString PinId;
		FString DefaultValue;
		return Pin->TryGetStringField(TEXT("Pin"), PinId)
			&& PinId == PinName
			&& Pin->TryGetStringField(TEXT("DefaultValue"), DefaultValue)
			&& DefaultValue == ExpectedDefault;
	});
}

bool SkippedGraphsContainClass(const TSharedRef<FJsonObject>& Body, const FString& ClassPath)
{
	const TSharedPtr<FJsonObject>* Skipped = nullptr;
	const TSharedPtr<FJsonObject>* Graphs = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (!Body->TryGetObjectField(TEXT("_Skipped"), Skipped) || !Skipped || !Skipped->IsValid()
		|| !(*Skipped)->TryGetObjectField(TEXT("Graphs"), Graphs) || !Graphs || !Graphs->IsValid()
		|| !(*Graphs)->TryGetArrayField(TEXT("Nodes"), Nodes) || !Nodes)
	{
		return false;
	}

	return Nodes->ContainsByPredicate([&](const TSharedPtr<FJsonValue>& Value)
	{
		const TSharedPtr<FJsonObject> Node = Value.IsValid() ? Value->AsObject() : nullptr;
		FString FoundClass;
		return Node.IsValid() && Node->TryGetStringField(TEXT("Class"), FoundClass) && FoundClass == ClassPath;
	});
}

TSharedPtr<FJsonObject> FindDiffEntryByPath(const TArray<TSharedPtr<FJsonValue>>& Entries, const FString& ExpectedPath)
{
	for (const TSharedPtr<FJsonValue>& Value : Entries)
	{
		const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
		FString Path;
		if (Entry.IsValid() && Entry->TryGetStringField(TEXT("path"), Path) && Path == ExpectedPath)
		{
			return Entry;
		}
	}
	return nullptr;
}

void RemoveExtractOnlyEvidence(const TSharedRef<FJsonObject>& Body)
{
	Body->RemoveField(TEXT("_Skipped"));
}

bool HasNonUnchangedGraphDiffEntry(const TArray<TSharedPtr<FJsonValue>>& Entries)
{
	for (const TSharedPtr<FJsonValue>& Value : Entries)
	{
		const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
		FString Path;
		FString Status;
		if (Entry.IsValid()
			&& Entry->TryGetStringField(TEXT("path"), Path)
			&& Path.StartsWith(TEXT("/Body/UbergraphPages/"))
			&& Entry->TryGetStringField(TEXT("status"), Status)
			&& Status != TEXT("unchanged"))
		{
			return true;
		}
	}
	return false;
}

FAssetDocumentCapabilityResult DiffBlueprintBody(
	UBlueprint* Blueprint,
	const TSharedRef<FJsonObject>& DesiredBody,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries,
	const TSharedPtr<FJsonObject>* Definitions = nullptr)
{
	const FUBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.Asset = Blueprint;
	Context.Definitions = Definitions;
	return Capability.Diff(Context, MakeBodyValue(DesiredBody), OutDiffEntries);
}

TSharedPtr<FJsonObject> FindGraphByName(const TSharedRef<FJsonObject>& Body, const FString& GraphName)
{
	const TArray<TSharedPtr<FJsonValue>>* Graphs = GetUbergraphPages(Body);
	if (!Graphs)
	{
		return nullptr;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Graphs)
	{
		const TSharedPtr<FJsonObject> Graph = Value.IsValid() ? Value->AsObject() : nullptr;
		FString Name;
		if (Graph.IsValid() && Graph->TryGetStringField(TEXT("Name"), Name) && Name == GraphName)
		{
			return Graph;
		}
	}
	return nullptr;
}

FString GetNodeIdByMemberName(const TSharedPtr<FJsonObject>& Graph, const FString& MemberName)
{
	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (!Graph.IsValid() || !Graph->TryGetArrayField(TEXT("Nodes"), Nodes) || !Nodes)
	{
		return FString();
	}

	const TSharedPtr<FJsonObject> Node = FindNodeByMemberName(*Nodes, MemberName);
	FString Id;
	return Node.IsValid() && Node->TryGetStringField(TEXT("Id"), Id) ? Id : FString();
}

bool RemoveNodeByMemberName(const TSharedPtr<FJsonObject>& Graph, const FString& MemberName)
{
	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (!Graph.IsValid() || !Graph->TryGetArrayField(TEXT("Nodes"), Nodes) || !Nodes)
	{
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> FilteredNodes;
	bool bRemoved = false;
	for (const TSharedPtr<FJsonValue>& Value : *Nodes)
	{
		const TSharedPtr<FJsonObject> Node = Value.IsValid() ? Value->AsObject() : nullptr;
		const TSharedPtr<FJsonObject>* Member = nullptr;
		FString Name;
		if (Node.IsValid()
			&& Node->TryGetObjectField(TEXT("Member"), Member)
			&& Member
			&& Member->IsValid()
			&& (*Member)->TryGetStringField(TEXT("Name"), Name)
			&& Name == MemberName)
		{
			bRemoved = true;
			continue;
		}
		FilteredNodes.Add(Value);
	}
	Graph->SetArrayField(TEXT("Nodes"), MoveTemp(FilteredNodes));
	return bRemoved;
}

bool SetPinDefaultForMemberNode(const TSharedPtr<FJsonObject>& Graph, const FString& MemberName, const FString& PinName, const FString& DefaultValue)
{
	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (!Graph.IsValid() || !Graph->TryGetArrayField(TEXT("Nodes"), Nodes) || !Nodes)
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Node = FindNodeByMemberName(*Nodes, MemberName);
	if (!Node.IsValid())
	{
		return false;
	}

	TSharedRef<FJsonObject> PinOverride = MakeShared<FJsonObject>();
	PinOverride->SetStringField(TEXT("Pin"), PinName);
	PinOverride->SetStringField(TEXT("DefaultValue"), DefaultValue);
	Node->SetArrayField(TEXT("PinOverrides"), MakeJsonArray({PinOverride}));
	return true;
}

bool ReplaceMemberWithDefinitionRef(
	const TSharedPtr<FJsonObject>& Graph,
	const FString& MemberName,
	const FString& DefinitionId)
{
	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (!Graph.IsValid() || !Graph->TryGetArrayField(TEXT("Nodes"), Nodes) || !Nodes)
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Node = FindNodeByMemberName(*Nodes, MemberName);
	if (!Node.IsValid())
	{
		return false;
	}

	TSharedRef<FJsonObject> DefinitionRef = MakeShared<FJsonObject>();
	DefinitionRef->SetStringField(TEXT("Kind"), TEXT("DefinitionRef"));
	DefinitionRef->SetStringField(TEXT("Id"), DefinitionId);
	Node->SetObjectField(TEXT("Member"), DefinitionRef);
	return true;
}

void AddLinkToGraph(
	const TSharedPtr<FJsonObject>& Graph,
	const FString& FromNode,
	const FString& FromPin,
	const FString& ToNode,
	const FString& ToPin)
{
	TSharedRef<FJsonObject> From = MakeShared<FJsonObject>();
	From->SetStringField(TEXT("Node"), FromNode);
	From->SetStringField(TEXT("Pin"), FromPin);

	TSharedRef<FJsonObject> To = MakeShared<FJsonObject>();
	To->SetStringField(TEXT("Node"), ToNode);
	To->SetStringField(TEXT("Pin"), ToPin);

	TSharedRef<FJsonObject> Link = MakeShared<FJsonObject>();
	Link->SetObjectField(TEXT("From"), From);
	Link->SetObjectField(TEXT("To"), To);
	Graph->SetArrayField(TEXT("Links"), MakeJsonArray({Link}));
}

FString GraphPath(const FString& GraphName)
{
	return FString::Printf(TEXT("/Body/UbergraphPages/%s"), *GraphName);
}

FString NodePath(const FString& GraphName, const FString& NodeId)
{
	return FString::Printf(TEXT("/Body/UbergraphPages/%s/Nodes/%s"), *GraphName, *NodeId);
}

FString PinPath(const FString& GraphName, const FString& NodeId, const FString& PinId)
{
	return FString::Printf(TEXT("/Body/UbergraphPages/%s/Nodes/%s/PinOverrides/%s"), *GraphName, *NodeId, *PinId);
}

FString LinkPath(const FString& GraphName, const FString& FromNode, const FString& FromPin, const FString& ToNode, const FString& ToPin)
{
	return FString::Printf(
		TEXT("/Body/UbergraphPages/%s/Links/%s:%s->%s:%s"),
		*GraphName,
		*FromNode,
		*FromPin,
		*ToNode,
		*ToPin);
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
	TestTrue(TEXT("Tier 1 event graph shape validates before graph apply exists"), Result.bSuccess);
	TestFalse(TEXT("Graph shape is not rejected by old protected-region guard"), ResultHasDiagnosticCode(Result, TEXT("UnsupportedUBlueprintRegion")));
	TestFalse(TEXT("Tier 1 event adapter is registered"), ResultHasDiagnostic(Result, TEXT("/Body/UbergraphPages/0/Nodes/0"), TEXT("UnsupportedGraphNodeClass")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphValidationPreservesMultipleParserDiagnosticsTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphValidation.PreservesMultipleParserDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphValidationPreservesMultipleParserDiagnosticsTest::RunTest(const FString&)
{
	const FUBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UBlueprint::StaticClass();

	TSharedRef<FJsonObject> InvalidGraph = MakeShared<FJsonObject>();
	InvalidGraph->SetStringField(TEXT("Unexpected"), TEXT("value"));
	InvalidGraph->SetArrayField(TEXT("Nodes"), {});
	InvalidGraph->SetArrayField(TEXT("Links"), {});

	TSharedRef<FJsonObject> Body = MakeBodyWithRegion(TEXT("UbergraphPages"), {InvalidGraph});
	const FAssetDocumentCapabilityResult Result = Capability.Validate(Context, MakeBodyValue(Body));
	TestFalse(TEXT("Invalid graph shape fails validation"), Result.bSuccess);
	TestTrue(TEXT("All graph parser diagnostics are preserved"), Result.Diagnostics.Num() >= 2);
	TestTrue(TEXT("MissingGraphName diagnostic is preserved"), ResultHasDiagnosticCode(Result, TEXT("MissingGraphName")));
	TestTrue(TEXT("MissingGraphSchema diagnostic is preserved"), ResultHasDiagnosticCode(Result, TEXT("MissingGraphSchema")));
	TestTrue(TEXT("UnknownGraphField diagnostic is preserved"), ResultHasDiagnosticCode(Result, TEXT("UnknownGraphField")));
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
	FAssetDocumentUBlueprintGraphValidationMultipleUnsupportedNodesHaveActionableDiagnosticsTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphValidation.MultipleUnsupportedNodesHaveActionableDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphValidationMultipleUnsupportedNodesHaveActionableDiagnosticsTest::RunTest(const FString&)
{
	const FUBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UBlueprint::StaticClass();

	TSharedRef<FJsonObject> Body = MakeBodyWithRegion(
		TEXT("UbergraphPages"),
		{MakeEventGraph({
			MakeGraphNode(TEXT("BranchA"), TEXT("/Script/BlueprintGraph.K2Node_IfThenElse")),
			MakeGraphNode(TEXT("BranchB"), TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"))
		})});

	const FAssetDocumentCapabilityResult Result = Capability.Validate(Context, MakeBodyValue(Body));
	TestFalse(TEXT("Unsupported node classes fail validation"), Result.bSuccess);
	TestTrue(TEXT("First unsupported node diagnostic is preserved"), ResultHasDiagnostic(Result, TEXT("/Body/UbergraphPages/0/Nodes/0"), TEXT("UnsupportedGraphNodeClass")));
	TestTrue(TEXT("Second unsupported node diagnostic is preserved"), ResultHasDiagnostic(Result, TEXT("/Body/UbergraphPages/0/Nodes/1"), TEXT("UnsupportedGraphNodeClass")));
	TestEqual(TEXT("Fallback payload includes both unsupported nodes"), GetUnsupportedGraphDiagnosticCount(Result), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphValidationSupportsTier1CallFunctionTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphValidation.SupportsTier1CallFunction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphValidationSupportsTier1CallFunctionTest::RunTest(const FString&)
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
	TestTrue(TEXT("Tier 1 reflected function validates"), Result.bSuccess);
	TestFalse(TEXT("Reflected function is no longer current-tier unsupported"), ResultHasDiagnostic(Result, TEXT("/Body/UbergraphPages/0/Nodes/0"), TEXT("UnsupportedGraphFunction")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphValidationUnresolvedFunctionHasActionableDiagnosticTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphValidation.UnresolvedFunctionHasActionableDiagnostic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphValidationUnresolvedFunctionHasActionableDiagnosticTest::RunTest(const FString&)
{
	const FUBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UBlueprint::StaticClass();

	TSharedRef<FJsonObject> Body = MakeBodyWithRegion(
		TEXT("UbergraphPages"),
		{MakeEventGraph({
			MakeGraphNode(
				TEXT("MissingFunction"),
				TEXT("/Script/BlueprintGraph.K2Node_CallFunction"),
				MakeMemberRef(TEXT("/Script/Engine.KismetSystemLibrary"), TEXT("FunctionThatDoesNotExist")))
		})});

	const FAssetDocumentCapabilityResult Result = Capability.Validate(Context, MakeBodyValue(Body));
	TestFalse(TEXT("Unresolved function member fails validation"), Result.bSuccess);
	TestTrue(TEXT("Unresolved function uses precise diagnostic code"), ResultHasDiagnostic(Result, TEXT("/Body/UbergraphPages/0/Nodes/0"), TEXT("UnresolvedGraphFunction")));
	TestFalse(TEXT("Unresolved function is not treated as unsupported node class"), ResultHasDiagnosticCode(Result, TEXT("UnsupportedGraphNodeClass")));

	const TSharedPtr<FJsonObject> Fallback = GetFirstUnsupportedGraphDiagnostic(Result);
	TestTrue(TEXT("Unresolved function fallback payload has actionable fields"), FallbackHasActionableFields(Fallback));
	if (Fallback.IsValid())
	{
		TestEqual(TEXT("Fallback code"), Fallback->GetStringField(TEXT("Code")), FString(TEXT("UnresolvedGraphFunction")));
		TestTrue(TEXT("Fallback reason mentions MemberRef"), Fallback->GetStringField(TEXT("Reason")).Contains(TEXT("MemberRef")));
		TestTrue(TEXT("Fallback suggested action mentions MemberRef"), Fallback->GetStringField(TEXT("SuggestedAction")).Contains(TEXT("MemberRef")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphExtractExtractsBeginPlayPrintStringTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphExtract.ExtractsBeginPlayPrintString",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphExtractExtractsBeginPlayPrintStringTest::RunTest(const FString&)
{
	UBlueprint* Blueprint = CreateTransientActorBlueprint(TEXT("BP_GraphExtractBeginPlayPrint"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient actor Blueprint has an EventGraph"), Graph);
	if (!Graph)
	{
		return false;
	}

	UK2Node_Event* BeginPlay = AddBeginPlayNode(Graph);
	UK2Node_CallFunction* Print = AddPrintStringNode(Graph, 320, 0, TEXT("Hello from extract"));
	TestTrue(TEXT("BeginPlay links to PrintString"), LinkPins(BeginPlay ? BeginPlay->FindPin(UEdGraphSchema_K2::PN_Then) : nullptr, Print ? Print->FindPin(UEdGraphSchema_K2::PN_Execute) : nullptr));

	FAssetDocumentCapabilityResult ExtractResult;
	const TSharedRef<FJsonObject> Body = ExtractBlueprintBody(Blueprint, ExtractResult);
	TestTrue(TEXT("Graph extract succeeds"), ExtractResult.bSuccess);

	const TArray<TSharedPtr<FJsonValue>>* Graphs = GetUbergraphPages(Body);
	TestTrue(TEXT("Extract writes Body.UbergraphPages"), Graphs && Graphs->Num() == 1);
	if (!Graphs || Graphs->IsEmpty())
	{
		return false;
	}

	const TSharedPtr<FJsonObject> ExtractedGraph = (*Graphs)[0]->AsObject();
	TestEqual(TEXT("Graph name"), ExtractedGraph->GetStringField(TEXT("Name")), FString(TEXT("EventGraph")));
	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Links = nullptr;
	TestTrue(TEXT("Graph has nodes"), ExtractedGraph->TryGetArrayField(TEXT("Nodes"), Nodes) && Nodes && Nodes->Num() >= 2);
	TestTrue(TEXT("Graph has one expanded link"), ExtractedGraph->TryGetArrayField(TEXT("Links"), Links) && Links && Links->Num() == 1);
	TestTrue(TEXT("BeginPlay node extracted"), FindNodeByMemberName(*Nodes, TEXT("ReceiveBeginPlay")).IsValid());
	const TSharedPtr<FJsonObject> PrintNode = FindNodeByMemberName(*Nodes, TEXT("PrintString"));
	TestTrue(TEXT("PrintString node extracted"), PrintNode.IsValid());
	TestTrue(TEXT("PrintString input default is sparse override"), NodeHasSparsePinOverride(PrintNode, TEXT("InString"), TEXT("Hello from extract")));

	if (Links && Links->Num() == 1)
	{
		const TSharedPtr<FJsonObject> Link = (*Links)[0]->AsObject();
		const TSharedPtr<FJsonObject>* From = nullptr;
		const TSharedPtr<FJsonObject>* To = nullptr;
		TestTrue(TEXT("Link From is expanded object"), Link->TryGetObjectField(TEXT("From"), From) && From && From->IsValid());
		TestTrue(TEXT("Link To is expanded object"), Link->TryGetObjectField(TEXT("To"), To) && To && To->IsValid());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphExtractExtractsVariableGetSetAndSelfTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphExtract.ExtractsVariableGetSetAndSelf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphExtractExtractsVariableGetSetAndSelfTest::RunTest(const FString&)
{
	UBlueprint* Blueprint = CreateTransientActorBlueprint(TEXT("BP_GraphExtractVars"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient actor Blueprint has an EventGraph"), Graph);
	if (!Blueprint || !Graph)
	{
		return false;
	}

	FEdGraphPinType IntPinType;
	IntPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	TestTrue(TEXT("Blueprint variable is added through UE API"), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("GraphCounter"), IntPinType, TEXT("0")));
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	AddVariableGetNode(Blueprint, Graph, TEXT("GraphCounter"), 0, 160);
	AddVariableSetNode(Blueprint, Graph, TEXT("GraphCounter"), 320, 160);
	AddSelfNode(Graph, 0, 320);

	FAssetDocumentCapabilityResult ExtractResult;
	const TSharedRef<FJsonObject> Body = ExtractBlueprintBody(Blueprint, ExtractResult);
	TestTrue(TEXT("Graph extract succeeds"), ExtractResult.bSuccess);
	const TArray<TSharedPtr<FJsonValue>>* Graphs = GetUbergraphPages(Body);
	TestTrue(TEXT("Extract writes Body.UbergraphPages"), Graphs && Graphs->Num() == 1);
	if (!Graphs || Graphs->IsEmpty())
	{
		return false;
	}

	const TSharedPtr<FJsonObject> ExtractedGraph = (*Graphs)[0]->AsObject();
	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	TestTrue(TEXT("Graph has nodes"), ExtractedGraph->TryGetArrayField(TEXT("Nodes"), Nodes) && Nodes && Nodes->Num() >= 3);
	TestTrue(TEXT("Variable get extracted"), FindNodeByClass(*Nodes, TEXT("/Script/BlueprintGraph.K2Node_VariableGet")).IsValid());
	TestTrue(TEXT("Variable set extracted"), FindNodeByClass(*Nodes, TEXT("/Script/BlueprintGraph.K2Node_VariableSet")).IsValid());
	const TSharedPtr<FJsonObject> SelfNode = FindNodeByClass(*Nodes, TEXT("/Script/BlueprintGraph.K2Node_Self"));
	TestTrue(TEXT("Self node extracted"), SelfNode.IsValid());
	TestFalse(TEXT("Self node has no MemberRef"), SelfNode.IsValid() && SelfNode->HasField(TEXT("Member")));
	TestTrue(TEXT("Variable member ref extracted"), FindNodeByMemberName(*Nodes, TEXT("GraphCounter")).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphExtractSkipsStaleVariableNodesTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphExtract.SkipsStaleVariableNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphExtractSkipsStaleVariableNodesTest::RunTest(const FString&)
{
	UBlueprint* Blueprint = CreateTransientActorBlueprint(TEXT("BP_GraphExtractStaleVars"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient actor Blueprint has an EventGraph"), Graph);
	if (!Blueprint || !Graph)
	{
		return false;
	}

	AddVariableGetNode(Blueprint, Graph, TEXT("DeletedCounter"), 0, 160);
	AddVariableSetNode(Blueprint, Graph, TEXT("DeletedCounter"), 320, 160);

	FAssetDocumentCapabilityResult ExtractResult;
	const TSharedRef<FJsonObject> Body = ExtractBlueprintBody(Blueprint, ExtractResult);
	TestTrue(TEXT("Graph extract succeeds with skipped stale variables"), ExtractResult.bSuccess);

	const TArray<TSharedPtr<FJsonValue>>* Graphs = GetUbergraphPages(Body);
	if (Graphs && !Graphs->IsEmpty())
	{
		const TSharedPtr<FJsonObject> ExtractedGraph = (*Graphs)[0]->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		TestTrue(TEXT("Graph nodes field is readable"), ExtractedGraph->TryGetArrayField(TEXT("Nodes"), Nodes) && Nodes);
		TestFalse(TEXT("Stale variable is not emitted as lossy MemberRef"), Nodes && FindNodeByMemberName(*Nodes, TEXT("DeletedCounter")).IsValid());
	}

	TestTrue(TEXT("Skipped graph evidence names stale variable get"), SkippedGraphsContainClass(Body, TEXT("/Script/BlueprintGraph.K2Node_VariableGet")));
	TestTrue(TEXT("Skipped graph evidence names stale variable set"), SkippedGraphsContainClass(Body, TEXT("/Script/BlueprintGraph.K2Node_VariableSet")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphExtractSkipsUnsupportedExistingNodesTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphExtract.SkipsUnsupportedExistingNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphExtractSkipsUnsupportedExistingNodesTest::RunTest(const FString&)
{
	UBlueprint* Blueprint = CreateTransientActorBlueprint(TEXT("BP_GraphExtractUnsupported"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient actor Blueprint has an EventGraph"), Graph);
	if (!Graph)
	{
		return false;
	}

	AddBeginPlayNode(Graph);
	AddUnsupportedBranchNode(Graph, 320, 0);

	FAssetDocumentCapabilityResult ExtractResult;
	const TSharedRef<FJsonObject> Body = ExtractBlueprintBody(Blueprint, ExtractResult);
	TestTrue(TEXT("Graph extract succeeds with skipped evidence"), ExtractResult.bSuccess);
	const TArray<TSharedPtr<FJsonValue>>* Graphs = GetUbergraphPages(Body);
	TestTrue(TEXT("Supported graph still extracted"), Graphs && Graphs->Num() == 1);
	if (Graphs && !Graphs->IsEmpty())
	{
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		const TSharedPtr<FJsonObject> ExtractedGraph = (*Graphs)[0]->AsObject();
		TestTrue(TEXT("Supported node is emitted"), ExtractedGraph->TryGetArrayField(TEXT("Nodes"), Nodes) && Nodes && FindNodeByMemberName(*Nodes, TEXT("ReceiveBeginPlay")).IsValid());
		TestFalse(TEXT("Unsupported node is not emitted as lossy NodeSpec"), Nodes && FindNodeByClass(*Nodes, TEXT("/Script/BlueprintGraph.K2Node_IfThenElse")).IsValid());
	}
	TestTrue(TEXT("Skipped graph evidence names unsupported class"), SkippedGraphsContainClass(Body, TEXT("/Script/BlueprintGraph.K2Node_IfThenElse")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphExtractKeepsPinOverridesSparseTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphExtract.KeepsPinOverridesSparse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphExtractKeepsPinOverridesSparseTest::RunTest(const FString&)
{
	UBlueprint* Blueprint = CreateTransientActorBlueprint(TEXT("BP_GraphExtractSparsePins"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient actor Blueprint has an EventGraph"), Graph);
	if (!Graph)
	{
		return false;
	}

	AddPrintStringNode(Graph, 320, 0);

	FAssetDocumentCapabilityResult ExtractResult;
	const TSharedRef<FJsonObject> Body = ExtractBlueprintBody(Blueprint, ExtractResult);
	TestTrue(TEXT("Graph extract succeeds"), ExtractResult.bSuccess);
	const TArray<TSharedPtr<FJsonValue>>* Graphs = GetUbergraphPages(Body);
	TestTrue(TEXT("Extract writes Body.UbergraphPages"), Graphs && Graphs->Num() == 1);
	if (!Graphs || Graphs->IsEmpty())
	{
		return false;
	}

	const TSharedPtr<FJsonObject> ExtractedGraph = (*Graphs)[0]->AsObject();
	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	TestTrue(TEXT("Graph has nodes"), ExtractedGraph->TryGetArrayField(TEXT("Nodes"), Nodes) && Nodes && Nodes->Num() >= 1);
	const TSharedPtr<FJsonObject> PrintNode = Nodes ? FindNodeByMemberName(*Nodes, TEXT("PrintString")) : nullptr;
	TestTrue(TEXT("PrintString node extracted"), PrintNode.IsValid());
	TestFalse(TEXT("Baseline/default pins are omitted from sparse PinOverrides"), PrintNode.IsValid() && PrintNode->HasField(TEXT("PinOverrides")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphDiffUnchangedAfterExtractTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphDiff.UnchangedAfterExtract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphDiffUnchangedAfterExtractTest::RunTest(const FString&)
{
	UBlueprint* Blueprint = CreateTransientActorBlueprint(TEXT("BP_GraphDiffUnchanged"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient actor Blueprint has an EventGraph"), Graph);
	if (!Graph)
	{
		return false;
	}

	UK2Node_Event* BeginPlay = AddBeginPlayNode(Graph);
	UK2Node_CallFunction* Print = AddPrintStringNode(Graph, 320, 0, TEXT("Hello"));
	TestTrue(TEXT("BeginPlay links to PrintString"), LinkPins(BeginPlay ? BeginPlay->FindPin(UEdGraphSchema_K2::PN_Then) : nullptr, Print ? Print->FindPin(UEdGraphSchema_K2::PN_Execute) : nullptr));

	FAssetDocumentCapabilityResult ExtractResult;
	const TSharedRef<FJsonObject> DesiredBody = ExtractBlueprintBody(Blueprint, ExtractResult);
	TestTrue(TEXT("Graph extract succeeds"), ExtractResult.bSuccess);
	RemoveExtractOnlyEvidence(DesiredBody);

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult = DiffBlueprintBody(Blueprint, DesiredBody, DiffEntries);
	TestTrue(TEXT("Graph diff succeeds"), DiffResult.bSuccess);

	const TSharedPtr<FJsonObject> GraphDiff = FindDiffEntryByPath(DiffEntries, GraphPath(TEXT("EventGraph")));
	TestTrue(TEXT("Graph diff includes EventGraph"), GraphDiff.IsValid());
	if (GraphDiff.IsValid())
	{
		TestEqual(TEXT("EventGraph diff is unchanged"), GraphDiff->GetStringField(TEXT("status")), FString(TEXT("unchanged")));
	}
	TestFalse(TEXT("Extracted graph desired has no changed graph diff entries"), HasNonUnchangedGraphDiffEntry(DiffEntries));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphDiffReportsMissingNodeTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphDiff.ReportsMissingNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphDiffReportsMissingNodeTest::RunTest(const FString&)
{
	UBlueprint* Blueprint = CreateTransientActorBlueprint(TEXT("BP_GraphDiffMissingNode"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient actor Blueprint has an EventGraph"), Graph);
	if (!Graph)
	{
		return false;
	}
	AddBeginPlayNode(Graph);

	FAssetDocumentCapabilityResult ExtractResult;
	const TSharedRef<FJsonObject> DesiredBody = ExtractBlueprintBody(Blueprint, ExtractResult);
	TestTrue(TEXT("Graph extract succeeds"), ExtractResult.bSuccess);
	RemoveExtractOnlyEvidence(DesiredBody);
	TSharedPtr<FJsonObject> DesiredGraph = FindGraphByName(DesiredBody, TEXT("EventGraph"));
	TestTrue(TEXT("Desired graph exists"), DesiredGraph.IsValid());
	if (!DesiredGraph.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ExistingNodes = nullptr;
	TestTrue(TEXT("Desired graph has nodes"), DesiredGraph->TryGetArrayField(TEXT("Nodes"), ExistingNodes) && ExistingNodes);
	TArray<TSharedPtr<FJsonValue>> Nodes = ExistingNodes ? *ExistingNodes : TArray<TSharedPtr<FJsonValue>>();
	TSharedRef<FJsonObject> DesiredPrint = MakeGraphNode(
		TEXT("PrintString"),
		TEXT("/Script/BlueprintGraph.K2Node_CallFunction"),
		MakeMemberRef(TEXT("/Script/Engine.KismetSystemLibrary"), TEXT("PrintString")));
	Nodes.Add(MakeShared<FJsonValueObject>(DesiredPrint));
	DesiredGraph->SetArrayField(TEXT("Nodes"), MoveTemp(Nodes));

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult = DiffBlueprintBody(Blueprint, DesiredBody, DiffEntries);
	TestTrue(TEXT("Graph diff succeeds"), DiffResult.bSuccess);
	const TSharedPtr<FJsonObject> MissingNode = FindDiffEntryByPath(DiffEntries, NodePath(TEXT("EventGraph"), TEXT("PrintString")));
	TestTrue(TEXT("Diff reports missing PrintString node"), MissingNode.IsValid());
	if (MissingNode.IsValid())
	{
		TestEqual(TEXT("Missing node status"), MissingNode->GetStringField(TEXT("status")), FString(TEXT("missing")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphDiffReportsExtraNodeTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphDiff.ReportsExtraNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphDiffReportsExtraNodeTest::RunTest(const FString&)
{
	UBlueprint* Blueprint = CreateTransientActorBlueprint(TEXT("BP_GraphDiffExtraNode"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient actor Blueprint has an EventGraph"), Graph);
	if (!Graph)
	{
		return false;
	}
	AddBeginPlayNode(Graph);
	AddPrintStringNode(Graph, 320, 0);

	FAssetDocumentCapabilityResult ExtractResult;
	const TSharedRef<FJsonObject> DesiredBody = ExtractBlueprintBody(Blueprint, ExtractResult);
	TestTrue(TEXT("Graph extract succeeds"), ExtractResult.bSuccess);
	RemoveExtractOnlyEvidence(DesiredBody);
	const TSharedPtr<FJsonObject> DesiredGraph = FindGraphByName(DesiredBody, TEXT("EventGraph"));
	const FString PrintNodeId = GetNodeIdByMemberName(DesiredGraph, TEXT("PrintString"));
	TestFalse(TEXT("Extracted PrintString node id exists"), PrintNodeId.IsEmpty());
	TestTrue(TEXT("Desired graph omits PrintString node"), RemoveNodeByMemberName(DesiredGraph, TEXT("PrintString")));

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult = DiffBlueprintBody(Blueprint, DesiredBody, DiffEntries);
	TestTrue(TEXT("Graph diff succeeds"), DiffResult.bSuccess);
	const TSharedPtr<FJsonObject> ExtraNode = FindDiffEntryByPath(DiffEntries, NodePath(TEXT("EventGraph"), PrintNodeId));
	TestTrue(TEXT("Diff reports extra PrintString node"), ExtraNode.IsValid());
	if (ExtraNode.IsValid())
	{
		TestEqual(TEXT("Extra node status"), ExtraNode->GetStringField(TEXT("status")), FString(TEXT("extra")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphDiffReportsChangedPinDefaultTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphDiff.ReportsChangedPinDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphDiffReportsChangedPinDefaultTest::RunTest(const FString&)
{
	UBlueprint* Blueprint = CreateTransientActorBlueprint(TEXT("BP_GraphDiffChangedPin"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient actor Blueprint has an EventGraph"), Graph);
	if (!Graph)
	{
		return false;
	}
	AddPrintStringNode(Graph, 320, 0, TEXT("Current"));

	FAssetDocumentCapabilityResult ExtractResult;
	const TSharedRef<FJsonObject> DesiredBody = ExtractBlueprintBody(Blueprint, ExtractResult);
	TestTrue(TEXT("Graph extract succeeds"), ExtractResult.bSuccess);
	RemoveExtractOnlyEvidence(DesiredBody);
	const TSharedPtr<FJsonObject> DesiredGraph = FindGraphByName(DesiredBody, TEXT("EventGraph"));
	const FString PrintNodeId = GetNodeIdByMemberName(DesiredGraph, TEXT("PrintString"));
	TestFalse(TEXT("Extracted PrintString node id exists"), PrintNodeId.IsEmpty());
	TestTrue(TEXT("Desired pin default is edited"), SetPinDefaultForMemberNode(DesiredGraph, TEXT("PrintString"), TEXT("InString"), TEXT("Desired")));

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult = DiffBlueprintBody(Blueprint, DesiredBody, DiffEntries);
	TestTrue(TEXT("Graph diff succeeds"), DiffResult.bSuccess);
	const TSharedPtr<FJsonObject> ChangedPin = FindDiffEntryByPath(DiffEntries, PinPath(TEXT("EventGraph"), PrintNodeId, TEXT("InString")));
	TestTrue(TEXT("Diff reports changed InString default"), ChangedPin.IsValid());
	if (ChangedPin.IsValid())
	{
		TestEqual(TEXT("Changed pin status"), ChangedPin->GetStringField(TEXT("status")), FString(TEXT("changed")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphDiffReportsMissingLinkTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphDiff.ReportsMissingLink",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphDiffReportsMissingLinkTest::RunTest(const FString&)
{
	UBlueprint* Blueprint = CreateTransientActorBlueprint(TEXT("BP_GraphDiffMissingLink"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient actor Blueprint has an EventGraph"), Graph);
	if (!Graph)
	{
		return false;
	}
	AddBeginPlayNode(Graph);
	AddPrintStringNode(Graph, 320, 0);

	FAssetDocumentCapabilityResult ExtractResult;
	const TSharedRef<FJsonObject> DesiredBody = ExtractBlueprintBody(Blueprint, ExtractResult);
	TestTrue(TEXT("Graph extract succeeds"), ExtractResult.bSuccess);
	RemoveExtractOnlyEvidence(DesiredBody);
	const TSharedPtr<FJsonObject> DesiredGraph = FindGraphByName(DesiredBody, TEXT("EventGraph"));
	const FString BeginPlayNodeId = GetNodeIdByMemberName(DesiredGraph, TEXT("ReceiveBeginPlay"));
	const FString PrintNodeId = GetNodeIdByMemberName(DesiredGraph, TEXT("PrintString"));
	TestFalse(TEXT("Extracted BeginPlay node id exists"), BeginPlayNodeId.IsEmpty());
	TestFalse(TEXT("Extracted PrintString node id exists"), PrintNodeId.IsEmpty());
	AddLinkToGraph(DesiredGraph, BeginPlayNodeId, UEdGraphSchema_K2::PN_Then.ToString(), PrintNodeId, UEdGraphSchema_K2::PN_Execute.ToString());

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult = DiffBlueprintBody(Blueprint, DesiredBody, DiffEntries);
	TestTrue(TEXT("Graph diff succeeds"), DiffResult.bSuccess);
	const TSharedPtr<FJsonObject> MissingLink = FindDiffEntryByPath(
		DiffEntries,
		LinkPath(TEXT("EventGraph"), BeginPlayNodeId, UEdGraphSchema_K2::PN_Then.ToString(), PrintNodeId, UEdGraphSchema_K2::PN_Execute.ToString()));
	TestTrue(TEXT("Diff reports missing execution link"), MissingLink.IsValid());
	if (MissingLink.IsValid())
	{
		TestEqual(TEXT("Missing link status"), MissingLink->GetStringField(TEXT("status")), FString(TEXT("missing")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintGraphDiffTreatsDefinitionRefAndInlineMemberRefAsEqualTest,
	"AssetFactory.AssetDocument.UBlueprint.GraphDiff.TreatsDefinitionRefAndInlineMemberRefAsEqual",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintGraphDiffTreatsDefinitionRefAndInlineMemberRefAsEqualTest::RunTest(const FString&)
{
	UBlueprint* Blueprint = CreateTransientActorBlueprint(TEXT("BP_GraphDiffDefinitionRef"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient actor Blueprint has an EventGraph"), Graph);
	if (!Graph)
	{
		return false;
	}
	AddPrintStringNode(Graph, 320, 0);

	FAssetDocumentCapabilityResult ExtractResult;
	const TSharedRef<FJsonObject> DesiredBody = ExtractBlueprintBody(Blueprint, ExtractResult);
	TestTrue(TEXT("Graph extract succeeds"), ExtractResult.bSuccess);
	RemoveExtractOnlyEvidence(DesiredBody);
	const TSharedPtr<FJsonObject> DesiredGraph = FindGraphByName(DesiredBody, TEXT("EventGraph"));
	const FString PrintNodeId = GetNodeIdByMemberName(DesiredGraph, TEXT("PrintString"));
	TestFalse(TEXT("Extracted PrintString node id exists"), PrintNodeId.IsEmpty());
	TestTrue(TEXT("Desired PrintString member uses DefinitionRef"), ReplaceMemberWithDefinitionRef(DesiredGraph, TEXT("PrintString"), TEXT("Func.KismetSystemLibrary.PrintString")));

	TSharedPtr<FJsonObject> Definitions = MakeShared<FJsonObject>();
	Definitions->SetObjectField(
		TEXT("Func.KismetSystemLibrary.PrintString"),
		MakeMemberRef(TEXT("/Script/Engine.KismetSystemLibrary"), TEXT("PrintString")));

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult = DiffBlueprintBody(Blueprint, DesiredBody, DiffEntries, &Definitions);
	TestTrue(TEXT("Graph diff succeeds"), DiffResult.bSuccess);
	const TSharedPtr<FJsonObject> PrintNodeDiff = FindDiffEntryByPath(DiffEntries, NodePath(TEXT("EventGraph"), PrintNodeId));
	TestTrue(TEXT("Diff includes PrintString node"), PrintNodeDiff.IsValid());
	if (PrintNodeDiff.IsValid())
	{
		TestEqual(TEXT("DefinitionRef and inline MemberRef compare unchanged"), PrintNodeDiff->GetStringField(TEXT("status")), FString(TEXT("unchanged")));
	}
	TestFalse(TEXT("DefinitionRef desired has no changed graph diff entries"), HasNonUnchangedGraphDiffEntry(DiffEntries));
	return true;
}

#endif
