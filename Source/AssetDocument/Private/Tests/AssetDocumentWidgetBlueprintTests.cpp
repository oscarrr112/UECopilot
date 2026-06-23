// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentLifecycle.h"
#include "AssetDocumentProfile.h"
#include "Profiles/WidgetBlueprintAssetDocumentCapability.h"
#include "Profiles/WidgetBlueprintAssetDocumentProfile.h"

#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Dom/JsonValue.h"
#include "Misc/Guid.h"
#include "Misc/AutomationTest.h"
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
	TestTrue(TEXT("Diff exposes non-empty WidgetTree as changed/skipped"), DiffEntries.ContainsByPredicate([](const TSharedPtr<FJsonValue>& Entry)
	{
		const TSharedPtr<FJsonObject> Object = Entry.IsValid() ? Entry->AsObject() : nullptr;
		FString Path;
		FString Status;
		return Object.IsValid()
			&& Object->TryGetStringField(TEXT("path"), Path)
			&& Path == TEXT("/Body/WidgetTree")
			&& Object->TryGetStringField(TEXT("status"), Status)
			&& Status != TEXT("unchanged");
	}));
	return true;
}

#endif
