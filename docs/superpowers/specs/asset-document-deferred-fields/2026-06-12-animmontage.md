# AnimMontage AssetDocument Deferred Fields

日期：2026-06-12

对应 spec：

- `docs/superpowers/specs/2026-06-12-asset-document-structured-capabilities-animmontage-design.md`

用途：

```text
记录 UAnimMontage AssetDocument Body 第一版暂不支持、仅 inspect 或 extract skipped 的字段。
后续 task/review 应优先从这里清理，不依赖正文里的临时备注。
```

---

## 1. 维护规则

每当 spec 或实现中出现以下情况，必须更新本文件：

- 字段暂不支持；
- 字段仅 inspect，不 apply；
- 字段 extract 时 skipped；
- 字段 diff 时 unsupported；
- 字段需要后续专门 body adapter 或 capability 才能表达。

每个条目需要包含：

- AssetDocument path；
- UE 结构或概念；
- 当前处理方式；
- deferred 原因；
- 清理条件。

已清理：

- 2026-06-15：`Body.Metadata` / `Body.SectionMetadata` 已支持 `EmbeddedObject` / `DefinitionRef` authoring 与 extract。v1 语义是 region-level replacement；`Body.Metadata` 出现时替换整个 asset `MetaData` array，不做 element-level merge，也不保留同一 array 内非 AssetDocument-managed metadata；`Body.SectionMetadata` 出现时替换整个 section metadata region，未列出的 section metadata 会被清空。

---

## 2. Deferred Fields

### 2.1 BranchingPoints

AssetDocument path：

```text
/Body/BranchingPoints
```

UE 结构：

- `FBranchingPoint`
- `FBranchingPointMarker`

当前处理方式：

- validate：如果 document 中出现该字段，返回 unsupported；
- apply：不写入；
- extract：不输出，返回 skipped entry；
- diff：返回 unsupported。

deferred 原因：

- UE 已经把旧 branching point 结构标记为 deprecated/迁移路径；
- 与 notify marker、timeline event、runtime trigger 行为强相关；
- 第一版先验证 slot/segment/section/notify object 创建，不把 branching point 作为阻塞项。

清理条件：

- 明确 UE 5.7 推荐的 branching point 或等价 notify marker authoring API；
- 能在 automation 中验证触发点保存、重开、extract 稳定；
- 能和 `Notifies` / `NotifyStates` 的 timeline 表达保持一致。

### 2.2 MarkerSync

AssetDocument path：

```text
/Body/Sync
/Body/Markers
/Body/SyncMarkers
```

UE 结构：

- `UAnimMontage::SyncGroup`
- `UAnimMontage::SyncSlotIndex`
- `FMarkerSyncData`
- `FAnimSyncMarker`

当前处理方式：

- `Body.Sync` 本轮已支持窄范围 authoring scalar：
  - `SyncGroup`
  - `SyncSlotIndex`
- validate/apply/extract 均覆盖 `Body.Sync`；
- `MarkerData.AuthoredSyncMarkers`、`Body.Markers`、`Body.SyncMarkers` 仍不作为 authoring region 输出。

仍 deferred 的原因：

- `MarkerData.AuthoredSyncMarkers` 会由 `UAnimMontage::CollectMarkers()` 基于 `SyncGroup`、`SyncSlotIndex` 和 referenced `UAnimSequence::AuthoredSyncMarkers` 收集，更像 derived montage marker evidence，不适合作为第一批直接 authoring data；
- referenced sequence markers 应由对应 `AnimSequence` AssetDocument 管理。

后续清理条件：

- `MarkerData` 仅作为 extract evidence 或 diagnostic，除非后续明确要支持 montage-owned authored markers；
- 如需 authoring markers，先定义 montage-owned marker 与 referenced sequence marker 的所有权边界。

### 2.3 RootMotionAdvancedSettings

AssetDocument path：

```text
/Body/RootMotion
```

UE 结构：

- montage root motion 相关字段；
- 可能部分字段可通过 `Properties` 表达。

当前处理方式：

- `Body.RootMotion` 本轮已支持 montage-side legacy scalar：
  - `bEnableRootMotionTranslation`
  - `bEnableRootMotionRotation`
  - `RootMotionRootLock`
- validate/apply/extract 均覆盖上述字段；
- `Body.RootMotion` 仍不表示完整 root motion 数据。

仍 deferred 的原因：

- `Body.RootMotion` 应解释为 montage-side legacy settings，不表示完整 root motion 数据；
- referenced sequence root motion settings 属于 `AnimSequence` sidecar，不应由 AnimMontage region 直接管理。

后续清理条件：

- 文档明确 sequence-owned root motion 不在 AnimMontage sidecar 范围内。

### 2.4 CurvesAndTimeStretch

AssetDocument path：

```text
/Body/Curves
/Body/TimeStretch
```

UE 结构：

- inherited `UAnimSequenceBase::RawCurveData`
- inherited animation data model curve data
- `UAnimMontage::TimeStretchCurve`
- `UAnimMontage::TimeStretchCurveName`

当前处理方式：

- document 中出现时 validate 失败；
- apply 不写入；
- extract 不输出语义化 block。

deferred 原因：

- `UAnimMontage` 通过 `UAnimSequenceBase` 继承了自己的 curve storage，runtime 会使用 `RawCurveData`；
- referenced `SlotAnimTracks[].AnimTrack.AnimSegments[].AnimReference` 上的 curves 属于被引用 `AnimSequence` 或 `AnimSequenceBase`，不属于 Montage sidecar owned region；
- `TimeStretchCurve` 的 `Markers` 和 `Sum_dT_i_by_C_i` 是 baked/cached output，应该由 UE 根据 source float curve 和 `TimeStretchCurveName` 生成，不应由 sidecar 手写。

清理条件：

- `Body.Curves` 明确只覆盖 montage-owned inherited curves；
- `Body.TimeStretch` 明确只覆盖 time stretch settings 和 source curve reference，不维护 baked markers；
- 自动化测试能证明修改 Montage 曲线不会修改 referenced sequence 曲线。

### 2.5 MontageEditorUILayout

AssetDocument path：

```text
/Body/Editor
```

UE 结构：

- montage editor UI 展开状态、轨道显示、editor-only layout。

当前处理方式：

- 不 inspect；
- 不 apply；
- 不 extract。

deferred 原因：

- 不是资产语义核心；
- 可能是 editor preference / transient / per-user state；
- 不适合作为第一版 AssetDocument 内容。

清理条件：

- 只有在明确存在可保存、可迁移、对协作有价值的 editor-only 字段时才重新评估。

---

## 3. 非 Deferred 项

以下内容不在本 deferred 文件中追踪，因为第一版已经纳入范围：

- `/Body/Notifies`
- `/Body/NotifyStates`

边界：

- 支持在 Montage timeline 内创建 `UAnimNotify` / `UAnimNotifyState` 实例；
- 支持 `Object.Kind=EmbeddedObject` 或 `Object.Kind=DefinitionRef`；
- 支持 `Object.Class` 完整 class path；
- 支持通过 `Object.Properties` 和通用 property setter 设置对象字段；
- 不实现独立 `AnimNotify` / `AnimNotifyState` capability；
- 不理解具体 AN/ANS class 的领域语义。

但这两个字段需要保留后续组合边界：

```text
AnimMontageCapability
  -> AnimMontageNotifyTimelinePlacementAdapter
    -> AssetDocumentFragmentCompiler
      -> EmbeddedObjectFragmentAdapter
      -> DefinitionRefFragmentAdapter
```

第一版实现时应避免把 notify 逻辑直接散落在 Montage adapter 主流程中。至少要保留以下边界：

- timeline placement：由 Montage notify timeline adapter 负责；
- object creation：由 fragment compiler 和 fragment adapter registry 负责；
- class/properties schema：必须可被未来 AN/ANS capability 复用；
- domain semantics：暂不实现，未来通过独立 AN/ANS capability 接入，但不建立 capability-to-capability bridge。

后续清理条件：

- 当独立 `AnimNotify` / `AnimNotifyState` capability 出现时，Montage 内嵌 notify 表达不应破坏；
- 如果 richer schema 增加了 AN/ANS 专属字段，旧的 `Object.Kind=EmbeddedObject` + `Object.Class` + `Object.Properties` 仍应能作为基础子集；
- AN/ANS capability 应复用 fragment schema；Montage 侧只负责 timeline placement。
