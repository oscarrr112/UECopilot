# AssetDocument Timeline Placement Region Adapter Design

日期：2026-07-01

状态：已实现

适用分支：`feature/asset-document-timeline-placement-region-adapter`

实现状态摘要：

- 已新增 public-region runtime 内部公共层 `FAssetDocumentTimelinePlacementRegionAdapter`、`FAssetDocumentTimelinePlacementHooks`、`FAssetDocumentTimelineTrackResolver` 和 `FAssetDocumentTimelinePlacementUtils`。
- AnimSequence `SyncMarkers`、`Notifies`、`NotifyStates` 已迁移到 timeline placement utility / adapter 管线；profile-specific materialization、repair、track resolver 和 unmanaged notify preservation 仍保留在 AnimSequence hook 中。
- AnimMontage `Notifies`、`NotifyStates` 已通过 `FAnimMontageNotifyPlacementAdapter` bridge 复用 timeline placement utility；Montage-specific managed notify materialization、preservation 和 writeback 没有进入公共 adapter。
- `CompositeSections` 第一版仅保持 utility 复用边界；`SlotAnimTracks` / nested `AnimSegments` 仍按本 spec 延期，没有迁移到 full timeline adapter。
- 本实现不改变 public Body schema、`BodySections` 或 `RegionPolicies` 语义。

## 1. 背景

AssetDocument public region runtime 已经把 object field、preview-apply-diff、identity array、graph wrapper、fragment array 等重复逻辑逐步抽到公共层。下一类明显重复的是 timeline placement region。

这里的 timeline placement 不是一个具体 UE 类型，而是一组稳定的结构特征：

- Body 中存在一个 placement array。
- array entry 是 JSON object。
- 每个 entry 有 timeline position，例如 `Time`、`LinkableTime`、`StartPos`。
- 部分 entry 有 duration 或 end time，例如 `Duration`、`AnimEndTime`。
- 部分 entry 有 human-readable identity，例如 `Name`、`SectionName`。
- 部分 entry 有 track identity，例如 `TrackName`、`TrackIndex`、slot/track name。
- validate 阶段需要统一处理 numeric shape、finite/non-negative/range、duplicate key、track existence。
- apply 阶段需要先得到 profile-specific staged result，再由 profile 调 UE API 或写回 UE struct。
- extract/diff 阶段需要稳定的 entry path 和 canonical comparison。

当前这些逻辑分散在多个 profile 中：

- AnimSequence `Notifies`
- AnimSequence `NotifyStates`
- AnimSequence `SyncMarkers`
- AnimMontage `Notifies`
- AnimMontage `NotifyStates`
- AnimMontage `CompositeSections`
- AnimMontage `SlotAnimTracks` / `AnimSegments`

其中 AnimMontage 已经有 profile-private `FAnimMontageNotifyPlacementAdapter`，但它仍然直接处理 Montage-specific notify materialization、managed notify naming、`FAnimNotifyEvent` 构造和 Montage extraction。AnimSequence 侧的 notify、notify state、sync marker parsing 仍在 `AnimSequenceAssetDocumentCapability.cpp` 内部展开。

本 spec 的目标不是把所有 timeline 对象强行统一成同一种数据模型，而是把“placement 生命周期的公共部分”抽成可组合 runtime，让 profile 通过 hooks 保留 UE 类型和语义。

## 2. 设计结论

第一版新增一个 composition-first 的公共 region adapter：

- `FAssetDocumentTimelinePlacementRegionAdapter`
- `FAssetDocumentTimelinePlacementRegionConfig`
- `FAssetDocumentTimelinePlacementHooks`
- `FAssetDocumentTimelineTrackResolver`
- `FAssetDocumentTimelinePlacementUtils`

adapter 负责 timeline placement 的通用 JSON/lifecycle 规则；profile 负责具体 UE materialization、repair、writeback 和 semantic fields。

第一版优先迁移：

- AnimSequence `Notifies`
- AnimSequence `NotifyStates`
- AnimSequence `SyncMarkers`
- AnimMontage `Notifies`
- AnimMontage `NotifyStates`

第一版不迁移，但允许复用 utility：

- AnimMontage `CompositeSections`

第一版明确延期：

- AnimMontage `SlotAnimTracks`
- AnimMontage nested `AnimSegments`

延期原因是 `SlotAnimTracks` 不是普通 flat placement array。它包含 slot track、anim track、segment、asset reference、loop/playrate/end-time 等多层语义，直接塞进第一版 timeline adapter 会让公共层过早理解 Montage segment 模型。

## 3. 目标

1. 统一 timeline placement array 的 JSON shape validation。
2. 统一 time/duration/name/track identity 的读取、path、diagnostic 和 duplicate-key 构造。
3. 统一 finite number、non-negative、duration positive、end time range 的校验入口。
4. 统一 track existence 和 track alias ambiguity 的 resolver 接口。
5. 统一 validate/preflight/apply/extract/diff 生命周期的 region adapter 入口。
6. 保持 profile-specific hooks 拥有 UE object/struct materialization 的全部控制权。
7. 保持现有 public AssetDocument body schema 不变。
8. 保持现有可观察 diagnostic code 和关键 path 稳定；确需改变 path 时必须有专门测试和迁移说明。
9. 让 AnimSequence 与 AnimMontage notify placement 共享同一套 duplicate key、numeric validation 和 track validation 基础设施。
10. 为后续 `CompositeSections`、slot/segment adapter 留出可组合入口，但第一版不把它们强行纳入。

## 4. 非目标

本 spec 明确不做：

- 不把 notify、notify state、sync marker、section、segment 的全部字段统一成一种对象。
- 不在公共 adapter 内 include 或调用 `UAnimSequence` / `UAnimMontage` API。
- 不让公共 adapter 知道 `FAnimNotifyEvent`、`FAnimSyncMarker`、`FCompositeSection`、`FSlotAnimationTrack`、`FAnimSegment`。
- 不让公共 adapter 创建、rename、attach、remove、duplicate 任何 UObject。
- 不把 `FAssetDocumentFragmentCompiler` 合并进 timeline adapter。
- 不改变 `EmbeddedObject`、`DefinitionRef`、`AssetRef` 等 fragment/object schema。
- 不改变 managed notify / notify-state naming prefix。
- 不改变 unmanaged notify preservation 语义。
- 不改变 apply 后 profile 需要执行的 cache repair、marker refresh、section linking、track rebuild。
- 不把 `SlotAnimTracks` / `AnimSegments` 作为第一版迁移对象。
- 不新增 public MCP schema 字段。

## 5. 现状梳理

### 5.1 AnimSequence

AnimSequence 当前有四类 timeline-adjacent region：

- `NotifyTracks`
- `Notifies`
- `NotifyStates`
- `SyncMarkers`

`NotifyTracks` 已经迁移到 `FAssetDocumentNamedArrayRegionAdapter`，不是本 spec 的主要迁移对象。Timeline placement adapter 需要消费 `NotifyTracks` 的解析结果，用它来校验 `TrackName`，但不替代 named array adapter。

`Notifies` 和 `NotifyStates` 当前重复处理：

- `/Body/Notifies` 和 `/Body/NotifyStates` 必须是 array。
- entry 必须是 object。
- `Time` 必须是 finite non-negative number，并且在 sequence timeline 范围内。
- `NotifyStates.Duration` 必须是 positive number。
- `TrackName` 可选，但如果出现必须能解析到现有 notify track。
- track alias 不能歧义。
- duplicate key 需要稳定识别。
- class/object fragment validation 与 timeline placement 混在同一段 parse 逻辑里。

`SyncMarkers` 当前重复处理：

- `/Body/SyncMarkers` 必须是 array。
- entry 必须是 object。
- `Name` 必须是 non-empty string。
- `Time` 必须通过 timeline range validation。
- duplicate key 由 `Name` 和 normalized `Time` 组成。
- extract 时按 `Time`、`Name` 排序以保证稳定输出。
- apply 后必须调用 marker cache refresh。

### 5.2 AnimMontage

AnimMontage 当前有四类 timeline-adjacent region：

- `Notifies`
- `NotifyStates`
- `CompositeSections`
- `SlotAnimTracks` / `AnimSegments`

`Notifies` 和 `NotifyStates` 已经集中在 profile-private `FAnimMontageNotifyPlacementAdapter`。它比 AnimSequence 更接近目标形态，但仍然同时承担两类职责：

- timeline placement 通用职责：array/object shape、time/duration、duplicate key、diagnostic path。
- Montage-specific 职责：notify object materialization、managed notify identification、`FAnimNotifyEvent` 构造、extract fragment JSON。

第一版应该把前者迁到公共 timeline placement adapter，把后者留在 AnimMontage hooks。

`CompositeSections` 与 timeline placement 有交集：

- section 有 `SectionName` identity。
- section 有 optional `LinkableTime`。
- section 有 `NextSectionName` reference validation。
- duplicate section name 需要稳定诊断。

但是 section linking、section order、default next-section 关系是 Montage-specific。第一版可以让它复用 `FAssetDocumentTimelinePlacementUtils` 中的 numeric/name/duplicate helper，但不要求完整迁移为 region adapter。

`SlotAnimTracks` / `AnimSegments` 第一版延期。它是 nested track + segment 模型，涉及 `AnimReference`、segment time range、play rate、loop count、track writeback，适合后续单独设计 `TimelineSegmentRegionAdapter` 或 track/segment wrapper。

## 6. 公共 API 设计

公共实现建议放在 private runtime 区域：

- `Source/AssetDocument/Private/Regions/AssetDocumentTimelinePlacementRegionAdapter.h`
- `Source/AssetDocument/Private/Regions/AssetDocumentTimelinePlacementRegionAdapter.cpp`
- `Source/AssetDocument/Private/Regions/AssetDocumentTimelinePlacementUtils.h`
- `Source/AssetDocument/Private/Regions/AssetDocumentTimelinePlacementUtils.cpp`

第一版不要求暴露到 `Source/AssetDocument/Public`。它是 AssetDocument 内部 public-region runtime 的公共层，而不是插件外部 API。

### 6.1 `FAssetDocumentTimelinePlacementRegionConfig`

config 描述 region 的 JSON 形态和公共 validation 规则。

建议字段：

```cpp
struct FAssetDocumentTimelinePlacementRegionConfig
{
	FName RegionName;
	FString BodyKey;
	FString EntryDebugName;

	FString TimeFieldName;
	bool bRequireTime = true;
	bool bAllowZeroTime = true;

	FString DurationFieldName;
	bool bHasDuration = false;
	bool bRequireDuration = false;
	bool bRequirePositiveDuration = true;
	bool bValidateEndTime = false;

	FString NameFieldName;
	bool bHasName = false;
	bool bRequireName = false;

	FString TrackNameFieldName;
	FString TrackIndexFieldName;
	bool bHasTrackIdentity = false;
	bool bRequireTrackIdentity = false;

	bool bExplicitEmptyMeansClear = true;
	bool bPreserveProfileDiagnosticPaths = true;
};
```

字段名是配置，不是硬编码 switch。profile 可以为 `Notifies`、`NotifyStates`、`SyncMarkers` 各自声明配置。

### 6.2 `FAssetDocumentTimelinePlacementEntry`

adapter 输出 profile-agnostic 的 staged placement entry。

建议字段：

```cpp
struct FAssetDocumentTimelinePlacementEntry
{
	int32 Index = INDEX_NONE;
	FString EntryPath;
	TSharedPtr<FJsonObject> EntryObject;

	TOptional<double> Time;
	TOptional<double> Duration;
	TOptional<FString> Name;
	TOptional<FString> TrackName;
	TOptional<int32> TrackIndex;

	FString DuplicateKey;
};
```

`EntryObject` 保留原始 object，让 hooks 可以读取 notify object fragment、sync marker custom field、Montage section field 等 profile-specific 内容。

### 6.3 `FAssetDocumentTimelineTrackResolver`

track resolver 是组合接口，不是继承基类。建议用 function refs / delegates 组成：

```cpp
struct FAssetDocumentTimelineTrackResolveRequest
{
	FString RegionName;
	int32 EntryIndex = INDEX_NONE;
	FString EntryPath;
	TOptional<FString> TrackName;
	TOptional<int32> TrackIndex;
};

struct FAssetDocumentTimelineTrackResolveResult
{
	bool bResolved = false;
	int32 TrackIndex = INDEX_NONE;
	FString CanonicalTrackName;
	FAssetDocumentCapabilityResult Error;
};

struct FAssetDocumentTimelineTrackResolver
{
	TFunction<FAssetDocumentTimelineTrackResolveResult(const FAssetDocumentTimelineTrackResolveRequest&)> Resolve;
};
```

AnimSequence 的 resolver 消费已解析的 `NotifyTracks`，处理 alias 和 ambiguity。AnimMontage notify placement 第一版可以不声明 track resolver，继续只接受 profile 现有的 `TrackIndex` 语义；如果未来 Montage 引入 track name，再加 resolver。

### 6.4 `FAssetDocumentTimelinePlacementHooks`

hooks 负责 profile-specific 语义。adapter 只在合适生命周期调用 hooks。

建议字段：

```cpp
struct FAssetDocumentTimelinePlacementHooks
{
	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentTimelinePlacementEntry& Entry)> ValidateSemanticEntry;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentTimelinePlacementEntry& Entry)> PreflightSemanticEntry;

	TFunction<FAssetDocumentCapabilityResult(
		const TArray<FAssetDocumentTimelinePlacementEntry>& Entries)> ApplyEntries;

	TFunction<FAssetDocumentCapabilityResult(
		TArray<TSharedPtr<FJsonValue>>& OutEntries)> ExtractEntries;

	TFunction<FAssetDocumentCapabilityResult()> PostApplyRepair;

	TFunction<FString(
		const FAssetDocumentTimelinePlacementEntry& Entry)> BuildDuplicateKey;
};
```

这些 hooks 必须是组合注入，不能要求 profile 继承一个 base adapter。原因：

- profile 已经有自己的 capability、compiler、parsed body、repair 流程。
- 不同 region 的 materialization 依赖不同临时状态。
- 第一版需要让 AnimSequence 和 AnimMontage 逐步迁移，不应该用继承层级绑死生命周期。

### 6.5 `FAssetDocumentTimelinePlacementRegionAdapter`

adapter 实现 `IAssetDocumentRegionAdapter`。

建议职责：

- `SupportsRegion`
  - 基于 `RegionName` / `BodyKey` 判断。
- `Validate`
  - 如果 Body 不包含 region，按 optional region 处理。
  - 如果包含 region，要求 value 是 array。
  - 要求每个 entry 是 object。
  - 读取配置声明的 time/duration/name/track fields。
  - 调 `FAssetDocumentTimelinePlacementUtils` 做 numeric/name/path/duplicate validation。
  - 调 track resolver。
  - 调 `ValidateSemanticEntry`。
- `Preflight`
  - 复用 parse 后 staged entries。
  - 调 `PreflightSemanticEntry`，例如 fragment compiler preflight。
- `Apply`
  - 只把 staged entries 交给 `ApplyEntries`。
  - 调 `PostApplyRepair`。
  - 不直接写 UE asset。
- `Extract`
  - 调 `ExtractEntries`。
  - 包装成 region array。
- `Diff`
  - 第一版可沿用 existing preview-apply-diff 或 canonical JSON compare。
  - 不引入 identity diff，除非 profile 已有稳定 identity key 并明确声明。

## 7. Utility 设计

`FAssetDocumentTimelinePlacementUtils` 放置纯函数，供 adapter 和尚未完整迁移的 profile 复用。

建议函数：

- `RequirePlacementArray`
- `RequirePlacementObject`
- `ReadFiniteNumberField`
- `ReadNonNegativeTimeField`
- `ReadPositiveDurationField`
- `ValidateTimelineRange`
- `ValidateTimelineEndRange`
- `ReadRequiredNameField`
- `ReadOptionalTrackNameField`
- `ReadOptionalTrackIndexField`
- `BuildEntryPath`
- `BuildFieldPath`
- `EscapeJsonPointerSegment`
- `BuildDuplicateKey`
- `DetectDuplicateKeys`
- `CanonicalizeTimeForKey`

utility 不应包含 UE animation 类型。timeline range 通过配置或 hook 提供：

```cpp
struct FAssetDocumentTimelineRange
{
	double MinTime = 0.0;
	double MaxTime = 0.0;
	bool bHasMaxTime = false;
};
```

AnimSequence 用 sequence play length 提供 range。AnimMontage 用 montage length 或现有 profile rule 提供 range。

## 8. Diagnostic 与 path 兼容

第一版必须优先保持现有 profile 的可观察诊断稳定。

要求：

1. diagnostic code 不因为迁移公共 adapter 而改名。
2. public body path 默认保持现有测试期望。
3. 对已经存在差异的 path style，adapter 必须允许 profile 配置 path builder。

当前需要特别注意：

- AnimSequence 通常使用 `/Body/Notifies/0/Time` 形态。
- 现有 AnimMontage notify placement helper 存在 `/Body/Notifies[0]/Time` 形态。

第一版不强制把两边 path 统一。公共 adapter 可以内部使用统一构造函数，但 profile 必须能够选择 legacy-compatible entry path。任何 path 迁移都必须作为独立行为变化进入 implementation plan，并添加 focused test。

## 9. Duplicate key 规则

adapter 只提供 duplicate-key 管线，不硬编码所有 key 组成。

建议默认 key pieces：

- `Name`，如果 config 声明 `bHasName`。
- canonicalized `Time`，如果 config 声明 `bRequireTime`。
- `Duration`，如果 config 声明 duration 参与 identity。
- resolved `TrackIndex` 或 canonical `TrackName`，如果 config 声明 track identity 参与 identity。
- hook-supplied semantic key，例如 notify class/object role。

各 region 第一版建议：

- AnimSequence `Notifies`：沿用现有 duplicate key 语义，由 hook 追加 notify semantic identity。
- AnimSequence `NotifyStates`：沿用现有 duplicate key 语义，并包含 duration/semantic identity。
- AnimSequence `SyncMarkers`：`Name + canonical Time`。
- AnimMontage `Notifies`：沿用现有 managed notify placement key。
- AnimMontage `NotifyStates`：沿用现有 managed notify-state placement key。

任何 duplicate key 语义变化都必须先由测试证明不会破坏现有 authoring 行为。

## 10. Lifecycle 细节

### 10.1 Validate

validate 只做无副作用检查：

1. Body key 是否存在。
2. value 是否 array。
3. entry 是否 object。
4. configured fields 是否存在且类型正确。
5. numeric 是否 finite。
6. time/duration 是否满足 non-negative、positive、range。
7. track identity 是否可解析。
8. duplicate key 是否重复。
9. semantic hook 是否通过。

validate 不得 materialize UObject，不得修改 asset，不得刷新 cache。

### 10.2 Preflight

preflight 可以做昂贵但可回滚的 semantic 准备，例如：

- fragment reference resolve。
- embedded object schema preflight。
- notify class compatibility check。

preflight 不得把对象 attach 到真实 asset，也不得产生不可回滚 mutation。

### 10.3 Apply

apply 的公共层流程：

1. 重用 validate/preflight 产物或重新 parse。
2. 把 staged entries 交给 `ApplyEntries`。
3. profile 构造 UE struct/object 并写回 asset。
4. profile 执行 `PostApplyRepair`。

`PostApplyRepair` 是显式 hook，不能隐藏在 adapter 内部。

AnimSequence 典型 repair：

- notify trigger offset refresh。
- notify state end trigger offset refresh。
- `RefreshSyncMarkerDataFromAuthored`。
- track remap 或 unmanaged notify preservation。

AnimMontage 典型 repair：

- managed/unmanaged notify merge。
- notify state duration/end offset refresh。
- section linking 或 slot/track rebuild，如果相关 region 未来迁移。

### 10.4 Extract

extract 由 profile hook 产生 JSON entries。adapter 只负责：

- 包装成 region array。
- 应用稳定排序选项，如果 config 声明。
- 使用统一 field/path helper 生成一致结构。

SyncMarkers 应保持按 `Time`、`Name` 排序。

### 10.5 Diff

第一版 diff 策略保持保守：

- 已经走 preview-apply-diff 的 profile 继续沿用。
- 可以用 canonical JSON compare 的 region 由 config 显式声明。
- 不在 timeline adapter 第一版引入 placement identity diff。

identity diff 可以作为后续增强，但必须先证明 duplicate key 语义足够稳定。

## 11. Profile 迁移方案

### 11.1 AnimSequence `Notifies`

迁移后：

- `ParseAnimSequenceNotifies` 中的 array/object/time/duration/track/duplicate 公共逻辑移入 timeline adapter。
- notify object fragment validation 留在 `ValidateSemanticEntry` / `PreflightSemanticEntry` hook。
- `FAnimNotifyEvent` 构造留在 AnimSequence profile。
- unmanaged notify preservation、track remap、offset refresh 留在 AnimSequence profile。

完成后，AnimSequence capability 不应继续复制 timeline field parsing helper。

### 11.2 AnimSequence `NotifyStates`

与 `Notifies` 共用同一个 adapter 类型，但使用不同 config：

- `bHasDuration = true`
- `bRequireDuration = true`
- `bRequirePositiveDuration = true`
- `bValidateEndTime = true`

notify state object materialization 留在 hook。

### 11.3 AnimSequence `SyncMarkers`

`SyncMarkers` 使用 timeline adapter 的轻量配置：

- `bHasName = true`
- `bRequireName = true`
- no duration
- no track identity
- duplicate key 为 `Name + canonical Time`

profile hook 只负责 staged marker 写回和 `RefreshSyncMarkerDataFromAuthored`。

### 11.4 AnimMontage `Notifies`

`FAnimMontageNotifyPlacementAdapter` 不应直接消失。第一版推荐把它降级为 Montage profile hook/bridge：

- 公共 adapter 接管 placement array parse/validate。
- Montage bridge 继续处理 notify object fragment、managed name、`FAnimNotifyEvent` 构造。
- `IsManagedNotifyEvent` 保留在 Montage-specific helper 中。
- existing extract 行为保持。

这样可以降低迁移风险，并保持 tests 对 managed/unmanaged notify 的覆盖价值。

### 11.5 AnimMontage `NotifyStates`

同 `Notifies`，但 config 声明 duration。Montage bridge 继续处理 notify state object fragment 和 duration 写入。

### 11.6 AnimMontage `CompositeSections`

第一版只允许复用 utility：

- section name required string。
- duplicate section name helper。
- optional `LinkableTime` numeric validation。
- `NextSectionName` reference validation 可以留在 profile。

不要求把 `CompositeSections` 变成 full `FAssetDocumentTimelinePlacementRegionAdapter` region。后续若 `CompositeSections` 也出现更多重复 lifecycle，再写小 spec 迁移。

### 11.7 AnimMontage `SlotAnimTracks` / `AnimSegments`

第一版不处理。后续应该单独评估：

- track region adapter 是否需要表达 nested track array。
- segment placement 是否需要单独 `TimelineSegmentRegionAdapter`。
- asset reference materialization 是否应与 source-file/import adapter 结合。

## 12. 与现有公共层的关系

Timeline placement adapter 应与已有公共层组合，而不是替代它们：

- `FAssetDocumentBodyCapabilityBase`
  - 负责 Body dispatch、known keys、cross-region validation。
- `FAssetDocumentNamedArrayRegionAdapter`
  - 继续负责 `NotifyTracks` 这类 named array。
- `FAssetDocumentFragmentArrayRegionAdapter`
  - 继续负责 metadata/user-data fragment arrays。
- `FAssetDocumentPreviewApplyDiffRegionAdapter`
  - timeline region 可继续通过 preview apply diff 生成行为 diff。
- `FAssetDocumentJsonRegionUtils`
  - 若已有函数覆盖 JSON shape/path/diagnostic，应优先复用，不重复造工具。

Timeline adapter 的边界是 placement 生命周期，不是整个 animation asset lifecycle。

## 13. 测试要求

### 13.1 Runtime adapter unit tests

新增 focused tests，至少覆盖：

- missing optional region 不报错。
- region value 非 array 报稳定 diagnostic。
- entry 非 object 报稳定 diagnostic。
- missing required time。
- non-finite time。
- negative time。
- out-of-range time。
- missing/invalid/zero-or-negative duration。
- duration end out of range。
- missing required name。
- duplicate key。
- unknown track。
- ambiguous track alias。
- semantic validation hook failure path 透传。
- explicit empty array 表示 clear。
- extract 输出 array 且排序稳定。

### 13.2 AnimSequence regression tests

保持并扩展现有 automation：

- notifies apply 成功。
- notify states apply 成功。
- sync markers apply 成功并刷新 authored marker cache。
- invalid notify time 不 mutation。
- invalid notify-state duration 不 mutation。
- duplicate notify key 不 mutation。
- duplicate sync marker key 不 mutation。
- unknown notify track 不 mutation。
- ambiguous track alias 不 mutation。
- embedded object preflight failure 不 mutation。
- unmanaged notify preservation 和 track remap 保持。

### 13.3 AnimMontage regression tests

保持并扩展现有 automation：

- managed notifies apply/extract 成功。
- managed notify states apply/extract 成功。
- unmanaged notify preservation 保持。
- invalid time/duration diagnostic code 和 path 保持。
- duplicate placement diagnostic 保持。
- embedded object / definition ref materialization 保持。
- `GetProfileCapabilities` 或 equivalent internal adapter list 更新为新 adapter/bridge 名称。

### 13.4 Verification

实现完成前至少执行：

1. `git diff --check`
2. UBT Development build
3. AssetDocument automation tests

如果在独立 worktree 实现，必须使用临时 host project + plugin junction 验证，不能用主项目同名插件结果判断 worktree 改动。

## 14. 成功标准

本 spec 完成后的成功标准：

1. Timeline placement 的 array/object/numeric/duplicate/track validation 不再分别散落在 AnimSequence 与 AnimMontage profile 中。
2. AnimSequence `Notifies`、`NotifyStates`、`SyncMarkers` 使用同一个公共 adapter 或同一套 public timeline utility。
3. AnimMontage `Notifies`、`NotifyStates` 的 profile-private adapter 被收敛为 hook/bridge，不再拥有公共 timeline parsing 主逻辑。
4. Apply 后 repair hook 是显式生命周期，不隐藏在公共 adapter 内。
5. 现有 public JSON schema、managed naming、diagnostic code 和重要 path 保持兼容。
6. `CompositeSections` 和 `SlotAnimTracks` 的延期边界在文档和代码注释中清晰可见。
7. 新增测试能证明公共 adapter 不依赖具体 UE animation 类型。

## 15. 实施顺序建议

后续 implementation plan 应拆成以下 tasks：

1. 新增 timeline placement utility 和 focused tests。
2. 新增 `FAssetDocumentTimelinePlacementRegionAdapter`、config、hooks、track resolver 和 adapter tests。
3. 迁移 AnimSequence `SyncMarkers`，验证 marker cache repair hook。
4. 迁移 AnimSequence `Notifies` / `NotifyStates`，验证 track resolver 和 fragment hook。
5. 改造 `FAnimMontageNotifyPlacementAdapter` 为 timeline hook/bridge，迁移 Montage `Notifies` / `NotifyStates`。
6. 让 `CompositeSections` 只复用 utility，保留 full adapter 迁移延期说明。
7. 运行 full verification，并更新 refactor chain 状态。

这个顺序先迁移最简单的 marker placement，再迁移带 track/object fragment 的 notify placement，最后处理 Montage bridge，能把公共层风险和 profile 行为风险分开审。

## 16. 长期维护规则

这条规则不是本 spec 内部建议，而是长期约束。对应约束源必须同步维护：

- `E:\GameDev\PluginsWarehouse\AGENTS.md`
- `docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md`

新增或扩展 timeline-like region 时，先判断是否满足以下任一条件：

- 有 placement array。
- 有 time/duration/end-time numeric validation。
- 有 track identity 或 track existence validation。
- 有 duplicate placement key。
- 有 apply 后 repair hook。
- 有 extract/diff path 稳定性要求。

满足任一条件时，不应直接在 profile capability 内新增整套 parser。应优先使用：

- `FAssetDocumentTimelinePlacementRegionAdapter`
- `FAssetDocumentTimelinePlacementUtils`
- profile-specific hooks

只有当该 region 的核心语义是 nested track/segment、graph/tree、source import、或非 timeline lifecycle 时，才应另写更贴合的公共 adapter spec。

如果 implementation plan 决定暂不使用完整 timeline adapter，必须明确记录：

- 当前允许保守处理的 region 和范围。
- 为什么只复用 utility 或暂不复用公共层。
- 后续升级触发条件。
- 升级入口文件/类。
- 清理成功标准。
- 相关测试/验证要求。
