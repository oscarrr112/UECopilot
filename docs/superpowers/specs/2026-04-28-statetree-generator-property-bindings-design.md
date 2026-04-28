# StateTree Generator Property Bindings 设计

**日期**：2026-04-28
**状态**：Draft
**范围**：StateTree generator Spec 5

## 目标

支持通过 AssetFactory JSON 创建、验证、提取和 round-trip StateTree 的 editor property bindings，并覆盖普通 `FStateTreeEditorPropertyBindings` 与 `FStateTreePropertyFunctionBase` property function binding。

本 spec 覆盖：

- `FPropertyBindingPath` source/target path 的 JSON 表达。
- State、node、parameter、context、evaluator、global task 等引用到 StateTree editor GUID 的解析。
- PropertyBag 参数 property ID 在 binding path segment 中的保留。
- 普通 property binding 的生成、校验、提取与 round-trip。
- Property function binding 的生成、校验、提取与 round-trip。
- MCP schema 与 smoke fixtures。

最终交付必须是完整功能，但实现按三个子阶段推进，分别冒烟：

- **5A**：普通 property binding core。
- **5B**：property function binding。
- **5C**：extract、round-trip、schema、完整 smoke。

## 非目标

- 设计或实现字符串 DSL，例如 `Root.Parameters.Speed -> Task.Duration`。本 spec 使用显式 JSON object 和 path segment array。
- 直接写 runtime compact data。generator 只构建 editor data，并交给 UE StateTree compiler 生成 runtime data。
- 覆盖所有 UE property binding 边角类型。第一版支持常见 property、数组下标、PropertyBag GUID、struct/object indirection；发现不支持的 path 形态必须给出清晰诊断。
- 重新设计 Spec 4 的参数 JSON。Spec 5 复用参数的 stable property ID 与 extraction 输出。

## JSON 契约

顶层使用 `bindings` / `Bindings` 数组。每个 entry 要么是普通 binding，要么是 property function binding。

普通 binding：

```json
{
	"bindings": [
		{
			"id": "duration-from-root-speed",
			"source": {
				"kind": "rootParameter",
				"path": ["MoveSpeed"]
			},
			"target": {
				"kind": "node",
				"state": "Root/Patrol",
				"node": "delay-task",
				"section": "instance",
				"path": ["Duration"]
			}
		}
	]
}
```

Property function binding：

```json
{
	"bindings": [
		{
			"id": "duration-from-sum",
			"target": {
				"kind": "node",
				"state": "Root/Patrol",
				"node": "delay-task",
				"section": "instance",
				"path": ["Duration"]
			},
			"function": {
				"type": "/Script/StateTreeModule.StateTreeAddFloatPropertyFunction",
				"output": ["Result"],
				"inputs": {
					"Left": {
						"source": {
							"kind": "rootParameter",
							"path": ["BaseDuration"]
						}
					},
					"Right": {
						"source": {
							"kind": "rootParameter",
							"path": ["BonusDuration"]
						}
					}
				}
			}
		}
	]
}
```

规则：

- `id` 可选，用于诊断、extract 稳定排序和 future diff。它不是 UE binding GUID，因为 UE binding collection 以 target path 为身份。
- 普通 binding 必须提供 `source` 和 `target`。
- function binding 必须提供 `function` 和 `target`。`function.output` 表示 function instance data 上作为 binding source 的 property path。
- 同一个 JSON 中不允许两个 entry 绑定到同一个 target path，避免 UE `AddBinding` 的“后者覆盖前者”行为造成隐藏结果。
- `path` 使用显式 segment array，不解析点号字符串。

## Path Segment

`path` 是 `FPropertyBindingPathSegment` 的 JSON 表达。短写字符串等价于 `{ "name": "..." }`：

```json
["Stats", { "name": "Values", "arrayIndex": 0 }, "Speed"]
```

规范形态：

```json
[
	{
		"name": "MoveSpeed",
		"guid": "02c9c5f8-4e0a-4fa7-a3d3-b9010a5a3c7a"
	}
]
```

支持字段：

- `name`：property name，必填。
- `arrayIndex`：数组或 static array 下标，默认不设置。
- `guid`：PropertyBag、Blueprint class、User Defined Struct 等 editor-only property GUID。参数路径由 resolver 自动补齐，输入中提供时必须与已解析 property 匹配。
- `instanceStruct`：instanced struct/object indirection 的 `UStruct` path，可选。第一版仅在 extract 已存在特殊 indirection 时输出；generator 会验证能否解析。
- `access`：`StructInstance`、`ObjectInstance` 等 UE `EPropertyBindingPropertyAccessType` 名称，可选。

为了保持输入简单，常规 fixture 使用字符串短写；extract 输出规范 object 形态只在需要保留 `guid`、`arrayIndex` 或 `instanceStruct` 时使用。

## Source Reference

`source.kind` 支持：

- `rootParameter`：StateTree root parameter bag。
- `stateParameter`：某个 state 的 local parameter bag。需要 `state` 引用。
- `context`：schema context data。需要 `name` 或 `class`，按 schema 提供的 bindable structs 解析。
- `evaluator`：global evaluator output。需要 `node` 引用。
- `globalTask`：global task output。需要 `node` 引用。
- `task`：state task output。需要 `state` 和 `node` 引用。
- `enterCondition`、`transitionCondition`、`consideration`：用于 extract 既有资产和高级用法；生成时如果 UE validator 拒绝作为 source，返回诊断。
- `function`：仅用于嵌套 function input，引用同一个 bindings array 中先前声明或当前 function 内部创建的 function node。

通用字段：

- `state`：state id、canonical path 或唯一 leaf name。
- `node`：node id。extract 以 node GUID 字符串输出；generator 同时接受用户稳定 id。
- `section`：`node`、`instance`、`executionRuntimeData`。source 默认 `instance`。
- `path`：relative property path segments。

## Target Reference

`target.kind` 支持：

- `node`：state 内 task/condition/consideration node，或 global evaluator/global task node。通过 `section` 指定 target data section。
- `rootParameter`：root parameter。用于 function binding 输出回参数等高级用法。
- `stateParameter`：state-local parameter。
- `transitionCondition`：transition condition node。需要 `state`、`transition`、`node`。
- `enterCondition`、`consideration`：state node。

Target 默认 `section` 为 `instance`。普通输入应优先绑定到 node instance property，除非确实需要绑定 node template 或 execution runtime data。

## Resolver 架构

新增 StateTree-local binding 模块，避免把复杂解析塞进 `StateTreeStateBuilder`：

- `StateTreeBindingTypes.h`：JSON spec structs 与枚举。
- `StateTreeBindingResolver.h/.cpp`：把显式 source/target object 解析为 `FPropertyBindingPath`。
- `StateTreeBindingBuilder.h/.cpp`：解析顶层 `bindings`，调用 UE editor bindings API。
- `StateTreeBindingExtract.h/.cpp` 或并入现有 `StateTreeExtract` 的小型 helper：把 editor bindings 转回 canonical JSON。

Resolver 构建 `FStateTreeBindingIndex`：

- state：按 GUID、stable id、canonical path、唯一 leaf name 索引。
- node：按 GUID、stable id、kind、所属 state/transition 索引。
- parameters：root/state/linked parameter 的 name、GUID、bag struct。
- bindable structs：从 editor data/schema 收集 context、evaluator、global task、state task 等 `FStateTreeBindableStructDesc`。

构建顺序：

1. 现有 schema、parameters、evaluators、global tasks、states、nodes、transitions 全部生成完成。
2. Finalize linked states 与 linked asset parameter overrides。
3. 创建 binding index。
4. 解析并添加普通/function bindings。
5. 调用 StateTree compile，让 UE 做最终 copy compatibility validation。

## 普通 Binding

普通 binding 解析为：

```cpp
EditorData.AddPropertyBinding(SourcePath, TargetPath);
```

generator 必须在添加前：

- 解析 source/target base GUID。
- 解析 path segment，并对 PropertyBag 参数补齐 property GUID。
- 用 base struct 或 base value 调用 `FPropertyBindingPath::UpdateSegments` / `UpdateSegmentsFromValue`，确保 path 存在。
- 检查 target path 在本次 config 中没有重复。

如果 UE compile 报 copy type 不兼容，AssetFactory 返回包含 binding id、source 描述、target 描述、UE 原始错误的诊断。

## Property Function Binding

function binding 分两步构建：

1. 用 `EditorBindings.AddFunctionBinding(FunctionStruct, OutputSegments, TargetPath)` 创建 property function node，并得到 function output source path。
2. 对 `function.inputs` 中每一项创建普通 binding：input source 到 function node instance data 的对应 input path。

`function.type` 必须解析为 `UScriptStruct`，且必须继承 `FStateTreePropertyFunctionBase`。`function.output` 必须指向 function instance data 上存在的 output property。

嵌套 function 支持通过递归表达：

```json
{
	"function": {
		"type": "/Script/StateTreeModule.StateTreeMultiplyFloatPropertyFunction",
		"output": ["Result"],
		"inputs": {
			"Left": {
				"function": {
					"type": "/Script/StateTreeModule.StateTreeAddFloatPropertyFunction",
					"output": ["Result"],
					"inputs": {
						"Left": { "source": { "kind": "rootParameter", "path": ["A"] } },
						"Right": { "source": { "kind": "rootParameter", "path": ["B"] } }
					}
				}
			},
			"Right": { "source": { "kind": "rootParameter", "path": ["Scale"] } }
		}
	}
}
```

第一版 implementation 可以先完成单层 function smoke，再完成嵌套 function extract/generate 兼容；最终 Spec5 完成前必须覆盖嵌套或明确验证递归路径不会破坏普通 function。

## Extraction

Extract 输出顶层 `bindings`，按 target path 稳定排序。

普通 binding 输出：

- `source`：用可读 canonical reference 表达，优先 stable id/path，必要时退回 GUID。
- `target`：同上。
- `path`：常规 segment 输出字符串短写；含 GUID、arrayIndex、instanceStruct 时输出 object。

Function binding 输出：

- 把 `FStateTreePropertyPathBinding::GetPropertyFunctionNode()` 有效的 binding 聚合为 `function` entry。
- `function.type` 输出 property function struct path。
- `function.output` 输出 binding source path 去掉 function node base 后的 segments。
- 所有 target 到 function node input property 的 bindings 聚合到 `function.inputs`。
- 无法反向归属的 function input binding 仍输出为普通 binding，并带可读诊断字段是不允许的；生产 extract 应失败，避免 round-trip 丢语义。

Round-trip 目标：

```text
Generate(input) -> Extract(A) -> Generate(A) -> Extract(B)
```

`A` 与 `B` 在 stable fields 上相等。允许 UE 自动生成的 GUID 在没有用户 id 的节点上不同，但 extractor 应尽量输出 deterministic/stable references，减少差异。

## 错误处理

所有 binding 错误都必须包含 binding id 或 array index。

必须覆盖的错误：

- `bindings` 不是 array。
- 普通 binding 缺少 `source` 或 `target`。
- function binding 缺少 `target`、`function.type`、`function.output`。
- 未知 `kind`、`section`、path segment field。
- state/node/transition/context/parameter 引用找不到。
- leaf state name 歧义，并列出匹配 canonical paths。
- path segment 在 base struct/value 上不存在。
- `guid` 与解析出的 PropertyBag property ID 不一致。
- target 重复。
- function type 找不到或不是 property function。
- function input/output path 不存在。
- UE compile 报 binding copy incompatible。

错误返回必须发生在资产保存前；不能保存半完成资产。

## MCP Schema

`MCP/schemas/StateTree.md` 需要移除 “Property bindings are future spec” 的描述，并新增：

- `bindings` 顶层字段。
- ordinary binding 示例。
- function binding 示例。
- path segment 规范。
- source/target kind 表格。
- invalid diagnostics 说明。

Schema 文档必须强调：不支持字符串 DSL，使用显式 JSON object。

## Verification

只做正常编译，不跑 `RunUAT BuildPlugin`。

本地验证：

```bash
npm --prefix MCP run build
~/UnrealEngine/Engine/Build/BatchFiles/Mac/Build.sh ProjectRPGEditor Mac Development -Project="/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject" -WaitMutex
```

MCP smoke：

- 正向：root parameter 到 task instance property。
- 正向：state parameter 到 condition/task property。
- 正向：context 或 evaluator/global task output 到 node property。
- 正向：float property function binding，GUI 中可看到 function/binding 结果。
- 正向：extract 后重新 generate，二次 extract stable。
- 反向：unknown source。
- 反向：unknown target。
- 反向：bad path。
- 反向：duplicate target。
- 反向：function type invalid。
- 反向：type incompatible。

GUI smoke：

- 启动真实编辑器 GUI，不使用 `UnrealEditor-Cmd`、`-NullRHI` 或 headless。
- 打开至少一个普通 binding fixture，确认节点上出现 binding 标识，细节面板显示绑定关系。
- 打开至少一个 property function fixture，确认 function binding 在 StateTree editor 中可见，并且保存后不丢失。

## 子阶段拆分

### 5A：普通 Binding Core

- 解析 `bindings` 数组。
- 建立 binding index。
- 支持 root/state parameter、context、evaluator/global task/task source。
- 支持 node instance target。
- 普通 binding extract。
- 正向/反向 MCP smoke。

### 5B：Property Function Binding

- 解析 `function` object。
- 创建 function binding node。
- 绑定 function inputs。
- 支持 extract function binding。
- 至少一个真实 UE float property function fixture。

### 5C：Round-trip 与文档

- 完成 canonical extract。
- 完成 `Extract -> Generate -> Extract` 稳定 verifier。
- 补 MCP schema。
- 完成 GUI smoke 与 invalid fixture 集。

## 风险与缓解

- UE bindable struct source 分类复杂：先用 editor data 与 schema public API 收集，缺口用小 fixture 验证后补齐。
- Property function extraction 需要把 function node input bindings 聚合回树形 JSON：先在 5B 生成时保存可反推结构，5C 再对 editor-authored assets 做保守失败策略。
- PropertyBag GUID 如果丢失会破坏 rename-safe binding：path resolver 必须从 `FPropertyBagPropertyDesc::ID` 补齐，并在 extract 输出必要 GUID。
- GUI smoke 容易被 worktree 冷编译影响：继续使用主 ProjectRPG `.uproject` 正常编译，不使用 BuildPlugin package。
