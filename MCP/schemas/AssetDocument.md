# AssetDocument Schema

AssetDocument is the generic asset sidecar format for reflected Unreal assets. It is not a `generate_assets` payload and it does not encode generator-specific subtypes in the `type` field. MCP tools forward documents to the `/assetfactory/assetdocument/*` HTTP routes; reflection, class lookup, property coercion, validation, and apply behavior live in the Unreal plugin.

## Document Shape

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

Required fields:

- `Target`: Required in sidecar files. It is the canonical asset path the sidecar owns, such as `/Game/Data/DA_Test`.
- `AssetType`: Use `GenericAsset`.
- `SchemaVersion`: Use `1`.

Common apply fields:

- `Class`: Required when creating an asset. It can be a native path or class name that the Unreal route resolves dynamically.
- `Action`: `Create`, `Update`, or `CreateOrUpdate`.
- `Properties`: Reflected asset properties to set.

Inline convenience:

- `Name` and `Path` may be used as convenience fields for inline documents. When present, they identify the target asset name and package path without requiring the caller to split them manually before sending the document.
- Sidecar files should still include `Target`; `Name` and `Path` are convenience, not a replacement for the required sidecar `Target`.

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
- `extract_asset_document`: Calls `POST /assetfactory/assetdocument/extract` and returns an AssetDocument draft for an existing asset.
- `validate_asset_document`: Calls `POST /assetfactory/assetdocument/validate` for either an inline document or a sidecar file path.
- `diff_asset_document`: Calls `POST /assetfactory/assetdocument/diff` to compare an inline document or sidecar file against the current asset state.
- `apply_asset_document`: Calls `POST /assetfactory/assetdocument/apply` with an inline AssetDocument JSON object.
- `apply_asset_document_file`: Calls `POST /assetfactory/assetdocument/apply-file` with a sidecar file path.

## Tool Arguments

Apply inline:

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
  "file_path": "E:/GameDev/Project/Saved/AssetFactory/Sidecars/DA_Test.assetdocument.json",
  "save_asset": true
}
```

Inspect:

```json
{
  "class_or_asset": "/Script/AssetFactory.TestDataAsset"
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

Validate or diff inline:

```json
{
  "document": {
    "SchemaVersion": 1,
    "AssetType": "GenericAsset",
    "Target": "/Game/Data/DA_Test",
    "Properties": {}
  }
}
```

Validate or diff sidecar:

```json
{
  "file_path": "E:/GameDev/Project/Saved/AssetFactory/Sidecars/DA_Test.assetdocument.json"
}
```

## File Watcher Behavior

The Unreal-side file watcher owns sidecar synchronization. When a watched AssetDocument sidecar changes on disk, the editor route may apply or validate the sidecar according to the watcher implementation and should mark watcher-triggered requests internally. MCP tools do not implement watcher behavior; use `apply_asset_document_file`, `validate_asset_document`, or `diff_asset_document` to explicitly exercise the same HTTP-side sidecar paths.
