# AnimationBlueprint AssetDocument Complete Graph Semantics Report

日期：2026-07-02

## 分支与 Worktree

- branch: `feature/asset-document-abp-complete-graph-impl`
- worktree: `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/abp-complete-graph-impl`
- base branch: `feature/asset-document-abp-complete-graph-plan`
- validation host: `C:/Users/HP/.config/superpowers/validation-hosts/abp-complete-graph-impl-worker-a/ABPGraphWorkerA.uproject`

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

## External HTTP Smoke Status

`docs/superpowers/verification/run_asset_document_animationblueprint_smoke.ps1` 已迁移到新的 recursive graph schema：

- `Body.AnimGraph` 使用 `{ Graphs: [...] }`
- `Body.StateMachines` 使用 state、transition、transition rule subgraph
- `Body.AnimLayers` 使用 graph region
- `Body.ParentAssetOverrides` 使用 `Node = "IdlePlayer"`

当前外部 HTTP smoke 未闭环：脚本启动普通 `UnrealEditor` 后，`http://127.0.0.1:8559/assetfactory/health` 在等待窗口内没有变为 reachable。对应 UE log 没有出现 AssetFactory HTTP server 初始化行，因此本次失败点是 validation host 的普通 Editor HTTP 服务未起来；不是 apply/extract/diff 逻辑已经失败。

还有一个需要明确保留的缺口：脚本目前对 diff 中 `/Body/AnimGraph`、`/Body/StateMachines`、`/Body/AnimLayers` 的 changed entries 做了 allowlist。这表示外部 smoke 即使跑到 diff 阶段，也还不能证明 authored graph node 内容已经完整 extract/diff roundtrip；它只能证明非 graph 区域和 parent override alias 没有异常 diff。自动化测试已覆盖 schema、apply/materialize、diagnostic 和 profile gates，但外部端到端 roundtrip 还需要继续补 graph extraction fidelity。

## 当前结论

代码侧的 recursive graph schema、动态 NodeSpawner/materialization、公共字段规则、state machine subgraph、anim layer/function/macro graph region、parent override node alias 已完成并通过 focused automation。

尚不能把这条分支标记为“外部端到端完全闭环”：`UnrealEditor` HTTP smoke 未跑通 health，且 authored graph extract/diff fidelity 仍需补齐后再取消 smoke 脚本中的 graph changed allowlist。
