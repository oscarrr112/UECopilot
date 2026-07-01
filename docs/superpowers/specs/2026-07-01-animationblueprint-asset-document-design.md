# AnimationBlueprint AssetDocument Design

日期：2026-07-01

状态：待审核

适用分支：`feature/asset-document-animationblueprint-spec`

基线：`d766122c1302aea39b773351566fc397fd0de291`

## 1. 背景

AssetDocument public region runtime 重构链已经进入“新资产 thin hook 验证”阶段。下一步选择 `UAnimBlueprint`，不是为了恢复旧 generator 路线，而是验证一个复杂 Blueprint-derived asset 能否按当前设计接入：

```text
sidecar + profile + policy + public region runtime + thin asset-specific hook
```

历史上的 AnimationBlueprint lifecycle generator 已证明 `UAnimBlueprintFactory` 可以创建真实 `UAnimBlueprint`，关键 factory 字段是：

- `ParentClass`
- `TargetSkeleton`
- `PreviewSkeletalMesh`
- `bTemplate`

但 generator lifecycle 不等于 AssetDocument。AssetDocument 需要声明可长期维护的 `Body.*` regions、policy、diff/apply/extract contract、deferred 边界和验证证据。

本 spec 只关注 AssetDocument，不新增专用 MCP tool，不扩展旧 generator payload。

## 2. 设计结论

本 spec 定义完整 ABP AssetDocument 目标，不把第一阶段实现切片当成最终能力边界。实现必须分阶段推进，但所有阶段都沿同一条架构线：

- 新增 exact-class profile：`/Script/Engine.AnimBlueprint` / `UAnimBlueprint::StaticClass()`。
- 使用 `FAssetDocumentBodyRegionDispatcher` 管理 Body lifecycle，不写完整 asset-specific body parser。
- 优先复用现有 public adapters/utilities：
  - `FAssetDocumentObjectRegionAdapter`
  - `FAssetDocumentObjectFieldSchemaUtils`
  - `FAssetDocumentNamedArrayRegionAdapter`
  - `FAssetDocumentGraphRegionWrapperAdapter`
  - `FAssetDocumentDeferredRegionAdapter`
  - `FAssetDocumentIdentityArrayDiffHelper`
  - `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTestFixture.h`
- 复用或抽出小的 common Blueprint hooks，承接 `ParentClass`、`ImplementedInterfaces`、`Variables`、`ClassDefaults`、K2 `UbergraphPages` 这类 `UBlueprint` 通用语义。
- ABP-specific hook 只负责 `TargetSkeleton`、template flag、preview mesh / preview anim blueprint、optimization flags、sync groups、compile/rebuild/refresh。
- AnimGraph / state machine / transition / anim layer / parent override 也是完整 ABP AssetDocument 的目标 surface，但必须通过后续公共 graph-family adapter 或 dedicated adapter spec 接入，不能写进 ABP 私有巨型 parser。
- `Body.AnimGraph` 第一阶段已允许 root-only pilot graph；真实 pose nodes 仍 deferred。`Body.StateMachines` 和 `Body.TransitionGraphs` 已绑定到 public state-machine adapter，但第一版仍只允许 empty/null/object compatibility value；non-empty authoring 要等真实 materialization 和 extract/diff roundtrip 后再解除。`Body.ParentAssetOverrides` 已进入 parent-node GUID identity-array adapter。其它复杂 region 继续作为 declared deferred regions 暴露，用 exact diagnostic 保护边界；后续阶段逐步解除 deferred。

设计分层不是：

1. **Lifecycle-only profile**：只支持 create/extract skeleton/preview。这个方案太浅，不能证明 public runtime 接入新资产的价值。
2. **One-shot private full parser**：一次实现 AnimGraph、state machine 和 transition graph，但把语义都塞进 `FAnimBlueprintAssetDocumentCapability`。这个方案会违背 public runtime / thin hook 方向。
3. **推荐方案**：一个完整 ABP spec，一个 master implementation plan，plan 内部拆成多个 milestones。先落 stable lifecycle / reference / Blueprint-common surface；再设计 animation graph 公共 adapter；最后接 state machine、transition、layer、parent override 等复杂 region。

## 3. 目标

1. 让 `/Script/Engine.AnimBlueprint` 出现在 AssetDocument registered profiles、template、inspect、validate、apply、extract、diff 的常规通路中。
2. 支持真实 `UAnimBlueprint` create/update lifecycle，使用 `UAnimBlueprintFactory`，并保留 factory 级字段约束。
3. 为 ABP 建立明确 `Body.*` authoring surface，避免把 derived/debug/editor transient 数据误写进 sidecar。
4. 复用 public region runtime 和现有 Blueprint / animation 公共 helper；如发现必须复制 UBlueprint 逻辑，先抽小公共 helper，再接 ABP。
5. 把 AnimGraph/state-machine 等复杂 authored surface 纳入完整目标；实现前必须先定义公共 adapter 边界、identity、canonicalization 和 verification。
6. 对尚未实现的复杂 region 使用 explicit deferred evidence 暴露，不静默吞掉。
7. 用 focused automation、full AssetDocument automation、MCP tests、external smoke 验证真实 asset 和 sidecar contract。

## 4. 非目标

本 spec 不允许：

- 不新增 `create_animation_blueprint`、`update_anim_graph` 等专用 MCP tools。
- 不恢复旧 `AnimationBlueprint` generator 作为 AssetDocument 的入口。
- 不通过 ABP 私有 parser author full `AnimGraph`、state machine、transition graph、blend tree 或 `UAnimGraphNode_*`。
- 不把 Anim Layer Interface 与普通 `UAnimBlueprint` 混成同一个 exact-class profile；`UAnimLayerInterfaceFactory` 需要后续单独 asset/profile 或明确 region extension。
- 不迁移 `PoseWatches`、`PoseWatchFolders`、debug data、compiled generated class caches。
- 不把 `UAnimBlueprintGeneratedClass`、`FAnimBlueprintDebugData`、property access library 或 node property index 当成 authored sidecar。
- 不新增 universal Blueprint adapter，也不让 public runtime 按 `UAnimBlueprint` 做全局 switch。

## 5. UE Surface Inventory

### 5.1 Managed authored data

| UE surface | AssetDocument region | Handling |
| --- | --- | --- |
| `ParentClass` from factory / Blueprint parent | `Body.ParentClass` | object region，必须是 `UAnimInstance` 子类 |
| `TargetSkeleton` | `Body.TargetSkeleton` | object/scalar reference region，template ABP 时必须为 null/absent |
| `bIsTemplate` / factory `bTemplate` | `Body.Template` | object region，声明 template flag 和 create/update constraints |
| `PreviewSkeletalMesh` | `Body.Preview` | object region，通过 getter/setter 或 reflected property thin hook |
| `PreviewAnimationBlueprint` | `Body.Preview` | object region，AssetRef，可选 |
| `PreviewAnimationBlueprintApplicationMethod` | `Body.Preview` | object field，enum/string |
| `PreviewAnimationBlueprintTag` | `Body.Preview` | object field，name/string |
| `bUseMultiThreadedAnimationUpdate` | `Body.Optimization` | object field |
| `bWarnAboutBlueprintUsage` | `Body.Optimization` | object field |
| `bEnableLinkedAnimLayerInstanceSharing` | `Body.Optimization` | object field |
| `Groups` (`FAnimGroupInfo`) | `Body.SyncGroups` | named array，identity 为 `Name` |
| Blueprint interfaces | `Body.ImplementedInterfaces` | common Blueprint array behavior |
| Blueprint variables | `Body.Variables` | common Blueprint identity-array behavior |
| Blueprint class defaults | `Body.ClassDefaults` | common Blueprint default-diff behavior |
| Event/K2 graph | `Body.UbergraphPages` | common Blueprint graph wrapper，覆盖 K2-supported subset |
| Anim graph root and `UAnimGraphNode_*` | `Body.AnimGraph` | root-only pilot 已接入 public adapter；真实 pose nodes 需要 animation graph node adapter |
| State machines / states / transitions | `Body.StateMachines` | public adapter 已注册；第一版只接受 empty/null/object compatibility value，non-empty authoring 等真实 nested graph materialization + extract roundtrip |
| Transition blend graphs | `Body.TransitionGraphs` | public adapter 已注册；第一版只接受 empty/null/object compatibility value，authored rule graph 等 transition materialization + extract roundtrip |
| Anim layer graph/interface authoring | `Body.AnimLayers` | planned managed region；涉及 Anim Layer Interface 与 linked layer compatibility |
| Parent node asset overrides | `Body.ParentAssetOverrides` | identity-array adapter 已支持，identity 为 parent node GUID |
| Default binding class | `Body.DefaultBinding` | object region；如果实现时确认只能作为 editor node creation policy，可阶段性 deferred |
| Function graphs / macro graphs | `Body.FunctionGraphs` / `Body.MacroGraphs` | inherited Blueprint graph regions；是否解除 deferred 取决于 common Blueprint graph plan |

### 5.2 Stage-gated managed data

以下 region 属于完整 ABP AssetDocument 目标，但不允许在第一阶段直接实现为 ABP 私有 parser。阶段实现前必须先写 adapter spec 或在 implementation plan 中引用已存在公共 adapter：

| AssetDocument path | Required public abstraction | Stage-1 behavior |
| --- | --- | --- |
| `Body.AnimGraph` | `FAssetDocumentAnimGraphRegionAdapter` root-only pilot；真实 pose nodes 仍需 animation graph node adapter + graph wrapper strategy | canonical root-only graph 已支持；非空 Nodes 返回 `UnsupportedAnimGraphNode` |
| `Body.StateMachines` | `FAssetDocumentAnimStateMachineRegionAdapter`，后续定义 state/transition identity | stage 1 empty/null/object compatibility only；真实 `UAnimationStateMachineGraph` materialization deferred |
| `Body.TransitionGraphs` | `FAssetDocumentAnimStateMachineRegionAdapter` transition graph slice | stage 1 empty/null/object compatibility only；authored rule nodes deferred |
| `Body.AnimLayers` | anim layer/interface adapter or separate profile | declared deferred，非空 exact diagnostic |
| `Body.ParentAssetOverrides` | `FAssetDocumentAnimParentAssetOverrideRegionAdapter` | parent-node GUID identity array 已支持；authored node alias resolver deferred |
| `Body.FunctionGraphs` / `Body.MacroGraphs` | common Blueprint graph support | follows UBlueprint deferred status |

Stage-gated region 必须使用 `FAssetDocumentDeferredRegionAdapter` 或 equivalent declared policy：允许 empty array/object/null，非空值返回 exact diagnostic，不得静默忽略。已解除的 pilot region 必须改用公共 adapter，并在 deferred-fields 文档中记录仍 deferred 的子字段。解除 deferred 时，必须删除对应 deferred entry 或改成已完成记录。

### 5.3 Excluded / derived / cache data

| UE surface | 处理 |
| --- | --- |
| `UAnimBlueprintGeneratedClass` / skeleton class | derived compile output，不 author |
| `FAnimBlueprintDebugData` | runtime/debug evidence，不 author |
| `FStateMachineDebugData` / node-to-index maps | derived debug mapping，不 author |
| `PropertyAccessLibrary` and compiled node property indexes | compiler output，不 author |
| `PoseWatches` / `PoseWatchFolders` | editor debug state，排除 |
| `bRefreshExtensions` | transient refresh flag，不 author |

## 6. Body Schema

Template 建议从完整 ABP schema 起步，但 stage-gated regions 可以是 empty/deferred values：

```json
{
  "SchemaVersion": 1,
  "Target": "/Game/AssetDocumentSmoke/ABP_AssetDocumentSmoke",
  "Class": "/Script/Engine.AnimBlueprint",
  "Action": "CreateOrUpdate",
  "Definitions": {},
  "Properties": {},
  "Body": {
    "ParentClass": {
      "Kind": "ClassRef",
      "Class": "/Script/Engine.AnimInstance"
    },
    "TargetSkeleton": {
      "Kind": "AssetRef",
      "Asset": "/Game/AssetDocumentSmoke/SK_AssetDocumentSmoke_Skeleton"
    },
    "Template": {
      "bIsTemplate": false
    },
    "Preview": {
      "PreviewSkeletalMesh": null,
      "PreviewAnimationBlueprint": null,
      "PreviewAnimationBlueprintApplicationMethod": "LinkedLayers",
      "PreviewAnimationBlueprintTag": ""
    },
    "Optimization": {
      "bUseMultiThreadedAnimationUpdate": true,
      "bWarnAboutBlueprintUsage": false,
      "bEnableLinkedAnimLayerInstanceSharing": false
    },
    "SyncGroups": [],
    "ImplementedInterfaces": [],
    "Variables": [],
    "ClassDefaults": {},
    "UbergraphPages": [],
    "AnimGraph": [],
    "StateMachines": [],
    "TransitionGraphs": [],
    "AnimLayers": [],
    "ParentAssetOverrides": []
  }
}
```

Notes：

- `TargetSkeleton` 可以是 `null`，但仅当 `Template.bIsTemplate == true`。
- `Body.AnimGraph` 在阶段 1 支持 canonical root-only graph，也继续接受 empty array/object/null 作为兼容 empty value；真实 pose nodes 仍 deferred。`Body.StateMachines` / `Body.TransitionGraphs` 在阶段 1 只接受 empty array/object/null 作为兼容 empty value；non-empty schema 必须等真实 apply/extract/diff roundtrip 后再开放。`Body.ParentAssetOverrides` 已支持 parent-node GUID identity-array apply/extract/diff。`Body.AnimLayers` 在阶段 1 只允许 empty array 或 explicit deferred empty value；完整目标仍是 authored region 或独立 profile。
- `FunctionGraphs` / `MacroGraphs` 可以不出现在 template 中；如果实现选择暴露它们，必须按 deferred policy 处理。
- `Properties` 只承接未被 Body 管理的 reflected delta，不得包含 `TargetSkeleton`、`PreviewSkeletalMesh`、optimization flags 等已由 Body 管理的字段。

## 7. Region Policy

| RegionId | Kind | Reducer/compare | Managed UE surface | Adapter / hook |
| --- | --- | --- | --- | --- |
| `Body.ParentClass` | object | default diff | `ParentClass` / generated class parent | object adapter + Blueprint common parent hook |
| `Body.TargetSkeleton` | object/scalar | default diff | `TargetSkeleton` | object adapter + ABP skeleton hook |
| `Body.Template` | object | default diff | `bIsTemplate` / factory `bTemplate` | object adapter + schema utility |
| `Body.Preview` | object | default diff | preview mesh / preview ABP fields | object adapter + ABP preview hook |
| `Body.Optimization` | object | default diff | optimization bools | object schema utility + reflection/thin hook |
| `Body.SyncGroups` | array | managed region | `Groups` | named array adapter, identity `Name` |
| `Body.ImplementedInterfaces` | array | managed region | `ImplementedInterfaces` | common Blueprint identity-array hook |
| `Body.Variables` | array | managed region | `NewVariables` | common Blueprint identity-array hook |
| `Body.ClassDefaults` | object | default diff | generated class CDO vs parent CDO | common Blueprint class-default hook |
| `Body.UbergraphPages` | graph | managed region | `UbergraphPages` | graph wrapper + `UBlueprintGraph` canonicalizer |
| `Body.AnimGraph` | graph | public root-only pilot adapter | anim graph editor graphs | root-only pilot; pose nodes deferred until animation graph node adapter |
| `Body.StateMachines` | graph/tree | public state-machine adapter boundary | state machine graphs | empty/null/object compatibility only; nested UE graph materialization deferred |
| `Body.TransitionGraphs` | graph | public state-machine adapter boundary | transition graphs | empty/null/object compatibility only; authored rule nodes deferred |
| `Body.AnimLayers` | graph/array | stage-gated managed region | anim layer graphs/interfaces | stage 1 deferred; later layer adapter/profile |
| `Body.ParentAssetOverrides` | array | parent-node GUID identity-array adapter | `ParentAssetOverrides` | apply/extract/diff supported; authored AnimGraph node alias resolver deferred |

Policies must list `ManagedUePropertyPaths` so profile inspection and sync evidence can explain ownership. If a field is private but accessible only through `UAnimBlueprint` methods, the policy still names the conceptual UE surface and the hook handles materialization.

## 8. Lifecycle And Cross-Region Validation

### 8.1 Create / update

Create path:

- Resolve `Body.ParentClass.Class`; it must be a non-null child of `UAnimInstance`.
- Resolve `Body.TargetSkeleton` unless `Body.Template.bIsTemplate == true`.
- Configure `UAnimBlueprintFactory` with `BlueprintType`, `ParentClass`, `TargetSkeleton`, `PreviewSkeletalMesh`, `bTemplate`.
- Create or load target asset through existing AssetDocument service flow.
- Apply Body regions through dispatcher.
- Compile/rebuild after structural changes.

Update path:

- Existing asset must be `UAnimBlueprint`.
- Parent class or template/skeleton changes require staged preflight before mutation.
- If a cross-region validation fails after partial staging, implementation must restore or discard the transient staging asset; it must not half-write the real asset.

### 8.2 Cross-region rules

- `Template.bIsTemplate == true` requires `TargetSkeleton == null` or absent.
- `Template.bIsTemplate == false` requires `TargetSkeleton` on create.
- `Preview.PreviewSkeletalMesh` should match `TargetSkeleton` when both are resolvable; mismatch returns diagnostic at `/Body/Preview/PreviewSkeletalMesh`.
- `Preview.PreviewAnimationBlueprint` must be compatible with target skeleton/template/interface constraints when UE exposes a reliable compatibility check; otherwise first implementation must document the conservative validation boundary.
- `ParentClass` must remain compatible with ABP class generation. The default is `/Script/Engine.AnimInstance`.
- `SyncGroups` names must be non-empty after `FName` normalization and unique case-insensitively.
- `UbergraphPages` preflight that depends on variables or parent class must run against a staged desired blueprint, following the WidgetBlueprint/UBlueprint pattern.

## 9. Public Runtime Composition

ABP implementation must not add another giant `FAnimBlueprintAssetDocumentCapability.cpp` that hand-parses all regions.

Expected composition:

1. `FAnimBlueprintAssetDocumentProfile`
   - exact class
   - template
   - body keys
   - region policies
   - internal adapter names
2. `FAnimBlueprintAssetDocumentCapability`
   - constructs dispatcher
   - wires object/named-array/deferred/graph adapters
   - owns ABP-specific hooks
   - calls post-apply compile/rebuild
3. `FAnimBlueprintLifecycleHook` or profile-private factory function
   - factory create/update preflight
   - skeleton/template/preview materialization
4. common Blueprint helper extraction if needed
   - parent class
   - interfaces
   - variables
   - class defaults
   - K2 graph wrapper staging

If ABP needs more than one copy-pasted block from `FUBlueprintAssetDocumentCapability`, implementation must pause and extract a small common Blueprint utility first. The common utility must be composition-based and cannot become a capability base inheritance chain.

## 10. Diagnostics

Required diagnostic examples:

| Case | Path | Code |
| --- | --- | --- |
| Body is not object | `/Body` | `InvalidBodyType` |
| unknown Body key | `/Body/<Key>` | existing dispatcher unknown-key code |
| parent class missing | `/Body/ParentClass/Class` | `MissingParentClass` or ABP-specific equivalent |
| parent class not `UAnimInstance` child | `/Body/ParentClass/Class` | `InvalidAnimBlueprintParentClass` |
| template with skeleton | `/Body/TargetSkeleton` | `InvalidTemplateSkeleton` |
| non-template without skeleton on create | `/Body/TargetSkeleton` | `MissingTargetSkeleton` |
| preview mesh skeleton mismatch | `/Body/Preview/PreviewSkeletalMesh` | `MismatchedPreviewSkeletalMeshSkeleton` |
| duplicate sync group | `/Body/SyncGroups/<Index>/Name` | `DuplicateSyncGroupName` |
| unsupported stage-gated graph region before adapter support | sibling region path, or `/Body/AnimGraph/AnimGraph/Nodes/<Index>` for unsupported AnimGraph node | `UnsupportedAnimBlueprintRegion` or `UnsupportedAnimGraphNode` |

Exact path/code assertions are required in tests. Runtime adapter tests should use `AssetDocumentRegionRuntimeTestFixture.h`; profile automation still verifies UE materialization.

## 11. Template And Profile Inspection

Profile inspection must expose:

- `Class = /Script/Engine.AnimBlueprint`
- `BodySections` with all managed and deferred body keys.
- `RegionPolicies` for every `Body.*` key.
- `InternalAdapters`, including public adapter names and ABP-specific hook/adapter names.
- deferred regions as declared policy entries, not missing undocumented behavior.

The profile must be exact-class registered in `AssetDocumentModule.cpp`; no parent-class fallback is assumed.

## 12. Validation Strategy

### 12.1 Focused tests

Add `AssetDocumentAnimBlueprintTests.cpp` with at least:

- profile registration and template shape.
- body schema validation for `ParentClass`, `TargetSkeleton`, `Template`, `Preview`, `Optimization`, `SyncGroups`.
- dispatcher unknown key and missing/invalid type behavior.
- stage-gated graph regions accept empty values and reject non-empty values with exact path/code until their adapter stage lands.
- `SyncGroups` duplicate and extract/diff path stability.
- create/update real `UAnimBlueprint` with `TargetSkeleton` and preview mesh.
- extract returns canonical Body regions and does not emit derived/debug/cache data.
- diff reports managed region changes and no unexpected graph authoring.

### 12.2 Integration verification

Implementation plan must include:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=<validation-host>.uproject" -NoHotReload
```

Focused automation:

```powershell
Automation RunTests AssetFactory.AssetDocument.AnimBlueprint
```

Regression automation:

```powershell
Automation RunTests AssetFactory.AssetDocument.UBlueprint
Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint
Automation RunTests AssetFactory.AssetDocument.RegionRuntime
Automation RunTests AssetFactory.AssetDocument
```

MCP:

```powershell
npm --prefix MCP test
```

External smoke:

- Create or update `/Game/AssetDocumentSmoke/ABP_AssetDocumentSmoke`.
- Sidecar path: `C:/AVH1/Content/AssetDocumentSmoke/ABP_AssetDocumentSmoke.assetdoc.json`.
- `apply-file` succeeds.
- `extract` returns `Class=/Script/Engine.AnimBlueprint` and expected Body regions.
- `diff` has no unexpected changed entries for managed regions.
- The real asset and sidecar remain inspectable.

## 13. Master Plan And Deferred Tracking

This spec is intentionally broader than the first implementation slice. The next implementation artifact should be one master plan for the full ABP AssetDocument chain. That plan may split the work into milestones, task groups, checkpoint commits and review gates, but it should keep one ordered plan file and one final ABP target.

If a later milestone discovers that a graph-family adapter boundary is still underspecified, the master plan should include a spec-review gate or a focused sub-spec task before implementation continues. That gate pauses execution; it does not mean the ABP work loses its single master-plan structure.

Recommended milestones:

1. **ABP core profile and lifecycle**
   - profile registration, template, inspection, `UAnimBlueprintFactory` create/update
   - `ParentClass`, `TargetSkeleton`, `Template`, `Preview`, `Optimization`, `SyncGroups`
   - common Blueprint regions that can be reused safely
   - stage-gated graph regions declared as deferred
2. **Animation graph adapter spec and pilot**
   - define `Body.AnimGraph` semantic identity
   - decide node/pin representation and canonicalization
   - prove at least one simple anim graph roundtrip without ABP-private parser sprawl
3. **State machine and transition graph support**
   - define state identity, transition identity and nested graph ownership
   - connect transition blend graphs through a dedicated public adapter or graph-family wrapper
4. **Anim layer and parent override support**
   - decide whether Anim Layer Interface needs a separate profile
   - implement `Body.ParentAssetOverrides` only after parent node GUID identity is stable
5. **Full ABP smoke and cleanup**
   - remove or close deferred entries that have real managed implementations
   - keep excluded derived/debug/cache fields excluded

The master plan's first milestone must add:

```text
docs/superpowers/specs/asset-document-deferred-fields/2026-07-01-animationblueprint.md
```

It must record stage-gated regions and excluded fields:

- `Body.AnimGraph`
- `Body.StateMachines`
- `Body.TransitionGraphs`
- `Body.AnimLayers`
- `Body.ParentAssetOverrides`
- `Body.DefaultBinding` if still not managed
- `Body.FunctionGraphs` / `Body.MacroGraphs` if exposed as deferred
- excluded derived/debug/cache fields

Each entry must include current behavior, deferred reason, cleanup trigger, upgrade entrypoint, and required verification. When a later milestone implements a region, that milestone must update this file in the same commit chain or task.

## 14. Implementation Plan Boundaries

The master implementation plan must cover the full ABP chain, starting with stage 1 checkpointed tasks:

1. Profile skeleton and template/inspection.
2. Lifecycle create/update hook with `UAnimBlueprintFactory`.
3. Dispatcher/object regions: `ParentClass`, `TargetSkeleton`, `Template`, `Preview`, `Optimization`.
4. `SyncGroups` named-array region.
5. Common Blueprint regions reuse/extraction: interfaces, variables, class defaults, K2 `UbergraphPages`.
6. Stage-gated graph regions and deferred-fields doc.
7. Animation graph adapter boundary gate and pilot.
8. State machine and transition graph milestone.
9. Anim layer and parent override milestone.
10. Focused automation, full verification, MCP, external smoke, final review.

Each task must declare `TASK_BASE`, allowed files, forbidden files, focused tests, checkpoint commit, and review diff range.

Later milestones inside the same plan must not reopen the ABP surface inventory from scratch. They should reference this spec and focus on the next adapter boundary, identity model, diagnostic contract and verification.

## 15. Stop Conditions

Stop and return to spec discussion if any implementation task requires:

- writing an ABP-private parser for `UAnimGraphNode_*`, state machine, or transition graph semantics.
- duplicating large blocks from `FUBlueprintAssetDocumentCapability` instead of extracting common Blueprint helper.
- adding asset-class branches to public region runtime or shared adapters.
- adding a capability base inheritance chain.
- treating `UAnimBlueprintGeneratedClass` or debug data as authored state.
- changing existing UBlueprint / WidgetBlueprint / AnimSequence behavior without a dedicated migration task.
- accepting non-empty stage-gated graph content before the corresponding adapter milestone lands.

## 16. Completion Definition

This spec is complete when:

- ABP owned/stage-gated/excluded surface is explicit.
- Full target Body keys and stage 1 policies are defined.
- Public runtime composition is fixed as dispatcher + adapters + hooks.
- AnimGraph/state-machine authoring is part of the full ABP target, with explicit stage gates and adapter requirements.
- Stage 1 verification includes focused ABP automation, regression automation, MCP tests and external smoke.
- The master implementation plan can advance each ABP milestone without reopening asset surface inventory.

After this spec is approved, the next step is an implementation plan under:

```text
docs/superpowers/plans/2026-07-01-animationblueprint-asset-document-implementation.md
```
