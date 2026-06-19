# UBlueprint AssetDocument Graph Regions 设计

日期：2026-06-19

目标资产类：`/Script/Engine.Blueprint`

依赖基础：`docs/superpowers/specs/2026-06-19-ublueprint-asset-document-design.md`

---

## 1. 目标

本 spec 定义普通 `UBlueprint` AssetDocument 的 graph/timeline 作者表示，覆盖：

- `Body.UbergraphPages`
- `Body.FunctionGraphs`
- `Body.MacroGraphs`
- `Body.Timelines`

实现上允许分 step 落地，但 public sidecar shape 必须在同一个 spec 中稳定下来。第一轮实现优先完成 shared canonical K2 graph infrastructure 和 `Body.UbergraphPages`，后续 step 复用同一套 graph shape 扩展 `FunctionGraphs`、`MacroGraphs` 和 `Timelines`。

核心原则：

- sidecar 仍是权威作者表示，不是 patch/op DSL。
- graph 表示必须是结构化 JSON，不允许把 `.uasset` graph 二进制 blob 或 editor serialization dump 当作主要作者表面。
- 缺失的 graph、node、pin default、link、timeline 表示应在 apply 后从 `.uasset` 中移除或恢复 baseline。
- 不新增 `BlueprintGenerator`，不复用旧 generator 作为 AssetDocument apply 路径；可以把既有 graph/node 代码当作 UE API 参考。

---

## 2. 范围

### 2.1 In Scope

- 普通 `UBlueprint` 的 K2 graph authoring。
- graph extract、validate、apply、diff 的 canonical representation。
- graph identity、node identity、pin identity、links、default pin values、member/function refs、layout metadata。
- Event graph pages stored in `UBlueprint::UbergraphPages`。
- User-created function graphs and interface function stubs stored in `UBlueprint::FunctionGraphs`。
- User-created macro graphs stored in `UBlueprint::MacroGraphs`。
- Timeline templates stored in `UBlueprint::Timelines` and timeline graph node reconciliation.
- Compile after graph apply and no-partial-save failure handling.

### 2.2 Out Of Scope

- `UWidgetBlueprint` widget tree/bindings。
- `UAnimBlueprint` AnimGraph、state machine、skeleton lifecycle。
- natural-language graph DSL。
- Full arbitrary K2 node support in the first implementation step。
- Per-user editor state: graph zoom、pan、selection、open tabs、editor viewport。
- Runtime spawned component instances produced by graph execution。

---

## 3. Body Shape

Graph regions share the same `GraphSpec` shape:

```json
{
  "Name": "EventGraph",
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "GraphGuid": "00000000000000000000000000000000",
  "Nodes": [],
  "Links": []
}
```

`GraphGuid` is optional on authored input. Extract should include it when UE provides a stable value. Apply must not require it for identity; graph identity is region-specific:

- `UbergraphPages`: `Name`
- `FunctionGraphs`: `Name`
- `MacroGraphs`: `Name`

`Timelines` do not use `GraphSpec` directly, but timeline nodes in graph regions reference timeline specs by timeline `Name`.

---

## 4. Canonical GraphSpec

### 4.1 Graph Fields

Required fields:

- `Name`: graph name.
- `Schema`: graph schema class path. First implementation supports only `/Script/BlueprintGraph.EdGraphSchema_K2`.
- `Nodes`: array of `NodeSpec`.
- `Links`: array of `LinkSpec`.

Optional fields:

- `GraphGuid`: extracted graph GUID string. Apply preserves when updating an existing graph if possible; create may generate a new GUID.
- `Category`: authoring category for function/macro graph lists when UE exposes a stable field.
- `Description`: graph description when UE exposes a stable editable field.

Unsupported fields must fail validation with `UnknownGraphField`.

### 4.2 NodeSpec

Canonical node shape:

```json
{
  "Id": "BeginPlay",
  "NodeGuid": "00000000000000000000000000000000",
  "Kind": "K2Node_Event",
  "Class": "/Script/BlueprintGraph.K2Node_Event",
  "Member": {
    "OwnerClass": "/Script/Engine.Actor",
    "Name": "ReceiveBeginPlay"
  },
  "Pins": [],
  "Position": {
    "X": 0,
    "Y": 0
  },
  "Comment": ""
}
```

Rules:

- `Id` is the sidecar identity within one graph. It must be unique, stable, and human-editable.
- `NodeGuid` is UE identity evidence. It is optional for create. Extract includes it. Apply uses `Id` as sidecar identity and may preserve `NodeGuid` on update when safe.
- `Kind` is the semantic node kind used by AssetDocument capability code.
- `Class` is the UE node class path. It must match the `Kind`.
- `Pins` is canonical pin data, including authored defaults. Pin order follows UE pin order on extract and sidecar order on apply when adding dynamic pins.
- `Position` contains authoring layout metadata. It is included because node location affects graph readability and is shared project state.
- `Comment` is authoring metadata. Empty string means no comment. Missing `Comment` means no comment.

The first implementation must reject node kinds outside its support matrix with `UnsupportedGraphNodeKind`; it must not silently preserve unsupported nodes in a managed graph when the sidecar omits them.

### 4.3 PinSpec

Canonical pin shape:

```json
{
  "Id": "Exec",
  "Name": "then",
  "Direction": "Output",
  "Type": {
    "PinCategory": "exec"
  },
  "DefaultValue": "",
  "DefaultObject": null,
  "DefaultTextValue": "",
  "Hidden": false,
  "AdvancedView": false
}
```

Rules:

- `Id` is the link address inside one node. For ordinary pins it should equal UE `PinName`. Dynamic pins may use a stable sidecar id when UE display name is not unique.
- `Name` is the UE pin name.
- `Direction` is `Input` or `Output`.
- `Type` uses the same `FEdGraphPinType` JSON shape already used by `Body.Variables`.
- `DefaultValue`, `DefaultObject`, and `DefaultTextValue` are authoritative for input pins when the pin is not linked.
- Missing default fields mean reset to UE pin baseline.
- Linked input pins may still carry default values in UE, but diff treats link state as authoritative behavior. Apply should clear irrelevant defaults when UE requires it for compile stability.
- Output pin defaults are ignored unless a specific UE node class exposes an editable output default; first implementation should reject authored output defaults with `InvalidGraphPinDefault`.

### 4.4 LinkSpec

Canonical link shape:

```json
{
  "From": {
    "Node": "BeginPlay",
    "Pin": "then"
  },
  "To": {
    "Node": "Print",
    "Pin": "execute"
  }
}
```

Rules:

- Links are directed from output pin to input pin.
- Both pins must resolve after node allocation and pin reconstruction.
- Duplicate links are rejected with `DuplicateGraphLink`.
- Type-incompatible links are rejected before mutation when UE schema can validate them.
- Missing link means remove that connection.

### 4.5 MemberRef

Canonical member reference shape:

```json
{
  "OwnerClass": "/Script/Engine.Actor",
  "Name": "ReceiveBeginPlay"
}
```

Optional fields:

- `Guid`: extracted UE member GUID when available.
- `SelfContext`: boolean for member references that are intentionally self-scoped.

Rules:

- Function/event/member identity must prefer explicit class path plus name.
- GUID is supporting evidence, not the only identity.
- Blueprint variables referenced by get/set nodes must exist in `Body.Variables` or be parent-class properties.
- Component variables referenced by get/set nodes must exist in `Body.Components` or parent/native component evidence.

---

## 5. Supported Node Matrix

The full `GraphSpec` is designed for arbitrary K2 graphs, but implementation lands in support tiers.

### 5.1 Tier 1: EventGraph 基础节点

First implementation must support these node kinds in `Body.UbergraphPages`:

- `K2Node_Event`
  - UE class: `/Script/BlueprintGraph.K2Node_Event`
  - Required `Member`.
  - First required native events: `Actor.ReceiveBeginPlay`, `Actor.ReceiveTick`.
  - Custom events are not Tier 1 unless explicitly implemented as `K2Node_CustomEvent`.
- `K2Node_CallFunction`
  - UE class: `/Script/BlueprintGraph.K2Node_CallFunction`
  - Required `Member`.
  - Supports ordinary callable functions that UE can resolve from reflected function path.
- `K2Node_VariableGet`
  - UE class: `/Script/BlueprintGraph.K2Node_VariableGet`
  - Required `Member`.
- `K2Node_VariableSet`
  - UE class: `/Script/BlueprintGraph.K2Node_VariableSet`
  - Required `Member`.
- `K2Node_Self`
  - UE class: `/Script/BlueprintGraph.K2Node_Self`
  - No member ref.

Tier 1 may extract unsupported existing node kinds as `_Skipped.Graphs` evidence, but apply of a managed graph containing unsupported nodes must fail unless those nodes are absent because the sidecar intentionally deletes them.

### 5.2 Tier 2: FunctionGraphs

Adds:

- `K2Node_FunctionEntry`
- `K2Node_FunctionResult`

Function graph spec extends `GraphSpec`:

```json
{
  "Name": "ApplyDamage",
  "Signature": {
    "Inputs": [
      {
        "Name": "Amount",
        "Type": {
          "PinCategory": "real",
          "PinSubCategory": "float"
        },
        "DefaultValue": "0.0"
      }
    ],
    "Outputs": []
  },
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [],
  "Links": []
}
```

Rules:

- `Signature` is authoritative.
- Function entry/result nodes must match `Signature` after apply.
- Interface-required function stubs are required when `Body.ImplementedInterfaces` includes the interface. If sidecar omits a required stub, apply creates an empty canonical stub. If sidecar includes it, apply validates signature compatibility with the interface.
- User-created functions absent from sidecar are deleted.

### 5.3 Tier 3: MacroGraphs

Adds:

- `K2Node_Tunnel` for macro entry and exit tunnel nodes.

Macro graph spec extends `GraphSpec`:

```json
{
  "Name": "DoWork",
  "Signature": {
    "Inputs": [],
    "Outputs": []
  },
  "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
  "Nodes": [],
  "Links": []
}
```

Rules:

- Macro tunnel nodes are part of canonical graph representation.
- Macro `Signature` is authoritative and must match tunnel pins.
- Wildcard pins are rejected in the first macro implementation unless a node-specific rule can roundtrip them deterministically.

### 5.4 Tier 4: Timelines

Timeline spec:

```json
{
  "Name": "DoorTimeline",
  "Length": 1.0,
  "LengthMode": "SpecifiedLength",
  "Loop": false,
  "AutoPlay": false,
  "IgnoreTimeDilation": false,
  "Tracks": [
    {
      "Name": "Alpha",
      "Kind": "Float",
      "Curve": {
        "Keys": [
          {
            "Time": 0.0,
            "Value": 0.0
          },
          {
            "Time": 1.0,
            "Value": 1.0
          }
        ]
      }
    }
  ]
}
```

Rules:

- Timeline `Name` is identity.
- Missing timeline means delete the `UTimelineTemplate` and reconcile graph timeline nodes.
- Timeline graph nodes must reference `Body.Timelines[*].Name`.
- Timeline-generated variables are derived implementation details and must not be independently authored in `Body.Variables`.
- Timeline track curve representation must use canonical key data, not external curve asset refs, unless a track explicitly references an external asset in a future extension.

---

## 6. Region Semantics

### 6.1 `Body.UbergraphPages`

- Missing `UbergraphPages` means empty authoritative event graph set except for UE-required baseline default graph.
- Missing graph page means remove that graph page when UE allows it.
- The default `EventGraph` is special:
  - If sidecar omits all event graphs, apply should remove user-authored nodes from the default graph, not necessarily delete the UE-required graph object.
  - Extract should emit an empty `EventGraph` only if it is needed as canonical baseline, or emit an empty array when no user-authored graph data remains. The implementation plan must pick one canonical behavior and test it.
- Missing node means delete the node.
- Missing link means delete the link.
- Missing pin default means reset to node default.

### 6.2 `Body.FunctionGraphs`

- Missing user function graph means delete it.
- Required interface function graph cannot be silently deleted while the interface remains implemented.
- Removing an interface via `Body.ImplementedInterfaces` should remove now-unneeded interface stubs unless they are also represented as user-authored functions with a distinct identity.

### 6.3 `Body.MacroGraphs`

- Missing macro graph means delete it.
- Missing tunnel pin from signature means remove it if UE allows; otherwise fail before mutation with `InvalidMacroSignature`.

### 6.4 `Body.Timelines`

- Missing timeline means delete it.
- Existing graph nodes that reference a deleted timeline must also be deleted or rejected before mutation. The implementation plan must choose one behavior per step:
  - First timeline step may reject deletion when graph references exist.
  - Full timeline step should reconcile graph nodes in the same staged apply.

---

## 7. Apply Pipeline

Graph/timeline apply must remain staged:

1. Parse all graph/timeline regions.
2. Validate graph region arrays, duplicate identities, node kinds, pin identities, member refs, links, timeline refs.
3. Resolve cross-region references against staged `ParentClass`, `Variables`, `Components`, `ImplementedInterfaces`, `ClassDefaults`, graph specs, and timeline specs.
4. Preflight parent class changes that would invalidate graph nodes.
5. Snapshot previous Blueprint graph state enough to rollback on failure, or mutate only after all preflight succeeds.
6. Apply dependency regions in this order:
   - `ParentClass`
   - `ImplementedInterfaces`
   - `Variables`
   - `Components`
   - `ClassDefaults`
   - `Timelines`
   - `UbergraphPages`
   - `FunctionGraphs`
   - `MacroGraphs`
7. Compile Blueprint.
8. If compile fails, rollback staged mutations where possible and return `BlueprintCompileFailed` with graph path diagnostics.
9. Save only after successful compile when `bSaveAsset` is requested.

No step may leave partially applied graph changes saved to disk.

---

## 8. Extract

Extract produces canonical graph specs:

- Graph arrays sorted by UE graph array order.
- Nodes sorted by UE node order, with `NodeGuid` fallback for deterministic output when UE order changes.
- Pins sorted by UE pin order.
- Links sorted by source node id, source pin id, target node id, target pin id.
- Supported nodes extract fully.
- Unsupported existing nodes are reported in `_Skipped.Graphs` with count and node class names. Once a graph region is marked fully managed for a tier, unsupported nodes in that tier should make extract report incomplete evidence rather than pretending full roundtrip.

Extract must not include editor-only graph zoom/pan/selection/tab state.

---

## 9. Diff

Diff parses desired graph regions using the same parser as apply, extracts current evidence, and compares canonical objects.

Required diff paths:

- `/Body/UbergraphPages/<GraphName>`
- `/Body/UbergraphPages/<GraphName>/Nodes/<NodeId>`
- `/Body/UbergraphPages/<GraphName>/Links/<FromNode>:<FromPin>-><ToNode>:<ToPin>`
- `/Body/FunctionGraphs/<GraphName>`
- `/Body/MacroGraphs/<GraphName>`
- `/Body/Timelines/<TimelineName>`

Diff statuses:

- `unchanged`: canonical current and desired values match.
- `changed`: value exists on both sides but differs.
- `missing`: desired exists, current does not.
- `extra`: current exists, desired does not.
- `unsupported`: current cannot be represented by the implemented graph tier.

Missing supported graph region fields mean empty authoritative state, consistent with the rest of `UBlueprint` Body semantics.

---

## 10. Validation Diagnostics

Required diagnostic codes:

- `InvalidGraphRegionType`: graph region is not an array.
- `DuplicateGraphName`: duplicate graph identity in one region.
- `InvalidGraphSchema`: unsupported graph schema.
- `UnknownGraphField`: unknown field in graph spec.
- `DuplicateGraphNodeId`: duplicate node identity in one graph.
- `UnsupportedGraphNodeKind`: node kind is not implemented for this tier.
- `InvalidGraphNodeClass`: `Class` does not match `Kind`.
- `MissingGraphMemberReference`: required member ref is missing.
- `UnresolvedGraphMemberReference`: member ref cannot be resolved.
- `InvalidGraphPin`: pin shape or pin direction is invalid.
- `InvalidGraphPinDefault`: authored pin default cannot be applied.
- `DuplicateGraphLink`: duplicate link.
- `UnresolvedGraphLinkEndpoint`: link node/pin endpoint does not exist.
- `InvalidGraphLinkType`: UE graph schema rejects the link.
- `InvalidFunctionSignature`: function signature and graph entry/result nodes conflict.
- `InvalidMacroSignature`: macro signature and tunnel nodes conflict.
- `UnresolvedTimelineReference`: graph node references a missing timeline.
- `UnsupportedTimelineTrackKind`: timeline track kind is not implemented.
- `BlueprintCompileFailed`: apply compiled graph into an invalid Blueprint.

Diagnostics should point at the narrowest possible JSON path.

---

## 11. Implementation Steps

This spec is implemented through checkpoint tasks, not one large patch.

### Step 1: GraphCore Data Model And Validation

- Add parser/serializer helpers for `GraphSpec`, `NodeSpec`, `PinSpec`, `LinkSpec`, and `MemberRef`.
- Keep apply rejection for non-empty graph regions except for validation tests that exercise parser failure modes.
- Add automation tests for duplicate graph names, duplicate node ids, unresolved links, unsupported node kinds, and invalid schema.

### Step 2: EventGraph Extract/Diff For Supported Nodes

- Extract `Body.UbergraphPages` for Tier 1 node kinds.
- Diff supported EventGraph nodes and links.
- Existing unsupported nodes produce `_Skipped.Graphs` and `unsupported` diff entries.

### Step 3: EventGraph Apply

- Apply `Body.UbergraphPages` for Tier 1 node kinds.
- Rebuild missing/extra nodes and links authoritatively.
- Compile and verify a real `BeginPlay -> PrintString` or equivalent smoke graph.

### Step 4: FunctionGraphs

- Add `Signature` support.
- Apply/extract/diff user-created function graphs.
- Support interface-required function stubs controlled jointly by `Body.ImplementedInterfaces`.

### Step 5: MacroGraphs

- Add macro `Signature` and tunnel node support.
- Apply/extract/diff user-created macros with supported internal Tier 1 nodes.

### Step 6: Timelines

- Add `Body.Timelines` parser/serializer.
- Support float tracks first.
- Reconcile timeline node references in graph regions.
- Compile and smoke a timeline Blueprint.

### Step 7: Final Graph Roundtrip Smoke

- Run UBT against `C:/AVH1`.
- Run focused automation for `AssetFactory.AssetDocument.UBlueprint`.
- Run full `AssetFactory.AssetDocument` automation.
- Run external HTTP smoke with a real graph sidecar under `C:/AVH1/Content/AssetDocumentSmoke/`.
- Update deferred-fields doc by removing completed graph/timeline entries or narrowing their remaining limits.
- Update final report.

---

## 12. Verification Requirements

Minimum automation coverage:

- Validate:
  - empty graph regions still pass.
  - duplicate graph/node/link identity fails.
  - unsupported node kind fails apply.
  - unresolved member ref/link/timeline ref fails before mutation.
- Apply:
  - create EventGraph with supported nodes and links.
  - update existing graph, deleting omitted nodes and links.
  - apply failure leaves previous graph state loadable.
  - function graph create/update/delete.
  - macro graph create/update/delete when that step lands.
  - timeline create/update/delete when that step lands.
- Extract:
  - supported graph roundtrips to canonical sidecar.
  - unsupported nodes are surfaced as skipped evidence.
- Diff:
  - unchanged graph reports unchanged.
  - omitted graph/node/link reports extra.
  - desired graph/node/link missing in asset reports missing.
- Smoke:
  - external HTTP apply-file/extract/diff for one Blueprint with graph content.

---

## 13. Open Risks

- UE may regenerate node GUIDs or pins during compile/reconstruction; sidecar identity must not depend solely on `NodeGuid`.
- Some K2 nodes allocate pins dynamically based on member refs or default values; implementation must reconstruct pins before applying links.
- Interface function stubs may be owned by UE/interface logic rather than user graph arrays. The function graph step must preserve required stubs while still deleting omitted user-created functions.
- Timeline templates produce generated variables and graph node references. Timeline apply must avoid treating derived timeline variables as user-authored `Body.Variables`.
- Unsupported node extraction must be honest. It is better to report unsupported evidence than to emit a lossy graph that looks roundtrippable.

---

## 14. Success Criteria

The graph regions are considered complete for this spec when:

- `Body.UbergraphPages`, `Body.FunctionGraphs`, `Body.MacroGraphs`, and `Body.Timelines` have stable schema docs and tests.
- Supported graph/timeline content can be apply-file roundtripped through a real `UBlueprint` in `C:/AVH1`.
- Omitted graph/timeline sidecar content deletes or resets current asset state according to the authoritative Body semantics.
- Unsupported graph/timeline content fails with clear diagnostics or is reported as skipped evidence on extract/diff.
- No Blueprint Generator or Blueprint-specific MCP tool is introduced.
