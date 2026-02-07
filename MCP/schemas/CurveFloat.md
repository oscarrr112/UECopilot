# CurveFloat Generator Schema

Creates float curve assets with keyframes.

## Top-Level Fields

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `Keys` | array | No | | Array of curve keyframes |

## Keys Array

Each key object:

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `Time` | number | No | `0.0` | Time position on the curve |
| `Value` | number | No | `0.0` | Float value at this time |
| `InterpMode` | string | No | `"Linear"` | Interpolation mode (case-insensitive) |

### InterpMode Values
- `"Linear"` - Linear interpolation between keys
- `"Cubic"` - Smooth cubic interpolation
- `"Constant"` - Step/hold (no interpolation)

## Example

```json
{
  "AssetType": "CurveFloat",
  "Name": "C_FadeIn",
  "Path": "/Game/Curves",
  "Keys": [
    { "Time": 0, "Value": 0, "InterpMode": "Cubic" },
    { "Time": 0.3, "Value": 0.8, "InterpMode": "Cubic" },
    { "Time": 1.0, "Value": 1.0 }
  ]
}
```
