# WidgetBlueprint AssetDocument Deferred Fields

日期：2026-06-23

状态：Draft，随 WidgetBlueprint implementation plan 更新

范围：记录 `/Script/UMGEditor.WidgetBlueprint` AssetDocument 工作中明确排除、延后或需要实现阶段验证的 UE surface。本文不用于降低主 spec 的完整 WidgetBlueprint 目标；它只防止实现中把未知 surface 静默当作“保留当前 asset 状态”。

---

## Authored but implementation-sensitive

这些属于 WidgetBlueprint authored surface。最终完成 WidgetBlueprint 时必须覆盖，或在发现 UE API 风险后由用户批准调整 scope。

| AssetDocument path | UE surface | 当前处理方式 | 原因 | 清理条件 |
| --- | --- | --- | --- | --- |
| `Body.Animations[].MovieScene.Tracks` | `UWidgetAnimation::MovieScene` track/channel data | validate/apply/extract/diff | MovieScene track families 多，需按 track type 分 checkpoint 实现 | 常用 UMG property tracks roundtrip，并对 unsupported authored track fail preflight |
| `Body.Animations[].Bindings` | `UWidgetAnimation::AnimationBindings` | validate/apply/extract/diff | binding GUID 与 widget rename/variable GUID 相关 | apply/extract/diff 稳定，rename/update 后 sync hash 稳定 |
| `Body.Bindings[].SourcePath` | `FDelegateEditorBinding::SourcePath` | validate/apply/extract/diff | path segment 需要函数/属性/member guid 校验 | function/property binding roundtrip 和 invalid diagnostics 覆盖 |
| `Body.UbergraphPages` widget-specific nodes | `UK2Node_WidgetAnimationEvent` and related UMG nodes | validate/apply/extract/diff | 需要扩展现有 graph node adapters | widget animation event node 有适配器或明确 unsupported diagnostic |
| `Body.WidgetTree.NamedSlotBindings` parent named slot inheritance | `UWidgetTree::NamedSlotBindings` plus parent named slot hosts | validate/apply/extract/diff | named slot ownership 需要区分 current asset 和 parent asset | parent slot content 不被错误内联；owned content roundtrip |

---

## Excluded derived/cache/transient data

这些不属于 WidgetBlueprint AssetDocument authoring surface。

| AssetDocument path | UE surface | 当前处理方式 | 排除原因 | 清理条件 |
| --- | --- | --- | --- | --- |
| `_Skipped.WidgetTree.AllWidgets` | `UWidgetTree::AllWidgets` | extract diagnostic only if useful | editor-only tree cache/index，可由 `RootWidget` 和 `NamedSlotBindings` 派生 | 不进入 authored Body |
| `_Skipped.GeneratedClassRuntimeBindings` | generated class runtime binding arrays | excluded | 编译产物/cache，由 `Bindings` materialize | 不进入 authored Body |
| `_Skipped.GeneratedClassWidgetTree` | compiled widget tree templates | excluded | 编译产物，由 `Body.WidgetTree` materialize | 不进入 authored Body |
| `_Skipped.DesignerSelection` | designer selection, hierarchy expansion, zoom | excluded | per-user/transient editor state | 不进入 authored Body |
| `_Skipped.AssetRegistryTags` | generated asset registry tags | excluded | derived metadata | 由 save/compile 自动更新 |
| `_Skipped.Thumbnail` | thumbnail data | excluded | derived/editor preview data | 不进入 authored Body |

---

## Referenced asset-owned data

这些由 referenced asset 自己的 AssetDocument 管理。

| AssetDocument path | UE surface | 当前处理方式 | 原因 | 清理条件 |
| --- | --- | --- | --- | --- |
| `Body.WidgetTree.*.ReferencedWidgetBlueprintBody` | nested `UserWidget` / child WidgetBlueprint class internals | asset/class ref only | referenced WidgetBlueprint 是独立 asset | 当前 sidecar 只引用 class/path，不内联其 Body |
| `Body.WidgetTree.*.Properties.*` referenced textures/materials/fonts | `UTexture`、`UMaterialInterface`、font assets | asset refs / reflected values | referenced asset-owned data | 不内联 referenced asset content |
| `Body.ClassDefaults.*` parent class definitions | parent `UUserWidget` C++ or parent WidgetBlueprint body | class ref/default baseline only | parent class/asset owns自身定义 | 只比较/恢复 baseline，不复制 parent body |

---

## Scope guard

- 如果 implementation 发现某个 authored surface 暂不可安全 apply，不允许静默保留 current asset state；必须让 validation/apply 返回明确 diagnostic，并更新本文。
- 如果某个 field 被证明只是 derived/cache/transient，应从 authored Body 中排除，并在本文的 excluded section 记录。
- 如果某个 field 被证明属于 referenced asset-owned data，应保留为 ref 或 evidence，不内联。
