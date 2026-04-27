# WidgetBlueprint Generator Schema

Creates UMG Widget Blueprint assets with a widget tree, slot positioning, styles, and property bindings.

## Update Behavior (Action: "Update")

When `Action` is `"Update"`, only fields present in the JSON are processed — missing fields are left unchanged. This enables **field-level patch updates**. Three mutually exclusive update paths:

| JSON Fields Provided | Behavior |
|---------------------|----------|
| `WidgetUpdates` | **Element-level patch (RECOMMENDED)** — add, update, or remove individual widgets safely |
| `RootWidget` + `RebuildTree: true` | **Full rebuild** — DESTROYS entire existing widget tree and rebuilds from scratch |
| Neither | **No-op** — widget tree stays unchanged (useful for only updating ParentClass or ClassDefaults) |

> **WARNING:** Using `RootWidget` in Update mode will **destroy all existing widgets** not listed in the new tree. Always prefer `WidgetUpdates` for incremental changes. If you must do a full rebuild, you must set `"RebuildTree": true` or the request will be rejected.

## Top-Level Fields

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `RootWidget` | object | **Yes** (Create) / No (Update) | | Root widget node definition (typically CanvasPanel). **In Update mode: DESTROYS entire tree and rebuilds — requires `RebuildTree: true`** |
| `WidgetUpdates` | array | No | | **Recommended for Update** — element-level widget patch operations (see WidgetUpdates section below) |
| `RebuildTree` | bool | No | `false` | Required confirmation flag when using `RootWidget` with `Action: "Update"`. Must be `true` to allow full tree rebuild. |
| `ParentClass` | string | No | `"UserWidget"` | Parent widget class |
| `ClassDefaults` | object | No | | CDO properties set via reflection (see ClassDefaults section) |
| `Bindings` | object | No | | Compatibility input for extracted or legacy top-level bindings keyed by `"WidgetName.PropertyName"`. Prefer widget-level `Bindings` for new authoring. Extract now emits widget-level bindings. |

## WidgetUpdates (Element-Level Patch) — Recommended for Updates

Used with `Action: "Update"` to modify individual widgets without rebuilding the entire tree. **This is the safe way to update existing WidgetBlueprints.**

Each entry in the `WidgetUpdates` array:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `Action` | string | No | `"Update"` (default), `"Add"`, `"Remove"`, or `"Move"` |
| `Name` | string | **Yes** (Update/Remove/Move) | Target widget name |
| `Parent` | string | **Yes** (Add only) | Name of parent panel widget to add into |
| `Widget` | object | **Yes** (Add only) | Widget node definition (same format as RootWidget children) |
| `NewParent` | string | **Yes** (Move only) | Name of destination panel widget |
| `NewSlot` | object | No (Move only) | New slot config after move. If omitted, slot is default for the new parent type |
| `Style` | object | No (Update) | Style properties to apply |
| `Properties` | object | No (Update) | Widget properties via reflection |
| `Slot` | object | No (Update) | Slot/layout configuration |
| `IsVariable` | bool | No (Update) | Expose as blueprint variable |
| `Bindings` | object | No (Update) | Property bindings |

`WidgetUpdates[].Bindings` uses the same widget-level binding value format shown in Bindings Format.

### WidgetUpdates Example: Update a Widget

```json
{
  "AssetType": "WidgetBlueprint",
  "Name": "WBP_HUD",
  "Path": "/Game/UI",
  "Action": "Update",
  "WidgetUpdates": [
    {
      "Action": "Update",
      "Name": "ScoreText",
      "Style": { "Font": { "Size": 48 }, "Color": "#FF0000" },
      "Properties": { "Text": "Score: 999" }
    }
  ]
}
```

### WidgetUpdates Example: Add a Widget

```json
{
  "AssetType": "WidgetBlueprint",
  "Name": "WBP_HUD",
  "Path": "/Game/UI",
  "Action": "Update",
  "WidgetUpdates": [
    {
      "Action": "Add",
      "Parent": "Root",
      "Widget": {
        "Type": "Image",
        "Name": "StatusIcon",
        "Slot": {
          "Anchors": { "Min": [1, 0], "Max": [1, 0] },
          "Alignment": [1, 0],
          "SizeToContent": true
        }
      }
    }
  ]
}
```

### WidgetUpdates Example: Remove a Widget

```json
{
  "AssetType": "WidgetBlueprint",
  "Name": "WBP_HUD",
  "Path": "/Game/UI",
  "Action": "Update",
  "WidgetUpdates": [
    { "Action": "Remove", "Name": "OldWidget" }
  ]
}
```

### WidgetUpdates Example: Move a Widget

**IMPORTANT:** Use `"Move"` instead of `"Remove"` + `"Add"` when repositioning an existing widget to a different parent. Move preserves ALL widget properties, children, IsVariable flags, and bindings automatically — no risk of data loss.

```json
{
  "AssetType": "WidgetBlueprint",
  "Name": "WBP_HUD",
  "Path": "/Game/UI",
  "Action": "Update",
  "WidgetUpdates": [
    {
      "Action": "Move",
      "Name": "SkillBar",
      "NewParent": "RightPanel",
      "NewSlot": {
        "Padding": [0, 4, 0, 4],
        "HAlign": "Fill"
      }
    }
  ]
}
```

> **Why Move instead of Remove + Add?**
> `Remove` + `Add` requires you to reconstruct the widget's full JSON — any missed property silently corrupts the widget.
> `Move` detaches the widget from its current parent and re-attaches it to the new parent as-is. Zero reconstruction, zero data loss.

### WidgetUpdates Example: Multiple Operations

```json
{
  "AssetType": "WidgetBlueprint",
  "Name": "WBP_HUD",
  "Path": "/Game/UI",
  "Action": "Update",
  "WidgetUpdates": [
    { "Action": "Remove", "Name": "OldScore" },
    { "Action": "Update", "Name": "TitleText", "Style": { "Color": "#00FF00" } },
    { "Action": "Add", "Parent": "Root", "Widget": { "Type": "TextBlock", "Name": "NewScore", "Properties": { "Text": "0" } } }
  ]
}
```

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

**IMPORTANT:** Use the asset path without `_C` suffix. The generator loads the Widget Blueprint asset first, then falls back to loading as a class with `_C` suffix automatically. Do NOT include `_C` in the path.

```json
{
  "Type": "UserWidget",
  "Properties": {
    "WidgetClass": "/Game/UI/WBP_HealthBar"
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

## ClassDefaults

Sets properties on the Blueprint's Class Default Object (CDO) via reflection. Useful for setting C++ base class properties.

```json
{
  "ClassDefaults": {
    "ProgressMaterial": "/Game/UI/Materials/M_CircularProgress",
    "AvailableColor": [0.2, 0.8, 0.2, 1.0],
    "MaxSlots": 5,
    "bShowLabel": true,
    "ElementIcons": {
      "Fire": "/Game/UI/Textures/element_fire",
      "Water": "/Game/UI/Textures/element_water"
    }
  }
}
```

Supports all property types: primitives, enums, structs (array shorthand `[1,2,3]` or object `{"X":1}`), object references (asset paths), arrays, and maps.

## Bindings Format

Widget-level `Bindings` is the canonical authoring and extract format. Place it on the widget node that owns the target property:

```json
{
  "Type": "TextBlock",
  "Name": "ScoreText",
  "Bindings": {
    "Text": "GetDisplayText",
    "Visibility": {
      "Kind": "Function",
      "Function": "GetTextVisibility"
    },
    "ColorAndOpacity": {
      "Kind": "Property",
      "Property": "TextColor"
    },
    "ToolTipText": {
      "Kind": "Property",
      "SourcePath": ["ViewModel", "DisplayText"]
    }
  }
}
```

Supported binding value forms:

- Simple function binding: `"PropertyName": "FunctionName"`
- Full function binding: `"PropertyName": { "Kind": "Function", "Function": "FunctionName" }`
- Property binding shorthand: `"PropertyName": { "Kind": "Property", "Property": "SourceProperty" }`
- Property binding path: `"PropertyName": { "Kind": "Property", "SourcePath": ["ViewModel", "DisplayText"] }`

Compatibility input is also accepted at the asset config top level:

```json
{
  "Bindings": {
    "ScoreText.Text": {
      "Kind": "Function",
      "Function": "GetDisplayText"
    }
  }
}
```

Top-level binding keys use `"WidgetName.PropertyName"`. This form is accepted for compatibility, but extraction emits widget-level `Bindings`.
If both formats target the same widget property in one request, the widget-level binding wins and the top-level compatibility entry is skipped with a warning.

Binding validation:

- The target widget is auto-exposed as `IsVariable: true`.
- The target property must have a reflected bindable delegate, usually `PropertyNameDelegate`.
- Function bindings must point to an existing function.
- Function signatures must match the target delegate or be supported by UMG's property binding adapter.
- Property delegate function bindings must be `const` or `BlueprintPure`.
- Property bindings are resolved through UE reflection and stored with `SourcePath`; single-segment paths may be displayed as `Property`.
- Invalid bindings fail generation with a specific error instead of being saved as broken bindings.
- Extraction may include read-only `BindingDiagnostics` and `Warnings` fields for verification and skipped-binding diagnostics. These fields are not authoring inputs.

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
