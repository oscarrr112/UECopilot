# UBlueprint AssetDocument Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the first authoritative `UBlueprint` AssetDocument profile so generic AssetDocument tools can inspect, template, validate, apply, extract, and diff ordinary `/Script/Engine.Blueprint` assets without adding a Blueprint generator.

**Architecture:** Add a focused `UBlueprint` profile and capability under `Source/AssetDocument/Private/Profiles`. The capability owns canonical `Body` sections for `ParentClass`, `ImplementedInterfaces`, `Variables`, `Components`, `ClassDefaults`, and explicitly protected graph/timeline sections. Implement early regions with authoritative reset/rebuild semantics, while unsupported graph/timeline regions validate as unsupported instead of being silently preserved.

**Tech Stack:** Unreal Engine 5.7 editor module C++, `UBlueprint`, `FBlueprintEditorUtils`, `FKismetEditorUtilities`, `USimpleConstructionScript`, `UInheritableComponentHandler`, AssetDocument profile/capability APIs, UE automation tests, MCP schema/profile tests.

---

## Implementation Context

**Spec:** `docs/superpowers/specs/2026-06-19-ublueprint-asset-document-design.md`

**Deferred fields:** `docs/superpowers/specs/asset-document-deferred-fields/2026-06-19-ublueprint.md`

**Base branch:** `feature/asset-document-structured-capabilities-spec`

**Implementation branch:** `feature/asset-document-ublueprint-impl`

**Implementation worktree:** `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-ublueprint-impl`

**Validation host:** prefer `C:/AVH1` with a junctioned `Plugins/UECopilot` pointing at the implementation worktree, because same-named project plugins can shadow worktree changes.

**Core constraints:**

- Do not add `BlueprintGenerator`.
- Do not route through `generate_assets`.
- Do not add a Blueprint-specific MCP tool.
- Do not treat missing sidecar entries as "preserve current asset" for any implemented `UBlueprint` region.
- Do not silently ignore unsupported graph/timeline body content.
- Keep user-facing docs, reports, and plan updates in Chinese; keep code identifiers and schema keys in English.

## File Structure

Create focused profile files:

- `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentProfile.h/.cpp`
  - Exact profile for `/Script/Engine.Blueprint`.
  - Owns document shape, template, body keys, region policies, and adapter dispatch.
- `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.h/.cpp`
  - Implements validation, preflight, apply, extract, and diff for all first-phase `UBlueprint` body keys.
  - Keeps Blueprint-specific parsing helpers private to the capability.
- `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp`
  - Automation coverage for profile inspection, template shape, validation, variables, components, unsupported graph protection, extraction, and diff.

Modify integration points:

- `Source/AssetDocument/Private/AssetDocumentModule.cpp`
  - Register `FUBlueprintAssetDocumentProfile`.
- `Source/AssetDocument/Private/AssetDocumentClassResolver.cpp`
  - Allow `/Script/Engine.Blueprint` for structured exact-profile flow without reopening GenericAsset creation for arbitrary Blueprint subclasses.
- `Source/AssetDocument/AssetDocument.Build.cs`
  - Add editor module dependencies needed for Blueprint/SCS mutation if missing, especially `BlueprintGraph` and `KismetCompiler`.
- `MCP/schemas/AssetDocument.md`
  - Document the `/Script/Engine.Blueprint` profile and its current supported body keys.

Do not touch existing `Source/AssetFactory/Private/Generators/BlueprintGenerator.cpp` except as read-only reference.

---

### Task 1: Register `UBlueprint` Profile Skeleton And Reject Unsafe Body Content

**Files:**
- Create: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentProfile.h`
- Create: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentProfile.cpp`
- Create: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.h`
- Create: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentModule.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp`

- [ ] **Step 1: Record task base**

Run:

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
git status --short
```

Expected: clean status and a non-empty `TASK_BASE`.

- [ ] **Step 2: Write failing profile registration tests**

Create `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp` with these initial tests:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentService.h"
#include "Profiles/UBlueprintAssetDocumentProfile.h"

#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintProfileTest,
	"AssetFactory.AssetDocument.UBlueprint.Profile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintProfileTest::RunTest(const FString& Parameters)
{
	FUBlueprintAssetDocumentProfile Profile;
	TestEqual(TEXT("Exact class is UBlueprint"), Profile.GetExactClass(), UBlueprint::StaticClass());

	const TArray<FName> BodyKeys = Profile.GetBodyKeys();
	TestTrue(TEXT("ParentClass body key is registered"), BodyKeys.Contains(TEXT("ParentClass")));
	TestTrue(TEXT("ImplementedInterfaces body key is registered"), BodyKeys.Contains(TEXT("ImplementedInterfaces")));
	TestTrue(TEXT("Variables body key is registered"), BodyKeys.Contains(TEXT("Variables")));
	TestTrue(TEXT("Components body key is registered"), BodyKeys.Contains(TEXT("Components")));
	TestTrue(TEXT("ClassDefaults body key is registered"), BodyKeys.Contains(TEXT("ClassDefaults")));
	TestTrue(TEXT("UbergraphPages body key is registered"), BodyKeys.Contains(TEXT("UbergraphPages")));
	TestTrue(TEXT("FunctionGraphs body key is registered"), BodyKeys.Contains(TEXT("FunctionGraphs")));
	TestTrue(TEXT("MacroGraphs body key is registered"), BodyKeys.Contains(TEXT("MacroGraphs")));
	TestTrue(TEXT("Timelines body key is registered"), BodyKeys.Contains(TEXT("Timelines")));

	FAssetDocumentTemplateContext Context;
	Context.Target = TEXT("/Game/AssetDocumentTests/BP_Template");
	Context.ClassPath = TEXT("/Script/Engine.Blueprint");
	TSharedRef<FJsonObject> Template = Profile.CreateTemplate(Context);

	TestEqual(TEXT("Template Class is Blueprint"), Template->GetStringField(TEXT("Class")), FString(TEXT("/Script/Engine.Blueprint")));
	TestEqual(TEXT("Template Target is preserved"), Template->GetStringField(TEXT("Target")), Context.Target);

	const TSharedPtr<FJsonObject>* Body = nullptr;
	TestTrue(TEXT("Template contains Body"), Template->TryGetObjectField(TEXT("Body"), Body) && Body && Body->IsValid());
	if (Body && Body->IsValid())
	{
		TestTrue(TEXT("Template Body contains ParentClass"), (*Body)->HasField(TEXT("ParentClass")));
		TestTrue(TEXT("Template Body contains Variables"), (*Body)->HasField(TEXT("Variables")));
		TestTrue(TEXT("Template Body contains Components"), (*Body)->HasField(TEXT("Components")));
	}

	const TArray<FAssetDocumentRegionPolicy> Policies = Profile.GetRegionPolicies();
	auto HasPolicy = [&Policies](FName RegionId)
	{
		for (const FAssetDocumentRegionPolicy& Policy : Policies)
		{
			if (Policy.RegionId == RegionId)
			{
				return true;
			}
		}
		return false;
	};

	TestTrue(TEXT("ParentClass has region policy"), HasPolicy(TEXT("Body.ParentClass")));
	TestTrue(TEXT("Variables has region policy"), HasPolicy(TEXT("Body.Variables")));
	TestTrue(TEXT("Components has region policy"), HasPolicy(TEXT("Body.Components")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintUnsupportedGraphProtectionTest,
	"AssetFactory.AssetDocument.UBlueprint.UnsupportedGraphProtection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintUnsupportedGraphProtectionTest::RunTest(const FString& Parameters)
{
	FUBlueprintAssetDocumentCapability Capability;
	FAssetDocumentCapabilityContext Context;
	Context.AssetClass = UBlueprint::StaticClass();

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Graphs;
	Graphs.Add(MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()));
	Body->SetArrayField(TEXT("UbergraphPages"), Graphs);

	const FAssetDocumentCapabilityResult Result = Capability.Validate(
		Context,
		StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueObject>(Body)));

	TestFalse(TEXT("Non-empty unsupported graph body fails validation"), Result.bSuccess);
	TestTrue(TEXT("Failure mentions unsupported graph region"), Result.Message.Contains(TEXT("UbergraphPages")));
	return true;
}

#endif
```

- [ ] **Step 3: Run focused test to verify it fails**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint.Profile;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/UBlueprintProfile"
```

Expected: compile failure before UBT, or automation cannot compile because new profile classes do not exist.

- [ ] **Step 4: Add profile and capability skeleton**

Create `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.h`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"

class FUBlueprintAssetDocumentCapability final : public IAssetDocumentCapability
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

Create `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/UBlueprintAssetDocumentCapability.h"

#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"

namespace
{
bool IsKnownBodyKey(const FString& BodyKey)
{
	for (const FName& KnownBodyKey : FUBlueprintAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (KnownBodyKey.ToString() == BodyKey)
		{
			return true;
		}
	}
	return false;
}

FAssetDocumentCapabilityResult BodyFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FAssetDocumentCapabilityResult RequireObjectValue(const TSharedPtr<FJsonValue>& Value, const FString& Path, TSharedPtr<FJsonObject>& OutObject)
{
	if (!Value.IsValid() || Value->Type != EJson::Object)
	{
		return BodyFailure(TEXT("Expected a JSON object"), Path, TEXT("InvalidBodySectionType"));
	}

	OutObject = Value->AsObject();
	if (!OutObject.IsValid())
	{
		return BodyFailure(TEXT("Expected a JSON object"), Path, TEXT("InvalidBodySectionType"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

bool IsNonEmptyArrayField(const TSharedRef<FJsonObject>& BodyObject, const TCHAR* FieldName)
{
	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	return BodyObject->TryGetArrayField(FieldName, Array) && Array && Array->Num() > 0;
}
}

const TArray<FName>& FUBlueprintAssetDocumentCapability::GetCanonicalBodyKeys()
{
	static const TArray<FName> Keys = {
		TEXT("ParentClass"),
		TEXT("ImplementedInterfaces"),
		TEXT("Variables"),
		TEXT("Components"),
		TEXT("ClassDefaults"),
		TEXT("UbergraphPages"),
		TEXT("FunctionGraphs"),
		TEXT("MacroGraphs"),
		TEXT("Timelines"),
		TEXT("BlueprintMetadata")
	};
	return Keys;
}

FName FUBlueprintAssetDocumentCapability::GetName() const
{
	return TEXT("UBlueprintBody");
}

TArray<FName> FUBlueprintAssetDocumentCapability::GetInternalAdapterNames() const
{
	return {TEXT("UBlueprintBody"), TEXT("UBlueprintAuthoritativeRegions")};
}

int32 FUBlueprintAssetDocumentCapability::GetApplyOrder() const
{
	return 60;
}

bool FUBlueprintAssetDocumentCapability::SupportsAsset(const UObject* Asset) const
{
	return Asset && Asset->GetClass() == UBlueprint::StaticClass();
}

bool FUBlueprintAssetDocumentCapability::SupportsClass(const UClass* AssetClass) const
{
	return AssetClass == UBlueprint::StaticClass();
}

TSharedRef<FJsonObject> FUBlueprintAssetDocumentCapability::GetSchemaHint() const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("ParentClass"), TEXT("ClassRef"));
	Schema->SetStringField(TEXT("ImplementedInterfaces"), TEXT("array<{Interface: ClassRef}>"));
	Schema->SetStringField(TEXT("Variables"), TEXT("array<{Name, Type, DefaultValue, Flags, Category, Tooltip}>"));
	Schema->SetStringField(TEXT("Components"), TEXT("array<{Key:{Name,OwnerClass}, Scope, Class, AttachTo, Root, Properties}>"));
	Schema->SetStringField(TEXT("ClassDefaults"), TEXT("object"));
	Schema->SetStringField(TEXT("UbergraphPages"), TEXT("array unsupported until graph region implementation"));
	Schema->SetStringField(TEXT("FunctionGraphs"), TEXT("array unsupported until graph region implementation"));
	Schema->SetStringField(TEXT("MacroGraphs"), TEXT("array unsupported until graph region implementation"));
	Schema->SetStringField(TEXT("Timelines"), TEXT("array unsupported until timeline region implementation"));
	return Schema;
}

FAssetDocumentCapabilityResult FUBlueprintAssetDocumentCapability::Validate(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	TSharedPtr<FJsonObject> BodyObject;
	FAssetDocumentCapabilityResult ObjectResult = RequireObjectValue(BodyJson, TEXT("Body"), BodyObject);
	if (!ObjectResult.bSuccess)
	{
		return ObjectResult;
	}
	return ValidateBodyObject(BodyObject.ToSharedRef());
}

FAssetDocumentCapabilityResult FUBlueprintAssetDocumentCapability::Preflight(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	return Validate(Context, BodyJson);
}

FAssetDocumentCapabilityResult FUBlueprintAssetDocumentCapability::Apply(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson)
{
	return Validate(Context, BodyJson);
}

FAssetDocumentCapabilityResult FUBlueprintAssetDocumentCapability::Extract(const FAssetDocumentCapabilityContext& Context, TSharedRef<FJsonObject>& OutBodyJson) const
{
	OutBodyJson->SetObjectField(TEXT("ParentClass"), MakeShared<FJsonObject>());
	OutBodyJson->SetArrayField(TEXT("ImplementedInterfaces"), {});
	OutBodyJson->SetArrayField(TEXT("Variables"), {});
	OutBodyJson->SetArrayField(TEXT("Components"), {});
	OutBodyJson->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
	OutBodyJson->SetArrayField(TEXT("UbergraphPages"), {});
	OutBodyJson->SetArrayField(TEXT("FunctionGraphs"), {});
	OutBodyJson->SetArrayField(TEXT("MacroGraphs"), {});
	OutBodyJson->SetArrayField(TEXT("Timelines"), {});
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted UBlueprint body scaffold"));
}

FAssetDocumentCapabilityResult FUBlueprintAssetDocumentCapability::Diff(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& DesiredJson, TArray<TSharedPtr<FJsonValue>>& OutDiffEntries) const
{
	return Validate(Context, DesiredJson);
}

FAssetDocumentCapabilityResult FUBlueprintAssetDocumentCapability::ValidateBodyObject(const TSharedRef<FJsonObject>& BodyObject) const
{
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BodyObject->Values)
	{
		if (!IsKnownBodyKey(Pair.Key))
		{
			return BodyFailure(FString::Printf(TEXT("Unknown UBlueprint Body key '%s'"), *Pair.Key), FString::Printf(TEXT("Body.%s"), *Pair.Key), TEXT("UnknownBodyKey"));
		}
	}

	const TCHAR* UnsupportedArrayFields[] = {
		TEXT("UbergraphPages"),
		TEXT("FunctionGraphs"),
		TEXT("MacroGraphs"),
		TEXT("Timelines")
	};

	for (const TCHAR* FieldName : UnsupportedArrayFields)
	{
		if (IsNonEmptyArrayField(BodyObject, FieldName))
		{
			return BodyFailure(
				FString::Printf(TEXT("Body.%s is reserved for a future UBlueprint graph/timeline region implementation and cannot be non-empty yet"), FieldName),
				FString::Printf(TEXT("Body.%s"), FieldName),
				TEXT("UnsupportedUBlueprintRegion"));
		}
	}

	return FAssetDocumentCapabilityResult::Success(TEXT("Validated UBlueprint body"));
}
```

Create `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentProfile.h`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"
#include "Profiles/UBlueprintAssetDocumentCapability.h"

class FUBlueprintAssetDocumentProfile final : public IAssetDocumentProfile
{
public:
	virtual UClass* GetExactClass() const override;
	virtual TSharedRef<FJsonObject> GetDocumentShape() const override;
	virtual TSharedRef<FJsonObject> CreateTemplate(const FAssetDocumentTemplateContext& Context) const override;
	virtual TArray<FName> GetBodyKeys() const override;
	virtual const IAssetDocumentCapability* ResolveBodyAdapter(FName BodyKey) const override;
	virtual TArray<FAssetDocumentRegionPolicy> GetRegionPolicies() const override;

private:
	FUBlueprintAssetDocumentCapability BodyCapability;
};
```

Create `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentProfile.cpp`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/UBlueprintAssetDocumentProfile.h"

#include "AssetDocumentPolicyRegistry.h"

#include "Engine/Blueprint.h"

namespace
{
bool MakeRegionPolicy(
	FName PresetName,
	FName RegionId,
	EAssetDocumentRegionKind RegionKind,
	TArray<FString> ManagedUePropertyPaths,
	FAssetDocumentRegionPolicy& OutPolicy)
{
	FAssetDocumentRegionPolicyPreset Preset;
	if (!FAssetDocumentPolicyRegistry::GetBuiltinPreset(PresetName, Preset))
	{
		return false;
	}

	FAssetDocumentRegionPolicyOverride Override;
	Override.RegionId = RegionId;
	Override.BodyPath = RegionId.ToString();
	Override.RegionKind = RegionKind;
	Override.ManagedUePropertyPaths = MoveTemp(ManagedUePropertyPaths);
	return FAssetDocumentPolicyRegistry::ExpandPreset(Preset, Override, OutPolicy);
}
}

UClass* FUBlueprintAssetDocumentProfile::GetExactClass() const
{
	return UBlueprint::StaticClass();
}

TSharedRef<FJsonObject> FUBlueprintAssetDocumentProfile::GetDocumentShape() const
{
	TSharedRef<FJsonObject> Shape = MakeShared<FJsonObject>();
	Shape->SetStringField(TEXT("Definitions"), TEXT("map<string, Fragment>"));
	Shape->SetStringField(TEXT("Properties"), TEXT("UBlueprint asset reflected properties"));
	Shape->SetObjectField(TEXT("Body"), BodyCapability.GetSchemaHint());
	return Shape;
}

TSharedRef<FJsonObject> FUBlueprintAssetDocumentProfile::CreateTemplate(const FAssetDocumentTemplateContext& Context) const
{
	TSharedRef<FJsonObject> ParentClass = MakeShared<FJsonObject>();
	ParentClass->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ParentClass->SetStringField(TEXT("Class"), TEXT("/Script/Engine.Actor"));

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), ParentClass);
	Body->SetArrayField(TEXT("ImplementedInterfaces"), {});
	Body->SetArrayField(TEXT("Variables"), {});
	Body->SetArrayField(TEXT("Components"), {});
	Body->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
	Body->SetArrayField(TEXT("UbergraphPages"), {});
	Body->SetArrayField(TEXT("FunctionGraphs"), {});
	Body->SetArrayField(TEXT("MacroGraphs"), {});
	Body->SetArrayField(TEXT("Timelines"), {});

	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetNumberField(TEXT("SchemaVersion"), 1);
	Template->SetStringField(TEXT("Target"), Context.Target);
	Template->SetStringField(TEXT("Class"), TEXT("/Script/Engine.Blueprint"));
	Template->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Template->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Body"), Body);
	return Template;
}

TArray<FName> FUBlueprintAssetDocumentProfile::GetBodyKeys() const
{
	return FUBlueprintAssetDocumentCapability::GetCanonicalBodyKeys();
}

const IAssetDocumentCapability* FUBlueprintAssetDocumentProfile::ResolveBodyAdapter(FName BodyKey) const
{
	if (BodyKey == TEXT("Body"))
	{
		return &BodyCapability;
	}

	for (const FName& KnownBodyKey : FUBlueprintAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (BodyKey == KnownBodyKey)
		{
			return &BodyCapability;
		}
	}
	return nullptr;
}

TArray<FAssetDocumentRegionPolicy> FUBlueprintAssetDocumentProfile::GetRegionPolicies() const
{
	TArray<FAssetDocumentRegionPolicy> Policies;
	Policies.Reserve(10);

	FAssetDocumentRegionPolicy Policy;
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.ParentClass"), EAssetDocumentRegionKind::Object, {TEXT("ParentClass")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.ImplementedInterfaces"), EAssetDocumentRegionKind::Array, {TEXT("ImplementedInterfaces")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.Variables"), EAssetDocumentRegionKind::Array, {TEXT("NewVariables")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.Components"), EAssetDocumentRegionKind::Object, {TEXT("SimpleConstructionScript"), TEXT("ComponentTemplates"), TEXT("InheritableComponentHandler")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.ClassDefaults"), EAssetDocumentRegionKind::Object, {TEXT("GeneratedClass.DefaultObject")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.UbergraphPages"), EAssetDocumentRegionKind::Array, {TEXT("UbergraphPages")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.FunctionGraphs"), EAssetDocumentRegionKind::Array, {TEXT("FunctionGraphs")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.MacroGraphs"), EAssetDocumentRegionKind::Array, {TEXT("MacroGraphs")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.Timelines"), EAssetDocumentRegionKind::Array, {TEXT("Timelines")}, Policy))
	{
		Policies.Add(Policy);
	}
	return Policies;
}
```

- [ ] **Step 5: Register the profile**

Modify `Source/AssetDocument/Private/AssetDocumentModule.cpp`:

```cpp
#include "Profiles/UBlueprintAssetDocumentProfile.h"
```

and in `StartupModule()` after the existing profile registrations:

```cpp
FAssetDocumentService::GetProfileRegistry().Register(MakeShared<FUBlueprintAssetDocumentProfile>());
```

- [ ] **Step 6: Run UBT and focused automation**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/UBlueprintTask1"
```

Expected: UBT succeeds and both UBlueprint automation tests pass.

- [ ] **Step 7: Commit Task 1**

Run:

```powershell
git status --short
git add Source/AssetDocument/Private/AssetDocumentModule.cpp Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentProfile.h Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentProfile.cpp Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.h Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp
git commit -m "feat(assetdoc): register ublueprint profile"
```

Expected: checkpoint commit created. Review range for this task is `$env:TASK_BASE..HEAD`.

---

### Task 2: Structured Document Creation For `UBlueprint`

**Files:**
- Modify: `Source/AssetDocument/Private/AssetDocumentClassResolver.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentLifecycle.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentLifecycle.h`
- Modify: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp`

- [ ] **Step 1: Record task base**

Run:

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
git status --short
```

- [ ] **Step 2: Add failing apply/create test**

Append this test to `AssetDocumentUBlueprintTests.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintCreateTest,
	"AssetFactory.AssetDocument.UBlueprint.Create",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintCreateTest::RunTest(const FString& Parameters)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_Create_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));

	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), TEXT("/Script/Engine.Blueprint"));
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());

	TSharedPtr<FJsonObject> ParentClass = MakeShared<FJsonObject>();
	ParentClass->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ParentClass->SetStringField(TEXT("Class"), TEXT("/Script/Engine.Actor"));

	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), ParentClass);
	Body->SetArrayField(TEXT("ImplementedInterfaces"), {});
	Body->SetArrayField(TEXT("Variables"), {});
	Body->SetArrayField(TEXT("Components"), {});
	Body->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
	Body->SetArrayField(TEXT("UbergraphPages"), {});
	Body->SetArrayField(TEXT("FunctionGraphs"), {});
	Body->SetArrayField(TEXT("MacroGraphs"), {});
	Body->SetArrayField(TEXT("Timelines"), {});
	Document->SetObjectField(TEXT("Body"), Body);

	FAssetDocumentService Service;
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;

	const FAssetDocumentResult Result = Service.Apply(Request);
	TestTrue(TEXT("Blueprint apply succeeds"), Result.IsSuccess());

	UObject* Created = LoadObject<UObject>(nullptr, *FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target)));
	UBlueprint* Blueprint = Cast<UBlueprint>(Created);
	TestNotNull(TEXT("Created asset is UBlueprint"), Blueprint);
	if (Blueprint)
	{
		TestEqual(TEXT("Parent class is Actor"), Blueprint->ParentClass, AActor::StaticClass());
	}
	return true;
}
```

Add the includes required by the test:

```cpp
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
```

- [ ] **Step 3: Verify failure**

Run UBT or focused automation. Expected: apply fails because GenericAsset class validation still rejects `UBlueprint` or lifecycle cannot create a Blueprint asset.

- [ ] **Step 4: Allow exact structured `UBlueprint` class resolution**

Modify `FAssetDocumentClassResolver::ValidateResolvedClass` so `UBlueprint::StaticClass()` is allowed, while other Blueprint subclasses remain rejected by GenericAsset-style validation:

```cpp
	if (Class->IsChildOf(UBlueprint::StaticClass()) && Class != UBlueprint::StaticClass())
	{
		OutError = FString::Printf(TEXT("Resolved class '%s' is a Blueprint-derived asset class that requires an exact AssetDocument profile"), *Class->GetName());
		return false;
	}
```

Keep `UMaterial` rejected.

- [ ] **Step 5: Add Blueprint lifecycle creation path**

Extend lifecycle with a Blueprint create branch that uses `FKismetEditorUtilities::CreateBlueprint` and reads the already-validated parent class from the document body. The minimal helper signature should be:

```cpp
static bool TryResolveBlueprintParentClass(const TSharedPtr<FJsonObject>& Document, UClass*& OutParentClass, FString& OutError);
```

Implementation rules:

- Read `Body.ParentClass.Kind == "ClassRef"`.
- Read `Body.ParentClass.Class` as a class path.
- Resolve with `StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath)`.
- Require parent class to be a non-abstract `UObject` subclass.
- Create the asset with:

```cpp
UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
	ParentClass,
	Package,
	*AssetName,
	BPTYPE_Normal,
	UBlueprint::StaticClass(),
	UBlueprintGeneratedClass::StaticClass());
```

- Compile once after creation.

- [ ] **Step 6: Keep update path exact**

When existing target asset is loaded for a `/Script/Engine.Blueprint` document:

```cpp
if (!ExistingAsset->IsA<UBlueprint>())
{
	return failure;
}
```

Do not reinterpret `Class` as the generated class; `Class` remains the asset class.

- [ ] **Step 7: Run verification**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint.Create;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/UBlueprintTask2"
```

Expected: create test passes.

- [ ] **Step 8: Commit Task 2**

Run:

```powershell
git add Source/AssetDocument/Private/AssetDocumentClassResolver.cpp Source/AssetDocument/Private/AssetDocumentLifecycle.cpp Source/AssetDocument/Private/AssetDocumentLifecycle.h Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp
git commit -m "feat(assetdoc): create ublueprint assets"
```

---

### Task 3: Authoritative ParentClass, Interfaces, And Variables

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.h`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp`

- [ ] **Step 1: Record task base**

Run:

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
```

- [ ] **Step 2: Add failing variables reset test**

Add a test that applies two variables, then applies a sidecar with one variable and verifies the missing variable is removed:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentUBlueprintVariablesAuthoritativeTest,
	"AssetFactory.AssetDocument.UBlueprint.VariablesAuthoritative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentUBlueprintVariablesAuthoritativeTest::RunTest(const FString& Parameters)
{
	const FString Target = FString::Printf(TEXT("/Game/AssetDocumentTests/BP_AD_Vars_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));

	auto MakeDoc = [&Target](bool bIncludeStamina)
	{
		TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
		Document->SetNumberField(TEXT("SchemaVersion"), 1);
		Document->SetStringField(TEXT("Target"), Target);
		Document->SetStringField(TEXT("Class"), TEXT("/Script/Engine.Blueprint"));
		Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
		Document->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
		Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());

		TSharedPtr<FJsonObject> ParentClass = MakeShared<FJsonObject>();
		ParentClass->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
		ParentClass->SetStringField(TEXT("Class"), TEXT("/Script/Engine.Actor"));

		TArray<TSharedPtr<FJsonValue>> Variables;
		auto AddFloatVariable = [&Variables](const TCHAR* Name, const TCHAR* DefaultValue)
		{
			TSharedPtr<FJsonObject> Type = MakeShared<FJsonObject>();
			Type->SetStringField(TEXT("PinCategory"), TEXT("real"));
			Type->SetStringField(TEXT("PinSubCategory"), TEXT("float"));

			TSharedPtr<FJsonObject> Var = MakeShared<FJsonObject>();
			Var->SetStringField(TEXT("Name"), Name);
			Var->SetObjectField(TEXT("Type"), Type);
			Var->SetStringField(TEXT("DefaultValue"), DefaultValue);
			Variables.Add(MakeShared<FJsonValueObject>(Var));
		};
		AddFloatVariable(TEXT("Health"), TEXT("100.0"));
		if (bIncludeStamina)
		{
			AddFloatVariable(TEXT("Stamina"), TEXT("50.0"));
		}

		TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetObjectField(TEXT("ParentClass"), ParentClass);
		Body->SetArrayField(TEXT("ImplementedInterfaces"), {});
		Body->SetArrayField(TEXT("Variables"), Variables);
		Body->SetArrayField(TEXT("Components"), {});
		Body->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
		Body->SetArrayField(TEXT("UbergraphPages"), {});
		Body->SetArrayField(TEXT("FunctionGraphs"), {});
		Body->SetArrayField(TEXT("MacroGraphs"), {});
		Body->SetArrayField(TEXT("Timelines"), {});
		Document->SetObjectField(TEXT("Body"), Body);
		return Document;
	};

	FAssetDocumentService Service;
	FAssetDocumentApplyRequest FirstRequest;
	FirstRequest.Document = MakeDoc(true);
	FirstRequest.bSaveAsset = false;
	TestTrue(TEXT("Initial apply succeeds"), Service.Apply(FirstRequest).IsSuccess());

	FAssetDocumentApplyRequest SecondRequest;
	SecondRequest.Document = MakeDoc(false);
	SecondRequest.bSaveAsset = false;
	TestTrue(TEXT("Second apply succeeds"), Service.Apply(SecondRequest).IsSuccess());

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target)));
	TestNotNull(TEXT("Blueprint exists"), Blueprint);
	if (Blueprint)
	{
		TestTrue(TEXT("Health remains"), Blueprint->NewVariables.ContainsByPredicate([](const FBPVariableDescription& Var) { return Var.VarName == TEXT("Health"); }));
		TestFalse(TEXT("Stamina was removed"), Blueprint->NewVariables.ContainsByPredicate([](const FBPVariableDescription& Var) { return Var.VarName == TEXT("Stamina"); }));
	}
	return true;
}
```

- [ ] **Step 3: Implement parsing helpers**

In `UBlueprintAssetDocumentCapability.cpp`, add helpers:

```cpp
FAssetDocumentCapabilityResult ReadBodyObject(const TSharedRef<FJsonValue>& BodyJson, TSharedPtr<FJsonObject>& OutBody);
FAssetDocumentCapabilityResult ReadClassRef(const TSharedPtr<FJsonObject>& Object, const FString& Path, UClass*& OutClass);
FAssetDocumentCapabilityResult ReadPinType(const TSharedPtr<FJsonObject>& TypeObject, FEdGraphPinType& OutPinType);
```

`ReadPinType` must map at least:

- `{ "PinCategory": "bool" }`
- `{ "PinCategory": "byte" }`
- `{ "PinCategory": "int" }`
- `{ "PinCategory": "int64" }`
- `{ "PinCategory": "real", "PinSubCategory": "float" }`
- `{ "PinCategory": "real", "PinSubCategory": "double" }`
- `{ "PinCategory": "name" }`
- `{ "PinCategory": "string" }`
- `{ "PinCategory": "text" }`
- `{ "PinCategory": "object", "PinSubCategoryObject": "/Script/Engine.Actor" }`
- `{ "PinCategory": "class", "PinSubCategoryObject": "/Script/Engine.Actor" }`

Use `UEdGraphSchema_K2` constants, not hardcoded engine-internal FNames where a constant exists.

- [ ] **Step 4: Implement authoritative variable apply**

Add:

```cpp
FAssetDocumentCapabilityResult ApplyVariables(UBlueprint* Blueprint, const TArray<TSharedPtr<FJsonValue>>& Variables);
```

Rules:

- Pre-parse all variables before mutating.
- Reject duplicate `Name`.
- Reject invalid type object.
- Remove all existing `Blueprint->NewVariables` not present in the sidecar using `FBlueprintEditorUtils::RemoveMemberVariable`.
- Add missing variables with `FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarName, PinType, DefaultValue)`.
- For existing variables, if type changed, remove and re-add.
- Set `DefaultValue`, `Category`, and tooltip metadata if present.
- Call `FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint)` once after changes.

- [ ] **Step 5: Implement ParentClass and interfaces validation**

For this task:

- Validate `Body.ParentClass` resolves to a non-abstract `UObject` class.
- If applying to an existing Blueprint, reject parent class changes unless all implemented regions can be rebuilt in this same apply.
- Validate `Body.ImplementedInterfaces[*].Interface` resolves to an interface class.
- Implement interface authoritative rebuild with `FBlueprintEditorUtils::ImplementNewInterface` and `FBlueprintEditorUtils::RemoveInterface`.

- [ ] **Step 6: Extract and diff variables**

Update `Extract`:

- emit `Body.ParentClass` as a `ClassRef`;
- emit `Variables` from `Blueprint->NewVariables`;
- keep graph/timeline arrays empty only when current asset also has no such authoring data, otherwise emit `_Skipped` diagnostics for unsupported graph/timeline evidence.

Update `Diff`:

- compare desired variables against extracted variables by `Name`;
- report `missing`, `extra`, and `changed` entries using the existing diff entry shape used by other capabilities.

- [ ] **Step 7: Run verification**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint.VariablesAuthoritative;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/UBlueprintTask3"
```

Expected: variables authoritative test passes.

- [ ] **Step 8: Commit Task 3**

Run:

```powershell
git add Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.h Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp
git commit -m "feat(assetdoc): apply ublueprint variables authoritatively"
```

---

### Task 4: Authoritative Owned SCS Components

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp`

- [ ] **Step 1: Record task base**

Run:

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
```

- [ ] **Step 2: Add failing owned component test**

Add an automation test that:

- applies a sidecar with `Sensor` and `Vision` owned SCS components;
- reapplies with only `Sensor`;
- verifies `Vision` is deleted;
- verifies `Sensor` has `SphereRadius` and `AttachTo` applied.

Use this sidecar component shape:

```json
{
  "Key": { "Name": "Sensor", "OwnerClass": "Self" },
  "Scope": "OwnedSCS",
  "Class": "/Script/Engine.SphereComponent",
  "AttachTo": { "Name": "DefaultSceneRoot", "OwnerClass": "Self" },
  "Properties": { "SphereRadius": 500.0 }
}
```

- [ ] **Step 3: Implement component key and class parsing**

Add private structs:

```cpp
struct FUBlueprintComponentKey
{
	FName Name;
	FString OwnerClass;
};

struct FUBlueprintComponentSpec
{
	FUBlueprintComponentKey Key;
	FString Scope;
	UClass* ComponentClass = nullptr;
	TOptional<FUBlueprintComponentKey> AttachTo;
	bool bRoot = false;
	TSharedPtr<FJsonObject> Properties;
};
```

Parsing rules:

- `Key.Name` is required.
- `Key.OwnerClass` is required.
- `Scope` must be `OwnedSCS`, `Inherited`, or `Native`.
- `OwnedSCS` requires `OwnerClass == "Self"`.
- Component class must resolve to a `UActorComponent` subclass.
- Duplicate keys fail validation.

- [ ] **Step 4: Implement owned SCS authoritative rebuild**

Add:

```cpp
FAssetDocumentCapabilityResult ApplyOwnedSCSComponents(UBlueprint* Blueprint, const TArray<FUBlueprintComponentSpec>& Specs);
```

Rules:

- Ensure `Blueprint->SimpleConstructionScript` exists for Actor-derived parent classes.
- Remove existing owned SCS nodes whose `{Name, "Self"}` key is absent from sidecar.
- Create missing owned SCS nodes with `USimpleConstructionScript::CreateNode`.
- Rebuild parent/child attachment after all nodes exist.
- Apply `Properties` to each component template with `FAssetDocumentPropertyAdapter::ApplyProperties`.
- If `Root` is true, set root using SCS APIs or the validated root replacement mechanism.
- Mark Blueprint structurally modified once.

- [ ] **Step 5: Validate non-Actor parent behavior**

If `Body.Components` contains any `OwnedSCS` component and `Blueprint->ParentClass` is not `AActor` or subclass, validation must fail with path `Body.Components`.

- [ ] **Step 6: Extract and diff owned components**

Update `Extract` to include owned SCS components with:

- `Key`
- `Scope`
- `Class`
- `AttachTo`
- `Root`
- `Properties` reduced against component class CDO or default SCS template.

Update `Diff` so missing/extra/changed owned components are reported under `Body.Components`.

- [ ] **Step 7: Run verification**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/UBlueprintTask4"
```

- [ ] **Step 8: Commit Task 4**

Run:

```powershell
git add Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp
git commit -m "feat(assetdoc): rebuild ublueprint owned components"
```

---

### Task 5: Inherited/Native Component Overrides And Class Defaults

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp`

- [ ] **Step 1: Record task base**

Run:

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
```

- [ ] **Step 2: Add failing inherited/native override test**

Create a Blueprint with parent `/Script/Engine.Character`, apply:

```json
{
  "Key": { "Name": "CharacterMovement", "OwnerClass": "/Script/Engine.Character" },
  "Scope": "Native",
  "Class": "/Script/Engine.CharacterMovementComponent",
  "Properties": { "MaxWalkSpeed": 700.0 }
}
```

Verify:

- `Blueprint->GetInheritableComponentHandler(true)` contains an override template for the component key after apply.
- Removing the entry from sidecar clears the override template.

- [ ] **Step 3: Implement inherited/native key resolution**

Add:

```cpp
FAssetDocumentCapabilityResult ResolveInheritedComponentKey(UBlueprint* Blueprint, const FUBlueprintComponentKey& PublicKey, FComponentKey& OutKey);
```

Rules:

- Resolve `OwnerClass` to a class unless it is `"Self"`.
- For parent Blueprint SCS components, construct the `FComponentKey` from the parent Blueprint and `FUCSComponentId`.
- For native components, use `UInheritableComponentHandler::FindKey(FName VariableName)` if it resolves, otherwise search component archetypes by name/class and fail if ambiguous.
- Never materialize inherited/native components as owned SCS nodes.

- [ ] **Step 4: Apply inherited/native property overrides**

For each `Scope == "Inherited"` or `Scope == "Native"`:

- create or get override template with `CreateOverridenComponentTemplate`;
- apply `Properties` using `FAssetDocumentPropertyAdapter`;
- if the sidecar omits a previously overridden inherited/native component key, call `RemoveOverridenComponentTemplate`.

- [ ] **Step 5: Implement inherited/native attach/root handling**

Implement the validated UE 5.7 route for inherited/native attach/root override if available. If the correct API is not available after targeted UE source inspection, make validation fail for inherited/native `AttachTo` or `Root` with `UnsupportedInheritedComponentAttachRoot`, and keep the deferred field document accurate by adding a dated note in this task commit.

This is the only allowed fallback for this task. Do not fake attach/root override by creating owned duplicate components.

- [ ] **Step 6: Implement ClassDefaults**

Apply `Body.ClassDefaults` to `Blueprint->GeneratedClass->GetDefaultObject()` after variables/components and after Blueprint compile creates a generated class. Missing `ClassDefaults` properties should reset to parent CDO values for properties present in the current Blueprint CDO evidence.

For initial implementation, support reflected scalar/object values already handled by `FAssetDocumentPropertyAdapter`; reject complex values that require unimplemented fragment handling with path-specific diagnostics.

- [ ] **Step 7: Run verification**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/UBlueprintTask5"
```

- [ ] **Step 8: Commit Task 5**

Run:

```powershell
git add Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp docs/superpowers/specs/asset-document-deferred-fields/2026-06-19-ublueprint.md
git commit -m "feat(assetdoc): apply ublueprint inherited component overrides"
```

---

### Task 6: MCP Schema, External Smoke, And Final Verification

**Files:**
- Modify: `MCP/schemas/AssetDocument.md`
- Modify: `MCP/src/index.ts` if profile catalog needs exact-profile schema exposure updates
- Create: `docs/superpowers/verification/asset_document_ublueprint_http_smoke.py`
- Create: `docs/reports/asset-document-ublueprint-phase1-report.md`

- [ ] **Step 1: Record task base**

Run:

```powershell
$env:TASK_BASE = (git rev-parse HEAD).Trim()
```

- [ ] **Step 2: Update schema docs**

Add `/Script/Engine.Blueprint` profile documentation to `MCP/schemas/AssetDocument.md`:

```markdown
## UBlueprint Profile

The `/Script/Engine.Blueprint` profile represents ordinary `UBlueprint` assets. It does not cover `UWidgetBlueprint`, `UAnimBlueprint`, or specialized Blueprint-derived assets.

The profile owns these canonical `Body` keys:

- `ParentClass`: `ClassRef` for the generated class parent.
- `ImplementedInterfaces`: authoritative array of implemented interface class refs.
- `Variables`: authoritative array of member variables using `FEdGraphPinType`-shaped `Type`.
- `Components`: authoritative component tree and inherited/native component override declarations keyed by `{Name, OwnerClass}`.
- `ClassDefaults`: generated CDO default value deltas.
- `UbergraphPages`, `FunctionGraphs`, `MacroGraphs`, and `Timelines`: reserved full-surface regions. Non-empty values are rejected until the graph/timeline region implementation lands.

Missing entries in implemented UBlueprint regions delete or reset the corresponding Blueprint authoring surface; they do not preserve current `.uasset` state.
```

- [ ] **Step 3: Add external smoke script**

Create `docs/superpowers/verification/asset_document_ublueprint_http_smoke.py` that:

- writes `C:/AVH1/Content/AssetDocumentSmoke/BP_BlueprintSidecarSmoke.assetdoc.json`;
- calls `/assetfactory/assetdocument/apply-file`;
- calls `/assetfactory/assetdocument/extract`;
- calls `/assetfactory/assetdocument/diff`;
- asserts the Blueprint target is `/Game/AssetDocumentSmoke/BP_BlueprintSidecarSmoke`;
- asserts variables/components appear in extract;
- asserts diff has no unexpected changed entries after apply.

Use the same HTTP helper style as `docs/superpowers/verification/asset_document_animsequence_http_smoke.py`.

- [ ] **Step 4: Run full verification**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/UBlueprintFinal"
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/AssetDocumentFull"
Push-Location MCP; npm test; Pop-Location
```

Then run the smoke against a live editor HTTP server on port `8559`:

```powershell
python docs/superpowers/verification/asset_document_ublueprint_http_smoke.py --base-url http://127.0.0.1:8559
```

- [ ] **Step 5: Write final report**

Create `docs/reports/asset-document-ublueprint-phase1-report.md` in Chinese. Include:

- branch and worktree;
- base commit and final commit;
- implemented regions;
- rejected/deferred regions;
- verification commands and results;
- smoke asset path and sidecar path;
- known risks.

- [ ] **Step 6: Commit Task 6**

Run:

```powershell
git add MCP/schemas/AssetDocument.md MCP/src/index.ts docs/superpowers/verification/asset_document_ublueprint_http_smoke.py docs/reports/asset-document-ublueprint-phase1-report.md
git commit -m "docs(assetdoc): document ublueprint profile verification"
```

---

## Final Review Requirements

After all tasks:

1. Run final `git status --short`.
2. Run final UBT and focused UBlueprint automation.
3. Run full `AssetFactory.AssetDocument` automation if time allows.
4. Run `npm test` in `MCP`.
5. Dispatch a final read-only code review over `SPEC_BASE..HEAD`, where `SPEC_BASE` is the commit before the implementation branch was created.
6. Fix all P1/P2 review findings and re-run focused verification.
7. Do not push or open a PR unless the user asks.

