# AssetFactory MCP Agent Broker 设计文档

- **日期**：2026-04-23
- **状态**：Draft（待用户审阅）
- **作者**：Codex + 用户协作
- **范围**：为 AssetFactory MCP server 增加 agent-aware broker，使 Codex、Claude 和 generic client 看到不同的工具目录与表述，同时保持底层执行实现统一

---

## 1. 目标与范围

### 1.1 目标

在不拆分 AssetFactory MCP 执行层的前提下，为现有 MCP server 增加一层基于 client identity 的 broker，使不同 agent 看到不同的工具目录与推荐路径。

本设计的直接目标：

1. **Codex 工具目录纯化**：Codex 只看到当前正式支持的资产工具、编辑器辅助工具和 BSL 蓝图工具链
2. **Claude 兼容保留**：Claude 仍可看到历史兼容的 JSON 蓝图工具链，避免破坏现有工作流
3. **统一执行实现**：所有工具仍由现有单一 MCP server 提供，不按 agent 拆成多套 handler / 多个独立进程
4. **明确 profile 边界**：`tools/list` 与 `tools/call` 都按相同 profile 策略执行，避免“目录隐藏但仍可误调旧工具”

### 1.2 非目标

- 不在本次设计中删除 legacy JSON 蓝图工具的底层实现
- 不重写 `assetfactory_mcp_server.py` sidecar 的模型调用策略
- 不把 MCP server 拆成多个外部 broker / proxy 进程
- 不修改 AssetFactory HTTP API 协议本身
- 不在本次设计里推进 worktree、分支管理或实现计划细化

### 1.3 成功标准

- Codex client 下，AssetFactory MCP 工具全套可用，但蓝图链路仅暴露 BSL 路径
- Claude client 下，现有兼容链路仍可继续工作
- 未识别 client 默认进入 `generic` profile，并使用现代工具目录，而不是历史兼容目录
- 工具执行逻辑仍然只有一套 canonical implementation，不出现 agent 分叉实现

---

## 2. 现状与问题

### 2.1 现状

当前 AssetFactory MCP server 主要由以下入口组成：

- `MCP/src/index.ts`：Node.js MCP server 主入口
- `MCP/README.md`：安装与接入说明
- `MCP/assetfactory_mcp_server.py`：Blueprint sidecar 与模型代理能力
- `.mcp.json`：本地 Codex/工具接入配置

`MCP/src/index.ts` 当前已经是标准 MCP `stdio` server，底层能力并非只支持 Claude；但工具表述与蓝图链路暴露方式仍带有较强历史包袱：

- 工具目录没有按 client 做分层
- Claude 时代的 legacy JSON 蓝图工具仍与现行 BSL 工具并列暴露
- Codex 虽然已有 `.mcp.json`，但当前看见的目录仍不够“现代链路优先”

### 2.2 当前问题

1. **Codex 工具噪音过大**
   Codex 不应再看到 `apply_blueprint_change` 及整套 legacy JSON 蓝图工具，否则模型会误用过时链路。

2. **工具目录缺少 client-aware 视图**
   同一套工具描述同时服务 Codex / Claude / generic，导致文案、排序和推荐路径无法针对不同 agent 优化。

3. **兼容性与现代化目标冲突**
   如果直接删除 legacy 工具，会打断 Claude 现有工作流；如果全部保留并统一暴露，又会继续污染 Codex 视图。

4. **目录与执行缺少统一约束**
   仅在 `tools/list` 隐藏工具并不够；如果 `tools/call` 仍允许 Codex 直接调用 legacy 工具，策略就会失效。

---

## 3. 设计原则

### 3.1 单一执行层

所有工具继续共用现有 MCP server 执行逻辑，不按 agent 拆分 handler，不复制执行层代码。

### 3.2 分表述，不分能力实现

对不同 agent 的差异主要体现在：

- 工具是否可见
- 工具描述文案
- 工具分组与排序
- 推荐工作流提示

而不是：

- 不同 tool name
- 不同 input schema
- 不同底层执行逻辑

### 3.3 Codex 纯 BSL 蓝图链路

Codex profile 下，蓝图能力只暴露：

- `extract_blueprint_graph`
- `extract_blueprint_as_bsl`
- `apply_blueprint_as_bsl`

legacy JSON 蓝图工具不再出现在 Codex 目录中，也不允许被 Codex profile 调用。

### 3.4 Generic 默认走现代链路

未识别 client 不应默认回到历史兼容目录，而应使用与 Codex 相同的现代蓝图目录，以避免继续扩大 legacy 工具的默认使用面。

---

## 4. 目标架构

本设计采用 **单一 MCP server + 进程内 broker** 架构，而不是独立外置 broker。

```
Client (Codex / Claude / Generic)
        |
        v
Initialize(clientInfo)
        |
        v
ClientProfileResolver
        |
        v
AgentAwareToolCatalogBroker
        |
        +--> tools/list -> profile-specific catalog
        |
        +--> tools/call -> ToolExecutionRouter -> CanonicalToolRegistry -> existing handlers
```

### 4.1 为什么不做独立 broker 进程

- 现有 `MCP/src/index.ts` 已经是标准 MCP server，无需再包一层协议转发
- 额外进程会引入更多调试与部署成本
- 当前目标是“目录视图分层 + 策略约束”，不是跨进程编排
- 进程内方案更容易复用现有 `tools/list` / `tools/call` 注册逻辑

---

## 5. 组件设计

### 5.1 `ClientProfileResolver`

职责：

- 在 MCP initialize 阶段读取 `clientInfo`
- 将 client 归一化为少量 profile：
  - `codex`
  - `claude`
  - `generic`

设计要点：

- `clientInfo` 缺失或识别失败时，默认归类为 `generic`
- `stdio` 连接先按“单连接单 profile”处理
- 后续若迁移到多 session transport，可扩展为按 session 保存 profile

建议输出：

```ts
type ClientProfile = "codex" | "claude" | "generic";
```

### 5.2 `CanonicalToolRegistry`

职责：

- 维护真实存在的 tool name、schema、handler 映射
- 保持现有工具实现为唯一事实来源

此 registry 中仍保留所有现有工具，包括：

- 资产工具
- 编辑器辅助工具
- BSL 蓝图工具
- legacy JSON 蓝图工具

但 registry 仅代表“实现存在”，不代表“所有 client 都可见、都可调用”。

### 5.3 `AgentAwareToolCatalogBroker`

职责：

- 根据 profile 生成对应的工具目录视图
- 统一处理可见性、文案、排序、推荐路径

允许调整的内容：

- 工具是否出现在 `tools/list`
- description 文案
- 工具排序
- 工具所属逻辑分组

默认不调整的内容：

- tool name
- input schema
- handler

### 5.4 `ToolExecutionRouter`

职责：

- 所有 `tools/call` 请求先经过 router
- 先按 profile 校验工具是否允许调用
- 通过校验后，再委托给 canonical handler

关键约束：

- `tools/list` 和 `tools/call` 必须共享同一套 profile policy
- 被 Codex 隐藏的 legacy 工具，不允许通过直接调用绕过

---

## 6. 工具编目与 profile 可见性

### 6.1 工具分组

#### A. 共享资产工具组

- `health_check`
- `list_generators`
- `get_generator_schema`
- `generate_assets`
- `extract_assets`
- `query_asset`
- `delete_assets`
- `update_datatable_rows`

#### B. 共享编辑器辅助组

- `get_editor_context`
- `execute_python`
- `get_viewport_screenshot`

#### C. 现代蓝图工具组（BSL）

- `extract_blueprint_graph`
- `extract_blueprint_as_bsl`
- `apply_blueprint_as_bsl`

#### D. 兼容蓝图工具组（legacy JSON）

- `apply_blueprint_change`
- `generate_blueprint_change`
- `repair_blueprint_json`
- `orchestrate_modify_request`
- `validate_blueprint_json`
- `layout_blueprint_graph`

### 6.2 Profile 可见性矩阵

| Tool Group | Codex | Claude | Generic |
|---|---:|---:|---:|
| 共享资产工具组 | Yes | Yes | Yes |
| 共享编辑器辅助组 | Yes | Yes | Yes |
| 现代蓝图工具组（BSL） | Yes | Yes | Yes |
| 兼容蓝图工具组（legacy JSON） | No | Yes | No |

### 6.3 目录排序策略

Codex / Generic 目录排序建议：

1. `health_check`
2. `list_generators`
3. `get_generator_schema`
4. `generate_assets`
5. `extract_assets`
6. `query_asset`
7. `delete_assets`
8. `update_datatable_rows`
9. `get_editor_context`
10. `execute_python`
11. `get_viewport_screenshot`
12. `extract_blueprint_graph`
13. `extract_blueprint_as_bsl`
14. `apply_blueprint_as_bsl`

Claude 目录排序建议：

- 前 14 项与 Codex / Generic 保持一致
- legacy JSON 蓝图工具附加在蓝图工具组之后

这样可以让默认工作流更明确：

- 先检查连接
- 再查看 generator / schema
- 再做资产生成或提取
- 蓝图修改最后统一进入 BSL 路径

---

## 7. Codex / Claude / Generic 表述策略

### 7.1 Codex

Codex profile 的目标不是“功能最多”，而是“减少歧义，强化正式链路”。

因此在 Codex 视图中：

- 仅暴露共享资产工具、共享编辑器辅助工具、BSL 蓝图工具
- 明确描述蓝图修改应通过 BSL 完成
- 不再出现 legacy JSON 蓝图工具的任何目录项

### 7.2 Claude

Claude profile 继续兼容现有历史工作流，因此：

- 保留所有共享工具
- 保留 BSL 蓝图工具
- 保留 legacy JSON 蓝图工具
- 允许后续在 description 中继续说明其兼容性质

### 7.3 Generic

Generic profile 是未识别 client 的安全默认值。

策略：

- 默认提供共享资产工具、共享编辑器辅助工具与 BSL 蓝图工具
- 不提供 legacy JSON 蓝图工具

理由：

- generic 代表未知 client，不应默认继承历史兼容包袱
- BSL 是当前正式蓝图链路，理应成为现代默认目录

---

## 8. 错误处理与回退策略

### 8.1 Client 识别失败

- 如果 initialize 时 `clientInfo` 缺失或无法识别，server 仍正常初始化
- profile 自动回退为 `generic`
- 不因为 client 识别失败而中断 MCP 服务

### 8.2 目录-执行一致性

如果某 profile 不允许某个工具：

- 该工具不出现在 `tools/list`
- 该工具在 `tools/call` 阶段也必须被拒绝

对 Codex，若调用 legacy JSON 蓝图工具，应返回明确错误，例如：

> 当前 client profile 不支持该工具，请改用 BSL 蓝图工具链。

### 8.3 启动期一致性校验

broker 配置中的每个 tool name 都必须能在 canonical registry 中找到。

若发现以下问题，应在启动时 fail fast：

- policy 引用了不存在的工具名
- 工具分组包含重复或冲突配置
- profile 目录构建结果为空或缺失关键基础工具

### 8.4 策略集中管理

禁止把 `if profile === "codex"` 这类判断散落到具体 handler 内。

所有 profile 差异应集中在：

- `ClientProfileResolver`
- `AgentAwareToolCatalogBroker`
- `ToolExecutionRouter`

这样才能保证：

- 行为一致
- 易于审计
- 日后删除 legacy 工具时修改面最小

---

## 9. 实现影响面

### 9.1 主要代码变更

优先改动：

- `MCP/src/index.ts`

建议在该文件内先做结构整理，再决定是否抽成局部模块文件。

可接受的结构演进方向：

1. 先在 `index.ts` 内引入 `ClientProfile`、工具分组与 policy 数据结构
2. 如代码体积明显增长，再拆出：
   - `clientProfiles.ts`
   - `toolCatalogBroker.ts`
   - `toolPolicies.ts`

### 9.2 文档与配置变更

需要同步更新：

- `MCP/README.md`
- `.mcp.json`

文档需要明确：

- 这是一个统一 MCP server，不是 Claude-only server
- Codex 连接时看到的是纯 BSL 蓝图目录
- Claude 仍保留兼容工具

### 9.3 不应改动的部分

本设计不要求修改：

- UE HTTP server 路由
- `assetfactory_mcp_server.py` 的 OpenAI / Claude / DeepSeek 调用分发
- BSL 工具本身的底层语义

---

## 10. 验证方案

### 10.1 目录验证

需要验证：

1. Codex profile 下：
   - 能看到共享资产工具
   - 能看到共享编辑器辅助工具
   - 能看到 BSL 蓝图工具
   - 看不到 `apply_blueprint_change` 和其余 legacy JSON 蓝图工具

2. Claude profile 下：
   - 能看到共享工具
   - 能看到 BSL 工具
   - 能看到 legacy JSON 蓝图工具

3. Generic profile 下：
   - 能看到共享工具
   - 能看到 BSL 工具
   - 看不到 legacy JSON 蓝图工具

### 10.2 调用验证

Codex profile 至少验证以下工具调用成功：

- `health_check`
- `list_generators`
- `get_generator_schema`
- `generate_assets`
- `extract_assets`
- `query_asset`
- `delete_assets`
- `extract_blueprint_graph`
- `extract_blueprint_as_bsl`
- `apply_blueprint_as_bsl`

Codex profile 下还需验证：

- 直接调用 `apply_blueprint_change` 被明确拒绝

### 10.3 UE 侧集成验证

按项目现有流程执行：

1. 运行 UBT 编译
2. 启动 Unreal Editor
3. 等待 AssetFactory HTTP server 就绪
4. 通过 Codex MCP 配置实际连一遍
5. 至少跑通：
   - 一条资产生成链
   - 一条 BSL 蓝图链

如果环境允许，再补一条 Claude 或 generic 的兼容回归。

---

## 11. 风险与后续工作

### 11.1 主要风险

1. **`clientInfo` 识别不稳定**
   不同 MCP client 上报的 name / version 可能格式不同，因此 profile 识别规则需要保守实现，并为 `generic` 留好默认路径。

2. **`index.ts` 继续膨胀**
   若 broker 逻辑全部硬塞在现有文件中，后续维护会变差，因此应尽量把 policy 数据与执行逻辑分开。

3. **目录隐藏但测试未覆盖**
   如果只验证 `tools/list`，而不验证 `tools/call` 的拒绝路径，Codex 仍可能误调 legacy 工具。

### 11.2 后续工作

- 若 broker 方案验证稳定，可进一步考虑把 legacy JSON 蓝图工具完全移出默认分发面
- 若未来引入更多 client，可继续扩展 profile policy，而不影响 canonical handler
- 若 `stdio` 之外需要多 session transport，再把 profile 记录升级为按 session 管理

---

## 12. 推荐实现顺序

1. 在 `MCP/src/index.ts` 中引入 profile 识别与 canonical tool registry 概念
2. 为工具定义分组与 profile policy
3. 改写 `tools/list` 为 broker 输出
4. 在 `tools/call` 前增加 profile 允许性校验
5. 更新 README 与 `.mcp.json` 相关说明
6. 跑本地目录验证与 UE 集成验证

该顺序的优点是：

- 先锁定目录与策略，再接入拒绝路径
- 执行层改动面最小
- 最后再补文档与配置，避免文档先于行为漂移

