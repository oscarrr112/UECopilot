# AssetDocument Region Canonicalizer 设计

日期：2026-06-22

## 背景

UBlueprint graph sidecar 已经可以通过 HTTP smoke 完成 `apply-file -> extract -> diff` 的语义验证，但 `apply-file` 仍可能返回：

```json
{
  "sidecar_sync_update_skipped": true,
  "sidecar_sync_update_skip_reason": "Post-apply asset evidence hash differs for region 'Body.UbergraphPages'"
}
```

这不是 graph apply 失败，而是 sidecar authored form 与 asset evidence form 在语义等价时仍可能 hash 不等。典型差异包括：

- UE 生成的 `GraphGuid` / `NodeGuid`。
- adapter 推断出的 `Capability`。
- sidecar 输入支持的 compact link sugar 与 extract 后的 expanded endpoint object。
- agent-friendly node id 与 extract 后的 canonical/member-based id。

现有实现中，`FAssetDocumentSidecarDelta::HashSidecarRegion()` 直接把 region value 交给 `FAssetDocumentCanonicalJson::HashJsonValue()`。`FAssetDocumentCanonicalJson` 是通用 JSON canonical serializer/hash 工具，只负责排序 key、稳定 number/string 输出、递归移除 `_Skipped`、`_meta` 和 `Policy.ExtractOnlyFields`。`FAssetDocumentSidecarSyncEngine` 已经是纯 hash/state decision engine，不计算 hash，也没有 region-specific 分支。

因此缺少的不是更多 sync decision 规则，而是一个位于 region value 与 hash/sync 之间的语义归一化层。

## 设计目标

- 保持 sidecar 是 sparse source-of-truth delta，不变成完整 asset mirror。
- 保持 `FAssetDocumentSidecarSyncEngine` 只负责 sync direction：`NoChange`、`ApplySidecarToAsset`、`RegenerateSidecarRegion`、`Conflict`、`NeedsInitialBaseline`。
- 保持 `FAssetDocumentCanonicalJson` 是资产无关的稳定 JSON 工具。
- 将 sidecar/evidence 的语义等价归一化集中到专门类，避免在 `AssetDocumentService.cpp` 中扩展硬编码 class/region if-list。
- 第一版专注解决 sync canonicalization，不把 `DefaultReducer` 或完整 sparse reduction 体系一并重构。

## 非目标

- 不实现任意 graph 语义等价判断。
- 不把 UE graph raw dump 写入 sidecar。
- 不把 `GraphGuid` / `NodeGuid` 等 generated metadata 作为 authored sidecar 的必填内容。
- 不在 `SidecarSyncEngine`、`DefaultReducer`、`CanonicalJson` 中加入 asset-class switch。
- 不改变 `Body.FunctionGraphs`、`Body.MacroGraphs`、`Body.Timelines` 的 deferred 状态。

## 推荐方案

新增薄中间层：

```cpp
class FAssetDocumentRegionCanonicalizer
{
public:
    static TSharedPtr<FJsonValue> CanonicalizeForHash(
        const FAssetDocumentRegionCanonicalizeContext& Context,
        const TSharedPtr<FJsonValue>& RegionValue);

    static TSharedPtr<FJsonValue> CanonicalizeForSidecarWriteback(
        const FAssetDocumentRegionCanonicalizeContext& Context,
        const TSharedPtr<FJsonValue>& RegionValue);

    static FString HashRegionValue(
        const FAssetDocumentRegionCanonicalizeContext& Context,
        const TSharedPtr<FJsonValue>& RegionValue);
};
```

其中 `FAssetDocumentRegionCanonicalizeContext` 至少包含：

- `RegionPolicy`
- `RegionId`
- source kind：`SidecarAuthored` 或 `AssetEvidence`
- optional `AssetClass`
- optional `Capability` / extension hook 名称

第一版默认实现：

- 对 hash form 复用现有 `FAssetDocumentCanonicalJson::CloneWithoutExtractOnlyFields()` 和 `HashJsonValue()`。
- 对 sidecar writeback form 保持 authored shape，默认只移除 extract-only diagnostic 字段，不主动补齐 generated metadata。
- 不改变现有无特殊 canonicalizer 的 region hash，作为兼容性 invariant。

## 职责边界

```text
UE Asset
  -> EvidenceExtractor / capability Extract
  -> RegionCanonicalizer.CanonicalizeForHash(AssetEvidence)
  -> sidecar sync hash

Sidecar authored region
  -> RegionCanonicalizer.CanonicalizeForHash(SidecarAuthored)
  -> sidecar sync hash

Asset evidence region
  -> RegionCanonicalizer.CanonicalizeForSidecarWriteback(AssetEvidence)
  -> sidecar authored/writeback region

SidecarSyncEngine
  -> compares opaque hashes and last sync state
```

`FAssetDocumentSidecarDelta` 继续负责：

- `FindRegionValue`
- `SetRegionValue`
- explicit empty 判断
- BodyPath traversal

`FAssetDocumentCanonicalJson` 继续负责：

- deterministic JSON serialization
- stable SHA1 hash
- generic extract-only field stripping

`FAssetDocumentSidecarSyncEngine` 继续负责：

- sync direction decision
- conflict resolution action construction

`DefaultReducer` 在本设计中仍是概念边界：负责 default/baseline reduction，不负责 sidecar/evidence semantic equivalence。

## Extension 方式

第一版 extension 应以 policy/capability hook 为主，不新增 asset-class switch。

推荐策略：

- 在 `FAssetDocumentRegionPolicy` 中增加 canonicalization hook 名称，或复用现有 `ExtensionHookName` 并明确 hook phase。
- 由 profile/capability 注册 hook resolver。
- generic path 使用 identity canonicalizer。
- special region 只处理自身 semantic equivalence，不接管 sync decision。

不推荐策略：

- 在 `AssetDocumentService.cpp` 增加 `if Class == UBlueprint && Region == Body.UbergraphPages`。
- 在 `FAssetDocumentCanonicalJson` 中识别 `GraphGuid` / `NodeGuid`。
- 在 `FAssetDocumentSidecarSyncEngine` 中识别 region kind 或 asset class。

## UBlueprint Graph 第一版 canonicalization

`Body.UbergraphPages` 的第一版目标是解决 generated metadata 与 authored form 的 hash divergence。

`CanonicalizeForHash()` 对 sidecar authored region 和 asset evidence region 统一执行：

- 忽略或规范化 `GraphGuid`。
- 忽略或规范化 `NodeGuid`。
- 允许缺省 `Capability` 与 adapter 推断 `Capability` 视为等价。
- 将 compact link sugar 与 expanded endpoint object 归一为 expanded comparable form。
- 保留 node member/class/pin/default/link 等语义字段。

`CanonicalizeForSidecarWriteback()` 不应强制把 `GraphGuid` / `NodeGuid` 写回 authored sidecar。writeback form 应优先保持 agent-friendly、sparse、可编辑；hash canonicalization 才处理 generated metadata 的等价性。

## AnimSequence 例外迁移

当前 `AssetDocumentService.cpp` 中存在 AnimSequence post-apply divergence hardcoded exception：精确 class path `/Script/Engine.AnimSequence` 加 region id 列表。

本设计要求后续 implementation plan 将它作为迁移目标：

- 删除或收敛 `IsAnimSequencePostApplyCanonicalDivergenceRegion()` 的扩展趋势。
- 将 AnimSequence 的 canonical divergence 表达为 region policy/capability canonicalizer 行为。
- 不要求第一步完全解决所有 AnimSequence writeback canonicalization，但必须停止新增同类 hardcoded exception。

## Error Handling

- canonicalizer 失败时返回明确 diagnostic code，例如 `RegionCanonicalizationFailed`。
- unsupported semantic equivalence 不应被默默当作 changed；应继续让 diff/sync 暴露 changed 或 skipped。
- `sidecar_sync_update_skipped=true` 仍可作为保守 fallback，但同一类可解释 generated metadata divergence 不应再触发 skip。
- hash form 与 writeback form 必须分离，避免为了 hash 稳定而破坏 sidecar authored shape。

## 测试策略

新增 focused automation tests：

- generic identity canonicalizer 保持现有 region hash bit-for-bit。
- `_Skipped`、`_meta`、`ExtractOnlyFields` 行为保持不变。
- `Body.UbergraphPages` sidecar 缺 `GraphGuid` / `NodeGuid` / inferred `Capability` 时，与 extracted asset evidence hash 相等。
- compact link sugar 与 expanded link form hash 相等。
- apply-file 成功后，语义等价的 `Body.UbergraphPages` 不再返回 `sidecar_sync_update_skipped=true`。
- unsupported graph content 仍产生 diagnostic/skipped，不被 canonicalizer 伪装为 equal。
- AnimSequence hardcoded divergence exception 至少有迁移保护测试，防止继续扩展 service-level if-list。

## GLM Review 结论

GLM 只读 review 结论为：`No blocking design issues`。

它确认：

- `FAssetDocumentRegionCanonicalizer` 应为独立类。
- 不应折进 `SidecarDelta`、`CanonicalJson`、`DefaultReducer` 或 `SidecarSyncEngine`。
- `HashSidecarRegion()` 是合适的 interception chokepoint。
- writeback form 与 hash form 必须分开。
- `IsAnimSequencePostApplyCanonicalDivergenceRegion()` 和 `/Script/Engine.AnimSequence` branch 应作为后续迁移成功标准。

它提出的两个非阻塞风险：

- 如果 writeback canonicalization 过度整理 authored form，re-extract roundtrip 可能不稳定；因此第一版应只对 hash 做强 canonicalization，writeback 保持 authored sparse form。
- UBlueprint graph 的完整语义等价，尤其 positional identity 与 signature-based identity，需要逐步推进；第一版只解决 generated metadata 和等价 schema form。

## 验收标准

- 新设计文档明确职责边界，且不要求在 `SidecarSyncEngine` 或 `CanonicalJson` 中加入 region-specific 逻辑。
- 后续 implementation plan 以 `FAssetDocumentRegionCanonicalizer` 为中心切分 task。
- UBlueprint graph sync skip 的修复路径是 canonicalization，而不是 service-level hardcoded exception。
- 设计保持 reflection/policy/capability hook 优先，避免静态资产类型列表。
