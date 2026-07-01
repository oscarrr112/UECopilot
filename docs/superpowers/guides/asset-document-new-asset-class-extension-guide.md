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

## AssetDocument 开发基线

AssetDocument 的开发、spec、plan、实现分支和 review 默认以这条线为基线：

```text
branch: feature/asset-document-structured-capabilities-spec
worktree: E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-structured-capabilities-spec
```

新建 AssetDocument asset class、公共 adapter、region runtime、schema/canonicalizer/diff 相关工作时，默认从该 branch/worktree fork 新的 implementation branch 和 worktree。除非用户明确指定，不要从 `master`、旧 generator worktree 或临时 experiment branch 作为 AssetDocument 基线。

review diff range 必须使用真实 base：

- spec final review：`SPEC_BASE..HEAD`
- task review：`TASK_BASE..HEAD`
- stacked branch：记录真实 parent commit，不得默认写成 `master..HEAD`

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

## 第四阶段：Deferred Fields 文档

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

## 第五阶段：Implementation Plan 必备项

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
2. 公共 adapter / hook blocker 清理：对复杂 graph/tree/timeline/fragment region，先补必要公共层，不把复杂 region 自动 deferred。
3. scalar/reference/object regions。
4. struct、array、timeline、map regions。
5. metadata/subobject/fragments。
6. graph/tree/state-machine/animation-layer 等复杂 authored regions。
7. derived-cache repair 和 validation hardening。
8. apply-file sync、extract/diff、external smoke。
9. final branch-level review report。

## 第六阶段：实现检查清单

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

## 第七阶段：测试与验证标准

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

## 第八阶段：Smoke Asset 规则

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

## 第九阶段：Final Report 必备内容

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
- 不要用 array index 当稳定 identity，除非已经证明该数组不会被用户排序、插入或删除。
- 不要把 referenced asset 的数据塞进当前 asset sidecar。
- 不要 author derived/cache fields。
- 不要把 deferred 文档当成 scope 缩小或完成证明。
- 不要把 Milestone 1、pilot、deferred gate、explicit diagnostic 当成 asset class 完成。
- 不要把 sidecar sync state 写入 `.uasset` 作为 v1 默认策略。
- 不要把目标架构文档中的 future class 当成已经存在的 production implementation。

## 新 Session Prompt 模板

```text
你正在 E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-structured-capabilities-spec 中扩展 AssetDocument 新资产类：<AssetClass>。
AssetDocument 开发基线固定为 branch `feature/asset-document-structured-capabilities-spec`；新 implementation branch/worktree 默认从该基线 fork。

请先读取：
- docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md
- docs/reports/asset-document-animmontage-branch-final-review.md
- docs/superpowers/specs/2026-06-13-asset-document-bidirectional-delta-sidecar-goal.md
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
