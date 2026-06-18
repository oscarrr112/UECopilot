# AnimSequence AssetDocument Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a complete post-import `/Script/Engine.AnimSequence` AssetDocument profile that manages AnimSequence-owned authored regions without extending import, reimport, legacy generators, or specialized MCP tools.

**Architecture:** Add an exact AnimSequence profile and focused capability/adapters under `Source/AssetDocument/Private/Profiles`. Reuse the existing AssetDocument service, profile registry, fragment compiler, region policies, generic HTTP routes, and generic MCP tools. Keep raw animation data, import metadata, compressed output, and referenced asset internals excluded or diagnostic-only.

**Tech Stack:** Unreal Engine 5.7 editor C++, `UAnimSequence`, `UAnimSequenceBase`, `UAnimationAsset`, `IAnimationDataController`, `IAnimationDataModel`, AssetDocument profile/capability APIs, UE automation tests, MCP TypeScript tests, validation host `C:/AVH1`.

---

## Implementation Context

**Implementation worktree:** `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-animsequence-impl`

**Implementation branch:** `feature/asset-document-animsequence-impl`

**SPEC_BASE:** `9b9d6ea7cd24f4733a4948100ef8a63fad3c3e55`

**Spec:** `docs/superpowers/specs/2026-06-18-animsequence-asset-document-design.md`

**Deferred fields:** `docs/superpowers/specs/asset-document-deferred-fields/2026-06-18-animsequence.md`

**Do not modify:**

- `Source/AssetFactory/Private/Generators/AnimSequenceGenerator.cpp`
- `Source/AssetFactory/Public/Generators/AnimSequenceGenerator.h`
- `MCP/schemas/AnimSequence.md`
- legacy `generate_assets` routing
- importer/reimport pipeline code

**Allowed production files:**

- `Source/AssetDocument/Private/AssetDocumentModule.cpp`
- `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentProfile.h`
- `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentProfile.cpp`
- `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.h`
- `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
- `Source/AssetDocument/Private/Profiles/AnimSequenceNotifyPlacementAdapter.h`
- `Source/AssetDocument/Private/Profiles/AnimSequenceNotifyPlacementAdapter.cpp`
- `MCP/schemas/AssetDocument.md`

**Allowed test/docs files:**

- `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`
- `docs/superpowers/verification/asset_document_animsequence_http_smoke.py`
- `docs/reports/asset-document-animsequence-complete-region-benchmark.md`

**Review discipline:**

- Each task starts by recording `TASK_BASE=HEAD`.
- Each task ends with a checkpoint commit.
- Task spec review range is exactly `TASK_BASE..HEAD`.
- Final branch review range is exactly `SPEC_BASE..HEAD`.
- Implementers may read nearby existing AssetDocument files, AnimMontage profile/capability/tests, and UE animation headers; do not broad-scan unrelated generator families.

## File Structure

Create focused AnimSequence profile files:

- `AnimSequenceAssetDocumentProfile.h/.cpp`: exact class, template, body keys, document shape, region policies.
- `AnimSequenceAssetDocumentCapability.h/.cpp`: body validation, preflight, apply, extract, diff, helpers for references, preview, playback, additive, root motion, compression, curves, markers, metadata, and user data.
- `AnimSequenceNotifyPlacementAdapter.h/.cpp`: reusable point notify and notify-state parsing/extraction for `UAnimSequence`.
- `AssetDocumentAnimSequenceTests.cpp`: focused automation tests for profile, validation, apply/extract/diff, sidecar sync, and smoke setup helpers.

Modify integration files:

- `AssetDocumentModule.cpp`: register `FAnimSequenceAssetDocumentProfile`.
- `MCP/schemas/AssetDocument.md`: document AnimSequence profile and excluded fields for generic MCP consumers.

Verification artifacts:

- `asset_document_animsequence_http_smoke.py`: external HTTP smoke that drives a running Editor, applies a sidecar to an existing sequence fixture, validates extract/diff, and leaves an inspectable smoke asset/sidecar.
- `asset-document-animsequence-complete-region-benchmark.md`: final report with evidence and residual risk.

---

### Task 1: Profile Skeleton, Body Keys, Region Policies, And Schema

**Files:**
- Create: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentProfile.h`
- Create: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentProfile.cpp`
- Create: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.h`
- Create: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentModule.cpp`
- Modify: `MCP/schemas/AssetDocument.md`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`

- [ ] **Step 1: Record task base**

Run:

```powershell
git rev-parse HEAD
```

Save the output as `TASK_BASE` in the subagent report. The expected starting point for this task is `9b9d6ea7cd24f4733a4948100ef8a63fad3c3e55` unless a prior checkpoint was intentionally added.

- [ ] **Step 2: Add failing profile/schema tests**

Create `AssetDocumentAnimSequenceTests.cpp` with:

```cpp
#include "AssetDocumentService.h"
#include "Profiles/AnimSequenceAssetDocumentProfile.h"

#include "Animation/AnimSequence.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
TSharedPtr<FJsonObject> FindObjectByStringField(const TArray<TSharedPtr<FJsonValue>>& Values, const FString& FieldName, const FString& ExpectedValue)
{
	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		if (!Value.IsValid() || Value->Type != EJson::Object)
		{
			continue;
		}
		TSharedPtr<FJsonObject> Object = Value->AsObject();
		if (!Object.IsValid())
		{
			continue;
		}
		FString ActualValue;
		if (Object->TryGetStringField(FieldName, ActualValue) && ActualValue == ExpectedValue)
		{
			return Object;
		}
	}
	return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimSequenceProfileShapeTest,
	"AssetFactory.AssetDocument.AnimSequence.ProfileShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimSequenceProfileShapeTest::RunTest(const FString&)
{
	const FAnimSequenceAssetDocumentProfile Profile;
	TestEqual(TEXT("Exact class is UAnimSequence"), Profile.GetExactClass(), UAnimSequence::StaticClass());

	const TArray<FName> BodyKeys = Profile.GetBodyKeys();
	const TArray<FName> ExpectedKeys = {
		TEXT("References"),
		TEXT("Preview"),
		TEXT("Playback"),
		TEXT("Additive"),
		TEXT("RootMotion"),
		TEXT("Compression"),
		TEXT("Curves"),
		TEXT("Notifies"),
		TEXT("NotifyStates"),
		TEXT("NotifyTracks"),
		TEXT("SyncMarkers"),
		TEXT("Metadata"),
		TEXT("AssetUserData"),
	};
	for (const FName& ExpectedKey : ExpectedKeys)
	{
		TestTrue(FString::Printf(TEXT("Body key %s exists"), *ExpectedKey.ToString()), BodyKeys.Contains(ExpectedKey));
		TestNotNull(FString::Printf(TEXT("Body key %s resolves adapter"), *ExpectedKey.ToString()), Profile.ResolveBodyAdapter(ExpectedKey));
	}

	const TArray<FAssetDocumentRegionPolicy> Policies = Profile.GetRegionPolicies();
	for (const FName& ExpectedKey : ExpectedKeys)
	{
		const FName RegionId(*FString::Printf(TEXT("Body.%s"), *ExpectedKey.ToString()));
		TestTrue(FString::Printf(TEXT("Region policy %s exists"), *RegionId.ToString()), Policies.ContainsByPredicate([RegionId](const FAssetDocumentRegionPolicy& Policy)
		{
			return Policy.RegionId == RegionId;
		}));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimSequenceRegisteredProfileSchemaTest,
	"AssetFactory.AssetDocument.AnimSequence.RegisteredProfileSchema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimSequenceRegisteredProfileSchemaTest::RunTest(const FString&)
{
	FAssetDocumentService::GetProfileRegistry().Register(MakeShared<FAnimSequenceAssetDocumentProfile>());
	FAssetDocumentService Service;
	const FAssetDocumentResult Result = Service.GetSchema();
	TestTrue(TEXT("Schema succeeds"), Result.IsSuccess());
	TestTrue(TEXT("Schema has payload"), Result.Payload.IsValid());
	if (!Result.Payload.IsValid())
	{
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* RegisteredProfiles = nullptr;
	TestTrue(TEXT("Schema includes registered_profiles"), Result.Payload->TryGetArrayField(TEXT("registered_profiles"), RegisteredProfiles));
	if (!RegisteredProfiles)
	{
		return true;
	}

	TSharedPtr<FJsonObject> AnimSequenceEntry = FindObjectByStringField(*RegisteredProfiles, TEXT("Class"), TEXT("/Script/Engine.AnimSequence"));
	TestTrue(TEXT("Registered profiles include AnimSequence"), AnimSequenceEntry.IsValid());
	if (AnimSequenceEntry.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* BodySections = nullptr;
		TestTrue(TEXT("AnimSequence entry includes BodySections"), AnimSequenceEntry->TryGetArrayField(TEXT("BodySections"), BodySections));
		const TArray<TSharedPtr<FJsonValue>>* RegionPolicies = nullptr;
		TestTrue(TEXT("AnimSequence entry includes RegionPolicies"), AnimSequenceEntry->TryGetArrayField(TEXT("RegionPolicies"), RegionPolicies));
	}
	return true;
}

#endif
```

These tests must fail before the profile files exist.

- [ ] **Step 3: Run focused tests to prove failure**

Run against validation host when available:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence.ProfileShape;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AnimSequenceProfile"
```

Expected before implementation: compile or automation fails because `AnimSequenceAssetDocumentProfile.h` is missing.

- [ ] **Step 4: Implement profile and stub capability**

Create `AnimSequenceAssetDocumentCapability.h` with:

```cpp
#pragma once

#include "AssetDocumentProfile.h"

class UAnimSequence;

class FAnimSequenceAssetDocumentCapability final : public IAssetDocumentCapability
{
public:
	static const TArray<FName>& GetCanonicalBodyKeys();

	virtual FName GetName() const override;
	virtual TArray<FName> GetInternalAdapterNames() const override;
	virtual int32 GetApplyOrder() const override;
	virtual bool SupportsAsset(const UObject* Asset) const override;
	virtual bool SupportsClass(const UClass* AssetClass) const override;
	virtual TSharedRef<FJsonObject> GetSchemaHint() const override;
	virtual FAssetDocumentCapabilityResult Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const override;
	virtual FAssetDocumentCapabilityResult Preflight(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const override;
	virtual FAssetDocumentCapabilityResult Apply(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) override;
	virtual FAssetDocumentCapabilityResult Extract(const FAssetDocumentCapabilityContext& Context, TSharedRef<FJsonObject>& OutBodyJson) const override;
	virtual FAssetDocumentCapabilityResult Diff(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& DesiredJson, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const override;

private:
	FAssetDocumentCapabilityResult ValidateBodyObject(const TSharedRef<FJsonObject>& BodyObject) const;
};
```

Create `AnimSequenceAssetDocumentProfile.h` with:

```cpp
#pragma once

#include "AssetDocumentProfile.h"
#include "Profiles/AnimSequenceAssetDocumentCapability.h"

class FAnimSequenceAssetDocumentProfile final : public IAssetDocumentProfile
{
public:
	virtual UClass* GetExactClass() const override;
	virtual TSharedRef<FJsonObject> GetDocumentShape() const override;
	virtual TSharedRef<FJsonObject> CreateTemplate(const FAssetDocumentTemplateContext& Context) const override;
	virtual TArray<FName> GetBodyKeys() const override;
	virtual const IAssetDocumentCapability* ResolveBodyAdapter(FName BodyKey) const override;
	virtual TArray<FAssetDocumentRegionPolicy> GetRegionPolicies() const override;

private:
	FAnimSequenceAssetDocumentCapability BodyCapability;
};
```

Implement both `.cpp` files following `AnimMontageAssetDocumentProfile.cpp` patterns. Canonical keys are:

```cpp
References, Preview, Playback, Additive, RootMotion, Compression, Curves, Notifies, NotifyStates, NotifyTracks, SyncMarkers, Metadata, AssetUserData
```

Initial `Apply`, `Extract`, and `Diff` may return explicit unsupported failures for regions not implemented yet, but `Validate` must reject unknown keys and extract-only `_Skipped`.

- [ ] **Step 5: Add region policies**

Use `DefaultDiff` for object/scalar regions:

```text
Body.References
Body.Preview
Body.Playback
Body.Additive
Body.RootMotion
Body.Compression
```

Use `ManagedRegion` for rebuild regions:

```text
Body.Curves
Body.Notifies
Body.NotifyStates
Body.NotifyTracks
Body.SyncMarkers
Body.Metadata
Body.AssetUserData
```

Managed UE property path hints:

```text
Skeleton, RetargetSource, RetargetSourceAsset
PreviewSkeletalMesh, PreviewPoseAsset
RateScale
AdditiveAnimType, RefPoseType, RefFrameIndex, RefPoseSeq
bEnableRootMotion, RootMotionRootLock, bForceRootLock, bUseNormalizedRootMotionScale
CompressionErrorThresholdScale, BoneCompressionSettings, CurveCompressionSettings, bDoNotOverrideCompression
RawCurveData
Notifies
Notifies
AnimNotifyTracks
AuthoredSyncMarkers
MetaData
AssetUserData
```

- [ ] **Step 6: Register the profile**

Modify `AssetDocumentModule.cpp`:

```cpp
#include "Profiles/AnimSequenceAssetDocumentProfile.h"
...
FAssetDocumentService::GetProfileRegistry().Register(MakeShared<FAnimSequenceAssetDocumentProfile>());
```

Keep the AnimMontage registration intact.

- [ ] **Step 7: Update generic AssetDocument schema docs**

Append a `## UAnimSequence Profile` section to `MCP/schemas/AssetDocument.md`. Document all canonical body keys and explicitly state that AnimSequence AssetDocument is post-import only and does not author raw tracks, import settings, or compressed output.

- [ ] **Step 8: Run focused verification**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AnimSequenceTask1"
```

Expected: Task 1 profile/schema tests pass. Apply/extract tests not added yet.

- [ ] **Step 9: Commit**

```powershell
git add Source/AssetDocument/Private/AssetDocumentModule.cpp Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentProfile.* Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.* Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp MCP/schemas/AssetDocument.md
git commit -m "feat(assetdoc): register animsequence profile"
```

---

### Task 2: References, Preview, Playback, Additive, RootMotion, And Compression Regions

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`

- [ ] **Step 1: Record task base**

Run `git rev-parse HEAD` and save it as `TASK_BASE`.

- [ ] **Step 2: Add failing region tests**

Add tests under `AssetFactory.AssetDocument.AnimSequence.ScalarRegions` that:

- create or load a fixture `UAnimSequence` under `/Game/AssetDocumentTests`;
- apply `Body.Playback.RateScale`;
- apply `Body.RootMotion` fields;
- apply `Body.Additive` fields with `AdditiveAnimType`, `RefPoseType`, `RefFrameIndex`, and null `RefPoseSeq`;
- apply `Body.Preview.PreviewMesh`;
- validate rejects authored `Body.Playback.PlayLength`;
- validate rejects authored `Body.Import`.

Use existing engine tutorial skeleton/mesh paths from AnimMontage tests where available:

```cpp
const TCHAR* TestSkeletonPath = TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton");
const TCHAR* TestPreviewMeshPath = TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP");
```

Fixture creation must use UE object creation only to produce a test container sequence; do not add generator code or import pipeline code to production.

- [ ] **Step 3: Implement parsing helpers**

In `AnimSequenceAssetDocumentCapability.cpp`, add local helper functions:

```cpp
FAssetDocumentCapabilityResult RequireObjectValue(...);
FAssetDocumentCapabilityResult RequireArrayValue(...);
FAssetDocumentCapabilityResult ReadOptionalBool(...);
FAssetDocumentCapabilityResult ReadOptionalNumber(...);
FAssetDocumentCapabilityResult ReadOptionalNonNegativeNumber(...);
FAssetDocumentCapabilityResult ReadOptionalString(...);
FAssetDocumentCapabilityResult ParseAssetRef(...);
FAssetDocumentCapabilityResult RejectUnsupportedAuthoredFields(...);
```

Rejected authored fields include:

```text
Body.Import
Body.RawTracks
Body.CompressedData
Body.Playback.PlayLength
Body.Playback.NumberOfSampledKeys
Body.Playback.SamplingFrameRate
```

- [ ] **Step 4: Implement scalar/reference apply**

Implement staged parsed struct:

```cpp
struct FParsedAnimSequenceBody
{
	bool bHasPreviewMesh = false;
	USkeletalMesh* PreviewMesh = nullptr;
	bool bHasRateScale = false;
	float RateScale = 1.0f;
	bool bHasAdditiveAnimType = false;
	EAdditiveAnimationType AdditiveAnimType = AAT_None;
	bool bHasRefPoseType = false;
	EAdditiveBasePoseType RefPoseType = ABPT_None;
	bool bHasRefFrameIndex = false;
	int32 RefFrameIndex = 0;
	bool bHasRefPoseSeq = false;
	UAnimSequence* RefPoseSeq = nullptr;
	bool bHasEnableRootMotion = false;
	bool bEnableRootMotion = false;
	bool bHasRootMotionRootLock = false;
	ERootMotionRootLock::Type RootMotionRootLock = ERootMotionRootLock::RefPose;
	bool bHasForceRootLock = false;
	bool bForceRootLock = false;
	bool bHasUseNormalizedRootMotionScale = false;
	bool bUseNormalizedRootMotionScale = false;
	bool bHasCompressionErrorThresholdScale = false;
	float CompressionErrorThresholdScale = 1.0f;
	bool bHasBoneCompressionSettings = false;
	UAnimBoneCompressionSettings* BoneCompressionSettings = nullptr;
	bool bHasCurveCompressionSettings = false;
	UAnimCurveCompressionSettings* CurveCompressionSettings = nullptr;
	bool bHasDoNotOverrideCompression = false;
	bool bDoNotOverrideCompression = false;
};
```

Apply only after full parse succeeds. Call `SetPreviewMesh`, assign scalar fields, call `ValidateCompressionSettings()` if available, then `MarkPackageDirty()`.

- [ ] **Step 5: Implement extract and diff for these regions**

`Extract` must emit:

```text
References.Skeleton
References.RetargetSource
Preview.PreviewMesh
Playback.RateScale
Additive.*
RootMotion.*
Compression.*
```

Do not emit authored values for play length or sampled keys. If exposing them, put them in `_Skipped` diagnostics only.

`Diff` may follow the AnimMontage pattern: duplicate current sequence to a transient package, apply desired body to duplicate, extract current and duplicate bodies, compare only authored region keys.

- [ ] **Step 6: Run verification**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence.ScalarRegions;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AnimSequenceTask2"
```

Expected: scalar/reference region tests pass.

- [ ] **Step 7: Commit**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp
git commit -m "feat(assetdoc): apply animsequence scalar regions"
```

---

### Task 3: Curves Region

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`

- [ ] **Step 1: Record task base**

Run `git rev-parse HEAD` and save it as `TASK_BASE`.

- [ ] **Step 2: Add failing curve tests**

Add tests under `AssetFactory.AssetDocument.AnimSequence.Curves`:

- apply two float curves with out-of-order keys and verify extraction is canonical by curve name and sorted key time;
- diff reports `unchanged` after apply and `changed` after desired key value changes;
- validation rejects duplicate curve names;
- validation rejects non-float curve fields such as `TransformCurves` and `Attributes`.

- [ ] **Step 3: Implement curve parser**

Supported shape:

```json
{
  "Name": "Speed",
  "Flags": ["Default"],
  "Keys": [
    { "Time": 0.0, "Value": 0.0, "InterpMode": "RCIM_Linear" }
  ]
}
```

Rules:

- `Name` is required and unique.
- `Time` is non-negative.
- `Value` is number.
- `InterpMode` defaults to linear when omitted.
- Flags accept `Default`, `DriveMorphTarget`, `DriveAttribute`, and `Editable`; unsupported flags fail validation with path diagnostics.

- [ ] **Step 4: Implement curve apply**

Use `UAnimSequence::GetController()` and animation data controller APIs where possible. If a helper from AnimMontage curve handling is reusable, refactor only local static helpers inside AnimSequence capability; do not move shared code unless the task remains narrow.

Apply mode:

- if `Body.Curves` is present, rebuild the managed float curve region by removing existing managed curves with matching names and adding desired curves;
- bracket controller mutations if the API supports it;
- refresh cache and mark package dirty.

- [ ] **Step 5: Implement curve extract/diff**

Extract from `IAnimationDataModel::GetFloatCurves()` or equivalent. Output `Name`, `Flags`, and sorted `Keys`. Diff by curve name and canonical JSON comparison.

- [ ] **Step 6: Run verification**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence.Curves;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AnimSequenceTask3"
```

Expected: curve tests pass.

- [ ] **Step 7: Commit**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp
git commit -m "feat(assetdoc): manage animsequence curves"
```

---

### Task 4: Notifies, NotifyStates, NotifyTracks, And SyncMarkers

**Files:**
- Create: `Source/AssetDocument/Private/Profiles/AnimSequenceNotifyPlacementAdapter.h`
- Create: `Source/AssetDocument/Private/Profiles/AnimSequenceNotifyPlacementAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.h`
- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`

- [ ] **Step 1: Record task base**

Run `git rev-parse HEAD` and save it as `TASK_BASE`.

- [ ] **Step 2: Add failing timeline tests**

Add tests under `AssetFactory.AssetDocument.AnimSequence.Timeline`:

- apply `Notifies` with named notify and embedded notify object;
- apply `NotifyStates` with embedded notify state object;
- apply `NotifyTracks` with track names and order;
- apply `SyncMarkers` and verify `AuthoredSyncMarkers` sorted and unique marker cache refreshed;
- validation rejects out-of-range notify time;
- validation rejects notify-state `Duration <= 0`;
- validation rejects duplicate sync marker identities.

- [ ] **Step 3: Implement notify adapter**

Create adapter methods:

```cpp
struct FAnimSequenceNotifyPlacementResult
{
	bool bHasNotifies = false;
	TArray<FAnimNotifyEvent> Notifies;
	bool bHasNotifyStates = false;
	TArray<FAnimNotifyEvent> NotifyStates;
};

class FAnimSequenceNotifyPlacementAdapter
{
public:
	static bool IsManagedNotifyEvent(const FAnimNotifyEvent& Event, const UAnimSequenceBase* Sequence = nullptr);
	static bool IsManagedNotifyStateEvent(const FAnimNotifyEvent& Event, const UAnimSequenceBase* Sequence = nullptr);
	FAssetDocumentCapabilityResult Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonObject>& BodyObject) const;
	FAssetDocumentCapabilityResult Compile(const FAssetDocumentFragmentCompiler& Compiler, const FAssetDocumentCapabilityContext& Context, UAnimSequence* Sequence, const TSharedRef<FJsonObject>& BodyObject, FAnimSequenceNotifyPlacementResult& OutResult) const;
	FAssetDocumentCapabilityResult Extract(const FAssetDocumentFragmentCompiler& Compiler, const UAnimSequence* Sequence, TSharedRef<FJsonObject>& OutBodyJson) const;
};
```

Use fragment compiler for embedded notify and notify-state objects. Use `TrackName` to resolve or create notify track index.

- [ ] **Step 4: Implement timeline apply**

Rules:

- When `Notifies` or `NotifyStates` is present, remove only managed events for that category and preserve unmanaged events.
- Append compiled events, sort, call `SortNotifies()`, `InitializeNotifyTrack()`, and `ClampNotifiesAtEndOfSequence()`.
- When `NotifyTracks` is present, rebuild `AnimNotifyTracks` using declared order and preserve notifies by track name.
- When `SyncMarkers` is present, rebuild `AuthoredSyncMarkers`, call `SortSyncMarkers()` and `RefreshSyncMarkerDataFromAuthored()`.

- [ ] **Step 5: Implement timeline extract/diff**

Extract canonical `Notifies`, `NotifyStates`, `NotifyTracks`, and `SyncMarkers`. Diff by semantic identity, not raw array index. Preserve extracted `_Skipped` metadata only for diagnostics and never accept authored `_Skipped`.

- [ ] **Step 6: Run verification**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence.Timeline;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AnimSequenceTask4"
```

Expected: timeline tests pass.

- [ ] **Step 7: Commit**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimSequenceNotifyPlacementAdapter.* Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.* Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp
git commit -m "feat(assetdoc): manage animsequence timeline regions"
```

---

### Task 5: Metadata And AssetUserData Regions

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`

- [ ] **Step 1: Record task base**

Run `git rev-parse HEAD` and save it as `TASK_BASE`.

- [ ] **Step 2: Add failing object-fragment tests**

Add tests under `AssetFactory.AssetDocument.AnimSequence.ObjectFragments`:

- create a temporary `UAnimMetaData` Blueprint class fixture as AnimMontage tests do;
- apply `Body.Metadata` with embedded metadata objects and verify `GetMetaData()` returns managed instances;
- apply `Body.AssetUserData` with embedded user data objects when a concrete non-abstract class exists;
- validation rejects duplicate `AssetUserData` class entries without explicit `Name`;
- extract returns canonical fragment objects for metadata and user data.

- [ ] **Step 3: Implement metadata apply/extract**

Use `FAssetDocumentFragmentCompiler::RegisterBuiltInAdapters()`. For metadata:

- compile each fragment with `ExpectedBaseClass = UAnimMetaData::StaticClass()`;
- move/rename compiled objects under the sequence when needed;
- use `EmptyMetaData()` then `AddMetaData()`;
- mark package dirty.

- [ ] **Step 4: Implement AssetUserData apply/extract**

Use `IInterface_AssetUserData` methods:

- compile fragments with `ExpectedBaseClass = UAssetUserData::StaticClass()`;
- require unique class or explicit `Name` when multiple instances share a class;
- remove managed user data objects before adding new managed objects;
- do not directly mutate private arrays when interface methods are available.

- [ ] **Step 5: Run verification**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence.ObjectFragments;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AnimSequenceTask5"
```

Expected: metadata and asset-user-data tests pass. If `AssetUserData` lacks a stable concrete fixture class, keep validation and extraction support plus a deferred-field report note, and explicitly mark only that sub-region partial in the task report.

- [ ] **Step 6: Commit**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp docs/superpowers/specs/asset-document-deferred-fields/2026-06-18-animsequence.md
git commit -m "feat(assetdoc): manage animsequence object fragments"
```

---

### Task 6: Sidecar Sync, Full Extract/Diff Hardening, MCP Tests, And External Smoke

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`
- Modify: `MCP/schemas/AssetDocument.md`
- Create: `docs/superpowers/verification/asset_document_animsequence_http_smoke.py`

- [ ] **Step 1: Record task base**

Run `git rev-parse HEAD` and save it as `TASK_BASE`.

- [ ] **Step 2: Add sidecar and full roundtrip tests**

Add tests under `AssetFactory.AssetDocument.AnimSequence.Roundtrip`:

- `ApplyFile` with a sidecar under `C:/AVH1/Content/AssetDocumentSmoke/AS_PostImportSidecarSmoke.assetdoc.json`;
- extract after apply returns all supported body regions;
- diff after apply reports unchanged for authored regions;
- authored `_Skipped` is rejected by validate/apply;
- extracted `_Skipped` is ignored by diff-only path.

- [ ] **Step 3: Harden extract/diff comparison**

Ensure:

- canonical JSON comparison ignores object key order;
- `Diff` strips extracted `_Skipped` before validation/comparison;
- `Validate` and `Apply` still reject authored `_Skipped`;
- unknown body keys include guidance when old names such as `RawTracks`, `Import`, or `CompressedData` are provided.

- [ ] **Step 4: Add/update MCP tests**

Run existing MCP tests and add coverage if missing so:

- `get_asset_document_schema` includes AnimSequence profile docs in `AssetDocument.md`;
- generic tool catalog remains unchanged; no specialized AnimSequence MCP tool is added.

Run:

```powershell
Push-Location MCP
npm test
npm run build
Pop-Location
```

Expected: MCP tests pass and `MCP/dist` is updated only as generated build output.

- [ ] **Step 5: Create external HTTP smoke**

Create `docs/superpowers/verification/asset_document_animsequence_http_smoke.py`.

Smoke behavior:

1. Connect to the running Editor HTTP server on port `8559`.
2. Ensure `/Game/AssetDocumentSmoke/AS_PostImportSidecarSmoke` exists by using Editor Python only for fixture preparation when needed.
3. Write `C:/AVH1/Content/AssetDocumentSmoke/AS_PostImportSidecarSmoke.assetdoc.json`.
4. POST `/assetfactory/assetdocument/apply-file`.
5. POST `/assetfactory/assetdocument/extract`.
6. POST `/assetfactory/assetdocument/diff`.
7. Verify all authored regions are unchanged after apply.
8. Leave the `.uasset` and sidecar on disk for inspection.

Do not call HTTP endpoints from the same `-ExecutePythonScript` process as final smoke; drive an already running editor or a runner-started editor externally.

- [ ] **Step 6: Run verification**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AnimSequenceTask6"
Push-Location MCP
npm test
Pop-Location
```

Then run the external smoke against a running editor:

```powershell
py docs/superpowers/verification/asset_document_animsequence_http_smoke.py --project C:/AVH1/AVH1.uproject --port 8559
```

Expected: UBT succeeds, focused AnimSequence automation passes, MCP tests pass, external smoke passes, and the smoke asset/sidecar remain in `C:/AVH1/Content/AssetDocumentSmoke`.

- [ ] **Step 7: Commit**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp MCP docs/superpowers/verification/asset_document_animsequence_http_smoke.py
git commit -m "test(assetdoc): verify animsequence sidecar roundtrip"
```

---

### Task 7: Final Verification Report And Branch Review

**Files:**
- Create: `docs/reports/asset-document-animsequence-complete-region-benchmark.md`
- Modify only if evidence reveals documentation correction: `docs/superpowers/specs/2026-06-18-animsequence-asset-document-design.md`
- Modify only if evidence reveals deferred correction: `docs/superpowers/specs/asset-document-deferred-fields/2026-06-18-animsequence.md`

- [ ] **Step 1: Record task base**

Run `git rev-parse HEAD` and save it as `TASK_BASE`.

- [ ] **Step 2: Run final verification commands**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AnimSequenceFinal"
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AssetDocumentFinal"
Push-Location MCP
npm test
Pop-Location
py docs/superpowers/verification/asset_document_animsequence_http_smoke.py --project C:/AVH1/AVH1.uproject --port 8559
```

If the editor crashes, inspect the newest log under `C:/AVH1/Saved/Logs`.

- [ ] **Step 3: Write report**

Create `asset-document-animsequence-complete-region-benchmark.md` with:

```markdown
# AnimSequence AssetDocument Complete Region Benchmark

## Branch

- Worktree:
- Branch:
- SPEC_BASE:
- Final HEAD:
- Reviewed range:

## Completed Regions

- Body.References:
- Body.Preview:
- Body.Playback:
- Body.Additive:
- Body.RootMotion:
- Body.Compression:
- Body.Curves:
- Body.Notifies:
- Body.NotifyStates:
- Body.NotifyTracks:
- Body.SyncMarkers:
- Body.Metadata:
- Body.AssetUserData:

## Excluded Or Deferred Fields

- Import/source data:
- Raw tracks:
- Compressed output:
- Derived sampling/length:
- Referenced asset internals:

## Verification Evidence

- UBT:
- Focused AnimSequence automation:
- Full AssetDocument automation:
- MCP tests:
- External HTTP smoke:

## Smoke Asset

- Asset path:
- Sidecar path:
- How to inspect:

## Review Results

- Task-level spec reviews:
- Task-level code quality reviews:
- Final branch review:

## Residual Risk

- Known limitations:
- Follow-up candidates:
```

Replace every bullet value with concrete evidence from this branch. Do not leave empty values in the committed report.

- [ ] **Step 4: Commit report**

```powershell
git add docs/reports/asset-document-animsequence-complete-region-benchmark.md docs/superpowers/specs/2026-06-18-animsequence-asset-document-design.md docs/superpowers/specs/asset-document-deferred-fields/2026-06-18-animsequence.md
git commit -m "docs(assetdoc): report animsequence benchmark"
```

- [ ] **Step 5: Final branch review**

Dispatch a read-only final reviewer for:

```text
SPEC_BASE..HEAD
```

Reviewer focus:

- no legacy generator/importer expansion;
- no specialized MCP tools;
- all managed regions have validate/apply/extract/diff coverage or documented partial status;
- tests and smoke evidence are real;
- sidecar remains post-import and delta-first;
- no accidental user/unrelated files included.

If findings appear, fix them in a new checkpoint commit and re-run the relevant verification.

---

## Self-Review

**Spec coverage:** This plan covers the exact profile, canonical body keys, region policies, scalar/reference regions, curves, timeline events, sync markers, metadata, asset user data, sidecar roundtrip, MCP schema exposure, external smoke, and final report required by the spec.

**Scope check:** The plan explicitly avoids old generator expansion, importer/reimport pipeline changes, raw animation authoring, compressed output authoring, and specialized MCP tools.

**Task boundaries:** Tasks are split by adjacent region groups and each task has a checkpoint commit plus focused verification. Implementation subagents should run sequentially because all tasks modify the same profile/capability files.

**Type consistency:** The plan consistently uses `FAnimSequenceAssetDocumentProfile`, `FAnimSequenceAssetDocumentCapability`, `FAnimSequenceNotifyPlacementAdapter`, `Body.*` region names from the spec, and `AssetFactory.AssetDocument.AnimSequence.*` automation names.

**Execution mode:** The user already chose subagent-driven execution. After this plan is committed, use `superpowers:subagent-driven-development` and execute tasks continuously.
