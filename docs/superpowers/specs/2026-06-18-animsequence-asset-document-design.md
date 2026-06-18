# AnimSequence AssetDocument Design

日期：2026-06-18

## 目标

本规格定义 `/Script/Engine.AnimSequence` 的 AssetDocument 扩展。目标不是新增动画导入能力，也不是扩展旧的 `FAnimSequenceGenerator`；目标是在动画已经由 UE 导入、重导入、录制或其他工具生成之后，用 `.assetdoc.json` 管理 AnimSequence 自己拥有的绝大多数可编辑作者面。

本工作只属于 AssetDocument：

- 新增 `AnimSequence` exact profile、capability、region policy、schema/profile inspection、tests、verification smoke 和报告。
- 继续使用通用 AssetDocument MCP tools，不新增 `create_anim_sequence`、`update_anim_sequence_region` 之类专用 MCP tool。
- 不扩展 `generate_assets`、旧 generator schema、导入器、FBX/Interchange/reimport pipeline。
- 不把 raw `.uasset` dump、raw bone track、compressed animation data 或导入源文件变成 agent-facing authoring format。

## 背景

AnimMontage benchmark 已证明 AssetDocument 可以覆盖结构化动画资产：`Profile + Capability + RegionPolicy + Body regions + fragment compiler + real smoke asset`。AnimSequence 是下一个合适 benchmark，因为它直接拥有 sequence-owned curves、notifies、sync markers、root motion settings、additive settings、compression settings 和 metadata，同时也暴露导入数据、压缩缓存、动画轨道等不应进入 sidecar 的边界。

旧 `AnimSequenceGenerator` 和 `MCP/schemas/AnimSequence.md` 只作为历史参考。它们证明过 minimal/patch 边界，但本规格不继续扩展旧 generator。新实现应把可复用经验迁移到 AssetDocument profile/capability，并保持 delta-first sidecar 方向。

## 非目标

- 不从 JSON 创建或替代完整动画导入结果。
- 不 author bone transform tracks、animated bone attributes、compressed data、derived sample count 或 compression cache。
- 不管理 `USkeleton`、`USkeletalMesh`、`UAnimSequence` 引用对象内部的数据。
- 不把 `AssetImportData`、source file path、reimport options 或 importer settings 作为 v1 managed region。
- 不新增 asset-specific MCP route 或 MCP tool。
- 不把旧 `FAnimSequenceGenerator` 作为本工作的一部分继续扩展。

## Authoring Surface Inventory

### Managed Authored Data

这些字段或概念属于导入后的 AnimSequence 作者面，应由 `Body` region 管理。

| Region | UE surface | 分类 | 处理方式 |
| --- | --- | --- | --- |
| `Body.References` | `UAnimationAsset::Skeleton`、`UAnimSequence::RetargetSource`、`UAnimSequence::RetargetSourceAsset` | referenced asset / retarget authoring | 管引用和 retarget source 配置，不管理 referenced asset 内容 |
| `Body.Preview` | `SetPreviewMesh()`、`PreviewSkeletalMesh`、`PreviewPoseAsset` | editor preview authoring | 管导入后预览配置 |
| `Body.Playback` | `UAnimSequenceBase::RateScale` | scalar authoring | 管 playback rate；play length 和 sampled keys 只作 evidence |
| `Body.Additive` | `AdditiveAnimType`、`RefPoseType`、`RefFrameIndex`、`RefPoseSeq` | sequence behavior authoring | 管 additive 设置和 base pose 引用 |
| `Body.RootMotion` | `bEnableRootMotion`、`RootMotionRootLock`、`bForceRootLock`、`bUseNormalizedRootMotionScale` | sequence behavior authoring | 管 root motion 设置，不 author root motion track data |
| `Body.Compression` | `CompressionErrorThresholdScale`、`BoneCompressionSettings`、`CurveCompressionSettings`、`VariableFrameStrippingSettings`、`bDoNotOverrideCompression` | post-import compression configuration | 管配置引用和 scalar 设置，不管压缩产物 |
| `Body.Curves` | animation data model float curves / curve flags / keys | sequence-owned timeline data | 管 sequence-owned float curve authoring |
| `Body.Notifies` | `UAnimSequenceBase::Notifies` point notifies | timeline data | 管 point events |
| `Body.NotifyStates` | `UAnimSequenceBase::Notifies` ranged notify states | timeline data | 管 ranged events |
| `Body.NotifyTracks` | `AnimNotifyTracks` | editor timeline layout / grouping | 管 notify track names/order；apply 后修复 notify track cache |
| `Body.SyncMarkers` | `UAnimSequence::AuthoredSyncMarkers` | sequence-owned marker timeline | 管 authored sync markers，刷新 derived marker cache |
| `Body.Metadata` | `UAnimationAsset::MetaData` | instanced object fragments | 用 fragment compiler 管 `UAnimMetaData` objects |
| `Body.AssetUserData` | `UAnimationAsset::AssetUserData` | instanced object fragments | 用 fragment compiler 管 `UAssetUserData` objects |

### Reflected Property Delta

普通 editable property 若不属于上面的 managed region，继续走 `Properties` default-diff 机制。实现中应避免把同一个 UE surface 同时放入 `Properties` 和 `Body` 管理；profile inspection 和 validation 需要暴露 managed surface，帮助 agent 避免重复声明。

### Referenced Asset-Owned Data

这些内容由被引用 asset 自己负责，不进入 AnimSequence sidecar：

- `USkeleton` bone tree、virtual bones、retarget sources、notifies cache。
- `USkeletalMesh` mesh data、materials、physics asset、preview mesh 内部配置。
- `RefPoseSeq` 指向的 sequence body。
- Notify class、metadata class、asset user data class 的 class definition。

### Derived / Cache / Import-Owned Data

这些字段可以作为 evidence 或 diagnostic 输出，但不作为 writable authoring field：

- `NumFrames`、`NumberOfKeys`、deprecated `SamplingFrameRate`。
- `NumberOfSampledKeys`、source frame count、target sampling frame rate、sequence play length。
- raw animation tracks、bone transform data、animated bone attributes。
- compressed animation data、compressed curve data、compression DDC/cache。
- `UniqueMarkerNames`、marker runtime cache、notify trigger cache。
- `AssetImportData` and source file metadata.
- thumbnail data and transient editor state.

## Document Shape

AnimSequence sidecar 使用通用 AssetDocument 结构：

```json
{
  "SchemaVersion": 1,
  "Target": "/Game/AssetDocumentSmoke/AS_PostImportSmoke",
  "Class": "/Script/Engine.AnimSequence",
  "Action": "Update",
  "Definitions": {},
  "Properties": {},
  "Body": {
    "References": {},
    "Preview": {},
    "Playback": {},
    "Additive": {},
    "RootMotion": {},
    "Compression": {},
    "Curves": [],
    "Notifies": [],
    "NotifyStates": [],
    "NotifyTracks": [],
    "SyncMarkers": [],
    "Metadata": [],
    "AssetUserData": []
  }
}
```

`Action` 默认应使用 `Update` 或 `CreateOrUpdate` 由 existing asset path 指向导入后的 AnimSequence。若目标 asset 不存在，first production benchmark 可以 fail fast，并在 smoke setup 中先准备真实导入或 fixture sequence；不通过 AssetDocument 生成动画轨道。

## Body Region Design

### `Body.References`

建议 shape：

```json
{
  "Skeleton": { "Kind": "AssetRef", "Path": "/Game/Characters/Hero/SKEL_Hero" },
  "RetargetSource": "Default",
  "RetargetSourceAsset": { "Kind": "AssetRef", "Path": "/Game/Characters/Hero/SK_Hero" }
}
```

Rules：

- `Skeleton` 主要用于验证当前 sequence skeleton 是否符合 sidecar 声明；v1 不默认替换 skeleton，除非 implementation plan 明确使用 `SetSkeleton` / `ReplaceSkeleton` 并验证 track remap 风险。
- `RetargetSource` 是 name identity。
- `RetargetSourceAsset` 使用 `USkeletalMesh` asset ref。apply 后调用 `SetRetargetSourceAsset` 或 `ClearRetargetSourceAsset`，并让 UE 更新 reference pose data。

RegionPolicy：

- `regionId`: `Body.References`
- `kind`: `Object`
- `identity`: field name
- `reducer`: default diff for scalar/ref fields
- `apply`: set validated references through engine APIs where available

### `Body.Preview`

建议 shape：

```json
{
  "PreviewMesh": { "Kind": "AssetRef", "Path": "/Game/Characters/Hero/SK_Hero" },
  "PreviewPoseAsset": { "Kind": "AssetRef", "Path": "/Game/Characters/Hero/PA_HeroPreview" }
}
```

Rules：

- `PreviewMesh` 通过 `SetPreviewMesh()` / `SetPreviewSkeletalMesh()` 设置。
- 需要验证 preview mesh 与 sequence skeleton 兼容。
- `PreviewPoseAsset` 是 preview-only authoring，若 API 稳定则纳入 apply/extract/diff；否则列入 deferred。

### `Body.Playback`

建议 shape：

```json
{
  "RateScale": 1.0
}
```

Rules：

- `RateScale` 必须为正数。
- `PlayLength`、`NumberOfSampledKeys`、`SamplingFrameRate` 可以出现在 extract payload 的 evidence 或 `_Skipped`，但 authored sidecar 中不接受这些字段。

### `Body.Additive`

建议 shape：

```json
{
  "AdditiveAnimType": "AAT_None",
  "RefPoseType": "ABPT_None",
  "RefFrameIndex": 0,
  "RefPoseSeq": { "Kind": "AssetRef", "Path": "/Game/Animations/AS_BasePose" }
}
```

Rules：

- enum values 使用 UE enum names，不缩写。
- `RefFrameIndex` 必须为非负整数，并且不超过当前 sequence 或 referenced base sequence 的有效范围。
- `RefPoseSeq` 指向 `UAnimSequence`，不内联 referenced sequence body。
- apply 后应调用 UE additive validity/repair 相关 API 或至少刷新 cache 并验证 `IsValidAdditive()`。

### `Body.RootMotion`

建议 shape：

```json
{
  "bEnableRootMotion": true,
  "RootMotionRootLock": "RefPose",
  "bForceRootLock": false,
  "bUseNormalizedRootMotionScale": false
}
```

Rules：

- 管 setting，不管 root bone track data。
- `RootMotionRootLock` 使用 UE stable enum names。
- apply 后刷新 sequence cache；需要验证 montage copied flag 不被误作为 authoring surface。

### `Body.Compression`

建议 shape：

```json
{
  "CompressionErrorThresholdScale": 1.0,
  "BoneCompressionSettings": { "Kind": "AssetRef", "Path": "/Engine/Animation/DefaultAnimBoneCompressionSettings" },
  "CurveCompressionSettings": { "Kind": "AssetRef", "Path": "/Engine/Animation/DefaultAnimCurveCompressionSettings" },
  "bDoNotOverrideCompression": false
}
```

Rules：

- 管配置，不管压缩结果。
- `VariableFrameStrippingSettings` 若 UE 5.7 public surface 可稳定设置，则纳入该 region；否则先 deferred。
- apply 后应调用 `ValidateCompressionSettings()`，必要时请求重压缩或清理 compression data，但不把压缩输出写入 sidecar。

### `Body.Curves`

建议 shape：

```json
[
  {
    "Name": "Speed",
    "Flags": ["AACF_DriveMorphTarget_DEPRECATED"],
    "Keys": [
      { "Time": 0.0, "Value": 0.0, "InterpMode": "RCIM_Linear" },
      { "Time": 0.5, "Value": 320.0, "InterpMode": "RCIM_Cubic" }
    ]
  }
]
```

Rules：

- identity 是 `Name`，不使用 array index。
- 支持 float curves；transform/vector/color curves 和 animated attributes 先 deferred，除非 implementation plan 明确验证 API。
- apply 使用 `IAnimationDataController` 或已验证的 UE animation data APIs，完整 preflight 后 staged apply。
- extract 输出 canonical ordered curves，diff 使用 curve name identity 和 key comparison。

### `Body.Notifies`

建议 shape：

```json
[
  {
    "Name": "Footstep",
    "Time": 0.12,
    "TrackName": "Feet",
    "Notify": { "Kind": "EmbeddedObject", "Class": "/Script/Engine.AnimNotify" }
  }
]
```

Rules：

- identity 优先使用 stable `Guid` if available；若 UE notify guid 不稳定，使用 `(Name, Time, Class, TrackName)` semantic key 并在 spec report 中记录风险。
- `Time` 必须在 sequence play length 范围内。
- 支持 named notify 和 embedded notify object fragments。
- apply 后调用 `SortNotifies()`、`InitializeNotifyTrack()`、`ClampNotifiesAtEndOfSequence()` 和 cache refresh。

### `Body.NotifyStates`

建议 shape：

```json
[
  {
    "Name": "DamageWindow",
    "Time": 0.25,
    "Duration": 0.18,
    "TrackName": "Combat",
    "NotifyState": { "Kind": "EmbeddedObject", "Class": "/Script/Engine.AnimNotifyState" }
  }
]
```

Rules：

- identity and validation follow `Body.Notifies`。
- `Duration` 必须为正数，`Time + Duration` 不超过 sequence play length。
- notify state object 使用 fragment compiler，outer 应归属于 sequence。

### `Body.NotifyTracks`

建议 shape：

```json
[
  { "TrackName": "Feet", "Color": "#7aa2f7" },
  { "TrackName": "Combat", "Color": "#f7768e" }
]
```

Rules：

- identity 是 `TrackName`。
- track order 可以保留，因为 Persona timeline display 有顺序语义；但 region policy 必须说明 order-sensitive。
- 如果 UE 会根据 notifies 自动生成 track entries，implementation plan 应先验证 `AnimNotifyTracks` 持久化行为。若不稳定，v1 改为 extract evidence + deferred apply。

### `Body.SyncMarkers`

建议 shape：

```json
[
  { "Name": "LeftFootDown", "Time": 0.1 },
  { "Name": "RightFootDown", "Time": 0.6 }
]
```

Rules：

- identity 是 `(Name, Time)`，name 是 primary semantic key。
- time 必须在 sequence play length 范围内。
- apply writes `AuthoredSyncMarkers`，然后调用 `SortSyncMarkers()` 和 `RefreshSyncMarkerDataFromAuthored()`。
- `UniqueMarkerNames` 是 derived evidence，不 author。

### `Body.Metadata`

建议 shape：

```json
[
  {
    "Kind": "EmbeddedObject",
    "Class": "/Script/Engine.AnimMetaData",
    "Properties": {}
  }
]
```

Rules：

- 使用 fragment compiler。
- identity 优先使用 object class + stable name if available；否则 managed array replacement。
- apply 使用 `EmptyMetaData()` + `AddMetaData()` staged replacement。

### `Body.AssetUserData`

建议 shape：

```json
[
  {
    "Kind": "EmbeddedObject",
    "Class": "/Script/Engine.AssetUserData",
    "Properties": {}
  }
]
```

Rules：

- 使用 fragment compiler。
- identity 优先 class key；同 class 多实例需要 explicit `Name` 字段，否则 reject。
- apply 使用 `IInterface_AssetUserData` methods，避免直接写 array。

## Apply / Extract / Diff Semantics

Apply：

- 必须完整 preflight 所有 authored body regions，禁止半写入。
- 对 timeline regions 使用 managed replacement 语义：sidecar 声明该 region 时，先清除该 region 管理的旧数据，再应用新数据。
- 对 object/scalar regions 使用 default diff 语义：只更新 sidecar 声明的字段。
- apply 后运行 region-specific repair hook，例如 notify track init、sync marker refresh、compression settings validation、cache refresh。

Extract：

- 输出 canonical `Body` keys。
- 对 managed writable regions 输出当前 managed state。
- 对 derived/import/cache fields 放入 `_Skipped` 或 profile diagnostics，不写入可 authored shape。
- 保持 JSON key names 使用 UE 稳定字段名或明确的 Body region 名。

Diff：

- diff authored fields against current asset by region。
- 对 `_Skipped` metadata 和 evidence 忽略 authored comparison。
- timeline diff 以 identity rule 比较，不以 array index 作为唯一依据。

Validate：

- reject unknown body keys，并对历史/缩写 key 给出 guidance。
- reject import/raw/compressed/cache fields in authored sidecar。
- reject referenced-owned data embedded under AnimSequence sidecar。

## Region Policy Summary

| RegionId | Kind | Managed UE surface | Identity | Default source | Apply mode | Repair hook |
| --- | --- | --- | --- | --- | --- | --- |
| `Body.References` | Object | `Skeleton`, `RetargetSource`, `RetargetSourceAsset` | field name | current asset / CDO | set validated refs | validate skeleton/retarget data |
| `Body.Preview` | Object | preview mesh/pose fields | field name | current asset / CDO | engine preview APIs | mark package dirty |
| `Body.Playback` | Object | `RateScale` | field name | CDO | set scalar | refresh cache |
| `Body.Additive` | Object | additive settings | field name | CDO | set scalar/ref | validate additive |
| `Body.RootMotion` | Object | root motion settings | field name | CDO | set scalar/enum | refresh cache |
| `Body.Compression` | Object | compression config | field name | project defaults / CDO | set scalar/ref | validate compression settings |
| `Body.Curves` | Array | float curves | curve name | empty managed set | rebuild curve region | animation data controller notify |
| `Body.Notifies` | Timeline | point notify events | semantic notify key | empty managed set | rebuild managed notifies | sort/init/clamp notifies |
| `Body.NotifyStates` | Timeline | ranged notify events | semantic notify key | empty managed set | rebuild managed notify states | sort/init/clamp notifies |
| `Body.NotifyTracks` | Array | notify track display data | track name | generated/current tracks | rebuild tracks | initialize notify tracks |
| `Body.SyncMarkers` | Timeline | `AuthoredSyncMarkers` | marker name + time | empty managed set | rebuild markers | sort/refresh markers |
| `Body.Metadata` | Array | `MetaData` | class/name or replacement | empty managed set | fragment rebuild | attach to sequence |
| `Body.AssetUserData` | Array | `AssetUserData` | class/name | empty managed set | interface methods | attach to sequence |

## Verification Expectations

The implementation plan must include:

- focused automation under `AssetFactory.AssetDocument.AnimSequence`;
- profile/schema tests proving `/Script/Engine.AnimSequence` is registered with all body keys and region policies;
- negative tests for unknown body keys, import/raw/compressed/cache fields, invalid references, out-of-range times, invalid enum names, duplicate identities, and unsupported curve/attribute types;
- apply/extract/diff roundtrip tests for each managed region;
- full `AssetFactory.AssetDocument` automation;
- MCP tests proving generic AssetDocument schema/profile inspection exposes AnimSequence regions without adding specialized tools;
- external HTTP smoke against `C:/AVH1` or another validation host;
- a real inspectable smoke asset such as `/Game/AssetDocumentSmoke/AS_PostImportSidecarSmoke` and sidecar `C:/AVH1/Content/AssetDocumentSmoke/AS_PostImportSidecarSmoke.assetdoc.json`.

Smoke setup may create or copy a fixture AnimSequence using UE-supported import/fixture preparation, but the AssetDocument benchmark itself starts after the AnimSequence exists and only manages post-import fields.

## Acceptance Criteria

- `inspect_asset_document_profile` for `/Script/Engine.AnimSequence` reports the exact profile, canonical body keys, and region policies.
- `validate_asset_document` rejects authored raw/import/compression-cache fields and accepts all supported post-import regions.
- `apply_asset_document` can update an existing imported AnimSequence across the managed regions without touching raw animation data.
- `extract_asset_document` returns canonical Body representation and clear skipped diagnostics for excluded fields.
- `diff_asset_document` reports region-level changes using semantic identity rules.
- `apply_asset_document_file` works with sidecar path validation and updates per-region sync evidence.
- A persistent smoke asset and sidecar remain available for user inspection.

## Open Design Decisions Resolved Here

- AnimSequence coverage is not a thin first slice; it is a complete post-import AssetDocument benchmark.
- The sidecar does not replace import or reimport; it starts after a sequence exists.
- `Body.Curves`, `Body.Notifies`, `Body.NotifyStates`, `Body.SyncMarkers`, `Body.Additive`, `Body.RootMotion`, `Body.Compression`, metadata, and user data are all in scope unless implementation evidence proves a specific field unsafe.
- Unsafe or importer-owned fields are documented in deferred fields, not silently ignored.
