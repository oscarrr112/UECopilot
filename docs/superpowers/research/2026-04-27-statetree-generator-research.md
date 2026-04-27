# StateTree Generator 调研汇总

**日期**：2026-04-27
**状态**：Research draft
**范围**：为后续 `StateTree` AssetFactory generator 设计提供源码依据与拆分依据。本文不包含实现计划。

---

## 1. 总结

StateTree generator 的正确方向是：**构建 `UStateTree.EditorData`，再调用官方 compiler 生成 runtime baked data**。不要直接写 `UStateTree` 的 `Frames`、`States`、`Nodes`、`Transitions`、`PropertyBindings`、`Parameters` 等运行期数组。

这点和 BehaviorTree generator 的经验类似：BT 的语义树和编辑器 graph 是两个层面；StateTree 更进一步，编辑态 `UStateTreeEditorData` 是权威输入，runtime compact data 是 compiler 产物。

功能完备的 generator 不只是 C++ 资产创建器，还要覆盖：

- `StateTree` C++ generator 与模块依赖。
- `UStateTreeFactory` / `FStateTreeEditorModule` / `UStateTreeEditingSubsystem` 生命周期。
- schema-aware validation。
- 动态节点构造：task、evaluator、condition、consideration、Blueprint wrapper 节点。
- state、subtree、linked state、linked asset、transition。
- root/state property bag。
- editor property bindings 与 property function bindings。
- extract / round-trip。
- MCP schema 暴露、fixtures、自动化验证。

---

## 2. 关键源码

### 2.1 StateTree asset 与 editor data

- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Public/StateTree.h`
- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeEditorData.h`
- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeState.h`
- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeEditorNode.h`

`UStateTree` 是 `UDataAsset`。编辑器源数据挂在 editor-only 字段：

```cpp
UPROPERTY()
TObjectPtr<UObject> EditorData;
```

运行期字段如 `Schema`、`Frames`、`States`、`Transitions`、`Nodes`、`DefaultInstanceData`、`PropertyBindings`、`Parameters`、`ContextDataDescs`、`ExternalDataDescs` 都应视为编译结果。

`UStateTreeEditorData` 包含：

- `Schema`
- `EditorSchema`
- `RootParameterPropertyBag`
- `Evaluators`
- `GlobalTasks`
- `EditorBindings`
- `SubTrees`

`SubTrees` 不是“只有 subtree”的数组。它同时包含主 root 和可链接 subtree roots。`AddRootState()` 实际是 `AddSubTree("Root")`。

### 2.2 官方创建路径

官方 factory 在：

- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Private/StateTreeFactory.cpp`

关键顺序：

1. `UStateTreeFactory::SetSchemaClass(UClass*)`
2. `NewObject<UStateTree>(Package, UStateTree::StaticClass(), Name, Flags)`
3. `FStateTreeEditorModule::GetModule().GetEditorDataClass(SchemaClass)`
4. `NewObject<UStateTreeEditorData>(StateTree, EditorDataClass, ..., RF_Transactional)`
5. `EditorData->Schema = NewObject<UStateTreeSchema>(EditorData, SchemaClass, ..., RF_Transactional)`
6. `EditorData->AddRootState()`
7. `FStateTreeEditorModule::GetModule().GetEditorSchemaClass(SchemaClass)`
8. `EditorData->EditorSchema = NewObject<UStateTreeEditorSchema>(EditorData, EditorSchemaClass, ..., RF_Transactional)`
9. `UStateTreeEditingSubsystem::CompileStateTree(NewStateTree, Log)`

**Decision**：generator 优先复用 `UStateTreeFactory`。如果后续实现因为 update/merge 需要更细控制，也必须逐步镜像 factory 顺序，不能硬编码 `UStateTreeEditorData` 或 `UStateTreeEditorSchema` 类型。

### 2.3 编译路径

相关源码：

- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeCompiler.h`
- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeCompilerManager.h`
- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Private/StateTreeCompilerManager.cpp`
- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Private/StateTreeEditingSubsystem.cpp`

`UStateTreeEditingSubsystem::CompileStateTree()` 会同步进入 compiler manager：

- 先 `ValidateStateTree(StateTree)`
- 再 `FStateTreeCompiler(Log).Compile(StateTree)`
- 成功后计算 editor data hash，写 `LastCompiledEditorDataHash`
- 失败后 `ResetCompiled()`，清空 runtime baked data，并把 hash 置 0

**风险**：Update 场景中，如果直接改已有 asset 的 `EditorData` 后编译失败，内存里的资产会处于“已修改但未编译”的危险状态。后续 spec 必须要求 update 有回滚策略，优先在 transient duplicate 上构建和编译验证，通过后再替换真实 asset。

---

## 3. Schema 与模块边界

### 3.1 Schema 是硬约束

相关源码：

- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Public/StateTreeSchema.h`
- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeEditorSchema.h`

`UStateTreeSchema` 提供：

- `IsStructAllowed`
- `IsClassAllowed`
- `IsExternalItemAllowed`
- `AllowEvaluators`
- `AllowEnterConditions`
- `AllowMultipleTasks`
- `AllowGlobalParameters`
- `AllowTasksCompletion`
- `IsStateTypeAllowed`
- `IsStateSelectionAllowed`
- `GetContextDataDescs`
- `GetGlobalParameterDataType`

`UStateTreeEditorSchema::Validate()` 可能清理非法数据。例如 schema 不允许 evaluator 时会清空 evaluator；不允许 multiple tasks 时会清空 `Tasks` 并使用 `SingleTask`。

**Decision**：generator 必须在编译前做 schema-aware validation。不能依赖 `ValidateStateTree()` 静默删数据，否则用户会得到“生成成功但内容消失”的坏体验。

### 3.2 常用 schema

Gameplay StateTree 相关源码：

- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/GameplayStateTree/Source/GameplayStateTreeModule/Public/Components/StateTreeComponentSchema.h`
- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/GameplayStateTree/Source/GameplayStateTreeModule/Public/Components/StateTreeAIComponentSchema.h`
- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/GameplayStateTree/Source/GameplayStateTreeModule/Private/Components/StateTreeComponentSchema.cpp`
- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/GameplayStateTree/Source/GameplayStateTreeModule/Private/Components/StateTreeAIComponentSchema.cpp`

`StateTreeComponentSchema` 默认 context 是 `Actor`。`StateTreeAIComponentSchema` 增加 `AIController`，并且 `Actor` context 通常指 Pawn。

### 3.3 预期模块依赖

初步依赖清单：

- `StateTreeModule`
- `StateTreeEditorModule`
- `StateTreeDeveloper`
- `StructUtils`
- `StructUtilsEditor`
- `PropertyBindingUtils`
- `PropertyBindingUtilsEditor`
- `GameplayTags`
- `GameplayStateTreeModule`，如果默认支持 gameplay/component/AI schema

`AssetFactory.uplugin` 可能还需要声明 StateTree / GameplayStateTree 插件依赖，或在 spec 中明确要求目标项目启用对应插件。

---

## 4. 编辑器节点模型

相关源码：

- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeEditorNode.h`
- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Public/StateTreeNodeBase.h`
- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Private/StateTreeCompiler.cpp`
- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Private/Customizations/StateTreeEditorNodeUtils.cpp`

`FStateTreeEditorNode` 的最小完整模型：

- `Node`：`FInstancedStruct`，内容是 `FStateTreeTaskBase` / `FStateTreeEvaluatorBase` / `FStateTreeConditionBase` / `FStateTreeConsiderationBase` 派生 struct。
- `Instance`：当 `Node.GetInstanceDataType()` 返回 `UScriptStruct` 时使用。
- `InstanceObject`：当 `Node.GetInstanceDataType()` 返回 `UClass` 时使用。
- `ExecutionRuntimeData` / `ExecutionRuntimeDataObject`：来自 `Node.GetExecutionRuntimeDataType()`。
- `ID`：实例 GUID，binding、node index、日志定位都依赖它。
- `ExpressionOperand` / `ExpressionIndent`：condition / consideration 表达式用，不能简单塞进 node properties。

**Decision**：节点 JSON 必须区分 node template properties 和 instance properties。

建议形态：

```json
{
  "id": "wait_before_attack",
  "node": {
    "struct": "/Script/StateTreeModule.StateTreeDelayTask",
    "properties": {
      "Name": "WaitBeforeAttack",
      "bTaskEnabled": true
    }
  },
  "instance": {
    "properties": {
      "Duration": 1.5,
      "RandomDeviation": 0.2
    }
  },
  "executionRuntimeData": {
    "properties": {}
  }
}
```

Blueprint 节点不是普通 struct。它们需要 wrapper struct，并把 `TaskClass` / `EvaluatorClass` / `ConditionClass` / `ConsiderationClass` 填到 wrapper，再创建 `InstanceObject`。

**Decision**：后续实现必须动态解析 `UScriptStruct` / `UClass`，调用 `GetInstanceDataType()` 和 `GetExecutionRuntimeDataType()` 初始化数据，复用 `FPropertySetterUtils` 设置属性。禁止按具体 StateTree 节点类型写 include 或 switch。

---

## 5. State 与 Transition 模型

相关源码：

- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeState.h`
- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Public/StateTreeTypes.h`
- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Private/StateTreeCompiler.cpp`

### 5.1 State type

State type 包括：

- `State`
- `Group`
- `Linked`
- `LinkedAsset`
- `Subtree`

`Linked` 跳到同资产内的 `Subtree`。`LinkedAsset` 跳到另一个 StateTree asset。`Subtree` 是可被 `Linked` 引用的独立入口。编译器会强制 `Linked` / `LinkedAsset` 的 `SelectionBehavior` 为 `TryEnterState`。

`TasksCompletion` 是 `EStateTreeTaskCompletionType`，目前只有 `All` / `Any`。如果 schema 不允许 task completion，会强制 `Any`。

### 5.2 Transition target

Transition target 建议映射 `FStateTreeStateLink.LinkType` / `EStateTreeTransitionType`：

- `None`
- `Succeeded`
- `Failed`
- `GotoState`
- `NextState`
- `NextSelectableState`

不要生成 deprecated `NotSet`。

### 5.3 引用解析

**Decision**：JSON 的 `id` 是主 identity，映射到 UE state/transition/node 的 GUID。path/name 只用于可读诊断和 fallback。

解析策略：

1. 第一遍创建全部 state object，建立 `id -> UStateTreeState*`、`path -> candidates`、`name -> candidates`。
2. 第二遍解析 `linkedSubtree`、`linkedAsset`、transition targets。
3. 优先按 `stateId` 解析，其次 canonical path，最后允许全局唯一 name。
4. name/path 歧义必须报错。

不要依赖 `UStateTreeState::GetPath()` 做长期引用。它基于 name，重名 sibling 和重命名都会破坏引用。

---

## 6. Parameters、Property Bag 与 Bindings

相关源码：

- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeEditorData.h`
- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeEditorPropertyBindings.h`
- `E:/Epic Games/UE_5.7/Engine/Plugins/Runtime/PropertyBindingUtils/Source/PropertyBindingUtils/Public/PropertyBindingPath.h`

Root parameters 来自 `EditorData.RootParameterPropertyBag`。编译后会复制到 `UStateTree::Parameters`，但 round-trip 应以 editor data 为准。

State parameters 是 `UStateTreeState.Parameters`，和 root parameters 是不同层级。linked state / linked asset 会更新参数 layout，不能只写裸字段。

Property binding 使用 `FPropertyBindingPath`。可读字符串类似 `Foo.Bar[1].Baz`，但稳定性依赖：

- source / target struct ID
- path segments
- property GUID
- instanced struct indirection
- property bag property ID

**Decision**：bindings 与 property function bindings 必须拆成单独子 spec。最小 generator 可以先支持基础 property bag；功能完备目标必须覆盖 binding round-trip 和 property function binding。

---

## 7. JSON 契约草案

本文只给方向，不作为最终 schema。

```json
{
  "AssetType": "StateTree",
  "Name": "ST_Enemy",
  "Path": "/Game/AI",
  "SchemaClass": "/Script/GameplayStateTreeModule.StateTreeAIComponentSchema",
  "SchemaProperties": {
    "ContextActorClass": "/Script/Engine.Pawn",
    "AIControllerClass": "/Script/AIModule.AIController"
  },
  "Parameters": [],
  "Evaluators": [],
  "GlobalTasks": [],
  "SubTrees": [
    {
      "id": "root",
      "name": "Root",
      "type": "State",
      "selectionBehavior": "TrySelectChildrenInOrder",
      "tasksCompletion": "Any",
      "children": [],
      "transitions": []
    }
  ],
  "Bindings": []
}
```

主要块：

- `SchemaClass`：必填，必须解析到非 abstract 的 `UStateTreeSchema` 子类。
- `SchemaProperties`：写入 schema instance。
- `Parameters`：root property bag。
- `Evaluators` / `GlobalTasks`：`FStateTreeEditorNode` 数组。
- `SubTrees`：包含主 root 和 `Type=Subtree` roots。
- `states[].Parameters`：state-level property bag。
- `states[].EnterConditions` / `Tasks` / `Considerations` / `SingleTask`：节点数组或单节点。
- `states[].Transitions`：transition、required event、delay、conditions。
- `Bindings`：editor property bindings。

---

## 8. 接入 UECopilot / MCP 的工作面

现有 BT generator 暴露了一个经验教训：C++ generator 进入主链路不等于 MCP schema 已闭环。StateTree 必须把工具层纳入 spec。

预期涉及：

- 新增 `Source/AssetFactory/Public/Generators/StateTreeGenerator.h`
- 新增 `Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp`
- 修改 `Source/AssetFactory/Private/AssetFactoryModule.cpp` 注册 generator
- 修改 `Source/AssetFactory/AssetFactory.Build.cs` 加依赖
- 必要时修改 `AssetFactory.uplugin` 插件依赖
- 新增 `MCP/schemas/StateTree.md`
- 修改 `MCP/src/index.ts` 的 `get_generator_schema` enum、fallback 文案、`generate_assets` description
- 如项目惯例要求，更新 `MCP/dist/index.js` / `MCP/dist/index.d.ts`
- 新增 `TestData/ST_*.json` fixtures

建议顺手修补 BT/BlackboardData MCP schema 暴露缺口，但这应作为独立或附属任务明确写入 spec，避免 StateTree 实现时顺手改散。

---

## 9. Validation 风险清单

生成前应尽量拦截：

- `SchemaClass` 不存在、不是 `UStateTreeSchema` 子类、abstract、模块未加载。
- schema 不允许 evaluator / enter condition / multiple tasks / task completion / global parameters。
- node struct/class 不被 schema 允许。
- node category 错误，例如 task 放到 condition 位置。
- `GetInstanceDataType()` / `GetExecutionRuntimeDataType()` 返回类型未正确初始化。
- Blueprint 节点没有 wrapper 或 instance object。
- state/transition/node id 重复。
- `GotoState` target 不存在或目标 `SelectionBehavior=None`。
- `Linked` 未引用同资产 `Type=Subtree` state。
- `LinkedAsset` 引用自身或 schema class 不兼容。
- `OnEvent` transition 缺少 tag/payload，或和 target required enter event 不兼容。
- schema 不允许 scheduled tick，但配置了 tick policy / custom tick rate。
- property bag 类型无法解析，或 property ID 丢失导致 binding 不稳定。
- binding source/target path 歧义或目标属性不存在。

---

## 10. Update / Extract / Round-trip 决策

### 10.1 Update

Create 可以先在新 package 上构建，编译成功后注册并保存。Update 需要更谨慎：

- 不应直接修改现有 asset 后再赌 compile 成功。
- 推荐在 transient duplicate 上构建完整 `EditorData` 并编译验证。
- 通过后替换真实 asset 的 `EditorData`，再次 compile/save。
- 失败时不保存，并尽量恢复原始 compiled data。

### 10.2 Extract

Extract 应以 `EditorData` 为源：

- 输出用户可编辑输入，不输出 runtime compact handles。
- 输出稳定 `id`、可读 `path`、显示 `name`。
- `diffOnly` 应和现有 generator 一样尽量比对默认值。
- 对 property bag 和 binding 必须保留 GUID / property ID，否则 round-trip 不可靠。

### 10.3 Round-trip

功能完备目标下，验收应包括：

- `Generate -> Extract` 输出完整可读 JSON。
- `Extract -> Generate -> Extract` 在稳定字段上相等。
- 编译后 asset 可在编辑器中打开，无 schema validation 清理导致的数据丢失。
- `UStateTree::Link()` 成功。

---

## 11. 待确认问题

这些问题建议进入后续子 spec 的 research 或 design 决策：

- `UStateTreeFactory` 能否满足 Update 场景，还是 Create 用 factory、Update 镜像 factory。
- Blueprint StateTree node wrapper 的动态创建是否有公共 helper 可复用，还是需要本地封装。
- `FStateTreeCompilerLog` 如何稳定提取错误消息；`Messages` 是 protected，可能需要从 `ToTokenizedMessages()` 转换。
- property bag 完整类型契约是否要一次支持数组、nested struct、object/class/soft references。
- property function bindings 是否首个功能版本就做，还是作为功能完备路线中的后续子 spec。
- editor visual validation 是否需要截图，或只检查 editor data / compile / extract 即可。

---

## 12. 初步结论

StateTree generator 可以做，而且应做成一个分阶段的功能完备 generator。第一阶段不能贪大，但架构必须从一开始站在完整模型上：

1. 以 `EditorData` 为权威输入。
2. 以 schema validation 为前置约束。
3. 以动态反射构造节点，避免硬编码节点类型。
4. 以 stable ID/GUID 维护 state、transition、node、binding 引用。
5. 以 compiler manager 生成 runtime data。
6. 以 extract/round-trip 作为功能完备的验收轴。
