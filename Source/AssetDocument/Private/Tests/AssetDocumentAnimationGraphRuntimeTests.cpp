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
	Context.JsonPath = FAssetDocumentGraphFieldRules::MakeFieldJsonPath(
		TEXT("/Body/AnimGraph/Graphs/AnimGraph/Nodes/IdlePlayer/Fields"),
		FieldPath);
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
	ClassRef->SetStringField(TEXT("Path"), ClassPath);
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

	EAssetDocumentGraphFieldTrait ResolvedTrait = EAssetDocumentGraphFieldTrait::Raw;
	const FAssetDocumentGraphFieldRuleResult ResolveResult =
		FAssetDocumentGraphFieldRules::ResolveTrait(
			Context,
			{ EAssetDocumentGraphFieldTrait::Raw, EAssetDocumentGraphFieldTrait::AssetRef },
			MakeShared<FJsonValueObject>(MakeAssetRef(TEXT("/Game/DoesNotNeedToExistForShape"))),
			ResolvedTrait);
	TestTrue(TEXT("Trait resolution succeeds"), ResolveResult.bSuccess);
	TestEqual(TEXT("AssetRef trait is selected"), ResolvedTrait, EAssetDocumentGraphFieldTrait::AssetRef);
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

	EAssetDocumentGraphFieldTrait ResolvedTrait = EAssetDocumentGraphFieldTrait::Raw;
	const FAssetDocumentGraphFieldRuleResult AmbiguousResolveResult =
		FAssetDocumentGraphFieldRules::ResolveTrait(
			Context,
			{ EAssetDocumentGraphFieldTrait::Name, EAssetDocumentGraphFieldTrait::Enum },
			MakeShared<FJsonValueString>(TEXT("Idle")),
			ResolvedTrait);
	TestFalse(TEXT("Multiple matching candidate traits are rejected"), AmbiguousResolveResult.bSuccess);
	TestEqual(
		TEXT("Ambiguous candidate trait code is explicit"),
		AmbiguousResolveResult.Code,
		FString(TEXT("AmbiguousGraphFieldTrait")));
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

	FAssetDocumentGraphFieldRuleContext NullableContext = Context;
	NullableContext.bAllowNull = true;
	const FAssetDocumentGraphFieldRuleResult NullableResult =
		FAssetDocumentGraphFieldRules::ValidateTraitShape(
			NullableContext,
			EAssetDocumentGraphFieldTrait::AssetRef,
			MakeShared<FJsonValueNull>());
	TestTrue(TEXT("Nullable AssetRef accepts explicit null"), NullableResult.bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphFieldRulesClassRefRequiresPathTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.FieldRules.ClassRefRequiresPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphFieldRulesClassRefRequiresPathTest::RunTest(const FString&)
{
	const FAssetDocumentGraphFieldRuleContext Context = MakeFieldRuleContext(TEXT("Node.InstanceClass"));
	TSharedRef<FJsonObject> ClassRef = MakeShared<FJsonObject>();
	ClassRef->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ClassRef->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimInstance"));

	const FAssetDocumentGraphFieldRuleResult Result =
		FAssetDocumentGraphFieldRules::ValidateTraitShape(
			Context,
			EAssetDocumentGraphFieldTrait::ClassRef,
			MakeShared<FJsonValueObject>(ClassRef));

	TestFalse(TEXT("ClassRef.Class-only shape is rejected"), Result.bSuccess);
	TestEqual(TEXT("ClassRef requires canonical Path"), Result.Code, FString(TEXT("MissingGraphFieldClassRefPath")));
	TestEqual(
		TEXT("ClassRef diagnostic points at Path"),
		Result.Path,
		FString(TEXT("/Body/AnimGraph/Graphs/AnimGraph/Nodes/IdlePlayer/Fields/Node.InstanceClass/Path")));
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

	TestEqual(TEXT("Full apply stage barriers are exposed"), Stages.Num(), 9);
	if (Stages.Num() == 9)
	{
		TestEqual(TEXT("Validation happens first"), Stages[0], EAssetDocumentGraphFieldApplyStage::Validate);
		TestEqual(TEXT("Identity and pin-affecting fields apply second"), Stages[1], EAssetDocumentGraphFieldApplyStage::IdentityAndPins);
		TestEqual(TEXT("Dynamic pins reconstruct after identity fields"), Stages[2], EAssetDocumentGraphFieldApplyStage::ReconstructDynamicPins);
		TestEqual(TEXT("Regular reflected fields apply after reconstruction"), Stages[3], EAssetDocumentGraphFieldApplyStage::Fields);
		TestEqual(TEXT("Pin defaults apply after reflected fields"), Stages[4], EAssetDocumentGraphFieldApplyStage::PinDefaults);
		TestEqual(TEXT("Layout applies after pin defaults"), Stages[5], EAssetDocumentGraphFieldApplyStage::Layout);
		TestEqual(TEXT("Links apply after layout"), Stages[6], EAssetDocumentGraphFieldApplyStage::Links);
		TestEqual(TEXT("Repair runs after links"), Stages[7], EAssetDocumentGraphFieldApplyStage::Repair);
		TestEqual(TEXT("Post-apply evidence is last"), Stages[8], EAssetDocumentGraphFieldApplyStage::PostApplyEvidence);
	}
	return true;
}

#endif
