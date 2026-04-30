# Animation Blueprint 语言架构设计

**日期：** 2026-04-30
**状态：** 供用户审阅的草稿
**分支：** `feature/animation-generators-specs`
**依赖：** Animation generator spec map

---

## 目标

设计 Animation Blueprint 图生成的创作契约，使 agent 能表达 AnimGraph、StateMachine 和普通 Blueprint 逻辑，而不需要编写底层 UE 图 JSON。

选定的架构如下：

- 顶层 AssetFactory JSON 负责资产元数据和编排；
- `AnimGraphDSL` 负责姿势图源码；
- `AnimStateMachineDSL` 负责状态机源码；
- `BSLFragment` 负责普通 Blueprint/EventGraph/function 逻辑；
- 规范化的内部 IR 用于生成、验证、提取和测试。

## 设计定位

Animation Blueprint 不是单一图模型。它至少包含三种不同语义：

- AnimGraph 中的姿势图语义；
- 状态机和过渡图语义；
- 用于事件和函数的普通 K2 Blueprint 逻辑。

把这些合并成一种面向用户的语言会让语言变得庞大，并且容易让 agent 出错。将它们拆分为源码块可以提供更好的诊断，并让职责归属更清晰：

```json
{
  "AnimGraph": {
    "Language": "AnimGraphDSL",
    "Source": "output = machine Locomotion"
  },
  "StateMachines": [
    {
      "Name": "Locomotion",
      "Language": "AnimStateMachineDSL",
      "Source": "entry Idle\nstate Idle { pose = sequence(\"/Game/Anim/Idle.Idle\") }"
    }
  ],
  "BlueprintGraphs": [
    {
      "Name": "EventGraph",
      "Language": "BSLFragment",
      "Source": "event BlueprintUpdateAnimation(DeltaTimeX: float) { }"
    }
  ]
}
```

这些源码块会先编译为内部 IR。只有通过验证的 IR 才允许修改 UE 图。

## 非目标

本设计不定义每一种高级 animation node 的最终语法。

本设计不替换现有 Blueprint generator 的变量契约。AnimationBlueprint 变量使用与 Blueprint generator 变量相同的形状：

```json
{ "Name": "Speed", "Type": "Float", "DefaultValue": "0.0" }
```

生成流程应接受 `Bool` 和 `Boolean` 作为 Blueprint bool 变量的别名，因为即使创作输入使用较短的 BlueprintGenerator 风格，提取结果也可能报告面向引擎的拼写。

本设计不把 YAML 作为主要契约。YAML 以后可以作为可选前端，但它应编译到相同的规范化 IR，并且不改变 UE 侧 generator。

本设计不把 UE clipboard text 暴露为主要源语言。Clipboard text 对调试导入/导出有用，但它与内部 node object serialization 耦合过深。

## 契约形状

### 顶层 AnimationBlueprint JSON

```json
{
  "AssetType": "AnimationBlueprint",
  "Name": "ABP_Enemy",
  "Path": "/Game/Anim",
  "ParentClass": "AnimInstance",
  "Skeleton": "/Game/Characters/SK_Enemy_Skeleton.SK_Enemy_Skeleton",
  "PreviewMesh": "/Game/Characters/SK_Enemy.SK_Enemy",
  "Variables": [
    { "Name": "Speed", "Type": "Float", "DefaultValue": "0.0" },
    { "Name": "Direction", "Type": "Float", "DefaultValue": "0.0" },
    { "Name": "bIsInAir", "Type": "Bool", "DefaultValue": "false" }
  ],
  "DefaultProperties": {},
  "AnimGraph": {
    "Language": "AnimGraphDSL",
    "Source": "output = machine Locomotion"
  },
  "StateMachines": [
    {
      "Name": "Locomotion",
      "Language": "AnimStateMachineDSL",
      "Source": "entry Idle\nstate Idle { pose = sequence(\"/Game/Anim/Idle.Idle\") }\nstate Move { pose = blendspace(\"/Game/Anim/BS_Locomotion.BS_Locomotion\", Speed, Direction) }\ntransition Idle -> Move when Speed > 5\ntransition Move -> Idle when Speed <= 5"
    }
  ],
  "BlueprintGraphs": [
    {
      "Name": "EventGraph",
      "Language": "BSLFragment",
      "Source": "event BlueprintUpdateAnimation(DeltaTimeX: float) { }"
    }
  ]
}
```

### 规范化 IR

在修改图之前，源码块会规范化为 canonical IR。IR 不要求与用户输入语法完全一致。它应足够稳定，以支持测试和提取。

AnimGraph IR 示例：

```json
{
  "output": { "ref": "Locomotion", "kind": "stateMachine" }
}
```

StateMachine IR 示例：

```json
{
  "name": "Locomotion",
  "entry": "Idle",
  "states": [
    {
      "name": "Idle",
      "pose": {
        "kind": "sequence",
        "asset": "/Game/Anim/Idle.Idle"
      }
    },
    {
      "name": "Move",
      "pose": {
        "kind": "blendspace",
        "asset": "/Game/Anim/BS_Locomotion.BS_Locomotion",
        "inputs": ["Speed", "Direction"]
      }
    }
  ],
  "transitions": [
    {
      "from": "Idle",
      "to": "Move",
      "condition": { "kind": "expression", "source": "Speed > 5" }
    }
  ]
}
```

## 语言边界

### AnimGraphDSL

AnimGraphDSL 负责姿势图组合：

```text
output = machine Locomotion
```

初始表达式：

```text
output = sequence("/Game/Anim/Idle.Idle")
output = blendspace("/Game/Anim/BS_Locomotion.BS_Locomotion", Speed, Direction)
output = machine Locomotion
output = cached_pose LocomotionPose
```

未来表达式：

```text
cached_pose LocomotionPose = machine Locomotion
output = slot "DefaultSlot" source LocomotionPose
output = layered_blend(base LocomotionPose, upper AttackPose, bone "spine_01")
output = raw_node("/Script/AnimGraph.AnimGraphNode_Custom", properties { })
```

AnimGraphDSL 验证：

- 引用的变量存在于顶层 `Variables` 或 parent class 中；
- 引用的 BlendSpace/Animation asset 能加载，并且匹配目标 skeleton；
- 引用的 state machine 存在；
- pose 表达式会生成 pose 输出。

### AnimStateMachineDSL

AnimStateMachineDSL 负责 state 和 transition。顶层 `StateMachines[].Name` 值是权威的机器名来源。未来可选的 DSL header 可以为了可读性重复该名称，但不匹配应作为验证错误。

```text
entry Idle

state Idle {
  pose = sequence("/Game/Anim/Idle.Idle")
}

state Move {
  pose = blendspace("/Game/Anim/BS_Locomotion.BS_Locomotion", Speed, Direction)
}

transition Idle -> Move when Speed > 5
transition Move -> Idle when Speed <= 5
```

未来 transition 选项：

```text
transition Idle -> Move when Speed > 5 {
  blend = 0.2
  priority = 1
}
```

StateMachineDSL 验证：

- state name 唯一；
- 除非显式允许默认值，否则必须恰好有一个 entry state；
- transition endpoint 存在；
- condition variable 必须存在；helper function 可以在 `BSLFragment` integration 满足它们之前保持未解析；
- pose 表达式可以通过 AnimGraph pose expression parser 编译。

### BlueprintGraphs 的 BSLFragment

`BSLFragment` 负责普通 Blueprint 逻辑。它是 `BlueprintGraphs[]` 的 fragment contract；当前 BSL parser 期望完整的 `blueprint ... extends ... { ... }` wrapper，因此 Animation Blueprint integration layer 必须在调用 BSL infrastructure 之前包装并路由 fragment。Animation-specific integration 应允许：

- `EventGraph`
- `BlueprintUpdateAnimation`
- transition rule 可以调用的 helper function
- 赋值给顶层 JSON 中声明的 Blueprint variable

示例：

```text
event BlueprintUpdateAnimation(DeltaTimeX: float) {
  // 集成后，BSLFragment 语法应遵循现有 BSL 的 event/function body 契约。
}
```

传给当前 parser 的完整 wrapper 可理解为：

```text
blueprint ABP_Enemy extends AnimInstance {
  event BlueprintUpdateAnimation(DeltaTimeX: float) { }
}
```

generator 不应使用 BSLFragment 描述 pose link。Pose link 属于 AnimGraphDSL 和 StateMachineDSL。

## 错误模型

每条诊断都应包含源码块：

- `AnimGraph.Source: line 1, column 10: unknown state machine 'Locomotion'`
- `StateMachines[0].Source: line 5, column 18: unknown variable 'Velocity'`
- `BlueprintGraphs[0].Source: line 2, column 3: BSL parser error: ...`

当解析或语义验证失败时，生成流程应在修改 UE 图之前失败。

当可以识别失败图时，UE compile error 应附带 asset type 和源码块。

## UE 图 Builder 策略

Builder 使用 canonical IR，而不是原始 source string。

AnimGraph builder 应：

- 查找或创建官方 AnimGraph；
- 保留默认 graph lifecycle 行为；
- 尽可能使用 `UAnimationGraphSchema` 和 animation node action；
- 将受支持的 pose node 连接到 `UAnimGraphNode_Root`；
- 避免把 pin-level JSON 作为用户契约。

StateMachine builder 应：

- 通过 UE graph/node lifecycle API 生成 state machine node；
- 让 state machine node 创建自己的 child graph；
- 通过 schema action 或 lifecycle API 创建 state node 和 transition node；
- 根据已验证的 condition IR 构建 transition rule graph。

BSLFragment integration layer 应：

- 将 BSLFragment 路由到请求的 EventGraph 或 function graph；
- 复用现有 BSL parser/compiler 概念；
- 为 `UAnimBlueprint` 适配 graph lookup 和 node placement。

## 提取

提取应优先使用 canonical IR，而不是重建 source。

初始提取可以返回：

```json
{
  "AnimGraph": {
    "Language": "CanonicalAnimGraph",
    "Graph": {}
  },
  "StateMachines": [
    {
      "Name": "Locomotion",
      "Language": "CanonicalAnimStateMachine",
      "Graph": {}
    }
  ]
}
```

未来，当受支持的图形状足够简单时，提取还可以额外输出生成的 DSL。第一批 graph spec 不要求完整保留源文本的 round-trip。

## 推荐的第一轮实现顺序

1. 先实现 `BlendSpace` 和 `AimOffset` generator。
2. 实现不带复杂 graph source 的 `AnimationBlueprint` lifecycle。
3. 实现规范化的最小 AnimGraph IR 和 graph builder。
4. 添加 `AnimGraphDSL` parser，作为该 IR 的 frontend。
5. 添加 `AnimStateMachineDSL` 和 transition rule。
6. 为 `BlueprintGraphs` 集成 BSLFragment。

这个顺序能让每一层在语言增长之前都可测试。
