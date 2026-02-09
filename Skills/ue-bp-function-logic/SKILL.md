---
name: ue-bp-function-logic
description: Generate or modify Blueprint function logic for deterministic input/output behavior, including branch conditions, comparisons, math, and return values. Use when the request focuses on adding or changing functions rather than EventGraph flow.
---

# UE Blueprint Function Logic

Build function graphs as executable nodes.

Required shape:
- Define function in `functions[]`.
- Set signature through `inputs[]` and `outputs[]` when non-void.
- Express implementation through `nodes[]`.

Core pattern:
1. Read variables/inputs.
2. Compute comparisons/math.
3. Branch with `Flow_Branch` when needed.
4. Return through explicit `Return` nodes for each path.

Quality rules:
- Keep branch true/false return paths explicit.
- Prefer explicit literal defaults on unconnected numeric pins.
- Avoid fake exec links into pure nodes (`Variable_Get`).
- Use stable node ids (`get_*`, `cmp_*`, `branch`, `return_*`).

Use script:
- `scripts/new_function_change.ps1 -BlueprintName "<bp>" -FunctionName "<fn>" -VariableName "<var>" [-Threshold 0] [-ParentClass Actor]`
