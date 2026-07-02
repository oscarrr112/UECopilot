# AnimationBlueprint AssetDocument Complete Graph Semantics Report

日期：2026-07-02

## 分支与 Worktree

- branch: `feature/asset-document-abp-complete-graph-impl`
- worktree: `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/abp-complete-graph-impl`
- base branch: `feature/asset-document-abp-complete-graph-plan`
- automation validation host: `C:/Users/HP/.config/superpowers/validation-hosts/abp-complete-graph-impl-worker-a/ABPGraphWorkerA.uproject`
- external smoke validation host: `C:/AVH1/AVH1.uproject`

## 已实现范围

本轮把 AnimationBlueprint 的图语义从第一版保守 profile 推进到 recursive graph family：

- `Body.AnimGraph` 改为 `{ Graphs: [...] }` 递归图 region，支持 graph/node/link/subgraph 的统一 schema。
- `Body.StateMachines` 改为递归图 region，state、transition、transition rule subgraph 走同一 graph schema。
- `Body.AnimLayers` 接入 graph region，不再是 deferred empty-only。
- `Body.FunctionGraphs`、`Body.MacroGraphs` 接入 Blueprint common graph adapter。
- graph core 支持 recursive graph region validation、definition refs、stable graph diff path 和 shared graph fields。
- animation graph runtime shell 支持动态 node action provider / NodeSpawner 发现、node materialization、字段规则、structural hook、subgraph walk。
- node 字段规则抽成公共 rule/trait/staged apply utility，而不是按节点类穷举。
- parent asset override 支持 `Node` alias 身份，运行时用 managed node object name 和 deterministic node guid 解析；`ParentNodeGuid` 仍保留为 fallback。
- deferred 文档已更新：ABP graph-family 旧的 empty-only gate 已关闭，剩余 deferred 只保留 obsolete side-list、debug/cache/derived/editor-only 等边界。

## Checkpoint Chain

- `9fc783f docs: plan animation blueprint complete graph implementation`
- `5f02a56 feat(assetdoc): add recursive graph family core`
- `4c7a9a9 fix(assetdoc): tighten recursive graph core validation`
- `7e0c019 fix(assetdoc): preserve shared graph fields in graph arrays`
- `61fa5d2 feat(assetdoc): add graph field rule utilities`
- `75115d3 fix(assetdoc): align graph field rule contracts`
- `580996a feat(assetdoc): add animation graph runtime shell`
- `05b7eb8 fix(assetdoc): tighten animation graph runtime shell`
- `c4cf549 feat(assetdoc): route anim graph through recursive schema`
- `f816c59 fix(assetdoc): harden recursive anim graph diagnostics`
- `f77fbc5 feat(assetdoc): add anim graph node action provider`
- `80a1ac4 feat(assetdoc): materialize anim graph nodes`
- `9f7f64b feat(assetdoc): materialize anim state machines`
- `b1a66f7 feat(assetdoc): support anim layer and blueprint graph regions`
- `b1761ed feat(assetdoc): resolve parent overrides by graph node identity`
- `dddc3b6 docs(assetdoc): close animation blueprint graph deferred gates`
- `2cc5e04 docs(assetdoc): report animation blueprint graph smoke status`

## Verification

UBT：

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" UnrealEditor Win64 Development "-Project=C:/Users/HP/.config/superpowers/validation-hosts/abp-complete-graph-impl-worker-a/ABPGraphWorkerA.uproject" -NoHotReload -DisableUnity
```

结果：通过，`Result: Succeeded`。

Graph core 自动化：

```powershell
Automation RunTests AssetFactory.AssetDocument.GraphCore
```

结果：通过，当前日志确认 `22` 个 `GraphCore` 用例 `Result={Success}`。

Animation graph runtime 自动化：

```powershell
Automation RunTests AssetFactory.AssetDocument.AnimationGraphRuntime
```

结果：通过，当前日志确认 `15` 个 `AnimationGraphRuntime` 用例 `Result={Success}`，覆盖 field rules、NodeSpawner boundary、structural hook、subgraph walk、managed node identity。

AnimationBlueprint profile 自动化：

```powershell
Automation RunTests AssetFactory.AssetDocument.AnimBlueprint
```

结果：通过，当前日志确认 `9` 个用例 `Result={Success}`：

- `AnimGraph`
- `AnimLayersAndParentAssetOverrides`
- `BlueprintCommonRegions`
- `CoreObjectRegions`
- `CreateUpdateLifecycle`
- `DeferredGraphGates`
- `ProfileShape`
- `StateMachines`
- `SyncGroups`

MCP：

```powershell
npm test
```

运行目录：`MCP/`。结果：通过，`tests=39`、`pass=39`、`fail=0`。`npm test` 生成的 `MCP/dist` 已恢复，未纳入本次 diff。

Smoke script parser：

```powershell
[System.Management.Automation.PSParser]::Tokenize((Get-Content docs/superpowers/verification/run_asset_document_animationblueprint_smoke.ps1 -Raw), [ref]$null)
```

结果：PowerShell parser 通过。

AVH1 UBT：

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload -DisableUnity
```

验证前确认 `C:/AVH1/Plugins/AssetFactory` junction 已指向当前 worktree：

```text
E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/abp-complete-graph-impl
```

结果：通过，`Result: Succeeded`。本次不是 up-to-date，UBT 实际重新编译了当前分支的 `AssetDocumentAnimGraphRegionAdapter`、`AssetDocumentAnimationGraphRuntime`、`AssetDocumentAnimBlueprintTests` 等文件。

## External HTTP Smoke Status

`docs/superpowers/verification/run_asset_document_animationblueprint_smoke.ps1` 已迁移到新的 recursive graph schema：

- `Body.AnimGraph` 使用 `{ Graphs: [...] }`
- `Body.StateMachines` 使用 state、transition、transition rule subgraph
- `Body.AnimLayers` 使用 graph region
- `Body.ParentAssetOverrides` 使用 `Node = "IdlePlayer"`

AVH1 外部 HTTP smoke 未闭环，但失败点已经从临时 host 的 health 问题推进到真实 ABP 语义问题：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File docs/superpowers/verification/run_asset_document_animationblueprint_smoke.ps1 -Project C:/AVH1/AVH1.uproject -KeepSidecar
```

结果：失败，`/assetfactory/health` 已可用，`apply-file` 返回：

```text
success=false
code=AnimBlueprintCompileFailed
message=Failed to compile AnimBlueprint after applying Body regions
path=/Body
target=/Game/AssetDocumentSmoke/ABP_AnimationBlueprintSmoke
```

为了排除脚本语法和 HTTP transport 问题，做过一次临时窄化验证：只保留 `AnimGraph` 的 `IdlePlayer` node、空 `StateMachines` / `AnimLayers` 和 `ParentAssetOverrides.Node = "IdlePlayer"`。该窄化输入可以通过 `apply-file` 和 `extract`，但 extract payload 显示：

- `payload.Body.AnimGraph.Graphs[0].Nodes = []`
- `payload.Body.ParentAssetOverrides = []`

这和代码现状吻合：`FAssetDocumentAnimGraphRegionAdapter::ExtractRegion` 当前固定返回 canonical empty graph，尚不能抽取 authored graph nodes。因此脚本目前对 diff 中 `/Body/AnimGraph`、`/Body/StateMachines`、`/Body/AnimLayers` 的 changed entries 仍有 allowlist；这不是最终完成态，只是把当前 graph extract/diff fidelity 缺口显式暴露出来。

## 当前结论

代码侧的 recursive graph schema、动态 NodeSpawner/materialization、公共字段规则、state machine subgraph、anim layer/function/macro graph region、parent override node alias 已完成并通过 focused automation。

尚不能把这条分支标记为“外部端到端完全闭环”：AVH1 已证明 HTTP server 可用，但 full graph apply 会触发 `AnimBlueprintCompileFailed`，窄化输入又证明 authored graph node / parent override alias 不能被 extract roundtrip。后续需要补齐 real graph extraction fidelity、parent override persisted extraction，以及 full graph apply 后的 ABP compile repair，再取消 smoke 脚本中的 graph changed allowlist。
