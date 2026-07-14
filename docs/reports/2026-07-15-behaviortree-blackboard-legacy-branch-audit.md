# BehaviorTree / BlackboardData 旧分支逐提交审计

**日期**：2026-07-15
**候选分支**：`origin/feature/asset-document-behaviortree-blackboard-impl`
**共同基点**：`12ea6ae87c37227cb73f65dd848b2a79c8ac2ee8`
**当前权威基线**：`da7b1c82ea5cabf02cd6e0675d4f2732272590dd`

## 结论

旧分支 42 个提交不是可直接整体合入的 production unit。分类为：19 个语义可集成、10 个需按当前公共 runtime 适配、4 个需重写、6 个过时丢弃、3 个与当前基线架构冲突并由新实现替代。

“可集成”只表示提交所建立的行为和测试意图保留，不表示原 SHA 可以绕过当前 TDD、Unity Build 或 save-reload 验证。为保留可审计历史，集成可使用三方 merge 作为候选代码落点，但冲突、重写和替代项在最终实现中必须有当前合同与测试证明，不能以 merge 成功作为完成证据。

旧证据的已确认缺口：

- focused 报告的 33 条只对应 BehaviorTree；没有证明 23 条 Blackboard focused suite；
- HTTP `127.0.0.1:8560` 绑定失败，真实外部 smoke 从未通过；
- `FInstancedStruct` 被明确作为 unsupported，违反动态完整 reflected authored surface；
- selector AllowedTypes/filter 被当作可写 authored 数据，实际是 concrete node class policy；
- `NodeName` 被用于 identity，而 UE graph 的稳定 identity 是 NodeGuid；
- runtime tree 被当作主要模型，未以 BTGraph 为唯一 authored truth；
- KeyType metadata、Comment Box fields、项目自定义节点、existing-asset save/reload rollback 覆盖不足；
- spec/plan/report 仍是旧里程碑状态，69 个 plan 项全部未勾选。

## 逐提交分类

| # | Commit | 主题 | 最终处置 | 理由 |
| ---: | --- | --- | --- | --- |
| 1 | `7b235bf782` | 初始 BT/BB 文档 | 丢弃 | 被后续累计 spec 覆盖，且模型已被 UE 5.7 inventory 修正 |
| 2 | `5478beb627` | editor layout 里程碑 | 丢弃 | 增量文档过时 |
| 3 | `f7e15bf0ad` | 完整 BT semantics 要求 | 丢弃 | 要求并入新 production design，原提交无独立实现 |
| 4 | `c563b6bd50` | 动态节点设计 | 修改后集成 | 保留动态类目标；改为 BTGraph/NodeGuid、完整反射和 class-policy selector |
| 5 | `8d846beef9` | 实施计划 | 重写 | 69 项未勾且架构落后；由 2026-07-15 production plan 替代 |
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
| 21 | `b3c2f6788d` | containers | 重写 | 原实现拒绝 FInstancedStruct；公共 runtime 必须完整支持 |
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

## 集成顺序

1. 先固定公共 property runtime、tree/recursive graph 边界、field rules 和 canonical/layout 合同。
2. 接入 Blackboard schema/profile，再保留 #8–16 的行为加固。
3. 接入 graph-first Tree adapter/materializer，再保留相关 validation/order 修正。
4. 重写 reflected runtime，补 `FInstancedStruct`、custom classes 和完整 editor surface。
5. 最后使用当前 Mac/UE 5.7 环境重新建立 build、BT、BB、full、MCP、8562 HTTP 证据。
