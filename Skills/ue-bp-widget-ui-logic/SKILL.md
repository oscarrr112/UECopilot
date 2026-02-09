---
name: ue-bp-widget-ui-logic
description: Generate UserWidget blueprint logic for UI events, display state updates, data-to-view transformations, and button-driven actions. Use when parent class is UserWidget or the request focuses on HUD/menu interaction behavior.
---

# UE Blueprint Widget UI Logic

Drive UI with event handlers plus small pure functions.

Typical requirements:
- Button click handling
- Value formatting and clamping
- Visibility/state toggles

Recommended split:
- EventGraph for UI event handling and state mutation.
- Functions for formatting and conditional display calculations.

Quality rules:
- Keep UI mutation in event handlers.
- Keep calculation/formatting pure where possible.
- Avoid long chains in a single click handler; factor into helper functions.

Use script:
- `scripts/new_widget_handler_request.ps1 -BlueprintName "<bp>" [-EventName OnClicked] [-DisplayVariable Score] [-Behavior "update view state"]`
