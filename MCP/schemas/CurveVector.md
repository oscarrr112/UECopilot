# CurveVector Generator Schema

Creates vector curve assets with 3-component (X, Y, Z) keyframes.

## Top-Level Fields

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `Keys` | array | No | | Array of curve keyframes |

## Keys Array

Each key object:

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `Time` | number | No | `0.0` | Time position on the curve |
| `Value` | array | No | `[0, 0, 0]` | 3-component vector `[X, Y, Z]` |
| `InterpMode` | string | No | `"Linear"` | Interpolation mode (case-insensitive) |

### InterpMode Values
- `"Linear"` - Linear interpolation
- `"Cubic"` - Smooth cubic interpolation
- `"Constant"` - Step/hold

## Example

```json
{
  "AssetType": "CurveVector",
  "Name": "C_MovePath",
  "Path": "/Game/Curves",
  "Keys": [
    { "Time": 0, "Value": [0, 0, 0] },
    { "Time": 1, "Value": [100, 0, 50], "InterpMode": "Cubic" },
    { "Time": 2, "Value": [200, 0, 0] }
  ]
}
```
