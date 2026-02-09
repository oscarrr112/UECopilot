---
name: ue-bp-character-gameplay
description: Generate Pawn or Character blueprint logic for movement, input handling, combat checks, stamina/health gating, and ability execution flow. Use when parent class is Pawn or Character and request involves player-controlled gameplay behavior.
---

# UE Blueprint Character Gameplay

Separate input, state checks, and action execution.

Typical requirements:
- Input-driven action dispatch
- Movement/combat condition checks
- Resource gates (health, stamina, cooldown)

Recommended graph structure:
1. Event/input entry.
2. Guard branches (`can act?`, `resource > 0?`).
3. Action path (set state, trigger call, update resource).
4. Failure path (return or no-op).

Quality rules:
- Keep combat predicates in dedicated functions for reuse.
- Use clear branch naming via node ids (`can_attack_branch`, `stamina_check`).
- Ensure both success and failure execution paths terminate predictably.

Use script:
- `scripts/new_character_gate_request.ps1 -BlueprintName "<bp>" [-ActionName Attack] [-ResourceVariable Stamina] [-MinimumRequired 1]`
