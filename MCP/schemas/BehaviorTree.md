# BehaviorTree 生成器 Schema

创建 UE `UBehaviorTree` 资产。本文档只描述当前 `BehaviorTreeGenerator` 已实现的 JSON contract；字段名、UE 类型名和 JSON 示例保持英文原文。

生成器会解析 `UBTNode` 子类，构建 runtime `RootNode`，设置 `BlackboardAsset`，并重建 editor graph。节点属性通过反射从 `Properties` 写入对应 UObject。

## 顶层字段

| 字段 | 类型 | 必填 | 说明 |
|-------|------|----------|-------------|
| `AssetType` | string | 是 | 必须是 `"BehaviorTree"` |
| `Name` | string | 是 | Behavior Tree 资产名 |
| `Path` | string | 是 | Content path，例如 `"/Game/AI"` |
| `Action` | string | 否 | `"Create"`、`"Update"` 或 `"CreateOrUpdate"` |
| `Blackboard` | string | 是* | 已存在 `UBlackboardData` 资产路径，例如 `"/Game/AI/BB_Enemy"`；可省略 `.BB_Enemy` 后缀 |
| `BlackboardInline` | object | 是* | 内联 BlackboardData 配置，由 `BlackboardDataGenerator` 以 `CreateOrUpdate` 生成 |
| `Root` | object | 是 | 行为树根节点；必须解析为 `UBTCompositeNode` 子类 |

`Blackboard` 与 `BlackboardInline` 互斥。生成时必须能解析出一个 `UBlackboardData`：使用已有资产时提供 `Blackboard`，需要同步创建/更新黑板时提供 `BlackboardInline`。

`BlackboardInline` 复用 `BlackboardDataGenerator` contract。若未提供 `Name`，当前生成器使用 `BB_<BehaviorTreeName>`；若未提供 `Path`，使用 BehaviorTree 的 `Path`。

## 节点形状

树节点使用 `Node` 指定 `UBTNode` 子类，附件节点使用 `Type` 指定 `UBTDecorator` 或 `UBTService` 子类。

| 字段 | 适用对象 | 类型 | 必填 | 说明 |
|-------|------------|------|----------|-------------|
| `Node` | tree node | string | 是 | `UBTCompositeNode` 或 `UBTTaskNode` 子类名/路径，例如 `"BTComposite_Sequence"`、`"BTTask_Wait"` |
| `Type` | decorator/service | string | 是 | `UBTDecorator` 或 `UBTService` 子类名/路径，例如 `"BTDecorator_Blackboard"` |
| `InstanceName` | tree node | string | 否 | 写入 `UBTNode::NodeName`，用于实例显示名 |
| `Properties` | node/decorator/service | object | 否 | 通过反射写入节点对象的属性 |
| `Children` | composite node | array | composite 必填 | 子节点数组；`UBTCompositeNode` 必须定义且非空 |
| `Decorators` | child node | array | 否 | 装饰器数组；当前实现把它们附着到父 composite 的 child link 上 |
| `Services` | composite node | array | 否 | service 数组；当前 JSON contract 只支持 composite-level services |

规则：

- `Root` 必须是 `UBTCompositeNode` 子类。
- `UBTCompositeNode` 必须有非空 `Children`。
- `UBTTaskNode` 不能有 `Children`。
- `Decorators` 必须是数组，数组元素必须是 object 且 `Type` 必须解析为 `UBTDecorator` 子类。
- `Services` 必须是数组，且只能出现在 composite node 上；不要在 task 上声明 `Services`。
- `Root.Decorators` 能通过结构校验，但生成阶段会记录 warning：`Root node decorators are not supported by UE BT model; ignored`。

## 属性（Properties）

`Properties` 使用 AssetFactory 的反射属性写入格式。简单值和对象值均取决于 UE 属性类型，例如 `FBlackboardKeySelector` 可以使用对象形式：

```json
{
	"Properties": {
		"BlackboardKey": {
			"SelectedKeyName": "HasTarget"
		}
	}
}
```

对于 `BTTask_RunBehavior` 这类包含可编辑 `UBehaviorTree` 引用的节点，当前生成器会反射检查对应属性；`BehaviorAsset` 必须是非空 BehaviorTree 资产路径，并且在使用 `Blackboard` 时会校验子树 blackboard 与父树兼容。

## 基础行为树示例

```json
{
	"AssetType": "BehaviorTree",
	"Name": "BT_TestSample",
	"Path": "/Game/UECopilotTests/AI",
	"Blackboard": "/Game/UECopilotTests/AI/BB_TestSample",
	"Root": {
		"Node": "BTComposite_Selector",
		"InstanceName": "RootSelector",
		"Children": [
			{
				"Node": "BTComposite_Sequence",
				"InstanceName": "AttackSeq",
				"Children": [
					{
						"Node": "BTTask_MoveTo",
						"Properties": {
							"AcceptableRadius": {
								"DefaultValue": 50.0
							}
						}
					},
					{
						"Node": "BTTask_Wait",
						"Properties": {
							"WaitTime": {
								"DefaultValue": 0.5
							}
						}
					}
				]
			},
			{
				"Node": "BTTask_Wait",
				"Properties": {
					"WaitTime": {
						"DefaultValue": 1.0
					}
				}
			}
		]
	}
}
```

## BlackboardInline 示例

```json
{
	"AssetType": "BehaviorTree",
	"Name": "BT_TestSample_Inline",
	"Path": "/Game/UECopilotTests/AI",
	"BlackboardInline": {
		"Name": "BB_TestSample_Inline",
		"Path": "/Game/UECopilotTests/AI",
		"Keys": [
			{
				"Name": "HasTarget",
				"Type": "Bool"
			},
			{
				"Name": "MoveLocation",
				"Type": "Vector"
			}
		]
	},
	"Root": {
		"Node": "BTComposite_Sequence",
		"Children": [
			{
				"Node": "BTTask_Wait",
				"Properties": {
					"WaitTime": {
						"DefaultValue": 0.2
					}
				}
			}
		]
	}
}
```

## Decorator 示例

`Decorators` 写在被装饰的 child node 上；生成器会把它附着到父 composite 的对应 child link。

```json
{
	"AssetType": "BehaviorTree",
	"Name": "BT_WithDecorator",
	"Path": "/Game/UECopilotTests/AI",
	"Blackboard": "/Game/UECopilotTests/AI/BB_TestSample",
	"Root": {
		"Node": "BTComposite_Sequence",
		"Children": [
			{
				"Node": "BTTask_Wait",
				"Decorators": [
					{
						"Type": "BTDecorator_Blackboard",
						"Properties": {
							"BlackboardKey": {
								"SelectedKeyName": "HasTarget"
							}
						}
					}
				],
				"Properties": {
					"WaitTime": {
						"DefaultValue": 0.5
					}
				}
			}
		]
	}
}
```

## Composite Service 示例

`Services` 只能声明在 composite node 上。当前 generator 不支持 task-level services。

```json
{
	"AssetType": "BehaviorTree",
	"Name": "BT_WithService",
	"Path": "/Game/UECopilotTests/AI",
	"Blackboard": "/Game/UECopilotTests/AI/BB_TestSample",
	"Root": {
		"Node": "BTComposite_Sequence",
		"Services": [
			{
				"Type": "BTService_DefaultFocus",
				"Properties": {
					"FocusPriority": "Gameplay"
				}
			}
		],
		"Children": [
			{
				"Node": "BTTask_Wait",
				"Properties": {
					"WaitTime": {
						"DefaultValue": 0.2
					}
				}
			}
		]
	}
}
```

## BTTask_RunBehavior Subtree 示例

```json
{
	"AssetType": "BehaviorTree",
	"Name": "BT_TestSample_Main",
	"Path": "/Game/UECopilotTests/AI",
	"Blackboard": "/Game/UECopilotTests/AI/BB_TestSample",
	"Root": {
		"Node": "BTComposite_Sequence",
		"Children": [
			{
				"Node": "BTTask_RunBehavior",
				"Properties": {
					"BehaviorAsset": "/Game/UECopilotTests/AI/BT_TestSample"
				}
			}
		]
	}
}
```

## 常见失败

以下错误文本与 `BehaviorTreeGenerator.cpp` 当前诊断尽量保持一致：

| 情况 | 诊断 |
|------|------------|
| `AssetType` 缺失或不是精确 `"BehaviorTree"` | `AssetType must be 'BehaviorTree'` |
| 同时提供 `Blackboard` 和 `BlackboardInline` | `Blackboard and BlackboardInline are mutually exclusive` |
| `Blackboard` 不是非空字符串 | `Blackboard must be a non-empty string` |
| `Blackboard` 资产无法加载 | `Blackboard asset could not be loaded: <path>` |
| `BlackboardInline` 不是 object | `BlackboardInline must be a JSON object` |
| `BlackboardInline` 校验失败 | `BlackboardInline validation failed: <BlackboardData error>` |
| `Root` 缺失或不是 object | `Root must exist and be a JSON object` |
| 节点缺失 `Node` | `Root...: Missing or empty Node field` |
| 节点类无法解析 | `Root...: Unknown BT node class '<Node>'` |
| `Root` 不是 composite | `Root: Root node '<Node>' must be a UBTCompositeNode subclass` |
| decorator/service 被当作树节点 | `Root...: Decorator '<Node>' cannot be used as a tree node` / `Root...: Service '<Node>' cannot be used as a tree node` |
| 非 composite/task 节点 | `Root...: Node '<Node>' must be a UBTCompositeNode or UBTTaskNode subclass` |
| `Children` 不是数组 | `Root...: Children must be an array` |
| task 声明 `Children` | `Root...: Non-composite node '<Node>' cannot have 'Children'` |
| composite 的 `Children` 为空 | `Root...: Composite node '<Node>' must have at least one child` |
| composite 缺少 `Children` | `Root...: Composite nodes must define Children` |
| `Decorators` 不是数组 | `Root...: Decorators must be an array` |
| decorator 元素无效 | `Root....Decorators[0]: Invalid decorator object` |
| decorator 缺失 `Type` | `Root....Decorators[0]: Missing or empty Type field` |
| decorator 类型不匹配 | `Root....Decorators[0]: Resolved class '<Type>' is not a UBTDecorator subclass` |
| `Services` 不是数组 | `Root...: Services must be an array` |
| task 声明 `Services` | `Root...: Only composite nodes can define Services` |
| service 元素无效 | `Root....Services[0]: Invalid service object` |
| service 缺失 `Type` | `Root....Services[0]: Missing or empty Type field` |
| service 类型不匹配 | `Root....Services[0]: Resolved class '<Type>' is not a UBTService subclass` |
| blackboard key 不存在 | `BB key reference error: node #<n> (<Class>).<Property> references key "<Key>" not present in blackboard. Available keys: [...]` |
| `BTTask_RunBehavior` 等缺少 BehaviorTree 路径 | `BT asset reference error: node #<n> (<Class>).<Property> must define a BehaviorTree asset path` |
| BehaviorTree 路径不是非空字符串 | `BT asset reference error: node #<n> (<Class>).<Property> must be a non-empty BehaviorTree asset path` |
| 子树资产无法加载 | `BT asset reference error: node #<n> (<Class>).<Property> could not load BehaviorTree asset '<path>'` |
| 子树 blackboard 不兼容 | `BT asset reference error: node #<n> (<Class>).<Property> references BehaviorTree '<path>' with incompatible blackboard '<child>'; parent blackboard is '<parent>'` |
| 生成阶段无法解析 blackboard | `Failed to resolve Blackboard` |
| 生成阶段 root 构建失败 | `Failed to build root node` |
| 生成阶段 root 不是 composite | `Root node must be a Composite` |
| editor graph 重建失败 | `Failed to rebuild behavior tree editor graph` |
