// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentSyncStateStore.h"

#include "AssetDocumentSidecar.h"
#include "AssetDocumentSidecarDelta.h"
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

FAssetDocumentRegionPolicy MakePolicy(const FString& BodyPath)
{
	FAssetDocumentRegionPolicy Policy;
	Policy.RegionId = FName(*BodyPath);
	Policy.BodyPath = BodyPath;
	return Policy;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSyncStateLoadEmptyTest,
	"AssetDocument.SyncState.LoadEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSyncStateLoadEmptyTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetStringField(TEXT("Target"), TEXT("/Game/AssetDocumentTest/M_Test"));

	FAssetDocumentSyncState State;
	FString Error;
	TestTrue(TEXT("Missing _meta.sync loads as default state"), FAssetDocumentSyncStateStore::LoadFromDocumentJson(Document, State, Error));
	TestTrue(TEXT("Missing _meta.sync leaves error empty"), Error.IsEmpty());
	TestEqual(TEXT("Default schema version is v1"), State.SchemaVersion, 1);
	TestTrue(TEXT("Default asset object path is empty"), State.AssetObjectPath.IsEmpty());
	TestTrue(TEXT("Default package guid is empty"), State.AssetPackageGuid.IsEmpty());
	TestTrue(TEXT("Default updated-at is empty"), State.UpdatedAtUtc.IsEmpty());
	TestEqual(TEXT("Default sync state has no regions"), State.Regions.Num(), 0);

	Document->SetStringField(TEXT("_meta"), TEXT("legacy diagnostic"));
	State.AssetObjectPath = TEXT("stale");
	Error = TEXT("stale error");
	TestTrue(TEXT("Legacy non-object _meta loads as default state"), FAssetDocumentSyncStateStore::LoadFromDocumentJson(Document, State, Error));
	TestTrue(TEXT("Legacy non-object _meta leaves error empty"), Error.IsEmpty());
	TestTrue(TEXT("Legacy non-object _meta resets state"), State.AssetObjectPath.IsEmpty());
	TestEqual(TEXT("Legacy non-object _meta has no regions"), State.Regions.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSyncStateRoundTripMetaTest,
	"AssetDocument.SyncState.RoundTripMeta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSyncStateRoundTripMetaTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("Blend"), TEXT("Linear"));

	TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetStringField(TEXT("Target"), TEXT("/Game/AssetDocumentTest/M_Test"));
	Document->SetObjectField(TEXT("Body"), Body);
	TSharedRef<FJsonObject> ExistingMeta = MakeShared<FJsonObject>();
	ExistingMeta->SetStringField(TEXT("existingNote"), TEXT("keep-me"));
	Document->SetObjectField(TEXT("_meta"), ExistingMeta);

	FAssetDocumentSyncState State;
	State.SchemaVersion = 1;
	State.AssetObjectPath = TEXT("/Game/AssetDocumentTest/M_Test");
	State.AssetPackageGuid = TEXT("package-guid-or-empty");
	State.UpdatedAtUtc = TEXT("2026-06-13T15:30:00Z");

	FAssetDocumentRegionSyncState FirstRegion;
	FirstRegion.PolicyVersion = 1;
	FirstRegion.SidecarHash = TEXT("sha1:first-sidecar");
	FirstRegion.AssetEvidenceHash = TEXT("sha1:first-asset");
	FirstRegion.LastSyncedAtUtc = TEXT("2026-06-13T15:30:00Z");
	FAssetDocumentSyncStateStore::UpdateRegionState(State, TEXT("Body.Blend"), FirstRegion);

	FAssetDocumentRegionSyncState ReplacementRegion = FirstRegion;
	ReplacementRegion.SidecarHash = TEXT("sha1:replacement-sidecar");
	FAssetDocumentSyncStateStore::UpdateRegionState(State, TEXT("Body.Blend"), ReplacementRegion);

	FAssetDocumentRegionSyncState SecondRegion;
	SecondRegion.PolicyVersion = 2;
	SecondRegion.SidecarHash = TEXT("sha1:second-sidecar");
	SecondRegion.AssetEvidenceHash = TEXT("sha1:second-asset");
	SecondRegion.LastSyncedAtUtc = TEXT("2026-06-13T15:31:00Z");
	FAssetDocumentSyncStateStore::UpdateRegionState(State, TEXT("Body.Notifies"), SecondRegion);

	FAssetDocumentSyncStateStore::WriteToDocumentJson(Document, State);

	TestEqual(TEXT("Write preserves Target"), Document->GetStringField(TEXT("Target")), FString(TEXT("/Game/AssetDocumentTest/M_Test")));
	const TSharedPtr<FJsonObject>* PreservedBody = nullptr;
	TestTrue(TEXT("Write preserves Body object"), Document->TryGetObjectField(TEXT("Body"), PreservedBody));

	const TSharedPtr<FJsonObject>* Meta = nullptr;
	TestTrue(TEXT("Write creates _meta object"), Document->TryGetObjectField(TEXT("_meta"), Meta));
	const TSharedPtr<FJsonObject>* Sync = nullptr;
	TestTrue(TEXT("Write creates _meta.sync object"), Meta && Meta->IsValid() && (*Meta)->TryGetObjectField(TEXT("sync"), Sync));
	if (Meta && Meta->IsValid())
	{
		TestEqual(TEXT("Write sets _meta.assetDocumentVersion"), static_cast<int32>((*Meta)->GetNumberField(TEXT("assetDocumentVersion"))), 1);
		TestEqual(TEXT("Write preserves existing _meta fields"), (*Meta)->GetStringField(TEXT("existingNote")), FString(TEXT("keep-me")));
	}
	if (Sync && Sync->IsValid())
	{
		TestEqual(TEXT("Sync schemaVersion writes as number"), static_cast<int32>((*Sync)->GetNumberField(TEXT("schemaVersion"))), 1);
		TestEqual(TEXT("Sync assetObjectPath round-trips"), (*Sync)->GetStringField(TEXT("assetObjectPath")), FString(TEXT("/Game/AssetDocumentTest/M_Test")));
	}

	FAssetDocumentSyncState LoadedState;
	FString Error;
	TestTrue(TEXT("Written _meta.sync loads"), FAssetDocumentSyncStateStore::LoadFromDocumentJson(Document, LoadedState, Error));
	TestTrue(TEXT("Round-trip leaves error empty"), Error.IsEmpty());
	TestEqual(TEXT("Loaded state has two regions"), LoadedState.Regions.Num(), 2);
	const FAssetDocumentRegionSyncState* LoadedBlend = LoadedState.Regions.Find(TEXT("Body.Blend"));
	const FAssetDocumentRegionSyncState* LoadedNotifies = LoadedState.Regions.Find(TEXT("Body.Notifies"));
	TestTrue(TEXT("Loaded Body.Blend region exists"), LoadedBlend != nullptr);
	TestTrue(TEXT("Loaded Body.Notifies region exists"), LoadedNotifies != nullptr);
	if (LoadedBlend)
	{
		TestEqual(TEXT("UpdateRegionState replaces existing region"), LoadedBlend->SidecarHash, FString(TEXT("sha1:replacement-sidecar")));
	}
	if (LoadedNotifies)
	{
		TestEqual(TEXT("Second region keeps policy version"), LoadedNotifies->PolicyVersion, 2);
	}

	const FString SidecarPath = FAssetDocumentSidecar::ResolveSidecarPathFromObjectPath(TEXT("/Game/AssetDocumentTest/M_Test"));
	FString ValidationError;
	TestTrue(
		TEXT("ValidateTargetMatchesSidecar ignores _meta.sync and uses Target"),
		FAssetDocumentSidecar::ValidateTargetMatchesSidecar(SidecarPath, Document, ValidationError));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSyncStateRegionKeysPreserveOriginalStringTest,
	"AssetDocument.SyncState.RegionKeysPreserveOriginalString",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSyncStateRegionKeysPreserveOriginalStringTest::RunTest(const FString& Parameters)
{
	const FString RegionId = TEXT("Body.CustomCASE");
	FAssetDocumentRegionSyncState Region;
	Region.SidecarHash = TEXT("sha1:case");

	FAssetDocumentSyncState State;
	FAssetDocumentSyncStateStore::UpdateRegionState(State, RegionId, Region);

	TestEqual(TEXT("String region id is stored once"), State.Regions.Num(), 1);
	const FAssetDocumentRegionSyncState* Loaded = State.Regions.Find(RegionId);
	TestTrue(TEXT("Original region id remains addressable"), Loaded != nullptr);
	if (Loaded)
	{
		TestEqual(TEXT("Original region id keeps value"), Loaded->SidecarHash, FString(TEXT("sha1:case")));
	}

	TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	FAssetDocumentSyncStateStore::WriteToDocumentJson(Document, State);

	const TSharedPtr<FJsonObject>* Meta = nullptr;
	const TSharedPtr<FJsonObject>* Sync = nullptr;
	const TSharedPtr<FJsonObject>* Regions = nullptr;
	TestTrue(TEXT("Written document has regions object"),
		Document->TryGetObjectField(TEXT("_meta"), Meta)
		&& Meta && Meta->IsValid()
		&& (*Meta)->TryGetObjectField(TEXT("sync"), Sync)
		&& Sync && Sync->IsValid()
		&& (*Sync)->TryGetObjectField(TEXT("regions"), Regions));
	if (Regions && Regions->IsValid())
	{
		TestTrue(TEXT("Written regions keep original key spelling"), (*Regions)->HasField(RegionId));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSyncStateMetaIgnoredByHashTest,
	"AssetDocument.SyncState.MetaIgnoredByHash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSyncStateMetaIgnoredByHashTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("Blend"), TEXT("Linear"));

	TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetObjectField(TEXT("Body"), Body);

	const FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Body.Blend"));
	const FString BaselineHash = FAssetDocumentSidecarDelta::HashSidecarRegion(Document, Policy);

	FAssetDocumentSyncState State;
	State.UpdatedAtUtc = TEXT("2026-06-13T15:30:00Z");
	FAssetDocumentRegionSyncState RegionState;
	RegionState.SidecarHash = BaselineHash;
	RegionState.LastSyncedAtUtc = TEXT("2026-06-13T15:30:00Z");
	FAssetDocumentSyncStateStore::UpdateRegionState(State, TEXT("Body.Blend"), RegionState);
	FAssetDocumentSyncStateStore::WriteToDocumentJson(Document, State);

	const FString HashWithSync = FAssetDocumentSidecarDelta::HashSidecarRegion(Document, Policy);

	State.UpdatedAtUtc = TEXT("2026-06-13T15:45:00Z");
	RegionState.LastSyncedAtUtc = TEXT("2026-06-13T15:45:00Z");
	FAssetDocumentSyncStateStore::UpdateRegionState(State, TEXT("Body.Blend"), RegionState);
	FAssetDocumentSyncStateStore::WriteToDocumentJson(Document, State);

	TestEqual(
		TEXT("_meta.sync changes do not alter region hash"),
		BaselineHash,
		FAssetDocumentSidecarDelta::HashSidecarRegion(Document, Policy));
	TestEqual(TEXT("Repeated _meta.sync writes remain ignored"), BaselineHash, HashWithSync);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSyncStateMalformedRegionsTest,
	"AssetDocument.SyncState.MalformedRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSyncStateMalformedRegionsTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Sync = MakeShared<FJsonObject>();
	Sync->SetStringField(TEXT("regions"), TEXT("not-an-object"));

	TSharedRef<FJsonObject> Meta = MakeShared<FJsonObject>();
	Meta->SetObjectField(TEXT("sync"), Sync);

	TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetObjectField(TEXT("_meta"), Meta);

	FAssetDocumentSyncState State;
	FString Error;
	TestFalse(TEXT("Malformed regions fails to load"), FAssetDocumentSyncStateStore::LoadFromDocumentJson(Document, State, Error));
	TestFalse(TEXT("Malformed regions reports an error"), Error.IsEmpty());
	TestEqual(TEXT("Malformed input JSON is not rewritten"), Sync->GetStringField(TEXT("regions")), FString(TEXT("not-an-object")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSyncStateMalformedIntegerOverflowTest,
	"AssetDocument.SyncState.MalformedIntegerOverflow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSyncStateMalformedIntegerOverflowTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Sync = MakeShared<FJsonObject>();
	Sync->SetNumberField(TEXT("schemaVersion"), static_cast<double>(MAX_int32) + 1.0);

	TSharedRef<FJsonObject> Meta = MakeShared<FJsonObject>();
	Meta->SetObjectField(TEXT("sync"), Sync);

	TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetObjectField(TEXT("_meta"), Meta);

	FAssetDocumentSyncState State;
	FString Error;
	TestFalse(TEXT("Overflowing schemaVersion fails to load"), FAssetDocumentSyncStateStore::LoadFromDocumentJson(Document, State, Error));
	TestFalse(TEXT("Overflowing schemaVersion reports an error"), Error.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSyncStateUpdateRegionNormalizesTest,
	"AssetDocument.SyncState.UpdateRegionNormalizes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSyncStateUpdateRegionNormalizesTest::RunTest(const FString& Parameters)
{
	FAssetDocumentSyncState State;
	FAssetDocumentRegionSyncState Region;
	Region.PolicyVersion = 0;
	Region.SidecarHash = TEXT("sha1:value");

	FAssetDocumentSyncStateStore::UpdateRegionState(State, FString(), Region);
	TestEqual(TEXT("Empty region id is ignored"), State.Regions.Num(), 0);

	FAssetDocumentSyncStateStore::UpdateRegionState(State, FString(TEXT("Body.Blend")), Region);
	const FAssetDocumentRegionSyncState* LoadedRegion = State.Regions.Find(TEXT("Body.Blend"));
	TestTrue(TEXT("Non-empty region id is written"), LoadedRegion != nullptr);
	if (LoadedRegion)
	{
		TestEqual(TEXT("Non-positive policy version normalizes to 1"), LoadedRegion->PolicyVersion, 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSyncStateWriteReplacesNonObjectMetaTest,
	"AssetDocument.SyncState.WriteReplacesNonObjectMeta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSyncStateWriteReplacesNonObjectMetaTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetStringField(TEXT("_meta"), TEXT("legacy diagnostic"));

	FAssetDocumentSyncState State;
	FAssetDocumentSyncStateStore::WriteToDocumentJson(Document, State);

	const TSharedPtr<FJsonObject>* Meta = nullptr;
	TestTrue(TEXT("Write replaces non-object _meta with object"), Document->TryGetObjectField(TEXT("_meta"), Meta));
	if (Meta && Meta->IsValid())
	{
		TestEqual(TEXT("Replaced _meta has assetDocumentVersion"), static_cast<int32>((*Meta)->GetNumberField(TEXT("assetDocumentVersion"))), 1);
		TestTrue(TEXT("Replaced _meta has sync object"), (*Meta)->HasTypedField<EJson::Object>(TEXT("sync")));
	}

	return true;
}

#endif
