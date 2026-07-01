# AssetDocument Fragment Array Region Adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a composable fragment array region adapter and migrate existing AnimSequence/AnimMontage fragment object array regions without changing public AssetDocument JSON behavior.

**Architecture:** Introduce a private `FAssetDocumentFragmentArrayRegionAdapter` that owns JSON array lifecycle, entry paths, hook dispatch, extract wrapping, and default diff. Keep fragment compiler use, UObject materialization, managed names, outer moves, rollback, reflection writes, montage section validation, and post-apply repair in profile-specific hooks.

**Tech Stack:** Unreal Engine 5.7 C++, AssetDocument region runtime, AssetDocument fragment compiler, UE automation tests, UBT.

---

## Reference Documents

- Spec: `docs/superpowers/specs/2026-07-01-asset-document-fragment-array-region-adapter-design.md`
- Refactor chain: `docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md`
- Region runtime examples:
  - `Source/AssetDocument/Private/Regions/AssetDocumentGraphRegionWrapperAdapter.h`
  - `Source/AssetDocument/Private/Regions/AssetDocumentGraphRegionWrapperAdapter.cpp`
  - `Source/AssetDocument/Private/Regions/AssetDocumentNamedArrayRegionAdapter.h`
  - `Source/AssetDocument/Private/Regions/AssetDocumentNamedArrayRegionAdapter.cpp`
- Fragment compiler:
  - `Source/AssetDocument/Public/AssetDocumentFragment.h`
  - `Source/AssetDocument/Private/AssetDocumentFragmentCompiler.h`
  - `Source/AssetDocument/Private/AssetDocumentFragmentCompiler.cpp`

## Worktree And Branch

Create implementation worktree from current parent branch:

- Base branch: `feature/asset-document-object-field-schema-dispatcher-migration`
- Base commit after this plan: record with `SPEC_BASE=$(git rev-parse HEAD)`
- New branch: `feature/asset-document-fragment-array-region-adapter`
- Suggested worktree: `E:\GameDev\PluginsWarehouse\.worktrees\UECopilot\asset-document-fragment-array-region-adapter`

Before each task:

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

After each task passes verification:

```powershell
git add <task files>
git commit -m "<task commit message>"
```

Do not include generated `Binaries/`, `Intermediate/`, or automation report artifacts.

## File Map

Create:

- `Source/AssetDocument/Private/Regions/AssetDocumentFragmentArrayRegionAdapter.h`
  - Declares fragment array config, entry, hooks, utility, and adapter.
- `Source/AssetDocument/Private/Regions/AssetDocumentFragmentArrayRegionAdapter.cpp`
  - Implements shape parsing, lifecycle dispatch, extract wrapping, and default diff.

Modify:

- `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`
  - Adds fake-hook tests for the common fragment array adapter.
- `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
  - Migrates `Metadata` and `AssetUserData` parsing/extract hooks to use fragment array utilities/adapter.
- `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`
  - Adds or adjusts tests only where current coverage does not pin migrated behavior.
- `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`
  - Migrates `Metadata` and reuses fragment array utility for `SectionMetadata` where helpful.
- `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`
  - Adds or adjusts tests only where current coverage does not pin migrated behavior.
- `docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md`
  - Marks ring 6 implemented/partially implemented with exact deferrals.

Review-only:

- `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentProfile.cpp`
- `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentProfile.cpp`
- `Source/AssetDocument/Private/Regions/AssetDocumentGraphRegionWrapperAdapter.*`
- `Source/AssetDocument/Private/Regions/AssetDocumentNamedArrayRegionAdapter.*`

## Shared Verification Commands

Use this trusted validation host:

- Host root: `C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter`
- Project: `C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/AVH1.uproject`
- Plugin junction: `C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/Plugins/AssetFactory`

Before the first UBT run, create or update the host so the plugin junction points at the implementation worktree root. Do not rely on `C:/AVH1` unless its `Plugins/AssetFactory` junction points to the active worktree.

UBT:

```powershell
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/AVH1.uproject' -NoHotReload
```

Automation examples:

```powershell
$report = 'C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/Saved/AutomationReports/FragmentArrayTask'
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.RegionRuntime.FragmentArray;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

## Task 1: Common Fragment Array Adapter

**Files:**

- Create: `Source/AssetDocument/Private/Regions/AssetDocumentFragmentArrayRegionAdapter.h`
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentFragmentArrayRegionAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: clean worktree on `feature/asset-document-fragment-array-region-adapter`.

- [ ] **Step 2: Add failing runtime tests**

Add this include to `AssetDocumentRegionRuntimeTests.cpp`:

```cpp
#include "Regions/AssetDocumentFragmentArrayRegionAdapter.h"
```

Append focused tests with these exact automation names:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeFragmentArraySupportsRegionTest,
	"AssetFactory.AssetDocument.RegionRuntime.FragmentArray.SupportsRegion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeFragmentArrayRejectsInvalidShapeTest,
	"AssetFactory.AssetDocument.RegionRuntime.FragmentArray.RejectsInvalidShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeFragmentArrayDispatchesHooksTest,
	"AssetFactory.AssetDocument.RegionRuntime.FragmentArray.DispatchesHooks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeFragmentArrayExtractAndDefaultDiffTest,
	"AssetFactory.AssetDocument.RegionRuntime.FragmentArray.ExtractAndDefaultDiff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
```

The tests must assert:

- `SupportsRegion` accepts `RegionId`, `BodyPath`, and `JsonPointer`.
- non-array desired value fails with code `InvalidFragmentArrayRegionType` at `/Body/TestFragments`.
- non-object entry fails with code `InvalidFragmentArrayEntryType` at `/Body/TestFragments/0`.
- `Validate` receives `Index == 0` and `JsonPointer == "/Body/TestFragments/0"`.
- empty array reaches `Apply` and can set `bOutChanged = true`.
- hook failure diagnostics propagate unchanged.
- `ExtractRegion` wraps `OutEntries` into JSON array.
- missing mandatory hooks fail with `UnsupportedFragmentArrayLifecycle`.
- default diff emits path `/Body/TestFragments`.

Use fake JSON fragments:

```cpp
TSharedRef<FJsonObject> MakeTestFragment(const TCHAR* Kind)
{
	TSharedRef<FJsonObject> Fragment = MakeShared<FJsonObject>();
	Fragment->SetStringField(TEXT("Kind"), Kind);
	return Fragment;
}
```

- [ ] **Step 3: Create adapter header**

Create `AssetDocumentFragmentArrayRegionAdapter.h` with:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"
#include "AssetDocumentRegionRuntime.h"

struct FAssetDocumentFragmentArrayRegionConfig
{
	FName AdapterName;
	FName RegionId;
	FString BodyPath;
	FString JsonPointer;
	FString SchemaLabel;
	bool bEnableDefaultDiff = true;
};

struct FAssetDocumentFragmentArrayEntry
{
	int32 Index = INDEX_NONE;
	FString JsonPointer;
	TSharedRef<FJsonObject> FragmentObject;
};

struct FAssetDocumentFragmentArrayHooks
{
	TFunction<FAssetDocumentCapabilityResult(const FAssetDocumentRegionContext&, const TArray<FAssetDocumentFragmentArrayEntry>&)> Validate;
	TFunction<FAssetDocumentCapabilityResult(FAssetDocumentRegionContext&, const TArray<FAssetDocumentFragmentArrayEntry>&)> Preflight;
	TFunction<FAssetDocumentCapabilityResult(FAssetDocumentRegionContext&, const TArray<FAssetDocumentFragmentArrayEntry>&, bool&)> Apply;
	TFunction<FAssetDocumentCapabilityResult(const FAssetDocumentRegionContext&, TArray<TSharedRef<FJsonObject>>&)> Extract;
	TFunction<FAssetDocumentCapabilityResult(const FAssetDocumentRegionContext&, const TArray<FAssetDocumentFragmentArrayEntry>&, TArray<TSharedPtr<FJsonValue>>&)> Diff;
};

struct FAssetDocumentFragmentArrayUtils
{
	static FString MakeEntryPath(const FString& BasePath, int32 Index);
	static FAssetDocumentCapabilityResult ParseObjectEntries(
		const TSharedPtr<FJsonValue>& Value,
		const FString& BasePath,
		TArray<FAssetDocumentFragmentArrayEntry>& OutEntries);
	static TSharedRef<FJsonValueArray> MakeArrayValue(const TArray<TSharedRef<FJsonObject>>& Entries);
};

class FAssetDocumentFragmentArrayRegionAdapter final : public IAssetDocumentRegionAdapter
{
public:
	FAssetDocumentFragmentArrayRegionAdapter(
		FAssetDocumentFragmentArrayRegionConfig InConfig,
		FAssetDocumentFragmentArrayHooks InHooks);

	virtual FName GetName() const override;
	virtual bool SupportsRegion(const FAssetDocumentRegionContext& Context) const override;
	virtual TSharedRef<FJsonObject> GetSchemaHint(const FAssetDocumentRegionContext& Context) const override;
	virtual FAssetDocumentCapabilityResult ValidateRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue) const override;
	virtual FAssetDocumentCapabilityResult PreflightRegion(
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue) const override;
	virtual FAssetDocumentCapabilityResult ApplyRegion(
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		bool& bOutChanged) override;
	virtual FAssetDocumentCapabilityResult ExtractRegion(
		const FAssetDocumentRegionContext& Context,
		TSharedPtr<FJsonValue>& OutCurrentValue) const override;
	virtual FAssetDocumentCapabilityResult DiffRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const override;

private:
	FAssetDocumentFragmentArrayRegionConfig Config;
	FAssetDocumentFragmentArrayHooks Hooks;
};
```

If `TSharedRef<FJsonValueArray>` is not valid for this codebase, implement `MakeArrayValue` as `TSharedRef<FJsonValue>` returning `MakeShared<FJsonValueArray>(Values)`.

- [ ] **Step 4: Implement adapter cpp**

Implement `AssetDocumentFragmentArrayRegionAdapter.cpp` with:

- `SupportsRegion`: match `RegionId`, `BodyPath`, or `JsonPointer`.
- `GetSchemaHint`: object with `Adapter`, `Label`, `Kind = "FragmentArray"`.
- `ParseObjectEntries`: require array; require every entry `EJson::Object` and valid `AsObject()`.
- `ValidateRegion`: parse entries, require `Hooks.Validate`.
- `PreflightRegion`: parse entries, no-op success if no hook.
- `ApplyRegion`: parse entries, set `bOutChanged = false` on shape failure, require `Hooks.Apply`.
- `ExtractRegion`: require `Hooks.Extract`, wrap entries into array.
- `DiffRegion`: parse desired entries, call `Hooks.Diff` if present; otherwise if `Config.bEnableDefaultDiff && Hooks.Extract`, extract current and canonical compare; otherwise fail `UnsupportedFragmentArrayLifecycle`.

Use failure helpers that produce:

```cpp
FAssetDocumentCapabilityResult::Failure(Message, Path, Code)
```

Required codes:

- `InvalidFragmentArrayRegionType`
- `InvalidFragmentArrayEntryType`
- `UnsupportedFragmentArrayLifecycle`

- [ ] **Step 5: Verify Task 1**

```powershell
git diff --check
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/AVH1.uproject' -NoHotReload
$report = 'C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/Saved/AutomationReports/FragmentArrayTask1'
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.RegionRuntime.FragmentArray;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected: UBT succeeds; four FragmentArray tests pass.

- [ ] **Step 6: Commit Task 1**

```powershell
git add Source/AssetDocument/Private/Regions/AssetDocumentFragmentArrayRegionAdapter.h Source/AssetDocument/Private/Regions/AssetDocumentFragmentArrayRegionAdapter.cpp Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp
git commit -m "feat: add asset document fragment array adapter"
```

## Task 2: AnimSequence Metadata Migration

**Files:**

- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp` only if existing coverage is insufficient.

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

- [ ] **Step 2: Include common adapter**

Add:

```cpp
#include "Regions/AssetDocumentFragmentArrayRegionAdapter.h"
```

- [ ] **Step 3: Add AnimSequence Metadata adapter helpers**

Add local helpers near managed object fragment functions:

```cpp
FAssetDocumentFragmentArrayRegionConfig MakeAnimSequenceMetadataFragmentArrayConfig()
{
	FAssetDocumentFragmentArrayRegionConfig Config;
	Config.AdapterName = TEXT("AnimSequenceMetadataFragmentArray");
	Config.RegionId = TEXT("Body.Metadata");
	Config.BodyPath = TEXT("Body.Metadata");
	Config.JsonPointer = TEXT("/Body/Metadata");
	Config.SchemaLabel = TEXT("AnimSequence Metadata");
	return Config;
}

FAssetDocumentRegionContext MakeAnimSequenceMetadataRegionContext(const FAssetDocumentCapabilityContext& Context)
{
	FAssetDocumentRegionContext RegionContext;
	RegionContext.Asset = Context.Asset;
	RegionContext.AssetClass = Context.AssetClass;
	RegionContext.TargetAssetPath = Context.TargetAssetPath;
	RegionContext.SourceDocumentPath = Context.SourceDocumentPath;
	RegionContext.Definitions = Context.Definitions;
	RegionContext.bIsDryRun = Context.bIsDryRun;
	RegionContext.Result = Context.Result;
	RegionContext.RegionId = TEXT("Body.Metadata");
	RegionContext.BodyPath = TEXT("Body.Metadata");
	RegionContext.JsonPointer = TEXT("/Body/Metadata");
	return RegionContext;
}
```

- [ ] **Step 4: Route Metadata shape/validate through adapter**

In `ParseManagedObjectFragmentArray` call sites for `Metadata`, replace direct array/object shape parsing with adapter parsing where possible.

Required behavior:

- non-array `Metadata` still fails at `/Body/Metadata`.
- non-object `Metadata[0]` still fails at `/Body/Metadata/0`.
- duplicate explicit name diagnostic remains `DuplicateMetadataKey`.
- invalid class diagnostics still point at `/Body/Metadata/0`.

The hook should convert adapter entries back to `TArray<TSharedPtr<FJsonValue>>` only at the boundary needed by existing `CompileManagedObjectFragments`, or introduce a local overload that accepts `TArray<FAssetDocumentFragmentArrayEntry>`.

Do not move `FAssetDocumentFragmentCompiler` into the common adapter.

- [ ] **Step 5: Route Metadata extract through adapter**

Use `FAssetDocumentFragmentArrayRegionAdapter::ExtractRegion` for `Metadata` extraction. Hook body should call existing `ExtractManagedMetadata(Compiler, Sequence, ExtractedMetadata)` and convert object entries to `TArray<TSharedRef<FJsonObject>>`.

If `ExtractManagedMetadata` can return non-object entries, return failure code `InvalidFragmentArrayEntryType` at `/Body/Metadata/<Index>`.

- [ ] **Step 6: Verify Task 2**

Run:

```powershell
git diff --check
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/AVH1.uproject' -NoHotReload
$report = 'C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/Saved/AutomationReports/FragmentArrayTask2AnimSequence'
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence.MetadataAndUserData;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

If the focused filter runs too little, run:

```powershell
$report = 'C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/Saved/AutomationReports/FragmentArrayTask2AnimSequenceAll'
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

- [ ] **Step 7: Commit Task 2**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp
git commit -m "refactor: route anim sequence metadata fragments through adapter"
```

Omit the test file if unchanged.

## Task 3: AnimSequence AssetUserData Migration

**Files:**

- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp` only if needed.

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

- [ ] **Step 2: Add AssetUserData adapter config/context**

Add:

```cpp
FAssetDocumentFragmentArrayRegionConfig MakeAnimSequenceAssetUserDataFragmentArrayConfig()
{
	FAssetDocumentFragmentArrayRegionConfig Config;
	Config.AdapterName = TEXT("AnimSequenceAssetUserDataFragmentArray");
	Config.RegionId = TEXT("Body.AssetUserData");
	Config.BodyPath = TEXT("Body.AssetUserData");
	Config.JsonPointer = TEXT("/Body/AssetUserData");
	Config.SchemaLabel = TEXT("AnimSequence AssetUserData");
	return Config;
}
```

Add a matching `MakeAnimSequenceAssetUserDataRegionContext` using `RegionId = Body.AssetUserData` and `JsonPointer = /Body/AssetUserData`.

- [ ] **Step 3: Route AssetUserData parse/apply/extract lifecycle through adapter**

Replace direct `Body.AssetUserData` shape validation, entry path creation, extract array wrapping, and default diff construction with calls through the fragment array adapter. Keep the asset-specific hook bodies responsible for fragment compile/materialize/write-back behavior and preserve:

- expected base class `UAssetUserData::StaticClass()`
- duplicate code `DuplicateAssetUserDataKey`
- managed prefix `ManagedAssetUserDataObjectPrefix`
- `ReplaceManagedAssetUserDataByReflection`
- empty array clears managed AssetUserData only
- unmanaged user data remains untouched

- [ ] **Step 4: Verify Task 3**

Run:

```powershell
git diff --check
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/AVH1.uproject' -NoHotReload
$report = 'C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/Saved/AutomationReports/FragmentArrayTask3AnimSequence'
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence.MetadataAndUserData;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected: Metadata/UserData tests pass, especially unmanaged user data preservation and invalid `/Body/AssetUserData/0` diagnostics.

- [ ] **Step 5: Commit Task 3**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp
git commit -m "refactor: route anim sequence asset user data fragments through adapter"
```

Omit the test file if unchanged.

## Task 4: AnimMontage Metadata Migration

**Files:**

- Modify: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp` only if needed.

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

- [ ] **Step 2: Include common adapter and add config**

Add:

```cpp
#include "Regions/AssetDocumentFragmentArrayRegionAdapter.h"
```

Add:

```cpp
FAssetDocumentFragmentArrayRegionConfig MakeAnimMontageMetadataFragmentArrayConfig()
{
	FAssetDocumentFragmentArrayRegionConfig Config;
	Config.AdapterName = TEXT("AnimMontageMetadataFragmentArray");
	Config.RegionId = TEXT("Body.Metadata");
	Config.BodyPath = TEXT("Body.Metadata");
	Config.JsonPointer = TEXT("/Body/Metadata");
	Config.SchemaLabel = TEXT("AnimMontage Metadata");
	return Config;
}
```

- [ ] **Step 3: Route Metadata array shape/compile/extract through adapter/utilities**

Preserve:

- `EmbeddedObject` / `DefinitionRef` allowlist.
- expected base class `UAnimMetaData::StaticClass()`.
- `MetadataStagingOuter` behavior.
- `MoveMetadataArrayToMontage`.
- compile failure rollback behavior.
- path `/Body/Metadata/<Index>`.

Do not migrate `SectionMetadata` in this task.

- [ ] **Step 4: Verify Task 4**

Run:

```powershell
git diff --check
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/AVH1.uproject' -NoHotReload
$report = 'C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/Saved/AutomationReports/FragmentArrayTask4AnimMontage'
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage.ApplyExtract.MetadataRegions;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

If the focused filter is insufficient, run `AssetFactory.AssetDocument.AnimMontage`.

- [ ] **Step 5: Commit Task 4**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp
git commit -m "refactor: route anim montage metadata fragments through adapter"
```

Omit the test file if unchanged.

## Task 5: AnimMontage AssetUserData Capability Check

**Files:**

- Modify: `docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md`
- Modify: `docs/superpowers/specs/2026-07-01-asset-document-fragment-array-region-adapter-design.md` only if current evidence contradicts the spec.

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

- [ ] **Step 2: Verify current AnimMontage AssetUserData support**

Run:

```powershell
rg -n "AssetUserData" Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentProfile.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp
```

Expected current evidence:

- `AnimMontageAssetDocumentProfile.cpp` has no `Body.AssetUserData` template or region policy.
- `AnimMontageAssetDocumentCapability.cpp` has no supported `AssetUserData` body key.

- [ ] **Step 3: Record deferral instead of adding new authoring**

If the expected evidence is confirmed, update refactor-chain ring 6 note with:

```markdown
- AnimMontage `AssetUserData` was checked in this branch and remains deferred because the current AnimMontage profile does not expose `Body.AssetUserData`; this refactor does not add new public authoring capability.
```

Do not add code support for AnimMontage `AssetUserData` in this spec.

- [ ] **Step 4: Commit Task 5**

```powershell
git add docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md docs/superpowers/specs/2026-07-01-asset-document-fragment-array-region-adapter-design.md
git commit -m "docs: defer anim montage asset user data fragment migration"
```

Omit the fragment-array spec if unchanged.

## Task 6: AnimMontage SectionMetadata Utility Reuse

**Files:**

- Modify: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp` only if needed.

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

- [ ] **Step 2: Reuse fragment array utility per section if it reduces duplication**

In `ParseMetadataRegions`, keep:

- section map parsing under `/Body/SectionMetadata`.
- section-name validation and `UnknownSectionMetadataTarget`.
- `SectionMetadataArrays` map.
- section assignment to `FCompositeSection`.

Only replace per-section loops where the common utility can preserve exact path `/Body/SectionMetadata/<SectionName>/<Index>` and exact diagnostics.

If the code becomes less clear or requires broad rewiring, leave `SectionMetadata` code unchanged and document the deferral in the task commit notes/refactor-chain.

- [ ] **Step 3: Verify Task 6**

Run:

```powershell
git diff --check
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/AVH1.uproject' -NoHotReload
$report = 'C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/Saved/AutomationReports/FragmentArrayTask6AnimMontageSectionMetadata'
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage.ApplyExtract.MetadataRegions;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected: section metadata target validation, empty object clear, start-only replacement, and compile failure rollback tests still pass.

- [ ] **Step 4: Commit Task 6**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp
git commit -m "refactor: reuse fragment array utilities for montage section metadata"
```

If no code changed because reuse was deferred, commit only the refactor-chain note with:

```powershell
git add docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md
git commit -m "docs: defer montage section metadata utility migration"
```

## Task 7: Refactor Chain Update And Full Verification

**Files:**

- Modify: `docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

- [ ] **Step 2: Scan final adapter usage**

Run:

```powershell
rg -n "FragmentArrayRegionAdapter|FragmentArrayUtils|ParseManagedObjectFragmentArray|CompileMetadataArray|ValidateMetadataArrayShape|ValidateMetadataArray\\(" Source/AssetDocument/Private/Regions Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp
```

Expected:

- common adapter exists under `Private/Regions`.
- AnimSequence `Metadata` and `AssetUserData` route through common adapter/hooks.
- AnimMontage `Metadata` routes through common adapter/hooks.
- direct montage `SectionMetadata` loops either use utilities or are documented as intentionally deferred.

- [ ] **Step 3: Update refactor-chain ring 6 status**

Add implementation status under `## 10. 第 6 环：Fragment Array Region Adapter`:

```markdown
实现状态（2026-07-01）：

- 已新增 `FAssetDocumentFragmentArrayRegionAdapter` 统一 fragment array shape validation、entry path、hook dispatch、extract wrapping 和 default diff。
- AnimSequence `Metadata` / `AssetUserData` 已迁移到公共 fragment array lifecycle；fragment compiler、managed prefix、rename、rollback 和 write-back 仍在 AnimSequence hooks。
- AnimMontage `Metadata` 已迁移到公共 fragment array lifecycle；metadata compile、outer move 和 montage write-back 仍在 AnimMontage hooks。
- AnimMontage `AssetUserData` 保持 deferred，因为当前 AnimMontage profile 未暴露 `Body.AssetUserData`。
- AnimMontage `SectionMetadata` 的 section map 语义仍由 AnimMontage capability 持有；per-section array utility reuse 按 Task 6 结果记录为 `implemented via FAssetDocumentFragmentArrayUtils per section` 或 `deferred because utility reuse added more indirection than duplicated code removed`，并补一句直接证据。
```

- [ ] **Step 4: Full verification**

Run:

```powershell
git diff --check
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/AVH1.uproject' -NoHotReload
$report = 'C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/Saved/AutomationReports/FragmentArrayFullAssetDocument'
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/Users/HP/.config/superpowers/validation-hosts/fragment-array-region-adapter/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected:

- UBT succeeds.
- Full AssetDocument automation has 0 failed and 0 notRun.
- Existing warnings are recorded with report path.

- [ ] **Step 5: Commit Task 7**

```powershell
git add docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md
git commit -m "docs: mark fragment array adapter migration status"
```

## Review Gates

After each task commit:

1. Spec compliance review over `TASK_BASE..HEAD`.
2. Code quality review over `TASK_BASE..HEAD`.
3. Fix all Critical/Important findings before next task.

After Task 7:

1. Final spec review over `SPEC_BASE..HEAD`.
2. Final code quality review over `SPEC_BASE..HEAD`.
3. Use finishing branch workflow to merge/hold/PR.

Review prompts must include only:

- this plan,
- fragment array spec,
- current task acceptance criteria,
- exact diff range,
- relevant files for that task.

## Completion Criteria

- `FAssetDocumentFragmentArrayRegionAdapter` exists and is tested.
- AnimSequence `Metadata` and `AssetUserData` use the common adapter lifecycle.
- AnimMontage `Metadata` uses the common adapter lifecycle.
- AnimMontage `AssetUserData` is either migrated if currently supported or explicitly deferred if not currently public.
- AnimMontage `SectionMetadata` either reuses fragment array utility per section or has an explicit deferral note.
- Fragment compiler and UObject materialization remain profile hooks.
- Public body schema is unchanged.
- Empty array clear/replace behavior remains covered.
- Diagnostic paths remain non-synthetic.
- UBT and full AssetDocument automation pass.
