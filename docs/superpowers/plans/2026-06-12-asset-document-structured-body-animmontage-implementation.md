# AssetDocument Structured Body AnimMontage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the existing AssetDocument module from reflected `Properties`-only assets to profile-driven `Body` documents, with `UAnimMontage` as the first structured asset pilot.

**Architecture:** Keep MCP/HTTP entrypoints generic. `FAssetDocumentService` remains the stable facade, but internally delegates common document validation, reflected property patching, profile lookup, body adapters, and fragment compilation to small registered components. `UAnimMontage` support is implemented as one profile/capability/placement adapter set; fragments such as `AssetRef`, `EmbeddedObject`, and `DefinitionRef` stay reusable across future asset profiles.

**Tech Stack:** Unreal Engine 5.7 C++ editor module, `Source/AssetDocument`, existing `FPropertySetterUtils` / `FAssetDocumentPropertyAdapter`, MCP TypeScript server, UE Automation tests, UBT Development builds.

---

## Implementation Context

Existing first-stage AssetDocument code is already present:

- `Source/AssetDocument/Public/AssetDocumentTypes.h`
- `Source/AssetDocument/Public/AssetDocumentService.h`
- `Source/AssetDocument/Private/AssetDocumentService.cpp`
- `Source/AssetDocument/Private/AssetDocumentHttpRoutes.cpp`
- `Source/AssetDocument/Private/AssetDocumentPropertyAdapter.*`
- `Source/AssetDocument/Private/AssetDocumentLifecycle.*`
- `Source/AssetDocument/Private/Tests/AssetDocumentServiceTests.cpp`
- `MCP/src/index.ts`
- `MCP/schemas/AssetDocument.md`

The implementation must preserve current `AssetType: "GenericAsset"` behavior and tests. New canonical structured documents may omit `AssetType`; if `AssetType` is present it must still be `GenericAsset` for the legacy reflected-property path.

No asset-specific MCP tools are allowed. Add only generic AssetDocument tools/routes:

- `inspect_asset_document_profile`
- `create_asset_document_template`

Existing tools remain:

- `get_asset_document_schema`
- `inspect_asset_document_target`
- `validate_asset_document`
- `apply_asset_document`
- `apply_asset_document_file`
- `extract_asset_document`
- `diff_asset_document`

Field naming rule: public `Body` and profile fields must use full UE stable names where possible. Do not introduce abbreviations or short aliases such as `Slots`, `Segments`, `Animation`, or `Sections`; use `SlotAnimTracks`, `AnimSegments`, `AnimReference`, and `CompositeSections`.

## File Structure

Create these focused AssetDocument units:

- `Source/AssetDocument/Public/AssetDocumentProfile.h`
  Public profile/capability interfaces used by service and tests.
- `Source/AssetDocument/Public/AssetDocumentFragment.h`
  Public fragment context/result/adapter interfaces.
- `Source/AssetDocument/Private/AssetDocumentProfileRegistry.h/.cpp`
  Registry for exact-class profiles.
- `Source/AssetDocument/Private/AssetDocumentFragmentCompiler.h/.cpp`
  Fragment compiler and adapter registry.
- `Source/AssetDocument/Private/Fragments/AssetDocumentAssetRefFragment.cpp`
  Loads and validates asset references.
- `Source/AssetDocument/Private/Fragments/AssetDocumentClassRefFragment.cpp`
  Resolves and validates class references.
- `Source/AssetDocument/Private/Fragments/AssetDocumentStructValueFragment.cpp`
  Builds UScriptStruct values for future adapters.
- `Source/AssetDocument/Private/Fragments/AssetDocumentEmbeddedObjectFragment.cpp`
  Creates transient or asset-owned embedded UObject instances and applies `Properties`.
- `Source/AssetDocument/Private/Fragments/AssetDocumentDefinitionRefFragment.cpp`
  Resolves `Definitions` entries and detects cycles.
- `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentProfile.h/.cpp`
  `UAnimMontage` profile, schema/template/routing.
- `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.h/.cpp`
  `UAnimMontage` body validation/apply/extract/diff.
- `Source/AssetDocument/Private/Profiles/AnimMontageNotifyPlacementAdapter.h/.cpp`
  Notify and notify-state timeline placement, using the fragment compiler.
- `Source/AssetDocument/Private/Tests/AssetDocumentFragmentTests.cpp`
  Unit-style automation tests for fragments and registries.
- `Source/AssetDocument/Private/Tests/AssetDocumentProfileTests.cpp`
  Profile/template/schema tests.
- `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`
  Structured apply/extract/diff tests for montage.

Modify:

- `Source/AssetDocument/Public/AssetDocumentTypes.h`
- `Source/AssetDocument/Public/AssetDocumentService.h`
- `Source/AssetDocument/Private/AssetDocumentService.cpp`
- `Source/AssetDocument/Private/AssetDocumentHttpRoutes.cpp`
- `Source/AssetDocument/Private/AssetDocumentModule.cpp`
- `Source/AssetDocument/AssetDocument.Build.cs`
- `MCP/src/index.ts`
- `MCP/schemas/AssetDocument.md`
- `MCP/src/broker/toolCatalog.test.ts` only if the generic tool list assertions need updating.

Do not modify:

- Existing `generate_assets` generator registry for this feature.
- Existing BSL tools.
- Existing asset-specific generator schemas except for references in docs if tests require it.

---

## Task 1: Profile and Fragment Interfaces

**Files:**
- Create: `Source/AssetDocument/Public/AssetDocumentProfile.h`
- Create: `Source/AssetDocument/Public/AssetDocumentFragment.h`
- Create: `Source/AssetDocument/Private/AssetDocumentProfileRegistry.h`
- Create: `Source/AssetDocument/Private/AssetDocumentProfileRegistry.cpp`
- Create: `Source/AssetDocument/Private/AssetDocumentFragmentCompiler.h`
- Create: `Source/AssetDocument/Private/AssetDocumentFragmentCompiler.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentModule.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentProfileTests.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentFragmentTests.cpp`

- [ ] **Step 1: Record task base**

Run:

```powershell
git rev-parse HEAD
```

Save the output locally as `TASK_BASE` for reviews. Review diff range for this task is `TASK_BASE..HEAD`.

- [ ] **Step 2: Write failing registry and empty compiler tests**

Create `Source/AssetDocument/Private/Tests/AssetDocumentProfileTests.cpp` with a minimal fake profile:

```cpp
#include "AssetDocumentProfile.h"
#include "AssetDocumentProfileRegistry.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
class FTestAssetDocumentProfile final : public IAssetDocumentProfile
{
public:
	explicit FTestAssetDocumentProfile(UClass* InClass)
		: Class(InClass)
	{
	}

	virtual UClass* GetExactClass() const override { return Class; }
	virtual TSharedRef<FJsonObject> GetDocumentShape() const override { return MakeShared<FJsonObject>(); }
	virtual TSharedRef<FJsonObject> CreateTemplate(const FAssetDocumentTemplateContext& Context) const override { return MakeShared<FJsonObject>(); }
	virtual TArray<FName> GetBodyKeys() const override { return {TEXT("BodyKey")}; }
	virtual const IAssetDocumentCapability* ResolveBodyAdapter(FName BodyKey) const override { return nullptr; }

private:
	UClass* Class = nullptr;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentProfileRegistryTest,
	"AssetFactory.AssetDocument.Profile.Registry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentProfileRegistryTest::RunTest(const FString& Parameters)
{
	FAssetDocumentProfileRegistry Registry;
	Registry.Register(MakeShared<FTestAssetDocumentProfile>(UObject::StaticClass()));

	TestNotNull(TEXT("Finds registered exact class profile"), Registry.FindForClass(UObject::StaticClass()));
	TestNull(TEXT("Does not return parent profile for unrelated exact class"), Registry.FindForClass(UPackage::StaticClass()));
	return true;
}

#endif
```

Create `Source/AssetDocument/Private/Tests/AssetDocumentFragmentTests.cpp` with:

```cpp
#include "AssetDocumentFragment.h"
#include "AssetDocumentFragmentCompiler.h"

#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
class FTestFragmentAdapter final : public IAssetDocumentFragmentAdapter
{
public:
	virtual FName GetKind() const override { return TEXT("TestKind"); }
	virtual bool SupportsContext(const FAssetDocumentFragmentContext& Context) const override { return true; }
	virtual FAssetDocumentFragmentResult Validate(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const override
	{
		return FAssetDocumentFragmentResult::Success();
	}
	virtual FAssetDocumentFragmentResult Compile(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const override
	{
		FAssetDocumentFragmentResult Result = FAssetDocumentFragmentResult::Success();
		Result.Value = MakeShared<FJsonValueString>(TEXT("compiled"));
		return Result;
	}
	virtual FAssetDocumentFragmentResult Extract(const FAssetDocumentFragmentExtractContext& Context, TSharedRef<FJsonObject>& OutFragmentJson) const override
	{
		OutFragmentJson->SetStringField(TEXT("Kind"), GetKind().ToString());
		return FAssetDocumentFragmentResult::Success();
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentFragmentCompilerDispatchTest,
	"AssetFactory.AssetDocument.Fragments.Dispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentFragmentCompilerDispatchTest::RunTest(const FString& Parameters)
{
	FAssetDocumentFragmentCompiler Compiler;
	Compiler.RegisterAdapter(MakeShared<FTestFragmentAdapter>());

	TSharedRef<FJsonObject> Fragment = MakeShared<FJsonObject>();
	Fragment->SetStringField(TEXT("Kind"), TEXT("TestKind"));

	FAssetDocumentFragmentContext Context;
	Context.JsonPath = TEXT("/Body/Test");

	const FAssetDocumentFragmentResult Result = Compiler.Compile(Fragment, Context);
	TestTrue(TEXT("Known fragment kind compiles"), Result.bSuccess);
	TestTrue(TEXT("Compile result has a JSON value"), Result.Value.IsValid());

	Fragment->SetStringField(TEXT("Kind"), TEXT("MissingKind"));
	const FAssetDocumentFragmentResult MissingResult = Compiler.Compile(Fragment, Context);
	TestFalse(TEXT("Unknown fragment kind fails"), MissingResult.bSuccess);
	TestTrue(TEXT("Unknown fragment includes JSON path"), MissingResult.Diagnostics.Num() > 0 && MissingResult.Diagnostics[0].Path == TEXT("/Body/Test"));
	return true;
}

#endif
```

- [ ] **Step 3: Run focused tests to verify they fail to compile**

Run:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected: compile fails because the new profile/fragment headers and classes do not exist.

- [ ] **Step 4: Add public profile interfaces**

Create `Source/AssetDocument/Public/AssetDocumentProfile.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "AssetDocumentTypes.h"

struct ASSETDOCUMENT_API FAssetDocumentTemplateContext
{
	FString Target;
	FString ClassPath;
};

struct ASSETDOCUMENT_API FAssetDocumentCapabilityContext
{
	UObject* Asset = nullptr;
	UClass* AssetClass = nullptr;
	FString TargetAssetPath;
	FString SourceDocumentPath;
	bool bIsDryRun = false;
	FAssetDocumentResult* Result = nullptr;
};

struct ASSETDOCUMENT_API FAssetDocumentCapabilityResult
{
	bool bSuccess = false;
	FString Message;
	TArray<FAssetDocumentDiagnostic> Diagnostics;
	TSharedPtr<FJsonObject> Payload;

	static FAssetDocumentCapabilityResult Success(const FString& Message = TEXT(""));
	static FAssetDocumentCapabilityResult Failure(const FString& Message, const FString& Path = TEXT(""), const FString& Code = TEXT("ValidationFailed"));
};

class ASSETDOCUMENT_API IAssetDocumentCapability
{
public:
	virtual ~IAssetDocumentCapability() = default;

	virtual FName GetName() const = 0;
	virtual int32 GetApplyOrder() const = 0;
	virtual bool SupportsAsset(const UObject* Asset) const = 0;
	virtual bool SupportsClass(const UClass* AssetClass) const = 0;
	virtual TSharedRef<FJsonObject> GetSchemaHint() const = 0;
	virtual FAssetDocumentCapabilityResult Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const = 0;
	virtual FAssetDocumentCapabilityResult Apply(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) = 0;
	virtual FAssetDocumentCapabilityResult Extract(const FAssetDocumentCapabilityContext& Context, TSharedRef<FJsonObject>& OutBodyJson) const = 0;
	virtual FAssetDocumentCapabilityResult Diff(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& DesiredJson, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const = 0;
};

class ASSETDOCUMENT_API IAssetDocumentProfile
{
public:
	virtual ~IAssetDocumentProfile() = default;

	virtual UClass* GetExactClass() const = 0;
	virtual TSharedRef<FJsonObject> GetDocumentShape() const = 0;
	virtual TSharedRef<FJsonObject> CreateTemplate(const FAssetDocumentTemplateContext& Context) const = 0;
	virtual TArray<FName> GetBodyKeys() const = 0;
	virtual const IAssetDocumentCapability* ResolveBodyAdapter(FName BodyKey) const = 0;
};
```

- [ ] **Step 5: Add public fragment interfaces**

Create `Source/AssetDocument/Public/AssetDocumentFragment.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AssetDocumentTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

struct ASSETDOCUMENT_API FAssetDocumentFragmentContext
{
	UObject* OwnerAsset = nullptr;
	UObject* Outer = nullptr;
	UClass* ExpectedBaseClass = nullptr;
	UScriptStruct* ExpectedStruct = nullptr;
	const TSharedPtr<FJsonObject>* Definitions = nullptr;
	TArray<FString> DefinitionStack;
	FString JsonPath;
	FString Role;
};

struct ASSETDOCUMENT_API FAssetDocumentFragmentExtractContext
{
	UObject* OwnerAsset = nullptr;
	UObject* ValueObject = nullptr;
	UScriptStruct* StructType = nullptr;
	const void* StructValue = nullptr;
	FString JsonPath;
	FString Role;
};

struct ASSETDOCUMENT_API FAssetDocumentFragmentResult
{
	bool bSuccess = false;
	FString Message;
	TArray<FAssetDocumentDiagnostic> Diagnostics;
	TSharedPtr<FJsonValue> Value;
	UObject* Object = nullptr;
	UClass* Class = nullptr;
	UScriptStruct* StructType = nullptr;
	TArray<uint8> StructBytes;

	static FAssetDocumentFragmentResult Success();
	static FAssetDocumentFragmentResult Failure(const FString& Message, const FString& Path = TEXT(""), const FString& Code = TEXT("FragmentFailed"));
};

class ASSETDOCUMENT_API IAssetDocumentFragmentAdapter
{
public:
	virtual ~IAssetDocumentFragmentAdapter() = default;

	virtual FName GetKind() const = 0;
	virtual bool SupportsContext(const FAssetDocumentFragmentContext& Context) const = 0;
	virtual FAssetDocumentFragmentResult Validate(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const = 0;
	virtual FAssetDocumentFragmentResult Compile(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const = 0;
	virtual FAssetDocumentFragmentResult Extract(const FAssetDocumentFragmentExtractContext& Context, TSharedRef<FJsonObject>& OutFragmentJson) const = 0;
};
```

- [ ] **Step 6: Implement registry and compiler dispatch**

Create `Source/AssetDocument/Private/AssetDocumentProfileRegistry.h/.cpp` with `Register`, `FindForClass`, and `GetAllProfiles`.

Create `Source/AssetDocument/Private/AssetDocumentFragmentCompiler.h/.cpp` with:

```cpp
class FAssetDocumentFragmentCompiler
{
public:
	void RegisterAdapter(TSharedRef<IAssetDocumentFragmentAdapter> Adapter);
	FAssetDocumentFragmentResult Validate(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const;
	FAssetDocumentFragmentResult Compile(const TSharedRef<FJsonObject>& FragmentJson, const FAssetDocumentFragmentContext& Context) const;
	FAssetDocumentFragmentResult Extract(const FAssetDocumentFragmentExtractContext& Context, TSharedRef<FJsonObject>& OutFragmentJson) const;

private:
	const IAssetDocumentFragmentAdapter* FindAdapter(FName Kind, const FAssetDocumentFragmentContext& Context) const;
	TMap<FName, TSharedRef<IAssetDocumentFragmentAdapter>> Adapters;
};
```

`Compile` and `Validate` must require `Kind` as a string. Missing or unknown kind returns one diagnostic with `Path=Context.JsonPath`.

- [ ] **Step 7: Run focused compile and tests**

Run UBT:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Then run automation in an editor host when available:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.Profile.Registry;Automation RunTests AssetFactory.AssetDocument.Fragments.Dispatch;Quit" -Unattended -nop4 -nosplash
```

Expected: UBT succeeds; both automation tests pass.

- [ ] **Step 8: Commit**

```powershell
git add Source/AssetDocument/Public/AssetDocumentProfile.h Source/AssetDocument/Public/AssetDocumentFragment.h Source/AssetDocument/Private/AssetDocumentProfileRegistry.* Source/AssetDocument/Private/AssetDocumentFragmentCompiler.* Source/AssetDocument/Private/Tests/AssetDocumentProfileTests.cpp Source/AssetDocument/Private/Tests/AssetDocumentFragmentTests.cpp
git commit -m "feat(assetdoc): add profile and fragment dispatch substrate"
```

---

## Task 2: Fragment Adapters

**Files:**
- Modify: `Source/AssetDocument/Private/AssetDocumentFragmentCompiler.h/.cpp`
- Create: `Source/AssetDocument/Private/Fragments/AssetDocumentAssetRefFragment.cpp`
- Create: `Source/AssetDocument/Private/Fragments/AssetDocumentClassRefFragment.cpp`
- Create: `Source/AssetDocument/Private/Fragments/AssetDocumentStructValueFragment.cpp`
- Create: `Source/AssetDocument/Private/Fragments/AssetDocumentEmbeddedObjectFragment.cpp`
- Create: `Source/AssetDocument/Private/Fragments/AssetDocumentDefinitionRefFragment.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentModule.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentFragmentTests.cpp`

- [ ] **Step 1: Record task base**

Run `git rev-parse HEAD` and save as `TASK_BASE`.

- [ ] **Step 2: Add failing tests for all fragment kinds**

Extend `FAssetDocumentFragmentCompilerDispatchTest` or add new tests:

- `AssetRef` loads `/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton` with `ExpectedBaseClass=USkeleton`.
- `ClassRef` resolves `/Script/Engine.AnimMontage` with `ExpectedBaseClass=UObject`.
- `StructValue` compiles `/Script/CoreUObject.Vector` with `X/Y/Z`.
- `EmbeddedObject` creates `/Script/AssetFactory.AssetFactoryNamedAnimNotifyState` with `ExpectedBaseClass=UAnimNotifyState` and `Outer=GetTransientPackage()`.
- `DefinitionRef` expands `Definitions.AttackAnim` to an `AssetRef`.
- `DefinitionRef` rejects a cycle `A -> B -> A`.

Use JSON helper shapes:

```cpp
TSharedRef<FJsonObject> MakeFragment(const TCHAR* Kind)
{
	TSharedRef<FJsonObject> Fragment = MakeShared<FJsonObject>();
	Fragment->SetStringField(TEXT("Kind"), Kind);
	return Fragment;
}
```

- [ ] **Step 3: Register built-in fragment adapters**

Add a public method on `FAssetDocumentFragmentCompiler`:

```cpp
void RegisterBuiltInAdapters();
```

Implement it by registering adapters for `AssetRef`, `ClassRef`, `StructValue`, `EmbeddedObject`, and `DefinitionRef`.

- [ ] **Step 4: Implement `AssetRef`**

Adapter rules:

- Required fields: `Kind="AssetRef"`, `Path`.
- Optional field: `Class`.
- Load with `LoadObject<UObject>(nullptr, *ObjectPath)`.
- If `Path` is a package path without dot, normalize to `Package.AssetName`.
- If `Context.ExpectedBaseClass` is set, loaded asset must `IsA`.
- If `Class` is set, resolve class and verify loaded asset `IsA(ResolvedClass)`.
- Return `Result.Object = LoadedAsset` and `Result.Value` as the original path string.

- [ ] **Step 5: Implement `ClassRef`**

Adapter rules:

- Required field: `Class`.
- Resolve with existing `FAssetDocumentClassResolver::ResolveClass`.
- If `Context.ExpectedBaseClass` is set, resolved class must be child of it.
- Return `Result.Class = ResolvedClass`.

- [ ] **Step 6: Implement `StructValue`**

Adapter rules:

- Required fields: `Struct`, `Properties`.
- Resolve `UScriptStruct` using `FindObject`/`LoadObject`.
- If `Context.ExpectedStruct` is set, require exact struct match.
- Allocate `StructBytes` with `Struct->GetStructureSize()`, call `InitializeStruct`.
- Use reflected property setting for each `Properties` entry. If direct reuse of `FAssetDocumentPropertyAdapter` is not feasible for struct memory, implement a small local loop using `FPropertySetterUtils` on a temporary container object only if the target struct cannot be set safely; otherwise return `UnsupportedStructProperty` diagnostic for unsupported fields.
- First version must support `FVector` numeric `X/Y/Z`.

- [ ] **Step 7: Implement `EmbeddedObject`**

Adapter rules:

- Required field: `Class`.
- Optional field: `Properties`.
- Resolve class and require `Context.ExpectedBaseClass` if present.
- Require `Context.Outer`.
- Reject abstract classes.
- Create with `NewObject<UObject>(Context.Outer, ResolvedClass, NAME_None, RF_Transactional)`.
- Apply `Properties` using `FAssetDocumentPropertyAdapter::ApplyProperties`.
- Return `Result.Object = NewObject`.

- [ ] **Step 8: Implement `DefinitionRef`**

Adapter rules:

- Required field: `Id`.
- Require `Context.Definitions`.
- Reject missing `Id`.
- Reject cycle if `Context.DefinitionStack` already contains `Id`.
- Lookup definition object in `Definitions`.
- Copy context, append `Id` to `DefinitionStack`, set `JsonPath` to `Context.JsonPath + " -> /Definitions/" + Id`.
- Call `FAssetDocumentFragmentCompiler::Compile` recursively on the referenced fragment.

To avoid static ownership, inject the owning compiler into the definition adapter constructor:

```cpp
explicit FAssetDocumentDefinitionRefFragmentAdapter(const FAssetDocumentFragmentCompiler& InCompiler);
```

- [ ] **Step 9: Run compile and fragment automation**

Run UBT and:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.Fragments;Quit" -Unattended -nop4 -nosplash
```

- [ ] **Step 10: Commit**

```powershell
git add Source/AssetDocument/Private/AssetDocumentFragmentCompiler.* Source/AssetDocument/Private/Fragments Source/AssetDocument/Private/Tests/AssetDocumentFragmentTests.cpp Source/AssetDocument/Private/AssetDocumentModule.cpp
git commit -m "feat(assetdoc): add reusable fragment adapters"
```

---

## Task 3: Generic Profile Service, Template, and MCP Routes

**Files:**
- Modify: `Source/AssetDocument/Public/AssetDocumentTypes.h`
- Modify: `Source/AssetDocument/Public/AssetDocumentService.h`
- Modify: `Source/AssetDocument/Private/AssetDocumentService.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentHttpRoutes.cpp`
- Modify: `MCP/src/index.ts`
- Modify: `MCP/schemas/AssetDocument.md`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentProfileTests.cpp`
- Test: MCP TypeScript tests if tool catalog assertions change.

- [ ] **Step 1: Record task base**

Run `git rev-parse HEAD` and save as `TASK_BASE`.

- [ ] **Step 2: Add request types and service methods**

In `AssetDocumentTypes.h`, add:

```cpp
struct ASSETDOCUMENT_API FAssetDocumentProfileRequest
{
	FString ClassOrAsset;
};

struct ASSETDOCUMENT_API FAssetDocumentTemplateRequest
{
	FString Class;
	FString Target;
};
```

In `AssetDocumentService.h`, add:

```cpp
FAssetDocumentResult InspectProfile(const FAssetDocumentProfileRequest& Request) const;
FAssetDocumentResult CreateTemplate(const FAssetDocumentTemplateRequest& Request) const;
FAssetDocumentResult GetSchema() const;
```

- [ ] **Step 3: Add failing service tests**

In `AssetDocumentProfileTests.cpp`, add tests:

- `InspectProfile` for `/Script/AssetFactory.TestDataAsset` returns generic reflected profile with no `BodySections`.
- `CreateTemplate` for `/Script/AssetFactory.TestDataAsset` returns canonical JSON with `SchemaVersion`, `Target`, `Class`, `Action`, `Properties`.
- `GetSchema` returns `field_naming` with `ban_abbreviations=true`.

Expected before implementation: compile or test failure.

- [ ] **Step 4: Implement generic profile fallback**

`FAssetDocumentService::InspectProfile` should:

- Resolve class or load asset like `Inspect`.
- If profile registry has exact profile, use it.
- Otherwise return a generic reflected profile payload:

```json
{
  "Class": "/Script/AssetFactory.TestDataAsset",
  "DocumentShape": {
    "Definitions": "map<string, Fragment>",
    "Properties": "reflected CDO-diff properties",
    "Body": {}
  },
  "BodySections": [],
  "FragmentKinds": ["AssetRef", "ClassRef", "StructValue", "EmbeddedObject", "DefinitionRef"]
}
```

`CreateTemplate` should:

- For exact profile, call profile `CreateTemplate`.
- For generic fallback, return legacy-compatible reflected template:

```json
{
  "SchemaVersion": 1,
  "Target": "/Game/Data/DA_Test",
  "Class": "/Script/AssetFactory.TestDataAsset",
  "Action": "CreateOrUpdate",
  "Definitions": {},
  "Properties": {}
}
```

- [ ] **Step 5: Implement generic schema payload**

`GetSchema` should return a JSON payload containing:

- `schema_version: 1`
- `asset_document_tools`
- `fragment_kinds`
- `field_naming.ban_abbreviations = true`
- `field_naming.use_ue_stable_field_names = true`
- `registered_profiles`

- [ ] **Step 6: Add HTTP routes**

In `AssetDocumentHttpRoutes.cpp`, add:

- `GET /assetfactory/assetdocument/profile?class_or_asset=...`
- `POST /assetfactory/assetdocument/template`

Also make `HandleSchema` call `Service->GetSchema()` instead of the local static-only schema response, while preserving the `routes` list in the payload.

- [ ] **Step 7: Add MCP tools**

In `MCP/src/index.ts`, add tool definitions:

- `inspect_asset_document_profile` with `class_or_asset: string`
- `create_asset_document_template` with `class: string`, `target: string`

Handlers:

```ts
inspect_asset_document_profile: async (args) => {
  const classOrAsset = requireStringArg(args, "class_or_asset");
  return callAssetDocumentApi(`/assetdocument/profile?class_or_asset=${encodeURIComponent(classOrAsset)}`, "GET");
},
create_asset_document_template: async (args) =>
  callAssetDocumentApi("/assetdocument/template", "POST", {
    Class: requireStringArg(args, "class"),
    Target: requireStringArg(args, "target"),
  }),
```

Update `MCP/schemas/AssetDocument.md` to document the two tools and the no-asset-specific-tool rule.

- [ ] **Step 8: Run tests**

Run:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
Push-Location MCP; npm test; Pop-Location
```

Then automation:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.Profile;Quit" -Unattended -nop4 -nosplash
```

- [ ] **Step 9: Commit**

```powershell
git add Source/AssetDocument/Public/AssetDocumentTypes.h Source/AssetDocument/Public/AssetDocumentService.h Source/AssetDocument/Private/AssetDocumentService.cpp Source/AssetDocument/Private/AssetDocumentHttpRoutes.cpp Source/AssetDocument/Private/Tests/AssetDocumentProfileTests.cpp MCP/src/index.ts MCP/schemas/AssetDocument.md
git commit -m "feat(assetdoc): expose generic profile and template entrypoints"
```

---

## Task 4: AnimMontage Profile and Body Validation

**Files:**
- Create: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentProfile.h`
- Create: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentProfile.cpp`
- Create: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.h`
- Create: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentModule.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentService.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentProfileTests.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`

- [ ] **Step 1: Record task base**

Run `git rev-parse HEAD` and save as `TASK_BASE`.

- [ ] **Step 2: Add failing profile/template tests**

Create `AssetDocumentAnimMontageTests.cpp` with helper:

```cpp
TSharedPtr<FJsonObject> MakeMontageDocument(const FString& Target)
{
	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimMontage"));
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Body"), MakeShared<FJsonObject>());
	return Document;
}
```

Add tests:

- `InspectProfile("/Script/Engine.AnimMontage")` includes `BodySections` with `SlotAnimTracks`, `CompositeSections`, `Notifies`, `NotifyStates`, `Blend`.
- `CreateTemplate("/Script/Engine.AnimMontage", "/Game/AssetDocumentTests/AM_Template")` returns no `AssetType`, includes `Definitions`, `Properties`, `Body.SlotAnimTracks`, `Body.CompositeSections`, `Body.Notifies`, `Body.NotifyStates`.
- `Validate` fails for unknown body key `/Body/Slots`.
- `Validate` fails for abbreviated `Body.Slots`.

- [ ] **Step 3: Implement profile**

`FAnimMontageAssetDocumentProfile`:

- `GetExactClass()` returns `UAnimMontage::StaticClass()`.
- `GetBodyKeys()` returns exact full keys:
  `Skeleton`, `PreviewMesh`, `SlotAnimTracks`, `CompositeSections`, `Notifies`, `NotifyStates`, `Blend`.
- `GetDocumentShape()` returns schema hint with fragment roles.
- `CreateTemplate()` returns canonical structured document:

```json
{
  "SchemaVersion": 1,
  "Target": "...",
  "Class": "/Script/Engine.AnimMontage",
  "Action": "CreateOrUpdate",
  "Definitions": {},
  "Properties": {},
  "Body": {
    "Skeleton": null,
    "PreviewMesh": null,
    "SlotAnimTracks": [],
    "CompositeSections": [],
    "Notifies": [],
    "NotifyStates": [],
    "Blend": {}
  }
}
```

- [ ] **Step 4: Implement capability validation skeleton**

`FAnimMontageAssetDocumentCapability::Validate` should:

- Require body value is object.
- Reject unknown keys.
- Reject abbreviated legacy keys with explicit messages:
  - `Slots` -> `Use SlotAnimTracks`
  - `Segments` -> `Use AnimSegments`
  - `Animation` -> `Use AnimReference`
  - `Sections` -> `Use CompositeSections`
- Validate top-level body section types:
  - `Skeleton`, `PreviewMesh`: null or object fragment
  - `SlotAnimTracks`, `CompositeSections`, `Notifies`, `NotifyStates`: array
  - `Blend`: object

- [ ] **Step 5: Route body validation in service**

Modify `ValidateGenericAssetDocument` into a common validation function that:

- Accepts documents with no `AssetType`.
- If `Body` exists or `Definitions` exists, resolves exact `Class`.
- Looks up profile.
- Rejects `Body` when no profile exists.
- Calls profile body adapter `Validate` for body entries or entire body.

Keep existing legacy `GenericAsset` tests passing.

- [ ] **Step 6: Register profile**

In `AssetDocumentModule.cpp`, when constructing service dependencies, register:

```cpp
ProfileRegistry.Register(MakeShared<FAnimMontageAssetDocumentProfile>());
```

If the current module does not own registries yet, add registry members beside the `FAssetDocumentService` shared instance.

- [ ] **Step 7: Run tests**

Run UBT and:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.Profile;Automation RunTests AssetFactory.AssetDocument.AnimMontage;Quit" -Unattended -nop4 -nosplash
```

- [ ] **Step 8: Commit**

```powershell
git add Source/AssetDocument/Private/Profiles Source/AssetDocument/Private/AssetDocumentModule.cpp Source/AssetDocument/Private/AssetDocumentService.cpp Source/AssetDocument/Private/Tests/AssetDocumentProfileTests.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp
git commit -m "feat(assetdoc): add anim montage profile validation"
```

---

## Task 5: AnimMontage Slot, Section, Blend Apply and Extract

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.*`
- Modify: `Source/AssetDocument/Private/AssetDocumentService.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`

- [ ] **Step 1: Record task base**

Run `git rev-parse HEAD` and save as `TASK_BASE`.

- [ ] **Step 2: Add failing apply test**

In `AssetDocumentAnimMontageTests.cpp`, add test `AssetFactory.AssetDocument.AnimMontage.Apply.Structure`:

- Use target `/Game/AssetDocumentTests/AM_Structure`.
- Document body:
  - `Skeleton` AssetRef `/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton`
  - `PreviewMesh` AssetRef `/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP`
  - `SlotAnimTracks[0].SlotName = "DefaultSlot"`
  - `SlotAnimTracks[0].AnimTrack.AnimSegments[0].AnimReference` references a generated or existing `UAnimSequenceBase`.
  - `StartPos=0`, `AnimStartTime=0`, `AnimEndTime=0.25`, `AnimPlayRate=1`, `LoopingCount=1`.
  - `CompositeSections` with `SectionName="Start"`, `LinkableTime=0`, `NextSectionName="End"` and `SectionName="End"`, `LinkableTime=0.25`.
  - `Blend.BlendInTime=0.1`, `Blend.BlendOutTime=0.2`.

Prefer an existing engine animation if available. If no suitable anim sequence loads in test setup, create a minimal `UAnimSequence` fixture with the existing `AnimSequenceGenerator` test pattern before applying the montage document.

Assertions:

- `Service.Apply` succeeds.
- Loaded `UAnimMontage` has skeleton and preview mesh.
- `SlotAnimTracks.Num() == 1`.
- `SlotAnimTracks[0].SlotName == "DefaultSlot"`.
- `AnimSegments.Num() == 1`.
- segment `GetAnimReference()` is not null.
- `CompositeSections.Num() == 2`.
- `CompositeSections[0].SectionName == "Start"`.
- `CompositeSections[0].NextSectionName == "End"`.

- [ ] **Step 3: Implement structured apply routing**

In `FAssetDocumentService::Apply` after reflected properties:

- If document has `Body`, find exact profile.
- For each body key, get `ResolveBodyAdapter`.
- Call `Apply` with context containing asset, class, target, source file path, and fragment compiler.
- On failure, cleanup newly created asset like property failure.

Do not hardcode `AnimMontage` fields in `FAssetDocumentService`.

- [ ] **Step 4: Implement AnimMontage skeleton/preview**

In capability:

- `Skeleton` fragment context `ExpectedBaseClass=USkeleton::StaticClass()`, `Outer=Montage`.
- Apply with `Montage->SetSkeleton(Cast<USkeleton>(Result.Object))`.
- `PreviewMesh` fragment context `ExpectedBaseClass=USkeletalMesh::StaticClass()`.
- Apply with `Montage->SetPreviewMesh(Cast<USkeletalMesh>(Result.Object))`.

- [ ] **Step 5: Implement SlotAnimTracks and AnimSegments**

Rules:

- `SlotAnimTracks` replaces all montage `SlotAnimTracks`.
- Each entry requires `SlotName` and object `AnimTrack`.
- `AnimTrack.AnimSegments` is required array if `AnimTrack` appears.
- Each segment requires `AnimReference` fragment resolving to `UAnimSequenceBase`.
- Build `FAnimSegment` and call `SetAnimReference`.
- Set numeric fields when present: `StartPos`, `AnimStartTime`, `AnimEndTime`, `AnimPlayRate`, `LoopingCount`.
- Reject `AnimPlayRate == 0`, `LoopingCount <= 0`, `AnimEndTime < AnimStartTime`.
- After segments, update montage composite length to max segment end.

- [ ] **Step 6: Implement CompositeSections**

Rules:

- `CompositeSections` replaces all montage `CompositeSections`.
- Each entry requires `SectionName`.
- Use `FCompositeSection::SetTime(LinkableTime)` for `LinkableTime`.
- Set `NextSectionName` if present.
- Reject duplicate/empty section names.
- Reject `NextSectionName` that does not exist.

- [ ] **Step 7: Implement Blend**

Rules:

- If `Blend.BlendInTime` present, update montage blend-in settings time.
- If `Blend.BlendOutTime` present, update montage blend-out settings time.
- If field names differ in UE 5.7, use reflected property lookup on `BlendIn` / `BlendOut` structs rather than hardcoded struct internals.

- [ ] **Step 8: Implement extract for structure**

`Extract` should include `Body` with:

- `Skeleton` AssetRef when present.
- `PreviewMesh` AssetRef when present.
- `SlotAnimTracks` and nested `AnimTrack.AnimSegments`.
- `CompositeSections` with `SectionName`, `LinkableTime`, `NextSectionName`.
- `Blend` with blend times.

`extract_asset_document` remains auxiliary and should not be required for authoring.

- [ ] **Step 9: Run tests**

Run UBT and:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage.Apply.Structure;Automation RunTests AssetFactory.AssetDocument.Read;Automation RunTests AssetFactory.AssetDocument.Apply;Quit" -Unattended -nop4 -nosplash
```

- [ ] **Step 10: Commit**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.* Source/AssetDocument/Private/AssetDocumentService.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp
git commit -m "feat(assetdoc): apply anim montage structured body"
```

---

## Task 6: AnimMontage Notify and NotifyState Placement

**Files:**
- Create: `Source/AssetDocument/Private/Profiles/AnimMontageNotifyPlacementAdapter.h`
- Create: `Source/AssetDocument/Private/Profiles/AnimMontageNotifyPlacementAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.*`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`

- [ ] **Step 1: Record task base**

Run `git rev-parse HEAD` and save as `TASK_BASE`.

- [ ] **Step 2: Add failing notify tests**

Add test `AssetFactory.AssetDocument.AnimMontage.Apply.Notifies`:

- Create montage document with skeleton and one segment as in Task 5.
- Add:

```json
"Notifies": [
  {
    "Time": 0.10,
    "Object": {
      "Kind": "EmbeddedObject",
      "Class": "/Script/Engine.AnimNotify"
    }
  }
],
"NotifyStates": [
  {
    "Time": 0.12,
    "Duration": 0.05,
    "Object": {
      "Kind": "EmbeddedObject",
      "Class": "/Script/AssetFactory.AssetFactoryNamedAnimNotifyState",
      "Properties": {}
    }
  }
]
```

Assertions:

- Apply succeeds.
- Montage notify events include one notify object of class `UAnimNotify`.
- Montage notify events include one notify state class `UAssetFactoryNamedAnimNotifyState`.
- Notify state duration is positive.

Add negative test:

- `NotifyStates[0].Duration = 0` fails validation with path `/Body/NotifyStates[0]/Duration`.
- `Notifies[0].Object.Class = "/Script/Engine.AnimNotifyState"` fails because expected base is `UAnimNotify`.

- [ ] **Step 3: Implement placement adapter**

`FAnimMontageNotifyPlacementAdapter` responsibilities:

- Parse `Time`.
- Parse optional `TrackIndex`, default 0.
- For notifies, call fragment compiler with `ExpectedBaseClass=UAnimNotify::StaticClass()`, `Outer=Montage`.
- For notify states, require `Duration > 0`, call fragment compiler with `ExpectedBaseClass=UAnimNotifyState::StaticClass()`, `Outer=Montage`.
- Create `FAnimNotifyEvent` entries and add to `Montage->Notifies`.
- Do not create standalone `AnimNotifyCapability`.

- [ ] **Step 4: Wire capability**

`FAnimMontageAssetDocumentCapability` should:

- Replace body-adapter-managed notifies when `Notifies` appears.
- Replace body-adapter-managed notify states when `NotifyStates` appears.
- Preserve unrelated montage data when fields are absent.

- [ ] **Step 5: Extract notify data**

Extract should output:

- `Notifies[].Time`
- `Notifies[].Object` as `EmbeddedObject` with class path and reflected writable properties if possible.
- `NotifyStates[].Time`
- `NotifyStates[].Duration`
- `NotifyStates[].Object`

Unsupported branching point fields remain deferred and must be represented as skipped metadata in result payload, not silently invented.

- [ ] **Step 6: Run tests**

Run:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
"E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimMontage.Apply.Notifies;Quit" -Unattended -nop4 -nosplash
```

- [ ] **Step 7: Commit**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimMontageNotifyPlacementAdapter.* Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.* Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp
git commit -m "feat(assetdoc): place anim montage notify fragments"
```

---

## Task 7: Diff, Schema Docs, and MCP Contract Hardening

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.*`
- Modify: `Source/AssetDocument/Private/AssetDocumentService.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentHttpRoutes.cpp`
- Modify: `MCP/src/index.ts`
- Modify: `MCP/schemas/AssetDocument.md`
- Modify: `MCP/src/broker/toolCatalog.test.ts` if needed
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp`

- [ ] **Step 1: Record task base**

Run `git rev-parse HEAD` and save as `TASK_BASE`.

- [ ] **Step 2: Add failing diff/profile/template contract tests**

Tests:

- `diff_asset_document` equivalent service call reports changed `Body` entries when slot count differs.
- `InspectProfile` for montage includes fragment kinds and internal adapters.
- `CreateTemplate` for montage returns canonical field names and no abbreviations.
- `GetSchema` includes no asset-specific MCP tools.

- [ ] **Step 3: Implement body diff**

First version may compare complete body sections by normalized JSON:

- Extract current body to JSON.
- Compare requested body section JSON with current section JSON.
- Emit arrays `changed`, `unchanged`, `skipped`, `failed` with `path` fields.
- Do not implement move detection.

Example changed entry:

```json
{
  "path": "/Body/SlotAnimTracks",
  "status": "changed"
}
```

- [ ] **Step 4: Update schema docs**

`MCP/schemas/AssetDocument.md` must document:

- `Definitions`
- fragment kinds
- profile/template workflow
- `Body` naming rule
- `UAnimMontage` profile keys
- no asset-specific MCP tools
- extract is auxiliary, not required for authoring

- [ ] **Step 5: Update MCP tool catalog tests**

If tool catalog tests assert exact tool names, update expected generic AssetDocument tools to include:

- `inspect_asset_document_profile`
- `create_asset_document_template`

Do not add any asset-specific tool names.

- [ ] **Step 6: Run tests**

Run:

```powershell
Push-Location MCP; npm test; Pop-Location
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
"E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -Unattended -nop4 -nosplash
```

- [ ] **Step 7: Commit**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimMontageAssetDocumentCapability.* Source/AssetDocument/Private/AssetDocumentService.cpp Source/AssetDocument/Private/AssetDocumentHttpRoutes.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimMontageTests.cpp MCP/src/index.ts MCP/schemas/AssetDocument.md MCP/src/broker/toolCatalog.test.ts
git commit -m "feat(assetdoc): complete structured body mcp contract"
```

---

## Task 8: Full Verification and Fix Review

**Files:**
- Modify only files needed to fix issues found by full verification.

- [ ] **Step 1: Record task base**

Run `git rev-parse HEAD` and save as `TASK_BASE`.

- [ ] **Step 2: Run full UBT**

Run:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected: build succeeds.

- [ ] **Step 3: Run MCP TypeScript tests**

Run:

```powershell
Push-Location MCP
npm test
Pop-Location
```

Expected: all MCP tests pass.

- [ ] **Step 4: Run AssetDocument automation**

Run:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -Unattended -nop4 -nosplash
```

Expected: all `AssetFactory.AssetDocument` automation tests pass.

- [ ] **Step 5: Run editor MCP smoke**

Start editor:

```powershell
Start-Process -FilePath "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" -ArgumentList "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -WindowStyle Hidden
```

Use MCP tools:

1. `health_check` until ready.
2. `get_asset_document_schema`.
3. `inspect_asset_document_profile` with `/Script/Engine.AnimMontage`.
4. `create_asset_document_template` with `/Script/Engine.AnimMontage` and `/Game/AssetDocumentSmoke/AM_MCP`.
5. `validate_asset_document` on the returned template.

Expected: all calls succeed and no asset-specific MCP tool is needed.

- [ ] **Step 6: Fix verification failures**

For any failure:

- identify whether it is code, test, environment, or host-project issue;
- fix code/test issues in the relevant files only;
- rerun the failing command;
- do not suppress tests or broaden scope.

- [ ] **Step 7: Commit final verification fixes**

If any files changed:

```powershell
git add <changed files>
git commit -m "fix(assetdoc): harden structured body verification"
```

If no files changed, do not create an empty commit.

---

## Review Plan

For each task:

1. Implementer subagent receives only the task text, relevant spec links, current worktree, branch, base commit, diff range, and allowed files.
2. Spec reviewer subagent reviews only `TASK_BASE..HEAD` against the task acceptance criteria.
3. Code quality reviewer subagent reviews only `TASK_BASE..HEAD` for correctness, maintainability, dynamic/reflection principles, and test gaps.
4. Open review findings must be fixed and re-reviewed before moving to the next task.
5. Each task ends with a checkpoint commit.

Final review:

- Review `SPEC_BASE..HEAD` after Task 8.
- Confirm no asset-specific MCP tools were added.
- Confirm no abbreviated `Body` field names were introduced.
- Confirm UBT, automation, MCP tests, and MCP smoke results are recorded.

## Verification Commands Summary

UBT:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Automation:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -Unattended -nop4 -nosplash
```

MCP:

```powershell
Push-Location MCP
npm test
Pop-Location
```

## Self-Review

- Spec coverage: profile/template/schema, fragments, no asset-specific MCP tools, no abbreviations, AnimMontage slot/section/blend/notifies, extract/diff, and final verification are each covered by tasks.
- Scope: this plan does not implement GraphIR, MaterialGraph, NiagaraGraph, AnimationBlueprint graph authoring, branching points, or independent AnimNotify capabilities.
- Placeholder scan: no unresolved placeholder markers remain; unsupported future scope is explicitly excluded.
- Type consistency: public names use `SlotAnimTracks`, `AnimTrack`, `AnimSegments`, `AnimReference`, `CompositeSections`, `SectionName`, `NextSectionName`, and `LinkableTime`.
