# AnimationBlueprint Complete Graph Smoke Fix Design

日期：2026-07-03

状态：待审核

适用范围：`feature/asset-document-abp-complete-graph-impl` 上的 `UAnimBlueprint` AssetDocument graph-family implementation

基线：

- implementation branch: `feature/asset-document-abp-complete-graph-impl`
- implementation base commit: `3facfed453e3c22cea4fc87b69e0db1241ec79b0`
- source spec: `docs/superpowers/specs/2026-07-02-animationblueprint-complete-graph-semantics-design.md`
- current report: `docs/reports/asset-document-animationblueprint-complete-graph-semantics-report.md`

## 1. 目的

本 spec 是 2026-07-02 ABP complete graph semantics spec 的失败驱动补充。它不重新定义 ABP graph-family 总目标，而是把 AVH1 外部 HTTP smoke 暴露的问题收敛成必须修复的语义缺口和验收标准。

当前实现已经有 recursive graph schema、NodeSpawner runtime shell、部分 node materialization、state machine materialization、AnimLayer region、Function/Macro graph region、ParentAssetOverrides node alias 等实现 checkpoint。但 AVH1 smoke 证明这些实现尚未形成完整端到端语义。

因此，本补充 spec 的核心判断是：

- focused automation 通过不等于 ABP complete graph semantics 完成。
- 能 apply 部分图节点不等于 graph region 可 roundtrip。
- `_Skipped` / diff allowlist / canonical empty extraction 只能作为未完成证据，不能作为完成策略。
- AVH1 HTTP apply-file / extract / diff 必须成为完成门槛的一部分。

## 2. 当前失败事实

### 2.1 Full Graph Apply 失败

运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File docs/superpowers/verification/run_asset_document_animationblueprint_smoke.ps1 -Project C:/AVH1/AVH1.uproject -KeepSidecar
```

结果：

```text
success=false
code=AnimBlueprintCompileFailed
message=Failed to compile AnimBlueprint after applying Body regions
path=/Body
target=/Game/AssetDocumentSmoke/ABP_AnimationBlueprintSmoke
```

该失败说明当前 apply 后生成的 ABP editor graph 不满足 UE 编译语义。可能来源包括但不限于：

- AnimGraph 节点被创建为孤立节点，没有接入 result/root pose 语义。
- graph links 尚未 materialize 到 UE pins。
- state machine node、state graph、transition rule graph 之间的 ownership / entry / result repair 不完整。
- transition rule subgraph 没有生成有效 transition result semantics。
- AnimLayer graph 可能被创建为函数图形态，但未满足 ABP layer signature / pose IO contract。
- apply 后 compile diagnostics 没有映射回具体 graph/node/field path，只退化为 `/Body`。

### 2.2 Narrowed Apply/Extract 证明 Roundtrip 缺口

报告中记录过一次窄化输入验证：只保留 `Body.AnimGraph` 的 `IdlePlayer` node、空 `StateMachines` / `AnimLayers`，以及 `ParentAssetOverrides.Node = "IdlePlayer"`。

该输入可以通过 `apply-file` 和 `extract`，但 extract payload 显示：

```text
payload.Body.AnimGraph.Graphs[0].Nodes = []
payload.Body.ParentAssetOverrides = []
```

该事实说明：

- `FAssetDocumentAnimGraphRegionAdapter::ExtractRegion` 当前仍返回 canonical empty graph。
- `FAssetDocumentAnimationGraphRuntime::ExtractGraph` 当前仍以 `_Skipped.Reason = AnimationGraphExtractionNotMaterialized` 表示未实现。
- ParentAssetOverrides 的 `Node` alias apply 和 persisted extract 没有形成可验证闭环。
- diff 对 `/Body/AnimGraph`、`/Body/StateMachines`、`/Body/AnimLayers` 的 allowlist 掩盖了 graph extract fidelity 缺口。

## 3. 修复目标

本修复必须把 ABP graph-family 从“能创建部分 UE 节点的 runtime shell”推进到“可 apply、compile、extract、diff 的真实 managed authored graph region”。

修复后的完成目标包括：

- `Body.AnimGraph` authored nodes、links、fields、pins、position 可以写入真实 ABP AnimGraph，并能被 extract 回同一 recursive graph semantic shape。
- `Body.AnimGraph` 必须能生成可编译 pose flow；孤立 authored pose nodes 不能静默通过。
- `Body.StateMachines` 必须能 materialize state machine node、state nodes、transition nodes、entry links、state pose subgraphs 和 transition rule subgraphs。
- transition rule subgraph 必须 materialize 为 UE transition graph，并产生有效 rule result semantics。
- `Body.AnimLayers` 必须要么完成 layer graph apply/extract/diff 和 signature validation，要么在 ordinary ABP profile 中明确诊断为 profile boundary；不得以“空图 accepted”伪装完成。
- `Body.ParentAssetOverrides` 的 `Node` alias 必须能从 authored AnimGraph node identity 解析到 UE parent node GUID，并在 extract/diff 中保留 `Node` identity。
- graph `Position` 必须 apply/extract/diff；不能只在 parser 或 focused test 中存在。
- external HTTP smoke 不得保留 graph changed allowlist。

## 4. 非目标

本修复不允许通过以下方式“修绿”：

- 从 smoke sidecar 删除 graph regions。
- 把 smoke 降级成只有 empty graph 或 canonical root graph。
- 在 smoke diff 中继续 allowlist `/Body/AnimGraph`、`/Body/StateMachines`、`/Body/AnimLayers` changed entries。
- 将 `_Skipped.AnimationGraphExtractionNotMaterialized` 作为成功 extraction。
- 在 compile failure 时只返回 `/Body`，不保留更具体的 graph/node/field evidence。
- 为 smoke 中使用到的节点写一次性 class-name hardcode。
- 用静态 `UAnimGraphNode_*` 白名单替代 NodeSpawner + reflection + field trait runtime。
- 将 authored graph semantic state 写入 sidecar sync metadata 或 report 文本，而不从 asset extract。

## 5. Required Semantic Corrections

### 5.1 AnimGraph Apply Must Produce Valid Pose Flow

`Body.AnimGraph` apply 必须满足：

- locate/create official `UAnimationGraph`。
- materialize authored `UAnimGraphNode_*` through NodeSpawner/action APIs。
- apply reflected fields before final pin/link validation when those fields affect pins.
- materialize authored links to UE graph pins using semantic pin identity.
- connect authored output pose to the official AnimGraph result node or equivalent schema-defined result surface.
- reject graphs where output pose is ambiguous, missing, or incompatible.
- compile after apply must succeed for the smoke graph.

如果 schema 需要显式 result endpoint，必须在 graph schema 中保留一个 stable field，例如：

```json
{
  "OutputPose": {
    "Node": "IdlePlayer",
    "Pin": "Pose"
  }
}
```

或者使用 equivalent link model 指向 framework result node。无论采用哪种表示，extract 必须能回读同一语义。

### 5.2 AnimGraph Extraction Must Be Real

`FAssetDocumentAnimGraphRegionAdapter::ExtractRegion` 不得返回固定 canonical empty graph。

Extraction must:

- enumerate managed authored nodes in the UE AnimGraph.
- recover `Node.Id` from managed node object name, deterministic GUID, evidence, or semantic resolver.
- extract `Class` from UE node class path.
- extract `Fields` through shared reflected property utilities and field traits.
- extract `Position` from `NodePosX` / `NodePosY`.
- extract links by semantic pin identity.
- extract result/output pose semantics.
- include `Evidence.NodeGuid` where useful, but never use it as primary authored identity.
- emit `_Skipped` only for nodes that truly cannot be represented, and smoke must fail if managed authored smoke nodes are skipped.

### 5.3 State Machine Subgraphs Must Be Owned Graphs

`Body.StateMachines` apply/extract/diff must treat state machine as a graph family, not a flat list of state/transition wrapper nodes.

Required semantics:

- root graph `Kind="StateMachine"` owns state and transition identities.
- each state can own `Kind="StatePose"` subgraph.
- each transition can own `Kind="TransitionRule"` subgraph.
- optional transition blend graph remains a supported schema shape if UE exposes stable authored semantics.
- entry state is semantic metadata or an authored link from entry framework node; it must extract and diff by state identity.
- state and transition positions must roundtrip.

Transition rule subgraph must:

- locate the UE transition graph owned by the transition node.
- materialize K2-compatible rule nodes or a schema-defined literal/result expression through the shared graph runtime.
- create or repair the transition result node.
- extract rule graph nodes, result endpoint, links and positions.

### 5.4 Links And Pins Are Mandatory

Current node materialization without link materialization is incomplete.

The graph runtime must:

- resolve pin endpoints after node reconstruction.
- reject missing or ambiguous pin names.
- validate schema direction/type compatibility before linking.
- clear or reconcile managed links deterministically.
- extract links from UE pins back to semantic endpoint identities.

Pin identity cannot be array index. Dynamic pins must be reconstructed from fields or explicit `Pins` declarations.

### 5.5 ParentAssetOverrides Node Alias Must Persist

For authored input:

```json
{
  "Node": "IdlePlayer",
  "NewAsset": {"Kind": "AssetRef", "Path": "/Game/Anim/Idle"}
}
```

Required behavior:

- apply resolves `Node` through the managed AnimGraph node identity map.
- apply writes the correct `FAnimParentNodeAssetOverride.ParentNodeGuid`.
- extract returns `Node = "IdlePlayer"` when the GUID maps back to a managed graph node.
- extract may include `Evidence.ParentNodeGuid`.
- diff path uses `/Body/ParentAssetOverrides/IdlePlayer/NewAsset`.
- raw `ParentNodeGuid` remains fallback only when no managed node alias exists.

If graph extraction cannot recover `IdlePlayer`, parent override extraction is incomplete and smoke must fail.

### 5.6 Compile Diagnostics Must Be Specific

Compile failure cannot remain only:

```text
path=/Body
code=AnimBlueprintCompileFailed
```

The repair/compile layer must preserve enough context to diagnose:

- graph path, for example `/Body/AnimGraph/Graphs/AnimGraph`;
- node path, for example `/Body/AnimGraph/Graphs/AnimGraph/Nodes/IdlePlayer`;
- field path, when property apply or signature mismatch caused failure;
- link path, when pose/data pins are incompatible;
- transition/state/layer owner path, when the failing graph is nested.

If UE compiler only exposes coarse messages, the adapter must attach best-effort evidence such as affected graph id, recently applied region, or managed node ids. Coarse `/Body` can remain as top-level diagnostic only when no closer mapping exists.

## 6. Smoke Contract

The ABP external smoke must be a hard completion gate.

Smoke sidecar must include at least:

- `Body.AnimGraph` with at least one real reflected `UAnimGraphNode_*` node.
- authored link or output pose semantics proving pose flow reaches result.
- `Body.StateMachines` with one state machine, two states, one transition, and a transition rule subgraph.
- at least one authored `Position` on graph nodes and state-machine nodes.
- `Body.ParentAssetOverrides` using `Node` alias, not only raw GUID.
- `Body.FunctionGraphs` / `Body.MacroGraphs` present as ABP-supported common graph regions or explicitly empty if not part of the smoke scenario.
- `Body.AnimLayers` either as a real valid layer graph scenario or as an explicit profile-boundary diagnostic that is not counted as completed ordinary ABP layer graph support.

Smoke must perform:

1. write sidecar under `C:/AVH1/Content/AssetDocumentSmoke/`.
2. call `/assetfactory/assetdocument/apply-file` with `save_asset=true`.
3. call `/assetfactory/assetdocument/extract`.
4. verify extracted graph nodes, links, positions and parent override alias.
5. call `/assetfactory/assetdocument/diff`.
6. fail if `failed` entries exist.
7. fail if any implemented graph region has unexpected `changed` entries.
8. fail if any smoke-managed graph node appears under `_Skipped`.

The smoke script must not allowlist graph region diffs as final passing behavior.

## 7. Verification Requirements

本修复完成后必须重新满足原 spec 的 verification，并额外满足以下 failure-driven checks：

- focused test proves `AnimGraph` apply creates a compilable output pose path.
- focused test proves `AnimGraph` extract returns applied managed nodes, links and positions.
- focused test proves `StateMachines` extract returns states, transition links, and transition rule subgraph.
- focused test proves parent override `Node` alias roundtrips after asset save/load when possible.
- focused test proves compile failure reports closest graph/node/link path for an intentionally invalid graph.
- diff test fails before fix and passes after fix without graph allowlist.
- AVH1 UBT builds current worktree plugin, not stale project plugin.
- AVH1 external HTTP smoke passes apply-file/extract/diff.
- final report marks previous `3facfed` failure as fixed with fresh evidence, not as historical passing evidence.

## 8. Completion Definition

本修复只有同时满足以下条件才算完成：

- No authored graph managed surface in smoke is extracted as canonical empty graph.
- No smoke-managed node is represented only by `_Skipped`.
- No smoke graph region requires changed-entry allowlist.
- `AnimBlueprintCompileFailed` no longer occurs for the smoke graph.
- `ParentAssetOverrides.Node` identity roundtrips.
- focused automation, full relevant AssetDocument automation, MCP tests, UBT, and AVH1 external HTTP smoke all have fresh recorded evidence.
- report conclusion may say complete only if there is no remaining unapproved managed authored graph deferred surface.

## 9. Relationship To Existing Documents

This spec supplements and tightens:

- `docs/superpowers/specs/2026-07-02-animationblueprint-complete-graph-semantics-design.md`
- `docs/superpowers/plans/2026-07-02-animationblueprint-complete-graph-semantics-implementation.md`
- `docs/reports/asset-document-animationblueprint-complete-graph-semantics-report.md`

The previous report remains useful as failure evidence. It must not be treated as completion evidence until the smoke failure and extraction fidelity gaps described here are fixed.
