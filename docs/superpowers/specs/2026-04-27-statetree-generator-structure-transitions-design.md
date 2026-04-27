# StateTree Generator Structure And Transitions Design

- Date: 2026-04-27
- Status: Approved for implementation
- Branch: `feature/statetree-structure-transitions`
- Base: `feature/statetree-dynamic-nodes`
- Depends on: Spec 1 core lifecycle, Spec 2 dynamic node construction

## Goal

Implement StateTree structure semantics beyond "states as node containers": state fields, transition target resolution, transition conditions, linked states, linked subtrees, and linked assets.

This spec makes generated StateTrees behave like real editor-authored state machines while keeping parameters and property bindings out of scope.

## Scope

This spec adds support for:

- Full state fields:
  - `id`
  - `name`
  - `type`
  - `selectionBehavior`
  - `tasksCompletion`
  - `tag`
  - `description`
  - `enabled`
  - `customTickRate`
- Recursive state hierarchy with deterministic IDs.
- Top-level `SubTrees` containing both normal root states and `type: "Subtree"` roots.
- State `transitions`.
- Transition fields:
  - `id`
  - `trigger`
  - `type`
  - `target`
  - `priority`
  - `enabled`
  - `delay`
  - `requiredEvent`
  - `conditions`
- Transition condition nodes using the Spec 2 dynamic node contract.
- Linked state and linked subtree references through `FStateTreeStateLink`.
- Linked asset states through `UStateTreeState::SetLinkedStateAsset()`.
- Extract output for the same structure fields and transition skeletons.

## Non-goals

This spec does not implement:

- Root or state parameters.
- Property bags.
- Property bindings or property function bindings.
- State parameter overrides for linked states/assets.
- Runtime compact data serialization.
- Behavioral automation tests of transition execution in PIE.
- Batch dependency ordering for generating a linked asset and a referencing StateTree in the same `generate_assets` request.

## JSON Contract

The existing StateTree schema keeps `SubTrees` as the top-level state root array.

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
			"tasksCompletion": "Any",
			"children": [
				{
					"id": "idle",
					"name": "Idle",
					"type": "State",
					"tasks": [],
					"transitions": [
						{
							"id": "idle-to-patrol",
							"trigger": "OnStateCompleted",
							"type": "GotoState",
							"target": "patrol",
							"priority": "Normal",
							"enabled": true
						}
					]
				},
				{
					"id": "patrol",
					"name": "Patrol",
					"type": "State"
				}
			]
		}
	]
}
```

### State Fields

| Field | Type | Required | Notes |
|---|---|---:|---|
| `id` | string | No | Stable input ID. If absent, generated from canonical path. |
| `name` | string | Yes | User-visible state name. |
| `type` | string | No | `State`, `Group`, `Linked`, `LinkedAsset`, or `Subtree`. Default: `State`. |
| `selectionBehavior` | string | No | Maps to `EStateTreeStateSelectionBehavior`. |
| `tasksCompletion` | string | No | Maps to `EStateTreeTaskCompletionType`. |
| `tag` | string | No | Gameplay tag string. |
| `description` | string | No | State description. |
| `enabled` | bool | No | Maps to `bEnabled`. Default UE value preserved. |
| `customTickRate` | number/object | No | Number enables `bHasCustomTickRate` and sets `CustomTickRate`; object form supports `{ "enabled": false }`. |
| `linkedState` | string | Required for `Linked` | Target state ID/path/name. |
| `linkedSubtree` | string | Required for `Subtree` extension links when used on non-subtree linked state fields. |
| `linkedAsset` | string | Required for `LinkedAsset` | StateTree asset path. |
| `transitions` | array | No | State transitions. |
| `tasks` / `enterConditions` / `considerations` / `children` | arrays | No | Existing Spec 2 fields. |

`type: "Subtree"` marks a top-level root that can be linked by other states. It may also have children, tasks, conditions, and transitions.

### Transition Fields

| Field | Type | Required | Notes |
|---|---|---:|---|
| `id` | string | No | Stable input ID. |
| `trigger` | string | No | `OnStateCompleted`, `OnStateSucceeded`, `OnStateFailed`, `OnTick`, `OnEvent`, `OnDelegate`. Default: `OnStateCompleted`. |
| `type` | string | No | `None`, `Succeeded`, `Failed`, `GotoState`, `NextState`, `NextSelectableState`. Default: `GotoState` when `target` is present, otherwise `Succeeded`. |
| `target` | string | Required for `GotoState` | State ID, canonical path, or unambiguous state name. |
| `priority` | string | No | `Low`, `Normal`, `Medium`, `High`, `Critical`. |
| `enabled` | bool | No | Maps to `bTransitionEnabled`. |
| `delay` | number/object | No | Number enables delay duration. Object supports `{ "duration": 0.2, "randomVariance": 0.05 }`. |
| `requiredEvent` | string/object | Required for `OnEvent` | String is event gameplay tag. Object supports `{ "tag": "Event.AI.SawPlayer" }`. |
| `conditions` | array | No | Dynamic condition nodes, `kind` defaults to `transitionCondition`. |

## Target Resolution

State resolution is two-phase.

Phase 1 builds all states and indexes them by:

- input `id`
- canonical path, for example `Root/Combat/Attack`
- leaf name if unique
- generated deterministic GUID

Phase 2 resolves references after the entire tree exists:

- `transition.target`
- state `linkedState`
- state `linkedSubtree`

Resolution rules:

1. Exact ID match wins.
2. Exact canonical path match wins.
3. Leaf name match is allowed only when unique.
4. Missing targets fail before compile.
5. Ambiguous leaf names fail before compile and list matching paths.
6. `linkedSubtree` targets must resolve to `type: "Subtree"`.
7. `linkedState` targets must not resolve to the state itself.

## Linked Asset Resolution

`type: "LinkedAsset"` requires `linkedAsset`.

The generator loads the asset path as `UStateTree`. It fails before compile if:

- the asset path is missing
- the asset does not exist
- the asset is not a `UStateTree`

Linked asset parameters are not synchronized by this spec beyond calling `SetLinkedStateAsset()`. Parameter overrides belong to Spec 4.

## Validation

The generator should reject unsupported or dangerous inputs before mutating editor data where practical:

- `transitions` must be an array of objects.
- Transition `conditions` must be array of objects.
- `GotoState` requires a resolvable `target`.
- `NextState`, `NextSelectableState`, `Succeeded`, `Failed`, and `None` must not require a `target`.
- `OnEvent` requires `requiredEvent`.
- `OnDelegate` is rejected in this spec because delegate listener binding requires additional editor data that is not part of this contract.
- `linkedState` and `linkedSubtree` cannot be used on `type: "State"` unless the state `type` is explicitly `Linked`.
- `linkedAsset` cannot be used unless `type: "LinkedAsset"`.
- Transition condition nodes are validated through Spec 2 `ValidateNodeSpec()` with `EAFStateTreeNodeKind::TransitionCondition`.

## Extraction

Extraction should output editor-facing stable structure, not runtime compact handles.

For each state, extract:

- `id`
- `name`
- `type`
- `selectionBehavior`
- `tasksCompletion`
- `tag`
- `description`
- `enabled`
- `customTickRate`
- `linkedState` or `linkedSubtree` when resolvable to an editor state
- `linkedAsset`
- node arrays from Spec 2
- `transitions`
- `children`

For each transition, extract:

- `id`
- `trigger`
- `type`
- `target`
- `priority`
- `enabled`
- `delay`
- `requiredEvent`
- `conditions`

If an extracted link cannot be mapped back to an editor state path, emit the underlying enum/type and omit `target` rather than producing an invalid target.

## Verification

Required smoke coverage:

- Complex success: nested states with `GotoState`, `NextState`, delay, and a transition condition node.
- Linked subtree success: a `Linked` state points at a top-level `Subtree`.
- Linked asset success: a `LinkedAsset` state references a previously generated minimal StateTree asset.
- Failure: missing transition target.
- Failure: ambiguous target leaf name.
- Failure: `OnEvent` without `requiredEvent`.
- Failure: linked subtree target points at a non-subtree state.

Editor/MCP verification must use real `UnrealEditor.app` without `-NullRHI` for at least one final visual smoke, matching the dynamic nodes spec.

