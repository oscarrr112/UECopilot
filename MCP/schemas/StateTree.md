# StateTree 生成器 Schema

通过 editor data 和官方 StateTree compiler 创建 UE StateTree 资产。

本文覆盖 schema 选择、schema properties、动态 editor nodes、state hierarchy、state fields、transitions、linked states/subtrees/assets、root/state parameters、linked parameter overrides、顶层 `bindings` 数组中的 property bindings / property function bindings、compile、save，以及结构提取。

## 顶层字段

| 字段 | 类型 | 必填 | 说明 |
|-------|------|----------|-------------|
| `AssetType` | string | 是 | 必须是 `"StateTree"` |
| `Name` | string | 是 | 资产名 |
| `Path` | string | 是 | Content path，例如 `"/Game/AI"` |
| `Action` | string | 否 | `"Create"`、`"Update"` 或 `"CreateOrUpdate"` |
| `SchemaClass` | string | 是 | `UStateTreeSchema` 子类 path 或精确 class name |
| `SchemaProperties` | object | 否 | 通过反射写入 schema instance 的 properties |
| `Evaluators` | array | 否 | global evaluator nodes |
| `GlobalTasks` | array | 否 | global task nodes |
| `GlobalTasksCompletion` | string | 否 | `"Any"` 或 `"All"` |
| `RootParameters` | object | 否 | root StateTree parameter property bag |
| `SubTrees` | array | 否 | 顶层 StateTree state roots |
| `bindings` | array | 否 | property bindings 和 property function bindings |

## 最小示例

```json
{
  "AssetType": "StateTree",
  "Name": "ST_Enemy",
  "Path": "/Game/AI",
  "SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema"
}
```

## 动态节点示例

```json
{
  "AssetType": "StateTree",
  "Name": "ST_Enemy",
  "Path": "/Game/AI",
  "SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema",
  "SubTrees": [
    {
      "id": "root",
      "name": "Root",
      "type": "State",
      "selectionBehavior": "TrySelectChildrenInOrder",
      "children": [
        {
          "id": "idle",
          "name": "Idle",
          "tasks": [
            {
              "id": "delay",
              "kind": "task",
              "type": "/Script/StateTreeModule.StateTreeDelayTask",
              "instance": {
                "properties": {
                  "Duration": 0.1,
                  "RandomDeviation": 0.0,
                  "bRunForever": false
                }
              }
            }
          ]
        }
      ]
    }
  ]
}
```

## 结构与 Transition 示例

```json
{
  "AssetType": "StateTree",
  "Name": "ST_EnemyCombat",
  "Path": "/Game/AI",
  "SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema",
  "SubTrees": [
    {
      "id": "root",
      "name": "Root",
      "type": "State",
      "selectionBehavior": "TrySelectChildrenInOrder",
      "children": [
        {
          "id": "patrol",
          "name": "Patrol",
          "description": "Default movement",
          "customTickRate": { "enabled": true, "value": 0.5 },
          "transitions": [
            {
              "id": "patrol-to-attack",
              "trigger": "OnStateSucceeded",
              "target": "attack",
              "priority": "High",
              "delay": { "enabled": true, "duration": 0.25, "randomVariance": 0.05 },
              "conditions": [
                {
                  "id": "chance",
                  "kind": "transitionCondition",
                  "type": "/Script/StateTreeModule.StateTreeRandomCondition",
                  "instance": { "properties": { "Threshold": 1.0 } }
                }
              ]
            }
          ]
        },
        {
          "id": "attack",
          "name": "Attack",
          "transitions": [
            { "id": "attack-to-patrol", "trigger": "OnStateCompleted", "target": "patrol" }
          ]
        }
      ]
    }
  ]
}
```

## 参数

`RootParameters`、state `parameters`、linked-state `parameterOverrides` 都使用 typed property-bag 形状。Parameter ID 可选；省略时，AssetFactory 会根据 parameter scope 和 name 生成 deterministic ID，让 extraction 能 round-trip stable identifiers。

```json
{
	"RootParameters": {
		"MoveSpeed": { "type": "Float", "value": 600.0 },
		"CanAttack": { "type": "Bool", "value": true },
		"SpawnOffset": {
			"type": "Struct:/Script/CoreUObject.Vector",
			"value": { "X": 0.0, "Y": 0.0, "Z": 80.0 }
		},
		"PatrolNames": { "type": "Set:Name", "value": ["North", "South"] }
	}
}
```

state-local parameters 使用同样的 entry 形状：

```json
{
	"id": "patrol",
	"name": "Patrol",
	"parameters": {
		"LocalSpeed": { "type": "Float", "value": 250.0, "overridden": true }
	}
}
```

linked 和 linked-asset states 应使用 `parameterOverrides`，而不是声明新的 local schema。AssetFactory 会解析 linked target、同步它的 parameter schema，再应用列出的 overrides：

```json
{
	"id": "use-linked-asset",
	"name": "UseLinkedAsset",
	"type": "LinkedAsset",
	"linkedAsset": "/Game/AI/ST_Shared.ST_Shared",
	"parameterOverrides": {
		"LinkedSpeed": { "value": 350.0, "overridden": true }
	}
}
```

支持的 generator types 包括 `Bool`、`Float`、`Name`、`String`、`Text`、`Struct:<StructName>`、`Object:<ClassName>`、`SoftObject:<ClassName>`、`Class:<ClassName>`、`SoftClass:<ClassName>`、`Array:<ElementType>` 和 `Set:<ElementType>`。`Struct` 接受可加载的 `UScriptStruct` path/name，并显式支持 `Vector`、`Vector2D`、`Rotator` aliases。UE 5.7 的 `EPropertyBagContainerType` 没有 map container，因此 StateTree parameters 会拒绝 `Map`。

`parameterOverrides` 可以省略 `type`，因为 linked target 已经定义 schema。如果提供 `type`，它必须匹配目标 parameter type。未知 override name 和 type mismatch 会在保存资产前被拒绝。

## AI Component Schema 示例

```json
{
  "AssetType": "StateTree",
  "Name": "ST_EnemyAI",
  "Path": "/Game/AI",
  "SchemaClass": "/Script/GameplayStateTreeModule.StateTreeAIComponentSchema",
  "SchemaProperties": {
    "ContextActorClass": "/Script/Engine.Pawn",
    "AIControllerClass": "/Script/AIModule.AIController"
  }
}
```

## State 形状

State entries 支持：

| 字段 | 类型 | 必填 | 说明 |
|-------|------|----------|-------------|
| `id` | string | 否 | 面向用户的 stable ID。GUID strings 会被保留，其他 string 会生成 deterministic GUID |
| `name` | string | 是 | State display name |
| `type` | string | 否 | `State`、`Group`、`Linked`、`LinkedAsset` 或 `Subtree` |
| `selectionBehavior` | string | 否 | `None`、`TryEnterState`、`TrySelectChildrenInOrder`、`TrySelectChildrenAtRandom`、`TrySelectChildrenWithHighestUtility`、`TrySelectChildrenAtRandomWeightedByUtility` 或 `TryFollowTransitions` |
| `tasksCompletion` | string | 否 | `Any` 或 `All` |
| `description` | string | 否 | State 描述 |
| `tag` | string | 否 | 分配给 state 的 Gameplay tag |
| `enabled` | bool | 否 | state 是否启用 |
| `customTickRate` | number/object | 否 | number shorthand 会启用 tick rate；object 支持 `enabled` 和 `value` |
| `linkedState` | string | 否 | `type: "Linked"` 时作为 `linkedSubtree` 的 legacy alias；target 必须解析为 `Subtree` state |
| `linkedSubtree` | string | 否 | `type: "Linked"` 时，按 `id`、canonical path 或唯一 leaf name 链接到 `Subtree` state |
| `linkedAsset` | string | 否 | `type: "LinkedAsset"` 时，指向另一个 `UStateTree` asset 的 object path |
| `parameters` | object | 否 | state-local parameter property bag。对 non-linked states，提取出的 overridden entries 会包含 `overridden: true` |
| `parameterOverrides` | object | 否 | linked 或 linked-asset parameter overrides，在 linked target schema 同步后应用 |
| `tasks` | array | 否 | state task nodes |
| `enterConditions` | array | 否 | state enter condition nodes |
| `considerations` | array | 否 | utility consideration nodes |
| `transitions` | array | 否 | transition entries |
| `children` | array | 否 | child state entries |

Canonical path 使用从顶层 subtree 开始、以 slash 分隔的 state names，例如 `Root/Combat/Attack`。State reference 的解析顺序是 stable `id`、canonical path、唯一 leaf `name`。含糊的 leaf name 会被拒绝，错误会列出所有匹配路径。

## Transition 形状

```json
{
  "id": "to-attack",
  "trigger": "OnStateCompleted",
  "type": "GotoState",
  "target": "attack",
  "priority": "Normal",
  "enabled": true,
  "delay": { "enabled": true, "duration": 0.25, "randomVariance": 0.05 },
  "requiredEvent": { "tag": "Event.StateTree.Attack" },
  "conditions": []
}
```

| 字段 | 类型 | 必填 | 说明 |
|-------|------|----------|-------------|
| `id` | string | 否 | 面向用户的 stable transition ID。GUID strings 会被保留，其他 string 会生成 deterministic GUID |
| `trigger` | string | 否 | `OnStateCompleted`、`OnStateSucceeded`、`OnStateFailed`、`OnTick` 或 `OnEvent`。`OnDelegate` 会被有意拒绝 |
| `type` | string | 否 | `None`、`Succeeded`、`Failed`、`GotoState`、`NextState` 或 `NextSelectableState`。存在 `target` 时默认 `GotoState`，否则默认 `Succeeded` |
| `target` | string | `GotoState` 必填 | 按 `id`、canonical path 或唯一 leaf name 引用 state |
| `priority` | string | 否 | `Low`、`Normal`、`Medium`、`High` 或 `Critical` |
| `enabled` | bool | 否 | transition 是否启用 |
| `delay` | number/object | 否 | number shorthand 会启用 delay duration；object 支持 `enabled`、`duration`、`randomVariance` |
| `requiredEvent` | string/object | `OnEvent` 必填 | Gameplay tag string 或 `{ "tag": "..." }` |
| `conditions` | array | 否 | 使用 `kind: "transitionCondition"` 的 dynamic condition nodes |

## 节点契约

`Evaluators`、`GlobalTasks`、state `tasks`、state `enterConditions`、state `considerations`、transition `conditions` 使用同一个 node shape。

```json
{
  "id": "compare-int",
  "kind": "enterCondition",
  "type": "/Script/StateTreeModule.StateTreeCompareIntCondition",
  "node": {
    "properties": {
      "bInvert": false,
      "Operator": "Equal"
    }
  },
  "instance": {
    "properties": {
      "Left": 1,
      "Right": 1
    }
  },
  "executionRuntimeData": {
    "properties": {}
  },
  "expression": {
    "indent": 0,
    "operand": "And"
  }
}
```

| 字段 | 类型 | 必填 | 说明 |
|-------|------|----------|-------------|
| `id` | string | 否 | 面向用户的 stable node ID |
| `kind` | string | 否 | 存在时必须匹配所在 slot：`evaluator`、`globalTask`、`task`、`enterCondition`、`transitionCondition` 或 `consideration` |
| `type` | string | 是 | C++ 节点的 `UScriptStruct` path/name，或 Blueprint 节点的 `UClass` path/name |
| `node.properties` | object | 否 | node template struct 或 Blueprint wrapper struct 上的 properties |
| `instance.properties` | object | 否 | struct instance data 或 Blueprint UObject instance data 上的 properties |
| `executionRuntimeData.properties` | object | 否 | execution runtime data 上的 properties |
| `expression` | object | 否 | condition/consideration expression metadata |
| `properties` | object | 否 | 简单 C++ struct 节点的 `instance.properties` alias |

C++ struct 节点会从 `type` 动态解析，按所在 slot 期望的 StateTree base struct 校验，再通过 `UStateTreeSchema::IsStructAllowed()` 检查，初始化为 `FStateTreeEditorNode`，最后交给官方 compiler 编译。

Blueprint 节点 class 会动态解析，按期望的 Blueprint node base class 校验，再通过 `UStateTreeSchema::IsClassAllowed()` 检查，使用匹配的 StateTree Blueprint wrapper struct 包装，并初始化 UObject instance。

## 绑定（bindings）

Property bindings 声明在顶层 `bindings` 数组中。每个 binding entry 必须定义一种 source 形式和一个 `target`：

- 普通 binding：`source` + `target`
- property function binding：`function` + `target`

普通 binding 示例：

```json
{
	"bindings": [
		{
			"id": "duration-from-root-parameter",
			"source": { "kind": "rootParameter", "path": ["DelaySeconds"] },
			"target": {
				"kind": "task",
				"state": "Root/Idle",
				"node": "delay-task",
				"section": "instance",
				"path": ["Duration"]
			}
		}
	]
}
```

property function binding 示例：

```json
{
	"bindings": [
		{
			"id": "duration-from-add-float",
			"function": {
				"type": "/Script/StateTreeModule.StateTreeAddFloatPropertyFunction",
				"output": ["Result"],
				"inputs": [
					{
						"target": ["Left"],
						"source": { "kind": "rootParameter", "path": ["BaseDelay"] }
					},
					{
						"target": ["Right"],
						"source": { "kind": "rootParameter", "path": ["BonusDelay"] }
					}
				]
			},
			"target": {
				"kind": "task",
				"state": "Root/Idle",
				"node": "delay-task",
				"section": "instance",
				"path": ["Duration"]
			}
		}
	]
}
```

`source` 和 `target` endpoints 使用 JSON object，包含 `kind`、可选 owner selectors、可选 `section`、以及 `path` 数组。支持的 endpoint `kind` 包括 `rootParameter`、`stateParameter`、`context`、`evaluator`、`globalTask`、`task`、`enterCondition`、`transitionCondition`、`consideration` 和 `node`。Property functions 使用 binding-level 或 nested `function` object 表达，不使用 `kind: "function"`。Multi-word value 接受 snake-case aliases，例如 `root_parameter`、`global_task`、`enter_condition`、`transition_condition`。

node-backed endpoints 可以通过 `section` 选择数据：`instance`、`node` 或 `executionRuntimeData`。默认是 `instance`。State-scoped node targets 使用 `state` 加 `node`；transition condition endpoints 还会使用 `transition` 标识 transition。

每个 path segment 支持 string shorthand 或显式 JSON object：

```json
{
	"path": [
		"Items",
		{ "name": "Entry", "arrayIndex": 0 },
		{
			"name": "Value",
			"guid": "00000000-0000-0000-0000-000000000000",
			"instanceStruct": "/Script/StateTreeModule.StateTreePropertyFunctionCommonBase",
			"access": "StructInstance"
		}
	]
}
```

显式 object shape 在 generator input 中支持 `name`、`arrayIndex`、`guid`、`instanceStruct` 和 `access`。`instanceStruct` 必须使用完整 script struct/object path，例如 `"/Script/StateTreeModule.StateTreePropertyFunctionCommonBase"`。`access` 支持 `StructInstance`、`ObjectInstance` 和 `Unset`；省略时默认 `Unset`。如果提供 `instanceStruct` 但省略 `access`，generator 会使用 `StructInstance`，因为该 segment 明确描述 instanced struct indirection。StateTree bindings 不支持 `"Root/Idle.delay-task.Duration"` 这类 string DSL；请使用显式 JSON endpoint objects 和 path arrays，让 generator 能校验每个 owner 和 segment。

Property function bindings 使用包含 `type`、`output`、`inputs` 的 `function` object。function `type` 必须解析为受支持的 StateTree property function。`output` 是 function result 上的 binding path array。Canonical `function.inputs` 是 array；每一项必须包含 `target` path array，并且只能包含 `source` 或 nested `function` 二者之一。Legacy object-shaped `inputs` 仍作为 generation compatibility input 接受，并按 object key 规范化为单段 `target`；extraction 和新 fixtures 只输出 canonical array。

无效配置的诊断会在可用时包含 binding index 或 binding `id`。常见失败包括 unknown source、unknown target、bad path、duplicate target path、bad function type 和 type mismatch。

## 提取与 Round-trip

`extract_assets` 会为通过该 generator 创建的资产输出 generator-readable StateTree JSON。支持的 round-trip 路径是：

```text
Generate -> Extract -> Generate from extracted JSON -> Extract -> semantic compare
```

项目 verifier：

```bash
python3 docs/superpowers/verification/statetree_roundtrip_check.py /tmp/left.json /tmp/right.json --fixture ST_Name
```

完整 MCP smoke helper：

```bash
UE_API_BASE=http://127.0.0.1:8559 npm --prefix MCP run smoke:statetree-roundtrip
```

Round-trip comparison 是语义比较，不是 byte-level 比较。它会忽略 compile hashes、字段顺序、等价于 missing fields 的 empty containers，以及 `1e-4` 以内的 float noise。它会比较 state paths、transitions、parameters、dynamic node fields、ordinary bindings 和 property function bindings。

`extract_assets` 会输出 root parameters、state-local parameters、linked parameter overrides、state fields、linked asset references、linked state paths、transitions、transition condition nodes，以及带有 reflected `node`、`instance`、`executionRuntimeData` properties 的 dynamic node skeletons。对 linked 和 linked-asset states，只会在 `parameterOverrides` 下输出 overridden linked parameters；完整 linked target schema 不会复制到普通 `parameters`。

Extraction 可能描述 generator input subset 之外的 existing editor-authored assets。这些字段除非明确标为 generator-readable，否则保持 extraction-only。Generator input 限于上面列出的支持类型。

当 extraction 检测到 nested property function input graph 循环引用已访问过的 function node 时，可能会输出只读 `diagnostics.bindings` warning：

```json
{
	"diagnostics": {
		"bindings": [
			{
				"code": "StateTree.Binding.FunctionCycle",
				"severity": "warning",
				"path": "$.bindings[0].function.inputs[0].function",
				"bindingTarget": "Root/Idle/delay-task.Duration",
				"message": "Skipped nested StateTree property function input because the graph references an already visited function node."
			}
		]
	}
}
```

Diagnostics 不是 authoring input，不应作为 contract data 发回 generation。Round-trip comparison 默认忽略 diagnostics；专门的 diagnostics 检查应断言稳定的 `code`，只有在测试明确覆盖可读输出时才比较 `message` 文本。当前 schema 只承诺 `StateTree.Binding.FunctionCycle` 这一类 extraction diagnostic；更丰富的 unsupported binding graph diagnostics 仍属于后续 diagnostics/polish。

## 当前限制

- `kind: "function"` 会为了 forward compatibility 被解析，但不会作为普通 source 或 target endpoint 解析；请使用 `function` object。
- Numeric integer、double、byte、enum parameter generation 暂不属于当前 StateTree parameter slice。
- `Update` 不能修改 `SchemaClass`；如果必须修改 schema class，请重新创建资产。
