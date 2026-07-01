# AssetDocument Timeline Placement Region Adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a composition-first timeline placement public region adapter and migrate the first AnimSequence/AnimMontage timeline placement regions without changing public AssetDocument JSON schema.

**Architecture:** Add private region runtime files for timeline placement config, parsed entries, track resolver, utility helpers, and `IAssetDocumentRegionAdapter` implementation. Profiles keep UE-specific materialization, notify object compilation, marker cache refresh, section linking, and managed/unmanaged merge in hooks. Migration proceeds from runtime tests to the smallest real region, then to notify-like regions, then to the Montage bridge.

**Tech Stack:** Unreal Engine 5.7 C++, AssetDocument private region runtime, Automation tests, UBT Development build, temporary validation host with plugin junction.

---

## References

- Spec: `docs/superpowers/specs/2026-07-01-asset-document-timeline-placement-region-adapter-design.md`
- Refactor chain: `docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md`
- Extension guide: `docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md`
- Existing runtime pattern: `Source/AssetDocument/Private/Regions/AssetDocumentFragmentArrayRegionAdapter.h`
- Existing runtime tests: `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`
- AnimSequence profile: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
- AnimMontage profile: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`
- Existing Montage bridge: `Source/AssetDocument/Private/Profiles/AnimMontageNotifyPlacementAdapter.h`
- Existing Montage bridge implementation: `Source/AssetDocument/Private/Profiles/AnimMontageNotifyPlacementAdapter.cpp`

## Worktree And Review Rules

- Create implementation branch from the current parent branch:
  - branch: `feature/asset-document-timeline-placement-region-adapter`
  - worktree: `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-timeline-placement-region-adapter`
- Record `SPEC_BASE=HEAD` before Task 1 implementation begins.
- Each task starts with `TASK_BASE=HEAD`.
- Each task must end with a checkpoint commit.
- Task review diff range is `TASK_BASE..HEAD`.
- Final spec review diff range is `SPEC_BASE..HEAD`.
- Do not modify unrelated files or revert user changes.
- Do not use the real `E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject` to judge worktree plugin changes. Use a temporary validation host with a junction pointing to this implementation worktree.

## File Structure

Create:

- `Source/AssetDocument/Private/Regions/AssetDocumentTimelinePlacementRegionAdapter.h`
  - Owns config structs, parsed entry structs, track resolver structs, hooks, utility declarations, and the adapter class.
- `Source/AssetDocument/Private/Regions/AssetDocumentTimelinePlacementRegionAdapter.cpp`
  - Owns JSON shape parsing, numeric validation, duplicate key detection, resolver invocation, adapter lifecycle, default extraction wrapping, and default diff.

Modify:

- `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`
  - Adds runtime tests for utility and adapter lifecycle.
- `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
  - Migrates `SyncMarkers`, then `Notifies` / `NotifyStates` parsing to timeline placement utility/adapter hooks.
- `Source/AssetDocument/Private/Profiles/AnimMontageNotifyPlacementAdapter.h`
  - Turns the existing Montage notify placement helper into a bridge over the public timeline placement adapter.
- `Source/AssetDocument/Private/Profiles/AnimMontageNotifyPlacementAdapter.cpp`
  - Replaces local placement array/time/duration parsing with timeline placement utility/adapter calls while keeping Montage-specific notify materialization.
- `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`
  - Keeps call sites stable and updates internal adapter naming only if needed.
- `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`
  - Adds focused regression coverage for migrated paths where existing tests do not already pin behavior.
- `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`
  - Adds focused regression coverage for Montage bridge path/diagnostic compatibility where existing tests do not already pin behavior.
- `docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md`
  - Marks the timeline placement ring implementation status after final verification.
- `docs/superpowers/specs/2026-07-01-asset-document-timeline-placement-region-adapter-design.md`
  - Updates status from `待审核` to `已实现` only after all tasks and final review pass.

## Shared Design Details

Use these exact first-version names unless implementation discovers a compile-level conflict:

```cpp
struct FAssetDocumentTimelineRange
{
	double MinTime = 0.0;
	double MaxTime = 0.0;
	bool bHasMaxTime = false;
};

struct FAssetDocumentTimelinePlacementRegionConfig
{
	FName AdapterName;
	FName RegionId;
	FString BodyPath;
	FString JsonPointer;
	FString SchemaLabel;

	FString TimeFieldName = TEXT("Time");
	bool bRequireTime = true;
	bool bAllowZeroTime = true;

	FString DurationFieldName;
	bool bHasDuration = false;
	bool bRequireDuration = false;
	bool bRequirePositiveDuration = true;
	bool bValidateEndTime = false;

	FString NameFieldName;
	bool bHasName = false;
	bool bRequireName = false;

	FString TrackNameFieldName;
	FString TrackIndexFieldName;
	bool bHasTrackIdentity = false;
	bool bRequireTrackIdentity = false;

	bool bEnableDefaultDiff = true;
	bool bPreserveProfileDiagnosticPaths = true;
};

struct FAssetDocumentTimelinePlacementEntry
{
	int32 Index = INDEX_NONE;
	FString JsonPointer;
	TSharedRef<FJsonObject> EntryObject;
	TOptional<double> Time;
	TOptional<double> Duration;
	TOptional<FString> Name;
	TOptional<FString> TrackName;
	TOptional<int32> TrackIndex;
	TOptional<int32> ResolvedTrackIndex;
	FString CanonicalTrackName;
	FString DuplicateKey;
};

struct FAssetDocumentTimelineTrackResolveRequest
{
	FName RegionId;
	int32 EntryIndex = INDEX_NONE;
	FString JsonPointer;
	TOptional<FString> TrackName;
	TOptional<int32> TrackIndex;
};

struct FAssetDocumentTimelineTrackResolveResult
{
	bool bResolved = false;
	int32 TrackIndex = INDEX_NONE;
	FString CanonicalTrackName;
	FAssetDocumentCapabilityResult Error = FAssetDocumentCapabilityResult::Success(TEXT("No track resolution required"));
};
```

Hooks use `TFunction` composition. Do not introduce a profile inheritance base:

```cpp
struct FAssetDocumentTimelineTrackResolver
{
	TFunction<FAssetDocumentTimelineTrackResolveResult(const FAssetDocumentTimelineTrackResolveRequest&)> Resolve;
};

struct FAssetDocumentTimelinePlacementHooks
{
	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		TArray<FAssetDocumentTimelinePlacementEntry>&)> Validate;

	TFunction<FAssetDocumentCapabilityResult(
		FAssetDocumentRegionContext&,
		TArray<FAssetDocumentTimelinePlacementEntry>&)> Preflight;

	TFunction<FAssetDocumentCapabilityResult(
		FAssetDocumentRegionContext&,
		const TArray<FAssetDocumentTimelinePlacementEntry>&,
		bool&)> Apply;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		TArray<TSharedRef<FJsonObject>>&)> Extract;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentRegionContext&,
		const TArray<FAssetDocumentTimelinePlacementEntry>&,
		TArray<TSharedPtr<FJsonValue>>&)> Diff;

	TFunction<FString(const FAssetDocumentTimelinePlacementEntry&)> BuildDuplicateKey;
	TFunction<FAssetDocumentTimelineRange(const FAssetDocumentRegionContext&)> GetTimelineRange;
};
```

## Task 1: Timeline Placement Runtime Adapter And Tests

**Files:**
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentTimelinePlacementRegionAdapter.h`
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentTimelinePlacementRegionAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`

- [ ] **Step 1: Record base**

Run:

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: clean worktree on `feature/asset-document-timeline-placement-region-adapter`.

- [ ] **Step 2: Add failing runtime tests**

Add `#include "Regions/AssetDocumentTimelinePlacementRegionAdapter.h"` near the other region adapter includes in `AssetDocumentRegionRuntimeTests.cpp`.

Add tests at the end of the region runtime section:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentTimelinePlacementUtilsRejectsInvalidShapesTest,
	"AssetFactory.AssetDocument.RegionRuntime.TimelinePlacement.InvalidShapes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentTimelinePlacementUtilsRejectsInvalidShapesTest::RunTest(const FString&)
{
	FAssetDocumentTimelinePlacementRegionConfig Config;
	Config.AdapterName = TEXT("TimelinePlacementTest");
	Config.RegionId = TEXT("Body.TestTimeline");
	Config.JsonPointer = TEXT("/Body/TestTimeline");
	Config.TimeFieldName = TEXT("Time");

	TArray<FAssetDocumentTimelinePlacementEntry> Entries;
	FAssetDocumentCapabilityResult Result =
		FAssetDocumentTimelinePlacementUtils::ParsePlacementEntries(
			MakeShared<FJsonValueString>(TEXT("bad")),
			Config,
			TEXT("/Body/TestTimeline"),
			nullptr,
			Entries);

	TestFalse(TEXT("Non-array timeline region fails"), Result.bSuccess);
	TestTrue(TEXT("Non-array diagnostic path is region path"), Result.Diagnostics.Num() > 0 && Result.Diagnostics[0].Path == TEXT("/Body/TestTimeline"));
	TestTrue(TEXT("Non-array diagnostic code is stable"), Result.Diagnostics.Num() > 0 && Result.Diagnostics[0].Code == TEXT("InvalidTimelinePlacementRegionType"));

	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Add(MakeShared<FJsonValueString>(TEXT("bad-entry")));
	Result = FAssetDocumentTimelinePlacementUtils::ParsePlacementEntries(
		MakeShared<FJsonValueArray>(Values),
		Config,
		TEXT("/Body/TestTimeline"),
		nullptr,
		Entries);

	TestFalse(TEXT("Non-object timeline entry fails"), Result.bSuccess);
	TestTrue(TEXT("Non-object diagnostic path includes index"), Result.Diagnostics.Num() > 0 && Result.Diagnostics[0].Path == TEXT("/Body/TestTimeline/0"));
	TestTrue(TEXT("Non-object diagnostic code is stable"), Result.Diagnostics.Num() > 0 && Result.Diagnostics[0].Code == TEXT("InvalidTimelinePlacementEntryType"));
	return true;
}
```

Add a numeric/range/duplicate test:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentTimelinePlacementUtilsValidatesNumbersAndDuplicatesTest,
	"AssetFactory.AssetDocument.RegionRuntime.TimelinePlacement.NumbersAndDuplicates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentTimelinePlacementUtilsValidatesNumbersAndDuplicatesTest::RunTest(const FString&)
{
	FAssetDocumentTimelinePlacementRegionConfig Config;
	Config.AdapterName = TEXT("TimelinePlacementTest");
	Config.RegionId = TEXT("Body.TestTimeline");
	Config.JsonPointer = TEXT("/Body/TestTimeline");
	Config.TimeFieldName = TEXT("Time");
	Config.DurationFieldName = TEXT("Duration");
	Config.bHasDuration = true;
	Config.bRequireDuration = true;
	Config.bValidateEndTime = true;
	Config.NameFieldName = TEXT("Name");
	Config.bHasName = true;
	Config.bRequireName = true;

	TSharedRef<FJsonObject> First = MakeShared<FJsonObject>();
	First->SetStringField(TEXT("Name"), TEXT("Hit"));
	First->SetNumberField(TEXT("Time"), 1.0);
	First->SetNumberField(TEXT("Duration"), 2.0);

	TSharedRef<FJsonObject> Duplicate = MakeShared<FJsonObject>();
	Duplicate->SetStringField(TEXT("Name"), TEXT("Hit"));
	Duplicate->SetNumberField(TEXT("Time"), 1.0);
	Duplicate->SetNumberField(TEXT("Duration"), 2.0);

	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Add(MakeShared<FJsonValueObject>(First));
	Values.Add(MakeShared<FJsonValueObject>(Duplicate));

	FAssetDocumentTimelineRange Range;
	Range.MinTime = 0.0;
	Range.MaxTime = 5.0;
	Range.bHasMaxTime = true;

	TArray<FAssetDocumentTimelinePlacementEntry> Entries;
	FAssetDocumentCapabilityResult Result =
		FAssetDocumentTimelinePlacementUtils::ParsePlacementEntries(
			MakeShared<FJsonValueArray>(Values),
			Config,
			TEXT("/Body/TestTimeline"),
			&Range,
			Entries);

	TestFalse(TEXT("Duplicate placement key fails"), Result.bSuccess);
	TestTrue(TEXT("Duplicate diagnostic code is stable"), Result.Diagnostics.Num() > 0 && Result.Diagnostics[0].Code == TEXT("DuplicateTimelinePlacementKey"));

	Duplicate->SetNumberField(TEXT("Time"), 4.5);
	Result = FAssetDocumentTimelinePlacementUtils::ParsePlacementEntries(
		MakeShared<FJsonValueArray>(Values),
		Config,
		TEXT("/Body/TestTimeline"),
		&Range,
		Entries);

	TestFalse(TEXT("Duration end outside range fails"), Result.bSuccess);
	TestTrue(TEXT("End range diagnostic code is stable"), Result.Diagnostics.Num() > 0 && Result.Diagnostics[0].Code == TEXT("InvalidTimelinePlacementEndTime"));
	return true;
}
```

Add an adapter lifecycle test:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentTimelinePlacementRegionAdapterDelegatesLifecycleTest,
	"AssetFactory.AssetDocument.RegionRuntime.TimelinePlacement.DelegatesLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentTimelinePlacementRegionAdapterDelegatesLifecycleTest::RunTest(const FString&)
{
	FAssetDocumentTimelinePlacementRegionConfig Config;
	Config.AdapterName = TEXT("TimelinePlacementTest");
	Config.RegionId = TEXT("Body.TestTimeline");
	Config.BodyPath = TEXT("Body.TestTimeline");
	Config.JsonPointer = TEXT("/Body/TestTimeline");
	Config.TimeFieldName = TEXT("Time");
	Config.NameFieldName = TEXT("Name");
	Config.bHasName = true;
	Config.bRequireName = true;

	int32 ValidateCalls = 0;
	int32 ApplyCalls = 0;
	FAssetDocumentTimelinePlacementHooks Hooks;
	Hooks.Validate = [&ValidateCalls](const FAssetDocumentRegionContext&, TArray<FAssetDocumentTimelinePlacementEntry>& Entries)
	{
		++ValidateCalls;
		return Entries.Num() == 1 && Entries[0].Name.IsSet() && Entries[0].Name.GetValue() == TEXT("Hit")
			? FAssetDocumentCapabilityResult::Success(TEXT("validated timeline placement"))
			: FAssetDocumentCapabilityResult::Failure(TEXT("unexpected timeline entry"), TEXT("/Body/TestTimeline"), TEXT("UnexpectedTimelineEntry"));
	};
	Hooks.Apply = [&ApplyCalls](
		FAssetDocumentRegionContext&,
		const TArray<FAssetDocumentTimelinePlacementEntry>& Entries,
		bool& bOutChanged)
	{
		++ApplyCalls;
		bOutChanged = Entries.Num() == 1;
		return FAssetDocumentCapabilityResult::Success(TEXT("applied timeline placement"));
	};
	Hooks.Extract = [](const FAssetDocumentRegionContext&, TArray<TSharedRef<FJsonObject>>& OutEntries)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Name"), TEXT("Hit"));
		Entry->SetNumberField(TEXT("Time"), 1.0);
		OutEntries.Add(Entry);
		return FAssetDocumentCapabilityResult::Success(TEXT("extracted timeline placement"));
	};

	FAssetDocumentTimelinePlacementRegionAdapter Adapter(MoveTemp(Config), MoveTemp(Hooks));
	const FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Body.TestTimeline"), TEXT("Body.TestTimeline"));
	FAssetDocumentRegionContext Context = MakeRuntimeContext(TEXT("Body.TestTimeline"), TEXT("/Body/TestTimeline"), &Policy);

	TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("Name"), TEXT("Hit"));
	Entry->SetNumberField(TEXT("Time"), 1.0);
	const TSharedPtr<FJsonValue> Desired = MakeArrayValue({MakeShared<FJsonValueObject>(Entry)});

	TestTrue(TEXT("Adapter supports configured region"), Adapter.SupportsRegion(Context));
	TestTrue(TEXT("Validate succeeds"), Adapter.ValidateRegion(Context, Desired).bSuccess);

	bool bChanged = false;
	TestTrue(TEXT("Apply succeeds"), Adapter.ApplyRegion(Context, Desired, bChanged).bSuccess);
	TestTrue(TEXT("Apply reports changed"), bChanged);
	TestEqual(TEXT("Validate called twice"), ValidateCalls, 2);
	TestEqual(TEXT("Apply called once"), ApplyCalls, 1);

	TSharedPtr<FJsonValue> Extracted;
	TestTrue(TEXT("Extract succeeds"), Adapter.ExtractRegion(Context, Extracted).bSuccess);
	TestTrue(TEXT("Extract returns array"), Extracted.IsValid() && Extracted->Type == EJson::Array);
	return true;
}
```

- [ ] **Step 3: Run focused tests and verify they fail to compile**

Run:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected: compile fails because `AssetDocumentTimelinePlacementRegionAdapter.h` does not exist.

- [ ] **Step 4: Implement the runtime adapter**

Create `AssetDocumentTimelinePlacementRegionAdapter.h/.cpp` following `AssetDocumentFragmentArrayRegionAdapter` style.

Required utility behavior:

- `MakeEntryPath(BasePath, Index)` returns `BasePath + "/" + Index`.
- `MakeFieldPath(EntryPath, FieldName)` returns `EntryPath + "/" + EscapeJsonPointerToken(FieldName)` using `FAssetDocumentJsonRegionUtils`.
- `ParsePlacementEntries` validates array/object shape.
- It reads configured `Time`, `Duration`, `Name`, `TrackName`, `TrackIndex`.
- It rejects non-finite numeric values with `InvalidTimelinePlacementNumber`.
- It rejects negative time with `InvalidTimelinePlacementTime`.
- It rejects missing required time with `MissingTimelinePlacementTime`.
- It rejects missing/invalid duration with `MissingTimelinePlacementDuration` or `InvalidTimelinePlacementDuration`.
- It rejects `Time + Duration` beyond `Range.MaxTime` with `InvalidTimelinePlacementEndTime`.
- It rejects missing required name with `MissingTimelinePlacementName`.
- It rejects duplicate placement keys with `DuplicateTimelinePlacementKey`.
- Default duplicate key uses configured name, time, duration, track name, and track index pieces.
- If a resolver is provided, call it after field parsing and before duplicate detection.
- If resolver returns `Error.bSuccess == false`, return that error.

Required adapter lifecycle:

- `SupportsRegion` matches `RegionId`, `BodyPath`, or `JsonPointer`.
- `ValidateRegion` parses and calls `Hooks.Validate` if present.
- `PreflightRegion` parses, validates, then calls `Hooks.Preflight` if present.
- `ApplyRegion` sets `bOutChanged = false` before parsing; parses, validates, requires `Hooks.Apply`, and returns hook result.
- `ExtractRegion` requires `Hooks.Extract` and wraps entries with `FJsonValueArray`.
- `DiffRegion` calls `Hooks.Diff` if present; otherwise uses `ExtractRegion` and `FAssetDocumentJsonRegionUtils::AppendCanonicalJsonDiff` or the same canonical diff helper used by `FAssetDocumentFragmentArrayRegionAdapter`.

- [ ] **Step 5: Run focused tests**

Run UBT and automation on the validation host if available. Minimum compile command:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected: UBT succeeds. If using the project `.uproject`, treat it as compile-only because same-name project plugin discovery can hide worktree behavior.

- [ ] **Step 6: Check and commit**

Run:

```powershell
git diff --check
git status --short
git add Source/AssetDocument/Private/Regions/AssetDocumentTimelinePlacementRegionAdapter.* Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp
git commit -m "feat: add timeline placement region adapter"
```

Expected: commit succeeds.

## Task 2: Migrate AnimSequence SyncMarkers To Timeline Placement Utility

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`

- [ ] **Step 1: Record base**

Run:

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: clean worktree.

- [ ] **Step 2: Add focused regression test**

Add or extend a SyncMarkers test in `AssetDocumentAnimSequenceTests.cpp` to assert:

- duplicate key remains `Name + canonical Time`.
- invalid time still uses the existing diagnostic code expected by current tests.
- apply failure does not mutate existing authored sync markers.

Use existing helpers in the file for creating a test sequence and applying an AssetDocument body. Do not create a new test fixture style.

- [ ] **Step 3: Refactor SyncMarkers parsing**

In `AnimSequenceAssetDocumentCapability.cpp`:

- Include `Regions/AssetDocumentTimelinePlacementRegionAdapter.h`.
- Replace local `/Body/SyncMarkers` array/object/name/time/duplicate parsing with `FAssetDocumentTimelinePlacementUtils::ParsePlacementEntries`.
- Use config:
  - `AdapterName = "AnimSequenceSyncMarkersTimelinePlacement"`
  - `RegionId = "Body.SyncMarkers"`
  - `JsonPointer = "/Body/SyncMarkers"`
  - `TimeFieldName = "Time"`
  - `NameFieldName = "Name"`
  - `bHasName = true`
  - `bRequireName = true`
  - no duration
  - no track identity
- Pass a range using sequence play length.
- Map generic utility failures to existing AnimSequence diagnostic codes if existing tests require profile-specific codes.
- Keep sync marker struct construction and `RefreshSyncMarkerDataFromAuthored()` in AnimSequence profile code.
- Keep extract sorting by `Time`, then `Name`.

- [ ] **Step 4: Run focused tests**

Run UBT and focused automation:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Then run the existing AssetDocument AnimSequence automation through the temporary validation host if available.

Expected: AnimSequence SyncMarkers tests pass; unrelated tests do not regress.

- [ ] **Step 5: Check and commit**

Run:

```powershell
git diff --check
git add Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp
git commit -m "refactor: route anim sequence sync markers through timeline utility"
```

Expected: commit succeeds.

## Task 3: Migrate AnimSequence Notifies And NotifyStates

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`

- [ ] **Step 1: Record base**

Run:

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: clean worktree.

- [ ] **Step 2: Add focused regression test**

Extend existing notify/notify-state tests to assert:

- invalid notify time keeps the existing diagnostic code.
- invalid notify-state duration keeps the existing diagnostic code.
- unknown `TrackName` keeps existing diagnostic code and path.
- ambiguous track alias keeps existing diagnostic code and path.
- duplicate notify and notify-state keys remain rejected.
- unmanaged notify preservation still works.

Use existing helpers; do not invent new sidecar application harnesses.

- [ ] **Step 3: Build a track resolver**

In `AnimSequenceAssetDocumentCapability.cpp`, add a small profile-local helper near existing notify parsing helpers:

```cpp
FAssetDocumentTimelineTrackResolver MakeAnimSequenceNotifyTrackResolver(
	const TArray<FParsedAnimSequenceNotifyTrack>& NotifyTracks)
{
	FAssetDocumentTimelineTrackResolver Resolver;
	Resolver.Resolve = [&NotifyTracks](const FAssetDocumentTimelineTrackResolveRequest& Request)
	{
		FAssetDocumentTimelineTrackResolveResult Result;
		if (!Request.TrackName.IsSet() && !Request.TrackIndex.IsSet())
		{
			Result.bResolved = false;
			return Result;
		}

		// Reuse existing AnimSequence track lookup and alias ambiguity logic here.
		// Return existing profile diagnostic codes for UnknownNotifyTrack and AmbiguousTrackNameAlias.
		return Result;
	};
	return Resolver;
}
```

Replace the comment block with calls to the existing track lookup/alias helper already used by `ParseAnimSequenceNotifies`. Do not introduce a second alias algorithm.

- [ ] **Step 4: Route Notifies and NotifyStates through timeline placement parsing**

Use two configs:

`Notifies`:

- `AdapterName = "AnimSequenceNotifiesTimelinePlacement"`
- `RegionId = "Body.Notifies"`
- `JsonPointer = "/Body/Notifies"`
- `TimeFieldName = "Time"`
- `TrackNameFieldName = "TrackName"`
- `bHasTrackIdentity = true`

`NotifyStates`:

- `AdapterName = "AnimSequenceNotifyStatesTimelinePlacement"`
- `RegionId = "Body.NotifyStates"`
- `JsonPointer = "/Body/NotifyStates"`
- `TimeFieldName = "Time"`
- `DurationFieldName = "Duration"`
- `bHasDuration = true`
- `bRequireDuration = true`
- `bRequirePositiveDuration = true`
- `bValidateEndTime = true`
- `TrackNameFieldName = "TrackName"`
- `bHasTrackIdentity = true`

Keep profile-owned logic in profile:

- notify object fragment validation.
- notify state object fragment validation.
- `FAnimNotifyEvent` construction.
- trigger/end offset refresh.
- unmanaged notify preservation.
- track remap.

- [ ] **Step 5: Run focused tests**

Run UBT and AnimSequence automation through validation host if available.

Expected: all existing AnimSequence AssetDocument tests pass.

- [ ] **Step 6: Check and commit**

Run:

```powershell
git diff --check
git add Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp
git commit -m "refactor: route anim sequence notifies through timeline adapter"
```

Expected: commit succeeds.

## Task 4: Convert AnimMontage NotifyPlacementAdapter Into Timeline Bridge

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimMontageNotifyPlacementAdapter.h`
- Modify: `Source/AssetDocument/Private/Profiles/AnimMontageNotifyPlacementAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`

- [ ] **Step 1: Record base**

Run:

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: clean worktree.

- [ ] **Step 2: Add focused Montage compatibility test**

Extend existing Montage notify placement tests to assert:

- invalid time and duration keep current diagnostic code.
- diagnostic paths keep existing Montage path style unless current tests already expect slash-style paths.
- managed/unmanaged notify preservation still works.
- extract output remains the same for managed `Notifies` and `NotifyStates`.

- [ ] **Step 3: Include the timeline runtime**

In `AnimMontageNotifyPlacementAdapter.cpp`, include:

```cpp
#include "Regions/AssetDocumentTimelinePlacementRegionAdapter.h"
```

Keep `IsManagedNotifyEvent` and `IsManagedNotifyStateEvent` as Montage-specific helpers.

- [ ] **Step 4: Replace local array/time/duration parsing**

In `FAnimMontageNotifyPlacementAdapter::Validate` and `Compile`:

- Use timeline placement parsing for `Notifies` and `NotifyStates`.
- Use config for `Notifies`:
  - `AdapterName = "AnimMontageNotifiesTimelinePlacement"`
  - `RegionId = "Body.Notifies"`
  - region path compatible with existing Montage diagnostics.
  - `TimeFieldName = "Time"`
  - `TrackIndexFieldName = "TrackIndex"` if existing Montage accepts it.
- Use config for `NotifyStates`:
  - `AdapterName = "AnimMontageNotifyStatesTimelinePlacement"`
  - `RegionId = "Body.NotifyStates"`
  - `TimeFieldName = "Time"`
  - `DurationFieldName = "Duration"`
  - `bHasDuration = true`
  - `bRequireDuration = true`
  - `bRequirePositiveDuration = true`
  - `bValidateEndTime = true`
  - `TrackIndexFieldName = "TrackIndex"` if existing Montage accepts it.
- Preserve existing Montage object fragment validation and materialization.
- Preserve existing managed notify names and prefixes.
- Preserve existing extract behavior.

If Montage currently uses `/Body/Notifies[0]/Time` diagnostics and tests pin it, add a small profile-local path adapter or utility option instead of globally changing the public timeline utility path style.

- [ ] **Step 5: Run focused tests**

Run UBT and AnimMontage automation through validation host if available.

Expected: all existing AnimMontage notify placement tests pass.

- [ ] **Step 6: Check and commit**

Run:

```powershell
git diff --check
git add Source/AssetDocument/Private/Profiles/AnimMontageNotifyPlacementAdapter.* Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp
git commit -m "refactor: bridge anim montage notifies to timeline adapter"
```

Expected: commit succeeds.

## Task 5: Inspection Names, Docs, And Deferred Boundaries

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
- Modify: `docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md`
- Modify: `docs/superpowers/specs/2026-07-01-asset-document-timeline-placement-region-adapter-design.md`

- [ ] **Step 1: Record base**

Run:

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: clean worktree.

- [ ] **Step 2: Update internal adapter inspection names**

Search:

```powershell
rg -n "InternalAdapters|GetInternalAdapterNames|AnimMontageNotifyPlacementAdapter|TimelinePlacement" Source/AssetDocument/Private/Profiles Source/AssetDocument/Private/Tests
```

If profile inspection lists internal adapters, ensure it includes the timeline placement adapter/bridge name without changing `BodySections` or `RegionPolicies`.

- [ ] **Step 3: Preserve deferred boundaries**

Confirm `CompositeSections`, `SlotAnimTracks`, and `AnimSegments` are not migrated into the first-version adapter. Add a short code comment only if the implementation leaves nearby timeline parsing code that future readers might try to fold into the adapter.

Acceptable comment:

```cpp
// CompositeSections and SlotAnimTracks are intentionally not full timeline adapter regions yet.
// See the timeline placement adapter spec for the deferred nested/section boundaries.
```

- [ ] **Step 4: Update docs**

In the refactor chain, under “第 7 环：Timeline Placement Region Adapter”, add implementation status:

- public adapter and utility created.
- AnimSequence `SyncMarkers`, `Notifies`, `NotifyStates` migrated.
- AnimMontage `Notifies`, `NotifyStates` bridged.
- `CompositeSections`, `SlotAnimTracks`, `AnimSegments` remain deferred as specified.

In the timeline placement design spec, change status to `已实现` only after code and focused tests are green.

- [ ] **Step 5: Check and commit**

Run:

```powershell
git diff --check
git add Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md docs/superpowers/specs/2026-07-01-asset-document-timeline-placement-region-adapter-design.md
git commit -m "docs: mark timeline placement adapter boundaries"
```

Expected: commit succeeds. If no profile source changes are needed, do not stage them.

## Task 6: Final Verification

**Files:**
- No production code changes expected.
- Validation artifacts must stay outside git-tracked paths.

- [ ] **Step 1: Record base**

Run:

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: clean worktree.

- [ ] **Step 2: Prepare validation host**

Use or create a temporary validation host outside the main project. Preferred path:

```text
C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject
```

The host `Plugins/UECopilot` junction must point to:

```text
E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-timeline-placement-region-adapter
```

- [ ] **Step 3: Run UBT**

Run:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -NoHotReload
```

Expected: `Result: Succeeded`.

- [ ] **Step 4: Run automation**

Run AssetDocument automation in the validation host editor. Minimum required filters:

- `AssetFactory.AssetDocument.RegionRuntime.TimelinePlacement`
- `AssetFactory.AssetDocument.AnimSequence`
- `AssetFactory.AssetDocument.AnimMontage`

Expected:

- 0 failed tests.
- Focused timeline placement tests are present and passing.
- Existing AnimSequence and AnimMontage tests are not regressed.

- [ ] **Step 5: Final checks**

Run:

```powershell
git diff --check
git status --short --branch
git log --oneline --decorate -6
```

Expected: clean worktree and task checkpoint commits visible.

- [ ] **Step 6: Commit validation docs only if docs changed**

If verification required doc-only updates, commit them:

```powershell
git add docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md docs/superpowers/specs/2026-07-01-asset-document-timeline-placement-region-adapter-design.md
git commit -m "docs: finalize timeline placement adapter verification"
```

Expected: commit succeeds, or no commit is needed because Task 5 already finalized docs.

## Final Review Requirements

After Task 6:

1. Dispatch a spec compliance review subagent over `SPEC_BASE..HEAD`.
2. Dispatch a code quality review subagent over `SPEC_BASE..HEAD`.
3. Fix all review findings with focused commits.
4. Re-run at least:
   - `git diff --check`
   - UBT Development build through validation host
   - focused AssetDocument automation filters listed above
5. Only then merge the implementation branch back to `feature/asset-document-object-field-schema-dispatcher-migration`.

## Plan Self-Review

Spec coverage:

- Runtime adapter/config/hooks/track resolver: Task 1.
- SyncMarkers first migration: Task 2.
- AnimSequence Notifies/NotifyStates migration: Task 3.
- AnimMontage Notifies/NotifyStates bridge: Task 4.
- CompositeSections and SlotAnimTracks deferred boundary: Task 5.
- UBT and automation validation through temp host: Task 6.
- Long-term docs and AGENTS constraints are already updated before this plan; Task 5 updates implementation status only.

No placeholder terms are intentionally left in task steps. Any implementation naming conflict must be resolved by preserving the behavior and updating the exact name consistently in code and tests within the same task commit.

