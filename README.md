# UECopilot (Asset Factory)

UE5 编辑器插件，通过 JSON 配置和 AI 辅助，程序化生成和修改 Unreal Engine 资产。

## 架构概览

```
AI (Claude / DeepSeek / OpenAI / ...)
  ↕ MCP Server (Node.js, 21 tools)
  ↕ HTTP Server (UE Editor, port 8559)
  ↕ Asset Generators / BSL Compiler / Python Execute
  ↕ Unreal Engine Assets
```

**三个模块：**

| 模块 | 职责 |
|------|------|
| **AssetFactory** | HTTP 服务、资产生成器、提取框架、注册表 |
| **AssetFactoryAI** | BSL 编译器/反编译器、蓝图 JSON Schema、节点布局引擎、AI 服务对接 |
| **AssetFactoryEditor** | 编辑器 UI（AI Chat、蓝图预览、蓝图 Diff、BSL 测试窗口） |

## 支持的资产类型（11 种）

| 类型 | 生成 | 提取 | 修改 | 说明 |
|------|:----:|:----:|:----:|------|
| **Blueprint** | ✅ | ✅ | ✅ | 任意父类（Actor/Character/GameMode/GAS/AnimInstance/AI/Widget 等），组件/变量/接口/CDO 属性 |
| **WidgetBlueprint** | ✅ | ✅ | ✅ | 递归 Widget 树，属性绑定，Slot 配置，支持 Add/Update/Remove 增量操作 |
| **DataAsset** | ✅ | ✅ | ✅ | 任意 UDataAsset 子类，通过反射设置属性 |
| **DataTable** | ✅ | ✅ | ✅ | CSV/行定义创建，支持行级增删改（`/datatable/rows`） |
| **Material** | ✅ | ✅ | ✅ | 内置 UI 模板（CircularProgress / CooldownSweep / GradientFill / HealthBarFill） |
| **CurveFloat** | ✅ | ✅ | ✅ | 浮点曲线，关键帧 + 插值模式 |
| **CurveVector** | ✅ | ✅ | ✅ | 向量曲线，3D 关键帧 |
| **InputAction** | ✅ | ✅ | ✅ | Enhanced Input Action，触发器和修改器 |
| **InputMappingContext** | ✅ | ✅ | ✅ | 输入映射，按键绑定到 Action |
| **GameplayTag** | ✅ | — | — | 注册到 DefaultGameplayTags.ini |

生成优先级：GameplayTag → InputAction → Curve → DataTable → DataAsset/Material → WidgetBlueprint → Blueprint

## 蓝图逻辑系统

### BSL (Blueprint Script Language)

专为 AI 设计的蓝图脚本语言，比 JSON 节省约 50% token。

```
BSL 源码 → Lexer → Parser → AST → Compiler → FBlueprintData (JSON IR) → NodeSpawner → UEdGraph
UEdGraph → Decompiler → AST → Emitter → BSL 源码
```

- **30+ 节点类型**：事件、函数、流程控制、变量、数学、比较、逻辑、Cast、数组、委托等
- **完整控制流**：If / While / For / ForEach / Switch / Sequence / DoOnce / Gate / Delay
- **表达式**：字面量、变量、函数调用、成员访问、数组下标、二元/一元运算、Cast、结构体字面量（Vector/Rotator/Transform）
- **Layout Engine**：基于 Sugiyama 算法的自动节点布局，带重叠检测

### Blueprint JSON Schema

完整的蓝图中间表示（`FBlueprintData`），支持：
- 事件图、函数图、宏
- 局部变量和图变量
- Pin 连接（执行流 + 数据流）
- 15 种变量类型（Bool/Int/Float/String/Vector/Rotator/Transform/Object/Class/Struct/Enum/Array/Set/Map 等）

## HTTP API

基础路径：`http://localhost:8559/assetfactory/`

| 端点 | 方法 | 说明 |
|------|------|------|
| `/generate` | POST | 从 JSON 配置生成资产（支持 Create/Update/CreateOrUpdate） |
| `/extract` | POST | 提取已有资产配置为 JSON |
| `/delete` | POST | 删除资产 |
| `/query` | POST | 按属性路径查询 JSON（支持 `Array[0]`、`Array[*]`、`Parent.Child`） |
| `/generators` | GET | 列出所有可用生成器 |
| `/health` | GET | 健康检查 |
| `/context` | GET | 获取编辑器状态：选中 Actor（含 Transform）、选中资产、当前关卡、打开的编辑器、PIE 状态 |
| `/screenshot` | GET | 截取视口截图（base64 JPEG，最大 1280px） |
| `/execute` | POST | 在编辑器中执行 Python 脚本（包裹在 Undo 事务中） |
| `/datatable/rows` | POST | DataTable 行级增删改 |
| `/extract_bsl` | POST | 反编译蓝图为 BSL 文本 |
| `/apply_bsl` | POST | 编译 BSL 并应用到蓝图 |
| `/extract_graph` | POST | 导出蓝图节点图为 JSON |

启动时自动写入 `{ProjectDir}/Saved/AssetFactory/service.json` 用于服务发现。

## MCP Server（AI 工具集成）

Node.js MCP Server，暴露 21 个工具给 AI Agent：

**资产操作：**
- `generate_assets` — 生成资产
- `extract_assets` — 提取资产配置
- `delete_assets` — 删除资产
- `query_asset` — 属性路径查询
- `list_generators` — 列出生成器
- `get_generator_schema` — 获取资产类型的 JSON Schema 文档

**蓝图逻辑：**
- `apply_blueprint_change` — 应用蓝图逻辑 JSON（编辑器在线或离线回退）
- `extract_blueprint_as_bsl` — 反编译蓝图为 BSL
- `apply_blueprint_as_bsl` — 编译 BSL 并应用
- `extract_blueprint_graph` — 导出蓝图节点图 JSON
- `generate_blueprint_change` — 通过 AI 生成蓝图逻辑 JSON
- `repair_blueprint_json` — 修复格式错误的蓝图 JSON
- `validate_blueprint_json` — 校验蓝图 JSON 结构
- `orchestrate_modify_request` — 完整流程：生成 → 修复 → 布局
- `layout_blueprint_graph` — 自动节点布局

**编辑器交互：**
- `get_editor_context` — 获取编辑器状态
- `get_viewport_screenshot` — 截取视口截图
- `execute_python` — 执行 Python 脚本
- `update_datatable_rows` — DataTable 行级操作
- `health_check` — 健康检查
- `chat_completion` — 调用 LLM 聊天补全

## AI Skills（蓝图生成专用提示词）

`Skills/` 目录下包含 7 套领域专用 Skill：

| Skill | 领域 |
|-------|------|
| `ue-bp-logic-router` | 路由：根据父类和需求类型分发到对应 Skill |
| `ue-bp-event-graph-logic` | EventGraph 逻辑（BeginPlay/Tick/自定义事件） |
| `ue-bp-function-logic` | 蓝图函数（纯函数、输入输出、分支、数学） |
| `ue-bp-actor-interaction` | Actor/组件交互（碰撞、Overlap、状态变化） |
| `ue-bp-character-gameplay` | 角色玩法（移动、输入、战斗、体力/生命） |
| `ue-bp-gameframework-rules` | GameMode/GameState/PlayerController 框架逻辑 |
| `ue-bp-widget-ui-logic` | Widget UI 逻辑（按钮事件、数据绑定、显示状态） |

## Python 脚本执行

通过 `/execute` 端点可执行任意 Python 脚本（依赖 PythonScriptPlugin）：
- 所有操作包裹在 Undo 事务中
- 可访问完整 `unreal` 模块
- 适用于批量操作、自定义验证、资产生成器未覆盖的场景

## 编辑器 UI

- **AI Chat 窗口** — 对话式蓝图生成，支持流式输出
- **蓝图预览** — 只读预览生成的蓝图图表，支持新增/修改/删除节点高亮
- **蓝图 Diff** — 并排对比蓝图变更
- **BSL 测试窗口** — 编写和调试 BSL 代码

## AI 服务支持

| 提供商 | 说明 |
|--------|------|
| DeepSeek | 默认 |
| OpenAI | |
| GLM (智谱) | |
| Ollama | 本地部署 |
| Custom | 任何 OpenAI 兼容 API |

配置路径：项目设置 → Plugins → Asset Factory

## 离线模式

`AssetFactoryApplyBlueprintCommandlet` 支持在编辑器未运行时修改蓝图：

```bash
UnrealEditor-Cmd -run=AssetFactoryApplyBlueprint
```

## 依赖

| 插件 | 必需 | 用途 |
|------|:----:|------|
| EnhancedInput | ✅ | InputAction / InputMappingContext |
| EditorScriptingUtilities | ✅ | 编辑器自动化 |
| PythonScriptPlugin | ✅ | Python 脚本执行 |
| GameplayAbilities | 可选 | GameplayEffect / GameplayAbility 支持 |

## 目录结构

```
UECopilot/
├── Source/
│   ├── AssetFactory/          # 核心：HTTP 服务、生成器、提取器
│   ├── AssetFactoryAI/        # BSL 编译链、蓝图 JSON、AI 服务
│   └── AssetFactoryEditor/    # 编辑器 UI 组件
├── MCP/
│   ├── src/index.ts           # Node.js MCP Server（21 tools）
│   └── assetfactory_mcp_server.py  # Python Sidecar
├── Skills/                    # AI 蓝图生成 Skill 提示词
├── docs/plans/                # 设计文档
└── AssetFactory.uplugin       # 插件描述
```
