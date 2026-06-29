// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentJsonRegionUtils.h"

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
	FAssetDocumentRegionRuntimeJsonUtilsRequireObjectTest,
	"AssetFactory.AssetDocument.RegionRuntime.JsonUtils.RequireObject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeJsonUtilsRequireObjectTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> OutObject;
	TSharedRef<FJsonObject> SourceObject = MakeShared<FJsonObject>();
	SourceObject->SetStringField(TEXT("Name"), TEXT("Preview"));

	FAssetDocumentCapabilityResult Result =
		FAssetDocumentJsonRegionUtils::RequireObjectValue(MakeObjectValue(SourceObject), TEXT("/Body/Preview"), OutObject);
	TestTrue(TEXT("Object value succeeds"), Result.bSuccess);
	TestTrue(TEXT("Object value is returned"), OutObject == SourceObject);

	Result = FAssetDocumentJsonRegionUtils::RequireObjectValue(
		MakeShared<FJsonValueString>(TEXT("not-object")),
		TEXT("/Body/Preview"),
		OutObject);
	TestFalse(TEXT("String value fails object requirement"), Result.bSuccess);
	TestEqual(TEXT("Failure path uses supplied JSON pointer"), Result.Diagnostics[0].Path, FString(TEXT("/Body/Preview")));
	TestEqual(TEXT("Failure code marks invalid section type"), Result.Diagnostics[0].Code, FString(TEXT("InvalidBodySectionType")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeJsonUtilsRequireArrayTest,
	"AssetFactory.AssetDocument.RegionRuntime.JsonUtils.RequireArray",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeJsonUtilsRequireArrayTest::RunTest(const FString& Parameters)
{
	const TArray<TSharedPtr<FJsonValue>>* OutArray = nullptr;
	FAssetDocumentCapabilityResult Result = FAssetDocumentJsonRegionUtils::RequireArrayValue(
		MakeArrayValue({MakeShared<FJsonValueString>(TEXT("A"))}),
		TEXT("/Body/Curves"),
		OutArray);
	TestTrue(TEXT("Array value succeeds"), Result.bSuccess);
	TestTrue(TEXT("Array pointer is returned"), OutArray && OutArray->Num() == 1);

	Result = FAssetDocumentJsonRegionUtils::RequireArrayValue(
		MakeObjectValue(MakeShared<FJsonObject>()),
		TEXT("/Body/Curves"),
		OutArray);
	TestFalse(TEXT("Object value fails array requirement"), Result.bSuccess);
	TestEqual(TEXT("Failure path uses supplied JSON pointer"), Result.Diagnostics[0].Path, FString(TEXT("/Body/Curves")));
	TestEqual(TEXT("Failure message describes array requirement"), Result.Message, FString(TEXT("Expected a JSON array")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeJsonUtilsJsonPointerEscapingTest,
	"AssetFactory.AssetDocument.RegionRuntime.JsonUtils.JsonPointerEscaping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeJsonUtilsJsonPointerEscapingTest::RunTest(const FString& Parameters)
{
	TestEqual(
		TEXT("Escapes JSON pointer slash and tilde tokens"),
		FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(TEXT("Curve/Name~LOD")),
		FString(TEXT("Curve~1Name~0LOD")));
	TestEqual(
		TEXT("Body path escapes body key token"),
		FAssetDocumentJsonRegionUtils::MakeBodyPath(TEXT("Graph/With~Token")),
		FString(TEXT("/Body/Graph~1With~0Token")));
	TestEqual(
		TEXT("Body array item path appends numeric token"),
		FAssetDocumentJsonRegionUtils::MakeBodyArrayItemPath(TEXT("Curves"), 7),
		FString(TEXT("/Body/Curves/7")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeJsonUtilsDiffEntryShapeTest,
	"AssetFactory.AssetDocument.RegionRuntime.JsonUtils.DiffEntryShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeJsonUtilsDiffEntryShapeTest::RunTest(const FString& Parameters)
{
	TArray<TSharedPtr<FJsonValue>> Entries;
	FAssetDocumentJsonRegionUtils::AddDiffEntry(
		Entries,
		TEXT("/Body/Preview"),
		TEXT("changed"),
		MakeShared<FJsonValueString>(TEXT("Before")),
		MakeShared<FJsonValueString>(TEXT("After")));

	TestEqual(TEXT("One diff entry is added"), Entries.Num(), 1);
	TestTrue(TEXT("Entry is a JSON object"), Entries[0].IsValid() && Entries[0]->Type == EJson::Object);
	const TSharedPtr<FJsonObject> EntryObject = Entries[0]->AsObject();
	TestEqual(TEXT("Entry path field"), EntryObject->GetStringField(TEXT("path")), FString(TEXT("/Body/Preview")));
	TestEqual(TEXT("Entry status field"), EntryObject->GetStringField(TEXT("status")), FString(TEXT("changed")));
	TestEqual(TEXT("Entry current field"), EntryObject->GetStringField(TEXT("current")), FString(TEXT("Before")));
	TestEqual(TEXT("Entry desired field"), EntryObject->GetStringField(TEXT("desired")), FString(TEXT("After")));

	TSharedRef<FJsonObject> FirstObject = MakeShared<FJsonObject>();
	FirstObject->SetNumberField(TEXT("B"), 2.0);
	FirstObject->SetNumberField(TEXT("A"), 1.0);
	TSharedRef<FJsonObject> SecondObject = MakeShared<FJsonObject>();
	SecondObject->SetNumberField(TEXT("A"), 1.0);
	SecondObject->SetNumberField(TEXT("B"), 2.0);
	TestEqual(
		TEXT("Comparable JSON ignores object field insertion order"),
		FAssetDocumentJsonRegionUtils::JsonValueToComparableString(MakeObjectValue(FirstObject)),
		FAssetDocumentJsonRegionUtils::JsonValueToComparableString(MakeObjectValue(SecondObject)));

	return true;
}

#endif
