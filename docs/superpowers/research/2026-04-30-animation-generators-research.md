# Animation Generators Research

- Date: 2026-04-30
- Branch: `feature/animation-generators-specs`
- Scope: AnimationBlueprint, BlendSpace, AimOffset, and animation graph authoring language research
- Status: Research summary for spec planning; no implementation in this branch

## Goal

The next generator family should cover animation authoring assets in a way that remains useful to agents. The target is not just creating an empty `UAnimBlueprint`, but eventually generating an Animation Blueprint that can express pose graphs, state machines, transition rules, and ordinary Blueprint logic while still using the existing AssetFactory JSON entry point.

The key design decision from brainstorming is:

- keep top-level AssetFactory input as JSON;
- keep ordinary Blueprint variables compatible with the existing `BlueprintGenerator` contract;
- use animation-specific source blocks for AnimGraph and StateMachine authoring;
- reuse BSL concepts for ordinary Blueprint/EventGraph/function logic where possible;
- normalize all graph source into internal semantic IR before touching UE graphs.

## Current Project Baseline

Relevant existing generator patterns:

- `IAssetGenerator` remains the common interface: `GetAssetType`, `Generate`, `ValidateConfig`, `CanExtract`, `Extract`, and priority.
- Simple asset generators such as curves use `AssetTools.CreateAsset` with a factory, then mark dirty and save.
- `BlueprintGenerator` already supports general Blueprint variables and CDO default properties.
- `StateTreeGenerator` is the best pattern for a complex generator: lifecycle code stays in the main generator, while parsing/building/extraction helpers live in a focused subdirectory.
- Existing BSL separates language parsing from UE graph creation: source -> parser -> AST/compiler -> graph writer. Animation graph work should preserve this separation.

Important existing Blueprint variable contract:

```json
"Variables": [
  { "Name": "Speed", "Type": "Float", "DefaultValue": "0.0" },
  { "Name": "bIsInAir", "Type": "Bool", "DefaultValue": "false" }
]
```

`AnimationBlueprintGenerator` should reuse this shape instead of inventing `Default` or a second variable declaration format. Extraction may emit engine spellings such as `Boolean`; generation should accept both `Bool` and `Boolean` as aliases when they map to the same Blueprint pin/category.

## BlendSpace And AimOffset Findings

BlendSpace assets should be their own generator, independent of AnimationBlueprint. They are animation assets that an Animation Blueprint later references.

Creation path:

```cpp
UBlendSpaceFactoryNew* Factory = NewObject<UBlendSpaceFactoryNew>();
Factory->TargetSkeleton = Skeleton;
Factory->PreviewSkeletalMesh = PreviewMesh;

UBlendSpace* BlendSpace = Cast<UBlendSpace>(
    AssetTools.CreateAsset(Name, Path, UBlendSpace::StaticClass(), Factory)
);
```

Factory variants:

- `UBlendSpaceFactoryNew` -> `UBlendSpace`
- `UBlendSpaceFactory1D` -> `UBlendSpace1D`
- `UAimOffsetBlendSpaceFactoryNew` -> `UAimOffsetBlendSpace`
- `UAimOffsetBlendSpaceFactory1D` -> `UAimOffsetBlendSpace1D`

Useful public API and fields:

- `FBlendParameter` describes axis display name, min/max, grid count, snap, and wrap.
- `FBlendSample` stores `UAnimSequence* Animation`, `SampleValue`, `RateScale`, and single-frame settings.
- `UBlendSpace::AddSample(UAnimSequence*, FVector)` adds samples.
- `UBlendSpace::ValidateSampleData()` validates sample data.
- `UBlendSpace::ResampleData()` rebuilds internal sample data.

AimOffset should share the same generator and JSON shape as BlendSpace, with stricter validation. AimOffset samples must be mesh-space rotation offset additive animations.

Recommended dependency order:

```text
Skeleton / SkeletalMesh / AnimSequence -> BlendSpace / AimOffset -> AnimationBlueprint
```

## AnimationBlueprint Findings

`AnimationBlueprintGenerator` should not reuse the generic `BlueprintGenerator` as its implementation path. A plain Blueprint with `ParentClass = AnimInstance` is not enough, because a real `UAnimBlueprint` needs skeleton-aware creation and the animation compiler path.

Official creation path:

```cpp
UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
Factory->ParentClass = UAnimInstance::StaticClass();
Factory->TargetSkeleton = Skeleton;
Factory->PreviewSkeletalMesh = PreviewMesh;

UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(
    AssetTools.CreateAsset(Name, Path, UAnimBlueprint::StaticClass(), Factory)
);
```

The factory internally calls `FKismetEditorUtilities::CreateBlueprint(..., UAnimBlueprint::StaticClass(), ...)` and then writes `TargetSkeleton` onto the blueprint, generated class, and skeleton generated class.

Compile path:

```cpp
FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
```

The AnimGraph module registers the specialized compiler for `UAnimBlueprint`. Save can use the same package save pattern as existing generators, or editor save helpers if needed.

Creation automatically gives the asset default EventGraph and AnimGraph scaffolding. AnimGraph graph mutation must use UE graph schema and node lifecycle rather than manually filling arrays.

## AnimGraph And StateMachine Complexity

AnimGraph and StateMachine generation is not a flat node list problem.

Important UE behavior:

- `UAnimationGraphSchema::CreateDefaultNodesForGraph` creates the AnimGraph root node.
- `UAnimationGraphSchema::SpawnNodeFromAsset` handles animation asset node spawning and skeleton compatibility, but mismatch handling can be a silent no-op, so the generator should prevalidate assets against the target skeleton or verify that a node was actually created.
- State machine nodes create nested `UAnimationStateMachineGraph` subgraphs and default entry nodes during node placement.
- State nodes create `UAnimationStateGraph` subgraphs with state result nodes.
- Transition nodes create `UAnimationTransitionGraph` subgraphs with transition result nodes.
- State machine schema can auto-insert transition nodes when connecting states.

Therefore, generator code should call schema/actions/node lifecycle APIs where possible. It should not hand-write graph arrays, pins, or subgraphs as if they were stable data.

## Language And Contract Decision

For an agent-facing tool, the best contract is not a single unified human language. The best contract is explicit, isolated source blocks:

- top-level JSON for asset shell and common Blueprint metadata;
- `AnimGraph` block for pose graph expression;
- `StateMachines[]` blocks for state machine expression;
- `BlueprintGraphs[]` blocks using `BSLFragment` for ordinary K2/EventGraph/function logic.

This keeps errors local. A parser error can point to `AnimGraph.Source`, `StateMachines[0].Source`, or `BlueprintGraphs[0].Source`.

`BSLFragment` is intentionally named as a fragment contract. The current BSL parser expects a complete `blueprint ... extends ... { ... }` wrapper, so Animation Blueprint integration should wrap/reroute these blocks before invoking existing BSL infrastructure instead of pretending naked event snippets are accepted today.

YAML is not recommended as a primary design axis. It changes surface syntax but does not solve graph semantics. If a YAML frontend is ever useful, it should compile to the same canonical JSON/IR outside the UE-side generator.

UE clipboard text (`FEdGraphUtilities::ExportNodesToText` / `ImportNodesFromText`) is useful for debugging or fallback import/export research, but it is too close to internal UObject text to be the long-term authoring format.

## Recommended Spec Direction

The animation generator family should be split into small, independently verifiable specs:

- Animation generator spec map and language architecture.
- BlendSpace/AimOffset generator.
- AnimationBlueprint lifecycle generator.
- Canonical AnimGraph IR and minimal pose graph builder.
- StateMachine source/IR and transition rule builder.
- BSLFragment integration for Animation Blueprint EventGraph/functions.
- Advanced animation nodes and raw-node escape hatch.
- Extraction, round-trip, MCP docs, and fixtures.

This keeps BlendSpace useful immediately, keeps AnimationBlueprint lifecycle verifiable before graph complexity, and gives the graph language space to stabilize before implementation.
