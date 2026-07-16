# BehaviorTree / BlackboardData 旧分支逐提交审计

**审计日期**：2026-07-15
**执行结果更新**：2026-07-16
**候选分支**：`origin/feature/asset-document-behaviortree-blackboard-impl`
**候选 tip**：`d2e55e5bda3c30e633b3cd9847bc8967e1c36e2a`
**共同基点**：`12ea6ae87c37227cb73f65dd848b2a79c8ac2ee8`
**当前实现分支**：`codex/finish-bt-bb-assetdocument`

## 结论

候选分支从共同基点起共有 42 个提交。当前分支通过 merge commit `f18e523b632dd7df35e7ae68f9e74ff704173891` 引入候选 tip；该 merge 的父提交是 `77284ac8dd27ee26e0c5a3a42ed94d60b2db8ada` 和 `d2e55e5bda3c30e633b3cd9847bc8967e1c36e2a`。因此候选 tip 已是当前 HEAD 的祖先，历史审计链可追溯。

“已在 ancestry 中”不等于 42 个提交未经修改地获得 production 接受。语义处置仍为：19 个集成、10 个修改后集成、4 个重写、6 个丢弃、3 个冲突并替代。最终验收来自当前代码、当前合同和 fresh scoped evidence，不来自 merge 成功本身。

旧证据的缺口及当前处置：

- 旧 focused 报告只有 33 条 BehaviorTree 证据；当前以 BehaviorTree `69/69` 和 BlackboardData `33/33` fresh reports 替代。
- 旧 HTTP `127.0.0.1:8560` 绑定失败；当前 8562 pre/post restart HTTP + MCP smoke 全通过。
- 旧实现把 `FInstancedStruct` 视为 unsupported；当前由共享 reflected-property runtime 覆盖 Struct/custom KeyType。
- selector AllowedTypes/filter 不再作为可写 authored 数据；只写 canonical Key，filter/ID/type/None policy 保持 class-derived。
- identity 使用稳定 NodeGuid，不再用 `NodeName` 代替。
- BTGraph 是 authored source；runtime tree、selector caches 和 DecoratorOps 由 UE rebuild。
- KeyType metadata、Comment Box、项目自定义节点和 existing-asset rollback 已进入当前 focused coverage。
- 旧 plan 的 69 个未勾选项已被当前 production plan 与 fresh evidence 取代。

## 逐提交分类

| # | Commit | 主题 | 最终处置 | 理由 |
| ---: | --- | --- | --- | --- |
| 1 | `7b235bf782` | 初始 BT/BB 文档 | 丢弃 | 被后续累计 spec 覆盖，且模型已被 UE 5.7 inventory 修正 |
| 2 | `5478beb627` | editor layout 里程碑 | 丢弃 | 增量文档过时 |
| 3 | `f7e15bf0ad` | 完整 BT semantics 要求 | 丢弃 | 要求并入新 production design，原提交无独立实现 |
| 4 | `c563b6bd50` | 动态节点设计 | 修改后集成 | 保留动态类目标；改为 BTGraph/NodeGuid、完整反射和 class-policy selector |
| 5 | `8d846beef9` | 实施计划 | 重写 | 旧计划架构和证据状态过时；由 2026-07-15 production plan 替代 |
| 6 | `9d19545a74` | profiles 注册 | 冲突/替代 | stub 生命周期和 Build.cs 与 baseline 冲突；按当前 profile runtime 重接 |
| 7 | `51fb289a35` | key schema utility | 修改后集成 | 保留解析/诊断意图；扩展 ordered keys、Struct/default/custom properties |
| 8 | `d57ed86fb7` | duplicate key validation | 集成 | duplicate rejection 行为有效 |
| 9 | `64b19b2a52` | key metadata | 集成 | entry/key metadata 往返意图有效，范围由新 inventory 扩展 |
| 10 | `787352640a` | key schema roundtrip | 集成 | parent/metadata roundtrip 行为有效 |
| 11 | `71ece41539` | key schema fields | 集成 | 字段 roundtrip 测试意图有效 |
| 12 | `a91e4966bd` | conflicting key types | 集成 | 精确拒绝行为有效 |
| 13 | `48bb61b901` | Blackboard profile | 修改后集成 | lifecycle 接到当前 exact-class/profile runtime 和全局 rollback |
| 14 | `e92fa807c6` | BB apply hardening | 集成 | dry-run、reorder、parent cycle、object preservation 有效 |
| 15 | `4fcc5689ca` | BB apply canonical compare | 集成 | repeated apply contract 有效 |
| 16 | `24aa750ca2` | BB diff canonical compare | 集成 | unchanged diff contract 有效 |
| 17 | `56c7de96e2` | reflected runtime | 重写 | 与 baseline property runtime 重复；改为公共 staged runtime 扩展 |
| 18 | `f1ec3c6f09` | selector filters | 修改后集成 | filter 只读/class-derived；只写 SelectedKeyName |
| 19 | `b869251ee9` | unsupported diagnostics | 重写 | 不允许以 unsupported/_Skipped 缩减合法 authored scope |
| 20 | `8465ad9a06` | nested reflected refs | 修改后集成 | 测试意图迁入公共 property runtime |
| 21 | `b3c2f6788d` | containers | 重写 | 原实现拒绝 FInstancedStruct；公共 runtime 必须支持 |
| 22 | `65e29ef7bb` | public tree adapter | 修改后集成 | 保留公共 region/diagnostic 合同；模型改为 graph-authored projection |
| 23 | `9296cbe36c` | authored diagnostic paths | 集成 | identity path/duplicate diagnostics 有效 |
| 24 | `6ca3a45acf` | validation lifecycle | 集成 | unknown fields/hooks/config path 行为有效 |
| 25 | `bbf26d2c26` | shallow authored fields | 集成 | strict unknown-field boundary 有效 |
| 26 | `5526c1189c` | BT profile lifecycle | 修改后集成 | 删除 strict-empty 死代码，接当前 lifecycle/rollback |
| 27 | `2f05cb751d` | BT materialization | 修改后集成 | 领域逻辑保留；materializer 改为 BTGraph-first |
| 28 | `f1d5441a00` | semantic tree hardening | 集成 | invalid decorator/diff path 行为有效 |
| 29 | `baa02b7fc1` | semantic order | 集成 | 顺序 diff 有效；identity 改为 NodeGuid |
| 30 | `cdf2f746c5` | BT/BB validation | 修改后集成 | selector/subtree 行为保留并接共享 BB schema |
| 31 | `2c8721b3f1` | compatibility alignment | 集成 | 兼容矩阵测试意图有效 |
| 32 | `609fa703e6` | editor layout | 冲突/替代 | 与 recursive graph Position/comment 及坐标语义冲突，按完整 editor surface 重建 |
| 33 | `d5a11308d7` | schema creates BT graph | 集成 | 通过 UE schema 建图方式正确 |
| 34 | `247f17f642` | sparse layout | 集成 | sparse/optional layout 行为在新合同下保留 |
| 35 | `ccf3b07dee` | empty comments deletion | 集成 | 显式删除语义有效 |
| 36 | `8282ef0975` | roundtrip/canonical/report | 冲突/替代 | 与公共 canonicalizer/region runtime 重叠，历史报告证据不完整 |
| 37 | `645cf6bb99` | remove hardcoded canonicalization | 集成 | 移除具体 service 特判是必要修正 |
| 38 | `62cc1b8892` | verification report | 丢弃 | 被后续历史报告覆盖且非本次 fresh evidence |
| 39 | `94e408706c` | public schema alignment | 修改后集成 | 测试意图保留，横跨组件按当前 runtime 重接 |
| 40 | `8c631a24ce` | final schema evidence | 丢弃 | 仅旧报告，无新增能力 |
| 41 | `4f3dd5d4df` | validation/apply atomicity | 集成 | transient outer/graph rollback 意图保留并扩展到 save/reload |
| 42 | `d2e55e5bda` | quality evidence | 丢弃 | build/test 为旧 Windows 记录，HTTP 明确失败 |

## 分类集合

### 集成（19）

`d57ed86fb7`, `64b19b2a52`, `787352640a`, `71ece41539`, `a91e4966bd`, `e92fa807c6`, `4fcc5689ca`, `24aa750ca2`, `9296cbe36c`, `6ca3a45acf`, `bbf26d2c26`, `f1d5441a00`, `baa02b7fc1`, `2c8721b3f1`, `d5a11308d7`, `247f17f642`, `ccf3b07dee`, `645cf6bb99`, `4f3dd5d4df`。

### 修改后集成（10）

`c563b6bd50`, `51fb289a35`, `48bb61b901`, `f1ec3c6f09`, `8465ad9a06`, `65e29ef7bb`, `5526c1189c`, `2f05cb751d`, `cdf2f746c5`, `94e408706c`。

### 重写（4）

`8d846beef9`, `56c7de96e2`, `b869251ee9`, `b3c2f6788d`。

### 丢弃（6）

`7b235bf782`, `5478beb627`, `f7e15bf0ad`, `62cc1b8892`, `8c631a24ce`, `d2e55e5bda`。

### 冲突并替代（3）

`9d19545a74`, `609fa703e6`, `8282ef0975`。

## 当前执行结果

- 候选历史已通过 merge commit 落入当前 ancestry；分类合计仍为 `19 + 10 + 4 + 6 + 3 = 42`。
- 默认 Unity Build 在 `AssetFactorySandboxBTBBEditor` fresh pass；helper collision 通过文件/领域前缀命名解决，没有禁用 Unity。
- Fresh focused UE evidence：
  - BehaviorTree `69/69`
  - BlackboardData `33/33`
  - Persistence `18/18`
  - AtomicFile `11/11`
  - RegionCanonicalizer `28/28`
  - ManagedProperties `2/2`
- Combined scoped report `BTBB_SCOPED_FINAL_AFTER_REVIEW_R2`：`161/161`，0 warnings，0 failures。
- MCP `39/39`；Python live-smoke harness unit `8/8`。
- 8562 pre-restart PID `90644` 与 post-restart PID `97092` 均通过；保存后的资产在新进程中 fresh extract/diff 成功。
- 完整 `AssetFactory.AssetDocument` 聚合套件不是本 Goal 门槛。ABP/AnimGraph/StateMachines 是另一分支处理的已知未完成功能，不阻塞本审计结论。

最终 scoped checkpoint SHA 与 clean-worktree 状态在提交后记录于 production plan 和交付汇报。
