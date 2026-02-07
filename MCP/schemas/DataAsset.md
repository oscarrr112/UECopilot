# DataAsset Generator Schema

Creates UDataAsset subclass instances with custom properties.

**IMPORTANT:** Cannot create base `UDataAsset` directly. Must specify a concrete C++ or Blueprint subclass.

## Top-Level Fields

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `ClassName` | string | **Yes** | | UDataAsset subclass name (e.g. `"MyGameDataAsset"`) |
| `Properties` | object | No | | Asset properties to set via reflection |

## Properties Format

Two formats supported (auto-detected by checking if first property has `"type"` field):

**Simple format:**
```json
{
  "Properties": {
    "DisplayName": "Fire Sword",
    "Damage": 50,
    "bIsRare": true,
    "Icon": "/Game/Textures/T_FireSword"
  }
}
```

**Typed format:**
```json
{
  "Properties": {
    "DisplayName": { "type": "string", "value": "Fire Sword" },
    "Damage": { "type": "float", "value": 50 },
    "Tags": { "type": "array", "value": ["weapon", "fire"] }
  }
}
```

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
