# 蓝图布局引擎改进 设计规格

## 目标

改进 UECopilot 插件的 `LayoutEngine`，解决自动布局时节点间距过大、连线分散、数据节点远离消费者等问题，使生成的蓝图图表更紧凑、更易读。

## 问题分析

当前布局引擎存在以下问题：

1. **固定节点尺寸估算（320x180）** — 所有节点不分大小都按同一尺寸处理，导致小节点（如 Get 变量）周围产生大量无意义空白
2. **默认间距过大** — HorizontalSpacing=300, VerticalSpacing=150，导致图表整体稀疏
3. **数据节点定位粗糙** — `PositionDataNodesNearConsumers` 使用固定偏移（180, -80），不考虑实际连接 Pin 位置
4. **重叠推挤过于激进** — 使用固定大尺寸做碰撞检测，本来不重叠的节点也被推开

## 架构

修改范围限定在两个文件：
- `LayoutEngine.h` — 新增 `EstimateNodeSize` 方法声明
- `LayoutEngine.cpp` — 修改尺寸估算、数据节点定位、默认参数

不改变整体 Sugiyama 分层算法架构，只优化尺寸估算和节点定位两个环节。

## 改动详情

### 1. 新增 EstimateNodeSize 静态方法

位置：`LayoutEngine.h` 新增声明，`LayoutEngine.cpp` 实现

基于 `UK2Node` 数据层可访问信息做启发式估算：

```cpp
static FVector2D EstimateNodeSize(UK2Node* Node);
```

估算逻辑：
- **高度** = `BaseHeight(60) + max(InputPinCount, OutputPinCount) * PinHeight(26)`
  - 只计算非隐藏 Pin（`!Pin->bHidden`）
  - BaseHeight 包含标题栏区域
- **宽度** = `max(MinWidth(150), TitleLen * CharWidth(8) + Padding(60))`
  - 使用 `Node->GetNodeTitle(ENodeTitleType::FullTitle)` 获取标题
  - MinWidth 保证最小可视宽度
- **特殊节点** — Comment 节点使用更大的默认尺寸

### 2. 替换所有硬编码节点尺寸

以下位置的固定 `320x180` 或 `200x100` 改为调用 `EstimateNodeSize`：

- `ResolveNodeOverlapsWithPush`（LayoutEngine.cpp:41）— `FNodePlacement` 构造
- `ResolveNodeOverlapsWithPush`（LayoutEngine.cpp:58-59）— `MakeRect` lambda
- `CalculateBoundingBox`（LayoutEngine.cpp:1463-1464）— 节点宽高常量

### 3. 改进 PositionDataNodesNearConsumers

当前逻辑（固定偏移 + 单向堆叠）替换为：

- **X 位置**：`consumer.NodePosX - HorizontalSpacing * 0.6`（跟随间距设定而非固定 180）
- **Y 位置**：基于连接的 Pin 索引估算 Y 偏移，让数据节点对齐到消费者节点上它实际连接的 Pin 高度附近
- **多消费者取重心**：当数据节点连接多个消费者时，取所有消费者位置的重心
- **双向堆叠**：碰撞时交替向上/向下分配，而不是只往上推

### 4. 调整默认参数

| 参数 | 现值 | 新值 |
|------|------|------|
| `FLayoutSettings::HorizontalSpacing` | 300.0f | 250.0f |
| `FLayoutSettings::VerticalSpacing` | 150.0f | 100.0f |
| `SubgraphSpacing`（AutoLayoutGraph 内局部变量） | 200.0f | 150.0f |
| `ResolveNodeOverlapsWithPush` 的 Padding 参数 | `VerticalSpacing * 0.35f` | 20.0f（固定值） |

### 5. 碰撞检测精度改进

`PositionDataNodesNearConsumers` 中的碰撞检测：
- 现有：`GridX = TargetX / 100` 量化到 100px 网格 + `StackSpacing = 100`
- 改为：使用 `EstimateNodeSize` 返回的实际尺寸 + 20px padding 做精确矩形碰撞检测

## 不改动的部分

- Sugiyama 分层算法（AssignLayers, MinimizeCrossings, CalculatePositions）
- 子图发现逻辑（FindSubgraphs, CollectConnectedNodes）
- Exec/Data 前驱后继关系查找
- CalculateLayout（基于 JSON 数据的预计算布局，不涉及实际节点）
- 头文件的 FLayoutSettings USTRUCT 结构（只改默认值）

## 测试方案

1. 重新编译插件
2. 使用 MCP 工具 `layout_blueprint_graph` 对现有 BP_InteractiveDoor 重新排布
3. 目视检查：节点间距是否紧凑、数据节点是否贴近消费者、连线是否短而清晰
4. 对比改进前后的截图
