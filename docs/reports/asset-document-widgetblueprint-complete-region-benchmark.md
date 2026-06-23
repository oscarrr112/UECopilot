# WidgetBlueprint AssetDocument 完整区域验证报告

## 基本信息

- Worktree: `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-widgetblueprint-impl`
- Branch: `feature/asset-document-widgetblueprint-impl`
- Base commit: `8c8a073b6189c396806daba4234bdf6cf82b2f2f`
- Review range: `8c8a073b6189c396806daba4234bdf6cf82b2f2f..HEAD`
- Validation host: `C:/AVH1`

## 完成区域

WidgetBlueprint AssetDocument profile 覆盖 `/Script/UMGEditor.WidgetBlueprint`，本轮确认完整流覆盖以下托管区域：

- `Body.ParentClass`
- `Body.ImplementedInterfaces`
- `Body.Variables`
- `Body.ClassDefaults`
- `Body.WidgetTree`
- `Body.Bindings`
- `Body.Animations`
- `Body.UbergraphPages`
- `Body.FunctionGraphs`
- `Body.MacroGraphs`
- `Body.Palette`
- `Body.EditorOptions`
- `Body.WidgetVariableGuids`

这些区域按 AssetDocument 语义处理为 authoritative managed content：遗漏的托管条目表示删除、清除 override 或恢复 baseline，不表示静默保留 `.uasset` 当前值。

## 排除的派生和缓存字段

以下字段仍按派生、缓存或 UE 运行时生成状态处理，不作为稳定 authored sidecar 输入：

- `WidgetVariableGuids` 中可由 `WidgetTree` / `Animations` 生成的条目：允许 sidecar 省略，apply/extract/diff/sync 中通过规范化写回稳定。
- 图区域中的 `GraphGuid`、节点 GUID、生成 pin ID 和布局类编辑器缓存：参与 extract 证据，但不作为 authored diff 的稳定来源。
- WidgetBlueprint 编译产物、Skeleton/GeneratedClass 运行时对象、编辑器 transient 状态。
- MovieScene/UMG 内部 cache、preview-only 状态和包内非 authored transient 数据。

## 延期字段和最终状态

当前 WidgetBlueprint AssetDocument 目标是完整实现已列出的 WidgetBlueprint profile 区域。仍未覆盖的 UE 家族不是 silent preservation：遇到未实现的 MovieScene track/channel 类型、未支持 graph node family 或不受控 WidgetBlueprint 内部对象时，apply/preflight/diff/extract 通过明确 diagnostic 暴露，例如 `UnsupportedWidgetAnimationTrack` 或 graph unsupported-node diagnostic。

最终状态：当前 spec 的 WidgetBlueprint AssetDocument 主区域已完成；剩余风险是未来 UE 类型家族扩展时需要添加对应 adapter，而不是把未知内容默认保留下来。

## 验证结果

- UBT: 成功。
  - 命令：`UnrealBuildTool.exe AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload`
  - 结果：`Target is up to date`，`Result: Succeeded`
- WidgetBlueprint focused automation: 成功。
  - Report: `C:/AVH1/Saved/AutomationReports/WidgetBlueprintFull/index.json`
  - 结果：58 succeeded，1 succeeded with warnings，0 failed，0 not run。
  - Warning: 已知 invalid-class 测试 warning，`WidgetTree.RejectsInvalidClass`。
- UBlueprint regression: 成功。
  - Report: `C:/AVH1/Saved/AutomationReports/UBlueprintRegression/index.json`
  - 结果：58 succeeded，0 failed，0 not run。
- GraphCore regression: 成功。
  - Report: `C:/AVH1/Saved/AutomationReports/GraphCoreRegression/index.json`
  - 结果：17 succeeded，0 failed，0 not run。
- Full AssetDocument automation: 成功。
  - Report: `C:/AVH1/Saved/AutomationReports/AssetDocumentFull/index.json`
  - 结果：194 succeeded，12 succeeded with warnings，0 failed，0 not run。
- MCP tests: 成功。
  - `npm test`
  - 结果：39 tests，39 pass，0 fail。
  - 备注：首次运行前需要 `npm ci` 安装依赖；`npm audit` 报告 8 个既有依赖漏洞（3 moderate，5 high），未阻塞本轮测试。
- External HTTP smoke: 成功。
  - Entrypoint: `docs/superpowers/verification/run_asset_document_widgetblueprint_smoke.ps1`
  - 命令：`powershell -NoProfile -ExecutionPolicy Bypass -File docs/superpowers/verification/run_asset_document_widgetblueprint_smoke.ps1 -Project C:/AVH1/AVH1.uproject -KeepSidecar`
  - 结果：`apply-file` 成功，`extract` 返回全部预期 WidgetBlueprint body regions，`diff` 无 unexpected changed entries。

## 外部 smoke 资产

- Asset path: `/Game/AssetDocumentSmoke/WBP_WidgetBlueprintSmoke`
- Sidecar path: `C:/AVH1/Content/AssetDocumentSmoke/WBP_WidgetBlueprintSmoke.assetdoc.json`
- `-KeepSidecar` 行为：保留 sidecar 文件，用于后续人工或自动复查。

## Review findings 和修复

- Task 6 fresh evidence 显示 UBT、WidgetBlueprint.Animations、WidgetBlueprint 自动化，以及 spec/quality re-review 已通过；Task 7 在此基础上只补集成、schema、MCP、外部 smoke 和最终报告。
- 新增 `ApplyFileCanonicalWriteback` 暴露 WidgetBlueprint graph schema 在 apply/extract hash 中不稳定：apply 后 UE 返回 `/Script/UMGEditor.WidgetGraphSchema`，标准 K2 graph 模型期望 `/Script/BlueprintGraph.EdGraphSchema_K2`。修复：在 graph hash canonicalization 中将 Widget graph schema 规范化为 K2 schema。
- 外部 smoke 初版使用 PowerShell 参数名 `$Host`，与内置只读变量冲突。修复：内部参数改为 `$ServerHost`，保留 `[Alias("Host")]` 兼容调用语义。
- 外部 smoke 初版在 `WidgetTree` authored property 上触发非目标 hash 差异。修复：smoke fixture 聚焦当前集成面，保留 variable widget、binding、animation、function graph、metadata 和 generated GUID canonical writeback 检查，不把 WidgetTree property canonicalization 噪声混入外部 smoke。
- MCP schema 文档已从早期保守文本更新为当前完整实现状态，并新增 MCP 测试断言，确认文档列出 `/Script/UMGEditor.WidgetBlueprint`、WidgetBlueprint body regions、unsupported diagnostics，同时不暴露 generator-only `AssetType: "WidgetBlueprint"` 输入路径。
