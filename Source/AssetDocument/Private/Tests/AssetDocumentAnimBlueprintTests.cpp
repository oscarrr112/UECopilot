// Copyright ProjectRPG. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetDocumentService.h"
#include "Profiles/AnimBlueprintAssetDocumentProfile.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"

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

TSharedRef<FJsonValue> MakeBodyWithNullRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetField(RegionName, MakeShared<FJsonValueNull>());
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithEmptyArrayRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(RegionName, TArray<TSharedPtr<FJsonValue>>());
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithEmptyObjectRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(RegionName, MakeShared<FJsonObject>());
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithNonEmptyDeferredRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> AuthoredNode = MakeShared<FJsonObject>();
	AuthoredNode->SetStringField(TEXT("Node"), TEXT("Bad"));
	TArray<TSharedPtr<FJsonValue>> RegionEntries;
	RegionEntries.Add(MakeShared<FJsonValueObject>(AuthoredNode));
	Body->SetArrayField(RegionName, MoveTemp(RegionEntries));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithNonEmptyDeferredObjectRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> RegionObject = MakeShared<FJsonObject>();
	RegionObject->SetStringField(TEXT("Node"), TEXT("Bad"));
	Body->SetObjectField(RegionName, RegionObject);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithScalarDeferredRegion(const TCHAR* RegionName)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(RegionName, TEXT("unsupported"));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithUnknownKey(const TCHAR* UnknownKey = TEXT("UnexpectedGraph"))
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(UnknownKey, TEXT("unsupported"));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonObject> MakeClassRef(const FString& ClassPath)
{
	TSharedRef<FJsonObject> ClassRef = MakeShared<FJsonObject>();
	ClassRef->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ClassRef->SetStringField(TEXT("Class"), ClassPath);
	return ClassRef;
}

TSharedRef<FJsonObject> MakeAssetRef(const FString& AssetPath)
{
	TSharedRef<FJsonObject> AssetRef = MakeShared<FJsonObject>();
	AssetRef->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	AssetRef->SetStringField(TEXT("Path"), AssetPath);
	return AssetRef;
}

TSharedRef<FJsonValue> MakeBodyWithMissingParentClass()
{
	TSharedRef<FJsonObject> ParentClass = MakeShared<FJsonObject>();
	ParentClass->SetStringField(TEXT("Kind"), TEXT("ClassRef"));

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), ParentClass);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithParentClass(const FString& ClassPath)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), MakeClassRef(ClassPath));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithTemplateAndTargetSkeleton()
{
	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetBoolField(TEXT("bIsTemplate"), true);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("Template"), Template);
	Body->SetObjectField(
		TEXT("TargetSkeleton"),
		MakeAssetRef(TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton")));
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithPreviewApplicationMethod(const FString& Method)
{
	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintApplicationMethod"), Method);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("Preview"), Preview);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonValue> MakeBodyWithUnknownOptimizationField()
{
	TSharedRef<FJsonObject> Optimization = MakeShared<FJsonObject>();
	Optimization->SetBoolField(TEXT("bUseMultiThreadedAnimationUpdate"), true);
	Optimization->SetBoolField(TEXT("bUnknownOptimizationFlag"), true);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("Optimization"), Optimization);
	return MakeShared<FJsonValueObject>(Body);
}

TSharedRef<FJsonObject> MakeAnimBlueprintApplyDocument(
	const FString& Target,
	const FString& ParentClassPath = TEXT("/Script/Engine.AnimInstance"),
	const FString& TargetSkeletonPath = TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh_Skeleton.DefaultSkeletalMesh_Skeleton"),
	const FString& PreviewMeshPath = TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh.DefaultSkeletalMesh"),
	bool bIsTemplate = false)
{
	TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimBlueprint"));
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), MakeClassRef(ParentClassPath));
	if (bIsTemplate)
	{
		Body->SetField(TEXT("TargetSkeleton"), MakeShared<FJsonValueNull>());
	}
	else
	{
		Body->SetObjectField(TEXT("TargetSkeleton"), MakeAssetRef(TargetSkeletonPath));
	}

	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetBoolField(TEXT("bIsTemplate"), bIsTemplate);
	Body->SetObjectField(TEXT("Template"), Template);

	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetObjectField(TEXT("PreviewSkeletalMesh"), MakeAssetRef(PreviewMeshPath));
	Preview->SetField(TEXT("PreviewAnimationBlueprint"), MakeShared<FJsonValueNull>());
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintApplicationMethod"), TEXT("LinkedLayers"));
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintTag"), TEXT(""));
	Body->SetObjectField(TEXT("Preview"), Preview);

	TSharedRef<FJsonObject> Optimization = MakeShared<FJsonObject>();
	Optimization->SetBoolField(TEXT("bUseMultiThreadedAnimationUpdate"), true);
	Optimization->SetBoolField(TEXT("bWarnAboutBlueprintUsage"), false);
	Optimization->SetBoolField(TEXT("bEnableLinkedAnimLayerInstanceSharing"), false);
	Body->SetObjectField(TEXT("Optimization"), Optimization);

	Body->SetArrayField(TEXT("SyncGroups"), {});
	Body->SetArrayField(TEXT("ImplementedInterfaces"), {});
	Body->SetArrayField(TEXT("Variables"), {});
	Body->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
	Body->SetArrayField(TEXT("UbergraphPages"), {});
	Body->SetArrayField(TEXT("AnimGraph"), {});
	Body->SetArrayField(TEXT("StateMachines"), {});
	Body->SetArrayField(TEXT("TransitionGraphs"), {});
	Body->SetArrayField(TEXT("AnimLayers"), {});
	Body->SetArrayField(TEXT("ParentAssetOverrides"), {});
	Document->SetObjectField(TEXT("Body"), Body);
	return Document;
}

bool HasDiagnostic(const FAssetDocumentCapabilityResult& Result, const FString& Path, const FString& Code)
{
	return Result.Diagnostics.ContainsByPredicate([&Path, &Code](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Path == Path && Diagnostic.Code == Code;
	});
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintDeferredGraphGatesTest,
	"AssetFactory.AssetDocument.AnimBlueprint.DeferredGraphGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintDeferredGraphGatesTest::RunTest(const FString&)
{
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UAnimBlueprint::StaticClass();
	const FAnimBlueprintAssetDocumentCapability Capability;

	for (const TCHAR* DeferredKey : {
		TEXT("AnimGraph"),
		TEXT("StateMachines"),
		TEXT("TransitionGraphs"),
		TEXT("AnimLayers"),
		TEXT("ParentAssetOverrides"),
	})
	{
		TestTrue(
			FString::Printf(TEXT("Body.%s accepts null while deferred"), DeferredKey),
			Capability.Validate(Context, MakeBodyWithNullRegion(DeferredKey)).bSuccess);
		TestTrue(
			FString::Printf(TEXT("Body.%s accepts empty array while deferred"), DeferredKey),
			Capability.Validate(Context, MakeBodyWithEmptyArrayRegion(DeferredKey)).bSuccess);
		TestTrue(
			FString::Printf(TEXT("Body.%s accepts empty object while deferred"), DeferredKey),
			Capability.Validate(Context, MakeBodyWithEmptyObjectRegion(DeferredKey)).bSuccess);

		const FAssetDocumentCapabilityResult NonEmptyResult =
			Capability.Validate(Context, MakeBodyWithNonEmptyDeferredRegion(DeferredKey));
		const FString ExpectedPath = FString::Printf(TEXT("/Body/%s"), DeferredKey);
		TestFalse(
			FString::Printf(TEXT("Body.%s rejects non-empty authored value while deferred"), DeferredKey),
			NonEmptyResult.bSuccess);
		TestTrue(
			FString::Printf(TEXT("Body.%s reports UnsupportedAnimBlueprintRegion at exact path"), DeferredKey),
			HasDiagnostic(NonEmptyResult, ExpectedPath, TEXT("UnsupportedAnimBlueprintRegion")));

		const FAssetDocumentCapabilityResult NonEmptyObjectResult =
			Capability.Validate(Context, MakeBodyWithNonEmptyDeferredObjectRegion(DeferredKey));
		TestFalse(
			FString::Printf(TEXT("Body.%s rejects non-empty object while deferred"), DeferredKey),
			NonEmptyObjectResult.bSuccess);
		TestTrue(
			FString::Printf(TEXT("Body.%s reports UnsupportedAnimBlueprintRegion for non-empty object"), DeferredKey),
			HasDiagnostic(NonEmptyObjectResult, ExpectedPath, TEXT("UnsupportedAnimBlueprintRegion")));

		const FAssetDocumentCapabilityResult ScalarResult =
			Capability.Validate(Context, MakeBodyWithScalarDeferredRegion(DeferredKey));
		TestFalse(
			FString::Printf(TEXT("Body.%s rejects scalar value while deferred"), DeferredKey),
			ScalarResult.bSuccess);
		TestTrue(
			FString::Printf(TEXT("Body.%s reports UnsupportedAnimBlueprintRegion for scalar"), DeferredKey),
			HasDiagnostic(ScalarResult, ExpectedPath, TEXT("UnsupportedAnimBlueprintRegion")));
	}

	const FAssetDocumentCapabilityResult UnknownKeyResult = Capability.Validate(Context, MakeBodyWithUnknownKey());
	TestFalse(TEXT("Unknown Body key rejects"), UnknownKeyResult.bSuccess);
	TestTrue(
		TEXT("Unknown Body key reports UnknownBodyKey"),
		HasDiagnostic(UnknownKeyResult, TEXT("/Body/UnexpectedGraph"), TEXT("UnknownBodyKey")));
	for (const TCHAR* UnknownBlueprintGraphKey : {TEXT("FunctionGraphs"), TEXT("MacroGraphs")})
	{
		const FAssetDocumentCapabilityResult UnknownBlueprintGraphResult =
			Capability.Validate(Context, MakeBodyWithUnknownKey(UnknownBlueprintGraphKey));
		TestFalse(
			FString::Printf(TEXT("Body.%s remains outside the ABP profile"), UnknownBlueprintGraphKey),
			UnknownBlueprintGraphResult.bSuccess);
		TestTrue(
			FString::Printf(TEXT("Body.%s reports UnknownBodyKey"), UnknownBlueprintGraphKey),
			HasDiagnostic(
				UnknownBlueprintGraphResult,
				FString::Printf(TEXT("/Body/%s"), UnknownBlueprintGraphKey),
				TEXT("UnknownBodyKey")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintCoreObjectRegionsTest,
	"AssetFactory.AssetDocument.AnimBlueprint.CoreObjectRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintCoreObjectRegionsTest::RunTest(const FString&)
{
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UAnimBlueprint::StaticClass();
	const FAnimBlueprintAssetDocumentCapability Capability;

	const FAssetDocumentCapabilityResult MissingParentClassResult =
		Capability.Validate(Context, MakeBodyWithMissingParentClass());
	TestFalse(TEXT("ParentClass missing Class rejects"), MissingParentClassResult.bSuccess);
	TestTrue(
		TEXT("ParentClass missing Class reports MissingParentClass"),
		HasDiagnostic(MissingParentClassResult, TEXT("/Body/ParentClass/Class"), TEXT("MissingParentClass")));

	const FAssetDocumentCapabilityResult InvalidParentClassResult =
		Capability.Validate(Context, MakeBodyWithParentClass(TEXT("/Script/Engine.Actor")));
	TestFalse(TEXT("ParentClass must be an AnimInstance child"), InvalidParentClassResult.bSuccess);
	TestTrue(
		TEXT("ParentClass reports InvalidAnimBlueprintParentClass"),
		HasDiagnostic(InvalidParentClassResult, TEXT("/Body/ParentClass/Class"), TEXT("InvalidAnimBlueprintParentClass")));

	const FAssetDocumentCapabilityResult InvalidTemplateSkeletonResult =
		Capability.Validate(Context, MakeBodyWithTemplateAndTargetSkeleton());
	TestFalse(TEXT("Template AnimBlueprint cannot author TargetSkeleton"), InvalidTemplateSkeletonResult.bSuccess);
	TestTrue(
		TEXT("Template skeleton conflict reports InvalidTemplateSkeleton"),
		HasDiagnostic(InvalidTemplateSkeletonResult, TEXT("/Body/TargetSkeleton"), TEXT("InvalidTemplateSkeleton"))
			|| HasDiagnostic(InvalidTemplateSkeletonResult, TEXT("/Body/Template/bIsTemplate"), TEXT("InvalidTemplateSkeleton")));

	const FAssetDocumentCapabilityResult InvalidPreviewMethodResult =
		Capability.Validate(Context, MakeBodyWithPreviewApplicationMethod(TEXT("Bogus")));
	TestFalse(TEXT("Preview rejects unknown application method"), InvalidPreviewMethodResult.bSuccess);
	TestTrue(
		TEXT("Preview method reports InvalidPreviewAnimationBlueprintApplicationMethod"),
		HasDiagnostic(
			InvalidPreviewMethodResult,
			TEXT("/Body/Preview/PreviewAnimationBlueprintApplicationMethod"),
			TEXT("InvalidPreviewAnimationBlueprintApplicationMethod")));

	const FAssetDocumentCapabilityResult UnknownOptimizationFieldResult =
		Capability.Validate(Context, MakeBodyWithUnknownOptimizationField());
	TestFalse(TEXT("Optimization rejects unknown object fields"), UnknownOptimizationFieldResult.bSuccess);
	TestTrue(
		TEXT("Optimization unknown field reports schema diagnostic"),
		HasDiagnostic(UnknownOptimizationFieldResult, TEXT("/Body/Optimization/bUnknownOptimizationFlag"), TEXT("UnknownObjectField")));

	const FAssetDocumentCapabilityResult UnknownBodyKeyResult = Capability.Validate(Context, MakeBodyWithUnknownKey());
	TestFalse(TEXT("Dispatcher still rejects unknown Body key"), UnknownBodyKeyResult.bSuccess);
	TestTrue(
		TEXT("Dispatcher preserves UnknownBodyKey code"),
		HasDiagnostic(UnknownBodyKeyResult, TEXT("/Body/UnexpectedGraph"), TEXT("UnknownBodyKey")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintCreateUpdateLifecycleTest,
	"AssetFactory.AssetDocument.AnimBlueprint.CreateUpdateLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintCreateUpdateLifecycleTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_Lifecycle_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
	const FString SkeletonPath = TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh_Skeleton.DefaultSkeletalMesh_Skeleton");
	const FString PreviewMeshPath = TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh.DefaultSkeletalMesh");

	FAssetDocumentService Service;
	FAssetDocumentApplyRequest Request;
	Request.Document = MakeAnimBlueprintApplyDocument(Target, TEXT("/Script/Engine.AnimInstance"), SkeletonPath, PreviewMeshPath, false);
	Request.bSaveAsset = false;

	const FAssetDocumentResult Result = Service.Apply(Request);
	if (!Result.IsSuccess())
	{
		AddError(FString::Printf(TEXT("AnimBlueprint apply failed: %s"), *Result.Message));
	}
	TestTrue(TEXT("AnimBlueprint apply succeeds"), Result.IsSuccess());

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
	TestNotNull(TEXT("Created asset is UAnimBlueprint"), AnimBlueprint);
	USkeleton* ExpectedSkeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	USkeletalMesh* ExpectedPreviewMesh = LoadObject<USkeletalMesh>(nullptr, *PreviewMeshPath);
	TestNotNull(TEXT("Expected skeleton asset loads"), ExpectedSkeleton);
	TestNotNull(TEXT("Expected preview mesh asset loads"), ExpectedPreviewMesh);
	if (AnimBlueprint)
	{
		TestEqual(TEXT("Parent class is AnimInstance"), AnimBlueprint->ParentClass.Get(), UAnimInstance::StaticClass());
		TestFalse(TEXT("Created AnimBlueprint is not a template"), AnimBlueprint->bIsTemplate);
		TestEqual(TEXT("TargetSkeleton is authored skeleton"), AnimBlueprint->TargetSkeleton.Get(), ExpectedSkeleton);
		TestEqual(TEXT("Preview mesh is authored mesh"), AnimBlueprint->GetPreviewMesh(), ExpectedPreviewMesh);
	}

	FAssetDocumentApplyRequest TemplateUpdateRequest;
	TemplateUpdateRequest.Document = MakeAnimBlueprintApplyDocument(Target, TEXT("/Script/Engine.AnimInstance"), SkeletonPath, PreviewMeshPath, true);
	TemplateUpdateRequest.bSaveAsset = false;
	const FAssetDocumentResult TemplateUpdateResult = Service.Apply(TemplateUpdateRequest);
	if (!TemplateUpdateResult.IsSuccess())
	{
		AddError(FString::Printf(TEXT("AnimBlueprint template update failed: %s"), *TemplateUpdateResult.Message));
	}
	TestTrue(TEXT("AnimBlueprint update succeeds"), TemplateUpdateResult.IsSuccess());
	if (AnimBlueprint)
	{
		TestTrue(TEXT("Updated AnimBlueprint is template"), AnimBlueprint->bIsTemplate);
		TestNull(TEXT("Template update clears TargetSkeleton"), AnimBlueprint->TargetSkeleton.Get());
	}

	const FString BadTarget = FString::Printf(TEXT("/Game/AssetDocumentTests/ABP_AD_Invalid_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FAssetDocumentApplyRequest BadRequest;
	BadRequest.Document = MakeAnimBlueprintApplyDocument(BadTarget, TEXT("/Script/Engine.Actor"), SkeletonPath, PreviewMeshPath, false);
	BadRequest.bSaveAsset = false;
	const FAssetDocumentResult BadResult = Service.Apply(BadRequest);
	TestFalse(TEXT("Invalid AnimBlueprint create preflight fails"), BadResult.IsSuccess());

	UObject* BadAsset = FindObject<UObject>(
		nullptr,
		*FString::Printf(TEXT("%s.%s"), *BadTarget, *FPackageName::GetLongPackageAssetName(BadTarget)));
	TestNull(TEXT("Invalid create does not leave a loadable asset"), BadAsset);
	return true;
}

#endif
