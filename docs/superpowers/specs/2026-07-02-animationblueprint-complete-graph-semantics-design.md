# AnimationBlueprint Complete Graph Semantics Design

日期：2026-07-02

状态：待审核

适用范围：`UAnimBlueprint` AssetDocument graph-family regions

## 1. 目标

本 spec 将 `UAnimBlueprint` 的 graph-family surface 从 root-only / empty-only gate 升级为完整 authored semantic target。实现可以拆 task 和 checkpoint commit，但目标不降级为 MVP。

完整目标包括：

- `Body.AnimGraph` 支持真实 authored pose graph。
- `Body.StateMachines` 支持非空 state machine、state、transition 和 nested state pose graph。
- `Body.TransitionGraphs` 支持真实 transition rule graph。
- `Body.AnimLayers` 支持 authored layer graph，或由同一模型明确接入 Anim Layer Interface profile。
- `Body.ParentAssetOverrides` 支持 authored node identity / alias resolver，不只依赖 raw parent node GUID。
- `Body.FunctionGraphs` / `Body.MacroGraphs` 若暴露，必须继承 common Blueprint graph support；不得继续靠 unknown key 表达完整 ABP graph target。
- graph `Position` / layout 是 authored semantic surface 的一部分。

## 2. 非目标

本 spec 不允许：

- 为每个 `UAnimGraphNode_*` 写一套手工 hard-coded adapter。
- 在 `FAnimBlueprintAssetDocumentCapability` 内新增 ABP 私有巨型 parser。
- 通过静态 class-name switch、静态类型列表或穷举 include 来覆盖节点差异。
- 对可发现但无法稳定 roundtrip 的节点静默 apply 或静默丢弃。
- 把 generated class、debug data、pose watch、compiler cache、property access generated output 当作 authored semantic state。

## 3. 核心设计结论

完整 ABP graph 语义必须基于：

```text
recursive graph/subgraph runtime
+ NodeSpawner-based node materialization
+ reflected property schema/read/write
+ semantic field traits
+ small structural hooks for UE graph ownership
```

节点覆盖策略不是“一个 `UAnimGraphNode_*` 一个 adapter”，而是：

- 用 UE node spawner / graph action system 创建节点 shell。
- 用反射读写 `UAnimGraphNode_*` 及其内部 `FAnimNode_*` properties。
- 用公共 GraphRuntime 统一管理 graph identity、node identity、pin identity、links、position、canonical diff。
- 用公共 field traits 解释一致字段语义，例如 asset ref、class ref、slot name、sync group name、cached pose name、layer name、skeleton compatibility。
- 只在 UE graph ownership / lifecycle 无法靠字段反射表达时使用 structural hook。

## 4. Recursive Graph Model

ABP graph-family region 必须共享同一套 recursive model。顶层 graph、state machine graph、state pose graph、transition rule graph、transition blend graph、anim layer graph 都是同一种 graph value，只是 `Kind` 和 owner context 不同。

Canonical shape:

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
| `Kind` | `AnimGraph`、`StateMachine`、`StatePose`、`TransitionRule`、`TransitionBlend`、`AnimLayer`、`FunctionGraph`、`MacroGraph` 等 |
| `Owner` | graph ownership descriptor；例如 owner node、state、transition、layer 或 parent graph |
| `Nodes` | authored nodes，identity 为 node `Id` |
| `Links` | pin-to-pin semantic links，endpoint 使用 node id + semantic pin id |
| `Subgraphs` | nested graph array；必须递归支持 |
| `Position` | optional graph-level editor/layout metadata；不得影响 runtime semantic equality，除非 diff mode 明确比较 layout |

Node fields:

| Field | Requirement |
| --- | --- |
| `Id` | authored stable node identity |
| `Class` | UE node class path or dynamic class alias resolved by spawner |
| `Spawner` | optional spawner/action descriptor when class alone is insufficient |
| `Kind` | optional semantic hint extracted from class/spawner; not required for dispatch |
| `Fields` | reflected authored properties after semantic field trait conversion |
| `Pins` | optional authored pin defaults / exposure metadata |
| `Position` | optional `{X,Y}` mapped to UE `NodePosX/NodePosY` |
| `SubgraphRefs` | optional references to child graphs owned by this node |
| `Evidence` | extract-only evidence such as UE `NodeGuid` when useful; not authored identity |

Link endpoint:

```json
{
  "Node": "IdlePlayer",
  "Pin": "Pose"
}
```

Pin identity must use semantic pin names or stable pin ids. Array index identity is forbidden unless a spec proves the pin list cannot be reordered, inserted, or generated dynamically.

## 5. Subgraph Requirements

### 5.1 AnimGraph

`Body.AnimGraph` owns the top-level pose graph and may contain subgraphs for state machines, cached-pose expansion evidence, linked layer calls, or future graph-owned constructs.

Required behavior:

- locate or create the official ABP AnimGraph.
- preserve root/result semantics.
- materialize reflected nodes and links.
- support node position.
- extract the graph into canonical recursive form.
- reject or mark skipped any node that can be discovered but cannot roundtrip safely.

### 5.2 StateMachines

`Body.StateMachines` must be recursive, not a flat side list. A state machine is a graph with `Kind="StateMachine"` and child graphs:

- state pose graph: `Kind="StatePose"` owned by state id.
- transition rule graph: `Kind="TransitionRule"` owned by transition id.
- optional transition blend graph: `Kind="TransitionBlend"` owned by transition id.

State-machine structural hook owns only:

- creation / lookup of `UAnimationStateMachineGraph`.
- state node and transition node lifecycle.
- entry node and schema links.
- state graph and transition graph outer / ownership.
- deletion / rename repair where UE graph structure requires it.

Node fields inside those graphs still go through NodeSpawner + reflection + field traits.

### 5.3 TransitionGraphs

Transition rules are subgraphs. They must not be modeled as special string expressions or one-off fields.

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
      }
    }
  ],
  "Links": []
}
```

The rule graph may contain K2-compatible nodes and animation transition helper nodes. It must use the same graph runtime as other subgraphs.

### 5.4 Cached Poses

Cached pose support is a cross-graph resolver requirement, not a node-specific hard-code.

Required semantics:

- `SaveCachedPose` declares a stable cached pose identity.
- `UseCachedPose` references that identity.
- duplicate cache names are exact validation errors.
- rename/update repairs all authored references or fails with exact diagnostics.
- extract/diff paths use cached pose identity, not transient node index.

### 5.5 Linked Graphs And Anim Layers

Linked anim graph / linked anim layer support must use field traits plus signature validation:

- class refs use `ClassRef<UAnimInstance>` or more specific trait.
- layer names use `LayerName` trait.
- interface/profile compatibility is validated before apply.
- input/output pose signature mismatch is a validation or compile diagnostic with JSON Pointer path.
- layer graph implementation uses the same recursive graph model.

## 6. NodeSpawner And Reflection Runtime

`FAssetDocumentAnimationGraphRuntime` must discover and materialize nodes dynamically:

- query UE graph actions / node spawners for the relevant graph schema and context.
- resolve `Class` / `Spawner` into a spawnable action.
- spawn nodes through UE lifecycle APIs.
- set reflected fields using shared property utilities.
- reconstruct nodes and refresh pins after field changes.
- validate that the intended class/node actually exists after spawn.

If a node class is discoverable but not spawnable in the current graph context, validation must fail before mutation.

## 7. Reflected Property Utilities

ABP graph implementation must reuse or introduce shared reflected property utilities for:

- JSON schema extraction from `FProperty`.
- read/write of UObject and UStruct fields.
- CDO/default comparison and canonical omission.
- enum, name, text, object, class, soft object, soft class, array, map, set, struct, vector, color and bool conversion.
- JSON Pointer diagnostics with shared escaping.
- per-field preflight validation.

This utility must not be ABP-specific.

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

## 9. Identity And Diff

Stable semantic identity is mandatory:

- graph identity: `Graph.Id`.
- node identity: `Node.Id`.
- state identity: state `Id`.
- transition identity: transition `Id`.
- cached pose identity: cache name / authored id.
- parent override identity: authored parent node id when resolvable; raw GUID remains extract evidence and compatibility fallback.

Semantic diff paths must not degrade to numeric array index paths. Examples:

```text
/Body/AnimGraph/Graphs/AnimGraph/Nodes/IdlePlayer/Fields/Sequence
/Body/StateMachines/Locomotion/States/Idle/Graph/Nodes/IdlePose
/Body/StateMachines/Locomotion/Transitions/IdleToRun/RuleGraph/Nodes/SpeedCheck
/Body/ParentAssetOverrides/IdlePlayer/NewAsset
```

## 10. Layout Semantics

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

## 11. Error Handling

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

## 12. Verification Requirements

Completion requires all of:

- focused graph parser/runtime unit tests.
- reflected property utility tests.
- node spawner discovery and materialization tests.
- `Body.AnimGraph` apply/extract/diff roundtrip with multiple reflected `UAnimGraphNode_*`.
- `Body.StateMachines` roundtrip with state pose graphs and transitions.
- `Body.TransitionGraphs` roundtrip with rule subgraph.
- cached pose reference roundtrip.
- linked layer / linked graph validation test.
- parent override alias resolver test.
- layout/position apply/extract/diff test.
- unsupported plugin or unrepresentable node evidence test.
- UBT build.
- focused `AssetFactory.AssetDocument.AnimBlueprint` automation.
- full AssetDocument automation.
- HTTP/MCP extract/apply/diff smoke against a real editor.

## 13. Spec Relationship

This spec extends and supersedes the root-only / empty-only graph-family boundaries in:

- `docs/superpowers/specs/2026-07-01-animationblueprint-asset-document-design.md`
- `docs/superpowers/specs/2026-07-01-animationblueprint-animgraph-adapter-design.md`
- `docs/superpowers/specs/asset-document-deferred-fields/2026-07-01-animationblueprint.md`

Those documents remain valid as historical implementation evidence for the current code state, but they are not the target capability for the next ABP graph implementation phase.
