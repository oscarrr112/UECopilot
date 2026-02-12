# DataAsset Generator Schema

Creates UDataAsset subclass instances with custom properties.

**IMPORTANT:** Cannot create base `UDataAsset` directly. Must specify a concrete C++ or Blueprint subclass.

## Update Behavior (Action: "Update")

When `Action` is `"Update"`, `ClassName` becomes optional (the existing asset's class is used). Only properties present in the JSON are updated — missing properties are left unchanged.

## Top-Level Fields

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `ClassName` | string | **Yes** (Create) / No (Update) | | UDataAsset subclass name (e.g. `"MyGameDataAsset"`) |
| `Properties` | object | No | | Asset properties to set via reflection |

## Properties Format

Two formats supported (auto-detected by checking if first property has `"type"` field):

**Simple format** (auto-detects type via reflection):
```json
{
  "Properties": {
    "DisplayName": "Fire Sword",
    "Damage": 50,
    "bIsRare": true,
    "Icon": "/Game/Textures/T_FireSword",
    "Color": [1, 0.5, 0, 1],
    "Offset": [10, 20, 30]
  }
}
```

**Typed format** (explicit type):
```json
{
  "Properties": {
    "DisplayName": { "type": "String", "value": "Fire Sword" },
    "Damage": { "type": "Float", "value": 50 },
    "Tags": { "type": "Array:String", "value": ["weapon", "fire"] },
    "Color": { "type": "FLinearColor", "value": "#FF8800" },
    "SpawnOffset": { "type": "FVector", "value": [10, 20, 30] }
  }
}
```

### Struct Value Formats

Struct types accept:
- **Array shorthand** for numeric-only structs: `[X, Y, Z]` for FVector, `[R, G, B, A]` for FLinearColor, etc.
- **Object format** for any struct: `{"X": 1, "Y": 2, "Z": 3}`
- **Special formats**: FLinearColor accepts hex `"#FF0000"` and named colors `"Red"`; FMargin accepts uniform number `10`

## Example

```json
{
  "AssetType": "DataAsset",
  "Name": "DA_FireSword",
  "Path": "/Game/Data/Weapons",
  "ClassName": "WeaponDataAsset",
  "Properties": {
    "WeaponName": "Fire Sword",
    "BaseDamage": 50,
    "AttackSpeed": 1.2,
    "bIsRanged": false,
    "Mesh": "/Game/Meshes/SM_FireSword"
  }
}
```
