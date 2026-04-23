# §8.2 NodeIndex 来源调研

**日期**：2026-04-23

**前置澄清**：UBTNode 并不存在 `TreeIndex` 字段。实际对应"编辑器 BT Graph 节点序号"的是 `UBTNode::ExecutionIndex`（`uint16`，`UPROPERTY(transient)`）。下文全部以该字段展开。

## 问题 1：ExecutionIndex 赋值点

UBTNode 声明（非序列化）：

```cpp
// BTNode.h:236-237
UPROPERTY(transient)
uint16 ExecutionIndex;
```

唯一写入入口是 `InitializeNode`：

```cpp
// BTNode.cpp:54-60
void UBTNode::InitializeNode(UBTCompositeNode* InParentNode, uint16 InExecutionIndex, uint16 InMemoryOffset, uint8 InTreeDepth)
{
    ParentNode = InParentNode; ExecutionIndex = InExecutionIndex;
    MemoryOffset = InMemoryOffset; TreeDepth = InTreeDepth;
}
```

InitializeNode 调用点（AIModule 侧）：`BehaviorTreeManager.cpp:303`（LoadTree）、`BTNode.cpp:99`（InitializeInSubtree 里对 duplicate 出来的 instanced 节点）。编辑器侧：`BehaviorTreeGraph.cpp:154`（UpdateAsset 先置 MAX_uint16）、`BehaviorTreeGraph.cpp:374`（decorator）、`BehaviorTreeGraph.cpp:919`（CreateBTFromGraph 对 RootNode）、以及 `BehaviorTreeGraph.cpp` 中 CreateChildren / CollectDecorators 的递归。

**结论**：`ExecutionIndex` 仅由 `InitializeNode` 写入；字段 transient，不随资产序列化。

## 问题 2：LoadTree 是否填 ExecutionIndex

```cpp
// BehaviorTreeManager.cpp:282-286
TemplateInfo.Template = Cast<UBTCompositeNode>(StaticDuplicateObject(Asset.RootNode, this));
...
InitializeNodeHelper(NULL, TemplateInfo.Template, 0, ExecutionIndex, InitList, Asset, this);
// BehaviorTreeManager.cpp:303
InitList[Index].Node->InitializeNode(InitList[Index].ParentNode, InitList[Index].ExecutionIndex, ...);
```

**结论**：LoadTree 先对 `Asset.RootNode` 做 `StaticDuplicateObject` 得到 `Template`，再对 Template 上的整棵树 DFS 赋 ExecutionIndex；**Asset 原节点本身不会被 LoadTree 修改**。

## 问题 3：编辑器图节点序号来源

```cpp
// SGraphNode_BehaviorTree.cpp:972-973
UBTNode* BTNode = Cast<UBTNode>(StateNode->NodeInstance);
Index = (BTNode && BTNode->GetExecutionIndex() < 0xffff) ? BTNode->GetExecutionIndex() : -1;
```

且 `ALWAYS_SHOW_BT_EXECUTION_INDEX 1`（`SGraphNode_BehaviorTree.cpp:37`）。`StateNode->NodeInstance` 就是 Asset 原节点（`BehaviorTreeGraph.cpp:853`）。Asset 原节点的 ExecutionIndex 是在编辑器打开该 BT 时由 `UBehaviorTreeGraph::OnLoaded → UpdateAsset → CreateBTFromGraph`（`BehaviorTreeGraph.cpp:919`、`:374`、以及 CreateChildren）按 DFS 写入的。

**结论**：图上数字直接是 Asset 原节点的 `ExecutionIndex`，由编辑器打开图时 DFS 赋值，不依赖运行时 LoadTree。

## 问题 4：InitializeComposite 调用关系

```cpp
// BTCompositeNode.cpp:30-33
void UBTCompositeNode::InitializeComposite(uint16 InLastExecutionIndex)
{ LastExecutionIndex = InLastExecutionIndex; }
```

调用者：`BehaviorTreeManager.cpp:259`（InitializeNodeHelper 结尾）、`BehaviorTreeTypes.cpp:47`（FBehaviorTreeInstance subtree 初始化）、`BehaviorTreeGraph.cpp:953`（CreateBTFromGraph 结尾）。`LastExecutionIndex` 无 UPROPERTY（`BTCompositeNode.h:218-219`），完全 transient。

**结论**：InitializeComposite 由 LoadTree 间接触发、也会被编辑器 UpdateAsset 触发；运行时和编辑器两路都调用。

## 问题 5：LoadObject 后的 ExecutionIndex 值

ExecutionIndex 为 `transient`，磁盘上无记录。外部 C++（无 Editor 模块）走 `LoadObject<UBehaviorTree>` 后：Asset 原节点的 ExecutionIndex 初始为 0（CDO 默认），既不会被 `OnLoaded`（只在 BT 编辑器打开时触发）覆盖，也不会被 LoadTree 覆盖（LoadTree 只写 Template 副本）。

**结论**：非 BT 编辑器环境下，`Asset.RootNode->...->ExecutionIndex` 全部为 0，无法用于 NodeIndex 定位。

## 候选方案

- **方案 A**：Extract 前调用 `UBehaviorTreeManager::LoadTree(Asset, Root, MemSize)`，再对返回的 `Root`（Template 副本）DFS 读 `ExecutionIndex`。副作用：LoadTree 会把模板缓存进 BTManager 的 `LoadedTemplates`，并对整棵树做 StaticDuplicateObject；需要一个可用的 World/BTManager。
- **方案 B**：在 Extract 路径自行模拟 `InitializeNodeHelper` 的 DFS（顺序：Composite → Services → 每个 Child 先 Decorators、再递归）。与 UE 算法完全一致无需 LoadTree，无副作用，只读。
- **方案 C**：放弃 NodeIndex 输出，spec §3.6 改为 future work。

## Decision

**采纳方案 B**：Extract 路径自行 DFS 编号，严格复刻 `InitializeNodeHelper` 的遍历顺序。

**理由**：
- `ExecutionIndex` 为 `transient`（`BTNode.h:236-237`），磁盘无记录；`LoadTree` 仅在 StaticDuplicateObject 出的 Template 副本上 DFS 赋值（`BehaviorTreeManager.cpp:282-303`），**Asset 原节点始终为 0** —— 方案 A 无法从 Asset 取到可用值。
- 方案 A 还需要可用的 World/BTManager 并会把模板缓存进 `LoadedTemplates`，副作用大；方案 C 放弃输出违背 Spec §6.3 错误消息需要节点定位的需求。
- 方案 B 纯只读、无依赖、与 UE 算法一致。

**实现算法**（Task 3-9 严格依此，与 `InitializeNodeHelper` 一致）：

```
DFS(node, idx):           # idx 按引用传入，自增
    node.index = idx++
    if node is Composite:
        for svc in node.Services: svc.index = idx++
        for child_slot in node.Children:
            for deco in child_slot.Decorators: deco.index = idx++
            DFS(child_slot.ChildNode, idx)
```

**术语修正**：Plan/Spec 中所有 `TreeIndex` 字段均指 `ExecutionIndex`。Task 3-9 执行时以本 research 为准，不回改 plan/spec 文档。
