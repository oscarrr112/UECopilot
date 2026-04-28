# BlackboardData Generator Schema

创建 UE `UBlackboardData` 资产。本文档只描述当前 `BlackboardDataGenerator` 已实现的 JSON contract；字段名、UE 类型名和 JSON 示例保持英文原文。

生成器会创建或更新 blackboard asset，解析可选父级 blackboard，并把 `Keys` 数组转换为 `FBlackboardEntry`。`Object` / `Class` / `Enum` key 会在生成阶段实例化对应 `UBlackboardKeyType` 并写入必要引用。

## 顶层字段

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `AssetType` | string | Yes | 必须是 `"BlackboardData"` |
| `Name` | string | Yes | BlackboardData 资产名 |
| `Path` | string | Yes | Content path，例如 `"/Game/AI"` |
| `Action` | string | No | `"Create"`、`"Update"` 或 `"CreateOrUpdate"` |
| `Parent` | string | No | 已存在的 parent blackboard asset path，例如 `"/Game/AI/BB_BaseEnemy"`；可省略 `.BB_BaseEnemy` 后缀 |
| `Keys` | array | Yes | 非空数组；每个元素描述一个 blackboard key |

`Parent` 会被加载为 `UBlackboardData` 并写入 `Blackboard->Parent`。如果提供 `Parent`，路径必须能解析到已有 blackboard asset。

## Key Shape

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `Name` | string | Yes | Blackboard key 名称；同一个 `Keys` 数组内必须唯一 |
| `Type` | string | Yes | key type；可用短名，也可用完整 `UBlackboardKeyType` class path |
| `BaseClass` | string | Required for `Object` / `Class` | `Object` 或 `Class` key 的 UE class reference，例如 `"/Script/Engine.Actor"` |
| `EnumName` | string | Required for `Enum` | `Enum` key 的 UE enum reference；可为 enum path 或可解析的 enum 名称 |
| `bInstanceSynced` | boolean | No | 写入 `FBlackboardEntry::bInstanceSynced`；省略时保持默认值 |
| `Description` | string | No | 写入 `FBlackboardEntry::EntryDescription` |

## Key Types

当前 contract 支持以下短名：

- `Bool`
- `Int`
- `Float`
- `String`
- `Name`
- `Vector`
- `Rotator`
- `Object`
- `Class`
- `Enum`

`Type` 也可以使用完整 `UBlackboardKeyType` class path，例如 `"/Script/AIModule.BlackboardKeyType_Object"`。短名会按当前生成器逻辑解析为 `BlackboardKeyType_<Type>`，并尝试从 `/Script/AIModule`、`/Script/AssetFactory`、当前项目模块和 `/Script/Engine` 查找。

规则：

- `Object` key 必须提供非空 `BaseClass`，且该 class reference 必须能加载。
- `Class` key 必须提供非空 `BaseClass`，且该 class reference 必须能加载。
- `Enum` key 必须提供非空 `EnumName`，且该 enum reference 必须能解析。
- 其他 key type 不需要 `BaseClass` 或 `EnumName`。

## Example

以下示例以 `TestData/BB_TestSample.json` 的字段和值为基础，并展示当前 contract 支持的 `Parent`、`Object`、`Class`、`Enum` key 写法：

```json
{
	"AssetType": "BlackboardData",
	"Name": "BB_TestSample",
	"Path": "/Game/UECopilotTests/AI",
	"Parent": "/Game/UECopilotTests/AI/BB_BaseSample",
	"Keys": [
		{
			"Name": "TargetActor",
			"Type": "Object",
			"BaseClass": "/Script/Engine.Actor"
		},
		{
			"Name": "PreferredActorClass",
			"Type": "Class",
			"BaseClass": "/Script/Engine.Actor"
		},
		{
			"Name": "MovementMode",
			"Type": "Enum",
			"EnumName": "/Script/Engine.EMovementMode"
		},
		{
			"Name": "HasTarget",
			"Type": "Bool",
			"bInstanceSynced": true
		},
		{
			"Name": "MoveLocation",
			"Type": "Vector"
		},
		{
			"Name": "TargetTag",
			"Type": "Name"
		},
		{
			"Name": "Score",
			"Type": "Int"
		},
		{
			"Name": "Speed",
			"Type": "Float",
			"Description": "Movement speed score"
		}
	]
}
```

`TestData/BB_TestSample.json` 当前实际内容不包含 `Parent`、`Class` 或 `Enum` key；上面的额外字段用于展示同一 generator contract 已支持的形状。

## Common Failures

| Situation | Diagnostic |
|-----------|------------|
| 配置对象无效 | `Invalid configuration object` |
| `Action` 为 `Create` 且资产已存在 | `Asset already exists` |
| `Action` 为 `Update` 且资产不存在 | `Asset does not exist for update` |
| 现有 blackboard asset 无法加载 | `Failed to load existing blackboard asset` |
| package 创建失败 | `Failed to create package` |
| blackboard asset 创建失败 | `Failed to create blackboard asset` |
| `Parent` 无法加载 | `Failed to load Parent blackboard '<path>'` |
| `Keys` 缺失、不是数组或为空 | `'Keys' must be a non-empty array` |
| `Keys` 中元素不是 object | `Keys[<index>] must be an object` |
| key 缺失非空 `Name` | `Keys[<index>].Name must be a non-empty string` |
| key 名重复 | `Duplicate blackboard key name '<name>'` |
| key 缺失非空 `Type` | `Keys[<index>] ('<name>') is missing a valid Type` |
| `Type` 无法解析为 `UBlackboardKeyType` | `Keys[<index>] ('<name>') has unsupported Type '<type>'` 或 `Unsupported key type '<type>'` |
| `Object` / `Class` key 缺失 `BaseClass` | `Keys[<index>] ('<name>') requires a non-empty BaseClass` |
| `BaseClass` 无法加载 | `Keys[<index>] ('<name>') failed to load BaseClass '<class>'` 或 `Failed to load BaseClass '<class>' for Object key '<name>'` / `Failed to load BaseClass '<class>' for Class key '<name>'` |
| `Enum` key 缺失 `EnumName` | `Keys[<index>] ('<name>') requires a non-empty EnumName` |
| `EnumName` 无法解析 | `Keys[<index>] ('<name>') failed to resolve EnumName '<enum>'` 或 `Failed to resolve EnumName '<enum>' for Enum key '<name>'` |
| 保存 package 失败 | `Failed to save blackboard package` |
