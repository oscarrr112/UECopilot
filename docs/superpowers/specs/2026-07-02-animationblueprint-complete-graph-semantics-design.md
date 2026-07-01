# AnimationBlueprint Complete Graph Semantics Design

日期：2026-07-02

状态：基线已校准，待 implementation plan

适用范围：`UAnimBlueprint` AssetDocument graph-family regions

当前基线：`feature/asset-document-structured-capabilities-spec @ d47cf8d`

## 1. 目标

本 spec 在现有 `UAnimBlueprint` AssetDocument profile、public region runtime 和 ABP graph-family adapter 基础上，定义新的完整 graph-family schema 和实现目标。目标不是兼容旧 root-only / empty-only schema，而是让 ABP AssetDocument 承担完整 authored graph 语义。

完整目标包括：

- `Body.AnimGraph` 使用新的 recursive graph schema 表达真实 authored pose graph。
- `Body.StateMachines` 使用同一 recursive graph schema 表达 state machine、state pose graph、transition rule graph 和 transition blend graph。
- transition rule 不再作为独立字符串或旧 `Body.TransitionGraphs` side list 语义存在；它是 state machine graph 的 `Subgraphs`。
- `Body.AnimLayers` 使用同一 recursive graph schema 表达 authored layer graph，或明确落到 Anim Layer Interface / linked layer profile 边界。
- `Body.ParentAssetOverrides` 从 raw parent node GUID identity 升级为 authored AnimGraph node identity / alias resolver；raw GUID 只作为 extract evidence 或 UE fallback。
- `Body.FunctionGraphs` / `Body.MacroGraphs` 必须接入 common Blueprint graph support，而不是继续在 ABP profile 中以 unknown key 拒绝。
- graph node `Position` / editor layout 是 authored semantic surface 的一部分，必须 apply/extract/diff。

## 2. 当前代码基线

当前分支已经包含以下生产代码能力，本 spec 必须从这些能力上升级，而不是从空白 ABP profile 开始：

- `FAnimBlueprintAssetDocumentProfile` 已注册 exact `UAnimBlueprint` profile。
- `FAnimBlueprintAssetDocumentCapability` 已通过 `FAssetDocumentBodyRegionDispatcher` 委托 public region adapters。
- `Body.ParentClass`、`Body.TargetSkeleton`、`Body.Template`、`Body.Preview`、`Body.Optimization`、`Body.SyncGroups` 已有 managed/object/named-array region。
- `Body.ImplementedInterfaces`、`Body.Variables`、`Body.ClassDefaults`、`Body.UbergraphPages` 已复用 common Blueprint region support。
- `Body.ParentAssetOverrides` 已有 parent-node GUID identity adapter。
- `Body.AnimGraph` 当前是 root-only pilot，只接受 canonical root graph 且 `Nodes` 必须为空。
- `Body.StateMachines` 和旧 `Body.TransitionGraphs` 当前只接受空值；non-empty value 返回 `UnsupportedAnimBlueprintRegion`。
- `Body.AnimLayers` 当前由 deferred adapter gate 保护，只接受空值。

当前 graph core 已有 `FAssetDocumentGraphSpec` / `FAssetDocumentNodeSpec` / `FAssetDocumentLinkSpec`，但它是 flat K2 graph model：

- graph fields：`Name`、`Schema`、`GraphGuid`、`Category`、`Description`、`Signature`、`Nodes`、`Links`。
- node fields：`Id`、`NodeGuid`、`Class`、`Capability`、`Member`、`PinOverrides`、`Position`、`Comment`。
- K2 graph adapter 已能 map node `Position` 到 `NodePosX` / `NodePosY`。

完整 ABP graph implementation 必须扩展或替换这套 flat graph core，使它支持 recursive graph family；不得在 ABP capability 内新增另一套私有 graph parser。

## 3. 非目标

本 spec 不允许：

- 为每个 `UAnimGraphNode_*` 写一套手工 hard-coded adapter。
- 在 `FAnimBlueprintAssetDocumentCapability` 内新增 ABP 私有巨型 parser。
- 通过静态 class-name switch、静态类型列表或穷举 include 来覆盖节点差异。
- 对可发现但无法稳定 roundtrip 的节点静默 apply 或静默丢弃。
- 把 generated class、debug data、pose watch、compiler cache、property access generated output 当作 authored semantic state。
- 为旧 root-only `Body.AnimGraph` array shape、旧 empty-only `Body.StateMachines`、旧 `Body.TransitionGraphs` side-list 提供 forward-compatible authoring path。实现可以写迁移测试或 diagnostic，但目标 schema 只接受新 shape。

## 4. 新 Graph-Family Schema

ABP graph-family region 统一采用 recursive graph value。顶层 graph、state machine graph、state pose graph、transition rule graph、transition blend graph、anim layer graph、function graph 和 macro graph 都使用同一种 graph value，只靠 `Kind`、`Owner` 和 UE structural hook 区分生命周期。

Canonical region shape:

```json
{
  "Graphs": [
    {
      "Id": "AnimGraph",
      "Kind": "AnimGraph",
      "Owner": null,
      "Nodes": [],
      "Links": [],
      "Subgraphs": []
    }
  ]
}
```

Graph fields:

| Field | Requirement |
| --- | --- |
| `Id` | authored stable graph identity；不得使用 array index 作为 semantic identity |
| `Kind` | `AnimGraph`、`StateMachine`、`StatePose`、`TransitionRule`、`TransitionBlend`、`AnimLayer`、`FunctionGraph`、`MacroGraph` |
| `Owner` | graph ownership descriptor；例如 owner node、state、transition、layer、function 或 parent graph |
| `Nodes` | authored nodes，identity 为 node `Id` |
| `Links` | pin-to-pin semantic links，endpoint 使用 node id + semantic pin id |
| `Subgraphs` | nested graph array；必须递归 parse/apply/extract/diff |
| `Position` | optional graph-level editor/layout metadata；layout diff 可与 semantic diff 分开报告 |
| `Evidence` | extract-only UE evidence，例如 `GraphGuid`、source graph path、compiler node evidence |

Node fields:

| Field | Requirement |
| --- | --- |
| `Id` | authored stable node identity |
| `Class` | UE node class path or dynamic class alias resolved by spawner |
| `Spawner` | optional spawner/action descriptor when class alone is insufficient |
| `Kind` | optional semantic hint extracted from class/spawner；不得作为硬编码 dispatch switch |
| `Fields` | reflected authored properties after semantic field trait conversion |
| `Pins` | optional authored pin defaults / exposure metadata |
| `Position` | optional `{X,Y}` mapped to UE `NodePosX` / `NodePosY` |
| `SubgraphRefs` | optional references to child graphs owned by this node |
| `Evidence` | extract-only evidence such as UE `NodeGuid` when useful；not authored identity |

Link endpoint:

```json
{
  "Node": "IdlePlayer",
  "Pin": "Pose"
}
```

Pin identity must use semantic pin names or stable pin ids. Array index identity is forbidden unless a spec proves the pin list cannot be reordered, inserted, or generated dynamically.

## 5. Body Region Mapping

### 5.1 `Body.AnimGraph`

`Body.AnimGraph` owns the top-level pose graph. Its value is the recursive graph-family region object, not the old root-only graph array.

Required behavior:

- locate or create the official ABP AnimGraph.
- preserve output/result semantics through a normal graph node/link model.
- materialize reflected `UAnimGraphNode_*` nodes and links.
- support node `Position`.
- extract the graph into canonical recursive form.
- surface unsupported extract-only nodes through diagnostics, `_Skipped` evidence, or diff `skipped` entries.

### 5.2 `Body.StateMachines`

`Body.StateMachines` owns state-machine graph values. A state machine is a graph with `Kind="StateMachine"` and nested subgraphs:

- state pose graph: `Kind="StatePose"` owned by state id.
- transition rule graph: `Kind="TransitionRule"` owned by transition id.
- optional transition blend graph: `Kind="TransitionBlend"` owned by transition id.

State-machine structural hook owns only:

- creation / lookup of `UAnimationStateMachineGraph`.
- state node and transition node lifecycle.
- entry node and schema links.
- state graph and transition graph outer / ownership.
- deletion / rename repair where UE graph structure requires it.

Node fields inside all nested graphs still go through NodeSpawner + reflection + field traits.

### 5.3 Transition Rule Graphs

Transition rules are subgraphs under `Body.StateMachines`. The target schema does not require a separate authored `Body.TransitionGraphs` region.

Example:

```json
{
  "Id": "IdleToRunRule",
  "Kind": "TransitionRule",
  "Owner": {
    "StateMachine": "Locomotion",
    "Transition": "IdleToRun"
  },
  "Nodes": [
    {
      "Id": "Speed",
      "Class": "/Script/BlueprintGraph.K2Node_VariableGet",
      "Fields": {
        "Variable": "Speed"
      },
      "Position": {"X": 120, "Y": 80}
    }
  ],
  "Links": []
}
```

The rule graph may contain K2-compatible nodes and animation transition helper nodes. It must use the same graph runtime as other subgraphs.

### 5.4 `Body.AnimLayers`

`Body.AnimLayers` owns authored layer graph values for exact `UAnimBlueprint` assets when the layer implementation lives in the ABP. Linked layer calls and Anim Layer Interface references use field traits plus signature validation:

- class refs use `ClassRef<UAnimInstance>` or more specific trait.
- layer names use `LayerName` trait.
- interface/profile compatibility is validated before apply.
- input/output pose signature mismatch is a validation or compile diagnostic with JSON Pointer path.
- layer graph implementation uses the same recursive graph model.

If a layer surface belongs to a separate Anim Layer Interface asset, the ABP profile must reference that asset and leave the interface-owned graph to its own exact-class profile.

### 5.5 `Body.FunctionGraphs` / `Body.MacroGraphs`

ABP must inherit common Blueprint `FunctionGraphs` and `MacroGraphs` support. Since current ABP profile only declares `UbergraphPages`, implementation must add `Body.FunctionGraphs` and `Body.MacroGraphs` region bindings through the same common Blueprint graph wrapper path used by `UBlueprint` / `WidgetBlueprint`, then extend that graph runtime only where ABP ownership differs.

### 5.6 `Body.ParentAssetOverrides`

Current code supports raw `ParentNodeGuid` identity. Target schema must allow authored node identity:

```json
{
  "Node": "IdlePlayer",
  "NewAsset": {"Kind": "AssetRef", "Path": "/Game/Anim/Run"}
}
```

The adapter must resolve authored `Node` identity to the UE parent node GUID when apply requires `FAnimParentNodeAssetOverride`. Extract may include raw GUID in `Evidence`, but diff path must use authored node identity whenever it is resolvable.

## 6. NodeSpawner And Reflection Runtime

Introduce a shared graph-family runtime, not an ABP-only parser. Working name:

```text
FAssetDocumentAnimationGraphRuntime
```

Required behavior:

- query UE graph actions / node spawners for the relevant graph schema and context.
- resolve `Class` / `Spawner` into a spawnable action.
- spawn nodes through UE lifecycle APIs.
- set reflected fields using shared property utilities.
- reconstruct nodes and refresh pins after field changes.
- validate that the intended class/node actually exists after spawn.
- map compile or schema failures back to closest graph/node/field JSON Pointer path.

If a node class is discoverable but not spawnable in the current graph context, validation must fail before mutation.

## 7. Reflected Property Utilities

ABP graph implementation must reuse or introduce shared reflected property utilities for:

- JSON schema extraction from `FProperty`.
- read/write of UObject and UStruct fields.
- CDO/default comparison and canonical omission.
- enum, name, text, object, class, soft object, soft class, array, map, set, struct, vector, color and bool conversion.
- JSON Pointer diagnostics with shared escaping.
- per-field preflight validation.

This utility must not be ABP-specific. Existing `FAssetDocumentPropertyAdapter` / `PropertySetterUtils` behavior should be reused where it fits, and extended publicly where graph node fields need UStruct inner-node support.

## 8. Semantic Field Traits

Reflection provides field shape; field traits provide authored meaning.

Required initial trait categories:

| Trait | Examples |
| --- | --- |
| `AssetRef<UAnimationAsset>` | sequence, blend space, montage references |
| `AssetRef<USkeleton>` | skeleton references |
| `ClassRef<UAnimInstance>` | linked graph/layer classes |
| `SlotName` | slot nodes and montage slot binding |
| `SyncGroupName` | sync group fields |
| `CachedPoseName` | save/use cached pose relation |
| `LayerName` | linked anim layer and layer implementation |
| `BoneName` / `BoneReference` | layered blend / skeletal controls |
| `CurveName` | curve-driven nodes |
| `NotifyName` / `MarkerName` | transition helpers if surfaced |

Traits can be selected by property type, metadata, property path, owner graph kind, or UE type relation. They must not rely on a growing class-name switch.

## 9. Cached Poses

Cached pose support is a cross-graph resolver requirement, not a node-specific hard-code.

Required semantics:

- `SaveCachedPose` declares a stable cached pose identity.
- `UseCachedPose` references that identity.
- duplicate cache names are exact validation errors.
- rename/update repairs all authored references or fails with exact diagnostics.
- extract/diff paths use cached pose identity, not transient node index.

## 10. Identity And Diff

Stable semantic identity is mandatory:

- graph identity: `Graph.Id`.
- node identity: `Node.Id`.
- state identity: state `Id`.
- transition identity: transition `Id`.
- cached pose identity: cache name / authored id.
- parent override identity: authored parent graph node id when resolvable; raw GUID remains extract evidence and UE apply fallback.

Semantic diff paths must not degrade to numeric array index paths. Examples:

```text
/Body/AnimGraph/Graphs/AnimGraph/Nodes/IdlePlayer/Fields/Sequence
/Body/StateMachines/Graphs/Locomotion/Subgraphs/IdlePose/Nodes/IdlePlayer
/Body/StateMachines/Graphs/Locomotion/Subgraphs/IdleToRunRule/Nodes/SpeedCheck
/Body/ParentAssetOverrides/IdlePlayer/NewAsset
```

## 11. Layout Semantics

`Position` is authored layout metadata for graph nodes:

```json
"Position": {"X": 320, "Y": 120}
```

Requirements:

- apply maps position to UE node `NodePosX` / `NodePosY`.
- extract returns canonical position when available.
- absent position triggers deterministic auto-layout or preserves existing position depending on apply mode.
- diff can compare semantic graph separately from layout-only changes.
- state machine entry/state/transition positions are included where UE exposes editor layout.

## 12. Error Handling

No graph content may be silently ignored.

Required diagnostics:

- unknown graph kind.
- unknown owner reference.
- unknown node id.
- duplicate graph/node/state/transition/cache identity.
- unspawnable node class/spawner.
- reflected field write failure.
- unsupported property type without trait.
- skeleton/asset compatibility failure.
- pin not found or ambiguous pin identity.
- link type/schema incompatibility.
- compile failure mapped to closest graph/node/field path.
- extract-only node that cannot be represented as authored semantic JSON.

Unsupported or extract-only content must surface via validation diagnostic, `_Skipped`, or diff `skipped` entries.

## 13. Implementation Impact

The implementation plan must include these migration tasks:

1. Extend public graph core from flat `FAssetDocumentGraphSpec` to recursive graph-family spec with `Id`、`Kind`、`Owner`、`Subgraphs`、node `Fields`、node `Spawner` and extract-only `Evidence`.
2. Replace `FAssetDocumentAnimGraphRegionAdapter` root-only shape with the new `Body.AnimGraph` object schema.
3. Replace `FAssetDocumentAnimStateMachineRegionAdapter` empty-only behavior with real state machine graph materialization/extract/diff.
4. Remove or repoint old `Body.TransitionGraphs` target behavior so transition rules are authored as state machine subgraphs.
5. Replace `Body.AnimLayers` deferred adapter with real layer graph support or explicit exact-profile boundary.
6. Add ABP `Body.FunctionGraphs` and `Body.MacroGraphs` region bindings through common Blueprint graph support.
7. Upgrade `Body.ParentAssetOverrides` identity from GUID-only to authored graph node identity with GUID evidence fallback.
8. Keep public runtime boundaries: `FAssetDocumentBodyRegionDispatcher` and `IAssetDocumentRegionAdapter` remain the region lifecycle entrypoint; ABP-specific hooks are only for UE graph ownership, compile/rebuild, and identity repair.

## 14. Verification Requirements

Completion requires all of:

- focused recursive graph parser/runtime unit tests.
- reflected property utility tests including UStruct inner `FAnimNode_*` fields.
- node spawner discovery and materialization tests.
- `Body.AnimGraph` apply/extract/diff roundtrip with multiple reflected `UAnimGraphNode_*`.
- `Body.StateMachines` roundtrip with state pose graphs and transitions.
- transition rule subgraph roundtrip.
- cached pose reference roundtrip.
- linked layer / linked graph validation test.
- parent override alias resolver test.
- layout/position apply/extract/diff test.
- unsupported plugin or unrepresentable node evidence test.
- UBT build.
- focused `AssetFactory.AssetDocument.AnimBlueprint` automation.
- full AssetDocument automation.
- HTTP/MCP extract/apply/diff smoke against a real editor.

## 15. Spec Relationship

This spec supersedes the current root-only / empty-only graph-family boundaries in:

- `docs/superpowers/specs/2026-07-01-animationblueprint-asset-document-design.md`
- `docs/superpowers/specs/2026-07-01-animationblueprint-animgraph-adapter-design.md`
- `docs/superpowers/specs/asset-document-deferred-fields/2026-07-01-animationblueprint.md`

Those documents remain historical implementation evidence for current code state. They are not target capability, and the next implementation plan must not treat their deferred gates as accepted scope reduction.
