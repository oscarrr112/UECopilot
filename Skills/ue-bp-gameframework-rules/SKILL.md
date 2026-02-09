---
name: ue-bp-gameframework-rules
description: Generate GameMode, GameState, PlayerController, and PlayerState blueprint logic for match rules, win/lose conditions, authority-side flow, and player lifecycle handling. Use when request targets core game-framework rule orchestration.
---

# UE Blueprint GameFramework Rules

Model rules as explicit state transitions and checks.

Typical requirements:
- Match start/end conditions
- Score/lives updates
- Controller-driven commands
- Replicated state decisions

Recommended approach:
1. Define rule-check functions returning bool/int state signals.
2. Call them from event triggers (BeginPlay, custom rule events).
3. Gate transitions with `Flow_Branch`.
4. Update authoritative variables in a single controlled path.

Quality rules:
- Keep rule predicates isolated from mutation nodes.
- Keep one authoritative update path per rule to avoid desync logic.
- Prefer small composable functions over large monolithic event graphs.

Use script:
- `scripts/new_rule_request.ps1 -BlueprintName "<bp>" -RuleName "<rule>" [-ConditionVariable Score] [-Threshold 10]`
