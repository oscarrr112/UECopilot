# AssetDocument UBlueprint Phase 1 验证报告

## 范围

- Branch: `feature/asset-document-ublueprint-impl`
- Worktree: `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-ublueprint-impl`
- Spec base: `c7e123bfc59212597c0be89094271cc4bb98a273`
- Task 6 base: `6efcbf28131bf05a4a393e072486e1475e3c01cb`
- Final commit: containing checkpoint commit; use `git log -1` for the exact hash.

本阶段只实现普通 `UBlueprint` AssetDocument profile。没有新增 Blueprint Generator，也没有新增 Blueprint 专用 MCP tool；MCP 仍使用通用 AssetDocument tools 和 profile/schema inspection。

## 已实现能力

- `/Script/Engine.Blueprint` exact profile 注册、schema hint、region policy 和 template body。
- `ParentClass` 创建/更新，使用 UE Blueprint factory/editor utility 路径生成普通 `UBlueprint`。
- `ImplementedInterfaces` 权威数组管理。
- `Variables` 权威数组管理，包括新增、更新、删除、metadata 清理、默认值应用、默认值持久化和 diff/extract roundtrip。
- `Components` 权威管理：
  - `OwnedSCS` component tree 新增、更新、删除、root/attach 校验和结构 preflight。
  - `Inherited` SCS component property override，使用 `UInheritableComponentHandler`。
  - `Native` component property override，应用到 generated CDO native component，并支持 object-name/alias canonical diff。
- `ClassDefaults` generated CDO default value delta apply/reset/extract/diff。
- 非空 `UbergraphPages`、`FunctionGraphs`、`MacroGraphs`、`Timelines` 明确拒绝。

## 明确拒绝或延期

- 不支持 `UWidgetBlueprint`、`UAnimBlueprint` 或 specialized Blueprint-derived assets。
- 不支持 graph/timeline authoring；相关 `Body` region 目前只能为空数组。
- `Inherited` / `Native` component 的 `AttachTo` 和 `Root` override 暂不支持，会返回 `UnsupportedInheritedComponentAttachRoot`。第一版只保证 inherited/native property override。
- HTTP smoke 中 apply-file 后 sidecar sync 层仍会提示 `Body.Variables` evidence hash 未重写；但后续 diff 对 authored variable/component region 已经是 unchanged。该项记录为 sync evidence canonicalization 的后续风险，不阻塞 phase 1 authoring/apply/diff。

## Task 6 期间发现并修复的问题

HTTP smoke 首次暴露了保存并重启 Editor 后 `/Body/Variables/Health` 被 diff 为 changed：desired `DefaultValue` 为 `"100.0"`，extract/current 为 `""`。修复方式：

- extract/diff 当前变量默认值时优先从 generated CDO 同名 `FProperty` 读取 canonical import text。
- `ApplyVariableDefaultsToGeneratedClass` 在默认值解析成功后写回 `FBPVariableDescription.DefaultValue` 并 mark Blueprint modified，保证保存后仍可 roundtrip。
- 避免在 `AddMemberVariable` 阶段提前传入非法默认值，防止 rollback/preflight 测试制造 UE compiler warning。
- 增加 automation 断言，确保 `Health` 变量默认值持久化到 variable description。

## 验证结果

- UBT:
  - Command: `"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload`
  - Result: succeeded.
- Focused UBlueprint automation:
  - Command: `"E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/UBlueprintFinalClean"`
  - Result: `20` succeeded, `0` succeededWithWarnings, `0` failed, `0` notRun.
- Full AssetDocument automation:
  - Command: `"E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AssetDocumentFullFinal"`
  - Result: `81` succeeded, `11` succeededWithWarnings, `0` failed, `0` notRun.
- MCP tests:
  - Command: `Push-Location MCP; npm ci; npm test; Pop-Location`
  - Result: `39` passed, `0` failed.
  - Note: `npm ci` reported existing dependency audit findings: `3` moderate and `5` high vulnerabilities.
- HTTP smoke:
  - Command: `python docs/superpowers/verification/asset_document_ublueprint_http_smoke.py --project C:/AVH1/AVH1.uproject --host 127.0.0.1 --port 8559 --health-timeout 240`
  - Result: passed.
  - Asset: `/Game/AssetDocumentSmoke/BP_BlueprintSidecarSmoke`
  - Asset file: `C:/AVH1/Content/AssetDocumentSmoke/BP_BlueprintSidecarSmoke.uasset`
  - Sidecar: `C:/AVH1/Content/AssetDocumentSmoke/BP_BlueprintSidecarSmoke.assetdoc.json`

## 主要风险

- `Body.Variables` sidecar sync evidence hash 仍可能因为 canonicalization 差异跳过 post-apply sidecar rewrite；当前 diff 验证已覆盖 authored value 稳定性。
- `ClassDefaults` 仅覆盖当前 property adapter 可表达的 writable values；extract 会用 `_Skipped.ClassDefaults` 报告 unsupported writable differences。
- inherited/native attach/root override 需要单独 UE API 机制确认后再开放 capability。
