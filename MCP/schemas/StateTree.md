# StateTree Generator Schema

Creates UE StateTree assets using editor data and the official StateTree compiler.

This schema covers schema selection, schema properties, dynamic editor nodes, state hierarchy, state fields, transitions, linked states/subtrees/assets, compile, save, and structure extraction. Parameters, property bags, and property bindings are covered by future StateTree specs.

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

## Structure And Transition Example

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
| `type` | string | No | `State`, `Group`, `Linked`, `LinkedAsset`, or `Subtree` |
| `selectionBehavior` | string | No | `None`, `TryEnterState`, `TrySelectChildrenInOrder`, `TrySelectChildrenAtRandom`, `TrySelectChildrenWithHighestUtility`, `TrySelectChildrenAtRandomWeightedByUtility`, or `TryFollowTransitions` |
| `tasksCompletion` | string | No | `Any` or `All` |
| `description` | string | No | State description |
| `tag` | string | No | Gameplay tag assigned to the state |
| `enabled` | bool | No | Whether the state is enabled |
| `customTickRate` | number/object | No | Number shorthand enables the rate; object supports `enabled` and `value` |
| `linkedState` | string | No | For `type: "Linked"`, legacy alias for `linkedSubtree`; target must resolve to a `Subtree` state |
| `linkedSubtree` | string | No | For `type: "Linked"`, links to a `Subtree` state by `id`, canonical path, or unique leaf name |
| `linkedAsset` | string | No | For `type: "LinkedAsset"`, object path to another `UStateTree` asset |
| `tasks` | array | No | State task nodes |
| `enterConditions` | array | No | State enter condition nodes |
| `considerations` | array | No | Utility consideration nodes |
| `transitions` | array | No | Transition entries |
| `children` | array | No | Child state entries |

Canonical paths use slash-separated state names from the top-level subtree, for example `Root/Combat/Attack`. State references resolve in this order: stable `id`, canonical path, then unique leaf `name`. Ambiguous leaf names are rejected and the error lists all matching paths.

## Transition Shape

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

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | No | Stable user-facing transition ID. GUID strings are preserved; other strings generate deterministic GUIDs |
| `trigger` | string | No | `OnStateCompleted`, `OnStateSucceeded`, `OnStateFailed`, `OnTick`, or `OnEvent`. `OnDelegate` is intentionally rejected |
| `type` | string | No | `None`, `Succeeded`, `Failed`, `GotoState`, `NextState`, or `NextSelectableState`. Defaults to `GotoState` when `target` is present, otherwise `Succeeded` |
| `target` | string | Required for `GotoState` | State reference by `id`, canonical path, or unique leaf name |
| `priority` | string | No | `Low`, `Normal`, `Medium`, `High`, or `Critical` |
| `enabled` | bool | No | Whether the transition is enabled |
| `delay` | number/object | No | Number shorthand enables the delay duration; object supports `enabled`, `duration`, and `randomVariance` |
| `requiredEvent` | string/object | Required for `OnEvent` | Gameplay tag string or `{ "tag": "..." }` |
| `conditions` | array | No | Dynamic condition nodes using `kind: "transitionCondition"` |

## Node Contract

The same node shape is used for `Evaluators`, `GlobalTasks`, state `tasks`, state `enterConditions`, state `considerations`, and transition `conditions`.

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

`extract_assets` emits state fields, linked asset references, linked state paths, transitions, transition condition nodes, and dynamic node skeletons with reflected `node`, `instance`, and `executionRuntimeData` properties. It intentionally omits runtime compact data, compiler handles, property binding internals, and property binding graphs.

## Current Limitations

- Root/state parameters and property bindings are reserved for later specs.
- `Update` cannot change `SchemaClass`; recreate the asset if the schema class must change.
