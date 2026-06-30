# AssetDocument Public Region Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 AssetDocument 已经重复出现的 Body/region lifecycle、JSON shape validation、diff entry、canonical compare、explicit empty/delete handling 抽到 public region runtime，使后续 asset profile 默认复用公共 region adapter，只保留必要的 asset-specific hook。

**Architecture:** 保留 `IAssetDocumentCapability` 作为 profile-level facade。新增 composition-first 的 `IAssetDocumentRegionAdapter`、`FAssetDocumentRegionRuntime`、`FAssetDocumentBodyRegionDispatcher` 和公共 region adapters。具体 profile 不继承新的 capability base，而是在现有 capability 内组合 dispatcher、region bindings、adapter map 和少量 hooks。

**Tech Stack:** Unreal Engine 5.7 C++ editor module, `AssetDocument` module, existing `IAssetDocumentProfile` / `IAssetDocumentCapability` / `FAssetDocumentRegionPolicy`, UE automation tests, MCP Node test suite, validation host `C:/AVH1`.

---

## Scope

本计划实现 `docs/superpowers/specs/2026-06-24-asset-document-public-region-runtime-design.md` 的第一版 runtime。第一版只迁移低风险、可验证的 region，不重写 graph/tree/timeline 内部 materializer。

必须完成：

- 新增 public region context、adapter interface、runtime、dispatcher 和 JSON utility。
- 至少一个现有 profile 的至少两个 `Body.*` regions 通过 dispatcher/runtime 调度。
- 至少一个 deferred region 使用公共 deferred adapter。
- 至少一个 object 或 named-array region 使用公共 adapter。
- 保持 `validate_asset_document`、`diff_asset_document`、`extract_asset_document`、`apply_asset_document` 和 `apply_asset_document_file` MCP contract 不变。

不做：

- 不新增新 asset class profile。
- 不把 graph/tree/timeline/import/source-file 合并成万能 adapter。
- 不在 runtime 中写 asset class switch。
- 不引入 `FAssetDocumentBodyCapabilityBase` 之类的继承基类。
- 不修改 sidecar sync engine 的 hash/state decision 语义。

## Implementation Worktree

实现阶段必须从已审阅的 spec 分支新建独立 worktree。执行前记录真实 base commit：

```powershell
Push-Location E:/GameDev/PluginsWarehouse/Plugins/UECopilot
git worktree add E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-public-region-runtime-implementation -b feature/asset-document-public-region-runtime docs/asset-document-public-region-runtime-spec
Pop-Location

Push-Location E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-public-region-runtime-implementation
$env:SPEC_BASE = git rev-parse HEAD
git status --short
Pop-Location
```

Expected result:

- New branch: `feature/asset-document-public-region-runtime`
- Worktree: `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-public-region-runtime-implementation`
- `git status --short` is empty.
- Final spec review diff range is `$env:SPEC_BASE..HEAD`.

If the branch or worktree already exists, inspect it first and continue only when its `HEAD` is the accepted spec/plan base and the worktree is clean.

## Target File Structure

Create:

- `Source/AssetDocument/Public/AssetDocumentRegion.h`
- `Source/AssetDocument/Private/AssetDocumentJsonRegionUtils.h`
- `Source/AssetDocument/Private/AssetDocumentJsonRegionUtils.cpp`
- `Source/AssetDocument/Private/AssetDocumentRegionRuntime.h`
- `Source/AssetDocument/Private/AssetDocumentRegionRuntime.cpp`
- `Source/AssetDocument/Private/AssetDocumentBodyRegionDispatcher.h`
- `Source/AssetDocument/Private/AssetDocumentBodyRegionDispatcher.cpp`
- `Source/AssetDocument/Private/Regions/AssetDocumentDeferredRegionAdapter.h`
- `Source/AssetDocument/Private/Regions/AssetDocumentDeferredRegionAdapter.cpp`
- `Source/AssetDocument/Private/Regions/AssetDocumentObjectRegionAdapter.h`
- `Source/AssetDocument/Private/Regions/AssetDocumentObjectRegionAdapter.cpp`
- `Source/AssetDocument/Private/Regions/AssetDocumentNamedArrayRegionAdapter.h`
- `Source/AssetDocument/Private/Regions/AssetDocumentNamedArrayRegionAdapter.cpp`
- `Source/AssetDocument/Private/Regions/AssetDocumentWidgetBlueprintRegionWrappers.h`
- `Source/AssetDocument/Private/Regions/AssetDocumentWidgetBlueprintRegionWrappers.cpp`
- `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`

Modify:

- `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentProfile.cpp`
- `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentProfile.cpp`
- `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.cpp`
- `docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md`

Do not modify:

- MCP public tool schemas unless compilation proves a currently included header path must be updated.
- `FAssetDocumentSidecarSyncEngine` decision logic.
- `FAssetDocumentCanonicalJson` domain semantics.

## Architecture Rules

- `IAssetDocumentRegionAdapter` is a pure abstract boundary. Concrete adapters use composition for parser/materializer/extractor/diff hooks.
- `FAssetDocumentBodyRegionDispatcher` is a new helper class owned by concrete capabilities. It is not where UE-specific logic lives.
- Dispatcher owns Body-level orchestration: Body object validation, known key validation, required region check, apply order, region diagnostics prefix, cross-region hook, post-apply repair hook.
- Region runtime owns lifecycle invariants: explicit empty/delete, default diff path, canonical compare, diff entry shape, adapter invocation, reducer/apply policy interpretation.
- Public adapters own reusable region shape behavior: deferred empty region, object region, named array region, preview-apply-diff wrapper.
- Asset-specific hooks own UE materialization/extraction/semantic identity/compile/rebuild/cache repair.

## Task 1: Region Interface And JSON Utilities

**Goal:** Land the public region type surface and shared JSON/diff helpers without changing existing profile behavior.

**Files:**

- `Source/AssetDocument/Public/AssetDocumentRegion.h`
- `Source/AssetDocument/Private/AssetDocumentJsonRegionUtils.h`
- `Source/AssetDocument/Private/AssetDocumentJsonRegionUtils.cpp`
- `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`

**Steps:**

- [ ] `Push-Location E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-public-region-runtime-implementation`
- [ ] Record `TASK_BASE`: `$env:TASK_BASE = git rev-parse HEAD`
- [ ] Add tests in `AssetDocumentRegionRuntimeTests.cpp` for JSON utility behavior before implementation:
  - `AssetFactory.AssetDocument.RegionRuntime.JsonUtils.RequireObject`
  - `AssetFactory.AssetDocument.RegionRuntime.JsonUtils.RequireArray`
  - `AssetFactory.AssetDocument.RegionRuntime.JsonUtils.JsonPointerEscaping`
  - `AssetFactory.AssetDocument.RegionRuntime.JsonUtils.DiffEntryShape`
- [ ] Add `FAssetDocumentRegionContext`, `FAssetDocumentRegionBinding`, and `IAssetDocumentRegionAdapter` in `AssetDocumentRegion.h`.
- [ ] Add `FAssetDocumentJsonRegionUtils` with:
  - `Failure`
  - `RequireObjectValue`
  - `RequireArrayValue`
  - `RequireStringField`
  - `RequireNumberField`
  - `RequireBoolField`
  - `EscapeJsonPointerToken`
  - `MakeBodyPath`
  - `MakeBodyArrayItemPath`
  - `JsonValueToComparableString`
  - `AddDiffEntry`
- [ ] Ensure helpers only depend on JSON/core AssetDocument result types and contain no asset-class includes.
- [ ] Build:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
```

- [ ] Run focused tests:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.RegionRuntime.JsonUtils;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/RegionRuntimeJsonUtils"
```

Expected result:

- UBT succeeds.
- Focused JSON utility tests pass.
- Existing profile code has no behavior changes.

Checkpoint:

```powershell
git diff --check
git status --short
git add Source/AssetDocument/Public/AssetDocumentRegion.h Source/AssetDocument/Private/AssetDocumentJsonRegionUtils.h Source/AssetDocument/Private/AssetDocumentJsonRegionUtils.cpp Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp
git commit -m "feat(assetdoc): add region interface and json helpers"
```

## Task 2: Region Runtime And Body Dispatcher

**Goal:** Implement runtime/dispatcher behavior with test-only adapters before touching real profiles.

**Files:**

- `Source/AssetDocument/Private/AssetDocumentRegionRuntime.h`
- `Source/AssetDocument/Private/AssetDocumentRegionRuntime.cpp`
- `Source/AssetDocument/Private/AssetDocumentBodyRegionDispatcher.h`
- `Source/AssetDocument/Private/AssetDocumentBodyRegionDispatcher.cpp`
- `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`

**Steps:**

- [ ] Record `TASK_BASE`: `$env:TASK_BASE = git rev-parse HEAD`
- [ ] Add fake test adapter classes inside `AssetDocumentRegionRuntimeTests.cpp`.
- [ ] Add runtime tests:
  - `AssetFactory.AssetDocument.RegionRuntime.Dispatch.ValidateCallsAdapter`
  - `AssetFactory.AssetDocument.RegionRuntime.Dispatch.PreflightUsesValidateDefault`
  - `AssetFactory.AssetDocument.RegionRuntime.Dispatch.ApplyReturnsChangedFlag`
  - `AssetFactory.AssetDocument.RegionRuntime.Dispatch.DiffEntryUsesJsonPointer`
  - `AssetFactory.AssetDocument.RegionRuntime.ExplicitEmpty.RejectsUnexpectedNull`
- [ ] Add dispatcher tests:
  - `AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.RejectsNonObjectBody`
  - `AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.RejectsUnknownBodyKey`
  - `AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.RequiresConfiguredRegion`
  - `AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.AppliesInConfiguredOrder`
  - `AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.CallsCrossRegionHook`
  - `AssetFactory.AssetDocument.RegionRuntime.BodyDispatcher.CallsPostApplyRepairHook`
- [ ] Implement `FAssetDocumentRegionRuntime` as a static orchestration helper with no profile includes.
- [ ] Implement `FAssetDocumentBodyRegionDispatcher` with config passed through constructor:
  - region bindings
  - region policies
  - adapter map
  - cross-region validation hook
  - post-apply repair hook
- [ ] Keep dispatcher methods matching `IAssetDocumentCapability` lifecycle shape:
  - `ValidateBody`
  - `PreflightBody`
  - `ApplyBody`
  - `ExtractBody`
  - `DiffBody`
- [ ] Build and run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.RegionRuntime;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/RegionRuntimeDispatcher"
```

Expected result:

- Runtime and dispatcher tests pass.
- Dispatcher can orchestrate fake regions without profile-specific inheritance.
- No real profile behavior changes.

Checkpoint:

```powershell
git diff --check
git status --short
git add Source/AssetDocument/Private/AssetDocumentRegionRuntime.h Source/AssetDocument/Private/AssetDocumentRegionRuntime.cpp Source/AssetDocument/Private/AssetDocumentBodyRegionDispatcher.h Source/AssetDocument/Private/AssetDocumentBodyRegionDispatcher.cpp Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp
git commit -m "feat(assetdoc): add region runtime dispatcher"
```

## Task 3: Deferred Region Adapter And Low-Risk Profile Migration

**Goal:** Replace profile-private deferred empty-region validation with a public deferred region adapter.

**Files:**

- `Source/AssetDocument/Private/Regions/AssetDocumentDeferredRegionAdapter.h`
- `Source/AssetDocument/Private/Regions/AssetDocumentDeferredRegionAdapter.cpp`
- `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentProfile.cpp`
- `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.cpp`
- `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`

**Steps:**

- [ ] Record `TASK_BASE`: `$env:TASK_BASE = git rev-parse HEAD`
- [ ] Add deferred adapter tests:
  - `AssetFactory.AssetDocument.RegionRuntime.DeferredRegion.AcceptsEmptyArray`
  - `AssetFactory.AssetDocument.RegionRuntime.DeferredRegion.AcceptsEmptyObject`
  - `AssetFactory.AssetDocument.RegionRuntime.DeferredRegion.RejectsNonEmptyArray`
  - `AssetFactory.AssetDocument.RegionRuntime.DeferredRegion.RejectsNonEmptyObject`
  - `AssetFactory.AssetDocument.RegionRuntime.DeferredRegion.ExtractsDeclaredDefault`
- [ ] Implement `FAssetDocumentDeferredRegionAdapter`.
- [ ] In `UBlueprintAssetDocumentProfile.cpp`, route deferred graph/timeline body keys through dispatcher/runtime where those regions are declared but authoring remains deferred:
  - `Body.FunctionGraphs`
  - `Body.MacroGraphs`
  - `Body.Timelines`
- [ ] In `WidgetBlueprintAssetDocumentProfile.cpp`, route deferred graph regions through the same adapter for declared-but-empty regions.
- [ ] Preserve existing diagnostic code/category names where tests already assert them. If a message changes, update only tests that assert the old wording rather than behavior.
- [ ] Confirm `GetInternalAdapterNames()` or equivalent profile inspection includes the deferred adapter name.
- [ ] Build and run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.RegionRuntime.DeferredRegion;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/DeferredRegion"
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint;Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/DeferredRegionProfiles"
```

Expected result:

- Deferred adapter tests pass.
- UBlueprint and WidgetBlueprint focused tests do not regress.
- Non-empty deferred regions still fail clearly instead of being silently ignored.

Checkpoint:

```powershell
git diff --check
git status --short
git add Source/AssetDocument/Private/Regions/AssetDocumentDeferredRegionAdapter.h Source/AssetDocument/Private/Regions/AssetDocumentDeferredRegionAdapter.cpp Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentProfile.cpp Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.cpp Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp
git commit -m "feat(assetdoc): route deferred regions through public adapter"
```

## Task 4: Object And Named Array Adapters With AnimSequence Pilot

**Goal:** Prove reusable object/named-array lifecycle on a real existing profile without taking over the entire AnimSequence capability.

**Files:**

- `Source/AssetDocument/Private/Regions/AssetDocumentObjectRegionAdapter.h`
- `Source/AssetDocument/Private/Regions/AssetDocumentObjectRegionAdapter.cpp`
- `Source/AssetDocument/Private/Regions/AssetDocumentNamedArrayRegionAdapter.h`
- `Source/AssetDocument/Private/Regions/AssetDocumentNamedArrayRegionAdapter.cpp`
- `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentProfile.cpp`
- `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`

**Pilot regions:**

- `Body.Preview` through `FAssetDocumentObjectRegionAdapter`.
- `Body.Playback` through `FAssetDocumentObjectRegionAdapter`.
- `Body.NotifyTracks` through `FAssetDocumentNamedArrayRegionAdapter`.

**Steps:**

- [ ] Record `TASK_BASE`: `$env:TASK_BASE = git rev-parse HEAD`
- [ ] Add object adapter tests:
  - `AssetFactory.AssetDocument.RegionRuntime.ObjectRegion.RequiresObject`
  - `AssetFactory.AssetDocument.RegionRuntime.ObjectRegion.CallsValidationHook`
  - `AssetFactory.AssetDocument.RegionRuntime.ObjectRegion.ExtractsViaHook`
  - `AssetFactory.AssetDocument.RegionRuntime.ObjectRegion.DiffsCanonicalJson`
- [ ] Add named array adapter tests:
  - `AssetFactory.AssetDocument.RegionRuntime.NamedArray.RequiresArray`
  - `AssetFactory.AssetDocument.RegionRuntime.NamedArray.RequiresUniqueIdentity`
  - `AssetFactory.AssetDocument.RegionRuntime.NamedArray.RejectsMissingIdentity`
  - `AssetFactory.AssetDocument.RegionRuntime.NamedArray.CanonicalizesByIdentity`
  - `AssetFactory.AssetDocument.RegionRuntime.NamedArray.PreservesAuthoredApplyOrderWhenConfigured`
- [ ] Implement `FAssetDocumentObjectRegionAdapter` with composed hooks:
  - region-specific validation hook
  - apply hook
  - extract hook
  - diff hook override slot
- [ ] Implement `FAssetDocumentNamedArrayRegionAdapter` with composed element handlers:
  - identity field config
  - duplicate identity validation
  - canonical ordering option
  - authored-order apply option
- [ ] Refactor `AnimSequenceAssetDocumentProfile.cpp` so existing `Preview`, `Playback`, and `NotifyTracks` logic is exposed as hooks consumed by the adapters.
- [ ] Do not move unrelated AnimSequence regions in this task.
- [ ] Ensure the AnimSequence capability still returns a profile-level `IAssetDocumentCapability`; it should compose `FAssetDocumentBodyRegionDispatcher`.
- [ ] Build and run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.RegionRuntime.ObjectRegion;Automation RunTests AssetFactory.AssetDocument.RegionRuntime.NamedArray;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/ObjectNamedArrayRegions"
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AnimSequenceRegionPilot"
```

Expected result:

- `Preview` and `Playback` are dispatched through object region adapter.
- `NotifyTracks` is dispatched through named array region adapter.
- AnimSequence focused automation remains green.
- At least two existing profile regions now use the public dispatcher/runtime.

Checkpoint:

```powershell
git diff --check
git status --short
git add Source/AssetDocument/Private/Regions/AssetDocumentObjectRegionAdapter.h Source/AssetDocument/Private/Regions/AssetDocumentObjectRegionAdapter.cpp Source/AssetDocument/Private/Regions/AssetDocumentNamedArrayRegionAdapter.h Source/AssetDocument/Private/Regions/AssetDocumentNamedArrayRegionAdapter.cpp Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentProfile.cpp Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp
git commit -m "feat(assetdoc): pilot public adapters on animsequence regions"
```

## Task 5: WidgetBlueprint Region Wrappers

**Goal:** Wrap existing WidgetBlueprint private adapters behind `IAssetDocumentRegionAdapter` without rewriting their internals.

**Files:**

- `Source/AssetDocument/Private/Regions/AssetDocumentWidgetBlueprintRegionWrappers.h`
- `Source/AssetDocument/Private/Regions/AssetDocumentWidgetBlueprintRegionWrappers.cpp`
- `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.cpp`
- `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`

**Wrapper targets:**

- `FWidgetBlueprintTreeAdapter`
- `FWidgetBlueprintBindingAdapter`
- `FWidgetBlueprintAnimationAdapter`
- `FWidgetBlueprintGraphAdapter`

**Steps:**

- [ ] Record `TASK_BASE`: `$env:TASK_BASE = git rev-parse HEAD`
- [ ] Add wrapper tests using fake call counters where real widget construction is not required:
  - `AssetFactory.AssetDocument.RegionRuntime.WidgetWrapper.DelegatesValidate`
  - `AssetFactory.AssetDocument.RegionRuntime.WidgetWrapper.DelegatesPreflight`
  - `AssetFactory.AssetDocument.RegionRuntime.WidgetWrapper.DelegatesExtract`
  - `AssetFactory.AssetDocument.RegionRuntime.WidgetWrapper.DelegatesDiff`
- [ ] Implement thin wrapper classes that translate `FAssetDocumentRegionContext` plus a single desired region value into the existing WidgetBlueprint adapter call shape.
- [ ] Move only outer lifecycle dispatch to runtime; keep WidgetTree, Bindings, Animations, and Graph materializer internals unchanged.
- [ ] Update `WidgetBlueprintAssetDocumentProfile.cpp` so dispatcher sees adapter names for wrapped regions and `GetInternalAdapterNames()` includes them.
- [ ] Build and run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.RegionRuntime.WidgetWrapper;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/WidgetWrapper"
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/WidgetBlueprintWrapper"
```

Expected result:

- WidgetBlueprint existing behavior remains stable.
- Public runtime can invoke WidgetBlueprint region wrappers.
- No graph/tree/timeline internals are rewritten in this task.

Checkpoint:

```powershell
git diff --check
git status --short
git add Source/AssetDocument/Private/Regions/AssetDocumentWidgetBlueprintRegionWrappers.h Source/AssetDocument/Private/Regions/AssetDocumentWidgetBlueprintRegionWrappers.cpp Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.cpp Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp
git commit -m "feat(assetdoc): wrap widget blueprint regions for public runtime"
```

## Task 6: Profile Inspection, Guide Update, And Full Regression

**Goal:** Make the new architecture visible to future implementers and prove the public runtime does not regress existing AssetDocument behavior.

**Files:**

- `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentProfile.cpp`
- `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentProfile.cpp`
- `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.cpp`
- `docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md`

**Steps:**

- [ ] Record `TASK_BASE`: `$env:TASK_BASE = git rev-parse HEAD`
- [ ] Confirm profile inspection shows stable `RegionPolicies` plus new internal adapter names.
- [ ] Update `asset-document-new-asset-class-extension-guide.md` with:
  - when to reuse dispatcher/runtime
  - when to write a new public region adapter
  - when an asset-specific hook is acceptable
  - why concrete adapters use composition rather than inheritance
  - examples for object, named array, deferred, graph wrapper, tree wrapper, timeline wrapper
- [ ] Run source scan for anti-patterns:

```powershell
git diff -U0 $env:SPEC_BASE..HEAD -- Source/AssetDocument/Public/AssetDocumentRegion.h Source/AssetDocument/Private/AssetDocumentJsonRegionUtils.* Source/AssetDocument/Private/AssetDocumentRegionRuntime.* Source/AssetDocument/Private/AssetDocumentBodyRegionDispatcher.* Source/AssetDocument/Private/Regions | rg -n "BodyCapabilityBase|UniversalRegionAdapter|switch\\s*\\(|UWidgetBlueprint::StaticClass\\(\\)|UAnimSequence::StaticClass\\(\\)"
```

Expected scan result:

- No new `BodyCapabilityBase` or `UniversalRegionAdapter`.
- No asset-specific class references appear in runtime/dispatcher/shared adapter infrastructure.

- [ ] Run focused public runtime automation:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.RegionRuntime;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/RegionRuntimeFinal"
```

- [ ] Run focused existing profile automation:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage;Automation RunTests AssetFactory.AssetDocument.AnimSequence;Automation RunTests AssetFactory.AssetDocument.UBlueprint;Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AssetDocumentProfilesFinal"
```

- [ ] Run full AssetDocument automation:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AssetDocumentFullFinal"
```

- [ ] Run MCP tests:

```powershell
Push-Location MCP
npm test
Pop-Location
```

Expected result:

- UBT succeeds.
- Region runtime tests pass.
- Existing profile focused tests pass.
- Full `AssetFactory.AssetDocument` automation passes.
- MCP `npm test` passes.

Checkpoint:

```powershell
git diff --check
git status --short
git add Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentProfile.cpp Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentProfile.cpp Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.cpp docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md
git commit -m "docs(assetdoc): document public region runtime extension flow"
```

## Review Gates

After each task:

- [ ] Review only `$env:TASK_BASE..HEAD`.
- [ ] Verify the checkpoint commit contains only files owned by the task.
- [ ] Verify no unrelated run artifacts are committed.
- [ ] Verify any behavior change has at least one focused test.

Before final handoff:

- [ ] Run final code quality review on `$env:SPEC_BASE..HEAD`.
- [ ] Run spec compliance review on `$env:SPEC_BASE..HEAD`.
- [ ] Fix review findings in focused commits.
- [ ] Re-run affected tests after fixes.
- [ ] Report exact commands, pass/fail status, and any environment limitation.

## Acceptance Checklist

- [ ] `AssetDocumentRegion.h` exposes public context, binding, and adapter interface.
- [ ] `FAssetDocumentJsonRegionUtils` replaces repeated profile-private JSON shape/diff helpers for migrated regions.
- [ ] `FAssetDocumentRegionRuntime` has no asset-class includes or switches.
- [ ] `FAssetDocumentBodyRegionDispatcher` is composed inside existing capabilities and does not become a base class.
- [ ] At least one deferred region uses `FAssetDocumentDeferredRegionAdapter`.
- [ ] `Body.Preview` and `Body.Playback` in AnimSequence use `FAssetDocumentObjectRegionAdapter`.
- [ ] `Body.NotifyTracks` in AnimSequence uses `FAssetDocumentNamedArrayRegionAdapter`.
- [ ] WidgetBlueprint wrapper adapters route existing private adapters through public runtime without rewriting internals.
- [ ] `inspect_asset_document_profile` still reports stable body sections, region policies, and internal adapters.
- [ ] Existing public MCP contract remains unchanged.
- [ ] Full `AssetFactory.AssetDocument` automation does not regress.
- [ ] MCP `npm test` does not regress.
