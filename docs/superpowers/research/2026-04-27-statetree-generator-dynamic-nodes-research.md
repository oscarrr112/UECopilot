# StateTree Generator Dynamic Nodes Research

- Date: 2026-04-27
- Branch: `feature/statetree-dynamic-nodes`
- Base branch for this stacked worktree: `feature/statetree-core-lifecycle`
- Scope: Spec 2 research for JSON contract and dynamic node construction

## Goal

This research supports the next StateTree generator slice after core lifecycle. The final product goal is a feature-complete StateTree generator, so Spec 2 must establish a durable node contract for later structure, parameters, bindings, extraction, and round-trip work. The first implementation can be staged, but the contract should not be a narrow `DelayTask`-only shortcut.

## Current Project Baseline

The previous branch added a core lifecycle generator:

- `Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp`
- `Source/AssetFactory/Public/Generators/StateTreeGenerator.h`
- `MCP/schemas/StateTree.md`
- StateTree module and plugin dependencies

That implementation creates and compiles a minimal `UStateTree` through official editor data and compiler paths. It intentionally rejects `SubTrees` and `Bindings` input. Spec 2 should relax the `SubTrees` rejection only for state and node input, leaving transitions, parameters, and bindings to later specs.

## Engine API Findings

### Editor Node Shape

`FStateTreeEditorNode` is the common editor-side container for tasks, evaluators, conditions, and considerations.

Reference:

- `E:\Epic Games\UE_5.7\Engine\Plugins\Runtime\StateTree\Source\StateTreeEditorModule\Public\StateTreeEditorNode.h`

Important fields:

- `Node`: `FInstancedStruct`, holds the node template struct or Blueprint wrapper struct.
- `Instance`: `FInstancedStruct`, holds struct instance data when `GetInstanceDataType()` returns `UScriptStruct`.
- `InstanceObject`: UObject instance data when `GetInstanceDataType()` returns `UClass`.
- `ExecutionRuntimeData`: `FInstancedStruct`, holds runtime data when `GetExecutionRuntimeDataType()` returns `UScriptStruct`.
- `ExecutionRuntimeDataObject`: UObject runtime data when `GetExecutionRuntimeDataType()` returns `UClass`.
- `ID`: stable editor node instance GUID.
- `ExpressionIndent` and `ExpressionOperand`: condition expression metadata.

`FStateTreeNodeBase::GetInstanceDataType()` and `GetExecutionRuntimeDataType()` return `const UStruct*`, not always `UScriptStruct*`.

Reference:

- `E:\Epic Games\UE_5.7\Engine\Plugins\Runtime\StateTree\Source\StateTreeModule\Public\StateTreeNodeBase.h`

### Official Builder APIs

The public template helpers initialize C++ struct nodes:

- `UStateTreeEditorData::AddEvaluator<T>()`
- `UStateTreeEditorData::AddGlobalTask<T>()`
- `UStateTreeState::AddEnterCondition<T>()`
- `UStateTreeState::AddTask<T>()`
- `FStateTreeTransition::AddCondition<T>()`

Reference:

- `E:\Epic Games\UE_5.7\Engine\Plugins\Runtime\StateTree\Source\StateTreeEditorModule\Public\StateTreeEditorData.h`
- `E:\Epic Games\UE_5.7\Engine\Plugins\Runtime\StateTree\Source\StateTreeEditorModule\Public\StateTreeState.h`

Those templates cannot be used directly for JSON dynamic node types. They are still useful as the algorithm to mirror:

1. Add an `FStateTreeEditorNode`.
2. Assign `ID`.
3. Initialize `Node` with a struct.
4. Ask the node for instance and runtime data types.
5. Initialize matching `Instance` / `InstanceObject` and `ExecutionRuntimeData` / `ExecutionRuntimeDataObject`.

There is no public `AddConsideration<T>()`. Considerations are stored directly in `UStateTreeState::Considerations`.

### Dynamic Construction Path

The editor UI's dynamic construction path is more relevant than the template helpers:

- `StateTreeEditorNodeUtils.cpp::SetNodeTypeStruct()`
- `StateTreeEditorNodeUtils.cpp::SetNodeTypeClass()`
- `StateTreeEditorNodeUtils.cpp::ConditionalUpdateNodeInstanceData()`

Reference:

- `E:\Epic Games\UE_5.7\Engine\Plugins\Runtime\StateTree\Source\StateTreeEditorModule\Private\Customizations\StateTreeEditorNodeUtils.cpp`

For C++ struct nodes, the UI initializes the node struct and then creates either struct instance data or object instance data depending on returned `UStruct` type.

For Blueprint nodes, the UI chooses one of these wrapper structs:

- `FStateTreeBlueprintTaskWrapper`
- `FStateTreeBlueprintEvaluatorWrapper`
- `FStateTreeBlueprintConditionWrapper`
- `FStateTreeBlueprintConsiderationWrapper`

Then it creates `InstanceObject = NewObject<UObject>(Outer, InClass)`.

### Compiler Strictness

The compiler validates instance data strictly:

- Instance struct type must exactly match `Node.GetInstanceDataType()`.
- Instance object class must exactly match `Node.GetInstanceDataType()`.
- Missing instance data is a compiler error.
- Execution runtime data follows the same rules.

Reference:

- `E:\Epic Games\UE_5.7\Engine\Plugins\Runtime\StateTree\Source\StateTreeEditorModule\Private\StateTreeCompiler.cpp`

This means generator validation should catch bad or missing node data before compile where possible.

## Schema Validation Findings

There is no single "category validation" API. Category metadata is used by the editor picker for grouping and display, not as the authoritative rule.

The authoritative checks are:

- `UStateTreeSchema::IsStructAllowed()`
- `UStateTreeSchema::IsClassAllowed()`
- `UStateTreeSchema::AllowEvaluators()`
- `UStateTreeSchema::AllowEnterConditions()`
- `UStateTreeSchema::AllowUtilityConsiderations()`
- `UStateTreeSchema::AllowMultipleTasks()`

Reference:

- `E:\Epic Games\UE_5.7\Engine\Plugins\Runtime\StateTree\Source\StateTreeModule\Public\StateTreeSchema.h`

`UStateTreeEditorSchema::Validate()` may remove disallowed nodes from editor data. The generator should not rely on that because silent removal is bad for LLM-driven workflows. Spec 2 should report clear validation errors before compile.

## Existing Project Patterns

`BehaviorTreeGenerator` offers reusable patterns but not direct architecture:

- It dynamically resolves node classes.
- It validates JSON structure before creation.
- It applies properties through reflection via `FPropertySetterUtils`.
- It reports readable errors for unknown node types and invalid structure.

It should not be copied in areas where BehaviorTree writes runtime tree nodes directly. StateTree generator must continue to write editor data and then compile.

Useful project helpers:

- `FClassFinderUtils::FindClassByName()` for UObject class lookup.
- `FPropertySetterUtils::SetStructFromJson()` for `FInstancedStruct` memory.
- `FPropertySetterUtils::SetPropertiesFromJson()` for UObject instance data.
- `FPropertySetterUtils::ExtractStructToJson()` and `ExtractPropertiesToJson()` for extraction.

Missing helper:

- A public `UScriptStruct` resolver. Spec 2 should add StateTree-local struct lookup rather than hard-coding node headers and class names.

## Fixture Candidates

Stable common nodes for smoke tests:

- `/Script/StateTreeModule.StateTreeDelayTask`
  - Instance: `/Script/StateTreeModule.StateTreeDelayTaskInstanceData`
  - Instance properties: `Duration`, `RandomDeviation`, `bRunForever`
- `/Script/StateTreeModule.StateTreeDebugTextTask`
  - Node properties: `Text`, `TextColor`, `FontScale`, `Offset`, `bEnabled`
  - Instance properties: `ReferenceActor`, `BindableText`
- `/Script/StateTreeModule.StateTreeCompareIntCondition`
  - Node properties: `bInvert`, `Operator`
  - Instance properties: `Left`, `Right`
- `/Script/StateTreeModule.StateTreeCompareFloatCondition`
  - Node properties: `bInvert`, `Operator`
  - Instance properties: `Left`, `Right`

`/Script/GameplayStateTreeModule.StateTreeComponentSchema` allows common task, condition, evaluator, consideration, and property function base structs. It is suitable for the common-node smoke path.

`/Script/GameplayStateTreeModule.StateTreeAIComponentSchema` additionally allows AI nodes. It should be used later for an AI schema validation fixture such as `StateTreeMoveToTask`.

## Recommended Interpretation For Spec 2

Spec 2 should define the full future-proof node contract for:

- C++ struct nodes
- Blueprint wrapper nodes
- tasks
- global tasks
- evaluators
- enter conditions
- transition conditions
- considerations
- node, instance, and execution runtime data properties
- stable node IDs

Implementation may be split:

- 2A: C++ struct node construction for tasks and enter conditions, with the full contract shape accepted and validated where possible.
- 2B: Blueprint wrapper node construction.
- 2C: richer extraction of node skeleton and properties.

This keeps the contract aligned with the final generator while letting implementation land in safe, reviewable steps.
