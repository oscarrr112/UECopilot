# Tool: `generate_blueprint_change`

## Purpose

Alias of `chat_completion`, intended for blueprint JSON generation prompts.

## Arguments

Same as `chat_completion`:
- `endpoint`
- `model`
- `messages`
- optional `api_key`, `timeout_seconds`, `max_tokens`, `temperature`

## Result

- `result.content` should contain generated blueprint JSON text (or fenced JSON text).
