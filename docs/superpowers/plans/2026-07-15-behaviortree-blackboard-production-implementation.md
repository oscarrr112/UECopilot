# BehaviorTree / BlackboardData AssetDocument Production Implementation Plan

> **执行要求：** 每项行为改动使用 TDD；每个 checkpoint 只在相关验证通过后提交；最终必须重新跑本 Goal 的全部 scoped gates。

**Goal:** 在 UE 5.7 基线完成无 authored gap 的 BehaviorTree/BlackboardData AssetDocument，并提供独立工程、真实保存重载和 8562 HTTP 证据。

**Architecture:** Blackboard 以 ordered local keys 为 authored truth；BehaviorTree 以 editor graph、stable NodeGuid、NodeInstance 和 deterministic semantic layout 为 authored truth；runtime tree/selector caches 由 UE 标准流程重建。所有真实写入都经过 staging preflight 和 existing-asset snapshot rollback。

## 固定环境

- Source base：`codex/asset-document-structured-capabilities@da7b1c8`
- Worktree：`/Users/pengao/Documents/AssetFactory/UECopilot/.worktrees/finish-bt-bb-assetdocument`
- Branch：`codex/finish-bt-bb-assetdocument`
- UE：`/Volumes/External/Unreal/Engines/UnrealEngine`
- Validation project：`/Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB`
- HTTP port：`8562`
- Candidate only：`origin/feature/asset-document-behaviortree-blackboard-impl`

## 交付合入

- Baseline branch：`codex/asset-document-structured-capabilities`
- Baseline worktree：`/Volumes/External/Unreal/Projects/UECopilotWorktrees/dev-environment`
- Feature branch 以 ff-only 方式在 `989701aa42e537216aebf8e88265c956ce67a753` 合入 baseline；该合入点的 feature/baseline tree 完全相同。
- BTBB validation project 的插件链接已从验证 worktree 切到 baseline worktree。

## 验收边界

本 Goal 的完成门槛只包括：

- BehaviorTree
- BlackboardData
- Persistence
- AtomicFile
- RegionCanonicalizer
- ManagedProperties
- 默认 Unity Build
- MCP/Python smoke
- 8562 实机保存、重启、fresh reload 验证

完整 `AssetFactory.AssetDocument` 聚合套件不是本 Goal 的门槛。AnimBlueprint/ABP 当前是另一分支处理的已知未完成功能；`AnimBlueprint.StateMachines`、AnimGraph snapshot、UObject refcount 相关失败只记录为无关已知状态，不阻塞 BT/Blackboard。

## Task 1：规格、清单与候选历史审计

- [x] 创建独立 worktree/branch 和独立验证项目；确认 8562 未占用。
- [x] 阅读 UE 5.7 BT/BB 源码并完成完整 surface inventory。
- [x] 写 production design 和 zero-authored-deferred 结论。
- [x] 逐提交分类旧分支 42 个提交并写 legacy audit。
- [x] 对规格运行 placeholder/stale-path/requirement-map 自审并提交 checkpoint。

## Task 2：候选代码基线与默认 Unity Build

- [x] 三方 merge 候选分支；保留当前公共 runtime/profile 架构和基线依赖。
- [x] 使用默认 Unity Build 暴露 translation-unit helper collision；未使用 `bUseUnity=false` 或 `-DisableUnity` 规避。
- [x] 将冲突 helper 改为文件/领域前缀命名，例如 `K2GraphJoinPath`、`K2GraphMakePositionObject`，消除 Unity TU 名称碰撞。
- [x] 运行 BT/BB focused tests并记录真实测试数。
- [x] MCP `npm test` 保持通过。

## Task 3：BlackboardData 完整 authored surface

- [x] ordered keys reorder/ID、duplicate、empty、parent shadow/cycle、atomic late failure。
- [x] Description、Category、`bInstanceSynced` save-reload。
- [x] Bool/Int/Float/Name/String/Vector/Rotator/Object/Class/Enum defaults 和 metadata。
- [x] Struct `FInstancedStruct` 与 custom KeyType reflected properties。
- [x] deprecated NativeEnum/unknown/null/invalid default rejection。
- [x] 统一 key schema/parser/materializer/extractor/canonical diff，保持 local key order。
- [x] parent/reference validation、staging 与 existing-asset rollback。
- [x] focused BlackboardData automation fresh pass：`33/33`。

## Task 4：BehaviorTree graph-as-source core

- [x] NodeGuid identity 与 `NodeName` 独立；new/update/reload GUID 稳定。
- [x] native/Blueprint/Angelscript/project concrete node 动态加载与错误类拒绝。
- [x] base/concrete NodeInstance editable properties 全往返。
- [x] 单根、无环、可达、合法 parent/child/pin 连接。
- [x] Children semantic order、X/Y rebuild order、layout/order conflict 与 duplicate coordinates。
- [x] materializer 创建/更新 editor graph wrapper/NodeInstance，再调用标准 `UpdateAsset()`。
- [x] extract/diff 从 graph authored source 读取，不从 runtime tree 反推身份。
- [x] standard rebuild/runtime mirror consistency 覆盖通过。

## Task 5：decorator、service、layout 与 comments

- [x] root/child decorators、services 的顺序、properties、GUID、save-reload。
- [x] composite decorator BoundGraph 的 Sink/Test/And/Or/Not、GUID、连线、properties。
- [x] decorator graph cycle/arity/dangling/multiple-link rejection。
- [x] ordinary node comments/bubbles 和 Comment Box 持久字段。
- [x] 主图及 nested decorator graph editor state 完整 apply/extract/diff。
- [x] runtime DecoratorOps 仅由 UE rebuild 产生并验证一致。
- [x] focused BehaviorTree automation fresh pass：`69/69`。

## Task 6：selector、subtree 和跨资产合同

- [x] selector 只写 Key；AllowedTypes/ID/type/None policy authored 输入拒绝。
- [x] selector missing/type mismatch/None policy 与 UE 5.7 `PreSave` 行为覆盖。
- [x] static/dynamic subtree 对 same、parent/child、sibling/equivalent Blackboard 的兼容矩阵。
- [x] dynamic subtree root decorator rejection 与 injected preview omission。
- [x] class-derived filter metadata inspection 与 name-based resolution。
- [x] conservative subtree compatibility validation。
- [x] BT/BB cross-asset focused coverage 全绿。

## Task 7：全局 atomic、canonical 与公共入口

- [x] new asset 在 property/body/rebuild/save/verify 各阶段失败不留 package/sidecar。
- [x] existing asset 在相同失败阶段恢复 object graph、subobjects、properties、refs、layout 和磁盘包。
- [x] extract→apply→extract、repeated apply、preview/diff/canonical order。
- [x] template/schema/validate/apply/inspect/extract/diff/sidecar/writeback 的 exact-class 与 diagnostics。
- [x] staging transaction、snapshot/rollback、save→fresh reload verification。
- [x] MCP schema/client surface 和 portable 8562 smoke harness。
- [x] scoped dependency gates fresh pass：
  - Persistence `18/18`
  - AtomicFile `11/11`
  - RegionCanonicalizer `28/28`
  - ManagedProperties `2/2`
  - MCP `39/39`
  - Python harness `8/8`

## Task 8：最终验证、文档和交付

- [x] 清空旧 editor/service，确认 8562 空闲后启动独立 validation Editor。
- [x] 默认 Unity Build fresh pass；未使用 `-DisableUnity`。
- [x] BehaviorTree focused automation fresh pass：`69/69`。
- [x] BlackboardData focused automation fresh pass：`33/33`。
- [x] Persistence/AtomicFile/RegionCanonicalizer/ManagedProperties fresh pass：`18/18`、`11/11`、`28/28`、`2/2`；combined scoped total `161/161`。
- [x] MCP `npm test` fresh pass：`39/39`。
- [x] Python live-smoke harness unit fresh pass：`8/8`。
- [x] 8562 real HTTP + MCP smoke：create/update/template/schema/validate/inspect/extract/diff/save-reload 全 pass。
- [x] 停止 pre-restart PID `90644`，以新 PID `97092` 重启并通过 fresh extract/diff；最后停止 Editor 并释放 8562。
- [x] 更新 plan、legacy audit、benchmark/report，移除旧聚合门槛和 stale 状态。
- [x] 完成独立最终 code review；修复 schema traversal，并补齐 unresolved、non-BT、CDO extraction failure 与 semantic-order 覆盖。
- [x] 运行 `git diff --check`、review range 和 clean-worktree 检查；最终 reviewer 结论为 Critical `0`、Important `0`、Ready to merge。
- [x] 提交全部 implementation/scoped 变更；implementation checkpoint 为 `aa3637ba3ec795c7919f891153891bf163d18de9`。
- [x] documentation closure 提交后再次确认 worktree clean；最终 HEAD 在交付汇报中记录。
- [x] 将 feature 以 ff-only 方式合入 `codex/asset-document-structured-capabilities`，未创建 merge commit、未改写历史。
- [x] 合入后在 baseline fresh 运行 MCP `39/39`、Python harness `8/8`，并确认 `git diff --check` 通过。
- [x] 核对 scoped UE/8562 证据对应 implementation checkpoint；其后的提交只修改交付文档，因此 runtime tree 与已验证实现一致。
- [x] 将 validation project 插件链接切到 baseline，并完成 baseline clean-worktree 审查。

## 证据索引

- Automation reports：`/Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/Saved/Automation`
- Combined scoped UE report：`/Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/Saved/Automation/BTBB_SCOPED_FINAL_AFTER_REVIEW_R2/index.json`
- 8562 evidence：`/Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/Saved/AssetFactory/Verification/BTBB-final-review-20260716-1913/summary.json`
- Pre-restart log：`/Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/Saved/Logs/BTBB-final-review-live-pre2-20260716_2.log`
- Post-restart log：`/Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/Saved/Logs/BTBB-final-review-live-post-20260716.log`

## 完成禁令

任一 BT/Blackboard authored surface 缺口、默认 Unity Build 失败、任一 scoped automation/MCP/Python/8562 smoke 失败、未验证 save-reload、文档与 HEAD 不一致、worktree 非 clean 或存在未提交变更时，禁止标记 goal complete。

ABP 已知失败和完整 `AssetFactory.AssetDocument` 聚合套件不属于上述禁令。
