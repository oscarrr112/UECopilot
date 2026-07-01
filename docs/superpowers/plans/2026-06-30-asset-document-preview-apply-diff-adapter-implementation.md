# AssetDocument Preview Apply Diff Adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a shared preview-apply-diff scaffold and migrate AnimMontage plus AnimSequence Body diff flows to it without moving asset-specific semantics into the public layer.

**Architecture:** Add a private-region helper `FAssetDocumentPreviewApplyDiffAdapter` that orchestrates Body diff lifecycle through composition hooks: validate desired Body, duplicate preview asset, make dry-run preview context, apply desired Body, extract current/preview Body, then emit per-key diff entries. Keep canonical JSON comparison and diff entry shape on `FAssetDocumentJsonRegionUtils`; keep UE asset materialization and AnimSequence pilot region diff in profile hooks.

**Tech Stack:** Unreal Engine 5.7 C++ plugin code, UE JSON DOM, AssetDocument private region runtime utilities, UE Automation Tests, `C:/AVH1` validation host.

---

## Source Specs And Base

- Spec: `docs/superpowers/specs/2026-06-30-asset-document-preview-apply-diff-adapter-design.md`
- Refactor chain: `docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md`
- Prior implementation base: `feature/asset-document-object-field-schema-dispatcher-migration`
- This spec should execute on a stacked branch: `feature/asset-document-preview-apply-diff-adapter`
- Worktree: use the already isolated linked worktree or a sibling worktree prepared by the controller.

## Non-Negotiable Constraints

- Do not add asset type switches to the public adapter. No `UAnimSequence` or `UAnimMontage` checks in `AssetDocumentPreviewApplyDiffAdapter.*`.
- Do not modify MCP TypeScript contracts.
- Do not modify graph/tree materializers, fragment compiler, object field schema utility, object region adapter, named array adapter, or body dispatcher.
- Do not migrate timeline placement, identity array diff, graph diff, or fragment arrays in this spec.
- Preserve diff entry shape: `{ "path", "status", "current", "desired" }`.
- Preserve `_Skipped` handling: strip it before diff traversal and from extracted current/preview bodies.
- Preserve diagnostic codes and paths for invalid Body type, unsupported asset, duplicate failure, apply failure, and extract failure.
- Preserve AnimSequence pilot diff handling for `Preview`, `Playback`, and `NotifyTracks`.
- Every task must start with `TASK_BASE=$(git rev-parse HEAD)` and must end with a checkpoint commit.
- Review scopes must use `TASK_BASE..HEAD`, not `master..HEAD`.

## File Map

- Create: `Source/AssetDocument/Private/Regions/AssetDocumentPreviewApplyDiffAdapter.h`
  - Declares `FAssetDocumentPreviewApplyDiffBodyKeyContext`, `FAssetDocumentPreviewApplyDiffHooks`, and `FAssetDocumentPreviewApplyDiffAdapter`.
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentPreviewApplyDiffAdapter.cpp`
  - Implements Body object guard, `_Skipped` stripping, preview lifecycle orchestration, default canonical body-key diff, and optional per-key override.
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`
  - Adds public runtime style tests for the adapter using fake `UObject` preview assets and hook counters.
- Modify: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`
  - Replaces profile-local preview diff scaffold with the adapter.
  - Removes duplicate `JsonValueToComparableString`, `AddBodyDiffEntry`, and `MakeBodyObjectForDiff` only after they become unused.
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`
  - Keeps existing diff tests green and adds `_Skipped`/changed-section regression if not already covered.
- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
  - Replaces profile-local preview diff scaffold with the adapter.
  - Keeps `DiffAnimSequenceObjectPilotRegion` and `DiffAnimSequenceNotifyTracksPilotRegion` as per-key override hooks.
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`
  - Adds/keeps assertions that `Preview`, `Playback`, and `NotifyTracks` diff paths still use existing pilot behavior.

## Validation Commands

Fast check:

```powershell
git diff --check
```

UBT:

```powershell
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/AVH1/AVH1.uproject' -NoHotReload
```

Automation filters:

```text
AssetFactory.AssetDocument.RegionRuntime.PreviewApplyDiff
AssetFactory.AssetDocument.AnimMontage
AssetFactory.AssetDocument.AnimSequence
AssetFactory.AssetDocument
```

Report parser:

```powershell
$p='C:/AVH1/Saved/AutomationReports/<ReportName>/index.json'
$j=Get-Content -Raw -LiteralPath $p | ConvertFrom-Json
$tests=@($j.tests)
[pscustomobject]@{
  tests=$tests.Count
  success=($tests|?{$_.state -eq 'Success'}).Count
  fail=($tests|?{$_.state -eq 'Fail'}).Count
  skipped=($tests|?{$_.state -eq 'Skipped'}).Count
  warnings=($tests|?{$_.state -eq 'SuccessWithWarnings'}).Count
  report=$p
} | ConvertTo-Json -Compress
```

## Task 1: Add Preview Apply Diff Adapter

**Files:**
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentPreviewApplyDiffAdapter.h`
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentPreviewApplyDiffAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: clean branch before task edits.

- [ ] **Step 2: Add failing adapter tests**

In `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`, add:

```cpp
#include "Regions/AssetDocumentPreviewApplyDiffAdapter.h"
```

Then append tests before `#endif`. The tests must cover these exact behaviors:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimePreviewApplyDiffRejectsNonObjectBodyTest,
	"AssetFactory.AssetDocument.RegionRuntime.PreviewApplyDiff.RejectsNonObjectBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimePreviewApplyDiffRejectsNonObjectBodyTest::RunTest(const FString&)
{
	FAssetDocumentPreviewApplyDiffHooks Hooks;
	FAssetDocumentPreviewApplyDiffAdapter Adapter(MoveTemp(Hooks));
	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	FAssetDocumentCapabilityContext Context;
	const FAssetDocumentCapabilityResult Result =
		Adapter.DiffBody(Context, MakeShared<FJsonValueString>(TEXT("not object")), DiffEntries);

	TestFalse(TEXT("Non-object Body fails"), Result.bSuccess);
	TestEqual(TEXT("Non-object Body diagnostic code"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(), FString(TEXT("InvalidBodyType")));
	TestEqual(TEXT("Non-object Body diagnostic path"), Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Path : FString(), FString(TEXT("/Body")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimePreviewApplyDiffDefaultDiffAndSkippedTest,
	"AssetFactory.AssetDocument.RegionRuntime.PreviewApplyDiff.DefaultDiffAndSkipped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimePreviewApplyDiffDefaultDiffAndSkippedTest::RunTest(const FString&)
{
	int32 ValidateCalls = 0;
	int32 DuplicateCalls = 0;
	int32 ApplyCalls = 0;
	int32 ExtractCalls = 0;
	TArray<FString> TraversedKeys;
	FAssetDocumentPreviewApplyDiffHooks Hooks;
	Hooks.ValidateDesiredBody = [&ValidateCalls](const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonObject>& Body)
	{
		++ValidateCalls;
		return Body->HasField(TEXT("_Skipped"))
			? FAssetDocumentCapabilityResult::Failure(TEXT("_Skipped was not stripped before validate"), TEXT("/Body/_Skipped"), TEXT("SkippedLeak"))
			: FAssetDocumentCapabilityResult::Success(TEXT("validated"));
	};
	Hooks.DuplicatePreviewAsset = [&DuplicateCalls](const FAssetDocumentCapabilityContext&, UObject*& OutPreviewAsset)
	{
		++DuplicateCalls;
		OutPreviewAsset = NewObject<UObject>(GetTransientPackage());
		return FAssetDocumentCapabilityResult::Success(TEXT("duplicated"));
	};
	Hooks.MakePreviewContext = [](const FAssetDocumentCapabilityContext& Context, UObject* PreviewAsset)
	{
		FAssetDocumentCapabilityContext PreviewContext = Context;
		PreviewContext.Asset = PreviewAsset;
		PreviewContext.AssetClass = UObject::StaticClass();
		PreviewContext.bIsDryRun = true;
		return PreviewContext;
	};
	Hooks.ApplyDesiredBody = [&ApplyCalls](const FAssetDocumentCapabilityContext& PreviewContext, const TSharedRef<FJsonValue>& DesiredBody)
	{
		++ApplyCalls;
		if (!PreviewContext.bIsDryRun || DesiredBody->AsObject()->HasField(TEXT("_Skipped")))
		{
			return FAssetDocumentCapabilityResult::Failure(TEXT("preview apply context/body invalid"), TEXT("/Body"), TEXT("PreviewApplyInvalid"));
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("applied"));
	};
	Hooks.ExtractBody = [&ExtractCalls](const FAssetDocumentCapabilityContext& ExtractContext, const TSharedRef<FJsonObject>& OutBody)
	{
		++ExtractCalls;
		TSharedRef<FJsonObject> Playback = MakeShared<FJsonObject>();
		Playback->SetNumberField(TEXT("RateScale"), ExtractContext.bIsDryRun ? 2.0 : 1.0);
		OutBody->SetObjectField(TEXT("Playback"), Playback);
		OutBody->SetObjectField(TEXT("_Skipped"), MakeShared<FJsonObject>());
		return FAssetDocumentCapabilityResult::Success(TEXT("extracted"));
	};
	Hooks.DiffBodyKey = [&TraversedKeys](
		const FAssetDocumentPreviewApplyDiffBodyKeyContext& KeyContext,
		TArray<TSharedPtr<FJsonValue>>&,
		bool& bOutHandled)
	{
		TraversedKeys.Add(KeyContext.BodyKey);
		bOutHandled = false;
		return FAssetDocumentCapabilityResult::Success(TEXT("not handled"));
	};

	TSharedRef<FJsonObject> DesiredBody = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> DesiredPlayback = MakeShared<FJsonObject>();
	DesiredPlayback->SetNumberField(TEXT("RateScale"), 2.0);
	DesiredBody->SetObjectField(TEXT("Playback"), DesiredPlayback);
	DesiredBody->SetObjectField(TEXT("_Skipped"), MakeShared<FJsonObject>());

	FAssetDocumentPreviewApplyDiffAdapter Adapter(MoveTemp(Hooks));
	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	FAssetDocumentCapabilityContext Context;
	const FAssetDocumentCapabilityResult Result =
		Adapter.DiffBody(Context, MakeShared<FJsonValueObject>(DesiredBody), DiffEntries);

	TestTrue(TEXT("Preview apply diff succeeds"), Result.bSuccess);
	TestEqual(TEXT("Validate called once"), ValidateCalls, 1);
	TestEqual(TEXT("Duplicate called once"), DuplicateCalls, 1);
	TestEqual(TEXT("Apply called once"), ApplyCalls, 1);
	TestEqual(TEXT("Extract called for current and preview"), ExtractCalls, 2);
	TestTrue(TEXT("Playback was traversed"), TraversedKeys.Contains(TEXT("Playback")));
	TestFalse(TEXT("_Skipped was not traversed"), TraversedKeys.Contains(TEXT("_Skipped")));
	TestEqual(TEXT("One default diff entry"), DiffEntries.Num(), 1);
	TestEqual(TEXT("Diff path"), DiffEntries.Num() > 0 ? DiffEntries[0]->AsObject()->GetStringField(TEXT("path")) : FString(), FString(TEXT("/Body/Playback")));
	TestEqual(TEXT("Diff status changed"), DiffEntries.Num() > 0 ? DiffEntries[0]->AsObject()->GetStringField(TEXT("status")) : FString(), FString(TEXT("changed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimePreviewApplyDiffCanonicalAndOverrideTest,
	"AssetFactory.AssetDocument.RegionRuntime.PreviewApplyDiff.CanonicalAndOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimePreviewApplyDiffCanonicalAndOverrideTest::RunTest(const FString&)
{
	FAssetDocumentPreviewApplyDiffHooks Hooks;
	Hooks.ValidateDesiredBody = [](const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonObject>&)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("validated"));
	};
	Hooks.DuplicatePreviewAsset = [](const FAssetDocumentCapabilityContext&, UObject*& OutPreviewAsset)
	{
		OutPreviewAsset = NewObject<UObject>(GetTransientPackage());
		return FAssetDocumentCapabilityResult::Success(TEXT("duplicated"));
	};
	Hooks.MakePreviewContext = [](const FAssetDocumentCapabilityContext& Context, UObject* PreviewAsset)
	{
		FAssetDocumentCapabilityContext PreviewContext = Context;
		PreviewContext.Asset = PreviewAsset;
		PreviewContext.bIsDryRun = true;
		return PreviewContext;
	};
	Hooks.ApplyDesiredBody = [](const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonValue>&)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("applied"));
	};
	Hooks.ExtractBody = [](const FAssetDocumentCapabilityContext& ExtractContext, const TSharedRef<FJsonObject>& OutBody)
	{
		TSharedRef<FJsonObject> Ordered = MakeShared<FJsonObject>();
		if (ExtractContext.bIsDryRun)
		{
			Ordered->SetStringField(TEXT("B"), TEXT("two"));
			Ordered->SetStringField(TEXT("A"), TEXT("one"));
		}
		else
		{
			Ordered->SetStringField(TEXT("A"), TEXT("one"));
			Ordered->SetStringField(TEXT("B"), TEXT("two"));
		}
		OutBody->SetObjectField(TEXT("Ordered"), Ordered);
		OutBody->SetStringField(TEXT("Override"), ExtractContext.bIsDryRun ? TEXT("preview") : TEXT("current"));
		return FAssetDocumentCapabilityResult::Success(TEXT("extracted"));
	};
	Hooks.DiffBodyKey = [](
		const FAssetDocumentPreviewApplyDiffBodyKeyContext& KeyContext,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries,
		bool& bOutHandled)
	{
		if (KeyContext.BodyKey != TEXT("Override"))
		{
			bOutHandled = false;
			return FAssetDocumentCapabilityResult::Success(TEXT("not handled"));
		}
		FAssetDocumentJsonRegionUtils::AddDiffEntry(
			OutDiffEntries,
			KeyContext.JsonPointer,
			TEXT("changed"),
			KeyContext.CurrentValue,
			KeyContext.DesiredValue);
		bOutHandled = true;
		return FAssetDocumentCapabilityResult::Success(TEXT("handled"));
	};

	TSharedRef<FJsonObject> DesiredBody = MakeShared<FJsonObject>();
	DesiredBody->SetObjectField(TEXT("Ordered"), MakeShared<FJsonObject>());
	DesiredBody->SetStringField(TEXT("Override"), TEXT("desired"));

	FAssetDocumentPreviewApplyDiffAdapter Adapter(MoveTemp(Hooks));
	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	FAssetDocumentCapabilityContext Context;
	const FAssetDocumentCapabilityResult Result =
		Adapter.DiffBody(Context, MakeShared<FJsonValueObject>(DesiredBody), DiffEntries);

	TestTrue(TEXT("Preview apply diff succeeds"), Result.bSuccess);
	TestEqual(TEXT("Two entries emitted"), DiffEntries.Num(), 2);
	TestEqual(TEXT("Canonical object order stays unchanged"), DiffEntries.Num() > 0 ? DiffEntries[0]->AsObject()->GetStringField(TEXT("status")) : FString(), FString(TEXT("unchanged")));
	TestEqual(TEXT("Override entry path"), DiffEntries.Num() > 1 ? DiffEntries[1]->AsObject()->GetStringField(TEXT("path")) : FString(), FString(TEXT("/Body/Override")));
	return true;
}
```

Run UBT before implementation. Expected: compile fails because `Regions/AssetDocumentPreviewApplyDiffAdapter.h` does not exist.

- [ ] **Step 3: Add adapter header**

Create `Source/AssetDocument/Private/Regions/AssetDocumentPreviewApplyDiffAdapter.h`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class UObject;

struct FAssetDocumentPreviewApplyDiffBodyKeyContext
{
	const FAssetDocumentCapabilityContext* CurrentContext = nullptr;
	const FAssetDocumentCapabilityContext* PreviewContext = nullptr;
	FString BodyKey;
	FString JsonPointer;
	TSharedPtr<FJsonValue> CurrentValue;
	TSharedPtr<FJsonValue> DesiredValue;
};

struct FAssetDocumentPreviewApplyDiffHooks
{
	TFunction<FAssetDocumentCapabilityResult(const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonObject>&)> ValidateDesiredBody;
	TFunction<FAssetDocumentCapabilityResult(const FAssetDocumentCapabilityContext&, UObject*&)> DuplicatePreviewAsset;
	TFunction<FAssetDocumentCapabilityContext(const FAssetDocumentCapabilityContext&, UObject*)> MakePreviewContext;
	TFunction<FAssetDocumentCapabilityResult(const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonValue>&)> ApplyDesiredBody;
	TFunction<FAssetDocumentCapabilityResult(const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonObject>&)> ExtractBody;
	TFunction<FAssetDocumentCapabilityResult(const FAssetDocumentPreviewApplyDiffBodyKeyContext&, TArray<TSharedPtr<FJsonValue>>&, bool&)> DiffBodyKey;
};

class FAssetDocumentPreviewApplyDiffAdapter
{
public:
	explicit FAssetDocumentPreviewApplyDiffAdapter(FAssetDocumentPreviewApplyDiffHooks InHooks);

	FAssetDocumentCapabilityResult DiffBody(
		const FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonValue>& DesiredJson,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const;

	static TSharedRef<FJsonObject> MakeBodyObjectForDiff(const TSharedRef<FJsonObject>& BodyObject);

private:
	FAssetDocumentPreviewApplyDiffHooks Hooks;
};
```

- [ ] **Step 4: Add adapter implementation**

Create `Source/AssetDocument/Private/Regions/AssetDocumentPreviewApplyDiffAdapter.cpp`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentPreviewApplyDiffAdapter.h"

#include "AssetDocumentJsonRegionUtils.h"

#include "UObject/UObjectGlobals.h"

namespace
{
FAssetDocumentCapabilityResult InvalidHookFailure(const FString& HookName)
{
	return FAssetDocumentJsonRegionUtils::Failure(
		TEXT("/Body"),
		TEXT("InvalidPreviewApplyDiffAdapter"),
		FString::Printf(TEXT("Preview apply diff adapter is missing %s hook"), *HookName));
}
}

FAssetDocumentPreviewApplyDiffAdapter::FAssetDocumentPreviewApplyDiffAdapter(FAssetDocumentPreviewApplyDiffHooks InHooks)
	: Hooks(MoveTemp(InHooks))
{
}

TSharedRef<FJsonObject> FAssetDocumentPreviewApplyDiffAdapter::MakeBodyObjectForDiff(const TSharedRef<FJsonObject>& BodyObject)
{
	if (!BodyObject->HasField(TEXT("_Skipped")))
	{
		return BodyObject;
	}

	TSharedRef<FJsonObject> DiffBody = MakeShared<FJsonObject>();
	DiffBody->Values = BodyObject->Values;
	DiffBody->RemoveField(TEXT("_Skipped"));
	return DiffBody;
}

FAssetDocumentCapabilityResult FAssetDocumentPreviewApplyDiffAdapter::DiffBody(
	const FAssetDocumentCapabilityContext& Context,
	const TSharedRef<FJsonValue>& DesiredJson,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	if (DesiredJson->Type != EJson::Object)
	{
		return FAssetDocumentJsonRegionUtils::Failure(TEXT("/Body"), TEXT("InvalidBodyType"), TEXT("Body must be a JSON object"));
	}

	const TSharedPtr<FJsonObject> DesiredBody = DesiredJson->AsObject();
	if (!DesiredBody.IsValid())
	{
		return FAssetDocumentJsonRegionUtils::Failure(TEXT("/Body"), TEXT("InvalidBodyType"), TEXT("Body must be a JSON object"));
	}

	const TSharedRef<FJsonObject> DesiredBodyForDiff = MakeBodyObjectForDiff(DesiredBody.ToSharedRef());
	const TSharedRef<FJsonValue> DesiredJsonForDiff = MakeShared<FJsonValueObject>(DesiredBodyForDiff);

	if (!Hooks.ValidateDesiredBody)
	{
		return InvalidHookFailure(TEXT("ValidateDesiredBody"));
	}
	FAssetDocumentCapabilityResult Result = Hooks.ValidateDesiredBody(Context, DesiredBodyForDiff);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (!Hooks.DuplicatePreviewAsset)
	{
		return InvalidHookFailure(TEXT("DuplicatePreviewAsset"));
	}
	UObject* PreviewAsset = nullptr;
	Result = Hooks.DuplicatePreviewAsset(Context, PreviewAsset);
	if (!Result.bSuccess)
	{
		return Result;
	}
	if (!PreviewAsset)
	{
		return FAssetDocumentJsonRegionUtils::Failure(TEXT("/Body"), TEXT("DuplicateFailed"), TEXT("Failed to duplicate asset for Body diff"));
	}

	if (!Hooks.MakePreviewContext)
	{
		return InvalidHookFailure(TEXT("MakePreviewContext"));
	}
	FAssetDocumentCapabilityContext PreviewContext = Hooks.MakePreviewContext(Context, PreviewAsset);

	if (!Hooks.ApplyDesiredBody)
	{
		return InvalidHookFailure(TEXT("ApplyDesiredBody"));
	}
	Result = Hooks.ApplyDesiredBody(PreviewContext, DesiredJsonForDiff);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (!Hooks.ExtractBody)
	{
		return InvalidHookFailure(TEXT("ExtractBody"));
	}
	TSharedRef<FJsonObject> CurrentBody = MakeShared<FJsonObject>();
	Result = Hooks.ExtractBody(Context, CurrentBody);
	if (!Result.bSuccess)
	{
		return Result;
	}
	CurrentBody = MakeBodyObjectForDiff(CurrentBody);

	TSharedRef<FJsonObject> PreviewBody = MakeShared<FJsonObject>();
	Result = Hooks.ExtractBody(PreviewContext, PreviewBody);
	if (!Result.bSuccess)
	{
		return Result;
	}
	PreviewBody = MakeBodyObjectForDiff(PreviewBody);

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : DesiredBodyForDiff->Values)
	{
		if (Pair.Key == TEXT("_Skipped"))
		{
			continue;
		}

		const TSharedPtr<FJsonValue>* CurrentValue = CurrentBody->Values.Find(Pair.Key);
		const TSharedPtr<FJsonValue> Current = CurrentValue ? *CurrentValue : MakeShared<FJsonValueNull>();
		const TSharedPtr<FJsonValue>* DesiredValue = PreviewBody->Values.Find(Pair.Key);
		const TSharedPtr<FJsonValue> Desired = DesiredValue ? *DesiredValue : MakeShared<FJsonValueNull>();

		if (Hooks.DiffBodyKey)
		{
			bool bHandled = false;
			FAssetDocumentPreviewApplyDiffBodyKeyContext KeyContext;
			KeyContext.CurrentContext = &Context;
			KeyContext.PreviewContext = &PreviewContext;
			KeyContext.BodyKey = Pair.Key;
			KeyContext.JsonPointer = FAssetDocumentJsonRegionUtils::MakeBodyPath(Pair.Key);
			KeyContext.CurrentValue = Current;
			KeyContext.DesiredValue = Desired;
			Result = Hooks.DiffBodyKey(KeyContext, OutDiffEntries, bHandled);
			if (!Result.bSuccess)
			{
				return Result;
			}
			if (bHandled)
			{
				continue;
			}
		}

		const FString Status =
			FAssetDocumentJsonRegionUtils::JsonValueToComparableString(Current) ==
			FAssetDocumentJsonRegionUtils::JsonValueToComparableString(Desired)
				? TEXT("unchanged")
				: TEXT("changed");
		FAssetDocumentJsonRegionUtils::AddDiffEntry(
			OutDiffEntries,
			FAssetDocumentJsonRegionUtils::MakeBodyPath(Pair.Key),
			Status,
			Current,
			Desired);
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Preview apply Body diffed"));
}
```

- [ ] **Step 5: Run focused validation**

```powershell
git diff --check
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/AVH1/AVH1.uproject' -NoHotReload
$report='C:/AVH1/Saved/AutomationReports/PreviewApplyDiffRegionRuntime'
if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report -Recurse -Force }
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.RegionRuntime.PreviewApplyDiff;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected: UBT succeeds and all `PreviewApplyDiff` runtime tests pass.

- [ ] **Step 6: Commit Task 1**

```powershell
git add Source/AssetDocument/Private/Regions/AssetDocumentPreviewApplyDiffAdapter.h Source/AssetDocument/Private/Regions/AssetDocumentPreviewApplyDiffAdapter.cpp Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp
git commit -m "feat: add asset document preview apply diff adapter"
```

## Task 2: Migrate AnimMontage Diff

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: clean worktree after Task 1 commit.

- [ ] **Step 2: Add/confirm AnimMontage diff regression coverage**

In `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`, keep these existing tests green:

- `AssetFactory.AssetDocument.AnimMontage.DiffTreatsDefinitionRefAsUnchanged`
- `AssetFactory.AssetDocument.AnimMontage.DiffIgnoresObjectFieldOrder`
- `AssetFactory.AssetDocument.AnimMontage.DiffReportsChangedBodySections`

If there is no dedicated `_Skipped` diff test, add this check near existing diff tests:

```cpp
	TSharedPtr<FJsonObject> SkippedDocument = MakeStructuredMontageDocument(Target, AnimSequence->GetPathName());
	SkippedDocument->GetObjectField(TEXT("Body"))->SetObjectField(TEXT("_Skipped"), MakeShared<FJsonObject>());
	const FAssetDocumentResult SkippedDiffResult = DiffDocument(SkippedDocument);
	TestTrue(TEXT("AnimMontage diff ignores extract-only Body._Skipped metadata"), SkippedDiffResult.IsSuccess());
	if (SkippedDiffResult.Payload.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* Changed = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Unchanged = nullptr;
		TestTrue(TEXT("Skipped diff payload includes changed array"), SkippedDiffResult.Payload->TryGetArrayField(TEXT("changed"), Changed));
		TestTrue(TEXT("Skipped diff payload includes unchanged array"), SkippedDiffResult.Payload->TryGetArrayField(TEXT("unchanged"), Unchanged));
		TestFalse(TEXT("Skipped diff does not report changed Body._Skipped"), Changed && JsonArrayContainsPath(*Changed, TEXT("/Body/_Skipped")));
		TestFalse(TEXT("Skipped diff does not report unchanged Body._Skipped"), Unchanged && JsonArrayContainsPath(*Unchanged, TEXT("/Body/_Skipped")));
	}
```

Run `AssetFactory.AssetDocument.AnimMontage.Diff` or the closest available AnimMontage diff filter before production changes. Expected: existing behavior passes; new `_Skipped` test should pass today or fail for a real behavior gap.

- [ ] **Step 3: Include the adapter**

In `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`, add:

```cpp
#include "Regions/AssetDocumentPreviewApplyDiffAdapter.h"
```

- [ ] **Step 4: Replace AnimMontage Diff scaffold with adapter**

Replace the body of `FAnimMontageAssetDocumentCapability::Diff` after the initial function signature with:

```cpp
	FAssetDocumentPreviewApplyDiffHooks Hooks;
	Hooks.ValidateDesiredBody = [this](
		const FAssetDocumentCapabilityContext& ValidateContext,
		const TSharedRef<FJsonObject>& DesiredBody)
	{
		return ValidateBodyObject(ValidateContext, DesiredBody);
	};
	Hooks.DuplicatePreviewAsset = [](const FAssetDocumentCapabilityContext& DiffContext, UObject*& OutPreviewAsset)
	{
		UAnimMontage* CurrentMontage = Cast<UAnimMontage>(DiffContext.Asset);
		if (!CurrentMontage)
		{
			return BodyFailure(TEXT("AnimMontage body diff requires UAnimMontage asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
		}

		UAnimMontage* PreviewMontage = DuplicateObject<UAnimMontage>(CurrentMontage, GetTransientPackage());
		if (!PreviewMontage)
		{
			return BodyFailure(TEXT("Failed to duplicate AnimMontage for Body diff"), TEXT("/Body"), TEXT("DuplicateFailed"));
		}

		OutPreviewAsset = PreviewMontage;
		return FAssetDocumentCapabilityResult::Success(TEXT("Duplicated AnimMontage for Body diff"));
	};
	Hooks.MakePreviewContext = [](const FAssetDocumentCapabilityContext& DiffContext, UObject* PreviewAsset)
	{
		FAssetDocumentCapabilityContext PreviewContext = DiffContext;
		PreviewContext.Asset = PreviewAsset;
		PreviewContext.AssetClass = UAnimMontage::StaticClass();
		PreviewContext.bIsDryRun = true;
		return PreviewContext;
	};
	Hooks.ApplyDesiredBody = [this](
		const FAssetDocumentCapabilityContext& PreviewContext,
		const TSharedRef<FJsonValue>& DesiredBody)
	{
		return const_cast<FAnimMontageAssetDocumentCapability*>(this)->Apply(PreviewContext, DesiredBody);
	};
	Hooks.ExtractBody = [this](
		const FAssetDocumentCapabilityContext& ExtractContext,
		const TSharedRef<FJsonObject>& OutBody)
	{
		return Extract(ExtractContext, OutBody);
	};

	FAssetDocumentPreviewApplyDiffAdapter Adapter(MoveTemp(Hooks));
	return Adapter.DiffBody(Context, DesiredJson, OutDiffEntries);
```

Remove profile-local `JsonValueToComparableString`, `AddBodyDiffEntry`, and `MakeBodyObjectForDiff` from AnimMontage if they become unused. Do not remove any helper still referenced outside `Diff`.

- [ ] **Step 5: Run focused validation**

```powershell
git diff --check
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/AVH1/AVH1.uproject' -NoHotReload
$report='C:/AVH1/Saved/AutomationReports/PreviewApplyDiffAnimMontage'
if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report -Recurse -Force }
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected: AnimMontage automation passes with no failures or warnings.

- [ ] **Step 6: Commit Task 2**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp
git commit -m "refactor: migrate anim montage preview apply diff"
```

## Task 3: Migrate AnimSequence Diff

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: clean worktree after Task 2 commit.

- [ ] **Step 2: Strengthen AnimSequence pilot diff regression tests**

In `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`, keep the existing scalar diff assertions around `Playback`:

```cpp
const TSharedPtr<FJsonObject> PlaybackDiff = FindDiffEntryByPath(DiffEntries, TEXT("/Body/Playback"));
TestEqual(TEXT("Diff marks Playback changed before apply"), PlaybackDiff->GetStringField(TEXT("status")), FString(TEXT("changed")));
```

Add a `Preview` diff check near the existing scalar-region diff checks if it does not already exist:

```cpp
	TSharedRef<FJsonObject> PreviewDiffBody = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> PreviewDiff = MakeShared<FJsonObject>();
	PreviewDiff->SetObjectField(TEXT("PreviewMesh"), MakeAssetRef(TestPreviewMeshPath));
	PreviewDiffBody->SetObjectField(TEXT("Preview"), PreviewDiff);
	DiffEntries.Reset();
	const FAssetDocumentCapabilityResult PreviewDiffResult = Capability.Diff(Context, MakeBodyValue(PreviewDiffBody), DiffEntries);
	TestTrue(TEXT("Diff succeeds for Preview pilot region"), PreviewDiffResult.bSuccess);
	const TSharedPtr<FJsonObject> PreviewDiffEntry = FindDiffEntryByPath(DiffEntries, TEXT("/Body/Preview"));
	TestTrue(TEXT("Diff reports Preview path through pilot region"), PreviewDiffEntry.IsValid());
```

Keep existing service-level `_Skipped` diff tests around `Diff ignores extract-only Body._Skipped metadata`; they must remain green after migration.

- [ ] **Step 3: Include the adapter**

In `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp`, add:

```cpp
#include "Regions/AssetDocumentPreviewApplyDiffAdapter.h"
```

- [ ] **Step 4: Replace AnimSequence Diff scaffold with adapter**

Replace `FAnimSequenceAssetDocumentCapability::Diff` with a hook-based implementation. The hooks must preserve the existing validation parser and pilot diff paths:

```cpp
	FAssetDocumentFragmentCompiler Compiler;
	Compiler.RegisterBuiltInAdapters();

	FAssetDocumentPreviewApplyDiffHooks Hooks;
	Hooks.ValidateDesiredBody = [&Compiler](
		const FAssetDocumentCapabilityContext& ValidateContext,
		const TSharedRef<FJsonObject>& DesiredBody)
	{
		FParsedAnimSequenceBody ParsedForValidation;
		return ParseAnimSequenceBody(
			&Compiler,
			ValidateContext,
			Cast<UAnimSequence>(ValidateContext.Asset),
			DesiredBody,
			false,
			ParsedForValidation);
	};
	Hooks.DuplicatePreviewAsset = [](const FAssetDocumentCapabilityContext& DiffContext, UObject*& OutPreviewAsset)
	{
		UAnimSequence* CurrentSequence = Cast<UAnimSequence>(DiffContext.Asset);
		if (!CurrentSequence)
		{
			return BodyFailure(TEXT("AnimSequence body diff requires UAnimSequence asset"), TEXT("/Body"), TEXT("UnsupportedAsset"));
		}

		UAnimSequence* PreviewSequence = DuplicateObject<UAnimSequence>(CurrentSequence, GetTransientPackage());
		if (!PreviewSequence)
		{
			return BodyFailure(TEXT("Failed to duplicate AnimSequence for Body diff"), TEXT("/Body"), TEXT("DuplicateFailed"));
		}

		OutPreviewAsset = PreviewSequence;
		return FAssetDocumentCapabilityResult::Success(TEXT("Duplicated AnimSequence for Body diff"));
	};
	Hooks.MakePreviewContext = [](const FAssetDocumentCapabilityContext& DiffContext, UObject* PreviewAsset)
	{
		FAssetDocumentCapabilityContext PreviewContext = DiffContext;
		PreviewContext.Asset = PreviewAsset;
		PreviewContext.AssetClass = UAnimSequence::StaticClass();
		PreviewContext.bIsDryRun = true;
		return PreviewContext;
	};
	Hooks.ApplyDesiredBody = [this](
		const FAssetDocumentCapabilityContext& PreviewContext,
		const TSharedRef<FJsonValue>& DesiredBody)
	{
		return const_cast<FAnimSequenceAssetDocumentCapability*>(this)->Apply(PreviewContext, DesiredBody);
	};
	Hooks.ExtractBody = [this](
		const FAssetDocumentCapabilityContext& ExtractContext,
		const TSharedRef<FJsonObject>& OutBody)
	{
		return Extract(ExtractContext, OutBody);
	};
	Hooks.DiffBodyKey = [](
		const FAssetDocumentPreviewApplyDiffBodyKeyContext& KeyContext,
		TArray<TSharedPtr<FJsonValue>>& OutEntries,
		bool& bOutHandled)
	{
		bOutHandled = false;
		if (!KeyContext.CurrentContext)
		{
			return FAssetDocumentCapabilityResult::Failure(TEXT("Missing current diff context"), TEXT("/Body"), TEXT("InvalidPreviewApplyDiffAdapter"));
		}
		if (KeyContext.BodyKey == TEXT("Preview") || KeyContext.BodyKey == TEXT("Playback"))
		{
			bOutHandled = true;
			return DiffAnimSequenceObjectPilotRegion(
				*KeyContext.CurrentContext,
				FName(*KeyContext.BodyKey),
				KeyContext.CurrentValue,
				KeyContext.DesiredValue,
				OutEntries);
		}
		if (KeyContext.BodyKey == TEXT("NotifyTracks"))
		{
			bOutHandled = true;
			return DiffAnimSequenceNotifyTracksPilotRegion(
				*KeyContext.CurrentContext,
				KeyContext.CurrentValue,
				KeyContext.DesiredValue,
				OutEntries);
		}
		return FAssetDocumentCapabilityResult::Success(TEXT("Default diff"));
	};

	FAssetDocumentPreviewApplyDiffAdapter Adapter(MoveTemp(Hooks));
	return Adapter.DiffBody(Context, DesiredJson, OutDiffEntries);
```

Remove profile-local `MakeBodyObjectForDiff` from AnimSequence if it becomes unused. Do not remove `JsonValueToComparableString` or `AddBodyDiffEntry` if they are still used by `SortExtractedObjectFragmentsForStableDiff`, `DiffAnimSequenceObjectPilotRegion`, or `DiffAnimSequenceNotifyTracksPilotRegion`.

- [ ] **Step 5: Run focused validation**

```powershell
git diff --check
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/AVH1/AVH1.uproject' -NoHotReload
$report='C:/AVH1/Saved/AutomationReports/PreviewApplyDiffAnimSequence'
if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report -Recurse -Force }
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected: AnimSequence automation passes with no failures or warnings.

- [ ] **Step 6: Commit Task 3**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp
git commit -m "refactor: migrate anim sequence preview apply diff"
```

## Final Verification

- [ ] **Step 1: Confirm forbidden boundaries**

```powershell
rg -n "UAnimSequence|UAnimMontage|AnimSequence|AnimMontage|Notify|Section|Curve" Source/AssetDocument/Private/Regions/AssetDocumentPreviewApplyDiffAdapter.*
```

Expected: no hits except comments if any. Prefer no hits.

```powershell
rg -n "optional schema|ObjectRegionAdapter.*FieldSchema|FieldSchema.*ObjectRegionAdapter|FAssetDocumentObjectRegionAdapter.*Schema|Schema.*FAssetDocumentObjectRegionAdapter" Source/AssetDocument/Private/Regions Source/AssetDocument/Private/Profiles
```

Expected: no new adapter schema config. Existing `GetSchemaHint` is acceptable.

- [ ] **Step 2: Run full checks**

```powershell
git diff --check
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/AVH1/AVH1.uproject' -NoHotReload
$report='C:/AVH1/Saved/AutomationReports/PreviewApplyDiffFullAssetDocument'
if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report -Recurse -Force }
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected: full `AssetFactory.AssetDocument` automation passes with 0 failures and 0 warnings.

- [ ] **Step 3: Final review**

Use final review scope:

```powershell
git log --oneline 76b5566..HEAD
git diff --stat 76b5566..HEAD
```

Expected: plan commit plus implementation checkpoint commits only.

## Subagent Execution Notes

- Implement one task at a time.
- Do not run Task 2 before Task 1 is committed and reviewed.
- Do not run Task 3 before Task 2 is committed and reviewed.
- Task 1 owns only `AssetDocumentPreviewApplyDiffAdapter.*` and `AssetDocumentRegionRuntimeTests.cpp`.
- Task 2 owns only AnimMontage profile/tests.
- Task 3 owns only AnimSequence profile/tests.
- Reviewer prompts must include the task acceptance criteria, touched files, and `TASK_BASE..HEAD`; do not attach unrelated long thread history.
- If a reviewer finds issues, send the same implementer a fix request and re-review the same diff range.

## Self-Review

- Spec coverage: tasks cover public adapter, AnimMontage migration, AnimSequence migration, boundary checks, and full automation.
- Placeholder scan: no placeholders remain.
- Type consistency: hook signatures in tests, header, and profile migration snippets all use `FAssetDocumentPreviewApplyDiffBodyKeyContext`, `FAssetDocumentPreviewApplyDiffHooks`, and `FAssetDocumentPreviewApplyDiffAdapter::DiffBody`.
