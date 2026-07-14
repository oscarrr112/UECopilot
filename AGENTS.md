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
