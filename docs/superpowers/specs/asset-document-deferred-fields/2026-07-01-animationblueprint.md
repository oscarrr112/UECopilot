# AnimationBlueprint AssetDocument Deferred Fields

本文档记录 `UAnimBlueprint` AssetDocument 当前仍允许保守处理、显式拒绝或排除的字段。完整 ABP graph 语义的设计入口是 `docs/superpowers/specs/2026-07-02-animationblueprint-complete-graph-semantics-design.md`，实现计划入口是 `docs/superpowers/plans/2026-07-02-animationblueprint-complete-graph-semantics-implementation.md`。

## 当前状态

ABP graph-family 的 region 级 deferred gate 已关闭：

- `Body.AnimGraph` 使用 recursive graph region；旧 array pilot shape 会在 `/Body/AnimGraph` 返回 `InvalidAnimGraphRegionType`。
- `Body.StateMachines` 使用 recursive graph region，支持 state machine graph、state/transition identity 和 transition rule subgraph ownership。
- `Body.AnimLayers` 使用 recursive graph region，支持 exact-class `UAnimBlueprint` 自有 anim layer graph 边界。
- `Body.FunctionGraphs` / `Body.MacroGraphs` 继承 common Blueprint graph adapter；ABP 的 `UAnimationGraph` layer/function graphs 不由 K2 common graph 空数组误删。
- `Body.ParentAssetOverrides` 支持 `Node` authored alias identity，并把 alias 解析到 stable parent graph node GUID；extract 使用 `Node`，同时在 `Evidence.ParentNodeGuid` 暴露 UE GUID。

这些 region 的 non-empty authored value 不得再返回 `UnsupportedAnimBlueprintRegion` 作为静默 deferred gate。仍不支持的 node class、field trait、shape 或 ownership 必须返回具体 diagnostic、`_Skipped` evidence 或 stable diff entry。

## 仍保留的边界

`Body.TransitionGraphs` 的旧 side-list shape 已废弃。transition rule graph 语义应挂在 `Body.StateMachines.Graphs[*].Subgraphs[*]`，通过 `(StateMachine, Transition)` ownership 建模。旧 side-list 的 non-empty authored value 当前仍显式拒绝：

- path: `/Body/TransitionGraphs`
- code: `UnsupportedAnimBlueprintRegion`

`Body.SyncGroups` 已解除 region 级 deferred gate，但只管理稳定 identity 字段 `Name`。`FAnimGroupInfo.Color` 暂不接受 authored value；如果作者提供 `Color`，必须返回：

- path: `/Body/SyncGroups/<Index>/Color`
- code: `UnsupportedSyncGroupColor`

`UAnimBlueprintGeneratedClass`、compiled class data、property access cache、debug data、pose watch、editor preview cache 和 transient compiler artifacts 不作为 authored `Body` state 暴露。它们属于 derived output、debug evidence 或 editor/runtime cache；extract 不应输出，diff 不应报告。

## 清理成功标准

一个 deferred 或 conservative entry 只有在同一 task 中满足以下条件后，才能从本文移除：

- 对应 `Body.*` region 有明确 adapter/policy/hook 归属。
- non-empty authored value 不再走 generic deferred rejection，而是完成 validate/preflight/apply/extract/diff 生命周期，或被明确标记为 obsolete shape。
- semantic diff path 不退化成不稳定 array index path；需要 identity 的 region 必须使用 stable identity。
- unsupported 内容通过 diagnostic、`_Skipped`、skipped diff entry 或 explicit rejection 暴露。
- focused automation、UBT 和必要 editor/AssetDocument smoke verification 有 fresh evidence。

## Required Tests And Verification

当前边界由这些测试覆盖：

- `AssetFactory.AssetDocument.AnimBlueprint.DeferredGraphGates`
- `AssetFactory.AssetDocument.AnimBlueprint.AnimGraph`
- `AssetFactory.AssetDocument.AnimBlueprint.StateMachines`
- `AssetFactory.AssetDocument.AnimBlueprint.AnimLayersAndParentAssetOverrides`
- `AssetFactory.AssetDocument.AnimBlueprint.SyncGroups`
- `AssetFactory.AssetDocument.AnimationGraphRuntime`

解除剩余条目时，至少补充：

- focused validation test：合法 non-empty authored value 通过，非法 shape 返回 exact path/code。
- apply/extract roundtrip test：真实 `UAnimBlueprint` 能 materialize 并重新抽取 canonical JSON。
- diff test：同一 authored semantic state 无 unexpected diff，变更返回 stable semantic path。
- unsupported-content test：未支持 node、field、state、transition、layer 或 override 有明确 diagnostic。
- UBT build：`UnrealEditor Win64 Development -NoHotReload` against a temp host that loads the worktree plugin.

## Deferred Entries

| Entry | Current behavior | Deferred reason | Cleanup trigger | Upgrade entrypoint | Minimum verification |
| --- | --- | --- | --- | --- | --- |
| `Body.TransitionGraphs` obsolete side-list shape | Empty array remains a compatibility no-op. Non-empty old side-list values return `/Body/TransitionGraphs` + `UnsupportedAnimBlueprintRegion`. | Transition rule semantics now belong to recursive `Body.StateMachines` subgraphs; keeping two authored shapes would split identity and diff paths. | A future migration utility intentionally translates old side-list documents into `StateMachines.Graphs[*].Subgraphs[*]`, or the compatibility key is removed from the profile. | `FAssetDocumentAnimStateMachineRegionAdapter` plus a document migration/canonicalizer hook if needed. | old-shape rejection test, migration roundtrip if added, ABP suite, UBT. |
| `Body.SyncGroups[].Color` | `Body.SyncGroups` manages `Name`; authored `Color` returns `/Body/SyncGroups/<Index>/Color` + `UnsupportedSyncGroupColor`. | `FLinearColor` JSON shape, default preservation, canonical compare and cross-profile color convention are not yet standardized. | Shared color schema/property convention can roundtrip `FAnimGroupInfo.Color` without ABP-local parsing. | `FAssetDocumentNamedArrayRegionAdapter` hooks + shared color schema utility. | `AssetFactory.AssetDocument.AnimBlueprint.SyncGroups` color apply/extract/diff case, color schema regression, UBT. |
| Derived/debug/cache ABP data | Not emitted in `Body`; not considered in diff. | Generated class, compiler output, debug and editor caches are not authored semantic state. | Only a new ABP design spec proving a field is authored semantic state may move it into `Body`. | ABP design spec + profile extract/diff code. | extract excludes derived/cache fields, diff ignores them, ABP suite, UBT. |
