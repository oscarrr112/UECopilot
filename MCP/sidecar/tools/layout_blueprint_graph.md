# Tool: `layout_blueprint_graph`

## Purpose

Apply deterministic node positioning for blueprint logic graphs to improve readability:

- left-to-right dependency flow
- non-overlapping node placement
- stable coordinates for repeated runs

## Required Arguments

- `blueprint_json` (string)

## Optional Arguments

- `horizontal_spacing` (number, default: `420`)
- `vertical_spacing` (number, default: `220`)
- `start_x` (number, default: `0`)
- `start_y` (number, default: `0`)
- `graph_gap_y` (number, default: `700`)

## Result Content

JSON string of the same blueprint object with `position` filled for graph nodes.
