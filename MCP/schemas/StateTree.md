# StateTree Generator Schema

Creates UE StateTree assets using editor data and the official StateTree compiler.

This schema covers schema selection, schema properties, dynamic editor nodes, state hierarchy, compile, save, and node skeleton extraction. Transitions, parameters, property bags, and property bindings are covered by future StateTree specs.

## Top-Level Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `AssetType` | string | Yes | Must be `"StateTree"` |
| `Name` | string | Yes | Asset name |
| `Path` | string | Yes | Content path, for example `"/Game/AI"` |
| `Action` | string | No | `"Create"`, `"Update"`, or `"CreateOrUpdate"` |
| `SchemaClass` | string | Yes | `UStateTreeSchema` subclass path or exact class name |
| `SchemaProperties` | object | No | Properties applied to the schema instance via reflection |
| `Evaluators` | array | No | Global evaluator nodes |
| `GlobalTasks` | array | No | Global task nodes |
| `GlobalTasksCompletion` | string | No | `"Any"` or `"All"` |
| `SubTrees` | array | No | Top-level StateTree state roots |

## Minimal Example

```json
{
  "AssetType": "StateTree",
  "Name": "ST_Enemy",
  "Path": "/Game/AI",
  "SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema"
}
```

## Dynamic Node Example

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

## AI Component Schema Example

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

## State Shape

State entries support:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | No | Stable user-facing ID. GUID strings are preserved; other strings generate deterministic GUIDs |
| `name` | string | Yes | State display name |
| `type` | string | No | `State`, `Group`, `Linked`, `LinkedAsset`, or `Subtree`. Linked behavior is reserved for later specs |
| `tasks` | array | No | State task nodes |
| `enterConditions` | array | No | State enter condition nodes |
| `considerations` | array | No | Utility consideration nodes |
| `children` | array | No | Child state entries |

## Node Contract

The same node shape is used for `Evaluators`, `GlobalTasks`, state `tasks`, state `enterConditions`, and state `considerations`.

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

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | No | Stable user-facing node ID |
| `kind` | string | No | Must match the containing slot when present: `evaluator`, `globalTask`, `task`, `enterCondition`, `transitionCondition`, or `consideration` |
| `type` | string | Yes | `UScriptStruct` path/name for C++ nodes or `UClass` path/name for Blueprint nodes |
| `node.properties` | object | No | Properties on the node template struct or Blueprint wrapper struct |
| `instance.properties` | object | No | Properties on struct instance data or Blueprint UObject instance data |
| `executionRuntimeData.properties` | object | No | Properties on execution runtime data |
| `expression` | object | No | Condition/consideration expression metadata |
| `properties` | object | No | Alias for `instance.properties` for simple C++ struct nodes |

C++ struct nodes are resolved dynamically from `type`, validated against the containing slot's expected StateTree base struct, checked through `UStateTreeSchema::IsStructAllowed()`, initialized as `FStateTreeEditorNode`, then compiled by the official compiler.

Blueprint node classes are resolved dynamically, validated against the expected Blueprint node base class, checked through `UStateTreeSchema::IsClassAllowed()`, wrapped with the matching StateTree Blueprint wrapper struct, and initialized with a UObject instance.

## Extraction

`extract_assets` emits dynamic node skeletons with reflected `node`, `instance`, and `executionRuntimeData` properties. It intentionally omits runtime compact data, compiler handles, property binding internals, and transition target resolution metadata.

## Current Limitations

- Transitions are not accepted as input yet.
- Linked states, linked subtrees, and linked assets are not accepted as input yet.
- Root/state parameters and property bindings are reserved for later specs.
- `Update` cannot change `SchemaClass`; recreate the asset if the schema class must change.
