# BehaviorTree / BlackboardData AssetDocument Production Implementation Plan

> **执行要求：** 每项行为改动使用 TDD；每个 checkpoint 只在相关验证通过后提交；最终必须重新跑全部门禁。

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

## Task 1：规格、清单与候选历史审计

- [x] 创建独立 worktree/branch 和独立验证项目；确认 8562 未占用。
- [x] 阅读 UE 5.7 BT/BB 源码并完成完整 surface inventory。
- [x] 写 production design 和 zero-authored-deferred 结论。
- [x] 逐提交分类旧分支 42 个提交并写 legacy audit。
- [x] 对规格运行 placeholder/stale-path/requirement-map 自审并提交 checkpoint。

## Task 2：候选代码基线与默认 Unity Build

- [ ] 三方 merge 候选分支，`AssetDocument.Build.cs` 保留现基线依赖并加入 AI editor/runtime 依赖。
- [ ] 运行默认 Unity Build 取得 RED；不得用 `bUseUnity=false` 或 `-DisableUnity` 规避。
- [ ] 消除 unity translation unit 的 private-helper ODR/name collision。
- [ ] 分别运行旧 BT/BB focused tests，记录真实测试数和失败矩阵。
- [ ] MCP `npm test` 保持通过。

## Task 3：BlackboardData 完整 authored surface

- [ ] RED：ordered keys reorder/ID、duplicate、empty、parent shadow/cycle、atomic late failure。
- [ ] RED：Description、Category、bInstanceSynced save-reload。
- [ ] RED：Bool/Int/Float/Name/String/Vector/Rotator/Object/Class/Enum defaults 和 metadata。
- [ ] RED：Struct `FInstancedStruct` 与 custom KeyType reflected properties。
- [ ] RED：deprecated NativeEnum/unknown/null/invalid default rejection。
- [ ] GREEN：统一 key schema/parser/materializer/extractor/canonical diff，保持 local key order。
- [ ] GREEN：parent/reference validation、staging 与 existing-asset rollback。
- [ ] focused BlackboardData automation 全绿并提交 checkpoint。

## Task 4：BehaviorTree graph-as-source core

- [ ] RED：NodeGuid identity 与 `NodeName` 独立；new/update/reload GUID 稳定。
- [ ] RED：native/Blueprint/Angelscript/project concrete node 动态加载与错误类拒绝。
- [ ] RED：base/concrete NodeInstance editable properties 全往返。
- [ ] RED：单根、无环、可达、合法 parent/child/pin 连接。
- [ ] RED：Children semantic order、X/Y rebuild order、layout/order conflict 与 duplicate coordinates。
- [ ] GREEN：materializer 直接创建/更新 editor graph wrapper/NodeInstance，再调用标准 `UpdateAsset()`。
- [ ] GREEN：extract/diff 从 graph authored source 读取，不从 runtime tree 反推身份。
- [ ] standard rebuild/runtime mirror consistency 测试通过并提交 checkpoint。

## Task 5：decorator、service、layout 与 comments

- [ ] RED：root/child decorators、services 的顺序、properties、GUID、save-reload。
- [ ] RED：composite decorator BoundGraph 的 Sink/Test/And/Or/Not、GUID、连线、properties。
- [ ] RED：decorator graph cycle/arity/dangling/multiple-link rejection。
- [ ] RED：ordinary node comments/bubbles 和 Comment Box 全持久字段。
- [ ] GREEN：主图及 nested decorator graph editor state 完整 apply/extract/diff。
- [ ] GREEN：runtime DecoratorOps 仅由 UE rebuild 产生并验证一致。
- [ ] focused BehaviorTree automation 全绿并提交 checkpoint。

## Task 6：selector、subtree 和跨资产合同

- [ ] RED：selector 只写 Key；AllowedTypes/ID/type/None policy authored 输入拒绝。
- [ ] RED：selector missing/type mismatch/None policy，包含 UE 5.7 `PreSave` true/false 行为实验。
- [ ] RED：static/dynamic subtree 对 same、parent/child、sibling/equivalent Blackboard 的兼容矩阵。
- [ ] RED：dynamic subtree root decorator rejection 与 injected preview omission。
- [ ] GREEN：class-derived filter metadata inspection 与 name-based resolution。
- [ ] GREEN：conservative subtree compatibility validation。
- [ ] focused BT/BB cross-asset tests 全绿并提交 checkpoint。

## Task 7：全局 atomic、canonical 与公共入口

- [ ] RED：new asset 在 property/body/rebuild/save/verify 各阶段失败不留 package/sidecar。
- [ ] RED：existing asset 在相同失败阶段 object graph、subobjects、properties、refs、layout 和磁盘包完全恢复。
- [ ] RED：extract→apply→extract、repeated apply、preview/diff/canonical order。
- [ ] RED：template/schema/validate/apply/inspect/extract/diff/sidecar/writeback 的 exact-class 与 diagnostics。
- [ ] GREEN：staging transaction、snapshot/rollback、save→fresh reload verification。
- [ ] GREEN：MCP schema/client surface 和 portable 8562 smoke harness。
- [ ] 完整 `AssetFactory.AssetDocument` 与 MCP tests 通过并提交 checkpoint。

## Task 8：最终验证、文档和交付

- [ ] 清空旧 editor/service，确认 8562 空闲后启动独立 validation Editor。
- [ ] 默认 Unity Build fresh pass。
- [ ] BehaviorTree focused automation fresh pass，记录数量。
- [ ] BlackboardData focused automation fresh pass，记录数量。
- [ ] 完整 `AssetFactory.AssetDocument` fresh pass，记录数量。
- [ ] MCP `npm test` fresh pass，记录数量。
- [ ] 8562 real HTTP smoke：独立 BB/BT 路径 create/update/template/schema/validate/inspect/extract/diff/save-reload 全 pass。
- [ ] 更新 plan checkbox、legacy audit、benchmark/report，移除 stale path/blocked/占位符。
- [ ] 运行 `git diff --check`、review range 和 clean-worktree 检查。
- [ ] 所有变更提交，记录最终 SHA、测试证据和剩余非 authored 风险。

## 完成禁令

任一 authored surface 缺口、默认 Unity Build 失败、任一 automation/MCP/HTTP smoke 失败、未验证 save-reload、文档与 HEAD 不一致、worktree 非 clean 或存在未提交变更时，禁止标记 goal complete。
