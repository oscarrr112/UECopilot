# InputMappingContext Generator Schema

Creates Enhanced Input Mapping Context assets that bind keys to Input Actions.

## Update Behavior (Action: "Update")

When `Action` is `"Update"`, only fields present in the JSON are processed. If `Mappings` is omitted, existing mappings are preserved. If `Mappings` is provided, all existing mappings are cleared and replaced with the new ones.

## Top-Level Fields

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `Mappings` | array | No | | Array of key-to-action mappings (if provided, replaces all existing mappings) |

## Mappings Array

Each mapping object:

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `Action` | string | **Yes** | | Full path to InputAction asset (e.g. `/Game/Input/IA_Move`) |
| `Key` | string | **Yes** | | Input key name (see Key Names below) |
| `Triggers` | array | No | | Per-mapping trigger type name strings |
| `Modifiers` | array | No | | Per-mapping modifier type name strings |

### Key Names

**Letters:** `A`-`Z` (case-insensitive)

**Numbers:** `0`-`9`

**Function keys:** `F1`-`F12`

**Special keys:**
- `Space` / `SpaceBar`
- `Enter` / `Return`
- `Escape` / `Esc`
- `Tab`, `Backspace` / `BackSpace`, `CapsLock`

**Arrow keys:** `Up`, `Down`, `Left`, `Right`

**Modifier keys:**
- `LeftShift`, `RightShift`, `Shift` (alias for LeftShift)
- `LeftControl`, `RightControl`, `Ctrl` (alias for LeftControl)
- `LeftAlt`, `RightAlt`, `Alt` (alias for LeftAlt)

**Mouse:**
- `LeftMouseButton`, `RightMouseButton`, `MiddleMouseButton`
- `ThumbMouseButton`, `ThumbMouseButton2`
- `MouseScrollUp`, `MouseScrollDown`

**Gamepad axes:** `Gamepad_LeftX`, `Gamepad_LeftY`, `Gamepad_RightX`, `Gamepad_RightY`

**Gamepad buttons:** `Gamepad_FaceButton_Bottom`, `Gamepad_FaceButton_Right`, `Gamepad_FaceButton_Left`, `Gamepad_FaceButton_Top`

### Trigger & Modifier Values
Same as InputAction (see InputAction schema).

## Example

```json
{
  "AssetType": "InputMappingContext",
  "Name": "IMC_Default",
  "Path": "/Game/Input",
  "Mappings": [
    {
      "Action": "/Game/Input/IA_Move",
      "Key": "W",
      "Modifiers": ["Swizzle", "Negate"]
    },
    {
      "Action": "/Game/Input/IA_Move",
      "Key": "S",
      "Modifiers": ["Swizzle"]
    },
    {
      "Action": "/Game/Input/IA_Move",
      "Key": "A",
      "Modifiers": ["Negate"]
    },
    {
      "Action": "/Game/Input/IA_Move",
      "Key": "D"
    },
    {
      "Action": "/Game/Input/IA_Jump",
      "Key": "SpaceBar",
      "Triggers": ["Pressed"]
    },
    {
      "Action": "/Game/Input/IA_Look",
      "Key": "Gamepad_RightX"
    }
  ]
}
```
