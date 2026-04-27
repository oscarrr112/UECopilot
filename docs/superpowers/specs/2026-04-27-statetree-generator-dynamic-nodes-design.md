# StateTree Generator Dynamic Nodes Design

- Date: 2026-04-27
- Status: Draft for implementation handoff
- Branch: `feature/statetree-dynamic-nodes`
- Depends on: `feature/statetree-core-lifecycle`

## Goal

Define and implement the StateTree generator's durable JSON node contract. This spec is about dynamic node construction, not the full state machine structure. It must support the final goal of a feature-complete StateTree generator, including later transitions, parameters, property bindings, extraction, and round-trip.

## Design Position

Spec 2 is the contract foundation for every later StateTree feature. The implementation can land in phases, but the JSON shape must be broad enough to survive the later specs.

The generator should accept a common node shape for all editor node slots:

- `Evaluators`
- `GlobalTasks`
- state `Tasks`
- state `EnterConditions`
- transition `Conditions` in a later structure spec
- state `Considerations`

The generator should construct `FStateTreeEditorNode` dynamically using UE reflection and StateTree schema validation, then rely on the official compiler to bake runtime data.

## Non-goals

This spec does not implement:

- transition target resolution
- linked states, linked subtrees, linked assets
- root/state parameters and property bags
- property bindings or property function bindings
- full round-trip equality
- direct writes to runtime compact StateTree data

Those belong to later specs. This spec must not block them by choosing a narrow or incompatible node contract.

## Brainstorming Outcome

Three approaches were considered.

### Approach A: Minimal C++ Task Slice

Only support common C++ task and condition nodes such as `StateTreeDelayTask` and `StateTreeCompareIntCondition`.

Trade-off: fastest implementation and verification, but too narrow as a design. It risks baking a contract that later needs to be broken for evaluators, Blueprint nodes, and bindings.

### Approach B: Full Node Contract With Phased Implementation

Define the complete node contract now, including Blueprint wrapper fields and runtime data fields. Implement in phases: C++ struct nodes first, Blueprint wrapper nodes next, extraction last.

Trade-off: more design work up front, but stable for the feature-complete generator.

### Approach C: Full Node Implementation In One PR

Implement C++ struct nodes, Blueprint wrapper nodes, extract, and all categories at once.

Trade-off: closest to final capability, but too much risk for one spec because Blueprint wrapper instance objects and validation are easy to get subtly wrong.

Selected approach: Approach B.

## JSON Contract

Top-level contract extends the core lifecycle schema:

```json
{
  "AssetType": "StateTree",
  "Name": "ST_Enemy",
  "Path": "/Game/AI",
  "SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema",
  "Evaluators": [],
  "GlobalTasks": [],
  "GlobalTasksCompletion": "Any",
  "SubTrees": [
    {
      "id": "root",
      "name": "Root",
      "children": [
        {
          "id": "idle",
          "name": "Idle",
          "tasks": []
        }
      ]
    }
  ]
}
```

Node shape:

```json
{
  "id": "delay",
  "kind": "task",
  "type": "/Script/StateTreeModule.StateTreeDelayTask",
  "node": {
    "properties": {}
  },
  "instance": {
    "properties": {
      "Duration": 0.1,
      "RandomDeviation": 0.0,
      "bRunForever": false
    }
  },
  "executionRuntimeData": {
    "properties": {}
  },
  "expression": {
    "indent": 0,
    "operand": "And"
  }
}
```

Field meanings:

- `id`: optional stable user-facing node ID. Generator derives deterministic GUIDs where practical. Extract must emit it later.
- `kind`: semantic slot kind. Valid values are `task`, `globalTask`, `evaluator`, `enterCondition`, `transitionCondition`, and `consideration`.
- `type`: C++ `UScriptStruct` path/name or Blueprint node class path/name.
- `node.properties`: properties on the node template struct or wrapper struct.
- `instance.properties`: properties on instance data, either struct memory or UObject instance.
- `executionRuntimeData.properties`: properties on execution runtime data.
- `expression`: valid for condition and consideration nodes. It maps to `ExpressionIndent` and `ExpressionOperand`.

For convenience, users may write `properties` as an alias for `instance.properties` only for C++ struct nodes whose user-facing editable fields live entirely in instance data. The canonical shape remains `node` / `instance` / `executionRuntimeData`.

## Dynamic Node Construction

The generator must not hard-code built-in StateTree node types.

For C++ struct nodes:

1. Resolve `type` to `UScriptStruct`.
2. Verify the struct derives from the expected StateTree base for the slot.
3. Verify `Schema->IsStructAllowed(Struct)`.
4. Append an `FStateTreeEditorNode` to the correct editor data field.
5. Set `ID`.
6. Initialize `EditorNode.Node.InitializeAs(Struct)`.
7. Apply `node.properties` to `EditorNode.Node.GetMutableMemory()` through `FPropertySetterUtils::SetStructFromJson()`.
8. Ask the node for `GetInstanceDataType()` and initialize struct or object data.
9. Apply `instance.properties`.
10. Ask the node for `GetExecutionRuntimeDataType()` and initialize struct or object data.
11. Apply `executionRuntimeData.properties`.

For Blueprint nodes:

1. Resolve `type` to `UClass`.
2. Verify it derives from the expected Blueprint base class for the slot.
3. Verify `Schema->IsClassAllowed(Class)`.
4. Initialize `EditorNode.Node` with the matching wrapper struct:
   - task: `FStateTreeBlueprintTaskWrapper`
   - evaluator: `FStateTreeBlueprintEvaluatorWrapper`
   - condition: `FStateTreeBlueprintConditionWrapper`
   - consideration: `FStateTreeBlueprintConsiderationWrapper`
5. Create `InstanceObject = NewObject<UObject>(Outer, Class)`.
6. Apply `instance.properties` to the object.
7. Initialize execution runtime data if the wrapper reports a runtime data type.

Blueprint wrapper support is part of the contract. It can be implemented after C++ struct node support in this branch or a stacked child branch if it becomes too large.

## State Shape For Spec 2

Spec 2 needs enough state structure to host nodes, but it does not own full structure semantics.

Accepted fields:

```json
{
  "id": "idle",
  "name": "Idle",
  "type": "State",
  "tasks": [],
  "enterConditions": [],
  "considerations": [],
  "children": []
}
```

Spec 2 may create child states recursively. It should not implement transitions or linked state semantics. If transition input appears, return a clear "not supported in Spec 2" error unless it only contains condition nodes under a later gated path.

## Validation Rules

The generator should validate before mutating editor data where possible:

- `SubTrees` must be an array of objects.
- State `name` must be present and non-empty.
- Node arrays must contain objects.
- `kind` must match the containing slot.
- Unknown `type` returns a readable error with the original type string.
- A condition struct in a task slot returns a category/type mismatch error before compile.
- Schema disallowed structs/classes return a readable schema error.
- If `Schema->AllowEvaluators()` is false, reject evaluator input.
- If `Schema->AllowEnterConditions()` is false, reject enter condition input.
- If `Schema->AllowUtilityConsiderations()` is false, reject consideration input.
- If `Schema->AllowMultipleTasks()` is false and more than one task is provided, reject rather than silently allowing editor schema validation to drop nodes.
- If a node reports required instance data, initialize it automatically. Missing user `instance.properties` is not an error if the instance data can be default-initialized.
- If a node reports an instance or runtime type that is neither `UScriptStruct` nor `UClass`, return an unsupported data type error.

## Extraction Requirements

Spec 2 extraction should move beyond subtree skeleton. It should emit node skeletons and reflected properties for nodes it can understand:

```json
{
  "id": "delay",
  "kind": "task",
  "type": "/Script/StateTreeModule.StateTreeDelayTask",
  "node": { "properties": {} },
  "instance": { "type": "/Script/StateTreeModule.StateTreeDelayTaskInstanceData", "properties": {} },
  "executionRuntimeData": { "properties": {} }
}
```

Full stable round-trip remains Spec 6. Spec 2 only needs enough extraction to verify dynamic node creation and to avoid hiding generated data from users.

## File Design

Keep `StateTreeGenerator.cpp` as lifecycle orchestration. Add focused helpers under:

- `Source/AssetFactory/Private/Generators/StateTree/StateTreeJsonTypes.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeClassResolver.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeClassResolver.cpp`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeNodeBuilder.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeNodeBuilder.cpp`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeStateBuilder.cpp`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeValidation.h`
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeValidation.cpp`

Later specs may add:

- `StateTreeBindingsBuilder.*`
- `StateTreeParameterBuilder.*`
- `StateTreeExtract.*`

## Fixtures

Positive fixtures:

- `TestData/ST_Dynamic_Delay_Minimal.json`
- `TestData/ST_Dynamic_DebugText_WithInstance.json`
- `TestData/ST_Dynamic_Condition_CompareInt.json`

Negative fixtures:

- `TestData/ST_Dynamic_Invalid_Category_TaskSlotCondition.json`
- `TestData/ST_Dynamic_Invalid_UnknownNode.json`
- `TestData/ST_Dynamic_Invalid_SchemaAIMoveToInComponent.json`

Avoid a "missing instance data" fixture unless the implementation adds a debug/test-only escape hatch. In normal generator behavior, required instance data should be auto-initialized with defaults.

## Verification

Minimum verification for implementation:

1. `npm run build` in `MCP`.
2. UBT Development compile in an isolated temp host project.
3. Editor/MCP smoke:
   - `health_check`
   - `generate_assets` with delay task fixture
   - `generate_assets` with debug text fixture
   - `generate_assets` with condition fixture
   - `extract_assets` verifies node skeleton and instance data
   - invalid fixtures return readable errors
4. Close Unreal Editor after verification.

## Handoff Notes

This branch currently contains only docs for Spec 2. It is intentionally stacked on the core lifecycle branch so another machine can continue without waiting for PR #2 to merge.
