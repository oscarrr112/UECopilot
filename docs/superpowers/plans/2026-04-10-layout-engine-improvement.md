# 蓝图布局引擎改进 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 改进 LayoutEngine 的节点尺寸估算和数据节点定位，使自动布局的蓝图更紧凑、连线更短。

**Architecture:** 新增 `EstimateNodeSize` 启发式方法替换所有硬编码尺寸，重写 `PositionDataNodesNearConsumers` 使数据节点基于 Pin 连接位置定位，调整默认间距参数。不改变 Sugiyama 分层算法核心。

**Tech Stack:** UE5 C++ (UK2Node, UEdGraphPin, EdGraphSchema_K2)

---

## 文件结构

- **修改:** `Plugins/UECopilot/Source/AssetFactoryAI/Public/Factory/LayoutEngine.h` — 新增 `EstimateNodeSize` 声明，修改默认间距
- **修改:** `Plugins/UECopilot/Source/AssetFactoryAI/Private/Factory/LayoutEngine.cpp` — 实现 `EstimateNodeSize`，替换硬编码尺寸，重写数据节点定位，调整参数

---

### Task 1: 调整默认间距参数

**Files:**
- Modify: `Plugins/UECopilot/Source/AssetFactoryAI/Public/Factory/LayoutEngine.h:22,26`
- Modify: `Plugins/UECopilot/Source/AssetFactoryAI/Private/Factory/LayoutEngine.cpp:300,549`

- [ ] **Step 1: 修改 FLayoutSettings 默认值**

在 `LayoutEngine.h` 中修改两个默认值：

```cpp
// 第 22 行，将 300.0f 改为 250.0f
float HorizontalSpacing = 250.0f;

// 第 26 行，将 150.0f 改为 100.0f
float VerticalSpacing = 100.0f;
```

- [ ] **Step 2: 修改 SubgraphSpacing**

在 `LayoutEngine.cpp` 的 `AutoLayoutGraph` 方法中，第 300 行：

```cpp
// 将 200.0f 改为 150.0f
const float SubgraphSpacing = 150.0f;
```

- [ ] **Step 3: 修改重叠检测 Padding**

在 `LayoutEngine.cpp` 的 `AutoLayoutNodes` 方法中，第 549 行：

```cpp
// 将 Settings.VerticalSpacing * 0.35f 改为固定 20.0f
ResolveNodeOverlapsWithPush(Nodes, 20.0f);
```

- [ ] **Step 4: 编译验证**

Run: 在 UE5 编辑器中编译（Live Coding 或完整构建）
Expected: 编译成功，无错误

- [ ] **Step 5: 提交**

```bash
git add Plugins/UECopilot/Source/AssetFactoryAI/Public/Factory/LayoutEngine.h Plugins/UECopilot/Source/AssetFactoryAI/Private/Factory/LayoutEngine.cpp
git commit -m "refactor(layout): 调整默认间距参数，减小节点间距"
```

---

### Task 2: 新增 EstimateNodeSize 方法

**Files:**
- Modify: `Plugins/UECopilot/Source/AssetFactoryAI/Public/Factory/LayoutEngine.h:246`
- Modify: `Plugins/UECopilot/Source/AssetFactoryAI/Private/Factory/LayoutEngine.cpp`（在 `IsPureDataNode` 方法之后插入）

- [ ] **Step 1: 在头文件添加声明**

在 `LayoutEngine.h` 的 `IsPureDataNode` 声明之后（第 246 行后）添加：

```cpp
	/**
	 * Estimate node visual size based on pin count and title length
	 * @param Node - The node to estimate size for
	 * @return Estimated (Width, Height) in pixels
	 */
	static FVector2D EstimateNodeSize(UK2Node* Node);
```

- [ ] **Step 2: 在 cpp 文件实现**

在 `LayoutEngine.cpp` 的 `IsPureDataNode` 方法之后（第 1280 行后）插入实现：

```cpp
FVector2D ULayoutEngine::EstimateNodeSize(UK2Node* Node)
{
	if (!Node)
	{
		return FVector2D(200.0f, 100.0f);
	}

	// Count visible pins
	int32 InputPinCount = 0;
	int32 OutputPinCount = 0;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->bHidden)
		{
			continue;
		}
		if (Pin->Direction == EGPD_Input)
		{
			InputPinCount++;
		}
		else
		{
			OutputPinCount++;
		}
	}

	// Height: base header + pins
	const float BaseHeight = 60.0f;
	const float PinHeight = 26.0f;
	float Height = BaseHeight + FMath::Max(InputPinCount, OutputPinCount) * PinHeight;

	// Width: based on title length
	const float MinWidth = 150.0f;
	const float CharWidth = 8.0f;
	const float WidthPadding = 60.0f;
	FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
	float Width = FMath::Max(MinWidth, Title.Len() * CharWidth + WidthPadding);

	// Clamp to reasonable range
	Width = FMath::Clamp(Width, 150.0f, 500.0f);
	Height = FMath::Clamp(Height, 60.0f, 600.0f);

	return FVector2D(Width, Height);
}
```

- [ ] **Step 3: 编译验证**

Run: 编译插件
Expected: 编译成功

- [ ] **Step 4: 提交**

```bash
git add Plugins/UECopilot/Source/AssetFactoryAI/Public/Factory/LayoutEngine.h Plugins/UECopilot/Source/AssetFactoryAI/Private/Factory/LayoutEngine.cpp
git commit -m "feat(layout): 新增 EstimateNodeSize 启发式节点尺寸估算"
```

---

### Task 3: 替换硬编码节点尺寸

**Files:**
- Modify: `Plugins/UECopilot/Source/AssetFactoryAI/Private/Factory/LayoutEngine.cpp:15-99,1452-1475`

- [ ] **Step 1: 修改 ResolveNodeOverlapsWithPush 中的 FNodePlacement 构造**

在 `LayoutEngine.cpp` 第 40-41 行，将固定尺寸改为动态估算：

```cpp
			// 替换原来的:
			// const FNodePlacement Placement(Node, static_cast<float>(Node->NodePosX), static_cast<float>(Node->NodePosY), 320.0f, 180.0f);
			// 改为:
			const FVector2D NodeSize = ULayoutEngine::EstimateNodeSize(Node);
			const FNodePlacement Placement(Node, static_cast<float>(Node->NodePosX), static_cast<float>(Node->NodePosY), NodeSize.X, NodeSize.Y);
```

- [ ] **Step 2: 修改 MakeRect lambda**

在 `LayoutEngine.cpp` 第 48-59 行，将 `MakeRect` lambda 改为使用动态尺寸：

```cpp
		auto MakeRect = [](const UK2Node* Node)
		{
			FNodePlacement Rect;
			if (Node)
			{
				Rect.X = static_cast<float>(Node->NodePosX);
				Rect.Y = static_cast<float>(Node->NodePosY);
				const FVector2D Size = ULayoutEngine::EstimateNodeSize(const_cast<UK2Node*>(Node));
				Rect.Width = Size.X;
				Rect.Height = Size.Y;
			}
			return Rect;
		};
```

- [ ] **Step 3: 修改 CalculateBoundingBox**

在 `LayoutEngine.cpp` 的 `CalculateBoundingBox` 方法（第 1452 行起），将固定尺寸替换：

```cpp
void ULayoutEngine::CalculateBoundingBox(const TArray<UK2Node*>& Nodes, float& OutMinX, float& OutMaxX, float& OutMinY, float& OutMaxY)
{
	if (Nodes.Num() == 0)
	{
		OutMinX = OutMaxX = OutMinY = OutMaxY = 0;
		return;
	}

	OutMinX = OutMinY = FLT_MAX;
	OutMaxX = OutMaxY = -FLT_MAX;

	for (UK2Node* Node : Nodes)
	{
		if (!Node) continue;

		const FVector2D NodeSize = EstimateNodeSize(Node);
		OutMinX = FMath::Min(OutMinX, static_cast<float>(Node->NodePosX));
		OutMaxX = FMath::Max(OutMaxX, static_cast<float>(Node->NodePosX) + NodeSize.X);
		OutMinY = FMath::Min(OutMinY, static_cast<float>(Node->NodePosY));
		OutMaxY = FMath::Max(OutMaxY, static_cast<float>(Node->NodePosY) + NodeSize.Y);
	}
}
```

- [ ] **Step 4: 编译验证**

Run: 编译插件
Expected: 编译成功

- [ ] **Step 5: 提交**

```bash
git add Plugins/UECopilot/Source/AssetFactoryAI/Private/Factory/LayoutEngine.cpp
git commit -m "refactor(layout): 用 EstimateNodeSize 替换所有硬编码节点尺寸"
```

---

### Task 4: 重写 PositionDataNodesNearConsumers

**Files:**
- Modify: `Plugins/UECopilot/Source/AssetFactoryAI/Private/Factory/LayoutEngine.cpp:1329-1428`

- [ ] **Step 1: 重写 PositionDataNodesNearConsumers 方法**

将 `LayoutEngine.cpp` 第 1329-1428 行的整个方法替换为：

```cpp
void ULayoutEngine::PositionDataNodesNearConsumers(
	TArray<UK2Node*>& DataNodes,
	TArray<UK2Node*>& ExecNodes,
	const FLayoutSettings& Settings)
{
	if (DataNodes.Num() == 0)
	{
		return;
	}

	const float OffsetX = Settings.HorizontalSpacing * 0.6f;
	const float CollisionPadding = 20.0f;

	// Track placed data nodes for collision detection
	TArray<FNodePlacement> PlacedNodes;

	// Pre-populate with exec node positions
	for (UK2Node* ExecNode : ExecNodes)
	{
		if (!ExecNode) continue;
		const FVector2D Size = EstimateNodeSize(ExecNode);
		PlacedNodes.Add(FNodePlacement(ExecNode, static_cast<float>(ExecNode->NodePosX),
			static_cast<float>(ExecNode->NodePosY), Size.X, Size.Y));
	}

	for (UK2Node* DataNode : DataNodes)
	{
		if (!DataNode) continue;

		// Find all consumers and their connected pin positions
		float SumX = 0.0f;
		float SumY = 0.0f;
		int32 ConsumerCount = 0;

		for (UEdGraphPin* Pin : DataNode->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output)
			{
				continue;
			}
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNode())
				{
					continue;
				}
				UK2Node* Consumer = Cast<UK2Node>(LinkedPin->GetOwningNode());
				if (!Consumer)
				{
					continue;
				}

				// Estimate the Y offset of the connected pin on the consumer
				int32 PinIndex = 0;
				int32 VisibleIndex = 0;
				for (UEdGraphPin* ConsumerPin : Consumer->Pins)
				{
					if (!ConsumerPin || ConsumerPin->bHidden)
					{
						continue;
					}
					if (ConsumerPin == LinkedPin)
					{
						PinIndex = VisibleIndex;
						break;
					}
					if (ConsumerPin->Direction == LinkedPin->Direction)
					{
						VisibleIndex++;
					}
				}

				float ConsumerPinY = static_cast<float>(Consumer->NodePosY) + 60.0f + PinIndex * 26.0f;
				SumX += static_cast<float>(Consumer->NodePosX);
				SumY += ConsumerPinY;
				ConsumerCount++;
			}
		}

		float TargetX, TargetY;
		if (ConsumerCount > 0)
		{
			// Position at barycenter of consumers, offset to the left
			TargetX = (SumX / ConsumerCount) - OffsetX;
			TargetY = (SumY / ConsumerCount) - 30.0f; // Center the node vertically on the pin
		}
		else
		{
			// Orphan data node
			TargetX = Settings.StartX - OffsetX;
			TargetY = Settings.StartY;
		}

		// Resolve collisions with already-placed nodes using alternating up/down
		const FVector2D DataSize = EstimateNodeSize(DataNode);
		FNodePlacement Candidate(DataNode, TargetX, TargetY, DataSize.X, DataSize.Y);

		int32 Direction = -1; // Start by trying upward
		float Step = DataSize.Y + CollisionPadding;
		int32 Attempts = 0;
		float OriginalY = TargetY;

		while (Attempts < 20)
		{
			bool bOverlap = false;
			for (const FNodePlacement& Existing : PlacedNodes)
			{
				if (Candidate.Overlaps(Existing, CollisionPadding))
				{
					bOverlap = true;
					break;
				}
			}

			if (!bOverlap)
			{
				break;
			}

			// Alternate: attempt 1 = up, attempt 2 = down, attempt 3 = up further, etc.
			Attempts++;
			float Offset = ((Attempts + 1) / 2) * Step;
			if (Attempts % 2 == 1)
			{
				Candidate.Y = OriginalY - Offset; // Up
			}
			else
			{
				Candidate.Y = OriginalY + Offset; // Down
			}
		}

		// Apply position
		DataNode->NodePosX = static_cast<int32>(Candidate.X);
		DataNode->NodePosY = static_cast<int32>(Candidate.Y);

		// Track this node for future collision checks
		PlacedNodes.Add(Candidate);
	}
}
```

- [ ] **Step 2: 编译验证**

Run: 编译插件
Expected: 编译成功

- [ ] **Step 3: 提交**

```bash
git add Plugins/UECopilot/Source/AssetFactoryAI/Private/Factory/LayoutEngine.cpp
git commit -m "refactor(layout): 重写数据节点定位逻辑，基于 Pin 连接位置和重心定位"
```

---

### Task 5: 编译测试并验证效果

- [ ] **Step 1: 完整编译插件**

Run: 在 UE5 编辑器中编译或使用 `UnrealBuildTool` 构建 Development Editor 配置
Expected: 编译成功，0 错误

- [ ] **Step 2: 使用 MCP 重新布局 BP_InteractiveDoor**

通过 MCP 工具 `layout_blueprint_graph` 对 `/Game/Blueprints/BP_InteractiveDoor` 的 EventGraph 执行自动布局：

```
调用 layout_blueprint_graph，参数：
- blueprint_path: "/Game/Blueprints/BP_InteractiveDoor"
- graph_name: "EventGraph"
```

- [ ] **Step 3: 目视检查布局效果**

在 UE5 编辑器中打开 BP_InteractiveDoor 的 EventGraph，检查：
- 节点间距是否比之前更紧凑
- Get 变量节点是否贴近它们连接的消费者节点
- 连线长度是否明显缩短
- 两组子图（BeginOverlap / EndOverlap）之间间距是否合理
- 没有节点重叠

- [ ] **Step 4: 最终提交（如果有任何微调）**

```bash
git add -A
git commit -m "fix(layout): 微调布局参数"
```
