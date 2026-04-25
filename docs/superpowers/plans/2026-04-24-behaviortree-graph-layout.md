# BehaviorTree 图布局 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `BehaviorTree` generator 在 Create 和 Update 后生成稳定、可读、结构驱动的 `BTGraph` 默认布局。

**Architecture:** 保持 runtime-first：先按现有逻辑构建 `UBehaviorTree::RootNode` 和 children/decorator/service 语义树，再复用 UE 编辑器 graph 构造路径创建 `UBehaviorTreeGraph` 和 root graph node，调用 `SpawnMissingNodes()` 反建 graph nodes，最后手写 `NodePosX/NodePosY`。布局算法只依赖 BT 树结构、graph node role 和 subnode 数量，不按具体 BT 节点类名分支。

**Tech Stack:** Unreal Engine 5.7 C++ editor modules、`BehaviorTreeEditor`、`AIGraph`、MCP `generate_assets` / `execute_python`、UBT `Development` 编译。

---

## File Structure

- Modify: `Source/AssetFactory/Private/Generators/BehaviorTreeGenerator.cpp`
  - 添加 editor-only BTGraph 创建和布局 helper。
  - 修改 `Generate`，不再在生成结束前清空 `BTGraph`。
  - 修改 `FinalizeBT`，在 execution index 初始化后生成并布局 graph。
- Modify: `Source/AssetFactory/AssetFactory.Build.cs`
  - 给 `AssetFactory` 添加实际需要的 editor module 依赖：`BehaviorTreeEditor`、`AIGraph`。
- Modify: `Source/AssetFactory/Public/AssetFactorySubsystem.h`
  - 暴露 editor-only 验证入口 `GetBehaviorTreeGraphLayoutDiagnostics`，供 MCP Python 调用。
- Modify: `Source/AssetFactory/Private/AssetFactorySubsystem.cpp`
  - 在 C++ 侧读取 protected graph/node 数据并返回 layout diagnostics JSON。
- Create: `docs/superpowers/verification/bt_graph_layout_check.py`
  - 保存 MCP `execute_python` 可复用验证脚本，通过 subsystem diagnostics 检查 graph 存在、坐标、层级关系和 subnode attachment。
- Reference only: `docs/superpowers/specs/2026-04-24-behaviortree-graph-layout-design.md`
  - 不在执行中随意扩展范围；如果实现发现 spec 需要修订，先记录再同步。

本仓库用户指令优先于通用计划模板：除非用户明确要求，不自动 `git commit`。每个任务末尾使用 `git status --short` 作为 checkpoint。

---

## 追加 Task: 布局密度调优

**背景：** 初版布局在复杂树上过于横向铺开。用户截图显示 `BT_LayoutComplex` 的根下分支线条过长，默认视图观感偏丑。调优应收紧横向，不改变 BT 语义，不新增 JSON 坐标字段。

**Files:**
- Modify: `Source/AssetFactory/Private/Generators/BehaviorTreeGenerator.cpp`
- Modify: `docs/superpowers/verification/bt_graph_layout_compactness_check.py`
- Reference: `Source/AssetFactory/Private/AssetFactorySubsystem.cpp`

- [x] **Step 1: 写 compactness 红灯验证**

Create `docs/superpowers/verification/bt_graph_layout_compactness_check.py`。脚本通过 `AssetFactorySubsystem.get_behavior_tree_graph_layout_diagnostics()` 读取 `/Game/UECopilotTests/BTLayout/BT_LayoutComplex.BT_LayoutComplex`，使用 diagnostics 中带 `estimatedBounds` 的 primary graph nodes 计算横向跨度，并断言复杂 fixture 至少包含 10 个 primary graph nodes。

Expected before tuning: current complex fixture span is greater than `3800` and the script fails.

- [x] **Step 2: 收紧布局常量**

In `BehaviorTreeGenerator.cpp`, adjust only structure-level layout constants:

```cpp
constexpr int32 HorizontalSpacing = 220;
constexpr int32 MinimumSubtreeWidth = 260;
constexpr int32 NodePadding = 32;
```

Keep `VerticalSpacing` unchanged. Do not add concrete BT node class rules.

- [x] **Step 3: 重新生成复杂 fixture 并验证**

Run MCP `generate_assets` for `BT_LayoutComplex`, then run:

```python
exec(open(r"E:\GameDev\worktrees\UECopilot\bt-graph-layout\docs\superpowers\verification\bt_graph_layout_compactness_check.py", encoding="utf-8").read())
```

Expected after tuning: diagnostics `verified=true`, no overlap errors, primary node count `>= 10`, primary bounds width `<= 3800`.

- [x] **Step 4: 回归验证**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" BTLayoutHostEditor Win64 Development "-Project=E:/GameDev/worktrees/UECopilot/bt-layout-host/BTLayoutHost.uproject" -NoHotReload
python -m py_compile docs\superpowers\verification\bt_graph_layout_check.py docs\superpowers\verification\bt_graph_layout_compactness_check.py
git diff --check
```

Run MCP diagnostics for both `BT_LayoutRed` and `BT_LayoutComplex`.

Verification results after tuning:
- UBT `BTLayoutHostEditor Win64 Development`: `Result: Succeeded`.
- `BT_LayoutComplex`: diagnostics `verified=true`, `blackboardPreserved=true`, `primaryNodeCount=13`, `primaryBoundsWidth=3616`, `maxPrimaryBoundsWidth=3800`.
- `BT_LayoutRed`: diagnostics `verified=true`, `blackboardPreserved=true`, root/composite/task hierarchy and subnode attachment valid.
- `python -m py_compile` for both verification scripts: exit 0.
- `git diff --check`: exit 0.
- Spec review, code quality review, and fix review subagents reported no remaining issues after the compactness script fix.

---

## 追加 Task: 人工排布风格紧凑化

**背景：** 用户手工排布截图显示，复杂 BT 可以比当前 `primaryBoundsWidth=3616` 更紧凑：根下分支仍保留左中右顺序，但 sibling 之间没有大段空白，局部任务节点更贴近父 composite。此任务继续遵守结构驱动原则，不引入具体节点类型或节点名称规则。

**Files:**
- Modify: `docs/superpowers/verification/bt_graph_layout_compactness_check.py`
- Modify: `Source/AssetFactory/Private/Generators/BehaviorTreeGenerator.cpp`
- Reference: `docs/superpowers/specs/2026-04-24-behaviortree-graph-layout-design.md`

- [x] **Step 1: 收紧红灯验证**

Change `MAX_PRIMARY_BOUNDS_WIDTH` in `docs/superpowers/verification/bt_graph_layout_compactness_check.py`:

```python
MAX_PRIMARY_BOUNDS_WIDTH = 3000
MAX_ROOT_CHILD_CENTER_GAP = 1200
MAX_CENTER_BRANCH_OFFSET = 320
ROOT_CHILD_ROW_TOLERANCE = 24
```

Run the script through MCP against the existing generated `BT_LayoutComplex`.

Expected before production code changes: diagnostics still reports `verified=true`, `primaryNodeCount >= 10`, but the script fails because current `primaryBoundsWidth` is `3616`, greater than `3000`. The script also checks that the root child row has left / center / right branches, the center branch is within 320 graph units of the root center, and adjacent root child branch centers are no more than 1200 graph units apart.

- [x] **Step 2: 改为更紧凑的结构级 sibling packing**

In `Source/AssetFactory/Private/Generators/BehaviorTreeGenerator.cpp`, reduce only structure-level layout constants used by the existing tree algorithm:

```cpp
constexpr int32 HorizontalSpacing = 100;
constexpr int32 MinimumSubtreeWidth = 240;
constexpr int32 NodePadding = 32;
```

Keep `VerticalSpacing` unchanged so tall composite nodes with Decorator / Service subnodes still have enough vertical room. Do not add rules for `CombatBranch`, `PatrolBranch`, `FallbackBranch`, `BTTask_Wait`, or any other concrete node class/name.

- [x] **Step 3: 重新生成复杂 fixture 并验证绿灯**

Run MCP `generate_assets` for `/Game/UECopilotTests/BTLayout/BT_LayoutComplex`, then run:

```python
exec(open(r"E:\GameDev\worktrees\UECopilot\bt-graph-layout\docs\superpowers\verification\bt_graph_layout_compactness_check.py", encoding="utf-8").read())
```

Expected after tuning: diagnostics `verified=true`, `blackboardPreserved=true`, `primaryNodeCount >= 10`, `primaryBoundsWidth <= 3000`, root child row count `>= 3`, center branch offset `<= 320`, and max root child center gap `<= 1200`.

- [x] **Step 4: 回归验证**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" BTLayoutHostEditor Win64 Development "-Project=E:/GameDev/worktrees/UECopilot/bt-layout-host/BTLayoutHost.uproject" -NoHotReload
python -m py_compile docs\superpowers\verification\bt_graph_layout_check.py docs\superpowers\verification\bt_graph_layout_compactness_check.py
git diff --check
```

Run MCP diagnostics for both `BT_LayoutRed` and `BT_LayoutComplex`. Reviewers must confirm the change remains structure-driven and does not overfit fixture-specific node names.

Verification results after this tuning:
- Red: after regenerating with the previous 3800-era generator, `BT_LayoutComplex` diagnostics still reported `verified=true`, but compactness failed with `primaryBoundsWidth=3616`, `maxPrimaryBoundsWidth=3000`.
- Green: after `HorizontalSpacing=100` and `MinimumSubtreeWidth=240`, regenerated `BT_LayoutComplex` reported `verified=true`, `blackboardPreserved=true`, `primaryNodeCount=13`, `primaryBoundsWidth=2728`, `rootChildRowCount=3`, `rootChildCenters=[-880.0, 182.0, 1062.0]`, `maxRootChildCenterGap=1062.0`, `centerBranchOffset=182.0`.
- Regression: regenerated `BT_LayoutRed` reported `verified=true`, `blackboardPreserved=true`; root/composite/task hierarchy and subnode attachment remained valid.
- UBT `BTLayoutHostEditor Win64 Development`: `Result: Succeeded`.

---

### Task 1: 写并确认 MCP 红灯验证

**Files:**
- Create: `docs/superpowers/verification/bt_graph_layout_check.py`
- No production code changes in this task.

- [ ] **Step 1: 创建 MCP 验证脚本**

Create `docs/superpowers/verification/bt_graph_layout_check.py`:

```python
import json
import unreal

ASSET_PATH = "/Game/UECopilotTests/BTLayout/BT_LayoutRed.BT_LayoutRed"

subsystem = unreal.get_editor_subsystem(unreal.AssetFactorySubsystem)
if subsystem is None:
    raise Exception(json.dumps({
        "assetPath": ASSET_PATH,
        "failure": "AssetFactorySubsystem is unavailable",
    }, ensure_ascii=False))

diagnostics_json = subsystem.get_behavior_tree_graph_layout_diagnostics(ASSET_PATH)
diagnostics = json.loads(diagnostics_json)

if diagnostics.get("errors"):
    raise Exception(json.dumps(diagnostics, ensure_ascii=False))

if not diagnostics.get("verified"):
    diagnostics["failure"] = "diagnostics did not report verified=true"
    raise Exception(json.dumps(diagnostics, ensure_ascii=False))

print(json.dumps(diagnostics, ensure_ascii=False))
```

- [ ] **Step 2: 生成红灯 fixture**

Run via MCP `generate_assets`:

```json
{
  "assets": [
    {
      "AssetType": "BehaviorTree",
      "Name": "BT_LayoutRed",
      "Path": "/Game/UECopilotTests/BTLayout",
      "Action": "CreateOrUpdate",
      "BlackboardInline": {
        "Name": "BB_BTLayoutRed",
        "Path": "/Game/UECopilotTests/BTLayout",
        "Keys": [
          { "Name": "HasTarget", "Type": "Bool" },
          { "Name": "MoveLocation", "Type": "Vector" }
        ]
      },
      "Root": {
        "Node": "BTComposite_Sequence",
        "InstanceName": "RootSequence",
        "Services": [
          { "Type": "BTService_DefaultFocus" }
        ],
        "Children": [
          {
            "Node": "BTTask_Wait",
            "InstanceName": "WaitShort",
            "Decorators": [
              {
                "Type": "BTDecorator_Blackboard",
                "Properties": {
                  "BlackboardKey": { "SelectedKeyName": "HasTarget" }
                }
              }
            ],
            "Properties": {
              "WaitTime": { "DefaultValue": 0.2 }
            }
          },
          {
            "Node": "BTTask_Wait",
            "InstanceName": "WaitLong",
            "Properties": {
              "WaitTime": { "DefaultValue": 1.0 }
            }
          }
        ]
      }
    }
  ]
}
```

Expected: generation succeeds, because existing semantic BT generation works.

- [ ] **Step 3: 运行红灯验证**

Run via MCP `execute_python` using the exact contents of `docs/superpowers/verification/bt_graph_layout_check.py`.

Expected before implementation:

```json
{
  "asset_loaded": true,
  "graph_path": "/Game/UECopilotTests/BTLayout/BT_LayoutRed.BT_LayoutRed:Behavior Tree",
  "has_bt_graph_inner": false
}
```

- [ ] **Step 4: Checkpoint**

Run:

```powershell
git status --short
```

Expected: only plan/spec/verification files are changed or untracked; no production code yet.

---

### Task 2: 添加 BTGraph editor 依赖和 include

**Files:**
- Modify: `Source/AssetFactory/AssetFactory.Build.cs`
- Modify: `Source/AssetFactory/Private/Generators/BehaviorTreeGenerator.cpp`

- [ ] **Step 1: 修改 module 依赖**

In `Source/AssetFactory/AssetFactory.Build.cs`, add these dependencies to `PrivateDependencyModuleNames` next to existing editor graph dependencies:

```csharp
"BehaviorTreeEditor",
"AIGraph",
```

Expected surrounding block:

```csharp
"Kismet",
"BlueprintGraph",
"BehaviorTreeEditor",
"AIGraph",
"EditorSubsystem",
"AIModule",
```

- [ ] **Step 2: 添加 editor-only include**

In `Source/AssetFactory/Private/Generators/BehaviorTreeGenerator.cpp`, add after existing BehaviorTree includes:

```cpp
#if WITH_EDITORONLY_DATA
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_BehaviorTree.h"
#include "Kismet2/BlueprintEditorUtils.h"
#endif
```

- [ ] **Step 3: 编译检查**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected: either compile succeeds, or fails only because currently打开的编辑器锁住 DLL。If DLL lock happens, close editor and rerun.

- [ ] **Step 4: Checkpoint**

Run:

```powershell
git status --short
```

Expected: `AssetFactory.Build.cs` and `BehaviorTreeGenerator.cpp` modified.

---

### Task 3: 实现 editor graph 创建 helper

**Files:**
- Modify: `Source/AssetFactory/Private/Generators/BehaviorTreeGenerator.cpp`

- [ ] **Step 1: 添加 graph 清理和 root 查找 helper**

Inside the anonymous namespace, after `InitializeBTNodeForAsset`, add an editor-only namespace:

```cpp
#if WITH_EDITORONLY_DATA
namespace BTEditorGraphLayout
{
	void RenameGraphOutOfAsset(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return;
		}

		Graph->SetFlags(RF_Transient);
		Graph->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
		Graph->MarkAsGarbage();
	}

	UBehaviorTreeGraphNode_Root* FindRootGraphNode(UBehaviorTreeGraph* Graph)
	{
		if (!Graph)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UBehaviorTreeGraphNode_Root* RootGraphNode = Cast<UBehaviorTreeGraphNode_Root>(Node))
			{
				return RootGraphNode;
			}
		}

		return nullptr;
	}
}
#endif
```

- [ ] **Step 2: 添加 `RebuildEditorGraph`**

In the same namespace, add:

```cpp
UBehaviorTreeGraph* RebuildEditorGraph(UBehaviorTree* BT)
{
	if (!BT)
	{
		return nullptr;
	}

	RenameGraphOutOfAsset(BT->BTGraph);
	BT->BTGraph = FBlueprintEditorUtils::CreateNewGraph(
		BT,
		TEXT("Behavior Tree"),
		UBehaviorTreeGraph::StaticClass(),
		UEdGraphSchema_BehaviorTree::StaticClass());

	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!Graph)
	{
		return nullptr;
	}

	Graph->LockUpdates();
	if (const UEdGraphSchema* Schema = Graph->GetSchema())
	{
		Schema->CreateDefaultNodesForGraph(*Graph);
	}
	Graph->OnCreated();
	Graph->Initialize();
	Graph->UnlockUpdates();

	Graph->UpdateClassData();
	Graph->NotifyGraphChanged();
	Graph->MarkPackageDirty();
	return Graph;
}
```

- [ ] **Step 3: Wire helper without layout**

In `FBehaviorTreeGenerator::FinalizeBT`, after `InitializeBTNodeForAsset`:

```cpp
#if WITH_EDITORONLY_DATA
	BTEditorGraphLayout::RebuildEditorGraph(BT);
#endif
```

- [ ] **Step 4: Stop clearing graph**

In `FBehaviorTreeGenerator::Generate`, remove:

```cpp
BehaviorTree->BTGraph = nullptr;
```

Keep:

```cpp
BehaviorTree->LastEditedDocuments.Reset();
```

- [ ] **Step 5: Run MCP red test again**

Run the Task 1 fixture and verification.

Expected after this task: graph exists, but layout may still be default UE spawn layout. The red test may pass only basic graph existence; full layout assertions are completed in later tasks.

---

### Task 4: 实现结构驱动树布局

**Files:**
- Modify: `Source/AssetFactory/Private/Generators/BehaviorTreeGenerator.cpp`

- [ ] **Step 1: 添加布局常量和 structs**

Inside `BTEditorGraphLayout`:

```cpp
constexpr int32 RootY = 0;
constexpr int32 FirstTreeNodeY = 180;
constexpr int32 VerticalSpacing = 220;
constexpr int32 HorizontalSpacing = 360;
constexpr int32 MinimumSubtreeWidth = 360;
constexpr int32 NodePadding = 40;
constexpr int32 BaseNodeWidth = 220;
constexpr int32 BaseNodeHeight = 90;
constexpr int32 RootNodeWidth = 180;
constexpr int32 RootNodeHeight = 80;
constexpr int32 SubNodeHeight = 42;
constexpr int32 MaxOverlapPasses = 8;

struct FEstimatedGraphNodeSize
{
	float Width = BaseNodeWidth;
	float Height = BaseNodeHeight;
};

struct FLayoutTreeNode
{
	UBTNode* BTNode = nullptr;
	UBehaviorTreeGraphNode* GraphNode = nullptr;
	TArray<FLayoutTreeNode> Children;
	FEstimatedGraphNodeSize Size;
	float SubtreeWidth = MinimumSubtreeWidth;
};

struct FPlacedGraphNode
{
	UBehaviorTreeGraphNode* GraphNode = nullptr;
	FEstimatedGraphNodeSize Size;
};
```

- [ ] **Step 2: 添加尺寸估算**

```cpp
FString GetBTNodeDisplayName(const UBTNode* Node)
{
	if (!Node)
	{
		return FString();
	}

	if (!Node->NodeName.IsEmpty())
	{
		return Node->NodeName;
	}

	UClass* NodeClass = Node->GetClass();
	return NodeClass ? NodeClass->GetName() : FString();
}

FEstimatedGraphNodeSize EstimatePrimaryGraphNodeSize(const UBehaviorTreeGraphNode* GraphNode)
{
	FEstimatedGraphNodeSize Result;
	const UBTNode* NodeInstance = GraphNode ? Cast<UBTNode>(GraphNode->NodeInstance) : nullptr;
	const FString DisplayName = GetBTNodeDisplayName(NodeInstance);
	const int32 TitleWidth = DisplayName.Len() * 8 + 80;
	const int32 SubNodeCount = GraphNode ? GraphNode->Decorators.Num() + GraphNode->Services.Num() : 0;

	Result.Width = static_cast<float>(FMath::Clamp(FMath::Max(BaseNodeWidth, TitleWidth), BaseNodeWidth, 520));
	Result.Height = static_cast<float>(FMath::Clamp(BaseNodeHeight + SubNodeCount * SubNodeHeight, BaseNodeHeight, 640));
	return Result;
}

FEstimatedGraphNodeSize EstimateRootGraphNodeSize()
{
	FEstimatedGraphNodeSize Result;
	Result.Width = RootNodeWidth;
	Result.Height = RootNodeHeight;
	return Result;
}
```

- [ ] **Step 3: 建立 BT node 到 graph node 的映射**

```cpp
void BuildGraphNodeMap(UBehaviorTreeGraph* Graph, TMap<UBTNode*, UBehaviorTreeGraphNode*>& OutGraphNodes)
{
	OutGraphNodes.Reset();
	if (!Graph)
	{
		return;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UBehaviorTreeGraphNode* BTGraphNode = Cast<UBehaviorTreeGraphNode>(Node);
		UBTNode* BTNode = BTGraphNode ? Cast<UBTNode>(BTGraphNode->NodeInstance) : nullptr;
		if (BTNode)
		{
			OutGraphNodes.Add(BTNode, BTGraphNode);
		}
	}
}
```

- [ ] **Step 4: 从 BT 结构构建 layout tree**

```cpp
bool BuildLayoutTree(UBTNode* BTNode, const TMap<UBTNode*, UBehaviorTreeGraphNode*>& GraphNodes, FLayoutTreeNode& OutTree)
{
	if (!BTNode)
	{
		return false;
	}

	UBehaviorTreeGraphNode* const* GraphNodePtr = GraphNodes.Find(BTNode);
	if (!GraphNodePtr || !*GraphNodePtr)
	{
		return false;
	}

	OutTree.BTNode = BTNode;
	OutTree.GraphNode = *GraphNodePtr;
	OutTree.Size = EstimatePrimaryGraphNodeSize(OutTree.GraphNode);
	OutTree.Children.Reset();

	if (UBTCompositeNode* Composite = Cast<UBTCompositeNode>(BTNode))
	{
		for (int32 ChildIndex = 0; ChildIndex < Composite->Children.Num(); ++ChildIndex)
		{
			UBTNode* ChildNode = Composite->GetChildNode(ChildIndex);
			FLayoutTreeNode ChildTree;
			if (BuildLayoutTree(ChildNode, GraphNodes, ChildTree))
			{
				OutTree.Children.Add(MoveTemp(ChildTree));
			}
		}
	}

	return true;
}
```

- [ ] **Step 5: 计算 subtree width 并放置节点**

```cpp
float ComputeSubtreeWidth(FLayoutTreeNode& Tree)
{
	float ChildrenWidth = 0.0f;
	for (int32 ChildIndex = 0; ChildIndex < Tree.Children.Num(); ++ChildIndex)
	{
		ChildrenWidth += ComputeSubtreeWidth(Tree.Children[ChildIndex]);
		if (ChildIndex > 0)
		{
			ChildrenWidth += HorizontalSpacing;
		}
	}

	const float OwnWidth = FMath::Max(static_cast<float>(MinimumSubtreeWidth), Tree.Size.Width + NodePadding);
	Tree.SubtreeWidth = Tree.Children.Num() > 0 ? FMath::Max(OwnWidth, ChildrenWidth) : OwnWidth;
	return Tree.SubtreeWidth;
}

void PositionLayoutTree(FLayoutTreeNode& Tree, float CenterX, float PosY, TArray<FPlacedGraphNode>& OutPlacedNodes)
{
	if (Tree.GraphNode)
	{
		Tree.GraphNode->Modify();
		Tree.GraphNode->NodePosX = FMath::RoundToInt(CenterX - Tree.Size.Width * 0.5f);
		Tree.GraphNode->NodePosY = FMath::RoundToInt(PosY);

		FPlacedGraphNode Placement;
		Placement.GraphNode = Tree.GraphNode;
		Placement.Size = Tree.Size;
		OutPlacedNodes.Add(Placement);
	}

	float ChildLeft = CenterX - Tree.SubtreeWidth * 0.5f;
	for (FLayoutTreeNode& Child : Tree.Children)
	{
		const float ChildCenterX = ChildLeft + Child.SubtreeWidth * 0.5f;
		PositionLayoutTree(Child, ChildCenterX, PosY + VerticalSpacing, OutPlacedNodes);
		ChildLeft += Child.SubtreeWidth + HorizontalSpacing;
	}
}
```

- [ ] **Step 6: 添加 overlap sweep**

```cpp
bool DoPlacedNodesOverlap(const FPlacedGraphNode& A, const FPlacedGraphNode& B, float Padding)
{
	if (!A.GraphNode || !B.GraphNode || A.GraphNode == B.GraphNode)
	{
		return false;
	}

	const float ALeft = static_cast<float>(A.GraphNode->NodePosX);
	const float ATop = static_cast<float>(A.GraphNode->NodePosY);
	const float BLeft = static_cast<float>(B.GraphNode->NodePosX);
	const float BTop = static_cast<float>(B.GraphNode->NodePosY);

	return !(ALeft + A.Size.Width + Padding <= BLeft ||
		BLeft + B.Size.Width + Padding <= ALeft ||
		ATop + A.Size.Height + Padding <= BTop ||
		BTop + B.Size.Height + Padding <= ATop);
}

void ResolveOverlaps(TArray<FPlacedGraphNode>& PlacedNodes)
{
	PlacedNodes.Sort([](const FPlacedGraphNode& A, const FPlacedGraphNode& B)
	{
		const int32 AY = A.GraphNode ? A.GraphNode->NodePosY : 0;
		const int32 BY = B.GraphNode ? B.GraphNode->NodePosY : 0;
		if (AY == BY)
		{
			const int32 AX = A.GraphNode ? A.GraphNode->NodePosX : 0;
			const int32 BX = B.GraphNode ? B.GraphNode->NodePosX : 0;
			return AX < BX;
		}
		return AY < BY;
	});

	for (int32 Pass = 0; Pass < MaxOverlapPasses; ++Pass)
	{
		bool bMovedAny = false;
		for (int32 Index = 0; Index < PlacedNodes.Num(); ++Index)
		{
			FPlacedGraphNode& Current = PlacedNodes[Index];
			if (!Current.GraphNode)
			{
				continue;
			}

			for (int32 PreviousIndex = 0; PreviousIndex < Index; ++PreviousIndex)
			{
				const FPlacedGraphNode& Previous = PlacedNodes[PreviousIndex];
				if (!Previous.GraphNode || !DoPlacedNodesOverlap(Previous, Current, NodePadding))
				{
					continue;
				}

				const int32 NewY = FMath::RoundToInt(static_cast<float>(Previous.GraphNode->NodePosY) + Previous.Size.Height + NodePadding);
				if (NewY > Current.GraphNode->NodePosY)
				{
					Current.GraphNode->Modify();
					Current.GraphNode->NodePosY = NewY;
					bMovedAny = true;
				}
			}
		}

		if (!bMovedAny)
		{
			break;
		}
	}
}
```

- [ ] **Step 7: 添加 `LayoutBehaviorTreeGraph` 并接到 `RebuildEditorGraph`**

```cpp
bool LayoutBehaviorTreeGraph(UBehaviorTree* BT, UBehaviorTreeGraph* Graph)
{
	if (!BT || !BT->RootNode || !Graph)
	{
		return false;
	}

	TMap<UBTNode*, UBehaviorTreeGraphNode*> GraphNodesByInstance;
	BuildGraphNodeMap(Graph, GraphNodesByInstance);

	FLayoutTreeNode LayoutRoot;
	if (!BuildLayoutTree(BT->RootNode, GraphNodesByInstance, LayoutRoot))
	{
		return false;
	}

	ComputeSubtreeWidth(LayoutRoot);

	TArray<FPlacedGraphNode> PlacedNodes;
	if (UBehaviorTreeGraphNode_Root* RootGraphNode = FindRootGraphNode(Graph))
	{
		const FEstimatedGraphNodeSize RootSize = EstimateRootGraphNodeSize();
		RootGraphNode->Modify();
		RootGraphNode->NodePosX = FMath::RoundToInt(-RootSize.Width * 0.5f);
		RootGraphNode->NodePosY = RootY;

		FPlacedGraphNode RootPlacement;
		RootPlacement.GraphNode = RootGraphNode;
		RootPlacement.Size = RootSize;
		PlacedNodes.Add(RootPlacement);
	}

	PositionLayoutTree(LayoutRoot, 0.0f, FirstTreeNodeY, PlacedNodes);
	ResolveOverlaps(PlacedNodes);
	return true;
}
```

In `RebuildEditorGraph`, before `UpdateClassData()`:

```cpp
LayoutBehaviorTreeGraph(BT, Graph);
Graph->UpdateClassData();
Graph->UpdateAsset(UBehaviorTreeGraph::ClearDebuggerFlags | UBehaviorTreeGraph::KeepRebuildCounter);
```

- [ ] **Step 8: Compile**

Run project UBT:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected: compile succeeds after editor is closed or DLL lock is cleared.

---

### Task 5: MCP Create / Update 绿灯验证

**Files:**
- Use: `docs/superpowers/verification/bt_graph_layout_check.py`
- No production code changes unless verification fails.

- [ ] **Step 1: Start editor and wait for MCP**

Run:

```powershell
Start-Process -FilePath "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" -ArgumentList "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject"
```

Then run MCP `health_check`.

Expected:

```json
{
  "status": "ok",
  "service": "AssetFactory",
  "port": 8559,
  "subsystemAvailable": true
}
```

- [ ] **Step 2: Run Create verification**

Run the Task 1 `generate_assets` fixture and then `execute_python` with `bt_graph_layout_check.py`.

Expected:

```json
{
  "assetLoaded": true,
  "hasGraph": true,
  "errors": [],
  "verified": true
}
```

The diagnostics JSON also includes graph node names, classes, runtime instances, `NodePosX/Y`, and decorator/service subnode attachment data.

- [ ] **Step 3: Run Update determinism verification**

Run the same `generate_assets` fixture twice with `"Action": "Update"`.

Then run `bt_graph_layout_check.py` after each update and compare the stable coordinate subset from the two diagnostics outputs:

```python
import json
import unreal

subsystem = unreal.get_editor_subsystem(unreal.AssetFactorySubsystem)
diagnostics = json.loads(subsystem.get_behavior_tree_graph_layout_diagnostics(
    "/Game/UECopilotTests/BTLayout/BT_LayoutRed.BT_LayoutRed"))
if diagnostics.get("errors"):
    raise Exception(json.dumps(diagnostics, ensure_ascii=False))

positions = sorted(
    {
        "class": node["class"],
        "x": node["x"],
        "y": node["y"],
    }
    for node in diagnostics["nodes"]
], key=lambda item: (item["class"], item["x"], item["y"]))

print(json.dumps({"positions": positions}, ensure_ascii=False))
```

Expected: the two outputs are identical.

- [ ] **Step 4: Checkpoint**

Run:

```powershell
git status --short
```

Expected: production files plus plan/spec/verification files modified; no unrelated files.

---

### Task 6: Subagent review gates

**Files:**
- Review only.

- [ ] **Step 1: Dispatch spec compliance reviewer**

Prompt:

```text
只读审查，不要修改文件。工作区：E:\GameDev\worktrees\UECopilot\bt-graph-layout。
请根据 docs/superpowers/specs/2026-04-24-behaviortree-graph-layout-design.md 和 docs/superpowers/plans/2026-04-24-behaviortree-graph-layout.md 审查当前改动。
重点检查：Create/Update 是否都有 BTGraph；布局是否结构驱动；是否硬编码具体 BT 节点类；Decorator/Service 是否作为 subnode；MCP 验证是否覆盖 graph 存在、坐标、层级、顺序。
输出中文 findings，按严重程度排序，包含文件路径和行号。
```

Expected: no P0/P1 spec gaps before moving on.

- [ ] **Step 2: Dispatch code quality reviewer**

Prompt:

```text
只读审查，不要修改文件。工作区：E:\GameDev\worktrees\UECopilot\bt-graph-layout。
请审查 Source/AssetFactory/Private/Generators/BehaviorTreeGenerator.cpp 和 Source/AssetFactory/AssetFactory.Build.cs 的当前改动。
重点检查：UE editor module include/link 风险、UpdateAsset 是否可能反向破坏 runtime BT、graph outer/dirty/save 生命周期、无头布局是否依赖 Slate、算法是否过大或可读性差、是否有未使用 helper、是否存在 DLL/link 风险。
输出中文 findings，按严重程度排序，包含文件路径和行号。
```

Expected: no unresolved correctness or maintainability findings.

- [ ] **Step 3: 修复 reviewer findings**

For each accepted finding:

1. Write or update failing MCP/compile check that reproduces it.
2. Apply the smallest implementation fix.
3. Re-run UBT and MCP verification.

Expected: reviewer findings resolved with evidence.

---

### Task 7: Final verification before completion

**Files:**
- No planned code changes.

- [x] **Step 1: Run UBT**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" BTLayoutHostEditor Win64 Development "-Project=E:/GameDev/worktrees/UECopilot/bt-layout-host/BTLayoutHost.uproject" -NoHotReload
```

Expected: `Result: Succeeded`.

- [x] **Step 2: Run MCP health**

Run MCP `health_check`.

Expected:

```json
{
  "status": "ok",
  "service": "AssetFactory",
  "port": 8559,
  "subsystemAvailable": true
}
```

- [x] **Step 3: Run MCP BehaviorTree layout check**

Run `generate_assets` CreateOrUpdate fixture and `execute_python` verification from `docs/superpowers/verification/bt_graph_layout_check.py`.

Expected: graph exists and layout assertions pass.

- [x] **Step 4: Final git status**

Run:

```powershell
git status --short
```

Expected changed files:

```text
 M Source/AssetFactory/AssetFactory.Build.cs
 M Source/AssetFactory/Private/AssetFactorySubsystem.cpp
 M Source/AssetFactory/Private/Generators/BehaviorTreeGenerator.cpp
 M Source/AssetFactory/Public/AssetFactorySubsystem.h
 M Source/AssetFactory/Public/Generators/BehaviorTreeGenerator.h
?? docs/superpowers/plans/2026-04-24-behaviortree-graph-layout.md
?? docs/superpowers/specs/2026-04-24-behaviortree-graph-layout-design.md
?? docs/superpowers/verification/
```

No commit unless user explicitly asks.

Final verification results:
- UBT `BTLayoutHostEditor Win64 Development`: `Result: Succeeded`.
- MCP `health_check`: `status=ok`, `service=AssetFactory`, `port=8559`, `subsystemAvailable=true`.
- MCP layout checks: `BT_LayoutRed` diagnostics `verified=true`; `BT_LayoutComplex` diagnostics `verified=true`, `primaryBoundsWidth=2728`, `rootChildRowCount=3`, `maxRootChildCenterGap=1062.0`, `centerBranchOffset=182.0`.
- `python -m py_compile docs\superpowers\verification\bt_graph_layout_check.py docs\superpowers\verification\bt_graph_layout_compactness_check.py`: exit 0.
- `git diff --check`: exit 0.

---

## Self-Review

- Spec coverage:
  - Create graph: Task 3 and Task 5.
  - Update graph: Task 3, Task 5 update determinism.
  - Root / Composite / Task / Decorator / Service layout: Task 4 and Task 5.
  - No concrete BT class hardcoding: Task 4 uses `UBTCompositeNode`, `UBTTaskNode` role and graph classes; reviewer checks this in Task 6.
  - MCP validation: Task 1, Task 5, Task 7.
  - UBT verification: Task 4, Task 7.
- Placeholder scan:
  - No `TBD` / `TODO` / “fill in later” instructions.
  - Commands and expected outcomes are listed.
- Type consistency:
  - Helper names match across tasks: `RebuildEditorGraph`, `BuildGraphNodeMap`, `BuildLayoutTree`, `ComputeSubtreeWidth`, `PositionLayoutTree`, `ResolveOverlaps`, `LayoutBehaviorTreeGraph`.
  - UE property names match source usage: `BTGraph`, `LastEditedDocuments`, `NodeInstance`, `Decorators`, `Services`, `NodePosX`, `NodePosY`.
