# WidgetBlueprint AssetDocument 权威表示设计

日期：2026-06-23

状态：Draft，待用户审阅

分支：`feature/asset-document-structured-capabilities-spec`

范围：完整覆盖 asset class 为 `/Script/UMGEditor.WidgetBlueprint` 的 `UWidgetBlueprint` AssetDocument 支持。分段只用于 implementation task、checkpoint commit 和回归验证；最终任务不是 MVP，而是完成完整 WidgetBlueprint authoring surface 的 apply/extract/diff 闭环。

---

## 1. 背景

WidgetBlueprint 是 AssetDocument 结构化能力线的下一个完整 asset class。它不应回到旧 `FWidgetBlueprintGenerator` / `AssetType: "WidgetBlueprint"` 模式，也不应新增 widget 专用 MCP tool。WidgetBlueprint 应像 AnimMontage、AnimSequence、UBlueprint 一样，通过 exact profile、capability、RegionPolicy、canonical extraction、authoritative apply 和通用 AssetDocument MCP tools 接入。

本 spec 只关注 AssetDocument。旧 generator、旧 MCP generator schema 和 `generate_assets` 可以作为历史行为参考，但不得作为实现入口、公共 schema 或验收标准。

UE 侧事实锚点：

- `UWidgetBlueprint` 位于 `UMGEditor/Public/WidgetBlueprint.h`，继承 `UBaseWidgetBlueprint`，核心 editor-only authoring surface 包括 `Bindings`、`Animations`、`WidgetVariableNameToGuidMap`、`PaletteCategory`、`bCanCallInitializedWithoutPlayerContext`。
- `UBaseWidgetBlueprint` / Blueprint 基类继续拥有普通 Blueprint 的 `ParentClass`、variables、graphs、generated class CDO defaults 等 surface；WidgetBlueprint 需要复用 UBlueprint 已合入的 graph/capability 表示，而不是重写一套 K2 graph JSON。
- `UWidgetTree` 位于 `UMG/Public/Blueprint/WidgetTree.h`，核心 authoring surface 是 `RootWidget`、`NamedSlotBindings` 和 editor-only `AllWidgets`。`AllWidgets` 是树索引/cache，不是 authoring source。
- `FDelegateEditorBinding` 存在于 `WidgetBlueprint.h`，使用 `ObjectName`、`PropertyName`、`FunctionName`、`SourceProperty`、`SourcePath`、`MemberGuid`、`Kind` 表达 UMG property bindings。
- `UWidgetAnimation` 位于 `UMG/Public/Animation/WidgetAnimation.h`，拥有 `MovieScene`、`AnimationBindings`、`DisplayLabel` 等 sequence data。Widget animation 是 WidgetBlueprint package 内 owned object，不是外部 asset ref。

---

## 2. 目标

完整目标：

1. 定义 `/Script/UMGEditor.WidgetBlueprint` 的 AssetDocument exact profile。
2. 使用通用 structured document shape：`Target`、`Class`、`Action`、`Definitions`、`Properties`、`Body`。
3. `Body` 覆盖 WidgetBlueprint 的完整作者可维护 surface：parent class、interfaces、variables、class defaults、WidgetTree、named slot content、bindings、animations、graphs、palette/editor behavior 和 widget variable GUID map。
4. sidecar 是 WidgetBlueprint 的权威文本表示。对 profile 声明的 managed regions，sidecar 缺失 entry 表示删除、清除 override 或恢复 baseline，而不是保留当前 `.uasset` 状态。
5. Apply 必须 staged/preflight：先完整解析和验证所有 authored regions，确认不会半写入后再 materialize。
6. Extract 必须输出 canonical sidecar 表示；diff 必须比较 sidecar 与当前 `.uasset` 的语义等价性。
7. WidgetTree 必须支持动态 widget class loading 和反射属性/slot 设置，不维护静态 widget 类型白名单。
8. Graph regions 复用已合入的 UBlueprint graph model、node adapters 和 unsupported diagnostics；WidgetTree 不伪装成 graph。
9. Bindings 必须验证 target widget/property 和 source function/property path，不允许保存 broken binding。
10. Animations 必须有明确 authored representation。实现可以按 task 完成不同 animation track 类型，但最终验收不能把 non-empty animation 数据列为 skipped 后仍宣称完整 WidgetBlueprint 完成。
11. 完成 UBT、focused automation、full AssetDocument automation、MCP tests 和 external HTTP smoke，并留下真实 smoke asset 与 sidecar 供检查。

---

## 3. 非目标

本 spec 不做：

- 不扩展旧 `FWidgetBlueprintGenerator`。
- 不扩展 `MCP/schemas/WidgetBlueprint.md` 作为 generator schema；AssetDocument schema 只进入 `MCP/schemas/AssetDocument.md` 和 profile inspection。
- 不新增 `create_widget_blueprint`、`update_widget_tree` 等专用 MCP tool。
- 不把 raw `.uasset`、raw serialized widget tree、raw MovieScene binary blob 作为 agent-facing authoring format。
- 不管理 referenced child WidgetBlueprint asset 的内部 WidgetTree；`UserWidget` / nested widget class 只作为 class 或 asset reference。
- 不把 editor viewport zoom、designer selection、preview-only transient state 作为 authored data。
- 不支持 `UEditorUtilityWidgetBlueprint`、`UUserWidgetBlueprint` 或其他 WidgetBlueprint 派生 asset class，除非后续单独 profile 明确覆盖。

---

## 4. 文档形状

最小 WidgetBlueprint sidecar：

```json
{
  "SchemaVersion": 1,
  "Target": "/Game/UI/WBP_InventoryPanel",
  "Class": "/Script/UMGEditor.WidgetBlueprint",
  "Action": "CreateOrUpdate",
  "Definitions": {},
  "Properties": {},
  "Body": {
    "ParentClass": {
      "Kind": "ClassRef",
      "Class": "/Script/UMG.UserWidget"
    },
    "ImplementedInterfaces": [],
    "Variables": [],
    "ClassDefaults": {},
    "WidgetTree": {
      "RootWidget": null,
      "NamedSlotBindings": {}
    },
    "Bindings": [],
    "Animations": [],
    "UbergraphPages": [],
    "FunctionGraphs": [],
    "MacroGraphs": [],
    "Palette": {},
    "EditorOptions": {},
    "WidgetVariableGuids": {}
  }
}
```

规则：

- `Class` 必须是 `/Script/UMGEditor.WidgetBlueprint`。
- `Body.ParentClass` 必须解析为 `UUserWidget` 子类。默认是 `/Script/UMG.UserWidget`。
- `Body.WidgetTree` 管 `UWidgetTree`；顶层 `Properties` 只管 `UWidgetBlueprint` asset 自身反射属性，不管 widget template properties。
- `Body.ClassDefaults` 管 generated class CDO editable properties。
- `Body.UbergraphPages`、`Body.FunctionGraphs`、`Body.MacroGraphs` 使用 UBlueprint graph model。
- `Body.Animations` 管 package-owned `UWidgetAnimation` objects；它们不是外部 asset refs。
- `Definitions` 可放复用 class refs、asset refs、literal fragments、common graph fragments 和 common animation fragments，但不拥有 package 生命周期。

---

## 5. 权威语义

WidgetBlueprint sidecar 的语义是：

```text
WidgetBlueprint baseline
+ AssetDocument sidecar
= expected UWidgetBlueprint asset state
```

对 profile 声明的所有 WidgetBlueprint regions：

- sidecar 缺失 `Body.WidgetTree.RootWidget` 表示当前 WidgetBlueprint 应没有 root widget。
- sidecar 缺失 widget tree entry 表示删除该 widget 及其 owned children 或 named slot content。
- sidecar 缺失 widget property/slot property 表示恢复该 widget/template/slot 的 baseline value。
- sidecar 缺失 binding 表示移除该 widget property binding。
- sidecar 缺失 animation 表示删除该 owned `UWidgetAnimation`。
- sidecar 缺失 animation track/channel/key 表示删除对应 authored timeline data。
- sidecar 缺失 graph/function/macro 表示删除对应 graph，前提是该 graph 不属于 UE 必需的 baseline graph。
- sidecar 缺失 palette/editor option 表示恢复 profile baseline。

这条规则只受 profile 边界限制。实现 task 可以分段，但每个未完成 region 必须在 implementation plan 和 deferred-fields 文档里有显式状态；不能把未建模 region 悄悄解释为“保留当前资产状态”。

---

## 6. Authoring Surface Inventory

### 6.1 Managed authored data

| Region | UE surface | 分类 | 处理方式 |
| --- | --- | --- | --- |
| `Body.ParentClass` | Blueprint parent class / generated class parent | lifecycle authoring | WidgetBlueprint lifecycle adapter 创建并设置 `UUserWidget` 子类 |
| `Body.ImplementedInterfaces` | `UBlueprint::ImplementedInterfaces` | Blueprint authoring | 复用 UBlueprint region |
| `Body.Variables` | `UBlueprint::NewVariables` plus widget variables | Blueprint/widget authoring | 复用 UBlueprint variable shape；widget variable flags 由 WidgetTree entry 驱动 |
| `Body.ClassDefaults` | generated `UUserWidget` CDO | reflected default diff | 复用 CDO setter/default reducer |
| `Body.WidgetTree` | `UWidgetTree::RootWidget`、`NamedSlotBindings` | tree authoring | Widget tree region adapter authoritative rebuild |
| `Body.Bindings` | `UWidgetBlueprint::Bindings` | binding authoring | binding region adapter validated apply/extract/diff |
| `Body.Animations` | `UWidgetBlueprint::Animations`、`UWidgetAnimation::MovieScene`、`AnimationBindings` | owned sequence authoring | widget animation region adapter |
| `Body.UbergraphPages` | event graphs | graph authoring | 复用 UBlueprint graph model |
| `Body.FunctionGraphs` | functions and binding source graphs | graph authoring | 复用 UBlueprint graph model |
| `Body.MacroGraphs` | macro graphs | graph authoring | 复用 UBlueprint graph model |
| `Body.Palette` | `PaletteCategory` and generated CDO palette category source | editor-visible metadata | small object region |
| `Body.EditorOptions` | `bCanCallInitializedWithoutPlayerContext` and stable editor behavior flags | asset editor behavior | small object region |
| `Body.WidgetVariableGuids` | `WidgetVariableNameToGuidMap` | stable reference metadata | managed map region |

### 6.2 Reflected property delta

`Properties` 可用于 `UWidgetBlueprint` asset 自身 editable reflected properties，前提是该 property 未被上面的 managed region 声明。profile inspection 应显示 managed surface，避免 agent 同时在 `Properties` 和 `Body` 管同一个 UE field。

### 6.3 Referenced asset-owned data

这些不进入当前 WidgetBlueprint sidecar：

- 作为 `UserWidget` / custom widget class 引用的另一个 WidgetBlueprint 的内部 WidgetTree。
- 引用的 material、texture、font、data table、style asset 的内部内容。
- parent `UUserWidget` C++ class definition 或 parent WidgetBlueprint body。

### 6.4 Derived/cache/editor transient data

这些不作为 authored data：

- `UWidgetTree::AllWidgets`。
- generated class runtime binding arrays、runtime animation binding cache、compiled template cache。
- designer selection、hierarchy expansion state、viewport zoom、preview player context。
- asset registry generated tags and thumbnail data。

---

## 7. Body Region Design

### 7.1 `Body.ParentClass`

表达 WidgetBlueprint generated class 的 parent class。

```json
"ParentClass": {
  "Kind": "ClassRef",
  "Class": "/Script/Game.InventoryPanelWidget"
}
```

Policy:

- `RegionId`: `Body.ParentClass`
- `RegionKind`: `Object`
- `DefaultSource`: `/Script/UMG.UserWidget`
- `ReducerMode`: `DefaultDiff`
- `ApplyMode`: `SetWidgetBlueprintParentClass`
- `ManagedUePropertyPaths`: `ParentClass`

Rules:

- class 必须是 `UUserWidget` 子类，不能是任意 `UObject`。
- parent class 变化是 structural change，必须触发 compile/save。
- parent change 可能影响 `ClassDefaults`、bindings source functions、named slots 和 exposed variables；preflight 必须先验证依赖。

### 7.2 `Body.ImplementedInterfaces`

复用 UBlueprint 形状：

```json
"ImplementedInterfaces": [
  {
    "Interface": {
      "Kind": "ClassRef",
      "Class": "/Script/Game.WidgetCommandSource"
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
- `ManagedUePropertyPaths`: `ImplementedInterfaces`

### 7.3 `Body.Variables`

复用 UBlueprint `FEdGraphPinType` variable shape。WidgetTree 中 `IsVariable` / `VariableName` 驱动 widget member variables；手写 `Variables` 不应重复声明同名 widget variable。

```json
"Variables": [
  {
    "Name": "SelectedItemId",
    "Type": {
      "PinCategory": "name"
    },
    "DefaultValue": "None",
    "Category": "Inventory"
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

- Widget variables generated from WidgetTree entries have canonical source in `Body.WidgetTree`.
- If a non-widget variable conflicts with a widget variable name, validation fails.
- Variable node support in graphs follows existing UBlueprint graph adapters。

### 7.4 `Body.ClassDefaults`

表达 generated `UUserWidget` CDO default property overrides。

```json
"ClassDefaults": {
  "bIsFocusable": true,
  "RenderOpacity": 0.95
}
```

Policy:

- `RegionId`: `Body.ClassDefaults`
- `RegionKind`: `Object`
- `DefaultSource`: parent class CDO
- `ReducerMode`: `DefaultDiff`
- `ApplyMode`: `SetGeneratedClassDefaults`
- `ManagedUePropertyPaths`: generated CDO editable properties

Rules:

- Widget template properties 不放在 `ClassDefaults`。
- Missing property 表示恢复 parent/default CDO value。

### 7.5 `Body.WidgetTree`

表达 `UWidgetTree::RootWidget` 和 `NamedSlotBindings`。

```json
"WidgetTree": {
  "RootWidget": {
    "Name": "RootCanvas",
    "Class": "/Script/UMG.CanvasPanel",
    "IsVariable": false,
    "Properties": {},
    "Children": [
      {
        "Name": "TitleText",
        "Class": "/Script/UMG.TextBlock",
        "IsVariable": true,
        "Slot": {
          "Anchors": {
            "Minimum": [0.5, 0.0],
            "Maximum": [0.5, 0.0]
          },
          "Alignment": [0.5, 0.0],
          "Position": [0.0, 48.0]
        },
        "Properties": {
          "Text": "Inventory"
        },
        "Children": []
      }
    ]
  },
  "NamedSlotBindings": {
    "Header": {
      "Name": "HeaderText",
      "Class": "/Script/UMG.TextBlock",
      "Properties": {
        "Text": "Header"
      }
    }
  }
}
```

Policy:

- `RegionId`: `Body.WidgetTree`
- `RegionKind`: `Tree`
- `Identity`: widget `Name` inside the owning `UWidgetTree`
- `DefaultSource`: empty root and empty named slot bindings
- `ReducerMode`: `ManagedRegion`
- `ApplyMode`: `RebuildWidgetTree`
- `ManagedUePropertyPaths`: `WidgetTree.RootWidget`, `WidgetTree.NamedSlotBindings`

Rules:

- `Class` accepts full class path or dynamically resolvable class name; implementation should use `StaticLoadClass` / existing class finder utilities with `UWidget` base class.
- Do not maintain a static widget type allowlist. Support is determined by class resolution, `UWidget` inheritance, slot/container capabilities, reflection writability and explicit diagnostics.
- `Children` are authoritative for panel/content widgets. Omitted child means delete/reset that child.
- For single-content widgets, adapter detects supported content API dynamically, such as `SetContent`, `GetContentSlot` or `INamedSlotInterface` where applicable.
- `Slot` belongs to the relationship between a child and parent, not the widget object itself.
- Slot properties are applied through target slot reflection after child insertion. Common shorthand may be accepted only if it canonicalizes to stable slot property names.
- `Properties` are widget template reflected property diffs. Missing property restores class/template default.
- `IsVariable` and `VariableName` drive generated widget member variables and `WidgetVariableNameToGuidMap` integration.
- Nested `UserWidget` instances are represented as a widget class reference on the widget template, not as an inline child WidgetBlueprint document.

### 7.6 `Body.Bindings`

表达 `UWidgetBlueprint::Bindings`。

```json
"Bindings": [
  {
    "Widget": "HealthBar",
    "Property": "Percent",
    "Kind": "Function",
    "Function": "GetHealthPercent"
  },
  {
    "Widget": "TitleText",
    "Property": "Text",
    "Kind": "Property",
    "SourcePath": [
      {
        "Kind": "Property",
        "Name": "Title"
      }
    ]
  }
]
```

Policy:

- `RegionId`: `Body.Bindings`
- `RegionKind`: `Array`
- `Identity`: `{Widget, Property}`
- `DefaultSource`: empty
- `ReducerMode`: `ManagedRegion`
- `ApplyMode`: `RebuildWidgetPropertyBindings`
- `ManagedUePropertyPaths`: `Bindings`

Rules:

- `Widget` must resolve to a widget in `Body.WidgetTree` or existing compiled widget tree during extract.
- `Property` must correspond to a bindable delegate target, normally `PropertyNameDelegate`.
- `Kind` supports `Function` and `Property` for v1.
- Function binding must resolve to an existing function graph or parent function with compatible return type/signature and pure/const requirements.
- Property binding must serialize `SourcePath` as canonical path segments; `MemberGuid` may be extracted and preserved as stability metadata, but name/path remains public identity.
- Conflicting duplicate `{Widget, Property}` bindings fail validation.

### 7.7 `Body.Animations`

表达 `UWidgetBlueprint::Animations` owned `UWidgetAnimation` objects。

```json
"Animations": [
  {
    "Name": "Intro",
    "DisplayLabel": "Intro",
    "LegacyFinishOnStop": false,
    "Bindings": [
      {
        "Widget": "TitleText",
        "Guid": "11111111-2222-3333-4444-555555555555"
      }
    ],
    "MovieScene": {
      "FrameRate": "30fps",
      "PlaybackRange": {
        "Start": 0,
        "End": 30
      },
      "Tracks": [
        {
          "Target": {
            "Widget": "TitleText",
            "Property": "RenderOpacity"
          },
          "Type": "Float",
          "Channels": [
            {
              "Name": "Value",
              "Keys": [
                { "Frame": 0, "Value": 0.0 },
                { "Frame": 30, "Value": 1.0 }
              ]
            }
          ]
        }
      ]
    }
  }
]
```

Policy:

- `RegionId`: `Body.Animations`
- `RegionKind`: `Timeline`
- `Identity`: animation `Name`
- `DefaultSource`: empty
- `ReducerMode`: `ManagedRegion`
- `ApplyMode`: `RebuildWidgetAnimations`
- `ManagedUePropertyPaths`: `Animations`

Rules:

- Missing animation means delete the owned `UWidgetAnimation` object and generated animation variable.
- Widget animation bindings must reference widgets in `Body.WidgetTree` by name.
- Track support should be implemented by generic MovieScene adapters where possible, but the public shape must remain widget-oriented.
- Implementation tasks may add track families incrementally, but final acceptance must cover at least scalar/property tracks commonly authored in WidgetBlueprints: float, color, transform, visibility or equivalent UMG property channels.
- Unsupported existing tracks during extract must appear as explicit diagnostics and prevent final "complete WidgetBlueprint" acceptance until covered or deliberately moved to deferred with user approval.

### 7.8 Graph regions

`Body.UbergraphPages`、`Body.FunctionGraphs`、`Body.MacroGraphs` reuse the UBlueprint graph model:

```json
"FunctionGraphs": [
  {
    "Name": "GetHealthPercent",
    "Schema": "/Script/UMGEditor.WidgetGraphSchema",
    "Signature": {
      "Outputs": [
        {
          "Name": "ReturnValue",
          "Type": {
            "PinCategory": "real",
            "PinSubCategory": "float"
          }
        }
      ]
    },
    "Nodes": [],
    "Links": []
  }
]
```

Policy:

- `Body.UbergraphPages`: `RegionKind = Graph`, managed paths `UbergraphPages`
- `Body.FunctionGraphs`: `RegionKind = Graph`, managed paths `FunctionGraphs`
- `Body.MacroGraphs`: `RegionKind = Graph`, managed paths `MacroGraphs`
- `CanonicalizerHookName`: reuse or extend `UBlueprintGraph` where graph representation matches

Rules:

- Graph node support follows existing `AssetDocumentGraph*` parser/diff/node adapters.
- Widget-specific nodes such as animation event nodes must have explicit adapters before they can be applied.
- Unsupported nodes fail apply preflight when authored and surface `_Skipped` evidence when extracted from existing assets.

### 7.9 `Body.Palette`

表达 stable palette metadata。

```json
"Palette": {
  "Category": "Inventory"
}
```

Policy:

- `RegionId`: `Body.Palette`
- `RegionKind`: `Object`
- `DefaultSource`: parent/generated CDO or empty category
- `ReducerMode`: `DefaultDiff`
- `ApplyMode`: `SetWidgetPaletteMetadata`
- `ManagedUePropertyPaths`: `PaletteCategory` and generated CDO palette source where applicable

Rules:

- `PaletteCategory` is serialized in asset tags, but UE notes the actual value lives on the `UUserWidget` CDO. Apply must update the authoritative source and mirror field consistently.

### 7.10 `Body.EditorOptions`

表达 stable WidgetBlueprint editor/runtime behavior flags。

```json
"EditorOptions": {
  "bCanCallInitializedWithoutPlayerContext": true
}
```

Policy:

- `RegionId`: `Body.EditorOptions`
- `RegionKind`: `Object`
- `DefaultSource`: class/profile default
- `ReducerMode`: `DefaultDiff`
- `ApplyMode`: `SetWidgetBlueprintEditorOptions`
- `ManagedUePropertyPaths`: `bCanCallInitializedWithoutPlayerContext`

Rules:

- 只接收稳定、serialized、非 transient 的 WidgetBlueprint options。
- 新增 options 必须先通过 UE header inventory 分类，不允许直接把 editor settings dump 进 Body。

### 7.11 `Body.WidgetVariableGuids`

表达 widget/animation variable name to GUID map。

```json
"WidgetVariableGuids": {
  "TitleText": "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
  "Intro": "ffffffff-1111-2222-3333-444444444444"
}
```

Policy:

- `RegionId`: `Body.WidgetVariableGuids`
- `RegionKind`: `Map`
- `Identity`: variable name
- `DefaultSource`: generated deterministic GUIDs or current baseline when adopting existing asset
- `ReducerMode`: `ManagedRegion`
- `ApplyMode`: `SetWidgetVariableGuidMap`
- `ManagedUePropertyPaths`: `WidgetVariableNameToGuidMap`

Rules:

- This region exists for reference stability, rename safety and extract/apply/diff determinism.
- Missing entry for an existing widget/animation variable means regenerate or remove according to corresponding WidgetTree/Animation entry.
- New sidecar authored without GUIDs may allow adapter to generate deterministic GUIDs, then sync writeback should canonicalize the sidecar.

---

## 8. RegionPolicy Summary

| Region | Kind | Reducer | Apply | Managed UE paths |
| --- | --- | --- | --- | --- |
| `Body.ParentClass` | Object | DefaultDiff | `SetWidgetBlueprintParentClass` | `ParentClass` |
| `Body.ImplementedInterfaces` | Array | ManagedRegion | `RebuildInterfaceRegion` | `ImplementedInterfaces` |
| `Body.Variables` | Array | ManagedRegion | `RebuildBlueprintVariables` | `NewVariables` |
| `Body.ClassDefaults` | Object | DefaultDiff | `SetGeneratedClassDefaults` | generated CDO editable properties |
| `Body.WidgetTree` | Tree | ManagedRegion | `RebuildWidgetTree` | `WidgetTree.RootWidget`, `WidgetTree.NamedSlotBindings` |
| `Body.Bindings` | Array | ManagedRegion | `RebuildWidgetPropertyBindings` | `Bindings` |
| `Body.Animations` | Timeline | ManagedRegion | `RebuildWidgetAnimations` | `Animations` |
| `Body.UbergraphPages` | Graph | ManagedRegion | `RebuildBlueprintGraphs` | `UbergraphPages` |
| `Body.FunctionGraphs` | Graph | ManagedRegion | `RebuildBlueprintFunctionGraphs` | `FunctionGraphs` |
| `Body.MacroGraphs` | Graph | ManagedRegion | `RebuildBlueprintMacroGraphs` | `MacroGraphs` |
| `Body.Palette` | Object | DefaultDiff | `SetWidgetPaletteMetadata` | `PaletteCategory` |
| `Body.EditorOptions` | Object | DefaultDiff | `SetWidgetBlueprintEditorOptions` | `bCanCallInitializedWithoutPlayerContext` |
| `Body.WidgetVariableGuids` | Map | ManagedRegion | `SetWidgetVariableGuidMap` | `WidgetVariableNameToGuidMap` |

---

## 9. Implementation Segmentation

这次最终任务必须完成完整 WidgetBlueprint 支持。分段只用于 task-level checkpoint、review 和 regression，不用于降低最终验收范围。

### Task 1: Profile, lifecycle, schema and empty asset contract

Scope:

- 新建 `FWidgetBlueprintAssetDocumentProfile`。
- 新建 `FWidgetBlueprintAssetDocumentCapability` skeleton。
- 注册 exact class `/Script/UMGEditor.WidgetBlueprint`。
- 实现 WidgetBlueprint lifecycle：create/load/save/compile for `UWidgetBlueprint`。
- `CreateTemplate` 输出完整 Body key skeleton。
- `MCP/schemas/AssetDocument.md` 和 profile inspection 暴露 WidgetBlueprint shape。

Focused regression:

- 创建空 `UWidgetBlueprint` sidecar。
- extract/diff 空 body 等价。
- wrong parent class、unknown body key、wrong body type 有明确 diagnostics。

Checkpoint:

- UBT。
- `AssetFactory.AssetDocument.WidgetBlueprint.Profile` automation。
- checkpoint commit。

### Task 2: WidgetTree authoritative rebuild

Scope:

- 实现 `Body.WidgetTree` parser、validator、apply、extract、diff。
- 支持 root widget、panel children、single-content widget、named slot bindings、slot properties、widget reflected properties、`IsVariable`、`VariableName`。
- 动态 class lookup，拒绝非 `UWidget` class。
- staged preflight 防半写入。

Focused regression:

- create root tree。
- update removes omitted child。
- update restores omitted widget property/slot property to default。
- named slot add/update/remove。
- nested `UserWidget` class reference。
- invalid widget class / duplicate name / invalid parent-child relationship diagnostics。

Checkpoint:

- UBT。
- `AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree` automation。
- apply-file -> extract -> diff smoke on real WBP。
- checkpoint commit。

### Task 3: Variables, class defaults, palette and editor options

Scope:

- 复用 UBlueprint `Variables` / `ClassDefaults` capability where safe。
- WidgetTree variable exposure 与 `Variables` 冲突检测。
- Implement `Body.Palette`、`Body.EditorOptions`、`Body.WidgetVariableGuids`。
- Ensure compile/save updates generated class and asset registry-relevant data。

Focused regression:

- CDO default apply/extract/diff。
- widget variable GUID canonical writeback。
- palette category apply/extract。
- `bCanCallInitializedWithoutPlayerContext` apply/extract。
- duplicate widget/non-widget variable name fails。

Checkpoint:

- UBT。
- `AssetFactory.AssetDocument.WidgetBlueprint.Metadata` automation。
- full `AssetFactory.AssetDocument.WidgetBlueprint` so far。
- checkpoint commit。

### Task 4: Bindings

Scope:

- Implement `Body.Bindings` parser、validator、apply、extract、diff。
- Support function and property bindings。
- Validate bindable target delegate, widget existence, source path/function, return type/signature and pure/const requirements。
- Integrate with graph/function regions when binding source function is authored in sidecar。

Focused regression:

- property binding roundtrip。
- function binding roundtrip。
- binding removal by omission。
- invalid widget/property/source function fails without mutating asset。
- extracted legacy/existing binding canonicalizes to `Body.Bindings`。

Checkpoint:

- UBT。
- `AssetFactory.AssetDocument.WidgetBlueprint.Bindings` automation。
- MCP tests for schema/profile diagnostics。
- checkpoint commit。

### Task 5: Graph regions for WidgetBlueprint

Scope:

- Reuse UBlueprint graph model for WidgetBlueprint event/function/macro graphs。
- Add widget-specific node adapters needed for binding source functions and animation events。
- Preserve unsupported graph diagnostics and no-half-delete behavior from UBlueprint graph work。

Focused regression:

- simple function graph used by binding applies and extracts。
- event graph with supported event/call/self/variable nodes roundtrips。
- unsupported node extraction reports `_Skipped` and apply rejects authored unsupported node。
- deletion safety checks concrete node support, not only adapter class membership。

Checkpoint:

- UBT。
- `AssetFactory.AssetDocument.WidgetBlueprint.Graphs` automation。
- `AssetFactory.AssetDocument.GraphCore` regression。
- checkpoint commit。

### Task 6: Animations

Scope:

- Implement `Body.Animations` parser、validator、apply、extract、diff。
- Own `UWidgetAnimation` lifecycle inside WidgetBlueprint package。
- Materialize `MovieScene` playback range, bindings and supported tracks/channels。
- Cover common UMG authored tracks required for realistic WidgetBlueprints。
- Remove omitted animations/tracks/channels/keys authoritatively。

Focused regression:

- create animation with widget binding and float/property keys。
- update removes omitted key/track/animation。
- extract/diff stable after apply。
- invalid widget binding or unsupported track fails preflight。
- existing unsupported track is explicit diagnostic and blocks final completeness unless documented and approved as deferred。

Checkpoint:

- UBT。
- `AssetFactory.AssetDocument.WidgetBlueprint.Animations` automation。
- real WBP animation smoke。
- checkpoint commit。

### Task 7: Integration, sync and external smoke

Scope:

- Full profile body key validation。
- RegionPolicy sync hashes for all WidgetBlueprint regions。
- apply-file sidecar canonical writeback for generated GUIDs/default reduction。
- external HTTP smoke using `C:/AVH1/Content/AssetDocumentSmoke/WBP_WidgetBlueprintSmoke.assetdoc.json`。
- docs/reports final benchmark。

Focused regression:

- full `AssetFactory.AssetDocument.WidgetBlueprint`。
- full `AssetFactory.AssetDocument`。
- `MCP npm test`。
- live editor HTTP smoke: apply-file -> extract -> diff。
- preserved sidecar and asset for manual inspection。

Checkpoint:

- UBT。
- focused automation。
- full automation。
- MCP tests。
- external smoke。
- final review report。
- closure checkpoint commit。

---

## 10. Verification Standard

最低完成标准：

1. UBT compile against validation host project, not the main project plugin shadow path.
2. Focused WidgetBlueprint automation passes:

```text
AssetFactory.AssetDocument.WidgetBlueprint
```

3. Existing graph/core regressions pass:

```text
AssetFactory.AssetDocument.GraphCore
AssetFactory.AssetDocument.UBlueprint
```

4. Full AssetDocument automation passes:

```text
AssetFactory.AssetDocument
```

5. MCP tests pass:

```text
Push-Location MCP; npm test; Pop-Location
```

6. External HTTP smoke passes against a real editor server:

```text
apply-file -> extract -> diff
```

Smoke asset:

```text
/Game/AssetDocumentSmoke/WBP_WidgetBlueprintSmoke
```

Smoke sidecar:

```text
C:/AVH1/Content/AssetDocumentSmoke/WBP_WidgetBlueprintSmoke.assetdoc.json
```

最终报告必须记录：

- worktree、branch、base、reviewed diff range。
- 每个 task checkpoint commit。
- completed regions。
- deferred/excluded fields。
- UBT、focused automation、full automation、MCP、external smoke 结果。
- real asset path 和 sidecar path。
- spec review、code quality review、fix review 的结论。

---

## 11. Review Gates

实现阶段必须按 task 做 checkpoint 和 review：

- 每个 task 开始记录 `TASK_BASE=HEAD`。
- task 完成后先跑 focused verification，再 checkpoint commit。
- task review 只审 `TASK_BASE..HEAD`。
- spec final review 只审 `SPEC_BASE..HEAD`。
- review prompt 只包含当前 task acceptance criteria、相关文件和 diff range，不附带全线程历史。
- 任何 skipped/unsupported WidgetBlueprint region 都必须在当前 task 的验收里显式解释，不能隐藏在日志里。

---

## 12. Open Risks

- `UWidgetAnimation` / MovieScene track coverage 可能是最大实现量。若实现中发现 UE track API 或 canonical representation 需要更大 adapter，应保留完整最终目标，但在 implementation plan 中把 track families 拆成多个 checkpoint task。
- WidgetTree 的 named slot inheritance 可能需要 parent WidgetBlueprint / parent class introspection。实现必须区分当前 asset owned named slot content 和 referenced parent asset-owned content。
- Binding validation depends on generated class state and function graph compilation. Preflight should avoid mutating asset before function/property source validation succeeds。
- Widget variable GUID canonical writeback may require sidecar sync update after apply。该行为必须和已有 canonicalizer writeback 机制一致。

---

## 13. Deferred / Excluded Fields

详见：

```text
docs/superpowers/specs/asset-document-deferred-fields/2026-06-23-widgetblueprint.md
```

Main spec 的完整目标不因 deferred 文档自动缩小。只有明确标记为 excluded 的 derived/cache/editor transient data 不属于最终验收；任何仍属于 authored data 的 deferred 项都必须在 implementation plan 里安排 task，或在用户批准后调整 scope。
