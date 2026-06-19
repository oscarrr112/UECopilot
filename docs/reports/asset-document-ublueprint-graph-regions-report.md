# UBlueprint AssetDocument Graph Regions Final Report

日期：2026-06-20

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

在 `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-ublueprint-impl` 运行：

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
```

结果：exit 0；`Target is up to date`；`Result: Succeeded`。

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.GraphCore;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/GraphCoreFinal"
```

结果：exit 0；`C:/AVH1/Saved/AutomationReports/GraphCoreFinal/index.json` 记录 `succeeded=17`、`failed=0`、`notRun=0`。

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/UBlueprintGraphFinal"
```

结果：exit 0；`C:/AVH1/Saved/AutomationReports/UBlueprintGraphFinal/index.json` 记录 `succeeded=52`、`failed=0`、`notRun=0`。

```powershell
Push-Location MCP; npm test; Pop-Location
```

结果：exit 0；Node test runner 记录 `tests 39`、`pass 39`、`fail 0`。`npm test` 生成的 `MCP/dist` 构建噪声已恢复，未纳入本 task diff。

```powershell
python docs/superpowers/verification/asset_document_ublueprint_graph_http_smoke.py --base-url http://127.0.0.1:8559
```

结果：exit 0。脚本临时启动 `C:/AVH1/AVH1.uproject` editor server，完成 `/assetfactory/assetdocument/apply-file` -> `/assetfactory/assetdocument/extract` -> `/assetfactory/assetdocument/diff`。

HTTP smoke sidecar:

- sidecar path: `C:/AVH1/Content/AssetDocumentSmoke/BP_GraphSidecarSmoke.assetdoc.json`
- asset path: `/Game/AssetDocumentSmoke/BP_GraphSidecarSmoke`
- asset file: `C:/AVH1/Content/AssetDocumentSmoke/BP_GraphSidecarSmoke.uasset`

Smoke graph:

- `ReceiveBeginPlay.then -> PrintString.execute`
- `PrintString.InString = "Hello from AssetDocument graph smoke"`

## Final Review Prep

本 worker 环境没有可调用的 subagent dispatch/review tool；未能派发独立只读 reviewer。已进行本地 diff 复核，重点检查：

- 文档是否仍声称 `UbergraphPages` 非空未实现。
- smoke 是否使用真实 `/assetfactory/assetdocument/apply-file`、`extract`、`diff` route。
- diff 是否只允许 generated graph metadata-only changed entries。
- task diff 是否只包含允许范围文件。

## Known Risks

- HTTP apply payload 仍返回 `sidecar_sync_update_skipped=true`，原因是 `Post-apply asset evidence hash differs for region 'Body.UbergraphPages'`。当前 smoke 会证明语义 graph diff 无 unexpected changed/failed/skipped entries，但 sidecar sync evidence canonicalization 仍是后续风险。
- Sparse sidecar 不写 `GraphGuid`、`NodeGuid`、`Capability` 时，file diff 会产生 metadata-only changed entries。HTTP smoke 已对这些 generated metadata 做归一化过滤，但这也说明后续需要决定它们是否应由 sidecar sync 自动回写或由 diff 默认忽略。
- Apply 输入中 agent-friendly node ids 可能在 extract 中被 canonical member-based ids 替换；当前 smoke 使用可 roundtrip 的 `ReceiveBeginPlay` / `PrintString` ids，并检查 semantic member names。
- `FunctionGraphs`、`MacroGraphs`、`Timelines` 仍未实现；任何非空 sidecar 内容应继续明确失败或报告 unsupported。
