# AssetDocument AnimMontage Branch-Level Final Review

日期：2026-06-18

## 范围

- Worktree: `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-structured-capabilities-spec`
- Branch: `feature/asset-document-structured-capabilities-spec`
- Base: `c7e123bfc59212597c0be89094271cc4bb98a273`
- Reviewed implementation/evidence head: `337a6b2f2c42b66e07ec54bfeb7ae817a49d0df1`
- Review range: `c7e123bfc59212597c0be89094271cc4bb98a273..337a6b2f2c42b66e07ec54bfeb7ae817a49d0df1`
- Closure checkpoint: this report update commit

## 阶段结论

本分支可以作为 **AnimMontage 完整 region 化 benchmark** 的阶段性收口。

这个结论的含义是：

- `UAnimMontage` 已作为第一个复杂 UE asset，完成 AssetDocument structured `Body` 的完整 region benchmark；
- 完成的是 region-managed authoring surface，不是完整 `.uasset` 镜像；
- 验证覆盖了 apply、extract、diff、apply-file sidecar sync、真实 validation-host smoke asset；
- 外部 HTTP smoke 已替代旧的同进程 `-ExecutePythonScript` smoke，成为当前可重复的一键验证入口。

## 已完成并可作为本阶段事实

以下 AnimMontage managed regions 已纳入实现、spec、benchmark report 和 verification：

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

关键边界：

- `Body.RootMotion` 只表示 AnimMontage 自身 legacy root motion settings，不表示 referenced sequence root motion data。
- `Body.Curves` 只表示 montage-owned float curves，不管理 referenced sequence curves。
- `Body.TimeStretch` 只维护 authored settings；baked markers/cache data 仍由 UE 生成。
- `MarkerData`、branching point cache、deprecated branching point fields、referenced sequence-owned data 仍是 excluded derived/cache 或其他资产所有权范围。

## 仍属于目标方向而非本阶段完成

以下内容已经在目标 spec 中固定方向，但不是本阶段完整实现：

- 通用 `EvidenceExtractor` / `DefaultReducer` / `SemanticCapability` / `SidecarDeltaCapability` / `SidecarSyncEngine` / `AuthoritativeApplyAdapter` 的完整重构拆分。
- `RegionPolicyPreset` 作为 profile/schema 层 policy 复用机制的系统化落地。
- element-level three-way merge。本阶段和 v1 方向仍是 region-level direction choice：`accept sidecar` 或 `accept asset`。
- 第二个 asset 类型的 benchmark，例如 `AnimSequence`、`WidgetBlueprint`、`Material` 或 graph-like asset。
- referenced `AnimSequence` curves、root motion、markers 的 AssetDocument 管理。

## 文档一致性复核

- `docs/superpowers/specs/2026-06-14-animmontage-region-benchmark.md` 将所有推荐 AnimMontage regions 标为 complete，并明确 derived/cache/referenced sequence data 不直接 author。
- `docs/superpowers/specs/asset-document-deferred-fields/2026-06-12-animmontage.md` 保留 BranchingPoints、MarkerData、完整 root motion data、editor layout 等 deferred/out-of-scope 边界；`Body.Curves`、`Body.TimeStretch`、`Body.Notifies`、`Body.NotifyStates` 已移出 deferred。
- `docs/superpowers/specs/2026-06-13-asset-document-bidirectional-delta-sidecar-goal.md` 记录 delta-first sidecar、per-region sync state、RegionPolicy/preset 方向，并把 AnimMontage complete region 列表更新到当前实现状态。
- `docs/reports/asset-document-animmontage-complete-region-benchmark.md` 记录完整 region 列表、排除字段、UBT/automation/MCP/external smoke 证据；旧同进程 smoke 保留为 historical BLOCKED，不计为通过。
- `docs/superpowers/plans/2026-06-16-asset-document-external-http-smoke-harness-implementation.md` 和实际 smoke harness 已统一使用 `C:/AVH1/Content/AssetDocumentSmoke/AM_DeltaSidecarSmoke.assetdoc.json`，不再把 `Saved` 路径作为 apply-file 默认 sidecar 路径。

## 验证证据

最近一次收口前已记录并复核的验证：

- UBT validation host: `AVH1Editor Win64 Development`，`Result: Succeeded`
- Full AnimMontage automation: `succeeded: 29, succeededWithWarnings: 2, failed: 0`
- Full AssetDocument automation: `succeeded: 57, succeededWithWarnings: 7, failed: 0`
- MCP tests: `pass: 39, fail: 0`
- External smoke runner:
  - Command: `powershell -NoProfile -ExecutionPolicy Bypass -File docs/superpowers/verification/run_asset_document_delta_sidecar_smoke.ps1 -Project C:/AVH1/AVH1.uproject -KeepSidecar`
  - Result: exit 0
  - Created/updated asset: `/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke`
  - Preserved sidecar: `C:/AVH1/Content/AssetDocumentSmoke/AM_DeltaSidecarSmoke.assetdoc.json`

本次 branch-level 收口额外做了轻量复核：

- `git status --short`
- branch/head/base 查询
- 相关 spec/plan/report/verification 文档一致性扫描
- production profile/test coverage 定位

## 剩余风险

- `succeededWithWarnings` 仍来自 UE automation warning 级别输出；本阶段按既有报告记录为非失败，但未来如果要发布到主线，建议单独归档 warning 明细。
- 旧 `-ExecutePythonScript` 同进程 HTTP smoke 仍不是有效入口；当前有效入口是外部 runner。
- `--keep-sidecar` 目前基本是兼容开关，因为 smoke 现在默认保留 sidecar，后续可以清理命名。
- 根目录没有 `package.json`；MCP 测试入口是 `MCP/npm test`，不是 repo root `npm test`。

## 建议下一步

下一阶段不建议继续扩张 AnimMontage region。本阶段已经足以作为复杂 asset benchmark。

建议下一步选择第二个 asset 类型做 region inventory 和工作量评估，优先候选是 `AnimSequence`，因为它能复用动画验证环境，同时暴露 sequence-owned curves、root motion、markers、raw animation data 等新的 ownership 边界。
