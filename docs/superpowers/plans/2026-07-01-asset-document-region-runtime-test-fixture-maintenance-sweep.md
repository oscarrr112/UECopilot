# AssetDocument Region Runtime Test Fixture And Maintenance Sweep Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the public region runtime refactor chain by adding a small shared region-runtime test fixture and updating the maintenance docs that keep future AssetDocument work on thin public-adapter hooks.

**Architecture:** Add a header-only test fixture under `Source/AssetDocument/Private/Tests/` and migrate only real, low-risk runtime adapter tests to it. Keep the fixture focused on context/value builders and explicit diagnostic/diff assertions; do not add a generic testing framework or hide path/code assertions behind broad helpers.

**Tech Stack:** Unreal Engine 5.7 C++, `WITH_DEV_AUTOMATION_TESTS`, AssetDocument public region runtime, PowerShell, UBT, Unreal automation tests.

---

## Scope And Base

`SPEC_BASE` for this plan is the current parent branch tip after the timeline placement merge:

```powershell
git -C E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-object-field-schema-dispatcher-migration rev-parse HEAD
```

Expected before execution:

```text
f637d776c8f4b1e87346e5a08d02838f6f14a52a
```

Implementation must happen in a new branch and worktree created from `feature/asset-document-object-field-schema-dispatcher-migration`. Do not implement directly in the parent worktree unless the user explicitly says to do so.

Suggested branch:

```text
feature/asset-document-region-runtime-test-fixture-sweep
```

Suggested worktree:

```text
E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-region-runtime-test-fixture-sweep
```

This plan intentionally excludes production behavior changes. It may edit test helper code and docs only, except for includes needed by migrated tests.

## File Responsibilities

- Create: `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTestFixture.h`
  - Header-only test fixture used only by `WITH_DEV_AUTOMATION_TESTS`.
  - Owns JSON value builders, region policy/context builders, dispatcher construction, diagnostic assertions, and diff-entry lookup helpers.
  - Must not include asset-specific profile headers or understand asset classes.
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`
  - Include the new fixture header.
  - Remove or stop using duplicated local helpers only after their call sites are migrated.
  - Migrate a bounded set of runtime tests: deferred region, object region, named array, dispatcher, preview apply diff, and one timeline placement test.
- Modify: `docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md`
  - Mark the test fixture ring as implemented when tests and reviews pass.
  - Keep the explicit statement that this is not a no-user generic framework.
- Modify: `docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md`
  - Add guidance that new public adapter tests should use the fixture for context/value/diagnostic boilerplate while still asserting exact path and code.
- Modify: `docs/superpowers/specs/asset-document-deferred-fields/2026-06-22-region-canonicalizer.md`
  - Add a short maintenance rule that future region-runtime/canonicalization tests should keep public runtime, adapter utility, and profile behavior evidence separate.

## Do Not Touch

- Do not modify `Source/AssetDocument/Private/Profiles/*` files.
- Do not change `Source/AssetDocument/Private/Regions/*` production adapters.
- Do not change MCP schemas or runtime TypeScript.
- Do not migrate all `AssetDocumentRegionRuntimeTests.cpp` helpers in one pass.
- Do not replace profile-level automation with fixture tests.
- Do not create a `UniversalTestFixture` that hides diagnostics or accepts fuzzy path/code matching.

## Task 1: Create The Minimal Test Fixture And Migrate Deferred Region Tests

**Files:**
- Create: `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTestFixture.h`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`

- [ ] **Step 1: Record task base**

Run:

```powershell
git rev-parse HEAD
```

Save the output in the task notes as `TASK_BASE`. Reviews for this task must use `TASK_BASE..HEAD`.

- [ ] **Step 2: Add the fixture header**

Create `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTestFixture.h` with this content:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentBodyRegionDispatcher.h"
#include "AssetDocumentJsonRegionUtils.h"
#include "AssetDocumentRegionRuntime.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AssetDocumentRegionRuntimeTest
{
inline TSharedPtr<FJsonValue> MakeObjectValue(TSharedRef<FJsonObject> Object)
{
	return MakeShared<FJsonValueObject>(Object);
}

inline TSharedPtr<FJsonValue> MakeArrayValue(TArray<TSharedPtr<FJsonValue>> Values)
{
	return MakeShared<FJsonValueArray>(MoveTemp(Values));
}

inline TSharedRef<FJsonValue> MakeObjectRef(TSharedRef<FJsonObject> Object)
{
	return MakeShared<FJsonValueObject>(Object);
}

inline TSharedRef<FJsonObject> MakeBodyWithField(const FString& FieldName, const TSharedPtr<FJsonValue>& FieldValue)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetField(FieldName, FieldValue);
	return Body;
}

inline FAssetDocumentRegionPolicy MakePolicy(const FName RegionId, const FString& BodyPath)
{
	FAssetDocumentRegionPolicy Policy;
	Policy.RegionId = RegionId;
	Policy.BodyPath = BodyPath;
	return Policy;
}

inline FAssetDocumentRegionPolicy MakeDeferredPolicy(
	const FName RegionId,
	const FString& BodyPath,
	const EAssetDocumentRegionKind RegionKind)
{
	FAssetDocumentRegionPolicy Policy = MakePolicy(RegionId, BodyPath);
	Policy.RegionKind = RegionKind;
	return Policy;
}

inline FAssetDocumentRegionBinding MakeBinding(
	const FName BodyKey,
	const FName RegionId,
	const FName AdapterName,
	const int32 ApplyOrder = 0,
	const bool bRequired = false)
{
	FAssetDocumentRegionBinding Binding;
	Binding.BodyKey = BodyKey;
	Binding.RegionId = RegionId;
	Binding.AdapterName = AdapterName;
	Binding.ApplyOrder = ApplyOrder;
	Binding.bRequired = bRequired;
	return Binding;
}

class FAssetDocumentRegionRuntimeTestContextBuilder
{
public:
	FAssetDocumentRegionRuntimeTestContextBuilder& WithRegionId(const FName InRegionId)
	{
		RegionId = InRegionId;
		return *this;
	}

	FAssetDocumentRegionRuntimeTestContextBuilder& WithBodyPath(const FString& InBodyPath)
	{
		BodyPath = InBodyPath;
		return *this;
	}

	FAssetDocumentRegionRuntimeTestContextBuilder& WithJsonPointer(const FString& InJsonPointer)
	{
		JsonPointer = InJsonPointer;
		return *this;
	}

	FAssetDocumentRegionRuntimeTestContextBuilder& WithPolicy(const FAssetDocumentRegionPolicy* InPolicy)
	{
		Policy = InPolicy;
		return *this;
	}

	FAssetDocumentRegionContext Build() const
	{
		FAssetDocumentRegionContext Context;
		Context.RegionId = RegionId;
		Context.BodyPath = BodyPath.IsEmpty()
			? FString::Printf(TEXT("Body.%s"), *RegionId.ToString())
			: BodyPath;
		Context.JsonPointer = JsonPointer;
		Context.Policy = Policy;
		return Context;
	}

private:
	FName RegionId = TEXT("Preview");
	FString BodyPath;
	FString JsonPointer = TEXT("/Body/Preview");
	const FAssetDocumentRegionPolicy* Policy = nullptr;
};

inline FAssetDocumentRegionContext MakeRuntimeContext(
	const FName RegionId = TEXT("Preview"),
	const FString& JsonPointer = TEXT("/Body/Preview"),
	const FAssetDocumentRegionPolicy* Policy = nullptr)
{
	return FAssetDocumentRegionRuntimeTestContextBuilder()
		.WithRegionId(RegionId)
		.WithJsonPointer(JsonPointer)
		.WithPolicy(Policy)
		.Build();
}

inline FAssetDocumentBodyRegionDispatcher MakeDispatcher(
	const TArray<FAssetDocumentRegionBinding>& Bindings,
	const TArray<FAssetDocumentRegionPolicy>& Policies,
	const TArray<IAssetDocumentRegionAdapter*>& Adapters,
	FAssetDocumentBodyRegionDispatcherHooks Hooks = {})
{
	TMap<FName, IAssetDocumentRegionAdapter*> AdapterMap;
	for (IAssetDocumentRegionAdapter* Adapter : Adapters)
	{
		AdapterMap.Add(Adapter->GetName(), Adapter);
	}

	return FAssetDocumentBodyRegionDispatcher(
		Bindings,
		Policies,
		AdapterMap,
		MoveTemp(Hooks));
}

inline FString GetDiffEntryPath(const TArray<TSharedPtr<FJsonValue>>& Entries, const int32 Index)
{
	const TSharedPtr<FJsonObject> Entry = Entries.IsValidIndex(Index) && Entries[Index].IsValid()
		? Entries[Index]->AsObject()
		: nullptr;
	return Entry.IsValid() ? Entry->GetStringField(TEXT("path")) : FString();
}

inline TSharedPtr<FJsonObject> FindDiffEntryByPath(
	const TArray<TSharedPtr<FJsonValue>>& Entries,
	const FString& ExpectedPath)
{
	for (const TSharedPtr<FJsonValue>& EntryValue : Entries)
	{
		const TSharedPtr<FJsonObject> Entry = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
		FString Path;
		if (Entry.IsValid() && Entry->TryGetStringField(TEXT("path"), Path) && Path == ExpectedPath)
		{
			return Entry;
		}
	}
	return nullptr;
}

inline bool TestDiagnostic(
	FAutomationTestBase* Test,
	const TCHAR* Label,
	const FAssetDocumentCapabilityResult& Result,
	const FString& ExpectedPath,
	const FString& ExpectedCode)
{
	const FString ActualPath = Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Path : FString();
	const FString ActualCode = Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString();
	bool bPassed = true;
	bPassed &= Test->TestEqual(FString::Printf(TEXT("%s diagnostic path"), Label), ActualPath, ExpectedPath);
	bPassed &= Test->TestEqual(FString::Printf(TEXT("%s diagnostic code"), Label), ActualCode, ExpectedCode);
	return bPassed;
}
}

#endif
```

- [ ] **Step 3: Include the fixture in runtime tests**

In `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`, add this include after the current AssetDocument includes:

```cpp
#include "AssetDocumentRegionRuntimeTestFixture.h"
```

Inside the anonymous namespace, after the opening `{`, add:

```cpp
using namespace AssetDocumentRegionRuntimeTest;
```

- [ ] **Step 4: Remove duplicate helper definitions that now come from the fixture**

Delete these local functions from `AssetDocumentRegionRuntimeTests.cpp` only after Step 3 compiles in your editor or by inspection:

```cpp
TSharedPtr<FJsonValue> MakeObjectValue(TSharedRef<FJsonObject> Object);
TSharedPtr<FJsonValue> MakeArrayValue(TArray<TSharedPtr<FJsonValue>> Values);
TSharedRef<FJsonValue> MakeObjectRef(TSharedRef<FJsonObject> Object);
TSharedRef<FJsonObject> MakeBodyWithField(const FString& FieldName, const TSharedPtr<FJsonValue>& FieldValue);
FString GetDiffEntryPath(const TArray<TSharedPtr<FJsonValue>>& Entries, const int32 Index);
TSharedPtr<FJsonObject> FindDiffEntryByPath(const TArray<TSharedPtr<FJsonValue>>& Entries, const FString& ExpectedPath);
FAssetDocumentRegionPolicy MakePolicy(const FName RegionId, const FString& BodyPath);
FAssetDocumentRegionPolicy MakeDeferredPolicy(const FName RegionId, const FString& BodyPath, const EAssetDocumentRegionKind RegionKind);
FAssetDocumentRegionBinding MakeBinding(const FName BodyKey, const FName RegionId, const FName AdapterName, const int32 ApplyOrder, const bool bRequired);
FAssetDocumentRegionContext MakeRuntimeContext(const FName RegionId, const FString& JsonPointer, const FAssetDocumentRegionPolicy* Policy);
FAssetDocumentBodyRegionDispatcher MakeDispatcher(const TArray<FAssetDocumentRegionBinding>& Bindings, const TArray<FAssetDocumentRegionPolicy>& Policies, const TArray<IAssetDocumentRegionAdapter*>& Adapters, FAssetDocumentBodyRegionDispatcherHooks Hooks);
```

Keep local helpers that are not part of the fixture in this task, including:

```cpp
TSharedPtr<FJsonValue> MakeIdentityValue(const FString& Name, const int32 Count);
TSharedRef<FJsonObject> MakeTestFragment(const TCHAR* Kind);
int32 GetIdentityCount(const TSharedPtr<FJsonValue>& Value);
struct FTestRegionAdapter;
```

- [ ] **Step 5: Migrate deferred region diagnostic assertions to `TestDiagnostic`**

For the deferred non-empty array test, replace this assertion shape:

```cpp
TestEqual(
	TEXT("Non-empty array diagnostic code"),
	Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Code : FString(),
	FString(TEXT("DeferredRegionMustBeEmpty")));
TestEqual(
	TEXT("Non-empty array diagnostic path"),
	Result.Diagnostics.Num() > 0 ? Result.Diagnostics[0].Path : FString(),
	FString(TEXT("/Body/FunctionGraphs")));
```

with:

```cpp
TestDiagnostic(this, TEXT("Non-empty array"), Result, TEXT("/Body/FunctionGraphs"), TEXT("DeferredRegionMustBeEmpty"));
```

For the deferred non-empty object test, replace the equivalent path/code assertions with:

```cpp
TestDiagnostic(this, TEXT("Non-empty object"), Result, TEXT("/Body/Preview"), TEXT("DeferredRegionMustBeEmpty"));
```

- [ ] **Step 6: Run focused deferred region automation**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl -ReportExportPath="C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/Saved/AutomationReports/RegionRuntimeFixtureDeferred" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.RegionRuntime.DeferredRegion; Quit"
```

Expected:

```text
UBT Result: Succeeded
Automation report has Failed=0 and NotRun=0
```

- [ ] **Step 7: Commit**

Run:

```powershell
git status --short
git diff --check
git add Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTestFixture.h Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp
git commit -m "test: add asset document region runtime fixture"
```

## Task 2: Migrate A Bounded Set Of Runtime Tests To The Fixture

**Files:**
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTestFixture.h` only if a helper is needed by at least two migrated tests.

- [ ] **Step 1: Record task base**

Run:

```powershell
git rev-parse HEAD
```

Save the output as `TASK_BASE`.

- [ ] **Step 2: Migrate object region diagnostic assertions**

In `FAssetDocumentRegionRuntimeObjectRegionRequiresObjectTest`, replace explicit diagnostic path/code assertions with:

```cpp
TestDiagnostic(this, TEXT("Non-object"), Result, TEXT("/Body/Preview"), TEXT("InvalidBodySectionType"));
```

In `FAssetDocumentRegionRuntimeObjectRegionMissingLifecycleHooksFailFastTest`, keep the separate apply/extract/diff lifecycle calls but assert diagnostics through `TestDiagnostic`:

```cpp
TestDiagnostic(this, TEXT("Object missing apply hook"), Result, TEXT("/Body/Playback"), TEXT("MissingRegionApplyHook"));
TestDiagnostic(this, TEXT("Object missing extract hook"), Result, TEXT("/Body/Playback"), TEXT("MissingRegionExtractHook"));
TestDiagnostic(this, TEXT("Object missing diff hook"), Result, TEXT("/Body/Playback"), TEXT("MissingRegionDiffHook"));
```

- [ ] **Step 3: Migrate named array diagnostic assertions**

In the named-array tests, use `TestDiagnostic` for these exact cases:

```cpp
TestDiagnostic(this, TEXT("Non-array"), Result, TEXT("/Body/NotifyTracks"), TEXT("InvalidBodySectionType"));
TestDiagnostic(this, TEXT("Duplicate identity"), Result, TEXT("/Body/NotifyTracks/1/Name"), TEXT("DuplicateNamedArrayIdentity"));
TestDiagnostic(this, TEXT("Normalized duplicate identity"), Result, TEXT("/Body/NotifyTracks/1/Name"), TEXT("DuplicateNamedArrayIdentity"));
TestDiagnostic(this, TEXT("Missing identity"), Result, TEXT("/Body/NotifyTracks/0/Name"), TEXT("MissingNamedArrayIdentity"));
```

Do not change canonicalization or diff assertions in this step.

- [ ] **Step 4: Migrate one dispatcher context construction site to the builder**

Pick `FAssetDocumentRegionRuntimeDispatchValidateCallsAdapterTest` and replace the direct `MakeRuntimeContext(...)` call with the builder so future tests have one visible example:

```cpp
const FAssetDocumentRegionContext Context = FAssetDocumentRegionRuntimeTestContextBuilder()
	.WithRegionId(TEXT("Preview"))
	.WithBodyPath(TEXT("Body.Preview"))
	.WithJsonPointer(TEXT("/Body/Preview"))
	.WithPolicy(&Policy)
	.Build();
```

Keep the existing `MakeRuntimeContext(...)` helper in the fixture for shorter tests.

- [ ] **Step 5: Migrate preview-apply-diff failure assertions**

In `FAssetDocumentRegionRuntimePreviewApplyDiffRejectsInvalidPreviewContextTest`, replace duplicated diagnostic path/code assertions with:

```cpp
TestDiagnostic(this, *CaseName, Result, TEXT("/Body"), TEXT("InvalidPreviewApplyDiffAdapter"));
```

In `FAssetDocumentRegionRuntimePreviewApplyDiffPropagatesHookFailuresTest`, keep each case explicit but route the final assertion through:

```cpp
TestDiagnostic(this, *CaseName, Result, ExpectedPath, ExpectedCode);
```

- [ ] **Step 6: Migrate one timeline placement diagnostic group**

In `FAssetDocumentTimelinePlacementUtilsRejectsInvalidShapesTest`, use `TestDiagnostic` for both shape failures:

```cpp
TestDiagnostic(this, TEXT("Non-array"), Result, TEXT("/Body/TestTimeline"), TEXT("InvalidTimelinePlacementRegionType"));
TestDiagnostic(this, TEXT("Non-object"), Result, TEXT("/Body/TestTimeline/0"), TEXT("InvalidTimelinePlacementEntryType"));
```

Do not migrate all timeline placement tests in this task. They are high-value regression tests and must remain easy to read.

- [ ] **Step 7: Run focused runtime automation**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl -ReportExportPath="C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/Saved/AutomationReports/RegionRuntimeFixtureCore" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.RegionRuntime; Quit"
```

Expected:

```text
UBT Result: Succeeded
Automation report has Failed=0 and NotRun=0
```

- [ ] **Step 8: Commit**

Run:

```powershell
git status --short
git diff --check
git add Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTestFixture.h Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp
git commit -m "test: reuse region runtime fixture in adapter tests"
```

## Task 3: Sweep Maintenance Documentation

**Files:**
- Modify: `docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md`
- Modify: `docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md`
- Modify: `docs/superpowers/specs/asset-document-deferred-fields/2026-06-22-region-canonicalizer.md`

- [ ] **Step 1: Record task base**

Run:

```powershell
git rev-parse HEAD
```

Save the output as `TASK_BASE`.

- [ ] **Step 2: Mark the eighth ring implemented in the refactor chain**

In `docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md`, under `## 12. 第 8 环：Region Runtime Test Fixture`, add this implementation status block after the completion standard:

```markdown
实现状态（2026-07-01）：

- 已新增 `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTestFixture.h`，覆盖 JSON value builder、region context builder、policy/binding helper、dispatcher construction、diagnostic assertion 和 diff entry lookup。
- `AssetDocumentRegionRuntimeTests.cpp` 的 deferred/object/named-array/dispatcher/preview-apply-diff/timeline placement 代表性用例已迁移到 fixture，保留 exact diagnostic path/code 断言。
- Fixture 仅服务 public region runtime tests，不替代 profile-level automation，也不隐藏 adapter utility 的关键语义断言。
```

Then update `## 13. 推荐执行顺序` third batch so item 1 reads:

```markdown
1. `Region Runtime Test Fixture`（已完成，后续新增 adapter tests 必须优先复用）
```

Keep items 2 and 3 as active guidance, not as completed history.

- [ ] **Step 3: Add fixture guidance to the new asset guide**

In `docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md`, after the table of common region shapes or before `### Timeline-like region 检查门槛`, add:

```markdown
### Public adapter test fixture 检查门槛

新增或扩展 public region adapter tests 时，优先复用 `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTestFixture.h`：

- 用 fixture 创建 `FAssetDocumentRegionContext`、policy、binding、dispatcher 和基础 JSON value。
- 用 fixture 的 diagnostic helper 断言 exact path/code，不允许只断言 `bSuccess == false`。
- adapter utility 的核心语义仍应在测试体中显式表达，例如 identity key、canonical order、timeline duplicate 顺序和 track resolver 行为。
- profile-level automation 仍负责 UE materialization、compile/rebuild/cache repair、asset save/load、MCP/apply-file smoke；fixture 不能替代这些验证。

如果新增测试选择不使用 fixture，implementation plan 必须说明原因，例如该测试不在 public region runtime 层，或者需要 profile/UE editor lifecycle。
```

- [ ] **Step 4: Add evidence separation rule to the canonicalizer deferred fields doc**

In `docs/superpowers/specs/asset-document-deferred-fields/2026-06-22-region-canonicalizer.md`, append a dated note to the architecture trigger section:

```markdown
2026-07-01 更新：public region runtime test fixture 已作为测试层公共入口落地。后续涉及 canonicalization、diff、region runtime 或 profile behavior 的测试必须区分三类证据：

- public runtime / adapter fixture tests：证明 shared runtime、adapter utility、diagnostic path/code 和 canonical JSON compare。
- profile automation tests：证明具体 UE asset materialization、post-apply repair、compile/rebuild 和 save/load。
- MCP / apply-file smoke：证明外部协议、sidecar path contract 和编辑器 HTTP/MCP 集成。

不得用 profile automation 的通过结果替代 public runtime fixture coverage，也不得用 fixture tests 宣称 UE materialization 已验证。
```

- [ ] **Step 5: Scan docs for stale fixture status**

Run:

```powershell
rg -n "Region Runtime Test Fixture|FAssetDocumentRegionRuntimeTestFixture|test fixture|后续新增 adapter tests" docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md docs/superpowers/specs/asset-document-deferred-fields/2026-06-22-region-canonicalizer.md
```

Expected:

```text
The three files mention the fixture consistently as implemented and bounded to public runtime tests.
```

- [ ] **Step 6: Commit**

Run:

```powershell
git status --short
git diff --check
git add docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md docs/superpowers/specs/asset-document-deferred-fields/2026-06-22-region-canonicalizer.md
git commit -m "docs: close asset document public runtime refactor chain"
```

## Task 4: Final Review And Verification

**Files:**
- No planned production edits.
- Review full diff from `SPEC_BASE..HEAD`.

- [ ] **Step 1: Run final static checks**

Run:

```powershell
git status --short --branch
git diff --check SPEC_BASE..HEAD
$Pattern = ('TB' + 'D|TO' + 'DO|' + '待' + '定|' + '占' + '位|place' + 'holder|fi' + 'll in la' + 'ter')
rg -n $Pattern docs/superpowers/plans/2026-07-01-asset-document-region-runtime-test-fixture-maintenance-sweep.md docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md docs/superpowers/specs/asset-document-deferred-fields/2026-06-22-region-canonicalizer.md
```

Expected:

```text
Working tree clean except intentional committed branch state.
git diff --check has no output.
No red-flag text matches are printed.
```

- [ ] **Step 2: Run UBT on validation host**

Ensure the validation host junction points at the task worktree:

```powershell
$junction = "C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/Plugins/AssetFactory"
Get-Item -LiteralPath $junction | Format-List FullName,LinkType,Target
```

If the target is not the task worktree, update the junction using the same safety check pattern used by prior AssetDocument tasks.

Then run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -NoHotReload
```

Expected:

```text
Result: Succeeded
```

- [ ] **Step 3: Run focused automation**

Run the full region runtime bucket:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl -ReportExportPath="C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/Saved/AutomationReports/RegionRuntimeFixtureFinal" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.RegionRuntime; Quit"
```

Read `index.json` and confirm:

```text
Failed=0
NotRun=0
```

- [ ] **Step 4: Run profile smoke automation for moved helpers**

Run a small profile smoke set to ensure fixture movement did not disturb timeline/profile tests:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl -ReportExportPath="C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/Saved/AutomationReports/RegionRuntimeFixtureAnimSequence" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence; Quit"
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl -ReportExportPath="C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/Saved/AutomationReports/RegionRuntimeFixtureAnimMontage" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage; Quit"
```

Expected:

```text
Both reports have Failed=0 and NotRun=0.
Warnings are acceptable only if they match pre-existing UE automation warning behavior and do not mark failed tests.
```

- [ ] **Step 5: Dispatch final reviews**

Dispatch two read-only reviewers with these exact ranges:

```text
Spec/code quality review diff range: SPEC_BASE..HEAD
Task review ranges: each TASK_BASE..HEAD from its task commit
```

Reviewer acceptance criteria:

- Fixture is small and test-only.
- Fixture has real migrated users.
- Diagnostic path/code assertions remain exact.
- No profile-level automation was replaced by fixture-only coverage.
- Docs now constrain future adapter tests and maintenance sweep behavior.
- No production adapter or profile behavior changed.

- [ ] **Step 6: Fix review findings, re-run focused verification, and commit fixes**

If reviewers request changes, use a fix worker with a narrow write scope:

```text
Allowed write scope:
- Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTestFixture.h
- Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp
- docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md
- docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md
- docs/superpowers/specs/asset-document-deferred-fields/2026-06-22-region-canonicalizer.md
```

After fixes, run:

```powershell
git diff --check
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -NoHotReload
```

Then re-run the automation bucket that covers the changed area.

Commit with:

```powershell
git add Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTestFixture.h Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md docs/superpowers/specs/asset-document-deferred-fields/2026-06-22-region-canonicalizer.md
git commit -m "fix: address region runtime fixture review"
```

## Completion Criteria

- `FAssetDocumentRegionRuntimeTestFixture` exists as a test-only fixture.
- At least one test each from deferred/object/named-array/dispatcher/preview-apply-diff/timeline runtime areas uses the fixture.
- Exact diagnostic path/code assertions remain visible and stable.
- `docs/superpowers/specs/2026-06-30-asset-document-public-region-runtime-refactor-chain.md` marks ring 8 implemented.
- `docs/superpowers/guides/asset-document-new-asset-class-extension-guide.md` tells future asset/profile work when to use the fixture.
- `docs/superpowers/specs/asset-document-deferred-fields/2026-06-22-region-canonicalizer.md` records evidence separation rules.
- UBT passes on the validation host.
- `AssetFactory.AssetDocument.RegionRuntime` automation passes with `Failed=0` and `NotRun=0`.
- AnimSequence and AnimMontage focused automation pass with `Failed=0` and `NotRun=0`.
- Final spec and code quality reviewers approve `SPEC_BASE..HEAD`.
