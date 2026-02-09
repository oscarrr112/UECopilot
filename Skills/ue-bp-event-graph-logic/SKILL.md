---
name: ue-bp-event-graph-logic
description: Generate or modify EventGraph-driven Blueprint logic using BeginPlay, Tick, and custom events with ordered execution chains. Use when the request is trigger-based, timeline-based, or event-reaction behavior.
---

# UE Blueprint EventGraph Logic

Build event-driven flow with explicit execution order.

Required shape:
- Put runtime logic in `event_graphs[]`.
- Prefer `EventGraph` as target graph unless a specific graph is requested.
- Include referenced event nodes (`event_begin`, `event_tick`, custom events).

Core pattern:
1. Choose trigger event.
2. Build execution chain (`execute` -> `then`).
3. Insert branch or sequence for multi-path behavior.
4. Feed data pins from getters and math/compare nodes.

Quality rules:
- Keep event entry explicit and connected to first exec consumer.
- Avoid orphaned nodes not reachable from event entries.
- For Tick modifications, append safely without breaking existing chain.

Use script:
- `scripts/new_event_change.ps1 -BlueprintName "<bp>" -TargetVariable "<var>" [-ParentClass Actor] [-GraphName EventGraph] [-EventNodeId event_begin] [-SetValue 1]`
