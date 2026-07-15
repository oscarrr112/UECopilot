# AssetDocument Live Apply Transaction Design

## Scope

Task 7b2 closes only the in-memory mutation boundary of
`FAssetDocumentService::Apply`. It does not claim package-save rollback,
fresh-load verification, disk-byte restoration, or sidecar atomicity; those
remain Task 7c.

The service must stage the complete document on a transient object before live
mutation, then provide one generic transaction around lifecycle materialization,
reflected `Properties`, and profile `Body` apply. BehaviorTree and BlackboardData
keep their existing profile-owned atomic apply logic. The service must not add
class-specific BT/BB branches.

## Considered approaches

1. Use Unreal's editor `FScopedTransaction`/undo stack. This is not selected:
   automation and headless service calls cannot depend on editor undo state,
   and undo does not define Asset Registry or new-package cleanup.
2. Duplicate the whole asset and swap the duplicate into the package. This is
   not selected: replacing the top-level object breaks stable identity and
   external references, and profile-owned graph/subobject outers still need
   domain-aware repair.
3. Use an explicit service transaction with reflected-property and ownership
   snapshots. This is selected. It preserves top-level identity, delegates
   Body-internal rollback to the profile that owns those invariants, and gives
   the service one place to restore cross-phase state and remove new assets.

## Transaction boundary

Introduce private `FAssetDocumentApplyTransaction` state created after all
7b1 preflight and complete-document staging succeed and before `CreateOrLoad`
can mutate live state.

For an existing target, staging duplicates the real asset to a transient outer,
then applies reflected `Properties` followed by the whole profile `Body` to that
duplicate. For a new target, a lifecycle-owned preview constructor creates the
correct object shape inside a unique `/Temp` package marked `RF_Transient`, after
which the same ordered Properties/Body staging runs. Blueprint-family previews
use direct Kismet construction so Widget preview creation does not broadcast
the global real-asset creation event. Successful staging performs profile
extraction or equivalent structural verification on the transient result. No
target package, Asset Registry event, file, or live asset mutation is allowed
during staging.

For an existing asset it records:

- the exact top-level asset pointer and package;
- the package dirty flag;
- a JSON snapshot of all writable reflected properties excluding the exact
  profile's `ManagedPropertyPaths`;
- the exact recursive set returned by `GetObjectsWithOuter(Asset, ..., true)`.

For a new target it records:

- the `FAssetDocumentLifecycleResult` after materialization, including target
  object path, created asset, and whether live creation occurred;
- the lifecycle cleanup responsibility for the already announced asset and its
  target package;
- the exact pre-materialization package-object identity set as weak handles,
  plus transaction-period strong guards for those baseline objects. `Resolve`
  captures this state before transient staging, including when the package
  exists but the target asset does not, so Blueprint staging and live compile
  garbage collection cannot destroy unrelated pre-existing package objects.

The transaction is explicitly committed only after the live Apply phases in
this task succeed. A destructor must not silently perform complex rollback;
all failure exits call `Rollback`, collect its diagnostics, and return them.

## Dirty existing assets

Task 7b2 uses fail-closed behavior only for durable Apply. After complete
staging succeeds, an existing dirty package with `bSaveAsset=true` returns
`DirtyExistingAssetTransactionUnsupported` before writing a live property or
Body region. With `bSaveAsset=false`, the transaction snapshots the current
in-memory values and dirty flag, so an update can compose with unsaved editor
state and a forced failure restores that exact current state.

## Rollback behavior

### Existing asset

When reflected `Properties` succeeded and a later service phase fails, the
transaction reapplies the reflected snapshot and restores the original dirty
flag. Any recursively owned object not present in the original set is detached
to the transient package and marked for garbage collection. If the exact set
cannot be restored, rollback reports a secondary diagnostic rather than hiding
the primary failure.

When a profile `Body` apply fails, that profile remains responsible for
restoring its own references, graphs, caches, and recursively owned objects.
The service transaction then restores any preceding reflected-property change,
restores dirty state, and verifies the recursive owned set. This composes the
two atomicity layers without teaching the service BT/BB details.

Existing-asset rollback always runs this reflected/asset-owned restoration
first. It then recomputes lifecycle cleanup work and removes any remaining
package-level delta, such as a late Blueprint generated-class companion. These
two rollback layers are composed rather than selected as alternatives.

Task 7b2 does not inject a service failure after a successful live Body apply
for an existing asset. Such a later failure needs a restorable Body snapshot
and belongs with Task 7c's save/fresh/verification transaction.

### New asset

Any failure after package creation calls the generic lifecycle cleanup. The
lifecycle result explicitly records its package, whether the operation owns
that package, whether the asset was announced to the registry, the original
package dirty state, every object created during materialization, and the exact
pre-materialization package-object baseline. Cleanup emits `AssetDeleted` only
for an announced asset and removes the current package-object delta, including
companions produced by later Blueprint compile/reinstance after the initial
created-object capture. A pre-existing package is never cleared or marked as
garbage; its baseline objects are pinned across every staging/live/rollback GC,
only the newly created delta is removed, and its dirty state is restored.
Package-only and asset-created/pre-registry failures emit no registry events
and must be retryable. A non-empty created-object/delta set itself is sufficient
to require cleanup even when all lifecycle state flags are false.

An Asset Registry `AssetAdded` delegate call cannot be retracted. Therefore:

- preflight failures remain 7b1's zero-event contract;
- forced failures after materialization observe one `AssetAdded`, followed by
  one `AssetRemoved`/`AssetDeleted`, and no final registry/object/package/file
  ghost;
- later work should continue moving fallible work before materialization where
  possible.

## Private failure hooks

`AssetDocumentServiceTestHooks.{h,cpp}` defines automation-only, single-use
hooks for `AfterStagedDocument`, `AfterNewLiveMaterialize`,
`AfterLiveProperties`, and `AfterNewLiveBody`, plus a single-use
rollback-verification diagnostic hook. Lifecycle creation has package-created
and asset-created/pre-registry hooks. The hooks are private to the module and
do not alter `FAssetDocumentService`'s public header. The two `New` phases must
never fire for an existing asset; `AfterNewLiveBody` is not an assertion that
Task 7b2 can roll back a successfully applied existing Body.

The phase hook is consumed immediately when its exact phase is reached. Each
armed Apply failure receives a game-thread-only generation token, and the
rollback hook is bound to that generation. An unrelated real rollback cannot
consume it; it is consumed only by the transaction handling that forced primary
failure.
Tests clear both hooks with an RAII guard to prevent cross-test contamination.

Failure results retain diagnostics in this order:

1. all diagnostics accumulated before the failure;
2. the primary phase/profile failure diagnostic;
3. rollback cleanup or verification diagnostics.

The result message remains the primary failure message even when rollback also
reports a problem.

## Test design

| Group | Observable contract |
| --- | --- |
| Complete staging | Generic, BlackboardData, BehaviorTree, UBlueprint, AnimBlueprint, and WidgetBlueprint fail after whole-document staging with no target object/package/registry/file or preview companion leak; retry succeeds with exact Blueprint generated-class identity. |
| Existing Generic | Forced failure after live Properties restores scalar properties, exact recursive ownership set, and clean dirty flag; retry succeeds. |
| Dirty Generic | Durable Apply fails after staging and before live mutation; in-memory-only Apply succeeds against current dirty state, and forced rollback restores that current state. |
| Existing BlackboardData | Forced Body failure preserves canonical Body, key object identity/set, recursive owned set, and dirty flag. |
| Existing BehaviorTree | Forced graph swap failure preserves canonical Body, graph/runtime objects, recursive owned set, and dirty flag. |
| New Generic | Forced post-registry and post-live-Properties failures emit Added then Removed, leave no ghost/file, and retry succeeds. |
| New BlackboardData | Forced post-new-live-Body failure removes the new key/object tree and all lifecycle ghosts; retry succeeds. |
| New BehaviorTree | Forced post-new-live-Body failure removes graph/runtime ownership and all lifecycle ghosts; retry succeeds. |
| Lifecycle ownership | Package-only and pre-registry failures have zero registry events; announced cleanup preserves a pre-existing package and its dirty flag. |
| Pre-existing package | Blueprint, AnimBlueprint, and WidgetBlueprint failure after live Body removes late package-level companions, preserves the exact original package object identities (including non-Standalone objects with no external strong reference), and succeeds on retry. |
| Owned-object cleanup | A newly created rooted parent/child object tree is unrooted, detached child-first, marked garbage or already reclaimed, the exact ownership set and reflected Properties are restored, and weak references expire after GC. |
| Partial lifecycle cleanup | Created-object or current-package delta cleanup runs even when `bCreated`, `bOwnsPackage`, and `bRegistryAnnounced` are all false, without touching baseline package objects. |
| Diagnostics | Primary phase diagnostic remains first and an injected rollback diagnostic is appended, never substituted. |

The new tests live in
`Source/AssetDocument/Private/Tests/AssetDocumentApplyTransactionTests.cpp`.
The RED checkpoint was recorded before production wiring. Production now routes
the hooks through the service transaction; UBT and Editor verification still
require the shared Unity window to be explicitly acquired.

Explicitly excluded from Task 7b2: failure after an existing asset's Body has
successfully committed, package save, fresh reload, canonical/structural
verification of persisted bytes, and sidecar write. Task 7c must cover those
states with a persistence-capable snapshot rather than extending this live-only
claim by wording.

## Production files

- Create `Source/AssetDocument/Private/AssetDocumentApplyTransaction.h`.
- Create `Source/AssetDocument/Private/AssetDocumentApplyTransaction.cpp`.
- Modify `Source/AssetDocument/Private/AssetDocumentLifecycle.h` for a generic
  transient-preview entry point backed by an owned `RF_Transient` package and
  explicit live package/registry ownership state.
- Modify `Source/AssetDocument/Private/AssetDocumentLifecycle.cpp` for that
  transient-preview implementation.
- Create `Source/AssetDocument/Private/AssetDocumentServiceTestHooks.h`.
- Create `Source/AssetDocument/Private/AssetDocumentServiceTestHooks.cpp`.
- Modify `Source/AssetDocument/Private/AssetDocumentService.cpp` only for
  generic transaction orchestration and private phase checks.
- Add
  `Source/AssetDocument/Private/Tests/AssetDocumentApplyTransactionTests.cpp`.
