# BehaviorTree + BlackboardData AssetDocument Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement full-surface AssetDocument support for `/Script/AIModule.BlackboardData` and `/Script/AIModule.BehaviorTree`, including Blackboard keys, BT semantic tree, dynamic task/composite/decorator/service materialization, editor layout, extract/diff, validation, and verification.

**Architecture:** Add exact AssetDocument profiles for `UBlackboardData` and `UBehaviorTree` using `FAssetDocumentBodyRegionDispatcher` and public region adapters. Blackboard keys are handled by a reusable key schema utility and named-array adapter; BehaviorTree semantic structure is handled by a reusable tree adapter plus a BT-specific materializer that uses dynamic class loading and reflection instead of node class switches. Editor layout is a separate `Body.EditorLayout` region applied after BT graph rebuild and diffed independently from runtime tree semantics.

**Tech Stack:** Unreal Engine 5.7 C++ editor modules, AssetDocument public region runtime, `AIModule`, `BehaviorTreeEditor`, `AIGraph`, `FClassFinderUtils`, `FPropertySetterUtils`, Automation Tests, UBT, MCP tests.

---

## Baseline And Branch

- Implementation worktree: `E:\GameDev\PluginsWarehouse\.worktrees\UECopilot\asset-document-behaviortree-blackboard-impl`
- Branch: `feature/asset-document-behaviortree-blackboard-impl`
- Base/spec commit: `c563b6bd5077c90897ab28cb118b9afb39e5fc8b`
- Spec: `docs/superpowers/specs/2026-07-02-behaviortree-blackboard-asset-document-design.md`
- Scope mode: full-surface. Milestones are execution order only; they do not reduce completion scope.

Each task starts by recording:

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short --branch
```

Each task ends with a checkpoint commit and review range:

```powershell
git log --oneline "$env:TASK_BASE..HEAD"
```

## File Map

### Build And Registration

- Modify: `Source/AssetDocument/AssetDocument.Build.cs`
  - Add `AIModule`, `GameplayTags`, `BehaviorTreeEditor`, and `AIGraph` private dependencies needed for BT/BB runtime and editor graph layout.
- Modify: `Source/AssetDocument/Private/AssetDocumentModule.cpp`
  - Register `FBlackboardDataAssetDocumentProfile` and `FBehaviorTreeAssetDocumentProfile`.
- Modify: `Source/AssetDocument/Private/AssetDocumentLifecycle.h`
- Modify: `Source/AssetDocument/Private/AssetDocumentLifecycle.cpp`
  - Add exact class create paths for `UBlackboardData` and `UBehaviorTree`.
- Modify: `Source/AssetDocument/Private/AssetDocumentClassResolver.cpp`
  - Ensure exact BT/BB classes are accepted for structured AssetDocument use.

### Public-ish Region And Utility Layer

- Create: `Source/AssetDocument/Private/Regions/AssetDocumentBlackboardKeySchemaUtils.h`
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentBlackboardKeySchemaUtils.cpp`
  - Parse/canonicalize BB key JSON, resolve `UBlackboardKeyType` classes dynamically, validate metadata, apply/extract key type metadata, lookup keys through parent chain.
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentBlackboardKeyRegionAdapter.h`
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentBlackboardKeyRegionAdapter.cpp`
  - Named array region adapter for `Body.Keys`, identity `Name`.
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentTreeRegionAdapter.h`
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentTreeRegionAdapter.cpp`
  - Generic tree JSON shape validation, identity collection, stable diff path helpers, recursive adapter hooks.
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentEditorLayoutRegionAdapter.h`
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentEditorLayoutRegionAdapter.cpp`
  - Optional editor presentation adapter keyed by semantic node ids.
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentReflectedPropertyUtils.h`
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentReflectedPropertyUtils.cpp`
  - Shared wrapper around `FPropertySetterUtils` plus extraction/diff support for authored editable reflected properties and special `FBlackboardKeySelector`.

### Profiles

- Create: `Source/AssetDocument/Private/Profiles/BlackboardDataAssetDocumentProfile.h`
- Create: `Source/AssetDocument/Private/Profiles/BlackboardDataAssetDocumentProfile.cpp`
  - Exact profile, template, policies, body dispatcher, `Body.Parent`, `Body.Keys`.
- Create: `Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentProfile.h`
- Create: `Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentProfile.cpp`
  - Exact profile, template, policies, body dispatcher, `Body.Blackboard`, `Body.Tree`, `Body.EditorLayout`.
- Create: `Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentMaterializer.h`
- Create: `Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentMaterializer.cpp`
  - BT-specific hooks for dynamic node class materialization, root decorators, edge decorators, decorator logic, services, graph rebuild, editor layout mapping.

### Tests And Evidence

- Create: `Source/AssetDocument/Private/Tests/AssetDocumentBlackboardDataTests.cpp`
- Create: `Source/AssetDocument/Private/Tests/AssetDocumentBehaviorTreeTests.cpp`
- Create: `docs/reports/asset-document-behaviortree-blackboard-complete-region-benchmark.md`
- Optionally create: `docs/superpowers/specs/asset-document-deferred-fields/2026-07-02-behaviortree-blackboard.md`
  - Only for excluded non-authored/runtime/editor-cache fields and user-approved blockers. Do not use it to shrink managed authored scope.

## Task 1: Build Dependencies, Registration Skeleton, And Failing Profile Tests

**Files:**
- Modify: `Source/AssetDocument/AssetDocument.Build.cs`
- Modify: `Source/AssetDocument/Private/AssetDocumentModule.cpp`
- Create: `Source/AssetDocument/Private/Profiles/BlackboardDataAssetDocumentProfile.h`
- Create: `Source/AssetDocument/Private/Profiles/BlackboardDataAssetDocumentProfile.cpp`
- Create: `Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentProfile.h`
- Create: `Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentProfile.cpp`
- Create: `Source/AssetDocument/Private/Tests/AssetDocumentBlackboardDataTests.cpp`
- Create: `Source/AssetDocument/Private/Tests/AssetDocumentBehaviorTreeTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short --branch
```

Expected: clean `feature/asset-document-behaviortree-blackboard-impl`.

- [ ] **Step 2: Add module dependencies**

In `Source/AssetDocument/AssetDocument.Build.cs`, add to `PrivateDependencyModuleNames`:

```csharp
"AIModule",
"GameplayTags",
"BehaviorTreeEditor",
"AIGraph",
```

Expected: `AssetDocument` can include BT/BB runtime and editor graph headers.

- [ ] **Step 3: Add profile class skeletons**

Create profile headers with exact classes:

```cpp
class FBlackboardDataAssetDocumentProfile final : public IAssetDocumentProfile
{
public:
	static FName ParentRegionAdapterName();
	static FName KeysRegionAdapterName();
	static TArray<FAssetDocumentRegionBinding> MakeRegionBindings();

	virtual UClass* GetExactClass() const override;
	virtual TSharedRef<FJsonObject> GetDocumentShape() const override;
	virtual TSharedRef<FJsonObject> CreateTemplate(const FAssetDocumentTemplateContext& Context) const override;
	virtual TArray<FName> GetBodyKeys() const override;
	virtual const IAssetDocumentCapability* ResolveBodyAdapter(FName BodyKey) const override;
	virtual TArray<FAssetDocumentRegionPolicy> GetRegionPolicies() const override;

private:
	FBlackboardDataAssetDocumentCapability BodyCapability;
};
```

```cpp
class FBehaviorTreeAssetDocumentProfile final : public IAssetDocumentProfile
{
public:
	static FName BlackboardRegionAdapterName();
	static FName TreeRegionAdapterName();
	static FName EditorLayoutRegionAdapterName();
	static TArray<FAssetDocumentRegionBinding> MakeRegionBindings();

	virtual UClass* GetExactClass() const override;
	virtual TSharedRef<FJsonObject> GetDocumentShape() const override;
	virtual TSharedRef<FJsonObject> CreateTemplate(const FAssetDocumentTemplateContext& Context) const override;
	virtual TArray<FName> GetBodyKeys() const override;
	virtual const IAssetDocumentCapability* ResolveBodyAdapter(FName BodyKey) const override;
	virtual TArray<FAssetDocumentRegionPolicy> GetRegionPolicies() const override;

private:
	FBehaviorTreeAssetDocumentCapability BodyCapability;
};
```

If capability types are not ready yet, implement minimal `IAssetDocumentCapability` classes in the same `.cpp` files that return exact `UnknownBodyKey`/`NotImplemented` failures. This task intentionally creates failing functional tests but should compile.

- [ ] **Step 4: Register skeleton profiles**

In `Source/AssetDocument/Private/AssetDocumentModule.cpp`, include the new headers and register profiles after existing profiles:

```cpp
#include "Profiles/BehaviorTreeAssetDocumentProfile.h"
#include "Profiles/BlackboardDataAssetDocumentProfile.h"
```

```cpp
FAssetDocumentService::GetProfileRegistry().Register(MakeShared<FBlackboardDataAssetDocumentProfile>());
FAssetDocumentService::GetProfileRegistry().Register(MakeShared<FBehaviorTreeAssetDocumentProfile>());
```

- [ ] **Step 5: Add failing profile tests**

In `AssetDocumentBlackboardDataTests.cpp`, add tests:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBlackboardDataProfileShapeTest,
	"AssetFactory.AssetDocument.BlackboardData.ProfileShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
```

Assertions:

- `/Script/AIModule.BlackboardData` resolves to a registered profile.
- Template `Class` is `/Script/AIModule.BlackboardData`.
- `Body` contains `Parent` and `Keys`.
- `GetRegionPolicies()` contains `Body.Parent` and `Body.Keys`.

In `AssetDocumentBehaviorTreeTests.cpp`, add tests:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeProfileShapeTest,
	"AssetFactory.AssetDocument.BehaviorTree.ProfileShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
```

Assertions:

- `/Script/AIModule.BehaviorTree` resolves to a registered profile.
- Template `Class` is `/Script/AIModule.BehaviorTree`.
- `Body` contains `Blackboard`, `Tree`, and `EditorLayout`.
- `Tree` contains `RootDecorators`, `RootDecoratorLogic`, and `Root`.
- `GetRegionPolicies()` contains `Body.Blackboard`, `Body.Tree`, and `Body.EditorLayout`.

- [ ] **Step 6: Run focused tests to verify current failure shape**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<validation-host>.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl "-ExecCmds=Automation RunTests AssetFactory.AssetDocument.BlackboardData.ProfileShape; Automation RunTests AssetFactory.AssetDocument.BehaviorTree.ProfileShape; Quit"
```

Expected before full implementation: compile may pass, profile tests may fail until skeletons are complete. If no validation host exists yet, record this as blocked evidence and run UBT in Step 7.

- [ ] **Step 7: Run UBT compile**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/Users/HP/.config/superpowers/validation-hosts/<bt-bb-host>/AVH1.uproject" -NoHotReload
```

Expected: compile succeeds. If validation host is missing, create it following repo `AGENTS.md` and record the host path in the task notes.

- [ ] **Step 8: Commit**

```powershell
git add Source/AssetDocument/AssetDocument.Build.cs Source/AssetDocument/Private/AssetDocumentModule.cpp Source/AssetDocument/Private/Profiles/BlackboardDataAssetDocumentProfile.* Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentProfile.* Source/AssetDocument/Private/Tests/AssetDocumentBlackboardDataTests.cpp Source/AssetDocument/Private/Tests/AssetDocumentBehaviorTreeTests.cpp
git commit -m "feat(assetdoc): register behavior tree blackboard profiles"
```

Review range: `$env:TASK_BASE..HEAD`.

## Task 2: Blackboard Key Schema Utility

**Files:**
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentBlackboardKeySchemaUtils.h`
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentBlackboardKeySchemaUtils.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentBlackboardDataTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short --branch
```

- [ ] **Step 2: Write failing utility tests**

Add tests under `AssetFactory.AssetDocument.BlackboardData.Keys`:

- `Bool`, `Int`, `Float`, `String`, `Name`, `Vector`, `Rotator` aliases resolve to `UBlackboardKeyType_*`.
- `Object` and `Class` require `BaseClass`.
- `Enum` requires an enum reference.
- explicit `KeyTypeClass` supports custom/loadable key type classes.
- duplicate local key names return `DuplicateBlackboardKey`.
- parent-chain lookup finds inherited keys but extraction of child keys stays local.

Expected diagnostic paths:

```text
/Body/Keys/<Name>/Type
/Body/Keys/<Name>/BaseClass
/Body/Keys/<Index>/Name
```

- [ ] **Step 3: Implement data structs and parse API**

Implement:

```cpp
struct FAssetDocumentBlackboardKeySpec
{
	FName Name;
	FString Type;
	TSoftClassPtr<UBlackboardKeyType> KeyTypeClass;
	TSoftClassPtr<UObject> BaseClass;
	TSoftObjectPtr<UObject> Enum;
	bool bInstanceSynced = false;
	TSharedPtr<FJsonObject> CanonicalJson;
};

struct FAssetDocumentBlackboardKeyLookupEntry
{
	FName Name;
	UClass* KeyTypeClass = nullptr;
	UClass* BaseClass = nullptr;
	UObject* EnumObject = nullptr;
	bool bInherited = false;
};

class FAssetDocumentBlackboardKeySchemaUtils
{
public:
	static FAssetDocumentCapabilityResult ParseKey(
		const TSharedRef<FJsonObject>& Json,
		const FString& Path,
		FAssetDocumentBlackboardKeySpec& OutSpec);
	static FAssetDocumentCapabilityResult ResolveKeyTypeClass(
		const FAssetDocumentBlackboardKeySpec& Spec,
		UClass*& OutClass,
		FString& OutCanonicalType);
	static FAssetDocumentCapabilityResult ValidateKeySpec(
		const FAssetDocumentBlackboardKeySpec& Spec,
		const FString& Path);
	static FAssetDocumentCapabilityResult BuildLookup(
		const UBlackboardData* Blackboard,
		TMap<FName, FAssetDocumentBlackboardKeyLookupEntry>& OutLookup);
	static bool FindKeyInLookup(
		const TMap<FName, FAssetDocumentBlackboardKeyLookupEntry>& Lookup,
		FName KeyName,
		FAssetDocumentBlackboardKeyLookupEntry& OutEntry);
	static TSharedRef<FJsonObject> ExtractKey(const FBlackboardEntry& Entry);
};
```

Use dynamic loading where possible:

```cpp
UClass* Class = FClassFinderUtils::FindClassByName(TypeName, UBlackboardKeyType::StaticClass());
```

Do not hardcode a behavior branch per BT node class. Key type alias mapping is allowed because it maps UE key type aliases, not BT node classes.

- [ ] **Step 4: Run focused tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<validation-host>.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl "-ExecCmds=Automation RunTests AssetFactory.AssetDocument.BlackboardData.Keys; Quit"
```

Expected: new key utility tests pass.

- [ ] **Step 5: Commit**

```powershell
git add Source/AssetDocument/Private/Regions/AssetDocumentBlackboardKeySchemaUtils.* Source/AssetDocument/Private/Tests/AssetDocumentBlackboardDataTests.cpp
git commit -m "feat(assetdoc): add blackboard key schema utility"
```

Review range: `$env:TASK_BASE..HEAD`.

## Task 3: BlackboardData Profile, Lifecycle, Apply, Extract, Diff

**Files:**
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentBlackboardKeyRegionAdapter.h`
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentBlackboardKeyRegionAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/BlackboardDataAssetDocumentProfile.h`
- Modify: `Source/AssetDocument/Private/Profiles/BlackboardDataAssetDocumentProfile.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentLifecycle.h`
- Modify: `Source/AssetDocument/Private/AssetDocumentLifecycle.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentClassResolver.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentBlackboardDataTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short --branch
```

- [ ] **Step 2: Write failing BlackboardData profile tests**

Add tests:

- `AssetFactory.AssetDocument.BlackboardData.ParentInheritance`
- `AssetFactory.AssetDocument.BlackboardData.ApplyExtractDiff`
- `AssetFactory.AssetDocument.BlackboardData.RejectsDuplicateKeys`
- `AssetFactory.AssetDocument.BlackboardData.RejectsInvalidKeyType`

Test asset paths should use `/Game/AssetDocumentTests/BB_AD_<TestName>`.

- [ ] **Step 3: Implement lifecycle create/load**

In `FAssetDocumentLifecycle::CreateAsset`, branch exact class:

```cpp
if (Class == UBlackboardData::StaticClass())
{
	return CreateBlackboardDataAsset(Target, Package, AssetName, Document);
}
```

`CreateBlackboardDataAsset` creates a `UBlackboardData` with `NewObject<UBlackboardData>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional)`, calls `FAssetRegistryModule::AssetCreated`, marks package dirty, and returns the asset.

- [ ] **Step 4: Implement BlackboardData body capability with dispatcher**

`FBlackboardDataAssetDocumentCapability` must:

- Use `FAssetDocumentBodyRegionDispatcher`.
- Accept only `Parent` and `Keys`.
- `Parent` resolves `AssetRef<UBlackboardData>` or `null`.
- `Keys` delegates to `FAssetDocumentBlackboardKeyRegionAdapter`.
- Unknown body key returns `UnknownBodyKey`.

- [ ] **Step 5: Implement key adapter apply/extract/diff**

`FAssetDocumentBlackboardKeyRegionAdapter` should compose `FAssetDocumentNamedArrayRegionAdapter` with identity `Name`.

Apply:

- parse all keys first.
- reject duplicates before mutating.
- construct `FBlackboardEntry` and key type UObject instances under the blackboard.
- replace local `Keys` as source-of-truth.
- preserve authored order.

Extract:

- emit local `Keys` only.
- include `Type`, `BaseClass`, `Enum`, `bInstanceSynced` when meaningful.

Diff:

- extract current keys.
- canonical compare by `Name`.
- paths use `/Body/Keys/<Name>`.

- [ ] **Step 6: Run focused tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<validation-host>.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl "-ExecCmds=Automation RunTests AssetFactory.AssetDocument.BlackboardData; Quit"
```

Expected: all BlackboardData tests pass.

- [ ] **Step 7: Commit**

```powershell
git add Source/AssetDocument/Private/Regions/AssetDocumentBlackboardKeyRegionAdapter.* Source/AssetDocument/Private/Profiles/BlackboardDataAssetDocumentProfile.* Source/AssetDocument/Private/AssetDocumentLifecycle.* Source/AssetDocument/Private/AssetDocumentClassResolver.cpp Source/AssetDocument/Private/Tests/AssetDocumentBlackboardDataTests.cpp
git commit -m "feat(assetdoc): implement blackboard data profile"
```

Review range: `$env:TASK_BASE..HEAD`.

## Task 4: Reflected Property Runtime For BT Authored Properties

**Files:**
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentReflectedPropertyUtils.h`
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentReflectedPropertyUtils.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentBehaviorTreeTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short --branch
```

- [ ] **Step 2: Write failing property tests**

Add `AssetFactory.AssetDocument.BehaviorTree.ReflectedProperties` with transient test objects or BT node CDO instances covering:

- numeric, bool, string, name, text, enum
- object references use `AssetRef` and class references use `ClassRef`; raw string paths are accepted only when an existing shared property runtime already canonicalizes them back to fragment form.
- struct extraction for `FBlackboardKeySelector`
- array/map properties supported by `FPropertySetterUtils`
- skip/reject non-authored or unsupported runtime/cache fields with exact path

- [ ] **Step 3: Implement shared reflected property helper**

Implement:

```cpp
class FAssetDocumentReflectedPropertyUtils
{
public:
	static FAssetDocumentCapabilityResult ValidateProperties(
		UObject* Object,
		const TSharedRef<FJsonObject>& Properties,
		const FString& Path);
	static FAssetDocumentCapabilityResult ApplyProperties(
		UObject* Object,
		const TSharedRef<FJsonObject>& Properties,
		const FString& Path);
	static FAssetDocumentCapabilityResult ExtractAuthoredProperties(
		UObject* Object,
		TSharedRef<FJsonObject>& OutProperties,
		const FString& Path);
	static FAssetDocumentCapabilityResult DiffProperties(
		UObject* Object,
		const TSharedRef<FJsonObject>& DesiredProperties,
		const FString& Path,
		TArray<TSharedPtr<FJsonValue>>& OutDiffEntries);
	static FAssetDocumentCapabilityResult ApplyBlackboardKeySelector(
		FStructProperty* Property,
		void* ValuePtr,
		const TSharedRef<FJsonObject>& Json,
		const FString& Path);
	static FAssetDocumentCapabilityResult ExtractBlackboardKeySelector(
		FStructProperty* Property,
		const void* ValuePtr,
		TSharedPtr<FJsonValue>& OutValue,
		const FString& Path);
};
```

Rules:

- never branch on concrete `UBTTask_*`, `UBTService_*`, `UBTDecorator_*`, or `UBTComposite_*` class names.
- exclude properties owned by `Body.Tree`: `Children`, `Services`, `Decorators`, `DecoratorOps`, `RootNode`, `RootDecorators`, `RootDecoratorOps`, `BTGraph`.
- use `FPropertySetterUtils` for application.
- use reflection for extraction and canonical JSON for diff.

- [ ] **Step 4: Run focused tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<validation-host>.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl "-ExecCmds=Automation RunTests AssetFactory.AssetDocument.BehaviorTree.ReflectedProperties; Quit"
```

Expected: reflected property tests pass.

- [ ] **Step 5: Commit**

```powershell
git add Source/AssetDocument/Private/Regions/AssetDocumentReflectedPropertyUtils.* Source/AssetDocument/Private/Tests/AssetDocumentBehaviorTreeTests.cpp
git commit -m "feat(assetdoc): add reflected property runtime for behavior trees"
```

Review range: `$env:TASK_BASE..HEAD`.

## Task 5: Public Tree Region Adapter

**Files:**
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentTreeRegionAdapter.h`
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentTreeRegionAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentBehaviorTreeTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short --branch
```

- [ ] **Step 2: Write failing generic tree adapter tests**

Add runtime fixture tests for:

- root object required.
- every node id required.
- duplicate node id rejected.
- child edge requires exactly one `Child`.
- decorator/service arrays collect identities.
- decorator logic operations accept `Test`, `And`, `Or`, `Not`.
- semantic diff paths use ids, not array indexes after ids are known.

- [ ] **Step 3: Implement adapter config and hooks**

Implement:

```cpp
struct FAssetDocumentTreeRegionAdapterConfig
{
	FName Name;
	FString RootField = TEXT("Root");
	FString IdField = TEXT("Id");
	FString ClassField = TEXT("Class");
	FString ChildrenField = TEXT("Children");
	FString ChildField = TEXT("Child");
	FString DecoratorsField = TEXT("Decorators");
	FString DecoratorLogicField = TEXT("DecoratorLogic");
	FString ServicesField = TEXT("Services");
};

struct FAssetDocumentTreeRegionAdapterHooks
{
	TFunction<FAssetDocumentCapabilityResult(const FAssetDocumentRegionContext&, const TSharedRef<FJsonObject>&)> ValidateTree;
	TFunction<FAssetDocumentCapabilityResult(FAssetDocumentRegionContext&, const TSharedRef<FJsonObject>&, bool&)> ApplyTree;
	TFunction<FAssetDocumentCapabilityResult(const FAssetDocumentRegionContext&, TSharedRef<FJsonObject>&)> ExtractTree;
	TFunction<FAssetDocumentCapabilityResult(const FAssetDocumentRegionContext&, const TSharedRef<FJsonObject>&, TArray<TSharedPtr<FJsonValue>>&)> DiffTree;
};
```

The adapter owns JSON shape, id collection, duplicate validation, and semantic path helpers. BT materialization stays in hooks.

- [ ] **Step 4: Run adapter tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<validation-host>.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl "-ExecCmds=Automation RunTests AssetFactory.AssetDocument.RegionRuntime.Tree; Quit"
```

Expected: public tree adapter tests pass.

- [ ] **Step 5: Commit**

```powershell
git add Source/AssetDocument/Private/Regions/AssetDocumentTreeRegionAdapter.* Source/AssetDocument/Private/Tests/AssetDocumentRegionRuntimeTests.cpp Source/AssetDocument/Private/Tests/AssetDocumentBehaviorTreeTests.cpp
git commit -m "feat(assetdoc): add public tree region adapter"
```

Review range: `$env:TASK_BASE..HEAD`.

## Task 6: BehaviorTree Lifecycle, Blackboard Region, And Empty Tree Profile Wiring

**Files:**
- Modify: `Source/AssetDocument/Private/AssetDocumentLifecycle.h`
- Modify: `Source/AssetDocument/Private/AssetDocumentLifecycle.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentClassResolver.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentProfile.h`
- Modify: `Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentProfile.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentBehaviorTreeTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short --branch
```

- [ ] **Step 2: Write failing profile lifecycle tests**

Tests:

- `AssetFactory.AssetDocument.BehaviorTree.BlackboardReference`
- `AssetFactory.AssetDocument.BehaviorTree.RejectsBlackboardInline`
- `AssetFactory.AssetDocument.BehaviorTree.RejectsMissingBlackboardForKeySelectors`

- [ ] **Step 3: Add lifecycle create for `UBehaviorTree`**

Create with:

```cpp
UBehaviorTree* BehaviorTree = NewObject<UBehaviorTree>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
```

Set package dirty and notify asset registry. Do not create inline blackboard.

- [ ] **Step 4: Implement `Body.Blackboard` region**

Use an object/ref adapter hook or focused object adapter:

- accept only `AssetRef<UBlackboardData>`.
- resolve existing asset.
- set `UBehaviorTree::BlackboardAsset`.
- extract object path as `AssetRef`.
- diff at `/Body/Blackboard`.

- [ ] **Step 5: Wire `Body.Tree` and `Body.EditorLayout` as present but strict**

At this task, `Body.Tree` only proves profile wiring and schema visibility; it is not a completion point. Task 7 must implement complete semantic tree apply/extract/diff before any BT capability is described as complete.

- [ ] **Step 6: Run focused tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<validation-host>.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl "-ExecCmds=Automation RunTests AssetFactory.AssetDocument.BehaviorTree.ProfileShape; Automation RunTests AssetFactory.AssetDocument.BehaviorTree.BlackboardReference; Quit"
```

Expected: profile lifecycle and blackboard tests pass.

- [ ] **Step 7: Commit**

```powershell
git add Source/AssetDocument/Private/AssetDocumentLifecycle.* Source/AssetDocument/Private/AssetDocumentClassResolver.cpp Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentProfile.* Source/AssetDocument/Private/Tests/AssetDocumentBehaviorTreeTests.cpp
git commit -m "feat(assetdoc): wire behavior tree profile lifecycle"
```

Review range: `$env:TASK_BASE..HEAD`.

## Task 7: BehaviorTree Dynamic Node Materializer And Complete Semantic Tree

**Files:**
- Create: `Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentMaterializer.h`
- Create: `Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentMaterializer.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentProfile.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentBehaviorTreeTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short --branch
```

- [ ] **Step 2: Write failing full semantic tests**

Tests:

- `AssetFactory.AssetDocument.BehaviorTree.Tree`
- `AssetFactory.AssetDocument.BehaviorTree.RootDecorators`
- `AssetFactory.AssetDocument.BehaviorTree.DecoratorLogic`
- `AssetFactory.AssetDocument.BehaviorTree.DynamicNodeClasses`
- `AssetFactory.AssetDocument.BehaviorTree.ApplyFailureDoesNotMutateExisting`

Test documents must include:

- root `BTComposite_Selector`.
- composite service with reflected `Interval`.
- edge decorator `BTDecorator_Blackboard`.
- `DecoratorLogic` with `Test`.
- task `BTTask_MoveTo` with `FBlackboardKeySelector`.
- subtree task `BTTask_RunBehavior` with `AssetRef<UBehaviorTree>`.

- [ ] **Step 3: Implement materializer parse and preflight**

The materializer must:

- parse the entire desired tree before mutating.
- resolve all classes dynamically with `FClassFinderUtils::FindClassByName` or `StaticLoadClass`.
- validate class compatibility by base type and position.
- collect ids for root, child nodes, root decorators, edge decorators, and services.
- reject duplicates with exact paths.
- validate decorator logic shape before mutation.

- [ ] **Step 4: Implement node creation**

Create nodes with the BT asset as outer:

```cpp
UBTNode* Node = NewObject<UBTNode>(BehaviorTree, NodeClass, NAME_None, RF_Transactional);
```

Then cast by expected base:

- root/internal composite: `UBTCompositeNode`
- leaf task: `UBTTaskNode`
- decorator: `UBTDecorator`
- service: `UBTService`

Apply reflected properties through `FAssetDocumentReflectedPropertyUtils`.

- [ ] **Step 5: Implement root decorators and child edge bindings**

Apply:

- `UBehaviorTree::RootDecorators`
- `UBehaviorTree::RootDecoratorOps`
- `UBTCompositeNode::Children[]`
- `FBTCompositeChild::ChildComposite` or `ChildTask`
- `FBTCompositeChild::Decorators`
- `FBTCompositeChild::DecoratorOps`
- `UBTCompositeNode::Services`

Never branch on concrete BT node class names.

- [ ] **Step 6: Implement extract**

Extract canonical JSON:

- every node has stable `Id`.
- classes output full `/Script/...` path when available.
- properties output authored editable reflected property JSON.
- edge decorators and decorator logic are under `Children[].Decorators` and `Children[].DecoratorLogic`.
- root decorators and root decorator logic are top-level under `Body.Tree`.

- [ ] **Step 7: Implement diff**

Use extracted canonical current tree vs desired tree:

- node paths use `/Body/Tree/<NodeId>`.
- root decorator paths use `/Body/Tree/RootDecorators/<Id>`.
- edge decorator paths use `/Body/Tree/<ParentId>/Children/<ChildId>/Decorators/<Id>`.
- decorator logic paths use index because logic op order is expression order.

- [ ] **Step 8: Run focused tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<validation-host>.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl "-ExecCmds=Automation RunTests AssetFactory.AssetDocument.BehaviorTree.Tree; Automation RunTests AssetFactory.AssetDocument.BehaviorTree.RootDecorators; Automation RunTests AssetFactory.AssetDocument.BehaviorTree.DecoratorLogic; Quit"
```

Expected: semantic tree tests pass.

- [ ] **Step 9: Commit**

```powershell
git add Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentMaterializer.* Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentProfile.cpp Source/AssetDocument/Private/Tests/AssetDocumentBehaviorTreeTests.cpp
git commit -m "feat(assetdoc): materialize behavior tree semantic tree"
```

Review range: `$env:TASK_BASE..HEAD`.

## Task 8: BT + BB Cross-Region Validation

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentMaterializer.*`
- Modify: `Source/AssetDocument/Private/Regions/AssetDocumentBlackboardKeySchemaUtils.*`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentBehaviorTreeTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short --branch
```

- [ ] **Step 2: Write failing validation tests**

Tests:

- `AssetFactory.AssetDocument.BehaviorTree.BlackboardKeySelectors`
- `AssetFactory.AssetDocument.BehaviorTree.SubtreeBlackboardCompatibility`
- `AssetFactory.AssetDocument.BehaviorTree.UnknownBlackboardKeyRejects`
- `AssetFactory.AssetDocument.BehaviorTree.IncompatibleBlackboardKeyTypeRejects`

- [ ] **Step 3: Validate key selector properties**

During preflight, inspect reflected properties on all task/composite/decorator/service nodes. For each `FBlackboardKeySelector`:

- read authored JSON key name from `Properties`.
- load referenced BT blackboard.
- build key lookup through parent chain.
- reject unknown key at `/Body/Tree/<Id>/Properties/<Field>/Key`.
- validate type compatibility if UE property filters expose allowed key types.

- [ ] **Step 4: Validate subtree compatibility**

For BT asset references in node properties such as `UBTTask_RunBehavior::BehaviorAsset`:

- resolve `AssetRef<UBehaviorTree>`.
- compare child `BlackboardAsset` and parent `BlackboardAsset`.
- use `UBlackboardData::IsRelatedTo` when available.
- reject incompatible subtree at `/Body/Tree/<Id>/Properties/<Field>`.

- [ ] **Step 5: Run focused tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<validation-host>.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl "-ExecCmds=Automation RunTests AssetFactory.AssetDocument.BehaviorTree.BlackboardKeySelectors; Automation RunTests AssetFactory.AssetDocument.BehaviorTree.SubtreeBlackboardCompatibility; Quit"
```

Expected: cross-region validation tests pass.

- [ ] **Step 6: Commit**

```powershell
git add Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentMaterializer.* Source/AssetDocument/Private/Regions/AssetDocumentBlackboardKeySchemaUtils.* Source/AssetDocument/Private/Tests/AssetDocumentBehaviorTreeTests.cpp
git commit -m "feat(assetdoc): validate behavior tree blackboard semantics"
```

Review range: `$env:TASK_BASE..HEAD`.

## Task 9: BehaviorTree Editor Layout Region

**Files:**
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentEditorLayoutRegionAdapter.h`
- Create: `Source/AssetDocument/Private/Regions/AssetDocumentEditorLayoutRegionAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentMaterializer.*`
- Modify: `Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentProfile.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentBehaviorTreeTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short --branch
```

- [ ] **Step 2: Write failing layout tests**

Tests:

- `AssetFactory.AssetDocument.BehaviorTree.EditorLayout`
- `AssetFactory.AssetDocument.BehaviorTree.EditorLayoutRejectsDanglingNode`
- `AssetFactory.AssetDocument.BehaviorTree.EditorLayoutOnlyDiff`

- [ ] **Step 3: Implement graph rebuild hook**

After semantic tree apply:

- ensure `UBehaviorTree::BTGraph` exists as `UBehaviorTreeGraph`.
- call UE editor graph rebuild path such as `UBehaviorTreeGraph::OnCreated`, `SpawnMissingNodes`, and `UpdateAsset` as verified by existing generator layout notes.
- map semantic `UBTNode*` instances to `UBehaviorTreeGraphNode*`.

- [ ] **Step 4: Implement layout apply**

`Body.EditorLayout.Nodes[]`:

- identity: `NodeId`.
- validate `NodeId` exists in semantic id map.
- set graph node `NodePosX` and `NodePosY`.

`Body.EditorLayout.Comments[]`:

- support stable fields only: `Id`, `Text`, `Position`, `Size`, and optional `Color` if UE storage is stable.
- reject unsupported comment fields with exact path.

- [ ] **Step 5: Implement layout extract/diff**

Extract supported layout into `Body.EditorLayout`.

Diff:

- layout node path `/Body/EditorLayout/Nodes/<NodeId>`.
- layout comment path `/Body/EditorLayout/Comments/<Id>`.
- layout-only changes must not appear as `Body.Tree` changes.

- [ ] **Step 6: Run focused tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<validation-host>.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl "-ExecCmds=Automation RunTests AssetFactory.AssetDocument.BehaviorTree.EditorLayout; Quit"
```

Expected: editor layout tests pass.

- [ ] **Step 7: Commit**

```powershell
git add Source/AssetDocument/Private/Regions/AssetDocumentEditorLayoutRegionAdapter.* Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentMaterializer.* Source/AssetDocument/Private/Profiles/BehaviorTreeAssetDocumentProfile.cpp Source/AssetDocument/Private/Tests/AssetDocumentBehaviorTreeTests.cpp
git commit -m "feat(assetdoc): support behavior tree editor layout"
```

Review range: `$env:TASK_BASE..HEAD`.

## Task 10: Full BT+BB Roundtrip, Sync, Verification, And Report

**Files:**
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentBlackboardDataTests.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentBehaviorTreeTests.cpp`
- Create: `docs/reports/asset-document-behaviortree-blackboard-complete-region-benchmark.md`
- Optional: `docs/superpowers/specs/asset-document-deferred-fields/2026-07-02-behaviortree-blackboard.md`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short --branch
```

- [ ] **Step 2: Add integrated tests**

Add tests:

- `AssetFactory.AssetDocument.BehaviorTree.FullApplyExtractDiff`
- `AssetFactory.AssetDocument.BehaviorTree.ApplyFileCanonicalWriteback`
- `AssetFactory.AssetDocument.BehaviorTree.ApplyFailureRollsBack`
- `AssetFactory.AssetDocument.BehaviorTree.ProfileInspectionListsAllRegions`
- `AssetFactory.AssetDocument.BlackboardData.ApplyFileCanonicalWriteback`

The full roundtrip document must include:

- Blackboard parent and local keys.
- BehaviorTree blackboard `AssetRef`.
- root composite.
- root decorator and `RootDecoratorLogic`.
- composite service.
- edge decorator and `DecoratorLogic`.
- task with `FBlackboardKeySelector`.
- subtree reference.
- `Body.EditorLayout`.

- [ ] **Step 3: Add report**

Create `docs/reports/asset-document-behaviortree-blackboard-complete-region-benchmark.md` with:

- branch, base, final range.
- completed managed regions.
- excluded derived/cache/transient fields.
- any user-approved blocker; if none, write `None`.
- UBT evidence.
- focused automation evidence.
- full `AssetFactory.AssetDocument` evidence.
- `npm --prefix MCP test` evidence.
- HTTP/editor smoke evidence or blocker.
- reviewer findings and fixes.

- [ ] **Step 4: Run UBT**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/Users/HP/.config/superpowers/validation-hosts/<bt-bb-host>/AVH1.uproject" -NoHotReload
```

Expected: build succeeds.

- [ ] **Step 5: Run focused automation**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<validation-host>.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl "-ReportExportPath=<report>" "-ExecCmds=Automation RunTests AssetFactory.AssetDocument.BehaviorTree; Automation RunTests AssetFactory.AssetDocument.BlackboardData; Quit"
```

Expected: all BT/BB focused tests pass.

- [ ] **Step 6: Run full AssetDocument automation**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<validation-host>.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl "-ReportExportPath=<report>" "-ExecCmds=Automation RunTests AssetFactory.AssetDocument; Quit"
```

Expected: full AssetDocument suite passes.

- [ ] **Step 7: Run MCP tests**

```powershell
npm --prefix MCP test
```

Expected: MCP tests pass.

- [ ] **Step 8: Run smoke or record blocker**

If editor HTTP smoke is available, start the validation host editor, wait for `health_check`, and apply/extract a BT+BB sidecar through MCP/HTTP.

If blocked by host listener or editor environment, record:

- exact command.
- exact error/log path.
- why it does not count as passing verification.

- [ ] **Step 9: Commit**

```powershell
git add Source/AssetDocument/Private/Tests/AssetDocumentBlackboardDataTests.cpp Source/AssetDocument/Private/Tests/AssetDocumentBehaviorTreeTests.cpp docs/reports/asset-document-behaviortree-blackboard-complete-region-benchmark.md docs/superpowers/specs/asset-document-deferred-fields/2026-07-02-behaviortree-blackboard.md
git commit -m "test(assetdoc): verify behavior tree blackboard roundtrip"
```

If the deferred-fields file is not needed, omit it from `git add`.

Review range: `$env:TASK_BASE..HEAD`.

## Review Plan

After every task:

1. Spec compliance review range: `TASK_BASE..HEAD`.
2. Code quality review range: `TASK_BASE..HEAD`.
3. Fix any findings in the same task range.
4. Re-run focused verification before moving on.

After Task 10:

1. Final spec review range: `c563b6bd5077c90897ab28cb118b9afb39e5fc8b..HEAD`.
2. Final code quality review range: `c563b6bd5077c90897ab28cb118b9afb39e5fc8b..HEAD`.
3. Final verification before completion:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/Users/HP/.config/superpowers/validation-hosts/<bt-bb-host>/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<validation-host>.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl "-ReportExportPath=<report>" "-ExecCmds=Automation RunTests AssetFactory.AssetDocument.BehaviorTree; Automation RunTests AssetFactory.AssetDocument.BlackboardData; Quit"
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<validation-host>.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl "-ReportExportPath=<report>" "-ExecCmds=Automation RunTests AssetFactory.AssetDocument; Quit"
npm --prefix MCP test
```

## Subagent Dispatch Boundaries

Use one worker per task unless a task is explicitly split by the controller. Workers must not inherit long thread context. Provide each worker:

- this plan task text.
- the BT+BB spec path.
- exact worktree, branch, base commit, and diff range.
- allowed write files.
- focused verification commands.

Do not run two implementation workers with overlapping write scopes at the same time. Read-only reviewers can run after each checkpoint commit.
