# AssetDocument 新资产类扩展指南

日期：2026-06-18

## 目标

本指南用于让新的 Codex session 或 subagent 快速开始一个新的 AssetDocument asset class implementation。

它不是某个资产的 spec，也不是 implementation plan。它是进入新资产前必须遵循的操作手册：先判断资产 authoring surface，再拆 managed regions，再写 spec 和 plan，最后用真实 asset 和 sidecar smoke 验证。

核心原则：

- AssetDocument 是 delta-first sidecar，不是完整 `.uasset` 镜像。
- Agent 编辑 `.assetdoc.json`，不编辑 patch operation DSL。
- 新 asset class 优先扩展 profile、schema、capability、region policy 和 verification，不新增专用 MCP tool。
- 能用 UE reflection、profile/schema、RegionPolicy 表达的能力，不默认扩张成 per-asset generator 或 per-structure reducer。
- 新 asset class 默认采用 full-surface mode：一个 spec / master implementation plan 必须一次性覆盖该 asset class 的完整 managed authored surface。milestone 只表示执行顺序、checkpoint commit 和 subagent 分工，不表示可以交付一个保守第一版。
- Deferred fields 文档只记录债务和诊断边界，不缩小完成目标。属于 managed authored surface 的 deferred entry 必须在同一个 master plan 中有清理 task，或记录用户明确批准的 blocker；否则 final 状态是 partial/blocked，不是 complete。
- 用户面向文档、计划、报告默认使用中文；代码标识符和 schema key 保持英文。

## 新 Session 启动顺序

新 session 接手一个 asset class 前，先读取这些文件：

```text
docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md
docs/reports/asset-document-animmontage-branch-final-review.md
docs/reports/asset-document-animmontage-complete-region-benchmark.md
docs/superpowers/specs/2026-06-13-asset-document-bidirectional-delta-sidecar-goal.md
docs/superpowers/specs/2026-06-24-asset-document-public-region-runtime-design.md
docs/superpowers/specs/2026-06-14-animmontage-region-benchmark.md
docs/superpowers/specs/asset-document-deferred-fields/2026-06-12-animmontage.md
```

然后只读当前目标 asset class 相关的 UE headers、现有 AssetDocument profile/capability/test 文件。不要扫全仓。

推荐代码锚点：

```text
Source/AssetDocument/Public/AssetDocumentProfile.h
Source/AssetDocument/Public/AssetDocumentFragment.h
Source/AssetDocument/Public/AssetDocumentTypes.h
Source/AssetDocument/Public/AssetDocumentRegion.h
Source/AssetDocument/Private/AssetDocumentService.cpp
Source/AssetDocument/Private/AssetDocumentRegionRuntime.cpp
Source/AssetDocument/Private/AssetDocumentBodyRegionDispatcher.cpp
Source/AssetDocument/Private/AssetDocumentJsonRegionUtils.cpp
Source/AssetDocument/Private/Regions/AssetDocumentDeferredRegionAdapter.cpp
Source/AssetDocument/Private/Regions/AssetDocumentObjectRegionAdapter.cpp
Source/AssetDocument/Private/Regions/AssetDocumentNamedArrayRegionAdapter.cpp
Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentProfile.cpp
Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp
Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp
MCP/schemas/AssetDocument.md
MCP/src/index.ts
```

## 第一阶段：Asset Surface Inventory

目标是回答：这个 UE asset 到底有哪些作者可编辑语义，哪些不应该进入 AssetDocument。

必须把每个字段或结构归入以下类别之一：

| 类别 | 判断标准 | 处理方式 |
| --- | --- | --- |
| Managed authored data | 用户或 agent 应长期维护，能从 sidecar 还原 asset 状态 | 定义 `Body.*` region |
| Reflected property delta | 普通 editable property，CDO/default diff 足够表达 | 放入 `Properties` 或 reflected scalar region |
| Referenced asset-owned data | 数据属于被引用 asset，不属于当前 asset | 不放进当前 sidecar，交给对应 asset class |
| Derived/cache data | UE 根据 authored data 计算或刷新 | 不 author，最多作为 evidence/diagnostic |
| Editor-authored layout / organization | 稳定保存、影响作者理解或维护的编辑器布局，例如 Blueprint/graph node position、comment box、track/section organization | 属于 managed authored surface，必须定义 `Body.*` region 或纳入对应 graph/tree/timeline region |
| Per-user / transient editor state | 选择状态、当前 viewport/camera、临时展开折叠、tab focus、local user preference 等不属于资产作者语义的数据 | 排除，最多作为 diagnostic |
| Deprecated/migration data | UE 标记 deprecated 或仅迁移用 | 排除，除非有明确兼容任务 |
| Unknown/risky data | API、所有权或持久化语义不清楚 | 先作为 blocker/research 处理；若属于 managed authored surface，不得自动退出本轮完成目标，除非用户明确批准 deferred |

inventory 产物应写成中文 spec 或 research 文档，至少包含：

- UE class path，例如 `/Script/Engine.AnimSequence`。
- 关键 UE header 和字段位置。
- 每个候选 field/struct 的分类。
- 初始 managed region 列表。
- 明确的 deferred / excluded 列表。
- 每个 managed authored deferred/blocker 的清理 task 或用户批准记录。
- 推荐 benchmark smoke asset。

## 第二阶段：定义 Body Regions

`Body` region 是 AssetDocument 面向 agent 的结构化作者编辑面。命名规则：

- 优先使用 UE 稳定字段名或稳定结构名。
- 禁止缩写，例如使用 `SlotAnimTracks`，不使用 `Slots`。
- 禁止为了“好看”随意重命名。
- 如果使用语义名，必须说明它为什么不是 UE 原字段，以及 adapter 如何 materialize。
- 一个 region 应该对应一个清晰 ownership 边界。

推荐 region 粒度：

- scalar/reference object：可以合并为小 object region，例如 `Body.RootMotion`。
- timeline/array/map：单独 region，例如 `Body.Notifies`、`Body.Curves`。
- graph/tree：单独 graph/tree region，不要塞进 `Properties`。
- editor-authored layout：如果 layout 稳定保存在 asset 中并影响作者维护，例如 Blueprint graph node position、comment box、animation track/section organization，必须随对应 graph/tree/timeline region author/extract/diff。
- derived/cache：不要作为 authoring region。

缺失字段语义：

- AssetDoc 是 source-of-truth delta，不是 one-shot sparse patch。
- 对 managed region 来说，sidecar 缺失字段表示没有持久化差异；sidecar-to-asset 同步时，受影响 managed field/region 应回到默认或基线，再应用 sidecar 声明的值。
- 对未 managed 的 asset 数据，sidecar 缺失绝不能解释为允许重置。

## 第三阶段：RegionPolicy 与默认策略

每个 managed region 都必须有 RegionPolicy。当前实现可能还没有完整通用 policy engine，但 spec 和 plan 必须按这个方向设计。

一个 region policy 至少说明：

- `regionId`：例如 `Body.Curves`
- `kind`：object、array、map、graph、scalar
- `targetProperty` 或 target UE surface
- identity rule：guid、name、semantic key、index forbidden unless proven stable
- default source：CDO、empty template、profile default、current baseline
- comparison rules：order、float tolerance、omit defaults
- reducer mode：例如 default diff、managed region replacement
- apply mode：set property、rebuild array region、rebuild map region、custom hook
- after-apply repair hook：如果 UE 需要刷新 cache

优先使用少量通用模式：

- `SetScalarProperty`
- `SetStructProperty`
- `RebuildArrayRegion`
- `RebuildMapRegion`
- `RebuildGraphRegion`

只有当 UE API 不是普通反射可写、或者 apply 后必须调用 engine-specific repair，才增加 custom hook。

## 第四阶段：Public Region Runtime 接入

新增或扩展 asset class 时，先判断每个 `Body.*` region 是否能通过 public region runtime 表达。默认方向是 profile 声明 `FAssetDocumentRegionPolicy`，capability 建立 `FAssetDocumentRegionBinding`，再把 validate、preflight、apply、extract、diff 委托给 `FAssetDocumentBodyRegionDispatcher` / `FAssetDocumentRegionRuntime` 和一个 public region adapter。

优先复用 dispatcher/runtime 的情况：

- `Body` key validation、unknown key rejection、required/optional region、apply order、JSON Pointer diagnostic、diff entry 构造等生命周期逻辑与现有 profile 重复。
- region 已经能用 `FAssetDocumentRegionPolicy` 描述 `RegionKind`、`ReducerMode`、`ApplyMode`、target surface、comparison/default/identity 规则。
- region 形态是 object、named array、deferred empty/delete、graph wrapper、tree wrapper、timeline wrapper 等公共模式之一。
- asset-specific capability 只是在按 body key 调度一组 adapter，没有必须跨 region 串联的 UE materialization 逻辑。

需要写新的 public region adapter 的情况：

- 新 region shape 可以覆盖多个 asset class，或者第二个 asset/profile 已经开始复制同类 validate/preflight/apply/extract/diff 代码。
- 现有 adapter 无法准确表达 identity、canonicalization、default diff、explicit empty/delete 或 preview-apply-diff 语义。
- 新 adapter 的输入输出仍能保持在 `FAssetDocumentRegionContext`、`FAssetDocumentRegionPolicy`、JSON value 和 diff entries 上，不需要知道具体 asset class 的完整 body contract。

允许保留 asset-specific hook 的情况：

- UE materialization 必须依赖具体 editor subsystem、compile/rebuild/refresh cache 或 post-apply repair。
- region 需要资产领域自己的 semantic identity，例如 graph node semantic id、Blueprint component alias、Widget variable GUID。
- 当前只有一个 asset 使用该行为，抽公共 adapter 会比局部 hook 更复杂；但第二次出现时必须重新评估并写 spec/plan 抽公共层。
- hook 只负责领域转换或修复，不重新实现 dispatcher/runtime 已有的 body key 校验、region dispatch、canonical JSON compare、diagnostic 和 diff helper。

具体 adapter 使用组合，不使用继承层级扩张：

- public adapter 直接实现 `IAssetDocumentRegionAdapter`，通过 config、hooks 或成员对象组合既有领域 adapter。
- wrapper adapter 可以薄封装 legacy domain adapter，例如 WidgetBlueprint tree、binding、animation、graph wrapper；wrapper 的职责是把 runtime context/value 转成旧 adapter 需要的调用，不把旧 adapter 变成 capability base。
- 不新增 `FAssetDocumentBodyCapabilityBase` 继承链作为 profile 的默认扩展方式。当前 runtime 入口是 interface + dispatcher + adapter composition。
- 不新增 `UniversalRegionAdapter`。如果一个 adapter 需要通过 asset class、region kind、body key 或 property name 做大 switch，它已经太宽，应拆成更窄的 public adapter 或 asset-specific hook。

### Object region schema 检查门槛

新增或扩展 object region 时，先判断该 region 是否能声明 `FAssetDocumentObjectFieldSchema`。默认结论应该是可以声明；profile hook 负责资产语义，字段白名单、required field、type validation 和 diagnostic path 由 `FAssetDocumentObjectFieldSchemaUtils` 统一处理。

命中 object region 后，spec 或 implementation plan 必须选择一种路径：

1. 声明 `FAssetDocumentObjectFieldSchema`，并在 `ValidateObject`、外部输入路径的 `ApplyObject`、需要比较 desired object 的 `DiffObject` 中调用 schema utility。
2. 暂不 schema 化，但必须说明原因、当前允许范围、后续清理入口和测试要求。

禁止事项：

- 不得在 profile capability 内复制字段白名单或类型校验。
- 不得把 UE 对象读写、`PropertySetterUtils`、`ClassFinderUtils`、profile-specific default 逻辑放进 schema utility。
- 不得新增第二套 JSON Pointer escaping；field path 必须复用 `FAssetDocumentJsonRegionUtils`。
- schema 自身配置错误必须在测试或 adapter construction 阶段暴露，不能在 profile 运行时静默吞掉。

### Identity 与 diff path 检查门槛

新增或扩展 named array、identity array、graph node、component、variable 等按身份管理的 region 时，必须先定义 stable identity。

默认规则：

- 禁止用 unstable array index 当 identity。
- 只有证明该数组不会被用户排序、插入或删除时，才允许 index identity。
- diff path 不得从 semantic path 退化成 array index path。
- `PathToken` 必须经过 `FAssetDocumentJsonRegionUtils` 的 JSON Pointer escaping。
- 如果使用 `FAssetDocumentIdentityArrayDiffHelper`，资产语义通过 hook/config 注入；不得让 named-array adapter 硬编码具体资产语义。

常见形态示例：

| 形态 | 适用场景 | 推荐入口 |
| --- | --- | --- |
| object region | `Body.Preview`、`Body.Playback`、`Body.ParentClass`、`Body.ClassDefaults` 这类 object/default-diff surface | `FAssetDocumentObjectRegionAdapter` + object policy；需要领域读写时用 hooks |
| named array region | `Body.NotifyTracks` 或按稳定 name/key 管理的 array | `FAssetDocumentNamedArrayRegionAdapter` + identity field config；禁止用 unstable array index 当 identity |
| deferred region | 当前只允许空数组/空对象/显式 deferred evidence 的 region | `FAssetDocumentDeferredRegionAdapter` + declared policy；必须写 deferred fields 文档和清理条件 |
| graph wrapper | `Body.UbergraphPages`、`Body.FunctionGraphs`、`Body.MacroGraphs` 这类 graph region | 薄 wrapper 组合现有 graph adapter；WidgetBlueprint 使用 synthetic `Body.WidgetBlueprintGraphRegions` + `/Body` 汇总多 graph body key |
| tree wrapper | `Body.WidgetTree` 这类 tree/object materialization | 薄 wrapper 组合 tree adapter；runtime 负责 validate/apply/extract/diff 调度 |
| timeline wrapper | animation、notify、track、timeline 类 region | 先判断是否能抽公共 timeline adapter；只有 UE compile/rebuild/repair 留在 asset-specific hook |

### Public adapter test fixture 检查门槛

新增或扩展 public region adapter tests 时，优先复用 `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTestFixture.h`：

- 用 fixture 创建 `FAssetDocumentRegionContext`、policy、binding、dispatcher 和基础 JSON value。
- 用 fixture 的 diagnostic helper 断言 exact path/code，不允许只断言 `bSuccess == false`。
- adapter utility 的核心语义仍应在测试体中显式表达，例如 identity key、canonical order、timeline duplicate 顺序和 track resolver 行为。
- profile-level automation 仍负责 UE materialization、compile/rebuild/cache repair、asset save/load、MCP/apply-file smoke；fixture 不能替代这些验证。

如果新增测试选择不使用 fixture，implementation plan 必须说明原因，例如该测试不在 public region runtime 层，或者需要 profile/UE editor lifecycle。

### Timeline-like region 检查门槛

新增或扩展 animation、notify、marker、section、track、timeline 类 `Body.*` region 时，先做 timeline-like checklist。满足以下任一条件，就不能直接在 profile capability 内新增一整套 parser：

- 有 placement array。
- 有 time/duration/end-time numeric validation。
- 有 track identity 或 track existence validation。
- 有 duplicate placement key。
- 有 apply 后 repair hook。
- 有 extract/diff path 稳定性要求。

命中 checklist 后，spec 或 implementation plan 必须选择一种路径：

1. 使用 `FAssetDocumentTimelinePlacementRegionAdapter` + `FAssetDocumentTimelinePlacementUtils` + profile-specific hooks。
2. 先实现或扩展适合该 region 的 public adapter / utility，再继续完成该 managed region。
3. 新写更贴合的 public adapter spec，例如 nested track/segment、graph/tree、source import 或非 timeline lifecycle。

禁止把命中 checklist 的 region 直接写成新的 asset-specific parser，除非文档同时说明：

- 为什么现有 timeline adapter/utility 不能表达。
- 同一 master plan 中要先补哪一个 public adapter / hook / verification task。
- 若暂时不实现，用户批准的 blocker 证据。
- 升级入口文件/类。
- 清理成功标准。
- 需要保留或新增的测试/验证项。

历史保守边界不能作为新 spec 的默认模板：

- `CompositeSections`、`SlotAnimTracks`、`AnimSegments` 这类 region 如果属于当前 asset 的 managed authored surface，必须在同一个 master plan 中安排实现或公共 adapter 扩展。
- 只有用户明确批准的 blocker 才能保留为 deferred，并且 final report 必须标记 partial/blocked，而不是 complete。

### Canonicalizer 与 unsupported/deferred 暴露规则

新增 sync/hash/canonicalization 行为时，必须先检查是否会引入 asset class / region specific divergence。

禁止事项：

- 不得在 `AssetDocumentService.cpp` 恢复可增长的 service-level asset class / region divergence if-list。
- 不得把 graph-specific equivalence 写进通用 canonical JSON helper。
- 不得静默吞掉 unsupported 或 deferred 的 graph/node/pin/function/region 内容。

允许路径：

- 新的等价归一进入 profile policy hook 或对应 canonicalizer strategy。
- unsupported 内容通过 diagnostic、`_Skipped.*`、diff `skipped` entries 或 explicit deferred rejection 暴露。
- deferred 或保守处理项同步写入 `docs/superpowers/specs/asset-document-deferred-fields/`，包含当前处理方式、deferred 原因、清理条件、升级入口和验证要求。若该项属于 managed authored surface，还必须在同一个 master plan 中安排清理 task，或记录用户明确批准的 blocker。

Profile inspection surface 必须保持稳定：

- `BodySections` 继续来自 profile 的 `GetBodyKeys()`，用于公开 agent-facing body keys。
- `RegionPolicies` 继续来自 profile 的 `GetRegionPolicies()`，用于公开 managed region policy。
- `InternalAdapters` 可以列出 capability 名、public adapter 名和 wrapper adapter 名；它是诊断/inspection 面，不应改变 `BodySections` 或 `RegionPolicies` 的语义。
- 如果新 runtime/wrapper 引入了 adapter name，更新 `GetInternalAdapterNames()`，但不要为了 adapter 名称改变 schema body key。

禁止的 public runtime 反模式：

- 当 object、named array、deferred、graph/tree/timeline wrapper 已有公共形态时，继续为每个 asset class 写一整套 extractor/applier/reducer/diff helper。
- 让 capability 继承某个 capability base 来共享 body dispatch；共享点应是 dispatcher/runtime/adapter，而不是 profile-specific inheritance。
- 写一个按 asset class 或 region kind 全局分发的 `UniversalRegionAdapter`。
- 在 runtime 或 shared adapter 中写 asset-class switch，例如按 `UWidgetBlueprint::StaticClass()`、`UAnimSequence::StaticClass()` 或硬编码 body key/property name 扩展行为。

## 第五阶段：Deferred Fields 文档

每个新 asset class 都应有自己的 deferred fields 文档：

```text
docs/superpowers/specs/asset-document-deferred-fields/YYYY-MM-DD-<asset-class>.md
```

每个 deferred entry 必须包含：

- AssetDocument path。
- UE 结构或概念。
- 当前处理方式：validate、apply、extract、diff。
- deferred 原因。
- 清理条件。

不要把“已经决定排除的 derived/cache data”和“managed authored data 暂时未实现”混在一起。前者可以作为 excluded/non-authoring 记录；后者是未清债务，必须在同一 master plan 中安排清理 task，或记录用户明确批准的 blocker。

Deferred fields 文档不得作为完成范围的缩小依据。Review 时必须先判断 deferred entry 是否属于 managed authored surface；如果属于且没有清理 task 或用户批准，当前 asset class 不能标记 complete。

## 第六阶段：Implementation Plan 必备项

对任何非平凡 asset class implementation，必须先写 implementation plan 到：

```text
docs/superpowers/plans/YYYY-MM-DD-<asset-class>-asset-document-implementation.md
```

计划必须拆成可 checkpoint 的 task，每个 task 包含：

- `TASK_BASE=HEAD`
- 失败测试或当前失败证据
- 实现步骤
- focused verification
- checkpoint commit
- spec review 和 code-quality review 范围

不要把完整 asset class 一次性交给一个 worker。按 region 或相邻 region 分 task，但所有 task 必须留在同一个 master plan 的 full-surface 目标下连续推进。不得把第一批 task、Milestone 1、pilot adapter 或 deferred gate 描述成 asset class 已完成。

推荐 task 顺序：

1. Region inventory contract：body keys、schema hint、template、region policies。
2. 公共 adapter / hook blocker 清理：对复杂 graph/tree/timeline/fragment region，先补必要公共层，不把复杂 region自动 deferred。
3. scalar/reference/object regions。
4. struct、array、timeline、map regions。
5. metadata/subobject/fragments。
6. graph/tree/state-machine/animation-layer 等复杂 authored regions。
7. derived-cache repair 和 validation hardening。
8. apply-file sync、extract/diff、external smoke。
9. final branch-level review report。

## 第七阶段：实现检查清单

每个新增 `Body` region 至少检查这些位置：

| 工作项 | 说明 |
| --- | --- |
| Canonical body keys | profile/capability 必须声明可接受 key |
| Shape validation | object/array/scalar 类型约束必须明确 |
| Schema hint | MCP/profile inspection 能看到 region shape |
| Template | template 能生成可编辑起稿 |
| Parse/preflight | 先完整解析和验证，避免半写入 |
| Apply | 写入 asset 时必须 staged commit |
| Extract | 输出 canonical sidecar 表示 |
| Diff | 能比较 sidecar 与当前 asset |
| RegionPolicy | 声明 managed UE surface 和 sync scope |
| Sync hash | apply-file/extract 能更新或检查 per-region sync state |
| Negative tests | invalid path、type、range、reference 都要有诊断 |
| Real asset test | 至少一个 automation 用真实 UE asset 验证 |
| External smoke | 最后要有可重复的一键 smoke |
| Report | 记录完成范围、非目标、验证证据和剩余风险 |

## 第八阶段：测试与验证标准

最低验证组合：

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
```

然后运行 focused automation，例如：

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.<AssetClass>;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/<AssetClass>"
```

最后运行完整 AssetDocument automation 和 MCP tests：

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AssetDocument"
Push-Location MCP; npm test; Pop-Location
```

如果有 HTTP smoke，使用外部 process 驱动已启动或 runner 启动的 Editor。不要用同一个 `-ExecutePythonScript` 进程调用本进程 HTTP endpoint 作为最终 smoke。

## 第九阶段：Smoke Asset 规则

每个 asset class benchmark 应留下真实可检查的 smoke asset：

```text
/Game/AssetDocumentSmoke/<AssetName>
```

sidecar 默认路径必须与 `apply-file` path contract 对齐：

```text
C:/AVH1/Content/AssetDocumentSmoke/<AssetName>.assetdoc.json
```

不要把 `Saved` 下的 extract output 当作 apply-file 默认 sidecar。`apply-file` 会根据 sidecar 文件路径反推 `/Game/...` target；放在 `Saved` 会破坏 target validation。

smoke 至少验证：

- 创建或更新真实 asset。
- 写入 sidecar。
- `apply-file` 成功。
- `extract` 能读回关键 region。
- `diff` 对已验证 region 没有 unexpected changed entries。
- sidecar 和 asset 保留给用户检查。

## 第十阶段：Final Report 必备内容

每个 asset class benchmark 完成后，写 report 到：

```text
docs/reports/asset-document-<asset-class>-complete-region-benchmark.md
```

报告必须包含：

- worktree、branch、base、reviewed range、closure checkpoint。
- 完成的 managed regions。
- excluded derived/cache fields。
- deferred fields；若其中仍有 managed authored surface，必须列为用户批准的 blocker 或未完成项。
- UBT result。
- focused automation result。
- full AssetDocument automation result。
- MCP test result。
- external smoke result。
- real asset path 和 sidecar path。
- reviewer findings 和修复结果。
- 哪些是已完成，哪些只是目标方向。

如果某个旧验证入口失败或被废弃，可以记录为 historical blocked，但必须明确它不计为 passing verification。若 final report 仍存在未获用户批准的 authored unsupported/deferred region，报告标题和结论不得使用 complete benchmark。

## 不要做的事

- 不要新增 `create_<asset_class>`、`update_<region>` 这类专用 MCP tools。
- 不要把 raw `.uasset` dump 当成 agent-facing authoring format。
- 不要为每个结构体默认创建一个专用 reducer 或 adapter。
- 不要在已有 public adapter 形态可覆盖时继续堆 per-asset extractor/applier/reducer stack。
- 不要用 capability base inheritance 作为共享 body dispatch 的默认解法。
- 不要新增 `UniversalRegionAdapter` 或在 runtime/shared adapter 中写 asset-class switch。
- 不要在 runtime/shared adapter 中硬编码 `UWidgetBlueprint::StaticClass()`、`UAnimSequence::StaticClass()` 这类具体 asset class。
- 不要用 array index 当稳定 identity，除非已经证明该数组不会被用户排序、插入或删除。
- 不要把 referenced asset 的数据塞进当前 asset sidecar。
- 不要 author derived/cache fields。
- 不要把 deferred 文档当成 scope 缩小或完成证明。
- 不要把 Milestone 1、pilot、deferred gate、explicit diagnostic 当成 asset class 完成。
- 不要把 sidecar sync state 写入 `.uasset` 作为 v1 默认策略。
- 不要把目标架构文档中的 future class 当成已经存在的 production implementation。

## 新 Session Prompt 模板

```text
你正在当前 spec/plan 指定的 UECopilot worktree 中扩展 AssetDocument 新资产类：<AssetClass>。

请先读取：
- docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md
- docs/reports/asset-document-animmontage-branch-final-review.md
- docs/superpowers/specs/2026-06-13-asset-document-bidirectional-delta-sidecar-goal.md
- docs/superpowers/specs/2026-06-24-asset-document-public-region-runtime-design.md
- docs/superpowers/specs/2026-06-14-animmontage-region-benchmark.md

任务目标：
1. 不扫全仓，只读取 <AssetClass> 相关 UE headers、现有 AssetDocument profile/capability/test 锚点。
2. 先写中文 region inventory，分类 managed authored surface / editor-authored layout / excluded derived-cache-transient-per-user / referenced-owned / blocker。
3. 再写 full-surface implementation plan，不直接改 production code；milestone 只能作为 checkpoint，不得缩小完成目标。
4. 保持 AssetDoc delta-first sidecar，不新增 patch DSL，不新增专用 MCP tool。
5. 如果某个 managed authored region 想 deferred，必须给出 blocker 证据并等待用户批准；否则在同一 master plan 中安排清理 task。
6. 所有面向用户的报告、计划和结论用中文。
```

## 完成定义

一个新 asset class benchmark 只有同时满足以下条件，才算完整完成：

- spec 明确 owned surface、完整 managed authored regions、editor-authored layout regions、excluded derived/cache/transient/per-user boundaries、referenced-owned boundaries。
- implementation plan 中每个 task 都有 checkpoint commit。
- production implementation 覆盖 apply、extract、diff、validate。
- apply-file sync 有 per-region evidence。
- 有真实 smoke asset 和 sidecar。
- UBT、focused automation、full AssetDocument automation、MCP tests、external smoke 都有记录。
- final report 明确区分已完成能力、excluded 非 authoring 数据、用户批准的 blocker。没有用户批准的 managed authored deferred/unsupported region 时，才允许写 complete。
