# AssetDocument RegionCanonicalizer Architecture Upgrade Triggers

日期：2026-06-22

对应设计：

- `docs/superpowers/specs/2026-06-22-asset-document-region-canonicalizer-design.md`

用途：

```text
记录 RegionCanonicalizer 第一版允许保守处理的边界，以及必须触发后续架构升级的条件。
后续 task/review 应优先从这里清理，不依赖设计 spec 正文里的临时备注。
```

---

## 1. 维护规则

每当 AssetDocument sync/hash/canonicalization 相关实现出现以下情况，必须更新本文件：

- 新增 `sidecar_sync_update_skipped=true` 的已知原因；
- 新增 region-specific canonicalization 行为；
- 新增或修改 service-level class/region exception；
- 新增 policy/capability canonicalization hook；
- 改变 hash form、writeback form 或 evidence form 的边界；
- 将 deferred trigger 清理为已完成。

每个条目需要包含：

- 当前允许的保守处理；
- 触发后续架构升级的具体条件；
- 升级时必须触碰或禁止触碰的文件/类；
- 清理成功标准；
- 最低测试/验证要求。

---

## 2. RegionCanonicalizer v1 Scope

### 2.1 Hash-only semantic canonicalization

当前允许的保守处理：

- `FAssetDocumentRegionCanonicalizer` 第一版可以先只服务于 comparable hash。
- `CanonicalizeForSidecarWriteback()` 默认保持 authored sparse shape，不强制写回 generated metadata。
- `Body.UbergraphPages` 第一版只处理 generated metadata 和 schema-form 等价：
  - `GraphGuid`
  - `NodeGuid`
  - inferred `Capability`
  - compact link sugar vs expanded endpoint object
  - agent-friendly id vs extracted canonical/member-based id 的已验证等价子集

触发后续架构升级的条件：

- 任何 implementation 需要为了 hash 稳定而改变 sidecar authored/writeback shape；
- apply-file 为同一个 region 多次成功 apply 后仍产生 writeback/extract oscillation；
- sidecar authored form 与 extract form 的差异不再只是 generated metadata/schema sugar，而是涉及 graph semantic identity；
- 新增第二个以上 region 需要不同的 hash form 与 writeback form。

升级要求：

- 将 hash form、writeback form、evidence form 的 API 明确拆成独立 request/response 类型；
- 每个 form 的 roundtrip contract 必须独立测试；
- 不允许把 writeback canonicalization 硬塞进 `FAssetDocumentCanonicalJson`。

清理成功标准：

- hash-only canonicalization 不会破坏 authored sidecar；
- writeback 行为可解释且稳定；
- HTTP smoke 中语义等价 region 不再因为 generated metadata divergence 返回 sync skipped。

最低验证：

- focused automation 覆盖 hash 等价；
- apply-file HTTP smoke 覆盖 sidecar rewrite 或明确 skip fallback；
- extract/diff roundtrip 覆盖 no oscillation。

### 2.2 AnimSequence service-level divergence exception

当前允许的保守处理：

- 现有 `AssetDocumentService.cpp` 中的 `IsAnimSequencePostApplyCanonicalDivergenceRegion()` 可以作为历史兼容存在到 RegionCanonicalizer 第一版实现前。
- 不允许继续向该 helper 添加新的 asset class 或 region id。

触发后续架构升级的条件：

- 任何新 region 想复用同类 “hash differs but accept anyway” 逻辑；
- 修改 `TryWriteApplyFileSyncState()` 时触碰到 AnimSequence divergence 分支；
- 新增 AssetDocument profile 遇到 post-apply canonical divergence；
- AnimSequence 相关 sync/hash 测试因为 divergence exception 需要扩展。

升级要求：

- 将 AnimSequence divergence 迁移到 policy/capability canonicalizer hook；
- 删除或收敛 `/Script/Engine.AnimSequence` exact class path 分支；
- `FAssetDocumentSidecarSyncEngine` 仍只能接收 opaque hash，不允许接收 asset class 或 region kind。

清理成功标准：

- `AssetDocumentService.cpp` 不再维护可增长的 class/region if-list；
- AnimSequence apply-file 现有 strict region 行为保持不变；
- previously accepted canonical divergence region 仍有清楚的 canonicalizer 测试覆盖。

最低验证：

- `AssetFactory.AssetDocument.AnimSequence` 相关 apply-file sync tests；
- `AssetDocument.SidecarSyncEngine` tests 保持不需要 asset class；
- `git grep` 确认没有新增 service-level asset class divergence list。

### 2.3 UBlueprint graph semantic identity beyond generated metadata

当前允许的保守处理：

- 第一版只承诺 generated metadata/schema-form 等价，不承诺任意 graph semantic equivalence。
- unsupported node/pin/function 仍必须通过 diagnostic、`_Skipped.Graphs` 或 diff skipped entries 暴露。

触发后续架构升级的条件：

- 需要把 positional node identity 升级为 signature/member-based identity；
- 需要支持 latent、delegate、dynamic multicast、wildcard/container pin 等 Tier 1 外 graph node；
- existing graph 与 sidecar graph 的等价判断需要跨 node adapter 协作；
- graph diff 开始需要忽略或重写 non-generated semantic fields 才能通过。

升级要求：

- 在 graph layer 定义明确的 semantic identity model；
- canonicalizer 只能调用 graph adapter 暴露的 semantic projection，不直接识别具体 `UK2Node_*` 类型；
- 不允许为了 hash 通过而吞掉 unsupported graph content。

清理成功标准：

- 每类新增 graph semantic equivalence 都有 red/green test；
- unsupported graph 内容仍可被 agent 定位到具体 path 和 reason；
- apply/extract/diff/compile smoke 覆盖新增 node lifecycle。

最低验证：

- focused `AssetFactory.AssetDocument.UBlueprint.Graph*` automation；
- UBT 编译；
- HTTP smoke 覆盖真实 `apply-file -> extract -> diff`。

### 2.4 DefaultReducer concrete class introduction

当前允许的保守处理：

- 当前 `DefaultReducer` 仍是概念边界，不要求第一版 RegionCanonicalizer 同时实现完整 reducer class。

触发后续架构升级的条件：

- 引入具体 `FAssetDocumentDefaultReducer` 或等价类；
- reducer 需要知道 sidecar/evidence semantic equivalence 才能工作；
- reducer 开始处理 generated metadata、sync hash 或 writeback canonicalization。

升级要求：

- `DefaultReducer` 只负责 default/baseline delta；
- `RegionCanonicalizer` 负责 semantic equivalence 和 form normalization；
- 两者之间使用明确的 normalized evidence/authored region value 传递，不互相读取 sync state。

清理成功标准：

- reducer tests 不需要构造 `_meta.sync`；
- canonicalizer tests 不需要知道 CDO/default baseline；
- sync engine tests 仍只比较 hashes。

最低验证：

- reducer focused tests；
- canonicalizer focused tests；
- existing `AssetDocument.SidecarDelta` / `AssetDocument.SidecarSyncEngine` tests 保持职责不扩散。

---

## 3. Review Checklist

任何后续 RegionCanonicalizer 相关 plan/review 必须检查：

- 是否新增了 service-level asset class / region if-list；
- 是否把 graph-specific 规则写进 `FAssetDocumentCanonicalJson`；
- 是否让 `FAssetDocumentSidecarSyncEngine` 认识 asset class、region kind 或 graph fields；
- 是否混淆了 hash form 和 sidecar writeback form；
- 是否把 unsupported content canonicalize 成了 equal；
- 是否更新了本维护文件中的 trigger 状态。

