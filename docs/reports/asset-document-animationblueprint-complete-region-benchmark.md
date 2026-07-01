# AnimationBlueprint AssetDocument Complete Region Benchmark

## Summary

`UAnimBlueprint` now has an exact AssetDocument profile with dispatcher-backed body regions, public/composed region adapters, and task-level checkpoint coverage.

Implemented body regions:

- Core object regions: `ParentClass`, `TargetSkeleton`, `Template`, `Preview`, `Optimization`.
- Named/common regions: `SyncGroups`, `ImplementedInterfaces`, `Variables`, `ClassDefaults`, `UbergraphPages`.
- Graph-family pilots: root-only `AnimGraph`; `StateMachines` and `TransitionGraphs` are bound to a public adapter boundary but remain empty-only until real graph materialization can roundtrip.
- Parent override region: `ParentAssetOverrides` by parent node GUID identity.

Still deferred by design:

- Authored AnimGraph pose nodes.
- Non-empty `StateMachines` authoring and real nested UE state-machine graph materialization.
- Non-empty `TransitionGraphs` authoring and authored transition rule graph nodes.
- `AnimLayers` in the regular `UAnimBlueprint` exact-class profile; Anim Layer Interface remains a separate profile/factory boundary.
- Parent override authored node alias resolver beyond raw parent node GUID identity.

## Verification

All commands below were run from:

```text
E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-animationblueprint-impl
```

UBT:

```text
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/Users/HP/.config/superpowers/validation-hosts/anim-blueprint-task1/AVH1.uproject" -NoHotReload
```

Result: passed, `Result: Succeeded`.

ABP focused automation:

```text
Automation RunTests AssetFactory.AssetDocument.AnimBlueprint
```

Report:

```text
C:/Users/HP/.config/superpowers/validation-hosts/anim-blueprint-task1/Saved/AutomationReports/AnimBlueprintImportantFix/index.json
```

Result after important fixes: `9 succeeded / 0 warnings / 0 failed / 0 notRun`.

AssetDocument full automation:

```text
Automation RunTests AssetFactory.AssetDocument
```

Report:

```text
C:/Users/HP/.config/superpowers/validation-hosts/anim-blueprint-task1/Saved/AutomationReports/AssetDocumentFinalAfterImportantFix/index.json
```

Result after important fixes: `succeeded=303`, `failed=0`, `notRun=0`.

MCP tests:

```text
npm --prefix MCP test
```

Result: `tests=39`, `pass=39`, `fail=0`.

Note: `MCP/node_modules` was installed locally to provide `tsc`; it is verification state only and must not be committed.

HTTP smoke runner:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File docs/superpowers/verification/run_asset_document_animationblueprint_smoke.ps1 -Project C:/Users/HP/.config/superpowers/validation-hosts/anim-blueprint-task1/AVH1.uproject -KeepSidecar
```

The runner writes an ABP sidecar, calls `/assetfactory/assetdocument/apply-file`, `/assetfactory/assetdocument/extract`, and `/assetfactory/assetdocument/diff`, then verifies class/body regions and no unexpected changed diff entries.

Current run result: blocked by the validation host HTTP listener, not by ABP AssetDocument logic.

- Attempt 1: default port `8559`; script started UnrealEditor, but `/assetfactory/health` did not become reachable before timeout.
- Attempt 2: temporary validation-host config set `HttpServerPort=8560`; `/assetfactory/health` still did not become reachable before timeout.
- Attempt 3: same `8560` with `bAllowRemoteConnections=True`; still timed out.
- Latest UE log evidence: `LogHttpListener: Error: HttpListener unable to bind to 127.0.0.1:8560`.
- The script remains the repeatable external smoke entrypoint once the host HTTP listener binding issue is resolved.

## Checkpoint Chain

- `7f5f4aa test: tighten animation blueprint profile gates`
- `06c37df feat: gate animation blueprint deferred graph regions`
- `04a8979 feat: validate animation blueprint core object regions`
- `40b4a75 feat: apply animation blueprint lifecycle documents`
- `e3bbab2 feat: manage animation blueprint sync groups`
- `a101823 feat: reuse blueprint common regions for animation blueprints`
- `2be77a4 feat: pilot animation blueprint anim graph region`
- `290d9da feat: manage animation blueprint state machines`
- `10e44f8 feat: manage animation blueprint layers and parent overrides`
- review fix: reject non-empty `StateMachines` / `TransitionGraphs` until apply/extract roundtrip exists, add core object diff, create skeleton gate, preview skeleton mismatch validation, and parent override extract-shape assertions.
- `a815587 fix: close animation blueprint review gaps` - fixes the Service.Apply create path `TargetSkeleton` gate, sparse diff authored-field compare, and update-only `Preview` skeleton mismatch fallback.

## Follow-up Gates

- Extend `FAssetDocumentAnimGraphRegionAdapter` before accepting real pose nodes.
- Extend `FAssetDocumentAnimStateMachineRegionAdapter` before accepting any non-empty `Body.StateMachines` or `Body.TransitionGraphs` value; the extension must include apply/extract/diff roundtrip so authored graph data is never silently discarded.
- Add a separate Anim Layer Interface profile or explicit region-extension spec before accepting non-empty `Body.AnimLayers`.
- Add an AnimGraph identity resolver before allowing parent override aliases other than raw parent node GUID.
