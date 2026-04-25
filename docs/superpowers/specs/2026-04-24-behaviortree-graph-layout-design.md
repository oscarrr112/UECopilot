# BehaviorTree 图布局设计

- **日期**：2026-04-24
- **状态**：Draft
- **范围**：为生成的 `UBehaviorTree` 资产添加确定性的编辑器图默认布局。

---

## 1. 目标

生成后的 BehaviorTree 资产在 UE 行为树编辑器中打开时，应当具备稳定、可读的默认布局。用户不需要在生成后手动拖拽节点，也能立刻看清行为树结构。

本 spec 扩展现有 BehaviorTree generator。它不改变行为树语义 JSON 契约，也不在输入 JSON 中新增用户可写的坐标字段。

## 2. 当前上下文

`FBehaviorTreeGenerator` 当前会直接构造运行时行为树结构：

- `UBehaviorTree::RootNode`
- `UBTCompositeNode::Children`
- `FBTCompositeChild::Decorators`
- `UBTCompositeNode::Services`

当前 JSON 契约只允许 composite 定义 `Services`。`UBTTaskNode::Services` 是 UE runtime 类型上存在的能力，但本 spec 不扩展 generator 的 JSON 语义，不新增 task-level service 生成能力。

生成结束时，它会清空编辑器图状态：

```cpp
BehaviorTree->BTGraph = nullptr;
BehaviorTree->LastEditedDocuments.Reset();
```

UE 在打开资产时可以恢复编辑器图。在 UE 5.7 中，`FBehaviorTreeEditor::RestoreBehaviorTree()` 会在 `BTGraph` 为空时创建图：

```cpp
BehaviorTree->BTGraph = FBlueprintEditorUtils::CreateNewGraph(...);
Schema->CreateDefaultNodesForGraph(*MyGraph);
MyGraph->OnCreated();
```

`UBehaviorTreeGraph::OnCreated()` 会调用 `SpawnMissingNodes()`，根据 BehaviorTree 资产生成图节点。这是应该复用的编辑器路径。不过，内置的 `AutoArrange()` 依赖 Slate node widget，不适合作为生成阶段的无头布局基础。

## 3. 非目标

- 不向 BehaviorTree JSON 添加 `NodePosX` / `NodePosY` 字段。
- 不在整棵树 `Update` 时保留用户手动拖拽过的图坐标；当前 Update 会替换语义树，因此应重建可预测布局。
- 不实现 NodeIndex partial update 或坐标保留式 patch。
- 不针对 `BTTask_MoveTo`、`BTComposite_Sequence` 或项目自定义节点等具体类硬编码布局行为。
- 不把截图作为主要自动化证明；截图可以继续作为人工 sanity check。

## 4. 推荐方案

复用 UE 编辑器图构造流程，然后应用确定性的结构布局。

generator 应当：

1. 先完成行为树语义资产构建。
2. 确保 `BTGraph` 存在且类型为 `UBehaviorTreeGraph`。
3. 让行为树 schema 创建默认 root graph node。
4. 让 `UBehaviorTreeGraph::OnCreated()` / `SpawnMissingNodes()` 根据 `RootNode` 创建图节点。
5. 对 `UEdGraphNode::NodePosX` 和 `NodePosY` 应用确定性布局。
6. 保存带有 graph 状态的 BT 资产。

这样既能保持图创建逻辑与 UE 编辑器行为一致，又能避免不稳定的 widget 驱动自动排布。

## 5. 布局规则

坐标只从 BT 结构派生，不从具体节点类名派生。

- Root graph node 居中放在顶部，`Y = 0`。
- root composite node 位于 Root 下方。
- 语义树每深入一层，`Y` 增加固定垂直间距。
- 兄弟树节点按 `Children` 顺序从左到右排列。
- 子树在父节点下方居中；更宽的子树占用更多水平空间。
- Composite 和 Task graph node 使用同一套树形布局算法。
- Decorator 和 Service 作为所属 graph node 的 subnode 处理，顺序必须与源数组一致。
- 本次只验证 generator JSON 当前支持的 composite service；如果未来扩展 task-level service 输入，布局层应继续把它作为 task graph node 的 subnode，而不是放进主树层级排序。

布局算法应先计算每个语义节点的 subtree width，再围绕父节点中心从左到右放置子节点。固定间距常量是可以接受的，但它们必须是命名常量，并且表达布局几何含义，而不是类特定行为。

建议默认值：

```cpp
constexpr int32 RootY = 0;
constexpr int32 FirstTreeNodeY = 180;
constexpr int32 VerticalSpacing = 220;
constexpr int32 HorizontalSpacing = 360;
constexpr int32 MinimumSubtreeWidth = 360;
```

这些值可以在编辑器验证后微调。

### 布局密度调优

初版布局优先避免重叠，复杂树会横向铺得过开。密度调优的目标是让 BT 编辑器打开后的默认图更接近人工整理结果：横向线条不应跨越过大空白区域，根下多分支仍应能一眼比较，但节点、Decorator、Service 不能互相压住。

调优规则：

- 保持 `VerticalSpacing` 不变，优先收紧横向常量。
- 兄弟节点间距应从保守铺开改为紧凑但不重叠。
- `MinimumSubtreeWidth` 只承担最小可读宽度，不应让小叶子分支额外占用大面积空白。
- diagnostics 必须继续使用 estimated bounds 检查真实矩形重叠。
- 对复杂测试树 `BT_LayoutComplex`，主树 estimated bounds（diagnostics 中带 `estimatedBounds` 的 primary graph nodes）横向跨度应压到初版的约 60%-70%，目标上限为 3800 unreal graph units，且参与计算的 primary graph nodes 不少于 10 个。

### 人工排布风格的进一步紧凑化

用户手工整理后的目标形态更像“紧凑扇形树”，而不是简单把同层节点平均铺开。布局应继续保留 BT child order，但减少父节点两侧分支之间的空白，让根下左、中、右分支更靠近根节点视觉中轴。该目标不要求硬编码某个节点名，也不要求所有 depth 保持同一水平线；它要求生成结果更接近人工整理的密度：

- sibling 间距仍可由结构级常量驱动，但常量必须足够紧凑，并继续通过 estimated bounds 验证真实矩形不会重叠。
- 叶子子树不应因为 `MinimumSubtreeWidth` 被过度放大；小任务节点应能贴近父 composite 的局部范围。
- 对 3 个以上 child 的 composite，应保持 child order，同时允许中间 child 靠近父节点中轴，左右 child 向两侧压缩。
- 对复杂测试树 `BT_LayoutComplex`，主树 estimated bounds 横向跨度目标上限收紧到 3000 unreal graph units。
- 对 `BT_LayoutComplex` 的 root child row，至少应形成左 / 中 / 右三个 primary branches：中间 branch 的 estimated center 距 root center 不超过 320 graph units，相邻 root child branch center 最大间隔不超过 1200 graph units。
- 纵向关系仍以 `child.NodePosY > parent.NodePosY` 为硬约束，Decorator / Service 只参与 owner 尺寸估算，不进入主树排序。

### 来自蓝图布局算法的启发

现有蓝图生成器的 `ULayoutEngine` 采用的是“结构分析 -> 初始布局 -> 碰撞修正”流程，而不是把节点直接塞进固定网格：

- 先根据执行连线构建层级关系，入口节点作为根。
- 对主要执行节点做分层布局，并按连接关系尽量减少交叉。
- 根据节点标题长度、pin 数量估算节点尺寸，而不是假设所有节点一样大。
- 布局完成后通过 placement grid 检测重叠，并做 push / sweep 修正。
- 对纯数据节点采用“靠近消费者”的局部布局，而不是放进主执行流。

BTGraph 可以借鉴这套原则，但不应该直接复用 K2 蓝图的完整 Sugiyama DAG 布局。BehaviorTree 的主结构天然是树，不是任意有向图；更稳定的算法应是 BT 专用树布局：

- 根据 `RootNode` 和 `Children` 递归计算每个子树宽度。
- 子树宽度不只使用固定 `MinimumSubtreeWidth`，还要参考 graph node 估算宽度。
- Graph node 尺寸估算可参考蓝图生成器：使用节点标题 / 类显示名长度、Decorator / Service subnode 数量估算宽高。
- 父节点放在子节点组的水平中心，兄弟节点严格按 `Children` 顺序从左到右。
- 主树布局后运行一次轻量 overlap sweep；修正重叠时必须保持父子层级和兄弟顺序不变。
- Decorator / Service 类似蓝图中的“靠近消费者”的节点：它们贴近 owner graph node，参与 owner 尺寸估算，但不进入主树层级排序。

实现可以复用蓝图布局算法的经验和少量通用思路，例如 node size estimation、placement grid、final sweep；但不要把 K2 专用逻辑搬到 BTGraph，例如 exec pin、pure data node、Branch / Sequence 特判。BT 的排序依据始终应来自 BT 数据结构本身。

## 6. Create 和 Update 行为

### Create

Create 必须同时生成行为树语义树和编辑器 `BTGraph`。打开新生成资产时，应直接看到可读图布局，而不是等待编辑器从空 graph 重建。

### Update

Update 应重建语义树，并基于新语义树重建 graph 布局。由于当前 generator 做的是整棵树替换，它可以丢弃旧 graph 坐标。

结果必须保持确定性：同一份 JSON 连续运行两次 Update，应得到相同 graph 坐标。

## 7. 动态化设计约束

实现必须延续 UECopilot 的动态化设计原则：

- 使用 `UBehaviorTreeGraph`、`UBehaviorTreeGraphNode`、`UAIGraphNode`、`UEdGraphNode` 和 `UBTNode` 的结构关系。
- 只允许用 `Cast<UBTCompositeNode>`、`Cast<UBTTaskNode>`、`Cast<UBTDecorator>`、`Cast<UBTService>` 判断宽泛 BT 角色。
- 不引入针对具体内置节点或项目节点类名的 `switch` / `case`。
- 不维护 task、composite、decorator 或 service 的静态类型列表。
- 语义节点生成继续使用 `FClassFinderUtils` 和 `FPropertySetterUtils`。

一个可接受的例外是 UE 编辑器图构造路径本身已经会为 SimpleParallel 和 RunBehavior 选择专用 graph node class。generator 应优先调用 UE 现有 graph 构造路径，而不是复制这些具体类选择逻辑。

## 8. 实现组件

### `FBehaviorTreeGenerator`

在 `FinalizeBT` 中增加 editor-only graph finalization 步骤。

职责：

- 保留现有 BT 节点 execution index 初始化。
- 确保 BehaviorTree graph 存在。
- 根据当前语义树重建 graph nodes。
- 应用确定性 graph 坐标。
- 保存前标记 graph 和 package dirty。

主生成逻辑不应在构建树后继续把 `BTGraph` 清为 null。如果更新现有资产，可以在重建前替换或清理旧 graph，避免 stale nodes 残留。

### Layout Helper

先在 `BehaviorTreeGenerator.cpp` 中添加聚焦 helper。若逻辑变大，再拆出小型 private helper 文件。

建议内部职责：

- 找到 root graph node。
- 建立 graph node 到对应 `UBTNode` instance 的映射。
- 通过 output pin link 或 BT node instance 构建 parent-child graph 关系。
- 计算 subtree width。
- 估算 graph node 宽高，避免长标题或多 subnode 节点互相压住。
- 写入 `NodePosX` 和 `NodePosY`。
- 对 primary tree graph nodes 做轻量 overlap sweep。
- 保存前验证每个语义节点都有对应 graph node。

### Build 依赖

实现大概率需要这些 editor module：

- `BehaviorTreeEditor`
- `AIGraph`
- 仅在选定 graph 创建 API 编译需要时添加 `GraphEditor` 或 `BlueprintGraph`

`AssetFactory.Build.cs` 目前已依赖 `BlueprintGraph` 和 `AIModule`；只添加实际编译需要的模块。

## 9. MCP 验证

自动化验证应在 `generate_assets` 之后使用 MCP `execute_python`。

最小检查项：

- 加载生成的 `UBehaviorTree`。
- 断言 `BTGraph` 存在。
- 断言 graph nodes 存在，并且只包含一个 root graph node。
- 断言主 tree graph node 的坐标不全是 `(0, 0)`。
- 断言两个 primary tree graph node 不会重叠在同一 `(NodePosX, NodePosY)`。
- 断言 root graph node 位于第一个 behavior node 上方。
- 断言每个 child graph node 的 `NodePosY` 大于 parent。
- 断言 sibling 的 `NodePosX` 顺序匹配 `Children` 顺序。
- 断言 decorators 和 services 作为 subnodes 挂在预期 owner graph node 下。

验证脚本应返回 JSON，让测试失败时能给出明确原因，而不是依赖日志文本。

## 10. 验收标准

- Create 会为新的 BehaviorTree 资产生成 `BTGraph`。
- Update 会为更新后的 BehaviorTree 资产重建可预测 `BTGraph`。
- 在 UE 编辑器中打开生成的 BT 时，默认布局可读。
- Root、Composite、Task、Decorator、Service 的关系符合 UE Behavior Tree 编辑器习惯。
- 布局由 BT 结构派生，不硬编码具体节点类。
- MCP 验证能确认 graph 存在、坐标不重叠、parent-child 坐标关系正确。
- 长标题节点或带多个 Decorator / Service 的节点不会回退到默认重叠布局。
- UBT `Development` 配置编译通过。
- 至少一个包含 Root、Composite、Task、Decorator、Service 的 BT 能通过 MCP generate/extract 验证。

## 11. 风险与缓解

### BehaviorTreeEditor 模块耦合

`UBehaviorTreeGraph` 是 editor-only 类型。插件当前运行在编辑器上下文中，但实现仍应在适当位置使用 editor-only guard。

### UE Graph 重建副作用

`UBehaviorTreeGraph::UpdateAsset()` 会根据 graph pins 反向重建语义 BT 数据。generator 应先构建语义数据，再根据语义数据创建 graph nodes 并排布；除非 graph 已确认完整，否则避免不必要的语义重写。

### Slate 依赖的 AutoArrange

`UBehaviorTreeGraph::AutoArrange()` 使用 Slate node widget，不应作为生成阶段布局来源。直接写 `UEdGraphNode` 坐标可以避免这类依赖。

### Subnode 坐标

Decorators 和 services 是 graph subnodes，不一定是独立 primary tree nodes。自动化验证应优先检查 subnode 的 parent attachment 和顺序，只在 UE 暴露有意义坐标时检查坐标。

## 12. 自审记录

- 没有引入 JSON 坐标输入。
- Create 和 Update 行为已明确。
- 验证可通过现有 MCP `execute_python` 自动化。
- 保留动态化设计约束。
- 已承认上一版 spec 曾把布局坐标列为非目标；本 spec 将范围收敛到 editor graph layout。
