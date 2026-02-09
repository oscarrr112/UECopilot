---
name: ue-bp-logic-router
description: Route Unreal Blueprint logic-generation tasks into the correct specialized workflow by parent class and requirement type. Use when the request is broad or mixed, such as "modify selected blueprint logic", and a decision is needed across Actor, Character, Widget, or GameFramework classes.
---

# UE Blueprint Logic Router

Classify the request before generating nodes.

For full categorization details, read `references/scenario-matrix.md`.

Use this matrix:
- Actor or ActorComponent gameplay interactions -> `ue-bp-actor-interaction`
- Pawn or Character movement/combat/input -> `ue-bp-character-gameplay`
- UserWidget UI flow and display logic -> `ue-bp-widget-ui-logic`
- GameMode, GameState, PlayerController, PlayerState rules -> `ue-bp-gameframework-rules`
- Function-only transforms, branch math, return-value behavior -> `ue-bp-function-logic`
- EventGraph-driven state transitions and triggers -> `ue-bp-event-graph-logic`

When routing:
- Preserve existing variables and graph names.
- Prefer additive modifications over destructive rewrites.
- Return only new or changed JSON elements.
- Keep function logic in `functions[].nodes`; do not use pseudo-code fields.

Use scripts:
- `scripts/infer_skill.ps1 -ParentClass "<class>" -Requirement "<text>"` to select skills.
- `scripts/validate_blueprint_json.ps1 -JsonPath "<path>"` to validate JSON before `/apply`.
- `scripts/run_bp_logic_tests.ps1` to run automation tests (`AssetFactoryAI.BlueprintJSON` by default).
- `scripts/verify_skills.ps1` to run a local end-to-end sanity check of all created skills.
- `scripts/run_skill_examples.ps1 [-RunCppTests]` to run concrete per-skill example tests and optional C++ automation.
