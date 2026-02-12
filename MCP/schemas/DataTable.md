# DataTable Generator Schema

Creates DataTable assets from CSV files with a specified row struct.

## Update Behavior (Action: "Update")

When `Action` is `"Update"`, both `RowStruct` and `CSVFilePath` become optional. If `CSVFilePath` is omitted, the existing table data is preserved. If `CSVFilePath` is provided, the table is emptied and re-imported from the new CSV. For row-level updates without full CSV reimport, use the `update_datatable_rows` tool instead.

## Top-Level Fields (generate_assets)

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `RowStruct` | string | **Yes** (Create) / No (Update) | | Name of the UScriptStruct (must derive from FTableRowBase). Supports with or without `F` prefix. |
| `CSVFilePath` | string | **Yes** (Create) / No (Update) | | Absolute path to the CSV file on disk |

## CSV Format

The CSV file must follow Unreal Engine's DataTable CSV format:

- First row: column headers matching struct property names
- First column: row name (unique key for each row)
- Subsequent columns: property values

Example CSV:
```csv
Name,DisplayName,MaxHealth,AttackPower
Hero_Warrior,Warrior,200,50
Hero_Mage,Mage,100,80
Hero_Rogue,Rogue,120,65
```

## Extract Output

When extracting, the output includes:

| Field | Type | Description |
|-------|------|-------------|
| `RowStruct` | string | Name of the row struct |
| `CSVContent` | string | Full CSV content of the table |

## Example: Full CSV Generation

```json
{
  "AssetType": "DataTable",
  "Name": "DT_Heroes",
  "Path": "/Game/Data",
  "RowStruct": "FHeroStats",
  "CSVFilePath": "D:/MyProject/Data/Heroes.csv"
}
```

## Incremental Row Updates (update_datatable_rows)

Use the `update_datatable_rows` tool to add, update, or delete specific rows without rewriting the entire CSV.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `asset` | string | **Yes** | Asset path of the existing DataTable (e.g., `/Game/Data/DT_Heroes`) |
| `rows` | object | No | Map of RowName → {FieldName: Value} to add or update |
| `deleteRows` | string[] | No | Array of row names to delete |

At least one of `rows` or `deleteRows` must be provided.

### Row Update Behavior

- **Update existing row**: Only the specified fields are modified; other fields remain unchanged.
- **Add new row**: A new row is created with default (empty) values for unspecified fields.
- **Delete row**: The row is removed from the DataTable.

### Value Format

- **Numbers**: Provide as JSON number (e.g., `42`, `3.14`)
- **Strings**: Provide as JSON string (e.g., `"Warrior"`)
- **Booleans**: Provide as JSON boolean (`true` / `false`)
- **Complex UE types** (FVector, FLinearColor, etc.): Provide as a string in UE text format (e.g., `"(X=1,Y=2,Z=3)"`, `"(R=1,G=0,B=0,A=1)"`)

### Example: Update Rows

```json
{
  "asset": "/Game/Data/DT_Heroes",
  "rows": {
    "Hero_Warrior": { "MaxHealth": 300, "AttackPower": 60 },
    "Hero_Ranger": { "DisplayName": "Ranger", "MaxHealth": 130, "AttackPower": 70 }
  },
  "deleteRows": ["Hero_Rogue"]
}
```

This will:
1. Update Hero_Warrior's MaxHealth and AttackPower (other fields unchanged)
2. Add a new Hero_Ranger row
3. Delete the Hero_Rogue row
