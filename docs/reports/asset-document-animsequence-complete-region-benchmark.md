# AssetDocument AnimSequence Complete Region Benchmark

## 范围

- Worktree: `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-animsequence-impl`
- Branch: `feature/asset-document-animsequence-impl`
- SPEC_BASE: `9b9d6ea7cd24f4733a4948100ef8a63fad3c3e55`
- Implementation verification HEAD: `01f9c0e7e854e6a46527db7930879eb32a43d512`
- Validation host: `C:/AVH1/AVH1.uproject`
- Sidecar model: post-import AssetDocument sidecar. This work does not replace FBX/import/reimport behavior and does not add AnimSequence-specific MCP tools.

## Complete AnimSequence Regions

- `Body.References`
- `Body.Preview`
- `Body.Playback`
- `Body.Additive`
- `Body.RootMotion`
- `Body.Compression`
- `Body.Curves`
- `Body.Notifies`
- `Body.NotifyStates`
- `Body.NotifyTracks`
- `Body.SyncMarkers`
- `Body.Metadata`
- `Body.AssetUserData`

这些区域是 AnimSequence-owned authored state，走通了 profile registration、schema exposure、validate/preflight/apply、extract、diff 和 sidecar roundtrip。实现仍保持稀疏 default-diff sidecar：未声明的 body region 不会被隐式重写，extract/diff-only 路径会忽略生产端标记出来的 `_Skipped` entries。

## Explicitly Excluded Fields

- raw animation track data
- import settings and reimport pipeline state
- compressed animation output and codec-derived data
- derived/cache-only data
- referenced asset internals
- transient/editor-only runtime state
- user-authored `_Skipped` body entries

这些字段不属于本轮 AnimSequence AssetDocument authoring surface。外部导入仍由 UE 原生导入流程负责；AssetDoc 只处理导入后资产上的可维护 authored sidecar。

## Fresh Verification

- UBT validation host:
  - Command: `"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload`
  - Result: exit 0, `Result: Succeeded`, target was up to date, total time `0.57` seconds.
- Focused AnimSequence automation:
  - Command: `UnrealEditor-Cmd.exe C:/AVH1/AVH1.uproject -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath=C:/AVH1/Saved/AutomationReports/AnimSequenceFinal`
  - Result: exit 0, `succeeded: 4`, `succeededWithWarnings: 4`, `failed: 0`, `notRun: 0`, `tests: 8`, `duration: 0.35912469029426575`.
  - Report path: `C:/AVH1/Saved/AutomationReports/AnimSequenceFinal/index.json`
- Full AssetDocument automation:
  - Command: `UnrealEditor-Cmd.exe C:/AVH1/AVH1.uproject -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath=C:/AVH1/Saved/AutomationReports/AssetDocumentFinal`
  - Result: exit 0, `succeeded: 60`, `succeededWithWarnings: 12`, `failed: 0`, `notRun: 0`, `tests: 72`, `duration: 1.7511979341506958`.
  - Report path: `C:/AVH1/Saved/AutomationReports/AssetDocumentFinal/index.json`
- MCP npm:
  - Command: `npm test` in `MCP/`
  - Result: exit 0, TAP `39/39` pass, `fail 0`.
  - Command: `npm run build` in `MCP/`
  - Result: exit 0, `tsc`.
- External HTTP smoke:
  - Editor launch: `UnrealEditor.exe C:/AVH1/AVH1.uproject`
  - Health endpoint: `{"status":"ok","service":"AssetFactory","port":8559,"subsystemAvailable":true}`
  - Command: `py docs/superpowers/verification/asset_document_animsequence_http_smoke.py --project C:/AVH1/AVH1.uproject --port 8559`
  - Result: exit 0, `AnimSequence AssetDocument HTTP smoke passed`.
  - Created/updated asset: `/Game/AssetDocumentSmoke/AS_PostImportSidecarSmoke`
  - Asset file: `C:/AVH1/Content/AssetDocumentSmoke/AS_PostImportSidecarSmoke.uasset`
  - Preserved sidecar: `C:/AVH1/Content/AssetDocumentSmoke/AS_PostImportSidecarSmoke.assetdoc.json`
  - Apply payload: `{"sidecar_file_path":"C:/AVH1/Content/AssetDocumentSmoke/AS_PostImportSidecarSmoke.assetdoc.json","sidecar_sync_update_skip_reason":"Post-apply asset evidence hash differs for region 'Body.Additive'","sidecar_sync_update_skipped":true,"triggered_by_watcher":false}`
- Cleanup checks:
  - Command: `git diff --check`
  - Result: exit 0.
  - Command: `git status --short`
  - Result before report commit: only `docs/reports/asset-document-animsequence-complete-region-benchmark.md` was untracked.

## Task Review Closure

- Task 1 profile/schema review passed after hardening empty stub semantics.
- Task 2 scalar-region review passed after validation was tightened for object types, booleans, compression shape, and partial mutation.
- Task 3 curves review passed after invalid curve payloads were rejected before mutation.
- Task 4 notify-region review passed after staged validation avoided partial mutation and object leaks.
- Task 5 metadata/user-data review passed after `AssetUserData` class semantics, object identity, deep validation, and rollback were covered.
- Task 6 sidecar roundtrip review passed after the diff-only `_Skipped` path gained explicit regression coverage.
- Final branch review range: `9b9d6ea7cd24f4733a4948100ef8a63fad3c3e55..HEAD`, to be run after this report checkpoint.

## Inspectable Smoke Artifact

- In editor: open validation host `C:/AVH1/AVH1.uproject`, then inspect `Content/AssetDocumentSmoke/AS_PostImportSidecarSmoke`.
- On disk: compare `C:/AVH1/Content/AssetDocumentSmoke/AS_PostImportSidecarSmoke.uasset` with `C:/AVH1/Content/AssetDocumentSmoke/AS_PostImportSidecarSmoke.assetdoc.json`.
- The smoke path intentionally uses an already-created AnimSequence asset plus `assetdocument/apply-file`, then verifies extract/diff through the generic AssetDocument HTTP surface.

## Residual Risk

- Focused and full automation suites completed with warnings from transient animation compression/fixture behavior; no tests failed.
- The external HTTP smoke deliberately preserved the sidecar under `Content` for inspection. The apply-file response reported `sidecar_sync_update_skipped` because `Body.Additive` evidence hash differed after apply; the authored apply/extract/diff checks still passed for the smoke target.
- This implementation does not attempt raw animation import, reimport, or compressed data authoring. Those remain UE/importer responsibilities by design.
