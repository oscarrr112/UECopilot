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

sidecar 是 agent 可编辑、可 diff、可 roundtrip 的 Blueprint graph 作者表面；`.uasset` graph 是由 sidecar materialize 出来的 UE 表示。设计参考 Godot `.tres/.tscn` 的文本资源原则：稳定局部 identity、显式引用、外部/内部资源分层、默认值稀疏保存；但 AssetDocument 仍使用 JSON 形状，不引入 Godot section 语法。

实现上允许分 step 落地，但公开 sidecar 形状必须在同一个 spec 中稳定。第一轮实现优先完成共享 `GraphCore` 和 `Body.UbergraphPages`，后续 step 复用同一套 graph 形状扩展 `FunctionGraphs`、`MacroGraphs` 和 `Timelines`。

核心原则：

- sidecar 仍是权威作者表示，不是 patch/op DSL。
- graph 表示必须是结构化 JSON，不允许把 `.uasset` graph 二进制 blob 或 editor serialization dump 当作作者表面。
- `GraphCore` 必须是薄中间层：只管 graph/node/pin/link identity、规范排序、diagnostics、引用解析和 staged apply 编排。
- K2 node 细节必须通过 reflection-first adapter hook 扩展；禁止在 `GraphCore` 或 region apply 主流程中维护硬编码 node/function/property 清单。
- 缺失的 graph、node、pin default、link、timeline 表示应在 apply 后从 `.uasset` 中移除或恢复基线。
- 不新增 `BlueprintGenerator`，不复用旧 generator 作为 AssetDocument apply 路径；可以把既有 graph/node 代码当作 UE API 参考。

---

## 2. 范围

### 2.1 范围内

- 普通 `UBlueprint` 的 K2 graph authoring。
- graph extract、validate、apply、diff 的规范表示。
- graph identity、node identity、link identity、default pin values、member/function refs、layout metadata。
- `Definitions` 对 graph refs、pin type、复杂 literal、timeline curve 等可复用 fragment 的承载。
- 存储在 `UBlueprint::UbergraphPages` 中的 Event graph pages。
- 存储在 `UBlueprint::FunctionGraphs` 中的 user-created function graphs 和 interface function stubs。
- 存储在 `UBlueprint::MacroGraphs` 中的 user-created macro graphs。
- 存储在 `UBlueprint::Timelines` 中的 Timeline templates，以及 timeline graph node reconciliation。
- graph apply 后的 compile 流程，以及失败时不保存半写入状态。

### 2.2 范围外

- `UWidgetBlueprint` widget tree/bindings。
- `UAnimBlueprint` AnimGraph、state machine、skeleton lifecycle。
- natural-language graph DSL。
- 第一实现 step 中支持任意 K2 node。
- 每用户 editor state：graph zoom、pan、selection、open tabs、editor viewport。
- graph 执行时动态生成的 runtime component instances。
- 可从 graph semantics 重建的 UE compiler/intermediate/cache fields。

---

## 3. 架构：薄 Core + 反射 Adapter

Graph 支持分为四层：

1. `GraphCore`
   - 解析 `GraphSpec`、`NodeSpec`、`PinOverrideSpec`、`LinkSpec`。
   - 校验 JSON 形状、duplicate ids、link endpoint syntax、规范排序。
   - 解析 `DefinitionRef` 和 cross-region references。
   - 产出尽可能窄的 JSON path diagnostics。
   - 不理解具体 K2 node 行为。

2. 普通 `RegionPolicy`
   - Graph regions 使用既有 `RegionPolicy` 模型，并设置 `ApplyMode: RebuildGraphRegion`。
   - policy 定义 identity rule、default source、comparison rule、reducer mode 和 after-apply hooks。
   - 本 spec 中任何 graph-specific policy 表述都只是 common policy model 的 preset/section，不是新的平行 runtime 系统。

3. `K2GraphAdapter`
   - 通过 UE editor APIs 创建或定位 `UEdGraph`。
   - 根据 `Schema` 动态解析 graph schema。
   - 调用 `FBlueprintEditorUtils`、`UEdGraphSchema_K2`、`AllocateDefaultPins`、`ReconstructNode`、compile/save lifecycle。
   - 拥有 engine-specific repair hooks；它不是 node inventory。

4. `NodeAdapterRegistry`
   - 通过 `Class` path 动态解析 node class，优先使用 `StaticLoadClass` 或已有 `ClassFinderUtils`。
   - 只有 reflection 本身无法完成 UE lifecycle operation 时，才选择薄 adapter。
   - adapter 可以处理绑定 `UFunction` 到 `UK2Node_CallFunction`、设置 event reference、reconcile timeline templates 等 lifecycle 操作。
   - adapter 不得枚举具体 function names、variable names、component names 或 project-specific classes。

硬编码分支策略：

- 允许：当 UE 需要 class-specific lifecycle call 时，按 UE node class 或 reflected capability 选择小型 adapter。
- 禁止：对具体 function names、property names、project class names 或全量 `K2Node_*` include 列表写 `switch` / 大型 `if` 链。
- 要求：只要 UE 暴露了足够 metadata，就使用 reflection 处理 `UFunction` / `FProperty` / `FEdGraphPinType` / component property resolution。

---

## 4. 文档形状

Graph regions 共享同一个 `GraphSpec` 形状。最小 EventGraph 示例：

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
            "From": {
              "Node": "BeginPlay",
              "Pin": "then"
            },
            "To": {
              "Node": "Print",
              "Pin": "execute"
            }
          }
        ]
      }
    ]
  }
}
```

规则：

- `Definitions` 是 Godot-style resource table 的 JSON 等价物。它支持 reusable refs 和 fragments，但不拥有 UE package lifecycle。
- Graph topology 保持 inline，放在 `Body.*Graphs` 下。Nodes、link endpoints 和 graph ordering 必须便于 agent 编辑。
- Nodes 默认不移入 `Definitions`。只有大型、共享或复用 payload 才应成为 definitions。
- `Timelines` 不直接使用 `GraphSpec`，但 graph region 中的 timeline nodes 可以通过 timeline `Name` 或 `DefinitionRef` 引用 timeline specs。

---

## 5. Graph Regions 的 Definitions

Graph regions 可以通过以下形状引用顶层 `Definitions`：

```json
{
  "Kind": "DefinitionRef",
  "Id": "Func.KismetSystemLibrary.PrintString"
}
```

初始 graph-relevant definition kinds：

- `ClassRef`
  - `Class`：UE class path。
- `AssetRef`
  - `Path`：asset path。
  - `Class`：可选的 expected asset class path。
- `MemberRef`
  - `OwnerClass`：class path 或 `"Self"`。
  - `Name`：reflected function/property/member name。
  - `Guid`：可选 extracted evidence。
  - `SelfContext`：可选 boolean。
- `PinType`
  - 使用与 `Body.Variables` 相同的 `FEdGraphPinType` JSON shape。
- `Literal`
  - typed literal payload，用于已经证明可 roundtrip 的复杂 defaults。
- `TimelineCurve`
  - reusable timeline tracks 的 canonical curve key payload。

校验规则：

- 未知 definition kind 失败，诊断码为 `UnknownDefinitionKind`。
- 未使用 definitions 只有在顶层 AssetDocument policy 允许 reusable fragments 时才允许；否则 diff 可以报告为 extra。
- 循环 `DefinitionRef` 链失败，诊断码为 `CircularDefinitionReference`。
- graph node 可以 inline 一个小型 ref object，也可以使用 `DefinitionRef`；两者 canonicalize 后得到同一个 resolved semantic value。

规范化与 diff 规则：

- Graph semantic comparison 在比较 nodes、pins、links、signatures 和 timelines 前，先 resolve `DefinitionRef`。
- Inline refs 与等价 `DefinitionRef` 在 managed graph regions 内比较为相等。
- Extract 不得 opportunistically hoist 简单 graph refs 到 `Definitions`。第一版 graph 实现中，extract 输出 inline `ClassRef`、`MemberRef`、`AssetRef` 和 `PinType`，除非源 sidecar 已存在且 sync 操作明确要求保留作者风格。
- 未来 extractor 只有在本 spec 为该 definition kind 定义 deterministic id rule 后，才能 hoist 大型 reusable payload。例如 timeline curves 可使用 `TimelineCurve.<TimelineName>.<TrackName>`。
- `/Definitions/<DefinitionId>` diff path 只用于 definition table 作者问题：duplicate/invalid definitions、policy 不允许时的 unused top-level fragments，或明确作为 definition 管理的 reusable definitions 发生变化。
- 如果目标 sidecar 使用 `DefinitionRef`，而当前 asset extract 得到 inline ref，不得仅因为缺少 `/Definitions/<DefinitionId>` 报 missing；graph semantic diff 比较 resolved value。
- Definition ids 是 authoring ids。它们必须在 `Definitions` 内稳定、唯一，并符合 `^[A-Za-z_][A-Za-z0-9_.:-]*$`。

---

## 6. Canonical GraphSpec

### 6.1 Graph Fields

必填字段：

- `Name`：graph name。
- `Schema`：graph schema class path。第一实现仅支持 `/Script/BlueprintGraph.EdGraphSchema_K2`，但必须动态解析。
- `Nodes`：`NodeSpec` array。
- `Links`：`LinkSpec` array。

可选字段：

- `GraphGuid`：extracted graph GUID evidence。Apply 不得依赖它作为 identity。
- `Category`：function/macro authoring category，仅在 UE 暴露稳定可编辑字段时使用。
- `Description`：graph description，仅在 UE 暴露稳定可编辑字段时使用。
- `Signature`：function/macro signature extension；见第 9 节和第 10 节。

Identity 规则：

- `UbergraphPages`：`Name`
- `FunctionGraphs`：`Name`
- `MacroGraphs`：`Name`

不支持字段失败，诊断码为 `UnknownGraphField`。

### 6.2 NodeSpec

Canonical node 形状：

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

规则：

- `Id` 是一个 graph 内的 sidecar identity。它必须唯一、稳定，并适合人工编辑。
- `Id` 必须符合 `^[A-Za-z_][A-Za-z0-9_-]*$`，以保证 link addressing、diagnostics 和 agent edits 不产生歧义。
- `Class` 是主要 UE node identity。它动态解析，并驱动 adapter lookup。
- `Capability` 是可选 semantic alias，仅用于 diagnostics/templates。存在 `Class` 时，它不得成为独立行为来源。
- Apply/extract dispatch 必须由 resolved `Class` 和 registered adapter capability 决定。省略或修改 `Capability` 不得改变行为，除非触发 validation diagnostics。
- `NodeGuid` 是 UE identity evidence。authored input 可省略。Extract 在 UE 提供稳定值时包含它。Apply 使用 `Id` 作为 sidecar identity，并可在安全时保留 `NodeGuid`。
- `Member` 仅在 node class 对应 adapter 声明需要 reflected member 时必填。
- `PinOverrides` 是 sparse 的。它只包含 authored pin defaults、dynamic pin declarations，或无法从 node class/member reflection 重建的 pin metadata。
- `Position` 是 authoring layout metadata。graph readability 是共享项目状态，所以需要包含它。
- `Comment` 是 authoring metadata。空字符串和字段缺失都表示无 comment。

`GraphCore` validation 不得要求硬编码 node class 列表。它只校验形状，并询问 `NodeAdapterRegistry`：resolved node class 是否在当前 apply/extract tier 中受支持。不支持 node class 失败，诊断码为 `UnsupportedGraphNodeClass`。

### 6.3 PinOverrideSpec

Pin overrides 必须保持 sparse：

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

规则：

- `Pin` 是一个 node 内的 link address。普通 pins 应等于 UE `PinName`。当 UE display name 不唯一时，dynamic pins 可以使用稳定 sidecar id。
- `Pin` 必须符合 `^[A-Za-z_][A-Za-z0-9_-]*$`。如果 UE pin name 不满足该规则，adapter 必须映射到稳定 sidecar pin id，并在 adapter-owned metadata 中保留 UE display/name evidence。
- 对普通 reflected pins，`Direction` 可省略，因为它可从 allocated UE pin 重建。对 dynamic pins，`Direction` 必填。
- `Type` 使用 `FEdGraphPinType` 形状，或使用指向 `PinType` 的 `DefinitionRef`。
- Default fields 仅对未连接 input pins 有权威含义。
- 缺失 default fields 表示 reset 到 node/pin baseline。
- Linked input pins 在 UE 中仍可能携带 default values，但 diff 以 link state 作为权威行为。UE 为 compile stability 需要时，apply 可以清理无关 defaults。
- Output pin defaults 默认拒绝，除非 node adapter 明确声明某个 output default 可编辑且可 roundtrip。
- 不得表示 transient UE pin fields、compiler state、cache flags 或 editor-only expansion state。

Extract 规则：

- Extract 仅在 pin 有 authored non-baseline default、dynamic pin metadata，或不可重建 authoring metadata 时输出 pin overrides。
- Link endpoints 可以引用不在 `PinOverrides` 中出现的 pins；apply 会先通过 node allocation 重建 pins，再创建 links。

### 6.4 LinkSpec

Canonical link 形状：

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

Compact form 只作为 input sugar 接受，且 node/pin ids 必须满足对应 regex：

```json
{
  "From": "BeginPlay.then",
  "To": "Print.execute"
}
```

规则：

- Links 从 output pin 指向 input pin。
- 两端 pins 必须在 node allocation 和 pin reconstruction 后可解析。
- Serializer 和 extract output 必须使用 expanded object shape。
- compact input 如果 endpoint syntax 有歧义或格式错误，失败码为 `InvalidGraphLinkEndpointSyntax`。
- duplicate links 失败，诊断码为 `DuplicateGraphLink`。
- 只要 UE schema 可以校验，type-incompatible links 必须在 mutation 前拒绝。
- 缺失 link 表示删除该 connection。

### 6.5 MemberRef

Canonical member reference 形状：

```json
{
  "Kind": "MemberRef",
  "OwnerClass": "/Script/Engine.Actor",
  "Name": "ReceiveBeginPlay"
}
```

可选字段：

- `Guid`：UE member GUID extracted evidence。
- `SelfContext`：用于明确 self-scoped member references 的 boolean。

规则：

- Function/event/member identity 优先使用 explicit owner class 加 name。
- `Guid` 是 supporting evidence，不是唯一 identity。
- `OwnerClass: "Self"` 解析到 staged `ParentClass`、`Body.Variables`、`Body.Components` 和 generated class evidence。
- get/set nodes 引用的 Blueprint variables 必须存在于 `Body.Variables`，或存在于 parent-class reflected properties。
- get/set nodes 引用的 component variables 必须存在于 `Body.Components`，或 parent/native component evidence。
- Function refs 通过 `UClass::FindFunctionByName` 或等价 reflection 解析；禁止 concrete function whitelist。
- Property refs 通过 `FProperty` reflection 和 Blueprint variable metadata 解析；禁止 concrete variable whitelist。

---

## 7. Reflection-First Node Capability Model

完整 `GraphSpec` 设计上可覆盖任意 K2 graphs，但实现按支持层级落地。Tiers 表达当前 adapter 覆盖范围，不是硬编码 semantic universe。

### 7.1 Adapter Contract

每个 node adapter 声明：

- 支持的 UE node class 或 reflected base capability。
- 必需 refs：none、`MemberRef`、`TimelineRef` 或 graph signature。
- 如何使用 dynamic class resolution 创建 node。
- 如何绑定 reflected function/property/event metadata。
- 如何让 UE allocate/reconstruct pins。
- 接受哪些 pin overrides。
- 如何 extract canonical sparse `NodeSpec`。

adapter code 可以 include 当前 task 真正操作的 node class 所需的最小 UE headers。不得 include 所有可能的 `K2Node_*` headers inventory。

### 7.2 Tier 1：EventGraph 基础节点

第一实现应通过以下 adapter capabilities 支持 `Body.UbergraphPages`：

- Event node adapter
  - 初始 adapter 覆盖范围：`/Script/BlueprintGraph.K2Node_Event`
  - Required `MemberRef`。
  - Event function 按 owner class 加 function name 解析。
  - 初始 smoke events 可以包含 `Actor.ReceiveBeginPlay` 和 `Actor.ReceiveTick`，但只要 UE reflection 可以安全解析 event，实现不得受限于硬编码 event-name whitelist。
- Call function adapter
  - 初始 adapter 覆盖范围：`/Script/BlueprintGraph.K2Node_CallFunction`
  - Required `MemberRef`。
  - 支持通过 reflected `UFunction` 解析的普通 callable functions。
  - 绑定 `UFunction` 后，由 UE allocation 产生 function-specific pin 形状。
- Variable get/set adapters
  - 初始 adapter 覆盖范围：`/Script/BlueprintGraph.K2Node_VariableGet`、`/Script/BlueprintGraph.K2Node_VariableSet`
  - Required `MemberRef`。
  - Variable/property 从 Blueprint variables、parent class `FProperty`、component vars 或 staged component evidence 解析。
- Self adapter
  - 初始 adapter 覆盖范围：`/Script/BlueprintGraph.K2Node_Self`
  - 不需要 member ref。

Tier 1 extract 可以把 unsupported existing node classes 报到 `_Skipped.Graphs` evidence。对 managed graph 执行 apply 时，如果 graph 内出现 unsupported node classes，必须失败；除非这些 nodes 已经因为 sidecar 有意删除而不存在于目标 graph 中。

这些 class paths 是第一实现 tier 的 registry 覆盖范围。`GraphCore` 和 graph parsers 不得依赖该列表。

### 7.3 Capability Boundary

扩展支持时应新增 adapter capability，而不是新增 branchy graph logic。例如：

- 添加 `K2Node_Branch` 时，应新增一个小型 adapter，用 reflected/dynamic node class 和已知 pin reconstruction 处理，而不是在 `GraphCore` 特判所有 branch link path。
- 添加 custom events 可能需要 `CustomEvent` adapter，因为 event creation 有 UE lifecycle semantics；但它仍然不得使用 project-specific event name inventories。
- 添加 array/map/make struct nodes 时，应优先使用 reflected pin allocation 和 typed default fragments。

---

## 8. Region Semantics

### 8.1 `Body.UbergraphPages`

- 缺失 `UbergraphPages` 表示 event graph set 为空，UE-required baseline default graph 除外。
- 缺失 graph page 表示在 UE 允许时移除该 graph page。
- 默认 `EventGraph` 是特殊情况：
  - 如果 sidecar 省略所有 event graphs，apply 应移除默认 graph 中的 user-authored nodes，但不一定删除 UE-required graph object。
  - Extract 应只在 canonical baseline 需要时输出空 `EventGraph`；否则无 user-authored graph data 时输出空数组。implementation plan 必须选择一个 canonical 行为并测试。
- 缺失 node 表示删除该 node。
- 缺失 link 表示删除该 link。
- 缺失 pin default 表示 reset 到 node/pin baseline。

### 8.2 `Body.FunctionGraphs`

- 缺失 user function graph 表示删除它。
- `Signature` 对 user-created function graphs 是权威的。
- interface 仍实现时，不得静默删除 required interface function graph。
- 通过 `Body.ImplementedInterfaces` 移除 interface 时，应移除不再需要的 interface stubs，除非它们同时以 distinct identity 表示为 user-authored functions。

### 8.3 `Body.MacroGraphs`

- 缺失 macro graph 表示删除它。
- `Signature` 对 macro tunnel pins 是权威的。
- signature 中缺失 tunnel pin 表示如果 UE 允许则移除；否则 mutation 前失败，诊断码为 `InvalidMacroSignature`。

### 8.4 `Body.Timelines`

- 缺失 timeline 表示删除它。
- 引用已删除 timeline 的现有 graph nodes 必须同时删除，或在 mutation 前拒绝。implementation plan 必须按 step 选择行为：
  - 第一 timeline step 可以在 graph references 存在时拒绝 deletion，作为实现阶段限制；同时必须有 deferred-fields entry，并返回 `UnresolvedTimelineReference` 或更窄 diagnostic。
  - 完整 timeline step 应在同一个 staged apply 中 reconcile graph nodes。

---

## 9. FunctionGraphs

Function graph spec 扩展 `GraphSpec`：

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

额外需要的 adapters：

- Function entry adapter。
- Function result adapter。

规则：

- Function entry/result nodes 在 apply 后必须匹配 `Signature`。
- interface-required function stubs 由 `Body.ImplementedInterfaces` 和 `Body.FunctionGraphs` 共同管理。
- interface signature compatibility 通过 reflected interface `UFunction` metadata 检查，不使用硬编码 interface list。
- sidecar 缺失的 user-created functions 应删除。

---

## 10. MacroGraphs

Macro graph spec 扩展 `GraphSpec`：

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

额外需要的 adapter：

- 用于 macro entry 和 exit tunnel nodes 的 tunnel node adapter。

规则：

- Macro tunnel nodes 是 canonical graph representation 的一部分。
- Macro `Signature` 是权威的，并且必须匹配 tunnel pins。
- 第一版 macro 实现拒绝 wildcard pins，除非 node adapter 可以确定性 roundtrip。

---

## 11. Timelines

Timeline spec：

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

规则：

- Timeline `Name` 是 identity。
- 缺失 timeline 表示删除 `UTimelineTemplate` 并 reconcile graph timeline nodes。
- Timeline graph nodes 必须引用 `Body.Timelines[*].Name` 或 timeline `DefinitionRef`。
- Timeline-generated variables 是 derived implementation details，不得在 `Body.Variables` 中独立 author。
- Timeline track curve representation 必须使用 canonical key data，或使用指向 `TimelineCurve` 的 `DefinitionRef`。
- External curve asset refs 是未来扩展；添加时必须使用 `AssetRef`。

---

## 12. Apply Pipeline

Graph/timeline apply 必须保持 staged：

1. 解析所有 graph/timeline regions 和 graph-relevant `Definitions`。
2. 校验 graph region arrays、duplicate identities、node classes、pin overrides、member refs、links、timeline refs。
3. 解析 `DefinitionRef` chains，并将 cross-region references 对齐到 staged `ParentClass`、`Variables`、`Components`、`ImplementedInterfaces`、`ClassDefaults`、graph specs 和 timeline specs。
4. 动态解析 graph schema 和 node classes；mutation 前拒绝 missing/unsupported classes。
5. 向 `NodeAdapterRegistry` 查询 resolved node classes 所需 adapters。缺失 adapter 失败，诊断码为 `UnsupportedGraphNodeClass`。
6. preflight parent class changes，确认不会破坏 reflected graph refs。
7. 在所有 preflight 成功后才 mutation，或 snapshot 足够的 previous Blueprint graph state 用于失败 rollback。
8. 按以下顺序 apply dependency regions：
   - `ParentClass`
   - `ImplementedInterfaces`
   - `Variables`
   - `Components`
   - `ClassDefaults`
   - `Timelines`
   - `UbergraphPages`
   - `FunctionGraphs`
   - `MacroGraphs`
9. 对每个 graph：
   - 通过 `K2GraphAdapter` create/find graph。
   - 按 sidecar `Id` create/find nodes。
   - 通过 node adapters 绑定 reflected refs。
   - 调用 UE pin allocation/reconstruction。
   - apply sparse pin overrides。
   - 通过 graph schema validation 创建 links。
   - 删除 omitted nodes/links。
10. Compile Blueprint。
11. 如果 compile 失败，尽可能 rollback staged mutations，并返回带 graph path diagnostics 的 `BlueprintCompileFailed`。
12. 仅在 compile 成功且请求 `bSaveAsset` 时保存。

任何 step 都不得把 partially applied graph changes 保存到磁盘。

---

## 13. Extract

Extract 产出 canonical graph specs：

- Graph arrays 按 UE graph array order 排序。
- Nodes 按 UE node order 排序；当 UE order 变化时，用 `NodeGuid` fallback 保持 deterministic output。
- 仅对 non-baseline authored defaults、dynamic pins，或不可重建 authoring metadata 输出 pin overrides。
- Links 按 source node id、source pin id、target node id、target pin id 排序。
- Extract 必须遵守第 5 节的 definition canonical rules。不得 opportunistically hoist simple refs 到 `Definitions`。
- 受支持 node classes 通过 adapters 完整 extract。
- Unsupported existing node classes 报到 `_Skipped.Graphs`，包含 count 和 node class names。一旦某 graph region 在当前 tier 被标记为 fully managed，unsupported nodes 应让 extract 报告 incomplete evidence，而不是输出看似可 roundtrip 的 lossy graph。

Extract 不得包含 editor-only graph zoom/pan/selection/tab state、compiler intermediates、transient pin flags 或 cache fields。

---

## 14. Diff

Diff 使用与 apply 相同的 parser 解析目标 graph regions，extract 当前 evidence，resolve definitions，然后比较 canonical semantic objects。

必需 diff paths：

- `/Definitions/<DefinitionId>`
- `/Body/UbergraphPages/<GraphName>`
- `/Body/UbergraphPages/<GraphName>/Nodes/<NodeId>`
- `/Body/UbergraphPages/<GraphName>/Nodes/<NodeId>/PinOverrides/<PinId>`
- `/Body/UbergraphPages/<GraphName>/Links/<FromNode>:<FromPin>-><ToNode>:<ToPin>`
- `/Body/FunctionGraphs/<GraphName>`
- `/Body/MacroGraphs/<GraphName>`
- `/Body/Timelines/<TimelineName>`

Diff 状态：

- `unchanged`：当前值和目标 canonical values 相等。
- `changed`：两侧都存在但值不同。
- `missing`：目标存在，当前不存在。
- `extra`：当前存在，目标不存在。
- `unsupported`：current 无法由已实现 graph capability 表达。

缺失 supported graph region fields 表示 empty authoritative state，与 `UBlueprint` Body 其他语义一致。

Path tokens 必须使用 JSON Pointer escaping。link path token 只用于 diagnostic display；expanded `LinkSpec` object 才是解析时的权威数据。

---

## 15. Validation Diagnostics

必需 diagnostic codes：

- `InvalidGraphRegionType`：graph region 不是 array。
- `DuplicateGraphName`：同一 region 内 graph identity 重复。
- `InvalidGraphSchema`：graph schema 不支持或无法解析。
- `UnknownGraphField`：graph spec 包含 unknown field。
- `UnknownDefinitionKind`：definition kind 不支持。
- `CircularDefinitionReference`：definition refs 形成环。
- `UnresolvedDefinitionReference`：definition ref target 不存在。
- `DuplicateGraphNodeId`：同一 graph 内 node identity 重复。
- `InvalidGraphNodeId`：node id 不符合 sidecar id regex。
- `UnresolvedGraphNodeClass`：node `Class` 无法加载。
- `UnsupportedGraphNodeClass`：resolved node class 在当前 tier 中没有 adapter。
- `InvalidGraphNodeCapability`：可选 `Capability` 与 resolved node adapter 冲突。
- `MissingGraphMemberReference`：缺失必需 member ref。
- `UnresolvedGraphMemberReference`：member ref 无法通过 reflection 或 staged Blueprint regions 解析。
- `InvalidGraphPin`：pin shape 或 pin direction 无效。
- `InvalidGraphPinId`：pin id 不符合 sidecar id regex。
- `InvalidGraphPinDefault`：authored pin default 无法 apply。
- `DuplicateGraphLink`：link 重复。
- `InvalidGraphLinkEndpointSyntax`：compact link syntax 有歧义或格式错误。
- `UnresolvedGraphLinkEndpoint`：node reconstruction 后 link node/pin endpoint 不存在。
- `InvalidGraphLinkType`：UE graph schema 拒绝该 link。
- `InvalidFunctionSignature`：function signature 与 graph entry/result nodes 冲突。
- `InvalidMacroSignature`：macro signature 与 tunnel nodes 冲突。
- `UnresolvedTimelineReference`：graph node 引用缺失 timeline。
- `UnsupportedTimelineTrackKind`：timeline track kind 未实现。
- `BlueprintCompileFailed`：apply 后 graph 编译为无效 Blueprint。

Diagnostics 应指向尽可能窄的 JSON path。

---

## 16. Implementation Steps

本 spec 通过 checkpoint tasks 实现，不作为一个大 patch 落地。

### Step 1: GraphCore Data Model And Validation

- 添加 `GraphSpec`、`NodeSpec`、`PinOverrideSpec`、`LinkSpec`、`MemberRef` 和 graph-relevant `DefinitionRef` parser/serializer helpers。
- 添加 `NodeAdapterRegistry` interface，不引入 broad K2 inventory。
- 对非空 graph regions 继续保持 apply rejection；只允许 validation tests exercise parser failure modes。
- 添加 automation tests：duplicate graph names、duplicate node ids、unresolved definitions、unresolved links、unsupported node classes、invalid schema。

### Step 2: Reflection-First Tier 1 Extract/Diff

- 对 Tier 1 adapter classes extract `Body.UbergraphPages`。
- 通过 reflection 解析 event/function/property refs。
- 仅输出 sparse pin overrides。
- diff supported EventGraph nodes 和 links。
- existing unsupported nodes 产出 `_Skipped.Graphs` 和 `unsupported` diff entries。

### Step 3: EventGraph Apply

- 对 Tier 1 adapter classes apply `Body.UbergraphPages`。
- 权威 rebuild missing/extra nodes 和 links。
- compile 并验证真实 `BeginPlay -> PrintString` 或等价 smoke graph；不得把 `PrintString` 硬编码为特殊函数。

### Step 4: Definitions Reuse Hardening

- 支持 `MemberRef`、`ClassRef`、`AssetRef`、`PinType` 和简单 `Literal` 的 `DefinitionRef`。
- 添加 canonicalization tests，证明 inline refs 与 definition refs 比较相等。
- 添加 cycle detection tests。

### Step 5: FunctionGraphs

- 添加 `Signature` 支持。
- apply/extract/diff user-created function graphs。
- 支持由 `Body.ImplementedInterfaces` 和 `Body.FunctionGraphs` 共同控制的 interface-required function stubs。
- 通过 reflected interface metadata 校验 interface signatures。

### Step 6: MacroGraphs

- 添加 macro `Signature` 和 tunnel node 支持。
- 对包含 supported internal Tier 1 nodes 的 user-created macros 执行 apply/extract/diff。

### Step 7: Timelines

- 添加 `Body.Timelines` parser/serializer。
- 第一版支持 float tracks。
- 在有价值时支持 `TimelineCurve` definition refs。
- reconcile graph regions 中的 timeline node references。
- compile 并 smoke 一个 timeline Blueprint。

### Step 8: Final Graph Roundtrip Smoke

- 对 `C:/AVH1` 运行 UBT。
- 运行 focused automation：`AssetFactory.AssetDocument.UBlueprint`。
- 运行完整 `AssetFactory.AssetDocument` automation。
- 使用 `C:/AVH1/Content/AssetDocumentSmoke/` 下真实 graph sidecar 运行 external HTTP apply-file/extract/diff smoke。
- 更新 deferred-fields doc，移除已完成 graph/timeline entries 或缩窄剩余限制。
- 更新 final report。

---

## 17. Verification Requirements

最低 automation 覆盖：

- Validate：
  - empty graph regions 仍然通过。
  - duplicate graph/node/link identity 失败。
  - unresolved/circular `DefinitionRef` 失败。
  - unsupported node class apply 失败。
  - unresolved member ref/link/timeline ref 在 mutation 前失败。
- Apply：
  - 创建包含 supported nodes 和 links 的 EventGraph。
  - 更新 existing graph，并删除 omitted nodes 和 links。
  - apply failure 后 previous graph state 仍可 load。
  - function graph create/update/delete。
  - macro graph create/update/delete，随对应 step 落地。
  - timeline create/update/delete，随对应 step 落地。
- Extract：
  - supported graph 可 roundtrip 到 canonical sidecar。
  - baseline pins 的 extract 保持 sparse。
  - unsupported nodes 作为 skipped evidence 暴露。
- Diff：
  - unchanged graph 报 unchanged。
  - inline ref 与等价 `DefinitionRef` 比较为 unchanged。
  - omitted graph/node/link 报 extra。
  - desired graph/node/link 在 asset 中不存在时报 missing。
- Architecture：
  - `GraphCore` tests 不依赖具体 `K2Node_*` subclasses。
  - 添加新 node class 只需要 adapter registration，不需要修改 graph parser/diff core。
- Smoke：
  - 对一个包含 graph content 的 Blueprint 运行 external HTTP apply-file/extract/diff。

---

## 18. Open Risks

- UE 可能在 compile/reconstruction 时重新生成 node GUIDs 或 pins；sidecar identity 不得只依赖 `NodeGuid`。
- 某些 K2 nodes 会基于 member refs 或 default values 动态分配 pins；实现必须在 apply links 前先 reconstruct pins。
- Reflection 可能足以校验 function/property，但不足以创建合法 node。这类场景需要薄 adapters，而不是 graph-core branching。
- Interface function stubs 可能由 UE/interface logic 管理，而不只是 user graph arrays。function graph step 必须保留 required stubs，同时仍删除 omitted user-created functions。
- Timeline templates 会产生 generated variables 和 graph node references。Timeline apply 必须避免把 derived timeline variables 当作 user-authored `Body.Variables`。
- Unsupported node extraction 必须诚实。报告 unsupported evidence 好过输出看似可 roundtrip 的 lossy graph。
- 过度把每个 node/pin 都放入 `Definitions` 会伤害 agent editing。Definitions 只用于 reusable refs/fragments，不是 graph topology 的默认存储位置。

---

## 19. Success Criteria

Graph regions 被视为完成时应满足：

- `Body.UbergraphPages`、`Body.FunctionGraphs`、`Body.MacroGraphs` 和 `Body.Timelines` 都有稳定 schema docs 和 tests。
- `GraphCore` 保持薄：没有 concrete function/property/project inventories，没有 raw UE graph dump fields，也没有 node-specific behavior in parser/diff core。
- supported graph/timeline content 可以通过 `C:/AVH1` 中真实 `UBlueprint` apply-file roundtrip。
- omitted graph/timeline sidecar content 会根据 authoritative Body semantics 删除或 reset 当前 asset state。
- unsupported graph/timeline content 会以清晰 diagnostics 失败，或在 extract/diff 中报告为 skipped evidence。
- `Definitions` 可以用于 reusable graph refs/fragments，同时不把普通 graph topology 移出 `Body.*Graphs`。
- 不引入 `BlueprintGenerator` 或 Blueprint-specific MCP tool。
