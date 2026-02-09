# Blueprint Logic Scenario Matrix

## By Parent Class

- `Actor` or `ActorComponent`
  - Overlap/trigger reactions
  - World interaction checks
  - Object-local helper functions
- `Pawn` or `Character`
  - Input -> condition -> action flow
  - Movement/combat gating
  - Resource-driven action logic
- `UserWidget`
  - Button/event handlers
  - Display transformation and toggles
  - Data-driven UI state
- `GameMode` / `GameState` / `PlayerController` / `PlayerState`
  - Rule orchestration and lifecycle events
  - Win/lose progression
  - Controller command dispatch

## By Requirement Type

- Function-focused
  - Deterministic return-value behavior
  - Branch math/compare transforms
  - Reusable predicate helpers
- EventGraph-focused
  - Runtime triggers and sequencing
  - Tick/BeginPlay/custom event chains
  - Graph-level state transitions
- Hybrid
  - EventGraph entry + function predicates + function mutators

## Node-Level Risk Hotspots

- Missing referenced entry node ids (`fn_entry`, `event_*`)
- Unsupported node type aliases
- Invalid exec links to pure nodes
- Multiple return paths collapsing into one result node
- Disordered layout reducing maintainability

