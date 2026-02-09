---
name: ue-bp-actor-interaction
description: Generate Actor and ActorComponent blueprint logic for world interactions, overlap triggers, object state changes, and utility gameplay functions. Use when parent class is Actor or ActorComponent and behavior is interaction-oriented.
---

# UE Blueprint Actor Interaction

Implement interaction behavior with clear trigger-action flow.

Typical requirements:
- Overlap start/end reactions
- Trigger-driven variable updates
- Object-local helper functions (cooldowns, checks, toggles)

Recommended node strategy:
- Event side: BeginPlay, custom events, overlap-related flow
- Data side: `Variable_Get/Set`, compare, math
- Decision side: `Flow_Branch`
- Encapsulate repeated checks into reusable functions

Quality rules:
- Keep mutable state in named variables with explicit defaults.
- Isolate world-trigger entry points from pure calculation functions.
- Return early in functions for invalid conditions to reduce graph complexity.

Use script:
- `scripts/new_actor_interaction_request.ps1 -BlueprintName "<bp>" [-Trigger BeginPlay] [-Action "set variable"] [-Variable bIsActive]`
