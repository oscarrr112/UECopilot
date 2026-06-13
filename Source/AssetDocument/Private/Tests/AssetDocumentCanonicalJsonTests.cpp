// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentCanonicalJson.h"
#include "AssetDocumentPolicy.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
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
	FAssetDocumentCanonicalJsonObjectOrderStableTest,
	"AssetDocument.CanonicalJson.ObjectOrderStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentCanonicalJsonObjectOrderStableTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> FirstObject = MakeShared<FJsonObject>();
	FirstObject->SetNumberField(TEXT("A"), 1.0);
	FirstObject->SetNumberField(TEXT("B"), 2.0);

	TSharedRef<FJsonObject> SecondObject = MakeShared<FJsonObject>();
	SecondObject->SetNumberField(TEXT("B"), 2.0);
	SecondObject->SetNumberField(TEXT("A"), 1.0);

	const FString FirstHash = FAssetDocumentCanonicalJson::HashJsonValue(MakeObjectValue(FirstObject));
	const FString SecondHash = FAssetDocumentCanonicalJson::HashJsonValue(MakeObjectValue(SecondObject));

	TestTrue(TEXT("Hash uses an explicit algorithm prefix"), FirstHash.StartsWith(TEXT("sha1:")));
	TestEqual(TEXT("Object key order does not change hash"), FirstHash, SecondHash);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentCanonicalJsonArrayOrderMattersTest,
	"AssetDocument.CanonicalJson.ArrayOrderMatters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentCanonicalJsonArrayOrderMattersTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonValue> FirstArray = MakeArrayValue({
		MakeShared<FJsonValueNumber>(1.0),
		MakeShared<FJsonValueNumber>(2.0)
	});
	const TSharedPtr<FJsonValue> SecondArray = MakeArrayValue({
		MakeShared<FJsonValueNumber>(2.0),
		MakeShared<FJsonValueNumber>(1.0)
	});

	TestNotEqual(
		TEXT("Array item order changes hash"),
		FAssetDocumentCanonicalJson::HashJsonValue(FirstArray),
		FAssetDocumentCanonicalJson::HashJsonValue(SecondArray));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentCanonicalJsonExtractOnlyFieldsIgnoredTest,
	"AssetDocument.CanonicalJson.ExtractOnlyFieldsIgnored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentCanonicalJsonExtractOnlyFieldsIgnoredTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> BaselineObject = MakeShared<FJsonObject>();
	BaselineObject->SetStringField(TEXT("Name"), TEXT("ManagedNotify"));
	BaselineObject->SetNumberField(TEXT("Frame"), 12.0);

	TSharedRef<FJsonObject> WithExtractOnlyObject = MakeShared<FJsonObject>();
	WithExtractOnlyObject->SetStringField(TEXT("Name"), TEXT("ManagedNotify"));
	WithExtractOnlyObject->SetNumberField(TEXT("Frame"), 12.0);
	WithExtractOnlyObject->SetObjectField(TEXT("_Skipped"), MakeShared<FJsonObject>());
	WithExtractOnlyObject->SetStringField(TEXT("_meta"), TEXT("extract diagnostics"));
	WithExtractOnlyObject->SetStringField(TEXT("_ProjectionMetrics"), TEXT("diagnostic"));

	FAssetDocumentRegionPolicy Policy;
	Policy.ExtractOnlyFields.Add(TEXT("_ProjectionMetrics"));

	const FString BaselineHash = FAssetDocumentCanonicalJson::HashJsonValue(MakeObjectValue(BaselineObject), &Policy);
	const FString ExtractOnlyHash = FAssetDocumentCanonicalJson::HashJsonValue(MakeObjectValue(WithExtractOnlyObject), &Policy);
	TestEqual(TEXT("Default and policy extract-only fields do not change hash"), BaselineHash, ExtractOnlyHash);

	TSharedPtr<FJsonValue> Clone = FAssetDocumentCanonicalJson::CloneWithoutExtractOnlyFields(MakeObjectValue(WithExtractOnlyObject), &Policy);
	TestTrue(TEXT("Clone remains an object"), Clone.IsValid() && Clone->Type == EJson::Object);
	if (Clone.IsValid() && Clone->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> CloneObject = Clone->AsObject();
		TestFalse(TEXT("Clone removes _Skipped"), CloneObject->HasField(TEXT("_Skipped")));
		TestFalse(TEXT("Clone removes _meta"), CloneObject->HasField(TEXT("_meta")));
		TestFalse(TEXT("Clone removes policy ExtractOnlyFields"), CloneObject->HasField(TEXT("_ProjectionMetrics")));
		TestTrue(TEXT("Clone keeps ordinary fields"), CloneObject->HasField(TEXT("Name")));
	}

	TSharedRef<FJsonObject> ChangedObject = MakeShared<FJsonObject>();
	ChangedObject->SetStringField(TEXT("Name"), TEXT("ManagedNotify"));
	ChangedObject->SetNumberField(TEXT("Frame"), 13.0);
	ChangedObject->SetObjectField(TEXT("_Skipped"), MakeShared<FJsonObject>());
	ChangedObject->SetStringField(TEXT("_meta"), TEXT("extract diagnostics"));
	ChangedObject->SetStringField(TEXT("_ProjectionMetrics"), TEXT("diagnostic"));

	TestNotEqual(
		TEXT("Ordinary field changes still change hash"),
		BaselineHash,
		FAssetDocumentCanonicalJson::HashJsonValue(MakeObjectValue(ChangedObject), &Policy));

	return true;
}

#endif
