# AssetDocument AnimMontage Complete Region Benchmark 最终证据报告

## 范围

- Worktree: `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-structured-capabilities-spec`
- Branch: `feature/asset-document-structured-capabilities-spec`
- Full implementation range: `dbc7cf9..bdc7c2df802eeeb9bd18db32e5917c6529d9e07f`
- Task 7 fix range: `7e7d0d62841ee5d7b0b95882d35f4b1dffd5d00a..bdc7c2df802eeeb9bd18db32e5917c6529d9e07f`

## Review Finding 修复

- P1 partial mutation: 新增 `AssetFactory.AssetDocument.AnimMontage.ApplyExtract.CurvesAndTimeStretch` 回归覆盖。用已有 montage，sidecar 同时提供 `Body.Sync.SyncGroup = "ShouldNotApply"`、`Body.RootMotion` 变更，以及 invalid `Body.TimeStretch.TimeStretchCurveName = "MissingCurve"`。修复后 apply 失败且 `SyncGroup`、root motion translation/rotation/root lock 均保持原值。
- P2 marker cache refresh: 未直接调用 `UAnimMontage::CollectMarkers()`。UE 5.7 头文件显示 `UAnimMontage` 是 `MinimalAPI`，`CollectMarkers()` 没有单独 `ENGINE_API` 导出；直接跨模块调用有链接风险。本轮保留本地 `RepairDerivedMontageMarkerCacheAfterApply()`，并明确它只是 sync settings apply 后的 derived marker cache repair，不是 AssetDoc authoring surface。
- 文档一致性: `docs/superpowers/specs/2026-06-13-asset-document-bidirectional-delta-sidecar-goal.md` 已更新 complete region 列表和 RootMotion/Curves/TimeStretch 边界。

## P1 RED/GREEN

- RED command:
  - `UnrealEditor-Cmd.exe C:/AVH1/AVH1.uproject -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage.ApplyExtract.CurvesAndTimeStretch;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath=C:/AVH1/Saved/AutomationReports/Task7P1Red`
  - Result: exit 1，`failed: 1`
  - Evidence: `Task7P1Red/index.json` 报 `SyncGroup` 从 `None` 变成 `ShouldNotApply`，root motion translation/rotation/root lock 也被改动。
- GREEN command:
  - `UnrealEditor-Cmd.exe C:/AVH1/AVH1.uproject -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage.ApplyExtract.CurvesAndTimeStretch;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath=C:/AVH1/Saved/AutomationReports/Task7P1Green`
  - Result: exit 0，`succeeded: 1, failed: 0`

## Fresh Verification

- UBT validation host:
  - Command: `"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload`
  - Result: exit 0，`Result: Succeeded`
- Full AnimMontage automation:
  - Command: `UnrealEditor-Cmd.exe C:/AVH1/AVH1.uproject -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath=C:/AVH1/Saved/AutomationReports/Task7AnimMontageFull`
  - Result: exit 0，`succeeded: 29, succeededWithWarnings: 2, failed: 0`
- Full AssetDocument automation:
  - Command: `UnrealEditor-Cmd.exe C:/AVH1/AVH1.uproject -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath=C:/AVH1/Saved/AutomationReports/Task7AssetDocumentFull`
  - Result: exit 0，`succeeded: 57, succeededWithWarnings: 7, failed: 0`
- MCP npm:
  - Root command: `npm test`
  - Root result: exit 1，根目录没有 `package.json`，`ENOENT`
  - MCP command: `npm test` in `MCP/`
  - MCP result: exit 0，`pass: 39, fail: 0`
- Smoke script:
  - Path: `docs/superpowers/verification/asset_document_delta_sidecar_smoke.py`
  - Montage target asset path: `/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke`
  - Generated animation source asset path: `/Game/Generated/Animation/AS_AssetDocSmoke`
  - Fresh command attempted: `UnrealEditor-Cmd.exe C:/AVH1/AVH1.uproject -Unattended -NullRHI -ExecutePythonScript=.../asset_document_delta_sidecar_smoke.py -LogCmds="LogPython verbose"`
  - Result: BLOCKED. UE log shows HTTP server started on port `8559`, then Python timed out waiting for in-process `/assetdocument/apply`; no smoke pass marker was emitted. Fresh smoke is not counted as a passing verification in this report.
- Cleanup checks:
  - `git diff --check`: exit 0，仅报告 CRLF normalization warnings。
  - `git status --short`: run before checkpoint commit; only本轮允许范围文件为 modified。

## Complete AnimMontage Regions

- `Body.Blend`
- `Body.SlotAnimTracks`
- `Body.CompositeSections`
- `Body.Notifies`
- `Body.NotifyStates`
- `Body.Sync`
- `Body.RootMotion`
- `Body.References`
- `Body.Preview`
- `Body.Metadata`
- `Body.SectionMetadata`
- `Body.TimeStretch`
- `Body.Curves`

## Excluded Derived/Cache Fields

- `MarkerData.AuthoredSyncMarkers`
- `MarkerData.UniqueMarkerNames`
- `BranchingPointMarkers`
- `BranchingPointStateNotifyIndices`
- `BranchingPoints_DEPRECATED`
- referenced sequence curves
- referenced sequence root motion flags
- baked `TimeStretchCurve.Markers`
- baked `TimeStretchCurve.Sum_dT_i_by_C_i`

`MarkerData` 仍是 excluded derived/cache data。AssetDoc 只 author `Body.Sync` 的 sync settings；在这些 settings 或 slot track apply 后，生产代码刷新 UE marker cache，避免 runtime evidence stale。

## 剩余风险

- 根目录没有 `package.json`，所以 repo root `npm test` 不是有效 MCP 测试入口；实际 MCP 测试入口为 `MCP/npm test`。
- `asset_document_delta_sidecar_smoke.py` 通过 `-ExecutePythonScript` 在同一 editor-cmd 进程里调用本进程 HTTP endpoint 会超时；若需要 fresh smoke pass，建议用已启动并可轮询健康状态的 Editor 实例，从外部 Python/HTTP 客户端驱动 smoke，或把 smoke 改成不经本进程 HTTP 的 automation 测试。
