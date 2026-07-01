# AnimationBlueprint Complete Graph Semantics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement full managed authored graph semantics for `UAnimBlueprint` AssetDocument using the new recursive graph schema, NodeSpawner/reflection node materialization, common field traits, and ABP structural hooks.

**Architecture:** Extend the public AssetDocument graph core from flat K2 graphs to recursive graph-family values, then layer shared reflected field traits and NodeSpawner runtime under thin ABP region adapters. ABP-specific code owns only UE graph lifecycle, state-machine/layer ownership, compile/repair, and identity repair; node fields and node coverage remain common graph-family behavior.

**Tech Stack:** UE 5.7 editor C++, `UAnimBlueprint`, `UAnimGraphNode_*`, `UAnimationStateMachineGraph`, `FProperty`, NodeSpawner / graph action APIs, AssetDocument public region runtime, Automation tests, UBT, HTTP/MCP smoke.

---

## Source And Review Ranges

- Source spec: `docs/superpowers/specs/2026-07-02-animationblueprint-complete-graph-semantics-design.md`
- Integration base: `feature/asset-document-structured-capabilities-spec @ d47cf8d`
- Spec checkpoint: `feature/asset-document-abp-complete-graph-plan @ ba5fb04`
- Implementation branch: `feature/asset-document-abp-complete-graph-impl`
- Implementation worktree: `E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/abp-complete-graph-impl`
- `SPEC_BASE=ba5fb04`
- For each implementation task: record `TASK_BASE=HEAD`, implement, verify, checkpoint commit, then review only `TASK_BASE..HEAD`.

No task may reintroduce old root-only or empty-only schema as an authored compatibility path. Migration diagnostics are allowed; target authoring schema is the new recursive graph-family shape.

## File Responsibility Map

### Public Graph Core

- Modify: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphTypes.h`
- Modify: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphTypes.cpp`
- Modify: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphParser.h`
- Modify: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphParser.cpp`
- Modify: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphDiff.h`
- Modify: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphDiff.cpp`
- Modify: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphDefinitionResolver.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentGraphCoreTests.cpp`

Responsibility: recursive graph-family model, parser, canonical writer, semantic diff, JSON Pointer paths, and definition resolver support.

### Shared Field And Node Runtime

- Create: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphFieldRules.h`
- Create: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphFieldRules.cpp`
- Create: `Source/AssetDocument/Private/Graphs/AssetDocumentAnimationGraphRuntime.h`
- Create: `Source/AssetDocument/Private/Graphs/AssetDocumentAnimationGraphRuntime.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentAnimationGraphRuntimeTests.cpp`

Responsibility: reflected field path validation, trait resolution, staged apply ordering, NodeSpawner resolution, dynamic pin validation, skipped extract-only evidence, and structural hook boundary.

### ABP Region Adapters And Hooks

- Modify: `Source/AssetDocument/Private/Regions/AssetDocumentAnimGraphRegionAdapter.h`
- Modify: `Source/AssetDocument/Private/Regions/AssetDocumentAnimGraphRegionAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Regions/AssetDocumentAnimStateMachineRegionAdapter.h`
- Modify: `Source/AssetDocument/Private/Regions/AssetDocumentAnimStateMachineRegionAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Regions/AssetDocumentAnimParentAssetOverrideRegionAdapter.h`
- Modify: `Source/AssetDocument/Private/Regions/AssetDocumentAnimParentAssetOverrideRegionAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentProfile.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentCapability.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp`

Responsibility: connect recursive graph runtime to `Body.AnimGraph`, `Body.StateMachines`, `Body.AnimLayers`, common Blueprint graph regions, and parent override alias identity.

### Build And Verification

- Modify if required: `Source/AssetDocument/AssetDocument.Build.cs`
- Use: `docs/superpowers/verification/run_asset_document_animationblueprint_smoke.ps1`
- Create if needed: `docs/superpowers/verification/run_asset_document_animationblueprint_complete_graph_smoke.ps1`
- Report: `docs/reports/asset-document-animationblueprint-complete-graph-semantics-report.md`

Responsibility: editor module dependencies, focused automation, full AssetDocument automation, and external smoke evidence.

## Task 1: Recursive Graph-Family Core

**Files:**
- Modify: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphTypes.h`
- Modify: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphTypes.cpp`
- Modify: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphParser.h`
- Modify: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphParser.cpp`
- Modify: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphDiff.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentGraphCoreTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short
```

Expected: clean worktree.

- [ ] **Step 2: Add failing recursive graph parser tests**

Add tests to `AssetDocumentGraphCoreTests.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphCoreRecursiveGraphParserTest,
	"AssetFactory.AssetDocument.GraphCore.RecursiveGraphParser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphCoreRecursiveGraphParserTest::RunTest(const FString&)
{
	TSharedRef<FJsonObject> TransitionRule = MakeShared<FJsonObject>();
	TransitionRule->SetStringField(TEXT("Id"), TEXT("IdleToRunRule"));
	TransitionRule->SetStringField(TEXT("Kind"), TEXT("TransitionRule"));
	TransitionRule->SetArrayField(TEXT("Nodes"), {});
	TransitionRule->SetArrayField(TEXT("Links"), {});
	TransitionRule->SetArrayField(TEXT("Subgraphs"), {});

	TSharedRef<FJsonObject> Machine = MakeShared<FJsonObject>();
	Machine->SetStringField(TEXT("Id"), TEXT("Locomotion"));
	Machine->SetStringField(TEXT("Kind"), TEXT("StateMachine"));
	Machine->SetArrayField(TEXT("Nodes"), {});
	Machine->SetArrayField(TEXT("Links"), {});
	Machine->SetArrayField(TEXT("Subgraphs"), {MakeShared<FJsonValueObject>(TransitionRule)});

	TSharedRef<FJsonObject> Region = MakeShared<FJsonObject>();
	Region->SetArrayField(TEXT("Graphs"), {MakeShared<FJsonValueObject>(Machine)});

	const FAssetDocumentGraphParseResult Result =
		FAssetDocumentGraphParser::ParseGraphRegion(Region, FAssetDocumentGraphParseOptions{TEXT("/Body/StateMachines"), true});

	TestTrue(TEXT("Recursive graph parses"), Result.IsValid());
	TestEqual(TEXT("One root graph"), Result.Graphs.Num(), 1);
	TestEqual(TEXT("Root graph id"), Result.Graphs[0].Id, FString(TEXT("Locomotion")));
	TestEqual(TEXT("Nested transition rule"), Result.Graphs[0].Subgraphs[0].Kind, FString(TEXT("TransitionRule")));
	return true;
}
```

Also add negative tests for duplicate graph id, duplicate node id, invalid `Subgraphs` type, invalid `Owner` type, and canonical write preserving `Position` / `Evidence`.

- [ ] **Step 3: Run test and confirm failure**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
```

Expected before implementation: compile failure for missing `ParseGraphRegion`, `Id`, `Kind`, `Subgraphs`, or tests fail if compiled.

- [ ] **Step 4: Extend graph types**

Add fields while preserving existing K2 fields for current non-ABP graph callers:

```cpp
struct FAssetDocumentNodeSpec
{
	FString Id;
	FString NodeGuid;
	FString Class;
	FString Capability;
	FString Kind;
	TSharedPtr<FJsonObject> Spawner;
	TSharedPtr<FJsonObject> Fields;
	TSharedPtr<FJsonObject> Pins;
	TSharedPtr<FJsonObject> Member;
	TArray<FAssetDocumentPinOverrideSpec> PinOverrides;
	TSharedPtr<FJsonObject> Position;
	TSharedPtr<FJsonObject> SubgraphRefs;
	TSharedPtr<FJsonObject> Evidence;
	FString Comment;
	bool bHasComment = false;
	TSharedRef<FJsonObject> ToJsonObject() const;
};

struct FAssetDocumentGraphSpec
{
	FString Id;
	FString Kind;
	TSharedPtr<FJsonObject> Owner;
	FString Name;
	FString Schema;
	FString GraphGuid;
	FString Category;
	FString Description;
	TSharedPtr<FJsonObject> Signature;
	TSharedPtr<FJsonObject> Position;
	TSharedPtr<FJsonObject> Evidence;
	TArray<FAssetDocumentNodeSpec> Nodes;
	TArray<FAssetDocumentLinkSpec> Links;
	TArray<FAssetDocumentGraphSpec> Subgraphs;
	TSharedRef<FJsonObject> ToJsonObject() const;
};
```

- [ ] **Step 5: Implement parser/writer**

Add `FAssetDocumentGraphParser::ParseGraphRegion(const TSharedRef<FJsonObject>& RegionObject, const FAssetDocumentGraphParseOptions& Options)`:

```cpp
static FAssetDocumentGraphParseResult ParseGraphRegion(
	const TSharedRef<FJsonObject>& RegionObject,
	const FAssetDocumentGraphParseOptions& Options = FAssetDocumentGraphParseOptions());
```

Rules:

- region object must contain `Graphs` array.
- graph object must contain `Id`, `Kind`, `Nodes`, `Links`, `Subgraphs`.
- `Owner`, `Position`, `Evidence`, node `Fields`, node `Pins`, node `Spawner`, node `SubgraphRefs` are optional objects.
- duplicate `Graph.Id` among siblings fails.
- duplicate `Node.Id` inside one graph fails.
- unknown fields fail when `bRejectUnknownGraphFields` is true.

- [ ] **Step 6: Implement recursive diff paths**

Update `AssetDocumentGraphDiff.cpp` to traverse `Subgraphs` by `Graph.Id` and emit paths like:

```text
/Body/StateMachines/Graphs/Locomotion/Subgraphs/IdleToRunRule/Nodes/SpeedCheck/Fields/Variable
```

Compare semantic fields and layout fields separately by setting diff entry `Kind` or `Category` to `layout` for `Position` changes if existing diff helper supports it; otherwise include `Path` and message text `layout`.

- [ ] **Step 7: Run focused graph tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.GraphCore;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="E:/GameDev/PluginsWarehouse/Saved/AutomationReports/GraphCore"
```

Expected: all GraphCore tests pass.

- [ ] **Step 8: Commit**

```powershell
git add Source/AssetDocument/Private/Graphs Source/AssetDocument/Private/Tests/AssetDocumentGraphCoreTests.cpp
git commit -m "feat(assetdoc): add recursive graph family core"
```

## Task 2: Common Field Rules

**Files:**
- Create: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphFieldRules.h`
- Create: `Source/AssetDocument/Private/Graphs/AssetDocumentGraphFieldRules.cpp`
- Test: `Source/AssetDocument/Private/Tests/AssetDocumentAnimationGraphRuntimeTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short
```

- [ ] **Step 2: Add failing field-rule tests**

Create `AssetDocumentAnimationGraphRuntimeTests.cpp` with tests:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentGraphFieldRulesTraitResolutionTest,
	"AssetFactory.AssetDocument.AnimationGraphRuntime.FieldRules.TraitResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentGraphFieldRulesTraitResolutionTest::RunTest(const FString&)
{
	FAssetDocumentGraphFieldRuleContext Context;
	Context.JsonPath = TEXT("/Body/AnimGraph/Graphs/AnimGraph/Nodes/IdlePlayer/Fields/Node.Sequence");
	Context.FieldPath = TEXT("Node.Sequence");
	Context.RequiredObjectClass = UAnimationAsset::StaticClass();

	TSharedRef<FJsonObject> AssetRef = MakeShared<FJsonObject>();
	AssetRef->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	AssetRef->SetStringField(TEXT("Path"), TEXT("/Game/DoesNotNeedToExistForShape"));

	FAssetDocumentGraphFieldRuleResult Result =
		FAssetDocumentGraphFieldRules::ValidateTraitShape(Context, EAssetDocumentGraphFieldTrait::AssetRef, MakeShared<FJsonValueObject>(AssetRef));

	TestTrue(TEXT("AssetRef trait shape validates"), Result.bSuccess);
	return true;
}
```

Add tests for ambiguous trait, raw string forbidden for `AssetRef`, default omission for primitive field, JSON pointer escaping, staged apply order list.

- [ ] **Step 3: Implement field-rule API**

Add:

```cpp
enum class EAssetDocumentGraphFieldTrait : uint8
{
	Raw,
	AssetRef,
	ClassRef,
	Name,
	SlotName,
	SyncGroupName,
	CachedPoseName,
	LayerName,
	BoneName,
	CurveName,
	GameplayTag,
	Enum,
	Color,
	Vector
};

struct FAssetDocumentGraphFieldRuleContext
{
	FString JsonPath;
	FString FieldPath;
	FString OwnerGraphKind;
	TWeakObjectPtr<UClass> RequiredObjectClass;
	TWeakObjectPtr<UClass> RequiredClassClass;
};

struct FAssetDocumentGraphFieldRuleResult
{
	bool bSuccess = true;
	FString Code;
	FString Path;
	FString Message;
	static FAssetDocumentGraphFieldRuleResult Success();
	static FAssetDocumentGraphFieldRuleResult Failure(const FString& InCode, const FString& InPath, const FString& InMessage);
};

class FAssetDocumentGraphFieldRules
{
public:
	static FAssetDocumentGraphFieldRuleResult ValidateTraitShape(
		const FAssetDocumentGraphFieldRuleContext& Context,
		EAssetDocumentGraphFieldTrait Trait,
		const TSharedPtr<FJsonValue>& Value);

	static bool ShouldOmitDefaultField(
		const TSharedPtr<FJsonValue>& Value,
		const TSharedPtr<FJsonValue>& DefaultValue,
		bool bFieldAffectsIdentityOrPins);
};
```

- [ ] **Step 4: Run focused tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimationGraphRuntime.FieldRules;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="E:/GameDev/PluginsWarehouse/Saved/AutomationReports/AnimationGraphFieldRules"
```

Expected: field-rule tests pass.

- [ ] **Step 5: Commit**

```powershell
git add Source/AssetDocument/Private/Graphs/AssetDocumentGraphFieldRules.* Source/AssetDocument/Private/Tests/AssetDocumentAnimationGraphRuntimeTests.cpp
git commit -m "feat(assetdoc): add graph field rule utilities"
```

## Task 3: Animation Graph Runtime Shell

**Files:**
- Create: `Source/AssetDocument/Private/Graphs/AssetDocumentAnimationGraphRuntime.h`
- Create: `Source/AssetDocument/Private/Graphs/AssetDocumentAnimationGraphRuntime.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimationGraphRuntimeTests.cpp`
- Modify if needed: `Source/AssetDocument/AssetDocument.Build.cs`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short
```

- [ ] **Step 2: Add failing runtime contract tests**

Add tests under `AssetFactory.AssetDocument.AnimationGraphRuntime.NodeRules`:

- `Node.Class` with no spawner resolves only when one candidate exists.
- duplicate candidates require `Node.Spawner`.
- unspawnable class fails preflight.
- dynamic pins are validated after field application.
- skipped extract-only node evidence is emitted.
- structural hook is invoked for graph ownership but never receives node field parsing.

Use fake hook structs where possible so this task does not require real `UAnimGraphNode_*` materialization yet.

- [ ] **Step 3: Implement runtime interfaces**

Add:

```cpp
struct FAssetDocumentAnimationGraphContext
{
	UObject* Asset = nullptr;
	UEdGraph* Graph = nullptr;
	FString GraphPath;
	FString GraphKind;
};

struct FAssetDocumentAnimationGraphNodeSpawnCandidate
{
	FString ClassPath;
	TSharedPtr<FJsonObject> Spawner;
	FString ActionKey;
	FString MenuName;
};

class IAssetDocumentAnimationGraphStructuralHook
{
public:
	virtual ~IAssetDocumentAnimationGraphStructuralHook() = default;
	virtual FAssetDocumentCapabilityResult LocateOrCreateGraph(
		const FAssetDocumentGraphSpec& GraphSpec,
		FAssetDocumentAnimationGraphContext& InOutContext) = 0;
	virtual FAssetDocumentCapabilityResult RepairAfterApply(
		const FAssetDocumentGraphSpec& GraphSpec,
		const FAssetDocumentAnimationGraphContext& Context) = 0;
};

class FAssetDocumentAnimationGraphRuntime
{
public:
	FAssetDocumentCapabilityResult ValidateGraph(
		const FAssetDocumentGraphSpec& GraphSpec,
		const FAssetDocumentAnimationGraphContext& Context) const;

	FAssetDocumentCapabilityResult ApplyGraph(
		const FAssetDocumentGraphSpec& GraphSpec,
		FAssetDocumentAnimationGraphContext& Context,
		IAssetDocumentAnimationGraphStructuralHook& Hook) const;

	FAssetDocumentCapabilityResult ExtractGraph(
		const FAssetDocumentAnimationGraphContext& Context,
		FAssetDocumentGraphSpec& OutGraph) const;
};
```

- [ ] **Step 4: Implement fake-backed contract behavior**

The first runtime shell may use fake candidate providers in tests, but production API must be shaped for UE graph action integration. Do not hardcode `UAnimGraphNode_*` classes in this task.

- [ ] **Step 5: Run focused tests and UBT**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimationGraphRuntime;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="E:/GameDev/PluginsWarehouse/Saved/AutomationReports/AnimationGraphRuntime"
```

- [ ] **Step 6: Commit**

```powershell
git add Source/AssetDocument/Private/Graphs/AssetDocumentAnimationGraphRuntime.* Source/AssetDocument/Private/Tests/AssetDocumentAnimationGraphRuntimeTests.cpp Source/AssetDocument/AssetDocument.Build.cs
git commit -m "feat(assetdoc): add animation graph runtime shell"
```

## Task 4: ABP AnimGraph Recursive Region

**Files:**
- Modify: `Source/AssetDocument/Private/Regions/AssetDocumentAnimGraphRegionAdapter.h`
- Modify: `Source/AssetDocument/Private/Regions/AssetDocumentAnimGraphRegionAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short
```

- [ ] **Step 2: Replace root-only tests with new schema tests**

Update `AssetFactory.AssetDocument.AnimBlueprint.AnimGraph` to use:

```json
{
  "Graphs": [
    {
      "Id": "AnimGraph",
      "Kind": "AnimGraph",
      "Owner": null,
      "Nodes": [],
      "Links": [],
      "Subgraphs": []
    }
  ]
}
```

Expected diagnostics:

- old array shape returns `/Body/AnimGraph` + `InvalidAnimGraphRegionType`.
- duplicate node id returns semantic `/Body/AnimGraph/Graphs/AnimGraph/Nodes/<Id>` path.
- unsupported node emits `_Skipped` or validation diagnostic, not silent success.

- [ ] **Step 3: Implement recursive adapter validation/extract/diff**

`FAssetDocumentAnimGraphRegionAdapter` must:

- accept only object region with `Graphs`.
- call `FAssetDocumentGraphParser::ParseGraphRegion`.
- require exactly one root `Kind="AnimGraph"` graph with `Id="AnimGraph"` for `Body.AnimGraph`.
- route diff through recursive graph diff.
- extract the existing ABP AnimGraph into recursive shape, initially with framework output evidence if real nodes are not yet materialized by later tasks.

- [ ] **Step 4: Apply via runtime shell**

Use `FAssetDocumentAnimationGraphRuntime` with an ABP AnimGraph structural hook. If real UE node materialization is not yet implemented for node families, non-empty authored nodes must fail with precise unspawnable/unsupported diagnostics from runtime, not root-only pilot diagnostics.

- [ ] **Step 5: Run focused ABP AnimGraph tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimBlueprint.AnimGraph;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="E:/GameDev/PluginsWarehouse/Saved/AutomationReports/ABPAnimGraph"
```

- [ ] **Step 6: Commit**

```powershell
git add Source/AssetDocument/Private/Regions/AssetDocumentAnimGraphRegionAdapter.* Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp
git commit -m "feat(assetdoc): route anim blueprint anim graph through recursive graph schema"
```

## Task 5: NodeSpawner Real Materialization

**Files:**
- Modify: `Source/AssetDocument/Private/Graphs/AssetDocumentAnimationGraphRuntime.h`
- Modify: `Source/AssetDocument/Private/Graphs/AssetDocumentAnimationGraphRuntime.cpp`
- Modify: `Source/AssetDocument/AssetDocument.Build.cs`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimationGraphRuntimeTests.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short
```

- [ ] **Step 2: Add real node family tests**

Add tests proving at least:

- sequence/blend player node with `AssetRef<UAnimationAsset>`.
- cached pose save/use pair.
- slot or sync group field.
- K2 variable get in transition rule context.

Tests must inspect extracted recursive JSON after apply and confirm:

- `Node.Id` is preserved.
- `Fields` contain semantic trait JSON.
- `Position` roundtrips.
- links use pin names.

- [ ] **Step 3: Add required UE module dependencies**

If compile requires modules, add only concrete dependencies verified by includes:

```csharp
PrivateDependencyModuleNames.AddRange(new string[]
{
	"AnimGraph",
	"AnimGraphRuntime",
	"KismetCompiler"
});
```

Do not add broad modules without compile evidence.

- [ ] **Step 4: Implement NodeSpawner integration**

Use UE graph schema/action APIs to query actions. Prefer dynamic class loading:

```cpp
UClass* DesiredClass = StaticLoadClass(UEdGraphNode::StaticClass(), nullptr, *NodeSpec.Class);
```

Do not include every `UAnimGraphNode_*` header. Use reflection and base graph node APIs.

- [ ] **Step 5: Implement reflected field apply/extract**

Use `FAssetDocumentGraphFieldRules` and `PropertySetterUtils` for:

- wrapper `UAnimGraphNode_*` UObject fields.
- inner `FAnimNode_*` struct fields through property path such as `Node.Sequence`.
- trait conversion for asset/class/name fields.

- [ ] **Step 6: Run runtime and ABP focused tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimationGraphRuntime; Automation RunTests AssetFactory.AssetDocument.AnimBlueprint.AnimGraph;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="E:/GameDev/PluginsWarehouse/Saved/AutomationReports/ABPNodeRuntime"
```

- [ ] **Step 7: Commit**

```powershell
git add Source/AssetDocument/Private/Graphs/AssetDocumentAnimationGraphRuntime.* Source/AssetDocument/Private/Tests/AssetDocumentAnimationGraphRuntimeTests.cpp Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp Source/AssetDocument/AssetDocument.Build.cs
git commit -m "feat(assetdoc): materialize animation graph nodes dynamically"
```

## Task 6: State Machines And Transition Subgraphs

**Files:**
- Modify: `Source/AssetDocument/Private/Regions/AssetDocumentAnimStateMachineRegionAdapter.h`
- Modify: `Source/AssetDocument/Private/Regions/AssetDocumentAnimStateMachineRegionAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short
```

- [ ] **Step 2: Add failing recursive state-machine tests**

Use `Body.StateMachines` object shape:

```json
{
  "Graphs": [
    {
      "Id": "Locomotion",
      "Kind": "StateMachine",
      "Owner": null,
      "Nodes": [
        {"Id": "Idle", "Class": "/Script/AnimGraph.AnimStateNode", "Kind": "State", "Position": {"X": 0, "Y": 0}},
        {"Id": "Run", "Class": "/Script/AnimGraph.AnimStateNode", "Kind": "State", "Position": {"X": 300, "Y": 0}}
      ],
      "Links": [],
      "Subgraphs": [
        {"Id": "IdlePose", "Kind": "StatePose", "Owner": {"State": "Idle"}, "Nodes": [], "Links": [], "Subgraphs": []},
        {"Id": "IdleToRunRule", "Kind": "TransitionRule", "Owner": {"Transition": "IdleToRun"}, "Nodes": [], "Links": [], "Subgraphs": []}
      ]
    }
  ]
}
```

Old authored `Body.TransitionGraphs` side-list should fail or be ignored only through explicit diagnostic; transition rule target is subgraph.

- [ ] **Step 3: Implement state-machine structural hook**

The hook owns:

- `UAnimationStateMachineGraph` create/lookup.
- state node lifecycle.
- transition node lifecycle.
- entry node and schema links.
- child pose/rule graph outer ownership.
- delete/rename repair.

All normal node fields still route through runtime.

- [ ] **Step 4: Extract and diff semantic paths**

Extract must output state machine graph with stable state/transition identities and positions. Diff paths must use:

```text
/Body/StateMachines/Graphs/Locomotion/Subgraphs/IdleToRunRule/Nodes/SpeedCheck
```

- [ ] **Step 5: Run focused tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimBlueprint.StateMachines;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="E:/GameDev/PluginsWarehouse/Saved/AutomationReports/ABPStateMachines"
```

- [ ] **Step 6: Commit**

```powershell
git add Source/AssetDocument/Private/Regions/AssetDocumentAnimStateMachineRegionAdapter.* Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp
git commit -m "feat(assetdoc): manage animation blueprint state machine subgraphs"
```

## Task 7: Anim Layers And Common Blueprint Graph Regions

**Files:**
- Modify: `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentProfile.cpp`
- Modify: `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentCapability.cpp`
- Modify: `Source/AssetDocument/Private/Regions/AssetDocumentAnimStateMachineRegionAdapter.*` or create a focused layer adapter if state-machine adapter is too broad.
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short
```

- [ ] **Step 2: Add failing tests**

Tests:

- `ProfileShape` includes `FunctionGraphs` and `MacroGraphs`.
- ABP `FunctionGraphs` and `MacroGraphs` use common Blueprint graph wrapper and roundtrip at least one K2 node.
- `AnimLayers` accepts recursive graph-family object, validates `LayerName`, and rejects signature mismatch.
- exact profile boundary test proves Anim Layer Interface-owned graph is not silently authored inside ordinary ABP.

- [ ] **Step 3: Add ABP region bindings**

In `MakeRegionBindings()` add:

```cpp
{TEXT("FunctionGraphs"), TEXT("Body.FunctionGraphs"), BlueprintCommonRegionAdapterName(), 110, false},
{TEXT("MacroGraphs"), TEXT("Body.MacroGraphs"), BlueprintCommonRegionAdapterName(), 120, false},
```

Add matching policies with schema `UBlueprintGraph`.

- [ ] **Step 4: Replace AnimLayers deferred gate**

Route `Body.AnimLayers` to real graph-family adapter or a thin layer structural hook. Remove `MarkDeferredRegionPolicy` only after tests prove non-empty value apply/extract/diff.

- [ ] **Step 5: Run focused tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimBlueprint.BlueprintCommonRegions; Automation RunTests AssetFactory.AssetDocument.AnimBlueprint.AnimLayersAndParentAssetOverrides;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="E:/GameDev/PluginsWarehouse/Saved/AutomationReports/ABPLayersCommonGraphs"
```

- [ ] **Step 6: Commit**

```powershell
git add Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentProfile.cpp Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentCapability.cpp Source/AssetDocument/Private/Regions Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp
git commit -m "feat(assetdoc): manage animation blueprint layers and common graphs"
```

## Task 8: Parent Override Alias Identity

**Files:**
- Modify: `Source/AssetDocument/Private/Regions/AssetDocumentAnimParentAssetOverrideRegionAdapter.h`
- Modify: `Source/AssetDocument/Private/Regions/AssetDocumentAnimParentAssetOverrideRegionAdapter.cpp`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short
```

- [ ] **Step 2: Add failing alias resolver tests**

Desired JSON:

```json
[
  {
    "Node": "IdlePlayer",
    "NewAsset": {"Kind": "AssetRef", "Path": "/Game/AssetDocumentTests/ABP_OverrideSeq"}
  }
]
```

Expected:

- apply resolves `Node` to parent graph node GUID.
- extract includes `Node` identity and `Evidence.ParentNodeGuid`.
- diff path is `/Body/ParentAssetOverrides/IdlePlayer/NewAsset`.
- raw `ParentNodeGuid` is still accepted only as UE fallback evidence when `Node` cannot be resolved.

- [ ] **Step 3: Implement resolver hook**

Use recursive AnimGraph node identity map produced by graph runtime. Do not search by array index. If alias is unresolved, fail with:

```text
path: /Body/ParentAssetOverrides/<Node>/Node
code: UnknownParentOverrideNode
```

- [ ] **Step 4: Run focused tests**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimBlueprint.AnimLayersAndParentAssetOverrides;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="E:/GameDev/PluginsWarehouse/Saved/AutomationReports/ABPParentOverrides"
```

- [ ] **Step 5: Commit**

```powershell
git add Source/AssetDocument/Private/Regions/AssetDocumentAnimParentAssetOverrideRegionAdapter.* Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp
git commit -m "feat(assetdoc): resolve parent overrides by graph node identity"
```

## Task 9: Deferred Document And Old Gate Cleanup

**Files:**
- Modify: `docs/superpowers/specs/asset-document-deferred-fields/2026-07-01-animationblueprint.md`
- Modify: `Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp`
- Modify as needed: `Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentProfile.cpp`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short
```

- [ ] **Step 2: Update tests removing old gates**

Replace `DeferredGraphGates` expectations:

- `Body.AnimGraph` old array shape rejected with new schema diagnostic.
- `Body.StateMachines` non-empty recursive schema accepted.
- `Body.AnimLayers` non-empty recursive schema accepted or exact interface boundary diagnostic.
- old `Body.TransitionGraphs` authored side-list rejected as obsolete target schema.

- [ ] **Step 3: Update deferred doc**

Keep only true excluded/derived/cache entries and any user-approved blockers. Remove entries that are now implemented by this plan. If a managed authored region remains unsupported, final state is blocked, not complete.

- [ ] **Step 4: Run ABP suite**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimBlueprint;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="E:/GameDev/PluginsWarehouse/Saved/AutomationReports/ABPComplete"
```

- [ ] **Step 5: Commit**

```powershell
git add docs/superpowers/specs/asset-document-deferred-fields/2026-07-01-animationblueprint.md Source/AssetDocument/Private/Tests/AssetDocumentAnimBlueprintTests.cpp Source/AssetDocument/Private/Profiles/AnimBlueprintAssetDocumentProfile.cpp
git commit -m "docs(assetdoc): close animation blueprint graph deferred gates"
```

## Task 10: External Smoke And Final Report

**Files:**
- Create or modify: `docs/superpowers/verification/run_asset_document_animationblueprint_complete_graph_smoke.ps1`
- Create: `docs/reports/asset-document-animationblueprint-complete-graph-semantics-report.md`

- [ ] **Step 1: Record task base**

```powershell
$env:TASK_BASE = (git rev-parse HEAD)
git status --short
```

- [ ] **Step 2: Add smoke script**

The smoke must:

- create or update `/Game/AssetDocumentSmoke/ABP_CompleteGraphSmoke`.
- write sidecar to `C:/AVH1/Content/AssetDocumentSmoke/ABP_CompleteGraphSmoke.assetdoc.json` or current temp host equivalent.
- include `Body.AnimGraph`, `Body.StateMachines`, transition rule subgraph, cached pose, linked layer or explicit profile boundary, parent override alias, and layout positions.
- call `/assetdocument/apply-file`, `/assetdocument/extract`, and `/assetdocument/diff`.
- assert no unexpected `changed` diff entries for implemented regions.

- [ ] **Step 3: Run full verification**

Use temp host validation if project plugin discovery would hide the worktree plugin:

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PluginsWarehouseEditor Win64 Development "-Project=E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -NoHotReload
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument.AnimBlueprint; Automation RunTests AssetFactory.AssetDocument.GraphCore; Automation RunTests AssetFactory.AssetDocument.AnimationGraphRuntime;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="E:/GameDev/PluginsWarehouse/Saved/AutomationReports/ABPCompleteGraph"
& "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:/GameDev/PluginsWarehouse/PluginsWarehouse.uproject" -Unattended -NullRHI -ExecCmds="Automation RunTests AssetFactory.AssetDocument;Quit" -TestExit="Automation Test Queue Empty" -ReportOutputPath="E:/GameDev/PluginsWarehouse/Saved/AutomationReports/AssetDocumentFull"
Push-Location MCP; npm test; Pop-Location
```

Then run the external HTTP smoke script against a real editor.

- [ ] **Step 4: Write final report**

Report must include:

- branch/worktree/base.
- `SPEC_BASE..HEAD`.
- completed graph regions.
- excluded generated/debug/cache fields.
- any user-approved blocker; otherwise no managed authored deferred graph surface.
- UBT result.
- focused automation result.
- full AssetDocument automation result.
- MCP test result.
- external smoke result.
- real asset and sidecar paths.
- review findings and fixes.

- [ ] **Step 5: Commit**

```powershell
git add docs/superpowers/verification/run_asset_document_animationblueprint_complete_graph_smoke.ps1 docs/reports/asset-document-animationblueprint-complete-graph-semantics-report.md
git commit -m "docs(assetdoc): report animation blueprint complete graph verification"
```

## Review And Dispatch Rules

For each task:

1. Implementer must use TDD where feasible.
2. Implementer records `TASK_BASE=HEAD`.
3. Implementer commits exactly the task result.
4. Spec reviewer reviews only `TASK_BASE..HEAD` against the task acceptance criteria and source spec.
5. Code quality reviewer reviews only `TASK_BASE..HEAD`.
6. Main coordinator does not start the next overlapping implementation task until reviews are clear.

Parallel subagents are allowed only for read-only exploration or for implementation tasks with disjoint files. The first safe split is:

- Task 1 implementation: graph core files only.
- Task 2 implementation: field rules files only.
- Task 3 exploration only: NodeSpawner API/prototype notes without writing production files, until Task 1/2 land.

Tasks 4 through 9 are sequential because they depend on graph core and runtime contracts and touch the same ABP test/capability files.

## Plan Self-Review

- Spec coverage: tasks cover recursive schema, node rules, field rules, NodeSpawner runtime, AnimGraph, StateMachines, transition subgraphs, AnimLayers, FunctionGraphs/MacroGraphs, ParentAssetOverrides, deferred cleanup, verification/report.
- Placeholder scan: no task uses red-flag placeholder terms as acceptance criteria.
- Type consistency: planned core types are `FAssetDocumentGraphSpec`, `FAssetDocumentNodeSpec`, `FAssetDocumentAnimationGraphRuntime`, `FAssetDocumentGraphFieldRules`, and current public region runtime types.
- Risk: full implementation depends on UE editor graph APIs and may require additional module dependencies discovered by UBT; any dependency addition must be justified by include/compile failure evidence.
