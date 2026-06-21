// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentRegionCanonicalizer.h"
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerIdentityHashMatchesCanonicalJsonTest,
	"AssetDocument.RegionCanonicalizer.IdentityHashMatchesCanonicalJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerIdentityHashMatchesCanonicalJsonTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionPolicy Policy;
	Policy.RegionId = TEXT("Body.Blend");
	Policy.BodyPath = TEXT("Body.Blend");
	Policy.ExtractOnlyFields.Add(TEXT("_ProjectionMetrics"));

	TSharedRef<FJsonObject> BaselineObject = MakeShared<FJsonObject>();
	BaselineObject->SetNumberField(TEXT("Value"), 1.0);

	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("Value"), 1.0);
	Object->SetStringField(TEXT("_ProjectionMetrics"), TEXT("diagnostic"));
	Object->SetStringField(TEXT("_meta"), TEXT("extract diagnostics"));

	FAssetDocumentRegionCanonicalizeContext Context;
	Context.Policy = &Policy;
	Context.Source = EAssetDocumentRegionCanonicalizeSource::SidecarAuthored;

	TestEqual(
		TEXT("Identity canonicalizer keeps canonical json hash"),
		FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, MakeObjectValue(Object)),
		FAssetDocumentCanonicalJson::HashJsonValue(MakeObjectValue(Object), &Policy));
	TestEqual(
		TEXT("Identity canonicalizer ignores extract-only fields"),
		FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, MakeObjectValue(Object)),
		FAssetDocumentCanonicalJson::HashJsonValue(MakeObjectValue(BaselineObject), &Policy));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerWritebackKeepsAuthoredShapeTest,
	"AssetDocument.RegionCanonicalizer.WritebackKeepsAuthoredShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerWritebackKeepsAuthoredShapeTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionPolicy Policy;
	Policy.RegionId = TEXT("Body.Blend");
	Policy.BodyPath = TEXT("Body.Blend");

	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("Value"), 1.0);
	Object->SetStringField(TEXT("_meta"), TEXT("diagnostic"));

	FAssetDocumentRegionCanonicalizeContext Context;
	Context.Policy = &Policy;
	Context.Source = EAssetDocumentRegionCanonicalizeSource::AssetEvidence;

	const TSharedPtr<FJsonValue> Writeback = FAssetDocumentRegionCanonicalizer::CanonicalizeForSidecarWriteback(Context, MakeObjectValue(Object));
	TestTrue(TEXT("Writeback remains object"), Writeback.IsValid() && Writeback->Type == EJson::Object);
	if (Writeback.IsValid() && Writeback->Type == EJson::Object)
	{
		TestTrue(TEXT("Writeback preserves ordinary value"), Writeback->AsObject()->HasField(TEXT("Value")));
		TestEqual(TEXT("Writeback preserves authored value"), Writeback->AsObject()->GetNumberField(TEXT("Value")), 1.0);
	}

	return true;
}

#endif
