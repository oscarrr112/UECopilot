# UBlueprint AssetDocument Deferred Fields

日期：2026-06-19

目标资产类：`/Script/Engine.Blueprint`

本文件记录 `UBlueprint` AssetDocument 完整权威表示中需要延期验证或分阶段实现的区域。延期不表示这些区域不属于 sidecar；它们是 Blueprint sidecar 完整性的明确缺口。

---

## Body.UbergraphPages

- AssetDocument path: `Body.UbergraphPages`
- UE surface: `UBlueprint::UbergraphPages`
- 当前处理方式:
  - validate: profile skeleton 阶段应识别该 key，但在 graph representation 未实现前拒绝非空内容。
  - apply: 未实现前拒绝应用非空 graph region。
  - extract: 未实现前可输出 extract-only diagnostic，不能声称 canonical full export。
  - diff: 未实现前应报告 region unsupported，而不是忽略差异。
- 延期原因: 需要定义 K2 graph canonical shape，包括 node class、node guid、pins、links、member references、默认 pin 值、schema、注释、layout metadata 和 subgraphs。
- 清理条件: 完成 canonical K2 graph region spec、roundtrip automation、真实 Blueprint compile smoke。

## Body.FunctionGraphs

- AssetDocument path: `Body.FunctionGraphs`
- UE surface: `UBlueprint::FunctionGraphs`
- 当前处理方式:
  - validate/apply/extract/diff 与 `Body.UbergraphPages` 一致。
- 延期原因: function graph 还需要函数签名、entry/return node 对齐、interface-required graph ownership 规则。
- 清理条件: 能 roundtrip user-created functions 和 interface function stubs，并能在 remove interface 时正确删除或保留相关 graph。

## Body.MacroGraphs

- AssetDocument path: `Body.MacroGraphs`
- UE surface: `UBlueprint::MacroGraphs`
- 当前处理方式:
  - validate/apply/extract/diff 与 graph regions 一致。
- 延期原因: macro tunnel nodes、local wildcard pins 和 macro-specific schema 行为需要单独验证。
- 清理条件: 能 roundtrip macro signature、tunnel nodes、internal nodes 和 links。

## Body.Timelines

- AssetDocument path: `Body.Timelines`
- UE surface: `UBlueprint::Timelines`
- 当前处理方式:
  - validate: 可先拒绝非空 timelines。
  - apply: graph reconciliation 未完成前不能应用 timeline region。
  - extract/diff: 未实现前报告 unsupported。
- 延期原因: `UTimelineTemplate` 与 generated variables、event/update/finished graph nodes、track curves 相关，不能只写 template array。
- 清理条件: 能 roundtrip timeline tracks、events、generated variable references，并在 Blueprint compile 后保持行为一致。

## Body.Components inherited/native attach and root override

- AssetDocument path: `Body.Components[*].AttachTo`, `Body.Components[*].Root` where `Scope` is `Inherited` or `Native`
- UE surface: `UInheritableComponentHandler`, native component archetypes, parent SCS templates
- 当前处理方式:
  - validate: 必须接受设计层语义，但 implementation task 可以在 API 未确认前拒绝 apply。
  - apply: 初始 implementation 可先实现 property override，再进入 attach/root override task。
  - extract/diff: 应标记为 region subfeature unsupported，不能忽略 existing override。
- 延期原因: inherited/native attach/root override 可能需要 UE editor subobject APIs 或 handler repair hooks，不能用复制成 owned SCS node 的方式伪造。
- 清理条件: 找到并验证 UE 5.7 中对 inherited/native component attach/root override 的正确编辑 API，并有 automation 覆盖 clear override。

## Body.ClassDefaults typed complex values

- AssetDocument path: `Body.ClassDefaults`
- UE surface: generated class CDO editable properties
- 当前处理方式:
  - validate/apply: 简单 reflected values 可复用 property adapter。
  - extract/diff: complex object/class/struct/container values 需要 canonical json 规则。
- 延期原因: Blueprint CDO defaults 覆盖 object refs、instanced subobjects、arrays/maps/sets 时，需要与 AssetDocument fragment system 对齐。
- 清理条件: complex property values 使用 `AssetRef`、`ClassRef`、`StructValue`、`EmbeddedObject` 或 canonical container values roundtrip。

## Variable stable rename migration

- AssetDocument path: `Body.Variables[*]`
- UE surface: `UBlueprint::NewVariables`, graph variable nodes
- 当前处理方式:
  - validate/apply: `Name` 是 identity；rename 表示 delete old plus add new。
  - extract/diff: 使用 current variable name。
- 延期原因: rename migration 需要 stable variable GUID 或 explicit previous-name metadata，避免破坏 existing graph references。
- 清理条件: 定义并验证 stable identity migration 规则，或确认 delete/add 是长期唯一语义。

## Graph editor-only layout state

- AssetDocument path: graph node layout fields inside `Body.*Graphs`
- UE surface: graph node position、comment bubble、zoom/pan、selection、editor tabs
- 当前处理方式:
  - node position and comments may be authoring metadata once graph region exists.
  - zoom/pan、selection、open tabs remain excluded editor-only state.
- 延期原因: 需要区分 graph readability metadata 和 per-user editor state。
- 清理条件: graph region spec 明确哪些 layout fields are authoring data，哪些永远 extract-only 或 excluded。

