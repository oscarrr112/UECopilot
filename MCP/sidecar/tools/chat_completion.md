# Tool: `chat_completion`

## Purpose

Forward chat messages to an OpenAI-compatible endpoint and return assistant content.

## Required Arguments

- `endpoint` (string)
- `model` (string)
- `messages` (array)

## Optional Arguments

- `api_key` (string)
- `timeout_seconds` (number, default `90`)
- `max_retries` (number, default `2`)
- `max_tokens` (number, default `4096`)
- `temperature` (number, default `0.7`)

## Result

- `result.content` is assistant text content.

## Failure Cases

- Missing required args
- HTTP/network errors
- Invalid model response structure

If `api_key` is omitted, sidecar attempts environment-based key resolution.
