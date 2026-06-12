# AssetDocument Schema

AssetDocument is the generic asset sidecar format for reflected Unreal assets. It is not a `generate_assets` payload and it does not encode generator-specific subtypes in the `type` field. MCP tools forward documents to the `/assetfactory/assetdocument/*` HTTP routes; reflection, class lookup, property coercion, validation, and apply behavior live in the Unreal plugin.

## Structured Template Shape

```json
{
  "SchemaVersion": 1,
  "Target": "/Game/Data/DA_Test",
  "Class": "/Script/AssetFactory.TestDataAsset",
  "Action": "CreateOrUpdate",
  "Definitions": {},
  "Properties": {}
}
```

This is the canonical shape returned by `create_asset_document_template`. It is generic and profile-driven; it does not include `AssetType`.

Structured template fields:

- `SchemaVersion`: Use `1`.
- `Target`: The canonical asset package path the document owns, such as `/Game/Data/DA_Test`.
- `Class`: A native path or class name that the Unreal route resolves dynamically.
- `Action`: Use `Create`, `Update`, or `CreateOrUpdate`.
- `Definitions`: A map of reusable fragments keyed by stable names.
- `Properties`: Reflected asset properties to set.

## Legacy Reflected Apply Shape

The current reflected apply, validate, diff, extract, and sidecar file routes still use the legacy GenericAsset sidecar shape:

```json
{
  "SchemaVersion": 1,
  "AssetType": "GenericAsset",
  "Target": "/Game/Data/DA_Test",
  "Class": "/Script/AssetFactory.TestDataAsset",
  "Action": "CreateOrUpdate",
  "Properties": {
    "TestString": "hello",
    "TestInt": 42,
    "TypedFloat": {
      "type": "float",
      "value": 3.5
    }
  }
}
```

Required fields for legacy reflected apply/validate/diff paths:

- `SchemaVersion`: Use `1`.
- `AssetType`: Use `GenericAsset` for these legacy reflected paths.
- `Target`: Required in sidecar files. It is the canonical asset path the sidecar owns, such as `/Game/Data/DA_Test`.
- `Class`: Required for validation, diff, and apply. It can be a native path or class name that the Unreal route resolves dynamically.
- `Action`: Required for validation, diff, and apply. Use `Create`, `Update`, or `CreateOrUpdate`.

Common apply fields:

- `Properties`: Reflected asset properties to set.

Inline convenience:

- `Name` and `Path` may be used as convenience fields for inline documents only when the Unreal route/service receiving the document accepts them.
- MCP forwards the raw document to Unreal and does not synthesize `Target` from `Name` and `Path`.
- Sidecar files and portable examples should include explicit `Target`; `Name` and `Path` are convenience, not a replacement for the required sidecar `Target`.

## Properties

`Properties` supports untyped and typed forms:

```json
{
  "Properties": {
    "TestString": "plain untyped value",
    "TestInt": 12,
    "TypedName": {
      "type": "Name",
      "value": "CreatedName"
    }
  }
}
```

Untyped values are raw JSON values. Typed values use an object with:

- `type`: The property type hint, such as `Name`, `Text`, `float`, or another reflected type accepted by the Unreal route.
- `value`: The value to apply.

Rule: no subtype in `type`. For arrays, maps, sets, structs, objects, soft references, and class references, keep subtype information in the value shape accepted by the route rather than inventing `type` strings like `Array<int>` or `Object:SomeClass`.

## MCP Tools

- `get_asset_document_schema`: Reads this schema document from `MCP/schemas/AssetDocument.md`.
- `inspect_asset_document_target`: Calls `GET /assetfactory/assetdocument/inspect?class_or_asset=...` to inspect writable reflected properties for a class or asset.
- `inspect_asset_document_profile`: Calls `GET /assetfactory/assetdocument/profile?class_or_asset=...` to inspect the generic AssetDocument profile for a class or asset.
- `create_asset_document_template`: Calls `POST /assetfactory/assetdocument/template` with `Class` and `Target` to create a canonical generic AssetDocument template.
- `extract_asset_document`: Calls `POST /assetfactory/assetdocument/extract` and returns an AssetDocument draft for an existing asset.
- `validate_asset_document`: Calls `POST /assetfactory/assetdocument/validate` for exactly one inline document or sidecar file path.
- `diff_asset_document`: Calls `POST /assetfactory/assetdocument/diff` to compare exactly one inline document or sidecar file against the current asset state.
- `apply_asset_document`: Calls `POST /assetfactory/assetdocument/apply` with an inline AssetDocument JSON object.
- `apply_asset_document_file`: Calls `POST /assetfactory/assetdocument/apply-file` with a sidecar file path. The sidecar `Target` must match the path-derived `/Game` asset target; use `validate_asset_document` or `diff_asset_document` first when you need a preflight check.

AssetDocument MCP tools are generic. Do not add asset-specific AssetDocument tools; use profile inspection and reflected schema data to discover capabilities for a class or asset.

## Tool Arguments

Apply inline using the legacy reflected shape:

```json
{
  "document": {
    "SchemaVersion": 1,
    "AssetType": "GenericAsset",
    "Target": "/Game/Data/DA_Test",
    "Class": "TestDataAsset",
    "Action": "CreateOrUpdate",
    "Properties": {
      "TestString": "hello"
    }
  }
}
```

Apply sidecar file:

```json
{
  "file_path": "E:/GameDev/Project/Content/Data/DA_Test.assetdoc.json",
  "save_asset": true
}
```

The sidecar file itself should include `Target`:

```json
{
  "SchemaVersion": 1,
  "AssetType": "GenericAsset",
  "Target": "/Game/Data/DA_Test",
  "Class": "TestDataAsset",
  "Action": "CreateOrUpdate",
  "Properties": {
    "TestString": "hello"
  }
}
```

Inspect:

```json
{
  "class_or_asset": "/Script/AssetFactory.TestDataAsset"
}
```

Inspect profile:

```json
{
  "class_or_asset": "/Script/AssetFactory.TestDataAsset"
}
```

Create template:

```json
{
  "class": "/Script/AssetFactory.TestDataAsset",
  "target": "/Game/Data/DA_Test"
}
```

Example template result:

```json
{
  "SchemaVersion": 1,
  "Target": "/Game/Data/DA_Test",
  "Class": "/Script/AssetFactory.TestDataAsset",
  "Action": "CreateOrUpdate",
  "Definitions": {},
  "Properties": {}
}
```

Extract:

```json
{
  "asset_path": "/Game/Data/DA_Test",
  "diff_only": true,
  "include_all_writable": false
}
```

Validate or diff inline using the legacy reflected shape:

```json
{
  "document": {
    "SchemaVersion": 1,
    "AssetType": "GenericAsset",
    "Target": "/Game/Data/DA_Test",
    "Class": "TestDataAsset",
    "Action": "CreateOrUpdate",
    "Properties": {}
  }
}
```

Validate or diff sidecar:

```json
{
  "file_path": "E:/GameDev/Project/Content/Data/DA_Test.assetdoc.json"
}
```

## File Watcher Behavior

The Unreal-side file watcher owns sidecar synchronization. When a watched AssetDocument sidecar changes on disk, the editor route may apply or validate the sidecar according to the watcher implementation and should mark watcher-triggered requests internally. MCP tools do not implement watcher behavior; use `apply_asset_document_file`, `validate_asset_document`, or `diff_asset_document` to explicitly exercise the same HTTP-side sidecar paths.
