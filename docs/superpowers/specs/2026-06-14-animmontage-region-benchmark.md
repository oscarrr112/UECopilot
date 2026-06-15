# AnimMontage Region Benchmark 梳理

日期：2026-06-14

## 目标

本文件把 `UAnimMontage` 作为第一个完整 benchmark 资产类型，梳理剩余 authored surface、候选 `Body` region、实现优先级、验证方式和单 region 工作量模型。

这个 benchmark 的目的不是把 `UAnimMontage` 序列化成完整镜像，而是评估 delta-first AssetDocument 要支持一个结构化 UE asset 时，需要为哪些有意义的作者语义建立 managed region。

## 当前实现基线

当前 `FAnimMontageAssetDocumentCapability` 的 canonical body keys 是：

- `References`
- `Preview`
- `Sync`
- `RootMotion`
- `Metadata`
- `SectionMetadata`
- `TimeStretch`
- `Curves`
- `SlotAnimTracks`
- `CompositeSections`
- `Notifies`
- `NotifyStates`
- `Blend`

当前 `FAnimMontageAssetDocumentProfile::GetRegionPolicies()` 声明以下 managed region：

| Region | UE property scope | 当前状态 |
| --- | --- | --- |
| `Body.Blend` | `BlendIn`, `BlendOut`, `BlendModeIn`, `BlendModeOut`, `BlendOutTriggerTime`, `bEnableAutoBlendOut` 的稳定子集 | complete expanded region |
| `Body.Sync` | `SyncGroup`, `SyncSlotIndex` | complete |
| `Body.RootMotion` | `bEnableRootMotionTranslation`, `bEnableRootMotionRotation`, `RootMotionRootLock` | complete as montage legacy settings |
| `Body.References` | `Skeleton` | complete |
| `Body.Preview` | `PreviewMesh`, `PreviewBasePose` | complete |
| `Body.Metadata` | asset-level `MetaData` | complete |
| `Body.SectionMetadata` | `FCompositeSection.MetaData` | complete |
| `Body.TimeStretch` | `TimeStretchCurveName` 和 `TimeStretchCurve` authored settings | complete with baked data excluded |
| `Body.Curves` | montage-owned inherited float curves | complete for montage-owned float curves |
| `Body.SlotAnimTracks` | `SlotAnimTracks` | 已实现 |
| `Body.CompositeSections` | `CompositeSections` | 已实现 |
| `Body.Notifies` | `Notifies` 中 AssetDocument-managed notify event | 已实现 |
| `Body.NotifyStates` | `Notifies` 中 AssetDocument-managed notify state event | 已实现 |

## UE 5.7 AnimMontage Authored Surface

以下字段来自 UE 5.7 `Engine/Source/Runtime/Engine/Classes/Animation/AnimMontage.h` 的 `UAnimMontage` 定义。

| UE 字段或概念 | 头文件位置 | 作者语义判断 | 当前 AssetDoc 覆盖 |
| --- | --- | --- | --- |
| `BlendModeIn` | `AnimMontage.h:630` | blend 输入模式 | `Body.Blend` complete expanded region |
| `BlendModeOut` | `AnimMontage.h:633` | blend 输出模式 | `Body.Blend` complete expanded region |
| `BlendIn` | `AnimMontage.h:637` | blend in 配置，当前覆盖稳定 time 字段 | `Body.Blend` complete expanded region |
| `BlendOut` | `AnimMontage.h:646` | blend out 配置，当前覆盖稳定 time 字段 | `Body.Blend` complete expanded region |
| `BlendOutTriggerTime` | `AnimMontage.h:657` | 自动 blend out 触发时间 | `Body.Blend` complete expanded region |
| `SyncGroup` | `AnimMontage.h:673` | marker sync group 名称 | `Body.Sync` complete |
| `SyncSlotIndex` | `AnimMontage.h:677` | 用于收集 marker 的 slot track index | `Body.Sync` complete |
| `MarkerData` | `AnimMontage.h:680` | 从 referenced sequence markers 收集的 marker cache/data | 不建议直接编辑 |
| `CompositeSections` | `AnimMontage.h:684` | montage section timeline | 已覆盖 |
| `SlotAnimTracks` | `AnimMontage.h:688` | slot track 与 segment authoring | 已覆盖 |
| `bEnableRootMotionTranslation` | `AnimMontage.h:698` | legacy root motion translation 开关 | `Body.RootMotion` complete as montage legacy settings |
| `bEnableRootMotionRotation` | `AnimMontage.h:702` | legacy root motion rotation 开关 | `Body.RootMotion` complete as montage legacy settings |
| `bEnableAutoBlendOut` | `AnimMontage.h:706` | 播放到末尾时是否自动 blend out | `Body.Blend` complete expanded region |
| `BlendProfileIn` | `AnimMontage.h:710` | blend profile 引用 | 未覆盖 |
| `BlendProfileOut` | `AnimMontage.h:714` | blend profile 引用 | 未覆盖 |
| `RootMotionRootLock` | `AnimMontage.h:718` | legacy root motion root lock | `Body.RootMotion` complete as montage legacy settings |
| `PreviewBasePose` | `AnimMontage.h:723` | editor-only additive preview base pose | `Body.Preview` complete |
| `BranchingPointMarkers` | `AnimMontage.h:938` | notify branching point 派生 cache | 不作为 authoring region |
| `BranchingPointStateNotifyIndices` | `AnimMontage.h:943` | branching state notify 派生索引 | 不作为 authoring region |
| `TimeStretchCurve` | `AnimMontage.h:984` | montage time stretch 结构 | `Body.TimeStretch` complete with baked data excluded |
| `TimeStretchCurveName` | `AnimMontage.h:988` | time stretch 使用的 curve 名称 | `Body.TimeStretch` complete with baked data excluded |
| inherited `RawCurveData` / animation data model curves | `AnimSequenceBase.h` inherited surface | Montage 自己拥有的曲线通道，不等同于 referenced sequence curves | `Body.Curves` complete for montage-owned float curves |

## Region 候选分类

### 第一批：低风险、高价值

第一批适合用来做 benchmark implementation，因为字段少、默认值清晰、验证方式直接。

| 推荐 region | 建议 Body key | 字段 | Apply 模式 | 状态 |
| --- | --- | --- | --- | --- |
| Blend 扩展 | `Body.Blend` | `BlendModeIn`, `BlendModeOut`, `BlendOutTriggerTime`, `bEnableAutoBlendOut`, `BlendIn`, `BlendOut` 的可稳定子集 | reflected scalar/reference + existing `FAlphaBlend` helper | complete expanded region |
| Sync 设置 | `Body.Sync` | `SyncGroup`, `SyncSlotIndex` | reflected scalar | complete |
| Root motion legacy 设置 | `Body.RootMotion` | `bEnableRootMotionTranslation`, `bEnableRootMotionRotation`, `RootMotionRootLock` | reflected scalar/enum | complete as montage legacy settings |
| Base refs | `Body.References` | `Skeleton` | reference fragment | complete |
| Preview additive settings | `Body.Preview` | `PreviewMesh`, `PreviewBasePose` | reference fragment | complete |

### 第二批：中风险，需要单独验证

| 候选 region | 建议 Body key | 字段 | 状态 |
| --- | --- | --- | --- |
| Time stretch | `Body.TimeStretch` | `TimeStretchCurveName`, `TimeStretchCurve` 的 authored settings | complete with baked data excluded |
| Montage-owned curves | `Body.Curves` | Montage 自己的 inherited `RawCurveData` / data model float curves | complete for montage-owned float curves |
| Metadata | `Body.Metadata` / `Body.SectionMetadata` | asset-level metadata、`FCompositeSection.MetaData` | complete |

### 暂不作为 AnimMontage 直接 region

| 概念 | 原因 |
| --- | --- |
| `MarkerData.AuthoredSyncMarkers` | `UAnimMontage::CollectMarkers()` 会从 `SyncGroup`、`SyncSlotIndex` 和 referenced `UAnimSequence::AuthoredSyncMarkers` 收集。它更像 derived montage marker evidence，不适合在 AnimMontage sidecar 里直接手写。 |
| referenced sequence animation curves | `SlotAnimTracks[].AnimTrack.AnimSegments[].AnimReference` 指向的 `UAnimSequenceBase` 曲线属于被引用资产，不属于 Montage sidecar 的 owned region。 |
| `BranchingPointMarkers` | 由 notify branching point 刷新而来，属于 cache/derived state。 |
| `BranchingPointStateNotifyIndices` | 由 branching notify state 计算出的索引，不是作者编辑面。 |
| `BranchingPoints_DEPRECATED` | editor-only deprecated 迁移字段，不应作为新 sidecar schema。 |
| `TimeStretchCurve.Markers` / `Sum_dT_i_by_C_i` | time stretch 的 baked/cached output，不应由 sidecar 直接维护。 |

## 对 `Curves` 和 `RootMotion` 的重新命名建议

之前目标文档写了 `Body.Curves` 和 `Body.RootMotion`。盘点后建议调整语义边界：

- `Body.RootMotion` 可以保留，但文档和 schema 必须说明它只覆盖 Montage 上的 legacy root motion settings：`bEnableRootMotionTranslation`、`bEnableRootMotionRotation`、`RootMotionRootLock`。
- `Body.Curves` 可以作为 AnimMontage 自己的曲线 region，但必须只表示 montage-owned inherited animation data curves，不包括 referenced sequences 的 curves。
- `Body.TimeStretch` 应作为 `Body.Curves` 的特殊消费者或配套 settings：`TimeStretchCurveName` 指向一条 montage-owned float curve，`TimeStretchCurve` 中 baked markers 由 UE 生成。
- referenced sequence curves 应进入对应 `AnimSequence` 的 AssetDocument，而不是塞进 AnimMontage sidecar。

## 单 Region 工作量模型

为一个新的 AnimMontage region 落地，实际需要修改或验证以下位置：

| 工作项 | 说明 |
| --- | --- |
| Body key | 更新 `GetCanonicalBodyKeys()`，否则 validation 会报 `UnknownBodyKey` |
| Shape validation | 更新 `ValidateBodyObjectShape()`，定义 object/array/scalar 类型约束 |
| Schema hint | 更新 `GetSchemaHint()`，让 MCP/schema inspection 暴露新字段 |
| Template | 更新 `CreateTemplate()`，如果新 region 应出现在模板中 |
| Parse/preflight | 在 `FParsedAnimMontageBody` 和 parse helper 中 staged 读取，避免半写入 |
| Apply | 在 `Apply()` 中 staged commit，并在必要时调用 UE repair hook |
| Extract | 在 `Extract()` 中输出 canonical sidecar 表示 |
| RegionPolicy | 在 `GetRegionPolicies()` 声明 region id、kind、managed UE property paths、reducer/apply mode |
| Sync/hash tests | 覆盖 `_meta.sync`、sidecar hash、asset evidence hash、accept sidecar/accept asset |
| Asset automation | 用真实 AnimMontage asset 验证 apply、extract、diff、保存、重开 |
| Docs | 更新本 benchmark、deferred fields、目标 spec 或 implementation plan |

粗略估算：

| Region 类型 | 典型改动规模 | 风险 |
| --- | --- | --- |
| scalar/reference object | 0.5 到 1 个 task | 低 |
| small struct object | 1 到 2 个 task | 中 |
| array/timeline rebuild | 2 到 4 个 task | 中高 |
| derived/cache-backed data | 先 research，再决定是否进入 task | 高 |

## 已完成实现顺序

### Task A：`Body.Blend` 扩展

状态：complete expanded region。在保留现有 `BlendInTime` / `BlendOutTime` 的基础上，加入低风险 blend option 字段。

候选字段：

- `BlendModeIn`
- `BlendModeOut`
- `BlendOutTriggerTime`
- `bEnableAutoBlendOut`

验收：

- sidecar 修改这些字段后，asset 中对应字段变化；
- asset 端修改这些字段后，extract/regenerate sidecar region；
- 缺失字段按 delta-first 语义回到默认或基线；
- 不破坏已有 `BlendInTime` / `BlendOutTime` smoke。

### Task B：`Body.Sync`

状态：complete。支持 marker sync 的 authoring settings，但不直接 author `MarkerData`。

候选字段：

- `SyncGroup`
- `SyncSlotIndex`

验收：

- sidecar 能设置 sync group 和 slot index；
- asset 端修改后能 regenerate；
- 当 referenced sequence 有 sync markers 时，UE 自己可以通过 `CollectMarkers()` / cache refresh 生成 marker evidence；
- sidecar 不暴露 `MarkerData.AuthoredSyncMarkers` 作为 editable data。

### Task C：`Body.RootMotion`

状态：complete as montage legacy settings。支持 Montage legacy root motion switches，并明确它不是完整 root motion 数据。

候选字段：

- `bEnableRootMotionTranslation`
- `bEnableRootMotionRotation`
- `RootMotionRootLock`

验收：

- sidecar 能设置布尔与 enum；
- asset 端修改后能 regenerate；
- 文档明确 sequence-owned root motion belongs to AnimSequence；
- automation 至少验证字段保存、重开和 extract 稳定。

### Task D：`Body.TimeStretch`

状态：complete with baked data excluded。实现可稳定 author 的 time stretch 表达。

候选字段：

- `TimeStretchCurveName`
- `TimeStretchCurve.SamplingRate`
- `TimeStretchCurve.CurveValueMinPrecision`

已确认：

- `Markers` 和 `Sum_dT_i_by_C_i` 是否应由 UE bake，而不是 sidecar 手写；
- apply 后是否需要调用 `BakeTimeStretchCurve()` 或其他 editor hook；
- 没有 referenced float curve 时如何验证。

### Task E：`Body.Curves`

状态：complete for montage-owned float curves。支持 Montage 自己的 inherited animation data curves，不管理 referenced sequence curves。

候选字段：

- montage-owned float curve names；
- float curve keys；
- curve flags 或 metadata 中确认为 authoring surface 的稳定字段。

验收：

- sidecar 中的 curve 写入 Montage 自己的数据模型或可保存 curve storage；
- `TimeStretchCurveName` 引用的曲线能被 UE bake 成 `TimeStretchCurve`；
- referenced `AnimReference` 资产的 curves 不被修改；
- extract 能稳定区分 montage-owned curves 和 referenced sequence curves。

## Benchmark 资产策略

继续使用验证 host，而不是真实主项目插件路径：

```text
C:/AVH1
```

建议保留或创建一个稳定 smoke asset：

```text
/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke
```

benchmark 应逐步给它增加：

- 至少一个 slot track 和 anim segment；
- 至少一个 composite section；
- 一个 managed notify；
- 一个 managed notify state；
- 非默认 blend settings；
- 非默认 sync settings；
- 非默认 root motion legacy settings；
- 非默认 time stretch settings；
- montage-owned float curve。

这样每个新增 region 都能在同一个真实 asset 上验证：

```text
extract -> sidecar delta -> apply-file -> save -> re-extract -> diff empty
```

## 当前结论

当前 benchmark 路线已落地为：

1. `Body.Blend` 已扩展为 complete expanded region。
2. `Body.Sync` 已纳入 sync settings，不直接管理 marker cache。
3. `Body.RootMotion` 已覆盖 Montage legacy root motion settings。
4. `Body.References`、`Body.Preview`、`Body.Metadata`、`Body.SectionMetadata` 已纳入 sync/conflict model。
5. `Body.TimeStretch` 已覆盖 authored settings，并排除 baked/cache data。
6. `Body.Curves` 已覆盖 montage-owned float curves。
7. referenced sequence curves 仍属于对应 `AnimSequence` AssetDocument，不塞进 AnimMontage sidecar。

这条路线能最大化复用当前 capability/profile/test 架构，同时真实评估一个结构化 asset 增加 region 时的成本。

## 关键依据

- `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp:877`：当前 canonical body keys。
- `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp:289`：当前 body shape validation。
- `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp:919`：当前 schema hint。
- `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp:984`：当前 apply 主流程。
- `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp:1066`：当前 extract 主流程。
- `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentProfile.cpp:56`：当前 template。
- `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentProfile.cpp:101`：当前 region policies。
- `E:/Epic Games/UE_5.7/Engine/Source/Runtime/Engine/Classes/Animation/AnimMontage.h:621`：`UAnimMontage` 定义。
- `E:/Epic Games/UE_5.7/Engine/Source/Runtime/Engine/Public/Animation/AnimTypes.h:481`：`FAnimSyncMarker`。
- `E:/Epic Games/UE_5.7/Engine/Source/Runtime/Engine/Public/Animation/AnimTypes.h:637`：`FMarkerSyncData`。
- `E:/Epic Games/UE_5.7/Engine/Source/Runtime/Engine/Classes/Animation/TimeStretchCurve.h:196`：`FTimeStretchCurve`。
- `E:/Epic Games/UE_5.7/Engine/Source/Runtime/Engine/Classes/Animation/AnimSequenceBase.h`：Montage 继承的 animation curve/data model surface。
