// Copyright ProjectRPG. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Profiles/AnimBlueprintAssetDocumentProfile.h"

#include "Animation/AnimBlueprint.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"

namespace
{
bool HasBodyKey(const TArray<FName>& BodyKeys, const TCHAR* Name)
{
	return BodyKeys.Contains(FName(Name));
}

TSharedRef<FJsonValue> MakeEmptyBodyValue()
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithNonEmptyDeferredRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> RegionEntries;
	RegionEntries.Add(MakeShared<FJsonValueString>(TEXT("unsupported")));
	Body->SetArrayField(RegionName, MoveTemp(RegionEntries));
	return MakeShared<FJsonValueObject>(Body);
}

const FAssetDocumentRegionPolicy* FindPolicy(const TArray<FAssetDocumentRegionPolicy>& Policies, const TCHAR* RegionId)
{
	return Policies.FindByPredicate([RegionId](const FAssetDocumentRegionPolicy& Policy)
	{
		return Policy.RegionId == FName(RegionId);
	});
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintProfileShapeTest,
	"AssetFactory.AssetDocument.AnimBlueprint.ProfileShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintProfileShapeTest::RunTest(const FString&)
{
	const FAnimBlueprintAssetDocumentProfile Profile;
	TestEqual(TEXT("Exact class is UAnimBlueprint"), Profile.GetExactClass(), UAnimBlueprint::StaticClass());

	FAssetDocumentTemplateContext TemplateContext;
	TemplateContext.Target = TEXT("/Game/AssetDocumentSmoke/ABP_AssetDocumentSmoke");
	const TSharedRef<FJsonObject> Template = Profile.CreateTemplate(TemplateContext);
	TestEqual(TEXT("Template class is AnimBlueprint"), Template->GetStringField(TEXT("Class")), FString(TEXT("/Script/Engine.AnimBlueprint")));

	const TSharedPtr<FJsonObject> Body = Template->GetObjectField(TEXT("Body"));
	TestTrue(TEXT("Template includes Body object"), Body.IsValid());
	if (Body.IsValid())
	{
		const TArray<FName> ExpectedTemplateBodyKeys = FAnimBlueprintAssetDocumentCapability::GetCanonicalBodyKeys();
		TestEqual(TEXT("Template Body only includes canonical keys"), Body->Values.Num(), ExpectedTemplateBodyKeys.Num());
		for (const FName& ExpectedKey : ExpectedTemplateBodyKeys)
		{
			TestTrue(
				FString::Printf(TEXT("Template includes Body.%s"), *ExpectedKey.ToString()),
				Body->HasField(ExpectedKey.ToString()));
		}
	}

	const TArray<FName> BodyKeys = Profile.GetBodyKeys();
	TestTrue(TEXT("Body keys include ParentClass"), HasBodyKey(BodyKeys, TEXT("ParentClass")));
	TestTrue(TEXT("Body keys include TargetSkeleton"), HasBodyKey(BodyKeys, TEXT("TargetSkeleton")));
	TestTrue(TEXT("Body keys include Template"), HasBodyKey(BodyKeys, TEXT("Template")));
	TestTrue(TEXT("Body keys include Preview"), HasBodyKey(BodyKeys, TEXT("Preview")));
	TestTrue(TEXT("Body keys include Optimization"), HasBodyKey(BodyKeys, TEXT("Optimization")));
	TestTrue(TEXT("Body keys include SyncGroups"), HasBodyKey(BodyKeys, TEXT("SyncGroups")));
	TestTrue(TEXT("Body keys include ImplementedInterfaces"), HasBodyKey(BodyKeys, TEXT("ImplementedInterfaces")));
	TestTrue(TEXT("Body keys include Variables"), HasBodyKey(BodyKeys, TEXT("Variables")));
	TestTrue(TEXT("Body keys include ClassDefaults"), HasBodyKey(BodyKeys, TEXT("ClassDefaults")));
	TestTrue(TEXT("Body keys include UbergraphPages"), HasBodyKey(BodyKeys, TEXT("UbergraphPages")));
	TestTrue(TEXT("Body keys include AnimGraph"), HasBodyKey(BodyKeys, TEXT("AnimGraph")));
	TestTrue(TEXT("Body keys include StateMachines"), HasBodyKey(BodyKeys, TEXT("StateMachines")));
	TestTrue(TEXT("Body keys include TransitionGraphs"), HasBodyKey(BodyKeys, TEXT("TransitionGraphs")));
	TestTrue(TEXT("Body keys include AnimLayers"), HasBodyKey(BodyKeys, TEXT("AnimLayers")));
	TestTrue(TEXT("Body keys include ParentAssetOverrides"), HasBodyKey(BodyKeys, TEXT("ParentAssetOverrides")));
	TestTrue(TEXT("Body root resolves adapter"), Profile.ResolveBodyAdapter(TEXT("Body")) != nullptr);
	TestTrue(TEXT("ParentClass resolves adapter"), Profile.ResolveBodyAdapter(TEXT("ParentClass")) != nullptr);

	const TArray<FAssetDocumentRegionPolicy> Policies = Profile.GetRegionPolicies();
	TestNotNull(TEXT("Policy includes Body.ParentClass"), FindPolicy(Policies, TEXT("Body.ParentClass")));
	TestNotNull(TEXT("Policy includes Body.TargetSkeleton"), FindPolicy(Policies, TEXT("Body.TargetSkeleton")));
	TestNotNull(TEXT("Policy includes Body.Template"), FindPolicy(Policies, TEXT("Body.Template")));
	TestNotNull(TEXT("Policy includes Body.Preview"), FindPolicy(Policies, TEXT("Body.Preview")));
	TestNotNull(TEXT("Policy includes Body.Optimization"), FindPolicy(Policies, TEXT("Body.Optimization")));
	TestNotNull(TEXT("Policy includes Body.SyncGroups"), FindPolicy(Policies, TEXT("Body.SyncGroups")));
	TestNotNull(TEXT("Policy includes Body.ImplementedInterfaces"), FindPolicy(Policies, TEXT("Body.ImplementedInterfaces")));
	TestNotNull(TEXT("Policy includes Body.Variables"), FindPolicy(Policies, TEXT("Body.Variables")));
	TestNotNull(TEXT("Policy includes Body.ClassDefaults"), FindPolicy(Policies, TEXT("Body.ClassDefaults")));
	TestNotNull(TEXT("Policy includes Body.UbergraphPages"), FindPolicy(Policies, TEXT("Body.UbergraphPages")));
	const FAssetDocumentRegionPolicy* AnimGraphPolicy = FindPolicy(Policies, TEXT("Body.AnimGraph"));
	const FAssetDocumentRegionPolicy* StateMachinesPolicy = FindPolicy(Policies, TEXT("Body.StateMachines"));
	const FAssetDocumentRegionPolicy* TransitionGraphsPolicy = FindPolicy(Policies, TEXT("Body.TransitionGraphs"));
	const FAssetDocumentRegionPolicy* AnimLayersPolicy = FindPolicy(Policies, TEXT("Body.AnimLayers"));
	const FAssetDocumentRegionPolicy* ParentAssetOverridesPolicy = FindPolicy(Policies, TEXT("Body.ParentAssetOverrides"));
	TestNotNull(TEXT("Policy includes Body.AnimGraph"), AnimGraphPolicy);
	TestNotNull(TEXT("Policy includes Body.StateMachines"), StateMachinesPolicy);
	TestNotNull(TEXT("Policy includes Body.TransitionGraphs"), TransitionGraphsPolicy);
	TestNotNull(TEXT("Policy includes Body.AnimLayers"), AnimLayersPolicy);
	TestNotNull(TEXT("Policy includes Body.ParentAssetOverrides"), ParentAssetOverridesPolicy);
	if (AnimGraphPolicy)
	{
		TestTrue(TEXT("AnimGraph policy is deferred/null-gated"), AnimGraphPolicy->ExplicitDeleteValues.Num() > 0);
	}
	if (StateMachinesPolicy)
	{
		TestTrue(TEXT("StateMachines policy is deferred/null-gated"), StateMachinesPolicy->ExplicitDeleteValues.Num() > 0);
	}
	if (TransitionGraphsPolicy)
	{
		TestTrue(TEXT("TransitionGraphs policy is deferred/null-gated"), TransitionGraphsPolicy->ExplicitDeleteValues.Num() > 0);
	}
	if (AnimLayersPolicy)
	{
		TestTrue(TEXT("AnimLayers policy is deferred/null-gated"), AnimLayersPolicy->ExplicitDeleteValues.Num() > 0);
	}
	if (ParentAssetOverridesPolicy)
	{
		TestTrue(TEXT("ParentAssetOverrides policy is deferred/null-gated"), ParentAssetOverridesPolicy->ExplicitDeleteValues.Num() > 0);
	}

	const IAssetDocumentCapability* BodyAdapter = Profile.ResolveBodyAdapter(TEXT("Body"));
	TestNotNull(TEXT("Body adapter resolves for validation"), BodyAdapter);
	if (BodyAdapter)
	{
		FAssetDocumentCapabilityContext Context;
		Context.AssetClass = UAnimBlueprint::StaticClass();
		TestTrue(TEXT("Empty Body validates"), BodyAdapter->Validate(Context, MakeEmptyBodyValue()).bSuccess);

		for (const TCHAR* DeferredKey : {
			TEXT("AnimGraph"),
			TEXT("StateMachines"),
			TEXT("TransitionGraphs"),
			TEXT("AnimLayers"),
			TEXT("ParentAssetOverrides"),
		})
		{
			const FAssetDocumentCapabilityResult Result = BodyAdapter->Validate(Context, MakeBodyWithNonEmptyDeferredRegion(DeferredKey));
			TestFalse(FString::Printf(TEXT("Body.%s rejects non-empty deferred content"), DeferredKey), Result.bSuccess);
			TestTrue(FString::Printf(TEXT("Body.%s reports a diagnostic"), DeferredKey), Result.Diagnostics.Num() > 0);
			if (Result.Diagnostics.Num() > 0)
			{
				TestEqual(
					FString::Printf(TEXT("Body.%s diagnostic path"), DeferredKey),
					Result.Diagnostics[0].Path,
					FString::Printf(TEXT("/Body/%s"), DeferredKey));
				TestEqual(
					FString::Printf(TEXT("Body.%s diagnostic code"), DeferredKey),
					Result.Diagnostics[0].Code,
					FString(TEXT("UnsupportedAnimBlueprintRegion")));
			}
		}
	}

	return true;
}

#endif
