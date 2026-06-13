// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentSidecarDelta.h"
#include "AssetDocumentPolicy.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
FAssetDocumentRegionPolicy MakePolicy(const FString& BodyPath, EAssetDocumentApplyMode ApplyMode = EAssetDocumentApplyMode::SetProperty)
{
	FAssetDocumentRegionPolicy Policy;
	Policy.RegionId = FName(*BodyPath);
	Policy.BodyPath = BodyPath;
	Policy.ApplyMode = ApplyMode;
	return Policy;
}

TSharedPtr<FJsonValue> MakeObjectValue(TSharedRef<FJsonObject> Object)
{
	return MakeShared<FJsonValueObject>(Object);
}

TSharedPtr<FJsonValue> MakeArrayValue(TArray<TSharedPtr<FJsonValue>> Values)
{
	return MakeShared<FJsonValueArray>(MoveTemp(Values));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarDeltaMissingRegionMeansUnsetTest,
	"AssetDocument.SidecarDelta.MissingRegionMeansUnset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarDeltaMissingRegionMeansUnsetTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetObjectField(TEXT("Body"), MakeShared<FJsonObject>());

	const FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Body.Notifies"), EAssetDocumentApplyMode::RebuildArrayRegion);
	const FAssetDocumentSidecarRegionValue Region = FAssetDocumentSidecarDelta::FindRegionValue(Document, Policy);

	TestEqual(TEXT("Missing region is unset"), Region.State, EAssetDocumentSidecarRegionState::Unset);
	TestFalse(TEXT("Missing region has no value"), Region.Value.IsValid());
	TestEqual(TEXT("Unset region hash is empty"), FAssetDocumentSidecarDelta::HashSidecarRegion(Document, Policy), FString());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarDeltaEmptyArrayIsExplicitTest,
	"AssetDocument.SidecarDelta.EmptyArrayIsExplicit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarDeltaEmptyArrayIsExplicitTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("Notifies"), {});

	TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetObjectField(TEXT("Body"), Body);

	const FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Body.Notifies"), EAssetDocumentApplyMode::RebuildArrayRegion);
	const FAssetDocumentSidecarRegionValue Region = FAssetDocumentSidecarDelta::FindRegionValue(Document, Policy);

	TestEqual(TEXT("Empty managed array is explicit empty"), Region.State, EAssetDocumentSidecarRegionState::ExplicitEmpty);
	TestTrue(TEXT("Empty managed array value remains valid"), Region.Value.IsValid() && Region.Value->Type == EJson::Array);
	TestTrue(TEXT("IsExplicitEmptyRegion accepts managed empty array"), FAssetDocumentSidecarDelta::IsExplicitEmptyRegion(Region.Value, Policy));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarDeltaHashIgnoresSkippedTest,
	"AssetDocument.SidecarDelta.HashIgnoresSkipped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarDeltaHashIgnoresSkippedTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> BaselineNotify = MakeShared<FJsonObject>();
	BaselineNotify->SetStringField(TEXT("NotifyName"), TEXT("ManagedNotify"));
	BaselineNotify->SetNumberField(TEXT("Frame"), 12.0);

	TSharedRef<FJsonObject> BaselineBody = MakeShared<FJsonObject>();
	BaselineBody->SetArrayField(TEXT("Notifies"), { MakeObjectValue(BaselineNotify) });
	TSharedRef<FJsonObject> BaselineDocument = MakeShared<FJsonObject>();
	BaselineDocument->SetObjectField(TEXT("Body"), BaselineBody);

	TSharedRef<FJsonObject> WithSkippedNotify = MakeShared<FJsonObject>();
	WithSkippedNotify->SetStringField(TEXT("NotifyName"), TEXT("ManagedNotify"));
	WithSkippedNotify->SetNumberField(TEXT("Frame"), 12.0);
	WithSkippedNotify->SetObjectField(TEXT("_Skipped"), MakeShared<FJsonObject>());
	WithSkippedNotify->SetStringField(TEXT("_ProjectionMetrics"), TEXT("diagnostic"));

	TSharedRef<FJsonObject> WithSkippedBody = MakeShared<FJsonObject>();
	WithSkippedBody->SetArrayField(TEXT("Notifies"), { MakeObjectValue(WithSkippedNotify) });
	TSharedRef<FJsonObject> WithSkippedDocument = MakeShared<FJsonObject>();
	WithSkippedDocument->SetObjectField(TEXT("Body"), WithSkippedBody);

	FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Body.Notifies"), EAssetDocumentApplyMode::RebuildArrayRegion);
	Policy.ExtractOnlyFields.Add(TEXT("_ProjectionMetrics"));

	TestEqual(
		TEXT("Region hash ignores canonical extract-only fields"),
		FAssetDocumentSidecarDelta::HashSidecarRegion(BaselineDocument, Policy),
		FAssetDocumentSidecarDelta::HashSidecarRegion(WithSkippedDocument, Policy));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarDeltaSetCreatesIntermediateObjectTest,
	"AssetDocument.SidecarDelta.SetCreatesIntermediateObject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarDeltaSetCreatesIntermediateObjectTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	const FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Body.Blend"));

	FString Error;
	TestTrue(
		TEXT("SetRegionValue creates missing intermediate objects"),
		FAssetDocumentSidecarDelta::SetRegionValue(Document, Policy, MakeShared<FJsonValueString>(TEXT("Linear")), Error));
	TestTrue(TEXT("SetRegionValue leaves error empty on success"), Error.IsEmpty());

	const FAssetDocumentSidecarRegionValue Region = FAssetDocumentSidecarDelta::FindRegionValue(Document, Policy);
	TestEqual(TEXT("Created scalar region is present"), Region.State, EAssetDocumentSidecarRegionState::Present);
	TestTrue(TEXT("Created scalar region keeps value"), Region.Value.IsValid() && Region.Value->AsString() == TEXT("Linear"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarDeltaSetRejectsNonObjectIntermediateTest,
	"AssetDocument.SidecarDelta.SetRejectsNonObjectIntermediate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarDeltaSetRejectsNonObjectIntermediateTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetStringField(TEXT("Body"), TEXT("not-an-object"));

	const FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Body.Blend"));

	FString Error;
	TestFalse(
		TEXT("SetRegionValue rejects non-object intermediate path"),
		FAssetDocumentSidecarDelta::SetRegionValue(Document, Policy, MakeShared<FJsonValueString>(TEXT("Linear")), Error));
	TestFalse(TEXT("SetRegionValue reports non-object path error"), Error.IsEmpty());
	TestEqual(TEXT("SetRegionValue does not overwrite non-object intermediate"), Document->GetStringField(TEXT("Body")), FString(TEXT("not-an-object")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarDeltaReadsScalarObjectArrayRegionsTest,
	"AssetDocument.SidecarDelta.ReadsScalarObjectArrayRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarDeltaReadsScalarObjectArrayRegionsTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> BlendObject = MakeShared<FJsonObject>();
	BlendObject->SetNumberField(TEXT("BlendInTime"), 0.25);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("Mode"), TEXT("Preview"));
	Body->SetObjectField(TEXT("Blend"), BlendObject);
	Body->SetArrayField(TEXT("Notifies"), { MakeShared<FJsonValueString>(TEXT("NotifyA")) });

	TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetObjectField(TEXT("Body"), Body);

	TestEqual(TEXT("Scalar region is present"), FAssetDocumentSidecarDelta::FindRegionValue(Document, MakePolicy(TEXT("Body.Mode"))).State, EAssetDocumentSidecarRegionState::Present);
	TestEqual(TEXT("Object region is present"), FAssetDocumentSidecarDelta::FindRegionValue(Document, MakePolicy(TEXT("Body.Blend"))).State, EAssetDocumentSidecarRegionState::Present);
	TestEqual(TEXT("Array region is present"), FAssetDocumentSidecarDelta::FindRegionValue(Document, MakePolicy(TEXT("Body.Notifies"))).State, EAssetDocumentSidecarRegionState::Present);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarDeltaEmptySentinelsRequirePolicyTest,
	"AssetDocument.SidecarDelta.EmptySentinelsRequirePolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarDeltaEmptySentinelsRequirePolicyTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonValue> NullValue = MakeShared<FJsonValueNull>();
	TSharedPtr<FJsonValue> EmptyObjectValue = MakeObjectValue(MakeShared<FJsonObject>());

	FAssetDocumentRegionPolicy DefaultPolicy = MakePolicy(TEXT("Body.Blend"));
	TestFalse(TEXT("Null is not explicit empty without policy sentinel"), FAssetDocumentSidecarDelta::IsExplicitEmptyRegion(NullValue, DefaultPolicy));
	TestFalse(TEXT("Empty object is not explicit empty without policy sentinel"), FAssetDocumentSidecarDelta::IsExplicitEmptyRegion(EmptyObjectValue, DefaultPolicy));

	FAssetDocumentRegionPolicy SentinelPolicy = DefaultPolicy;
	SentinelPolicy.ExplicitDeleteValues.Add(TEXT("null"));
	SentinelPolicy.ExplicitDeleteValues.Add(TEXT("empty_object"));
	TestTrue(TEXT("Null is explicit empty with policy sentinel"), FAssetDocumentSidecarDelta::IsExplicitEmptyRegion(NullValue, SentinelPolicy));
	TestTrue(TEXT("Empty object is explicit empty with policy sentinel"), FAssetDocumentSidecarDelta::IsExplicitEmptyRegion(EmptyObjectValue, SentinelPolicy));

	return true;
}

#endif
