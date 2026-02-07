# DataTable Generator Schema

Creates DataTable assets from CSV files with a specified row struct.

## Top-Level Fields

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `RowStruct` | string | **Yes** | | Name of the UScriptStruct (must derive from FTableRowBase). Supports with or without `F` prefix. |
| `CSVFilePath` | string | **Yes** | | Absolute path to the CSV file on disk |

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

## Example

```json
{
  "AssetType": "DataTable",
  "Name": "DT_Heroes",
  "Path": "/Game/Data",
  "RowStruct": "FHeroStats",
  "CSVFilePath": "D:/MyProject/Data/Heroes.csv"
}
```
