# Blueprint Generator Schema

Creates Actor-based Blueprint assets with components, variables, interfaces, and default properties.

## Top-Level Fields

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `ParentClass` | string | No | `"Actor"` | Parent class name (e.g. `"Actor"`, `"Character"`, `"Pawn"`) |
| `Components` | array | No | | Array of component definitions (Actor-based blueprints only) |
| `Variables` | array | No | | Array of blueprint variable definitions |
| `Interfaces` | array | No | | Array of interface class name strings |
| `DefaultProperties` | object | No | | Properties to set on the Class Default Object (CDO) |

## Components Array

Each component object:

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `Name` | string | **Yes** | | Component variable name in blueprint |
| `Class` | string | **Yes*** | | Component class name (e.g. `"StaticMeshComponent"`, `"BoxComponent"`). *Not required when Action="Remove" |
| `Action` | string | No | `"CreateOrUpdate"` | `"CreateOrUpdate"`, `"Create"`, `"Update"`, or `"Remove"` (case-insensitive) |
| `Properties` | object | No | | Component properties to set via reflection |
| `bIsRoot` | bool | No | `false` | Set as root scene component (only for new components) |
| `AttachTo` | string | No | | Name of parent component to attach to (only for new components) |

**IMPORTANT:** Use `Class` (NOT `Type`) and `AttachTo` (NOT `Parent`).

### Component Hierarchy Rules
- `bIsRoot: true` makes this the default scene root (must be SceneComponent)
- `AttachTo` resolves from both existing SCS nodes AND newly created nodes in this batch
- If `AttachTo` parent not found, component is added to root
- When updating existing components, hierarchy (bIsRoot, AttachTo) is NOT changed

## Variables Array

Each variable object:

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `Name` | string | **Yes** | | Variable name |
| `Type` | string | No | `"Float"` | Variable type (case-insensitive) |
| `DefaultValue` | string | No | | Default value as string |

### Supported Variable Types
- Primitives: `Bool`, `Byte`, `Int`, `Int64`, `Float`, `Double`, `Name`, `String`, `Text`
- Structs: `Vector`, `Rotator`, `Transform`, `Color`, `LinearColor`
- Any class name (resolved dynamically)

## Properties Format

Two formats supported (detected by checking if first property has a `"type"` field):

**Simple format:**
```json
{
  "Properties": {
    "StaticMesh": "/Engine/BasicShapes/Cube.Cube",
    "Mobility": "Movable",
    "CastShadow": true
  }
}
```

**Typed format:**
```json
{
  "Properties": {
    "Health": { "type": "float", "value": 100 },
    "Name": { "type": "string", "value": "Player" }
  }
}
```

## Complete Example

```json
{
  "AssetType": "Blueprint",
  "Name": "BP_Enemy",
  "Path": "/Game/Blueprints",
  "ParentClass": "Character",
  "Interfaces": ["DamageableInterface"],
  "Variables": [
    { "Name": "Health", "Type": "Float", "DefaultValue": "100" },
    { "Name": "IsDead", "Type": "Bool" }
  ],
  "Components": [
    {
      "Name": "Root",
      "Class": "SceneComponent",
      "bIsRoot": true
    },
    {
      "Name": "Mesh",
      "Class": "StaticMeshComponent",
      "AttachTo": "Root",
      "Properties": {
        "StaticMesh": "/Engine/BasicShapes/Cube.Cube"
      }
    },
    {
      "Name": "HitBox",
      "Class": "BoxComponent",
      "AttachTo": "Root"
    }
  ],
  "DefaultProperties": {
    "AutoPossessAI": "PlacedInWorldOrSpawned"
  }
}
```

## Update Example (Remove + Add Component)

```json
{
  "AssetType": "Blueprint",
  "Name": "BP_Enemy",
  "Path": "/Game/Blueprints",
  "Action": "Update",
  "Components": [
    { "Name": "OldComponent", "Action": "Remove" },
    {
      "Name": "NewComponent",
      "Class": "SphereComponent",
      "AttachTo": "Root"
    }
  ]
}
```
