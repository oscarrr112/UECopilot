# AssetDocument AnimSequence Complete Region Benchmark

## 范围

- Worktree: `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-animsequence-impl`
- Branch: `feature/asset-document-animsequence-impl`
- SPEC_BASE: `9b9d6ea7cd24f4733a4948100ef8a63fad3c3e55`
- Implementation verification HEAD: final checkpoint commit containing this report.
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

这些区域是 AnimSequence-owned authored state，走通了 profile registration、schema exposure、validate/preflight/apply、extract、diff 和 sidecar roundtrip。实现仍保持稀疏 sidecar：未声明的 body region 不会被隐式重写，extract/diff-only 路径会忽略生产端标记出来的 `_Skipped` entries。`Body.Curves` 当前是 sequence-owned float curve sparse add/update patch；显式 delete/clear 暂列 deferred。

## Explicitly Excluded Fields

- raw animation track data
- import settings and reimport pipeline state
- compressed animation output and codec-derived data
- derived/cache-only data
- referenced asset internals
- transient/editor-only runtime state
- user-authored `_Skipped` body entries
- `PreviewPoseAsset` authoring

这些字段不属于本轮 AnimSequence AssetDocument authoring surface。外部导入仍由 UE 原生导入流程负责；AssetDoc 只处理导入后资产上的可维护 authored sidecar。

## Fresh Verification

- UBT validation host:
  - Command: `"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload`
  - Result: exit 0, `Result: Succeeded`, latest fix build total time `4.36` seconds.
- Focused AnimSequence automation:
  - Command: `UnrealEditor-Cmd.exe C:/AVH1/AVH1.uproject -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath=C:/AVH1/Saved/AutomationReports/AnimSequenceFixReview7`
  - Result: exit 0, `Success: 8`, `failed: 0`, `tests: 8`.
  - Report path: `C:/AVH1/Saved/AutomationReports/AnimSequenceFixReview7/index.json`
- Focused AnimMontage sync regression:
  - Command: `UnrealEditor-Cmd.exe C:/AVH1/AVH1.uproject -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage.ApplyFile.SyncState;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath=C:/AVH1/Saved/AutomationReports/AnimMontageSyncRegression3`
  - Result: exit 0.
- Full AssetDocument automation:
  - Command: `UnrealEditor-Cmd.exe C:/AVH1/AVH1.uproject -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath=C:/AVH1/Saved/AutomationReports/AssetDocumentFinal4`
  - Result: exit 0, `Success: 72`, `failed: 0`, `tests: 72`.
  - Report path: `C:/AVH1/Saved/AutomationReports/AssetDocumentFinal4/index.json`
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
  - Apply payload: `{"sidecar_file_path": "C:/AVH1/Content/AssetDocumentSmoke/AS_PostImportSidecarSmoke.assetdoc.json", "triggered_by_watcher": false}`
  - Sync assertion: smoke requires top-level `wrote_sidecar == true` and fails if payload contains `sidecar_sync_update_skipped`.
- Cleanup checks:
  - Command: `git diff --check`
  - Result: exit 0 after review fixes.
  - Command: `git status --short`
  - Result before final checkpoint: only this task's AssetDocument/source/docs/smoke/report files were modified.

## Task Review Closure

- Task 1 profile/schema review passed after hardening empty stub semantics.
- Task 2 scalar-region review passed after validation was tightened for object types, booleans, compression shape, and partial mutation.
- Task 3 curves review passed after invalid curve payloads were rejected before mutation.
- Task 4 notify-region review passed after staged validation avoided partial mutation and object leaks.
- Task 5 metadata/user-data review passed after `AssetUserData` class semantics, object identity, deep validation, and rollback were covered.
- Task 6 sidecar roundtrip review passed after the diff-only `_Skipped` path gained explicit regression coverage.
- Final branch review found four contract issues and they are now addressed:
  - `ApplyFile` no longer reports `sidecar_sync_update_skipped` for AnimSequence post-import sidecars. The post-apply canonical divergence exception is restricted to AnimSequence regions that canonicalize or rebuild data (`Body.Additive`, `Body.Compression`, `Body.Curves`, `Body.Notifies`, `Body.NotifyStates`, `Body.NotifyTracks`, `Body.SyncMarkers`, `Body.Metadata`, `Body.AssetUserData`); strict regions (`Body.References`, `Body.Preview`, `Body.Playback`, `Body.RootMotion`) are tested to keep `sidecarHash == assetEvidenceHash`, and the existing AnimMontage mismatch guard is preserved.
  - `PreviewPoseAsset` is deferred and removed from supported `Body.Preview` policy/schema hints.
  - `Body.Curves` is documented and tested as sparse add/update, not destructive rebuild/delete.
  - `Validate` and `Preflight` now deep-check embedded notify and notify-state `Properties` before any region mutates.
- Final fix review range: `b65d00f5bd133feabc3c7fa9b6e1d90399caf3dd..HEAD`, to be run after this report checkpoint.

## Inspectable Smoke Artifact

- In editor: open validation host `C:/AVH1/AVH1.uproject`, then inspect `Content/AssetDocumentSmoke/AS_PostImportSidecarSmoke`.
- On disk: compare `C:/AVH1/Content/AssetDocumentSmoke/AS_PostImportSidecarSmoke.uasset` with `C:/AVH1/Content/AssetDocumentSmoke/AS_PostImportSidecarSmoke.assetdoc.json`.
- The smoke path intentionally uses an already-created AnimSequence asset plus `assetdocument/apply-file`, then verifies extract/diff through the generic AssetDocument HTTP surface.

## Residual Risk

- Focused and full automation suites completed with warnings from transient animation compression/fixture behavior; no tests failed.
- The external HTTP smoke deliberately preserved the sidecar under `Content` for inspection. The apply-file response no longer reports `sidecar_sync_update_skipped`.
- `Body.Curves` deletion/clear and `PreviewPoseAsset` authoring remain deferred because the verified UE 5.7 post-import APIs were not stable enough for this checkpoint.
- This implementation does not attempt raw animation import, reimport, or compressed data authoring. Those remain UE/importer responsibilities by design.
