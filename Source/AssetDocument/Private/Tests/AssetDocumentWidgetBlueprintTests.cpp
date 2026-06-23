// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentLifecycle.h"
#include "AssetDocumentProfile.h"
#include "AssetDocumentService.h"
#include "Profiles/WidgetBlueprintAssetDocumentCapability.h"
#include "Profiles/WidgetBlueprintAssetDocumentProfile.h"

#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/ContentWidget.h"
#include "Components/NamedSlot.h"
#include "Components/TextBlock.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Tunnel.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/Guid.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Test/TestUserWidget.h"
#include "WidgetBlueprint.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
TSharedRef<FJsonObject> MakeUserWidgetParentClassRef()
{
	TSharedRef<FJsonObject> ParentClass = MakeShared<FJsonObject>();
	ParentClass->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ParentClass->SetStringField(TEXT("Class"), TEXT("/Script/UMG.UserWidget"));
	return ParentClass;
}

TSharedRef<FJsonObject> MakeDefaultWidgetBlueprintBody()
{
	TSharedRef<FJsonObject> WidgetTree = MakeShared<FJsonObject>();
	WidgetTree->SetField(TEXT("RootWidget"), MakeShared<FJsonValueNull>());
	WidgetTree->SetObjectField(TEXT("NamedSlotBindings"), MakeShared<FJsonObject>());

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), MakeUserWidgetParentClassRef());
	Body->SetArrayField(TEXT("ImplementedInterfaces"), {});
	Body->SetArrayField(TEXT("Variables"), {});
	Body->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
	Body->SetObjectField(TEXT("WidgetTree"), WidgetTree);
	Body->SetArrayField(TEXT("Bindings"), {});
	Body->SetArrayField(TEXT("Animations"), {});
	Body->SetArrayField(TEXT("UbergraphPages"), {});
	Body->SetArrayField(TEXT("FunctionGraphs"), {});
	Body->SetArrayField(TEXT("MacroGraphs"), {});
	Body->SetObjectField(TEXT("Palette"), MakeShared<FJsonObject>());
	Body->SetObjectField(TEXT("EditorOptions"), MakeShared<FJsonObject>());
	Body->SetObjectField(TEXT("WidgetVariableGuids"), MakeShared<FJsonObject>());
	return Body;
}

FString MakeUniqueWidgetBlueprintTarget(const TCHAR* Prefix)
{
	return FString::Printf(
		TEXT("/Game/AssetDocumentTests/%s_%s"),
		Prefix,
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
}

TSharedRef<FJsonValue> MakeBodyJsonValue(const TSharedRef<FJsonObject>& Body)
{
	return StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueObject>(Body));
}

static TSharedRef<FJsonObject> MakeClassRef(const FString& ClassPath)
{
	TSharedRef<FJsonObject> ClassRef = MakeShared<FJsonObject>();
	ClassRef->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ClassRef->SetStringField(TEXT("Class"), ClassPath);
	return ClassRef;
}

TSharedRef<FJsonObject> MakeTestUserWidgetParentClassRef()
{
	return MakeClassRef(UTestUserWidget::StaticClass()->GetPathName());
}

static TSharedPtr<FJsonObject> MakeWidgetBlueprintDocument(const FString& Target, TSharedPtr<FJsonObject> Body)
{
	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), TEXT("/Script/UMGEditor.WidgetBlueprint"));
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Body"), Body);
	return Document;
}

static TSharedRef<FJsonObject> MakeWidgetNode(const FString& Name, const FString& ClassPath)
{
	TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("Name"), Name);
	Node->SetStringField(TEXT("Class"), ClassPath);
	Node->SetBoolField(TEXT("IsVariable"), false);
	Node->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Node->SetObjectField(TEXT("Slot"), MakeShared<FJsonObject>());
	Node->SetArrayField(TEXT("Children"), {});
	return Node;
}

static FAssetDocumentApplyRequest MakeApplyFileRequest(TSharedPtr<FJsonObject> Document)
{
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;
	return Request;
}

TSharedRef<FJsonObject> MakeWidgetTreeBody(TSharedPtr<FJsonObject> WidgetTree)
{
	TSharedRef<FJsonObject> Body = MakeDefaultWidgetBlueprintBody();
	Body->SetObjectField(TEXT("WidgetTree"), WidgetTree);
	return Body;
}

TSharedPtr<FJsonObject> MakeWidgetTree(TSharedPtr<FJsonObject> RootWidget)
{
	TSharedPtr<FJsonObject> WidgetTree = MakeShared<FJsonObject>();
	if (RootWidget.IsValid())
	{
		WidgetTree->SetObjectField(TEXT("RootWidget"), RootWidget);
	}
	else
	{
		WidgetTree->SetField(TEXT("RootWidget"), MakeShared<FJsonValueNull>());
	}
	WidgetTree->SetObjectField(TEXT("NamedSlotBindings"), MakeShared<FJsonObject>());
	return WidgetTree;
}

FString MakeObjectPathFromTarget(const FString& Target)
{
	return FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
}

UWidgetBlueprint* LoadWidgetBlueprintForTarget(const FString& Target)
{
	return LoadObject<UWidgetBlueprint>(nullptr, *MakeObjectPathFromTarget(Target));
}

bool ResultHasDiagnosticCode(const FAssetDocumentResult& Result, const FString& ExpectedCode)
{
	return Result.Diagnostics.ContainsByPredicate([&ExpectedCode](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == ExpectedCode;
	});
}

bool ResultHasDiagnosticPath(const FAssetDocumentResult& Result, const FString& ExpectedPath)
{
	return Result.Diagnostics.ContainsByPredicate([&ExpectedPath](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Path == ExpectedPath;
	});
}

bool DiffPayloadHasNoChangedOrFailedEntries(const TSharedPtr<FJsonObject>& Payload)
{
	if (!Payload.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Changed = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Failed = nullptr;
	return Payload->TryGetArrayField(TEXT("changed"), Changed)
		&& Payload->TryGetArrayField(TEXT("failed"), Failed)
		&& Changed
		&& Failed
		&& Changed->Num() == 0
		&& Failed->Num() == 0;
}

bool DiffPayloadHasChangedEntries(const TSharedPtr<FJsonObject>& Payload)
{
	if (!Payload.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Changed = nullptr;
	return Payload->TryGetArrayField(TEXT("changed"), Changed)
		&& Changed
		&& Changed->Num() > 0;
}

TSharedPtr<FJsonObject> GetExtractedBody(const FAssetDocumentResult& ExtractResult)
{
	if (!ExtractResult.Payload.IsValid())
	{
		return nullptr;
	}

	const TSharedPtr<FJsonObject>* Body = nullptr;
	if (ExtractResult.Payload->TryGetObjectField(TEXT("Body"), Body) && Body && Body->IsValid())
	{
		return *Body;
	}
	return nullptr;
}

TSharedRef<FJsonObject> MakeFloatPinType()
{
	TSharedRef<FJsonObject> Type = MakeShared<FJsonObject>();
	Type->SetStringField(TEXT("PinCategory"), TEXT("real"));
	Type->SetStringField(TEXT("PinSubCategory"), TEXT("float"));
	return Type;
}

TSharedPtr<FJsonObject> MakeFloatVariable(const TCHAR* Name, const TCHAR* DefaultValue, const TCHAR* Category = nullptr, const TCHAR* Tooltip = nullptr)
{
	TSharedPtr<FJsonObject> Variable = MakeShared<FJsonObject>();
	Variable->SetStringField(TEXT("Name"), Name);
	Variable->SetObjectField(TEXT("Type"), MakeFloatPinType());
	Variable->SetStringField(TEXT("DefaultValue"), DefaultValue);
	if (Category)
	{
		Variable->SetStringField(TEXT("Category"), Category);
	}
	if (Tooltip)
	{
		Variable->SetStringField(TEXT("Tooltip"), Tooltip);
	}
	return Variable;
}

TArray<TSharedPtr<FJsonValue>> MakeVariableArray(std::initializer_list<TSharedPtr<FJsonObject>> Variables)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	for (const TSharedPtr<FJsonObject>& Variable : Variables)
	{
		Result.Add(MakeShared<FJsonValueObject>(Variable));
	}
	return Result;
}

TSharedRef<FJsonObject> MakeBindingFixtureBody()
{
	TSharedRef<FJsonObject> TitleText = MakeWidgetNode(TEXT("TitleText"), TEXT("/Script/UMG.TextBlock"));
	TitleText->SetBoolField(TEXT("IsVariable"), true);
	TitleText->SetStringField(TEXT("VariableName"), TEXT("TitleText"));

	TSharedRef<FJsonObject> Body = MakeWidgetTreeBody(MakeWidgetTree(TitleText));
	Body->SetObjectField(TEXT("ParentClass"), MakeTestUserWidgetParentClassRef());
	return Body;
}

TSharedPtr<FJsonObject> MakeFunctionBinding(const TCHAR* Widget, const TCHAR* Property, const TCHAR* Function)
{
	TSharedPtr<FJsonObject> Binding = MakeShared<FJsonObject>();
	Binding->SetStringField(TEXT("Widget"), Widget);
	Binding->SetStringField(TEXT("Property"), Property);
	Binding->SetStringField(TEXT("Kind"), TEXT("Function"));
	Binding->SetStringField(TEXT("Function"), Function);
	return Binding;
}

TSharedPtr<FJsonObject> MakePropertyBinding(const TCHAR* Widget, const TCHAR* Property, std::initializer_list<const TCHAR*> SourcePath)
{
	TSharedPtr<FJsonObject> Binding = MakeShared<FJsonObject>();
	Binding->SetStringField(TEXT("Widget"), Widget);
	Binding->SetStringField(TEXT("Property"), Property);
	Binding->SetStringField(TEXT("Kind"), TEXT("Property"));

	TArray<TSharedPtr<FJsonValue>> Path;
	for (const TCHAR* Segment : SourcePath)
	{
		Path.Add(MakeShared<FJsonValueString>(Segment));
	}
	Binding->SetArrayField(TEXT("SourcePath"), Path);
	return Binding;
}

void SetBindings(TSharedRef<FJsonObject> Body, std::initializer_list<TSharedPtr<FJsonObject>> Bindings)
{
	TArray<TSharedPtr<FJsonValue>> BindingValues;
	for (const TSharedPtr<FJsonObject>& Binding : Bindings)
	{
		BindingValues.Add(MakeShared<FJsonValueObject>(Binding));
	}
	Body->SetArrayField(TEXT("Bindings"), BindingValues);
}

TSharedPtr<FJsonObject> MakeGraphMemberRef(const TCHAR* OwnerClass, const TCHAR* Name)
{
	TSharedPtr<FJsonObject> Member = MakeShared<FJsonObject>();
	Member->SetStringField(TEXT("Kind"), TEXT("MemberRef"));
	Member->SetStringField(TEXT("OwnerClass"), OwnerClass);
	Member->SetStringField(TEXT("Name"), Name);
	return Member;
}

TSharedPtr<FJsonObject> MakeGraphNode(const TCHAR* Id, const TCHAR* ClassPath, TSharedPtr<FJsonObject> Member = nullptr)
{
	TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("Id"), Id);
	Node->SetStringField(TEXT("Class"), ClassPath);
	if (Member.IsValid())
	{
		Node->SetObjectField(TEXT("Member"), Member);
	}
	TSharedRef<FJsonObject> Position = MakeShared<FJsonObject>();
	Position->SetNumberField(TEXT("X"), 0);
	Position->SetNumberField(TEXT("Y"), 0);
	Node->SetObjectField(TEXT("Position"), Position);
	return Node;
}

TSharedPtr<FJsonObject> MakeGraph(const TCHAR* Name, const TCHAR* Schema, std::initializer_list<TSharedPtr<FJsonObject>> Nodes)
{
	TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();
	Graph->SetStringField(TEXT("Name"), Name);
	Graph->SetStringField(TEXT("Schema"), Schema);
	TArray<TSharedPtr<FJsonValue>> NodeValues;
	for (const TSharedPtr<FJsonObject>& Node : Nodes)
	{
		NodeValues.Add(MakeShared<FJsonValueObject>(Node.ToSharedRef()));
	}
	Graph->SetArrayField(TEXT("Nodes"), NodeValues);
	Graph->SetArrayField(TEXT("Links"), {});
	return Graph;
}

void SetGraphRegion(TSharedRef<FJsonObject> Body, const TCHAR* RegionName, std::initializer_list<TSharedPtr<FJsonObject>> Graphs)
{
	TArray<TSharedPtr<FJsonValue>> GraphValues;
	for (const TSharedPtr<FJsonObject>& Graph : Graphs)
	{
		GraphValues.Add(MakeShared<FJsonValueObject>(Graph.ToSharedRef()));
	}
	Body->SetArrayField(RegionName, GraphValues);
}

TArray<TSharedPtr<FJsonValue>> GetExtractedBindings(const FAssetDocumentResult& ExtractResult)
{
	TArray<TSharedPtr<FJsonValue>> Empty;
	TSharedPtr<FJsonObject> Body = GetExtractedBody(ExtractResult);
	if (!Body.IsValid())
	{
		return Empty;
	}

	const TArray<TSharedPtr<FJsonValue>>* Bindings = nullptr;
	if (Body->TryGetArrayField(TEXT("Bindings"), Bindings) && Bindings)
	{
		return *Bindings;
	}
	return Empty;
}

const TArray<TSharedPtr<FJsonValue>>* GetExtractedGraphRegion(const FAssetDocumentResult& ExtractResult, const TCHAR* RegionName)
{
	TSharedPtr<FJsonObject> Body = GetExtractedBody(ExtractResult);
	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	return Body.IsValid() && Body->TryGetArrayField(RegionName, Graphs) ? Graphs : nullptr;
}

TSharedPtr<FJsonObject> FindExtractedGraph(const FAssetDocumentResult& ExtractResult, const TCHAR* RegionName, const TCHAR* GraphName)
{
	const TArray<TSharedPtr<FJsonValue>>* Graphs = GetExtractedGraphRegion(ExtractResult, RegionName);
	if (!Graphs)
	{
		return nullptr;
	}

	for (const TSharedPtr<FJsonValue>& GraphValue : *Graphs)
	{
		const TSharedPtr<FJsonObject> Graph = GraphValue.IsValid() && GraphValue->Type == EJson::Object ? GraphValue->AsObject() : nullptr;
		FString Name;
		if (Graph.IsValid() && Graph->TryGetStringField(TEXT("Name"), Name) && Name == GraphName)
		{
			return Graph;
		}
	}
	return nullptr;
}

bool ExtractedGraphHasNodeClass(const TSharedPtr<FJsonObject>& Graph, const TCHAR* ClassPath)
{
	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (!Graph.IsValid() || !Graph->TryGetArrayField(TEXT("Nodes"), Nodes) || !Nodes)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
	{
		const TSharedPtr<FJsonObject> Node = NodeValue.IsValid() && NodeValue->Type == EJson::Object ? NodeValue->AsObject() : nullptr;
		FString NodeClass;
		if (Node.IsValid() && Node->TryGetStringField(TEXT("Class"), NodeClass) && NodeClass == ClassPath)
		{
			return true;
		}
	}
	return false;
}

bool ExtractedBodySkippedGraphContainsClass(const FAssetDocumentResult& ExtractResult, const TCHAR* ClassPath)
{
	TSharedPtr<FJsonObject> Body = GetExtractedBody(ExtractResult);
	const TSharedPtr<FJsonObject>* Skipped = nullptr;
	const TSharedPtr<FJsonObject>* Graphs = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (!Body.IsValid()
		|| !Body->TryGetObjectField(TEXT("_Skipped"), Skipped)
		|| !Skipped
		|| !(*Skipped)->TryGetObjectField(TEXT("Graphs"), Graphs)
		|| !Graphs
		|| !(*Graphs)->TryGetArrayField(TEXT("Nodes"), Nodes)
		|| !Nodes)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
	{
		const TSharedPtr<FJsonObject> Node = NodeValue.IsValid() && NodeValue->Type == EJson::Object ? NodeValue->AsObject() : nullptr;
		FString NodeClass;
		if (Node.IsValid() && Node->TryGetStringField(TEXT("Class"), NodeClass) && NodeClass == ClassPath)
		{
			return true;
		}
	}
	return false;
}

UEdGraph* FindWidgetBlueprintGraphByName(const UWidgetBlueprint* WidgetBlueprint, const FString& GraphName)
{
	if (!WidgetBlueprint)
	{
		return nullptr;
	}

	for (UEdGraph* Graph : WidgetBlueprint->UbergraphPages)
	{
		if (Graph && Graph->GetName() == GraphName)
		{
			return Graph;
		}
	}
	for (UEdGraph* Graph : WidgetBlueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == GraphName)
		{
			return Graph;
		}
	}
	for (UEdGraph* Graph : WidgetBlueprint->MacroGraphs)
	{
		if (Graph && Graph->GetName() == GraphName)
		{
			return Graph;
		}
	}
	return nullptr;
}

bool GraphHasConcreteNodeClass(const UEdGraph* Graph, const UClass* NodeClass)
{
	return Graph && NodeClass && Graph->Nodes.ContainsByPredicate([NodeClass](const UEdGraphNode* Node)
	{
		return Node && Node->IsA(NodeClass);
	});
}

UObject* GetWidgetBlueprintCDO(const UWidgetBlueprint* WidgetBlueprint)
{
	return WidgetBlueprint && WidgetBlueprint->GeneratedClass
		? WidgetBlueprint->GeneratedClass->GetDefaultObject(false)
		: nullptr;
}

bool GetGeneratedBoolDefault(const UWidgetBlueprint* WidgetBlueprint, FName PropertyName, bool& OutValue)
{
	UObject* CDO = GetWidgetBlueprintCDO(WidgetBlueprint);
	FBoolProperty* Property = CDO ? FindFProperty<FBoolProperty>(CDO->GetClass(), PropertyName) : nullptr;
	if (!Property)
	{
		return false;
	}
	OutValue = Property->GetPropertyValue_InContainer(CDO);
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintProfileTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Profile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintProfileTest::RunTest(const FString&)
{
	FWidgetBlueprintAssetDocumentProfile Profile;
	TestEqual(TEXT("Exact class is UWidgetBlueprint"), Profile.GetExactClass(), UWidgetBlueprint::StaticClass());

	const TArray<FName> BodyKeys = Profile.GetBodyKeys();
	TestTrue(TEXT("ParentClass body key is registered"), BodyKeys.Contains(TEXT("ParentClass")));
	TestTrue(TEXT("WidgetTree body key is registered"), BodyKeys.Contains(TEXT("WidgetTree")));
	TestTrue(TEXT("Bindings body key is registered"), BodyKeys.Contains(TEXT("Bindings")));
	TestTrue(TEXT("Animations body key is registered"), BodyKeys.Contains(TEXT("Animations")));
	TestTrue(TEXT("FunctionGraphs body key is registered"), BodyKeys.Contains(TEXT("FunctionGraphs")));
	TestTrue(TEXT("WidgetVariableGuids body key is registered"), BodyKeys.Contains(TEXT("WidgetVariableGuids")));

	FAssetDocumentTemplateContext Context;
	Context.Target = TEXT("/Game/AssetDocumentTests/WBP_Template");
	Context.ClassPath = TEXT("/Script/UMGEditor.WidgetBlueprint");
	TSharedRef<FJsonObject> Template = Profile.CreateTemplate(Context);

	TestEqual(TEXT("Template class is WidgetBlueprint"), Template->GetStringField(TEXT("Class")), FString(TEXT("/Script/UMGEditor.WidgetBlueprint")));
	const TSharedPtr<FJsonObject>* Body = nullptr;
	TestTrue(TEXT("Template contains Body"), Template->TryGetObjectField(TEXT("Body"), Body) && Body && Body->IsValid());
	if (Body && Body->IsValid())
	{
		TestTrue(TEXT("Body contains ParentClass"), (*Body)->HasField(TEXT("ParentClass")));
		TestTrue(TEXT("Body contains WidgetTree"), (*Body)->HasField(TEXT("WidgetTree")));
		TestTrue(TEXT("Body contains Bindings"), (*Body)->HasField(TEXT("Bindings")));
		TestTrue(TEXT("Body contains Animations"), (*Body)->HasField(TEXT("Animations")));
	}

	const TArray<FAssetDocumentRegionPolicy> Policies = Profile.GetRegionPolicies();
	auto HasPolicy = [&Policies](FName RegionId)
	{
		return Policies.ContainsByPredicate([RegionId](const FAssetDocumentRegionPolicy& Policy)
		{
			return Policy.RegionId == RegionId;
		});
	};
	TestTrue(TEXT("WidgetTree has region policy"), HasPolicy(TEXT("Body.WidgetTree")));
	TestTrue(TEXT("Bindings has region policy"), HasPolicy(TEXT("Body.Bindings")));
	TestTrue(TEXT("Animations has region policy"), HasPolicy(TEXT("Body.Animations")));
	const FAssetDocumentRegionPolicy* WidgetVariableGuidsPolicy = Policies.FindByPredicate([](const FAssetDocumentRegionPolicy& Policy)
	{
		return Policy.RegionId == TEXT("Body.WidgetVariableGuids");
	});
	TestNotNull(TEXT("WidgetVariableGuids has region policy"), WidgetVariableGuidsPolicy);
	if (WidgetVariableGuidsPolicy)
	{
		TestEqual(TEXT("WidgetVariableGuids uses canonicalizer"), WidgetVariableGuidsPolicy->CanonicalizerHookName, FName(TEXT("WidgetBlueprintWidgetVariableGuids")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintRejectsNonUserWidgetParentTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.RejectsNonUserWidgetParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintRejectsNonUserWidgetParentTest::RunTest(const FString&)
{
	FWidgetBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UWidgetBlueprint::StaticClass();

	TSharedRef<FJsonObject> ParentClass = MakeShared<FJsonObject>();
	ParentClass->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ParentClass->SetStringField(TEXT("Class"), TEXT("/Script/Engine.Actor"));

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), ParentClass);
	Body->SetObjectField(TEXT("WidgetTree"), MakeShared<FJsonObject>());
	Body->SetArrayField(TEXT("Bindings"), {});
	Body->SetArrayField(TEXT("Animations"), {});

	const FAssetDocumentCapabilityResult Result = Capability.Validate(
		Context,
		StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueObject>(Body)));

	TestFalse(TEXT("Non-UUserWidget parent fails validation"), Result.bSuccess);
	TestTrue(TEXT("Diagnostic mentions ParentClass"), Result.Message.Contains(TEXT("ParentClass")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintTemplateBodyValidatesTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.TemplateBodyValidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintTemplateBodyValidatesTest::RunTest(const FString&)
{
	FWidgetBlueprintAssetDocumentProfile Profile;
	FAssetDocumentTemplateContext TemplateContext;
	TemplateContext.Target = TEXT("/Game/AssetDocumentTests/WBP_TemplateBodyValidates");
	TemplateContext.ClassPath = TEXT("/Script/UMGEditor.WidgetBlueprint");

	const TSharedRef<FJsonObject> Template = Profile.CreateTemplate(TemplateContext);
	const TSharedPtr<FJsonObject>* Body = nullptr;
	TestTrue(TEXT("Template contains Body"), Template->TryGetObjectField(TEXT("Body"), Body) && Body && Body->IsValid());
	if (!Body || !Body->IsValid())
	{
		return false;
	}

	FWidgetBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UWidgetBlueprint::StaticClass();
	const FAssetDocumentCapabilityResult Result = Capability.Validate(
		Context,
		MakeBodyJsonValue((*Body).ToSharedRef()));

	TestTrue(TEXT("Template body validates with default UUserWidget parent"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Message);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintLifecycleCreatesDefaultParentTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.LifecycleCreatesDefaultParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintLifecycleCreatesDefaultParentTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_LifecycleDefaultParent"));
	TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetObjectField(TEXT("Body"), MakeDefaultWidgetBlueprintBody());

	const FAssetDocumentLifecycleResult Result = FAssetDocumentLifecycle::CreateOrLoad(
		Target,
		UWidgetBlueprint::StaticClass(),
		EAssetDocumentLifecycleAction::Create,
		Document);

	ON_SCOPE_EXIT
	{
		FAssetDocumentLifecycle::CleanupCreatedAsset(Result);
	};

	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Result.Asset);
	TestTrue(TEXT("Lifecycle creates exact UWidgetBlueprint"), WidgetBlueprint != nullptr);
	TestTrue(TEXT("Lifecycle result is marked created"), Result.bCreated);
	TestTrue(TEXT("Lifecycle has no error"), Result.Error.IsEmpty());
	if (WidgetBlueprint)
	{
		TestEqual(TEXT("Default parent is UUserWidget"), WidgetBlueprint->ParentClass.Get(), UUserWidget::StaticClass());
	}
	return Result.Error.IsEmpty() && WidgetBlueprint != nullptr;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintRejectsNonEmptyAuthoredRegionsTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.RejectsNonEmptyAuthoredRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintRejectsNonEmptyAuthoredRegionsTest::RunTest(const FString&)
{
	FWidgetBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UWidgetBlueprint::StaticClass();

	TSharedRef<FJsonObject> Body = MakeDefaultWidgetBlueprintBody();
	TArray<TSharedPtr<FJsonValue>> Animations;
	Animations.Add(MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()));
	Body->SetArrayField(TEXT("Animations"), Animations);

	const FAssetDocumentCapabilityResult Result = Capability.Validate(Context, MakeBodyJsonValue(Body));
	TestFalse(TEXT("Non-empty authored Animations fail validation"), Result.bSuccess);
	TestTrue(TEXT("Diagnostic uses UnsupportedWidgetBlueprintRegion"), Result.Diagnostics.ContainsByPredicate([](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("UnsupportedWidgetBlueprintRegion");
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintExistingNonEmptyStateBlocksTask1ApplyTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.ExistingNonEmptyStateBlocksTask1Apply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintExistingNonEmptyStateBlocksTask1ApplyTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_NonEmptyApplyGuard"));
	TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetObjectField(TEXT("Body"), MakeDefaultWidgetBlueprintBody());

	const FAssetDocumentLifecycleResult LifecycleResult = FAssetDocumentLifecycle::CreateOrLoad(
		Target,
		UWidgetBlueprint::StaticClass(),
		EAssetDocumentLifecycleAction::Create,
		Document);

	ON_SCOPE_EXIT
	{
		FAssetDocumentLifecycle::CleanupCreatedAsset(LifecycleResult);
	};

	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(LifecycleResult.Asset);
	TestTrue(TEXT("Lifecycle created WidgetBlueprint"), WidgetBlueprint != nullptr);
	if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
	{
		return false;
	}

	UWidget* RootWidget = WidgetBlueprint->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetBlueprint->WidgetTree->RootWidget = RootWidget;
	UWidgetAnimation* Animation = NewObject<UWidgetAnimation>(WidgetBlueprint, TEXT("Intro"));
	WidgetBlueprint->Animations.Add(Animation);

	FWidgetBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.Asset = WidgetBlueprint;
	Context.AssetClass = UWidgetBlueprint::StaticClass();

	const FAssetDocumentCapabilityResult ApplyResult = Capability.Apply(Context, MakeBodyJsonValue(MakeDefaultWidgetBlueprintBody()));
	TestFalse(TEXT("Task 1 apply refuses existing non-empty unsupported state"), ApplyResult.bSuccess);
	TestTrue(TEXT("Apply diagnostic uses UnsupportedWidgetBlueprintRegion"), ApplyResult.Diagnostics.ContainsByPredicate([](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("UnsupportedWidgetBlueprintRegion");
	}));
	TestTrue(TEXT("RootWidget remains intact"), WidgetBlueprint->WidgetTree->RootWidget == RootWidget);
	TestEqual(TEXT("Animations remain intact"), WidgetBlueprint->Animations.Num(), 1);

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult = Capability.Diff(Context, MakeBodyJsonValue(MakeDefaultWidgetBlueprintBody()), DiffEntries);
	TestTrue(TEXT("Diff succeeds for inspection"), DiffResult.bSuccess);
	TestTrue(TEXT("Diff exposes non-empty unsupported Animations as changed/skipped"), DiffEntries.ContainsByPredicate([](const TSharedPtr<FJsonValue>& Entry)
	{
		const TSharedPtr<FJsonObject> Object = Entry.IsValid() ? Entry->AsObject() : nullptr;
		FString Path;
		FString Status;
		return Object.IsValid()
			&& Object->TryGetStringField(TEXT("path"), Path)
			&& Path == TEXT("/Body/Animations")
			&& Object->TryGetStringField(TEXT("status"), Status)
			&& Status != TEXT("unchanged");
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintMetadataClassDefaultsAuthoritativeTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Metadata.ClassDefaultsAuthoritative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintMetadataClassDefaultsAuthoritativeTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_MetadataClassDefaults"));
	FAssetDocumentService Service;

	TSharedRef<FJsonObject> Body = MakeDefaultWidgetBlueprintBody();
	Body->GetObjectField(TEXT("ClassDefaults"))->SetBoolField(TEXT("bIsFocusable"), true);
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, Body)));
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("ClassDefaults apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("ClassDefaults apply succeeds"), ApplyResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint loads"), WidgetBlueprint);
	UUserWidget* GeneratedCDO = WidgetBlueprint && WidgetBlueprint->GeneratedClass
		? Cast<UUserWidget>(WidgetBlueprint->GeneratedClass->GetDefaultObject(false))
		: nullptr;
	TestNotNull(TEXT("Generated CDO is a UUserWidget"), GeneratedCDO);
	if (GeneratedCDO)
	{
		TestTrue(TEXT("bIsFocusable is applied to generated CDO"), GeneratedCDO->IsFocusable());
	}

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds after ClassDefaults apply"), ExtractResult.IsSuccess());
	TSharedPtr<FJsonObject> ExtractedBody = GetExtractedBody(ExtractResult);
	const TSharedPtr<FJsonObject>* ExtractedClassDefaults = nullptr;
	TestTrue(TEXT("Extract includes ClassDefaults"), ExtractedBody.IsValid() && ExtractedBody->TryGetObjectField(TEXT("ClassDefaults"), ExtractedClassDefaults));
	if (ExtractedClassDefaults && ExtractedClassDefaults->IsValid())
	{
		TestTrue(TEXT("Extracted ClassDefaults includes bIsFocusable"), (*ExtractedClassDefaults)->GetBoolField(TEXT("bIsFocusable")));
	}

	TSharedRef<FJsonObject> ResetBody = MakeDefaultWidgetBlueprintBody();
	const FAssetDocumentResult ResetResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, ResetBody)));
	if (!ResetResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("ClassDefaults reset failed: %s"), *ResetResult.Message));
	}
	TestTrue(TEXT("Omitted ClassDefaults property resets to parent baseline"), ResetResult.IsSuccess());
	WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	GeneratedCDO = WidgetBlueprint && WidgetBlueprint->GeneratedClass
		? Cast<UUserWidget>(WidgetBlueprint->GeneratedClass->GetDefaultObject(false))
		: nullptr;
	if (GeneratedCDO)
	{
		TestFalse(TEXT("bIsFocusable resets when omitted"), GeneratedCDO->IsFocusable());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintMetadataClassDefaultsParentChangeTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Metadata.ClassDefaultsParentChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintMetadataClassDefaultsParentChangeTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_MetadataClassDefaultsParent"));
	FAssetDocumentService Service;
	const FAssetDocumentResult InitialResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, MakeDefaultWidgetBlueprintBody())));
	TestTrue(TEXT("Initial default WidgetBlueprint apply succeeds"), InitialResult.IsSuccess());

	TSharedRef<FJsonObject> Body = MakeDefaultWidgetBlueprintBody();
	Body->SetObjectField(TEXT("ParentClass"), MakeClassRef(UTestUserWidget::StaticClass()->GetPathName()));
	Body->GetObjectField(TEXT("ClassDefaults"))->SetBoolField(TEXT("bTextEnabled"), false);

	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, Body)));
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Parent-change ClassDefaults apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("Parent change with new-parent ClassDefaults applies"), ApplyResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint loads"), WidgetBlueprint);
	TestEqual(TEXT("Parent class changed to TestUserWidget"), WidgetBlueprint ? WidgetBlueprint->ParentClass.Get() : nullptr, UTestUserWidget::StaticClass());
	const UTestUserWidget* GeneratedCDO = WidgetBlueprint && WidgetBlueprint->GeneratedClass
		? Cast<UTestUserWidget>(WidgetBlueprint->GeneratedClass->GetDefaultObject(false))
		: nullptr;
	TestNotNull(TEXT("Generated CDO uses TestUserWidget parent"), GeneratedCDO);
	if (GeneratedCDO)
	{
		TestFalse(TEXT("new-parent class default is applied"), GeneratedCDO->bTextEnabled);
	}

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = MakeWidgetBlueprintDocument(Target, Body);
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("Diff succeeds after parent-change ClassDefaults apply"), DiffResult.IsSuccess());
	TestTrue(TEXT("Parent-change ClassDefaults diff is unchanged"), DiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintMetadataPaletteCategoryRoundTripTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Metadata.PaletteCategoryRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintMetadataPaletteCategoryRoundTripTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_MetadataPalette"));
	TSharedRef<FJsonObject> Body = MakeDefaultWidgetBlueprintBody();
	Body->GetObjectField(TEXT("Palette"))->SetStringField(TEXT("Category"), TEXT("AssetDoc Metadata"));

	FAssetDocumentService Service;
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, Body)));
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Palette apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("Palette category apply succeeds"), ApplyResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint loads"), WidgetBlueprint);
	if (WidgetBlueprint)
	{
		TestEqual(TEXT("PaletteCategory mirror is updated"), WidgetBlueprint->PaletteCategory, FString(TEXT("AssetDoc Metadata")));
	}
	UUserWidget* GeneratedCDO = WidgetBlueprint && WidgetBlueprint->GeneratedClass
		? Cast<UUserWidget>(WidgetBlueprint->GeneratedClass->GetDefaultObject(false))
		: nullptr;
	if (GeneratedCDO)
	{
		TestEqual(TEXT("Generated CDO palette source is updated"), GeneratedCDO->GetPaletteCategory().ToString(), FString(TEXT("AssetDoc Metadata")));
	}

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds after Palette apply"), ExtractResult.IsSuccess());
	TSharedPtr<FJsonObject> ExtractedBody = GetExtractedBody(ExtractResult);
	const TSharedPtr<FJsonObject>* ExtractedPalette = nullptr;
	TestTrue(TEXT("Extract includes Palette"), ExtractedBody.IsValid() && ExtractedBody->TryGetObjectField(TEXT("Palette"), ExtractedPalette));
	if (ExtractedPalette && ExtractedPalette->IsValid())
	{
		TestEqual(TEXT("Palette category roundtrips"), (*ExtractedPalette)->GetStringField(TEXT("Category")), FString(TEXT("AssetDoc Metadata")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintMetadataPaletteEditorOptionsResetTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Metadata.PaletteEditorOptionsReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintMetadataPaletteEditorOptionsResetTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_MetadataPaletteEditorReset"));
	TSharedRef<FJsonObject> SetBody = MakeDefaultWidgetBlueprintBody();
	SetBody->GetObjectField(TEXT("Palette"))->SetStringField(TEXT("Category"), TEXT("Transient Category"));
	SetBody->GetObjectField(TEXT("EditorOptions"))->SetBoolField(TEXT("bCanCallInitializedWithoutPlayerContext"), true);

	FAssetDocumentService Service;
	const FAssetDocumentResult SetResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, SetBody)));
	TestTrue(TEXT("Initial Palette/EditorOptions apply succeeds"), SetResult.IsSuccess());

	TSharedRef<FJsonObject> ResetBody = MakeDefaultWidgetBlueprintBody();
	const FAssetDocumentResult ResetResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, ResetBody)));
	if (!ResetResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Palette/EditorOptions reset failed: %s"), *ResetResult.Message));
	}
	TestTrue(TEXT("Empty Palette/EditorOptions reset applies"), ResetResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint loads"), WidgetBlueprint);
	if (WidgetBlueprint)
	{
		TestTrue(TEXT("PaletteCategory clears on empty Palette"), WidgetBlueprint->PaletteCategory.IsEmpty());
		TestFalse(TEXT("Editor option resets false on empty EditorOptions"), WidgetBlueprint->bCanCallInitializedWithoutPlayerContext);
	}

	UUserWidget* GeneratedCDO = WidgetBlueprint && WidgetBlueprint->GeneratedClass
		? Cast<UUserWidget>(WidgetBlueprint->GeneratedClass->GetDefaultObject(false))
		: nullptr;
	if (GeneratedCDO)
	{
		TestTrue(TEXT("Generated CDO palette clears"), GeneratedCDO->GetPaletteCategory().IsEmpty());
	}
	const UWidgetBlueprintGeneratedClass* GeneratedClass = WidgetBlueprint
		? Cast<UWidgetBlueprintGeneratedClass>(WidgetBlueprint->GeneratedClass)
		: nullptr;
	if (GeneratedClass)
	{
		TestFalse(TEXT("Generated class editor option resets"), GeneratedClass->bCanCallInitializedWithoutPlayerContext);
	}

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = MakeWidgetBlueprintDocument(Target, ResetBody);
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("Diff succeeds after Palette/EditorOptions reset"), DiffResult.IsSuccess());
	TestTrue(TEXT("Reset Palette/EditorOptions diff is unchanged"), DiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintMetadataRejectsInvalidPaletteEditorOptionsTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Metadata.RejectsInvalidPaletteEditorOptions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintMetadataRejectsInvalidPaletteEditorOptionsTest::RunTest(const FString&)
{
	FAssetDocumentService Service;

	TSharedRef<FJsonObject> PaletteUnknownBody = MakeDefaultWidgetBlueprintBody();
	PaletteUnknownBody->GetObjectField(TEXT("Palette"))->SetStringField(TEXT("Unexpected"), TEXT("value"));
	const FAssetDocumentResult PaletteUnknownResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(
		MakeUniqueWidgetBlueprintTarget(TEXT("WBP_MetadataPaletteUnknown")),
		PaletteUnknownBody)));
	TestFalse(TEXT("Unknown Palette field rejects apply"), PaletteUnknownResult.IsSuccess());
	TestTrue(TEXT("Unknown Palette diagnostic is reported"), ResultHasDiagnosticCode(PaletteUnknownResult, TEXT("UnknownPaletteField")));

	TSharedRef<FJsonObject> PaletteTypeBody = MakeDefaultWidgetBlueprintBody();
	PaletteTypeBody->GetObjectField(TEXT("Palette"))->SetBoolField(TEXT("Category"), true);
	const FAssetDocumentResult PaletteTypeResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(
		MakeUniqueWidgetBlueprintTarget(TEXT("WBP_MetadataPaletteType")),
		PaletteTypeBody)));
	TestFalse(TEXT("Non-string Palette.Category rejects apply"), PaletteTypeResult.IsSuccess());
	TestTrue(TEXT("Invalid Palette.Category diagnostic is reported"), ResultHasDiagnosticCode(PaletteTypeResult, TEXT("InvalidPaletteCategory")));

	TSharedRef<FJsonObject> EditorTypeBody = MakeDefaultWidgetBlueprintBody();
	EditorTypeBody->GetObjectField(TEXT("EditorOptions"))->SetStringField(TEXT("bCanCallInitializedWithoutPlayerContext"), TEXT("true"));
	const FAssetDocumentResult EditorTypeResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(
		MakeUniqueWidgetBlueprintTarget(TEXT("WBP_MetadataEditorType")),
		EditorTypeBody)));
	TestFalse(TEXT("Non-bool EditorOptions flag rejects apply"), EditorTypeResult.IsSuccess());
	TestTrue(TEXT("Invalid EditorOptions diagnostic is reported"), ResultHasDiagnosticCode(EditorTypeResult, TEXT("InvalidEditorOption")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintMetadataEditorOptionsRoundTripTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Metadata.EditorOptionsRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintMetadataEditorOptionsRoundTripTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_MetadataEditorOptions"));
	TSharedRef<FJsonObject> Body = MakeDefaultWidgetBlueprintBody();
	Body->GetObjectField(TEXT("EditorOptions"))->SetBoolField(TEXT("bCanCallInitializedWithoutPlayerContext"), true);

	FAssetDocumentService Service;
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, Body)));
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("EditorOptions apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("EditorOptions apply succeeds"), ApplyResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint loads"), WidgetBlueprint);
	if (WidgetBlueprint)
	{
		TestTrue(TEXT("Editor option is applied to UWidgetBlueprint"), WidgetBlueprint->bCanCallInitializedWithoutPlayerContext);
	}
	UWidgetBlueprintGeneratedClass* GeneratedClass = WidgetBlueprint
		? Cast<UWidgetBlueprintGeneratedClass>(WidgetBlueprint->GeneratedClass)
		: nullptr;
	if (GeneratedClass)
	{
		TestTrue(TEXT("Editor option compiles to generated class"), GeneratedClass->bCanCallInitializedWithoutPlayerContext);
	}

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds after EditorOptions apply"), ExtractResult.IsSuccess());
	TSharedPtr<FJsonObject> ExtractedBody = GetExtractedBody(ExtractResult);
	const TSharedPtr<FJsonObject>* ExtractedEditorOptions = nullptr;
	TestTrue(TEXT("Extract includes EditorOptions"), ExtractedBody.IsValid() && ExtractedBody->TryGetObjectField(TEXT("EditorOptions"), ExtractedEditorOptions));
	if (ExtractedEditorOptions && ExtractedEditorOptions->IsValid())
	{
		TestTrue(TEXT("Editor option roundtrips"), (*ExtractedEditorOptions)->GetBoolField(TEXT("bCanCallInitializedWithoutPlayerContext")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintMetadataWidgetVariableGuidCanonicalizesTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Metadata.WidgetVariableGuidCanonicalizes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintMetadataWidgetVariableGuidCanonicalizesTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_MetadataWidgetVariableGuids"));
	TSharedRef<FJsonObject> Root = MakeWidgetNode(TEXT("RootCanvas"), TEXT("/Script/UMG.CanvasPanel"));
	TSharedRef<FJsonObject> TitleText = MakeWidgetNode(TEXT("TitleText"), TEXT("/Script/UMG.TextBlock"));
	TitleText->SetBoolField(TEXT("IsVariable"), true);
	TitleText->SetStringField(TEXT("VariableName"), TEXT("TitleText"));
	TArray<TSharedPtr<FJsonValue>> Children;
	Children.Add(MakeShared<FJsonValueObject>(TitleText));
	Root->SetArrayField(TEXT("Children"), Children);

	TSharedRef<FJsonObject> Body = MakeWidgetTreeBody(MakeWidgetTree(Root));
	Body->RemoveField(TEXT("WidgetVariableGuids"));

	FAssetDocumentService Service;
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, Body)));
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("WidgetVariableGuids fixture apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("Variable WidgetTree apply succeeds with omitted WidgetVariableGuids"), ApplyResult.IsSuccess());

	const FGuid ExpectedGuid = FGuid::NewDeterministicGuid(FString::Printf(TEXT("%s|WidgetVariableGuids|%s"), *Target, TEXT("TitleText")));
	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint loads"), WidgetBlueprint);
	if (WidgetBlueprint)
	{
		TestEqual(TEXT("Deterministic widget variable GUID is written to UE map"), WidgetBlueprint->WidgetVariableNameToGuidMap.FindRef(TEXT("TitleText")), ExpectedGuid);
	}

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds after GUID canonicalization"), ExtractResult.IsSuccess());
	TSharedPtr<FJsonObject> ExtractedBody = GetExtractedBody(ExtractResult);
	const TSharedPtr<FJsonObject>* ExtractedGuids = nullptr;
	TestTrue(TEXT("Extract includes WidgetVariableGuids"), ExtractedBody.IsValid() && ExtractedBody->TryGetObjectField(TEXT("WidgetVariableGuids"), ExtractedGuids));
	if (ExtractedGuids && ExtractedGuids->IsValid())
	{
		TestEqual(TEXT("Extracted GUID is deterministic"), (*ExtractedGuids)->GetStringField(TEXT("TitleText")), ExpectedGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintMetadataRejectsUnknownWidgetVariableGuidTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Metadata.RejectsUnknownWidgetVariableGuid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintMetadataRejectsUnknownWidgetVariableGuidTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_MetadataUnknownWidgetGuid"));
	TSharedRef<FJsonObject> Root = MakeWidgetNode(TEXT("RootCanvas"), TEXT("/Script/UMG.CanvasPanel"));
	TSharedRef<FJsonObject> TitleText = MakeWidgetNode(TEXT("TitleText"), TEXT("/Script/UMG.TextBlock"));
	TitleText->SetBoolField(TEXT("IsVariable"), true);
	TitleText->SetStringField(TEXT("VariableName"), TEXT("TitleText"));
	TArray<TSharedPtr<FJsonValue>> Children;
	Children.Add(MakeShared<FJsonValueObject>(TitleText));
	Root->SetArrayField(TEXT("Children"), Children);

	TSharedRef<FJsonObject> Body = MakeWidgetTreeBody(MakeWidgetTree(Root));
	Body->GetObjectField(TEXT("WidgetVariableGuids"))->SetStringField(
		TEXT("TitleTypo"),
		FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));

	FAssetDocumentService Service;
	const FAssetDocumentResult Result = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, Body)));
	TestFalse(TEXT("Unknown WidgetVariableGuids key rejects apply"), Result.IsSuccess());
	TestTrue(TEXT("Unknown WidgetVariableGuids diagnostic is reported"), ResultHasDiagnosticCode(Result, TEXT("UnknownWidgetVariableGuid")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintMetadataRejectsVariableWidgetNameConflictTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Metadata.RejectsVariableWidgetNameConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintMetadataRejectsVariableWidgetNameConflictTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_MetadataVariableConflict"));
	TSharedRef<FJsonObject> Root = MakeWidgetNode(TEXT("TitleText"), TEXT("/Script/UMG.TextBlock"));
	Root->SetBoolField(TEXT("IsVariable"), true);
	Root->SetStringField(TEXT("VariableName"), TEXT("TitleText"));

	TSharedRef<FJsonObject> Body = MakeWidgetTreeBody(MakeWidgetTree(Root));
	TSharedPtr<FJsonObject> Variable = MakeShared<FJsonObject>();
	Variable->SetStringField(TEXT("Name"), TEXT("TitleText"));
	Variable->SetObjectField(TEXT("Type"), MakeFloatPinType());
	Variable->SetStringField(TEXT("DefaultValue"), TEXT("1.0"));
	Body->SetArrayField(TEXT("Variables"), {MakeShared<FJsonValueObject>(Variable)});

	FAssetDocumentService Service;
	const FAssetDocumentResult Result = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, Body)));
	TestFalse(TEXT("Explicit variable conflicting with variable widget rejects apply"), Result.IsSuccess());
	TestTrue(TEXT("Conflict diagnostic is reported"), ResultHasDiagnosticCode(Result, TEXT("VariableWidgetNameConflict")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintMetadataVariableRoundTripTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Metadata.VariableRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintMetadataVariableRoundTripTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_MetadataVariableRoundTrip"));
	TSharedRef<FJsonObject> Body = MakeDefaultWidgetBlueprintBody();
	Body->SetArrayField(
		TEXT("Variables"),
		MakeVariableArray({MakeFloatVariable(TEXT("Health"), TEXT("100.0"), TEXT("Stats"), TEXT("Hit points"))}));

	FAssetDocumentService Service;
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, Body)));
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Variable apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("Non-widget Blueprint variable apply succeeds"), ApplyResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint loads"), WidgetBlueprint);
	if (WidgetBlueprint)
	{
		const FBPVariableDescription* Health = WidgetBlueprint->NewVariables.FindByPredicate([](const FBPVariableDescription& Variable)
		{
			return Variable.VarName == TEXT("Health");
		});
		TestNotNull(TEXT("Health variable exists"), Health);
		if (Health)
		{
			TestEqual(TEXT("Health category is applied"), Health->Category.ToString(), FString(TEXT("Stats")));
			TestTrue(TEXT("Health tooltip is applied"), Health->HasMetaData(FBlueprintMetadata::MD_Tooltip));
			TestEqual(TEXT("Health default value is persisted"), Health->DefaultValue, FString(TEXT("100.0")));
		}
	}

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds after variable apply"), ExtractResult.IsSuccess());
	TSharedPtr<FJsonObject> ExtractedBody = GetExtractedBody(ExtractResult);
	const TArray<TSharedPtr<FJsonValue>>* ExtractedVariables = nullptr;
	TestTrue(TEXT("Extract includes Variables"), ExtractedBody.IsValid() && ExtractedBody->TryGetArrayField(TEXT("Variables"), ExtractedVariables));
	TestEqual(TEXT("Extracted Variables has one entry"), ExtractedVariables ? ExtractedVariables->Num() : -1, 1);
	if (ExtractedVariables && ExtractedVariables->Num() == 1 && (*ExtractedVariables)[0].IsValid() && (*ExtractedVariables)[0]->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Variable = (*ExtractedVariables)[0]->AsObject();
		TestEqual(TEXT("Extracted variable name"), Variable->GetStringField(TEXT("Name")), FString(TEXT("Health")));
		TestEqual(TEXT("Extracted variable category"), Variable->GetStringField(TEXT("Category")), FString(TEXT("Stats")));
		TestEqual(TEXT("Extracted variable tooltip"), Variable->GetStringField(TEXT("Tooltip")), FString(TEXT("Hit points")));
	}

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = MakeWidgetBlueprintDocument(Target, Body);
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("Diff succeeds after variable roundtrip"), DiffResult.IsSuccess());
	TestTrue(TEXT("Variable roundtrip diff is unchanged"), DiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintBindingsFunctionRoundTripTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Bindings.FunctionRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintBindingsFunctionRoundTripTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_BindingsFunctionRoundTrip"));
	TSharedRef<FJsonObject> Body = MakeBindingFixtureBody();
	SetBindings(Body, {MakeFunctionBinding(TEXT("TitleText"), TEXT("Text"), TEXT("GetDisplayText"))});

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = MakeWidgetBlueprintDocument(Target, Body);
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyFileRequest(Document));
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Function binding apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("Function binding apply succeeds"), ApplyResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint loads"), WidgetBlueprint);
	if (WidgetBlueprint)
	{
		TestEqual(TEXT("One UE binding is materialized"), WidgetBlueprint->Bindings.Num(), 1);
		if (WidgetBlueprint->Bindings.Num() == 1)
		{
			const FDelegateEditorBinding& Binding = WidgetBlueprint->Bindings[0];
			TestEqual(TEXT("Binding object name is canonical"), Binding.ObjectName, FString(TEXT("TitleText")));
			TestEqual(TEXT("Binding target property is Text"), Binding.PropertyName, FName(TEXT("Text")));
			TestEqual(TEXT("Binding kind is Function"), Binding.Kind, EBindingKind::Function);
			TestEqual(TEXT("Binding function is preserved"), Binding.FunctionName, FName(TEXT("GetDisplayText")));
		}

		const UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WidgetBlueprint->GeneratedClass);
		TestNotNull(TEXT("Runtime generated class exists"), GeneratedClass);
		if (GeneratedClass)
		{
			TestEqual(TEXT("One runtime binding is emitted"), GeneratedClass->Bindings.Num(), 1);
			if (GeneratedClass->Bindings.Num() == 1)
			{
				const FDelegateRuntimeBinding& RuntimeBinding = GeneratedClass->Bindings[0];
				TestEqual(TEXT("Runtime binding object name"), RuntimeBinding.ObjectName, FString(TEXT("TitleText")));
				TestEqual(TEXT("Runtime binding target property"), RuntimeBinding.PropertyName, FName(TEXT("Text")));
				TestEqual(TEXT("Runtime binding kind is Function"), RuntimeBinding.Kind, EBindingKind::Function);
				TestEqual(TEXT("Runtime binding function"), RuntimeBinding.FunctionName, FName(TEXT("GetDisplayText")));
			}
		}
	}

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds after function binding apply"), ExtractResult.IsSuccess());
	const TArray<TSharedPtr<FJsonValue>> ExtractedBindings = GetExtractedBindings(ExtractResult);
	TestEqual(TEXT("Extract returns one binding"), ExtractedBindings.Num(), 1);
	if (ExtractedBindings.Num() == 1 && ExtractedBindings[0].IsValid() && ExtractedBindings[0]->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Binding = ExtractedBindings[0]->AsObject();
		TestEqual(TEXT("Extracted binding widget"), Binding->GetStringField(TEXT("Widget")), FString(TEXT("TitleText")));
		TestEqual(TEXT("Extracted binding property"), Binding->GetStringField(TEXT("Property")), FString(TEXT("Text")));
		TestEqual(TEXT("Extracted binding kind"), Binding->GetStringField(TEXT("Kind")), FString(TEXT("Function")));
		TestEqual(TEXT("Extracted binding function"), Binding->GetStringField(TEXT("Function")), FString(TEXT("GetDisplayText")));
	}

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = Document;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("Diff succeeds after function binding roundtrip"), DiffResult.IsSuccess());
	TestTrue(TEXT("Function binding diff is unchanged"), DiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintBindingsPropertyRoundTripTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Bindings.PropertyRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintBindingsPropertyRoundTripTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_BindingsPropertyRoundTrip"));
	TSharedRef<FJsonObject> Body = MakeBindingFixtureBody();
	SetBindings(Body, {MakePropertyBinding(TEXT("TitleText"), TEXT("Text"), {TEXT("DisplayText")})});

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = MakeWidgetBlueprintDocument(Target, Body);
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyFileRequest(Document));
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Property binding apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("Property binding apply succeeds"), ApplyResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint loads"), WidgetBlueprint);
	if (WidgetBlueprint)
	{
		TestEqual(TEXT("One UE property binding is materialized"), WidgetBlueprint->Bindings.Num(), 1);
		if (WidgetBlueprint->Bindings.Num() == 1)
		{
			const FDelegateEditorBinding& Binding = WidgetBlueprint->Bindings[0];
			TestEqual(TEXT("Property binding object name is canonical"), Binding.ObjectName, FString(TEXT("TitleText")));
			TestEqual(TEXT("Property binding target property is Text"), Binding.PropertyName, FName(TEXT("Text")));
			TestEqual(TEXT("Binding kind is Property"), Binding.Kind, EBindingKind::Property);
			TestFalse(TEXT("Property binding uses SourcePath"), Binding.SourcePath.IsEmpty());
		}

		const UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WidgetBlueprint->GeneratedClass);
		TestNotNull(TEXT("Runtime generated class exists"), GeneratedClass);
		if (GeneratedClass)
		{
			TestEqual(TEXT("One runtime property binding is emitted"), GeneratedClass->Bindings.Num(), 1);
			if (GeneratedClass->Bindings.Num() == 1)
			{
				const FDelegateRuntimeBinding& RuntimeBinding = GeneratedClass->Bindings[0];
				TestEqual(TEXT("Runtime property binding object name"), RuntimeBinding.ObjectName, FString(TEXT("TitleText")));
				TestEqual(TEXT("Runtime property binding target property"), RuntimeBinding.PropertyName, FName(TEXT("Text")));
				TestEqual(TEXT("Runtime binding kind is Property"), RuntimeBinding.Kind, EBindingKind::Property);
				TestTrue(TEXT("Runtime property binding source path is valid"), RuntimeBinding.SourcePath.IsValid());
			}
		}
	}

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds after property binding apply"), ExtractResult.IsSuccess());
	const TArray<TSharedPtr<FJsonValue>> ExtractedBindings = GetExtractedBindings(ExtractResult);
	TestEqual(TEXT("Extract returns one property binding"), ExtractedBindings.Num(), 1);
	if (ExtractedBindings.Num() == 1 && ExtractedBindings[0].IsValid() && ExtractedBindings[0]->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Binding = ExtractedBindings[0]->AsObject();
		TestEqual(TEXT("Extracted property binding kind"), Binding->GetStringField(TEXT("Kind")), FString(TEXT("Property")));
		const TArray<TSharedPtr<FJsonValue>>* SourcePath = nullptr;
		TestTrue(TEXT("Extracted property binding prefers SourcePath"), Binding->TryGetArrayField(TEXT("SourcePath"), SourcePath) && SourcePath && SourcePath->Num() == 1);
		if (SourcePath && SourcePath->Num() == 1)
		{
			TestEqual(TEXT("Extracted SourcePath segment"), (*SourcePath)[0]->AsString(), FString(TEXT("DisplayText")));
		}
		TestFalse(TEXT("Extracted property binding omits legacy Property"), Binding->HasField(TEXT("SourceProperty")));
	}

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = Document;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("Diff succeeds after property binding roundtrip"), DiffResult.IsSuccess());
	TestTrue(TEXT("Property binding diff is unchanged"), DiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintBindingsOmissionRemovesBindingTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Bindings.OmissionRemovesBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintBindingsOmissionRemovesBindingTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_BindingsOmissionRemoves"));
	FAssetDocumentService Service;

	TSharedRef<FJsonObject> InitialBody = MakeBindingFixtureBody();
	SetBindings(InitialBody, {MakeFunctionBinding(TEXT("TitleText"), TEXT("Text"), TEXT("GetDisplayText"))});
	const FAssetDocumentResult InitialResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, InitialBody)));
	TestTrue(TEXT("Initial binding apply succeeds"), InitialResult.IsSuccess());

	TSharedRef<FJsonObject> ResetBody = MakeBindingFixtureBody();
	const FAssetDocumentResult ResetResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, ResetBody)));
	if (!ResetResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Binding omission reset failed: %s"), *ResetResult.Message));
	}
	TestTrue(TEXT("Omitted Bindings reset applies"), ResetResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint loads"), WidgetBlueprint);
	if (WidgetBlueprint)
	{
		TestEqual(TEXT("Omitted Bindings removes existing UE binding"), WidgetBlueprint->Bindings.Num(), 0);
	}

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = MakeWidgetBlueprintDocument(Target, ResetBody);
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("Diff succeeds after binding omission"), DiffResult.IsSuccess());
	TestTrue(TEXT("Omitted binding diff is unchanged"), DiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintBindingsRejectsMissingWidgetTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Bindings.RejectsMissingWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintBindingsRejectsMissingWidgetTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_BindingsRejectsMissingWidget"));
	TSharedRef<FJsonObject> Body = MakeBindingFixtureBody();
	SetBindings(Body, {MakeFunctionBinding(TEXT("MissingText"), TEXT("Text"), TEXT("GetDisplayText"))});

	FAssetDocumentService Service;
	const FAssetDocumentResult Result = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, Body)));
	TestFalse(TEXT("Missing binding widget rejects apply"), Result.IsSuccess());
	TestTrue(TEXT("Missing binding widget diagnostic is reported"), ResultHasDiagnosticCode(Result, TEXT("MissingBindingWidget")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintBindingsRejectsInvalidFunctionSignatureTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Bindings.RejectsInvalidFunctionSignature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintBindingsRejectsInvalidFunctionSignatureTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_BindingsRejectsInvalidFunction"));
	TSharedRef<FJsonObject> Body = MakeBindingFixtureBody();
	SetBindings(Body, {MakeFunctionBinding(TEXT("TitleText"), TEXT("Text"), TEXT("GetObjectForText"))});

	FAssetDocumentService Service;
	const FAssetDocumentResult Result = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, Body)));
	TestFalse(TEXT("Invalid binding function signature rejects apply"), Result.IsSuccess());
	TestTrue(TEXT("Invalid binding function diagnostic is reported"), ResultHasDiagnosticCode(Result, TEXT("InvalidBindingFunctionSignature")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintBindingsRejectsDuplicateTargetTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Bindings.RejectsDuplicateTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintBindingsRejectsDuplicateTargetTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_BindingsRejectsDuplicate"));
	TSharedRef<FJsonObject> Body = MakeBindingFixtureBody();
	SetBindings(Body, {
		MakeFunctionBinding(TEXT("TitleText"), TEXT("Text"), TEXT("GetDisplayText")),
		MakePropertyBinding(TEXT("TitleText"), TEXT("Text"), {TEXT("DisplayText")})
	});

	FAssetDocumentService Service;
	const FAssetDocumentResult Result = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, Body)));
	TestFalse(TEXT("Duplicate binding target rejects apply"), Result.IsSuccess());
	TestTrue(TEXT("Duplicate binding target diagnostic is reported"), ResultHasDiagnosticCode(Result, TEXT("DuplicateBindingTarget")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintBindingsInvalidPreflightPreservesExistingAssetTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Bindings.InvalidPreflightPreservesExistingAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintBindingsInvalidPreflightPreservesExistingAssetTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_BindingsInvalidPreflightPreserves"));
	FAssetDocumentService Service;

	TSharedRef<FJsonObject> InitialBody = MakeBindingFixtureBody();
	InitialBody->GetObjectField(TEXT("ClassDefaults"))->SetBoolField(TEXT("bIsFocusable"), true);
	InitialBody->GetObjectField(TEXT("WidgetTree"))->GetObjectField(TEXT("RootWidget"))->GetObjectField(TEXT("Properties"))->SetStringField(TEXT("Text"), TEXT("Original title"));
	SetBindings(InitialBody, {MakeFunctionBinding(TEXT("TitleText"), TEXT("Text"), TEXT("GetDisplayText"))});
	const FAssetDocumentResult InitialResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, InitialBody)));
	TestTrue(TEXT("Initial valid binding fixture applies"), InitialResult.IsSuccess());

	UWidgetBlueprint* InitialBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("Initial WidgetBlueprint loads"), InitialBlueprint);
	if (!InitialBlueprint)
	{
		return false;
	}

	bool bInitialFocusable = false;
	TestTrue(TEXT("Initial bIsFocusable can be read"), GetGeneratedBoolDefault(InitialBlueprint, TEXT("bIsFocusable"), bInitialFocusable));
	TestTrue(TEXT("Initial bIsFocusable is true"), bInitialFocusable);

	UTextBlock* InitialTitleText = InitialBlueprint->WidgetTree ? Cast<UTextBlock>(InitialBlueprint->WidgetTree->FindWidget(TEXT("TitleText"))) : nullptr;
	TestNotNull(TEXT("Initial TitleText exists"), InitialTitleText);
	TestEqual(TEXT("Initial TitleText text"), InitialTitleText ? InitialTitleText->GetText().ToString() : FString(), FString(TEXT("Original title")));

	TSharedRef<FJsonObject> InvalidBody = MakeBindingFixtureBody();
	InvalidBody->GetObjectField(TEXT("ClassDefaults"))->SetBoolField(TEXT("bIsFocusable"), false);
	InvalidBody->GetObjectField(TEXT("WidgetTree"))->GetObjectField(TEXT("RootWidget"))->GetObjectField(TEXT("Properties"))->SetStringField(TEXT("Text"), TEXT("Mutated title"));
	SetBindings(InvalidBody, {MakeFunctionBinding(TEXT("MissingText"), TEXT("Text"), TEXT("GetDisplayText"))});
	const FAssetDocumentResult InvalidResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, InvalidBody)));
	TestFalse(TEXT("Invalid binding rejects apply"), InvalidResult.IsSuccess());
	TestTrue(TEXT("Invalid binding reports missing widget"), ResultHasDiagnosticCode(InvalidResult, TEXT("MissingBindingWidget")));
	TestTrue(TEXT("Invalid binding diagnostic points at widget field"), ResultHasDiagnosticPath(InvalidResult, TEXT("/Body/Bindings/0/Widget")));

	UWidgetBlueprint* AfterFailureBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint still loads after failed apply"), AfterFailureBlueprint);
	if (AfterFailureBlueprint)
	{
		bool bAfterFailureFocusable = false;
		TestTrue(TEXT("After-failure bIsFocusable can be read"), GetGeneratedBoolDefault(AfterFailureBlueprint, TEXT("bIsFocusable"), bAfterFailureFocusable));
		TestTrue(TEXT("After-failure bIsFocusable remains true"), bAfterFailureFocusable);

		UTextBlock* AfterFailureTitleText = AfterFailureBlueprint->WidgetTree ? Cast<UTextBlock>(AfterFailureBlueprint->WidgetTree->FindWidget(TEXT("TitleText"))) : nullptr;
		TestNotNull(TEXT("After-failure TitleText still exists"), AfterFailureTitleText);
		TestEqual(TEXT("After-failure TitleText text remains unchanged"), AfterFailureTitleText ? AfterFailureTitleText->GetText().ToString() : FString(), FString(TEXT("Original title")));
		TestEqual(TEXT("After-failure binding remains original"), AfterFailureBlueprint->Bindings.Num(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintBindingsRejectsMismatchedMemberGuidTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Bindings.RejectsMismatchedMemberGuid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintBindingsRejectsMismatchedMemberGuidTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_BindingsRejectsMismatchedGuid"));
	TSharedRef<FJsonObject> Body = MakeBindingFixtureBody();
	TSharedPtr<FJsonObject> Binding = MakeFunctionBinding(TEXT("TitleText"), TEXT("Text"), TEXT("GetDisplayText"));
	Binding->SetStringField(TEXT("MemberGuid"), TEXT("11111111-2222-3333-4444-555555555555"));
	SetBindings(Body, {Binding});

	FAssetDocumentService Service;
	const FAssetDocumentResult Result = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, Body)));
	TestFalse(TEXT("Mismatched function MemberGuid rejects apply"), Result.IsSuccess());
	TestTrue(TEXT("Mismatched function MemberGuid diagnostic is reported"), ResultHasDiagnosticCode(Result, TEXT("MismatchedBindingMemberGuid")));
	TestTrue(TEXT("Mismatched function MemberGuid diagnostic path is precise"), ResultHasDiagnosticPath(Result, TEXT("/Body/Bindings/0/MemberGuid")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintGraphsFunctionGraphForBindingRoundTripTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Graphs.FunctionGraphForBindingRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintGraphsFunctionGraphForBindingRoundTripTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_GraphsFunctionBindingRoundTrip"));
	TSharedRef<FJsonObject> Body = MakeBindingFixtureBody();
	SetGraphRegion(Body, TEXT("FunctionGraphs"), {
		MakeGraph(
			TEXT("GetTitleFromAuthoredGraph"),
			TEXT("/Script/BlueprintGraph.EdGraphSchema_K2"),
			{MakeGraphNode(TEXT("Self"), TEXT("/Script/BlueprintGraph.K2Node_Self"))})
	});
	SetBindings(Body, {MakeFunctionBinding(TEXT("TitleText"), TEXT("Text"), TEXT("GetTitleFromAuthoredGraph"))});

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = MakeWidgetBlueprintDocument(Target, Body);
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyFileRequest(Document));
	TestFalse(TEXT("Binding to authored graph without supported signature rejects apply"), ApplyResult.IsSuccess());
	TestTrue(TEXT("Unsupported authored binding function reports signature diagnostic"), ResultHasDiagnosticCode(ApplyResult, TEXT("InvalidBindingFunctionSignature")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintGraphsEventGraphRoundTripTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Graphs.EventGraphRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintGraphsEventGraphRoundTripTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_GraphsEventGraphRoundTrip"));
	TSharedRef<FJsonObject> Body = MakeBindingFixtureBody();
	SetGraphRegion(Body, TEXT("UbergraphPages"), {
		MakeGraph(
			TEXT("EventGraph"),
			TEXT("/Script/UMGEditor.WidgetGraphSchema"),
			{
				MakeGraphNode(TEXT("Self"), TEXT("/Script/BlueprintGraph.K2Node_Self")),
				MakeGraphNode(
					TEXT("GetDisplayText"),
					TEXT("/Script/BlueprintGraph.K2Node_CallFunction"),
					MakeGraphMemberRef(TEXT("Self"), TEXT("GetDisplayText")))
			})
	});

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = MakeWidgetBlueprintDocument(Target, Body);
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyFileRequest(Document));
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("EventGraph apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("Widget EventGraph applies"), ApplyResult.IsSuccess());

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds after EventGraph apply"), ExtractResult.IsSuccess());
	const TSharedPtr<FJsonObject> EventGraph = FindExtractedGraph(ExtractResult, TEXT("UbergraphPages"), TEXT("EventGraph"));
	TestTrue(TEXT("EventGraph extracts"), EventGraph.IsValid());
	TestTrue(TEXT("Self node extracts"), ExtractedGraphHasNodeClass(EventGraph, TEXT("/Script/BlueprintGraph.K2Node_Self")));
	TestTrue(TEXT("CallFunction node extracts"), ExtractedGraphHasNodeClass(EventGraph, TEXT("/Script/BlueprintGraph.K2Node_CallFunction")));

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = Document;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("Diff succeeds after EventGraph roundtrip"), DiffResult.IsSuccess());
	TestTrue(TEXT("EventGraph roundtrip diff is unchanged"), DiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));

	TSharedRef<FJsonObject> ChangedBody = MakeBindingFixtureBody();
	SetGraphRegion(ChangedBody, TEXT("UbergraphPages"), {
		MakeGraph(
			TEXT("EventGraph"),
			TEXT("/Script/UMGEditor.WidgetGraphSchema"),
			{MakeGraphNode(TEXT("Self"), TEXT("/Script/BlueprintGraph.K2Node_Self"))})
	});
	FAssetDocumentDiffRequest ChangedDiffRequest;
	ChangedDiffRequest.Document = MakeWidgetBlueprintDocument(Target, ChangedBody);
	const FAssetDocumentResult ChangedDiffResult = Service.Diff(ChangedDiffRequest);
	TestTrue(TEXT("Changed EventGraph diff succeeds"), ChangedDiffResult.IsSuccess());
	TestTrue(TEXT("Changed EventGraph reports semantic diff"), DiffPayloadHasChangedEntries(ChangedDiffResult.Payload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintGraphsMacroGraphRoundTripTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Graphs.MacroGraphRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintGraphsMacroGraphRoundTripTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_GraphsMacroGraphRoundTrip"));
	TSharedRef<FJsonObject> Body = MakeBindingFixtureBody();
	SetGraphRegion(Body, TEXT("MacroGraphs"), {
		MakeGraph(
			TEXT("FormatTitleMacro"),
			TEXT("/Script/BlueprintGraph.EdGraphSchema_K2"),
			{MakeGraphNode(TEXT("Self"), TEXT("/Script/BlueprintGraph.K2Node_Self"))})
	});

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = MakeWidgetBlueprintDocument(Target, Body);
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyFileRequest(Document));
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("MacroGraph apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("Widget MacroGraph applies"), ApplyResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	UEdGraph* MacroGraph = FindWidgetBlueprintGraphByName(WidgetBlueprint, TEXT("FormatTitleMacro"));
	TestNotNull(TEXT("MacroGraph exists in WidgetBlueprint"), MacroGraph);
	TestTrue(TEXT("MacroGraph keeps UE macro tunnel framework nodes"), GraphHasConcreteNodeClass(MacroGraph, UK2Node_Tunnel::StaticClass()));

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds after MacroGraph apply"), ExtractResult.IsSuccess());
	const TSharedPtr<FJsonObject> ExtractedMacroGraph = FindExtractedGraph(ExtractResult, TEXT("MacroGraphs"), TEXT("FormatTitleMacro"));
	TestTrue(TEXT("MacroGraph extracts"), ExtractedMacroGraph.IsValid());
	TestTrue(TEXT("MacroGraph self node extracts"), ExtractedGraphHasNodeClass(ExtractedMacroGraph, TEXT("/Script/BlueprintGraph.K2Node_Self")));

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = Document;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("Diff succeeds after MacroGraph roundtrip"), DiffResult.IsSuccess());
	TestTrue(TEXT("MacroGraph roundtrip diff is unchanged"), DiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));

	TSharedRef<FJsonObject> ChangedBody = MakeBindingFixtureBody();
	SetGraphRegion(ChangedBody, TEXT("MacroGraphs"), {
		MakeGraph(TEXT("FormatTitleMacro"), TEXT("/Script/BlueprintGraph.EdGraphSchema_K2"), {})
	});
	FAssetDocumentDiffRequest ChangedDiffRequest;
	ChangedDiffRequest.Document = MakeWidgetBlueprintDocument(Target, ChangedBody);
	const FAssetDocumentResult ChangedDiffResult = Service.Diff(ChangedDiffRequest);
	TestTrue(TEXT("Changed MacroGraph diff succeeds"), ChangedDiffResult.IsSuccess());
	TestTrue(TEXT("Changed MacroGraph reports semantic diff"), DiffPayloadHasChangedEntries(ChangedDiffResult.Payload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintGraphsDesiredVariableSelfMemberRoundTripTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Graphs.DesiredVariableSelfMemberRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintGraphsDesiredVariableSelfMemberRoundTripTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_GraphsDesiredVariableSelfMember"));
	TSharedRef<FJsonObject> Body = MakeBindingFixtureBody();
	Body->SetArrayField(TEXT("Variables"), MakeVariableArray({MakeFloatVariable(TEXT("Score"), TEXT("42.0"))}));
	SetGraphRegion(Body, TEXT("UbergraphPages"), {
		MakeGraph(
			TEXT("EventGraph"),
			TEXT("/Script/UMGEditor.WidgetGraphSchema"),
			{
				MakeGraphNode(
					TEXT("Get_Score"),
					TEXT("/Script/BlueprintGraph.K2Node_VariableGet"),
					MakeGraphMemberRef(TEXT("Self"), TEXT("Score"))),
				MakeGraphNode(
					TEXT("Set_Score"),
					TEXT("/Script/BlueprintGraph.K2Node_VariableSet"),
					MakeGraphMemberRef(TEXT("Self"), TEXT("Score")))
			})
	});

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = MakeWidgetBlueprintDocument(Target, Body);
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyFileRequest(Document));
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Desired variable graph apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("Graph nodes can reference desired Self variable"), ApplyResult.IsSuccess());

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds after desired variable graph apply"), ExtractResult.IsSuccess());
	const TSharedPtr<FJsonObject> EventGraph = FindExtractedGraph(ExtractResult, TEXT("UbergraphPages"), TEXT("EventGraph"));
	TestTrue(TEXT("VariableGet extracts"), ExtractedGraphHasNodeClass(EventGraph, TEXT("/Script/BlueprintGraph.K2Node_VariableGet")));
	TestTrue(TEXT("VariableSet extracts"), ExtractedGraphHasNodeClass(EventGraph, TEXT("/Script/BlueprintGraph.K2Node_VariableSet")));

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = Document;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("Diff succeeds after desired variable graph roundtrip"), DiffResult.IsSuccess());
	TestTrue(TEXT("Desired variable graph roundtrip diff is unchanged"), DiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintGraphsUnsupportedExistingNodePreflightPreservesBodyTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Graphs.UnsupportedExistingNodePreflightPreservesBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintGraphsUnsupportedExistingNodePreflightPreservesBodyTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_GraphsUnsupportedPreflightPreservesBody"));
	FAssetDocumentService Service;

	TSharedRef<FJsonObject> InitialTitle = MakeWidgetNode(TEXT("TitleText"), TEXT("/Script/UMG.TextBlock"));
	InitialTitle->GetObjectField(TEXT("Properties"))->SetStringField(TEXT("Text"), TEXT("Original title"));
	TSharedRef<FJsonObject> InitialBody = MakeWidgetTreeBody(MakeWidgetTree(InitialTitle));
	SetGraphRegion(InitialBody, TEXT("UbergraphPages"), {
		MakeGraph(
			TEXT("EventGraph"),
			TEXT("/Script/UMGEditor.WidgetGraphSchema"),
			{MakeGraphNode(TEXT("Self"), TEXT("/Script/BlueprintGraph.K2Node_Self"))})
	});
	const FAssetDocumentResult InitialResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, InitialBody)));
	TestTrue(TEXT("Initial graph/body fixture applies"), InitialResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	UEdGraph* EventGraph = FindWidgetBlueprintGraphByName(WidgetBlueprint, TEXT("EventGraph"));
	TestNotNull(TEXT("EventGraph exists before injecting unsupported node"), EventGraph);
	if (!EventGraph)
	{
		return false;
	}

	UK2Node_IfThenElse* Branch = NewObject<UK2Node_IfThenElse>(EventGraph, UK2Node_IfThenElse::StaticClass(), NAME_None, RF_Transactional);
	Branch->CreateNewGuid();
	Branch->NodePosX = 320;
	Branch->NodePosY = 0;
	EventGraph->AddNode(Branch, true, false);
	Branch->AllocateDefaultPins();
	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);

	TSharedRef<FJsonObject> ChangedTitle = MakeWidgetNode(TEXT("TitleText"), TEXT("/Script/UMG.TextBlock"));
	ChangedTitle->GetObjectField(TEXT("Properties"))->SetStringField(TEXT("Text"), TEXT("Mutated title"));
	TSharedRef<FJsonObject> InvalidBody = MakeWidgetTreeBody(MakeWidgetTree(ChangedTitle));
	const FAssetDocumentResult InvalidResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, InvalidBody)));
	TestFalse(TEXT("Unsupported existing graph node rejects apply during preflight"), InvalidResult.IsSuccess());
	TestTrue(TEXT("Unsupported existing graph node diagnostic is reported"), ResultHasDiagnosticCode(InvalidResult, TEXT("UnsupportedGraphNodeClass")));

	UWidgetBlueprint* AfterFailureBlueprint = LoadWidgetBlueprintForTarget(Target);
	UTextBlock* AfterFailureTitleText = AfterFailureBlueprint && AfterFailureBlueprint->WidgetTree
		? Cast<UTextBlock>(AfterFailureBlueprint->WidgetTree->FindWidget(TEXT("TitleText")))
		: nullptr;
	TestNotNull(TEXT("TitleText still exists after rejected graph preflight"), AfterFailureTitleText);
	TestEqual(TEXT("Body text remains unchanged after rejected graph preflight"), AfterFailureTitleText ? AfterFailureTitleText->GetText().ToString() : FString(), FString(TEXT("Original title")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintGraphsSameGraphResidualUnsupportedNodePreservesBodyTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Graphs.SameGraphResidualUnsupportedNodePreservesBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintGraphsSameGraphResidualUnsupportedNodePreservesBodyTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_GraphsSameGraphResidualPreservesBody"));
	FAssetDocumentService Service;

	TSharedRef<FJsonObject> InitialTitle = MakeWidgetNode(TEXT("TitleText"), TEXT("/Script/UMG.TextBlock"));
	InitialTitle->GetObjectField(TEXT("Properties"))->SetStringField(TEXT("Text"), TEXT("Original title"));
	TSharedRef<FJsonObject> InitialBody = MakeWidgetTreeBody(MakeWidgetTree(InitialTitle));
	SetGraphRegion(InitialBody, TEXT("UbergraphPages"), {
		MakeGraph(
			TEXT("EventGraph"),
			TEXT("/Script/UMGEditor.WidgetGraphSchema"),
			{MakeGraphNode(TEXT("Self"), TEXT("/Script/BlueprintGraph.K2Node_Self"))})
	});
	const FAssetDocumentResult InitialResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, InitialBody)));
	TestTrue(TEXT("Initial same-graph residual fixture applies"), InitialResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	UEdGraph* EventGraph = FindWidgetBlueprintGraphByName(WidgetBlueprint, TEXT("EventGraph"));
	TestNotNull(TEXT("EventGraph exists before injecting same-graph residual node"), EventGraph);
	if (!EventGraph)
	{
		return false;
	}

	UK2Node_IfThenElse* Branch = NewObject<UK2Node_IfThenElse>(EventGraph, UK2Node_IfThenElse::StaticClass(), NAME_None, RF_Transactional);
	Branch->CreateNewGuid();
	Branch->NodePosX = 640;
	Branch->NodePosY = 0;
	EventGraph->AddNode(Branch, true, false);
	Branch->AllocateDefaultPins();
	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);

	TSharedRef<FJsonObject> ChangedTitle = MakeWidgetNode(TEXT("TitleText"), TEXT("/Script/UMG.TextBlock"));
	ChangedTitle->GetObjectField(TEXT("Properties"))->SetStringField(TEXT("Text"), TEXT("Mutated title"));
	TSharedRef<FJsonObject> InvalidBody = MakeWidgetTreeBody(MakeWidgetTree(ChangedTitle));
	SetGraphRegion(InvalidBody, TEXT("UbergraphPages"), {
		MakeGraph(
			TEXT("EventGraph"),
			TEXT("/Script/UMGEditor.WidgetGraphSchema"),
			{MakeGraphNode(TEXT("Self"), TEXT("/Script/BlueprintGraph.K2Node_Self"))})
	});
	const FAssetDocumentResult InvalidResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, InvalidBody)));
	TestFalse(TEXT("Unsupported residual node in same graph rejects apply during preflight"), InvalidResult.IsSuccess());
	TestTrue(TEXT("Same-graph residual diagnostic is reported"), ResultHasDiagnosticCode(InvalidResult, TEXT("UnsupportedGraphNodeClass")));

	UWidgetBlueprint* AfterFailureBlueprint = LoadWidgetBlueprintForTarget(Target);
	UTextBlock* AfterFailureTitleText = AfterFailureBlueprint && AfterFailureBlueprint->WidgetTree
		? Cast<UTextBlock>(AfterFailureBlueprint->WidgetTree->FindWidget(TEXT("TitleText")))
		: nullptr;
	TestNotNull(TEXT("TitleText still exists after same-graph rejected preflight"), AfterFailureTitleText);
	TestEqual(TEXT("Body text remains unchanged after same-graph rejected preflight"), AfterFailureTitleText ? AfterFailureTitleText->GetText().ToString() : FString(), FString(TEXT("Original title")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintGraphsAnimationEventNodeRoundTripTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Graphs.AnimationEventNodeRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintGraphsAnimationEventNodeRoundTripTest::RunTest(const FString&)
{
	FWidgetBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UWidgetBlueprint::StaticClass();

	TSharedRef<FJsonObject> Body = MakeDefaultWidgetBlueprintBody();
	SetGraphRegion(Body, TEXT("UbergraphPages"), {
		MakeGraph(
			TEXT("EventGraph"),
			TEXT("/Script/UMGEditor.WidgetGraphSchema"),
			{MakeGraphNode(TEXT("IntroStarted"), TEXT("/Script/UMGEditor.K2Node_WidgetAnimationEvent"))})
	});

	const FAssetDocumentCapabilityResult Result = Capability.Validate(Context, MakeBodyJsonValue(Body));
	TestFalse(TEXT("Authored widget animation event is explicit unsupported in Task 5"), Result.bSuccess);
	TestTrue(TEXT("Unsupported widget animation event uses graph diagnostic"), Result.Diagnostics.ContainsByPredicate([](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Path == TEXT("/Body/UbergraphPages/0/Nodes/0")
			&& Diagnostic.Code == TEXT("UnsupportedGraphNodeClass");
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintGraphsUnsupportedNodeRejectsApplyTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Graphs.UnsupportedNodeRejectsApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintGraphsUnsupportedNodeRejectsApplyTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_GraphsUnsupportedRejectsApply"));
	FAssetDocumentService Service;

	TSharedRef<FJsonObject> InitialBody = MakeBindingFixtureBody();
	SetGraphRegion(InitialBody, TEXT("UbergraphPages"), {
		MakeGraph(
			TEXT("EventGraph"),
			TEXT("/Script/UMGEditor.WidgetGraphSchema"),
			{MakeGraphNode(TEXT("Self"), TEXT("/Script/BlueprintGraph.K2Node_Self"))})
	});
	const FAssetDocumentResult InitialResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, InitialBody)));
	TestTrue(TEXT("Initial supported graph apply succeeds"), InitialResult.IsSuccess());

	TSharedRef<FJsonObject> InvalidBody = MakeBindingFixtureBody();
	SetGraphRegion(InvalidBody, TEXT("UbergraphPages"), {
		MakeGraph(
			TEXT("EventGraph"),
			TEXT("/Script/UMGEditor.WidgetGraphSchema"),
			{MakeGraphNode(TEXT("Branch"), TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"))})
	});
	const FAssetDocumentResult InvalidResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, InvalidBody)));
	TestFalse(TEXT("Unsupported graph node rejects apply"), InvalidResult.IsSuccess());
	TestTrue(TEXT("Unsupported graph node diagnostic is reported"), ResultHasDiagnosticCode(InvalidResult, TEXT("UnsupportedGraphNodeClass")));

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	UEdGraph* EventGraph = FindWidgetBlueprintGraphByName(WidgetBlueprint, TEXT("EventGraph"));
	TestNotNull(TEXT("Existing EventGraph remains after rejected apply"), EventGraph);
	TestTrue(TEXT("Existing Self node remains after rejected apply"), EventGraph && EventGraph->Nodes.ContainsByPredicate([](UEdGraphNode* Node)
	{
		return Node && Node->GetClass()->GetPathName() == TEXT("/Script/BlueprintGraph.K2Node_Self");
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintGraphsExtractReportsUnsupportedNodesTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Graphs.ExtractReportsUnsupportedNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintGraphsExtractReportsUnsupportedNodesTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_GraphsExtractUnsupportedNodes"));
	FAssetDocumentService Service;

	TSharedRef<FJsonObject> Body = MakeBindingFixtureBody();
	SetGraphRegion(Body, TEXT("UbergraphPages"), {
		MakeGraph(
			TEXT("EventGraph"),
			TEXT("/Script/UMGEditor.WidgetGraphSchema"),
			{MakeGraphNode(TEXT("Self"), TEXT("/Script/BlueprintGraph.K2Node_Self"))})
	});
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, Body)));
	TestTrue(TEXT("Supported graph fixture applies"), ApplyResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	UEdGraph* EventGraph = FindWidgetBlueprintGraphByName(WidgetBlueprint, TEXT("EventGraph"));
	TestNotNull(TEXT("EventGraph exists before injecting unsupported node"), EventGraph);
	if (!EventGraph)
	{
		return false;
	}

	UK2Node_IfThenElse* Branch = NewObject<UK2Node_IfThenElse>(EventGraph, UK2Node_IfThenElse::StaticClass(), NAME_None, RF_Transactional);
	Branch->CreateNewGuid();
	Branch->NodePosX = 320;
	Branch->NodePosY = 0;
	EventGraph->AddNode(Branch, true, false);
	Branch->AllocateDefaultPins();
	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds with unsupported graph node evidence"), ExtractResult.IsSuccess());
	TestTrue(TEXT("Unsupported graph node is reported in skipped evidence"), ExtractedBodySkippedGraphContainsClass(ExtractResult, TEXT("/Script/BlueprintGraph.K2Node_IfThenElse")));
	const TSharedPtr<FJsonObject> ExtractedEventGraph = FindExtractedGraph(ExtractResult, TEXT("UbergraphPages"), TEXT("EventGraph"));
	TestTrue(TEXT("Supported nodes still extract"), ExtractedGraphHasNodeClass(ExtractedEventGraph, TEXT("/Script/BlueprintGraph.K2Node_Self")));
	TestFalse(TEXT("Unsupported node is not emitted as lossy graph node"), ExtractedGraphHasNodeClass(ExtractedEventGraph, TEXT("/Script/BlueprintGraph.K2Node_IfThenElse")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintWidgetTreeCreateExtractDiffTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree.CreateExtractDiff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintWidgetTreeCreateExtractDiffTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_WidgetTreeCreateExtractDiff"));

	TSharedRef<FJsonObject> Root = MakeWidgetNode(TEXT("RootCanvas"), TEXT("/Script/UMG.CanvasPanel"));
	TSharedRef<FJsonObject> TitleText = MakeWidgetNode(TEXT("TitleText"), TEXT("/Script/UMG.TextBlock"));
	TitleText->GetObjectField(TEXT("Properties"))->SetStringField(TEXT("Text"), TEXT("Hello WidgetTree"));
	TArray<TSharedPtr<FJsonValue>> Children;
	Children.Add(MakeShared<FJsonValueObject>(TitleText));
	Root->SetArrayField(TEXT("Children"), Children);

	TSharedPtr<FJsonObject> Document = MakeWidgetBlueprintDocument(Target, MakeWidgetTreeBody(MakeWidgetTree(Root)));
	FAssetDocumentService Service;
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyFileRequest(Document));
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("WidgetTree apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("WidgetTree apply succeeds"), ApplyResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint exists"), WidgetBlueprint);
	if (WidgetBlueprint && WidgetBlueprint->WidgetTree)
	{
		TestNotNull(TEXT("RootCanvas exists"), WidgetBlueprint->WidgetTree->FindWidget(TEXT("RootCanvas")));
		TestNotNull(TEXT("TitleText exists"), WidgetBlueprint->WidgetTree->FindWidget(TEXT("TitleText")));
	}

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds after WidgetTree apply"), ExtractResult.IsSuccess());
	TestTrue(TEXT("Extract returns payload"), ExtractResult.Payload.IsValid());
	if (ExtractResult.Payload.IsValid())
	{
		const TSharedPtr<FJsonObject>* ExtractedBody = nullptr;
		TestTrue(TEXT("Extracted document contains Body"), ExtractResult.Payload->TryGetObjectField(TEXT("Body"), ExtractedBody) && ExtractedBody && ExtractedBody->IsValid());
		if (ExtractedBody && ExtractedBody->IsValid())
		{
			const TSharedPtr<FJsonObject>* ExtractedWidgetTree = nullptr;
			TestTrue(TEXT("Extracted Body contains WidgetTree"), (*ExtractedBody)->TryGetObjectField(TEXT("WidgetTree"), ExtractedWidgetTree) && ExtractedWidgetTree && ExtractedWidgetTree->IsValid());
			if (ExtractedWidgetTree && ExtractedWidgetTree->IsValid())
			{
				const TSharedPtr<FJsonObject>* ExtractedRoot = nullptr;
				TestTrue(TEXT("Extracted WidgetTree contains RootWidget"), (*ExtractedWidgetTree)->TryGetObjectField(TEXT("RootWidget"), ExtractedRoot) && ExtractedRoot && ExtractedRoot->IsValid());
				if (ExtractedRoot && ExtractedRoot->IsValid())
				{
					TestEqual(TEXT("Extracted root name is canonical"), (*ExtractedRoot)->GetStringField(TEXT("Name")), FString(TEXT("RootCanvas")));
				}
			}
		}
	}

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = Document;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("Diff succeeds after WidgetTree apply"), DiffResult.IsSuccess());
	TestTrue(TEXT("Desired WidgetTree is unchanged after roundtrip"), DiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintWidgetTreeOmittedChildDeletesTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree.OmittedChildDeletes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintWidgetTreeOmittedChildDeletesTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_WidgetTreeOmittedChildDeletes"));
	FAssetDocumentService Service;

	TSharedRef<FJsonObject> RootWithChild = MakeWidgetNode(TEXT("RootCanvas"), TEXT("/Script/UMG.CanvasPanel"));
	TSharedRef<FJsonObject> TitleText = MakeWidgetNode(TEXT("TitleText"), TEXT("/Script/UMG.TextBlock"));
	TArray<TSharedPtr<FJsonValue>> Children;
	Children.Add(MakeShared<FJsonValueObject>(TitleText));
	RootWithChild->SetArrayField(TEXT("Children"), Children);

	const FAssetDocumentResult FirstResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, MakeWidgetTreeBody(MakeWidgetTree(RootWithChild)))));
	TestTrue(TEXT("Initial WidgetTree with child applies"), FirstResult.IsSuccess());

	TSharedRef<FJsonObject> RootWithoutChild = MakeWidgetNode(TEXT("RootCanvas"), TEXT("/Script/UMG.CanvasPanel"));
	const FAssetDocumentResult SecondResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, MakeWidgetTreeBody(MakeWidgetTree(RootWithoutChild)))));
	if (!SecondResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Second WidgetTree apply failed: %s"), *SecondResult.Message));
	}
	TestTrue(TEXT("Second WidgetTree apply succeeds"), SecondResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint exists"), WidgetBlueprint);
	if (WidgetBlueprint && WidgetBlueprint->WidgetTree)
	{
		TestNotNull(TEXT("RootCanvas remains"), WidgetBlueprint->WidgetTree->FindWidget(TEXT("RootCanvas")));
		TestNull(TEXT("Omitted child is deleted"), WidgetBlueprint->WidgetTree->FindWidget(TEXT("TitleText")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintWidgetTreeRestoresOmittedPropertiesTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree.RestoresOmittedProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintWidgetTreeRestoresOmittedPropertiesTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_WidgetTreeRestoresOmittedProperties"));
	FAssetDocumentService Service;

	TSharedRef<FJsonObject> TextWithValue = MakeWidgetNode(TEXT("TitleText"), TEXT("/Script/UMG.TextBlock"));
	TextWithValue->GetObjectField(TEXT("Properties"))->SetStringField(TEXT("Text"), TEXT("Authored title"));
	const FAssetDocumentResult FirstResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, MakeWidgetTreeBody(MakeWidgetTree(TextWithValue)))));
	TestTrue(TEXT("Initial TextBlock property apply succeeds"), FirstResult.IsSuccess());

	TSharedRef<FJsonObject> TextWithoutProperties = MakeWidgetNode(TEXT("TitleText"), TEXT("/Script/UMG.TextBlock"));
	TextWithoutProperties->RemoveField(TEXT("Properties"));
	const FAssetDocumentResult SecondResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, MakeWidgetTreeBody(MakeWidgetTree(TextWithoutProperties)))));
	if (!SecondResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Omitted property apply failed: %s"), *SecondResult.Message));
	}
	TestTrue(TEXT("Omitted property apply succeeds"), SecondResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint exists"), WidgetBlueprint);
	if (WidgetBlueprint && WidgetBlueprint->WidgetTree)
	{
		UTextBlock* TextBlock = Cast<UTextBlock>(WidgetBlueprint->WidgetTree->FindWidget(TEXT("TitleText")));
		TestNotNull(TEXT("TitleText remains a TextBlock"), TextBlock);
		if (TextBlock)
		{
			TestTrue(TEXT("Omitted Text property restores default empty text"), TextBlock->GetText().IsEmpty());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintWidgetTreeNamedSlotBindingsTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree.NamedSlotBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintWidgetTreeNamedSlotBindingsTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_WidgetTreeNamedSlotBindings"));

	TSharedPtr<FJsonObject> WidgetTree = MakeWidgetTree(MakeWidgetNode(TEXT("RootSlot"), TEXT("/Script/UMG.NamedSlot")));
	TSharedPtr<FJsonObject> NamedSlotBindings = MakeShared<FJsonObject>();
	NamedSlotBindings->SetObjectField(TEXT("Header"), MakeWidgetNode(TEXT("HeaderText"), TEXT("/Script/UMG.TextBlock")));
	WidgetTree->SetObjectField(TEXT("NamedSlotBindings"), NamedSlotBindings);

	FAssetDocumentService Service;
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, MakeWidgetTreeBody(WidgetTree))));
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Named slot WidgetTree apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("Named slot WidgetTree apply succeeds"), ApplyResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint exists"), WidgetBlueprint);
	if (WidgetBlueprint && WidgetBlueprint->WidgetTree)
	{
		TestNotNull(TEXT("Named slot content widget exists"), WidgetBlueprint->WidgetTree->FindWidget(TEXT("HeaderText")));
		TestTrue(TEXT("Named slot binding exists"), WidgetBlueprint->WidgetTree->NamedSlotBindings.Contains(TEXT("Header")));
		TestEqual(TEXT("Named slot binding points to HeaderText"), WidgetBlueprint->WidgetTree->NamedSlotBindings.FindRef(TEXT("Header"))->GetFName(), FName(TEXT("HeaderText")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintWidgetTreeCompilesTreeOnlyRebuildTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree.CompilesTreeOnlyRebuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintWidgetTreeCompilesTreeOnlyRebuildTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_WidgetTreeCompilesTreeOnlyRebuild"));
	FAssetDocumentService Service;

	const FAssetDocumentResult InitialResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, MakeDefaultWidgetBlueprintBody())));
	TestTrue(TEXT("Initial empty WidgetBlueprint apply succeeds"), InitialResult.IsSuccess());

	TSharedRef<FJsonObject> Root = MakeWidgetNode(TEXT("RootCanvas"), TEXT("/Script/UMG.CanvasPanel"));
	TSharedRef<FJsonObject> CompiledTitle = MakeWidgetNode(TEXT("CompiledTitle"), TEXT("/Script/UMG.TextBlock"));
	TArray<TSharedPtr<FJsonValue>> Children;
	Children.Add(MakeShared<FJsonValueObject>(CompiledTitle));
	Root->SetArrayField(TEXT("Children"), Children);

	const FAssetDocumentResult TreeOnlyResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, MakeWidgetTreeBody(MakeWidgetTree(Root)))));
	if (!TreeOnlyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("WidgetTree-only apply failed: %s"), *TreeOnlyResult.Message));
	}
	TestTrue(TEXT("WidgetTree-only apply succeeds without ParentClass change"), TreeOnlyResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint exists"), WidgetBlueprint);
	if (WidgetBlueprint)
	{
		UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WidgetBlueprint->GeneratedClass);
		TestNotNull(TEXT("WidgetBlueprint generated class exists"), GeneratedClass);
		if (GeneratedClass)
		{
			UWidgetTree* GeneratedTree = GeneratedClass->GetWidgetTreeArchetype();
			TestNotNull(TEXT("Compiled generated class has WidgetTree archetype"), GeneratedTree);
			if (GeneratedTree)
			{
				TestNotNull(TEXT("Compiled generated class includes WidgetTree-only child"), GeneratedTree->FindWidget(TEXT("CompiledTitle")));
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintWidgetTreeSingleContentWidgetRoundtripTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree.SingleContentWidgetRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintWidgetTreeSingleContentWidgetRoundtripTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_WidgetTreeSingleContentWidgetRoundtrip"));

	TSharedRef<FJsonObject> RootBorder = MakeWidgetNode(TEXT("RootBorder"), TEXT("/Script/UMG.Border"));
	TSharedRef<FJsonObject> BorderText = MakeWidgetNode(TEXT("BorderText"), TEXT("/Script/UMG.TextBlock"));
	BorderText->GetObjectField(TEXT("Properties"))->SetStringField(TEXT("Text"), TEXT("Inside border"));
	TArray<TSharedPtr<FJsonValue>> Children;
	Children.Add(MakeShared<FJsonValueObject>(BorderText));
	RootBorder->SetArrayField(TEXT("Children"), Children);

	TSharedPtr<FJsonObject> Document = MakeWidgetBlueprintDocument(Target, MakeWidgetTreeBody(MakeWidgetTree(RootBorder)));
	FAssetDocumentService Service;
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyFileRequest(Document));
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Single-content WidgetTree apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("Single-content WidgetTree apply succeeds"), ApplyResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint exists"), WidgetBlueprint);
	if (WidgetBlueprint && WidgetBlueprint->WidgetTree)
	{
		UContentWidget* Border = Cast<UContentWidget>(WidgetBlueprint->WidgetTree->FindWidget(TEXT("RootBorder")));
		TestNotNull(TEXT("RootBorder is a content widget"), Border);
		if (Border)
		{
			UWidget* Content = Border->GetContent();
			TestNotNull(TEXT("RootBorder has authored content"), Content);
			if (Content)
			{
				TestEqual(TEXT("RootBorder content is BorderText"), Content->GetFName(), FName(TEXT("BorderText")));
			}
		}
	}

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds after single-content WidgetTree apply"), ExtractResult.IsSuccess());
	TestTrue(TEXT("Extract returns payload"), ExtractResult.Payload.IsValid());
	if (ExtractResult.Payload.IsValid())
	{
		const TSharedPtr<FJsonObject>* ExtractedBody = nullptr;
		TestTrue(TEXT("Extracted document contains Body"), ExtractResult.Payload->TryGetObjectField(TEXT("Body"), ExtractedBody) && ExtractedBody && ExtractedBody->IsValid());
		if (ExtractedBody && ExtractedBody->IsValid())
		{
			const TSharedPtr<FJsonObject>* ExtractedWidgetTree = nullptr;
			TestTrue(TEXT("Extracted Body contains WidgetTree"), (*ExtractedBody)->TryGetObjectField(TEXT("WidgetTree"), ExtractedWidgetTree) && ExtractedWidgetTree && ExtractedWidgetTree->IsValid());
			if (ExtractedWidgetTree && ExtractedWidgetTree->IsValid())
			{
				const TSharedPtr<FJsonObject>* ExtractedRoot = nullptr;
				TestTrue(TEXT("Extracted WidgetTree contains RootWidget"), (*ExtractedWidgetTree)->TryGetObjectField(TEXT("RootWidget"), ExtractedRoot) && ExtractedRoot && ExtractedRoot->IsValid());
				if (ExtractedRoot && ExtractedRoot->IsValid())
				{
					const TArray<TSharedPtr<FJsonValue>>* ExtractedChildren = nullptr;
					TestTrue(TEXT("Extracted RootBorder preserves content child"), (*ExtractedRoot)->TryGetArrayField(TEXT("Children"), ExtractedChildren) && ExtractedChildren && ExtractedChildren->Num() == 1);
					if (ExtractedChildren && ExtractedChildren->Num() == 1 && (*ExtractedChildren)[0].IsValid() && (*ExtractedChildren)[0]->Type == EJson::Object)
					{
						TestEqual(TEXT("Extracted RootBorder child is BorderText"), (*ExtractedChildren)[0]->AsObject()->GetStringField(TEXT("Name")), FString(TEXT("BorderText")));
					}
				}
			}
		}
	}

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = Document;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("Diff succeeds after single-content WidgetTree apply"), DiffResult.IsSuccess());
	TestTrue(TEXT("Single-content WidgetTree is unchanged after roundtrip"), DiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));

	TSharedRef<FJsonObject> RootBorderWithoutChild = MakeWidgetNode(TEXT("RootBorder"), TEXT("/Script/UMG.Border"));
	FAssetDocumentDiffRequest MissingChildDiffRequest;
	MissingChildDiffRequest.Document = MakeWidgetBlueprintDocument(Target, MakeWidgetTreeBody(MakeWidgetTree(RootBorderWithoutChild)));
	const FAssetDocumentResult MissingChildDiffResult = Service.Diff(MissingChildDiffRequest);
	TestTrue(TEXT("Diff succeeds for missing single-content child"), MissingChildDiffResult.IsSuccess());
	TestFalse(TEXT("Diff reports omitted single-content child"), DiffPayloadHasNoChangedOrFailedEntries(MissingChildDiffResult.Payload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintWidgetTreeNoOpApplyPreservesRootTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree.NoOpApplyPreservesRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintWidgetTreeNoOpApplyPreservesRootTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_WidgetTreeNoOpApplyPreservesRoot"));
	TSharedRef<FJsonObject> Root = MakeWidgetNode(TEXT("RootCanvas"), TEXT("/Script/UMG.CanvasPanel"));
	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = MakeWidgetBlueprintDocument(Target, MakeWidgetTreeBody(MakeWidgetTree(Root)));

	const FAssetDocumentResult FirstResult = Service.Apply(MakeApplyFileRequest(Document));
	TestTrue(TEXT("Initial WidgetTree apply succeeds"), FirstResult.IsSuccess());

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint loads after initial apply"), WidgetBlueprint);
	UWidget* RootBefore = WidgetBlueprint && WidgetBlueprint->WidgetTree ? WidgetBlueprint->WidgetTree->RootWidget : nullptr;
	TestNotNull(TEXT("Initial root widget exists"), RootBefore);

	const FAssetDocumentResult SecondResult = Service.Apply(MakeApplyFileRequest(Document));
	if (!SecondResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Second identical WidgetTree apply failed: %s"), *SecondResult.Message));
	}
	TestTrue(TEXT("Second identical WidgetTree apply succeeds"), SecondResult.IsSuccess());

	UWidgetBlueprint* ReloadedWidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	UWidget* RootAfter = ReloadedWidgetBlueprint && ReloadedWidgetBlueprint->WidgetTree ? ReloadedWidgetBlueprint->WidgetTree->RootWidget : nullptr;
	TestEqual(TEXT("Identical WidgetTree apply preserves existing root object"), RootAfter, RootBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintWidgetTreeVariableRoundtripTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree.VariableRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintWidgetTreeVariableRoundtripTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_WidgetTreeVariableRoundtrip"));
	TSharedRef<FJsonObject> Root = MakeWidgetNode(TEXT("RootCanvas"), TEXT("/Script/UMG.CanvasPanel"));
	TSharedRef<FJsonObject> TitleText = MakeWidgetNode(TEXT("TitleText"), TEXT("/Script/UMG.TextBlock"));
	TitleText->SetBoolField(TEXT("IsVariable"), true);
	TitleText->SetStringField(TEXT("VariableName"), TEXT("TitleText"));
	TArray<TSharedPtr<FJsonValue>> Children;
	Children.Add(MakeShared<FJsonValueObject>(TitleText));
	Root->SetArrayField(TEXT("Children"), Children);

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Document = MakeWidgetBlueprintDocument(Target, MakeWidgetTreeBody(MakeWidgetTree(Root)));
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyFileRequest(Document));
	if (!ApplyResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("Variable WidgetTree apply failed: %s"), *ApplyResult.Message));
	}
	TestTrue(TEXT("Variable WidgetTree apply succeeds"), ApplyResult.IsSuccess());

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("Extract succeeds after variable WidgetTree apply"), ExtractResult.IsSuccess());
	if (ExtractResult.Payload.IsValid())
	{
		const TSharedPtr<FJsonObject>* ExtractedBody = nullptr;
		if (ExtractResult.Payload->TryGetObjectField(TEXT("Body"), ExtractedBody) && ExtractedBody && ExtractedBody->IsValid())
		{
			const TSharedPtr<FJsonObject>* ExtractedWidgetTree = nullptr;
			if ((*ExtractedBody)->TryGetObjectField(TEXT("WidgetTree"), ExtractedWidgetTree) && ExtractedWidgetTree && ExtractedWidgetTree->IsValid())
			{
				const TSharedPtr<FJsonObject>* ExtractedRoot = nullptr;
				if ((*ExtractedWidgetTree)->TryGetObjectField(TEXT("RootWidget"), ExtractedRoot) && ExtractedRoot && ExtractedRoot->IsValid())
				{
					TestFalse(TEXT("Non-variable root omits VariableName"), (*ExtractedRoot)->HasField(TEXT("VariableName")));
					const TArray<TSharedPtr<FJsonValue>>* ExtractedChildren = nullptr;
					if ((*ExtractedRoot)->TryGetArrayField(TEXT("Children"), ExtractedChildren))
					{
						TestEqual(TEXT("Root has one extracted child"), ExtractedChildren->Num(), 1);
						if (ExtractedChildren->IsValidIndex(0) && (*ExtractedChildren)[0].IsValid() && (*ExtractedChildren)[0]->Type == EJson::Object)
						{
							const TSharedPtr<FJsonObject> ExtractedChild = (*ExtractedChildren)[0]->AsObject();
							TestTrue(TEXT("Variable child extracts IsVariable"), ExtractedChild->GetBoolField(TEXT("IsVariable")));
							TestEqual(TEXT("VariableName roundtrips as widget name"), ExtractedChild->GetStringField(TEXT("VariableName")), FString(TEXT("TitleText")));
						}
					}
				}
			}
		}
	}

	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.Document = Document;
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("Diff succeeds after variable WidgetTree apply"), DiffResult.IsSuccess());
	TestTrue(TEXT("Variable WidgetTree is unchanged after roundtrip"), DiffPayloadHasNoChangedOrFailedEntries(DiffResult.Payload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintWidgetTreeRejectsMismatchedVariableNameTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree.RejectsMismatchedVariableName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintWidgetTreeRejectsMismatchedVariableNameTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_WidgetTreeRejectsMismatchedVariableName"));
	TSharedRef<FJsonObject> Root = MakeWidgetNode(TEXT("RootCanvas"), TEXT("/Script/UMG.CanvasPanel"));
	Root->SetBoolField(TEXT("IsVariable"), true);
	Root->SetStringField(TEXT("VariableName"), TEXT("RenamedRoot"));

	FAssetDocumentService Service;
	const FAssetDocumentResult Result = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, MakeWidgetTreeBody(MakeWidgetTree(Root)))));
	TestFalse(TEXT("Mismatched VariableName rejects apply"), Result.IsSuccess());
	TestTrue(TEXT("Mismatched VariableName diagnostic is reported"), ResultHasDiagnosticCode(Result, TEXT("UnsupportedWidgetVariableName")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintWidgetTreeRejectsInvalidClassTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree.RejectsInvalidClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintWidgetTreeRejectsInvalidClassTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_WidgetTreeRejectsInvalidClass"));
	TSharedRef<FJsonObject> ValidRoot = MakeWidgetNode(TEXT("RootCanvas"), TEXT("/Script/UMG.CanvasPanel"));
	FAssetDocumentService Service;
	TestTrue(TEXT("Valid fixture apply succeeds"), Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, MakeWidgetTreeBody(MakeWidgetTree(ValidRoot))))).IsSuccess());

	TSharedRef<FJsonObject> InvalidRoot = MakeWidgetNode(TEXT("BadRoot"), TEXT("/Script/Engine.Actor"));
	const FAssetDocumentResult BadResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, MakeWidgetTreeBody(MakeWidgetTree(InvalidRoot)))));
	TestFalse(TEXT("Invalid widget class rejects apply"), BadResult.IsSuccess());
	TestTrue(TEXT("Invalid class diagnostic is reported"), ResultHasDiagnosticCode(BadResult, TEXT("InvalidWidgetClass")) || ResultHasDiagnosticCode(BadResult, TEXT("UnresolvedWidgetClass")));

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint remains loadable after invalid apply"), WidgetBlueprint);
	if (WidgetBlueprint && WidgetBlueprint->WidgetTree)
	{
		TestNotNull(TEXT("Existing RootCanvas survives invalid apply"), WidgetBlueprint->WidgetTree->FindWidget(TEXT("RootCanvas")));
		TestNull(TEXT("BadRoot is not half-applied"), WidgetBlueprint->WidgetTree->FindWidget(TEXT("BadRoot")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintWidgetTreeRejectsNullWidgetTreeTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree.RejectsNullWidgetTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintWidgetTreeRejectsNullWidgetTreeTest::RunTest(const FString&)
{
	FWidgetBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UWidgetBlueprint::StaticClass();

	TSharedRef<FJsonObject> NullWidgetTreeBody = MakeDefaultWidgetBlueprintBody();
	NullWidgetTreeBody->SetField(TEXT("WidgetTree"), MakeShared<FJsonValueNull>());
	const FAssetDocumentCapabilityResult NullWidgetTreeResult = Capability.Validate(Context, MakeBodyJsonValue(NullWidgetTreeBody));
	TestFalse(TEXT("Explicit null Body.WidgetTree fails validation"), NullWidgetTreeResult.bSuccess);
	TestTrue(TEXT("Null WidgetTree reports invalid section type"), NullWidgetTreeResult.Diagnostics.ContainsByPredicate([](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Path == TEXT("/Body/WidgetTree") && Diagnostic.Code == TEXT("InvalidBodySectionType");
	}));

	TSharedRef<FJsonObject> RootNullBody = MakeDefaultWidgetBlueprintBody();
	TSharedPtr<FJsonObject> WidgetTree = MakeShared<FJsonObject>();
	WidgetTree->SetField(TEXT("RootWidget"), MakeShared<FJsonValueNull>());
	WidgetTree->SetObjectField(TEXT("NamedSlotBindings"), MakeShared<FJsonObject>());
	RootNullBody->SetObjectField(TEXT("WidgetTree"), WidgetTree);
	const FAssetDocumentCapabilityResult RootNullResult = Capability.Validate(Context, MakeBodyJsonValue(RootNullBody));
	TestTrue(TEXT("Null WidgetTree.RootWidget remains valid"), RootNullResult.bSuccess);
	if (!RootNullResult.bSuccess)
	{
		AddError(RootNullResult.Message);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintWidgetTreeRejectsDuplicateNamesTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree.RejectsDuplicateNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintWidgetTreeRejectsDuplicateNamesTest::RunTest(const FString&)
{
	const FString Target = MakeUniqueWidgetBlueprintTarget(TEXT("WBP_WidgetTreeRejectsDuplicateNames"));
	TSharedRef<FJsonObject> ValidRoot = MakeWidgetNode(TEXT("RootCanvas"), TEXT("/Script/UMG.CanvasPanel"));
	FAssetDocumentService Service;
	TestTrue(TEXT("Valid fixture apply succeeds"), Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, MakeWidgetTreeBody(MakeWidgetTree(ValidRoot))))).IsSuccess());

	TSharedRef<FJsonObject> DuplicateRoot = MakeWidgetNode(TEXT("DuplicateName"), TEXT("/Script/UMG.CanvasPanel"));
	TSharedRef<FJsonObject> DuplicateChild = MakeWidgetNode(TEXT("DuplicateName"), TEXT("/Script/UMG.TextBlock"));
	TArray<TSharedPtr<FJsonValue>> Children;
	Children.Add(MakeShared<FJsonValueObject>(DuplicateChild));
	DuplicateRoot->SetArrayField(TEXT("Children"), Children);

	const FAssetDocumentResult BadResult = Service.Apply(MakeApplyFileRequest(MakeWidgetBlueprintDocument(Target, MakeWidgetTreeBody(MakeWidgetTree(DuplicateRoot)))));
	TestFalse(TEXT("Duplicate widget names reject apply"), BadResult.IsSuccess());
	TestTrue(TEXT("Duplicate name diagnostic is reported"), ResultHasDiagnosticCode(BadResult, TEXT("DuplicateWidgetName")));

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintForTarget(Target);
	TestNotNull(TEXT("WidgetBlueprint remains loadable after duplicate-name apply"), WidgetBlueprint);
	if (WidgetBlueprint && WidgetBlueprint->WidgetTree)
	{
		TestNotNull(TEXT("Existing RootCanvas survives duplicate-name apply"), WidgetBlueprint->WidgetTree->FindWidget(TEXT("RootCanvas")));
		TestNull(TEXT("DuplicateName is not half-applied"), WidgetBlueprint->WidgetTree->FindWidget(TEXT("DuplicateName")));
	}
	return true;
}

#endif
