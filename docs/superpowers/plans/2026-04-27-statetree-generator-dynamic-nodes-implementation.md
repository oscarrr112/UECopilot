# StateTree Generator Dynamic Nodes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the durable StateTree JSON node contract and the first dynamic node construction slice for C++ struct nodes, with clear extension points for Blueprint wrapper nodes and extraction.

**Architecture:** Keep `FStateTreeGenerator` focused on lifecycle orchestration. Add StateTree-specific helpers for JSON parsing, type resolution, validation, node initialization, state application, and node extraction. Build `UStateTreeEditorData`, then compile through `UStateTreeEditingSubsystem::CompileStateTree()`.

**Tech Stack:** Unreal Engine 5.7, StateTree editor/runtime modules, UBT, AssetFactory generator framework, MCP schema docs, JSON fixtures, `FPropertySetterUtils`.

---

## Current Branch Context

- Worktree: `C:\Users\HP\.config\superpowers\worktrees\UECopilot\statetree-dynamic-nodes`
- Branch: `feature/statetree-dynamic-nodes`
- Base: `feature/statetree-core-lifecycle`
- Do not implement directly on `master` or the main workspace.
- This plan assumes the core lifecycle StateTree generator from PR #2 is present.

## File Map

Create:

- `Source/AssetFactory/Private/Generators/StateTree/StateTreeJsonTypes.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeClassResolver.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeClassResolver.cpp`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeNodeBuilder.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeNodeBuilder.cpp`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeValidation.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeValidation.cpp`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.cpp`
- `TestData/ST_Dynamic_Delay_Minimal.json`
- `TestData/ST_Dynamic_DebugText_WithInstance.json`
- `TestData/ST_Dynamic_Condition_CompareInt.json`
- `TestData/ST_Dynamic_Invalid_Category_TaskSlotCondition.json`
- `TestData/ST_Dynamic_Invalid_UnknownNode.json`
- `TestData/ST_Dynamic_Invalid_SchemaAIMoveToInComponent.json`

Modify:

- `Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp`
- `Source/AssetFactory/Public/Generators/StateTreeGenerator.h`
- `Source/AssetFactory/AssetFactory.Build.cs` if new helper includes require additional modules
- `MCP/schemas/StateTree.md`

Generated files:

- Update `MCP/dist/index.js` only if MCP TypeScript source changes require it. Do not include `MCP/dist/broker/*` line-ending noise.

---

## Task 1: Add JSON Types And Contract Constants

**Files:**

- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeJsonTypes.h`

- [ ] **Step 1: Add enums and field constants**

Create the header with:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

enum class EAFStateTreeNodeKind : uint8
{
	Evaluator,
	GlobalTask,
	Task,
	EnterCondition,
	TransitionCondition,
	Consideration,
};

struct FAFStateTreeNodeSpec
{
	FString Id;
	EAFStateTreeNodeKind Kind = EAFStateTreeNodeKind::Task;
	FString Type;
	TSharedPtr<FJsonObject> NodeProperties;
	TSharedPtr<FJsonObject> InstanceProperties;
	TSharedPtr<FJsonObject> ExecutionRuntimeDataProperties;
	int32 ExpressionIndent = 0;
	FString ExpressionOperand = TEXT("And");
};
```

- [ ] **Step 2: Add parse helpers**

Add declarations:

```cpp
namespace UE::AssetFactory::StateTree
{
	bool TryParseNodeKind(const FString& Value, EAFStateTreeNodeKind& OutKind);
	FString NodeKindToString(EAFStateTreeNodeKind Kind);
	bool ParseNodeSpec(TSharedPtr<FJsonObject> NodeObject, EAFStateTreeNodeKind ExpectedKind, FAFStateTreeNodeSpec& OutSpec, FString& OutError);
}
```

- [ ] **Step 3: Implement inline parse logic in the header or a matching cpp**

Use exact expected `kind` validation:

```cpp
if (NodeObject->HasField(TEXT("kind")))
{
	FString KindString;
	if (!NodeObject->TryGetStringField(TEXT("kind"), KindString) || !TryParseNodeKind(KindString, OutSpec.Kind))
	{
		OutError = FString::Printf(TEXT("Unknown StateTree node kind: %s"), *KindString);
		return false;
	}
	if (OutSpec.Kind != ExpectedKind)
	{
		OutError = FString::Printf(TEXT("StateTree node kind '%s' cannot be used in '%s' slot"), *NodeKindToString(OutSpec.Kind), *NodeKindToString(ExpectedKind));
		return false;
	}
}
else
{
	OutSpec.Kind = ExpectedKind;
}
```

- [ ] **Step 4: Run a compile-oriented check**

Run:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" UnrealEditor Win64 Development "-Project=<temp-host>.uproject" -NoHotReload
```

Expected: this may not be runnable until later tasks wire files into a temp host. At minimum, no syntax should be obviously invalid.

---

## Task 2: Add Dynamic Struct/Class Resolver

**Files:**

- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeClassResolver.h`
- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeClassResolver.cpp`

- [ ] **Step 1: Implement `ResolveScriptStruct`**

The resolver must support:

- full path: `/Script/StateTreeModule.StateTreeDelayTask`
- plain struct name: `StateTreeDelayTask`
- name with `F` prefix: `FStateTreeDelayTask`

Implementation shape:

```cpp
UScriptStruct* ResolveScriptStruct(const FString& TypeName);
```

Use `FindObject<UScriptStruct>(nullptr, *TypeName)` and `TObjectIterator<UScriptStruct>` fallback. For plain names, compare both `Struct->GetName()` and name without leading `F`.

- [ ] **Step 2: Implement `ResolveNodeClass`**

Use existing `FClassFinderUtils::FindClassByName(TypeName, UObject::StaticClass())` plus load path handling.

```cpp
UClass* ResolveNodeClass(const FString& TypeName);
```

- [ ] **Step 3: Add slot base helpers**

Expose:

```cpp
const UScriptStruct* GetExpectedBaseStruct(EAFStateTreeNodeKind Kind);
UClass* GetExpectedBlueprintBaseClass(EAFStateTreeNodeKind Kind);
```

Expected mappings:

- task/globalTask -> `FStateTreeTaskBase` and `UStateTreeTaskBlueprintBase`
- evaluator -> `FStateTreeEvaluatorBase` and `UStateTreeEvaluatorBlueprintBase`
- enterCondition/transitionCondition -> `FStateTreeConditionBase` and `UStateTreeConditionBlueprintBase`
- consideration -> `FStateTreeConsiderationBase` and `UStateTreeConsiderationBlueprintBase`

- [ ] **Step 4: Validate no hard-coded concrete node list**

Search changed files for concrete built-in node names like `StateTreeDelayTask` outside fixtures/docs. They must not appear in production code.

---

## Task 3: Add Node Validation

**Files:**

- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeValidation.h`
- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeValidation.cpp`

- [ ] **Step 1: Add validation entry point**

```cpp
bool ValidateNodeSpec(
	const UStateTreeSchema& Schema,
	const FAFStateTreeNodeSpec& Spec,
	FString& OutError);
```

- [ ] **Step 2: Validate category and schema**

Rules:

- Resolve as struct first.
- If struct exists, require `Struct->IsChildOf(ExpectedBaseStruct)`.
- Require `Schema.IsStructAllowed(Struct)`.
- If no struct, resolve class.
- If class exists, require `Class->IsChildOf(ExpectedBlueprintBaseClass)`.
- Require `Schema.IsClassAllowed(Class)`.
- If neither exists, return `Unknown StateTree node type: <type>`.

- [ ] **Step 3: Validate schema-level feature switches**

Reject:

- evaluator input when `!Schema.AllowEvaluators()`
- enter condition input when `!Schema.AllowEnterConditions()`
- consideration input when `!Schema.AllowUtilityConsiderations()`

For tasks, defer `AllowMultipleTasks()` validation to the state builder because it depends on task count.

- [ ] **Step 4: Add readable errors**

Examples:

- `StateTree node '/Script/StateTreeModule.StateTreeCompareIntCondition' is a condition and cannot be used in a task slot`
- `StateTree schema '/Script/GameplayStateTreeModule.StateTreeComponentSchema' does not allow node '/Script/GameplayStateTreeModule.StateTreeMoveToTask'`

---

## Task 4: Add Dynamic Node Builder For C++ Struct Nodes

**Files:**

- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeNodeBuilder.h`
- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeNodeBuilder.cpp`

- [ ] **Step 1: Add builder entry point**

```cpp
bool BuildEditorNode(
	UObject* Outer,
	const UStateTreeSchema& Schema,
	const FAFStateTreeNodeSpec& Spec,
	FStateTreeEditorNode& OutNode,
	FString& OutError);
```

- [ ] **Step 2: Implement C++ struct node path**

For `UScriptStruct* Struct`:

```cpp
OutNode.Reset();
OutNode.ID = MakeNodeGuid(Spec);
OutNode.Node.InitializeAs(Struct);
if (Spec.NodeProperties.IsValid())
{
	FPropertySetterUtils::SetStructFromJson(Struct, OutNode.Node.GetMutableMemory(), MakeShared<FJsonValueObject>(Spec.NodeProperties));
}
```

Then inspect `OutNode.GetNode().Get().GetInstanceDataType()` or equivalent `FStateTreeNodeBase` view.

- [ ] **Step 3: Initialize instance data**

If returned type is `UScriptStruct`, initialize `OutNode.Instance.InitializeAs(InstanceStruct)` and apply `Spec.InstanceProperties`.

If returned type is `UClass`, create `OutNode.InstanceObject = NewObject<UObject>(Outer, InstanceClass)` and apply `FPropertySetterUtils::SetPropertiesFromJson()`.

If no type is returned, reject provided `instance.properties` because there is nowhere to apply them.

- [ ] **Step 4: Initialize execution runtime data**

Mirror the instance data logic for `ExecutionRuntimeData` and `ExecutionRuntimeDataObject`.

- [ ] **Step 5: Apply expression metadata**

For conditions and considerations:

```cpp
OutNode.ExpressionIndent = FMath::Clamp(Spec.ExpressionIndent, 0, 255);
OutNode.ExpressionOperand = ParseExpressionOperand(Spec.ExpressionOperand);
```

Default operand is `And`.

---

## Task 5: Add Blueprint Wrapper Node Path

**Files:**

- Modify: `StateTreeNodeBuilder.h`
- Modify: `StateTreeNodeBuilder.cpp`

- [ ] **Step 1: Add wrapper mapping**

Map `EAFStateTreeNodeKind` to wrapper structs:

- task/globalTask -> `FStateTreeBlueprintTaskWrapper`
- evaluator -> `FStateTreeBlueprintEvaluatorWrapper`
- enterCondition/transitionCondition -> `FStateTreeBlueprintConditionWrapper`
- consideration -> `FStateTreeBlueprintConsiderationWrapper`

- [ ] **Step 2: Resolve and validate Blueprint node class**

Require the class to derive from the expected Blueprint base class.

- [ ] **Step 3: Initialize wrapper and instance object**

Use the same pattern as `StateTreeEditorNodeUtils.cpp::SetNodeTypeClass()`:

```cpp
OutNode.Node.InitializeAs(WrapperStruct);
OutNode.InstanceObject = NewObject<UObject>(Outer, NodeClass, NAME_None, RF_Transactional);
```

- [ ] **Step 4: Apply instance properties**

Use `FPropertySetterUtils::SetPropertiesFromJson(OutNode.InstanceObject, Spec.InstanceProperties)`.

- [ ] **Step 5: Add a clear unsupported error if runtime object data appears before implemented**

If wrapper runtime data is not fully supported in the first implementation pass, return:

```text
Blueprint StateTree node execution runtime data is not supported in this dynamic nodes slice
```

Do not silently ignore runtime fields.

---

## Task 6: Add State Builder And Wire Into Generator

**Files:**

- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.h`
- Create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.cpp`
- Modify: `Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp`
- Modify: `Source/AssetFactory/Public/Generators/StateTreeGenerator.h`

- [ ] **Step 1: Add entry point**

```cpp
bool ApplyStateTreeConfig(
	UStateTreeEditorData& EditorData,
	TSharedPtr<FJsonObject> Config,
	FString& OutError);
```

- [ ] **Step 2: Parse `SubTrees`**

If absent, preserve core lifecycle behavior.

If present, clear existing generated root/subtree state data only for create path. For update path, keep the core lifecycle conservative behavior unless the design explicitly allows replacing generated structure.

- [ ] **Step 3: Build states recursively**

For each state:

- `id`
- `name`
- `type`
- `tasks`
- `enterConditions`
- `considerations`
- `children`

Do not implement transitions here.

- [ ] **Step 4: Build node arrays**

Append nodes into:

- `EditorData.Evaluators`
- `EditorData.GlobalTasks`
- `State.Tasks` or `State.SingleTask`
- `State.EnterConditions`
- `State.Considerations`

Reject multiple state tasks if schema does not allow them.

- [ ] **Step 5: Wire into `FStateTreeGenerator::Generate()`**

Insert after `ApplySchemaProperties()` and before `CompileStateTree()`:

```cpp
if (!ApplyStateTreeConfig(*EditorData, Config, Error))
{
	return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, Error);
}
```

---

## Task 7: Add Node Extraction Skeleton

**Files:**

- Modify: `StateTreeGenerator.cpp`
- Optionally create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.h`
- Optionally create: `Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.cpp`

- [ ] **Step 1: Extract editor nodes**

Emit:

- `id`
- `kind`
- `type`
- `node.properties`
- `instance.type`
- `instance.properties`
- `executionRuntimeData.type`
- `executionRuntimeData.properties`
- `expression`

- [ ] **Step 2: Extract state hierarchy**

Include state `children`, `tasks`, `enterConditions`, and `considerations`.

- [ ] **Step 3: Keep runtime compact data out**

Do not emit runtime handles, compact indexes, frames, or compiler internals.

---

## Task 8: Add Fixtures And Schema Docs

**Files:**

- Create fixture files listed in File Map
- Modify: `MCP/schemas/StateTree.md`

- [ ] **Step 1: Add positive fixture: delay task**

Use `StateTreeComponentSchema` and one child state with `StateTreeDelayTask`.

- [ ] **Step 2: Add positive fixture: debug text task**

Use `StateTreeDebugTextTask` and separate `node.properties` / `instance.properties`.

- [ ] **Step 3: Add positive fixture: compare int condition**

Use `StateTreeCompareIntCondition` in `enterConditions`.

- [ ] **Step 4: Add negative fixtures**

Add:

- condition in task slot
- unknown node type
- AI MoveTo task under component schema

- [ ] **Step 5: Update MCP schema doc**

Document:

- top-level `Evaluators`, `GlobalTasks`, `SubTrees`
- node contract
- C++ struct and Blueprint class behavior
- limitations for transitions, parameters, and bindings

---

## Task 9: Verification

**Files:**

- No code changes unless verification exposes bugs.

- [ ] **Step 1: TypeScript build**

Run:

```powershell
npm run build
```

from `MCP`.

Expected: exit 0.

- [ ] **Step 2: Diff hygiene**

Run:

```powershell
git diff --check
```

Expected: exit 0. If `MCP/dist/broker/*` appears with only line-ending noise, restore it before commit.

- [ ] **Step 3: UBT compile**

Use an isolated temp host project and compile with:

```powershell
"E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" UnrealEditor Win64 Development "-Project=<temp-host>.uproject" -NoHotReload
```

Expected: `Result: Succeeded`.

- [ ] **Step 4: Editor/MCP smoke**

Launch editor for an isolated temp host project, wait for `health_check`, then verify:

- `generate_assets` succeeds for delay task fixture.
- `generate_assets` succeeds for debug text fixture.
- `generate_assets` succeeds for compare int condition fixture.
- `extract_assets` returns node skeletons and instance properties.
- invalid fixtures fail with readable errors.

- [ ] **Step 5: Close editor**

Close `UnrealEditor.exe` after verification so later UBT builds are not blocked.

---

## Implementation Notes

- Do not add concrete built-in node includes for `StateTreeDelayTask`, `StateTreeDebugTextTask`, or `StateTreeCompareIntCondition` in production code.
- Use dynamic `UScriptStruct` / `UClass` resolution.
- Prefer `FPropertySetterUtils` for property application.
- Do not rely on `UStateTreeEditorSchema::Validate()` silently deleting invalid nodes.
- Keep every subagent's write ownership narrow. Recommended split:
  - Agent A: resolver + validation
  - Agent B: node builder
  - Agent C: state builder + generator wiring
  - Agent D: extraction + fixtures + MCP docs
- Review order after each implementation batch:
  - spec compliance review
  - code quality review
  - fix review if needed
