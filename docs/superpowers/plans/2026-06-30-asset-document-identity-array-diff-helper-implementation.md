# AssetDocument Identity Array Diff Helper Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a reusable composition-based identity array diff helper and migrate small real AssetDocument diff slices to it without changing public JSON behavior.

**Architecture:** Add a private region-runtime helper that receives already materialized JSON elements plus hooks for equality/path/change decisions. The helper owns current/desired identity alignment and diff entry generation; profile code keeps asset semantics such as interface class loading, variable type/default comparison, and any future component alias handling. First migrate `ImplementedInterfaces` in both `UBlueprint` and `WidgetBlueprint`, then migrate `UBlueprint Variables` if Task 2 review confirms the helper boundary is clean.

**Tech Stack:** Unreal Engine 5.7 C++, `FJsonValue` / `FJsonObject`, AssetDocument private region runtime utilities, Unreal automation tests, UBT validation host `C:/AVH1`.

---

## SPEC_BASE

`SPEC_BASE=8f89b3e`

Spec: `docs/superpowers/specs/2026-06-30-asset-document-identity-array-diff-helper-design.md`

Current branch before implementation: `feature/asset-document-object-field-schema-dispatcher-migration`

After this plan is committed, record the plan commit as `IMPLEMENTATION_BASE`:

```powershell
git rev-parse HEAD
```

Implementation must create a new stacked branch from that plan commit:

```powershell
git switch -c feature/asset-document-identity-array-diff-helper
```

Do not implement directly on `feature/asset-document-object-field-schema-dispatcher-migration` after this plan commit. Record `TASK_BASE=HEAD` before each task. Final implementation review uses `IMPLEMENTATION_BASE..HEAD`; final spec-chain review may use `8f89b3e..HEAD` to include the spec terminology correction and this plan.

## File Map

- Create: `Source/AssetDocument/Private/Regions/AssetDocumentIdentityArrayDiffHelper.h`
  - Defines helper element/options/hooks/context structs and the static `Diff` entry point.
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentIdentityArrayDiffHelper.cpp`
  - Implements duplicate guard, default path construction, default equality, current-first then desired-only traversal, and diff entry construction with the existing `path` / `status` / optional `change` / `current` / `desired` shape.
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`
  - Adds helper-level tests for unchanged, changed, current-only, desired-only, custom path/equality/change, JSON pointer escaping, and duplicate identity failure.
- Modify: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp`
  - Migrates `ImplementedInterfaces` diff loops to helper.
  - Later migrates `Variables` diff loop to helper if Task 2 is approved.
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.cpp`
  - Migrates `ImplementedInterfaces` diff loops to helper.
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp`
  - Strengthens per-element path/status/change assertions for `ImplementedInterfaces` and `Variables`.
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp`
  - Adds or strengthens `ImplementedInterfaces` diff assertions.

Do not modify:

- `Source/AssetDocument/Private/Regions/AssetDocumentNamedArrayRegionAdapter.*` unless a compile-only include issue proves unavoidable.
- AnimSequence / AnimMontage profiles in this spec.
- Graph, fragment, timeline, binding, animation, or tree adapters.
- MCP code or public schema generation.

## Shared Commands

Use the current implementation worktree:

```powershell
cd E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-object-field-schema-dispatcher-migration
```

UBT validation:

```powershell
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/AVH1/AVH1.uproject' -NoHotReload
```

Report parse example for Task 1's green report:

```powershell
$p='C:/AVH1/Saved/AutomationReports/IdentityArrayDiffHelper/index.json'
$json = Get-Content -LiteralPath $p -Raw | ConvertFrom-Json
$tests = @($json.tests)
[pscustomobject]@{
  tests=$tests.Count
  success=@($tests | Where-Object { $_.state -eq 'Success' }).Count
  fail=@($tests | Where-Object { $_.state -eq 'Fail' }).Count
  skipped_or_notrun=@($tests | Where-Object { $_.state -eq 'Skipped' -or $_.state -eq 'NotRun' }).Count
  tests_with_warnings=@($tests | Where-Object { $_.warnings -and $_.warnings.Count -gt 0 }).Count
} | ConvertTo-Json -Compress
```

## Task 1: Public Helper And Runtime Tests

**TASK_BASE:** record with `git rev-parse HEAD` before editing.

**Files:**
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentIdentityArrayDiffHelper.h`
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentIdentityArrayDiffHelper.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`

- [ ] **Step 1: Write failing runtime tests**

Add `#include "Regions/AssetDocumentIdentityArrayDiffHelper.h"` near the other region includes in `AssetDocumentRegionRuntimeTests.cpp`.

Add helpers near existing test-local JSON helpers:

```cpp
TSharedPtr<FJsonValue> MakeIdentityValue(const FString& Name, const int32 Count)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("Name"), Name);
	Object->SetNumberField(TEXT("Count"), Count);
	return MakeShared<FJsonValueObject>(Object);
}

TSharedPtr<FJsonObject> FindDiffEntryByPath(
	const TArray<TSharedPtr<FJsonValue>>& Entries,
	const FString& Path)
{
	for (const TSharedPtr<FJsonValue>& EntryValue : Entries)
	{
		const TSharedPtr<FJsonObject> EntryObject = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
		if (EntryObject.IsValid() && EntryObject->GetStringField(TEXT("path")) == Path)
		{
			return EntryObject;
		}
	}
	return nullptr;
}
```

Add automation tests:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentIdentityArrayDiffHelperBasicTest,
	"AssetFactory.AssetDocument.RegionRuntime.IdentityArrayDiff.Basic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentIdentityArrayDiffHelperBasicTest::RunTest(const FString&)
{
	TArray<FAssetDocumentIdentityArrayDiffElement> Current;
	Current.Add({TEXT("Alpha"), TEXT("Alpha"), MakeIdentityValue(TEXT("Alpha"), 1)});
	Current.Add({TEXT("Beta"), TEXT("Beta"), MakeIdentityValue(TEXT("Beta"), 2)});
	Current.Add({TEXT("CurrentOnly"), TEXT("CurrentOnly"), MakeIdentityValue(TEXT("CurrentOnly"), 3)});

	TArray<FAssetDocumentIdentityArrayDiffElement> Desired;
	Desired.Add({TEXT("Alpha"), TEXT("Alpha"), MakeIdentityValue(TEXT("Alpha"), 1)});
	Desired.Add({TEXT("Beta"), TEXT("Beta"), MakeIdentityValue(TEXT("Beta"), 20)});
	Desired.Add({TEXT("DesiredOnly"), TEXT("DesiredOnly"), MakeIdentityValue(TEXT("DesiredOnly"), 4)});

	FAssetDocumentIdentityArrayDiffOptions Options;
	Options.RegionPath = TEXT("/Body/TestArray");

	TArray<TSharedPtr<FJsonValue>> Entries;
	const FAssetDocumentCapabilityResult Result =
		FAssetDocumentIdentityArrayDiffHelper::Diff(Options, Current, Desired, {}, Entries);

	TestTrue(TEXT("identity array diff succeeds"), Result.bSuccess);
	TestEqual(TEXT("diff emits current-first plus desired-only entries"), Entries.Num(), 4);

	const TSharedPtr<FJsonObject> Alpha = FindDiffEntryByPath(Entries, TEXT("/Body/TestArray/Alpha"));
	TestTrue(TEXT("Alpha entry exists"), Alpha.IsValid());
	if (Alpha.IsValid())
	{
		TestEqual(TEXT("Alpha unchanged"), Alpha->GetStringField(TEXT("status")), FString(TEXT("unchanged")));
		TestFalse(TEXT("Alpha has no change"), Alpha->HasField(TEXT("change")));
	}

	const TSharedPtr<FJsonObject> Beta = FindDiffEntryByPath(Entries, TEXT("/Body/TestArray/Beta"));
	TestTrue(TEXT("Beta entry exists"), Beta.IsValid());
	if (Beta.IsValid())
	{
		TestEqual(TEXT("Beta changed"), Beta->GetStringField(TEXT("status")), FString(TEXT("changed")));
		TestEqual(TEXT("Beta changed change"), Beta->GetStringField(TEXT("change")), FString(TEXT("changed")));
	}

	const TSharedPtr<FJsonObject> CurrentOnly = FindDiffEntryByPath(Entries, TEXT("/Body/TestArray/CurrentOnly"));
	TestTrue(TEXT("current-only entry exists"), CurrentOnly.IsValid());
	if (CurrentOnly.IsValid())
	{
		TestEqual(TEXT("current-only status"), CurrentOnly->GetStringField(TEXT("status")), FString(TEXT("changed")));
		TestEqual(TEXT("current-only change"), CurrentOnly->GetStringField(TEXT("change")), FString(TEXT("extra")));
		const TSharedPtr<FJsonValue> DesiredValue = CurrentOnly->TryGetField(TEXT("desired"));
		TestTrue(TEXT("current-only desired is null"), DesiredValue.IsValid() && DesiredValue->IsNull());
	}

	const TSharedPtr<FJsonObject> DesiredOnly = FindDiffEntryByPath(Entries, TEXT("/Body/TestArray/DesiredOnly"));
	TestTrue(TEXT("desired-only entry exists"), DesiredOnly.IsValid());
	if (DesiredOnly.IsValid())
	{
		TestEqual(TEXT("desired-only status"), DesiredOnly->GetStringField(TEXT("status")), FString(TEXT("changed")));
		TestEqual(TEXT("desired-only change"), DesiredOnly->GetStringField(TEXT("change")), FString(TEXT("missing")));
	}

	return true;
}
```

Also add a second test:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentIdentityArrayDiffHelperHooksAndDuplicateTest,
	"AssetFactory.AssetDocument.RegionRuntime.IdentityArrayDiff.HooksAndDuplicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentIdentityArrayDiffHelperHooksAndDuplicateTest::RunTest(const FString&)
{
	FAssetDocumentIdentityArrayDiffOptions Options;
	Options.RegionPath = TEXT("/Body/Interfaces");

	TArray<FAssetDocumentIdentityArrayDiffElement> Current;
	Current.Add({TEXT("/Script/Test.Interface"), TEXT("/Script/Test.Interface"), MakeIdentityValue(TEXT("Interface"), 1)});

	TArray<FAssetDocumentIdentityArrayDiffElement> Desired;
	Desired.Add({TEXT("/Script/Test.Interface"), TEXT("/Script/Test.Interface"), MakeIdentityValue(TEXT("Interface"), 99)});

	FAssetDocumentIdentityArrayDiffHooks Hooks;
	Hooks.AreElementsEqual = [](const FAssetDocumentIdentityArrayDiffEntryContext&)
	{
		return true;
	};
	Hooks.MakePath = [](const FAssetDocumentIdentityArrayDiffEntryContext& Entry)
	{
		return FString::Printf(TEXT("/Custom/%s"), *Entry.Identity);
	};
	Hooks.MakeChange = [](const FAssetDocumentIdentityArrayDiffEntryContext&)
	{
		return FString(TEXT("semantic"));
	};

	TArray<TSharedPtr<FJsonValue>> Entries;
	const FAssetDocumentCapabilityResult Result =
		FAssetDocumentIdentityArrayDiffHelper::Diff(Options, Current, Desired, Hooks, Entries);
	TestTrue(TEXT("custom hook diff succeeds"), Result.bSuccess);
	TestEqual(TEXT("custom hook emits one entry"), Entries.Num(), 1);
	const TSharedPtr<FJsonObject> Entry = Entries.Num() == 1 && Entries[0].IsValid() ? Entries[0]->AsObject() : nullptr;
	TestTrue(TEXT("custom hook entry object"), Entry.IsValid());
	if (Entry.IsValid())
	{
		TestEqual(TEXT("custom path used"), Entry->GetStringField(TEXT("path")), FString(TEXT("/Custom//Script/Test.Interface")));
		TestEqual(TEXT("custom equality marks unchanged"), Entry->GetStringField(TEXT("status")), FString(TEXT("unchanged")));
	}

	Current.Add({TEXT("/Script/Test.Interface"), TEXT("Duplicate"), MakeIdentityValue(TEXT("Duplicate"), 2)});
	TArray<TSharedPtr<FJsonValue>> DuplicateEntries;
	const FAssetDocumentCapabilityResult DuplicateResult =
		FAssetDocumentIdentityArrayDiffHelper::Diff(Options, Current, Desired, Hooks, DuplicateEntries);
	TestFalse(TEXT("duplicate identity fails"), DuplicateResult.bSuccess);
	TestTrue(TEXT("duplicate diagnostic path is region path"),
		DuplicateResult.Diagnostics.Num() > 0 && DuplicateResult.Diagnostics[0].Path == TEXT("/Body/Interfaces"));
	TestTrue(TEXT("duplicate diagnostic code is stable"),
		DuplicateResult.Diagnostics.Num() > 0 && DuplicateResult.Diagnostics[0].Code == TEXT("DuplicateIdentityArrayDiffIdentity"));

	return true;
}
```

- [ ] **Step 2: Run failing tests**

Run:

```powershell
$report='C:/AVH1/Saved/AutomationReports/IdentityArrayDiffHelperRed'
if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report -Recurse -Force }
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.RegionRuntime.IdentityArrayDiff;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected: compile or test discovery fails because `AssetDocumentIdentityArrayDiffHelper.h` does not exist.

- [ ] **Step 3: Implement helper**

Create `AssetDocumentIdentityArrayDiffHelper.h` with this public shape:

```cpp
#pragma once

#include "AssetDocumentTypes.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

struct FAssetDocumentIdentityArrayDiffElement
{
	FString Identity;
	FString PathToken;
	TSharedPtr<FJsonValue> Value;
};

struct FAssetDocumentIdentityArrayDiffEntryContext
{
	FString Identity;
	FString Path;
	TSharedPtr<FJsonValue> CurrentValue;
	TSharedPtr<FJsonValue> DesiredValue;
	bool bHasCurrent = false;
	bool bHasDesired = false;
};

struct FAssetDocumentIdentityArrayDiffOptions
{
	FString RegionPath;
	FString ExtraChange = TEXT("extra");
	FString MissingChange = TEXT("missing");
	FString ChangedChange = TEXT("changed");
	bool bEmitUnchanged = true;
};

struct FAssetDocumentIdentityArrayDiffHooks
{
	TFunction<bool(const FAssetDocumentIdentityArrayDiffEntryContext&)> AreElementsEqual;
	TFunction<FString(const FAssetDocumentIdentityArrayDiffEntryContext&)> MakePath;
	TFunction<FString(const FAssetDocumentIdentityArrayDiffEntryContext&)> MakeChange;
};

class FAssetDocumentIdentityArrayDiffHelper
{
public:
	static FAssetDocumentCapabilityResult Diff(
		const FAssetDocumentIdentityArrayDiffOptions& Options,
		const TArray<FAssetDocumentIdentityArrayDiffElement>& CurrentElements,
		const TArray<FAssetDocumentIdentityArrayDiffElement>& DesiredElements,
		const FAssetDocumentIdentityArrayDiffHooks& Hooks,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries);
};
```

Create `AssetDocumentIdentityArrayDiffHelper.cpp` with these implementation rules:

```cpp
#include "Regions/AssetDocumentIdentityArrayDiffHelper.h"

#include "AssetDocumentJsonRegionUtils.h"

namespace
{
FString MakeDefaultPath(const FAssetDocumentIdentityArrayDiffOptions& Options, const FString& PathToken)
{
	return FString::Printf(
		TEXT("%s/%s"),
		*Options.RegionPath,
		*FAssetDocumentJsonRegionUtils::EscapeJsonPointerToken(PathToken));
}

FAssetDocumentCapabilityResult ValidateUniqueIdentities(
	const FAssetDocumentIdentityArrayDiffOptions& Options,
	const TArray<FAssetDocumentIdentityArrayDiffElement>& Elements,
	const TCHAR* Side)
{
	TSet<FString> Seen;
	for (const FAssetDocumentIdentityArrayDiffElement& Element : Elements)
	{
		if (Element.Identity.IsEmpty())
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				Options.RegionPath,
				TEXT("MissingIdentityArrayDiffIdentity"),
				FString::Printf(TEXT("Identity array diff %s element is missing identity"), Side));
		}
		if (Seen.Contains(Element.Identity))
		{
			return FAssetDocumentJsonRegionUtils::Failure(
				Options.RegionPath,
				TEXT("DuplicateIdentityArrayDiffIdentity"),
				FString::Printf(TEXT("Duplicate identity array diff %s identity %s"), Side, *Element.Identity));
		}
		Seen.Add(Element.Identity);
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Identity array diff identities are unique"));
}

TSharedPtr<FJsonValue> NullValue()
{
	return MakeShared<FJsonValueNull>();
}

void AddIdentityDiffEntry(
	TArray<TSharedPtr<FJsonValue>>& Entries,
	const FString& Path,
	const FString& Status,
	const TSharedPtr<FJsonValue>& Current,
	const TSharedPtr<FJsonValue>& Desired,
	const FString& Change)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("path"), Path);
	Entry->SetStringField(TEXT("status"), Status);
	if (!Change.IsEmpty())
	{
		Entry->SetStringField(TEXT("change"), Change);
	}
	Entry->SetField(TEXT("current"), Current.IsValid() ? Current : MakeShared<FJsonValueNull>());
	Entry->SetField(TEXT("desired"), Desired.IsValid() ? Desired : MakeShared<FJsonValueNull>());
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
}
}

FAssetDocumentCapabilityResult FAssetDocumentIdentityArrayDiffHelper::Diff(
	const FAssetDocumentIdentityArrayDiffOptions& Options,
	const TArray<FAssetDocumentIdentityArrayDiffElement>& CurrentElements,
	const TArray<FAssetDocumentIdentityArrayDiffElement>& DesiredElements,
	const FAssetDocumentIdentityArrayDiffHooks& Hooks,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
{
	if (Options.RegionPath.IsEmpty())
	{
		return FAssetDocumentJsonRegionUtils::Failure(
			TEXT("/Body"),
			TEXT("MissingIdentityArrayDiffRegionPath"),
			TEXT("Identity array diff requires a region path"));
	}

	FAssetDocumentCapabilityResult Result = ValidateUniqueIdentities(Options, CurrentElements, TEXT("current"));
	if (!Result.bSuccess)
	{
		return Result;
	}
	Result = ValidateUniqueIdentities(Options, DesiredElements, TEXT("desired"));
	if (!Result.bSuccess)
	{
		return Result;
	}

	TMap<FString, const FAssetDocumentIdentityArrayDiffElement*> DesiredByIdentity;
	for (const FAssetDocumentIdentityArrayDiffElement& Desired : DesiredElements)
	{
		DesiredByIdentity.Add(Desired.Identity, &Desired);
	}

	TSet<FString> SeenDesired;
	auto EmitEntry = [&](const FAssetDocumentIdentityArrayDiffElement* Current, const FAssetDocumentIdentityArrayDiffElement* Desired)
	{
		FAssetDocumentIdentityArrayDiffEntryContext Entry;
		Entry.Identity = Current ? Current->Identity : Desired->Identity;
		const FString PathToken = Current ? Current->PathToken : Desired->PathToken;
		Entry.CurrentValue = Current ? Current->Value : NullValue();
		Entry.DesiredValue = Desired ? Desired->Value : NullValue();
		Entry.bHasCurrent = Current != nullptr;
		Entry.bHasDesired = Desired != nullptr;
		Entry.Path = Hooks.MakePath ? Hooks.MakePath(Entry) : MakeDefaultPath(Options, PathToken);

		const bool bEqual = Entry.bHasCurrent && Entry.bHasDesired
			? (Hooks.AreElementsEqual
				? Hooks.AreElementsEqual(Entry)
				: FAssetDocumentJsonRegionUtils::JsonValueToComparableString(Entry.CurrentValue) ==
					FAssetDocumentJsonRegionUtils::JsonValueToComparableString(Entry.DesiredValue))
			: false;

		if (bEqual && !Options.bEmitUnchanged)
		{
			return;
		}

		const FString Status = bEqual ? TEXT("unchanged") : TEXT("changed");
		FString change;
		if (!bEqual)
		{
			if (!Entry.bHasDesired)
			{
				change = Options.ExtraChange;
			}
			else if (!Entry.bHasCurrent)
			{
				change = Options.MissingChange;
			}
			else
			{
				change = Hooks.MakeChange ? Hooks.MakeChange(Entry) : Options.ChangedChange;
			}
		}

		AddIdentityDiffEntry(
			OutDiffEntries,
			Entry.Path,
			Status,
			Entry.CurrentValue,
			Entry.DesiredValue,
			change);
	};

	for (const FAssetDocumentIdentityArrayDiffElement& Current : CurrentElements)
	{
		const FAssetDocumentIdentityArrayDiffElement* const* Desired = DesiredByIdentity.Find(Current.Identity);
		if (Desired && *Desired)
		{
			SeenDesired.Add(Current.Identity);
			EmitEntry(&Current, *Desired);
		}
		else
		{
			EmitEntry(&Current, nullptr);
		}
	}

	for (const FAssetDocumentIdentityArrayDiffElement& Desired : DesiredElements)
	{
		if (!SeenDesired.Contains(Desired.Identity))
		{
			EmitEntry(nullptr, &Desired);
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Identity array diffed"));
}
```

Important fix while coding: compute hook path after `Entry.Identity`, `CurrentValue`, `DesiredValue`, `bHasCurrent`, and `bHasDesired` are populated if path hooks need those fields. If following the snippet literally, move `Entry.Path = ...` after the remaining entry fields are assigned.

- [ ] **Step 4: Run focused helper tests**

Run UBT:

```powershell
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/AVH1/AVH1.uproject' -NoHotReload
```

Run:

```powershell
$report='C:/AVH1/Saved/AutomationReports/IdentityArrayDiffHelper'
if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report -Recurse -Force }
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.RegionRuntime.IdentityArrayDiff;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected: all `IdentityArrayDiff` tests pass. Parse report and include counts in commit note.

- [ ] **Step 5: Boundary scan and commit**

Run:

```powershell
rg -n "UBlueprint|UWidgetBlueprint|UAnimSequence|AnimSequence|AnimMontage|Variables|ImplementedInterfaces|Components" Source/AssetDocument/Private/Regions/AssetDocumentIdentityArrayDiffHelper.h Source/AssetDocument/Private/Regions/AssetDocumentIdentityArrayDiffHelper.cpp
git diff --check
git status --short
```

Expected: boundary scan has no output; `git diff --check` passes.

Commit:

```powershell
git add Source/AssetDocument/Private/Regions/AssetDocumentIdentityArrayDiffHelper.h Source/AssetDocument/Private/Regions/AssetDocumentIdentityArrayDiffHelper.cpp Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp
git commit -m "feat: add asset document identity array diff helper"
```

Then request spec and code-quality review over `TASK_BASE..HEAD`.

## Task 2: Migrate ImplementedInterfaces In UBlueprint And WidgetBlueprint

**TASK_BASE:** record with `git rev-parse HEAD` after Task 1 review approval.

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp`

- [ ] **Step 1: Add focused profile tests before migration**

In `AssetDocumentUBlueprintTests.cpp`, strengthen the existing interface diff coverage around the current `/Body/ImplementedInterfaces//Script/Engine.ActorSoundParameterInterface` assertion. Ensure it checks status and change:

```cpp
TSharedPtr<FJsonObject> InterfaceDiff = FindDiffEntryByPath(
	InterfaceDiffEntries,
	TEXT("/Body/ImplementedInterfaces//Script/Engine.ActorSoundParameterInterface"));
TestTrue(TEXT("ImplementedInterfaces diff uses semantic interface path"), InterfaceDiff.IsValid());
if (InterfaceDiff.IsValid())
{
	TestEqual(TEXT("ImplementedInterfaces missing interface is changed"), InterfaceDiff->GetStringField(TEXT("status")), FString(TEXT("changed")));
	TestEqual(TEXT("ImplementedInterfaces missing interface keeps missing change"), InterfaceDiff->GetStringField(TEXT("change")), FString(TEXT("missing")));
}
```

If the existing UBlueprint implementation currently does not expose `change` for interface desired-only entries, record that as baseline and assert only path/status in the red step; Task 2 implementation must not introduce a new behavior change unless spec review explicitly accepts it.

In `AssetDocumentWidgetBlueprintTests.cpp`, add a diff test near the existing `ImplementedInterfaces.RoundTrip` tests:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintImplementedInterfacesDiffTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.ImplementedInterfaces.Diff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintImplementedInterfacesDiffTest::RunTest(const FString&)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/WBP_AD_InterfaceDiff_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString InterfacePath = TEXT("/Script/Engine.ActorSoundParameterInterface");
	TSharedRef<FJsonObject> Body = MakeWidgetBlueprintBody();
	SetImplementedInterfaces(Body, {MakeImplementedInterface(InterfacePath)});

	FAssetDocumentService Service;
	FAssetDocumentApplyRequest ApplyRequest;
	ApplyRequest.Document = MakeWidgetBlueprintDocument(Target, Body);
	ApplyRequest.bSaveAsset = false;
	TestTrue(TEXT("initial interface apply succeeds"), Service.Apply(ApplyRequest).IsSuccess());

	TSharedRef<FJsonObject> EmptyBody = MakeWidgetBlueprintBody();
	SetImplementedInterfaces(EmptyBody, {});
	FAssetDocumentDiffRequest DiffRequest;
	DiffRequest.AssetPath = Target;
	DiffRequest.Document = MakeWidgetBlueprintDocument(Target, EmptyBody);
	const FAssetDocumentResult DiffResult = Service.Diff(DiffRequest);
	TestTrue(TEXT("interface diff succeeds"), DiffResult.IsSuccess());

	const TArray<TSharedPtr<FJsonValue>>* Changed = nullptr;
	TestTrue(TEXT("diff payload includes changed"), DiffResult.Payload->TryGetArrayField(TEXT("changed"), Changed));
	TestTrue(TEXT("changed includes current-only interface semantic path"),
		Changed && JsonArrayContainsPath(*Changed, TEXT("/Body/ImplementedInterfaces//Script/Engine.ActorSoundParameterInterface")));

	return true;
}
```

Use the existing WidgetBlueprint test-local helpers from the same file: `MakeWidgetBlueprintDocument`, `MakeWidgetBlueprintBody`, `MakeImplementedInterface`, `SetImplementedInterfaces`, and `JsonArrayContainsPath`.

- [ ] **Step 2: Run focused tests to capture baseline**

Run:

```powershell
$report='C:/AVH1/Saved/AutomationReports/IdentityArrayDiffInterfacesBaseline'
if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report -Recurse -Force }
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint;Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint.ImplementedInterfaces;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected: tests pass before migration. If a newly added change-field assertion fails, inspect the old `WidgetBlueprint` `AddBodyDiffEntry` implementation at `WidgetBlueprintAssetDocumentCapability.cpp:2209`; it has no `change` field, so the correct Task 2 behavior is to preserve no `change` field for WidgetBlueprint implemented-interface entries.

- [ ] **Step 3: Migrate UBlueprint ImplementedInterfaces**

Add include:

```cpp
#include "Regions/AssetDocumentIdentityArrayDiffHelper.h"
```

Replace the `DesiredByPath` / `SeenCurrentInterfaces` loops in `FBlueprintAssetDocumentCapability::Diff` for `ImplementedInterfaces` with helper-backed logic:

```cpp
TArray<FAssetDocumentIdentityArrayDiffElement> CurrentInterfaces;
for (const FBPInterfaceDescription& CurrentInterface : Blueprint->ImplementedInterfaces)
{
	if (!CurrentInterface.Interface)
	{
		continue;
	}
	const FString CurrentPath = GetClassPath(CurrentInterface.Interface);
	CurrentInterfaces.Add({
		CurrentPath,
		CurrentPath,
		MakeInterfaceDiffValue(CurrentInterface.Interface)
	});
}

TArray<FAssetDocumentIdentityArrayDiffElement> DesiredInterfaceElements;
for (const FUBlueprintInterfaceSpec& DesiredInterface : DesiredInterfaces)
{
	const FString DesiredPath = GetClassPath(DesiredInterface.InterfaceClass);
	DesiredInterfaceElements.Add({
		DesiredPath,
		DesiredPath,
		MakeShared<FJsonValueObject>(InterfaceToJsonObject(DesiredInterface.InterfaceClass))
	});
}

FAssetDocumentIdentityArrayDiffOptions InterfaceDiffOptions;
InterfaceDiffOptions.RegionPath = TEXT("/Body/ImplementedInterfaces");

FAssetDocumentIdentityArrayDiffHooks InterfaceDiffHooks;
InterfaceDiffHooks.AreElementsEqual = [](const FAssetDocumentIdentityArrayDiffEntryContext&)
{
	return true;
};

const FAssetDocumentCapabilityResult InterfaceDiffResult =
	FAssetDocumentIdentityArrayDiffHelper::Diff(
		InterfaceDiffOptions,
		CurrentInterfaces,
		DesiredInterfaceElements,
		InterfaceDiffHooks,
		OutDiffEntries);
if (!InterfaceDiffResult.bSuccess)
{
	return InterfaceDiffResult;
}
```

Rationale: elements with the same interface class path are semantically equal. Current-only and desired-only still emit changed entries with null opposite values.

If old `UBlueprint` used `MakeInterfaceDiffValue(*DesiredInterface)` instead of `InterfaceToJsonObject`, choose the value shape that preserves existing tests. Do not change public JSON shape.

- [ ] **Step 4: Migrate WidgetBlueprint ImplementedInterfaces**

Add include:

```cpp
#include "Regions/AssetDocumentIdentityArrayDiffHelper.h"
```

Replace the WidgetBlueprint `ImplementedInterfaces` `DesiredByPath` / `SeenCurrentInterfaces` loops with the same helper pattern, using `FWidgetBlueprintInterfaceSpec` and `WidgetBlueprint->ImplementedInterfaces`.

Preserve WidgetBlueprint’s current change behavior. If old entries had no `change` argument, set helper options to empty changes and use the helper only if `FAssetDocumentJsonRegionUtils::AddDiffEntry` omits empty change:

```cpp
FAssetDocumentIdentityArrayDiffOptions InterfaceDiffOptions;
InterfaceDiffOptions.RegionPath = TEXT("/Body/ImplementedInterfaces");
InterfaceDiffOptions.ExtraChange.Reset();
InterfaceDiffOptions.MissingChange.Reset();
InterfaceDiffOptions.ChangedChange.Reset();
```

If tests show WidgetBlueprint already accepts `extra` / `missing`, keep defaults.

- [ ] **Step 5: Run focused profile automation**

Run UBT, then:

```powershell
$report='C:/AVH1/Saved/AutomationReports/IdentityArrayDiffInterfaces'
if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report -Recurse -Force }
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint;Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint.ImplementedInterfaces;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected: UBlueprint and WidgetBlueprint implemented-interface tests pass.

- [ ] **Step 6: Boundary scan and commit**

Run:

```powershell
rg -n "UBlueprint|UWidgetBlueprint|UAnimSequence|AnimSequence|AnimMontage|Variables|ImplementedInterfaces|Components" Source/AssetDocument/Private/Regions/AssetDocumentIdentityArrayDiffHelper.h Source/AssetDocument/Private/Regions/AssetDocumentIdentityArrayDiffHelper.cpp
rg -n "DesiredByPath|SeenCurrentInterfaces" Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.cpp
git diff --check
```

Expected: helper boundary scan has no output; old interface diff loop names no longer exist or no longer apply to `ImplementedInterfaces`.

Commit:

```powershell
git add Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp
git commit -m "refactor: migrate implemented interface identity diff"
```

Then request spec and code-quality review over `TASK_BASE..HEAD`.

## Task 3: Migrate UBlueprint Variables

**TASK_BASE:** record with `git rev-parse HEAD` after Task 2 review approval.

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp`

- [ ] **Step 1: Strengthen UBlueprint variable diff tests**

Near existing tests that inspect `/Body/Variables/Health`, ensure assertions cover:

```cpp
TSharedPtr<FJsonObject> HealthDiff = FindDiffEntryByPath(DiffEntries, TEXT("/Body/Variables/Health"));
TestTrue(TEXT("Variables diff uses semantic variable path"), HealthDiff.IsValid());
if (HealthDiff.IsValid())
{
	TestEqual(TEXT("Health variable diff is changed"), HealthDiff->GetStringField(TEXT("status")), FString(TEXT("changed")));
	TestTrue(TEXT("Health variable diff has current"), HealthDiff->HasField(TEXT("current")));
	TestTrue(TEXT("Health variable diff has desired"), HealthDiff->HasField(TEXT("desired")));
}
```

Add a desired-only variable assertion if not already present:

```cpp
TSharedPtr<FJsonObject> NewVariableDiff = FindDiffEntryByPath(DiffEntries, TEXT("/Body/Variables/NewScore"));
TestTrue(TEXT("Desired-only variable uses semantic path"), NewVariableDiff.IsValid());
if (NewVariableDiff.IsValid())
{
	TestEqual(TEXT("Desired-only variable is changed"), NewVariableDiff->GetStringField(TEXT("status")), FString(TEXT("changed")));
	TestEqual(TEXT("Desired-only variable keeps missing change"), NewVariableDiff->GetStringField(TEXT("change")), FString(TEXT("missing")));
}
```

Use existing test setup helpers to create `NewScore`; do not introduce a new broad fixture.

- [ ] **Step 2: Run UBlueprint baseline**

Run:

```powershell
$report='C:/AVH1/Saved/AutomationReports/IdentityArrayDiffVariablesBaseline'
if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report -Recurse -Force }
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected: baseline passes with strengthened assertions.

- [ ] **Step 3: Migrate variable diff loop**

Replace the `DesiredByName` / `SeenCurrent` loops for `Variables` in `FBlueprintAssetDocumentCapability::Diff`.

Build current elements:

```cpp
TArray<FAssetDocumentIdentityArrayDiffElement> CurrentVariables;
for (const FBPVariableDescription& CurrentVariable : Blueprint->NewVariables)
{
	CurrentVariables.Add({
		CurrentVariable.VarName.ToString(),
		CurrentVariable.VarName.ToString(),
		MakeVariableDiffValue(Blueprint, CurrentVariable)
	});
}
```

Build desired elements:

```cpp
TMap<FString, FUBlueprintVariableSpec> DesiredVariableSpecsByName;
TArray<FAssetDocumentIdentityArrayDiffElement> DesiredVariableElements;
for (const FUBlueprintVariableSpec& DesiredVariable : DesiredVariables)
{
	const FString VariableName = DesiredVariable.Name.ToString();
	DesiredVariableSpecsByName.Add(VariableName, DesiredVariable);
	DesiredVariableElements.Add({
		VariableName,
		VariableName,
		MakeShared<FJsonValueObject>(VariableSpecToJsonObject(DesiredVariable))
	});
}
```

Use equality hook that preserves existing semantic compare:

```cpp
FAssetDocumentIdentityArrayDiffOptions VariableDiffOptions;
VariableDiffOptions.RegionPath = TEXT("/Body/Variables");

FAssetDocumentIdentityArrayDiffHooks VariableDiffHooks;
VariableDiffHooks.AreElementsEqual =
	[Blueprint, &DesiredVariableSpecsByName](const FAssetDocumentIdentityArrayDiffEntryContext& Entry)
{
	if (!Entry.bHasCurrent || !Entry.bHasDesired)
	{
		return false;
	}

	const FBPVariableDescription* CurrentVariable = Blueprint->NewVariables.FindByPredicate(
		[&Entry](const FBPVariableDescription& Candidate)
		{
			return Candidate.VarName.ToString() == Entry.Identity;
		});
	const FUBlueprintVariableSpec* DesiredVariable = DesiredVariableSpecsByName.Find(Entry.Identity);
	if (!CurrentVariable || !DesiredVariable)
	{
		return false;
	}

	const bool bTypeChanged = AuthoredPinTypesDiffer(CurrentVariable->VarType, DesiredVariable->Type);
	const FString CurrentDefaultValue = ResolveVariableDefaultValue(Blueprint, *CurrentVariable);
	const bool bDefaultChanged = AuthoredDefaultValuesDiffer(CurrentVariable->VarType, CurrentDefaultValue, DesiredVariable->DefaultValue);
	const FString DesiredCategory = DesiredVariable->Category.IsSet() ? DesiredVariable->Category.GetValue() : FString();
	const bool bCategoryChanged = CurrentVariable->Category.ToString() != DesiredCategory;
	const FString CurrentTooltip = CurrentVariable->HasMetaData(FBlueprintMetadata::MD_Tooltip)
		? CurrentVariable->GetMetaData(FBlueprintMetadata::MD_Tooltip)
		: FString();
	const FString DesiredTooltip = DesiredVariable->Tooltip.IsSet() ? DesiredVariable->Tooltip.GetValue() : FString();
	const bool bTooltipChanged = CurrentTooltip != DesiredTooltip;
	return !(bTypeChanged || bDefaultChanged || bCategoryChanged || bTooltipChanged);
};
```

Call helper:

```cpp
const FAssetDocumentCapabilityResult VariableDiffResult =
	FAssetDocumentIdentityArrayDiffHelper::Diff(
		VariableDiffOptions,
		CurrentVariables,
		DesiredVariableElements,
		VariableDiffHooks,
		OutDiffEntries);
if (!VariableDiffResult.bSuccess)
{
	return VariableDiffResult;
}
```

Do not change variable parsing, applying, extraction, type handling, default handling, category handling, or tooltip handling.

- [ ] **Step 4: Run focused UBlueprint automation**

Run UBT, then:

```powershell
$report='C:/AVH1/Saved/AutomationReports/IdentityArrayDiffVariables'
if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report -Recurse -Force }
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected: all UBlueprint tests pass.

- [ ] **Step 5: Commit**

Run:

```powershell
git diff --check
rg -n "DesiredByName|SeenCurrent" Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp
```

Expected: `DesiredByName` / `SeenCurrent` no longer exist in the variables diff block. They may still exist elsewhere only if unrelated.

Commit:

```powershell
git add Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp
git commit -m "refactor: migrate ublueprint variable identity diff"
```

Then request spec and code-quality review over `TASK_BASE..HEAD`.

## Final Verification And Review

After all approved tasks:

- [ ] **Boundary scan**

```powershell
rg -n "UBlueprint|UWidgetBlueprint|UAnimSequence|AnimSequence|AnimMontage|Variables|ImplementedInterfaces|Components" Source/AssetDocument/Private/Regions/AssetDocumentIdentityArrayDiffHelper.h Source/AssetDocument/Private/Regions/AssetDocumentIdentityArrayDiffHelper.cpp
rg -n "DesiredByPath|SeenCurrentInterfaces" Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.cpp
git diff --check 8f89b3e..HEAD
```

- [ ] **UBT**

```powershell
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/AVH1/AVH1.uproject' -NoHotReload
```

- [ ] **Full automation**

```powershell
$report='C:/AVH1/Saved/AutomationReports/IdentityArrayDiffFullAssetDocument'
if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report -Recurse -Force }
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

- [ ] **Final review**

Dispatch final read-only reviewer with:

- worktree: `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-object-field-schema-dispatcher-migration`
- branch: `feature/asset-document-identity-array-diff-helper`
- spec: `docs/superpowers/specs/2026-06-30-asset-document-identity-array-diff-helper-design.md`
- plan: `docs/superpowers/plans/2026-06-30-asset-document-identity-array-diff-helper-implementation.md`
- implementation diff range: `IMPLEMENTATION_BASE..HEAD`
- spec-chain diff range: `8f89b3e..HEAD`
- focus: helper boundary, no asset-specific logic, no inheritance, `ImplementedInterfaces` shared usage, variable semantic compare preserved, path/status/change/value compatibility, test adequacy.

Do not merge this branch back to `feature/asset-document-object-field-schema-dispatcher-migration` until final review is approved and the user confirms merge.
