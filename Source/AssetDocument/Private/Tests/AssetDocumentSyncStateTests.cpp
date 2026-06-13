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

#endif
