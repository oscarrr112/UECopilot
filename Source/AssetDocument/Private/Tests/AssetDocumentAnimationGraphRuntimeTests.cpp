// Copyright ProjectRPG. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Graphs/AssetDocumentGraphFieldRules.h"

#include "Animation/AnimationAsset.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

namespace
{
FAssetDocumentGraphFieldRuleContext MakeFieldRuleContext(const FString& FieldPath)
{
	FAssetDocumentGraphFieldRuleContext Context;
	Context.JsonPath = FString::Printf(
		TEXT("/Body/AnimGraph/Graphs/AnimGraph/Nodes/IdlePlayer/Fields/%s"),
		*FAssetDocumentGraphFieldRules::MakeJsonPathToken(FieldPath));
	Context.FieldPath = FieldPath;
	Context.OwnerGraphKind = TEXT("AnimGraph");
	Context.RequiredObjectClass = UAnimationAsset::StaticClass();
	return Context;
}

TSharedRef<FJsonObject> MakeAssetRef(const FString& Path)
{
	TSharedRef<FJsonObject> AssetRef = MakeShared<FJsonObject>();
	AssetRef->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	AssetRef->SetStringField(TEXT("Path"), Path);
	return AssetRef;
}

TSharedRef<FJsonObject> MakeClassRef(const FString& ClassPath)
{
	TSharedRef<FJsonObject> ClassRef = MakeShared<FJsonObject>();
	ClassRef->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ClassRef->SetStringField(TEXT("Class"), ClassPath);
	return ClassRef;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphFieldRulesTraitResolutionTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.FieldRules.TraitResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphFieldRulesTraitResolutionTest::RunTest(const FString&)
{
	const FAssetDocumentGraphFieldRuleContext Context = MakeFieldRuleContext(TEXT("Node.Sequence"));

	const FAssetDocumentGraphFieldRuleResult AssetResult =
		FAssetDocumentGraphFieldRules::ValidateTraitShape(
			Context,
			EAssetDocumentGraphFieldTrait::AssetRef,
			MakeShared<FJsonValueObject>(MakeAssetRef(TEXT("/Game/DoesNotNeedToExistForShape"))));

	TestTrue(TEXT("AssetRef trait shape validates"), AssetResult.bSuccess);

	const FAssetDocumentGraphFieldRuleResult ClassResult =
		FAssetDocumentGraphFieldRules::ValidateTraitShape(
			Context,
			EAssetDocumentGraphFieldTrait::ClassRef,
			MakeShared<FJsonValueObject>(MakeClassRef(TEXT("/Script/Engine.AnimInstance"))));

	TestTrue(TEXT("ClassRef trait shape validates"), ClassResult.bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphFieldRulesAmbiguousTraitTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.FieldRules.AmbiguousTrait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphFieldRulesAmbiguousTraitTest::RunTest(const FString&)
{
	const FAssetDocumentGraphFieldRuleContext Context = MakeFieldRuleContext(TEXT("Node.Sequence"));
	const FAssetDocumentGraphFieldRuleResult Result =
		FAssetDocumentGraphFieldRules::ValidateTraitShape(
			Context,
			EAssetDocumentGraphFieldTrait::Raw,
			MakeShared<FJsonValueObject>(MakeAssetRef(TEXT("/Game/Idle"))));

	TestFalse(TEXT("Semantic object cannot silently fall back to raw"), Result.bSuccess);
	TestEqual(TEXT("Ambiguous trait code is explicit"), Result.Code, FString(TEXT("AmbiguousGraphFieldTrait")));
	TestEqual(TEXT("Ambiguous trait path points at Kind"), Result.Path, FString(TEXT("/Body/AnimGraph/Graphs/AnimGraph/Nodes/IdlePlayer/Fields/Node.Sequence/Kind")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphFieldRulesAssetRefRejectsRawStringTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.FieldRules.AssetRefRejectsRawString",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphFieldRulesAssetRefRejectsRawStringTest::RunTest(const FString&)
{
	const FAssetDocumentGraphFieldRuleContext Context = MakeFieldRuleContext(TEXT("Node.Sequence"));
	const FAssetDocumentGraphFieldRuleResult Result =
		FAssetDocumentGraphFieldRules::ValidateTraitShape(
			Context,
			EAssetDocumentGraphFieldTrait::AssetRef,
			MakeShared<FJsonValueString>(TEXT("/Game/Idle")));

	TestFalse(TEXT("AssetRef rejects raw string paths"), Result.bSuccess);
	TestEqual(TEXT("AssetRef raw string failure code"), Result.Code, FString(TEXT("InvalidGraphFieldAssetRef")));
	TestEqual(TEXT("AssetRef raw string failure path"), Result.Path, Context.JsonPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphFieldRulesDefaultOmissionTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.FieldRules.DefaultOmission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphFieldRulesDefaultOmissionTest::RunTest(const FString&)
{
	TestTrue(
		TEXT("Primitive default values can be omitted"),
		FAssetDocumentGraphFieldRules::ShouldOmitDefaultField(
			MakeShared<FJsonValueNumber>(1.0),
			MakeShared<FJsonValueNumber>(1.0),
			false));

	TestFalse(
		TEXT("Identity or pin-affecting defaults stay authored"),
		FAssetDocumentGraphFieldRules::ShouldOmitDefaultField(
			MakeShared<FJsonValueString>(TEXT("Pose")),
			MakeShared<FJsonValueString>(TEXT("Pose")),
			true));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphFieldRulesJsonPointerEscapingTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.FieldRules.JsonPointerEscaping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphFieldRulesJsonPointerEscapingTest::RunTest(const FString&)
{
	FAssetDocumentGraphFieldRuleContext Context = MakeFieldRuleContext(TEXT("Node.Groups/A~B"));

	TSharedRef<FJsonObject> AssetRef = MakeAssetRef(TEXT("/Game/Idle"));
	AssetRef->RemoveField(TEXT("Path"));
	const FAssetDocumentGraphFieldRuleResult Result =
		FAssetDocumentGraphFieldRules::ValidateTraitShape(
			Context,
			EAssetDocumentGraphFieldTrait::AssetRef,
			MakeShared<FJsonValueObject>(AssetRef));

	TestFalse(TEXT("Missing AssetRef path fails"), Result.bSuccess);
	TestEqual(
		TEXT("Diagnostic path uses JSON pointer escaping"),
		Result.Path,
		FString(TEXT("/Body/AnimGraph/Graphs/AnimGraph/Nodes/IdlePlayer/Fields/Node.Groups~1A~0B/Path")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphFieldRulesStagedApplyOrderTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.FieldRules.StagedApplyOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphFieldRulesStagedApplyOrderTest::RunTest(const FString&)
{
	const TArray<EAssetDocumentGraphFieldApplyStage> Stages = FAssetDocumentGraphFieldRules::GetStagedApplyOrder();

	TestEqual(TEXT("Three apply stages are exposed"), Stages.Num(), 3);
	if (Stages.Num() == 3)
	{
		TestEqual(TEXT("Validation happens first"), Stages[0], EAssetDocumentGraphFieldApplyStage::Validate);
		TestEqual(TEXT("Identity and pin-affecting fields apply second"), Stages[1], EAssetDocumentGraphFieldApplyStage::IdentityAndPins);
		TestEqual(TEXT("Regular reflected fields apply last"), Stages[2], EAssetDocumentGraphFieldApplyStage::Fields);
	}
	return true;
}

#endif
