# §8.3 Subtree BB 兼容性调研

**日期**：2026-04-23

## 问题 1：IsRelatedTo 实现

`BlackboardData.h:159-167`：
```cpp
/** returns true if OtherAsset is somewhere up the parent chain of this asset. Node that it will return false if *this == OtherAsset */
AIMODULE_API bool IsChildOf(const UBlackboardData& OtherAsset) const;

/** returns true if OtherAsset is equal to *this, or is it's parent, or *this is OtherAsset's parent */
bool IsRelatedTo(const UBlackboardData& OtherAsset) const
{
    return this == &OtherAsset || IsChildOf(OtherAsset) || OtherAsset.IsChildOf(*this)
        || (Parent && OtherAsset.Parent && Parent->IsRelatedTo(*OtherAsset.Parent));
}
```

`BlackboardData.cpp:308-319`：
```cpp
bool UBlackboardData::IsChildOf(const UBlackboardData& OtherAsset) const
{
    const UBlackboardData* TmpParent = Parent;
    while (TmpParent != nullptr && TmpParent != &OtherAsset) { TmpParent = TmpParent->Parent; }
    return (TmpParent == &OtherAsset);
}
```

**结论**：`IsRelatedTo` 对称（同身/父链双向/共同祖先均 true），`IsChildOf` 单向沿 Parent 链全深度上溯，签名 `const`、参数为引用。

## 问题 2：RunBehavior 自带校验

`BTTask_RunBehavior.cpp:43-55` 仅在 `GetStaticDescription` 里用 `IsChildOf` 做提示，**无 `OnInstanceCreated`/`InitializeFromAsset` 运行时拦截**：
```cpp
bIsBBCompatible = BlackboardData == OtherBlackboardData || BlackboardData->IsChildOf(*OtherBlackboardData);
return FString::Printf(TEXT("%s: %s%s"), ..., bIsBBCompatible ? TEXT("") : TEXT(" (Blackboard not compatible)"));
```

**结论**：仅在 Node 描述字符串里追加 "(Blackboard not compatible)"，不阻止节点运行，由 PushInstance 兜底。

## 问题 3：编辑器 Property 校验

`BTTask_RunBehavior.h:43-45`：
```cpp
UPROPERTY(Category = Node, EditAnywhere)
TObjectPtr<UBehaviorTree> BehaviorAsset;
```

**结论**：UPROPERTY 无 `AllowedClasses`/`OnGetAssetTags` meta；BehaviorTreeEditor 内无对应 Customization（Grep 仅 Tab/Module/Mode 引用，均非 detail customization）。编辑器不按 BB 过滤候选资产，允许用户拖任意 BT。

## 问题 4：PushInstance 不兼容行为

`BehaviorTreeComponent.cpp:2707-2716`：
```cpp
bool UBehaviorTreeComponent::PushInstance(UBehaviorTree& TreeAsset)
{
    if (TreeAsset.BlackboardAsset && BlackboardComp && !BlackboardComp->IsCompatibleWith(TreeAsset.BlackboardAsset))
    {
        UE_VLOG(GetOwner(), LogBehaviorTree, Warning, TEXT("Failed to execute tree %s: blackboard %s is not compatibile with current: %s!"), ...);
        return false;
    }
```

`BlackboardComponent.cpp:490-506` —— `IsCompatibleWith` 沿 `BlackboardAsset->Parent` 链上溯，命中 `It == TestAsset` 或 `It->Keys == TestAsset->Keys`（数组指针相等）返回 true，否则 false。

**结论**：运行时 VLog Warning + `return false`，`ExecuteTask` 遂返回 `Failed`，不 crash。兼容语义 = 子 BB 是父 BB 的祖先，或两者共享同一 Keys 数组；**非对称**，与 `IsRelatedTo` 语义不同。

## 兼容判定逻辑伪代码

```python
def is_subtree_compatible(parent_bb, child_bb):
    # UE 运行时语义（BlackboardComponent::IsCompatibleWith）
    # parent_bb = 外层 BTComponent 当前持有的 BB
    # child_bb  = 子 BT 的 BlackboardAsset
    if child_bb is None: return True           # PushInstance 首条判定
    it = parent_bb
    while it is not None:
        if it is child_bb: return True         # child 是 parent 或 parent 祖先
        if it.Keys is child_bb.Keys: return True  # 共享 keys 数组
        it = it.Parent
    return False
```

## 推荐校验函数签名

```cpp
// FBehaviorTreeGenerator::ValidateConfig 中调用
TOptional<FString> ValidateSubtreeCompat(
    const UBlackboardData* ParentBB,
    const UBehaviorTree*   ChildTree);
// 返回 TOptional<FString>: 空=兼容；有值=错误消息
```

## 候选方案

- **方案 A**：复用 `UBlackboardComponent::IsCompatibleWith` 语义（沿 `Parent` 链 + `Keys` 相等）。与运行时完全一致，推荐。
- **方案 B**：自行按 key name+type 逐项匹配。严格但与引擎不一致，可能误报。
- **方案 C**：不拦截，交给运行时 VLog Warning。与 Spec §6.5 "generate 前拦截"相悖。

## Decision

**采纳方案 A**：在 ValidateConfig 中复用 `UBlackboardComponent::IsCompatibleWith` 的非对称语义（沿 parent_bb 的 `Parent` 链上溯，命中 `It == child_bb` 或 `&It->Keys == &child_bb->Keys` 即兼容）。

**理由**：
- 与 UE 运行时 `PushInstance`（`BehaviorTreeComponent.cpp:2707-2716`）判定 100% 一致，避免 Validate 过严或过宽。
- `IsRelatedTo` 对称语义不适用：例如 parent_bb 是 child_bb 的祖先时 `IsRelatedTo` 返 true，但运行时 `IsCompatibleWith` 要求 child 是 parent 或 parent 的祖先（反向不行），会导致 Validate 放过但运行时失败。
- 方案 B 逐 key 比对与引擎语义不一致，易误报；方案 C 不拦截违背 Spec §6.5 "generate 前拦截"。

**实现位置**：归入 Plan Task 3-4（并入 BB key 强校验），不新增 Task。Task 3-4 执行时按以下伪代码扩展 `ValidateBBKeyReferences`，发现 `UBTTask_RunBehavior.BehaviorAsset` 时调用：

```cpp
// ParentBB = 当前 BT 解析后的 BlackboardAsset（含 Blackboard/BlackboardInline 两种来源）
// ChildBT  = 从 BehaviorAsset 字段 LoadObject 得到的子 BT
bool IsCompatible(const UBlackboardData* ParentBB, const UBlackboardData* ChildBB)
{
    if (!ChildBB) return true;  // 子 BT 无 BB 则始终兼容（与 PushInstance 首条判定一致）
    for (const UBlackboardData* It = ParentBB; It; It = It->Parent)
    {
        if (It == ChildBB) return true;
        if (&It->Keys == &ChildBB->Keys) return true;  // Keys 数组指针相等
    }
    return false;
}
```

**术语修正**：Plan/Spec 中 `IsRelatedTo` 做 subtree 兼容判定的描述按本 research 为准，不回改 plan/spec 文档。

