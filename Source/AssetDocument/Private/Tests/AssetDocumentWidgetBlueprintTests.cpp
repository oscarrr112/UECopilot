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
#include "Misc/Guid.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
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
	TArray<TSharedPtr<FJsonValue>> Bindings;
	Bindings.Add(MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()));
	Body->SetArrayField(TEXT("Bindings"), Bindings);

	const FAssetDocumentCapabilityResult Result = Capability.Validate(Context, MakeBodyJsonValue(Body));
	TestFalse(TEXT("Non-empty authored Bindings fail validation"), Result.bSuccess);
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
	WidgetBlueprint->Bindings.AddDefaulted();
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
	TestEqual(TEXT("Bindings remain intact"), WidgetBlueprint->Bindings.Num(), 1);
	TestEqual(TEXT("Animations remain intact"), WidgetBlueprint->Animations.Num(), 1);

	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	const FAssetDocumentCapabilityResult DiffResult = Capability.Diff(Context, MakeBodyJsonValue(MakeDefaultWidgetBlueprintBody()), DiffEntries);
	TestTrue(TEXT("Diff succeeds for inspection"), DiffResult.bSuccess);
	TestTrue(TEXT("Diff exposes non-empty unsupported Bindings as changed/skipped"), DiffEntries.ContainsByPredicate([](const TSharedPtr<FJsonValue>& Entry)
	{
		const TSharedPtr<FJsonObject> Object = Entry.IsValid() ? Entry->AsObject() : nullptr;
		FString Path;
		FString Status;
		return Object.IsValid()
			&& Object->TryGetStringField(TEXT("path"), Path)
			&& Path == TEXT("/Body/Bindings")
			&& Object->TryGetStringField(TEXT("status"), Status)
			&& Status != TEXT("unchanged");
	}));
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
