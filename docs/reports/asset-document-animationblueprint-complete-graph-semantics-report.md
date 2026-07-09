# AnimationBlueprint AssetDocument Complete Graph Semantics Report

日期：2026-07-10

## 当前基线

- integration branch: `feature/asset-document-structured-capabilities-spec`
- integration worktree: `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-structured-capabilities-spec`
- merged implementation branch: `feature/asset-document-abp-complete-graph-smoke-fix-impl`
- merge commit: `1e85176 merge: animation blueprint graph smoke fix into structured capabilities`
- smoke-fix implementation checkpoint: `15bdd30 test(assetdoc): harden animation blueprint graph smoke`
- AVH1 validation host: `C:/AVH1/AVH1.uproject`
- AVH1 plugin junction: `C:/AVH1/Plugins/AssetFactory -> E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-structured-capabilities-spec`

## 已实现范围

AnimationBlueprint graph-family regions 已从 focused automation 形态推进到 AVH1 external HTTP smoke 可闭环的语义形态：

- `Body.AnimGraph` 使用 recursive graph region，覆盖 graph/node/link/subgraph、layout position、managed node identity、authored field delta 和 output pose link。
- `Body.StateMachines` 使用 recursive graph region，覆盖 state、transition、transition rule subgraph、entry state、state/transition layout 和 transition rule graph。
- `Body.AnimLayers` 接入 graph region，不再是 empty-only / deferred gate。
- `Body.FunctionGraphs`、`Body.MacroGraphs` 继续走 Blueprint common graph adapter。
- animation graph runtime 支持动态 node action provider / `UBlueprintNodeSpawner` materialization、公共字段规则、structural hook、subgraph walk 和 managed node identity reuse。
- node 字段规则抽成公共 rule/trait/staged apply utility，避免按 `UAnimGraphNode_*` 逐类硬编码。
- `Body.ParentAssetOverrides` 支持 `Node` alias identity，运行时用 managed node object name / deterministic node guid 解析，`ParentNodeGuid` 仅作为 fallback。
- smoke-managed graph nodes 不再 extract 成 canonical empty graph，也不再通过 `_Skipped` 或 diff allowlist 掩盖。

## 历史失败基线

`3facfed453e3c22cea4fc87b69e0db1241ec79b0` 仍作为本轮 smoke-fix 的红基线证据保留。当时 AVH1 external HTTP smoke 已经能启动 HTTP server，但 full graph apply 会返回：

```text
success=false
code=AnimBlueprintCompileFailed
message=Failed to compile AnimBlueprint after applying Body regions
path=/Body
target=/Game/AssetDocumentSmoke/ABP_AnimationBlueprintSmoke
```

窄化输入还能进一步证明当时 extract 会把 authored graph node 和 `ParentAssetOverrides.Node = "IdlePlayer"` 抽成空结果；该失败不是 transport 问题，而是 graph apply/extract fidelity 缺口。现在这部分只作为 previous failure evidence，不再代表当前基线状态。

## Fresh Verification

UBT：

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload -DisableUnity
```

结果：通过，`Result: Succeeded`。本次对 AVH1 validation host 做了实际 rebuild，完成 `182` actions，总耗时约 `37.97s`。

Animation graph runtime 自动化：

```powershell
Automation RunTests AssetFactory.AssetDocument.AnimationGraphRuntime
```

结果：通过，进程 exit code `0`；日志确认 found `28` automation tests，`28` 个 `Result={Success}`，`0` fail/error。

AnimationBlueprint profile 自动化：

```powershell
Automation RunTests AssetFactory.AssetDocument.AnimBlueprint
```

结果：通过，进程 exit code `0`；日志确认 found `9` automation tests，`9` 个 `Result={Success}`，`0` fail/error。覆盖：

- `AnimGraph`
- `AnimLayersAndParentAssetOverrides`
- `BlueprintCommonRegions`
- `CoreObjectRegions`
- `CreateUpdateLifecycle`
- `DeferredGraphGates`
- `ProfileShape`
- `StateMachines`
- `SyncGroups`

完整 AssetDocument 自动化：

```powershell
Automation RunTests AssetFactory.AssetDocument
```

结果：通过，进程 exit code `0`；日志确认 found `336` automation tests，`336` 个 `Result={Success}`，`0` fail/error。

MCP：

```powershell
Push-Location MCP
npm test
Pop-Location
```

结果：通过，Node test runner 输出 `tests 39`、`pass 39`、`fail 0`。`npm test` 触碰到的 `MCP/dist` line-ending 工作区噪声已作为 run artifact 恢复，不纳入 checkpoint。

External HTTP smoke：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File docs/superpowers/verification/run_asset_document_animationblueprint_smoke.ps1 -Project C:/AVH1/AVH1.uproject -KeepSidecar
```

结果：通过，脚本输出：

```text
AnimationBlueprint AssetDocument HTTP smoke passed
```

真实资产与 sidecar：

- asset: `/Game/AssetDocumentSmoke/ABP_AnimationBlueprintSmoke`
- sidecar: `C:/AVH1/Content/AssetDocumentSmoke/ABP_AnimationBlueprintSmoke.assetdoc.json`

HTTP smoke 本轮验证了 `apply-file -> save -> extract -> diff` 闭环。`apply-file` payload 返回 `success=true`、`saved_asset=true`、`triggered_by_watcher=false`。保留的 sidecar 中仍可见 `IdlePlayer`、`StateMachines`、`AnimLayers` 和 `ParentAssetOverrides.Node = "IdlePlayer"`，并且没有 `_Skipped` entry。

## 当前结论

`feature/asset-document-abp-complete-graph-smoke-fix-impl` 已 merge 回 `feature/asset-document-structured-capabilities-spec`，并且当前基线通过了 Task 6 要求的 UBT、focused automation、full AssetDocument automation、MCP tests 和 AVH1 external HTTP smoke。

因此，`2026-07-03-animationblueprint-complete-graph-smoke-fix-implementation.md` 的 smoke-fix closure 已完成。当前 smoke-managed ABP graph surface 没有未获批准的 authored deferred / `_Skipped` / diff allowlist 作为完成前提。
