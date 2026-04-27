# StateTree Generator Schema

Creates UE StateTree assets using editor data and the official StateTree compiler.

This schema currently covers the core lifecycle slice only: schema selection, schema properties, compile, save, and skeleton extraction. Tasks, transitions, parameters, property bags, and property bindings are covered by future StateTree specs.

## Top-Level Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `AssetType` | string | Yes | Must be `"StateTree"` |
| `Name` | string | Yes | Asset name |
| `Path` | string | Yes | Content path, for example `"/Game/AI"` |
| `Action` | string | No | `"Create"`, `"Update"`, or `"CreateOrUpdate"` |
| `SchemaClass` | string | Yes | `UStateTreeSchema` subclass path or exact class name |
| `SchemaProperties` | object | No | Properties applied to the schema instance via reflection |

## Minimal Example

```json
{
  "AssetType": "StateTree",
  "Name": "ST_Enemy",
  "Path": "/Game/AI",
  "SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema"
}
```

## AI Component Schema Example

```json
{
  "AssetType": "StateTree",
  "Name": "ST_EnemyAI",
  "Path": "/Game/AI",
  "SchemaClass": "/Script/GameplayStateTreeModule.StateTreeAIComponentSchema",
  "SchemaProperties": {
    "ContextActorClass": "/Script/Engine.Pawn",
    "AIControllerClass": "/Script/AIModule.AIController"
  }
}
```

## Current Limitations

- `SubTrees` are extracted as a skeleton but are not yet accepted as input.
- Tasks, evaluators, conditions, considerations, transitions, parameters, and bindings are not part of this lifecycle slice.
- `Update` cannot change `SchemaClass`; recreate the asset if the schema class must change.
