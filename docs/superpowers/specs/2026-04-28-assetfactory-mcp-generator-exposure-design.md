# AssetFactory MCP 生成器暴露设计

**日期**：2026-04-28  
**状态**：草稿（待用户审阅）  
**范围**：StateTree generator Spec 7，同时补齐已有 BehaviorTree / BlackboardData generator 的 MCP 暴露  
**依赖**：StateTree Spec 1-6 已完成；BehaviorTree / BlackboardData C++ generator 已存在

---

## 1. 背景

StateTree generator 已经完成创建、动态节点、结构/transition、parameters/property bags、property bindings、extract/round-trip 的核心能力，并通过真实 Editor + MCP round-trip smoke 验证。MCP 层现在已经可以在 `generate_assets` / `get_generator_schema` 中看到 `StateTree`，但缺少专门的 schema-level 回归测试来锁住这个暴露面。

同时，插件里已经存在 `BehaviorTree` 和 `BlackboardData` generator，并有 `TestData/BT_*.json` 与 `TestData/BB_TestSample.json` fixtures，但 MCP 的可发现 asset type 列表和 `MCP/schemas` 目录还没有把它们作为一等 generator 暴露。结果是工具层用户必须靠猜，或者从测试数据里反推 JSON 契约。

本 spec 的目标是把 MCP 层的 generator contract 正式封口：让 `StateTree`、`BehaviorTree`、`BlackboardData` 都能被 MCP 工具发现、读取 schema、并由测试防止回归。

---

## 2. 目标

- `get_generator_schema("StateTree")`、`get_generator_schema("BehaviorTree")`、`get_generator_schema("BlackboardData")` 都返回正式 schema 文档。
- `generate_assets` tool schema 的 asset type 描述和枚举包含这三个类型。
- schema missing fallback 的 available-types 文案包含这三个类型。
- MCP 测试覆盖 schema 可读性、tool schema enum、fallback 文案，防止以后只改一处导致工具层不一致。
- `MCP/dist` 与 `MCP/src` 同步，保持现有发布形态。
- 文档用中文说明已有能力和限制，不承诺 C++ generator 当前没有实现的语义。

---

## 3. 非目标

- 不新增或重构 C++ `BehaviorTree` / `BlackboardData` generator 语义。
- 不扩展 StateTree generator 的 JSON contract。
- 不把 `BehaviorTree` / `BlackboardData` 的真实 Editor/MCP smoke 套件放进本 spec；这属于后续 Spec 8 验证收束。
- 不改 `apply_blueprint_*`、BSL broker、sidecar AI 工具策略。
- 不把 generator 列表改成运行时扫描 UE 端注册表；MCP tool schema 仍保持静态、可预测、可测试。

---

## 4. 当前代码事实

### 4.1 StateTree

- 已有 `MCP/schemas/StateTree.md`。
- `MCP/src/index.ts` 和 `MCP/dist/index.js` 已经在 `generate_assets` 描述、`get_generator_schema` enum、fallback 文案中包含 `StateTree`。
- 已有 `MCP/scripts/statetree_roundtrip_mcp_smoke.mjs` 负责真实 Editor round-trip smoke。

### 4.2 BehaviorTree

- 已有 C++ generator：`Source/AssetFactory/Private/Generators/BehaviorTreeGenerator.cpp`。
- 公开 asset type 为 `BehaviorTree`。
- 已有 fixtures：
  - `TestData/BT_TestSample.json`
  - `TestData/BT_TestSample_Subtree.json`
  - `TestData/BT_TestSample_inline_bb.json`
  - 多个 invalid BT fixtures
- 已有 graph diagnostics 能力用于布局检查，但本 spec 只文档化 generator input contract。

### 4.3 BlackboardData

- 已有 C++ generator：`Source/AssetFactory/Private/Generators/BlackboardDataGenerator.cpp`。
- 公开 asset type 为 `BlackboardData`。
- 已有 fixture：`TestData/BB_TestSample.json`。

---

## 5. 设计

### 5.1 MCP asset type 单一来源

当前 `MCP/src/index.ts` 里 asset type 列表散落在：

- `loadSchema()` 的 missing-schema fallback 文案。
- `generate_assets` description。
- `generate_assets.inputSchema.assets.items.properties.AssetType.description`。
- `get_generator_schema.inputSchema.properties.asset_type.description`。
- `get_generator_schema.inputSchema.properties.asset_type.enum`。

本 spec 要把这些静态列表收敛为一个 MCP 侧常量，例如：

```ts
const GENERATOR_ASSET_TYPES = [
	"Blueprint",
	"WidgetBlueprint",
	"StateTree",
	"BehaviorTree",
	"BlackboardData",
	"Material",
	"DataAsset",
	"DataTable",
	"CurveFloat",
	"CurveVector",
	"InputAction",
	"InputMappingContext",
	"GameplayTag",
] as const;
```

描述文案可以保留不同上下文的自然语言，但类型列表必须来自同一个常量，避免以后 `enum` 有、fallback 没有，或者文档存在、工具不可选。

### 5.2 StateTree schema 封口

`StateTree.md` 保持现有完整内容，但本 spec 要做一次 MCP 暴露角度的整理：

- 顶部明确它覆盖生成、更新、提取、round-trip 的当前支持范围。
- 保留最小、动态节点、结构/transition、parameters、bindings、round-trip 示例。
- 保留 limitations，尤其是 `instanceStruct/access`、function input map、numeric integer/double/byte/enum parameter 等限制。
- 如果发现英文说明残留，改成中文；UE class/path/value 字段名保持英文原文。

### 5.3 BehaviorTree schema

新增 `MCP/schemas/BehaviorTree.md`，只记录已有 generator contract。

应包含：

- 顶层字段：
  - `AssetType: "BehaviorTree"`
  - `Name`
  - `Path`
  - `Action`
  - `Blackboard`
  - `BlackboardInline`
  - `Root`
- `Blackboard` 与 `BlackboardInline` 的互斥/优先级说明。
- `Root` node shape：
  - `Node`
  - `InstanceName`
  - `Properties`
  - `Children`
  - `Decorators`
  - `Services`
- Composite / task / decorator / service 的表达方式。
- 子树示例：`BTTask_RunBehavior` + `BehaviorAsset`。
- inline blackboard 示例。
- 常见失败：
  - unknown node class
  - composite without children
  - task with children
  - missing blackboard key

BehaviorTree 文档不承诺 task-level service 生成能力，除非源码确认已支持。若当前 generator 只允许 composite-level services，文档必须直接写清楚。

### 5.4 BlackboardData schema

新增 `MCP/schemas/BlackboardData.md`，记录已有 key contract。

应包含：

- 顶层字段：
  - `AssetType: "BlackboardData"`
  - `Name`
  - `Path`
  - `Action`
  - `Keys`
- key shape：
  - `Name`
  - `Type`
  - `BaseClass`
  - `EnumName`
  - `bInstanceSynced`
- `Parent` blackboard path。
- 已支持 key type 从 generator 源码和 `BB_TestSample.json` 提炼；至少覆盖 `Bool`、`Int`、`Float`、`String`、`Name`、`Vector`、`Rotator`、`Object`、`Class`、`Enum`。
- 说明 `Object` key 使用 `BaseClass` 限定 class。
- 说明 `Class` key 使用 `BaseClass` 限定 base class，`Enum` key 使用 `EnumName` 指向枚举。
- 示例必须能对应现有 `BB_TestSample.json`。

### 5.5 测试

新增 MCP schema-level 测试，不依赖 UE Editor：

- 通过 MCP stdio client 调用 `get_generator_schema`：
  - `StateTree` 返回标题和关键字段，例如 `RootParameters`、`bindings`、`Round-trip`。
  - `BehaviorTree` 返回标题和关键字段，例如 `BlackboardInline`、`Root`、`BTTask_RunBehavior`。
  - `BlackboardData` 返回标题和关键字段，例如 `Keys`、`BaseClass`、`bInstanceSynced`。
- 直接检查 `generate_assets` tool schema：
  - `AssetType` enum 包含 `StateTree`、`BehaviorTree`、`BlackboardData`。
- 直接检查 unknown schema fallback：
  - fallback 文案包含这三个类型。

如果测试结构需要访问 `tools` 或 `loadSchema`，优先把纯 helper 轻量 export，或者新增不启动 UE 的 stdio integration test。不要为了测试把运行时工具逻辑大改。

### 5.6 构建与 dist

本 spec 涉及 TypeScript 源码和 schema 文档：

- 修改 `MCP/src/index.ts`。
- 新增 `MCP/schemas/BehaviorTree.md`。
- 新增 `MCP/schemas/BlackboardData.md`。
- 必要时整理 `MCP/schemas/StateTree.md`。
- 运行 `npm --prefix MCP run build`，同步 `MCP/dist/index.js`。
- 运行 `npm --prefix MCP test`。

不需要 `RunUAT BuildPlugin`。如果没有触碰 C++，不需要 UE 普通编译；如果实现中误触 C++，必须用项目普通编译验证，不打包插件。

---

## 6. 错误处理与文案原则

- `get_generator_schema` 找不到文件时，返回统一 available-types 列表。
- 文档中的错误示例要使用 generator 当前会返回的诊断语义，不编造不存在的错误码。
- schema 文档用中文写面向 Codex/LLM 的说明；JSON 字段、UE class path、枚举值保持源码原文。
- 对还没 smoke 的能力，用“当前 generator contract 支持/不支持”描述，不用“已验证完整链路”这种容易误导的字眼。

---

## 7. 验收标准

- `get_generator_schema(StateTree)`、`get_generator_schema(BehaviorTree)`、`get_generator_schema(BlackboardData)` 在 MCP 测试中成功。
- `generate_assets` 的 tool schema enum 包含 `StateTree`、`BehaviorTree`、`BlackboardData`。
- fallback available-types 包含 `StateTree`、`BehaviorTree`、`BlackboardData`。
- `MCP/schemas/BehaviorTree.md` 与现有 BT fixtures 不矛盾。
- `MCP/schemas/BlackboardData.md` 与现有 BB fixture 不矛盾。
- `MCP/schemas/StateTree.md` 不再出现面向用户的英文段落，除非是字段名、UE 类型名、命令、代码块或固定术语。
- `npm --prefix MCP test` 通过。
- `git diff --check` 通过。

---

## 8. 后续衔接

本 spec 完成后，下一步自然进入 Spec 8：把 StateTree、BehaviorTree、BlackboardData 的 positive/negative fixtures、真实 Editor MCP generate/extract、GUI smoke 统一成长期验收入口。Spec 8 可以复用本 spec 新增的 schema docs 作为工具层契约来源。
