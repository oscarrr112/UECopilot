# BehaviorTree + BlackboardData AssetDocument Production Benchmark

## Scope

- Verification date: `2026-07-16`
- Branch: `codex/finish-bt-bb-assetdocument`
- Worktree: `/Users/pengao/Documents/AssetFactory/UECopilot/.worktrees/finish-bt-bb-assetdocument`
- UE: `/Volumes/External/Unreal/Engines/UnrealEngine`
- Validation project: `/Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/AssetFactorySandbox.uproject`
- Plugin link: `/Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/Plugins/AssetFactory` points only to this worktree.
- HTTP: `127.0.0.1:8562`
- Production design: `docs/superpowers/specs/2026-07-15-behaviortree-blackboard-asset-document-production-design.md`
- Surface inventory: `docs/superpowers/specs/asset-document-surface-inventory/2026-07-15-behaviortree-blackboard.md`
- Deferred/excluded audit: `docs/superpowers/specs/asset-document-deferred-fields/2026-07-15-behaviortree-blackboard.md`
- Production plan: `docs/superpowers/plans/2026-07-15-behaviortree-blackboard-production-implementation.md`

This benchmark covers only the corrected BT/Blackboard Goal boundary: BehaviorTree, BlackboardData, Persistence, AtomicFile, RegionCanonicalizer, ManagedProperties, default Unity Build, MCP/Python smoke, and the real 8562 restart flow.

The full `AssetFactory.AssetDocument` aggregate suite is not a completion gate for this Goal. AnimBlueprint/ABP is an unrelated known incomplete capability handled by another branch. An aggregate failure under `AnimBlueprint.StateMachines`, AnimGraph snapshot, or UObject refcount paths must not be attributed to BT/Blackboard and is not represented as a blocker here.

## Managed Authored Regions

### `UBlackboardData`

- `Body.Parent`
  - Optional parent `AssetRef<UBlackboardData>`.
  - Parent-chain cycle rejection.
  - Inherited lookup and local parent-shadow rejection.
- `Body.Keys`
  - Ordered local keys; canonicalization never sorts them because order affects Blackboard key IDs.
  - Stable identity by key name, duplicate rejection, description, category, instance sync, explicit `KeyTypeClass`, and reflected key-type properties/defaults.
  - UE 5.7 built-in Bool/Int/Float/Name/String/Vector/Rotator/Object/Class/Enum plus Struct/custom key-type authored metadata supported by the shared reflected-property runtime.
  - Parent changes refresh retained derived caches; failed apply restores asset objects, caches, package bytes, and sidecar state.

### `UBehaviorTree`

- `Body.BlackboardAsset`
  - Canonical BehaviorTree Blackboard reference.
  - Sparse Blackboard replacement is an overlay on an existing graph, not a create/lifecycle shortcut.
- `Body.Tree`
  - `BTGraph` is the authored source of truth; runtime `RootNode`, Children, Services, and DecoratorOps are rebuilt by UE.
  - Persistent `NodeGuid` identity is independent from authored `NodeName`.
  - Root/nested composites, tasks, services, ordinary decorators, composite decorator logic graphs, semantic child order, subtree references, editor positions, node comments/bubbles, and Comment Boxes.
  - Any valid loadable non-abstract native, Blueprint, Angelscript, or project BT node class is resolved dynamically.
  - Safe instance-editable properties use the shared reflected runtime; no AIModule class whitelist or fixture-only path is used.
  - `FBlackboardKeySelector` authors only canonical public `Key`; AllowedTypes, resolved key type/ID, and None policy remain class-derived policy/cache.
  - Generated pin objects and other editor internals are excluded, while logical graph links are managed and validated.

## Excluded State

- Runtime/debug/cache/transient state, generated UObject identity, selector key IDs/types, ValueOrBlackboardKey cached IDs, compiled execution data, and other UE rebuild products.
- Per-user editor state, graph view settings, schema objects, generated pins, and transient graph indexes.
- Referenced-asset-owned bodies such as a subtree asset or parent Blackboard body when the current document only authors an `AssetRef`.
- These exclusions are recorded in the deferred/excluded audit and are not writable authored gaps.

## Final Canonicalization Fixes

The real 8562 smoke found two strict sidecar-sync mismatches that focused automation did not previously expose:

1. Sparse BT node properties versus reflected CDO defaults.
   - `UBTTask_Wait` extract materialized `WaitTime={DefaultValue:5, Key:"None"}` and `RandomDeviation={DefaultValue:0, Key:"None"}` although the sidecar legally omitted both.
   - `BehaviorTreePostApply` now dynamically resolves each `UBTNode` subclass and its CDO, and removes only property values exactly equal to the reflected CDO baseline from the hash clone.
   - No concrete node class or property name is hardcoded.
   - Non-default values remain hash-significant.
   - Class/CDO/default extraction failure leaves the JSON unchanged, so strict persistence fails closed.

2. Non-semantic storage order.
   - Extract and diff already treat Tree Comments and composite-decorator BoundGraph Nodes/Links by stable identity.
   - Hash canonicalization now sorts Comments by `Id`, BoundGraph Nodes by `Id`, and BoundGraph Links by `From`, `To`, `ToInput`.
   - Semantic arrays such as BT Children, Services, and Decorators are not sorted.
   - Hash-only sorting does not change sidecar writeback array order; writeback still follows the existing default-field normalization.

Code-review hardening:

- Schema-aware traversal.
   - Default removal and storage-order normalization traverse only the BehaviorTree schema: Tree Root, Children, Services, Decorators, and direct composite-decorator BoundGraph Nodes/Links.
   - Reflected authored `Properties` remain opaque even when custom data happens to contain fields named `Class`, `Properties`, or `BoundGraph`.
   - Unresolved classes, non-BT classes, and extraction failures remain fail-closed and hash-significant.

TDD evidence:

- Node CDO defaults RED: `BTBB_SUBTREE_DEFAULTS_RED_R2`, `1/1` failed with unequal hashes.
- Node CDO defaults GREEN: `BTBB_SUBTREE_DEFAULTS_GREEN_R1`, `1/1` passed.
- Storage order RED: `BTBB_STORAGE_ORDER_RED_R2`, `1/1` failed with unequal hashes.
- Storage order GREEN: `BTBB_STORAGE_ORDER_GREEN_R1`, `1/1` passed.
- Schema traversal review RED: `BTBB_SCHEMA_TRAVERSAL_REVIEW_RED_R2`, `1/1` failed because authored property shapes were incorrectly normalized.
- Schema traversal review GREEN: `BTBB_SCHEMA_TRAVERSAL_REVIEW_GREEN_R1`, BehaviorTreePostApply `7/7` passed.
- CDO extraction failure coverage: `BTBB_EXTRACTION_FAILURE_GREEN_R1`, `1/1` passed using a valid `UBTTaskNode` whose integer-key Map is unsupported by the reflected extractor.
- Final combined scoped regression: `BTBB_SCOPED_FINAL_AFTER_REVIEW_R2`, `161/161`, `0` warnings, `0` failures.

Automation reports are under:

`/Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/Saved/Automation`

## Build And Focused Verification

Default Unity Build, with no `-DisableUnity`:

```text
/Volumes/External/Unreal/Engines/UnrealEngine/Engine/Build/BatchFiles/Mac/Build.sh \
  AssetFactorySandboxBTBBEditor Mac Development \
  -Project=/Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/AssetFactorySandbox.uproject \
  -WaitMutex -NoUBA -NoHotReload
```

Result: `Succeeded`.

The UE/Xcode PostBuildSync output mentioned an ABP workspace, but the actual UBT target, compiled/link output, metadata, deploy target, and result were `AssetFactorySandboxBTBBEditor` and `UnrealEditor-AssetDocument.dylib`. Per the corrected scope, that unrelated workspace message was recorded and not expanded into ABP investigation.

Fresh focused evidence:

| Gate | Report/command | Result |
| --- | --- | ---: |
| BehaviorTree | `BTBB_SCOPED_FINAL_AFTER_REVIEW_R2` | `69/69`, 0 warnings, 0 failures |
| BlackboardData | `BTBB_SCOPED_FINAL_AFTER_REVIEW_R2` | `33/33`, 0 warnings, 0 failures |
| Persistence | `BTBB_SCOPED_FINAL_AFTER_REVIEW_R2` | `18/18`, 0 warnings, 0 failures |
| AtomicFile | `BTBB_SCOPED_FINAL_AFTER_REVIEW_R2` | `11/11`, 0 warnings, 0 failures |
| RegionCanonicalizer | `BTBB_SCOPED_FINAL_AFTER_REVIEW_R2` | `28/28`, 0 warnings, 0 failures |
| ManagedProperties | `BTBB_SCOPED_FINAL_AFTER_REVIEW_R2` | `2/2`, 0 warnings, 0 failures |
| Combined scoped UE total | `BTBB_SCOPED_FINAL_AFTER_REVIEW_R2` | `161/161`, 0 warnings, 0 failures |
| MCP | `npm test` in `MCP` | `39/39`, 0 failures |
| Python harness unit | `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v docs.superpowers.verification.test_btbb_asset_document_live_smoke` | `8/8`, `OK` |

## 8562 HTTP + MCP Restart Evidence

- Evidence root:
  `/Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/Saved/AssetFactory/Verification/BTBB-final-review-20260716-1913`
- Final summary:
  `/Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/Saved/AssetFactory/Verification/BTBB-final-review-20260716-1913/summary.json`
- Run ID: `a346c0ca-1a49-4eb8-80fa-05306af38190`
- Asset root: `/Game/AssetDocumentSmoke/BTBBFinalReview`
- Pre-restart:
  - listener PID `90644`
  - service start `2026-07-16T11:13:16.746Z`
  - HTTP `passed`
  - MCP `passed`
  - failed/missing/extra/skipped all empty
- Post-restart:
  - listener PID `97092`
  - service start `2026-07-16T11:16:08.188Z`
  - fresh HTTP extract/diff `passed`
  - failed/missing/extra/skipped all empty
- Editor logs:
  - `/Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/Saved/Logs/BTBB-final-review-live-pre2-20260716_2.log`
  - `/Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/Saved/Logs/BTBB-final-review-live-post-20260716.log`

Persisted assets and sidecars:

- `BB_BTBB_Parent.uasset` / `BB_BTBB_Parent.assetdoc.json`
- `BB_BTBB_Main.uasset` / `BB_BTBB_Main.assetdoc.json`
- `BT_BTBB_Subtree.uasset` / `BT_BTBB_Subtree.assetdoc.json`
- `BT_BTBB_Main.uasset` / `BT_BTBB_Main.assetdoc.json`

All are under:

`/Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/Content/AssetDocumentSmoke/BTBBFinalReview`

## Final Status

- All corrected BT/Blackboard Goal gates pass.
- The real save → restart → fresh reload → extract/diff path passes on 8562 with a changed listener PID.
- `git diff --check` passes.
- ABP remains explicitly out of scope and is handled by another branch.
- Implementation checkpoint: `aa3637ba3ec795c7919f891153891bf163d18de9`.
- The documentation closure commit and final clean-worktree HEAD are recorded in the delivery handoff.
