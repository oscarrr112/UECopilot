# Tool: `validate_blueprint_json`

## Purpose

Local structural validation for blueprint JSON before apply.

## Required Arguments

- `blueprint_json` (string)

## Validation Scope

- root must be JSON object
- required fields:
  - `name` (string, non-empty)
  - `parent_class` (string, non-empty)
- if present, fields must be arrays:
  - `variables`
  - `functions`
  - `event_graphs`

## Result Content

JSON string:
```json
{
  "valid": true,
  "errors": []
}
```
