# AnimSequence AssetDocument Deferred Fields

日期：2026-06-18

本文记录 `/Script/Engine.AnimSequence` AssetDocument 设计中不作为第一批 authored sidecar surface 的字段。这里的 deferred 不等于永远排除；每项都说明当前处理方式和清理条件。

## Import / Source Data

### `/Body/Import`

- UE surface: `UAnimSequence::AssetImportData` and importer-owned source file metadata.
- 当前处理方式:
  - validate: reject authored `Body.Import`。
  - apply: no-op because authored import data is not accepted。
  - extract: may report as skipped evidence with reason `ImportOwnedData`。
  - diff: ignore skipped evidence。
- deferred 原因: 本轮只做导入之后的 AssetDocument sidecar，不扩展 FBX、Interchange、reimport 或 source file workflow。
- 清理条件: 单独设计 import/reimport pipeline spec，并明确 source path、source control、DCC tool、reimport settings 和 deterministic import 的责任边界。

## Raw Animation Tracks

### `/Body/RawTracks`

- UE surface: raw bone transform tracks, source animation keys, animated bone attributes, and related animation data model track payloads.
- 当前处理方式:
  - validate: reject authored raw track fields such as `RawAnimationData`, `RawTracks`, `BoneTracks`, `TransformTracks`, `AnimatedBoneAttributes`。
  - apply: no-op because authored fields are rejected。
  - extract: skipped evidence only if useful for diagnostics。
  - diff: ignore skipped evidence。
- deferred 原因: raw animation data is import-owned or controller-owned high-volume data. Treating it as sidecar JSON would turn AssetDocument into an import replacement and create large unstable diffs.
- 清理条件: A future animation data authoring spec proves a stable, compact, semantic representation for selected track operations without replacing import.

## Compressed Animation Data

### `/Body/CompressedData`

- UE surface: compressed bone data, compressed curve data, DDC/cache payloads, platform-specific compression output.
- 当前处理方式:
  - validate: reject authored compressed/cache fields。
  - apply: sidecar may update compression settings through `Body.Compression`, but never authored compressed output。
  - extract: skipped evidence only。
  - diff: ignore skipped evidence。
- deferred 原因: compressed data is derived output from source data and compression settings. It is not a stable authoring surface.
- 清理条件: none for authored sidecar; only diagnostic reporting may be improved.

## Derived Sampling And Length Fields

### `/Body/Playback/PlayLength`

- UE surface: `SequenceLength` / `GetPlayLength()`.
- 当前处理方式:
  - validate: reject authored `PlayLength`。
  - apply: no-op because authored field is rejected。
  - extract: skipped evidence with current value may be emitted。
  - diff: ignore skipped evidence。
- deferred 原因: play length is derived from imported/source animation data and controller state, not a post-import scalar setting.
- 清理条件: If a future operation spec supports trimming through UE animation data controller, it should be an explicit operation, not a simple scalar field.

### `/Body/Playback/NumberOfSampledKeys`

- UE surface: `NumberOfSampledKeys`, deprecated `NumFrames`, deprecated `NumberOfKeys`.
- 当前处理方式:
  - validate: reject authored sampled-key and frame-count fields。
  - apply: no-op because authored fields are rejected。
  - extract: skipped evidence with current values may be emitted。
  - diff: ignore skipped evidence。
- deferred 原因: these values are derived from the animation data model and platform target sampling.
- 清理条件: none for authored sidecar.

### `/Body/Playback/SamplingFrameRate`

- UE surface: deprecated `SamplingFrameRate`, data model frame rate, platform target frame rate.
- 当前处理方式:
  - validate: reject authored `SamplingFrameRate` and `FrameRate` in AnimSequence AssetDocument v1。
  - apply: no-op because authored fields are rejected。
  - extract: skipped evidence may include source/model frame rate and target sampling frame rate。
  - diff: ignore skipped evidence。
- deferred 原因: changing frame rate can imply resampling source animation data, which belongs to import/controller operations rather than post-import sidecar scalar editing.
- 清理条件: A future explicit resample operation is designed and verified.

## Compression Configuration Candidates

### `/Body/Compression/VariableFrameStrippingSettings`

- UE surface: `VariableFrameStrippingSettings`.
- 当前处理方式:
  - validate: accept only after implementation confirms stable reflected shape and apply API; otherwise reject with `DeferredField`。
  - apply: deferred until verified。
  - extract: skipped evidence if not supported。
  - diff: ignore skipped evidence。
- deferred 原因: It may be a stable compression setting, but implementation must verify the exact UE 5.7 property type, serialization shape, and post-apply compression behavior.
- 清理条件: focused test proves setting, extracting, diffing, and validating compression settings works without authoring compression output.

## Notify Track Stability

### `/Body/NotifyTracks/*/Color`

- UE surface: `FAnimNotifyTrack` display color and editor timeline data.
- 当前处理方式:
  - validate: accept only if UE 5.7 persistence and extraction are verified; otherwise reject with `DeferredField`。
  - apply: deferred until verified。
  - extract: skipped evidence if not supported。
  - diff: ignore skipped evidence。
- deferred 原因: notify tracks are partly editor display state and partly notify organization. Track name/order is likely authored; color persistence needs focused proof.
- 清理条件: automation demonstrates saved asset roundtrip preserves track color and order.

## Curve Types Beyond Float Curves

### `/Body/Curves/TransformCurves`

- UE surface: transform/vector/color curves and non-float animation curve data.
- 当前处理方式:
  - validate: reject non-float curve declarations unless implementation plan explicitly supports a verified type。
  - apply: no-op because unsupported authored fields are rejected。
  - extract: skipped evidence if present。
  - diff: ignore skipped evidence。
- deferred 原因: float curves are the first stable sequence-owned curve surface. Other curve and attribute types need separate API and serialization proof.
- 清理条件: Add a dedicated task with failing tests for each curve type and prove roundtrip through `IAnimationDataController`.

### `/Body/Attributes`

- UE surface: animated bone attributes and `AttributeCurves`.
- 当前处理方式:
  - validate: reject authored attributes。
  - apply: no-op because authored fields are rejected。
  - extract: skipped evidence。
  - diff: ignore skipped evidence。
- deferred 原因: attributes are high-dimensional animation data and can be import-owned. They need an operation-level design, not default sidecar arrays.
- 清理条件: future attribute authoring spec defines stable identity, type system, and controller APIs.

## Retarget Source Reference Pose Cache

### `/Body/References/RetargetSourceAssetReferencePose`

- UE surface: `RetargetSourceAssetReferencePose`.
- 当前处理方式:
  - validate: reject authored reference pose transforms。
  - apply: set `RetargetSourceAsset` only through engine API and let UE update stored pose data。
  - extract: skipped evidence。
  - diff: ignore skipped evidence。
- deferred 原因: the transform array is derived from the selected retarget source asset.
- 清理条件: none for authored sidecar; it should remain derived.

## Marker Derived Data

### `/Body/SyncMarkers/UniqueMarkerNames`

- UE surface: `UniqueMarkerNames` and runtime marker cache.
- 当前处理方式:
  - validate: reject authored unique marker/cache fields。
  - apply: write `AuthoredSyncMarkers`, then call marker sort/refresh APIs。
  - extract: skipped evidence if useful。
  - diff: ignore skipped evidence。
- deferred 原因: unique marker names are derived from authored sync markers.
- 清理条件: none for authored sidecar.

## Skeleton And Referenced Asset Internals

### `/Body/SkeletonData`

- UE surface: referenced `USkeleton` internals, skeleton notifies, retarget sources, virtual bones.
- 当前处理方式:
  - validate: reject embedded skeleton-owned data under AnimSequence。
  - apply: no-op because authored fields are rejected。
  - extract: skipped evidence only for reference path。
  - diff: ignore skipped evidence。
- deferred 原因: skeleton data belongs to the `USkeleton` asset and should have its own AssetDocument profile if needed.
- 清理条件: implement a `USkeleton` AssetDocument profile.

### `/Body/PreviewMeshData`

- UE surface: referenced `USkeletalMesh` internals.
- 当前处理方式:
  - validate: reject embedded skeletal mesh-owned data under AnimSequence。
  - apply: no-op because authored fields are rejected。
  - extract: skipped evidence only for reference path。
  - diff: ignore skipped evidence。
- deferred 原因: mesh data belongs to the referenced `USkeletalMesh`.
- 清理条件: implement a `USkeletalMesh` AssetDocument profile or rely on existing mesh tooling.

## Thumbnail And Editor Transient State

### `/Body/Thumbnail`

- UE surface: `ThumbnailInfo`.
- 当前处理方式:
  - validate: reject authored thumbnail fields。
  - apply: no-op because authored fields are rejected。
  - extract: skipped evidence only if needed。
  - diff: ignore skipped evidence。
- deferred 原因: thumbnail is derived/editor presentation data, not semantic asset authoring.
- 清理条件: none for semantic AssetDocument.

### `/Body/TransientEditorState`

- UE surface: transient duplicate fields and Persona-only display state.
- 当前处理方式:
  - validate: reject authored transient editor fields。
  - apply: no-op because authored fields are rejected。
  - extract: skipped evidence only。
  - diff: ignore skipped evidence。
- deferred 原因: transient editor state is not source-of-truth content.
- 清理条件: none for sidecar v1.
