# BehaviorTree / BlackboardData AssetDocument 完整表面清单

**日期**：2026-07-15
**状态**：生产实现的权威清单
**UE 基线**：UE 5.7.3 compatible CL 47537391
**范围**：`UBehaviorTree`、`UBlackboardData` 及其编辑器 authored subobjects

## 1. 判定规则

本清单只把能够跨保存、重载并由资产作者控制的数据归为 authored。运行时编译镜像、缓存、调试状态、会话状态和引用资产的内部内容不进入写入模型。所有 authored 表面必须满足以下三选一：

1. 由 AssetDocument 管理，并有 apply/extract/diff/save-reload 自动化；
2. 明确归为 referenced-asset-owned，只管理引用；
3. 有 UE 源码证据证明它是 derived/runtime/cache/session 数据，并有拒绝或忽略测试。

生产版本不允许存在“已确认 authored 但以后再做”的字段。

## 2. BehaviorTree 表面

| UE 表面 | 分类 | AssetDocument 合同 |
| --- | --- | --- |
| `UBehaviorTree::BTGraph` | managed authored source | 树结构、节点 wrapper、连接、稳定 GUID、布局和注释的唯一真源 |
| 图节点 `NodeGuid` | managed authored identity | `Body.Tree` 中的 canonical `Id`；新建、更新和重载必须稳定 |
| 图节点 concrete wrapper/class | managed authored semantic | 由 schema 按 `UBTCompositeNode`、`UBTTaskNode`、`UBTDecorator`、`UBTService` 动态选择 |
| 图节点 `NodeInstance` class | managed authored semantic | 接受可加载且 concrete 的 native、Blueprint、Angelscript/project class path |
| `NodeInstance` 可实例编辑属性 | managed authored semantic | 通用反射读写；包含 `NodeName`，不把它当身份 |
| 主图拓扑 | managed authored semantic | 单根、无环、所有运行节点可达、parent/child 类别合法 |
| `NodePosX/Y` | managed authored semantic | 同输出 pin 子节点按 X/Y 排序；必须与 `Children[]` 顺序一致且不允许并列坐标 |
| composite child decorators/services 及顺序 | managed authored semantic | 顺序保留，GUID 稳定，属性完整往返 |
| root decorators 及顺序 | managed authored semantic | 由根连接和 decorator graph 管理，runtime arrays 由 UE 重建 |
| composite decorator `BoundGraph` | managed authored semantic/editor | 管理 Sink、logic/test 节点、连线、GUID、位置、注释、`CompositeName`、`bShowOperations` |
| 普通图节点注释 | managed authored editor | `NodeComment`、bubble pinned/visible |
| Comment Box | managed authored editor | text、position、size、color、font size、bubble/detail/color flags、move mode、node details |
| Graph GUID | managed authored editor identity | 提取并保留；新图由 UE 生成后进入 canonical 文档 |
| Graph pins | derived structural | 只管理逻辑连接；pin 结构由 wrapper/schema 创建，不接受任意 pin authored patch |
| `RootNode`、runtime Children/Services | derived compiled mirror | 标准 `UBehaviorTreeGraph::UpdateAsset()` 重建；输入出现即拒绝 |
| `RootDecorators`、`RootDecoratorOps`、child `DecoratorOps` | derived compiled mirror | 从主图和 decorator graph 编译；输入出现即拒绝 |
| `ExecutionIndex`、`MemoryOffset`、`TreeDepth`、`ParentNode` | derived runtime | 不提取、不写入；输入出现即拒绝 |
| injected decorator preview、subtree version/path | derived editor/runtime cache | 不接受为本地 authored 输入 |
| `FValueOrBlackboardKey_*` hidden cached Key ID | derived runtime cache | canonical 只管理公开 key name 与 literal/default；Blackboard 或 key order 变化后按 name 失效重解 |
| debugger、breakpoint、运行计数、error state | debug/session | 排除 |
| `LastEditedDocuments`、视口状态 | session/per-user | 排除 |

### 2.1 节点基础 authored 属性

通用反射路径至少覆盖 `UBTNode::NodeName`、composite 的 `bApplyDecoratorScope`、task 的 `bIgnoreRestartSelf`、decorator 的 inverse condition/`FlowAbortMode`、service 的 interval/deviation/search-start/timer-reset，以及 concrete project node 上所有安全的实例可编辑属性。排除 `Transient`、运行时初始化状态、owner/cache/debug 属性。

### 2.2 Decorator 逻辑

AssetDocument 管理 decorator 的 authored expression graph，而不是 `FBTDecoratorLogic` 编译数组。必须拒绝：逻辑环、缺失或不可达 Sink、NOT 非一元、AND/OR 无有效输入、同一输入多链接、dangling 节点、非法 test 类，以及把 injected preview 当本地节点。

### 2.3 子树引用

- `UBTTask_RunBehavior::BehaviorAsset`：managed reference；目标树内容为 referenced-asset-owned。
- `UBTTask_RunBehaviorDynamic::InjectionTag` 与 `DefaultBehaviorAsset`：managed authored/reference。
- runtime `BehaviorAsset`：derived instance state。
- 静态子树需要通过 Blackboard compatibility 预检；动态子树引用含 root decorator 的树时拒绝，因为引擎不会注入这些 decorator。

### 2.4 Sparse Blackboard overlay

仅含 `Body.BlackboardAsset` 的 `Update` 保留现有 authored Tree，但不是“只改一个引用就跳过树验证”。Validate、Diff、Apply 都必须把 retained tree 投影到目标 Blackboard，重新验证并刷新 selector、`FValueOrBlackboardKey_*` 和 decorator operation/cache。该 sparse 形态只适用于已存在 BT；`Create` 或不存在目标仍要求完整有效语义。

若 retained `UBTDecorator_Blackboard` 指向 enum key，而新 Blackboard 的同名 key 更换了 `UEnum` 或 value mapping，旧 `StringValue`/`IntValue` 不得被猜测或静默沿用。请求必须同时显式提供 Tree property 更新，否则在 retained property 的精确路径拒绝。

## 3. BlackboardData 表面

| UE 表面 | 分类 | AssetDocument 合同 |
| --- | --- | --- |
| `Parent` | managed reference | 只管理引用；拒绝 parent cycle |
| 本地 `Keys` 数组顺序 | managed authored semantic | 严格保序；顺序决定 Key ID，不进行 canonical sort |
| `EntryName` | managed authored identity | 本地稳定 identity；拒绝空名、同层重复和父链 shadow |
| `EntryDescription` | managed authored editor | 完整往返 |
| `EntryCategory` | managed authored editor | 完整往返 |
| `bInstanceSynced` | managed authored semantic | 完整往返 |
| `KeyType` concrete class | managed authored semantic | native/project custom key type 动态加载；废弃 NativeEnum 不接受新输入 |
| `KeyType` 实例可编辑属性 | managed authored semantic | 通用反射完整往返；内建类型详见下表 |
| `ParentKeys`、`FirstKeyID`、`bHasSynchronizedKeys` | derived cache | 由 `UpdateParentKeys()`/验证重建 |
| numeric Key ID | derived runtime identity | 文档只使用 key name |

### 3.1 内建 KeyType authored 元数据

| KeyType | 必须管理的 authored 字段 |
| --- | --- |
| Bool | `bDefaultValue` |
| Int、Float、Name、String | `DefaultValue` |
| Vector、Rotator | `DefaultValue`、`bUseDefaultValue` |
| Object | `BaseClass`、`DefaultValue` |
| Class | `BaseClass`、`DefaultValue` |
| Enum | `EnumType`、`EnumName`、`DefaultValue` |
| Struct | `FInstancedStruct DefaultValue` |
| Custom | 所有安全的实例可编辑 reflected properties |

`ValueSize`、`SupportedOp`、内部 runtime value storage 和 derived enum validity 不属于 authored 输入。Object/Class 默认值必须满足 BaseClass；Enum 值必须可解析且在 uint8 范围；Struct 默认值必须与声明的 script struct 一致。

## 4. BlackboardKeySelector

`FBlackboardKeySelector` 的稳定资产选择值只有 `SelectedKeyName`。canonical 节点属性用 `{ "Key": "TargetActor" }` 表达选择，实际写入 `SelectedKeyName`。

| selector 字段 | 分类 | 合同 |
| --- | --- | --- |
| `SelectedKeyName` | managed authored semantic | 按 name 写入，保存前验证存在且类型匹配 |
| `AllowedTypes` | derived class policy | 节点构造函数建立；只在 schema/inspect 中作为只读 filter metadata 暴露 |
| `bNoneIsAllowedValue` | derived class policy | 不允许外部覆盖；按 concrete node 默认策略验证 |
| `SelectedKeyType`、`SelectedKeyID` | derived cache | 保存/初始化时重算 |

同类的 `FValueOrBlackboardKey_*` 结构只公开 key name 与 literal/default authored 值；其 hidden Key ID 同样是 derived cache，不进入 schema、extract 或 diff。

不得把 filter metadata 做成可写 authored 字段。外部提供 `AllowedTypes`、`SelectedKeyID`、`SelectedKeyType` 或 `bNoneIsAllowedValue` 时返回精确 JSON Pointer diagnostic。

## 5. 拒绝矩阵

必须在 mutate 前拒绝：未知字段、不可加载/abstract/类别错误的节点或 key class、主图/装饰器图循环、非法链接、断开运行节点、重复 sibling 坐标、语义顺序与布局顺序冲突、Blackboard parent cycle、空/重复/shadow key、null/deprecated KeyType、非法默认值、selector 缺失或类型不符、子树 Blackboard 不兼容、动态子树 root-decorator 不兼容、任何 runtime mirror/debug/cache 字段输入。

所有拒绝路径必须通过自动化证明：新资产不留包或 sidecar；已有资产的 object graph、properties、引用、布局、磁盘包和提取结果均不变。

## 6. 源码证据索引

- `BehaviorTreeEditor.cpp:1391-1402`：保存从 graph `OnSave()` 进入。
- `BehaviorTreeGraph.cpp:102-187, 510-755, 902-959`：graph 编译 runtime tree、坐标排序和孤儿清理。
- `BehaviorTreeEditorTypes.h:30-43`：X/Y 排序合同。
- `AIGraphSchema.cpp:74-105`：新 graph node GUID。
- `BehaviorTreeDecoratorGraph.cpp:27-198`：decorator expression 与 ops 的双向编译。
- `BlackboardData.cpp:49-53, 82-138, 284-290`：entry equality、顺序/ID、parent shadow validation。
- `BehaviorTreeTypes.h:620-693`：selector 持久与 transient 字段。
- `BehaviorTreeTypes.cpp:545-596`：selector name/filter resolution。
- `BehaviorTreeManager.cpp:181-214`：静态 subtree decorator injection。
- `BlackboardComponent.cpp:490-506`：runtime subtree Blackboard compatibility。
