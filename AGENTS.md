# UECopilot Project Instructions

## Development Baseline

- 后续所有功能、修复、实现计划和 Git worktree，统一以 `origin/feature/asset-document-structured-capabilities-spec` 为权威 baseline。
- baseline 是上述远程分支的最新已获取 tip，不是 `master`，也不是旧的 `codex/dev-environment`。
- 本规则写入时的 baseline 审计快照为 `75dbdbe61c0ceb6619125a486b8e449e99378279`（`docs(assetdoc): align transition graph obsolete diagnostics`）。该 SHA 用于追溯；长期权威仍是分支本身。
- 当前外置盘开发 worktree 使用 `codex/asset-document-structured-capabilities`，其起点必须包含上述 baseline。
- 创建新的开发分支或 worktree 前，先执行：

  ```bash
  git fetch origin feature/asset-document-structured-capabilities-spec
  BRANCH=codex/replace-with-task-name
  git switch -c "$BRANCH" origin/feature/asset-document-structured-capabilities-spec
  ```

- 开始开发前，使用下面的命令确认 baseline 是当前分支的祖先：

  ```bash
  git merge-base --is-ancestor origin/feature/asset-document-structured-capabilities-spec HEAD
  ```

- 如果远程 baseline 在现有任务进行期间前进，不要静默 reset 或 rebase；先核对差异，再显式合并或变基。
- 除非用户明确更新本文件中的决定，不得默认切回 `master` 或从其他旧分支开始后续开发。

## BehaviorTree / BlackboardData AssetDocument Invariants

- `UBehaviorTree` 的 authored source-of-truth 是 `BTGraph`、持久 `NodeGuid`、graph topology、graph-node `NodeInstance` 和 editor-authored state；`RootNode`、Children、Services、DecoratorOps 等 runtime tree 只由 UE 标准 graph rebuild 产生，不能成为第二份可写真源。
- Behavior Tree 同一 parent 下的子节点会按 `NodePosX/NodePosY` 决定执行顺序；AssetDocument 的 semantic child order 与 editor layout 必须一致，不能把位置当成无语义装饰。
- BT node identity 使用持久 `NodeGuid`，不得使用 `NodeName`、数组下标、UObject name 或地址。`NodeName` 是独立 authored property。
- `UBlackboardData` 只管理 ordered local `Keys`；本地顺序会影响 Key ID，canonicalization 不得排序。拒绝本地 duplicate、父链 shadow 和 parent cycle。
- `FBlackboardKeySelector` 只把 `SelectedKeyName` 作为 authored selection；AllowedTypes、SelectedKeyType、SelectedKeyID 和 None policy 是 concrete node class policy/cache，只读暴露并用于验证，不接受外部写入。
- `FValueOrBlackboardKey_*` 只把公开的 key name 与 literal/default 值视为 authored；内部 cached Key ID 是 derived runtime cache。Blackboard 引用或 key 顺序变化后必须失效并按 name 重解，不能提取、diff 或写回缓存值。
- 仅提供 `Body.BlackboardAsset` 的 sparse `Update` 是对现有 BT authored tree 的 overlay，不是 create/lifecycle 入口；必须用目标 Blackboard 重新验证并刷新所有 retained selector/value-or-key。目标不存在或 `Create` 缺少有效完整语义时不得借 sparse 路径合成资产。
- sparse Blackboard 替换若改变 retained `UBTDecorator_Blackboard` enum key 的 enum/value mapping，必须要求同一请求显式重写对应 Tree property；不得静默保留或猜测 stale `StringValue`/`IntValue`。
- BT/BB 所有合法、可加载、非 abstract、位置兼容的 native、Blueprint、Angelscript/project class 及安全实例可编辑属性必须走动态 class loading 和公共 reflected property runtime；不得用 AIModule 白名单、`_Skipped`、empty-only、validate-only 或 authored deferred 缩减范围。
- BT/BB apply 必须 staging preflight、existing-asset snapshot rollback，并在成功返回前完成 save → fresh reload → canonical extract/diff verification。
