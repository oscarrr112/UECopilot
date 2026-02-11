# Tool: `repair_blueprint_json`

## Purpose

Attempt to repair malformed AI output into a valid single JSON blueprint object.

## Required Arguments

- `endpoint` (string)
- `model` (string)
- `source_text` (string)

## Optional Arguments

- `api_key` (string)
- `timeout_seconds` (number, default `90`)
- `max_tokens` (number, default `4096`)
- `temperature` (number, default `0.2`)

## Result

- `result.content` returns repaired JSON text (or best-effort text from model).
