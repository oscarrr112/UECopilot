# InputAction Generator Schema

Creates Enhanced Input Action assets.

## Top-Level Fields

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `ValueType` | string | No | `"Boolean"` | Input value type (case-insensitive) |
| `Triggers` | array | No | | Array of trigger type name strings |
| `Modifiers` | array | No | | Array of modifier type name strings |

### ValueType Values
- `"Boolean"` - On/off binary input (button press)
- `"Axis1D"` - Single-axis analog (e.g. trigger)
- `"Axis2D"` - Two-axis analog (e.g. thumbstick)
- `"Axis3D"` - Three-axis analog

### Trigger Values (case-insensitive)
- `"Down"` - While key is held down
- `"Pressed"` - On key press
- `"Released"` - On key release
- `"Hold"` - After holding for a duration
- `"Tap"` - Quick press
- `"Pulse"` - Repeating while held

### Modifier Values (case-insensitive)
- `"Negate"` - Invert input value
- `"Swizzle"` or `"SwizzleAxis"` - Rearrange axis components
- `"Scalar"` - Multiply by scalar
- `"DeadZone"` - Ignore input below threshold
- `"Smooth"` - Smooth/dampen input

## Example

```json
{
  "AssetType": "InputAction",
  "Name": "IA_Move",
  "Path": "/Game/Input",
  "ValueType": "Axis2D",
  "Triggers": ["Down"],
  "Modifiers": ["DeadZone", "Smooth"]
}
```
