# Animation Blueprint 语言架构设计

**日期：** 2026-04-30
**状态：** 供用户审阅的草稿
**分支：** `feature/animation-generators-specs`
**依赖：** Animation generator spec map

---

## 2026-06-12 修订说明

本草稿中的 `AnimGraphDSL`、`AnimStateMachineDSL` 和 `BSLFragment` 路线已被 AssetDocument GraphIR 方向取代。新的长期方向是：Agent-facing 输入直接使用 AssetDocument `Body.Graphs` / typed JSON GraphIR；不再为下一阶段图资产新增自定义源码语言、源码块前端或“源码 -> JSON IR -> UE 图”的主路径。

本文保留的 DSL 章节只作为历史设计背景阅读。新的实现规格应以 `2026-06-11-generic-asset-document-design.md` 和 `2026-06-12-asset-document-structured-capabilities-animmontage-design.md` 的 AssetDocument/Profile/Body/GraphIR 约定为准。

## 目标

设计 Animation Blueprint 图生成的创作契约，使 agent 能通过 typed JSON GraphIR 表达 AnimGraph、StateMachine 和普通 Blueprint/EventGraph 逻辑，而不需要编写底层 UE serializer JSON 或自定义源码语言。

选定的架构如下：

- AssetDocument 顶层 `Class`、`Target`、`Properties` 负责资产元数据、默认值和反射属性；
- `Body.Graphs.AnimGraph` 负责姿势图 typed JSON GraphIR；
- `Body.Graphs.StateMachines` 负责状态机 typed JSON GraphIR；
- `Body.Graphs.BlueprintGraphs` 负责普通 Blueprint/EventGraph/function typed JSON GraphIR；
- 这份 JSON GraphIR 同时是 Agent-facing canonical format 和 compiler validation/materialization input。

## 设计定位

Animation Blueprint 不是单一图模型。它至少包含三种不同语义：

- AnimGraph 中的姿势图语义；
- 状态机和过渡图语义；
- 用于事件和函数的普通 K2 Blueprint 逻辑。

把这些合并成一种源码语言会让语言变得庞大，并且容易让 agent 出错。新的契约把它们拆成 profile 暴露的 JSON graph sections；每个 section 使用稳定 ID、显式 pin、显式 edge 和 JSON Pointer diagnostics：

```json
{
  "Body": {
    "Graphs": {
      "AnimGraph": {
        "Nodes": {
          "Locomotion": {
            "Kind": "StateMachineRef",
            "StateMachine": "Locomotion"
          }
        },
        "Outputs": {
          "Pose": {
            "From": { "Node": "Locomotion", "Pin": "Pose" }
          }
        }
      },
      "StateMachines": {
        "Locomotion": {
          "Entry": "Idle",
          "States": {
            "Idle": {
              "Pose": {
                "Kind": "SequencePlayer",
                "Animation": "/Game/Anim/Idle.Idle"
              }
            }
          },
          "Transitions": []
        }
      },
      "BlueprintGraphs": {
        "EventGraph": {
          "Nodes": {},
          "Edges": []
        }
      }
    }
  }
}
```

这些 graph sections 会先被 profile schema validate 和 normalize。只有通过验证的 GraphIR 才允许修改 UE 图。

## 非目标

本设计不定义每一种高级 animation node 的最终语法。

本设计不替换现有 Blueprint generator 的变量契约。AnimationBlueprint 变量使用与 Blueprint generator 变量相同的形状：

```json
{ "Name": "Speed", "Type": "Float", "DefaultValue": "0.0" }
```

生成流程应接受 `Bool` 和 `Boolean` 作为 Blueprint bool 变量的别名，因为即使创作输入使用较短的 BlueprintGenerator 风格，提取结果也可能报告面向引擎的拼写。

本设计不把 YAML、BSL fragment、AnimGraphDSL 或 StateMachineDSL 作为主要契约，也不把它们列为下一阶段可选前端。新的 Agent-facing 契约只有 AssetDocument JSON GraphIR。

本设计不把 UE clipboard text 暴露为主要源语言。Clipboard text 对调试导入/导出有用，但它与内部 node object serialization 耦合过深。

## 契约形状

### 顶层 AssetDocument JSON

```json
{
  "Target": "/Game/Anim/ABP_Enemy",
  "Class": "/Script/Engine.AnimBlueprint",
  "Properties": {
    "TargetSkeleton": {
      "Kind": "AssetRef",
      "Path": "/Game/Characters/SK_Enemy_Skeleton.SK_Enemy_Skeleton"
    },
    "PreviewSkeletalMesh": {
      "Kind": "AssetRef",
      "Path": "/Game/Characters/SK_Enemy.SK_Enemy"
    }
  },
  "Body": {
    "Variables": [
      { "Name": "Speed", "Type": "Float", "DefaultValue": "0.0" },
      { "Name": "Direction", "Type": "Float", "DefaultValue": "0.0" },
      { "Name": "bIsInAir", "Type": "Bool", "DefaultValue": "false" }
    ],
    "Graphs": {
      "AnimGraph": {
        "Nodes": {
          "Locomotion": {
            "Kind": "StateMachineRef",
            "StateMachine": "Locomotion"
          }
        },
        "Outputs": {
          "Pose": {
            "From": { "Node": "Locomotion", "Pin": "Pose" }
          }
        }
      },
      "StateMachines": {
        "Locomotion": {
          "Entry": "Idle",
          "States": {
            "Idle": {
              "Pose": {
                "Kind": "SequencePlayer",
                "Animation": "/Game/Anim/Idle.Idle"
              }
            },
            "Move": {
              "Pose": {
                "Kind": "BlendSpacePlayer",
                "BlendSpace": "/Game/Anim/BS_Locomotion.BS_Locomotion",
                "Inputs": {
                  "X": { "Variable": "Speed" },
                  "Y": { "Variable": "Direction" }
                }
              }
            }
          },
          "Transitions": [
            {
              "From": "Idle",
              "To": "Move",
              "Condition": {
                "Kind": "Compare",
                "Operator": ">",
                "Left": { "Variable": "Speed" },
                "Right": { "Literal": 5.0 }
              }
            }
          ]
        }
      },
      "BlueprintGraphs": {
        "EventGraph": {
          "Nodes": {},
          "Edges": []
        }
      }
    }
  }
}
```

### GraphIR 原则

`Body.Graphs` 是 Agent-facing canonical format。它不是低层 UE serializer，也不是源码语言的编译产物。profile 可以在 apply 前 normalize GraphIR，但 sidecar 里保存的仍然是 typed JSON graph。

GraphIR 必须满足：

- 节点使用稳定 ID，便于 patch、diff 和 diagnostics；
- 节点类型通过 `Kind` 或 `Class` 显式表达；
- 边使用 `{ "Node": "...", "Pin": "..." }` endpoint object，不使用压缩数组；
- asset/class/object/struct 值复用 AssetDocument fragment schema；
- 所有 validation error 使用 JSON Pointer，例如 `/Body/Graphs/StateMachines/Locomotion/Transitions/0/Condition/Left`。

## 图边界

### AnimGraph

`Body.Graphs.AnimGraph` 负责 pose-flow 语义，例如：

- output pose；
- sequence player；
- blendspace player；
- state machine reference；
- cached pose；
- slot；
- layered blend；
- raw animation node object。

它不声明普通 Blueprint 变量，也不表达 K2 执行逻辑。

### StateMachines

`Body.Graphs.StateMachines` 负责：

- entry state；
- states；
- state pose graph；
- transitions；
- transition blend/crossfade settings；
- transition condition graph；
- 支持时的 nested state machine references。

map key 是权威的 machine identity。transition endpoint 必须引用已声明 state。condition 可以引用 variables、literal、简单 expression graph 或 named helper function。

### BlueprintGraphs

`Body.Graphs.BlueprintGraphs` 负责普通 Blueprint 图逻辑：

- `EventGraph`；
- animation update event；
- helper functions；
- variable update logic；
- transition rules 引用的 bool functions。

新的 AssetDocument 图管线不通过 BSL fragment wrapping。已有 BSL 工具可以保留为历史 Blueprint 工具，但不作为 AnimationBlueprint GraphIR 的主入口或可选前端。

## 错误模型

每条诊断都应指向 JSON Pointer：

- `/Body/Graphs/AnimGraph/Outputs/Pose/From: unknown node 'Locomotion'`
- `/Body/Graphs/StateMachines/Locomotion/States/Move/Pose/Inputs/Y: unknown variable 'Direction'`
- `/Body/Graphs/BlueprintGraphs/EventGraph/Edges/0/To/Pin: unknown pin 'Execute'`

当 schema validation 或语义验证失败时，生成流程应在修改 UE 图之前失败。

当可以识别失败图时，UE compile error 应附带 asset type、graph name 和 JSON Pointer。

## UE 图 Builder 策略

Builder 使用 canonical GraphIR。

AnimGraph builder 应：

- 查找或创建官方 AnimGraph；
- 保留默认 graph lifecycle 行为；
- 尽可能使用 `UAnimationGraphSchema` 和 animation node action；
- 将受支持的 pose node 连接到 `UAnimGraphNode_Root`；
- 避免把 UE pin-level serializer JSON 暴露为 Agent-facing 契约。

StateMachine builder 应：

- 通过 UE graph/node lifecycle API 生成 state machine node；
- 让 state machine node 创建自己的 child graph；
- 通过 schema action 或 lifecycle API 创建 state node 和 transition node；
- 根据已验证的 condition GraphIR 构建 transition rule graph。

BlueprintGraph builder 应：

- 将 `Body.Graphs.BlueprintGraphs` 路由到请求的 EventGraph 或 function graph；
- 使用 typed JSON nodes/edges 生成 K2 graph；
- 为 `UAnimBlueprint` 适配 graph lookup、function graph lifecycle 和 node placement。

## 提取

提取应优先输出 GraphIR，而不是重建源码语言。第一批 graph spec 不要求完整 round-trip UE 图中所有节点；不支持的节点应以 deferred fields 或 raw node object 记录。

初始提取可以返回：

```json
{
  "Body": {
    "Graphs": {
      "AnimGraph": {
        "Nodes": {},
        "Edges": [],
        "Outputs": {}
      },
      "StateMachines": {},
      "BlueprintGraphs": {}
    }
  }
}
```

## 推荐的第一轮实现顺序

1. 先实现 `BlendSpace` 和 `AimOffset` generator。
2. 实现不带复杂 graph body 的 `AnimationBlueprint` lifecycle。
3. 定义通用 `Body.Graphs` GraphIR schema、validation、diagnostics 和 template。
4. 实现最小 AnimGraph GraphIR builder。
5. 实现 StateMachine GraphIR builder 和 transition condition graph。
6. 实现 Blueprint/EventGraph GraphIR builder。

这个顺序能让每一层在 graph schema 增长之前都可测试。
