# UBlueprint AssetDocument 权威表示设计

日期：2026-06-19

状态：Draft，待用户审阅

分支：`feature/asset-document-structured-capabilities-spec`

范围：只覆盖 asset class 为 `/Script/Engine.Blueprint` 的普通 `UBlueprint`。不覆盖 `UWidgetBlueprint`、`UAnimBlueprint` 或其他专用 Blueprint 派生资产。

---

## 1. 背景

AssetDocument 的结构化能力线已经明确：sidecar 不是一次性 patch，也不是 generator payload，而是资产的另一种作者可维护表示。对 `UBlueprint` 来说，这个原则必须更强：Blueprint sidecar 应作为 Blueprint asset 的权威文本形态，`.uasset` 是由 sidecar materialize 出来的 Unreal 表示。

因此，本设计不新增 `BlueprintGenerator`，不走 `AssetGeneratorRegistry`，不添加专用 MCP tool。`UBlueprint` 通过 AssetDocument profile、body regions、RegionPolicy、EvidenceExtractor、DefaultReducer、AuthoritativeApplyAdapter 接入现有结构化能力框架。

UE 侧事实锚点：

- `UBlueprint` 的核心 authoring surface 位于 `Engine/Classes/Engine/Blueprint.h`：`SimpleConstructionScript`、`UbergraphPages`、`FunctionGraphs`、`MacroGraphs`、`ComponentTemplates`、`Timelines`、`InheritableComponentHandler`、`NewVariables`、`ImplementedInterfaces`。
- owned SCS component tree 由 `USimpleConstructionScript` / `USCS_Node` 管理，关键 API 包括 `CreateNode`、`AddNode`、`RemoveNode`、`RemoveNodeAndPromoteChildren`、`GetAllNodes`、`GetDefaultSceneRootNode`。
- inherited/native component overrides 由 `UInheritableComponentHandler` 和 `FComponentKey` 管理，关键 API 包括 `CreateOverridenComponentTemplate`、`RemoveOverridenComponentTemplate`、`FindKey`、`GetOverridenComponentTemplate`。
- Blueprint mutation 应使用 `FBlueprintEditorUtils` 这类 editor API，并在结构变化后调用 `MarkBlueprintAsStructurallyModified` / compile/save 流程。

---

## 2. 目标

完整目标：

1. 定义 `/Script/Engine.Blueprint` 的 AssetDocument profile。
2. 使用 structured template shape：`Target`、`Class`、`Action`、`Definitions`、`Properties`、`Body`，不再使用 `AssetType: "Blueprint"`。
3. `Body` 表达普通 `UBlueprint` 的全部作者可编辑结构。
4. sidecar 是权威文本表示。sidecar 缺失的变量、组件、override、graph、function、macro、timeline 等，在 apply 后不应继续留在 `.uasset` 中。
5. Apply 语义是 authoritative rebuild：对 profile 声明的 Blueprint surface 先恢复到 baseline，再按 sidecar 重建。
6. Extract 语义是 canonical sidecar export，不是临时草稿。
7. Diff 语义是比较 sidecar 表示与当前 `.uasset` 表示是否等价。
8. 支持 owned SCS components，也支持 inherited/native component property、attach/root override。
9. 组件定位 key 使用 `{Name, OwnerClass}`。owned self component 的 `OwnerClass` 使用 `"Self"`。
10. 实现可以按 region 分阶段，但 spec 必须把未实现 region 作为明确缺口，不允许把未建模数据默认为“保留当前 asset 状态”。

---

## 3. 非目标

本 spec 不覆盖：

- `UWidgetBlueprint` 的 WidgetTree、Bindings、Designer metadata。
- `UAnimBlueprint` 的 AnimGraph、StateMachine、Skeleton/PreviewMesh 专用生命周期。
- BehaviorTree、StateTree、Material、Niagara 等非 `UBlueprint` asset。
- 运行时动态 AddComponent 节点产生的实例化逻辑。
- 对 graph 节点做自然语言 DSL。Graph 只能通过明确、可验证、可 roundtrip 的结构化表示进入 sidecar。
- 新增 `BlueprintGenerator` 或复用旧 `FBlueprintGenerator` 作为 AssetDocument 实现路径。

---

## 4. 文档形状

最小 `UBlueprint` sidecar：

```json
{
  "SchemaVersion": 1,
  "Target": "/Game/Blueprints/BP_Enemy",
  "Class": "/Script/Engine.Blueprint",
  "Action": "CreateOrUpdate",
  "Definitions": {},
  "Properties": {},
  "Body": {
    "ParentClass": {
      "Kind": "ClassRef",
      "Class": "/Script/Engine.Character"
    },
    "ImplementedInterfaces": [],
    "Variables": [],
    "Components": [],
    "UbergraphPages": [],
    "FunctionGraphs": [],
    "MacroGraphs": [],
    "Timelines": []
  }
}
```

规则：

- `Class` 表示 asset class，必须为 `/Script/Engine.Blueprint`。
- `Body.ParentClass` 表示 Blueprint generated class 的 parent class。
- `Body` key 使用 UE 稳定字段名，例如 `UbergraphPages`、`FunctionGraphs`、`MacroGraphs`，不使用短别名如 `Graphs`、`Functions`。
- `Properties` 只用于 `UBlueprint` asset 自身的 reflected property delta。Blueprint generated CDO defaults 不放在顶层 `Properties`，而放入 `Body.ClassDefaults`。
- `Definitions` 用于复用 class refs、asset refs、embedded objects、literal graph fragments 等，不拥有 UE package 生命周期。

---

## 5. 权威语义

`UBlueprint` profile 与 AnimMontage/AnimSequence 的 delta sidecar 方向一致，但在语义上更接近完整作者表示：

```text
Blueprint baseline
+ AssetDocument sidecar
= expected UBlueprint asset state
```

对 profile 声明的所有 Blueprint regions：

- sidecar 缺失某个 region，表示该 region 应恢复到 baseline 或空作者状态。
- sidecar 缺失某个 array entry，表示该 entry 应删除。
- sidecar 缺失某个 property，表示该 property override 应清除或恢复默认。
- sidecar 缺失 inherited/native component override，表示本 Blueprint 对该 component 的 override 应移除。
- sidecar 缺失 graph/function/macro/timeline，表示该 graph/function/macro/timeline 不应存在于当前 Blueprint。

这条规则只受 profile 边界限制，不受实施阶段限制。某 region 暂未实现时，必须在 deferred-fields 文档中记录，并且 validate/apply 应显式拒绝该 region 或拒绝宣称完整 roundtrip。

---

## 6. Body Regions

### 6.1 `Body.ParentClass`

表达 `UBlueprint::ParentClass`。

```json
"ParentClass": {
  "Kind": "ClassRef",
  "Class": "/Script/Engine.Character"
}
```

Policy:

- `RegionId`: `Body.ParentClass`
- `RegionKind`: `Object`
- `DefaultSource`: profile default `/Script/Engine.Actor` for Actor-first templates, or explicit class required for non-Actor templates
- `ReducerMode`: `DefaultDiff`
- `ApplyMode`: `SetBlueprintParentClass`
- `ManagedUePropertyPaths`: `ParentClass`

Apply notes:

- Existing Blueprint parent changes are structural changes and require compile/reinstancing.
- If parent changes invalidate SCS components, graph calls, or default properties, validation should report dependent region errors before mutating.

### 6.2 `Body.ImplementedInterfaces`

表达 `UBlueprint::ImplementedInterfaces`。

```json
"ImplementedInterfaces": [
  {
    "Interface": {
      "Kind": "ClassRef",
      "Class": "/Script/Game.Damageable"
    }
  }
]
```

Policy:

- `RegionId`: `Body.ImplementedInterfaces`
- `RegionKind`: `Array`
- `Identity`: interface class path
- `DefaultSource`: empty
- `ReducerMode`: `ManagedRegion`
- `ApplyMode`: `RebuildInterfaceRegion`
- `ManagedUePropertyPaths`: `ImplementedInterfaces`, interface stub graphs owned by the interface

Apply notes:

- Apply should add missing interfaces and remove interfaces absent from sidecar.
- Removing an interface must also remove or reconcile interface function graphs according to graph region policy.
- Interface class must be an interface class.

### 6.3 `Body.Variables`

表达 `UBlueprint::NewVariables`。

```json
"Variables": [
  {
    "Name": "Health",
    "Type": {
      "PinCategory": "real",
      "PinSubCategory": "float"
    },
    "DefaultValue": "100.0",
    "Flags": {
      "InstanceEditable": true,
      "ExposeOnSpawn": false,
      "Private": false
    },
    "Category": "Stats",
    "Tooltip": "Enemy health"
  }
]
```

Policy:

- `RegionId`: `Body.Variables`
- `RegionKind`: `Array`
- `Identity`: `Name`
- `DefaultSource`: empty
- `ReducerMode`: `ManagedRegion`
- `ApplyMode`: `RebuildBlueprintVariables`
- `ManagedUePropertyPaths`: `NewVariables`

Rules:

- Missing variable means remove the member variable and its variable nodes when UE utilities require it.
- Variable type uses `FEdGraphPinType` shape, not a short string-only type.
- `DefaultValue` uses UE canonical default value string until a typed default value fragment is proven roundtrippable for all pin categories.
- Rename is represented as delete old name plus add new name unless an implementation task adds stable GUID-based migration.

### 6.4 `Body.ClassDefaults`

表达 Blueprint generated CDO default property overrides。

```json
"ClassDefaults": {
  "MaxHealth": 100,
  "WalkSpeed": 450.0
}
```

Policy:

- `RegionId`: `Body.ClassDefaults`
- `RegionKind`: `Object`
- `DefaultSource`: parent class CDO or parent Blueprint generated CDO
- `ReducerMode`: `DefaultDiff`
- `ApplyMode`: `SetGeneratedClassDefaults`
- `ManagedUePropertyPaths`: generated class CDO editable properties

Rules:

- Missing default property means restore parent/default CDO value for that property.
- Only editable, non-transient, non-deprecated CDO properties are authorable.
- Component template defaults belong to `Body.Components`, not `Body.ClassDefaults`.

### 6.5 `Body.Components`

表达 owned SCS component tree，以及 inherited/native component overrides。

```json
"Components": [
  {
    "Key": {
      "Name": "CapsuleComponent",
      "OwnerClass": "/Script/Engine.Character"
    },
    "Scope": "Inherited",
    "Class": "/Script/Engine.CapsuleComponent",
    "Root": true,
    "Properties": {
      "CapsuleRadius": 42.0,
      "CapsuleHalfHeight": 96.0
    }
  },
  {
    "Key": {
      "Name": "Sensor",
      "OwnerClass": "Self"
    },
    "Scope": "OwnedSCS",
    "Class": "/Script/Engine.SphereComponent",
    "AttachTo": {
      "Name": "CapsuleComponent",
      "OwnerClass": "/Script/Engine.Character"
    },
    "Properties": {
      "SphereRadius": 500.0
    }
  }
]
```

Policy:

- `RegionId`: `Body.Components`
- `RegionKind`: `Tree`
- `Identity`: `{Key.Name, Key.OwnerClass}`
- `DefaultSource`: parent SCS/native templates plus empty owned SCS set
- `ReducerMode`: `ManagedRegion`
- `ApplyMode`: `RebuildBlueprintComponentTree`
- `ManagedUePropertyPaths`: `SimpleConstructionScript`, `ComponentTemplates`, `InheritableComponentHandler`

Rules:

- `Scope` values:
  - `OwnedSCS`: component node owned by this Blueprint's `USimpleConstructionScript`.
  - `Inherited`: component inherited from parent Blueprint SCS.
  - `Native`: component inherited from native C++ class default subobject.
- Missing `OwnedSCS` component means delete the SCS node.
- Missing `Inherited` or `Native` component entry means remove this Blueprint's override for that component.
- Missing component property means clear override and restore baseline template value.
- `AttachTo` and `Root` are authoritative when present. Absence means restore baseline attach/root relation for inherited/native or use profile default relation for owned SCS.
- `{Name, OwnerClass}` is the public key. Internally this should map to `FComponentKey` or a validated equivalent.
- Owned components may use `"OwnerClass": "Self"` only.

### 6.6 `Body.UbergraphPages`

表达 `UBlueprint::UbergraphPages`，即 event graph pages。

```json
"UbergraphPages": [
  {
    "Name": "EventGraph",
    "Nodes": [],
    "Links": []
  }
]
```

Policy:

- `RegionId`: `Body.UbergraphPages`
- `RegionKind`: `GraphArray`
- `Identity`: graph name
- `DefaultSource`: profile default event graph for newly created Blueprint
- `ReducerMode`: `ManagedGraphRegion`
- `ApplyMode`: `RebuildBlueprintGraphs`
- `ManagedUePropertyPaths`: `UbergraphPages`

Rules:

- Missing graph page means remove it.
- Graph representation must be structural and roundtrippable: node class, node guid, member references, pins, links, default pin values, node position, comment metadata, and graph schema.
- User-facing sidecar may not use opaque serialized binary graph blobs as the primary authoring surface.

### 6.7 `Body.FunctionGraphs`

表达 user-created function graphs and interface function stubs stored in `UBlueprint::FunctionGraphs`.

```json
"FunctionGraphs": [
  {
    "Name": "ApplyDamage",
    "Signature": {
      "Inputs": [],
      "Outputs": []
    },
    "Nodes": [],
    "Links": []
  }
]
```

Policy:

- `RegionId`: `Body.FunctionGraphs`
- `RegionKind`: `GraphArray`
- `Identity`: function name
- `DefaultSource`: empty plus required interface stubs
- `ReducerMode`: `ManagedGraphRegion`
- `ApplyMode`: `RebuildBlueprintFunctionGraphs`
- `ManagedUePropertyPaths`: `FunctionGraphs`

Rules:

- Missing function graph means remove it if it is not required by an implemented interface.
- Interface-required graphs are governed jointly by `Body.ImplementedInterfaces` and `Body.FunctionGraphs`.
- Function signature is authoritative and must match graph entry/return nodes after apply.

### 6.8 `Body.MacroGraphs`

表达 `UBlueprint::MacroGraphs`。

Policy:

- `RegionId`: `Body.MacroGraphs`
- `RegionKind`: `GraphArray`
- `Identity`: macro name
- `DefaultSource`: empty
- `ReducerMode`: `ManagedGraphRegion`
- `ApplyMode`: `RebuildBlueprintMacroGraphs`
- `ManagedUePropertyPaths`: `MacroGraphs`

Rules:

- Missing macro graph means remove it.
- Macro tunnel nodes and local pin signatures are part of the canonical graph representation.

### 6.9 `Body.Timelines`

表达 `UBlueprint::Timelines` (`UTimelineTemplate`)。

```json
"Timelines": [
  {
    "Name": "DoorTimeline",
    "Length": 1.0,
    "Tracks": []
  }
]
```

Policy:

- `RegionId`: `Body.Timelines`
- `RegionKind`: `Array`
- `Identity`: timeline name
- `DefaultSource`: empty
- `ReducerMode`: `ManagedRegion`
- `ApplyMode`: `RebuildTimelineTemplates`
- `ManagedUePropertyPaths`: `Timelines`

Rules:

- Missing timeline means remove it.
- Timeline variables, timeline component references, and graph nodes that call timeline events must be reconciled with graph regions.

### 6.10 `Body.BlueprintMetadata`

表达 Blueprint-level authoring metadata that is not graph layout/user state.

Potential fields:

- Blueprint category/display metadata.
- Description/documentation metadata.
- Deprecated flag or deprecation message when stable and author-editable.

Policy:

- `RegionId`: `Body.BlueprintMetadata`
- `RegionKind`: `Object`
- `DefaultSource`: empty/default metadata
- `ReducerMode`: `DefaultDiff`
- `ApplyMode`: `SetBlueprintMetadata`

Rules:

- Editor tab layout, viewport camera, graph zoom/pan, selection state, compile cache, and thumbnail data are not authoring metadata.

---

## 7. Baseline 与缺失字段

Baseline sources:

- New Blueprint baseline: asset created with `UBlueprintFactory` / `FKismetEditorUtilities::CreateBlueprint` for the declared `ParentClass`.
- Existing Blueprint baseline: parent class, parent Blueprint generated class, native component templates, inherited SCS templates, and empty owned regions.
- Component property baseline:
  - `OwnedSCS`: component class CDO or profile-created template default before sidecar values.
  - `Inherited`: inherited SCS template resolved from parent Blueprint.
  - `Native`: native class default subobject template.
- Graph baseline:
  - Empty user graph arrays, plus engine-required default event graph when UE creates one.
  - Required interface function stubs derived from `Body.ImplementedInterfaces`.

Missing semantics:

- Missing top-level `Body` for `/Script/Engine.Blueprint` means all body regions are defaulted. Validation should require `Body` once this profile is active to avoid accidental full reset from a malformed document.
- Missing region entry means delete or reset that entry.
- Missing scalar/object field inside a region means reset that field to region baseline.

---

## 8. Apply 流程

High-level flow:

1. Validate `Class == "/Script/Engine.Blueprint"` and target path.
2. Resolve `Body.ParentClass`.
3. Create or load `UBlueprint`.
4. Parse all regions into a staged plan before mutating.
5. Validate cross-region references:
   - variables referenced by graph nodes exist or are created in the same plan;
   - component `AttachTo` keys resolve to owned, inherited, or native components;
   - interface function graphs match implemented interfaces;
   - timeline graph references match `Body.Timelines`;
   - parent class supports SCS components when `Body.Components` contains component tree entries.
6. Reset Blueprint-owned surface to baseline for all profile regions.
7. Apply regions in dependency order:
   - ParentClass
   - ImplementedInterfaces
   - Variables
   - Components
   - ClassDefaults
   - Timelines
   - Graphs
   - BlueprintMetadata
8. Refresh nodes, compile Blueprint, save package if requested.
9. Update sync state per region.

Failure handling:

- Preflight must catch all parse and reference errors before destructive reset.
- If mutation fails after reset starts, apply should restore from an in-memory snapshot when feasible, or fail without saving and mark the asset dirty for manual recovery.
- Validation errors should report `Body` path and UE target surface.

---

## 9. Extract 与 Diff

Extract:

- Loads the `UBlueprint`.
- Builds evidence for every profile region.
- Reduces evidence against baseline.
- Emits canonical AssetDocument JSON.
- Uses stable ordering:
  - interfaces by class path;
  - variables by declaration order unless a stable user order exists;
  - components by tree order;
  - graphs by Blueprint array order;
  - graph nodes by graph order plus node guid fallback.

Diff:

- Parses sidecar into the same staged representation used by apply.
- Extracts current Blueprint evidence.
- Compares canonical sidecar representation to current reduced evidence per region.
- Reports changed, missing, extra, and incompatible entries with region path diagnostics.

Roundtrip invariant:

```text
sidecar -> apply -> extract -> canonical sidecar
```

should produce no semantic diff for implemented regions.

---

## 10. Region Implementation Order

The complete profile owns all regions above, but implementation should be split:

1. Profile skeleton, template, schema, inspection, validation rejecting unsupported graph authoring.
2. `Body.ParentClass`, `Body.ImplementedInterfaces`, `Body.Variables`.
3. `Body.Components` for owned SCS components.
4. `Body.Components` for inherited/native override, attach, and root.
5. `Body.ClassDefaults`.
6. `Body.UbergraphPages`, `Body.FunctionGraphs`, `Body.MacroGraphs` using a canonical K2 graph representation.
7. `Body.Timelines` plus graph reconciliation.
8. Full extract/diff/apply-file smoke on real `/Game/AssetDocumentSmoke/BP_*` assets.

Each implementation task should have task-level checkpoint commits and reviews over `TASK_BASE..HEAD`.

---

## 11. Validation 与测试

Minimum verification per region:

- C++ automation test for validate/apply/extract/diff.
- Negative tests for missing class, invalid component key, unsupported pin type, duplicate identity, unresolved graph reference, and invalid attach/root relation.
- Compile verification after apply.
- Real asset smoke in validation host with sidecar path under `C:/AVH1/Content/AssetDocumentSmoke/`.
- MCP schema/profile/template tests without adding Blueprint-specific MCP tools.

Focused smoke target:

```text
/Game/AssetDocumentSmoke/BP_BlueprintSidecarSmoke
C:/AVH1/Content/AssetDocumentSmoke/BP_BlueprintSidecarSmoke.assetdoc.json
```

Smoke should prove:

- sidecar creates or updates a real `UBlueprint`;
- variables and components materialize;
- inherited/native overrides apply and clear when removed from sidecar;
- extract returns canonical body regions;
- diff is clean after apply;
- Blueprint compiles.

---

## 12. Open Implementation Risks

These are not design blockers, but they must be resolved in implementation plans:

- Stable graph representation for K2 nodes must not become an opaque binary dump.
- ParentClass changes may invalidate multiple regions and need staged rebuild ordering.
- Inherited/native attach/root override may require UE editor APIs beyond raw template mutation.
- Variable rename and component rename need either delete/add semantics or stable identity migration.
- Interface function graph ownership can overlap with `Body.FunctionGraphs`.
- Timeline templates have graph references and generated variables that need cross-region repair.
- Blueprint compile/reinstance failures must be reported with useful diagnostics and no silent partial save.
