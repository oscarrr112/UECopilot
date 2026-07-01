# AnimationBlueprint AssetDocument Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a complete `UAnimBlueprint` AssetDocument chain under one master plan, starting with a core profile/lifecycle milestone and continuing through graph-family milestones without reopening the asset surface inventory.

**Architecture:** Add an exact-class `UAnimBlueprint` profile that uses `FAssetDocumentBodyRegionDispatcher`, public region adapters, and thin ABP-specific hooks. Stage-gated graph-family regions are declared in the same master plan and remain deferred until their adapter gates land.

**Tech Stack:** Unreal Engine 5.7 C++, AssetDocument public region runtime, `UAnimBlueprintFactory`, `UAnimBlueprint`, `UAnimInstance`, automation tests, PowerShell UBT/Editor-Cmd verification, MCP npm tests.

---

## Source Spec

This plan implements:

```text
docs/superpowers/specs/2026-07-01-animationblueprint-asset-document-design.md
```

`SPEC_BASE` for this plan is:

```text
de6447c5ce525ebc320723cda43f07880dc35494
```

Implementation must start from an independent worktree:

```text
branch: feature/asset-document-animationblueprint-impl
worktree: E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-animationblueprint-impl
```

Before each task:

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short
```

After each task:

```powershell
git diff --check $env:TASK_BASE..HEAD
git status --short
git commit -m "<task checkpoint>"
```

Review ranges:

- Task review: `TASK_BASE..HEAD`
- Final review: `SPEC_BASE..HEAD`

## Master Milestones

This is one master plan. The full ABP target stays in this file.

1. **Core profile and lifecycle**: exact profile, template, policy surface, deferred graph gates, focused tests.
2. **Core object regions**: `ParentClass`, `TargetSkeleton`, `Template`, `Preview`, `Optimization`, `SyncGroups`.
3. **Blueprint-common region reuse**: interfaces, variables, class defaults, K2 `UbergraphPages`.
4. **Animation graph adapter gate and pilot**: define public graph-family boundary before accepting non-empty `Body.AnimGraph`.
5. **State machine and transition graph milestone**: nested graph/tree adapter work.
6. **Anim layer and parent override milestone**: layer/interface boundary and `ParentAssetOverrides`.
7. **Final smoke and cleanup**: deferred entries closed or explicitly retained, full validation.

The first implementation batch executes Milestone 1 tasks only. Later milestones remain in this same plan and must not reopen the ABP surface inventory unless a review gate explicitly changes the spec.

## File Structure

### New production files

```text
Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentProfile.h
Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentProfile.cpp
Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentCapability.h
Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentCapability.cpp
```

Responsibilities:

- `FAnimBlueprintAssetDocumentProfile`: exact class, template, body keys, region policies, `ResolveBodyAdapter`.
- `FAnimBlueprintAssetDocumentCapability`: body schema hint, dispatcher construction, validation/preflight/apply/extract/diff, ABP-specific hooks.

### Modified production files

```text
Source/AssetDocument/Private/AssetDocumentModule.cpp
Source/AssetDocument/AssetDocument.Build.cs
```

Responsibilities:

- register `FAnimBlueprintAssetDocumentProfile`.
- add required private module dependencies only if compile evidence requires them, likely `AnimGraph`, `Persona`, or animation editor modules for later milestones. Milestone 1 should first try existing `Engine`, `UnrealEd`, `BlueprintGraph`, `AssetTools` dependencies plus direct includes.

### New test and docs files

```text
Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp
docs/superpowers/specs/asset-document-deferred-fields/2026-07-01-animationblueprint.md
docs/reports/asset-document-animationblueprint-complete-region-benchmark.md
docs/superpowers/verification/run_asset_document_animationblueprint_smoke.ps1
```

Responsibilities:

- focused automation for ABP profile and lifecycle.
- deferred-field tracking for stage-gated regions.
- final evidence report and external HTTP smoke runner.

## Common Constants

Use these names unless a test reveals an existing convention conflict:

```cpp
static constexpr const TCHAR* AnimBlueprintClassPath = TEXT("/Script/Engine.AnimBlueprint");
static constexpr const TCHAR* DefaultAnimInstanceClassPath = TEXT("/Script/Engine.AnimInstance");
static constexpr const TCHAR* AnimBlueprintObjectAdapterName = TEXT("AnimBlueprintObjectRegionAdapter");
static constexpr const TCHAR* AnimBlueprintSyncGroupsAdapterName = TEXT("AnimBlueprintSyncGroupsNamedArrayRegionAdapter");
static constexpr const TCHAR* AnimBlueprintDeferredAdapterName = TEXT("AnimBlueprintDeferredRegionAdapter");
```

Canonical body keys:

```cpp
ParentClass
TargetSkeleton
Template
Preview
Optimization
SyncGroups
ImplementedInterfaces
Variables
ClassDefaults
UbergraphPages
AnimGraph
StateMachines
TransitionGraphs
AnimLayers
ParentAssetOverrides
```

Milestone 1 may include common Blueprint keys in template/profile but only has to enforce deferred graph gates and core ABP object/named-array region validation.

## Task 1: Profile Skeleton, Template, Policies, And Registration

**Files:**

- Create: `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentProfile.h`
- Create: `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentProfile.cpp`
- Create: `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentCapability.h`
- Create: `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentCapability.cpp`
- Create: `Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentModule.cpp`

**Acceptance criteria:**

- `FAnimBlueprintAssetDocumentProfile::GetExactClass()` returns `UAnimBlueprint::StaticClass()`.
- `CreateTemplate()` returns `Class=/Script/Engine.AnimBlueprint`.
- `GetBodyKeys()` includes all full target body keys listed in this plan.
- `GetRegionPolicies()` includes at least one policy per body key.
- stage-gated graph-family policies are present and marked for deferred empty/null handling.
- profile is registered in `AssetDocumentModule.cpp`.
- focused automation `AssetFactory.AssetDocument.AnimBlueprint.ProfileShape` passes.

- [ ] **Step 1: Record task base**

Run:

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short
```

Expected: clean worktree.

- [ ] **Step 2: Create failing profile shape test**

Create `Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp` with this initial test scaffold:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Profiles/AnimBlueprintAssetDocumentProfile.h"

#include "Animation/AnimBlueprint.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"

namespace
{
bool HasBodyKey(const TArray<FName>& BodyKeys, const TCHAR* Name)
{
	return BodyKeys.Contains(FName(Name));
}

const FAssetDocumentRegionPolicy* FindPolicy(const TArray<FAssetDocumentRegionPolicy>& Policies, const TCHAR* RegionId)
{
	return Policies.FindByPredicate([RegionId](const FAssetDocumentRegionPolicy& Policy)
	{
		return Policy.RegionId == FName(RegionId);
	});
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentAnimBlueprintProfileShapeTest,
	"AssetFactory.AssetDocument.AnimBlueprint.ProfileShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentAnimBlueprintProfileShapeTest::RunTest(const FString&)
{
	const FAnimBlueprintAssetDocumentProfile Profile;
	TestEqual(TEXT("Exact class is UAnimBlueprint"), Profile.GetExactClass(), UAnimBlueprint::StaticClass());

	FAssetDocumentTemplateContext TemplateContext;
	TemplateContext.Target = TEXT("/Game/AssetDocumentSmoke/ABP_AssetDocumentSmoke");
	const TSharedRef<FJsonObject> Template = Profile.CreateTemplate(TemplateContext);
	TestEqual(TEXT("Template class is AnimBlueprint"), Template->GetStringField(TEXT("Class")), FString(TEXT("/Script/Engine.AnimBlueprint")));

	const TSharedPtr<FJsonObject> Body = Template->GetObjectField(TEXT("Body"));
	TestTrue(TEXT("Template includes Body object"), Body.IsValid());
	if (Body.IsValid())
	{
		TestTrue(TEXT("Template includes ParentClass"), Body->HasTypedField<EJson::Object>(TEXT("ParentClass")));
		TestTrue(TEXT("Template includes TargetSkeleton"), Body->HasField(TEXT("TargetSkeleton")));
		TestTrue(TEXT("Template includes Template"), Body->HasTypedField<EJson::Object>(TEXT("Template")));
		TestTrue(TEXT("Template includes Preview"), Body->HasTypedField<EJson::Object>(TEXT("Preview")));
		TestTrue(TEXT("Template includes Optimization"), Body->HasTypedField<EJson::Object>(TEXT("Optimization")));
		TestTrue(TEXT("Template includes SyncGroups"), Body->HasTypedField<EJson::Array>(TEXT("SyncGroups")));
		TestTrue(TEXT("Template includes AnimGraph deferred gate"), Body->HasTypedField<EJson::Array>(TEXT("AnimGraph")));
		TestTrue(TEXT("Template includes StateMachines deferred gate"), Body->HasTypedField<EJson::Array>(TEXT("StateMachines")));
	}

	const TArray<FName> BodyKeys = Profile.GetBodyKeys();
	TestTrue(TEXT("Body keys include ParentClass"), HasBodyKey(BodyKeys, TEXT("ParentClass")));
	TestTrue(TEXT("Body keys include TargetSkeleton"), HasBodyKey(BodyKeys, TEXT("TargetSkeleton")));
	TestTrue(TEXT("Body keys include Template"), HasBodyKey(BodyKeys, TEXT("Template")));
	TestTrue(TEXT("Body keys include Preview"), HasBodyKey(BodyKeys, TEXT("Preview")));
	TestTrue(TEXT("Body keys include Optimization"), HasBodyKey(BodyKeys, TEXT("Optimization")));
	TestTrue(TEXT("Body keys include SyncGroups"), HasBodyKey(BodyKeys, TEXT("SyncGroups")));
	TestTrue(TEXT("Body keys include AnimGraph"), HasBodyKey(BodyKeys, TEXT("AnimGraph")));
	TestTrue(TEXT("Body root resolves adapter"), Profile.ResolveBodyAdapter(TEXT("Body")) != nullptr);
	TestTrue(TEXT("ParentClass resolves adapter"), Profile.ResolveBodyAdapter(TEXT("ParentClass")) != nullptr);

	const TArray<FAssetDocumentRegionPolicy> Policies = Profile.GetRegionPolicies();
	TestNotNull(TEXT("Policy includes Body.ParentClass"), FindPolicy(Policies, TEXT("Body.ParentClass")));
	TestNotNull(TEXT("Policy includes Body.TargetSkeleton"), FindPolicy(Policies, TEXT("Body.TargetSkeleton")));
	TestNotNull(TEXT("Policy includes Body.AnimGraph"), FindPolicy(Policies, TEXT("Body.AnimGraph")));
	TestNotNull(TEXT("Policy includes Body.StateMachines"), FindPolicy(Policies, TEXT("Body.StateMachines")));

	return true;
}

#endif
```

- [ ] **Step 3: Run focused test and confirm it fails to compile**

Run UBT:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -NoHotReload
```

Expected: compile fails because `AnimBlueprintAssetDocumentProfile.h` does not exist.

- [ ] **Step 4: Add minimal capability header**

Create `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentCapability.h`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"

class FAnimBlueprintAssetDocumentCapability final : public IAssetDocumentCapability
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
};
```

- [ ] **Step 5: Add minimal profile header**

Create `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentProfile.h`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "AssetDocumentProfile.h"
#include "Profiles/AnimBlueprintAssetDocumentCapability.h"

class FAnimBlueprintAssetDocumentProfile final : public IAssetDocumentProfile
{
public:
	virtual UClass* GetExactClass() const override;
	virtual TSharedRef<FJsonObject> GetDocumentShape() const override;
	virtual TSharedRef<FJsonObject> CreateTemplate(const FAssetDocumentTemplateContext& Context) const override;
	virtual TArray<FName> GetBodyKeys() const override;
	virtual const IAssetDocumentCapability* ResolveBodyAdapter(FName BodyKey) const override;
	virtual TArray<FAssetDocumentRegionPolicy> GetRegionPolicies() const override;

private:
	FAnimBlueprintAssetDocumentCapability BodyCapability;
};
```

- [ ] **Step 6: Add minimal profile implementation**

Create `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentProfile.cpp` using the existing profile pattern:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/AnimBlueprintAssetDocumentProfile.h"

#include "AssetDocumentPolicyRegistry.h"

#include "Animation/AnimBlueprint.h"
#include "Dom/JsonValue.h"

namespace
{
TArray<TSharedPtr<FJsonValue>> MakeEmptyArray()
{
	return TArray<TSharedPtr<FJsonValue>>();
}

bool MakeRegionPolicy(
	FName PresetName,
	FName RegionId,
	EAssetDocumentRegionKind RegionKind,
	TArray<FString> ManagedUePropertyPaths,
	FAssetDocumentRegionPolicy& OutPolicy,
	FName CanonicalizerHookName = NAME_None)
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
	if (ManagedUePropertyPaths.Num() > 0)
	{
		Override.ManagedUePropertyPaths = MoveTemp(ManagedUePropertyPaths);
	}
	if (!CanonicalizerHookName.IsNone())
	{
		Override.CanonicalizerHookName = CanonicalizerHookName;
	}
	return FAssetDocumentPolicyRegistry::ExpandPreset(Preset, Override, OutPolicy);
}

void MarkDeferredRegionPolicy(FAssetDocumentRegionPolicy& Policy)
{
	Policy.ExplicitDeleteValues.Add(FAssetDocumentExplicitDeleteValues::Null());
}

TSharedRef<FJsonObject> MakeClassRef(const FString& ClassPath)
{
	TSharedRef<FJsonObject> ClassRef = MakeShared<FJsonObject>();
	ClassRef->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	ClassRef->SetStringField(TEXT("Class"), ClassPath);
	return ClassRef;
}

TSharedRef<FJsonObject> MakeAssetRefOrNull(const FString& AssetPath)
{
	TSharedRef<FJsonObject> AssetRef = MakeShared<FJsonObject>();
	AssetRef->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	AssetRef->SetStringField(TEXT("Asset"), AssetPath);
	return AssetRef;
}
}

UClass* FAnimBlueprintAssetDocumentProfile::GetExactClass() const
{
	return UAnimBlueprint::StaticClass();
}

TSharedRef<FJsonObject> FAnimBlueprintAssetDocumentProfile::GetDocumentShape() const
{
	TSharedRef<FJsonObject> Shape = MakeShared<FJsonObject>();
	Shape->SetStringField(TEXT("Definitions"), TEXT("map<string, Fragment>"));
	Shape->SetStringField(TEXT("Properties"), TEXT("reflected UAnimBlueprint properties not owned by Body regions"));
	Shape->SetObjectField(TEXT("Body"), BodyCapability.GetSchemaHint());
	return Shape;
}

TSharedRef<FJsonObject> FAnimBlueprintAssetDocumentProfile::CreateTemplate(const FAssetDocumentTemplateContext& Context) const
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ParentClass"), MakeClassRef(TEXT("/Script/Engine.AnimInstance")));
	Body->SetField(TEXT("TargetSkeleton"), MakeShared<FJsonValueNull>());

	TSharedRef<FJsonObject> TemplateRegion = MakeShared<FJsonObject>();
	TemplateRegion->SetBoolField(TEXT("bIsTemplate"), false);
	Body->SetObjectField(TEXT("Template"), TemplateRegion);

	TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
	Preview->SetField(TEXT("PreviewSkeletalMesh"), MakeShared<FJsonValueNull>());
	Preview->SetField(TEXT("PreviewAnimationBlueprint"), MakeShared<FJsonValueNull>());
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintApplicationMethod"), TEXT("LinkedLayers"));
	Preview->SetStringField(TEXT("PreviewAnimationBlueprintTag"), TEXT(""));
	Body->SetObjectField(TEXT("Preview"), Preview);

	TSharedRef<FJsonObject> Optimization = MakeShared<FJsonObject>();
	Optimization->SetBoolField(TEXT("bUseMultiThreadedAnimationUpdate"), true);
	Optimization->SetBoolField(TEXT("bWarnAboutBlueprintUsage"), false);
	Optimization->SetBoolField(TEXT("bEnableLinkedAnimLayerInstanceSharing"), false);
	Body->SetObjectField(TEXT("Optimization"), Optimization);

	Body->SetArrayField(TEXT("SyncGroups"), MakeEmptyArray());
	Body->SetArrayField(TEXT("ImplementedInterfaces"), MakeEmptyArray());
	Body->SetArrayField(TEXT("Variables"), MakeEmptyArray());
	Body->SetObjectField(TEXT("ClassDefaults"), MakeShared<FJsonObject>());
	Body->SetArrayField(TEXT("UbergraphPages"), MakeEmptyArray());
	Body->SetArrayField(TEXT("AnimGraph"), MakeEmptyArray());
	Body->SetArrayField(TEXT("StateMachines"), MakeEmptyArray());
	Body->SetArrayField(TEXT("TransitionGraphs"), MakeEmptyArray());
	Body->SetArrayField(TEXT("AnimLayers"), MakeEmptyArray());
	Body->SetArrayField(TEXT("ParentAssetOverrides"), MakeEmptyArray());

	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetNumberField(TEXT("SchemaVersion"), 1);
	Template->SetStringField(TEXT("Target"), Context.Target);
	Template->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimBlueprint"));
	Template->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Template->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Body"), Body);
	return Template;
}

TArray<FName> FAnimBlueprintAssetDocumentProfile::GetBodyKeys() const
{
	return FAnimBlueprintAssetDocumentCapability::GetCanonicalBodyKeys();
}

const IAssetDocumentCapability* FAnimBlueprintAssetDocumentProfile::ResolveBodyAdapter(FName BodyKey) const
{
	if (BodyKey == TEXT("Body"))
	{
		return &BodyCapability;
	}

	for (const FName& KnownBodyKey : FAnimBlueprintAssetDocumentCapability::GetCanonicalBodyKeys())
	{
		if (BodyKey == KnownBodyKey)
		{
			return &BodyCapability;
		}
	}

	return nullptr;
}

TArray<FAssetDocumentRegionPolicy> FAnimBlueprintAssetDocumentProfile::GetRegionPolicies() const
{
	TArray<FAssetDocumentRegionPolicy> Policies;
	Policies.Reserve(FAnimBlueprintAssetDocumentCapability::GetCanonicalBodyKeys().Num());

	FAssetDocumentRegionPolicy Policy;
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.ParentClass"), EAssetDocumentRegionKind::Object, {TEXT("ParentClass")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.TargetSkeleton"), EAssetDocumentRegionKind::Object, {TEXT("TargetSkeleton")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.Template"), EAssetDocumentRegionKind::Object, {TEXT("bIsTemplate")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.Preview"), EAssetDocumentRegionKind::Object, {TEXT("PreviewSkeletalMesh"), TEXT("PreviewAnimationBlueprint")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.Optimization"), EAssetDocumentRegionKind::Object, {TEXT("bUseMultiThreadedAnimationUpdate"), TEXT("bWarnAboutBlueprintUsage"), TEXT("bEnableLinkedAnimLayerInstanceSharing")}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.SyncGroups"), EAssetDocumentRegionKind::Array, {TEXT("Groups")}, Policy))
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
	if (MakeRegionPolicy(TEXT("DefaultDiff"), TEXT("Body.ClassDefaults"), EAssetDocumentRegionKind::Object, {}, Policy))
	{
		Policies.Add(Policy);
	}
	if (MakeRegionPolicy(TEXT("ManagedRegion"), TEXT("Body.UbergraphPages"), EAssetDocumentRegionKind::Graph, {TEXT("UbergraphPages")}, Policy, TEXT("UBlueprintGraph")))
	{
		Policies.Add(Policy);
	}
	const TArray<TPair<FName, EAssetDocumentRegionKind>> DeferredRegions = {
		{TEXT("Body.AnimGraph"), EAssetDocumentRegionKind::Graph},
		{TEXT("Body.StateMachines"), EAssetDocumentRegionKind::Graph},
		{TEXT("Body.TransitionGraphs"), EAssetDocumentRegionKind::Graph},
		{TEXT("Body.AnimLayers"), EAssetDocumentRegionKind::Graph},
		{TEXT("Body.ParentAssetOverrides"), EAssetDocumentRegionKind::Array},
	};
	for (const TPair<FName, EAssetDocumentRegionKind>& DeferredRegion : DeferredRegions)
	{
		if (MakeRegionPolicy(TEXT("ManagedRegion"), DeferredRegion.Key, DeferredRegion.Value, {}, Policy))
		{
			MarkDeferredRegionPolicy(Policy);
			Policies.Add(Policy);
		}
	}
	return Policies;
}
```

- [ ] **Step 7: Add minimal capability implementation**

Create `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentCapability.cpp`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/AnimBlueprintAssetDocumentCapability.h"

#include "Animation/AnimBlueprint.h"
#include "Dom/JsonValue.h"

const TArray<FName>& FAnimBlueprintAssetDocumentCapability::GetCanonicalBodyKeys()
{
	static const TArray<FName> Keys = {
		TEXT("ParentClass"),
		TEXT("TargetSkeleton"),
		TEXT("Template"),
		TEXT("Preview"),
		TEXT("Optimization"),
		TEXT("SyncGroups"),
		TEXT("ImplementedInterfaces"),
		TEXT("Variables"),
		TEXT("ClassDefaults"),
		TEXT("UbergraphPages"),
		TEXT("AnimGraph"),
		TEXT("StateMachines"),
		TEXT("TransitionGraphs"),
		TEXT("AnimLayers"),
		TEXT("ParentAssetOverrides"),
	};
	return Keys;
}

FName FAnimBlueprintAssetDocumentCapability::GetName() const
{
	return TEXT("AnimBlueprintBody");
}

TArray<FName> FAnimBlueprintAssetDocumentCapability::GetInternalAdapterNames() const
{
	return {
		TEXT("AnimBlueprintObjectRegionAdapter"),
		TEXT("AnimBlueprintSyncGroupsNamedArrayRegionAdapter"),
		TEXT("AnimBlueprintDeferredRegionAdapter"),
		TEXT("UBlueprintGraphRegionAdapter"),
	};
}

int32 FAnimBlueprintAssetDocumentCapability::GetApplyOrder() const
{
	return 100;
}

bool FAnimBlueprintAssetDocumentCapability::SupportsAsset(const UObject* Asset) const
{
	return Asset && Asset->IsA<UAnimBlueprint>();
}

bool FAnimBlueprintAssetDocumentCapability::SupportsClass(const UClass* AssetClass) const
{
	return AssetClass == UAnimBlueprint::StaticClass();
}

TSharedRef<FJsonObject> FAnimBlueprintAssetDocumentCapability::GetSchemaHint() const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("ParentClass"), TEXT("ClassRef object; UAnimInstance child"));
	Schema->SetStringField(TEXT("TargetSkeleton"), TEXT("AssetRef object or null"));
	Schema->SetStringField(TEXT("Template"), TEXT("object { bIsTemplate }"));
	Schema->SetStringField(TEXT("Preview"), TEXT("object { PreviewSkeletalMesh, PreviewAnimationBlueprint, PreviewAnimationBlueprintApplicationMethod, PreviewAnimationBlueprintTag }"));
	Schema->SetStringField(TEXT("Optimization"), TEXT("object { bUseMultiThreadedAnimationUpdate, bWarnAboutBlueprintUsage, bEnableLinkedAnimLayerInstanceSharing }"));
	Schema->SetStringField(TEXT("SyncGroups"), TEXT("array keyed by Name"));
	Schema->SetStringField(TEXT("ImplementedInterfaces"), TEXT("Blueprint common interface array"));
	Schema->SetStringField(TEXT("Variables"), TEXT("Blueprint common variable array"));
	Schema->SetStringField(TEXT("ClassDefaults"), TEXT("Blueprint common generated CDO default diff object"));
	Schema->SetStringField(TEXT("UbergraphPages"), TEXT("Blueprint common K2 graph array"));
	Schema->SetStringField(TEXT("AnimGraph"), TEXT("stage-gated deferred animation graph array"));
	Schema->SetStringField(TEXT("StateMachines"), TEXT("stage-gated deferred state machine array"));
	Schema->SetStringField(TEXT("TransitionGraphs"), TEXT("stage-gated deferred transition graph array"));
	Schema->SetStringField(TEXT("AnimLayers"), TEXT("stage-gated deferred anim layer array"));
	Schema->SetStringField(TEXT("ParentAssetOverrides"), TEXT("stage-gated deferred parent asset override array"));
	return Schema;
}

FAssetDocumentCapabilityResult FAnimBlueprintAssetDocumentCapability::Validate(const FAssetDocumentCapabilityContext&, const TSharedRef<FJsonValue>& BodyJson) const
{
	if (BodyJson->Type != EJson::Object)
	{
		return FAssetDocumentCapabilityResult::Failure(TEXT("Body must be a JSON object"), TEXT("/Body"), TEXT("InvalidBodyType"));
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Validated AnimBlueprint Body shape"));
}

FAssetDocumentCapabilityResult FAnimBlueprintAssetDocumentCapability::Preflight(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson) const
{
	return Validate(Context, BodyJson);
}

FAssetDocumentCapabilityResult FAnimBlueprintAssetDocumentCapability::Apply(FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& BodyJson)
{
	return Validate(Context, BodyJson);
}

FAssetDocumentCapabilityResult FAnimBlueprintAssetDocumentCapability::Extract(const FAssetDocumentCapabilityContext&, TSharedRef<FJsonObject>& OutBodyJson) const
{
	for (const FName& BodyKey : GetCanonicalBodyKeys())
	{
		if (BodyKey == TEXT("ParentClass") || BodyKey == TEXT("Template") || BodyKey == TEXT("Preview") || BodyKey == TEXT("Optimization") || BodyKey == TEXT("ClassDefaults"))
		{
			OutBodyJson->SetObjectField(BodyKey.ToString(), MakeShared<FJsonObject>());
		}
		else
		{
			OutBodyJson->SetArrayField(BodyKey.ToString(), TArray<TSharedPtr<FJsonValue>>());
		}
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted AnimBlueprint Body scaffold"));
}

FAssetDocumentCapabilityResult FAnimBlueprintAssetDocumentCapability::Diff(const FAssetDocumentCapabilityContext& Context, const TSharedRef<FJsonValue>& DesiredJson, TArray<TSharedPtr<FJsonValue>>&) const
{
	return Validate(Context, DesiredJson);
}
```

The implementation in this task is intentionally scaffold-level. Later tasks replace `Validate/Preflight/Apply/Extract/Diff` internals with dispatcher-backed behavior.

- [ ] **Step 8: Register profile in module**

Modify `Source/AssetDocument/Private/AssetDocumentModule.cpp`:

```cpp
#include "Profiles/AnimBlueprintAssetDocumentProfile.h"
```

Add startup registration near existing profiles:

```cpp
FAssetDocumentService::GetProfileRegistry().Register(MakeShared<FAnimBlueprintAssetDocumentProfile>());
```

- [ ] **Step 9: Run UBT**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -NoHotReload
```

Expected: `Result: Succeeded`.

- [ ] **Step 10: Run focused automation**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl -ReportExportPath="C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/Saved/AutomationReports/AnimBlueprintTask1" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimBlueprint.ProfileShape; Quit"
```

Expected report summary: `Failed=0`, `NotRun=0`.

- [ ] **Step 11: Commit**

Run:

```powershell
git diff --check $env:TASK_BASE..HEAD
git status --short
git add Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentProfile.h Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentProfile.cpp Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentCapability.h Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentCapability.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp Source/AssetDocument/Private/AssetDocumentModule.cpp
git commit -m "feat: add animation blueprint asset document profile"
```

## Task 2: Deferred Graph Gates And Deferred Fields Doc

**Files:**

- Modify: `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentCapability.cpp`
- Create: `docs/superpowers/specs/asset-document-deferred-fields/2026-07-01-animationblueprint.md`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp`

**Acceptance criteria:**

- `AnimGraph`, `StateMachines`, `TransitionGraphs`, `AnimLayers`, `ParentAssetOverrides` accept empty arrays/null and reject non-empty values.
- rejection path/code is exact.
- deferred-fields doc records current behavior, cleanup trigger, upgrade entrypoint and verification.

- [ ] **Step 1: Add failing deferred gate test**

Append test `AssetFactory.AssetDocument.AnimBlueprint.DeferredGraphGates` that calls `Validate` with:

```json
{
  "AnimGraph": [{ "Node": "Bad" }]
}
```

Expected diagnostic:

```text
Path: /Body/AnimGraph
Code: UnsupportedAnimBlueprintRegion
```

Repeat one case for `StateMachines` and `ParentAssetOverrides`.

- [ ] **Step 2: Implement deferred gate validation**

In capability validation, require `Body` object, reject unknown body keys, and for stage-gated keys allow only null, empty array or empty object. Reuse `FAssetDocumentDeferredRegionAdapter` if possible; if not, create a profile-private helper that preserves the same exact semantics and plan a later dispatcher replacement in Task 3.

- [ ] **Step 3: Add deferred fields doc**

Create `docs/superpowers/specs/asset-document-deferred-fields/2026-07-01-animationblueprint.md` with entries for:

```text
Body.AnimGraph
Body.StateMachines
Body.TransitionGraphs
Body.AnimLayers
Body.ParentAssetOverrides
Body.FunctionGraphs
Body.MacroGraphs
UAnimBlueprintGeneratedClass / debug / pose watch / property access cache
```

Each entry must include:

```text
Current behavior
Deferred reason
Cleanup trigger
Upgrade entrypoint
Minimum verification
```

- [ ] **Step 4: Verify and commit**

Run UBT and:

```powershell
Automation RunTests AssetFactory.AssetDocument.AnimBlueprint.DeferredGraphGates
git commit -m "feat: gate animation blueprint deferred graph regions"
```

## Task 3: Dispatcher-Backed Core Object Regions

**Files:**

- Modify: `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentProfile.*`
- Modify: `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentCapability.*`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp`

**Acceptance criteria:**

- dispatcher handles known/unknown keys, apply order and object region lifecycle.
- object field schema validates `Template`, `Preview`, `Optimization`.
- `ParentClass` resolves only `UAnimInstance` subclasses.
- `TargetSkeleton` resolves `USkeleton` asset refs or null when template.

- [ ] **Step 1: Add object validation tests**

Add tests:

```text
AssetFactory.AssetDocument.AnimBlueprint.CoreObjectRegions
```

Cases:

- `/Body/ParentClass/Class` missing -> `MissingParentClass`.
- parent class not `UAnimInstance` child -> `InvalidAnimBlueprintParentClass`.
- `Template.bIsTemplate=true` with non-null `TargetSkeleton` -> `InvalidTemplateSkeleton`.
- `Preview.PreviewAnimationBlueprintApplicationMethod="Bogus"` -> `InvalidPreviewAnimationBlueprintApplicationMethod`.
- unknown `Optimization` field -> object schema unknown-field diagnostic.

- [ ] **Step 2: Implement object schema helpers**

Use `FAssetDocumentObjectFieldSchemaUtils` for:

```text
Body.Template
Body.Preview
Body.Optimization
```

Keep class/asset resolution in ABP hooks, not in schema utility.

- [ ] **Step 3: Replace scaffold validation with dispatcher**

Construct `FAssetDocumentBodyRegionDispatcher` with bindings:

```text
ParentClass order 10
TargetSkeleton order 20
Template order 30
Preview order 40
Optimization order 50
SyncGroups order 60
ImplementedInterfaces order 70
Variables order 80
ClassDefaults order 90
UbergraphPages order 100
stage-gated deferred regions order 200+
```

- [ ] **Step 4: Verify and commit**

Run:

```powershell
Automation RunTests AssetFactory.AssetDocument.AnimBlueprint.CoreObjectRegions
Automation RunTests AssetFactory.AssetDocument.RegionRuntime
git commit -m "feat: validate animation blueprint core object regions"
```

## Task 4: UAnimBlueprintFactory Create/Update Lifecycle

**Files:**

- Modify: `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentCapability.*`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp`

**Acceptance criteria:**

- applying a document can create or update a real `UAnimBlueprint`.
- create path uses `UAnimBlueprintFactory`.
- invalid create preflight does not leave a loadable asset.
- extract returns canonical core regions.

- [ ] **Step 1: Add create/update automation**

Add test:

```text
AssetFactory.AssetDocument.AnimBlueprint.CreateUpdateLifecycle
```

The test must create `/Game/AssetDocumentSmoke/ABP_AssetDocumentSmoke` through `FAssetDocumentService::Apply`, then load it as `UAnimBlueprint` and verify:

```text
TargetSkeleton
bIsTemplate
Preview mesh if authored
ParentClass
```

- [ ] **Step 2: Implement factory create hook**

Use `UAnimBlueprintFactory` with:

```cpp
Factory->BlueprintType = BPTYPE_Normal;
Factory->ParentClass = ResolvedAnimInstanceClass;
Factory->TargetSkeleton = ResolvedSkeleton;
Factory->PreviewSkeletalMesh = ResolvedPreviewMesh;
Factory->bTemplate = bIsTemplate;
```

- [ ] **Step 3: Implement update hook and compile repair**

After mutating managed regions, call the same compile/mark-dirty pattern used by `UBlueprint` / `WidgetBlueprint` capabilities. Do not save asset in unit tests unless the test intentionally uses a persistent smoke fixture.

- [ ] **Step 4: Verify and commit**

Run:

```powershell
Automation RunTests AssetFactory.AssetDocument.AnimBlueprint.CreateUpdateLifecycle
git commit -m "feat: apply animation blueprint lifecycle documents"
```

## Task 5: SyncGroups Named Array Region

**Files:**

- Modify: `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentProfile.*`
- Modify: `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentCapability.*`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp`

**Acceptance criteria:**

- `Body.SyncGroups` uses named-array identity `Name`.
- duplicate names are rejected case-insensitively.
- extract/diff paths are stable.

- [ ] **Step 1: Add SyncGroups tests**

Add test:

```text
AssetFactory.AssetDocument.AnimBlueprint.SyncGroups
```

Cases:

- valid two groups apply/extract.
- duplicate `Locomotion` / `locomotion` rejects at `/Body/SyncGroups/1/Name`.
- diff uses `/Body/SyncGroups/<Name>` or escaped stable identity path, not unstable array index except in validation diagnostic.

- [ ] **Step 2: Implement named-array config**

Create config:

```cpp
FAssetDocumentNamedArrayRegionAdapterConfig Config;
Config.Name = TEXT("AnimBlueprintSyncGroupsNamedArrayRegionAdapter");
Config.IdentityField = TEXT("Name");
Config.DuplicateIdentityCode = TEXT("DuplicateSyncGroupName");
Config.NormalizeIdentity = [](const FString& Identity) { return FName(*Identity).ToString().ToLower(); };
Config.bCanonicalizeByIdentity = false;
Config.bPreserveAuthoredApplyOrder = true;
```

- [ ] **Step 3: Implement apply/extract hooks**

Map `FAnimGroupInfo`:

```text
Name -> string
Color -> linear color object or string form matching existing property adapter conventions
```

If color serialization is not already stable, first version may preserve only `Name` and document `Color` as deferred field with cleanup trigger.

- [ ] **Step 4: Verify and commit**

Run:

```powershell
Automation RunTests AssetFactory.AssetDocument.AnimBlueprint.SyncGroups
git commit -m "feat: manage animation blueprint sync groups"
```

## Task 6: Blueprint Common Regions Reuse

**Files:**

- Create or Modify: `Source/AssetDocument/Private/Profiles/BlueprintAssetDocumentCommon.*` only if reuse requires extraction.
- Modify: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentCapability.*` only for extraction with no behavior change.
- Modify: `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentCapability.*`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp`

**Acceptance criteria:**

- ABP reuses Blueprint common behavior for interfaces, variables, class defaults and K2 `UbergraphPages`.
- large copy-paste from `FUBlueprintAssetDocumentCapability` is not allowed.
- existing UBlueprint and WidgetBlueprint tests remain green.

- [ ] **Step 1: Identify the smallest common helper extraction**

If ABP needs more than one copied block from `FUBlueprintAssetDocumentCapability`, extract common functions into `BlueprintAssetDocumentCommon.*`.

Allowed common helper responsibilities:

```text
ParentClass class-ref parse
ImplementedInterfaces parse/apply/extract/diff
Variables parse/apply/extract/diff
ClassDefaults CDO default diff
Graph wrapper staged preflight helpers
```

Forbidden:

```text
ABP TargetSkeleton
WidgetTree
Components
AnimGraph
StateMachine
```

- [ ] **Step 2: Add ABP common region tests**

Add:

```text
AssetFactory.AssetDocument.AnimBlueprint.BlueprintCommonRegions
```

Verify at least:

- `ImplementedInterfaces` validates interface classes.
- `Variables` roundtrip one simple variable.
- `ClassDefaults` applies a simple default value to generated CDO.
- empty `UbergraphPages` uses graph wrapper and keeps stage-gated AnimGraph separate.

- [ ] **Step 3: Run regression suite**

Run:

```powershell
Automation RunTests AssetFactory.AssetDocument.UBlueprint
Automation RunTests AssetFactory.AssetDocument.WidgetBlueprint
Automation RunTests AssetFactory.AssetDocument.AnimBlueprint.BlueprintCommonRegions
```

- [ ] **Step 4: Commit**

```powershell
git commit -m "feat: reuse blueprint common regions for animation blueprints"
```

## Task 7: Animation Graph Adapter Boundary Gate And Pilot

**Files:**

- Modify: `docs/superpowers/specs/2026-07-01-animationblueprint-asset-document-design.md` only if spec boundary needs refinement.
- Create: `docs/superpowers/specs/2026-07-01-animationblueprint-animgraph-adapter-design.md` if no existing graph-family adapter can cover it.
- Create or Modify production files only after the adapter boundary is reviewed.

**Acceptance criteria:**

- `Body.AnimGraph` is not implemented by ABP-private parser.
- plan records node identity, pin identity, canonicalization and diff path.
- at least one simple anim graph roundtrip is supported after the gate.

- [ ] **Step 1: Adapter boundary review gate**

Before production code for `Body.AnimGraph`, write or cite a focused adapter boundary section covering:

```text
node identity
pin identity
supported node subset
unsupported node evidence
canonical graph compare
compile/rebuild hook
diagnostic paths
```

- [ ] **Step 2: Implement pilot only after review**

Pilot target:

```text
one simple anim graph root with a supported minimal node subset
```

Do not include state machines in this task.

- [ ] **Step 3: Verify and commit**

Run:

```powershell
Automation RunTests AssetFactory.AssetDocument.AnimBlueprint.AnimGraph
Automation RunTests AssetFactory.AssetDocument.GraphCore
git commit -m "feat: pilot animation blueprint anim graph region"
```

## Task 8: State Machine And Transition Graph Milestone

**Files:** decided by Task 7 adapter boundary.

**Acceptance criteria:**

- `Body.StateMachines` and `Body.TransitionGraphs` have stable state/transition identities.
- no state machine logic is hardcoded in ABP body dispatcher.

- [ ] **Step 1: Add state machine identity tests**
- [ ] **Step 2: Implement nested state-machine adapter**
- [ ] **Step 3: Implement transition graph mapping**
- [ ] **Step 4: Verify with focused automation**
- [ ] **Step 5: Commit with message `feat: manage animation blueprint state machines`**

## Task 9: Anim Layers And Parent Asset Overrides

**Files:** decided by Task 7/8 adapter boundary.

**Acceptance criteria:**

- Anim layer support either lands in this profile as a managed region or is split into a separate exact-class/interface profile with documented reason.
- `ParentAssetOverrides` only lands after parent node GUID identity is stable.

- [ ] **Step 1: Add layer/interface boundary tests**
- [ ] **Step 2: Implement layer region or separate profile boundary**
- [ ] **Step 3: Add parent asset override tests**
- [ ] **Step 4: Implement identity-array parent override region**
- [ ] **Step 5: Commit with message `feat: manage animation blueprint layers and parent overrides`**

## Task 10: External Smoke, MCP, Final Verification, And Report

**Files:**

- Create: `docs/superpowers/verification/run_asset_document_animationblueprint_smoke.ps1`
- Create: `docs/reports/asset-document-animationblueprint-complete-region-benchmark.md`
- Modify: deferred-fields doc to close implemented regions.

**Acceptance criteria:**

- UBT passes.
- focused ABP automation passes.
- regression automation passes.
- MCP tests pass.
- external smoke proves apply-file/extract/diff for real ABP sidecar.
- final spec and code quality reviews approve `SPEC_BASE..HEAD`.

- [ ] **Step 1: Run UBT**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -NoHotReload
```

- [ ] **Step 2: Run automation**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl -ReportExportPath="C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/Saved/AutomationReports/AnimBlueprintFinal" -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimBlueprint; Quit"
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/AVH1.uproject" -Unattended -NullRHI -NoSplash -NoSound -NoSourceControl -ReportExportPath="C:/Users/HP/.config/superpowers/validation-hosts/timeline-placement-region-adapter/Saved/AutomationReports/AssetDocumentFinal" -ExecCmds="Automation RunTests AssetFactory.AssetDocument; Quit"
```

- [ ] **Step 3: Run MCP tests**

```powershell
npm --prefix MCP test
```

- [ ] **Step 4: Run external smoke**

Smoke script must:

```text
write C:/AVH1/Content/AssetDocumentSmoke/ABP_AssetDocumentSmoke.assetdoc.json
call /assetfactory/assetdocument/apply-file
call /assetfactory/assetdocument/extract
call /assetfactory/assetdocument/diff
assert Class == /Script/Engine.AnimBlueprint
assert managed Body regions are present
assert no unexpected changed entries after apply
```

- [ ] **Step 5: Dispatch final reviewers**

Final spec review prompt must use:

```text
diff range: de6447c5ce525ebc320723cda43f07880dc35494..HEAD
spec: docs/superpowers/specs/2026-07-01-animationblueprint-asset-document-design.md
plan: docs/superpowers/plans/2026-07-01-animationblueprint-asset-document-implementation.md
```

- [ ] **Step 6: Commit final report**

```powershell
git add docs/reports/asset-document-animationblueprint-complete-region-benchmark.md docs/superpowers/verification/run_asset_document_animationblueprint_smoke.ps1 docs/superpowers/specs/asset-document-deferred-fields/2026-07-01-animationblueprint.md
git commit -m "docs: report animation blueprint asset document verification"
```

## Plan Self-Review

- Spec coverage: the plan covers exact profile, lifecycle, core object/named-array regions, Blueprint common reuse, stage-gated graph-family regions, deferred tracking, verification and report.
- Placeholder scan: no unresolved placeholder markers; graph-family later milestones intentionally have boundary gates because the spec requires adapter design before production code.
- Type consistency: names use `FAnimBlueprintAssetDocumentProfile`, `FAnimBlueprintAssetDocumentCapability`, `UAnimBlueprint`, `UAnimBlueprintFactory`, and `AssetFactory.AssetDocument.AnimBlueprint`.
- Risk boundary: Task 1 is scaffold/profile only; dispatcher/object lifecycle begins in Task 3; full graph-family implementation is gated by Task 7.
