# WidgetBlueprint AssetDocument Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement complete `/Script/UMGEditor.WidgetBlueprint` AssetDocument support so generic AssetDocument tools can create/update, validate, apply, extract, diff, sync, and smoke-test WidgetBlueprint assets without using the old WidgetBlueprint generator.

**Architecture:** Add an exact `UWidgetBlueprint` profile and a set of focused WidgetBlueprint capabilities under `Source/AssetDocument/Private/Profiles`. Reuse the merged UBlueprint graph model for K2 graph regions, add a dedicated tree adapter for `UWidgetTree`, dedicated adapters for UMG bindings and widget animations, and keep every managed region authoritative through RegionPolicy and canonical sync writeback.

**Tech Stack:** Unreal Engine 5.7 editor C++, `UWidgetBlueprint`, `UWidgetTree`, `FDelegateEditorBinding`, `UWidgetAnimation`, `UMovieScene`, existing AssetDocument profile/capability/RegionPolicy/canonicalizer APIs, UE automation tests, MCP schema/profile tests, external HTTP smoke through `apply-file -> extract -> diff`.

---

## Implementation Context

**Spec:** `docs/superpowers/specs/2026-06-23-widgetblueprint-asset-document-design.md`

**Deferred fields:** `docs/superpowers/specs/asset-document-deferred-fields/2026-06-23-widgetblueprint.md`

**Base branch:** `feature/asset-document-structured-capabilities-spec`

**Implementation branch:** `feature/asset-document-widgetblueprint-impl`

**Implementation worktree:** `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-widgetblueprint-impl`

**Validation host:** use `C:/AVH1` with `Plugins/UECopilot` junctioned to the implementation worktree. Do not validate the implementation by building the main `PluginsWarehouse.uproject` while the main project has a same-named plugin that can shadow the worktree plugin.

**Core constraints:**

- Do not use or extend `FWidgetBlueprintGenerator`.
- Do not route AssetDocument work through `generate_assets`.
- Do not add WidgetBlueprint-specific MCP tools.
- Do not maintain a static widget type allowlist.
- Do not treat omitted managed WidgetBlueprint regions as "preserve current asset".
- Do not silently keep unsupported authored WidgetBlueprint content; fail validation/apply with explicit diagnostics.
- Keep user-facing docs/reports in Chinese; keep code identifiers and schema keys in English.

## Execution Setup

Run these commands before Task 1:

```powershell
Push-Location E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-structured-capabilities-spec
git status --short --branch
git rev-parse HEAD
git worktree add E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-widgetblueprint-impl -b feature/asset-document-widgetblueprint-impl HEAD
Pop-Location
Push-Location E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-widgetblueprint-impl
git status --short --branch
```

Expected:

- source branch is `feature/asset-document-structured-capabilities-spec`;
- implementation worktree branch is `feature/asset-document-widgetblueprint-impl`;
- status is clean before starting Task 1.

Before dispatching each implementation subagent, show the user this fork scope:

```text
worktree: E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-widgetblueprint-impl
branch: feature/asset-document-widgetblueprint-impl
base commit: record the output of `git rev-parse HEAD` as `TASK_BASE` at task start
diff range: TASK_BASE..HEAD
allowed write scope: files listed in the current task only
allowed read scope: task files, spec, deferred fields doc, AssetDocument guide, directly relevant UE headers
context inheritance: task acceptance criteria only, no full thread dump
```

## File Structure

Create WidgetBlueprint-specific files:

- `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.h/.cpp`
  - Exact profile for `/Script/UMGEditor.WidgetBlueprint`.
  - Owns document shape, template, body keys, RegionPolicy, and body adapter dispatch.
- `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.h/.cpp`
  - Orchestrates validation/preflight/apply/extract/diff across WidgetBlueprint body keys.
  - Owns parent class, palette, editor options, widget variable GUID map, and dispatch to sub-adapters.
- `Source/AssetDocument/Private/Profiles/WidgetBlueprintTreeAdapter.h/.cpp`
  - Parses, validates, materializes, extracts, and diffs `Body.WidgetTree`.
  - Contains dynamic widget class lookup, slot reflection, single-content detection, named slot binding handling, and widget variable exposure.
- `Source/AssetDocument/Private/Profiles/WidgetBlueprintBindingAdapter.h/.cpp`
  - Parses, validates, materializes, extracts, and diffs `Body.Bindings`.
  - Builds and validates `FDelegateEditorBinding` values.
- `Source/AssetDocument/Private/Profiles/WidgetBlueprintAnimationAdapter.h/.cpp`
  - Parses, validates, materializes, extracts, and diffs `Body.Animations`.
  - Owns package-local `UWidgetAnimation` and supported `UMovieScene` track/channel representation.
- `Source/AssetDocument/Private/Profiles/WidgetBlueprintGraphAdapter.h/.cpp`
  - Bridges WidgetBlueprint graph regions to the existing UBlueprint graph model and adds widget-specific graph support.
- `Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp`
  - Focused profile, lifecycle, WidgetTree, metadata, bindings, animations, sync, and smoke automation.

Modify integration files:

- `Source/AssetDocument/Private/AssetDocumentModule.cpp`
  - Register `FWidgetBlueprintAssetDocumentProfile`.
- `Source/AssetDocument/Private/AssetDocumentClassResolver.cpp`
  - Permit exact-profile resolution for `UWidgetBlueprint` while continuing to reject arbitrary Blueprint-derived asset classes in generic flow.
- `Source/AssetDocument/Private/AssetDocumentLifecycle.h/.cpp`
  - Add exact WidgetBlueprint lifecycle creation path using `UWidgetBlueprintFactory` or the editor API proven by tests.
- `Source/AssetDocument/Private/AssetDocumentRegionCanonicalizer.cpp`
  - Add canonicalizer hooks for WidgetTree, WidgetVariableGuids, Bindings, and Animations where sync writeback needs deterministic ordering/generated GUIDs.
- `Source/AssetDocument/AssetDocument.Build.cs`
  - Add dependencies required by UMG editor and MovieScene support: `UMG`, `UMGEditor`, `Slate`, `SlateCore`, `MovieScene`, `MovieSceneTracks`, `KismetCompiler` if missing.
- `MCP/schemas/AssetDocument.md`
  - Document WidgetBlueprint profile, body keys, and validation semantics.
- `docs/reports/asset-document-widgetblueprint-complete-region-benchmark.md`
  - Final completion and verification evidence report.

Do not modify:

- `Source/AssetFactory/Private/Generators/WidgetBlueprintGenerator.cpp`
- `Source/AssetFactory/Public/Generators/WidgetBlueprintGenerator.h`
- `MCP/schemas/WidgetBlueprint.md`

Those files are read-only historical references for this work.

---

### Task 1: Profile, Lifecycle, Schema, And Empty Asset Contract

**Files:**
- Create: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.h`
- Create: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.cpp`
- Create: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.h`
- Create: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentModule.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentClassResolver.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentLifecycle.h`
- Modify: `Source/AssetDocument/Private/AssetDocumentLifecycle.cpp`
- Modify: `Source/AssetDocument/AssetDocument.Build.cs`
- Modify: `MCP/schemas/AssetDocument.md`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
git status --short
```

Expected: clean status and non-empty `TASK_BASE`.

- [ ] **Step 2: Write failing profile and lifecycle tests**

Create `Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp` with initial automation tests:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentService.h"
#include "Profiles/WidgetBlueprintAssetDocumentProfile.h"

#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "WidgetBlueprint.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintProfileTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.Profile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintProfileTest::RunTest(const FString&)
{
	FWidgetBlueprintAssetDocumentProfile Profile;
	TestEqual(TEXT("Exact class is UWidgetBlueprint"), Profile.GetExactClass(), UWidgetBlueprint::StaticClass());

	const TArray<FName> BodyKeys = Profile.GetBodyKeys();
	TestTrue(TEXT("ParentClass body key is registered"), BodyKeys.Contains(TEXT("ParentClass")));
	TestTrue(TEXT("WidgetTree body key is registered"), BodyKeys.Contains(TEXT("WidgetTree")));
	TestTrue(TEXT("Bindings body key is registered"), BodyKeys.Contains(TEXT("Bindings")));
	TestTrue(TEXT("Animations body key is registered"), BodyKeys.Contains(TEXT("Animations")));
	TestTrue(TEXT("FunctionGraphs body key is registered"), BodyKeys.Contains(TEXT("FunctionGraphs")));
	TestTrue(TEXT("WidgetVariableGuids body key is registered"), BodyKeys.Contains(TEXT("WidgetVariableGuids")));

	FAssetDocumentTemplateContext Context;
	Context.Target = TEXT("/Game/AssetDocumentTests/WBP_Template");
	Context.ClassPath = TEXT("/Script/UMGEditor.WidgetBlueprint");
	TSharedRef<FJsonObject> Template = Profile.CreateTemplate(Context);

	TestEqual(TEXT("Template class is WidgetBlueprint"), Template->GetStringField(TEXT("Class")), FString(TEXT("/Script/UMGEditor.WidgetBlueprint")));
	const TSharedPtr<FJsonObject>* Body = nullptr;
	TestTrue(TEXT("Template contains Body"), Template->TryGetObjectField(TEXT("Body"), Body) && Body && Body->IsValid());
	if (Body && Body->IsValid())
	{
		TestTrue(TEXT("Body contains ParentClass"), (*Body)->HasField(TEXT("ParentClass")));
		TestTrue(TEXT("Body contains WidgetTree"), (*Body)->HasField(TEXT("WidgetTree")));
		TestTrue(TEXT("Body contains Bindings"), (*Body)->HasField(TEXT("Bindings")));
		TestTrue(TEXT("Body contains Animations"), (*Body)->HasField(TEXT("Animations")));
	}

	const TArray<FAssetDocumentRegionPolicy> Policies = Profile.GetRegionPolicies();
	auto HasPolicy = [&Policies](FName RegionId)
	{
		return Policies.ContainsByPredicate([RegionId](const FAssetDocumentRegionPolicy& Policy)
		{
			return Policy.RegionId == RegionId;
		});
	};
	TestTrue(TEXT("WidgetTree has region policy"), HasPolicy(TEXT("Body.WidgetTree")));
	TestTrue(TEXT("Bindings has region policy"), HasPolicy(TEXT("Body.Bindings")));
	TestTrue(TEXT("Animations has region policy"), HasPolicy(TEXT("Body.Animations")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentWidgetBlueprintRejectsNonUserWidgetParentTest,
	"AssetFactory.AssetDocument.WidgetBlueprint.RejectsNonUserWidgetParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentWidgetBlueprintRejectsNonUserWidgetParentTest::RunTest(const FString&)
{
	FWidgetBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UWidgetBlueprint::StaticClass();

	TSharedRef<FJsonObject> ParentClass = MakeShared<FJsonObject>();
	ParentClass->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ParentClass->SetStringField(TEXT("Class"), TEXT("/Script/Engine.Actor"));

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), ParentClass);
	Body->SetObjectField(TEXT("WidgetTree"), MakeShared<FJsonObject>());
	Body->SetArrayField(TEXT("Bindings"), {});
	Body->SetArrayField(TEXT("Animations"), {});

	const FAssetDocumentCapabilityResult Result = Capability.Validate(
		Context,
		StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueObject>(Body)));

	TestFalse(TEXT("Non-UUserWidget parent fails validation"), Result.bSuccess);
	TestTrue(TEXT("Diagnostic mentions ParentClass"), Result.Message.Contains(TEXT("ParentClass")));
	return true;
}

#endif
```

- [ ] **Step 3: Run failing test command**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
```

Expected: compile fails because `WidgetBlueprintAssetDocumentProfile.h` and `WidgetBlueprintAssetDocumentCapability.h` do not exist.

- [ ] **Step 4: Implement profile skeleton**

Create profile files with exact class, template body keys, region policies, and adapter resolution:

```cpp
// WidgetBlueprintAssetDocumentProfile.h
#pragma once

#include "AssetDocumentProfile.h"
#include "Profiles/WidgetBlueprintAssetDocumentCapability.h"

class FWidgetBlueprintAssetDocumentProfile final : public IAssetDocumentProfile
{
public:
	virtual UClass* GetExactClass() const override;
	virtual TSharedRef<FJsonObject> GetDocumentShape() const override;
	virtual TSharedRef<FJsonObject> CreateTemplate(const FAssetDocumentTemplateContext& Context) const override;
	virtual TArray<FName> GetBodyKeys() const override;
	virtual const IAssetDocumentCapability* ResolveBodyAdapter(FName BodyKey) const override;
	virtual TArray<FAssetDocumentRegionPolicy> GetRegionPolicies() const override;

private:
	FWidgetBlueprintAssetDocumentCapability BodyCapability;
};
```

Implementation requirements:

- `GetExactClass()` returns `UWidgetBlueprint::StaticClass()`.
- `CreateTemplate()` sets `Class` to `/Script/UMGEditor.WidgetBlueprint`, default parent to `/Script/UMG.UserWidget`, and initializes every Body key from the spec.
- `GetRegionPolicies()` creates policies for `Body.ParentClass`, `Body.ImplementedInterfaces`, `Body.Variables`, `Body.ClassDefaults`, `Body.WidgetTree`, `Body.Bindings`, `Body.Animations`, `Body.UbergraphPages`, `Body.FunctionGraphs`, `Body.MacroGraphs`, `Body.Palette`, `Body.EditorOptions`, and `Body.WidgetVariableGuids`.
- Graph policies reuse `CanonicalizerHookName = "UBlueprintGraph"` where applicable.

- [ ] **Step 5: Implement capability skeleton validation**

Create `FWidgetBlueprintAssetDocumentCapability` with:

- `GetCanonicalBodyKeys()`
- `SupportsAsset()`
- `SupportsClass()`
- `GetSchemaHint()`
- `Validate()`
- `Preflight()`
- `Apply()`
- `Extract()`
- `Diff()`

Task 1 behavior:

- `Validate()` accepts empty/default `WidgetTree`, empty `Bindings`, empty `Animations`, empty graph arrays, `Palette`, `EditorOptions`, and `WidgetVariableGuids`.
- `Validate()` rejects unknown Body keys.
- `Validate()` rejects `Body.ParentClass` if it is not a `ClassRef` resolving to a `UUserWidget` subclass.
- `Apply()` may only support empty body creation in Task 1 and must return explicit `UnsupportedWidgetBlueprintRegion` for non-empty region content not implemented yet.
- `Extract()` must emit canonical empty/default body for an empty WidgetBlueprint asset.
- `Diff()` must compare canonical extracted body to desired JSON and return changed entries for mismatched top-level regions.

- [ ] **Step 6: Add lifecycle support**

Modify `AssetDocumentLifecycle` so exact `UWidgetBlueprint::StaticClass()` creation:

- resolves `Body.ParentClass.Class`;
- validates it is a `UUserWidget` subclass;
- creates a WidgetBlueprint using an editor-supported WidgetBlueprint factory/API;
- compiles/saves the created asset;
- refuses to treat `UWidgetBlueprint` as generic `UObject` creation.

Modify `AssetDocumentClassResolver` so exact-profile WidgetBlueprint is allowed, while arbitrary Blueprint-derived asset classes still require exact profiles.

- [ ] **Step 7: Register module and schema**

Modify `AssetDocumentModule.cpp`:

```cpp
#include "Profiles/WidgetBlueprintAssetDocumentProfile.h"
...
FAssetDocumentService::GetProfileRegistry().Register(MakeShared<FWidgetBlueprintAssetDocumentProfile>());
```

Modify `AssetDocument.Build.cs` to add missing dependencies for includes used in this task. Use only modules needed by compilation.

Modify `MCP/schemas/AssetDocument.md` to list `/Script/UMGEditor.WidgetBlueprint`, body keys, and the rule that it is AssetDocument-only.

- [ ] **Step 8: Verify Task 1**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint.Profile;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/WidgetBlueprintProfile"
```

Expected: UBT succeeds and profile automation succeeds.

- [ ] **Step 9: Commit Task 1**

```powershell
git status --short
git add Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.* Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.* Source/AssetDocument/Private/AssetDocumentModule.cpp Source/AssetDocument/Private/AssetDocumentClassResolver.cpp Source/AssetDocument/Private/AssetDocumentLifecycle.* Source/AssetDocument/AssetDocument.Build.cs Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp MCP/schemas/AssetDocument.md
git commit -m "feat(assetdoc): add widgetblueprint profile skeleton"
```

Review scope: `TASK_BASE..HEAD`, files listed in Task 1 only.

---

### Task 2: WidgetTree Authoritative Rebuild

**Files:**
- Create: `Source/AssetDocument/Private/Profiles/WidgetBlueprintTreeAdapter.h`
- Create: `Source/AssetDocument/Private/Profiles/WidgetBlueprintTreeAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.h`
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
git status --short
```

Expected: clean status.

- [ ] **Step 2: Add failing WidgetTree tests**

Append tests named:

- `AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree.CreateExtractDiff`
- `AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree.OmittedChildDeletes`
- `AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree.RestoresOmittedProperties`
- `AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree.NamedSlotBindings`
- `AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree.RejectsInvalidClass`
- `AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree.RejectsDuplicateNames`

Use helper builders in the test file:

```cpp
static TSharedRef<FJsonObject> MakeClassRef(const FString& ClassPath);
static TSharedPtr<FJsonObject> MakeWidgetBlueprintDocument(const FString& Target, TSharedPtr<FJsonObject> Body);
static TSharedRef<FJsonObject> MakeWidgetNode(const FString& Name, const FString& ClassPath);
static FAssetDocumentApplyRequest MakeApplyFileRequest(TSharedPtr<FJsonObject> Document);
```

The create/extract/diff test must build a `CanvasPanel` root with a `TextBlock` child named `TitleText`, apply it, load the `UWidgetBlueprint`, assert `WidgetTree->FindWidget("TitleText") != nullptr`, extract, and diff expected unchanged.

- [ ] **Step 3: Run failing WidgetTree tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/WidgetBlueprintWidgetTreeRed"
```

Expected: tests fail because `Body.WidgetTree` only accepts empty/default body.

- [ ] **Step 4: Implement WidgetTree parser**

Create `FWidgetBlueprintTreeSpec` types in the adapter implementation:

```cpp
struct FWidgetBlueprintNodeSpec
{
	FName Name;
	FString Class;
	bool bIsVariable = false;
	TOptional<FName> VariableName;
	TSharedPtr<FJsonObject> Properties;
	TSharedPtr<FJsonObject> Slot;
	TArray<FWidgetBlueprintNodeSpec> Children;
};

struct FWidgetBlueprintTreeSpec
{
	TOptional<FWidgetBlueprintNodeSpec> RootWidget;
	TMap<FName, FWidgetBlueprintNodeSpec> NamedSlotBindings;
};
```

Parser behavior:

- `WidgetTree` must be object.
- `RootWidget` may be null or object.
- `NamedSlotBindings` must be object mapping slot names to widget nodes.
- Every widget node requires non-empty `Name` and `Class`.
- Names must be unique across root tree and named slot content.
- `Children` defaults to empty array.
- `Properties` and `Slot` must be objects when present.

- [ ] **Step 5: Implement WidgetTree materialization**

Adapter behavior:

- Resolve classes with `StaticLoadClass(UWidget::StaticClass(), nullptr, *ClassPath)` first, then existing class finder utilities using `UWidget::StaticClass()` as base.
- Use `WidgetTree->ConstructWidget<UWidget>(ResolvedClass, WidgetName)`.
- For panel widgets, add children through `UPanelWidget::AddChild`.
- For single-content widgets, detect content setter support through reflection/function lookup or `UContentWidget`/`INamedSlotInterface`.
- Apply slot properties after insertion using `FAssetDocumentPropertyAdapter` or a local target-property-aware reflection helper.
- Apply widget `Properties` through reflected property adapter; invalid properties fail preflight.
- Mark exposed variables through WidgetBlueprint editor utilities and update variable GUID map in Task 3.
- Apply atomically: build a replacement tree in memory, validate classes/properties/relationships first, then replace `RootWidget` and named slot bindings.

- [ ] **Step 6: Implement WidgetTree extraction and diff**

Extraction behavior:

- Traverse `WidgetTree->RootWidget`.
- Emit canonical `Name`, `Class`, `IsVariable`, `VariableName`, `Properties`, `Slot`, and `Children`.
- Sort named slot bindings by slot name.
- Omit default widget and slot properties according to default reducer rules.

Diff behavior:

- Extract current canonical `Body.WidgetTree`.
- Compare desired vs current recursively.
- Report changed entries with paths such as `/Body/WidgetTree/RootWidget/Children/TitleText/Properties/Text`.

- [ ] **Step 7: Verify Task 2**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint.WidgetTree;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/WidgetBlueprintWidgetTreeGreen"
```

Expected: WidgetTree tests pass.

- [ ] **Step 8: Commit Task 2**

```powershell
git add Source/AssetDocument/Private/Profiles/WidgetBlueprintTreeAdapter.* Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.* Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.cpp Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp
git commit -m "feat(assetdoc): materialize widgetblueprint widget tree"
```

Review scope: `TASK_BASE..HEAD`, files listed in Task 2 only.

---

### Task 3: Variables, Class Defaults, Palette, Editor Options, And Widget Variable GUIDs

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.h`
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintTreeAdapter.h`
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintTreeAdapter.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentRegionCanonicalizer.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
git status --short
```

- [ ] **Step 2: Add failing metadata/default tests**

Add tests:

- `AssetFactory.AssetDocument.WidgetBlueprint.Metadata.ClassDefaultsAuthoritative`
- `AssetFactory.AssetDocument.WidgetBlueprint.Metadata.PaletteCategoryRoundTrip`
- `AssetFactory.AssetDocument.WidgetBlueprint.Metadata.EditorOptionsRoundTrip`
- `AssetFactory.AssetDocument.WidgetBlueprint.Metadata.WidgetVariableGuidCanonicalizes`
- `AssetFactory.AssetDocument.WidgetBlueprint.Metadata.RejectsVariableWidgetNameConflict`

Test assertions:

- CDO property apply/extract/diff roundtrips for stable `UUserWidget` editable properties such as `bIsFocusable` when available.
- `Palette.Category` updates extracted category.
- `EditorOptions.bCanCallInitializedWithoutPlayerContext` applies/extracts.
- missing `WidgetVariableGuids` for variable widgets triggers deterministic generated GUID writeback in extracted/canonical sidecar.
- manual `Variables` entry with same name as `WidgetTree` variable fails validation.

- [ ] **Step 3: Run failing metadata tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint.Metadata;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/WidgetBlueprintMetadataRed"
```

Expected: tests fail before implementation.

- [ ] **Step 4: Implement metadata/default regions**

Implementation behavior:

- Reuse UBlueprint variable/class default parsing where signatures align.
- Apply `Body.ClassDefaults` to WidgetBlueprint generated CDO after compile creates a generated class.
- Apply `Body.Palette.Category` to the authoritative palette source and mirror `PaletteCategory`.
- Apply `Body.EditorOptions.bCanCallInitializedWithoutPlayerContext` directly on `UWidgetBlueprint`.
- Build `Body.WidgetVariableGuids` from `WidgetVariableNameToGuidMap`.
- For missing GUID entries, generate deterministic GUIDs based on target path, variable name, and region kind; canonical sync writeback must output them.
- Reject conflicts between explicit `Body.Variables[].Name` and variable widget names from `Body.WidgetTree`.

- [ ] **Step 5: Add canonicalizer support**

Modify `AssetDocumentRegionCanonicalizer.cpp`:

- add `WidgetBlueprintWidgetVariableGuids` strategy;
- sort GUID map by variable name;
- remove generated/extract-only fields from hash;
- wire the strategy from `FWidgetBlueprintAssetDocumentProfile::GetRegionPolicies()`.

- [ ] **Step 6: Verify Task 3**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint.Metadata;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/WidgetBlueprintMetadataGreen"
```

Expected: metadata/default tests pass.

- [ ] **Step 7: Commit Task 3**

```powershell
git add Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.* Source/AssetDocument/Private/Profiles/WidgetBlueprintTreeAdapter.* Source/AssetDocument/Private/AssetDocumentRegionCanonicalizer.cpp Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp
git commit -m "feat(assetdoc): sync widgetblueprint metadata regions"
```

Review scope: `TASK_BASE..HEAD`, files listed in Task 3 only.

---

### Task 4: UMG Property Bindings

**Files:**
- Create: `Source/AssetDocument/Private/Profiles/WidgetBlueprintBindingAdapter.h`
- Create: `Source/AssetDocument/Private/Profiles/WidgetBlueprintBindingAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.h`
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
git status --short
```

- [ ] **Step 2: Add failing binding tests**

Add tests:

- `AssetFactory.AssetDocument.WidgetBlueprint.Bindings.FunctionRoundTrip`
- `AssetFactory.AssetDocument.WidgetBlueprint.Bindings.PropertyRoundTrip`
- `AssetFactory.AssetDocument.WidgetBlueprint.Bindings.OmissionRemovesBinding`
- `AssetFactory.AssetDocument.WidgetBlueprint.Bindings.RejectsMissingWidget`
- `AssetFactory.AssetDocument.WidgetBlueprint.Bindings.RejectsInvalidFunctionSignature`
- `AssetFactory.AssetDocument.WidgetBlueprint.Bindings.RejectsDuplicateTarget`

Test data:

- create a WBP parented to `UTestUserWidget` or an equivalent test user widget class that has known pure/const functions;
- create a `TextBlock` named `TitleText`;
- bind `Text` to a pure function returning `FText`;
- bind a second property through `SourcePath` when a compatible source property exists.

- [ ] **Step 3: Run failing binding tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint.Bindings;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/WidgetBlueprintBindingsRed"
```

- [ ] **Step 4: Implement binding adapter**

Adapter public types:

```cpp
struct FWidgetBlueprintBindingSpec
{
	FName Widget;
	FName Property;
	FString Kind;
	TOptional<FName> Function;
	TArray<FEditorPropertyPathSegment> SourcePath;
	TOptional<FGuid> MemberGuid;
};
```

Behavior:

- Parse `Body.Bindings` as array.
- Identity is `{Widget, Property}`.
- Reject duplicate identities.
- Resolve target widget through `UWidgetTree::FindWidget`.
- Resolve target delegate property by looking for `PropertyNameDelegate` or UE-supported binding metadata.
- For `Kind = Function`, resolve function graph or parent class function and validate signature through `FDelegateEditorBinding::IsBindingValid`.
- For `Kind = Property`, build `FEditorPropertyPath` and validate it against the destination delegate.
- Apply by replacing `UWidgetBlueprint::Bindings` with canonical array.
- Extract `UWidgetBlueprint::Bindings` into canonical `Body.Bindings`, preferring `SourcePath` over legacy top-level binding shape.
- Diff compares sorted binding array by `{Widget, Property}`.

- [ ] **Step 5: Verify Task 4**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint.Bindings;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/WidgetBlueprintBindingsGreen"
```

- [ ] **Step 6: Commit Task 4**

```powershell
git add Source/AssetDocument/Private/Profiles/WidgetBlueprintBindingAdapter.* Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.* Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.cpp Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp
git commit -m "feat(assetdoc): materialize widgetblueprint bindings"
```

Review scope: `TASK_BASE..HEAD`, files listed in Task 4 only.

---

### Task 5: WidgetBlueprint Graph Regions

**Files:**
- Create: `Source/AssetDocument/Private/Profiles/WidgetBlueprintGraphAdapter.h`
- Create: `Source/AssetDocument/Private/Profiles/WidgetBlueprintGraphAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Graphs/K2GraphAdapter.h`
- Modify: `Source/AssetDocument/Private/Graphs/K2GraphAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Graphs/AssetDocumentNodeAdapter.h`
- Modify: `Source/AssetDocument/Private/Graphs/AssetDocumentNodeAdapter.cpp`
- Create: `Source/AssetDocument/Private/Graphs/K2NodeAdapters/WidgetAnimationEventNodeAdapter.h`
- Create: `Source/AssetDocument/Private/Graphs/K2NodeAdapters/WidgetAnimationEventNodeAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.*`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentGraphCoreTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
git status --short
```

- [ ] **Step 2: Add failing graph tests**

Add tests:

- `AssetFactory.AssetDocument.WidgetBlueprint.Graphs.FunctionGraphForBindingRoundTrip`
- `AssetFactory.AssetDocument.WidgetBlueprint.Graphs.EventGraphRoundTrip`
- `AssetFactory.AssetDocument.WidgetBlueprint.Graphs.AnimationEventNodeRoundTrip`
- `AssetFactory.AssetDocument.WidgetBlueprint.Graphs.UnsupportedNodeRejectsApply`
- `AssetFactory.AssetDocument.WidgetBlueprint.Graphs.ExtractReportsUnsupportedNodes`

Acceptance:

- simple function graph used by binding applies and extracts;
- event/call/self/variable nodes continue to roundtrip using existing adapters;
- widget animation event node has adapter or explicit unsupported diagnostic;
- unsupported authored node fails preflight before deleting existing graph nodes.

- [ ] **Step 3: Run failing graph tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint.Graphs;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/WidgetBlueprintGraphsRed"
```

- [ ] **Step 4: Bridge WidgetBlueprint to graph core**

Behavior:

- Use current UBlueprint graph parser/diff shape for `UbergraphPages`, `FunctionGraphs`, and `MacroGraphs`.
- Accept graph schema `/Script/UMGEditor.WidgetGraphSchema` and standard K2 schema where UE stores it.
- Pass `UWidgetBlueprint` as `UBlueprint*` to existing graph adapters where valid.
- Keep unsupported node diagnostics payload compatible with UBlueprint graph diagnostics.
- Do not permit graph apply to remove existing unsupported concrete nodes unless the concrete existing node is supported by adapter preflight.

- [ ] **Step 5: Add widget-specific node adapters**

Implement `WidgetAnimationEventNodeAdapter` if tests require authored animation events:

- public capability string: `WidgetAnimationEvent`;
- member fields: animation name, event type `Started` or `Finished`;
- validate animation exists in `Body.Animations`;
- extract node member data from `UK2Node_WidgetAnimationEvent`.

- [ ] **Step 6: Verify Task 5**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint.Graphs;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/WidgetBlueprintGraphsGreen"
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.GraphCore;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/WidgetBlueprintGraphCore"
```

- [ ] **Step 7: Commit Task 5**

```powershell
git add Source/AssetDocument/Private/Profiles/WidgetBlueprintGraphAdapter.* Source/AssetDocument/Private/Graphs Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.* Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp Source/AssetDocument/Private/Tests/AssetDocumentGraphCoreTests.cpp
git commit -m "feat(assetdoc): support widgetblueprint graph regions"
```

Review scope: `TASK_BASE..HEAD`, files listed in Task 5 only.

---

### Task 6: Widget Animations

**Files:**
- Create: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAnimationAdapter.h`
- Create: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAnimationAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.*`
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentRegionCanonicalizer.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
git status --short
```

- [ ] **Step 2: Add failing animation tests**

Add tests:

- `AssetFactory.AssetDocument.WidgetBlueprint.Animations.CreateFloatTrackRoundTrip`
- `AssetFactory.AssetDocument.WidgetBlueprint.Animations.CreateTransformTrackRoundTrip`
- `AssetFactory.AssetDocument.WidgetBlueprint.Animations.OmissionDeletesAnimation`
- `AssetFactory.AssetDocument.WidgetBlueprint.Animations.OmissionDeletesTrackAndKey`
- `AssetFactory.AssetDocument.WidgetBlueprint.Animations.RejectsMissingWidgetBinding`
- `AssetFactory.AssetDocument.WidgetBlueprint.Animations.UnsupportedTrackBlocksCompleteDiff`

Acceptance:

- create an animation named `Intro`;
- bind it to widget `TitleText`;
- materialize a float key track for `RenderOpacity`;
- materialize a transform or color/visibility equivalent track supported by UE APIs;
- extract canonical animation body;
- diff unchanged after apply;
- omission deletes animation/tracks/keys authoritatively.

- [ ] **Step 3: Run failing animation tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint.Animations;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/WidgetBlueprintAnimationsRed"
```

- [ ] **Step 4: Implement animation adapter**

Adapter behavior:

- Parse `Body.Animations` as array keyed by animation `Name`.
- Create package-owned `UWidgetAnimation` objects under the WidgetBlueprint.
- Create or rebuild `UMovieScene` for each animation.
- Apply playback range and frame rate.
- Create widget object bindings by widget name.
- Support float property tracks and at least one additional common UMG property track family, such as transform, color, or visibility, based on stable UE MovieScene API availability.
- Extract supported tracks/channels/keys into canonical JSON.
- Diff extracted current vs desired.
- Reject authored unsupported track types with `UnsupportedWidgetAnimationTrack`.
- For existing unsupported tracks during extract, emit explicit diagnostic and mark final completeness blocked until track is implemented or user approves deferral.

- [ ] **Step 5: Add animation canonicalization**

Modify `AssetDocumentRegionCanonicalizer.cpp`:

- sort animations by name;
- sort bindings by widget name;
- sort tracks by target widget/property/type;
- sort keys by frame;
- ignore extract-only diagnostics in hash.

- [ ] **Step 6: Verify Task 6**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint.Animations;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/WidgetBlueprintAnimationsGreen"
```

- [ ] **Step 7: Commit Task 6**

```powershell
git add Source/AssetDocument/Private/Profiles/WidgetBlueprintAnimationAdapter.* Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.* Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.cpp Source/AssetDocument/Private/AssetDocumentRegionCanonicalizer.cpp Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp
git commit -m "feat(assetdoc): materialize widgetblueprint animations"
```

Review scope: `TASK_BASE..HEAD`, files listed in Task 6 only.

---

### Task 7: Integration, Sync, MCP, External Smoke, And Final Report

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentRegionCanonicalizer.cpp`
- Modify: `MCP/schemas/AssetDocument.md`
- Modify: `MCP/src/index.ts` only if profile/schema text exposed to MCP needs updating
- Create: `docs/superpowers/verification/run_asset_document_widgetblueprint_smoke.ps1`
- Create: `docs/reports/asset-document-widgetblueprint-complete-region-benchmark.md`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp`
- Test: `MCP/src/__tests__` files if MCP schema/profile tests are present

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
git status --short
```

- [ ] **Step 2: Add failing integration and smoke tests**

Add tests:

- `AssetFactory.AssetDocument.WidgetBlueprint.FullApplyExtractDiff`
- `AssetFactory.AssetDocument.WidgetBlueprint.ApplyFileCanonicalWriteback`
- `AssetFactory.AssetDocument.WidgetBlueprint.OmittedRegionsAreAuthoritative`
- `AssetFactory.AssetDocument.WidgetBlueprint.ApplyFailureRollsBack`
- `AssetFactory.AssetDocument.WidgetBlueprint.ProfileInspectionListsAllRegions`

MCP tests must verify:

- profile/schema output lists `/Script/UMGEditor.WidgetBlueprint`;
- `Body.WidgetTree`, `Body.Bindings`, `Body.Animations`, graph regions, metadata regions are documented;
- generator-only `AssetType: "WidgetBlueprint"` is not presented as AssetDocument input.

- [ ] **Step 3: Implement external smoke script**

Create `docs/superpowers/verification/run_asset_document_widgetblueprint_smoke.ps1` with parameters:

```powershell
param(
  [string]$Project = "C:/AVH1/AVH1.uproject",
  [string]$Host = "127.0.0.1",
  [int]$Port = 8559,
  [switch]$KeepSidecar
)
```

Script behavior:

- writes `C:/AVH1/Content/AssetDocumentSmoke/WBP_WidgetBlueprintSmoke.assetdoc.json`;
- includes ParentClass, WidgetTree, ClassDefaults, Bindings, at least one FunctionGraph, one Animation, Palette, EditorOptions, WidgetVariableGuids omitted for canonical writeback;
- calls HTTP `apply-file`;
- calls HTTP `extract`;
- calls HTTP `diff`;
- fails non-zero if apply fails, extract misses any expected region, or diff reports unexpected changed entries;
- preserves sidecar when `-KeepSidecar` is supplied.

- [ ] **Step 4: Run full verification**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/WidgetBlueprintFull"
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/UBlueprintRegression"
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.GraphCore;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/GraphCoreRegression"
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AssetDocumentFull"
Push-Location MCP
npm test
Pop-Location
```

Expected:

- UBT succeeds.
- `AssetFactory.AssetDocument.WidgetBlueprint` succeeds.
- `AssetFactory.AssetDocument.UBlueprint` succeeds.
- `AssetFactory.AssetDocument.GraphCore` succeeds.
- full `AssetFactory.AssetDocument` succeeds.
- `npm test` succeeds.

- [ ] **Step 5: Run external smoke**

Start or reuse a real editor for `C:/AVH1/AVH1.uproject`, wait for the AssetFactory HTTP server on port `8559`, then run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File docs/superpowers/verification/run_asset_document_widgetblueprint_smoke.ps1 -Project C:/AVH1/AVH1.uproject -KeepSidecar
```

Expected:

- created or updated asset: `/Game/AssetDocumentSmoke/WBP_WidgetBlueprintSmoke`;
- preserved sidecar: `C:/AVH1/Content/AssetDocumentSmoke/WBP_WidgetBlueprintSmoke.assetdoc.json`;
- apply-file succeeds;
- extract returns all WidgetBlueprint body regions;
- diff has no unexpected changed entries.

- [ ] **Step 6: Write final report**

Create `docs/reports/asset-document-widgetblueprint-complete-region-benchmark.md` with:

- worktree, branch, base commit, reviewed range;
- completed regions;
- excluded derived/cache fields;
- deferred fields and their final status;
- UBT result;
- focused WidgetBlueprint automation result;
- UBlueprint and GraphCore regression result;
- full AssetDocument automation result;
- MCP test result;
- external smoke result;
- real asset path and sidecar path;
- reviewer findings and fixes.

- [ ] **Step 7: Commit Task 7**

```powershell
git add Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentProfile.cpp Source/AssetDocument/Private/Profiles/WidgetBlueprintAssetDocumentCapability.cpp Source/AssetDocument/Private/AssetDocumentRegionCanonicalizer.cpp Source/AssetDocument/Private/Tests/AssetDocumentWidgetBlueprintTests.cpp MCP/schemas/AssetDocument.md MCP/src/index.ts docs/superpowers/verification/run_asset_document_widgetblueprint_smoke.ps1 docs/reports/asset-document-widgetblueprint-complete-region-benchmark.md
git commit -m "test(assetdoc): verify widgetblueprint asset document flow"
```

Review scope: `TASK_BASE..HEAD`, files listed in Task 7 only.

---

## Final Review And Integration

After Task 7:

1. Run `git status --short`.
2. Record `SPEC_BASE` as the merge-base with `feature/asset-document-structured-capabilities-spec`.
3. Run a spec final review over `SPEC_BASE..HEAD`.
4. Run a code quality review over `SPEC_BASE..HEAD`.
5. Fix review findings with narrow commits.
6. Run final UBT, focused WidgetBlueprint automation, full AssetDocument automation, MCP tests, and external smoke again.
7. Merge back into `feature/asset-document-structured-capabilities-spec` only after confirming ancestry and user approval.

Final commands:

```powershell
git merge-base feature/asset-document-structured-capabilities-spec HEAD
git log --oneline feature/asset-document-structured-capabilities-spec..HEAD
git status --short
```

Do not push, publish, or open a PR unless the user explicitly asks.
