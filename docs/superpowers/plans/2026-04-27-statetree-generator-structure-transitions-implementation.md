# StateTree Generator Structure And Transitions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add StateTree state field semantics, transitions, linked state/subtree, linked asset, and extraction for those editor structures.

**Architecture:** Keep `FStateTreeGenerator` as lifecycle orchestration. Extend the existing StateTree helper layer with a structure model, a two-phase state index/resolver, transition construction, linked-state finalization, and extraction updates. Continue building editor data only, then compile through the official StateTree compiler.

**Tech Stack:** Unreal Engine 5.7, AssetFactory C++, StateTree editor/runtime modules, MCP JSON schema docs, `FPropertySetterUtils`, real Editor/MCP smoke tests.

---

## Current Branch Context

- Worktree: `/Volumes/Mac/GameDev/ProjectRPG/.worktrees/AssetFactory-statetree-structure-transitions`
- Branch: `feature/statetree-structure-transitions`
- Base branch: `feature/statetree-dynamic-nodes`
- Base commit: `0e1ec4f Add dynamic StateTree node generation`

Do not implement directly in `/Volumes/Mac/GameDev/ProjectRPG/Plugins/AssetFactory`. That path is used only for temporary smoke-test mounting.

## File Map

Create:

- `Source/AssetFactory/Private/Generators/StateTree/StateTreeStructureTypes.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeTransitionBuilder.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeTransitionBuilder.cpp`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeLinkResolver.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeLinkResolver.cpp`
- `TestData/ST_Structure_Transitions.json`
- `TestData/ST_Structure_LinkedSubtree.json`
- `TestData/ST_Structure_LinkedAsset_Target.json`
- `TestData/ST_Structure_LinkedAsset_Referencer.json`
- `TestData/ST_Structure_Invalid_MissingTransitionTarget.json`
- `TestData/ST_Structure_Invalid_AmbiguousTransitionTarget.json`
- `TestData/ST_Structure_Invalid_EventMissingTag.json`
- `TestData/ST_Structure_Invalid_LinkedSubtreeTargetNotSubtree.json`

Modify:

- `Source/AssetFactory/Private/Generators/StateTree/StateTreeJsonTypes.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.cpp`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.cpp`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.h`
- `Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp`
- `MCP/schemas/StateTree.md`

## Task 1: Add Structure Types And Parse Helpers

**Files:**

- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeStructureTypes.h`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeJsonTypes.h`

- [ ] **Step 1: Add state and transition specs**

Create `StateTreeStructureTypes.h` with these types:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Generators/StateTree/StateTreeJsonTypes.h"
#include "GameplayTagContainer.h"
#include "StateTreeTypes.h"

struct FAFStateTreeTransitionDelaySpec
{
	bool bEnabled = false;
	float Duration = 0.0f;
	float RandomVariance = 0.0f;
};

struct FAFStateTreeTransitionSpec
{
	FString Id;
	EStateTreeTransitionTrigger Trigger = EStateTreeTransitionTrigger::OnStateCompleted;
	EStateTreeTransitionType Type = EStateTreeTransitionType::Succeeded;
	FString Target;
	EStateTreeTransitionPriority Priority = EStateTreeTransitionPriority::Normal;
	bool bEnabled = true;
	FAFStateTreeTransitionDelaySpec Delay;
	FGameplayTag RequiredEventTag;
	TArray<FAFStateTreeNodeSpec> Conditions;
};

struct FAFStateTreeCustomTickRateSpec
{
	bool bTouched = false;
	bool bEnabled = false;
	float Value = 0.0f;
};

struct FAFStateTreeStateSpec
{
	FString Id;
	FString Name;
	FString CanonicalPath;
	EStateTreeStateType Type = EStateTreeStateType::State;
	TOptional<EStateTreeStateSelectionBehavior> SelectionBehavior;
	TOptional<EStateTreeTaskCompletionType> TasksCompletion;
	FGameplayTag Tag;
	FString Description;
	TOptional<bool> bEnabled;
	FAFStateTreeCustomTickRateSpec CustomTickRate;
	FString LinkedState;
	FString LinkedSubtree;
	FString LinkedAsset;
	TArray<FAFStateTreeNodeSpec> Tasks;
	TArray<FAFStateTreeNodeSpec> EnterConditions;
	TArray<FAFStateTreeNodeSpec> Considerations;
	TArray<FAFStateTreeTransitionSpec> Transitions;
	TArray<FAFStateTreeStateSpec> Children;
};
```

- [ ] **Step 2: Add parse declarations**

Append declarations under `namespace UE::AssetFactory::StateTree`:

```cpp
bool TryParseStateType(const FString& Value, EStateTreeStateType& OutType);
bool TryParseSelectionBehavior(const FString& Value, EStateTreeStateSelectionBehavior& OutValue);
bool TryParseTaskCompletionType(const FString& Value, EStateTreeTaskCompletionType& OutValue);
bool TryParseTransitionTrigger(const FString& Value, EStateTreeTransitionTrigger& OutValue);
bool TryParseTransitionType(const FString& Value, EStateTreeTransitionType& OutValue);
bool TryParseTransitionPriority(const FString& Value, EStateTreeTransitionPriority& OutValue);
bool ParseStateSpec(TSharedPtr<FJsonObject> StateObject, const FString& ParentPath, FAFStateTreeStateSpec& OutSpec, FString& OutError);
bool ParseTransitionSpec(TSharedPtr<FJsonObject> TransitionObject, FAFStateTreeTransitionSpec& OutSpec, FString& OutError);
```

- [ ] **Step 3: Implement enum parsing**

Implement parsing with exact names and case-insensitive aliases:

```cpp
if (Value.Equals(TEXT("State"), ESearchCase::IgnoreCase)) { OutType = EStateTreeStateType::State; return true; }
if (Value.Equals(TEXT("Group"), ESearchCase::IgnoreCase)) { OutType = EStateTreeStateType::Group; return true; }
if (Value.Equals(TEXT("Linked"), ESearchCase::IgnoreCase)) { OutType = EStateTreeStateType::Linked; return true; }
if (Value.Equals(TEXT("LinkedAsset"), ESearchCase::IgnoreCase)) { OutType = EStateTreeStateType::LinkedAsset; return true; }
if (Value.Equals(TEXT("Subtree"), ESearchCase::IgnoreCase)) { OutType = EStateTreeStateType::Subtree; return true; }
return false;
```

Use the same pattern for transition trigger/type/priority and selection/task completion enums.

- [ ] **Step 4: Parse state fields**

`ParseStateSpec()` must:

- Require non-empty `name`.
- Default `type` to `State`.
- Set `CanonicalPath` to `ParentPath.IsEmpty() ? Name : ParentPath + "/" + Name`.
- Parse `tasks`, `enterConditions`, and `considerations` through `ParseNodeSpec()`.
- Parse `transitions` through `ParseTransitionSpec()`.
- Parse `children` recursively.
- Parse `customTickRate` as number or object.

- [ ] **Step 5: Parse transition fields**

`ParseTransitionSpec()` must:

- Default `trigger` to `OnStateCompleted`.
- Default `type` to `GotoState` when `target` is present, otherwise `Succeeded`.
- Parse `delay` as number or object.
- Parse `requiredEvent` as string or object with `tag`.
- Parse `conditions` as `EAFStateTreeNodeKind::TransitionCondition`.
- Reject `trigger: "OnDelegate"` with error `StateTree transition trigger 'OnDelegate' is not supported by this spec`.

## Task 2: Add State Index And Link Resolver

**Files:**

- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeLinkResolver.h`
- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeLinkResolver.cpp`

- [ ] **Step 1: Define state index**

Create:

```cpp
struct FAFStateTreeStateIndex
{
	TMap<FString, UStateTreeState*> ById;
	TMap<FString, UStateTreeState*> ByPath;
	TMap<FString, TArray<UStateTreeState*>> ByName;
	TMap<const UStateTreeState*, FString> PathByState;
};
```

- [ ] **Step 2: Add register helper**

Implement:

```cpp
bool RegisterStateReference(
	FAFStateTreeStateIndex& Index,
	const FString& Id,
	const FString& CanonicalPath,
	UStateTreeState& State,
	FString& OutError);
```

Rules:

- Non-empty duplicate `id` fails.
- Duplicate canonical path fails.
- Leaf names are collected for ambiguity detection.

- [ ] **Step 3: Add resolve helper**

Implement:

```cpp
bool ResolveStateReference(
	const FAFStateTreeStateIndex& Index,
	const FString& Reference,
	UStateTreeState*& OutState,
	FString& OutError);
```

Resolution order:

1. `ById`
2. `ByPath`
3. unique `ByName`

If name is ambiguous, error must include all matching paths.

- [ ] **Step 4: Add link builder helper**

Implement:

```cpp
FStateTreeStateLink MakeStateLink(EStateTreeTransitionType Type, const UStateTreeState* State);
```

For `GotoState`, call `State->GetLinkToState()` when `State` exists. For non-target transition types, construct `FStateTreeStateLink(Type)`.

## Task 3: Build State Fields And Index States

**Files:**

- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.h`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.cpp`

- [ ] **Step 1: Extend state builder signature**

Change the public API from taking raw JSON only to parsing first:

```cpp
bool ApplyStateTreeConfig(
	UStateTreeEditorData& EditorData,
	TSharedPtr<FJsonObject> Config,
	FString& OutError);
```

Internally, parse `SubTrees` into `TArray<FAFStateTreeStateSpec>`, then build states.

- [ ] **Step 2: Build state fields**

When creating each `UStateTreeState`, set:

```cpp
State.Name = FName(*Spec.Name);
State.Type = Spec.Type;
State.Description = Spec.Description;
State.Tag = Spec.Tag;
if (Spec.SelectionBehavior.IsSet()) { State.SelectionBehavior = Spec.SelectionBehavior.GetValue(); }
if (Spec.TasksCompletion.IsSet()) { State.TasksCompletion = Spec.TasksCompletion.GetValue(); }
if (Spec.bEnabled.IsSet()) { State.bEnabled = Spec.bEnabled.GetValue(); }
if (Spec.CustomTickRate.bTouched)
{
	State.bHasCustomTickRate = Spec.CustomTickRate.bEnabled;
	State.CustomTickRate = Spec.CustomTickRate.bEnabled ? Spec.CustomTickRate.Value : 0.0f;
}
State.ID = MakeDeterministicStateGuid(Spec.Id, Spec.CanonicalPath);
```

- [ ] **Step 3: Register states during construction**

After each state object exists and fields are assigned, call `RegisterStateReference()` with `Spec.Id`, `Spec.CanonicalPath`, and `State`.

- [ ] **Step 4: Preserve Spec 2 node building**

Continue using existing node array handling for:

- `Tasks`
- `EnterConditions`
- `Considerations`
- recursive `Children`

Do not change dynamic node validation except to allow transition conditions in Task 4.

## Task 4: Build Transitions

**Files:**

- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeTransitionBuilder.h`
- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeTransitionBuilder.cpp`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.cpp`

- [ ] **Step 1: Add transition builder API**

Declare:

```cpp
bool BuildTransitionsForState(
	UObject* Outer,
	const UStateTreeSchema& Schema,
	const FAFStateTreeStateIndex& Index,
	const FAFStateTreeStateSpec& Spec,
	UStateTreeState& State,
	FString& OutError);
```

- [ ] **Step 2: Validate transition target rules**

For each transition:

- `GotoState` requires `Target`.
- `NextState`, `NextSelectableState`, `Succeeded`, `Failed`, and `None` do not require `Target`.
- `OnEvent` requires `RequiredEventTag.IsValid()`.

- [ ] **Step 3: Construct transition**

Append transition:

```cpp
FStateTreeTransition& Transition = State.Transitions.AddDefaulted_GetRef();
Transition.ID = MakeDeterministicTransitionGuid(Spec.Id, State.ID, Index);
Transition.Trigger = Spec.Trigger;
Transition.State = MakeStateLink(Spec.Type, TargetState);
Transition.Priority = Spec.Priority;
Transition.bTransitionEnabled = Spec.bEnabled;
Transition.bDelayTransition = Spec.Delay.bEnabled;
Transition.DelayDuration = Spec.Delay.Duration;
Transition.DelayRandomVariance = Spec.Delay.RandomVariance;
Transition.RequiredEvent.Tag = Spec.RequiredEventTag;
```

- [ ] **Step 4: Build transition conditions**

For each `ConditionSpec`:

```cpp
if (!ValidateNodeSpec(Schema, ConditionSpec, OutError)) { return false; }
FStateTreeEditorNode& ConditionNode = Transition.Conditions.AddDefaulted_GetRef();
if (!BuildEditorNode(Outer, Schema, ConditionSpec, ConditionNode, OutError)) { return false; }
```

The existing `BuildEditorNode()` supports `EAFStateTreeNodeKind::TransitionCondition`.

- [ ] **Step 5: Call builder after all states exist**

After recursive construction and indexing completes, traverse all parsed specs with their matching `UStateTreeState*` and call `BuildTransitionsForState()`.

## Task 5: Linked State, Subtree, And Asset Finalization

**Files:**

- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.cpp`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeLinkResolver.cpp`

- [ ] **Step 1: Finalize `type: "Linked"`**

For linked states:

```cpp
if (Spec.Type == EStateTreeStateType::Linked)
{
	UStateTreeState* Target = nullptr;
	const FString Ref = !Spec.LinkedSubtree.IsEmpty() ? Spec.LinkedSubtree : Spec.LinkedState;
	if (!ResolveStateReference(Index, Ref, Target, OutError)) { return false; }
	if (Target == &State) { OutError = FString::Printf(TEXT("State '%s' cannot link to itself"), *Spec.CanonicalPath); return false; }
	if (!Spec.LinkedSubtree.IsEmpty() && Target->Type != EStateTreeStateType::Subtree)
	{
		OutError = FString::Printf(TEXT("State '%s' linkedSubtree target '%s' is not a Subtree"), *Spec.CanonicalPath, *Ref);
		return false;
	}
	State.SetLinkedState(Target->GetLinkToState());
}
```

- [ ] **Step 2: Finalize `type: "LinkedAsset"`**

For linked asset states:

```cpp
UStateTree* LinkedAsset = LoadObject<UStateTree>(nullptr, *Spec.LinkedAsset);
if (!LinkedAsset)
{
	OutError = FString::Printf(TEXT("State '%s' linkedAsset '%s' is not a StateTree asset"), *Spec.CanonicalPath, *Spec.LinkedAsset);
	return false;
}
State.SetLinkedStateAsset(LinkedAsset);
```

- [ ] **Step 3: Reject misplaced link fields**

Return clear errors for:

- `linkedAsset` on non-`LinkedAsset` state.
- `linkedState`/`linkedSubtree` on non-`Linked` state.
- `Linked` without `linkedState` or `linkedSubtree`.
- `LinkedAsset` without `linkedAsset`.

## Task 6: Update Extraction

**Files:**

- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.h`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.cpp`

- [ ] **Step 1: Extract state fields**

For each state emit:

```json
{
	"id": "Root/Patrol",
	"name": "Patrol",
	"type": "State",
	"selectionBehavior": "TrySelectChildrenInOrder",
	"tasksCompletion": "Any",
	"description": "",
	"enabled": true
}
```

Use string helpers for enums.

- [ ] **Step 2: Extract linked fields**

If `State.Type == Linked`, emit either `linkedSubtree` when target type is `Subtree`, otherwise `linkedState`, using canonical path if known.

If `State.Type == LinkedAsset` and `State.LinkedAsset`, emit `linkedAsset` as `State.LinkedAsset->GetPathName()`.

- [ ] **Step 3: Extract transitions**

Emit transition fields:

```json
{
	"id": "guid-or-derived-id",
	"trigger": "OnStateCompleted",
	"type": "GotoState",
	"target": "Root/Patrol",
	"priority": "Normal",
	"enabled": true,
	"delay": { "duration": 0.2, "randomVariance": 0.05 },
	"conditions": []
}
```

For `conditions`, reuse `ExtractEditorNodes(Transition.Conditions, TEXT("transitionCondition"), bDiffOnly)`.

## Task 7: Fixtures And MCP Schema

**Files:**

- Create fixture files listed in File Map.
- Modify: `MCP/schemas/StateTree.md`

- [ ] **Step 1: Add positive fixtures**

Add four positive fixtures:

- `ST_Structure_Transitions.json`: nested states, `GotoState`, `NextState`, transition delay.
- `ST_Structure_LinkedSubtree.json`: top-level `Subtree` plus `Linked` state.
- `ST_Structure_LinkedAsset_Target.json`: minimal linked target asset.
- `ST_Structure_LinkedAsset_Referencer.json`: `LinkedAsset` state referencing the target.

- [ ] **Step 2: Add negative fixtures**

Add four negative fixtures:

- Missing target.
- Ambiguous target leaf name.
- OnEvent missing required event.
- Linked subtree target not `Subtree`.

- [ ] **Step 3: Update schema docs**

Document state fields, transition fields, linked-state examples, and invalid-case guidance.

## Task 8: Verification

**Files:**

- No source changes unless verification exposes a bug.

- [ ] **Step 1: Static checks**

Run:

```bash
git diff --check
npm run build
```

Expected:

- `git diff --check` exits 0.
- MCP TypeScript build exits 0.

- [ ] **Step 2: Project compile**

With `Plugins/AssetFactory` mounted to this worktree, run:

```bash
~/UnrealEngine/Engine/Build/BatchFiles/Mac/Build.sh ProjectRPGEditor Mac Development -Project="/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject" -NoHotReload -NoMutex -architecture=arm64
```

Expected: `Result: Succeeded`.

- [ ] **Step 3: Real Editor/MCP smoke**

Launch GUI Editor without `-NullRHI`:

```bash
open -na /Users/pengao/UnrealEngine/Engine/Binaries/Mac/UnrealEditor.app --args "/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject" -nop4 -NoSound -log
```

Wait for:

```bash
curl -fsS http://127.0.0.1:8559/assetfactory/health
```

Then run MCP generation for:

- `ST_Structure_Transitions.json`
- `ST_Structure_LinkedSubtree.json`
- `ST_Structure_LinkedAsset_Target.json`
- `ST_Structure_LinkedAsset_Referencer.json`

Extract all generated assets and open the richest success case in the Editor.

- [ ] **Step 4: Negative smoke**

Run `generate_assets` for each invalid fixture. Expected:

- `success: false`
- `failed: 1`
- readable error
- no Editor crash
- no `.uasset` saved for invalid asset

- [ ] **Step 5: Log evidence**

Collect lines from:

```bash
"/Users/pengao/Library/Logs/Unreal Engine/ProjectRPGEditor/ProjectRPG.log"
```

Required lines:

- `AssetFactory HTTP Server started on port 8559`
- success case `Compile StateTree ... succeeded`
- success case `LogSavePackage ... .uasset`
- invalid case `[FAILED] StateTree`
- `LogAssetEditorSubsystem: Opening Asset editor for StateTree ...`

## Task 9: Commit

**Files:**

- All changed source, docs, schema, and fixture files.

- [ ] **Step 1: Final status check**

Run:

```bash
git status --short
git diff --stat
```

- [ ] **Step 2: Commit**

Run:

```bash
git add Source/AssetFactory/Private/Generators/StateTree MCP/schemas/StateTree.md TestData/ST_Structure_*.json docs/superpowers/specs/2026-04-27-statetree-generator-structure-transitions-design.md docs/superpowers/plans/2026-04-27-statetree-generator-structure-transitions-implementation.md
git commit -m "Add StateTree structure and transitions"
```

