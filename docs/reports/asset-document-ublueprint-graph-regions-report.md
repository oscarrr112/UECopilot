# UBlueprint AssetDocument Graph Regions Final Report

日期：2026-06-22

## 分支与 worktree

- branch: `feature/asset-document-ublueprint-impl`
- worktree: `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-ublueprint-impl`
- validation host: `C:/AVH1`
- Task 7 base: `779d3cb43255df209184353d4d571e2708982004`
- 用户消息给出的 base `779d3cb1b33f7bf9c7f89811c8e3882148945461` 在当前 worktree 中不可解析；本 task 按实际 `HEAD` 记录 `TASK_BASE`。

## Commit Range

- UBlueprint graph implementation range against integration branch: `f81b73fc13026e86a78dbb643563ce331d0e9743..HEAD`
- Task 7 documentation/smoke range: `779d3cb43255df209184353d4d571e2708982004..HEAD`，包含本报告、schema/deferred 文档和 HTTP smoke 脚本 checkpoint。

## 已实现范围

`Body.UbergraphPages` 已作为 ordinary `/Script/Engine.Blueprint` 的 Tier 1 EventGraph region 接入 AssetDocument：

- canonical `GraphSpec` / `NodeSpec` / expanded `LinkSpec` schema 已文档化。
- compact link input sugar 仅作为输入便利形式，canonical output 仍为 expanded endpoint object。
- graph diff 会在比较前解析 `Definitions`，使等价的 inline `MemberRef` 与 `DefinitionRef` 不产生语义差异。
- apply/extract/diff 使用 `GraphCore` + K2 adapter，不走 `BlueprintGenerator` 或 BSL apply 路径。

Tier 1 node classes：

- `/Script/BlueprintGraph.K2Node_Event`
- `/Script/BlueprintGraph.K2Node_CallFunction`
- `/Script/BlueprintGraph.K2Node_VariableGet`
- `/Script/BlueprintGraph.K2Node_VariableSet`
- `/Script/BlueprintGraph.K2Node_Self`

## Unsupported Fallback

unsupported graph content 不输出 raw UE graph dump。当前 fallback diagnostic 包含：

- `Code`
- `Path`
- `Class`
- `Capability`
- `Member`
- `Reason`
- `SuggestedAction`

已有 unsupported node/function/pin pattern 会通过 validation diagnostic、extract-only `_Skipped.Graphs` evidence 或 diff `skipped` entries 暴露，避免静默吞掉 graph 差异。

## Deferred Scope

以下区域仍保持明确 unsupported/deferred：

- `Body.FunctionGraphs`
- `Body.MacroGraphs`
- `Body.Timelines`

`Body.UbergraphPages` 内仍延期的子范围包括非 Tier 1 node adapters、复杂 pin 类型、wildcard/expanded pins、复杂 literal/default object canonicalization，以及 zoom/pan、selection、open tabs 等 per-user editor-only 状态。

## Verification

本轮接力修复在 `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-ublueprint-impl` 运行的可行检查：

```powershell
git diff --check
```

结果：通过。

```powershell
rg -n "IsAnimSequencePostApplyCanonicalDivergenceRegion|bAnimSequencePostImportSidecar|AllowedPostApplyCanonicalDivergenceRegions" Source/AssetDocument/Private Source/AssetDocument/Public
```

结果：通过，未命中旧 service exception/static scan 目标。

```powershell
rg -n "Body\.UbergraphPages.*sidecar_sync|UBlueprint.*sidecar_sync|/Script/Engine\.AnimSequence.*sidecar|IsAnimSequencePostApplyCanonicalDivergenceRegion" Source/AssetDocument/Private/AssetDocumentService.cpp
```

结果：通过，未命中旧 service exception/static scan 目标。

```powershell
node --test dist/broker/*.test.js
```

结果：通过，`tests 39`、`pass 39`、`fail 0`。

本轮未把以下旧轮次结果重新声明为通过：

- UBT blocked：写入 `C:/Users/HP/AppData/Local/UnrealEngine/Intermediate/Build/UnrealBuildTool.Env.BuildConfiguration.xml` 时触发 `UnauthorizedAccessException`。
- UnrealEditor-Cmd automation exit 1：本轮没有 stdout，也没有新的 automation report 可作为通过证据。
- HTTP health refused：`127.0.0.1:8559` 未就绪，health request 被拒绝。
- fresh `npm test` / build blocked：`MCP/dist/*` 写入或 unlink 触发 `EPERM`，本轮只复用了 existing-dist 的 broker tests。

HTTP smoke sidecar:

- sidecar path: `C:/AVH1/Content/AssetDocumentSmoke/BP_GraphSidecarSmoke.assetdoc.json`
- asset path: `/Game/AssetDocumentSmoke/BP_GraphSidecarSmoke`
- asset file: `C:/AVH1/Content/AssetDocumentSmoke/BP_GraphSidecarSmoke.uasset`

Smoke graph:

- `ReceiveBeginPlay.then -> PrintString.execute`
- `PrintString.InString = "Hello from AssetDocument graph smoke"`

## Final Review Prep

主线已派发独立只读 reviewer，范围为 `73d573b398b5189dadf1365dbc45091dc296eb0a..0252a141ec13f5ca9b3ca03ddecc952226ac5db4`。本地 diff 复核与 reviewer 重点检查：

- 文档是否仍声称 `UbergraphPages` 非空未实现。
- smoke 是否使用真实 `/assetfactory/assetdocument/apply-file`、`extract`、`diff` route。
- diff 是否只允许 generated graph metadata-only changed entries。
- task diff 是否只包含允许范围文件。

Review 后已修复的阻塞项：

- graph apply 失败路径调整为在 components / class defaults 写入前执行，并在 graph 内部 apply failure 时恢复 graph snapshot，避免外层 Body 写入被半应用。
- successful apply 删除现有 graph 内容前会拒绝删除当前 Tier 1 无法表示的 existing node/graph，并返回 `UnsupportedGraphNodeClass`。
- `K2Node_CallFunction` 通过 UFunction metadata / pin FProperty 反射做保守能力检查，对 latent、custom thunk、dynamic/wildcard/container 相关函数和 unsupported pin default 返回 `UnsupportedGraphFunction` / `UnsupportedGraphPinDefault`；删除 existing call-function node 前也会复用同一函数级能力检查，避免把现有 unsupported latent node 当作普通 Tier 1 call node 静默删掉。

## Known Risks

- `RegionCanonicalizer` 已通过 `UBlueprintGraph` hook 将 generated graph metadata 归一化到可比较 hash form；sparse sidecar 不写 `GraphGuid`、`NodeGuid`、`Capability` 时，代码和测试/脚本现在拒绝 `sidecar_sync_update_skipped`，但运行时 HTTP smoke 本轮未完成，不能把 apply-file sync rewrite 记为已运行通过。
- HTTP smoke 脚本的预期仍是 apply-file 成功后拒绝 `sidecar_sync_update_skipped` payload，并继续验证语义 graph diff 无 unexpected changed/failed/skipped entries；本轮 health 未连上 `127.0.0.1:8559`，因此没有新的 smoke 通过证据。
- Apply 输入中 agent-friendly node ids 可能在 extract 中被 canonical member-based ids 替换；当前 smoke 使用可 roundtrip 的 `ReceiveBeginPlay` / `PrintString` ids，并检查 semantic member names。
- 更深层 graph semantic identity 仍需后续收敛，包括复杂重命名/重绑定场景、非 Tier 1 adapters、复杂 pin default/object canonicalization，以及 `FunctionGraphs`、`MacroGraphs`、`Timelines`；任何这些未实现区域的非空 sidecar 内容应继续明确失败或报告 unsupported。
