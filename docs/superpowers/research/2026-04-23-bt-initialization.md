# §8.1 BT 初始化与 PostEditChange 调研

**日期**：2026-04-23

## 问题 1：FBehaviorTreeEditor 初始化路径

`UAssetDefinition_BehaviorTree::OpenAssets` 最终调用 `FBehaviorTreeEditorModule::CreateBehaviorTreeEditor`，再到 `FBehaviorTreeEditor::InitBehaviorTreeEditor`。真正的图表初始化发生在模式激活时：`FBehaviorTreeEditorApplicationMode::PostActivateMode` 调用 `RestoreBehaviorTree`。

```cpp
// BehaviorTreeEditor.cpp:342-394 (FBehaviorTreeEditor::RestoreBehaviorTree)
UBehaviorTreeGraph* MyGraph = Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph);
const bool bNewGraph = MyGraph == NULL;
if (MyGraph == NULL)
{
    const TSubclassOf<UEdGraphSchema> SchemaClass = GetDefault<UBehaviorTreeGraph>(GraphClass)->Schema;
    BehaviorTree->BTGraph = FBlueprintEditorUtils::CreateNewGraph(BehaviorTree, GraphName, GraphClass, SchemaClass);
    MyGraph = Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph);
    const UEdGraphSchema* Schema = MyGraph->GetSchema();
    Schema->CreateDefaultNodesForGraph(*MyGraph);   // 创建 Root 图节点
    MyGraph->OnCreated();                            // 调用 SpawnMissingNodes 从 BT->RootNode 反向生成图节点
}
else { MyGraph->OnLoaded(); }
MyGraph->Initialize();
// 随后调用 MyGraph->UpdateAsset(...)
```

**结论**：`InitBehaviorTreeEditor` 本身不构建 Graph；Graph 在 `PostActivateMode→RestoreBehaviorTree` 被惰性创建。若 `BT->BTGraph == nullptr`，编辑器会自行 `CreateNewGraph + CreateDefaultNodesForGraph + OnCreated`。

## 问题 2：PostEditChange 副作用

`E:/Epic Games/UE_5.7/Engine/Source/Runtime/AIModule/Private/BehaviorTree/BehaviorTree.cpp:1-17`：`UBehaviorTree` 仅实现空构造函数与 `GetBlackboardAsset`，**未重载** `PostEditChange`、`PostLoad`、`PostInitProperties`。搜索 `BehaviorTreeGraph.cpp` 也无 `PostLoad`/`PostEditChange`。

**结论**：对 `UBehaviorTree` 调 `PostEditChange()` 无任何下游 effect——不会创建 Graph、不会触发 UpdateAsset。仅 `MarkPackageDirty` 的副作用。

## 问题 3：UpdateAsset 调用时机

`UBehaviorTreeGraph::UpdateAsset` (`BehaviorTreeGraph.cpp:102-187`) 会调 `CreateBTFromGraph(RootEdNode)`，后者 `BehaviorTreeGraph.cpp:902-909` 直接清空并重写 `BTAsset->RootNode`、`RootDecorators`、`RootDecoratorOps`。调用点：
- `RestoreBehaviorTree` 末尾每次调用 (`BehaviorTreeEditor.cpp:379 / 384`)
- `SaveAsset_Execute→BTGraph->OnSave→UpdateAsset` (`BehaviorTreeEditor.cpp:1391-1403`、`BehaviorTreeGraph.cpp:208-212`)
- `OnPackageSaved` 当存在注入节点变化时 (`BehaviorTreeEditor.cpp:1185-1196`)

**结论**：编辑器内任意打开/保存都会用当前 Graph 覆盖 `RootNode`。若外部生成时 BTGraph 已存在但为空，打开后 RootNode 会被清空。

## 问题 4：BTGraph 字段

`BehaviorTree.h:23-33`：`TObjectPtr<UEdGraph> BTGraph` 位于 `WITH_EDITORONLY_DATA`。非必需——编辑器打开时若为 null，`RestoreBehaviorTree` 会自动构造，并在 `OnCreated→SpawnMissingNodes→BTGraphHelpers::SpawnMissingGraphNodes` (`BehaviorTreeGraph.cpp:887-898、813-884`) 里从 `Asset->RootNode` 递归生成 Composite/Task/Decorator/Service 图节点。

**结论**：BTGraph 可以缺省；关键是 `RootNode` 必须是完整的 `UBTCompositeNode` 树。仅 `NewObject + 设置 RootNode + SavePackage` 的资产，首次打开就能正确显示图表。

## 问题 5：LoadTree 副作用

`BehaviorTreeManager.cpp:263-317`：只读 `Asset.RootNode`；通过 `StaticDuplicateObject` 复制到 `FBehaviorTreeTemplateInfo.Template`，缓存在 `LoadedTemplates`；在 Template 上跑 `InitializeNodeHelper / InitializeNode` 计算 `ExecutionIndex` 与内存偏移。不修改 Asset 自身，**纯运行时**逻辑，由 `UBehaviorTreeComponent::StartTree` 调用。

**结论**：LoadTree 属于运行时，创建资产阶段不应调用，也无必要调用。

## 候选方案

- **方案 A**：仅 `PostEditChange()` + `MarkPackageDirty` + `SavePackage`（最轻量）
- **方案 B**：A + 显式构造 `UBehaviorTreeGraph` 并设为 `BT->BTGraph`（编辑器 UI 友好）
- **方案 C**：A + `UBehaviorTreeManager::LoadTree` 触发 runtime init（可能有副作用）

## Decision

**采纳方案 A**：FinalizeBT 实现为 `BT->PostEditChange() + BT->MarkPackageDirty()`，不显式构造 BTGraph。

**理由**：
- 证据显示 BTGraph 可缺省：`RestoreBehaviorTree`（`BehaviorTreeEditor.cpp:342-394`）在首次打开时自动 `CreateNewGraph + CreateDefaultNodesForGraph + OnCreated()`，`SpawnMissingGraphNodes`（`BehaviorTreeGraph.cpp:813-884`）从 `Asset->RootNode` 反向生成完整图节点树。
- 方案 B 若仅 `NewObject<UBehaviorTreeGraph>` 而不跑 SpawnMissingNodes，`RestoreBehaviorTree` 末尾的 `UpdateAsset`（`BehaviorTreeEditor.cpp:379`）会触发 `CreateBTFromGraph`（`BehaviorTreeGraph.cpp:902-909`）**清空 RootNode**——比不做更糟；要做对就得把 SpawnMissingNodes 抄一遍，复杂度不值。
- 方案 C 属于运行时（`BehaviorTreeComponent::StartTree` 路径），与资产创建正交。

**实现注记**：`UBehaviorTree::PostEditChange` 未重载，实际只触发 `UObject::PostEditChange` 默认路径 + 后续 `MarkPackageDirty`；保留 `PostEditChange` 调用作为防御（未来引擎若添加重载自动享受）。

