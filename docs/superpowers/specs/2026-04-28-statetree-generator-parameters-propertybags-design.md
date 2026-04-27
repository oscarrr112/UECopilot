# StateTree Generator 参数与 PropertyBag 设计

**日期**：2026-04-28
**状态**：Draft
**范围**：StateTree generator Spec 4

## 目标

支持通过 JSON 创建、更新、提取 StateTree 的可编辑参数，并沿用 Blueprint generator 已经使用的 typed property JSON 风格，同时补上 UE PropertyBag 额外需要的 schema、身份和同步语义。

本 spec 覆盖：

- `UStateTreeEditorData::RootParameterPropertyBag` 上的 root parameters。
- `UStateTreeState::Parameters` 上的 state parameters。
- root/state 参数的 extract 与 generate/extract round-trip。
- 稳定的参数 property ID，保证后续 property bindings 能安全引用参数。
- state parameter overrides。
- linked subtree 与 linked asset 的参数同步。

generator 仍然只构建 editor data，再交给官方 StateTree compiler 生成 runtime data。不会直接写 baked runtime parameter data。

## 非目标

- Property bindings 与 property function bindings。这些仍属于 Spec 5，但本 spec 必须保存 Spec 5 需要的参数 ID。
- Runtime-only StateTree compact data 序列化。
- 创建一套与 Blueprint property JSON 无关的新 PropertyBag JSON 方言。
- 为具体游戏或 fixture 硬编码参数类型。

## JSON 契约

StateTree 参数使用 per-parameter entry，并扩展 Blueprint generator 的 typed property 格式：

```json
{
	"RootParameters": {
		"MoveSpeed": {
			"type": "Float",
			"id": "02c9c5f8-4e0a-4fa7-a3d3-b9010a5a3c7a",
			"value": 600.0
		},
		"TargetActor": {
			"type": "Object:/Script/Engine.Actor",
			"value": "/Game/Test/BP_Target.BP_Target"
		}
	}
}
```

规则：

- `type` 必填，并沿用 `FPropertySetterUtils` 风格：`Float`、`Bool`、`String`、`Name`、`Text`、`Enum:<EnumName>`、`Struct:<StructName>`、`Object:<ClassName>`、`SoftObject:<ClassName>`、`Class:<ClassName>`、`SoftClass:<ClassName>`、`Array:<ElementType>`、`Map:<KeyType>:<ValueType>`、`Set:<ElementType>`。
- `value` 默认必填。只有调用方明确想使用 PropertyBag 创建后的默认值时，才允许省略。
- `id` 可选。存在时必须是合法 GUID，并原样保留。缺省时 generator 基于 bag scope 和参数名生成 deterministic GUID。
- 同一个 bag 内参数名大小写敏感，重复参数名非法。
- 为兼容现有命名风格，generator 同时接受 `RootParameters` 和 `rootParameters`。

State 参数写在各自 state 对象内部：

```json
{
	"name": "Patrol",
	"parameters": {
		"PatrolRadius": {
			"type": "Float",
			"value": 1200.0
		}
	}
}
```

State parameter override 显式表达，方便 extraction round-trip：

```json
{
	"name": "Attack",
	"parameterOverrides": {
		"MoveSpeed": {
			"value": 900.0,
			"overridden": true
		}
	}
}
```

如果 UE 5.7 在内部把 override 与 state parameter default 存在同一个 bag 中，extract 可以输出规范化后的 `parameters` 形态，并附带 `overridden: true`。generation 同时接受两种形态。实现计划阶段必须先确认 UE 5.7 的正式 editor API，再决定内部写入路径。

## 架构

新增一个 StateTree-local 的 PropertyBag adapter，不把 PropertyBag 细节塞进 state builder。

建议文件：

- `Private/Generators/StateTree/StateTreePropertyBagAdapter.h`
- `Private/Generators/StateTree/StateTreePropertyBagAdapter.cpp`

adapter 负责：

- 解析参数 JSON entry 为 typed spec。
- 把 Blueprint-style type string 映射到 PropertyBag property descriptor。
- 创建或更新 PropertyBag schema entry。
- 找到 bag 生成出的 `FProperty`，并复用 `FPropertySetterUtils::SetPropertyValueFromJson` 写值。
- 把 bag schema 和 value 反向提取成 typed JSON。
- 保留或生成稳定 property ID。

现有 StateTree builder 只做编排：

- `StateTreeStateBuilder` 解析 root/state parameter section，并调用 adapter。
- linked state finalize 阶段调用 adapter 做 linked parameter 同步。
- `StateTreeExtract` 调用 adapter 序列化 root/state bags。

这样 Spec 4 边界清楚，Spec 5 也能复用同一套参数 lookup 和 ID 映射。

## 类型支持

Spec 4 以完整沿用 Blueprint-style value coverage 为目标，但以 UE PropertyBag 实际支持能力为准：

- 数字 primitive：signed/unsigned integers、float、double。
- `Bool`。
- `String`、`Name`、`Text`。
- `Enum:<EnumName>`。
- `Struct:<StructName>`，包含 nested struct value。
- `Object:<ClassName>`。
- `SoftObject:<ClassName>`。
- `Class:<ClassName>`。
- `SoftClass:<ClassName>`。
- `Array:<ElementType>`。
- `Map:<KeyType>:<ValueType>`，前提是当前 UE 版本的 PropertyBag API 支持。
- `Set:<ElementType>`，前提是当前 UE 版本的 PropertyBag API 支持。

不支持的组合必须在 asset compile 前返回清晰诊断。不能静默丢参数，也不能保存半错的资产。

示例：

```json
{
	"DamageType": {
		"type": "Enum:/Script/Game.RPGDamageType",
		"value": "Fire"
	},
	"SpawnOffset": {
		"type": "Struct:/Script/CoreUObject.Vector",
		"value": {
			"X": 10,
			"Y": 0,
			"Z": 80
		}
	},
	"Waypoints": {
		"type": "Array:Struct:/Script/CoreUObject.Vector",
		"value": [
			{ "X": 0, "Y": 0, "Z": 0 },
			{ "X": 300, "Y": 0, "Z": 0 }
		]
	}
}
```

## ID 策略

PropertyBag property ID 是外部契约的一部分，因为 Spec 5 的 bindings 会引用这些参数。

生成规则：

1. JSON 提供 `id` 时，先验证为 GUID，再使用该值。
2. update 现有资产时，如果同名参数已存在，默认保留已有 PropertyBag property ID，除非 JSON 显式提供不同 `id`。
3. 创建新参数且没有 `id` 时，根据以下信息生成 deterministic GUID：
	- asset path；
	- bag scope，取 `root` 或 canonical state path；
	- parameter name；
	- parameter type string。
4. Extract 永远输出 `id`。

修改参数类型视为 schema migration。只要安全就应尽量保留 ID；但如果 UE 拒绝修改，或保留 ID 会得到非法 bag descriptor，则必须失败。

## Root 与 State 参数

Root parameters 在 state structure compile 前应用，这样 nodes、overrides 和未来 bindings 都能解析它们。

State parameters 在 states 已有稳定 canonical paths 和 IDs 后应用，但仍然要早于最终 StateTree compile。这样 deterministic parameter ID 可以使用 canonical state path，linked-state 同步也能检查最终 link。

State JSON parser 接受：

- `parameters` / `Parameters`
- `parameterOverrides` / `ParameterOverrides`

generator 需要验证：

- 每个 parameter entry 都是 object。
- schema-defining entry 必须有 `type`。
- `value` 必须能写入创建后的 PropertyBag reflected property。
- `parameterOverrides` 不能引用不存在的 root/linked parameter，除非 UE API 明确支持创建 local override slot。

## Linked 参数

Linked subtree 与 linked asset 的行为必须显式：

- `Linked` state 指向 subtree 时，generator 需要基于目标 subtree 暴露的参数做兼容同步。
- `LinkedAsset` state 会加载目标 asset，并从 linked asset 的 root parameter bag 同步参数。
- linked state 上的 override 使用同样的 typed value 语义，但 schema 来源是 linked target，不是任意新建的本地 schema。
- 如果 linked target 暴露的参数类型无法被本地 override 表达，generation 失败，并报告 linked state path 与 parameter name。

本节只关注 editor-visible 参数状态。runtime 行为通过 compile、reload、editor/MCP smoke 验证，不直接写 baked fields。

## 提取

Extract 输出：

- asset config 层的 `RootParameters`，当 root bag 有 entry 时输出。
- state 上的 `parameters`，当 state-local parameters 存在时输出。
- linked states 上的 linked parameter overrides。
- 每个参数 entry 都输出 `id`、`type`、`value`。

Extract 应优先输出 generation 能接受的同一套 type string。object/class/soft refs 输出稳定 path string。struct 与 container 使用 bag property 对应的 reflected `FProperty`，通过 `FPropertySetterUtils::ExtractPropertyToJson` 输出。

`diffOnly` 行为：

- `diffOnly=false` 输出所有 bag values。
- `diffOnly=true` 可以省略与默认值相同的 value，但不能省略重建参数 schema 必需的 metadata。

## 校验与错误处理

尽量在 compile 前完成校验：

- 未知 type string。
- 未知 enum、struct、object class 或 class constraint。
- 不支持的 PropertyBag descriptor 组合。
- 同一 scope 内重复 parameter name。
- 非法 GUID。
- override 引用未知 parameter。
- value 无法通过 reflected property 写入。
- linked asset 无法加载，或不是 `UStateTree`。
- linked parameter type mismatch。

错误信息应该包含：

- asset path 或 state canonical path；
- parameter name；
- type string；
- 失败原因。

示例：

```text
StateTree state 'Root/Attack' parameter override 'MoveSpeed' references unknown linked parameter
```

## 冒烟与验证策略

Spec 4 是一个设计，但实现时按三层冒烟：

### 4A：Root/State 基础参数

正向：

- 生成包含 root parameters 和普通 state parameters 的 StateTree，覆盖 primitive、enum、object/class/soft refs。
- 打开 GUI editor，确认参数在编辑器可见。
- extract asset，确认 `id/type/value` 可以 round-trip。

反向：

- unknown type string。
- invalid GUID。
- value type mismatch。

### 4B：复杂类型

正向：

- struct parameter。
- nested struct parameter。
- array parameter。
- UE PropertyBag 支持时验证 array-of-struct parameter。
- UE PropertyBag 支持时验证 map/set parameters。

反向：

- unsupported container shape 返回稳定错误。
- struct field value 错误时 compile 前失败。

### 4C：Overrides 与 Linked 参数

正向：

- state parameter override。
- linked subtree parameter override。
- linked asset parameter override。
- generate/extract/generate 保留 IDs。

反向：

- override unknown linked parameter。
- override type mismatch。
- linked asset missing 或 wrong type。

验证命令沿用前几个 spec：

- `npm --prefix MCP run build`
- Project editor UBT build
- 对成功 fixtures 做真实 GUI editor MCP smoke
- 通过 `generate_assets` 冒烟 invalid fixture
- 通过 `extract_assets` 做 extract smoke

最终证据必须包含真实 editor/MCP 生成资产，不只用 headless compile。

## 设计风险

- UE 5.7 PropertyBag 支持的 container 组合可能少于 `FPropertySetterUtils` 可写入的普通 reflected UObject property。设计上接受同一套 type string，但对 unsupported bag descriptor 明确失败。
- State override 的内部存储在不同 StateTree 版本中可能不同。实现计划必须检查 UE 5.7 的真实 API，并使用官方 editor path，不能猜内存布局。
- Linked asset parameter sync 可能依赖 compile 或 post-load refresh hooks。如果官方 API 暴露同步 helper，就使用官方 helper；否则只窄范围镜像 editor 行为，并在实现中记录具体 engine symbol。

## 成功标准

Spec 4 完成时必须满足：

- Root 与 state parameter bags 可以 generate、在 editor 打开、compile、save、reload、extract。
- generator 在 UE PropertyBag 支持范围内覆盖约定的完整 typed JSON surface。
- unsupported type/container combinations 返回清晰错误。
- State 和 linked parameter overrides 在真实 editor-visible asset 中工作。
- 参数 IDs 在 generate/extract/generate 和 update flows 中保持稳定。
- 为 Spec 5 property bindings 留出稳定的参数 lookup surface。
