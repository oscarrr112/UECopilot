# AnimationBlueprint Graph-Family Adapter Boundary

## 1. Goal

`Body.AnimGraph` must be owned by a public graph-family region adapter, not by `FAnimBlueprintAssetDocumentCapability` private parsing code.

The first pilot supports only a canonical root-only AnimGraph:

```json
[
  {
    "Name": "AnimGraph",
    "Nodes": [],
    "OutputPose": {
      "Node": null,
      "Pin": "Result"
    }
  }
]
```

This is intentionally a boundary pilot. It proves the ABP profile can route AnimGraph through a dedicated adapter and stable diff/extract paths before adding pose node materialization.

## 2. Node Identity

Pilot scope has no authored pose nodes. `Nodes` must be an empty array.

Future node support must use a stable authored `Id` string per node. UE graph node GUIDs may be recorded as extraction evidence, but they are not the authored identity unless a future adapter spec proves they remain stable across rebuilds.

## 3. Pin Identity

The only supported pin identity is the graph output pose pin:

- JSON path: `/Body/AnimGraph/AnimGraph/OutputPose/Pin`
- canonical value: `Result`

Future node pins must use semantic pin names from the supported node adapter, not pin array indexes.

## 4. Canonicalization

The adapter canonical form is always:

- one graph object;
- `Name == "AnimGraph"`;
- `Nodes == []`;
- `OutputPose.Node == null`;
- `OutputPose.Pin == "Result"`.

The adapter accepts `null`, empty array, and empty object as delete/empty compatibility values and canonicalizes them to the root-only graph on extract.

## 5. Unsupported Node Evidence

Any non-empty `Nodes` array fails validation:

- path: `/Body/AnimGraph/AnimGraph/Nodes/<Index>`
- code: `UnsupportedAnimGraphNode`

Unknown graph-level keys fail at their JSON Pointer with `UnknownAnimGraphField`.

## 6. Diff Paths

Diff paths must be semantic, not numeric array positions:

- graph: `/Body/AnimGraph/AnimGraph`
- output: `/Body/AnimGraph/AnimGraph/OutputPose`
- unsupported future node diagnostics: `/Body/AnimGraph/AnimGraph/Nodes/<Id>`

The root-only pilot reports unchanged when desired canonical JSON equals the extracted canonical JSON and changed otherwise.

## 7. Compile/Rebuild Hook

The pilot does not materialize UE pose nodes. Apply marks the region handled but does not rebuild AnimGraph structure. The ABP profile post-apply repair remains responsible for compile/dirty behavior after any applied region.

When real pose nodes land, the adapter must own graph rebuild and report compile failures through `/Body/AnimGraph`.

## 8. Deferred Boundaries

Still deferred after this pilot:

- SequencePlayer, BlendSpacePlayer, cached poses, slot nodes.
- StateMachine references.
- AnimLayers.
- ParentAssetOverrides.

Adding any of those requires extending this public adapter or writing a sibling public adapter spec first.

## 9. State Machine Adapter Boundary

`Body.StateMachines` and `Body.TransitionGraphs` are owned by `FAssetDocumentAnimStateMachineRegionAdapter`, not by `FAnimBlueprintAssetDocumentCapability` private parsing code.

The first state-machine milestone supports stable authored identities and semantic diff paths:

```json
[
  {
    "Name": "Locomotion",
    "EntryState": "Idle",
    "States": [{"Id": "Idle"}, {"Id": "Run"}],
    "Transitions": [{"Id": "IdleToRun", "From": "Idle", "To": "Run", "Rule": "IdleToRun"}]
  }
]
```

The adapter must validate:

- state-machine identity: `Name`, duplicate check case-insensitive.
- state identity: `States[].Id`, duplicate check within the owning state machine.
- transition identity: `Transitions[].Id`, duplicate check within the owning state machine.
- transition endpoints: `From` and `To` must reference states in the same state machine.

Diff paths must be semantic:

- state machine: `/Body/StateMachines/<Name>`
- state: `/Body/StateMachines/<Name>/States/<Id>`
- transition: `/Body/StateMachines/<Name>/Transitions/<Id>`

The current milestone does not yet materialize `UAnimationStateMachineGraph`, `UAnimStateNode`, or `UAnimStateTransitionNode`; it establishes the identity contract that real nested graph materialization must preserve.

## 10. Transition Graph Boundary

`Body.TransitionGraphs` uses stable `(StateMachine, Transition)` identity:

```json
[
  {
    "StateMachine": "Locomotion",
    "Transition": "IdleToRun",
    "Nodes": [],
    "Result": {"Node": null, "Pin": "CanEnterTransition"}
  }
]
```

The first milestone supports only the root-only transition rule graph. Any authored rule node must fail with:

- path: `/Body/TransitionGraphs/<StateMachine>/<Transition>/Nodes/<Index>`
- code: `UnsupportedTransitionGraphNode`

Diff paths must be semantic:

- transition graph: `/Body/TransitionGraphs/<StateMachine>/<Transition>`

Adding bool literal, time remaining, sync marker, or custom blend graph nodes requires extending this public adapter or adding a sibling public graph adapter spec. The ABP capability may register and compose that adapter, but must not grow a transition graph parser.
