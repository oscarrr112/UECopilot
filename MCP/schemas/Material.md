# Material Generator Schema

Creates UI Material assets from predefined templates. All materials are created with `MD_UI` domain, `Translucent` blend mode, and `Unlit` shading.

## Update Behavior (Action: "Update")

When `Action` is `"Update"`, `Template` becomes optional. If `Template` is provided, the material's node graph is cleared and rebuilt from scratch using that template (with optional `Parameters`). If `Template` is omitted, the existing node graph is preserved unchanged and the material is re-saved as-is. Note: `Parameters` are construction-time settings consumed by the template builder, so updating parameters requires providing `Template` as well to trigger a rebuild.

## Top-Level Fields

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `Template` | string | **Yes** (Create) / No (Update) | | Template name (see Available Templates) |
| `Parameters` | object | No | | Template-specific parameters |

## Available Templates

### CircularProgress
Circular progress indicator (like a radial timer). Progress sweeps clockwise from top.

**Material Parameters (runtime):**
- `Progress` (Scalar, 0-1): Fill amount. Default: 0.5

**JSON Parameters:** None currently parsed from config (uses hardcoded defaults for colors).

**Built-in defaults:**
- Fill color: golden (0.83, 0.66, 0.33)
- Background color: dark gray (0.05, 0.05, 0.08)
- Fill opacity: 1.0, Background opacity: 0.8

---

### CooldownSweep
Circular cooldown overlay (darkens filled area). Sweeps clockwise from top.

**Material Parameters (runtime):**
- `Progress` (Scalar, 0-1): Cooldown amount. Default: 0.5
- `OverlayColor` (Vector): Overlay RGB color. Default: black (0,0,0)
- `OverlayOpacity` (Scalar, 0-1): Overlay opacity. Default: 0.7

**JSON Parameters:** None currently parsed from config.

---

### GradientFill
Horizontal linear gradient between two colors.

**Material Parameters (runtime):**
- `StartColor` (Vector): Left color
- `EndColor` (Vector): Right color
- `Opacity` (Scalar): Overall opacity

**JSON Parameters:**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `StartColor` | array | `[0.55, 0.41, 0.08, 1]` | Start color `[R, G, B]` or `[R, G, B, A]` |
| `EndColor` | array | `[0.96, 0.83, 0.52, 1]` | End color `[R, G, B]` or `[R, G, B, A]` |
| `Opacity` | number | `1.0` | Overall opacity (0-1) |

---

### HealthBarFill
Horizontal fill bar with shine effect. Fills from left to right.

**Material Parameters (runtime):**
- `FillPercent` (Scalar, 0-1): Fill amount. Default: 1.0
- `HealthColor` (Vector): Bar color. Default: green (0.29, 0.87, 0.5)

**JSON Parameters:** None currently parsed from config.

## Examples

```json
{
  "AssetType": "Material",
  "Name": "M_Gradient",
  "Path": "/Game/Materials",
  "Template": "GradientFill",
  "Parameters": {
    "StartColor": [0.1, 0.1, 0.8],
    "EndColor": [0.8, 0.1, 0.1],
    "Opacity": 0.9
  }
}
```

```json
{
  "AssetType": "Material",
  "Name": "M_CircleProgress",
  "Path": "/Game/Materials",
  "Template": "CircularProgress"
}
```
