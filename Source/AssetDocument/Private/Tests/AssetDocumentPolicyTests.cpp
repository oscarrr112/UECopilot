// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentPolicy.h"

#include "AssetDocumentPolicyRegistry.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPolicyPresetExpansionTest,
	"AssetDocument.Policy.PresetExpansion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPolicyPresetExpansionTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionPolicyPreset Preset;
	TestTrue(TEXT("ManagedRegion preset is registered"), FAssetDocumentPolicyRegistry::GetBuiltinPreset(TEXT("ManagedRegion"), Preset));

	FAssetDocumentRegionPolicyOverride Override;
	Override.RegionId = TEXT("Body.CompositeSections");
	Override.BodyPath = TEXT("$.Body.CompositeSections");
	Override.RegionKind = EAssetDocumentRegionKind::Timeline;
	Override.ReducerMode = EAssetDocumentReducerMode::ManagedRegion;
	Override.ApplyMode = EAssetDocumentApplyMode::RebuildArrayRegion;
	Override.ManagedUePropertyPaths = {TEXT("CompositeSections")};
	Override.ExtensionHookName = TEXT("ApplyCompositeSections");

	FAssetDocumentRegionPolicy FirstPolicy;
	TestTrue(TEXT("Preset expands with an override"), FAssetDocumentPolicyRegistry::ExpandPreset(Preset, Override, FirstPolicy));

	FAssetDocumentRegionPolicy SecondPolicy;
	TestTrue(TEXT("Same preset and override expands again"), FAssetDocumentPolicyRegistry::ExpandPreset(Preset, Override, SecondPolicy));

	TestEqual(TEXT("RegionId is overridden"), FirstPolicy.RegionId, FName(TEXT("Body.CompositeSections")));
	TestEqual(TEXT("BodyPath is overridden"), FirstPolicy.BodyPath, FString(TEXT("$.Body.CompositeSections")));
	TestEqual(TEXT("RegionKind is overridden"), FirstPolicy.RegionKind, EAssetDocumentRegionKind::Timeline);
	TestEqual(TEXT("ReducerMode is overridden"), FirstPolicy.ReducerMode, EAssetDocumentReducerMode::ManagedRegion);
	TestEqual(TEXT("ApplyMode is overridden"), FirstPolicy.ApplyMode, EAssetDocumentApplyMode::RebuildArrayRegion);
	TestEqual(TEXT("ManagedUePropertyPaths is overridden"), FirstPolicy.ManagedUePropertyPaths.Num(), 1);
	if (FirstPolicy.ManagedUePropertyPaths.Num() == 1)
	{
		TestEqual(TEXT("ManagedUePropertyPaths preserves requested property path"), FirstPolicy.ManagedUePropertyPaths[0], FString(TEXT("CompositeSections")));
	}
	TestTrue(TEXT("ExtensionHookName is overridden"), FirstPolicy.ExtensionHookName.IsSet());
	if (FirstPolicy.ExtensionHookName.IsSet())
	{
		TestEqual(TEXT("ExtensionHookName preserves requested hook"), FirstPolicy.ExtensionHookName.GetValue(), FName(TEXT("ApplyCompositeSections")));
	}

	TestEqual(TEXT("Stable expansion keeps RegionId"), SecondPolicy.RegionId, FirstPolicy.RegionId);
	TestEqual(TEXT("Stable expansion keeps BodyPath"), SecondPolicy.BodyPath, FirstPolicy.BodyPath);
	TestEqual(TEXT("Stable expansion keeps ApplyMode"), SecondPolicy.ApplyMode, FirstPolicy.ApplyMode);
	TestEqual(TEXT("Stable expansion keeps ReducerMode"), SecondPolicy.ReducerMode, FirstPolicy.ReducerMode);
	TestEqual(TEXT("Stable expansion keeps ManagedUePropertyPaths count"), SecondPolicy.ManagedUePropertyPaths.Num(), FirstPolicy.ManagedUePropertyPaths.Num());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPolicyJsonExportTest,
	"AssetDocument.Policy.JsonExport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPolicyJsonExportTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionPolicy Policy;
	Policy.RegionId = TEXT("Body.Blend");
	Policy.BodyPath = TEXT("$.Body.Blend");
	Policy.RegionKind = EAssetDocumentRegionKind::Object;
	Policy.DefaultSource = EAssetDocumentDefaultSource::CDO;
	Policy.ReducerMode = EAssetDocumentReducerMode::DefaultDiff;
	Policy.ApplyMode = EAssetDocumentApplyMode::SetProperty;
	Policy.ManagedUePropertyPaths = {TEXT("BlendIn"), TEXT("BlendOut")};

	const TSharedRef<FJsonObject> Json = FAssetDocumentPolicyRegistry::ExportPolicyToJson(Policy);

	TestEqual(TEXT("JSON includes RegionId"), Json->GetStringField(TEXT("RegionId")), FString(TEXT("Body.Blend")));
	TestEqual(TEXT("JSON includes BodyPath"), Json->GetStringField(TEXT("BodyPath")), FString(TEXT("$.Body.Blend")));
	TestEqual(TEXT("JSON includes RegionKind"), Json->GetStringField(TEXT("RegionKind")), FString(TEXT("Object")));
	TestEqual(TEXT("JSON includes DefaultSource"), Json->GetStringField(TEXT("DefaultSource")), FString(TEXT("CDO")));
	TestEqual(TEXT("JSON includes ReducerMode"), Json->GetStringField(TEXT("ReducerMode")), FString(TEXT("DefaultDiff")));
	TestEqual(TEXT("JSON includes ApplyMode"), Json->GetStringField(TEXT("ApplyMode")), FString(TEXT("SetProperty")));

	const TArray<TSharedPtr<FJsonValue>>* ManagedPaths = nullptr;
	TestTrue(TEXT("JSON includes non-empty ManagedUePropertyPaths"), Json->TryGetArrayField(TEXT("ManagedUePropertyPaths"), ManagedPaths));
	if (ManagedPaths)
	{
		TestEqual(TEXT("JSON exports all managed paths"), ManagedPaths->Num(), 2);
	}

	TestFalse(TEXT("JSON omits empty IdentityRules"), Json->HasField(TEXT("IdentityRules")));
	TestFalse(TEXT("JSON omits empty ComparisonRules"), Json->HasField(TEXT("ComparisonRules")));
	TestFalse(TEXT("JSON omits empty ExtractOnlyFields"), Json->HasField(TEXT("ExtractOnlyFields")));
	TestFalse(TEXT("JSON omits empty ExplicitDeleteValues"), Json->HasField(TEXT("ExplicitDeleteValues")));
	TestFalse(TEXT("JSON omits unset ExtensionHookName"), Json->HasField(TEXT("ExtensionHookName")));
	TestFalse(TEXT("JSON omits preset expansion scratch fields"), Json->HasField(TEXT("PresetName")));

	return true;
}

#endif
