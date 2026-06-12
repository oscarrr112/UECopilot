# AnimMontage AssetDocument Deferred Fields

日期：2026-06-12

对应 spec：

- `docs/superpowers/specs/2026-06-12-asset-document-structured-capabilities-animmontage-design.md`

用途：

```text
记录 AnimMontage capability 第一版暂不支持、仅 inspect 或 extract skipped 的字段。
后续 task/review 应优先从这里清理，不依赖正文里的临时备注。
```

---

## 1. 维护规则

每当 spec 或实现中出现以下情况，必须更新本文件：

- 字段暂不支持；
- 字段仅 inspect，不 apply；
- 字段 extract 时 skipped；
- 字段 diff 时 unsupported；
- 字段需要后续专门 capability 才能表达。

每个条目需要包含：

- AssetDocument path；
- UE 结构或概念；
- 当前处理方式；
- deferred 原因；
- 清理条件。

---

## 2. Deferred Fields

### 2.1 BranchingPoints

AssetDocument path：

```text
/Capabilities/AnimMontage/BranchingPoints
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
/Capabilities/AnimMontage/Markers
/Capabilities/AnimMontage/SyncMarkers
```

UE 结构：

- animation sync marker / marker track 相关结构。

当前处理方式：

- inspect 可报告“未支持”；
- document 中出现时 validate 失败；
- extract skipped。

deferred 原因：

- marker sync 通常影响 runtime animation synchronization；
- 需要和 AnimSequence / AnimComposite 的 marker 表达统一；
- 不应只在 Montage adapter 内孤立实现。

清理条件：

- 先设计 animation marker 的通用 AssetDocument 表达；
- 至少覆盖 AnimSequenceBase 与 AnimMontage 的共同路径；
- 自动化测试能验证保存后 marker 数量、名称、时间稳定。

### 2.3 RootMotionAdvancedSettings

AssetDocument path：

```text
/Capabilities/AnimMontage/RootMotion
```

UE 结构：

- montage root motion 相关字段；
- 可能部分字段可通过 `Properties` 表达。

当前处理方式：

- 可反射字段继续走 `Properties`；
- 语义化 `RootMotion` block 暂不支持；
- extract 不输出语义化 block。

deferred 原因：

- 部分字段是普通 reflected properties，不需要急着进入 structured capability；
- root motion 的行为验证需要 runtime playback 或 animation eval 测试。

清理条件：

- 确认哪些字段必须用 structured block 而不是 `Properties`；
- 建立最小 runtime/editor 验证，避免只测序列化不测行为。

### 2.4 MetadataObjectAuthoring

AssetDocument path：

```text
/Capabilities/AnimMontage/Sections[*]/MetaData
/Capabilities/AnimMontage/MetaData
```

UE 结构：

- `UAnimMetaData`
- section metadata arrays。

当前处理方式：

- validate：document 中出现时 unsupported；
- apply：不创建；
- extract：skipped。

deferred 原因：

- metadata 是 instanced UObject，但它的语义和 notify object 不同；
- 需要确认 outer、duplication、editor display、asset save 稳定性；
- 可以复用第一版为 notify object 引入的 `AssetDocumentFragmentCompiler` 和 `EmbeddedObjectFragmentAdapter`，但不与 notify authoring 绑在同一个 task。

清理条件：

- `EmbeddedObjectFragmentAdapter` 已经通过 notify / notify state 验证；
- metadata class path + properties + array ordering 有稳定 schema；
- extract/diff 能 round-trip。

### 2.5 MontageEditorUILayout

AssetDocument path：

```text
/Capabilities/AnimMontage/Editor
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

- `/Capabilities/AnimMontage/Notifies`
- `/Capabilities/AnimMontage/NotifyStates`

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
