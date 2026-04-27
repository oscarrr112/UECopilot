# StateTree Generator Core Lifecycle 设计文档

- **日期**：2026-04-27
- **状态**：Draft（待用户审阅）
- **作者**：Codex + 用户协作
- **范围**：StateTree generator 的第一个子 spec，只建立最小可验证资产生命周期闭环

---

## 1. 目标

新增 `AssetType: "StateTree"` 的最小 generator 闭环，让 UECopilot 能创建、编译、保存、提取一个合法的 UE 5.7 `UStateTree` 资产。

本 spec 的核心目标是把生命周期走通：

1. 动态解析 `SchemaClass`。
2. 创建 `UStateTree`。
3. 按官方路径创建 `UStateTreeEditorData`、`Schema`、`EditorSchema` 和默认 root state。
4. 调用 `UStateTreeEditingSubsystem::CompileStateTree()`。
5. 只有编译成功才注册资产、标脏并保存。
6. `extract_assets` 能读出最小 skeleton：asset type、schema、schema properties、root/subtree 概览、compiled hash。
7. MCP 能通过 `get_generator_schema(StateTree)` 看见最小 schema。

最终 generator 会功能完备，但第一步只做“能创建且能被官方 compiler 接受”的核心闭环。

---

## 2. 非目标

本 spec 明确不做以下能力，它们会进入后续子 spec：

- 不构造 task、evaluator、global task、condition、consideration。
- 不支持 transition、linked state、linked asset。
- 不支持 root/state parameters 或 property bag schema。
- 不支持 StateTree property bindings。
- 不支持 property function bindings。
- 不支持 Blueprint StateTree nodes。
- 不做复杂 tree patch，只处理最小 root skeleton。
- 不直接写 `UStateTree` 的 runtime baked fields，例如 `Frames`、`States`、`Nodes`、`Transitions`、`PropertyBindings`。

---

## 3. 设计决策

### 3.1 使用 EditorData 作为唯一输入源

StateTree 的运行期数据是 compiler 产物。generator 必须构建 `UStateTree.EditorData`，然后让引擎 compiler 生成 runtime data。

禁止做法：

```cpp
StateTree->States = ...;
StateTree->Nodes = ...;
StateTree->PropertyBindings = ...;
```

正确路径：

```cpp
UStateTreeEditorData* EditorData = ...;
EditorData->Schema = ...;
EditorData->EditorSchema = ...;
EditorData->AddRootState();
UStateTreeEditingSubsystem::CompileStateTree(StateTree, Log);
```

### 3.2 优先复用官方 Factory

Create 场景优先使用 `UStateTreeFactory`：

```cpp
UStateTreeFactory* Factory = NewObject<UStateTreeFactory>();
Factory->SetSchemaClass(SchemaClass);
UStateTree* StateTree = Cast<UStateTree>(
    Factory->FactoryCreateNew(
        UStateTree::StaticClass(),
        Package,
        *Name,
        RF_Public | RF_Standalone | RF_Transactional,
        nullptr,
        GWarn));
```

如果实现时发现 `UStateTreeFactory` 对错误诊断或 update 行为不够细，允许本地镜像 factory 初始化顺序，但必须保留下面两个动态查询：

- `FStateTreeEditorModule::GetModule().GetEditorDataClass(SchemaClass)`
- `FStateTreeEditorModule::GetModule().GetEditorSchemaClass(SchemaClass)`

不能硬编码 `UStateTreeEditorData` 或 `UStateTreeEditorSchema`，因为 schema 可以注册专用 editor data/editor schema。

### 3.3 编译失败不保存

Create 场景中，只有 `CompileStateTree()` 成功后才执行：

- `FAssetRegistryModule::AssetCreated(StateTree)`
- `StateTree->PostEditChange()`
- `StateTree->MarkPackageDirty()`
- `UPackage::SavePackage(...)`

如果编译失败：

- 不注册资产。
- 不保存 package。
- 返回失败结果，包含 compiler log 可读摘要。

### 3.4 Update 只做保守最小能力

本 spec 的 `Update` / `CreateOrUpdate` 只允许更新 metadata 层面的最小字段：

- `SchemaProperties`

不允许在本阶段更新已有 tree structure。原因是 StateTree 编译失败会清空 runtime baked data；完整 update 需要 transient duplicate 和回滚策略，放到后续 spec。

如果请求在 `Action: "Update"` 中改变 `SchemaClass`，本 spec 返回错误：

```text
StateTree Update cannot change SchemaClass in core lifecycle spec. Recreate the asset or use a future migration spec.
```

---

## 4. JSON 契约

### 4.1 最小 Create

```json
{
  "AssetType": "StateTree",
  "Name": "ST_Enemy",
  "Path": "/Game/AI",
  "SchemaClass": "/Script/GameplayStateTreeModule.StateTreeAIComponentSchema"
}
```

### 4.2 Create with schema properties

```json
{
  "AssetType": "StateTree",
  "Name": "ST_Enemy",
  "Path": "/Game/AI",
  "SchemaClass": "/Script/GameplayStateTreeModule.StateTreeAIComponentSchema",
  "SchemaProperties": {
    "ContextActorClass": "/Script/Engine.Pawn",
    "AIControllerClass": "/Script/AIModule.AIController"
  }
}
```

### 4.3 Fields

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `AssetType` | string | 是 | 必须是 `"StateTree"` |
| `Name` | string | 是 | 资产名 |
| `Path` | string | 是 | Content path，例如 `"/Game/AI"` |
| `SchemaClass` | string | 是 | `UStateTreeSchema` 子类，支持完整路径和精确类名 |
| `SchemaProperties` | object | 否 | 通过反射设置到 schema instance |

### 4.4 Extract 输出

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
  "SubTrees": [
    {
      "name": "Root",
      "type": "State",
      "id": "00000000-0000-0000-0000-000000000000"
    }
  ],
  "Compiled": {
    "lastCompiledEditorDataHash": 123456789
  }
}
```

`SubTrees` 在本 spec 中只作为 skeleton 输出，不作为输入解析。后续 State Structure spec 会正式定义 `SubTrees` 输入契约。

---

## 5. 类解析与 schema validation

`SchemaClass` 解析规则：

1. 如果是 `/Script/...` 或 `/Game/...` 路径，优先 `LoadClass<UStateTreeSchema>()`。
2. 否则使用项目已有 `FClassFinderUtils::FindClassByName(SchemaClass, UStateTreeSchema::StaticClass())` 做精确类名查找。
3. 找不到返回错误。
4. 找到但不是 `UStateTreeSchema` 子类返回错误。
5. 找到但 `CLASS_Abstract` 返回错误。

错误示例：

```text
Unknown StateTree SchemaClass: StateTreeDoesNotExist
SchemaClass '/Script/Engine.Actor' is not a UStateTreeSchema subclass
SchemaClass '/Script/StateTreeModule.StateTreeSchema' is abstract and cannot be instantiated
```

---

## 6. 生成生命周期

### 6.1 Create

流程：

1. `ValidateConfig()` 校验 `SchemaClass`、路径、action。
2. 创建或取得 package。
3. 使用 `UStateTreeFactory` 创建 `UStateTree`。
4. 取得 `UStateTreeEditorData`。
5. 反射写入 `SchemaProperties`。
6. 调用 `UStateTreeEditingSubsystem::CompileStateTree(StateTree, Log)`。
7. 编译失败：不注册、不保存、返回 compiler log。
8. 编译成功：注册资产、`PostEditChange()`、`MarkPackageDirty()`、保存。

### 6.2 CreateOrUpdate

如果资产不存在，走 Create。

如果资产存在，走 Update。

### 6.3 Update

流程：

1. 加载已有 `UStateTree`。
2. 确认已有 schema class 与 JSON `SchemaClass` 一致。
3. 反射写入 `SchemaProperties`。
4. 调用 `CompileStateTree()`。
5. 编译成功后保存。
6. 编译失败时不保存，并返回错误。

本阶段 update 不修改 state tree structure，因此不引入复杂 patch 和回滚机制。后续结构化 update spec 必须重审这一点。

---

## 7. Extract 设计

`CanExtract()` 只接受 `UStateTree`。

`Extract()` 输出：

- `AssetType`
- `Name`
- `Path`
- `SchemaClass`
- `SchemaProperties`
- `SubTrees` skeleton
- `Compiled.lastCompiledEditorDataHash`

Extract 只读取 `EditorData`，不输出 runtime baked arrays。

`SchemaProperties` 使用 `PropertySetterUtils` 的反向提取能力或等价反射逻辑。`bDiffOnly=true` 时，只输出和 schema CDO 不同的字段。

---

## 8. MCP 暴露

本 spec 需要最小 MCP 闭环：

- 新增 `MCP/schemas/StateTree.md`，只写本 spec 支持的字段和示例。
- 更新 `MCP/src/index.ts`：
  - `get_generator_schema` enum 增加 `StateTree`
  - fallback 文案增加 `StateTree`
  - `generate_assets` description 增加 `StateTree`
- 如果项目当前提交 `MCP/dist`，同步构建 dist。

这一步必须和 C++ generator 同步完成，避免出现 BT generator 那种“C++ 已注册但 MCP schema 不知道”的断层。

---

## 9. 预期文件

### C++

- `Source/AssetFactory/Public/Generators/StateTreeGenerator.h`
- `Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp`
- `Source/AssetFactory/Private/AssetFactoryModule.cpp`
- `Source/AssetFactory/AssetFactory.Build.cs`
- `AssetFactory.uplugin`，仅当 StateTree / GameplayStateTree 插件依赖必须显式声明时修改

### MCP / Docs / Fixtures

- `MCP/schemas/StateTree.md`
- `MCP/src/index.ts`
- `MCP/dist/index.js`，如项目需要提交 dist
- `MCP/dist/index.d.ts`，如项目需要提交 dist
- `TestData/ST_Core_Minimal.json`
- `TestData/ST_Core_AIComponentSchema.json`
- `TestData/ST_Core_InvalidSchema.json`

---

## 10. 测试与验证

### 10.1 静态验证

- `ValidateConfig()` 对缺失 `SchemaClass` 返回错误。
- unknown schema 返回错误。
- 非 `UStateTreeSchema` 子类返回错误。
- abstract schema 返回错误。
- Update 改变 `SchemaClass` 返回错误。

### 10.2 编译验证

运行 UBT：

```bash
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

### 10.3 MCP / Editor 验证

1. 后台启动编辑器：

```bash
"E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject"
```

2. `health_check` 等待 HTTP server 就绪。
3. `get_generator_schema(StateTree)` 返回 StateTree schema。
4. `generate_assets` 生成 `ST_Core_Minimal`。
5. `extract_assets` 输出 `SchemaClass` 和 root skeleton。
6. `execute_python` 调用 `UStateTreeEditingSubsystem::CompileStateTree()` 或读取 `LastCompiledEditorDataHash` 确认资产可编译。

---

## 11. 验收标准

本 spec 完成时必须满足：

- `StateTree` generator 注册到 AssetFactory。
- `get_generator_schema(StateTree)` 可用。
- 最小 JSON 能生成一个保存后的 `.uasset`。
- 生成资产重新加载后仍有 `EditorData`、`Schema`、`EditorSchema` 和 root state。
- `CompileStateTree()` 成功，`LastCompiledEditorDataHash != 0`。
- `extract_assets` 能输出可读 skeleton。
- invalid schema fixture 返回清晰错误，不 crash，不保存半成品。
- 实现没有硬编码具体 StateTree task/evaluator/condition 类型。

---

## 12. 后续衔接

本 spec 只建立地基。完成后按下面顺序继续：

1. `StateTree JSON Contract + Dynamic Node Construction`
2. `StateTree Structure + Transitions + Linked Assets`
3. `StateTree Parameters + Property Bags`
4. `StateTree Property Bindings + Property Function Bindings`
5. `StateTree Extract + Round-trip`

Property Bindings 子 spec 不能复用 WidgetBlueprintGenerator 的 `FDelegateEditorBinding` 模型；它需要围绕 `FStateTreeEditorPropertyBindings`、`FPropertyBindingPath`、struct GUID 和 property bag property ID 独立设计。
