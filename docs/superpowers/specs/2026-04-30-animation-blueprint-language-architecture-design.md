# Animation Blueprint Language Architecture Design

**Date:** 2026-04-30
**Status:** Draft for user review
**Branch:** `feature/animation-generators-specs`
**Depends on:** Animation generator spec map

---

## Goal

Design the authoring contract for Animation Blueprint graph generation so agents can express AnimGraph, StateMachine, and ordinary Blueprint logic without writing low-level UE graph JSON.

The selected architecture is:

- top-level AssetFactory JSON for asset metadata and orchestration;
- `AnimGraphDSL` for pose graph source;
- `AnimStateMachineDSL` for state machine source;
- `BSLFragment` for ordinary Blueprint/EventGraph/function logic;
- canonical internal IR for generation, validation, extraction, and tests.

## Design Position

Animation Blueprint is not one graph model. It contains at least three different semantics:

- pose graph semantics in AnimGraph;
- state machine and transition graph semantics;
- ordinary K2 Blueprint logic for events and functions.

Combining these into one user-facing language would make the language large and error-prone for agents. Splitting them into source blocks gives better diagnostics and clearer ownership:

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

The source blocks compile into internal IR first. Only validated IR is allowed to mutate UE graphs.

## Non-Goals

This design does not define final syntax for every advanced animation node.

This design does not replace the existing Blueprint generator variable contract. AnimationBlueprint variables use the same shape as Blueprint generator variables:

```json
{ "Name": "Speed", "Type": "Float", "DefaultValue": "0.0" }
```

Generation should accept both `Bool` and `Boolean` as aliases for Blueprint bool variables, because extraction may report engine-facing spellings even when authored input used the shorter BlueprintGenerator style.

This design does not use YAML as a primary contract. YAML can be an optional frontend later, but it should compile to the same canonical IR and not change the UE-side generator.

This design does not expose UE clipboard text as the main source language. Clipboard text can be useful for debug import/export, but it is too coupled to internal node object serialization.

## Contract Shape

### Top-Level AnimationBlueprint JSON

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

### Canonical IR

Source blocks normalize to canonical IR before graph mutation. The IR is not required to match exact user input syntax. It should be stable enough for tests and extraction.

Example AnimGraph IR:

```json
{
  "output": { "ref": "Locomotion", "kind": "stateMachine" }
}
```

Example StateMachine IR:

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

## Language Boundaries

### AnimGraphDSL

AnimGraphDSL owns pose graph composition:

```text
output = machine Locomotion
```

Initial expressions:

```text
output = sequence("/Game/Anim/Idle.Idle")
output = blendspace("/Game/Anim/BS_Locomotion.BS_Locomotion", Speed, Direction)
output = machine Locomotion
output = cached_pose LocomotionPose
```

Future expressions:

```text
cached_pose LocomotionPose = machine Locomotion
output = slot "DefaultSlot" source LocomotionPose
output = layered_blend(base LocomotionPose, upper AttackPose, bone "spine_01")
output = raw_node("/Script/AnimGraph.AnimGraphNode_Custom", properties { })
```

AnimGraphDSL validates:

- referenced variables exist in top-level `Variables` or parent class;
- referenced BlendSpace/Animation assets load and match target skeleton;
- referenced state machines exist;
- pose expressions produce pose outputs.

### AnimStateMachineDSL

AnimStateMachineDSL owns states and transitions. The top-level `StateMachines[].Name` value is the authoritative machine name. A future optional DSL header may repeat the name for readability, but a mismatch should be a validation error.

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

Future transition options:

```text
transition Idle -> Move when Speed > 5 {
  blend = 0.2
  priority = 1
}
```

StateMachineDSL validates:

- unique state names;
- exactly one entry state unless defaulting is explicitly allowed;
- transition endpoints exist;
- condition variables must exist; helper functions may remain unresolved until BSLFragment integration satisfies them;
- pose expressions can compile through the AnimGraph pose expression parser.

### BSLFragment For BlueprintGraphs

`BSLFragment` owns ordinary Blueprint logic. It is a fragment contract for `BlueprintGraphs[]`; the current BSL parser expects a complete `blueprint ... extends ... { ... }` wrapper, so the Animation Blueprint integration layer must wrap and route fragments before invoking BSL infrastructure. Animation-specific integration should allow:

- `EventGraph`
- `BlueprintUpdateAnimation`
- helper functions that transition rules can call
- assignment to Blueprint variables declared in top-level JSON

Example:

```text
event BlueprintUpdateAnimation(DeltaTimeX: float) {
  // BSLFragment syntax should follow the existing BSL event/function body contract once integrated.
}
```

The full wrapper passed to today's parser is conceptualized as:

```text
blueprint ABP_Enemy extends AnimInstance {
  event BlueprintUpdateAnimation(DeltaTimeX: float) { }
}
```

The generator should not use BSLFragment to describe pose links. Pose links belong to AnimGraphDSL and StateMachineDSL.

## Error Model

Every diagnostic should include the source block:

- `AnimGraph.Source: line 1, column 10: unknown state machine 'Locomotion'`
- `StateMachines[0].Source: line 5, column 18: unknown variable 'Velocity'`
- `BlueprintGraphs[0].Source: line 2, column 3: BSL parser error: ...`

Generation should fail before UE graph mutation when parsing or semantic validation fails.

UE compile errors should be wrapped with the asset type and source block when the failing graph can be identified.

## UE Graph Builder Strategy

Builders consume canonical IR, not raw source strings.

The AnimGraph builder should:

- find or create the official AnimGraph;
- preserve default graph lifecycle behavior;
- use `UAnimationGraphSchema` and animation node actions where possible;
- connect supported pose nodes to `UAnimGraphNode_Root`;
- avoid pin-level JSON as a user contract.

The StateMachine builder should:

- spawn the state machine node through UE graph/node lifecycle APIs;
- let state machine nodes create their child graphs;
- create state nodes and transition nodes through schema actions or lifecycle APIs;
- build transition rule graphs from validated condition IR.

The BSLFragment integration layer should:

- route BSLFragment to the requested EventGraph or function graph;
- reuse existing BSL parser/compiler concepts;
- adapt graph lookup and node placement for `UAnimBlueprint`.

## Extraction

Extraction should prefer canonical IR over source reconstruction.

Initial extraction can return:

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

Future extraction may additionally emit generated DSL when the supported graph shape is simple enough. Full source-preserving round-trip is not required for the first graph specs.

## Recommended First Implementation Sequence

1. Implement `BlendSpace` and `AimOffset` generator first.
2. Implement `AnimationBlueprint` lifecycle without complex graph source.
3. Implement canonical minimal AnimGraph IR and graph builder.
4. Add `AnimGraphDSL` parser as a frontend to that IR.
5. Add `AnimStateMachineDSL` and transition rules.
6. Integrate BSLFragment for `BlueprintGraphs`.

This sequence makes each layer testable before the language grows.
