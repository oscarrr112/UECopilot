# StateTree Generator Schema

Creates UE StateTree assets using editor data and the official StateTree compiler.

This schema covers schema selection, schema properties, dynamic editor nodes, state hierarchy, state fields, transitions, linked states/subtrees/assets, root/state parameters, linked parameter overrides, property bindings and property function bindings through the top-level `bindings` array, compile, save, and structure extraction.

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
| `RootParameters` | object | No | Root StateTree parameter property bag |
| `SubTrees` | array | No | Top-level StateTree state roots |
| `bindings` | array | No | Property bindings and property function bindings |

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

## Parameters

`RootParameters`, state `parameters`, and linked-state `parameterOverrides` use a typed property-bag shape. Parameter IDs are optional; when omitted, AssetFactory creates deterministic IDs from the parameter scope and name so extraction can round-trip stable identifiers.

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

State-local parameters use the same entry shape:

```json
{
	"id": "patrol",
	"name": "Patrol",
	"parameters": {
		"LocalSpeed": { "type": "Float", "value": 250.0, "overridden": true }
	}
}
```

Linked and linked-asset states should use `parameterOverrides` instead of declaring a fresh local schema. AssetFactory resolves the linked target, syncs its parameter schema, then applies the listed overrides:

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

Supported generator types are `Bool`, `Float`, `Name`, `String`, `Text`, `Struct:<StructName>`, `Object:<ClassName>`, `SoftObject:<ClassName>`, `Class:<ClassName>`, `SoftClass:<ClassName>`, `Array:<ElementType>`, and `Set:<ElementType>`. `Struct` accepts loadable `UScriptStruct` paths/names and includes explicit support for `Vector`, `Vector2D`, and `Rotator` aliases. `Map` is rejected for StateTree parameters on UE 5.7 because `EPropertyBagContainerType` does not expose a map container.

`parameterOverrides` may omit `type` because the linked target defines the schema. If `type` is provided, it must match the target parameter type. Unknown override names and type mismatches are rejected before the asset is saved.

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
| `parameters` | object | No | State-local parameter property bag. For non-linked states, extracted overridden entries include `overridden: true` |
| `parameterOverrides` | object | No | Linked or linked-asset parameter overrides applied after the linked target schema is synchronized |
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

## Bindings

Property bindings are declared in the top-level `bindings` array. Each binding entry must define exactly one source form and one `target`:

- Ordinary binding: `source` + `target`
- Property function binding: `function` + `target`

Ordinary binding example:

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

Property function binding example:

```json
{
	"bindings": [
		{
			"id": "duration-from-add-float",
			"function": {
				"type": "/Script/StateTreeModule.StateTreeAddFloatPropertyFunction",
				"output": ["Result"],
				"inputs": {
					"Left": {
						"source": { "kind": "rootParameter", "path": ["BaseDelay"] }
					},
					"Right": {
						"source": { "kind": "rootParameter", "path": ["BonusDelay"] }
					}
				}
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

`source` and `target` endpoints use a JSON object with `kind`, optional owner selectors, optional `section`, and a `path` array. Supported endpoint `kind` values are `rootParameter`, `stateParameter`, `context`, `evaluator`, `globalTask`, `task`, `enterCondition`, `transitionCondition`, `consideration`, and `node`. Property functions are expressed with a binding-level or nested `function` object, not `kind: "function"`. Snake-case aliases are accepted for multi-word values, for example `root_parameter`, `global_task`, `enter_condition`, and `transition_condition`.

Node-backed endpoints may select data with `section`: `instance`, `node`, or `executionRuntimeData`. The default is `instance`. State-scoped node targets use `state` plus `node`; transition condition endpoints also use `transition` to identify the transition.

Each path segment supports either string shorthand or an explicit JSON object:

```json
{
	"path": [
		"Items",
		{ "name": "Entry", "arrayIndex": 0 },
		{ "name": "Value", "guid": "00000000-0000-0000-0000-000000000000" }
	]
}
```

The explicit object shape supports `name`, `arrayIndex`, and `guid` for generator input. Extraction can preserve `instanceStruct` and `access` metadata from existing editor assets, but generation rejects those fields until instanced indirection support lands. StateTree bindings do not support a string DSL such as `"Root/Idle.delay-task.Duration"`; use explicit JSON endpoint objects and path arrays so the generator can validate each owner and segment.

Property function bindings use a `function` object with `type`, `output`, and `inputs`. The function `type` must resolve to a supported StateTree property function. `output` is a binding path array on the function result, and each input value must be a JSON object containing exactly one of `source` or nested `function`.

Invalid diagnostics include the binding index or binding `id` where available. Common failures include unknown source, unknown target, bad path, duplicate target path, bad function type, and type mismatch.

## Extraction

`extract_assets` emits root parameters, state-local parameters, linked parameter overrides, state fields, linked asset references, linked state paths, transitions, transition condition nodes, and dynamic node skeletons with reflected `node`, `instance`, and `executionRuntimeData` properties. For linked and linked-asset states, only overridden linked parameters are emitted under `parameterOverrides`; the full linked target schema is not copied into ordinary `parameters`.

Extraction may describe existing editor-authored property bag types that are not accepted by the generator yet. Generator input is limited to the supported generator types listed above.

## Current Limitations

- Function input map keys currently support single property names. Multi-segment editor-authored function input target extraction is deferred.
- Generator input rejects path segment `instanceStruct` and `access`; extraction may emit them for editor-authored assets, but they are not round-trip supported yet.
- `kind: "function"` is parsed for forward compatibility but is not resolved as a normal source or target endpoint; use a `function` object instead.
- Numeric integer, double, byte, and enum parameter generation is intentionally not part of the current StateTree parameter slice.
- `Update` cannot change `SchemaClass`; recreate the asset if the schema class must change.
