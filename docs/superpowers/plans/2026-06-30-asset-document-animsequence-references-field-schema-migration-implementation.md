# AssetDocument AnimSequence References Field Schema Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate AnimSequence `Body.References` field allow-list and JSON type validation from `RejectUnknownObjectFields` to `FAssetDocumentObjectFieldSchemaUtils` while preserving existing reference semantics.

**Architecture:** `FAssetDocumentObjectRegionAdapter` stays schema-agnostic. AnimSequence declares a `FAssetDocumentObjectFieldSchema` beside the existing `Preview` and `Playback` schema helpers, then calls `ValidateAnimSequenceObjectFieldSchema` from `ValidateBodyObjectShape`. Asset reference loading, skeleton compatibility, retarget source behavior, and null semantics remain in the existing AnimSequence parser/apply hooks.

**Tech Stack:** Unreal Engine 5.7 C++ plugin code, UE JSON DOM, UE Automation Tests, AssetDocument public region runtime, existing `C:/AVH1` validation host.

---

## Source Specs And Current Base

- Design spec: `docs/superpowers/specs/2026-06-30-asset-document-object-field-schema-dispatcher-migration-design.md`
- Refactor chain: `docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md`
- Existing implementation branch: `feature/asset-document-object-field-schema-dispatcher-migration`
- Existing worktree: `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-object-field-schema-dispatcher-migration`
- Current prerequisite commit: `cf2a93c fix: preserve nullable anim sequence preview mesh schema`

## Non-Negotiable Constraints

- Do not add schema config to `FAssetDocumentObjectRegionAdapter`.
- Do not add inheritance-based capability base classes.
- Do not migrate Additive, RootMotion, Compression, Curves, Notifies, NotifyStates, NotifyTracks, SyncMarkers, Metadata, AssetUserData, AnimMontage, UBlueprint, or WidgetBlueprint in this follow-up.
- Do not change MCP TypeScript contracts.
- Preserve existing diagnostics:
  - Unknown `Body.References.*` fields keep `UnsupportedAuthoredField`.
  - Invalid `Body.References.Skeleton` object type keeps `InvalidObjectReference`.
  - Invalid `Body.References.RetargetSource` string type keeps `InvalidStringField`.
  - Invalid `Body.References.RetargetSourceAsset` object type keeps `InvalidObjectReference`.
  - Authored `Body.References.Skeleton = null` remains rejected by semantic parsing with `NullNotAllowed`.
  - Authored `Body.References.RetargetSourceAsset = null` remains allowed and is handled by semantic parsing as an explicit clear.
- Every implementation task must record `TASK_BASE=$(git rev-parse HEAD)` and commit its own checkpoint before review.
- Review scopes must use `TASK_BASE..HEAD`.

## File Map

- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
  - Add `MakeAnimSequenceReferencesSchema()`.
  - Replace `RejectUnknownObjectFields(BodyObject, TEXT("References"), ...)` with `ValidateAnimSequenceObjectFieldSchema(BodyObject, TEXT("References"), MakeAnimSequenceReferencesSchema())`.
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`
  - Extend `FAssetDocumentAnimSequenceScalarRegionsTest` with References schema regression tests.
- Do not modify:
  - `Source/AssetDocument/Private/Regions/AssetDocumentObjectRegionAdapter.*`
  - `Source/AssetDocument/Private/Regions/AssetDocumentObjectFieldSchemaUtils.*` unless a test exposes a missing generic behavior.

## Task 1: Migrate AnimSequence References Field Schema

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: clean worktree on `feature/asset-document-object-field-schema-dispatcher-migration`.

- [ ] **Step 2: Add failing References schema regression tests**

In `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`, locate `FAssetDocumentAnimSequenceScalarRegionsTest::RunTest` near the existing `UnknownPreviewFieldBody` and `UnknownPlaybackFieldBody` checks. Add this block before the `UnknownPreviewFieldBody` block:

```cpp
	TSharedRef<FJsonObject> UnknownReferencesFieldBody = MakePlaybackRateBody(4.55);
	TSharedRef<FJsonObject> UnknownReferences = MakeShared<FJsonObject>();
	UnknownReferences->SetStringField(TEXT("RetargetSourceAsset/Bad~Field"), TEXT("unexpected"));
	UnknownReferencesFieldBody->SetObjectField(TEXT("References"), UnknownReferences);
	const FAssetDocumentCapabilityResult UnknownReferencesResult =
		Capability.Validate(Context, MakeBodyValue(UnknownReferencesFieldBody));
	TestFalse(TEXT("Validate rejects unknown Body.References field"), UnknownReferencesResult.bSuccess);
	TestTrue(
		TEXT("Unknown Body.References field diagnostic uses escaped field path"),
		HasDiagnostic(UnknownReferencesResult, TEXT("/Body/References/RetargetSourceAsset~1Bad~0Field"), TEXT("UnsupportedAuthoredField")));

	TSharedRef<FJsonObject> InvalidSkeletonTypeBody = MakePlaybackRateBody(4.56);
	TSharedRef<FJsonObject> InvalidSkeletonReferences = MakeShared<FJsonObject>();
	InvalidSkeletonReferences->SetStringField(TEXT("Skeleton"), TEXT("not an asset ref object"));
	InvalidSkeletonTypeBody->SetObjectField(TEXT("References"), InvalidSkeletonReferences);
	const FAssetDocumentCapabilityResult InvalidSkeletonTypeResult =
		Capability.Validate(Context, MakeBodyValue(InvalidSkeletonTypeBody));
	TestFalse(TEXT("Validate rejects non-object Body.References.Skeleton"), InvalidSkeletonTypeResult.bSuccess);
	TestTrue(
		TEXT("Invalid Body.References.Skeleton diagnostic is stable"),
		HasDiagnostic(InvalidSkeletonTypeResult, TEXT("/Body/References/Skeleton"), TEXT("InvalidObjectReference")));

	TSharedRef<FJsonObject> InvalidRetargetSourceTypeBody = MakePlaybackRateBody(4.57);
	TSharedRef<FJsonObject> InvalidRetargetSourceReferences = MakeShared<FJsonObject>();
	InvalidRetargetSourceReferences->SetNumberField(TEXT("RetargetSource"), 123.0);
	InvalidRetargetSourceTypeBody->SetObjectField(TEXT("References"), InvalidRetargetSourceReferences);
	const FAssetDocumentCapabilityResult InvalidRetargetSourceTypeResult =
		Capability.Validate(Context, MakeBodyValue(InvalidRetargetSourceTypeBody));
	TestFalse(TEXT("Validate rejects non-string Body.References.RetargetSource"), InvalidRetargetSourceTypeResult.bSuccess);
	TestTrue(
		TEXT("Invalid Body.References.RetargetSource diagnostic is stable"),
		HasDiagnostic(InvalidRetargetSourceTypeResult, TEXT("/Body/References/RetargetSource"), TEXT("InvalidStringField")));

	TSharedRef<FJsonObject> NullRetargetSourceAssetBody = MakePlaybackRateBody(4.58);
	TSharedRef<FJsonObject> NullRetargetSourceAssetReferences = MakeShared<FJsonObject>();
	NullRetargetSourceAssetReferences->SetField(TEXT("RetargetSourceAsset"), MakeShared<FJsonValueNull>());
	NullRetargetSourceAssetBody->SetObjectField(TEXT("References"), NullRetargetSourceAssetReferences);
	const FAssetDocumentCapabilityResult NullRetargetSourceAssetValidateResult =
		Capability.Validate(Context, MakeBodyValue(NullRetargetSourceAssetBody));
	TestTrue(TEXT("Validate accepts null Body.References.RetargetSourceAsset for clearing"), NullRetargetSourceAssetValidateResult.bSuccess);
	const FAssetDocumentCapabilityResult NullRetargetSourceAssetApplyResult =
		Capability.Apply(Context, MakeBodyValue(NullRetargetSourceAssetBody));
	TestTrue(TEXT("Apply accepts null Body.References.RetargetSourceAsset for clearing"), NullRetargetSourceAssetApplyResult.bSuccess);
```

Run the focused automation before production changes:

```powershell
$report='C:/AVH1/Saved/AutomationReports/ReferencesSchemaRedAnimSequenceScalar'
if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report -Recurse -Force }
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence.ScalarRegions;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected before implementation: the unknown field test fails on the unescaped path if it still uses `RejectUnknownObjectFields`, or the typed field tests fail if schema utility is not yet connected. If all pass because existing parser already covers them, record that this is a compatibility-locking regression test before continuing.

- [ ] **Step 3: Add the References schema helper**

In `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`, add this helper after `MakeAnimSequencePlaybackSchema()`:

```cpp
FAssetDocumentObjectFieldSchema MakeAnimSequenceReferencesSchema()
{
	FAssetDocumentObjectFieldSchema Schema;
	Schema.Fields.Add({
		TEXT("Skeleton"),
		EJson::Object,
		false,
		TEXT("MissingSkeleton"),
		TEXT("InvalidObjectReference"),
	});
	Schema.Fields.Add({
		TEXT("RetargetSource"),
		EJson::String,
		false,
		TEXT("MissingRetargetSource"),
		TEXT("InvalidStringField"),
		TEXT("RetargetSource must be a string"),
	});
	Schema.Fields.Add({
		TEXT("RetargetSourceAsset"),
		EJson::Object,
		false,
		TEXT("MissingRetargetSourceAsset"),
		TEXT("InvalidObjectReference"),
		FString(),
		true,
	});
	Schema.bRejectUnknownFields = true;
	Schema.UnknownFieldCode = TEXT("UnsupportedAuthoredField");
	Schema.UnknownFieldMessageFormat = TEXT("Body.References.%s is not supported by the AnimSequence Task 2 scalar capability");
	return Schema;
}
```

Do not mark `Skeleton` nullable. Existing `ParseAssetRef(... bAllowNull=false ...)` must continue to reject authored `Skeleton = null` with `NullNotAllowed`.

- [ ] **Step 4: Wire References to schema utility**

In `ValidateBodyObjectShape`, replace:

```cpp
RejectUnknownObjectFields(BodyObject, TEXT("References"), { TEXT("Skeleton"), TEXT("RetargetSource"), TEXT("RetargetSourceAsset") }),
```

with:

```cpp
ValidateAnimSequenceObjectFieldSchema(BodyObject, TEXT("References"), MakeAnimSequenceReferencesSchema()),
```

Keep `RejectUnsupportedAuthoredFields(BodyObject)` before this validation. Do not remove `ParseAssetRef` or `ReadOptionalString`; those remain the semantic parser and preserve object reference loading, skeleton compatibility, non-empty string handling, and explicit null behavior.

- [ ] **Step 5: Run focused validation**

```powershell
git diff --check
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/AVH1/AVH1.uproject' -NoHotReload
```

Run:

```powershell
$report='C:/AVH1/Saved/AutomationReports/ReferencesSchemaAnimSequenceScalar'
if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report -Recurse -Force }
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence.ScalarRegions;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected: `AssetFactory.AssetDocument.AnimSequence.ScalarRegions` succeeds with 0 failures and 0 warnings.

- [ ] **Step 6: Run boundary checks**

```powershell
rg -n "optional schema|ObjectRegionAdapter.*FieldSchema|FieldSchema.*ObjectRegionAdapter|FAssetDocumentObjectRegionAdapter.*Schema|Schema.*FAssetDocumentObjectRegionAdapter" Source/AssetDocument/Private/Regions Source/AssetDocument/Private/Profiles
```

Expected: no new adapter schema config. The existing `GetSchemaHint` hit in `AssetDocumentObjectRegionAdapter.cpp` is acceptable.

- [ ] **Step 7: Commit Task 1**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp
git commit -m "refactor: migrate anim sequence references field schema"
```

Expected: one checkpoint commit containing only the References schema migration and tests.

## Review And Final Verification

- [ ] Dispatch a spec-compliance reviewer over `TASK_BASE..HEAD`.
- [ ] Dispatch a code-quality reviewer over `TASK_BASE..HEAD`.
- [ ] If either reviewer finds issues, fix them in the same branch and re-review the same range.
- [ ] After approval, run:

```powershell
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/AVH1/AVH1.uproject' -NoHotReload
```

- [ ] Run full AssetDocument automation when this follow-up is ready to hand back:

```powershell
$report='C:/AVH1/Saved/AutomationReports/ReferencesSchemaFullAssetDocument'
if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report -Recurse -Force }
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected: full `AssetFactory.AssetDocument` succeeds with 0 failures and 0 warnings.

## Self-Review

- Spec coverage: this follow-up advances the accepted object field schema utility pattern to one more AnimSequence object region without touching excluded complex regions.
- Placeholder scan: no placeholder tasks remain.
- Type consistency: `Skeleton` and `RetargetSourceAsset` use `EJson::Object`; only `RetargetSourceAsset` allows null because existing semantic parser permits clearing it.
