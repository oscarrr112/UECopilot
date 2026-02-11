# Tool: `orchestrate_modify_request`

## Purpose

Two-step orchestration for `/modify`:
1. Run generation
2. If output is not valid JSON object, run repair

## Arguments

Same generation args:
- `endpoint`
- `model`
- `messages`
- optional `api_key`, `timeout_seconds`, `max_tokens`, `temperature`

## Behavior

- If first generation contains a valid JSON object, return it directly.
- Otherwise invoke repair flow and return repaired content.

## Result

- `result.content` text intended for blueprint parser consumption.
