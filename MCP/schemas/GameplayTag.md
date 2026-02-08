# GameplayTag Generator Schema

Registers GameplayTags in the project. Tags are persisted to `Config/DefaultGameplayTags.ini` and registered at runtime for immediate use by other generators (e.g., Blueprint/GameplayEffect).

**Priority**: -10 (processed before all other generators)

## Top-Level Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `Tags` | array | Yes | Array of tag definitions (string or object format) |

## Tags Array Formats

### Simple String Format
Just the tag name using dot-separated hierarchy:
```json
"Status.Burning"
```

### Object Format (with description)
```json
{"Tag": "Status.Burning", "DevComment": "Applied when target is burning"}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `Tag` | string | Yes | Tag name in dot notation (e.g., `"Damage.Fire"`) |
| `DevComment` | string | No | Developer comment / description |

## Tag Naming Rules
- Use dot-separated hierarchy: `"Category.Subcategory.Name"`
- No spaces allowed
- Common conventions: `Status.*`, `Damage.*`, `Ability.*`, `GameplayCue.*`, `Event.*`

## Behavior
- **Duplicate handling**: Tags that already exist in the ini file are skipped (no duplicates created)
- **Runtime registration**: Tags are immediately available via `FGameplayTag::RequestGameplayTag` after generation
- **Persistence**: Tags survive editor restart (written to ini file)
- **Action parameter**: Ignored (always merges — existing tags are preserved, new tags are appended)

## Examples

### Register GAS status tags
```json
{
  "AssetType": "GameplayTag",
  "Name": "GAS_StatusTags",
  "Path": "/Config/Tags",
  "Tags": [
    "Status.Burning",
    "Status.Frozen",
    "Status.Poisoned",
    "Status.Stunned"
  ]
}
```

### Register tags with descriptions
```json
{
  "AssetType": "GameplayTag",
  "Name": "DamageTags",
  "Path": "/Config/Tags",
  "Tags": [
    {"Tag": "Damage.Physical", "DevComment": "Physical damage type"},
    {"Tag": "Damage.Fire", "DevComment": "Fire elemental damage"},
    {"Tag": "Damage.Ice", "DevComment": "Ice elemental damage"},
    {"Tag": "GameplayCue.Fire.Damage", "DevComment": "VFX cue for fire damage"}
  ]
}
```

### Mixed format
```json
{
  "AssetType": "GameplayTag",
  "Name": "AbilityTags",
  "Path": "/Config/Tags",
  "Tags": [
    "Ability.Skill.Fireball",
    "Ability.Skill.IceBlast",
    {"Tag": "Ability.Cooldown.Global", "DevComment": "Global cooldown tag"}
  ]
}
```
