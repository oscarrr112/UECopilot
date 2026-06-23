// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentProfile.h"
#include "Profiles/WidgetBlueprintAssetDocumentCapability.h"
#include "Profiles/WidgetBlueprintAssetDocumentProfile.h"

#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "WidgetBlueprint.h"

#if WITH_DEV_AUTOMATION_TESTS

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

#endif
