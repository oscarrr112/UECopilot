# WidgetBlueprint Generator Schema

Creates UMG Widget Blueprint assets with a widget tree, slot positioning, styles, and property bindings.

## Top-Level Fields

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `RootWidget` | object | **Yes** | | Root widget node definition (typically CanvasPanel) |
| `ParentClass` | string | No | `"UserWidget"` | Parent widget class |
| `ClassDefaults` | object | No | | CDO properties applied after compilation |

## Widget Node Fields

Each widget node (including RootWidget):

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `Type` | string | **Yes** | | Widget class: `"CanvasPanel"`, `"VerticalBox"`, `"HorizontalBox"`, `"TextBlock"`, `"Image"`, `"Button"`, `"ProgressBar"`, `"Border"`, etc. |
| `Name` | string | No | Auto-generated | Widget name (auto: `{Type}_{Counter}`) |
| `Action` | string | No | `"CreateOrUpdate"` | `"CreateOrUpdate"`, `"Create"`, `"Update"`, or `"Remove"` |
| `Children` | array | No | | Child widget nodes (for PanelWidget types) |
| `Slot` | object | No | | Slot/layout configuration (see Slot Fields) |
| `Style` | object | No | | Style properties (see Style Fields) |
| `Properties` | object | No | | Widget properties via reflection |
| `IsVariable` | bool | No | `false` | Expose as blueprint variable |
| `VariableName` | string | No | Same as `Name` | Custom variable name |
| `Bindings` | object | No | | Property bindings (auto-sets IsVariable=true) |

### ContentWidget vs PanelWidget
- **PanelWidget** (CanvasPanel, VerticalBox, HorizontalBox, etc.): supports multiple Children
- **ContentWidget** (Button, Border, ScaleBox, etc.): only first child in Children is used
- Detection is automatic via `SetContent()` / `GetContentSlot()` function lookup

### Widget Instantiation (Embedding Other Widget Blueprints)
```json
{
  "Type": "UserWidget",
  "Properties": {
    "WidgetClass": "/Game/UI/WBP_HealthBar.WBP_HealthBar_C"
  }
}
```

## Slot Fields

### CanvasPanel Slot (Direct API)

For children of CanvasPanel:

| Field | Type | Description |
|-------|------|-------------|
| `Anchors` | object | Anchor points (see formats below) |
| `Offsets` | array/number | Margin offsets `[Left, Top, Right, Bottom]` or uniform number |
| `Alignment` | array | Pivot point `[X, Y]` range 0-1 |
| `Position` | array | Convenience: `[X, Y]` sets Offsets.Left and Offsets.Top |
| `Size` | array | Convenience: `[X, Y]` sets Offsets.Right and Offsets.Bottom |
| `SizeToContent` | bool | Auto-size widget to its content |
| `ZOrder` | number | Layer order (integer) |

#### Anchors Format (two supported formats)

**Object format:**
```json
{
  "Anchors": {
    "Minimum": { "X": 0.5, "Y": 0.5 },
    "Maximum": { "X": 0.5, "Y": 0.5 }
  }
}
```

**Array format:**
```json
{
  "Anchors": {
    "Min": [0.5, 0.5],
    "Max": [0.5, 0.5]
  }
}
```

Both `Min`/`Minimum` and `Max`/`Maximum` are accepted.

#### Common Anchor Presets
| Preset | Min | Max | Alignment |
|--------|-----|-----|-----------|
| Top-Left | [0, 0] | [0, 0] | [0, 0] |
| Top-Center | [0.5, 0] | [0.5, 0] | [0.5, 0] |
| Top-Right | [1, 0] | [1, 0] | [1, 0] |
| Center | [0.5, 0.5] | [0.5, 0.5] | [0.5, 0.5] |
| Bottom-Center | [0.5, 1] | [0.5, 1] | [0.5, 1] |
| Stretch Full | [0, 0] | [1, 1] | [0, 0] |

### Other Slot Types (Reflection-based)

For children of VerticalBox, HorizontalBox, GridPanel, etc.:

| Field | Type | Description |
|-------|------|-------------|
| `Padding` | array/number | `[Left, Top, Right, Bottom]` or uniform number |
| `HAlign` or `HorizontalAlignment` | string | `"Left"`, `"Center"`, `"Right"`, `"Fill"` |
| `VAlign` or `VerticalAlignment` | string | `"Top"`, `"Center"`, `"Bottom"`, `"Fill"` |
| `Size` | string/object | `"Fill"` or `"Auto"`, or `{"SizeRule": "Fill"}` |
| `SizeToContent` or `AutoSize` | bool | Auto-size |
| `Row` | number | Grid row index |
| `Column` | number | Grid column index |
| `RowSpan` | number | Grid row span |
| `ColumnSpan` | number | Grid column span |

## Style Fields

| Field | Type | Description |
|-------|------|-------------|
| `Color` | various | Widget color (see Color Format) |
| `Brush` | object | Brush configuration (see Brush Fields) |
| `Font` | object | Font configuration: `{"Size": 24}` |
| *(any other key)* | various | Applied via reflection as widget property |

### Color Format (multiple accepted)
- Hex string: `"#FF0000"`, `"#RRGGBBAA"`
- Named: `"White"`, `"Black"`, `"Red"`, `"Green"`, `"Blue"`, `"Yellow"`, `"Transparent"`
- Array: `[R, G, B, A]` (0.0-1.0)
- Object: `{"R": 1.0, "G": 0.0, "B": 0.0, "A": 1.0}`

### Brush Fields

```json
{
  "Brush": {
    "Image": "/Game/Textures/MyTexture",
    "ImageSize": [128, 128],
    "Tint": "#FF0000",
    "DrawAs": "Image"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `Image` or `ResourceObject` | string | Asset path (Texture2D, Material, or any UObject) |
| `ImageSize` | array | `[Width, Height]` in pixels |
| `Tint` | color | Tint color (same formats as Color) |
| `DrawAs` | string | `"Box"`, `"Image"`, `"Border"`, `"NoDrawType"` |

## Bindings Format

Widget-level property bindings:

```json
{
  "Bindings": {
    "Text": "GetDisplayText",
    "Visibility": {
      "Function": "GetTextVisibility",
      "Kind": "Function"
    }
  }
}
```

- Simple: `"PropertyName": "FunctionName"`
- Full: `"PropertyName": {"Function": "FunctionName", "Kind": "Function"}`
- Property binding: `"PropertyName": {"Property": "SourceProperty", "Kind": "Property"}`
- Functions validated against C++ base class and blueprint graphs
- Auto-sets `IsVariable: true`

## Complete Example

```json
{
  "AssetType": "WidgetBlueprint",
  "Name": "WBP_HUD",
  "Path": "/Game/UI",
  "RootWidget": {
    "Type": "CanvasPanel",
    "Name": "Root",
    "Children": [
      {
        "Type": "TextBlock",
        "Name": "ScoreText",
        "Slot": {
          "Anchors": { "Min": [0.5, 0.05], "Max": [0.5, 0.05] },
          "Alignment": [0.5, 0],
          "SizeToContent": true
        },
        "Style": {
          "Font": { "Size": 36 },
          "Color": "#FFD700"
        },
        "Properties": { "Text": "Score: 0" }
      },
      {
        "Type": "Image",
        "Name": "HealthBarBg",
        "Slot": {
          "Anchors": { "Min": [0, 1], "Max": [0, 1] },
          "Alignment": [0, 1],
          "Offsets": [20, -20, 200, 30]
        },
        "Style": {
          "Brush": {
            "Image": "/Game/Materials/M_HealthBar",
            "ImageSize": [200, 30]
          }
        }
      },
      {
        "Type": "Button",
        "Name": "MenuBtn",
        "IsVariable": true,
        "Slot": {
          "Anchors": { "Min": [1, 0], "Max": [1, 0] },
          "Alignment": [1, 0],
          "SizeToContent": true
        },
        "Children": [
          {
            "Type": "TextBlock",
            "Name": "MenuBtnText",
            "Style": { "Font": { "Size": 18 } },
            "Properties": { "Text": "Menu" }
          }
        ]
      }
    ]
  }
}
```
