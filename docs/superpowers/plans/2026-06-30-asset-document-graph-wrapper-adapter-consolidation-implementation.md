# AssetDocument Graph Wrapper Adapter Consolidation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract a composable graph region lifecycle wrapper and migrate WidgetBlueprint/UBlueprint graph lifecycle wiring to share it without changing graph materialization behavior.

**Architecture:** Add `FAssetDocumentGraphRegionWrapperAdapter` as a private region-runtime adapter configured with hooks. The adapter owns only synthetic body-region lifecycle plumbing; profile graph adapters still own graph parsing, node/pin identity, diff paths, K2 materialization, staged validation, compile, and repair.

**Tech Stack:** Unreal Engine 5.7 C++, AssetDocument profile/runtime code, UE automation tests, UBT.

---

## Reference Documents

- Spec: `docs/superpowers/specs/2026-06-30-asset-document-graph-wrapper-adapter-consolidation-design.md`
- Refactor chain: `docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md`
- Public region runtime design: `docs/superpowers/specs/2026-06-24-asset-document-public-region-runtime-design.md`

## Worktree And Branch

Create an implementation worktree from:

- Base branch: `feature/asset-document-object-field-schema-dispatcher-migration`
- Base commit after plan approval: record with `SPEC_BASE=$(git rev-parse HEAD)`
- New branch: `feature/asset-document-graph-wrapper-adapter-consolidation`
- Suggested worktree: `E:\GameDev\PluginsWarehouse\.worktrees\UECopilot\asset-document-graph-wrapper-adapter-consolidation`

Before each task:

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

After each task passes its verification:

```powershell
git add <task files>
git commit -m "<task commit message>"
```

Do not include unrelated run artifacts. If generated validation reports are kept, put them under an ignored location.

## File Map

Create:

- `Source/AssetDocument/Private/Regions/AssetDocumentGraphRegionWrapperAdapter.h`
  - Declares `FAssetDocumentGraphRegionWrapperConfig`, `FAssetDocumentGraphRegionWrapperHooks`, and `FAssetDocumentGraphRegionWrapperAdapter`.
- `Source/AssetDocument/Private/Regions/AssetDocumentGraphRegionWrapperAdapter.cpp`
  - Implements `IAssetDocumentRegionAdapter` lifecycle methods through configured hooks.

Modify:

- `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`
  - Adds fake-hook automation tests for the generic graph wrapper.
- `Source/AssetDocument/Private/Regions/AssetDocumentWidgetBlueprintRegionWrappers.cpp`
  - Replaces `FWidgetBlueprintGraphRegionAdapter` lifecycle implementation with delegation to the generic wrapper.
- `Source/AssetDocument/Private/Regions/AssetDocumentWidgetBlueprintRegionWrappers.h`
  - Keeps the public `FWidgetBlueprintGraphRegionAdapter` class name, constructors, and methods stable; private members may change only as needed.
- `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp`
  - Adds UBlueprint synthetic graph runtime binding and routes validate/apply/extract/diff through the generic wrapper while preserving staged validation.
- `docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md`
  - Marks ring 5 as implemented after code and verification.

Review-only context:

- `Source/AssetDocument/Private/Profiles/UBlueprintGraphRegionAdapter.h`
- `Source/AssetDocument/Private/Profiles/UBlueprintGraphRegionAdapter.cpp`
- `Source/AssetDocument/Private/Profiles/WidgetBlueprintGraphAdapter.h`
- `Source/AssetDocument/Private/Profiles/WidgetBlueprintGraphAdapter.cpp`
- `Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp`
- `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp`

### Task 1: Common Graph Wrapper Adapter

**Files:**

- Create: `Source/AssetDocument/Private/Regions/AssetDocumentGraphRegionWrapperAdapter.h`
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentGraphRegionWrapperAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`

- [ ] **Step 1: Record task base**

Run:

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: clean working tree on `feature/asset-document-graph-wrapper-adapter-consolidation`.

- [ ] **Step 2: Write failing tests for wrapper support and lifecycle hooks**

Add this include near the other region runtime includes in `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`:

```cpp
#include "Regions/AssetDocumentGraphRegionWrapperAdapter.h"
```

Append tests after the preview-apply-diff tests or near other region runtime adapter tests. Use exact automation names:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeGraphWrapperSupportsSyntheticBodyTest,
	"AssetFactory.AssetDocument.RegionRuntime.GraphWrapper.SupportsSyntheticBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeGraphWrapperSupportsSyntheticBodyTest::RunTest(const FString&)
{
	FAssetDocumentGraphRegionWrapperConfig Config;
	Config.AdapterName = TEXT("TestGraphWrapper");
	Config.RegionId = TEXT("Body.TestGraphRegions");
	Config.BodyPath = TEXT("Body.TestGraphRegions");
	Config.JsonPointer = TEXT("/Body");
	FAssetDocumentGraphRegionWrapperHooks Hooks;
	FAssetDocumentGraphRegionWrapperAdapter Adapter(MoveTemp(Config), MoveTemp(Hooks));

	FAssetDocumentRegionContext Context;
	Context.RegionId = TEXT("Body.TestGraphRegions");
	Context.BodyPath = TEXT("Body.TestGraphRegions");
	Context.JsonPointer = TEXT("/Body");
	TestTrue(TEXT("Region id is supported"), Adapter.SupportsRegion(Context));

	Context.RegionId = TEXT("Different.Region");
	TestTrue(TEXT("Body path is supported"), Adapter.SupportsRegion(Context));

	Context.JsonPointer = TEXT("/Body/UbergraphPages");
	TestFalse(TEXT("Non-body pointer is rejected"), Adapter.SupportsRegion(Context));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeGraphWrapperDispatchesHooksTest,
	"AssetFactory.AssetDocument.RegionRuntime.GraphWrapper.DispatchesHooks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeGraphWrapperDispatchesHooksTest::RunTest(const FString&)
{
	int32 ValidateCalls = 0;
	int32 PreflightCalls = 0;
	int32 ApplyCalls = 0;
	int32 ExtractCalls = 0;
	int32 DiffCalls = 0;

	FAssetDocumentGraphRegionWrapperConfig Config;
	Config.AdapterName = TEXT("TestGraphWrapper");
	Config.RegionId = TEXT("Body.TestGraphRegions");
	Config.BodyPath = TEXT("Body.TestGraphRegions");
	Config.JsonPointer = TEXT("/Body");

	FAssetDocumentGraphRegionWrapperHooks Hooks;
	Hooks.Validate = [&ValidateCalls](const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonObject>& BodyObject)
	{
		++ValidateCalls;
		return BodyObject->HasField(TEXT("Graphs"))
			? FAssetDocumentCapabilityResult::Success(TEXT("validated"))
			: FAssetDocumentCapabilityResult::Failure(TEXT("missing graphs"), TEXT("/Body/Graphs"), TEXT("MissingGraphs"));
	};
	Hooks.Preflight = [&PreflightCalls](FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonObject>&)
	{
		++PreflightCalls;
		Context.bIsDryRun = true;
		return FAssetDocumentCapabilityResult::Success(TEXT("preflighted"));
	};
	Hooks.Apply = [&ApplyCalls](FAssetDocumentCapabilityContext&, const TSharedRef<FJsonObject>&, bool& bOutChanged)
	{
		++ApplyCalls;
		bOutChanged = true;
		return FAssetDocumentCapabilityResult::Success(TEXT("applied"));
	};
	Hooks.Extract = [&ExtractCalls](const FAssetDocumentCapabilityContext&, TSharedRef<FJsonObject>& OutBodyObject)
	{
		++ExtractCalls;
		OutBodyObject->SetStringField(TEXT("Graphs"), TEXT("current"));
		return FAssetDocumentCapabilityResult::Success(TEXT("extracted"));
	};
	Hooks.Diff = [&DiffCalls](const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonObject>&, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
	{
		++DiffCalls;
		FAssetDocumentJsonRegionUtils::AddDiffEntry(OutDiffEntries, TEXT("/Body/Graphs"), TEXT("changed"), nullptr, nullptr);
		return FAssetDocumentCapabilityResult::Success(TEXT("diffed"));
	};

	FAssetDocumentGraphRegionWrapperAdapter Adapter(MoveTemp(Config), MoveTemp(Hooks));
	FAssetDocumentRegionContext RegionContext;
	RegionContext.RegionId = TEXT("Body.TestGraphRegions");
	RegionContext.BodyPath = TEXT("Body.TestGraphRegions");
	RegionContext.JsonPointer = TEXT("/Body");

	TSharedRef<FJsonObject> BodyObject = MakeShared<FJsonObject>();
	BodyObject->SetArrayField(TEXT("Graphs"), {});
	const TSharedRef<FJsonValueObject> BodyValue = MakeShared<FJsonValueObject>(BodyObject);

	TestTrue(TEXT("Validate succeeds"), Adapter.ValidateRegion(RegionContext, BodyValue).bSuccess);
	FAssetDocumentRegionContext PreflightContext = RegionContext;
	TestTrue(TEXT("Preflight succeeds"), Adapter.PreflightRegion(PreflightContext, BodyValue).bSuccess);
	bool bChanged = false;
	TestTrue(TEXT("Apply succeeds"), Adapter.ApplyRegion(RegionContext, BodyValue, bChanged).bSuccess);
	TestTrue(TEXT("Apply propagates changed"), bChanged);
	TSharedPtr<FJsonValue> ExtractedValue;
	TestTrue(TEXT("Extract succeeds"), Adapter.ExtractRegion(RegionContext, ExtractedValue).bSuccess);
	TestTrue(TEXT("Extract returns object"), ExtractedValue.IsValid() && ExtractedValue->Type == EJson::Object);
	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	TestTrue(TEXT("Diff succeeds"), Adapter.DiffRegion(RegionContext, BodyValue, DiffEntries).bSuccess);

	TestEqual(TEXT("Validate calls"), ValidateCalls, 1);
	TestEqual(TEXT("Preflight calls"), PreflightCalls, 1);
	TestEqual(TEXT("Apply calls"), ApplyCalls, 1);
	TestEqual(TEXT("Extract calls"), ExtractCalls, 1);
	TestEqual(TEXT("Diff calls"), DiffCalls, 1);
	TestEqual(TEXT("One diff entry"), DiffEntries.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionRuntimeGraphWrapperRejectsInvalidBodyAndMissingHooksTest,
	"AssetFactory.AssetDocument.RegionRuntime.GraphWrapper.RejectsInvalidBodyAndMissingHooks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionRuntimeGraphWrapperRejectsInvalidBodyAndMissingHooksTest::RunTest(const FString&)
{
	FAssetDocumentGraphRegionWrapperConfig Config;
	Config.AdapterName = TEXT("TestGraphWrapper");
	Config.RegionId = TEXT("Body.TestGraphRegions");
	Config.BodyPath = TEXT("Body.TestGraphRegions");
	Config.JsonPointer = TEXT("/Body");
	FAssetDocumentGraphRegionWrapperHooks Hooks;
	FAssetDocumentGraphRegionWrapperAdapter Adapter(MoveTemp(Config), MoveTemp(Hooks));

	FAssetDocumentRegionContext RegionContext;
	RegionContext.RegionId = TEXT("Body.TestGraphRegions");
	RegionContext.BodyPath = TEXT("Body.TestGraphRegions");
	RegionContext.JsonPointer = TEXT("/Body");

	const FAssetDocumentCapabilityResult InvalidBodyResult =
		Adapter.ValidateRegion(RegionContext, MakeShared<FJsonValueString>(TEXT("not object")));
	TestFalse(TEXT("Invalid body fails"), InvalidBodyResult.bSuccess);
	TestEqual(TEXT("Invalid body code"), InvalidBodyResult.Diagnostics.Num() > 0 ? InvalidBodyResult.Diagnostics[0].Code : FString(), FString(TEXT("InvalidRegionType")));
	TestEqual(TEXT("Invalid body path"), InvalidBodyResult.Diagnostics.Num() > 0 ? InvalidBodyResult.Diagnostics[0].Path : FString(), FString(TEXT("/Body")));

	TSharedRef<FJsonObject> BodyObject = MakeShared<FJsonObject>();
	const FAssetDocumentCapabilityResult MissingHookResult =
		Adapter.ValidateRegion(RegionContext, MakeShared<FJsonValueObject>(BodyObject));
	TestFalse(TEXT("Missing hook fails"), MissingHookResult.bSuccess);
	TestEqual(TEXT("Missing hook code"), MissingHookResult.Diagnostics.Num() > 0 ? MissingHookResult.Diagnostics[0].Code : FString(), FString(TEXT("UnsupportedGraphRegionLifecycle")));

	bool bChanged = true;
	const FAssetDocumentCapabilityResult InvalidApplyBodyResult =
		Adapter.ApplyRegion(RegionContext, MakeShared<FJsonValueString>(TEXT("not object")), bChanged);
	TestFalse(TEXT("Invalid apply body fails"), InvalidApplyBodyResult.bSuccess);
	TestFalse(TEXT("Invalid apply resets changed"), bChanged);
	return true;
}
```

Run:

```powershell
git diff --check
```

Expected: clean whitespace check. The new tests will not compile until the adapter files exist.

- [ ] **Step 3: Implement the adapter header**

Create `Source/AssetDocument/Private/Regions/AssetDocumentGraphRegionWrapperAdapter.h`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"
#include "AssetDocumentRegionRuntime.h"

struct FAssetDocumentGraphRegionWrapperConfig
{
	FName AdapterName;
	FString RegionId;
	FString BodyPath;
	FString JsonPointer = TEXT("/Body");
	FString SchemaLabel;
};

struct FAssetDocumentGraphRegionWrapperHooks
{
	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& BodyObject)> Validate;

	TFunction<FAssetDocumentCapabilityResult(
		FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& BodyObject)> Preflight;

	TFunction<FAssetDocumentCapabilityResult(
		FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& BodyObject,
		bool& bOutChanged)> Apply;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentCapabilityContext& Context,
		TSharedRef<FJsonObject>& OutBodyObject)> Extract;

	TFunction<FAssetDocumentCapabilityResult(
		const FAssetDocumentCapabilityContext& Context,
		const TSharedRef<FJsonObject>& BodyObject,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)> Diff;
};

class FAssetDocumentGraphRegionWrapperAdapter final : public IAssetDocumentRegionAdapter
{
public:
	FAssetDocumentGraphRegionWrapperAdapter(
		FAssetDocumentGraphRegionWrapperConfig InConfig,
		FAssetDocumentGraphRegionWrapperHooks InHooks);

	virtual FName GetName() const override;
	virtual bool SupportsRegion(const FAssetDocumentRegionContext& Context) const override;
	virtual TSharedRef<FJsonObject> GetSchemaHint(const FAssetDocumentRegionContext& Context) const override;
	virtual FAssetDocumentCapabilityResult ValidateRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue) const override;
	virtual FAssetDocumentCapabilityResult PreflightRegion(
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue) const override;
	virtual FAssetDocumentCapabilityResult ApplyRegion(
		FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		bool& bOutChanged) override;
	virtual FAssetDocumentCapabilityResult ExtractRegion(
		const FAssetDocumentRegionContext& Context,
		TSharedPtr<FJsonValue>& OutCurrentValue) const override;
	virtual FAssetDocumentCapabilityResult DiffRegion(
		const FAssetDocumentRegionContext& Context,
		const TSharedPtr<FJsonValue>& DesiredValue,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const override;

private:
	FAssetDocumentGraphRegionWrapperConfig Config;
	FAssetDocumentGraphRegionWrapperHooks Hooks;
};
```

- [ ] **Step 4: Implement the adapter cpp**

Create `Source/AssetDocument/Private/Regions/AssetDocumentGraphRegionWrapperAdapter.cpp` using these exact helper names:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#include "Regions/AssetDocumentGraphRegionWrapperAdapter.h"

#include "AssetDocumentJsonRegionUtils.h"

namespace
{
FString RegionPath(const FAssetDocumentRegionContext& Context)
{
	return Context.JsonPointer.IsEmpty() ? Context.BodyPath : Context.JsonPointer;
}

FAssetDocumentCapabilityContext ToCapabilityContext(const FAssetDocumentRegionContext& Context)
{
	FAssetDocumentCapabilityContext CapabilityContext;
	CapabilityContext.Asset = Context.Asset;
	CapabilityContext.AssetClass = Context.AssetClass;
	CapabilityContext.TargetAssetPath = Context.TargetAssetPath;
	CapabilityContext.SourceDocumentPath = Context.SourceDocumentPath;
	CapabilityContext.Definitions = Context.Definitions;
	CapabilityContext.bIsDryRun = Context.bIsDryRun;
	CapabilityContext.Result = Context.Result;
	return CapabilityContext;
}

FAssetDocumentCapabilityResult UnsupportedLifecycleFailure(const FAssetDocumentRegionContext& Context, const TCHAR* Lifecycle)
{
	return FAssetDocumentCapabilityResult::Failure(
		FString::Printf(TEXT("Graph region wrapper '%s' does not implement %s"), *Context.RegionId.ToString(), Lifecycle),
		RegionPath(Context),
		TEXT("UnsupportedGraphRegionLifecycle"));
}

FAssetDocumentCapabilityResult RequireGraphBodyObject(
	const FAssetDocumentRegionContext& Context,
	const TSharedPtr<FJsonValue>& DesiredValue,
	TSharedPtr<FJsonObject>& OutBodyObject)
{
	return FAssetDocumentJsonRegionUtils::RequireObjectValue(DesiredValue, RegionPath(Context), OutBodyObject);
}
}
```

Then implement the methods:

- Constructor moves `Config` and `Hooks`.
- `GetName` returns `Config.AdapterName`.
- `SupportsRegion` returns true only when:
  - `Context.JsonPointer == Config.JsonPointer`
  - and `Context.RegionId == FName(*Config.RegionId)` or `Context.BodyPath == Config.BodyPath`
- `GetSchemaHint` returns object with:
  - `"Adapter"` = `GetName().ToString()`
  - optional `"Label"` when `Config.SchemaLabel` is not empty.
- `ValidateRegion` requires object, returns unsupported if `Hooks.Validate` is unset, otherwise calls `Hooks.Validate(ToCapabilityContext(Context), BodyObject.ToSharedRef())`.
- `PreflightRegion` requires object, returns unsupported if `Hooks.Preflight` is unset, otherwise creates `FAssetDocumentCapabilityContext CapabilityContext = ToCapabilityContext(Context)` and calls the hook.
- `ApplyRegion` requires object; on shape failure set `bOutChanged = false`; returns unsupported if `Hooks.Apply` is unset; otherwise calls hook with mutable capability context and `bOutChanged`.
- `ExtractRegion` creates `TSharedRef<FJsonObject> BodyObject = MakeShared<FJsonObject>()`; returns unsupported if `Hooks.Extract` is unset; on success sets `OutCurrentValue = MakeShared<FJsonValueObject>(BodyObject)`.
- `DiffRegion` requires object, returns unsupported if `Hooks.Diff` is unset, otherwise calls the hook.

The adapter must not include WidgetBlueprint, UBlueprint, graph parser, or K2 graph headers.

- [ ] **Step 5: Run targeted compile/test**

Run:

```powershell
git diff --check
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/AVH1/AVH1.uproject' -NoHotReload
```

Expected: whitespace check passes and UBT succeeds.

If automation is affordable at this task, run:

```powershell
$report = 'C:/AVH1/Saved/AutomationReports/GraphWrapperTask1'
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.RegionRuntime.GraphWrapper;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected: all `AssetFactory.AssetDocument.RegionRuntime.GraphWrapper.*` tests pass.

- [ ] **Step 6: Commit Task 1**

```powershell
git add Source/AssetDocument/Private/Regions/AssetDocumentGraphRegionWrapperAdapter.h Source/AssetDocument/Private/Regions/AssetDocumentGraphRegionWrapperAdapter.cpp Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp
git commit -m "feat: add asset document graph region wrapper adapter"
```

### Task 2: WidgetBlueprint Graph Wrapper Migration

**Files:**

- Modify: `Source/AssetDocument/Private/Regions/AssetDocumentWidgetBlueprintRegionWrappers.cpp`
- Modify: `Source/AssetDocument/Private/Regions/AssetDocumentWidgetBlueprintRegionWrappers.h`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp` only if a regression assertion is missing.

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: clean working tree.

- [ ] **Step 2: Replace Widget graph lifecycle body with generic wrapper delegation**

In `AssetDocumentWidgetBlueprintRegionWrappers.cpp`:

1. Add include:

```cpp
#include "Regions/AssetDocumentGraphRegionWrapperAdapter.h"
```

2. Remove the graph-only local helpers that become redundant:

- `RequireGraphBodyObject`
- `SupportsGraphBodyRegion`

Keep shared helpers still used by tree/binding/animation adapters:

- `MakeSchemaHint`
- `SupportsBodyKey`
- `RegionPath`
- `AddCanonicalDiffEntry`
- `ToCapabilityContext` if still needed by non-graph code

3. Add helper factory near the anonymous namespace:

```cpp
FAssetDocumentGraphRegionWrapperConfig MakeWidgetBlueprintGraphWrapperConfig()
{
	FAssetDocumentGraphRegionWrapperConfig Config;
	Config.AdapterName = FWidgetBlueprintGraphRegionAdapter::AdapterName();
	Config.RegionId = TEXT("Body.WidgetBlueprintGraphRegions");
	Config.BodyPath = TEXT("Body.WidgetBlueprintGraphRegions");
	Config.JsonPointer = TEXT("/Body");
	Config.SchemaLabel = TEXT("WidgetBlueprintGraphRegions");
	return Config;
}

FAssetDocumentGraphRegionWrapperHooks MakeWidgetBlueprintGraphWrapperHooks(
	UBlueprint* DesiredStateBlueprint,
	const FWidgetBlueprintRegionAdapterHooks& Hooks)
{
	FAssetDocumentGraphRegionWrapperHooks GraphHooks;
	GraphHooks.Validate = [&Hooks](const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonObject>& BodyObject)
	{
		if (Hooks.Validate)
		{
			FAssetDocumentRegionContext RegionContext;
			RegionContext.Asset = Context.Asset;
			RegionContext.AssetClass = Context.AssetClass;
			RegionContext.TargetAssetPath = Context.TargetAssetPath;
			RegionContext.SourceDocumentPath = Context.SourceDocumentPath;
			RegionContext.Definitions = Context.Definitions;
			RegionContext.bIsDryRun = Context.bIsDryRun;
			RegionContext.Result = Context.Result;
			RegionContext.RegionId = TEXT("Body.WidgetBlueprintGraphRegions");
			RegionContext.BodyPath = TEXT("Body.WidgetBlueprintGraphRegions");
			RegionContext.JsonPointer = TEXT("/Body");
			return Hooks.Validate(RegionContext, MakeShared<FJsonValueObject>(BodyObject));
		}
		return FWidgetBlueprintGraphAdapter().ValidateRegions(Context, BodyObject);
	};
	GraphHooks.Preflight = [DesiredStateBlueprint, &Hooks](FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonObject>& BodyObject)
	{
		if (Hooks.Preflight)
		{
			FAssetDocumentRegionContext RegionContext;
			RegionContext.Asset = Context.Asset;
			RegionContext.AssetClass = Context.AssetClass;
			RegionContext.TargetAssetPath = Context.TargetAssetPath;
			RegionContext.SourceDocumentPath = Context.SourceDocumentPath;
			RegionContext.Definitions = Context.Definitions;
			RegionContext.bIsDryRun = Context.bIsDryRun;
			RegionContext.Result = Context.Result;
			RegionContext.RegionId = TEXT("Body.WidgetBlueprintGraphRegions");
			RegionContext.BodyPath = TEXT("Body.WidgetBlueprintGraphRegions");
			RegionContext.JsonPointer = TEXT("/Body");
			return Hooks.Preflight(RegionContext, MakeShared<FJsonValueObject>(BodyObject));
		}
		return FWidgetBlueprintGraphAdapter().PreflightRegions(
			Context,
			BodyObject,
			DesiredStateBlueprint ? DesiredStateBlueprint : Cast<UBlueprint>(Context.Asset));
	};
	GraphHooks.Apply = [&Hooks](FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonObject>& BodyObject, bool& bOutChanged)
	{
		if (Hooks.Apply)
		{
			FAssetDocumentRegionContext RegionContext;
			RegionContext.Asset = Context.Asset;
			RegionContext.AssetClass = Context.AssetClass;
			RegionContext.TargetAssetPath = Context.TargetAssetPath;
			RegionContext.SourceDocumentPath = Context.SourceDocumentPath;
			RegionContext.Definitions = Context.Definitions;
			RegionContext.bIsDryRun = Context.bIsDryRun;
			RegionContext.Result = Context.Result;
			RegionContext.RegionId = TEXT("Body.WidgetBlueprintGraphRegions");
			RegionContext.BodyPath = TEXT("Body.WidgetBlueprintGraphRegions");
			RegionContext.JsonPointer = TEXT("/Body");
			return Hooks.Apply(RegionContext, MakeShared<FJsonValueObject>(BodyObject), bOutChanged);
		}
		return FWidgetBlueprintGraphAdapter().ApplyRegions(Context, BodyObject, bOutChanged);
	};
	GraphHooks.Extract = [&Hooks](const FAssetDocumentCapabilityContext& Context, TSharedRef<FJsonObject>& OutBodyObject)
	{
		if (Hooks.Extract)
		{
			FAssetDocumentRegionContext RegionContext;
			RegionContext.Asset = Context.Asset;
			RegionContext.AssetClass = Context.AssetClass;
			RegionContext.TargetAssetPath = Context.TargetAssetPath;
			RegionContext.SourceDocumentPath = Context.SourceDocumentPath;
			RegionContext.Definitions = Context.Definitions;
			RegionContext.bIsDryRun = Context.bIsDryRun;
			RegionContext.Result = Context.Result;
			RegionContext.RegionId = TEXT("Body.WidgetBlueprintGraphRegions");
			RegionContext.BodyPath = TEXT("Body.WidgetBlueprintGraphRegions");
			RegionContext.JsonPointer = TEXT("/Body");
			TSharedPtr<FJsonValue> ExtractedValue;
			const FAssetDocumentCapabilityResult Result = Hooks.Extract(RegionContext, ExtractedValue);
			if (Result.bSuccess && ExtractedValue.IsValid() && ExtractedValue->Type == EJson::Object)
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ExtractedValue->AsObject()->Values)
				{
					OutBodyObject->SetField(Pair.Key, Pair.Value);
				}
			}
			return Result;
		}
		return FWidgetBlueprintGraphAdapter().ExtractRegions(Context, OutBodyObject);
	};
	GraphHooks.Diff = [&Hooks](const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonObject>& BodyObject, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
	{
		if (Hooks.Diff)
		{
			FAssetDocumentRegionContext RegionContext;
			RegionContext.Asset = Context.Asset;
			RegionContext.AssetClass = Context.AssetClass;
			RegionContext.TargetAssetPath = Context.TargetAssetPath;
			RegionContext.SourceDocumentPath = Context.SourceDocumentPath;
			RegionContext.Definitions = Context.Definitions;
			RegionContext.bIsDryRun = Context.bIsDryRun;
			RegionContext.Result = Context.Result;
			RegionContext.RegionId = TEXT("Body.WidgetBlueprintGraphRegions");
			RegionContext.BodyPath = TEXT("Body.WidgetBlueprintGraphRegions");
			RegionContext.JsonPointer = TEXT("/Body");
			return Hooks.Diff(RegionContext, MakeShared<FJsonValueObject>(BodyObject), OutDiffEntries);
		}
		return FWidgetBlueprintGraphAdapter().DiffRegions(Context, BodyObject, OutDiffEntries);
	};
	return GraphHooks;
}
```

If the repeated region-context construction becomes too noisy, extract a small local helper:

```cpp
FAssetDocumentRegionContext MakeWidgetBlueprintGraphRegionContext(const FAssetDocumentCapabilityContext& Context)
```

Do not introduce inheritance. Do not move graph parsing into the wrapper.

- [ ] **Step 3: Route `FWidgetBlueprintGraphRegionAdapter` methods through the generic wrapper**

Replace method bodies:

```cpp
bool FWidgetBlueprintGraphRegionAdapter::SupportsRegion(const FAssetDocumentRegionContext& Context) const
{
	return FAssetDocumentGraphRegionWrapperAdapter(
		MakeWidgetBlueprintGraphWrapperConfig(),
		MakeWidgetBlueprintGraphWrapperHooks(DesiredStateBlueprint, Hooks)).SupportsRegion(Context);
}
```

Use the same pattern for `GetSchemaHint`, `ValidateRegion`, `PreflightRegion`, `ApplyRegion`, `ExtractRegion`, and `DiffRegion`.

Keep:

```cpp
FName FWidgetBlueprintGraphRegionAdapter::AdapterName()
{
	return TEXT("WidgetBlueprintGraph");
}
```

The public constructors and method signatures in `AssetDocumentWidgetBlueprintRegionWrappers.h` should remain compatible.

- [ ] **Step 4: Verify Widget migration**

Run:

```powershell
git diff --check
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/AVH1/AVH1.uproject' -NoHotReload
```

Then run focused Widget graph tests:

```powershell
$report = 'C:/AVH1/Saved/AutomationReports/GraphWrapperTask2Widget'
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint.Graphs;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

If the filter finds no tests because the exact grouping differs, run:

```powershell
$report = 'C:/AVH1/Saved/AutomationReports/GraphWrapperTask2WidgetAll'
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected: WidgetBlueprint graph tests pass; no diff path regression.

- [ ] **Step 5: Commit Task 2**

```powershell
git add Source/AssetDocument/Private/Regions/AssetDocumentWidgetBlueprintRegionWrappers.cpp Source/AssetDocument/Private/Regions/AssetDocumentWidgetBlueprintRegionWrappers.h
git commit -m "refactor: route widget blueprint graph lifecycle through wrapper"
```

### Task 3: UBlueprint Graph Wrapper Migration

**Files:**

- Modify: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp` only if existing tests do not cover the synthetic lifecycle path.

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: clean working tree.

- [ ] **Step 2: Add generic wrapper include and UBlueprint graph runtime helpers**

In `UBlueprintAssetDocumentCapability.cpp`, add:

```cpp
#include "Regions/AssetDocumentGraphRegionWrapperAdapter.h"
```

Near other local helper declarations, add functions:

```cpp
FAssetDocumentRegionContext MakeUBlueprintGraphRegionContext(const FAssetDocumentCapabilityContext& Context)
{
	FAssetDocumentRegionContext RegionContext;
	RegionContext.Asset = Context.Asset;
	RegionContext.AssetClass = Context.AssetClass;
	RegionContext.TargetAssetPath = Context.TargetAssetPath;
	RegionContext.SourceDocumentPath = Context.SourceDocumentPath;
	RegionContext.Definitions = Context.Definitions;
	RegionContext.bIsDryRun = Context.bIsDryRun;
	RegionContext.Result = Context.Result;
	RegionContext.RegionId = TEXT("Body.UBlueprintGraphRegions");
	RegionContext.BodyPath = TEXT("Body.UBlueprintGraphRegions");
	RegionContext.JsonPointer = TEXT("/Body");
	return RegionContext;
}

FAssetDocumentGraphRegionWrapperConfig MakeUBlueprintGraphWrapperConfig()
{
	FAssetDocumentGraphRegionWrapperConfig Config;
	Config.AdapterName = TEXT("UBlueprintGraphRegionAdapter");
	Config.RegionId = TEXT("Body.UBlueprintGraphRegions");
	Config.BodyPath = TEXT("Body.UBlueprintGraphRegions");
	Config.JsonPointer = TEXT("/Body");
	Config.SchemaLabel = TEXT("UBlueprintGraphRegions");
	return Config;
}

FAssetDocumentGraphRegionWrapperHooks MakeUBlueprintGraphWrapperHooks()
{
	FAssetDocumentGraphRegionWrapperHooks Hooks;
	Hooks.Validate = [](const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonObject>& BodyObject)
	{
		return FUBlueprintGraphRegionAdapter().ValidateRegions(Context, BodyObject);
	};
	Hooks.Preflight = [](FAssetDocumentCapabilityContext&, const TSharedRef<FJsonObject>&)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("UBlueprint graph preflight is handled by validate/apply hooks"));
	};
	Hooks.Apply = [](FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonObject>& BodyObject, bool& bOutChanged)
	{
		return ApplyUBlueprintGraphRegions(Context, BodyObject, bOutChanged);
	};
	Hooks.Extract = [](const FAssetDocumentCapabilityContext& Context, TSharedRef<FJsonObject>& OutBodyObject)
	{
		return FUBlueprintGraphRegionAdapter().ExtractRegions(Context, OutBodyObject);
	};
	Hooks.Diff = [](const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonObject>& BodyObject, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
	{
		return FUBlueprintGraphRegionAdapter().DiffRegions(Context, BodyObject, OutDiffEntries);
	};
	return Hooks;
}

FAssetDocumentGraphRegionWrapperAdapter MakeUBlueprintGraphWrapperAdapter()
{
	return FAssetDocumentGraphRegionWrapperAdapter(
		MakeUBlueprintGraphWrapperConfig(),
		MakeUBlueprintGraphWrapperHooks());
}
```

If `ApplyUBlueprintGraphRegions` is not visible at helper location, move only the helper definitions below its forward declaration and keep no broad include changes.

- [ ] **Step 3: Route non-staged validate through wrapper**

In `ValidateGraphRegionsWithStagedVariables`, replace direct default validation:

```cpp
return GraphRegionAdapter.ValidateRegions(Context, BodyObject);
```

with:

```cpp
FAssetDocumentRegionContext GraphRegionContext = MakeUBlueprintGraphRegionContext(Context);
return MakeUBlueprintGraphWrapperAdapter().ValidateRegion(GraphRegionContext, MakeShared<FJsonValueObject>(BodyObject));
```

For staged validation, keep all temporary blueprint / variable staging logic unchanged, then replace:

```cpp
return GraphRegionAdapter.ValidateRegions(GraphContext, BodyObject);
```

with wrapper validate using `GraphContext`.

Do not move staging into `FAssetDocumentGraphRegionWrapperAdapter`.

- [ ] **Step 4: Route apply/extract/diff through wrapper**

In apply body, replace:

```cpp
ApplyUBlueprintGraphRegions(Context, BodyObject.ToSharedRef(), bGraphChanged);
```

with:

```cpp
FAssetDocumentRegionContext GraphRegionContext = MakeUBlueprintGraphRegionContext(Context);
MakeUBlueprintGraphWrapperAdapter().ApplyRegion(
	GraphRegionContext,
	MakeShared<FJsonValueObject>(BodyObject.ToSharedRef()),
	bGraphChanged);
```

In extract body, replace direct `FUBlueprintGraphRegionAdapter().ExtractRegions(...)` with:

```cpp
TSharedPtr<FJsonValue> ExtractedGraphBodyValue;
FAssetDocumentRegionContext GraphRegionContext = MakeUBlueprintGraphRegionContext(Context);
const FAssetDocumentCapabilityResult GraphExtractResult =
	MakeUBlueprintGraphWrapperAdapter().ExtractRegion(GraphRegionContext, ExtractedGraphBodyValue);
if (!GraphExtractResult.bSuccess)
{
	return GraphExtractResult;
}
if (ExtractedGraphBodyValue.IsValid() && ExtractedGraphBodyValue->Type == EJson::Object)
{
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ExtractedGraphBodyValue->AsObject()->Values)
	{
		OutBodyJson->SetField(Pair.Key, Pair.Value);
	}
}
```

Keep the existing lines that set `FunctionGraphs`, `MacroGraphs`, and `Timelines` to empty arrays after graph extract.

In diff body, replace direct `FUBlueprintGraphRegionAdapter().DiffRegions(...)` with wrapper diff:

```cpp
FAssetDocumentRegionContext GraphRegionContext = MakeUBlueprintGraphRegionContext(Context);
const FAssetDocumentCapabilityResult GraphDiffResult =
	MakeUBlueprintGraphWrapperAdapter().DiffRegion(
		GraphRegionContext,
		MakeShared<FJsonValueObject>(DesiredBody.ToSharedRef()),
		OutDiffEntries);
```

Do not add public support for UBlueprint `FunctionGraphs` or `MacroGraphs`.

- [ ] **Step 5: Add regression assertion only if needed**

If existing UBlueprint graph tests do not exercise wrapper routed apply/extract/diff, add one focused assertion to `AssetDocumentUBlueprintTests.cpp` near existing UBlueprint graph tests:

- Apply a body with `UbergraphPages`.
- Extract the body.
- Assert `UbergraphPages` exists.
- Assert `FunctionGraphs` and `MacroGraphs` remain empty arrays.

Use the existing test helpers in that file rather than creating new asset factories.

- [ ] **Step 6: Verify UBlueprint migration**

Run:

```powershell
git diff --check
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/AVH1/AVH1.uproject' -NoHotReload
```

Then run focused UBlueprint tests:

```powershell
$report = 'C:/AVH1/Saved/AutomationReports/GraphWrapperTask3UBlueprint'
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected: UBlueprint tests pass; `FunctionGraphs` / `MacroGraphs` behavior remains deferred/empty.

- [ ] **Step 7: Commit Task 3**

```powershell
git add Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp
git commit -m "refactor: route ublueprint graph lifecycle through wrapper"
```

If `AssetDocumentUBlueprintTests.cpp` was not changed, omit it from `git add`.

### Task 4: Cleanup, Refactor Chain Update, Full Verification

**Files:**

- Modify: `docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md`
- Modify code files only for review cleanup found after Task 1-3.

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = git rev-parse HEAD
git status --short --branch
```

Expected: clean working tree.

- [ ] **Step 2: Scan for duplicate graph lifecycle plumbing**

Run:

```powershell
rg -n "RequireGraphBodyObject|SupportsGraphBodyRegion|ValidateRegion\\(|PreflightRegion\\(|ApplyRegion\\(|ExtractRegion\\(|DiffRegion\\(" Source/AssetDocument/Private/Regions Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp
```

Expected:

- `RequireGraphBodyObject` should only exist in `AssetDocumentGraphRegionWrapperAdapter.cpp`, unless a non-graph adapter legitimately uses the name for another purpose.
- `SupportsGraphBodyRegion` should be removed or replaced by wrapper config matching.
- Widget graph `ValidateRegion` / `PreflightRegion` / `ApplyRegion` / `ExtractRegion` / `DiffRegion` should delegate to `FAssetDocumentGraphRegionWrapperAdapter`.
- UBlueprint graph validate/apply/extract/diff should call wrapper helpers, not direct `FUBlueprintGraphRegionAdapter` except inside UBlueprint graph hooks.

Fix only obvious duplication introduced by this task. Do not refactor unrelated Widget tree/binding/animation adapters.

- [ ] **Step 3: Update refactor-chain ring 5 status**

In `docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md`, update section `## 9. 第 5 环：Graph Wrapper Adapter Consolidation` with an implementation note:

```markdown
实现状态（2026-06-30）：

- 已新增 `FAssetDocumentGraphRegionWrapperAdapter` 作为 synthetic graph body lifecycle wrapper。
- WidgetBlueprint graph region adapter 已委托公共 wrapper，graph materializer 仍为 `FWidgetBlueprintGraphAdapter`。
- UBlueprint graph validate / apply / extract / diff 已通过公共 wrapper 接线，staged parent / variable validation 保持 UBlueprint hook。
- UBlueprint `FunctionGraphs` / `MacroGraphs` 仍未新增 authoring 支持。
```

- [ ] **Step 4: Full verification**

Run:

```powershell
git diff --check
& 'E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' AVH1Editor Win64 Development '-Project=C:/AVH1/AVH1.uproject' -NoHotReload
```

Run automation:

```powershell
$report = 'C:/AVH1/Saved/AutomationReports/GraphWrapperFullAssetDocument'
& 'E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/AVH1/AVH1.uproject' -unattended -nop4 -nosplash -nullrhi -NoSound -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="$report"
```

Expected:

- UBT succeeds.
- AssetFactory.AssetDocument automation finishes with no failures.
- Warnings are acceptable only if they match known pre-existing warnings; record count and report path in final task notes.

- [ ] **Step 5: Commit Task 4**

```powershell
git add docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md Source/AssetDocument/Private/Regions/AssetDocumentGraphRegionWrapperAdapter.h Source/AssetDocument/Private/Regions/AssetDocumentGraphRegionWrapperAdapter.cpp Source/AssetDocument/Private/Regions/AssetDocumentWidgetBlueprintRegionWrappers.cpp Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp
git commit -m "docs: mark graph wrapper adapter consolidation implemented"
```

Only add code files if Task 4 made cleanup changes.

## Review Gates

After each task commit:

1. Spec compliance review over `TASK_BASE..HEAD`.
2. Code quality review over `TASK_BASE..HEAD`.
3. Fix any findings in a follow-up commit before starting the next task.

After Task 4:

1. Final spec review over `SPEC_BASE..HEAD`.
2. Final code quality review over `SPEC_BASE..HEAD`.
3. If approved, use the finishing branch workflow to decide merge/hold.

Review prompts must include only:

- This plan.
- The graph wrapper spec.
- The current task acceptance criteria.
- The exact diff range.
- Relevant files for the task.

Do not attach unrelated historical specs or legacy generator context.

## Completion Criteria

- `FAssetDocumentGraphRegionWrapperAdapter` exists and is covered by automation tests.
- WidgetBlueprint graph region lifecycle delegates to the common wrapper.
- UBlueprint graph validate/apply/extract/diff delegates to the common wrapper.
- Graph materializers remain profile-specific.
- Public AssetDocument JSON body shape is unchanged.
- Diagnostic and diff paths stay under real body keys.
- UBlueprint `FunctionGraphs` / `MacroGraphs` remain non-authorable in this change.
- `git diff --check`, UBT, and AssetDocument automation have been run and recorded.
