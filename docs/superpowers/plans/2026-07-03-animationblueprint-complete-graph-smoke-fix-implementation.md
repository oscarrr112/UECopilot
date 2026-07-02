# AnimationBlueprint Complete Graph Smoke Fix Implementation Plan

日期：2026-07-03

状态：待执行

Source spec: `docs/superpowers/specs/2026-07-03-animationblueprint-complete-graph-smoke-fix-design.md`

## Goal

修复 `feature/asset-document-abp-complete-graph-impl` 上 AVH1 外部 HTTP smoke 暴露的真实端到端缺口，使 `UAnimBlueprint` graph-family regions 不再停留在 runtime shell / focused automation 通过状态，而是能 apply、compile、extract、diff、HTTP smoke 闭环。

完成标准来自 source spec：

- full graph apply 不再返回 `AnimBlueprintCompileFailed`。
- smoke-managed graph nodes 不再 extract 成 canonical empty graph 或 `_Skipped`。
- `/Body/AnimGraph`、`/Body/StateMachines`、`/Body/AnimLayers` 不再需要 diff allowlist。
- `ParentAssetOverrides.Node` alias 能 apply/extract/diff roundtrip。
- AVH1 external HTTP smoke 通过。

## Branch And Worktree

Spec/plan branch:

```text
branch: feature/asset-document-abp-complete-graph-smoke-fix-spec
worktree: E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/abp-complete-graph-smoke-fix-spec
base: feature/asset-document-abp-complete-graph-impl @ 3facfed453e3c22cea4fc87b69e0db1241ec79b0
```

Implementation branch to create after this plan is accepted:

```text
branch: feature/asset-document-abp-complete-graph-smoke-fix-impl
worktree: E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/abp-complete-graph-smoke-fix-impl
base: feature/asset-document-abp-complete-graph-smoke-fix-spec
```

Review ranges:

- `SPEC_BASE=3facfed453e3c22cea4fc87b69e0db1241ec79b0`
- `PLAN_BASE=<commit containing source spec>`
- each implementation task records `TASK_BASE=HEAD`
- task reviews use `TASK_BASE..HEAD`
- final review uses `SPEC_BASE..HEAD`

## Current Failure Evidence

External smoke command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File docs/superpowers/verification/run_asset_document_animationblueprint_smoke.ps1 -Project C:/AVH1/AVH1.uproject -KeepSidecar
```

Current result:

```text
success=false
code=AnimBlueprintCompileFailed
message=Failed to compile AnimBlueprint after applying Body regions
path=/Body
target=/Game/AssetDocumentSmoke/ABP_AnimationBlueprintSmoke
```

Narrowed proof:

```text
payload.Body.AnimGraph.Graphs[0].Nodes = []
payload.Body.ParentAssetOverrides = []
```

This plan treats both as red tests. No task may solve the failure by weakening the smoke fixture, deleting graph regions, or reintroducing allowlists.

## File Ownership Map

### Graph Runtime

Owned by Task 1 worker:

- `Source/AssetDocument/Private/Graphs/AssetDocumentAnimationGraphRuntime.h`
- `Source/AssetDocument/Private/Graphs/AssetDocumentAnimationGraphRuntime.cpp`
- `Source/AssetDocument/Private/Graphs/AssetDocumentAnimationGraphNodeActionProvider.h`
- `Source/AssetDocument/Private/Graphs/AssetDocumentAnimationGraphNodeActionProvider.cpp`
- `Source/AssetDocument/Private/Tests/AssetDocumentAnimationGraphRuntimeTests.cpp`

### AnimGraph Region

Owned by Task 2 worker:

- `Source/AssetDocument/Private/Regions/AssetDocumentAnimGraphRegionAdapter.h`
- `Source/AssetDocument/Private/Regions/AssetDocumentAnimGraphRegionAdapter.cpp`
- AnimGraph-specific sections of `Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp`

### State Machine And Transition Rule Region

Owned by Task 3 worker:

- `Source/AssetDocument/Private/Regions/AssetDocumentAnimStateMachineRegionAdapter.h`
- `Source/AssetDocument/Private/Regions/AssetDocumentAnimStateMachineRegionAdapter.cpp`
- StateMachine-specific sections of `Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp`

### Parent Overrides And Smoke

Owned by Task 4/5 coordinator:

- `Source/AssetDocument/Private/Regions/AssetDocumentAnimParentAssetOverrideRegionAdapter.h`
- `Source/AssetDocument/Private/Regions/AssetDocumentAnimParentAssetOverrideRegionAdapter.cpp`
- `docs/superpowers/verification/run_asset_document_animationblueprint_smoke.ps1`
- `docs/reports/asset-document-animationblueprint-complete-graph-semantics-report.md`

Shared-test edits to `AssetDocumentAnimBlueprintTests.cpp` must be coordinated after Task 1 lands to avoid overlapping patches.

## Task 0: Red Test And Diagnostic Baseline

Owner: coordinator.

Purpose: lock the current failure so later tasks cannot accidentally hide it.

Steps:

1. Record `TASK_BASE=HEAD`.
2. Add or update focused tests that currently fail or are marked expected-fail locally:
   - `AnimGraph` applied node must extract back with `Node.Id`, `Class`, `Position`.
   - `AnimGraph` without output pose/link must fail before compile or report closest graph path.
   - diff of currently applied smoke graph must show changed entries before fix.
3. Add a report note that current external smoke is red and why.
4. Do not alter production behavior except adding tests/diagnostic docs.
5. Commit:

```text
test(assetdoc): capture animation blueprint graph smoke red cases
```

Verification:

- UBT compiles tests.
- Focused red tests are documented if they cannot be committed as failing automation.

## Task 1: Graph Runtime Links, Pin Resolution, And Real Extraction Core

Owner: worker A.

Purpose: upgrade `FAssetDocumentAnimationGraphRuntime` from node-spawn shell to graph runtime that can materialize links and extract managed nodes.

Required behavior:

- Maintain an authored node identity map during apply.
- Reuse existing UE node when managed identity already exists.
- Resolve link endpoints after node reconstruction.
- Reject missing or ambiguous pins with semantic JSON Pointer paths.
- Materialize UE pin links.
- Extract managed nodes from `UEdGraph::Nodes`.
- Extract `Node.Id`, `Class`, `Position`, `Evidence.NodeGuid`.
- Extract managed links between managed nodes.
- Keep `_Skipped` only for unrepresentable non-managed nodes; smoke-managed nodes cannot be skipped.

Acceptance tests:

- Runtime fake graph test for link endpoint validation.
- Real editor graph test for managed node extraction and position roundtrip.
- Regression test that `ExtractGraph` no longer returns only `_Skipped.AnimationGraphExtractionNotMaterialized` for managed nodes.

Verification:

```powershell
Automation RunTests AssetFactory.AssetDocument.AnimationGraphRuntime
```

Checkpoint:

```text
feat(assetdoc): extract managed animation graph nodes and links
```

## Task 2: AnimGraph Compileable Pose Flow

Owner: worker B after Task 1 lands.

Purpose: make `Body.AnimGraph` apply produce a valid ABP pose flow and extract it back.

Required behavior:

- Parse and validate explicit output pose semantics, either through `OutputPose` or a schema-defined result link.
- Locate official AnimGraph result node.
- Connect authored pose output to result pose input.
- Apply reflected fields needed by the smoke node, including animation asset refs for sequence player.
- Validate target skeleton compatibility for authored animation assets when possible.
- Extract output pose semantics and managed nodes.
- Diff desired/current without canonical empty fallback.

Acceptance tests:

- `Body.AnimGraph` with one sequence player, position, asset field, and output pose compiles.
- Extract includes the sequence player node and position.
- Diff returns unchanged after apply/extract.
- Invalid/missing output pose fails before compile with `/Body/AnimGraph/Graphs/AnimGraph` path.

Verification:

```powershell
Automation RunTests AssetFactory.AssetDocument.AnimBlueprint.AnimGraph
```

Checkpoint:

```text
fix(assetdoc): make animation blueprint anim graph compile and extract
```

## Task 3: State Machine And Transition Rule Subgraph Roundtrip

Owner: worker C after Task 1 lands.

Purpose: complete `Body.StateMachines` as real recursive graph family, including transition rule subgraph ownership.

Required behavior:

- Create/lookup state machine node in root AnimGraph.
- Create/lookup `UAnimationStateMachineGraph`.
- Materialize states, transitions, entry link, positions.
- Bind each state-owned `StatePose` subgraph to its state graph.
- Bind each transition-owned `TransitionRule` subgraph to its transition graph.
- Create/repair transition result node.
- Extract states, transition links, entry metadata, subgraph identities, transition rule graph, and positions.
- Diff by graph/state/transition identity, not array index.

Acceptance tests:

- `Locomotion` with `Idle`, `Run`, `IdleToRun` applies and extracts.
- Entry state roundtrips.
- `IdleToRunRule` subgraph extracts under `/Body/StateMachines/Graphs/Locomotion/Subgraphs/IdleToRunRule`.
- Missing transition target reports exact path.

Verification:

```powershell
Automation RunTests AssetFactory.AssetDocument.AnimBlueprint.StateMachines
```

Checkpoint:

```text
fix(assetdoc): roundtrip animation blueprint state machine subgraphs
```

## Task 4: Parent Override Alias Persistence

Owner: coordinator after Task 2 lands.

Purpose: make `ParentAssetOverrides.Node` depend on real extracted AnimGraph identity rather than only apply-time deterministic GUID.

Required behavior:

- Resolve `Node` alias from managed AnimGraph extraction identity map.
- Apply `FAnimParentNodeAssetOverride.ParentNodeGuid`.
- Extract alias when GUID maps to managed node.
- Include `Evidence.ParentNodeGuid`.
- Diff path uses `/Body/ParentAssetOverrides/<Node>/NewAsset`.

Acceptance tests:

- Apply `Node="IdlePlayer"` then extract returns `Node="IdlePlayer"`.
- Diff after apply is unchanged.
- Unknown alias fails at `/Body/ParentAssetOverrides/<Node>/Node`.

Verification:

```powershell
Automation RunTests AssetFactory.AssetDocument.AnimBlueprint.AnimLayersAndParentAssetOverrides
```

Checkpoint:

```text
fix(assetdoc): persist animation blueprint parent override node aliases
```

## Task 5: Smoke Script Hardening And Allowlist Removal

Owner: coordinator after Tasks 2-4 land.

Purpose: convert smoke from diagnostic script to final gate.

Required behavior:

- Smoke sidecar includes compileable AnimGraph pose flow.
- Smoke sidecar includes StateMachine transition rule subgraph.
- Smoke verifies extracted nodes, links, positions, state machine, and parent override alias.
- Remove graph changed-entry allowlist.
- Fail if any smoke-managed graph node is `_Skipped`.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File docs/superpowers/verification/run_asset_document_animationblueprint_smoke.ps1 -Project C:/AVH1/AVH1.uproject -KeepSidecar
```

Checkpoint:

```text
test(assetdoc): require animation blueprint graph smoke roundtrip
```

## Task 6: Full Verification And Report Repair

Owner: coordinator.

Purpose: close the branch with fresh evidence.

Required verification:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload -DisableUnity
Automation RunTests AssetFactory.AssetDocument.AnimationGraphRuntime
Automation RunTests AssetFactory.AssetDocument.AnimBlueprint
Automation RunTests AssetFactory.AssetDocument
Push-Location MCP; npm test; Pop-Location
powershell -NoProfile -ExecutionPolicy Bypass -File docs/superpowers/verification/run_asset_document_animationblueprint_smoke.ps1 -Project C:/AVH1/AVH1.uproject -KeepSidecar
```

Report requirements:

- Mark `3facfed` smoke failure as previous failure evidence.
- Include fresh passing AVH1 smoke evidence.
- Do not call the branch complete if any managed authored graph surface remains unapproved deferred.
- Include real asset path and sidecar path.

Checkpoint:

```text
docs(assetdoc): report animation blueprint complete graph smoke closure
```

## Subagent Dispatch Rules

Before spawning a worker, coordinator must show:

- worker branch/worktree
- base commit
- diff range
- allowed write scope
- verification command
- whether it inherits context

Workers must not revert other workers' changes. Workers must checkpoint their task before final response.

Safe initial dispatch:

- Worker A can start Task 1 immediately because it owns graph runtime files.

Blocked dispatch:

- Worker B should wait for Task 1 because AnimGraph apply/extract depends on runtime extraction/link APIs.
- Worker C may explore state-machine UE APIs while Worker A runs, but production edits should wait until runtime link/extract contracts are stable.

## Plan Self-Review

- This plan does not weaken the smoke fixture.
- This plan does not accept `_Skipped` or canonical empty graph extraction for smoke-managed nodes.
- This plan does not reintroduce static `UAnimGraphNode_*` whitelists.
- This plan keeps ABP-specific hooks limited to graph ownership, compile/repair, and identity mapping.
- This plan treats external AVH1 HTTP smoke as a required completion gate.
