# UBlueprint AssetDocument Graph Regions 设计

日期：2026-06-19

目标资产类：`/Script/Engine.Blueprint`

依赖基础：`docs/superpowers/specs/2026-06-19-ublueprint-asset-document-design.md`

状态：Draft，待实现计划拆分

---

## 1. 目标

本 spec 定义普通 `UBlueprint` AssetDocument 的 graph/timeline 作者表示，覆盖：

- `Body.UbergraphPages`
- `Body.FunctionGraphs`
- `Body.MacroGraphs`
- `Body.Timelines`

sidecar 是 agent 可编辑、可 diff、可 roundtrip 的 Blueprint graph 作者表面；`.uasset` graph 是由 sidecar materialize 出来的 UE 表示。设计参考 Godot `.tres/.tscn` 的文本资源原则：稳定局部 identity、显式引用、外部/内部资源分层、默认值稀疏保存；但 AssetDocument 仍使用 JSON shape，不引入 Godot section 语法。

实现上允许分 step 落地，但 public sidecar shape 必须在同一个 spec 中稳定。第一轮实现优先完成 shared `GraphCore` 和 `Body.UbergraphPages`，后续 step 复用同一套 graph shape 扩展 `FunctionGraphs`、`MacroGraphs` 和 `Timelines`。

核心原则：

- sidecar 仍是权威作者表示，不是 patch/op DSL。
- graph 表示必须是结构化 JSON，不允许把 `.uasset` graph 二进制 blob 或 editor serialization dump 当作作者表面。
- GraphCore 必须是薄中间层：只管 graph/node/pin/link identity、canonical ordering、diagnostics、引用解析和 staged apply 编排。
- K2 node 细节必须通过 reflection-first adapter hook 扩展；禁止在 GraphCore 或 region apply 主流程中维护硬编码 node/function/property inventory。
- 缺失的 graph、node、pin default、link、timeline 表示应在 apply 后从 `.uasset` 中移除或恢复 baseline。
- 不新增 `BlueprintGenerator`，不复用旧 generator 作为 AssetDocument apply 路径；可以把既有 graph/node 代码当作 UE API 参考。

---

## 2. 范围

### 2.1 In Scope

- 普通 `UBlueprint` 的 K2 graph authoring。
- graph extract、validate、apply、diff 的 canonical representation。
- graph identity、node identity、link identity、default pin values、member/function refs、layout metadata。
- `Definitions` 对 graph refs、pin type、复杂 literal、timeline curve 等可复用 fragment 的承载。
- Event graph pages stored in `UBlueprint::UbergraphPages`。
- User-created function graphs and interface function stubs stored in `UBlueprint::FunctionGraphs`。
- User-created macro graphs stored in `UBlueprint::MacroGraphs`。
- Timeline templates stored in `UBlueprint::Timelines` and timeline graph node reconciliation。
- Compile after graph apply and no-partial-save failure handling。

### 2.2 Out Of Scope

- `UWidgetBlueprint` widget tree/bindings。
- `UAnimBlueprint` AnimGraph、state machine、skeleton lifecycle。
- natural-language graph DSL。
- Full arbitrary K2 node support in the first implementation step。
- Per-user editor state: graph zoom、pan、selection、open tabs、editor viewport。
- Runtime spawned component instances produced by graph execution。
- UE compiler/intermediate/cache fields that can be reconstructed from graph semantics。

---

## 3. Architecture: Thin Core + Reflection Adapters

Graph support is split into four layers:

1. `GraphCore`
   - Parses `GraphSpec`、`NodeSpec`、`PinOverrideSpec`、`LinkSpec`。
   - Validates JSON shape、duplicate ids、link endpoint syntax、canonical ordering。
   - Resolves `DefinitionRef` and cross-region references.
   - Produces narrow JSON path diagnostics.
   - Does not know concrete K2 node behavior.

2. `GraphRegionPolicy`
   - Connects `Body.UbergraphPages` / `FunctionGraphs` / `MacroGraphs` to `RebuildGraphRegion` apply mode.
   - Defines identity rule, default source, comparison rule, reducer mode, and after-apply hooks.
   - Keeps graph support aligned with existing `RegionPolicy` / `DefaultReducer` / `AuthoritativeApplyAdapter` direction.

3. `K2GraphAdapter`
   - Creates or locates `UEdGraph` instances through UE editor APIs.
   - Resolves graph schema dynamically from `Schema`.
   - Invokes `FBlueprintEditorUtils`, `UEdGraphSchema_K2`, `AllocateDefaultPins`, `ReconstructNode`, compile/save lifecycle.
   - Owns engine-specific repair hooks; it is not a node inventory.

4. `NodeAdapterRegistry`
   - Resolves node class dynamically by `Class` path using `StaticLoadClass` / existing `ClassFinderUtils` where available.
   - Selects a thin adapter only when reflection alone cannot perform the UE lifecycle operation.
   - Adapters may handle lifecycle operations such as binding `UFunction` to a `UK2Node_CallFunction`, setting an event reference, or reconciling timeline templates.
   - Adapters must not enumerate concrete function names, variable names, component names, or project-specific classes.

Hard-coded branching policy:

- Allowed: small adapter selection by UE node class or reflected capability when UE requires a class-specific lifecycle call.
- Not allowed: `switch` / large `if` chains over concrete function names, property names, project class names, or exhaustive `K2Node_*` include lists.
- Required: use reflection for `UFunction` / `FProperty` / `FEdGraphPinType` / component property resolution wherever UE exposes enough metadata.

---

## 4. Document Shape

Graph regions share the same `GraphSpec` shape. A minimal EventGraph example:

```json
{
  "Definitions": {
    "Func.KismetSystemLibrary.PrintString": {
      "Kind": "MemberRef",
      "OwnerClass": "/Script/Engine.KismetSystemLibrary",
      "Name": "PrintString"
    }
  },
  "Body": {
    "UbergraphPages": [
      {
        "Name": "EventGraph",
        "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
        "Nodes": [
          {
            "Id": "BeginPlay",
            "Class": "/Script/BlueprintGraph.K2Node_Event",
            "Member": {
              "Kind": "MemberRef",
              "OwnerClass": "/Script/Engine.Actor",
              "Name": "ReceiveBeginPlay"
            },
            "Position": {
              "X": 0,
              "Y": 0
            }
          },
          {
            "Id": "Print",
            "Class": "/Script/BlueprintGraph.K2Node_CallFunction",
            "Member": {
              "Kind": "DefinitionRef",
              "Id": "Func.KismetSystemLibrary.PrintString"
            },
            "PinOverrides": [
              {
                "Pin": "InString",
                "DefaultValue": "Hello"
              }
            ],
            "Position": {
              "X": 320,
              "Y": 0
            }
          }
        ],
        "Links": [
          {
            "From": "BeginPlay.then",
            "To": "Print.execute"
          }
        ]
      }
    ]
  }
}
```

Rules:

- `Definitions` is the Godot-style resource table equivalent. It supports reusable refs and fragments, but it does not own UE package lifecycle.
- Graph topology stays inline under `Body.*Graphs`. Nodes, link endpoints, and graph ordering must remain easy for agents to edit.
- Nodes are not moved into `Definitions` by default. Only large, shared, or reused payloads should become definitions.
- `Timelines` do not use `GraphSpec` directly, but timeline nodes in graph regions reference timeline specs by timeline `Name` or `DefinitionRef`.

---

## 5. Definitions For Graph Regions

Graph regions may reference top-level `Definitions` through:

```json
{
  "Kind": "DefinitionRef",
  "Id": "Func.KismetSystemLibrary.PrintString"
}
```

Initial graph-relevant definition kinds:

- `ClassRef`
  - `Class`: UE class path.
- `AssetRef`
  - `Path`: asset path.
  - `Class`: optional expected asset class path.
- `MemberRef`
  - `OwnerClass`: class path or `"Self"`.
  - `Name`: reflected function/property/member name.
  - `Guid`: optional extracted evidence.
  - `SelfContext`: optional boolean.
- `PinType`
  - Uses the same `FEdGraphPinType` JSON shape as `Body.Variables`.
- `Literal`
  - Typed literal payload for complex defaults once a typed fragment is proven roundtrippable.
- `TimelineCurve`
  - Canonical curve key payload for reusable timeline tracks.

Validation rules:

- Unknown definition kind fails with `UnknownDefinitionKind`.
- Unused definitions are allowed only when the top-level AssetDocument policy already allows reusable fragments; otherwise diff may report them as extra.
- Circular `DefinitionRef` chains fail with `CircularDefinitionReference`.
- A graph node may inline a small ref object or use `DefinitionRef`; both canonicalize to the same resolved semantic value.

---

## 6. Canonical GraphSpec

### 6.1 Graph Fields

Required fields:

- `Name`: graph name.
- `Schema`: graph schema class path. First implementation supports only `/Script/BlueprintGraph.EdGraphSchema_K2`, resolved dynamically.
- `Nodes`: array of `NodeSpec`.
- `Links`: array of `LinkSpec`.

Optional fields:

- `GraphGuid`: extracted graph GUID evidence. Apply must not require it for identity.
- `Category`: function/macro authoring category when UE exposes a stable editable field.
- `Description`: graph description when UE exposes a stable editable field.
- `Signature`: function/macro signature extension; see sections 11 and 12.

Identity rules:

- `UbergraphPages`: `Name`
- `FunctionGraphs`: `Name`
- `MacroGraphs`: `Name`

Unsupported fields fail validation with `UnknownGraphField`.

### 6.2 NodeSpec

Canonical node shape:

```json
{
  "Id": "Print",
  "NodeGuid": "00000000000000000000000000000000",
  "Class": "/Script/BlueprintGraph.K2Node_CallFunction",
  "Capability": "CallFunction",
  "Member": {
    "Kind": "MemberRef",
    "OwnerClass": "/Script/Engine.KismetSystemLibrary",
    "Name": "PrintString"
  },
  "PinOverrides": [],
  "Position": {
    "X": 320,
    "Y": 0
  },
  "Comment": ""
}
```

Rules:

- `Id` is the sidecar identity within one graph. It must be unique, stable, and human-editable.
- `Class` is the primary UE node identity. It is resolved dynamically and drives adapter lookup.
- `Capability` is an optional semantic alias for diagnostics/templates. It must not become an independent source of behavior when `Class` is present.
- `NodeGuid` is UE identity evidence. It is optional for authored input. Extract includes it when UE provides a stable value. Apply uses `Id` as sidecar identity and may preserve `NodeGuid` on update when safe.
- `Member` is required only for node classes whose adapter declares a reflected member requirement.
- `PinOverrides` is sparse. It contains only authored pin defaults, dynamic pin declarations, or pin metadata that cannot be reconstructed from node class/member reflection.
- `Position` contains authoring layout metadata. It is included because graph readability is shared project state.
- `Comment` is authoring metadata. Empty string and missing field both mean no comment.

GraphCore validation must not require a hard-coded list of node classes. It validates shape and asks `NodeAdapterRegistry` whether the resolved node class is supported for the current apply/extract tier. Unsupported node classes fail with `UnsupportedGraphNodeClass`.

### 6.3 PinOverrideSpec

Pin overrides are intentionally sparse:

```json
{
  "Pin": "InString",
  "Direction": "Input",
  "Type": {
    "Kind": "DefinitionRef",
    "Id": "Type.String"
  },
  "DefaultValue": "Hello",
  "DefaultObject": null,
  "DefaultTextValue": "",
  "Hidden": false,
  "AdvancedView": false
}
```

Rules:

- `Pin` is the link address inside one node. For ordinary pins it should equal UE `PinName`. Dynamic pins may use a stable sidecar id when UE display name is not unique.
- `Direction` is optional for ordinary reflected pins because it can be reconstructed from the allocated UE pin. It is required for dynamic pins.
- `Type` uses `FEdGraphPinType` shape or `DefinitionRef` to a `PinType`.
- Default fields are authoritative only for input pins when the pin is not linked.
- Missing default fields mean reset to node/pin baseline.
- Linked input pins may still carry default values in UE, but diff treats link state as authoritative behavior. Apply may clear irrelevant defaults when UE requires it for compile stability.
- Output pin defaults are rejected unless a node adapter explicitly declares that a specific output default is editable and roundtrippable.
- Transient UE pin fields, compiler state, cache flags, and editor-only expansion state must not be represented.

Extract rules:

- Extract emits pin overrides only when a pin has an authored non-baseline default, dynamic pin metadata, or non-reconstructable authoring metadata.
- Link endpoints may reference pins that are not present in `PinOverrides`; apply reconstructs them through node allocation before link creation.

### 6.4 LinkSpec

Canonical compact link shape:

```json
{
  "From": "BeginPlay.then",
  "To": "Print.execute"
}
```

Expanded form is also accepted and canonicalized:

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

### 6.5 MemberRef

Canonical member reference shape:

```json
{
  "Kind": "MemberRef",
  "OwnerClass": "/Script/Engine.Actor",
  "Name": "ReceiveBeginPlay"
}
```

Optional fields:

- `Guid`: extracted UE member GUID when available.
- `SelfContext`: boolean for member references that are intentionally self-scoped.

Rules:

- Function/event/member identity must prefer explicit owner class plus name.
- `Guid` is supporting evidence, not the only identity.
- `OwnerClass: "Self"` resolves against staged `ParentClass`, `Body.Variables`, `Body.Components`, and generated class evidence.
- Blueprint variables referenced by get/set nodes must exist in `Body.Variables` or be parent-class reflected properties.
- Component variables referenced by get/set nodes must exist in `Body.Components` or parent/native component evidence.
- Function refs resolve through `UClass::FindFunctionByName` or equivalent reflection; no concrete function whitelist is allowed.
- Property refs resolve through `FProperty` reflection and Blueprint variable metadata; no concrete variable whitelist is allowed.

---

## 7. Reflection-First Node Capability Model

The full `GraphSpec` is designed for arbitrary K2 graphs, but implementation lands in support tiers. Tiers describe current adapter coverage, not a hard-coded semantic universe.

### 7.1 Adapter Contract

Each node adapter declares:

- supported UE node class or reflected base capability.
- required refs: none, `MemberRef`, `TimelineRef`, or graph signature.
- how to create the node using dynamic class resolution.
- how to bind reflected function/property/event metadata.
- how to let UE allocate/reconstruct pins.
- which pin overrides are accepted.
- how to extract a canonical sparse `NodeSpec`.

Adapter code may include the minimum UE headers needed for the node classes it actually manipulates in that task. It must not include a broad inventory of all possible `K2Node_*` headers.

### 7.2 Tier 1: EventGraph 基础节点

First implementation should support `Body.UbergraphPages` through these adapter capabilities:

- Event node adapter
  - Initial UE class: `/Script/BlueprintGraph.K2Node_Event`
  - Required `MemberRef`.
  - Event function resolves by owner class plus function name.
  - Initial smoke events may include `Actor.ReceiveBeginPlay` and `Actor.ReceiveTick`, but implementation must not be limited by a hard-coded event-name whitelist if UE reflection resolves the event safely.
- Call function adapter
  - Initial UE class: `/Script/BlueprintGraph.K2Node_CallFunction`
  - Required `MemberRef`.
  - Supports ordinary callable functions resolved from reflected `UFunction`.
  - Function-specific pin shape comes from UE allocation after binding the `UFunction`.
- Variable get/set adapters
  - Initial UE classes: `/Script/BlueprintGraph.K2Node_VariableGet`, `/Script/BlueprintGraph.K2Node_VariableSet`
  - Required `MemberRef`.
  - Variable/property resolves from Blueprint variables, parent class `FProperty`, component vars, or staged component evidence.
- Self adapter
  - Initial UE class: `/Script/BlueprintGraph.K2Node_Self`
  - No member ref.

Tier 1 may extract unsupported existing node classes as `_Skipped.Graphs` evidence. Apply of a managed graph containing unsupported node classes must fail unless those nodes are absent because the sidecar intentionally deletes them.

### 7.3 Capability Boundary

Support expansion should add adapter capability, not branchy graph logic. Examples:

- Adding `K2Node_Branch` should add a tiny adapter for a reflected/dynamic node class with known pin reconstruction, not special-case every branch link path in GraphCore.
- Adding custom events may require a `CustomEvent` adapter because event creation has UE lifecycle semantics; it must still avoid project-specific event name inventories.
- Adding array/map/make struct nodes should prefer reflected pin allocation and typed default fragments.

---

## 8. Region Semantics

### 8.1 `Body.UbergraphPages`

- Missing `UbergraphPages` means empty authoritative event graph set except for UE-required baseline default graph.
- Missing graph page means remove that graph page when UE allows it.
- The default `EventGraph` is special:
  - If sidecar omits all event graphs, apply should remove user-authored nodes from the default graph, not necessarily delete the UE-required graph object.
  - Extract should emit an empty `EventGraph` only if it is needed as canonical baseline, or emit an empty array when no user-authored graph data remains. The implementation plan must pick one canonical behavior and test it.
- Missing node means delete the node.
- Missing link means delete the link.
- Missing pin default means reset to node/pin baseline.

### 8.2 `Body.FunctionGraphs`

- Missing user function graph means delete it.
- `Signature` is authoritative for user-created function graphs.
- Required interface function graph cannot be silently deleted while the interface remains implemented.
- Removing an interface via `Body.ImplementedInterfaces` should remove now-unneeded interface stubs unless they are also represented as user-authored functions with a distinct identity.

### 8.3 `Body.MacroGraphs`

- Missing macro graph means delete it.
- `Signature` is authoritative for macro tunnel pins.
- Missing tunnel pin from signature means remove it if UE allows; otherwise fail before mutation with `InvalidMacroSignature`.

### 8.4 `Body.Timelines`

- Missing timeline means delete it.
- Existing graph nodes that reference a deleted timeline must also be deleted or rejected before mutation. The implementation plan must choose one behavior per step:
  - First timeline step may reject deletion when graph references exist.
  - Full timeline step should reconcile graph nodes in the same staged apply.

---

## 9. FunctionGraphs

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

Required additional adapters:

- Function entry adapter.
- Function result adapter.

Rules:

- Function entry/result nodes must match `Signature` after apply.
- Interface-required function stubs are governed jointly by `Body.ImplementedInterfaces` and `Body.FunctionGraphs`.
- Interface signature compatibility is checked through reflected interface `UFunction` metadata, not a hard-coded interface list.
- User-created functions absent from sidecar are deleted.

---

## 10. MacroGraphs

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

Required additional adapter:

- Tunnel node adapter for macro entry and exit tunnel nodes.

Rules:

- Macro tunnel nodes are part of canonical graph representation.
- Macro `Signature` is authoritative and must match tunnel pins.
- Wildcard pins are rejected in the first macro implementation unless a node adapter can roundtrip them deterministically.

---

## 11. Timelines

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
- Timeline graph nodes must reference `Body.Timelines[*].Name` or a timeline `DefinitionRef`.
- Timeline-generated variables are derived implementation details and must not be independently authored in `Body.Variables`.
- Timeline track curve representation must use canonical key data or `DefinitionRef` to `TimelineCurve`.
- External curve asset refs are future extension and must use `AssetRef` when added.

---

## 12. Apply Pipeline

Graph/timeline apply must remain staged:

1. Parse all graph/timeline regions and graph-relevant `Definitions`.
2. Validate graph region arrays, duplicate identities, node classes, pin overrides, member refs, links, timeline refs.
3. Resolve `DefinitionRef` chains and cross-region references against staged `ParentClass`, `Variables`, `Components`, `ImplementedInterfaces`, `ClassDefaults`, graph specs, and timeline specs.
4. Resolve graph schema and node classes dynamically; reject missing/unsupported classes before mutation.
5. Ask `NodeAdapterRegistry` for adapters required by the resolved node classes. Missing adapter fails with `UnsupportedGraphNodeClass`.
6. Preflight parent class changes that would invalidate reflected graph refs.
7. Snapshot previous Blueprint graph state enough to rollback on failure, or mutate only after all preflight succeeds.
8. Apply dependency regions in this order:
   - `ParentClass`
   - `ImplementedInterfaces`
   - `Variables`
   - `Components`
   - `ClassDefaults`
   - `Timelines`
   - `UbergraphPages`
   - `FunctionGraphs`
   - `MacroGraphs`
9. For each graph:
   - create/find graph through `K2GraphAdapter`.
   - create/find nodes by sidecar `Id`.
   - bind reflected refs through node adapters.
   - call UE pin allocation/reconstruction.
   - apply sparse pin overrides.
   - create links through graph schema validation.
   - delete omitted nodes/links.
10. Compile Blueprint.
11. If compile fails, rollback staged mutations where possible and return `BlueprintCompileFailed` with graph path diagnostics.
12. Save only after successful compile when `bSaveAsset` is requested.

No step may leave partially applied graph changes saved to disk.

---

## 13. Extract

Extract produces canonical graph specs:

- Graph arrays sorted by UE graph array order.
- Nodes sorted by UE node order, with `NodeGuid` fallback for deterministic output when UE order changes.
- Pin overrides emitted only for non-baseline authored defaults, dynamic pins, or non-reconstructable authoring metadata.
- Links sorted by source node id, source pin id, target node id, target pin id.
- Repeated refs may be hoisted into `Definitions` when doing so improves stability or avoids large repeated payloads.
- Supported node classes extract fully through adapters.
- Unsupported existing node classes are reported in `_Skipped.Graphs` with count and node class names. Once a graph region is marked fully managed for a tier, unsupported nodes in that tier should make extract report incomplete evidence rather than pretending full roundtrip.

Extract must not include editor-only graph zoom/pan/selection/tab state, compiler intermediates, transient pin flags, or cache fields.

---

## 14. Diff

Diff parses desired graph regions using the same parser as apply, extracts current evidence, resolves definitions, and compares canonical semantic objects.

Required diff paths:

- `/Definitions/<DefinitionId>`
- `/Body/UbergraphPages/<GraphName>`
- `/Body/UbergraphPages/<GraphName>/Nodes/<NodeId>`
- `/Body/UbergraphPages/<GraphName>/Nodes/<NodeId>/PinOverrides/<PinId>`
- `/Body/UbergraphPages/<GraphName>/Links/<FromNode>:<FromPin>-><ToNode>:<ToPin>`
- `/Body/FunctionGraphs/<GraphName>`
- `/Body/MacroGraphs/<GraphName>`
- `/Body/Timelines/<TimelineName>`

Diff statuses:

- `unchanged`: canonical current and desired values match.
- `changed`: value exists on both sides but differs.
- `missing`: desired exists, current does not.
- `extra`: current exists, desired does not.
- `unsupported`: current cannot be represented by implemented graph capability.

Missing supported graph region fields mean empty authoritative state, consistent with the rest of `UBlueprint` Body semantics.

---

## 15. Validation Diagnostics

Required diagnostic codes:

- `InvalidGraphRegionType`: graph region is not an array.
- `DuplicateGraphName`: duplicate graph identity in one region.
- `InvalidGraphSchema`: unsupported or unresolved graph schema.
- `UnknownGraphField`: unknown field in graph spec.
- `UnknownDefinitionKind`: definition kind is not supported.
- `CircularDefinitionReference`: definition refs form a cycle.
- `UnresolvedDefinitionReference`: definition ref target does not exist.
- `DuplicateGraphNodeId`: duplicate node identity in one graph.
- `UnresolvedGraphNodeClass`: node `Class` cannot be loaded.
- `UnsupportedGraphNodeClass`: resolved node class has no adapter for this tier.
- `InvalidGraphNodeCapability`: optional `Capability` conflicts with the resolved node adapter.
- `MissingGraphMemberReference`: required member ref is missing.
- `UnresolvedGraphMemberReference`: member ref cannot be resolved through reflection or staged Blueprint regions.
- `InvalidGraphPin`: pin shape or pin direction is invalid.
- `InvalidGraphPinDefault`: authored pin default cannot be applied.
- `DuplicateGraphLink`: duplicate link.
- `UnresolvedGraphLinkEndpoint`: link node/pin endpoint does not exist after node reconstruction.
- `InvalidGraphLinkType`: UE graph schema rejects the link.
- `InvalidFunctionSignature`: function signature and graph entry/result nodes conflict.
- `InvalidMacroSignature`: macro signature and tunnel nodes conflict.
- `UnresolvedTimelineReference`: graph node references a missing timeline.
- `UnsupportedTimelineTrackKind`: timeline track kind is not implemented.
- `BlueprintCompileFailed`: apply compiled graph into an invalid Blueprint.

Diagnostics should point at the narrowest possible JSON path.

---

## 16. Implementation Steps

This spec is implemented through checkpoint tasks, not one large patch.

### Step 1: GraphCore Data Model And Validation

- Add parser/serializer helpers for `GraphSpec`, `NodeSpec`, `PinOverrideSpec`, `LinkSpec`, `MemberRef`, and graph-relevant `DefinitionRef`.
- Add `NodeAdapterRegistry` interface with no broad K2 inventory.
- Keep apply rejection for non-empty graph regions except validation tests that exercise parser failure modes.
- Add automation tests for duplicate graph names, duplicate node ids, unresolved definitions, unresolved links, unsupported node classes, and invalid schema.

### Step 2: Reflection-First Tier 1 Extract/Diff

- Extract `Body.UbergraphPages` for Tier 1 adapter classes.
- Resolve event/function/property refs through reflection.
- Emit sparse pin overrides only.
- Diff supported EventGraph nodes and links.
- Existing unsupported nodes produce `_Skipped.Graphs` and `unsupported` diff entries.

### Step 3: EventGraph Apply

- Apply `Body.UbergraphPages` for Tier 1 adapter classes.
- Rebuild missing/extra nodes and links authoritatively.
- Compile and verify a real `BeginPlay -> PrintString` or equivalent smoke graph without hard-coding `PrintString` as a special function.

### Step 4: Definitions Reuse Hardening

- Support `DefinitionRef` for `MemberRef`, `ClassRef`, `AssetRef`, `PinType`, and simple `Literal`.
- Add canonicalization tests proving inline refs and definition refs compare equal.
- Add cycle detection tests.

### Step 5: FunctionGraphs

- Add `Signature` support.
- Apply/extract/diff user-created function graphs.
- Support interface-required function stubs controlled jointly by `Body.ImplementedInterfaces`.
- Validate interface signatures through reflected interface metadata.

### Step 6: MacroGraphs

- Add macro `Signature` and tunnel node support.
- Apply/extract/diff user-created macros with supported internal Tier 1 nodes.

### Step 7: Timelines

- Add `Body.Timelines` parser/serializer.
- Support float tracks first.
- Support `TimelineCurve` definition refs where useful.
- Reconcile timeline node references in graph regions.
- Compile and smoke a timeline Blueprint.

### Step 8: Final Graph Roundtrip Smoke

- Run UBT against `C:/AVH1`.
- Run focused automation for `AssetFactory.AssetDocument.UBlueprint`.
- Run full `AssetFactory.AssetDocument` automation.
- Run external HTTP smoke with a real graph sidecar under `C:/AVH1/Content/AssetDocumentSmoke/`.
- Update deferred-fields doc by removing completed graph/timeline entries or narrowing their remaining limits.
- Update final report.

---

## 17. Verification Requirements

Minimum automation coverage:

- Validate:
  - empty graph regions still pass.
  - duplicate graph/node/link identity fails.
  - unresolved/circular `DefinitionRef` fails.
  - unsupported node class fails apply.
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
  - extract is sparse for baseline pins.
  - unsupported nodes are surfaced as skipped evidence.
- Diff:
  - unchanged graph reports unchanged.
  - inline ref and equivalent `DefinitionRef` compare unchanged.
  - omitted graph/node/link reports extra.
  - desired graph/node/link missing in asset reports missing.
- Architecture:
  - GraphCore tests do not depend on concrete `K2Node_*` subclasses.
  - adding a new node class requires adapter registration, not edits to graph parser/diff core.
- Smoke:
  - external HTTP apply-file/extract/diff for one Blueprint with graph content.

---

## 18. Open Risks

- UE may regenerate node GUIDs or pins during compile/reconstruction; sidecar identity must not depend solely on `NodeGuid`.
- Some K2 nodes allocate pins dynamically based on member refs or default values; implementation must reconstruct pins before applying links.
- Reflection may expose enough metadata to validate a function/property but not enough lifecycle operations to create a valid node. Such cases need thin adapters, not graph-core branching.
- Interface function stubs may be owned by UE/interface logic rather than user graph arrays. The function graph step must preserve required stubs while still deleting omitted user-created functions.
- Timeline templates produce generated variables and graph node references. Timeline apply must avoid treating derived timeline variables as user-authored `Body.Variables`.
- Unsupported node extraction must be honest. It is better to report unsupported evidence than to emit a lossy graph that looks roundtrippable.
- Overusing `Definitions` for every node/pin would make agent editing worse. Definitions are for reusable refs/fragments, not the default storage for graph topology.

---

## 19. Success Criteria

The graph regions are considered complete for this spec when:

- `Body.UbergraphPages`, `Body.FunctionGraphs`, `Body.MacroGraphs`, and `Body.Timelines` have stable schema docs and tests.
- GraphCore remains thin: no concrete function/property/project inventories, no raw UE graph dump fields, and no node-specific behavior in parser/diff core.
- Supported graph/timeline content can be apply-file roundtripped through a real `UBlueprint` in `C:/AVH1`.
- Omitted graph/timeline sidecar content deletes or resets current asset state according to the authoritative Body semantics.
- Unsupported graph/timeline content fails with clear diagnostics or is reported as skipped evidence on extract/diff.
- `Definitions` can be used for reusable graph refs/fragments without moving normal graph topology out of `Body.*Graphs`.
- No Blueprint Generator or Blueprint-specific MCP tool is introduced.
