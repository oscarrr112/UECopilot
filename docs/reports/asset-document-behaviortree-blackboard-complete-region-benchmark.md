# BehaviorTree + BlackboardData AssetDocument Complete Region Benchmark

## Scope

- Branch: `feature/asset-document-behaviortree-blackboard-impl`
- Worktree: `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-behaviortree-blackboard-impl`
- Task base: `ccf3b07dee95d021ee9c58001f9a2978ac20e49d`
- Final range: `ccf3b07dee95d021ee9c58001f9a2978ac20e49d..` the Task 10 fix checkpoint commit containing this report update. The exact final head SHA is recorded by the checkpoint commit and final handoff evidence.
- Spec: `docs/superpowers/specs/2026-07-02-behaviortree-blackboard-asset-document-design.md`
- Plan: `docs/superpowers/plans/2026-07-02-behaviortree-blackboard-asset-document-implementation.md`

## Completed Managed Regions

- `UBlackboardData`
  - `Body.Parent`: optional parent `AssetRef<UBlackboardData>`.
  - `Body.Keys`: local authored keys, stable key identity by name, key type class, description, instance sync flag, and supported key type properties such as object base class and enum references.
- `UBehaviorTree`
  - `Body.Blackboard`: behavior tree blackboard `AssetRef`.
  - `Body.Tree`: semantic tree root composite, root decorators, `RootDecoratorLogic`, composite services, edge decorators, edge `DecoratorLogic`, tasks with reflected authored properties, `FBlackboardKeySelector`, and subtree `AssetRef`.
  - `Body.EditorLayout`: editor-authored graph node positions and comment boxes keyed by semantic node/comment ids.
- Integrated Task 10 roundtrip fixture covers Blackboard parent plus local keys, BehaviorTree blackboard reference, root composite, root decorator logic, service, edge decorator logic, selector task, subtree reference, and editor layout in one document.

## Excluded Fields

- Derived/cache/transient BehaviorTree runtime initialization state, compiled ordering caches, transient object paths, and generated object identities.
- UE editor graph internals not represented by supported `Body.EditorLayout` fields, such as pins, graph schema state, view settings, and per-user editor state.
- Referenced asset-owned data, including the full subtree asset body and parent BlackboardData body when only an `AssetRef` is authored.
- Empty generated structure arrays such as absent/empty service, child, decorator, and decorator-logic arrays may be normalized for sidecar sync hashing. Reflected authored node properties, including `FBlackboardKeySelector.BlackboardKey` and `AllowedTypes`, are not normalized away.

## Blockers

- HTTP/editor smoke is blocked by the validation host HTTP listener failing to bind. This is not counted as a passing smoke.
  - Start command: `Start-Process -FilePath 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe' -ArgumentList 'C:/Users/HP/.config/superpowers/validation-hosts/bt-bb-task1/AVH1.uproject','-NoSplash','-NoSourceControl' -WindowStyle Hidden -PassThru`
  - Editor PID: `46356`
  - Service file: `C:/Users/HP/.config/superpowers/validation-hosts/bt-bb-task1/Saved/AssetFactory/service.json`
  - Service file port: `8560`
  - Health probe: `Invoke-RestMethod http://127.0.0.1:8560/assetfactory/health`
  - Probe result: timed out after 120 seconds; last error was connection refused by `127.0.0.1:8560`.
  - Log path: `C:/Users/HP/.config/superpowers/validation-hosts/bt-bb-task1/Saved/Logs/AVH1.log`
  - Log evidence: `LogHttpListener: Error: HttpListener unable to bind to 127.0.0.1:8560`, followed by service discovery being written for port `8560`.

## Verification Evidence

- UBT against validation host:
  - Command: `& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/Users/HP/.config/superpowers/validation-hosts/bt-bb-task1/AVH1.uproject" -NoHotReload`
  - Result after SPEC REVIEW fix: exit `0`, `Result: Succeeded`, total execution time `5.72` seconds.
- Focused automation:
  - Command: `& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Users/HP/.config/superpowers/validation-hosts/bt-bb-task1/AVH1.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl "-ReportExportPath=C:/Users/HP/.config/superpowers/validation-hosts/bt-bb-task1/Saved/AutomationReports/BTBBFocused" "-ExecCmds=Automation RunTests AssetFactory.AssetDocument.BehaviorTree; Automation RunTests AssetFactory.AssetDocument.BlackboardData; Quit" -TestExit="Automation Test Queue Empty"`
  - Result after SPEC REVIEW fix: exit `0`.
  - Report: `C:/Users/HP/.config/superpowers/validation-hosts/bt-bb-task1/Saved/AutomationReports/BTBBFocused/index.json`
  - Summary: `succeeded=25`, `succeededWithWarnings=5`, `failed=0`, `notRun=0`, duration `0.439883708953857`.
  - Affected test evidence: `AssetFactory.AssetDocument.BehaviorTree.ApplyFileCanonicalWriteback` state `Success`, duration `0.0634196996688843`.
- Full AssetDocument automation:
  - Command: `& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Users/HP/.config/superpowers/validation-hosts/bt-bb-task1/AVH1.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl "-ReportExportPath=C:/Users/HP/.config/superpowers/validation-hosts/bt-bb-task1/Saved/AutomationReports/AssetDocumentFull" "-ExecCmds=Automation RunTests AssetFactory.AssetDocument; Quit" -TestExit="Automation Test Queue Empty"`
  - Result after SPEC REVIEW fix: exit `0`.
  - Report: `C:/Users/HP/.config/superpowers/validation-hosts/bt-bb-task1/Saved/AutomationReports/AssetDocumentFull/index.json`
  - Summary: `succeeded=337`, `succeededWithWarnings=27`, `failed=0`, `notRun=0`, duration `25.3909530639648`.
  - Affected test evidence: `AssetFactory.AssetDocument.BehaviorTree.ApplyFileCanonicalWriteback` state `Success`, duration `0.0713520050048828`.
- MCP tests:
  - Initial `npm --prefix MCP test` failed because `tsc` was not installed in `MCP/node_modules`.
  - Ran `npm --prefix MCP install`; this installed local test dependencies only. Generated `MCP/dist` and lockfile changes were restored before commit.
  - Final command after SPEC REVIEW fix: `npm --prefix MCP test`
  - Result: exit `0`, `# pass 39`, `# fail 0`, duration `2659.3167ms`.
  - Note: `npm install` reported existing dependency audit findings: `3` moderate and `5` high vulnerabilities. No dependency upgrade was made in this task.
- Diff and status evidence:
  - Command: `git diff --check ccf3b07dee95d021ee9c58001f9a2978ac20e49d..HEAD`
  - Result: recorded as required final checkpoint verification; exit/status is reported in the final handoff after the checkpoint commit.
  - Command: `git status --short`
  - Result: recorded as required final checkpoint verification; clean status is reported in the final handoff after the checkpoint commit.

## Reviewer Findings And Fixes

- Task 9 chain fixes already present at this task base:
  - `d5a1130 fix(assetdoc): create behavior tree editor graph through schema` fixed editor graph creation through the expected UE graph schema path.
  - `247f17f fix(assetdoc): preserve sparse editor layout semantics` fixed sparse `Body.EditorLayout` behavior so omitted layout fields do not delete authored comments.
  - `ccf3b07 test(assetdoc): cover explicit editor layout comment deletion` added explicit empty comment deletion coverage.
- Task 10 implementation fixes:
  - Rollback test initially asserted the old root diagnostic path. Fixed expected diagnostic to `/Body/Tree/Root/Class`.
  - `ApplyFile` sync initially skipped `Body.Tree` because authored sparse tree JSON and post-apply extracted evidence differed by generated defaults. Fixed by adding `BehaviorTreePostApply` region canonicalization on the `Body.Tree` policy.
  - `ApplyFile` sync then skipped `Body.EditorLayout` because extracted layout arrays are sorted by stable ids and comment colors roundtrip through `FLinearColor`. Fixed by adding an `EditorLayout` region canonicalizer that sorts `Nodes` by `NodeId`, sorts `Comments` by `Id`, and stabilizes numeric precision.
  - The production changes are intentionally scoped to profile policy hooks and region canonicalizer strategies; no service-level asset-class if-list was added.
- Task 10 SPEC REVIEW fixes:
  - Removed hardcoded BehaviorTree reflected property/default whitelists and the `/Script/AIModule.BTService_DefaultFocus` `BlackboardKey` special case from `BehaviorTreePostApply` canonicalization.
  - Narrowed `BehaviorTreePostApply` to generated empty structural array normalization only; it no longer removes `Properties`, `NodeName`, `BlackboardKey`, `AllowedTypes`, or reflected default values.
  - Updated the integrated Task 10 BehaviorTree authored fixture to build node `Properties` through the same reflected `ApplyProperties`/`ExtractAuthoredProperties` path used by the profile, so `ApplyFile` writeback is stable without hiding reflected authored properties.
  - Added `ApplyFileCanonicalWriteback` regression assertions that `MoveToTarget` selector `AllowedTypes` differences and `FocusService` `BlackboardKey` differences are reported by diff rather than swallowed by canonicalization.

## Final Status

- Complete managed BT+BB authored surface covered by tests and automation.
- UBT, focused automation, full AssetDocument automation, and MCP tests pass.
- HTTP/editor smoke remains blocked by local listener bind failure on the validation host and is recorded above as a non-passing smoke.
