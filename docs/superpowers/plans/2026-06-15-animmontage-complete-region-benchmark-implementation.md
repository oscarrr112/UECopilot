# AnimMontage Complete Region Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring every AnimMontage region that was not classified as "not recommended" into the AssetDocument benchmark, with real apply/extract/diff/sync verification.

**Architecture:** Keep `AssetDoc` as delta-first sidecar, not a patch language and not a full `.uasset` mirror. Extend the existing `FAnimMontageAssetDocumentCapability` staged parse/apply/extract flow, keep region policies in `FAnimMontageAssetDocumentProfile`, and split complex subregions into small helpers rather than growing ad-hoc code inside `Apply()`.

**Tech Stack:** Unreal Engine 5.7 C++ editor module, `UAnimMontage`, `FAssetDocumentService`, AssetDocument sidecar sync, UE reflection helpers, Automation tests, UBT validation host at `C:/AVH1`.

---

## Scope

Implement all candidate regions from `docs/superpowers/specs/2026-06-14-animmontage-region-benchmark.md` except the items explicitly classified as direct non-goals:

- Implement or region-manage: `Body.Blend`, `Body.Sync`, `Body.RootMotion`, `Body.References`, `Body.Preview`, `Body.Metadata`, `Body.SectionMetadata`, `Body.TimeStretch`, `Body.Curves`.
- Do not author directly: `MarkerData.AuthoredSyncMarkers`, `MarkerData.UniqueMarkerNames`, `BranchingPointMarkers`, `BranchingPointStateNotifyIndices`, `BranchingPoints_DEPRECATED`, referenced sequence curves, referenced sequence root motion flags, baked `TimeStretchCurve.Markers`, baked `TimeStretchCurve.Sum_dT_i_by_C_i`.

## Files

- Modify: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentProfile.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocument.Build.cs` if curve controller headers require an extra module dependency
- Modify: `docs/superpowers/specs/2026-06-14-animmontage-region-benchmark.md`
- Modify: `docs/superpowers/specs/asset-document-deferred-fields/2026-06-12-animmontage.md`
- Modify: `docs/superpowers/verification/asset_document_delta_sidecar_smoke.py` if the existing smoke script needs new field assertions
- Do not modify: `Source/AssetDocument/Private/Projectors/AnimMontageProjectorSlice.cpp` unless production tests prove the old projector slice must compile against changed public schema

## Verification Commands

Use the validation host for UE-facing checks:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
```

Run focused automation first:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AnimMontageCompleteRegionBenchmark"
```

Run full AssetDocument automation before final review:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AssetDocumentCompleteRegionBenchmark"
```

Run MCP tests if schemas or TypeScript-visible profile output changes:

```powershell
npm test
```

---

## Task 1: Region Inventory Contract And Shared Parsers

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentProfile.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE=(git rev-parse HEAD)
git status --short
```

Expected: clean worktree and `TASK_BASE` set to the current commit.

- [ ] **Step 2: Add failing profile/schema tests for all new managed regions**

Add assertions to `FAssetDocumentAnimMontageInspectProfileTest::RunTest`:

```cpp
const TArray<FString> ExpectedNewRegionIds = {
	TEXT("Body.References"),
	TEXT("Body.Preview"),
	TEXT("Body.Sync"),
	TEXT("Body.RootMotion"),
	TEXT("Body.Metadata"),
	TEXT("Body.SectionMetadata"),
	TEXT("Body.TimeStretch"),
	TEXT("Body.Curves"),
};

for (const FString& RegionId : ExpectedNewRegionIds)
{
	FAssetDocumentRegionPolicy RegionPolicy;
	TestTrue(
		FString::Printf(TEXT("AnimMontage profile declares %s policy"), *RegionId),
		Profile.GetRegionPolicy(FName(*RegionId), RegionPolicy));
}
```

Also extend the `BodySections` assertion list to expect `References`, `Preview`, `Sync`, `RootMotion`, `Metadata`, `SectionMetadata`, `TimeStretch`, and `Curves`.

- [ ] **Step 3: Run the focused automation to verify the test fails**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage.InspectProfileIncludesStructuredBody;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/Task1ProfileFail"
```

Expected: failure showing missing region policies or missing body sections.

- [ ] **Step 4: Extend canonical body keys and shape validation**

In `FAnimMontageAssetDocumentCapability::GetCanonicalBodyKeys()`, add:

```cpp
TEXT("References"),
TEXT("Preview"),
TEXT("Sync"),
TEXT("RootMotion"),
TEXT("Metadata"),
TEXT("SectionMetadata"),
TEXT("TimeStretch"),
TEXT("Curves"),
```

In `ValidateBodyObjectShape()`, add object/array requirements:

```cpp
RequireObject(TEXT("References")),
RequireObject(TEXT("Preview")),
RequireObject(TEXT("Sync")),
RequireObject(TEXT("RootMotion")),
RequireArray(TEXT("Metadata")),
RequireObject(TEXT("SectionMetadata")),
RequireObject(TEXT("TimeStretch")),
RequireArray(TEXT("Curves")),
```

Keep existing `Skeleton` and `PreviewMesh` accepted during this transition. They stay backward compatible aliases until Task 2 moves them under `Body.References` / `Body.Preview`.

- [ ] **Step 5: Extend schema hint and template**

In `GetSchemaHint()`, add:

```cpp
Schema->SetStringField(TEXT("References"), TEXT("object"));
Schema->SetStringField(TEXT("Preview"), TEXT("object"));
Schema->SetStringField(TEXT("Sync"), TEXT("object"));
Schema->SetStringField(TEXT("RootMotion"), TEXT("object"));
Schema->SetStringField(TEXT("Metadata"), TEXT("array<EmbeddedObject|DefinitionRef>"));
Schema->SetStringField(TEXT("SectionMetadata"), TEXT("map<SectionName,array<EmbeddedObject|DefinitionRef>>"));
Schema->SetStringField(TEXT("TimeStretch"), TEXT("object"));
Schema->SetStringField(TEXT("Curves"), TEXT("array<FloatCurve>"));
```

In `CreateTemplate()`, add empty defaults:

```cpp
Body->SetObjectField(TEXT("References"), MakeShared<FJsonObject>());
Body->SetObjectField(TEXT("Preview"), MakeShared<FJsonObject>());
Body->SetObjectField(TEXT("Sync"), MakeShared<FJsonObject>());
Body->SetObjectField(TEXT("RootMotion"), MakeShared<FJsonObject>());
Body->SetArrayField(TEXT("Metadata"), MakeEmptyArray());
Body->SetObjectField(TEXT("SectionMetadata"), MakeShared<FJsonObject>());
Body->SetObjectField(TEXT("TimeStretch"), MakeShared<FJsonObject>());
Body->SetArrayField(TEXT("Curves"), MakeEmptyArray());
```

- [ ] **Step 6: Add region policies**

In `FAnimMontageAssetDocumentProfile::GetRegionPolicies()`, update reserve count and add policies:

```cpp
if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.References"), EAssetDocumentRegionKind::Object, {TEXT("Skeleton")}, Policy))
{
	Policies.Add(Policy);
}
if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.Preview"), EAssetDocumentRegionKind::Object, {TEXT("PreviewMesh"), TEXT("PreviewBasePose")}, Policy))
{
	Policies.Add(Policy);
}
if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.Sync"), EAssetDocumentRegionKind::Object, {TEXT("SyncGroup"), TEXT("SyncSlotIndex")}, Policy))
{
	Policies.Add(Policy);
}
if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.RootMotion"), EAssetDocumentRegionKind::Object, {TEXT("bEnableRootMotionTranslation"), TEXT("bEnableRootMotionRotation"), TEXT("RootMotionRootLock")}, Policy))
{
	Policies.Add(Policy);
}
if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.Metadata"), EAssetDocumentRegionKind::Array, {TEXT("MetaData")}, Policy))
{
	Policies.Add(Policy);
}
if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.SectionMetadata"), EAssetDocumentRegionKind::Object, {TEXT("CompositeSections")}, Policy))
{
	Policies.Add(Policy);
}
if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.TimeStretch"), EAssetDocumentRegionKind::Object, {TEXT("TimeStretchCurve"), TEXT("TimeStretchCurveName")}, Policy))
{
	Policies.Add(Policy);
}
if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.Curves"), EAssetDocumentRegionKind::Array, {TEXT("RawCurveData")}, Policy))
{
	Policies.Add(Policy);
}
```

- [ ] **Step 7: Run the profile tests again**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage.InspectProfileIncludesStructuredBody;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/Task1ProfilePass"
```

Expected: pass for profile/schema tests.

- [ ] **Step 8: Commit Task 1**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentProfile.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp
git commit -m "feat(assetdoc): declare remaining animmontage regions"
```

---

## Task 2: References, Preview, Sync, And RootMotion Scalar Regions

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`
- Modify: `docs/superpowers/specs/asset-document-deferred-fields/2026-06-12-animmontage.md`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE=(git rev-parse HEAD)
git status --short
```

- [ ] **Step 2: Add failing apply/extract tests**

Add one automation test named:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimMontageApplyExtractScalarRegionsTest,
	"AssetFactory.AssetDocument.AnimMontage.ApplyExtract.ScalarRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
```

The test should:

1. Create a structured montage with `MakeStructuredMontageDocument`.
2. Set:

```cpp
TSharedPtr<FJsonObject> Body = Document->GetObjectField(TEXT("Body"));

TSharedRef<FJsonObject> References = MakeShared<FJsonObject>();
References->SetObjectField(TEXT("Skeleton"), MakeAssetRef(GeneratedSkeletonPath));
Body->SetObjectField(TEXT("References"), References);

TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
Preview->SetObjectField(TEXT("PreviewMesh"), MakeAssetRef(GeneratedPreviewMeshPath));
Preview->SetObjectField(TEXT("PreviewBasePose"), MakeAssetRef(AnimSequence->GetPathName()));
Body->SetObjectField(TEXT("Preview"), Preview);

TSharedRef<FJsonObject> Sync = MakeShared<FJsonObject>();
Sync->SetStringField(TEXT("SyncGroup"), TEXT("Locomotion"));
Sync->SetNumberField(TEXT("SyncSlotIndex"), 0);
Body->SetObjectField(TEXT("Sync"), Sync);

TSharedRef<FJsonObject> RootMotion = MakeShared<FJsonObject>();
RootMotion->SetBoolField(TEXT("bEnableRootMotionTranslation"), true);
RootMotion->SetBoolField(TEXT("bEnableRootMotionRotation"), true);
RootMotion->SetStringField(TEXT("RootMotionRootLock"), TEXT("Zero"));
Body->SetObjectField(TEXT("RootMotion"), RootMotion);
```

3. Apply and load the montage.
4. Assert `Montage->GetSkeleton()`, `Montage->GetPreviewMesh()`, `Montage->PreviewBasePose`, `Montage->SyncGroup`, `Montage->SyncSlotIndex`, `Montage->bEnableRootMotionTranslation`, `Montage->bEnableRootMotionRotation`, and `Montage->RootMotionRootLock`.
5. Extract and assert the same fields appear under `Body.References`, `Body.Preview`, `Body.Sync`, and `Body.RootMotion`.

- [ ] **Step 3: Verify the new test fails**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage.ApplyExtract.ScalarRegions;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/Task2Fail"
```

Expected: failure because new fields are not parsed/applied/extracted.

- [ ] **Step 4: Add parsed fields**

Extend `FParsedAnimMontageBody`:

```cpp
bool bHasPreviewBasePose = false;
UAnimSequence* PreviewBasePose = nullptr;
bool bHasSyncGroup = false;
FName SyncGroup = NAME_None;
bool bHasSyncSlotIndex = false;
int32 SyncSlotIndex = 0;
bool bHasRootMotionTranslation = false;
bool bRootMotionTranslation = false;
bool bHasRootMotionRotation = false;
bool bRootMotionRotation = false;
bool bHasRootMotionRootLock = false;
ERootMotionRootLock::Type RootMotionRootLock = ERootMotionRootLock::RefPose;
```

- [ ] **Step 5: Parse `References` and keep backward compatibility**

Update `ParseAnimMontageBody()` so:

- `Body.References.Skeleton` overrides legacy `Body.Skeleton` when both exist.
- legacy `Body.Skeleton` still works.

Use the existing `ParseObjectReference()` helper with `USkeleton::StaticClass()`.

- [ ] **Step 6: Parse and apply `Preview`**

Parse:

- `Body.Preview.PreviewMesh` as `USkeletalMesh`
- `Body.Preview.PreviewBasePose` as `UAnimSequence`

Apply:

```cpp
if (ParsedBody.bHasPreviewBasePose)
{
	Montage->PreviewBasePose = ParsedBody.PreviewBasePose;
}
```

Keep legacy `Body.PreviewMesh` accepted and mapped to `Montage->SetPreviewMesh`.

- [ ] **Step 7: Parse and apply `Sync`**

Parse:

- `SyncGroup`: string, empty string maps to `NAME_None`
- `SyncSlotIndex`: non-negative integer

Apply:

```cpp
if (ParsedBody.bHasSyncGroup)
{
	Montage->SyncGroup = ParsedBody.SyncGroup;
}
if (ParsedBody.bHasSyncSlotIndex)
{
	Montage->SyncSlotIndex = ParsedBody.SyncSlotIndex;
}
```

After slot track application and sync application, call:

```cpp
Montage->CollectMarkers();
```

only when `SyncGroup`, `SyncSlotIndex`, or `SlotAnimTracks` changed.

- [ ] **Step 8: Parse and apply `RootMotion`**

Add enum parser for:

```text
RefPose
AnimFirstFrame
Zero
```

Reject unknown values with path `/Body/RootMotion/RootMotionRootLock` and code `InvalidRootMotionRootLock`.

Apply:

```cpp
if (ParsedBody.bHasRootMotionTranslation)
{
	Montage->bEnableRootMotionTranslation = ParsedBody.bRootMotionTranslation;
}
if (ParsedBody.bHasRootMotionRotation)
{
	Montage->bEnableRootMotionRotation = ParsedBody.bRootMotionRotation;
}
if (ParsedBody.bHasRootMotionRootLock)
{
	Montage->RootMotionRootLock = ParsedBody.RootMotionRootLock;
}
```

- [ ] **Step 9: Extract scalar regions**

In `Extract()`, write:

```cpp
References->SetObjectField(TEXT("Skeleton"), SkeletonRef);
Preview->SetObjectField(TEXT("PreviewMesh"), PreviewMeshRef);
Preview->SetObjectField(TEXT("PreviewBasePose"), PreviewBasePoseRef);
Sync->SetStringField(TEXT("SyncGroup"), Montage->SyncGroup.ToString());
Sync->SetNumberField(TEXT("SyncSlotIndex"), Montage->SyncSlotIndex);
RootMotion->SetBoolField(TEXT("bEnableRootMotionTranslation"), Montage->bEnableRootMotionTranslation);
RootMotion->SetBoolField(TEXT("bEnableRootMotionRotation"), Montage->bEnableRootMotionRotation);
RootMotion->SetStringField(TEXT("RootMotionRootLock"), RootMotionRootLockToString(Montage->RootMotionRootLock));
```

Keep emitting legacy `Skeleton` and `PreviewMesh` during this task to preserve existing tests.

- [ ] **Step 10: Add validation tests**

Add table-style invalid cases:

- `/Body/Sync/SyncSlotIndex`: `-1` -> `InvalidSyncSlotIndex`
- `/Body/Sync/SyncSlotIndex`: `1.5` -> `InvalidSyncSlotIndex`
- `/Body/RootMotion/RootMotionRootLock`: `"Bad"` -> `InvalidRootMotionRootLock`
- `/Body/Preview/PreviewBasePose`: asset ref to non-`UAnimSequence` -> `InvalidObjectReference`

- [ ] **Step 11: Run focused automation**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage.ApplyExtract.ScalarRegions;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/Task2Pass"
```

Expected: pass.

- [ ] **Step 12: Update deferred docs**

In `docs/superpowers/specs/asset-document-deferred-fields/2026-06-12-animmontage.md`, remove `Body.Sync` and `Body.RootMotion` from deferred wording once automation passes. Keep `MarkerData` and referenced sequence root motion documented as out of scope.

- [ ] **Step 13: Commit Task 2**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp docs/superpowers/specs/asset-document-deferred-fields/2026-06-12-animmontage.md
git commit -m "feat(assetdoc): add animmontage scalar regions"
```

---

## Task 3: Expanded Blend Region

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE=(git rev-parse HEAD)
git status --short
```

- [ ] **Step 2: Add failing expanded blend test**

Add test `AssetFactory.AssetDocument.AnimMontage.ApplyExtract.ExpandedBlend` that sets:

```cpp
TSharedPtr<FJsonObject> Blend = Document->GetObjectField(TEXT("Body"))->GetObjectField(TEXT("Blend"));
Blend->SetNumberField(TEXT("BlendInTime"), 0.2);
Blend->SetNumberField(TEXT("BlendOutTime"), 0.3);
Blend->SetStringField(TEXT("BlendModeIn"), TEXT("Inertialization"));
Blend->SetStringField(TEXT("BlendModeOut"), TEXT("Standard"));
Blend->SetNumberField(TEXT("BlendOutTriggerTime"), 0.15);
Blend->SetBoolField(TEXT("bEnableAutoBlendOut"), false);
```

Assert after apply:

```cpp
TestEqual(TEXT("BlendModeIn"), Montage->BlendModeIn, EMontageBlendMode::Inertialization);
TestEqual(TEXT("BlendModeOut"), Montage->BlendModeOut, EMontageBlendMode::Standard);
TestTrue(TEXT("BlendOutTriggerTime"), FMath::IsNearlyEqual(Montage->BlendOutTriggerTime, 0.15f));
TestFalse(TEXT("Auto blend out disabled"), Montage->bEnableAutoBlendOut);
```

Extract and assert the same fields under `Body.Blend`.

- [ ] **Step 3: Run the new test and confirm failure**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage.ApplyExtract.ExpandedBlend;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/Task3Fail"
```

- [ ] **Step 4: Extend parsed blend fields**

Add to `FParsedAnimMontageBody`:

```cpp
bool bHasBlendModeIn = false;
EMontageBlendMode BlendModeIn = EMontageBlendMode::Standard;
bool bHasBlendModeOut = false;
EMontageBlendMode BlendModeOut = EMontageBlendMode::Standard;
bool bHasBlendOutTriggerTime = false;
float BlendOutTriggerTime = -1.0f;
bool bHasEnableAutoBlendOut = false;
bool bEnableAutoBlendOut = true;
```

- [ ] **Step 5: Add blend mode parser**

Implement helper:

```cpp
bool TryParseMontageBlendMode(const FString& Text, EMontageBlendMode& OutMode)
{
	if (Text == TEXT("Standard"))
	{
		OutMode = EMontageBlendMode::Standard;
		return true;
	}
	if (Text == TEXT("Inertialization"))
	{
		OutMode = EMontageBlendMode::Inertialization;
		return true;
	}
	return false;
}
```

Reject unknown values with `InvalidBlendMode`.

- [ ] **Step 6: Update `ParseBlend()`**

Read:

- `BlendModeIn`: optional string
- `BlendModeOut`: optional string
- `BlendOutTriggerTime`: optional number, allow `-1.0` or any non-negative value
- `bEnableAutoBlendOut`: optional bool

Use `/Body/Blend/<Field>` diagnostic paths.

- [ ] **Step 7: Apply and extract expanded blend**

Apply:

```cpp
if (ParsedBody.bHasBlendModeIn) { Montage->BlendModeIn = ParsedBody.BlendModeIn; }
if (ParsedBody.bHasBlendModeOut) { Montage->BlendModeOut = ParsedBody.BlendModeOut; }
if (ParsedBody.bHasBlendOutTriggerTime) { Montage->BlendOutTriggerTime = ParsedBody.BlendOutTriggerTime; }
if (ParsedBody.bHasEnableAutoBlendOut) { Montage->bEnableAutoBlendOut = ParsedBody.bEnableAutoBlendOut; }
```

Extract string values for blend modes and numeric/bool fields.

- [ ] **Step 8: Add invalid blend tests**

Add cases:

- `BlendModeIn = "Bad"` -> `InvalidBlendMode`
- `BlendOutTriggerTime = -2` -> `InvalidBlendOutTriggerTime`
- `bEnableAutoBlendOut = "yes"` -> `InvalidBooleanField`

- [ ] **Step 9: Run focused automation**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage.ApplyExtract.ExpandedBlend;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/Task3Pass"
```

- [ ] **Step 10: Commit Task 3**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp
git commit -m "feat(assetdoc): expand animmontage blend region"
```

---

## Task 4: Asset Metadata And Section Metadata

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`
- Modify: `docs/superpowers/specs/asset-document-deferred-fields/2026-06-12-animmontage.md`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE=(git rev-parse HEAD)
git status --short
```

- [ ] **Step 2: Add failing metadata apply/extract test**

Add test `AssetFactory.AssetDocument.AnimMontage.ApplyExtract.MetadataRegions`.

Use `UAnimMetaData` as the initial metadata class:

```cpp
TSharedRef<FJsonObject> MetadataObject = MakeShared<FJsonObject>();
MetadataObject->SetStringField(TEXT("Kind"), TEXT("EmbeddedObject"));
MetadataObject->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimMetaData"));
MetadataObject->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());

TArray<TSharedPtr<FJsonValue>> MetadataValues;
MetadataValues.Add(MakeShared<FJsonValueObject>(MetadataObject));
Body->SetArrayField(TEXT("Metadata"), MetadataValues);
```

For section metadata:

```cpp
TSharedRef<FJsonObject> SectionMetadata = MakeShared<FJsonObject>();
SectionMetadata->SetArrayField(TEXT("Default"), MetadataValues);
Body->SetObjectField(TEXT("SectionMetadata"), SectionMetadata);
```

Assert:

- `Montage->MetaData.Num() == 1`
- `Montage->CompositeSections[0].GetMetaData().Num() == 1`
- extract outputs `Body.Metadata[0].Object.Class` or equivalent embedded object fragment shape used by `AssetDocumentFragmentCompiler::Extract`.

- [ ] **Step 3: Run metadata test and confirm failure**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage.ApplyExtract.MetadataRegions;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/Task4Fail"
```

- [ ] **Step 4: Add parsed metadata fields**

Extend `FParsedAnimMontageBody`:

```cpp
bool bHasMetadata = false;
TArray<TObjectPtr<UAnimMetaData>> Metadata;
bool bHasSectionMetadata = false;
TMap<FName, TArray<TObjectPtr<UAnimMetaData>>> SectionMetadataByName;
```

- [ ] **Step 5: Implement metadata fragment compilation**

Add helper:

```cpp
FAssetDocumentCapabilityResult CompileMetadataArray(
	const FAssetDocumentFragmentCompiler& Compiler,
	const FAssetDocumentCapabilityContext& Context,
	UAnimMontage* Montage,
	const TArray<TSharedPtr<FJsonValue>>& Values,
	const FString& BasePath,
	TArray<TObjectPtr<UAnimMetaData>>& OutMetadata);
```

Each array element must be an object fragment. Compile with `ExpectedBaseClass = UAnimMetaData::StaticClass()` and `Outer = Montage`.

- [ ] **Step 6: Parse `Body.Metadata` and `Body.SectionMetadata`**

Rules:

- `Body.Metadata` present means replace AssetDocument-managed asset metadata.
- `Body.SectionMetadata` is an object keyed by `SectionName`.
- section key must match an existing section name after `CompositeSections` parse.
- empty array clears metadata for that section.

Reject missing section names with code `UnknownSectionMetadataTarget`.

- [ ] **Step 7: Apply metadata**

Apply asset metadata:

```cpp
if (ParsedBody.bHasMetadata)
{
	Montage->EmptyMetaData();
	for (UAnimMetaData* Entry : ParsedBody.Metadata)
	{
		Montage->AddMetaData(Entry);
	}
}
```

Apply section metadata by finding `FCompositeSection` by `SectionName` and replacing its `MetaData` array.

- [ ] **Step 8: Extract metadata**

For each `UAnimMetaData*`, call `Compiler.Extract()` with `Kind = "EmbeddedObject"` and emit under:

```json
"Metadata": [
  { "Kind": "EmbeddedObject", "Class": "/Script/Engine.AnimMetaData", "Properties": {} }
]
```

For section metadata, emit:

```json
"SectionMetadata": {
  "Default": [
    { "Kind": "EmbeddedObject", "Class": "/Script/Engine.AnimMetaData", "Properties": {} }
  ]
}
```

- [ ] **Step 9: Add managed-region preservation tests**

Add a test where:

- asset has unmanaged manually inserted metadata before apply;
- sidecar applies `Body.Metadata`;
- v1 behavior for `Body.Metadata` is full managed replacement, so unmanaged metadata inside the same `MetaData` array is not preserved.

Document this in the test name and in deferred docs. This differs from notifies, where managed markers preserve unmanaged notify events.

- [ ] **Step 10: Run focused automation**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage.ApplyExtract.MetadataRegions;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/Task4Pass"
```

- [ ] **Step 11: Update deferred docs**

Remove `MetadataObjectAuthoring` as a deferred item once automation passes. Add a note that v1 metadata replacement is region-level replacement, not element-level merge.

- [ ] **Step 12: Commit Task 4**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp docs/superpowers/specs/asset-document-deferred-fields/2026-06-12-animmontage.md
git commit -m "feat(assetdoc): add animmontage metadata regions"
```

---

## Task 5: Montage-Owned Curves And TimeStretch

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocument.Build.cs` if `IAnimationDataController` needs a module dependency
- Modify: `docs/superpowers/specs/asset-document-deferred-fields/2026-06-12-animmontage.md`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE=(git rev-parse HEAD)
git status --short
```

- [ ] **Step 2: Add compile includes for curve APIs**

In `AnimMontageAssetDocumentCapability.cpp`, add:

```cpp
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimCurveTypes.h"
#include "Curves/RichCurve.h"
```

Run UBT after includes. If compile reports a missing module dependency, add the minimal dependency to `Source/AssetDocument/Private/AssetDocument.Build.cs`.

- [ ] **Step 3: Add failing curve/time stretch test**

Add test `AssetFactory.AssetDocument.AnimMontage.ApplyExtract.CurvesAndTimeStretch`.

Sidecar shape:

```json
"Curves": [
  {
    "Name": "MontageTimeStretchCurve",
    "Flags": ["Default"],
    "Keys": [
      { "Time": 0.0, "Value": 0.0 },
      { "Time": 0.5, "Value": 1.0 },
      { "Time": 1.0, "Value": 0.0 }
    ]
  }
],
"TimeStretch": {
  "TimeStretchCurveName": "MontageTimeStretchCurve",
  "SamplingRate": 30.0,
  "CurveValueMinPrecision": 0.02
}
```

The test must assert:

- `Montage->GetDataModel()->FindFloatCurve(FAnimationCurveIdentifier(FName("MontageTimeStretchCurve"), ERawCurveTrackTypes::RCT_Float)) != nullptr`
- extract returns the curve name and three keys;
- extract returns `Body.TimeStretch.TimeStretchCurveName`;
- referenced `AnimSequence` used by the slot has no newly added `MontageTimeStretchCurve`.

- [ ] **Step 4: Run test and confirm failure**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage.ApplyExtract.CurvesAndTimeStretch;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/Task5Fail"
```

- [ ] **Step 5: Add parsed curve/time stretch fields**

Add structs in the anonymous namespace:

```cpp
struct FParsedFloatCurveKey
{
	float Time = 0.0f;
	float Value = 0.0f;
};

struct FParsedFloatCurve
{
	FName Name = NAME_None;
	int32 Flags = AACF_DefaultCurve;
	TArray<FParsedFloatCurveKey> Keys;
};
```

Extend `FParsedAnimMontageBody`:

```cpp
bool bHasCurves = false;
TArray<FParsedFloatCurve> Curves;
bool bHasTimeStretch = false;
bool bHasTimeStretchCurveName = false;
FName TimeStretchCurveName = NAME_None;
bool bHasTimeStretchSamplingRate = false;
float TimeStretchSamplingRate = 60.0f;
bool bHasTimeStretchCurveValueMinPrecision = false;
float TimeStretchCurveValueMinPrecision = 0.01f;
```

- [ ] **Step 6: Parse `Body.Curves`**

Rules:

- `Curves` is an array.
- `Name` is required and non-empty.
- `Keys` is required and non-empty.
- key `Time` must be non-negative.
- key `Value` must be numeric.
- keys are sorted by time during apply.

Reject invalid fields with codes:

- `InvalidCurveName`
- `InvalidCurveKeys`
- `InvalidCurveKeyTime`
- `InvalidCurveKeyValue`

- [ ] **Step 7: Apply montage-owned curves**

Use `Montage->GetDataModelInterface()->GetController()` or `Montage->GetController()` if available from `UAnimSequenceBase`.

Apply mode:

```cpp
IAnimationDataController& Controller = Montage->GetController();
IAnimationDataController::FScopedBracket Bracket(Controller, LOCTEXT("AssetDocumentApplyMontageCurves", "Apply AssetDocument Montage Curves"), false);

Controller.RemoveAllCurvesOfType(ERawCurveTrackTypes::RCT_Float, false);
for (const FParsedFloatCurve& Curve : ParsedBody.Curves)
{
	const FAnimationCurveIdentifier CurveId(Curve.Name, ERawCurveTrackTypes::RCT_Float);
	Controller.AddCurve(CurveId, Curve.Flags, false);
	TArray<FRichCurveKey> RichKeys;
	for (const FParsedFloatCurveKey& Key : Curve.Keys)
	{
		RichKeys.Add(FRichCurveKey(Key.Time, Key.Value));
	}
	Controller.SetCurveKeys(CurveId, RichKeys, false);
}
```

This is region-level replacement for montage-owned float curves.

- [ ] **Step 8: Apply `Body.TimeStretch`**

Set:

```cpp
Montage->TimeStretchCurveName = ParsedBody.TimeStretchCurveName;
```

For `SamplingRate` and `CurveValueMinPrecision`, use reflected property writes on `Montage->TimeStretchCurve` because those fields are private in `FTimeStretchCurve`.

Use `FProperty` lookup on `FTimeStretchCurve::StaticStruct()` for:

- `SamplingRate`
- `CurveValueMinPrecision`

After curves and settings are applied, call:

```cpp
Montage->BakeTimeStretchCurve();
```

- [ ] **Step 9: Extract curves and time stretch**

Extract `Montage->GetDataModelInterface()->GetFloatCurves()` when available. For each `FFloatCurve`, emit:

```json
{
  "Name": "CurveName",
  "Flags": ["Default"],
  "Keys": [
    { "Time": 0.0, "Value": 0.0 }
  ]
}
```

Extract `TimeStretchCurveName`, `SamplingRate`, and `CurveValueMinPrecision`. Do not extract `Markers` or `Sum_dT_i_by_C_i`.

- [ ] **Step 10: Add negative tests**

Add cases:

- `Curves[0].Name = ""` -> `InvalidCurveName`
- `Curves[0].Keys = []` -> `InvalidCurveKeys`
- `Curves[0].Keys[0].Time = -1` -> `InvalidCurveKeyTime`
- `TimeStretch.SamplingRate = 0` -> `InvalidTimeStretchSamplingRate`
- `TimeStretch.CurveValueMinPrecision = -0.1` -> `InvalidTimeStretchCurveValueMinPrecision`

- [ ] **Step 11: Run focused automation and UBT**

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage.ApplyExtract.CurvesAndTimeStretch;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/Task5Pass"
```

- [ ] **Step 12: Update deferred docs**

Remove `CurvesAndTimeStretch` as deferred once tests pass. Keep notes that referenced sequence curves and baked time stretch data are out of scope.

- [ ] **Step 13: Commit Task 5**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp Source/AssetDocument/Private/AssetDocument.Build.cs docs/superpowers/specs/asset-document-deferred-fields/2026-06-12-animmontage.md
git commit -m "feat(assetdoc): add animmontage curves and time stretch regions"
```

---

## Task 6: Sidecar Sync And Benchmark Smoke Coverage

**Files:**
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`
- Modify: `docs/superpowers/specs/2026-06-14-animmontage-region-benchmark.md`
- Modify: `docs/superpowers/verification/asset_document_delta_sidecar_smoke.py`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE=(git rev-parse HEAD)
git status --short
```

- [ ] **Step 2: Extend sync state tests**

Update existing tests that call `ExpectExtractSyncRegion()` and `ExpectApplyFileSyncRegion()` to include:

```cpp
Body.References
Body.Preview
Body.Sync
Body.RootMotion
Body.Metadata
Body.SectionMetadata
Body.TimeStretch
Body.Curves
```

Use `Profile.GetRegionPolicy()` for each region and assert:

- region state exists after extract;
- `SidecarHash` is non-empty;
- `AssetHash` is non-empty;
- `LastSyncedAtUtc` is non-empty.

- [ ] **Step 3: Add accept-asset regeneration test for a non-blend region**

Add a test that:

1. Extracts a sidecar.
2. Mutates `Montage->SyncGroup = FName(TEXT("EditorChanged"))`.
3. Calls accept-asset flow.
4. Asserts regenerated sidecar has `Body.Sync.SyncGroup == "EditorChanged"`.
5. Asserts unrelated `Body.Notifies` and `Body.SlotAnimTracks` are preserved.

- [ ] **Step 4: Add apply-file sidecar-to-asset test for all new regions**

Extend `FAssetDocumentAnimMontageApplyFileSyncStateTest` or add a new test:

```cpp
FAssetDocumentAnimMontageApplyFileCompleteRegionsSyncStateTest
"AssetFactory.AssetDocument.AnimMontage.ApplyFile.CompleteRegionsSyncState"
```

Sidecar should set one non-default value in each new region. Apply-file with sync rewrite enabled. Assert:

- asset fields changed;
- sidecar `_meta.sync.regions` includes every new region;
- re-extract then diff returns no changed entries for those regions.

- [ ] **Step 5: Update Python smoke script**

In `docs/superpowers/verification/asset_document_delta_sidecar_smoke.py`, extend the smoke sidecar body with representative values:

```python
"Sync": {"SyncGroup": "AssetDocSmoke", "SyncSlotIndex": 0},
"RootMotion": {
    "bEnableRootMotionTranslation": True,
    "bEnableRootMotionRotation": True,
    "RootMotionRootLock": "Zero",
},
"TimeStretch": {
    "TimeStretchCurveName": "MontageTimeStretchCurve",
    "SamplingRate": 30.0,
    "CurveValueMinPrecision": 0.02,
},
"Curves": [
    {
        "Name": "MontageTimeStretchCurve",
        "Flags": ["Default"],
        "Keys": [
            {"Time": 0.0, "Value": 0.0},
            {"Time": 0.5, "Value": 1.0},
            {"Time": 1.0, "Value": 0.0},
        ],
    }
],
```

Add smoke assertions for extracted `Body.Sync`, `Body.RootMotion`, `Body.TimeStretch`, and `Body.Curves`.

- [ ] **Step 6: Update benchmark docs**

In `docs/superpowers/specs/2026-06-14-animmontage-region-benchmark.md`, change region status table:

- `Body.Blend`: complete expanded region
- `Body.Sync`: complete
- `Body.RootMotion`: complete as montage legacy settings
- `Body.References`: complete
- `Body.Preview`: complete
- `Body.Metadata`: complete
- `Body.SectionMetadata`: complete
- `Body.TimeStretch`: complete with baked data excluded
- `Body.Curves`: complete for montage-owned float curves

Keep the out-of-scope table for derived/cache/referenced sequence data.

- [ ] **Step 7: Run full verification**

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/Task6AnimMontage"
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/Task6AssetDocument"
npm test
```

Expected:

- UBT succeeds.
- `AssetFactory.AssetDocument.AnimMontage` succeeds.
- `AssetFactory.AssetDocument` succeeds.
- MCP `npm test` succeeds.

- [ ] **Step 8: Commit Task 6**

```powershell
git add Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp docs/superpowers/specs/2026-06-14-animmontage-region-benchmark.md docs/superpowers/verification/asset_document_delta_sidecar_smoke.py
git commit -m "test(assetdoc): verify complete animmontage region benchmark"
```

---

## Task 7: Review Fixes And Final Evidence

**Files:**
- Modify only files called out by review findings.

- [ ] **Step 1: Run code-quality review on `SPEC_BASE..HEAD`**

Use a read-only review subagent. Provide:

- diff range from the branch base before this plan execution to `HEAD`;
- this implementation plan;
- benchmark spec;
- explicit review focus: region scope drift, accidental authoring of derived/cache fields, missing sync hashes, missed validation, partial mutation risk.

- [ ] **Step 2: Fix review findings**

For each accepted finding, add a focused test first, then patch the implementation.

- [ ] **Step 3: Re-run verification**

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/FinalAssetDocument"
npm test
```

- [ ] **Step 4: Commit review fixes**

```powershell
git add Source/AssetDocument docs/superpowers
git commit -m "fix(assetdoc): address animmontage benchmark review"
```

- [ ] **Step 5: Record final evidence**

Append verification results to a report under:

```text
docs/reports/asset-document-animmontage-complete-region-benchmark.md
```

Include:

- commit range;
- UBT command and result;
- automation command and result;
- MCP test result;
- smoke asset path;
- list of complete regions and excluded derived/cache fields.

---

## Self-Review Checklist

- Spec coverage: every region from the benchmark that was not explicitly discouraged has a task.
- Non-goal coverage: derived/cache/deprecated/referenced sequence-owned data is excluded explicitly.
- TDD coverage: each implementation task starts with a failing automation test.
- Sync coverage: final task checks `_meta.sync.regions` for new regions.
- Verification coverage: UBT, focused automation, full AssetDocument automation, and MCP tests are listed.
- Commit coverage: every task ends with a checkpoint commit.
