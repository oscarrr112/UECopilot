# AnimationBlueprint AssetDocument Deferred Fields

本文档记录 `UAnimBlueprint` AssetDocument 第一阶段允许的保守范围，以及后续解除 deferred gate 时必须满足的架构和验证条件。

## 当前允许的保守范围

第一阶段允许 `Body.AnimGraph` 使用 root-only pilot graph。`Body.StateMachines`、`Body.TransitionGraphs`、`Body.AnimLayers` 和 `Body.ParentAssetOverrides` 仍只允许使用空值占位：

- `null`
- empty array
- empty object

这些仍 deferred 的 region 遇到任何非空 authored value 都必须在 `Validate` 阶段拒绝，并返回诊断：

- path: `/Body/<Key>`
- code: `UnsupportedAnimBlueprintRegion`

未知 `Body` key 必须继续返回 `UnknownBodyKey`。当前阶段不得在 `FAnimBlueprintAssetDocumentCapability` 内新增 ABP 私有 graph parser、state machine parser、transition graph parser、anim layer parser 或 parent override materializer。

`Body.AnimGraph` 已解除全量 deferred gate，但第一版只支持 canonical root-only graph：

```json
[
  {
    "Name": "AnimGraph",
    "Nodes": [],
    "OutputPose": {"Node": null, "Pin": "Result"}
  }
]
```

任何 authored pose node 仍必须返回 `/Body/AnimGraph/AnimGraph/Nodes/<Index>` + `UnsupportedAnimGraphNode`。后续支持 SequencePlayer、BlendSpace、StateMachineRef 或 Slot 等节点前，必须先扩展公共 adapter spec，不得写入 ABP 私有 parser。

`Body.SyncGroups` 已解除 region 级 deferred gate，但第一版只管理稳定 identity 字段 `Name`。`FAnimGroupInfo.Color` 暂不接受 authored value；如果作者提供 `Color`，必须在 `/Body/SyncGroups/<Index>/Color` 返回 `UnsupportedSyncGroupColor`。后续只有在颜色序列化格式和 diff canonicalization 稳定后，才能把 `Color` 加回同一个 named-array adapter。

## 后续升级触发条件

只有满足对应条件后，才允许解除某个 region 的 deferred gate：

- 已有或新写的公共 graph-family adapter spec 定义 node identity、pin identity、canonicalization、diff path 和 unsupported-node diagnostics。
- state machine 支持已定义稳定 state identity、transition identity 和 nested graph ownership。
- transition graph 支持已能绑定到稳定 transition identity。
- anim layer 支持已明确 `UAnimBlueprint` exact-class profile 与 Anim Layer Interface profile 的边界。
- `ParentAssetOverrides` 已能通过稳定 parent node GUID identity 匹配目标节点。
- 后续实现可以复用 public adapter/runtime；不需要把具体 `UAnimGraphNode_*` 语义写进 ABP 私有巨型类。

## 升级入口

解除 deferred gate 时优先从这些入口推进：

- `docs/superpowers/plans/2026-07-01-animationblueprint-asset-document-implementation.md`
- `docs/superpowers/specs/2026-07-01-animationblueprint-asset-document-design.md`
- `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentCapability.cpp`
- `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentCapability.h`
- `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentProfile.cpp`
- `Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp`
- public graph-family adapter files under `Source/AssetDocument/Private/Regions/` or `Source/AssetDocument/Private/Graphs/` if the adapter is shared across profiles.

## 清理成功标准

一个 deferred entry 只有在同一 task 中完成以下事项后，才能从本文档移除或改为已实现记录：

- 对应 `Body.*` region 有明确 adapter/policy/hook 归属。
- non-empty authored value 不再走 `UnsupportedAnimBlueprintRegion`，而是完成 validate/preflight/apply/extract/diff 生命周期。
- semantic diff path 不退化成不稳定 array index path；需要 identity 的 region 必须使用稳定 identity。
- unsupported 内容通过 diagnostic、skipped diff entry 或显式 rejection 暴露。
- 部分解除的 region 必须记录仍 deferred 的子字段；子字段解除时需要补 focused validation、apply/extract 和 stable diff path。
- `AssetFactory.AssetDocument.AnimBlueprint.DeferredGraphGates` 已更新，证明该 region 的 gate 被有意解除，并继续覆盖仍 deferred 的 sibling regions。
- focused automation、UBT 和必要的 editor/AssetDocument smoke verification 有 fresh evidence。

## Required Tests And Verification

当前 gate 行为由以下测试覆盖：

- `AssetFactory.AssetDocument.AnimBlueprint.DeferredGraphGates`

后续解除任一 region 时，至少补充：

- focused validation test：合法 non-empty authored value 通过，非法 shape 返回 exact path/code。
- apply/extract roundtrip test：真实 `UAnimBlueprint` 能 materialize 并重新抽取 canonical JSON。
- diff test：同一 authored semantic state 无 diff，变更返回稳定 semantic path。
- unsupported-content test：未支持 node、state、transition、layer 或 override 有明确 diagnostic。
- UBT build：`AVH1Editor Win64 Development -NoHotReload`。
- Automation：对应 `AssetFactory.AssetDocument.AnimBlueprint.<Region>` focused test，以及相关公共 graph adapter regression。

## Deferred Entries

| Entry | Current behavior | Deferred reason | Cleanup trigger | Upgrade entrypoint | Minimum verification |
| --- | --- | --- | --- | --- | --- |
| `Body.AnimGraph` authored pose nodes | root-only pilot 已支持；`Nodes` 必须为空。非空 node 返回 `/Body/AnimGraph/AnimGraph/Nodes/<Index>` + `UnsupportedAnimGraphNode`。 | 真实 pose node 仍需要 animation graph node adapter、node/pin identity、canonical graph compare 和 compile/rebuild hook。 | `Body.AnimGraph` adapter spec 扩展到具体 node subset，并证明至少一个真实 pose node roundtrip 不依赖 ABP 私有 parser。 | `FAssetDocumentAnimGraphRegionAdapter` 或 sibling public graph-family adapter。 | `AssetFactory.AssetDocument.AnimBlueprint.AnimGraph` 扩展 node case、公共 graph adapter regression、UBT。 |
| `Body.StateMachines` | 只接受 `null`、empty array、empty object；非空值返回 `/Body/StateMachines` + `UnsupportedAnimBlueprintRegion`。 | 需要 nested state-machine adapter，定义 state identity、transition identity 和 graph ownership。 | state machine identity/diff/apply/extract contract 完成。 | state-machine public adapter + `FAnimBlueprintAssetDocumentCapability` hook。 | focused state-machine automation、transition identity regression、UBT。 |
| `Body.TransitionGraphs` | 只接受 `null`、empty array、empty object；非空值返回 `/Body/TransitionGraphs` + `UnsupportedAnimBlueprintRegion`。 | transition blend graph 必须绑定到稳定 state-machine transition identity。 | transition graph adapter 能通过 transition identity 定位、materialize、extract 和 diff。 | transition graph public adapter + state-machine adapter integration。 | transition graph focused automation、state-machine regression、UBT。 |
| `Body.AnimLayers` | 只接受 `null`、empty array、empty object；非空值返回 `/Body/AnimLayers` + `UnsupportedAnimBlueprintRegion`。 | Anim Layer Interface 与普通 `UAnimBlueprint` exact-class profile 边界尚未实现。 | 已决定 layer/interface profile 边界，并实现 layer graph validate/apply/extract/diff。 | anim layer adapter 或独立 exact-class profile。 | anim layer focused automation、profile boundary test、UBT。 |
| `Body.ParentAssetOverrides` | 只接受 `null`、empty array、empty object；非空值返回 `/Body/ParentAssetOverrides` + `UnsupportedAnimBlueprintRegion`。 | parent override identity 依赖 parent node GUID；在 `Body.AnimGraph` identity 稳定前不能可靠匹配。 | parent node GUID identity 已稳定，override region 使用 identity-array helper 或等价公共 adapter。 | `FAssetDocumentIdentityArrayDiffHelper` + AnimGraph identity resolver。 | parent override focused automation、AnimGraph identity regression、UBT。 |
| `Body.SyncGroups[].Color` | `Body.SyncGroups` 当前通过 named-array adapter 管理 `Name`；如果 element 包含 `Color`，返回 `/Body/SyncGroups/<Index>/Color` + `UnsupportedSyncGroupColor`。 | `FLinearColor` 的 JSON 表达、默认值保留、canonical compare 和 diff 还没有跨 profile 稳定约定；先避免引入 ABP 私有颜色 parser。 | 公共 color/schema utility 或 property adapter convention 明确定义 linear color JSON canonical form，并能 roundtrip `FAnimGroupInfo.Color`。 | `FAssetDocumentNamedArrayRegionAdapter` hooks + shared color schema utility。 | `AssetFactory.AssetDocument.AnimBlueprint.SyncGroups` 增加 Color apply/extract/diff case、color schema regression、UBT。 |
| `Body.FunctionGraphs` / `Body.MacroGraphs` | 当前 ABP profile 不声明这些 keys；如果作者提供为 `Body` key，按 unknown key 拒绝。 | 其语义应跟随 common Blueprint graph support，而不是 ABP 私有实现。 | common Blueprint graph plan 明确支持后，再决定 ABP 是否继承或声明对应 region。 | common Blueprint graph adapter/profile hook。 | UBlueprint graph regression、ABP unknown/deferred boundary test、UBT。 |
| `UAnimBlueprintGeneratedClass` / debug / pose watch / property access cache | 不作为 `Body` authored state 暴露。 | 这些数据属于 derived compile output、runtime/debug evidence 或 editor transient/cache。 | 默认保持 excluded；只有新的 spec 明确证明其中某项是 authored semantic state 才能改变。 | ABP design spec + profile inspection/extract code。 | extract 不输出 derived/debug/cache 字段，diff 不报告这些字段，UBT。 |
