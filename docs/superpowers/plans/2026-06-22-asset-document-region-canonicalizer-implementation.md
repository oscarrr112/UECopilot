# AssetDocument Region Canonicalizer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a thin `FAssetDocumentRegionCanonicalizer` layer so semantically equivalent sidecar/evidence regions, starting with `Body.UbergraphPages`, produce stable sync hashes without moving region-specific logic into `SidecarSyncEngine`, `CanonicalJson`, or `DefaultReducer`.

**Architecture:** Introduce a policy-driven canonicalization hook name, a small canonicalizer registry, and a generic identity strategy that delegates to existing `FAssetDocumentCanonicalJson`. Route region hashing through the canonicalizer, then add a `UBlueprintGraph` strategy that normalizes generated graph metadata and equivalent graph schema forms for comparable hash only.

**Tech Stack:** Unreal Engine 5.7 C++ editor module, `FJsonObject` / `FJsonValue`, existing `AssetDocument` policy/profile/capability APIs, UE automation tests, validation host `C:/AVH1`.

---

## Scope

This plan implements the first RegionCanonicalizer slice:

- Generic canonicalizer API and registry.
- Policy hook plumbing.
- Hash-path integration.
- UBlueprint `Body.UbergraphPages` generated-metadata canonicalization.
- AnimSequence post-apply divergence migration out of `AssetDocumentService.cpp` and into policy-driven canonicalizer hooks.
- Apply-file smoke proving `Body.UbergraphPages` no longer skips sync rewrite for semantic graph equality.

This plan does not implement full graph semantic equivalence and does not introduce a concrete `DefaultReducer`. Because this plan touches the region hash and apply-file sync path, it must also migrate the existing AnimSequence post-apply divergence exception instead of leaving it as a service-level if-list.

## File Structure

- Create `Source/AssetDocument/Private/AssetDocumentRegionCanonicalizer.h`
  - Defines source kind, context, strategy interface, registry, canonicalize/hash helpers.
- Create `Source/AssetDocument/Private/AssetDocumentRegionCanonicalizer.cpp`
  - Implements generic identity strategy, hook-name strategy registry, builtin `UBlueprintGraph` and `AnimSequencePostApply` strategies.
- Modify `Source/AssetDocument/Public/AssetDocumentPolicy.h`
  - Adds `CanonicalizerHookName` to `FAssetDocumentRegionPolicy` and override support.
- Modify `Source/AssetDocument/Private/AssetDocumentPolicyRegistry.cpp`
  - Expands and exports `CanonicalizerHookName`.
- Modify `Source/AssetDocument/Private/AssetDocumentSidecarDelta.h/.cpp`
  - Adds source-aware hash overload and routes existing hash through canonicalizer.
- Modify `Source/AssetDocument/Private/AssetDocumentService.cpp`
  - Uses `SidecarAuthored` hash for sidecar documents and `AssetEvidence` hash for evidence documents in apply-file sync and regenerate-sidecar paths.
- Modify `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentProfile.cpp`
  - Sets `CanonicalizerHookName = "UBlueprintGraph"` for `Body.UbergraphPages` only.
- Modify `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentProfile.cpp`
  - Sets `CanonicalizerHookName = "AnimSequencePostApply"` for the AnimSequence regions that currently require the service-level divergence exception.
- Create `Source/AssetDocument/Private/Tests/AssetDocumentRegionCanonicalizerTests.cpp`
  - Generic and graph canonicalizer focused tests.
- Modify existing tests:
  - `Source/AssetDocument/Private/Tests/AssetDocumentPolicyTests.cpp`
  - `Source/AssetDocument/Private/Tests/AssetDocumentSidecarDeltaTests.cpp`
  - `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintGraphTests.cpp`
  - `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`
- Modify smoke script if needed:
  - `docs/superpowers/verification/asset_document_ublueprint_graph_http_smoke.py`
- Update docs:
  - `docs/superpowers/specs/asset-document-deferred-fields/2026-06-22-region-canonicalizer.md`
  - `docs/reports/asset-document-ublueprint-graph-regions-report.md`

## Task 1: Add Policy Hook Surface

**Files:**
- Modify: `Source/AssetDocument/Public/AssetDocumentPolicy.h`
- Modify: `Source/AssetDocument/Private/AssetDocumentPolicyRegistry.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentPolicyTests.cpp`

- [ ] **Step 1: Write the failing policy test**

Add a focused test near the existing policy override/export tests:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentPolicyCanonicalizerHookExpandsAndExportsTest,
	"AssetDocument.Policy.CanonicalizerHookExpandsAndExports",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentPolicyCanonicalizerHookExpandsAndExportsTest::RunTest(const FString& Parameters)
{
	FAssetDocumentRegionPolicyPreset Preset;
	TestTrue(TEXT("ManagedRegion preset exists"), FindPreset(FAssetDocumentPolicyRegistry::GetBuiltinPresets(), TEXT("ManagedRegion"), Preset));

	FAssetDocumentRegionPolicyOverride Override;
	Override.RegionId = TEXT("Body.UbergraphPages");
	Override.BodyPath = TEXT("Body.UbergraphPages");
	Override.CanonicalizerHookName = TEXT("UBlueprintGraph");

	FAssetDocumentRegionPolicy Policy;
	TestTrue(TEXT("Preset expands with canonicalizer hook"), FAssetDocumentPolicyRegistry::ExpandPreset(Preset, Override, Policy));
	TestEqual(TEXT("Expanded policy stores canonicalizer hook"), Policy.CanonicalizerHookName, FName(TEXT("UBlueprintGraph")));

	const TSharedRef<FJsonObject> Exported = FAssetDocumentPolicyRegistry::ExportPolicyToJson(Policy);
	TestEqual(TEXT("Exported policy includes canonicalizer hook"), Exported->GetStringField(TEXT("CanonicalizerHookName")), FString(TEXT("UBlueprintGraph")));
	return true;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetDocument.Policy.CanonicalizerHookExpandsAndExports;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/RegionCanonicalizerPolicyRed"
```

Expected: compile fails because `CanonicalizerHookName` does not exist, or automation fails if the test is temporarily compiled after adding a local stub.

- [ ] **Step 3: Add policy fields**

In `FAssetDocumentRegionPolicy`, add:

```cpp
FName CanonicalizerHookName;
```

In `FAssetDocumentRegionPolicyOverride`, add:

```cpp
TOptional<FName> CanonicalizerHookName;
```

- [ ] **Step 4: Expand and export the hook**

In `FAssetDocumentPolicyRegistry::ExpandPreset`, after existing simple override assignments, add:

```cpp
if (Override.CanonicalizerHookName.IsSet())
{
	OutPolicy.CanonicalizerHookName = Override.CanonicalizerHookName.GetValue();
}
```

In `FAssetDocumentPolicyRegistry::ExportPolicyToJson`, add:

```cpp
if (!Policy.CanonicalizerHookName.IsNone())
{
	Json->SetStringField(TEXT("CanonicalizerHookName"), Policy.CanonicalizerHookName.ToString());
}
```

- [ ] **Step 5: Run policy tests**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetDocument.Policy;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/RegionCanonicalizerPolicyGreen"
```

Expected: all `AssetDocument.Policy` tests pass.

- [ ] **Step 6: Commit**

```powershell
git add Source/AssetDocument/Public/AssetDocumentPolicy.h Source/AssetDocument/Private/AssetDocumentPolicyRegistry.cpp Source/AssetDocument/Private/Tests/AssetDocumentPolicyTests.cpp
git commit -m "feat(assetdoc): add region canonicalizer policy hook"
```

## Task 2: Add Generic Region Canonicalizer Core

**Files:**
- Create: `Source/AssetDocument/Private/AssetDocumentRegionCanonicalizer.h`
- Create: `Source/AssetDocument/Private/AssetDocumentRegionCanonicalizer.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentRegionCanonicalizerTests.cpp`

- [ ] **Step 1: Write failing generic canonicalizer tests**

Create `AssetDocumentRegionCanonicalizerTests.cpp` with generic tests:

```cpp
#include "AssetDocumentRegionCanonicalizer.h"
#include "AssetDocumentCanonicalJson.h"
#include "AssetDocumentPolicy.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
TSharedPtr<FJsonValue> ObjectValue(TSharedRef<FJsonObject> Object)
{
	return MakeShared<FJsonValueObject>(Object);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerIdentityHashMatchesCanonicalJsonTest,
	"AssetDocument.RegionCanonicalizer.IdentityHashMatchesCanonicalJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerIdentityHashMatchesCanonicalJsonTest::RunTest(const FString&)
{
	FAssetDocumentRegionPolicy Policy;
	Policy.RegionId = TEXT("Body.Blend");
	Policy.BodyPath = TEXT("Body.Blend");
	Policy.ExtractOnlyFields.Add(TEXT("_ProjectionMetrics"));

	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("Value"), 1.0);
	Object->SetStringField(TEXT("_ProjectionMetrics"), TEXT("diagnostic"));

	FAssetDocumentRegionCanonicalizeContext Context;
	Context.Policy = &Policy;
	Context.Source = EAssetDocumentRegionCanonicalizeSource::SidecarAuthored;

	TestEqual(
		TEXT("Identity canonicalizer keeps canonical json hash"),
		FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, ObjectValue(Object)),
		FAssetDocumentCanonicalJson::HashJsonValue(ObjectValue(Object), &Policy));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerWritebackKeepsAuthoredShapeTest,
	"AssetDocument.RegionCanonicalizer.WritebackKeepsAuthoredShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerWritebackKeepsAuthoredShapeTest::RunTest(const FString&)
{
	FAssetDocumentRegionPolicy Policy;
	Policy.RegionId = TEXT("Body.Blend");
	Policy.BodyPath = TEXT("Body.Blend");

	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("Value"), 1.0);
	Object->SetStringField(TEXT("_meta"), TEXT("diagnostic"));

	FAssetDocumentRegionCanonicalizeContext Context;
	Context.Policy = &Policy;
	Context.Source = EAssetDocumentRegionCanonicalizeSource::AssetEvidence;

	const TSharedPtr<FJsonValue> Writeback = FAssetDocumentRegionCanonicalizer::CanonicalizeForSidecarWriteback(Context, ObjectValue(Object));
	TestTrue(TEXT("Writeback remains object"), Writeback.IsValid() && Writeback->Type == EJson::Object);
	TestTrue(TEXT("Writeback preserves ordinary value"), Writeback->AsObject()->HasField(TEXT("Value")));
	return true;
}

#endif
```

- [ ] **Step 2: Run the tests to verify they fail**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
```

Expected: compile fails because `AssetDocumentRegionCanonicalizer.h` does not exist.

- [ ] **Step 3: Implement the core header**

Create `AssetDocumentRegionCanonicalizer.h`:

```cpp
#pragma once

#include "AssetDocumentPolicy.h"

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

enum class EAssetDocumentRegionCanonicalizeSource : uint8
{
	SidecarAuthored,
	AssetEvidence
};

struct FAssetDocumentRegionCanonicalizeContext
{
	const FAssetDocumentRegionPolicy* Policy = nullptr;
	EAssetDocumentRegionCanonicalizeSource Source = EAssetDocumentRegionCanonicalizeSource::SidecarAuthored;
	UClass* AssetClass = nullptr;
};

class IAssetDocumentRegionCanonicalizationStrategy
{
public:
	virtual ~IAssetDocumentRegionCanonicalizationStrategy() = default;
	virtual TSharedPtr<FJsonValue> CanonicalizeForHash(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const = 0;
	virtual TSharedPtr<FJsonValue> CanonicalizeForSidecarWriteback(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const = 0;
};

class FAssetDocumentRegionCanonicalizer
{
public:
	static TSharedPtr<FJsonValue> CanonicalizeForHash(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue);
	static TSharedPtr<FJsonValue> CanonicalizeForSidecarWriteback(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue);
	static FString HashRegionValue(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue);
};
```

- [ ] **Step 4: Implement the generic identity strategy**

Create `AssetDocumentRegionCanonicalizer.cpp` with a private identity strategy:

```cpp
#include "AssetDocumentRegionCanonicalizer.h"

#include "AssetDocumentCanonicalJson.h"

namespace
{
class FAssetDocumentIdentityRegionCanonicalizationStrategy final : public IAssetDocumentRegionCanonicalizationStrategy
{
public:
	virtual TSharedPtr<FJsonValue> CanonicalizeForHash(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		return FAssetDocumentCanonicalJson::CloneWithoutExtractOnlyFields(
			RegionValue,
			Context.Policy);
	}

	virtual TSharedPtr<FJsonValue> CanonicalizeForSidecarWriteback(
		const FAssetDocumentRegionCanonicalizeContext&,
		const TSharedPtr<FJsonValue>& RegionValue) const override
	{
		return FAssetDocumentCanonicalJson::CloneWithoutExtractOnlyFields(RegionValue);
	}
};

const IAssetDocumentRegionCanonicalizationStrategy& GetIdentityStrategy()
{
	static FAssetDocumentIdentityRegionCanonicalizationStrategy Strategy;
	return Strategy;
}
}

TSharedPtr<FJsonValue> FAssetDocumentRegionCanonicalizer::CanonicalizeForHash(
	const FAssetDocumentRegionCanonicalizeContext& Context,
	const TSharedPtr<FJsonValue>& RegionValue)
{
	return GetIdentityStrategy().CanonicalizeForHash(Context, RegionValue);
}

TSharedPtr<FJsonValue> FAssetDocumentRegionCanonicalizer::CanonicalizeForSidecarWriteback(
	const FAssetDocumentRegionCanonicalizeContext& Context,
	const TSharedPtr<FJsonValue>& RegionValue)
{
	return GetIdentityStrategy().CanonicalizeForSidecarWriteback(Context, RegionValue);
}

FString FAssetDocumentRegionCanonicalizer::HashRegionValue(
	const FAssetDocumentRegionCanonicalizeContext& Context,
	const TSharedPtr<FJsonValue>& RegionValue)
{
	return FAssetDocumentCanonicalJson::HashJsonValue(CanonicalizeForHash(Context, RegionValue), nullptr);
}
```

- [ ] **Step 5: Run generic tests**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetDocument.RegionCanonicalizer;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/RegionCanonicalizerCoreGreen"
```

Expected: `AssetDocument.RegionCanonicalizer` passes.

- [ ] **Step 6: Commit**

```powershell
git add Source/AssetDocument/Private/AssetDocumentRegionCanonicalizer.h Source/AssetDocument/Private/AssetDocumentRegionCanonicalizer.cpp Source/AssetDocument/Private/Tests/AssetDocumentRegionCanonicalizerTests.cpp
git commit -m "feat(assetdoc): add region canonicalizer core"
```

## Task 3: Route Region Hashing Through Canonicalizer

**Files:**
- Modify: `Source/AssetDocument/Private/AssetDocumentSidecarDelta.h`
- Modify: `Source/AssetDocument/Private/AssetDocumentSidecarDelta.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentService.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentSidecarDeltaTests.cpp`

- [ ] **Step 1: Write failing source-kind hash test**

Add to `AssetDocumentSidecarDeltaTests.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentSidecarDeltaHashAcceptsSourceKindTest,
	"AssetDocument.SidecarDelta.HashAcceptsSourceKind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentSidecarDeltaHashAcceptsSourceKindTest::RunTest(const FString&)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("Blend"), MakeShared<FJsonObject>());
	TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetObjectField(TEXT("Body"), Body);

	const FAssetDocumentRegionPolicy Policy = MakePolicy(TEXT("Body.Blend"));
	const FString SidecarHash = FAssetDocumentSidecarDelta::HashSidecarRegion(
		Document,
		Policy,
		EAssetDocumentRegionCanonicalizeSource::SidecarAuthored);
	const FString EvidenceHash = FAssetDocumentSidecarDelta::HashSidecarRegion(
		Document,
		Policy,
		EAssetDocumentRegionCanonicalizeSource::AssetEvidence);
	TestFalse(TEXT("Sidecar hash is initialized"), SidecarHash.IsEmpty());
	TestEqual(TEXT("Default source kinds hash equally"), SidecarHash, EvidenceHash);
	return true;
}
```

- [ ] **Step 2: Run compile to verify failure**

Run UBT:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
```

Expected: compile fails because the source-kind overload does not exist.

- [ ] **Step 3: Add source-kind overload**

In `AssetDocumentSidecarDelta.h`, include `AssetDocumentRegionCanonicalizer.h` and change declarations:

```cpp
static FString HashSidecarRegion(const TSharedRef<FJsonObject>& DocumentJson, const FAssetDocumentRegionPolicy& Policy);
static FString HashSidecarRegion(
	const TSharedRef<FJsonObject>& DocumentJson,
	const FAssetDocumentRegionPolicy& Policy,
	EAssetDocumentRegionCanonicalizeSource Source,
	UClass* AssetClass = nullptr);
```

In `.cpp`, implement old overload by calling the new overload with `SidecarAuthored`. In the new overload, use:

```cpp
FAssetDocumentRegionCanonicalizeContext Context;
Context.Policy = &Policy;
Context.Source = Source;
Context.AssetClass = AssetClass;
return FAssetDocumentRegionCanonicalizer::HashRegionValue(Context, Region.Value);
```

- [ ] **Step 4: Route service hash calls by source**

In `TryWriteApplyFileSyncState()`:

```cpp
const FString SidecarHash = FAssetDocumentSidecarDelta::HashSidecarRegion(
	SourceDocument.ToSharedRef(),
	Policy,
	EAssetDocumentRegionCanonicalizeSource::SidecarAuthored,
	AppliedAsset->GetClass());
const FString AssetEvidenceHash = FAssetDocumentSidecarDelta::HashSidecarRegion(
	EvidenceDocument,
	Policy,
	EAssetDocumentRegionCanonicalizeSource::AssetEvidence,
	AppliedAsset->GetClass());
```

In regenerate-sidecar path, use `AssetEvidence` for evidence hash and `SidecarAuthored` for staged sidecar hash. In extract initial sync state, keep default sidecar authored hash because the extracted document is already the authored output surface.

- [ ] **Step 5: Run delta and sync tests**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetDocument.SidecarDelta;Automation RunTests AssetDocument.SidecarSyncEngine;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/RegionCanonicalizerHashRouteGreen"
```

Expected: `SidecarDelta` and `SidecarSyncEngine` tests pass. `SidecarSyncEngine` should remain unchanged.

- [ ] **Step 6: Commit**

```powershell
git add Source/AssetDocument/Private/AssetDocumentSidecarDelta.h Source/AssetDocument/Private/AssetDocumentSidecarDelta.cpp Source/AssetDocument/Private/AssetDocumentService.cpp Source/AssetDocument/Private/Tests/AssetDocumentSidecarDeltaTests.cpp
git commit -m "feat(assetdoc): route region hashes through canonicalizer"
```

## Task 4: Implement UBlueprint Graph Hash Canonicalization

**Files:**
- Modify: `Source/AssetDocument/Private/AssetDocumentRegionCanonicalizer.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentProfile.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentRegionCanonicalizerTests.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintGraphTests.cpp`

- [ ] **Step 1: Write failing graph generated metadata test**

In `AssetDocumentRegionCanonicalizerTests.cpp`, add helper objects and test:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentRegionCanonicalizerGraphIgnoresGeneratedMetadataTest,
	"AssetDocument.RegionCanonicalizer.Graph.IgnoresGeneratedMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentRegionCanonicalizerGraphIgnoresGeneratedMetadataTest::RunTest(const FString&)
{
	FAssetDocumentRegionPolicy Policy;
	Policy.RegionId = TEXT("Body.UbergraphPages");
	Policy.BodyPath = TEXT("Body.UbergraphPages");
	Policy.RegionKind = EAssetDocumentRegionKind::Graph;
	Policy.CanonicalizerHookName = TEXT("UBlueprintGraph");

	TSharedRef<FJsonObject> SidecarNode = MakeShared<FJsonObject>();
	SidecarNode->SetStringField(TEXT("Id"), TEXT("BeginPlay"));
	SidecarNode->SetStringField(TEXT("Class"), TEXT("/Script/BlueprintGraph.K2Node_Event"));
	TSharedRef<FJsonObject> Member = MakeShared<FJsonObject>();
	Member->SetStringField(TEXT("Kind"), TEXT("Function"));
	Member->SetStringField(TEXT("OwnerClass"), TEXT("/Script/Engine.Actor"));
	Member->SetStringField(TEXT("Name"), TEXT("ReceiveBeginPlay"));
	SidecarNode->SetObjectField(TEXT("Member"), Member);

	TSharedRef<FJsonObject> EvidenceNode = MakeShared<FJsonObject>();
	EvidenceNode->Values = SidecarNode->Values;
	EvidenceNode->SetStringField(TEXT("Id"), TEXT("ReceiveBeginPlay"));
	EvidenceNode->SetStringField(TEXT("NodeGuid"), TEXT("11111111111111111111111111111111"));
	EvidenceNode->SetStringField(TEXT("Capability"), TEXT("Event"));

	TSharedRef<FJsonObject> SidecarGraph = MakeShared<FJsonObject>();
	SidecarGraph->SetStringField(TEXT("Name"), TEXT("EventGraph"));
	SidecarGraph->SetStringField(TEXT("Schema"), TEXT("/Script/BlueprintGraph.EdGraphSchema_K2"));
	SidecarGraph->SetArrayField(TEXT("Nodes"), {ObjectValue(SidecarNode)});
	SidecarGraph->SetArrayField(TEXT("Links"), {});

	TSharedRef<FJsonObject> EvidenceGraph = MakeShared<FJsonObject>();
	EvidenceGraph->Values = SidecarGraph->Values;
	EvidenceGraph->SetStringField(TEXT("GraphGuid"), TEXT("22222222222222222222222222222222"));
	EvidenceGraph->SetArrayField(TEXT("Nodes"), {ObjectValue(EvidenceNode)});

	FAssetDocumentRegionCanonicalizeContext SidecarContext;
	SidecarContext.Policy = &Policy;
	SidecarContext.Source = EAssetDocumentRegionCanonicalizeSource::SidecarAuthored;

	FAssetDocumentRegionCanonicalizeContext EvidenceContext = SidecarContext;
	EvidenceContext.Source = EAssetDocumentRegionCanonicalizeSource::AssetEvidence;

	TestEqual(
		TEXT("Generated graph metadata does not change hash"),
		FAssetDocumentRegionCanonicalizer::HashRegionValue(SidecarContext, MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ObjectValue(SidecarGraph)})),
		FAssetDocumentRegionCanonicalizer::HashRegionValue(EvidenceContext, MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ObjectValue(EvidenceGraph)})));
	return true;
}
```

- [ ] **Step 2: Run the graph canonicalizer test to verify failure**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetDocument.RegionCanonicalizer.Graph.IgnoresGeneratedMetadata;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/RegionCanonicalizerGraphRed"
```

Expected: test fails because the identity strategy hashes generated metadata.

- [ ] **Step 3: Add strategy registry lookup by hook name**

In `AssetDocumentRegionCanonicalizer.cpp`, replace direct identity access with a registry lookup keyed only by `FAssetDocumentRegionPolicy::CanonicalizerHookName`:

```cpp
const TMap<FName, const IAssetDocumentRegionCanonicalizationStrategy*>& GetBuiltinCanonicalizerStrategies()
{
	static const TMap<FName, const IAssetDocumentRegionCanonicalizationStrategy*> Strategies = {
		{FName(TEXT("UBlueprintGraph")), &GetUBlueprintGraphStrategy()},
		{FName(TEXT("AnimSequencePostApply")), &GetAnimSequencePostApplyStrategy()},
	};
	return Strategies;
}

const IAssetDocumentRegionCanonicalizationStrategy& ResolveStrategy(
	const FAssetDocumentRegionCanonicalizeContext& Context)
{
	const FName HookName = Context.Policy ? Context.Policy->CanonicalizerHookName : NAME_None;
	if (const IAssetDocumentRegionCanonicalizationStrategy* const* Strategy = GetBuiltinCanonicalizerStrategies().Find(HookName))
	{
		return **Strategy;
	}
	return GetIdentityStrategy();
}
```

Do not inspect asset class here. The dispatch key is the policy hook name, and new built-in behavior is added by registering another strategy entry rather than adding class/region branches in sync code.

- [ ] **Step 4: Implement UBlueprint graph comparable projection**

In the `UBlueprintGraph` strategy:

- Parse the graph array using `FAssetDocumentGraphParser::ParseGraphArray`.
- If parsing fails, fall back to identity canonicalization so validation/diff still reports parser diagnostics elsewhere.
- For each graph:
  - clear `GraphGuid`;
  - for each node, clear `NodeGuid` and `Capability`;
  - compute a semantic node id from class + member canonical JSON when present;
  - rewrite node `Id` to that semantic id when unique in the graph;
  - rewrite link endpoint node ids through the same map;
  - keep `Position`, `Comment`, pin defaults, class, member, and links.
- Write back using `FAssetDocumentGraphParser::WriteCanonicalGraphArray`.

Use helper functions with focused names:

```cpp
FString MakeGraphNodeSemanticKey(const FAssetDocumentNodeSpec& Node);
void RewriteGraphNodeIdsForHash(FAssetDocumentGraphSpec& Graph);
TSharedPtr<FJsonValue> CanonicalizeGraphArrayForHash(const TSharedPtr<FJsonValue>& RegionValue);
```

- [ ] **Step 5: Set UBlueprint policy hook**

In `FUBlueprintAssetDocumentProfile::GetRegionPolicies()`, after creating `Body.UbergraphPages` policy:

```cpp
Policy.CanonicalizerHookName = TEXT("UBlueprintGraph");
```

Do not set the hook for `Body.FunctionGraphs`, `Body.MacroGraphs`, or `Body.Timelines` in this task because those regions remain deferred.

- [ ] **Step 6: Add profile policy test**

In `AssetDocumentUBlueprintTests.cpp`, extend the profile policy test or add a focused test asserting:

```cpp
const FAssetDocumentRegionPolicy* UbergraphPolicy = FindPolicyByRegionId(Policies, TEXT("Body.UbergraphPages"));
TestNotNull(TEXT("Ubergraph policy exists"), UbergraphPolicy);
if (UbergraphPolicy)
{
	TestEqual(TEXT("Ubergraph uses graph canonicalizer"), UbergraphPolicy->CanonicalizerHookName, FName(TEXT("UBlueprintGraph")));
}
```

- [ ] **Step 7: Run focused tests**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetDocument.RegionCanonicalizer;Automation RunTests AssetFactory.AssetDocument.UBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/RegionCanonicalizerGraphGreen"
```

Expected: `RegionCanonicalizer` and `UBlueprint` tests pass.

- [ ] **Step 8: Commit**

```powershell
git add Source/AssetDocument/Private/AssetDocumentRegionCanonicalizer.cpp Source/AssetDocument/Private/Profiles/UBlueprintAssetDocumentProfile.cpp Source/AssetDocument/Private/Tests/AssetDocumentRegionCanonicalizerTests.cpp Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintTests.cpp
git commit -m "feat(assetdoc): canonicalize ublueprint graph region hashes"
```

## Task 5: Prove Apply-File Sync No Longer Skips UBlueprint Graphs

**Files:**
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintGraphTests.cpp`
- Modify: `docs/superpowers/verification/asset_document_ublueprint_graph_http_smoke.py`
- Modify: `docs/reports/asset-document-ublueprint-graph-regions-report.md`

- [ ] **Step 1: Write failing C++ apply-file sync test**

Add a test under `AssetFactory.AssetDocument.UBlueprint.GraphApply` that:

- writes a sidecar with `Body.UbergraphPages` using agent-friendly ids and without `GraphGuid`, `NodeGuid`, or `Capability`;
- calls `FAssetDocumentService::ApplyFile` with sidecar rewrite enabled;
- asserts `Result.IsSuccess()`;
- asserts `Result.Payload` does not contain `sidecar_sync_update_skipped`;
- reloads sidecar JSON and asserts `_meta.sync.regions.Body.UbergraphPages` exists.

Use existing service-side apply-file test helpers from `AssetDocumentUBlueprintTests.cpp` or duplicate only the minimal helper needed in `AssetDocumentUBlueprintGraphTests.cpp`.

- [ ] **Step 2: Run the new test to verify it fails before final integration**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint.GraphApply.ApplyFileWritesSyncStateForGeneratedMetadataOnlyDiff;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/RegionCanonicalizerApplyFileRed"
```

Expected before Task 4 integration: failure with `sidecar_sync_update_skipped` for `Body.UbergraphPages`. Expected after Task 4: pass.

- [ ] **Step 3: Tighten HTTP smoke expectations**

In `asset_document_ublueprint_graph_http_smoke.py`, change the current accepted payload behavior:

```python
if apply_payload.get("sidecar_sync_update_skipped") is True:
    raise RuntimeError(f"apply-file skipped sidecar sync update: {json.dumps(apply_payload, sort_keys=True)}")
```

Keep existing semantic diff normalization checks; they still protect the graph content.

- [ ] **Step 4: Run UBlueprint graph apply tests**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.UBlueprint.GraphApply;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/RegionCanonicalizerGraphApplyGreen"
```

Expected: all graph apply tests pass, including the apply-file sync test.

- [ ] **Step 5: Run HTTP smoke against editor**

Start editor:

```powershell
$p = Start-Process -FilePath "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" -ArgumentList '"C:/AVH1/AVH1.uproject"' -PassThru -WindowStyle Hidden
```

Poll health:

```powershell
Invoke-RestMethod -Uri http://127.0.0.1:8559/assetfactory/health
```

Run smoke:

```powershell
python docs/superpowers/verification/asset_document_ublueprint_graph_http_smoke.py --base-url http://127.0.0.1:8559
```

Expected: script exits 0 and does not print or accept `sidecar_sync_update_skipped=true`.

Stop editor:

```powershell
Stop-Process -Id $p.Id -Force
```

- [ ] **Step 6: Update report**

In `asset-document-ublueprint-graph-regions-report.md`, replace the known risk entry:

```text
HTTP apply payload previously returned sidecar_sync_update_skipped=true for Body.UbergraphPages generated metadata divergence. RegionCanonicalizer now normalizes the comparable hash form for generated graph metadata; HTTP smoke rejects sync skipped payloads.
```

Keep any remaining risk about deeper graph semantic identity beyond generated metadata.

- [ ] **Step 7: Commit**

```powershell
git add Source/AssetDocument/Private/Tests/AssetDocumentUBlueprintGraphTests.cpp docs/superpowers/verification/asset_document_ublueprint_graph_http_smoke.py docs/reports/asset-document-ublueprint-graph-regions-report.md
git commit -m "test(assetdoc): require graph apply-file sync rewrite"
```

## Task 6: Migrate AnimSequence Divergence To Canonicalizer Hook

**Files:**
- Modify: `Source/AssetDocument/Private/AssetDocumentRegionCanonicalizer.cpp`
- Modify: `Source/AssetDocument/Private/AssetDocumentService.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentProfile.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp`
- Modify: `docs/superpowers/specs/asset-document-deferred-fields/2026-06-22-region-canonicalizer.md`

This task is part of the same spec, not a follow-up. The implementation touches `TryWriteApplyFileSyncState()` and region hash canonicalization, so it has already triggered the maintenance-doc condition for removing the service-level AnimSequence divergence exception.

- [ ] **Step 1: Write the failing AnimSequence migration test**

In `AssetDocumentAnimSequenceTests.cpp`, update the existing apply-file sync test around the current `AllowedPostApplyCanonicalDivergenceRegions` block.

Replace the allow-list branch:

```cpp
if (!AllowedPostApplyCanonicalDivergenceRegions.Contains(RegionId))
{
	TestEqual(FString::Printf(TEXT("%s strict sync hashes match"), *RegionId), SidecarHash, AssetEvidenceHash);
}
```

with a strict assertion for all regions:

```cpp
TestEqual(FString::Printf(TEXT("%s canonical sync hashes match"), *RegionId), SidecarHash, AssetEvidenceHash);
```

Remove the local `AllowedPostApplyCanonicalDivergenceRegions` set from the test. This makes the current service-level exception fail because it writes mismatched sidecar/evidence hashes for those regions.

- [ ] **Step 2: Run the AnimSequence test to verify it fails**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence.Roundtrip;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/RegionCanonicalizerAnimSequenceRed"
```

Expected: failure on one of the previously allowed divergence regions because `sidecarHash != assetEvidenceHash`.

- [ ] **Step 3: Set AnimSequence policy hooks**

In `FAnimSequenceAssetDocumentProfile::GetRegionPolicies()`, after each currently divergent policy is created and before `Policies.Add(Policy)`, set:

```cpp
Policy.CanonicalizerHookName = TEXT("AnimSequencePostApply");
```

Apply this to:

```text
Body.Additive
Body.Compression
Body.Curves
Body.Notifies
Body.NotifyStates
Body.NotifyTracks
Body.SyncMarkers
Body.Metadata
Body.AssetUserData
```

Do not set it for strict regions:

```text
Body.References
Body.Preview
Body.Playback
Body.RootMotion
```

- [ ] **Step 4: Implement AnimSequence canonicalization strategy**

In `AssetDocumentRegionCanonicalizer.cpp`, add an `AnimSequencePostApply` strategy and register it in `GetBuiltinCanonicalizerStrategies()`.

First version behavior:

- For hash form, use the existing generic canonical JSON shape, then normalize only fields proven by the red `AssetFactory.AssetDocument.AnimSequence.Roundtrip` failure to be post-apply generated or rebuilt values.
- Do not normalize strict regions because they do not have the hook.
- Do not inspect `AssetClass`; the profile policy chooses the hook.
- Do not touch writeback form beyond existing extract-only cleanup.
- Keep one focused helper per normalized field family, named by data shape rather than region branch. Example names: `NormalizeGeneratedObjectPathFields`, `NormalizeGeneratedNotifyHandleFields`, `NormalizeGeneratedCurveCompressionFields`. Add only helpers that the red test evidence requires.

Required helper structure:

```cpp
class FAssetDocumentAnimSequencePostApplyCanonicalizationStrategy final : public IAssetDocumentRegionCanonicalizationStrategy
{
public:
	virtual TSharedPtr<FJsonValue> CanonicalizeForHash(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const override;

	virtual TSharedPtr<FJsonValue> CanonicalizeForSidecarWriteback(
		const FAssetDocumentRegionCanonicalizeContext& Context,
		const TSharedPtr<FJsonValue>& RegionValue) const override;
};
```

The implementation must be evidence-driven by the red test. If a region still mismatches, inspect the sidecar/evidence JSON for that region and add the smallest field-level normalization that preserves authored semantics. Do not replace a whole region, object, or array with a constant hash; that would hide real semantic changes.

- [ ] **Step 5: Remove service-level AnimSequence exception**

In `AssetDocumentService.cpp`:

- delete `IsAnimSequencePostApplyCanonicalDivergenceRegion`;
- delete `bAnimSequencePostImportSidecar`;
- replace the divergence check with a plain hash equality check:

```cpp
if (SidecarHash != AssetEvidenceHash)
{
	OutSkipReason = FString::Printf(TEXT("Post-apply asset evidence hash differs for region '%s'"), *Policy.RegionId.ToString());
	return false;
}
```

- [ ] **Step 6: Add profile policy assertions**

In `AssetDocumentAnimSequenceTests.cpp`, extend the policy test around the existing `FindPolicyByRegionId` checks:

```cpp
const TSet<FString> PostApplyCanonicalizedRegions = {
	TEXT("Body.Additive"),
	TEXT("Body.Compression"),
	TEXT("Body.Curves"),
	TEXT("Body.Notifies"),
	TEXT("Body.NotifyStates"),
	TEXT("Body.NotifyTracks"),
	TEXT("Body.SyncMarkers"),
	TEXT("Body.Metadata"),
	TEXT("Body.AssetUserData"),
};
for (const FString& RegionId : PostApplyCanonicalizedRegions)
{
	const FAssetDocumentRegionPolicy* Policy = FindPolicyByRegionId(Policies, FName(*RegionId));
	TestNotNull(FString::Printf(TEXT("%s policy exists"), *RegionId), Policy);
	if (Policy)
	{
		TestEqual(FString::Printf(TEXT("%s uses AnimSequence post-apply canonicalizer"), *RegionId), Policy->CanonicalizerHookName, FName(TEXT("AnimSequencePostApply")));
	}
}
```

Also assert strict regions have no hook:

```cpp
const TSet<FString> StrictRegions = {
	TEXT("Body.References"),
	TEXT("Body.Preview"),
	TEXT("Body.Playback"),
	TEXT("Body.RootMotion"),
};
for (const FString& RegionId : StrictRegions)
{
	const FAssetDocumentRegionPolicy* Policy = FindPolicyByRegionId(Policies, FName(*RegionId));
	if (Policy)
	{
		TestTrue(FString::Printf(TEXT("%s keeps identity canonicalizer"), *RegionId), Policy->CanonicalizerHookName.IsNone());
	}
}
```

- [ ] **Step 7: Run AnimSequence focused tests**

Run:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimSequence;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/RegionCanonicalizerAnimSequenceGreen"
```

Expected:

- all AnimSequence tests pass;
- apply-file sync writes sidecar state;
- every sync region records matching `sidecarHash` and `assetEvidenceHash` after canonicalization.

- [ ] **Step 8: Verify service-level exception is gone**

Run:

```powershell
rg -n "IsAnimSequencePostApplyCanonicalDivergenceRegion|/Script/Engine.AnimSequence|Body.UbergraphPages.*sidecar_sync|UBlueprint.*sidecar_sync" Source/AssetDocument/Private/AssetDocumentService.cpp
```

Expected:

- no `IsAnimSequencePostApplyCanonicalDivergenceRegion`;
- no `/Script/Engine.AnimSequence` branch in apply-file sync;
- no UBlueprint graph sync exception.

- [ ] **Step 9: Update maintenance doc status**

In `2026-06-22-region-canonicalizer.md`, under `2.2 AnimSequence service-level divergence exception`, add a dated note:

```markdown
已检查：

- 2026-06-22：RegionCanonicalizer v1 implementation 已将 AnimSequence post-apply divergence 从 `AssetDocumentService.cpp` 的 class/region if-list 迁移到 `AnimSequencePostApply` policy canonicalizer hook。后续如新增同类 divergence，必须通过 policy/capability hook，不得恢复 service-level exception。
```

- [ ] **Step 10: Run doc placeholder scan**

Run:

```powershell
rg -n "TBD|TODO|待定|未定|占位|placeholder" docs/superpowers/specs/asset-document-deferred-fields/2026-06-22-region-canonicalizer.md docs/superpowers/specs/2026-06-22-asset-document-region-canonicalizer-design.md
```

Expected: no matches.

- [ ] **Step 11: Commit**

```powershell
git add Source/AssetDocument/Private/AssetDocumentRegionCanonicalizer.cpp Source/AssetDocument/Private/AssetDocumentService.cpp Source/AssetDocument/Private/Profiles/AnimSequenceAssetDocumentProfile.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimSequenceTests.cpp docs/superpowers/specs/asset-document-deferred-fields/2026-06-22-region-canonicalizer.md
git commit -m "feat(assetdoc): migrate animsequence sync canonicalization"
```

## Task 7: Final Verification And Review Prep

**Files:**
- Modify only if verification reveals documentation mismatch:
  - `docs/reports/asset-document-ublueprint-graph-regions-report.md`

- [ ] **Step 1: Run UBT**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
```

Expected: `Result: Succeeded`.

- [ ] **Step 2: Run focused automation suites**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/AVH1/AVH1.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetDocument.RegionCanonicalizer;Automation RunTests AssetDocument.SidecarDelta;Automation RunTests AssetDocument.SidecarSyncEngine;Automation RunTests AssetFactory.AssetDocument.UBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="C:/AVH1/Saved/AutomationReports/RegionCanonicalizerFinal"
```

Expected: all requested tests pass.

- [ ] **Step 3: Run MCP tests**

```powershell
Push-Location MCP
npm test
$code = $LASTEXITCODE
Pop-Location
exit $code
```

Expected: Node test runner passes. If `MCP/dist` changes only due to build output, restore it:

```powershell
git restore -- MCP/dist
```

- [ ] **Step 4: Run HTTP smoke**

Use the same editor health and smoke commands from Task 5. Expected: smoke passes and rejects any `sidecar_sync_update_skipped=true` payload.

- [ ] **Step 5: Run clean checks**

```powershell
git diff --check
git status --short
```

Expected: no whitespace errors; status only contains intended docs/report updates if any.

- [ ] **Step 6: Dispatch final reviews**

Use a read-only spec reviewer and then a read-only code-quality reviewer. Review ranges must be:

```text
SPEC_BASE=<commit before Task 1>
SPEC_BASE..HEAD
```

Reviewer prompt must include:

- this plan path;
- `2026-06-22-asset-document-region-canonicalizer-design.md`;
- `2026-06-22-region-canonicalizer.md`;
- verification results;
- explicit instruction to check no service-level UBlueprint/graph exception was added;
- explicit instruction to check the AnimSequence service-level divergence exception was removed rather than expanded.

- [ ] **Step 7: Fix review findings or commit final report**

If reviewers find issues, fix them with focused commits and re-review. If no issues, update final report if needed and commit:

```powershell
git add docs/reports/asset-document-ublueprint-graph-regions-report.md
git commit -m "docs(assetdoc): report region canonicalizer verification"
```

If no report change is needed, do not create an empty commit.

## Completion Criteria

- `Body.UbergraphPages` apply-file no longer returns `sidecar_sync_update_skipped=true` for generated metadata/schema-form divergence.
- No UBlueprint/graph special case is added to `AssetDocumentService.cpp`.
- `FAssetDocumentSidecarSyncEngine` remains hash/state-only.
- `FAssetDocumentCanonicalJson` remains asset-agnostic.
- Region-specific behavior is driven by `CanonicalizerHookName`.
- AnimSequence post-apply divergence no longer lives in `AssetDocumentService.cpp`.
- The maintenance trigger doc is updated with v1 status.
- UBT, focused automation, MCP tests, and HTTP smoke pass.
