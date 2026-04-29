# StateTree Rich Binding Paths And Function Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make StateTree binding path metadata authorable, make property function inputs use explicit multi-segment `target` paths, and emit structured extraction diagnostics for unsupported or cyclic function graphs.

**Architecture:** Normalize property function inputs into an ordered array model at parse time, while accepting legacy object input maps for old fixtures. Reuse the existing endpoint/path resolver, extend `FPropertyBindingPathSegment` construction to consume `instanceStruct` and `access`, and return extraction diagnostics next to extracted bindings without making diagnostics authoring input.

**Tech Stack:** Unreal Engine 5.7 C++ editor plugin, StateTree editor bindings, PropertyBindingUtils, TypeScript MCP broker tests, JSON fixtures, Python semantic round-trip checker, real Editor/MCP smoke via `localhost:8559`.

---

## File Structure

- `MCP/src/broker/stateTreeRichBindingContract.test.ts`: static contract tests for canonical `function.inputs[]`, legacy object compatibility fixtures, rich path metadata in docs/fixtures, manifest coverage, and diagnostics smoke artifacts.
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingTypes.h`: C++ parse model for explicit function input targets.
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeJsonTypes.h`: JSON parsing for canonical input arrays, legacy input objects, and exact validation errors.
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingBuilder.cpp`: function binding preparation using parsed target paths instead of object map keys.
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingResolver.cpp`: rich path segment construction for `guid`, `arrayIndex`, `instanceStruct`, and `access`.
- `Source/AssetFactory/Private/Test/StateTreeRichBindingTestTypes.h`: AssetFactory-only StateTree property function test type with an `FInstancedStruct` input path for smokeable rich metadata.
- `Source/AssetFactory/Private/Test/StateTreeRichBindingTestTypes.cpp`: execution implementation for the AssetFactory-only property function.
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingExtract.h`: extraction result wrapper with bindings and diagnostics.
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingExtract.cpp`: canonical function input extraction and diagnostic collection.
- `Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp`: copy extraction diagnostics into top-level StateTree JSON.
- `docs/superpowers/verification/statetree_roundtrip_check.py`: canonicalize rich path segment metadata, canonicalize legacy/function input representations, and ignore diagnostics during round-trip semantic compare.
- `docs/superpowers/verification/statetree_binding_roundtrip_check.py`: keep binding-only comparison aligned with the canonical input array.
- `MCP/schemas/StateTree.md`: document authorable rich path segment metadata, canonical function input arrays, legacy compatibility, and extraction diagnostics.
- `TestData/ST_Bindings_Function.json`: migrate to canonical function input array.
- `TestData/ST_RoundTrip_Comprehensive.json`: migrate embedded function inputs to canonical arrays.
- `TestData/ST_Bindings_Invalid_BadFunctionType.json`: migrate invalid function input shape while preserving the intended bad type error.
- `TestData/ST_Bindings_Function_LegacyInputsObject.json`: compatibility fixture proving old object-shaped inputs still generate.
- `TestData/ST_Bindings_Function_RichInputTargets.json`: positive fixture for explicit `target` arrays and rich segment metadata that can be generated.
- `TestData/ST_Bindings_Invalid_FunctionInputMissingTarget.json`: negative fixture for canonical input entries without `target`.
- `TestData/ST_Bindings_Invalid_FunctionInputEmptyTarget.json`: negative fixture for canonical input entries with empty target arrays.
- `TestData/ST_Bindings_Invalid_FunctionInputBadAccess.json`: negative fixture for unknown `access`.
- `TestData/ST_Bindings_Invalid_FunctionInputUnknownInstanceStruct.json`: negative fixture for unknown `instanceStruct`.
- `TestData/ST_Bindings_ExtractDiagnostics_FunctionCycle.expected.json`: checked diagnostic shape for cyclic function graphs.
- `TestData/StateTreeSmokeManifest.json`: include new positive/negative binding fixtures in normal smoke.
- `MCP/scripts/statetree_roundtrip_mcp_smoke.mjs`: assert extracted diagnostics do not appear in normal smoke outputs and optionally check diagnostic fixture artifacts when present.
- `/Volumes/Mac/GameDev/ProjectRPG/AGENTS.md`: remove or narrow the StateTree final verification deferred limits entry after smoke confirms this spec; commit this in the ProjectRPG root repository, not inside the AssetFactory plugin repository.

## Task 1: Contract Tests And Fixture Baseline

**Files:**
- Create: `MCP/src/broker/stateTreeRichBindingContract.test.ts`
- Modify: `TestData/ST_Bindings_Function.json`
- Modify: `TestData/ST_RoundTrip_Comprehensive.json`
- Modify: `TestData/ST_Bindings_Invalid_BadFunctionType.json`
- Create: `TestData/ST_Bindings_Function_LegacyInputsObject.json`
- Create: `TestData/ST_Bindings_Function_RichInputTargets.json`
- Create: `TestData/ST_Bindings_Invalid_FunctionInputMissingTarget.json`
- Create: `TestData/ST_Bindings_Invalid_FunctionInputEmptyTarget.json`
- Create: `TestData/ST_Bindings_Invalid_FunctionInputBadAccess.json`
- Create: `TestData/ST_Bindings_Invalid_FunctionInputUnknownInstanceStruct.json`
- Create: `TestData/ST_Bindings_ExtractDiagnostics_FunctionCycle.expected.json`
- Modify: `TestData/StateTreeSmokeManifest.json`

- [ ] **Step 1: Add static contract tests**

Create `MCP/src/broker/stateTreeRichBindingContract.test.ts`:

```ts
import test from "node:test";
import assert from "node:assert/strict";
import { existsSync, readFileSync } from "fs";
import { dirname, join } from "path";
import { fileURLToPath } from "url";

const DIST_DIR = dirname(dirname(fileURLToPath(import.meta.url)));
const PROJECT_DIR = dirname(DIST_DIR);
const REPO_DIR = dirname(PROJECT_DIR);
const TEST_DATA_DIR = join(REPO_DIR, "TestData");
const MANIFEST_PATH = join(TEST_DATA_DIR, "StateTreeSmokeManifest.json");

type JsonObject = Record<string, unknown>;

function readJson(name: string): JsonObject {
	return JSON.parse(readFileSync(join(TEST_DATA_DIR, name), "utf8")) as JsonObject;
}

function readFixture(name: string): JsonObject {
	return readJson(`${name}.json`);
}

function asObject(value: unknown, label: string): JsonObject {
	assert.equal(typeof value, "object", `${label} should be an object`);
	assert.notEqual(value, null, `${label} should not be null`);
	assert.ok(!Array.isArray(value), `${label} should not be an array`);
	return value as JsonObject;
}

function asArray(value: unknown, label: string): unknown[] {
	assert.ok(Array.isArray(value), `${label} should be an array`);
	return value;
}

function collectFunctionObjects(value: unknown, path = "$"): Array<{ path: string; functionObject: JsonObject }> {
	const result: Array<{ path: string; functionObject: JsonObject }> = [];
	if (Array.isArray(value)) {
		value.forEach((child, index) => result.push(...collectFunctionObjects(child, `${path}[${index}]`)));
		return result;
	}
	if (!value || typeof value !== "object") {
		return result;
	}

	const object = value as JsonObject;
	if (object.function) {
		result.push({ path: `${path}.function`, functionObject: asObject(object.function, `${path}.function`) });
	}
	for (const [key, child] of Object.entries(object)) {
		result.push(...collectFunctionObjects(child, `${path}.${key}`));
	}
	return result;
}

function assertCanonicalFunctionInputs(fixtureName: string): void {
	const fixture = readFixture(fixtureName);
	for (const entry of collectFunctionObjects(fixture)) {
		const inputs = asArray(entry.functionObject.inputs, `${fixtureName}${entry.path}.inputs`);
		assert.ok(inputs.length > 0, `${fixtureName}${entry.path}.inputs should not be empty`);
		for (const [index, inputValue] of inputs.entries()) {
			const input = asObject(inputValue, `${fixtureName}${entry.path}.inputs[${index}]`);
			const target = asArray(input.target, `${fixtureName}${entry.path}.inputs[${index}].target`);
			assert.ok(target.length > 0, `${fixtureName}${entry.path}.inputs[${index}].target should not be empty`);
			assert.notEqual("name" in input, true, `${fixtureName}${entry.path}.inputs[${index}] should use target, not name`);
			const hasSource = Object.prototype.hasOwnProperty.call(input, "source");
			const hasFunction = Object.prototype.hasOwnProperty.call(input, "function");
			assert.notEqual(hasSource, hasFunction, `${fixtureName}${entry.path}.inputs[${index}] should define exactly one of source/function`);
		}
	}
}

test("canonical StateTree function fixtures use explicit inputs arrays", () => {
	for (const fixtureName of [
		"ST_Bindings_Function",
		"ST_RoundTrip_Comprehensive",
		"ST_Bindings_Invalid_BadFunctionType",
		"ST_Bindings_Function_RichInputTargets",
	]) {
		assertCanonicalFunctionInputs(fixtureName);
	}
});

test("legacy object-shaped function input fixture is isolated and named as compatibility", () => {
	const legacy = readFixture("ST_Bindings_Function_LegacyInputsObject");
	const functions = collectFunctionObjects(legacy);
	assert.ok(functions.length > 0, "legacy fixture should contain a property function");
	for (const entry of functions) {
		assert.equal(Array.isArray(entry.functionObject.inputs), false, `${entry.path}.inputs should remain legacy object shape`);
		assert.equal(typeof entry.functionObject.inputs, "object", `${entry.path}.inputs should be an object`);
	}
});

test("rich binding path segment metadata appears only in canonical path arrays", () => {
	const fixture = readFixture("ST_Bindings_Function_RichInputTargets");
	const serialized = JSON.stringify(fixture);
	assert.match(serialized, /"instanceStruct"/);
	assert.match(serialized, /"access"/);
	for (const entry of collectFunctionObjects(fixture)) {
		asArray(entry.functionObject.inputs, `${entry.path}.inputs`);
	}
});

test("new binding fixtures are present in the StateTree smoke manifest", () => {
	const manifest = readJson("StateTreeSmokeManifest.json");
	const fixtures = asArray(manifest.fixtures, "manifest.fixtures").map((entry) => asObject(entry, "manifest fixture"));
	const byName = new Map(fixtures.map((entry) => [String(entry.name), entry]));
	for (const name of [
		"ST_Bindings_Function_LegacyInputsObject",
		"ST_Bindings_Function_RichInputTargets",
		"ST_Bindings_Invalid_FunctionInputMissingTarget",
		"ST_Bindings_Invalid_FunctionInputEmptyTarget",
		"ST_Bindings_Invalid_FunctionInputBadAccess",
		"ST_Bindings_Invalid_FunctionInputUnknownInstanceStruct",
	]) {
		assert.ok(byName.has(name), `${name} should be listed in StateTreeSmokeManifest.json`);
		assert.ok(existsSync(join(TEST_DATA_DIR, `${name}.json`)), `${name}.json should exist`);
	}
	assert.equal(byName.get("ST_Bindings_Function_LegacyInputsObject")?.kind, "positive");
	assert.equal(byName.get("ST_Bindings_Function_RichInputTargets")?.kind, "positive");
	for (const name of [
		"ST_Bindings_Invalid_FunctionInputMissingTarget",
		"ST_Bindings_Invalid_FunctionInputEmptyTarget",
		"ST_Bindings_Invalid_FunctionInputBadAccess",
		"ST_Bindings_Invalid_FunctionInputUnknownInstanceStruct",
	]) {
		assert.equal(byName.get(name)?.kind, "negative");
		assert.ok(String(byName.get(name)?.expectedError ?? "").length > 0, `${name} should have expectedError`);
	}
});

test("function extraction diagnostics contract uses stable code and path fields", () => {
	const expected = readJson("ST_Bindings_ExtractDiagnostics_FunctionCycle.expected.json");
	const diagnostics = asObject(expected.diagnostics, "diagnostics");
	const bindings = asArray(diagnostics.bindings, "diagnostics.bindings");
	assert.ok(bindings.length >= 1, "diagnostics.bindings should contain at least one diagnostic");
	const first = asObject(bindings[0], "diagnostics.bindings[0]");
	assert.equal(first.code, "StateTree.Binding.FunctionCycle");
	assert.equal(typeof first.message, "string");
	assert.equal(typeof first.path, "string");
	assert.equal(typeof first.bindingTarget, "string");
});
```

- [ ] **Step 2: Run static tests to verify the new contract fails before fixtures exist**

Run:

```bash
npm --prefix MCP test
```

Expected:

```text
not ok ... canonical StateTree function fixtures use explicit inputs arrays
...
ST_Bindings_Function_RichInputTargets.json
```

- [ ] **Step 3: Migrate `ST_Bindings_Function.json` to canonical input arrays**

Replace the `function.inputs` object with:

```json
"inputs": [
	{
		"target": ["Left"],
		"source": { "kind": "rootParameter", "path": ["BaseDelay"] }
	},
	{
		"target": ["Right"],
		"source": { "kind": "rootParameter", "path": ["BonusDelay"] }
	}
]
```

- [ ] **Step 4: Migrate `ST_RoundTrip_Comprehensive.json` function input maps**

For each property function object in `TestData/ST_RoundTrip_Comprehensive.json`, replace:

```json
"inputs": {
	"Left": {
		"source": { "kind": "rootParameter", "path": ["BaseDelay"] }
	},
	"Right": {
		"source": { "kind": "rootParameter", "path": ["BonusDelay"] }
	}
}
```

with:

```json
"inputs": [
	{
		"target": ["Left"],
		"source": { "kind": "rootParameter", "path": ["BaseDelay"] }
	},
	{
		"target": ["Right"],
		"source": { "kind": "rootParameter", "path": ["BonusDelay"] }
	}
]
```

Use the actual source endpoint values already present in the file; only change the container shape and add `target`.

- [ ] **Step 5: Migrate `ST_Bindings_Invalid_BadFunctionType.json` without changing its failure purpose**

Replace the invalid fixture's function input object with:

```json
"inputs": [
	{
		"target": ["Left"],
		"source": { "kind": "rootParameter", "path": ["BaseDelay"] }
	}
]
```

Keep the existing invalid `type` unchanged so the expected failure remains `property function type`.

- [ ] **Step 6: Add the legacy compatibility fixture**

Create `TestData/ST_Bindings_Function_LegacyInputsObject.json` as an exact copy of the pre-migration `ST_Bindings_Function.json` object-shaped input fixture:

```json
{
	"AssetType": "StateTree",
	"Name": "ST_Bindings_Function_LegacyInputsObject",
	"Path": "/Game/AFSmoke",
	"Action": "CreateOrUpdate",
	"SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema",
	"rootParameters": {
		"BaseDelay": { "type": "Float", "value": 0.25 },
		"BonusDelay": { "type": "Float", "value": 0.15 }
	},
	"SubTrees": [
		{
			"id": "root",
			"name": "Root",
			"type": "State",
			"children": [
				{
					"id": "idle",
					"name": "Idle",
					"type": "State",
					"tasks": [
						{
							"id": "delay-task",
							"kind": "task",
							"type": "/Script/StateTreeModule.StateTreeDelayTask",
							"instance": {
								"properties": {
									"Duration": 0.1
								}
							}
						}
					]
				}
			]
		}
	],
	"bindings": [
		{
			"id": "duration-from-add-float-legacy-input-object",
			"function": {
				"type": "/Script/StateTreeModule.StateTreeAddFloatPropertyFunction",
				"output": ["Result"],
				"inputs": {
					"Left": {
						"source": { "kind": "rootParameter", "path": ["BaseDelay"] }
					},
					"Right": {
						"source": { "kind": "rootParameter", "path": ["BonusDelay"] }
					}
				}
			},
			"target": {
				"kind": "task",
				"state": "Root/Idle",
				"node": "delay-task",
				"section": "instance",
				"path": ["Duration"]
			}
		}
	]
}
```

- [ ] **Step 7: Add the rich input target positive fixture**

Create `TestData/ST_Bindings_Function_RichInputTargets.json`. This fixture uses the AssetFactory-only test property function added in Task 3; static tests can reference it before the C++ type exists because they only validate JSON shape.

```json
{
	"AssetType": "StateTree",
	"Name": "ST_Bindings_Function_RichInputTargets",
	"Path": "/Game/AFSmoke",
	"Action": "CreateOrUpdate",
	"SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema",
	"rootParameters": {
		"BaseDelay": { "type": "Float", "value": 0.25 }
	},
	"SubTrees": [
		{
			"id": "root",
			"name": "Root",
			"type": "State",
			"children": [
				{
					"id": "idle",
					"name": "Idle",
					"type": "State",
					"tasks": [
						{
							"id": "delay-task",
							"kind": "task",
							"type": "/Script/StateTreeModule.StateTreeDelayTask",
							"instance": {
								"properties": {
									"Duration": 0.1
								}
							}
						}
					]
				}
			]
		}
	],
	"bindings": [
		{
			"id": "duration-from-add-float-rich-input-targets",
			"function": {
				"type": "/Script/AssetFactory.AFStateTreeRichBindingInputPropertyFunction",
				"output": ["Result"],
				"inputs": [
					{
						"target": [
							{
								"name": "DynamicInput",
								"instanceStruct": "/Script/AssetFactory.AFStateTreeRichBindingPayload",
								"access": "StructInstance"
							},
							"Value"
						],
						"source": { "kind": "rootParameter", "path": ["BaseDelay"] }
					}
				]
			},
			"target": {
				"kind": "task",
				"state": "Root/Idle",
				"node": "delay-task",
				"section": "instance",
				"path": ["Duration"]
			}
		}
	]
}
```

- [ ] **Step 8: Add negative fixtures for canonical input target validation**

Create `TestData/ST_Bindings_Invalid_FunctionInputMissingTarget.json`:

```json
{
	"AssetType": "StateTree",
	"Name": "ST_Bindings_Invalid_FunctionInputMissingTarget",
	"Path": "/Game/AFSmoke",
	"Action": "CreateOrUpdate",
	"SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema",
	"rootParameters": {
		"BaseDelay": { "type": "Float", "value": 0.25 }
	},
	"SubTrees": [
		{
			"id": "root",
			"name": "Root",
			"type": "State",
			"children": [
				{
					"id": "idle",
					"name": "Idle",
					"type": "State",
					"tasks": [
						{
							"id": "delay-task",
							"kind": "task",
							"type": "/Script/StateTreeModule.StateTreeDelayTask",
							"instance": { "properties": { "Duration": 0.1 } }
						}
					]
				}
			]
		}
	],
	"bindings": [
		{
			"id": "missing-input-target",
			"function": {
				"type": "/Script/StateTreeModule.StateTreeAddFloatPropertyFunction",
				"output": ["Result"],
				"inputs": [
					{
						"source": { "kind": "rootParameter", "path": ["BaseDelay"] }
					}
				]
			},
			"target": {
				"kind": "task",
				"state": "Root/Idle",
				"node": "delay-task",
				"section": "instance",
				"path": ["Duration"]
			}
		}
	]
}
```

Create `TestData/ST_Bindings_Invalid_FunctionInputEmptyTarget.json` by copying the same file and changing:

```json
"Name": "ST_Bindings_Invalid_FunctionInputEmptyTarget"
```

and the input entry to:

```json
{
	"target": [],
	"source": { "kind": "rootParameter", "path": ["BaseDelay"] }
}
```

- [ ] **Step 9: Add negative fixtures for rich path metadata validation**

Create `TestData/ST_Bindings_Invalid_FunctionInputBadAccess.json` by copying `ST_Bindings_Function_RichInputTargets.json` and changing:

```json
"Name": "ST_Bindings_Invalid_FunctionInputBadAccess"
```

and the first input target segment to:

```json
{
	"name": "DynamicInput",
	"instanceStruct": "/Script/AssetFactory.AFStateTreeRichBindingPayload",
	"access": "BadAccess"
}
```

Create `TestData/ST_Bindings_Invalid_FunctionInputUnknownInstanceStruct.json` by copying `ST_Bindings_Function_RichInputTargets.json` and changing:

```json
"Name": "ST_Bindings_Invalid_FunctionInputUnknownInstanceStruct"
```

and the first input target segment to:

```json
{
	"name": "DynamicInput",
	"instanceStruct": "/Script/StateTreeModule.DoesNotExist",
	"access": "StructInstance"
}
```

- [ ] **Step 10: Add expected diagnostic contract JSON**

Create `TestData/ST_Bindings_ExtractDiagnostics_FunctionCycle.expected.json`:

```json
{
	"diagnostics": {
		"bindings": [
			{
				"code": "StateTree.Binding.FunctionCycle",
				"severity": "warning",
				"path": "$.bindings[0].function.inputs[0].function",
				"bindingTarget": "00000000-0000-0000-0000-000000000000:Result",
				"message": "Skipped nested StateTree property function input because the graph references an already visited function node."
			}
		]
	}
}
```

The zero GUID is fixture-only static sample data; extraction checks compare `code`, `severity`, and field presence, not this literal GUID.

- [ ] **Step 11: Update the smoke manifest**

Insert these fixtures in `TestData/StateTreeSmokeManifest.json` after `ST_Bindings_Function`:

```json
{ "name": "ST_Bindings_Function_LegacyInputsObject", "kind": "positive", "spec": "bindings", "description": "Legacy property function input object compatibility.", "roundTrip": true },
{ "name": "ST_Bindings_Function_RichInputTargets", "kind": "positive", "spec": "bindings", "description": "Property function input targets with rich path segment metadata.", "roundTrip": true }
```

Insert these negative fixtures near the other binding negatives:

```json
{ "name": "ST_Bindings_Invalid_FunctionInputMissingTarget", "kind": "negative", "spec": "bindings", "description": "Reject canonical function input without target.", "expectedError": "must define required field 'target'" },
{ "name": "ST_Bindings_Invalid_FunctionInputEmptyTarget", "kind": "negative", "spec": "bindings", "description": "Reject canonical function input with empty target path.", "expectedError": "target must contain at least one path segment" },
{ "name": "ST_Bindings_Invalid_FunctionInputBadAccess", "kind": "negative", "spec": "bindings", "description": "Reject unknown rich binding path access metadata.", "expectedError": "unknown access" },
{ "name": "ST_Bindings_Invalid_FunctionInputUnknownInstanceStruct", "kind": "negative", "spec": "bindings", "description": "Reject unknown rich binding path instance struct metadata.", "expectedError": "unknown instanceStruct" }
```

- [ ] **Step 12: Run static tests again and capture current failures**

Run:

```bash
npm --prefix MCP test
```

Expected before C++ parser changes:

```text
ok ... StateTree rich binding contract static tests
```

The static contract tests should pass after fixture edits. Real generation still fails later because C++ still expects object-shaped function inputs.

- [ ] **Step 13: Commit Task 1**

Run:

```bash
git add MCP/src/broker/stateTreeRichBindingContract.test.ts TestData/ST_Bindings_Function.json TestData/ST_RoundTrip_Comprehensive.json TestData/ST_Bindings_Invalid_BadFunctionType.json TestData/ST_Bindings_Function_LegacyInputsObject.json TestData/ST_Bindings_Function_RichInputTargets.json TestData/ST_Bindings_Invalid_FunctionInputMissingTarget.json TestData/ST_Bindings_Invalid_FunctionInputEmptyTarget.json TestData/ST_Bindings_Invalid_FunctionInputBadAccess.json TestData/ST_Bindings_Invalid_FunctionInputUnknownInstanceStruct.json TestData/ST_Bindings_ExtractDiagnostics_FunctionCycle.expected.json TestData/StateTreeSmokeManifest.json
git commit -m "test: add StateTree rich binding path contracts"
```

## Task 2: Canonical Function Input Parser And Builder

**Files:**
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingTypes.h`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeJsonTypes.h`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingBuilder.cpp`

- [ ] **Step 1: Change function input parse model from map key to explicit target path**

In `StateTreeBindingTypes.h`, replace `FAFStateTreeBindingFunctionInputSpec` and the `Inputs` field with:

```cpp
struct FAFStateTreeBindingFunctionInputSpec
{
	TArray<FAFStateTreeBindingPathSegmentSpec> TargetPath;
	FAFStateTreeBindingEndpointSpec Source;
	TSharedPtr<FAFStateTreeBindingFunctionSpec> Function;
	FString SourceLabel;
	bool bHasSource = false;
	bool bHasFunction = false;
};

struct FAFStateTreeBindingFunctionSpec
{
	FString Type;
	TArray<FAFStateTreeBindingPathSegmentSpec> OutputPath;
	TArray<FAFStateTreeBindingFunctionInputSpec> Inputs;
};
```

- [ ] **Step 2: Replace input parser signature**

In `StateTreeJsonTypes.h`, replace `ParseBindingFunctionInputSpec` with:

```cpp
	inline bool ParseBindingFunctionInputSpec(
		const TSharedPtr<FJsonObject>& InputObject,
		int32 BindingIndex,
		int32 InputIndex,
		const FString& Label,
		FAFStateTreeBindingFunctionInputSpec& OutSpec,
		FString& OutError)
	{
		OutSpec = FAFStateTreeBindingFunctionInputSpec();
		OutSpec.SourceLabel = FString::Printf(TEXT("%s.inputs[%d]"), *Label, InputIndex);
		if (!InputObject.IsValid())
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] function %s input[%d] must be a JSON object"), BindingIndex, *Label, InputIndex);
			return false;
		}

		if (!HasBindingField(InputObject, TEXT("target"), TEXT("Target")))
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] function %s input[%d] must define required field 'target'"), BindingIndex, *Label, InputIndex);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* TargetValues = nullptr;
		if (!TryGetBindingArrayField(InputObject, TEXT("target"), TEXT("Target"), TargetValues))
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] function %s input[%d] target must be an array"), BindingIndex, *Label, InputIndex);
			return false;
		}
		if (!ParseBindingPathSegments(*TargetValues, FString::Printf(TEXT("%s.inputs[%d].target"), *Label, InputIndex), OutSpec.TargetPath, OutError))
		{
			return false;
		}
		if (OutSpec.TargetPath.IsEmpty())
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] function %s input[%d] target must contain at least one path segment"), BindingIndex, *Label, InputIndex);
			return false;
		}

		const bool bHasSourceField = HasBindingField(InputObject, TEXT("source"), TEXT("Source"));
		const bool bHasFunctionField = HasBindingField(InputObject, TEXT("function"), TEXT("Function"));
		if (bHasSourceField == bHasFunctionField)
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] function %s input[%d] must define exactly one of 'source' or 'function'"), BindingIndex, *Label, InputIndex);
			return false;
		}

		if (bHasSourceField)
		{
			TSharedPtr<FJsonObject> SourceObject;
			if (!TryGetBindingObjectField(InputObject, TEXT("source"), TEXT("Source"), SourceObject))
			{
				OutError = FString::Printf(TEXT("StateTree binding[%d] function %s input[%d] source must be a JSON object"), BindingIndex, *Label, InputIndex);
				return false;
			}
			if (!ParseBindingEndpointSpec(SourceObject, FString::Printf(TEXT("%s.inputs[%d].source"), *Label, InputIndex), OutSpec.Source, OutError))
			{
				return false;
			}
			OutSpec.bHasSource = true;
			return true;
		}

		TSharedPtr<FJsonObject> FunctionObject;
		if (!TryGetBindingObjectField(InputObject, TEXT("function"), TEXT("Function"), FunctionObject))
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] function %s input[%d] function must be a JSON object"), BindingIndex, *Label, InputIndex);
			return false;
		}

		OutSpec.Function = MakeShared<FAFStateTreeBindingFunctionSpec>();
		if (!ParseBindingFunctionSpec(FunctionObject, BindingIndex, FString::Printf(TEXT("%s.inputs[%d].function"), *Label, InputIndex), *OutSpec.Function, OutError))
		{
			return false;
		}
		OutSpec.bHasFunction = true;
		return true;
	}
```

- [ ] **Step 3: Add legacy object input normalization helper**

Still in `StateTreeJsonTypes.h`, add this helper immediately before `ParseBindingFunctionSpec`:

```cpp
	inline bool ParseLegacyBindingFunctionInputSpec(
		const TSharedPtr<FJsonObject>& InputObject,
		int32 BindingIndex,
		const FString& InputName,
		const FString& Label,
		FAFStateTreeBindingFunctionInputSpec& OutSpec,
		FString& OutError)
	{
		if (InputName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] function %s legacy input name must be non-empty"), BindingIndex, *Label);
			return false;
		}

		TSharedPtr<FJsonObject> NormalizedInput = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> TargetValues;
		TargetValues.Add(MakeShared<FJsonValueString>(InputName));
		NormalizedInput->SetArrayField(TEXT("target"), TargetValues);

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : InputObject->Values)
		{
			NormalizedInput->SetField(Pair.Key, Pair.Value);
		}

		if (!ParseBindingFunctionInputSpec(NormalizedInput, BindingIndex, 0, Label + TEXT(".legacyInputs.") + InputName, OutSpec, OutError))
		{
			return false;
		}
		OutSpec.SourceLabel = Label + TEXT(".inputs.") + InputName;
		return true;
	}
```

- [ ] **Step 4: Replace `inputs` parser to accept arrays first and legacy objects second**

Inside `ParseBindingFunctionSpec`, replace the existing `inputs` object block with:

```cpp
		if (!HasBindingField(FunctionObject, TEXT("inputs"), TEXT("Inputs")))
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] function %s is missing required field 'inputs'"), BindingIndex, *Label);
			return false;
		}

		const TSharedPtr<FJsonValue> InputsValue = FunctionObject->TryGetField(TEXT("inputs")).IsValid()
			? FunctionObject->TryGetField(TEXT("inputs"))
			: FunctionObject->TryGetField(TEXT("Inputs"));
		if (!InputsValue.IsValid())
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] function %s inputs are invalid"), BindingIndex, *Label);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* InputValues = nullptr;
		if (InputsValue->TryGetArray(InputValues))
		{
			if (!InputValues || InputValues->Num() == 0)
			{
				OutError = FString::Printf(TEXT("StateTree binding[%d] function %s inputs must contain at least one entry"), BindingIndex, *Label);
				return false;
			}

			for (int32 InputIndex = 0; InputIndex < InputValues->Num(); ++InputIndex)
			{
				const TSharedPtr<FJsonObject>* InputObject = nullptr;
				if (!(*InputValues)[InputIndex].IsValid()
					|| !(*InputValues)[InputIndex]->TryGetObject(InputObject)
					|| !InputObject
					|| !InputObject->IsValid())
				{
					OutError = FString::Printf(TEXT("StateTree binding[%d] function %s input[%d] must be a JSON object"), BindingIndex, *Label, InputIndex);
					return false;
				}

				FAFStateTreeBindingFunctionInputSpec InputSpec;
				if (!ParseBindingFunctionInputSpec(*InputObject, BindingIndex, InputIndex, Label, InputSpec, OutError))
				{
					return false;
				}
				OutSpec.Inputs.Add(MoveTemp(InputSpec));
			}
			return true;
		}

		const TSharedPtr<FJsonObject>* InputsObject = nullptr;
		if (!InputsValue->TryGetObject(InputsObject) || !InputsObject || !InputsObject->IsValid())
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] function %s inputs must be an array or legacy JSON object"), BindingIndex, *Label);
			return false;
		}
		if ((*InputsObject)->Values.Num() == 0)
		{
			OutError = FString::Printf(TEXT("StateTree binding[%d] function %s inputs must contain at least one entry"), BindingIndex, *Label);
			return false;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*InputsObject)->Values)
		{
			const TSharedPtr<FJsonObject>* InputObject = nullptr;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(InputObject) || !InputObject || !InputObject->IsValid())
			{
				OutError = FString::Printf(TEXT("StateTree binding[%d] function %s input '%s' must be a JSON object"), BindingIndex, *Label, *Pair.Key);
				return false;
			}

			FAFStateTreeBindingFunctionInputSpec InputSpec;
			if (!ParseLegacyBindingFunctionInputSpec(*InputObject, BindingIndex, Pair.Key, Label, InputSpec, OutError))
			{
				return false;
			}
			OutSpec.Inputs.Add(MoveTemp(InputSpec));
		}

		return true;
```

- [ ] **Step 5: Remove single-name-only builder validation**

In `StateTreeBindingBuilder.cpp`, delete `ValidateFunctionInputName`. It is replaced by reflected path validation against the function instance struct.

- [ ] **Step 6: Update builder loop to use explicit target paths**

In `PrepareFunctionBinding`, replace the loop over `TMap` pairs with:

```cpp
		for (int32 InputIndex = 0; InputIndex < FunctionSpec.Inputs.Num(); ++InputIndex)
		{
			const FAFStateTreeBindingFunctionInputSpec& InputSpec = FunctionSpec.Inputs[InputIndex];

			FPreparedFunctionInputBinding PreparedInput;
			TArray<FPropertyBindingPathSegment> InputSegments = MakeBindingPathSegments(InputSpec.TargetPath, OutError);
			if (!OutError.IsEmpty())
			{
				OutError = FString::Printf(TEXT("StateTree %s function input[%d] target could not be resolved: %s"), *BindingLabel, InputIndex, *OutError);
				return false;
			}
			if (!ValidateFunctionPath(
				InstanceStruct,
				InputSegments,
				FString::Printf(TEXT("function input[%d]"), InputIndex),
				PreparedInput.TargetSegments,
				OutError))
			{
				return false;
			}

			PreparedInput.InputName = FPropertyBindingPath(FGuid::NewGuid(), PreparedInput.TargetSegments).ToString();
			const FString InputLabel = FString::Printf(TEXT("%s function input[%d] '%s'"), *BindingLabel, InputIndex, *PreparedInput.InputName);
			if (InputSpec.bHasSource)
			{
				if (!ResolveBindingEndpointPath(Index, InputSpec.Source, PreparedInput.SourcePath, OutError))
				{
					OutError = FString::Printf(TEXT("StateTree %s source could not be resolved: %s"), *InputLabel, *OutError);
					return false;
				}
				PreparedInput.bHasSource = true;
			}
			else if (InputSpec.bHasFunction && InputSpec.Function.IsValid())
			{
				PreparedInput.Function = MakeShared<FPreparedFunctionBinding>();
				if (!PrepareFunctionBinding(Index, *InputSpec.Function, InputLabel, *PreparedInput.Function, OutError))
				{
					OutError = FString::Printf(TEXT("StateTree %s function failed: %s"), *InputLabel, *OutError);
					return false;
				}
				PreparedInput.bHasFunction = true;
			}

			OutPrepared.Inputs.Add(MoveTemp(PreparedInput));
		}
```

- [ ] **Step 7: Run TypeScript tests**

Run:

```bash
npm --prefix MCP test
```

Expected:

```text
# pass ...
```

- [ ] **Step 8: Run C++ syntax/build validation**

Run the normal project compile, not `BuildPlugin`:

```bash
~/UnrealEngine/Engine/Build/BatchFiles/Mac/Build.sh ProjectRPGEditor Mac Development -Project="/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject" -WaitMutex
```

Expected:

```text
BUILD SUCCESSFUL
```

If the shared build cache is cold and this starts compiling unrelated Engine actions, stop it after action counts are printed and use the Editor hot build path during the smoke task. Record the action count in the task notes instead of running `RunUAT`.

- [ ] **Step 9: Commit Task 2**

Run:

```bash
git add Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingTypes.h Source/AssetFactory/Private/Generators/StateTree/StateTreeJsonTypes.h Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingBuilder.cpp
git commit -m "feat: parse explicit StateTree function input targets"
```

## Task 3: Rich Binding Path Segment Generation

**Files:**
- Create: `Source/AssetFactory/Private/Test/StateTreeRichBindingTestTypes.h`
- Create: `Source/AssetFactory/Private/Test/StateTreeRichBindingTestTypes.cpp`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingResolver.cpp`

- [ ] **Step 1: Add an AssetFactory-only StateTree property function with an instanced struct input**

Create `Source/AssetFactory/Private/Test/StateTreeRichBindingTestTypes.h`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreePropertyFunctionBase.h"
#include "StructUtils/InstancedStruct.h"
#include "StateTreeRichBindingTestTypes.generated.h"

struct FStateTreeExecutionContext;

USTRUCT()
struct FAFStateTreeRichBindingPayload
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	float Value = 0.f;
};

USTRUCT()
struct FAFStateTreeRichBindingInputPropertyFunctionInstanceData
{
	GENERATED_BODY()

	FAFStateTreeRichBindingInputPropertyFunctionInstanceData()
	{
		DynamicInput.InitializeAs(FAFStateTreeRichBindingPayload::StaticStruct());
	}

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (BaseStruct = "/Script/AssetFactory.AFStateTreeRichBindingPayload"))
	FInstancedStruct DynamicInput;

	UPROPERTY(EditAnywhere, Category = Output)
	float Result = 0.f;
};

USTRUCT(meta = (DisplayName = "AF Rich Binding Input", Category = "AssetFactory|Test"))
struct FAFStateTreeRichBindingInputPropertyFunction : public FStateTreePropertyFunctionBase
{
	GENERATED_BODY()

	using FInstanceDataType = FAFStateTreeRichBindingInputPropertyFunctionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual void Execute(FStateTreeExecutionContext& Context) const override;
};
```

Create `Source/AssetFactory/Private/Test/StateTreeRichBindingTestTypes.cpp`:

```cpp
// Copyright ProjectRPG. All Rights Reserved.

#include "Test/StateTreeRichBindingTestTypes.h"

#include "StateTreeExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeRichBindingTestTypes)

void FAFStateTreeRichBindingInputPropertyFunction::Execute(FStateTreeExecutionContext& Context) const
{
	FAFStateTreeRichBindingInputPropertyFunctionInstanceData& InstanceData =
		Context.GetInstanceData<FAFStateTreeRichBindingInputPropertyFunctionInstanceData>(*this);

	if (const FAFStateTreeRichBindingPayload* Payload = InstanceData.DynamicInput.GetPtr<FAFStateTreeRichBindingPayload>())
	{
		InstanceData.Result = Payload->Value;
		return;
	}

	InstanceData.Result = 0.f;
}
```

This test type gives the smoke fixture a real `FInstancedStruct` segment (`DynamicInput`) followed by an inner property (`Value`). `FPropertyBindingPath::UpdateSegments` preserves `instanceStruct/access` for that path shape.

- [ ] **Step 2: Add rich path metadata helpers**

In the anonymous namespace of `StateTreeBindingResolver.cpp`, add:

```cpp
	UStruct* ResolveBindingPathInstanceStruct(const FString& InstanceStructPath)
	{
		if (InstanceStructPath.IsEmpty())
		{
			return nullptr;
		}

		if (UStruct* Struct = FindObject<UStruct>(nullptr, *InstanceStructPath))
		{
			return Struct;
		}
		return Cast<UStruct>(StaticLoadObject(UStruct::StaticClass(), nullptr, *InstanceStructPath));
	}

	bool TryParseBindingPathAccess(const FString& Access, EPropertyBindingPropertyAccessType& OutAccess)
	{
		OutAccess = EPropertyBindingPropertyAccessType::Unset;
		if (Access.IsEmpty())
		{
			return true;
		}

		if (Access.Equals(TEXT("Unset"), ESearchCase::IgnoreCase))
		{
			OutAccess = EPropertyBindingPropertyAccessType::Unset;
			return true;
		}
		if (Access.Equals(TEXT("StructInstance"), ESearchCase::IgnoreCase))
		{
			OutAccess = EPropertyBindingPropertyAccessType::StructInstance;
			return true;
		}
		if (Access.Equals(TEXT("ObjectInstance"), ESearchCase::IgnoreCase))
		{
			OutAccess = EPropertyBindingPropertyAccessType::ObjectInstance;
			return true;
		}

		if (const UEnum* AccessEnum = StaticEnum<EPropertyBindingPropertyAccessType>())
		{
			const int64 Value = AccessEnum->GetValueByNameString(Access);
			if (Value != INDEX_NONE)
			{
				OutAccess = static_cast<EPropertyBindingPropertyAccessType>(Value);
				return true;
			}
		}
		return false;
	}
```

- [ ] **Step 3: Replace the generation rejection in `MakeBindingPathSegments`**

Replace:

```cpp
		if (!Spec.InstanceStruct.IsEmpty() || !Spec.Access.IsEmpty())
		{
			OutError = FString::Printf(TEXT("StateTree binding path segment '%s' instanceStruct/access is not supported for generation yet"), *Spec.Name);
			return {};
		}

		FPropertyBindingPathSegment Segment(FName(*Spec.Name), Spec.ArrayIndex);
```

with:

```cpp
		EPropertyBindingPropertyAccessType AccessType = EPropertyBindingPropertyAccessType::Unset;
		if (!TryParseBindingPathAccess(Spec.Access, AccessType))
		{
			OutError = FString::Printf(TEXT("StateTree binding path segment '%s' has unknown access '%s'"), *Spec.Name, *Spec.Access);
			return {};
		}

		UStruct* InstanceStruct = nullptr;
		if (!Spec.InstanceStruct.IsEmpty())
		{
			InstanceStruct = ResolveBindingPathInstanceStruct(Spec.InstanceStruct);
			if (!InstanceStruct)
			{
				OutError = FString::Printf(TEXT("StateTree binding path segment '%s' has unknown instanceStruct '%s'"), *Spec.Name, *Spec.InstanceStruct);
				return {};
			}
			if (AccessType == EPropertyBindingPropertyAccessType::Unset)
			{
				AccessType = EPropertyBindingPropertyAccessType::StructInstance;
			}
		}
		else if (AccessType != EPropertyBindingPropertyAccessType::Unset)
		{
			OutError = FString::Printf(TEXT("StateTree binding path segment '%s' access requires instanceStruct"), *Spec.Name);
			return {};
		}

		FPropertyBindingPathSegment Segment(FName(*Spec.Name), Spec.ArrayIndex);
		if (InstanceStruct)
		{
			Segment.SetInstanceStruct(InstanceStruct, AccessType);
		}
```

- [ ] **Step 4: Run a targeted negative parser/generation smoke through MCP only after compile**

After Task 2 compile succeeds, keep this command ready for the real Editor smoke task:

```bash
UE_API_BASE=http://127.0.0.1:8559 npm --prefix MCP run smoke:statetree-roundtrip
```

Expected after this task and before extraction changes: generation should accept the new positive rich input fixture and reject the bad metadata fixtures with expected errors. Round-trip may still fail because extraction still emits old input object maps.

- [ ] **Step 5: Commit Task 3**

Run:

```bash
git add Source/AssetFactory/Private/Test/StateTreeRichBindingTestTypes.h Source/AssetFactory/Private/Test/StateTreeRichBindingTestTypes.cpp Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingResolver.cpp
git commit -m "feat: author StateTree rich binding path segments"
```

## Task 4: Canonical Function Extraction And Diagnostics

**Files:**
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingExtract.h`
- Modify: `Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingExtract.cpp`
- Modify: `Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp`

- [ ] **Step 1: Replace extraction return type**

In `StateTreeBindingExtract.h`, replace the function declaration with:

```cpp
namespace UE::AssetFactory::StateTree
{
	struct FAFStateTreeBindingDiagnostic
	{
		FString Code;
		FString Severity;
		FString Path;
		FString BindingTarget;
		FString Message;
	};

	struct FAFStateTreeBindingExtractionResult
	{
		TArray<TSharedPtr<FJsonValue>> Bindings;
		TArray<FAFStateTreeBindingDiagnostic> Diagnostics;
	};

	FAFStateTreeBindingExtractionResult ExtractPropertyBindings(const UStateTreeEditorData* EditorData);
}
```

- [ ] **Step 2: Add diagnostics storage and helpers in extraction context**

In `StateTreeBindingExtract.cpp`, add the diagnostics array to `FBindingExtractContext`:

```cpp
		TArray<UE::AssetFactory::StateTree::FAFStateTreeBindingDiagnostic> Diagnostics;
```

Change `BuildExtractContext` to return a mutable context as it already does. Add these helpers in the anonymous namespace:

```cpp
	void AddBindingDiagnostic(
		FBindingExtractContext& Context,
		const FString& Code,
		const FString& Path,
		const FString& BindingTarget,
		const FString& Message)
	{
		UE::AssetFactory::StateTree::FAFStateTreeBindingDiagnostic Diagnostic;
		Diagnostic.Code = Code;
		Diagnostic.Severity = TEXT("warning");
		Diagnostic.Path = Path;
		Diagnostic.BindingTarget = BindingTarget;
		Diagnostic.Message = Message;
		Context.Diagnostics.Add(MoveTemp(Diagnostic));
	}

	TSharedPtr<FJsonObject> BindingDiagnosticToJson(const UE::AssetFactory::StateTree::FAFStateTreeBindingDiagnostic& Diagnostic)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("code"), Diagnostic.Code);
		Json->SetStringField(TEXT("severity"), Diagnostic.Severity);
		Json->SetStringField(TEXT("path"), Diagnostic.Path);
		Json->SetStringField(TEXT("bindingTarget"), Diagnostic.BindingTarget);
		Json->SetStringField(TEXT("message"), Diagnostic.Message);
		return Json;
	}
```

- [ ] **Step 3: Emit `access` during path segment extraction**

In `ExtractPathSegment`, after writing `instanceStruct`, add:

```cpp
		const EPropertyBindingPropertyAccessType AccessType = Segment.GetInstancedStructAccessType();
		if (AccessType != EPropertyBindingPropertyAccessType::Unset)
		{
			if (const UEnum* AccessEnum = StaticEnum<EPropertyBindingPropertyAccessType>())
			{
				SegmentJson->SetStringField(TEXT("access"), AccessEnum->GetNameStringByValue(static_cast<int64>(AccessType)));
			}
		}
```

- [ ] **Step 4: Replace single-name input extraction with target arrays**

Delete `ExtractFunctionInputName`. Add:

```cpp
	TArray<TSharedPtr<FJsonValue>> ExtractFunctionInputTarget(const FPropertyBindingPath& InputPath)
	{
		return ExtractPathSegments(InputPath);
	}
```

- [ ] **Step 5: Make function extraction mutable and diagnostic-aware**

Change these signatures:

```cpp
	TSharedPtr<FJsonObject> ExtractFunctionSpec(
		FBindingExtractContext& Context,
		const FPropertyBindingBinding& Binding,
		TSet<FGuid>& VisitedFunctionIds,
		const FString& JsonPath);

	TSharedPtr<FJsonObject> ExtractFunctionInputSpec(
		FBindingExtractContext& Context,
		const FPropertyBindingBinding& Binding,
		TSet<FGuid>& VisitedFunctionIds,
		const FString& JsonPath)
```

Inside `ExtractFunctionInputSpec`, call nested extraction with:

```cpp
			InputJson->SetObjectField(TEXT("function"), ExtractFunctionSpec(Context, Binding, VisitedFunctionIds, JsonPath + TEXT(".function")));
```

Keep source extraction unchanged:

```cpp
			InputJson->SetObjectField(TEXT("source"), ExtractEndpoint(Context, Binding.GetSourcePath()));
```

- [ ] **Step 6: Emit function inputs as arrays and diagnose cycles**

Inside `ExtractFunctionSpec`, replace `TSharedPtr<FJsonObject> InputsJson = MakeShared<FJsonObject>();` with:

```cpp
		TArray<TSharedPtr<FJsonValue>> InputValues;
```

Replace the recursion guard body with:

```cpp
		if (VisitedFunctionIds.Contains(FunctionNodeId))
		{
			AddBindingDiagnostic(
				Context,
				TEXT("StateTree.Binding.FunctionCycle"),
				JsonPath,
				MakeBindingTargetKey(Binding.GetTargetPath()),
				TEXT("Skipped nested StateTree property function input because the graph references an already visited function node."));
			FunctionJson->SetArrayField(TEXT("inputs"), InputValues);
			return FunctionJson;
		}

		VisitedFunctionIds.Add(FunctionNodeId);

		TArray<const FPropertyBindingBinding*> InputBindings;
		Context.BindingsByTargetStructId.MultiFind(FunctionNodeId, InputBindings);
		InputBindings.Sort([](const FPropertyBindingBinding& A, const FPropertyBindingBinding& B)
		{
			return A.GetTargetPath().ToString() < B.GetTargetPath().ToString();
		});

		for (int32 InputIndex = 0; InputIndex < InputBindings.Num(); ++InputIndex)
		{
			const FPropertyBindingBinding* InputBinding = InputBindings[InputIndex];
			if (!InputBinding)
			{
				continue;
			}

			TSharedPtr<FJsonObject> InputJson = ExtractFunctionInputSpec(
				Context,
				*InputBinding,
				VisitedFunctionIds,
				FString::Printf(TEXT("%s.inputs[%d]"), *JsonPath, InputValues.Num()));
			InputJson->SetArrayField(TEXT("target"), ExtractFunctionInputTarget(InputBinding->GetTargetPath()));
			InputValues.Add(MakeShared<FJsonValueObject>(InputJson));
		}

		VisitedFunctionIds.Remove(FunctionNodeId);
		FunctionJson->SetArrayField(TEXT("inputs"), InputValues);
		return FunctionJson;
```

This removes the previous `NumSegments() != 1` skip. Multi-segment input targets now round-trip as JSON path arrays.

- [ ] **Step 7: Update top-level binding extraction calls**

In `ExtractBinding`, change the signature to:

```cpp
	TSharedPtr<FJsonObject> ExtractBinding(FBindingExtractContext& Context, const FPropertyBindingBinding& Binding, int32 BindingIndex)
```

Call property function extraction with:

```cpp
			TSet<FGuid> VisitedFunctionIds;
			BindingJson->SetObjectField(
				TEXT("function"),
				ExtractFunctionSpec(Context, Binding, VisitedFunctionIds, FString::Printf(TEXT("$.bindings[%d].function"), BindingIndex)));
```

- [ ] **Step 8: Return bindings and diagnostics**

Replace `ExtractPropertyBindings` implementation header:

```cpp
UE::AssetFactory::StateTree::FAFStateTreeBindingExtractionResult UE::AssetFactory::StateTree::ExtractPropertyBindings(const UStateTreeEditorData* EditorData)
{
	FAFStateTreeBindingExtractionResult Result;
	if (!EditorData)
	{
		return Result;
	}
```

Replace every `return Result;` that previously returned an array with the struct. When appending bindings, use:

```cpp
	for (int32 BindingIndex = 0; BindingIndex < TopLevelBindings.Num(); ++BindingIndex)
	{
		if (const FPropertyBindingBinding* Binding = TopLevelBindings[BindingIndex])
		{
			if (TSharedPtr<FJsonObject> BindingJson = ExtractBinding(ContextWithBindings, *Binding, Result.Bindings.Num()))
			{
				Result.Bindings.Add(MakeShared<FJsonValueObject>(BindingJson));
			}
		}
	}

	Result.Diagnostics = MoveTemp(ContextWithBindings.Diagnostics);
	return Result;
```

- [ ] **Step 9: Attach diagnostics in the generator extraction output**

In `StateTreeGenerator.cpp`, replace:

```cpp
		TArray<TSharedPtr<FJsonValue>> Bindings = UE::AssetFactory::StateTree::ExtractPropertyBindings(EditorData);
		if (!Bindings.IsEmpty())
		{
			OutJson->SetArrayField(TEXT("bindings"), Bindings);
		}
```

with:

```cpp
		UE::AssetFactory::StateTree::FAFStateTreeBindingExtractionResult BindingExtraction = UE::AssetFactory::StateTree::ExtractPropertyBindings(EditorData);
		if (!BindingExtraction.Bindings.IsEmpty())
		{
			OutJson->SetArrayField(TEXT("bindings"), BindingExtraction.Bindings);
		}
		if (!BindingExtraction.Diagnostics.IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> BindingDiagnostics;
			for (const UE::AssetFactory::StateTree::FAFStateTreeBindingDiagnostic& Diagnostic : BindingExtraction.Diagnostics)
			{
				TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
				Json->SetStringField(TEXT("code"), Diagnostic.Code);
				Json->SetStringField(TEXT("severity"), Diagnostic.Severity);
				Json->SetStringField(TEXT("path"), Diagnostic.Path);
				Json->SetStringField(TEXT("bindingTarget"), Diagnostic.BindingTarget);
				Json->SetStringField(TEXT("message"), Diagnostic.Message);
				BindingDiagnostics.Add(MakeShared<FJsonValueObject>(Json));
			}

			TSharedPtr<FJsonObject> DiagnosticsJson = MakeShared<FJsonObject>();
			DiagnosticsJson->SetArrayField(TEXT("bindings"), BindingDiagnostics);
			OutJson->SetObjectField(TEXT("diagnostics"), DiagnosticsJson);
		}
```

- [ ] **Step 10: Run compile**

Run:

```bash
~/UnrealEngine/Engine/Build/BatchFiles/Mac/Build.sh ProjectRPGEditor Mac Development -Project="/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject" -WaitMutex
```

Expected:

```text
BUILD SUCCESSFUL
```

- [ ] **Step 11: Commit Task 4**

Run:

```bash
git add Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingExtract.h Source/AssetFactory/Private/Generators/StateTree/StateTreeBindingExtract.cpp Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp
git commit -m "feat: extract StateTree function input paths with diagnostics"
```

## Task 5: Round-trip Verifiers And MCP Smoke Script

**Files:**
- Modify: `docs/superpowers/verification/statetree_roundtrip_check.py`
- Modify: `docs/superpowers/verification/statetree_binding_roundtrip_check.py`
- Modify: `MCP/scripts/statetree_roundtrip_mcp_smoke.mjs`

- [ ] **Step 1: Preserve rich path metadata in round-trip canonicalization**

In `statetree_roundtrip_check.py`, replace `_is_segment_dict` with:

```python
def _is_segment_dict(value):
	return (
		isinstance(value, dict)
		and "name" in value
		and set(value.keys()).issubset({"name", "guid", "arrayIndex", "instanceStruct", "access"})
	)
```

Replace `_normalize_path_segment` with:

```python
def _normalize_path_segment(segment):
	if isinstance(segment, dict) and "name" in segment:
		normalized = {"name": segment["name"]}
		for key in ("guid", "arrayIndex", "instanceStruct", "access"):
			if key in segment:
				normalized[key] = segment[key]
		return normalized
	if isinstance(segment, str):
		return {"name": segment}
	return _canonicalize(segment)
```

- [ ] **Step 2: Ignore extraction diagnostics during semantic round-trip compare**

Add `"diagnostics"` and `"Diagnostics"` to `IGNORED_KEYS`:

```python
IGNORED_KEYS = {
	"Compiled",
	"compiled",
	"diagnostics",
	"Diagnostics",
	"lastCompiledEditorDataHash",
}
```

- [ ] **Step 3: Canonicalize legacy and canonical function inputs to the same shape**

Add this helper above `_canonicalize`:

```python
def _normalize_function_inputs(value):
	if isinstance(value, dict):
		items = []
		for key, child in value.items():
			if not isinstance(child, dict):
				items.append(_canonicalize(child, "inputs"))
				continue
			entry = {"target": [{"name": key}]}
			for child_key, child_value in child.items():
				entry[child_key] = _canonicalize(child_value, child_key)
			items.append(entry)
		return sorted(items, key=_binding_sort_key)
	if isinstance(value, list):
		return sorted([_canonicalize(child, "inputs") for child in value], key=_binding_sort_key)
	return _canonicalize(value, "inputs")
```

In `_canonicalize`, inside the dictionary loop before the path/output check, insert:

```python
			if key == "inputs" and parent_key == "function":
				result[key] = _normalize_function_inputs(raw_child)
				continue
```

- [ ] **Step 4: Compare new metadata fields in segment mismatches**

In `_find_segment_mismatch`, add checks for `instanceStruct` and `access` after `guid`:

```python
	for key in ("instanceStruct", "access"):
		if (key in left) != (key in right):
			return path + [f".{key}"], left.get(key, "<missing>"), right.get(key, "<missing>")
		if left.get(key) != right.get(key):
			return path + [f".{key}"], left.get(key), right.get(key)
```

- [ ] **Step 5: Keep binding-only verifier aligned**

Replace `docs/superpowers/verification/statetree_binding_roundtrip_check.py` with:

```python
#!/usr/bin/env python3

import json
import sys


def _load_bindings(path):
	with open(path, "r", encoding="utf-8") as handle:
		config = json.load(handle)
	bindings = config.get("bindings")
	if bindings is None:
		bindings = config.get("Bindings")
	if bindings is None:
		bindings = []
	if not isinstance(bindings, list):
		raise ValueError(f"{path}: bindings must be an array")
	return _normalize(bindings)


def _normalize_path_segment(segment):
	if isinstance(segment, str):
		return {"name": segment}
	if isinstance(segment, dict) and "name" in segment:
		return {key: segment[key] for key in ("name", "guid", "arrayIndex", "instanceStruct", "access") if key in segment}
	return _normalize(segment)


def _normalize_function_inputs(value):
	if isinstance(value, dict):
		items = []
		for key, child in value.items():
			entry = {"target": [{"name": key}]}
			if isinstance(child, dict):
				for child_key, child_value in child.items():
					entry[child_key] = _normalize(child_value)
			else:
				entry["value"] = _normalize(child)
			items.append(entry)
		return sorted(items, key=_stable_json)
	if isinstance(value, list):
		return sorted([_normalize(child) for child in value], key=_stable_json)
	return _normalize(value)


def _normalize(value, parent_key=None):
	if isinstance(value, dict):
		result = {}
		for key, child in value.items():
			if key == "id":
				continue
			if key in ("diagnostics", "Diagnostics"):
				continue
			if key in ("path", "output") and isinstance(child, list):
				result[key] = [_normalize_path_segment(segment) for segment in child]
				continue
			if key == "inputs" and parent_key == "function":
				result[key] = _normalize_function_inputs(child)
				continue
			result[key] = _normalize(child, key)
		return result
	if isinstance(value, list):
		return sorted([_normalize(child, parent_key) for child in value], key=_stable_json)
	return value


def _stable_json(value):
	return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def main(argv):
	if len(argv) != 3:
		print("usage: statetree_binding_roundtrip_check.py left.json right.json", file=sys.stderr)
		return 2
	left = _load_bindings(argv[1])
	right = _load_bindings(argv[2])
	if left != right:
		print("StateTree binding round-trip mismatch", file=sys.stderr)
		print("left:", json.dumps(left, ensure_ascii=False, indent=2, sort_keys=True), file=sys.stderr)
		print("right:", json.dumps(right, ensure_ascii=False, indent=2, sort_keys=True), file=sys.stderr)
		return 1
	print(f"StateTree binding round-trip stable: {len(left)} bindings")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
```

- [ ] **Step 6: Assert normal smoke emits no diagnostics**

In `MCP/scripts/statetree_roundtrip_mcp_smoke.mjs`, add this helper:

```js
function assertNoUnexpectedDiagnostics(config, phase) {
	if (config && config.diagnostics && Array.isArray(config.diagnostics.bindings) && config.diagnostics.bindings.length > 0) {
		throw new Error(`${phase} extracted diagnostics for ${config.Name}: ${JSON.stringify(config.diagnostics)}`);
	}
}
```

In `writeExtractedConfigs`, after `config.Action = "CreateOrUpdate";`, add:

```js
		assertNoUnexpectedDiagnostics(config, phase);
```

- [ ] **Step 7: Run verifier tests**

Run:

```bash
npm --prefix MCP test
python3 docs/superpowers/verification/statetree_roundtrip_check.py TestData/ST_Bindings_Function.json TestData/ST_Bindings_Function.json --fixture ST_Bindings_Function
python3 docs/superpowers/verification/statetree_binding_roundtrip_check.py TestData/ST_Bindings_Function.json TestData/ST_Bindings_Function_LegacyInputsObject.json
```

Expected:

```text
# pass ...
StateTree round-trip stable: ST_Bindings_Function ...
StateTree binding round-trip stable: 1 bindings
```

- [ ] **Step 8: Commit Task 5**

Run:

```bash
git add docs/superpowers/verification/statetree_roundtrip_check.py docs/superpowers/verification/statetree_binding_roundtrip_check.py MCP/scripts/statetree_roundtrip_mcp_smoke.mjs
git commit -m "test: normalize StateTree rich binding round trips"
```

## Task 6: Schema Docs

**Files:**
- Modify: `MCP/schemas/StateTree.md`
- Modify: `docs/superpowers/specs/2026-04-29-statetree-binding-rich-paths-function-diagnostics-design.md`

- [ ] **Step 1: Update path segment docs**

In `MCP/schemas/StateTree.md`, replace the paragraph that says generation rejects `instanceStruct/access` with:

```markdown
显式 object shape 在 generator input 中支持 `name`、`arrayIndex`、`guid`、`instanceStruct` 和 `access`。`instanceStruct` 使用完整 script struct/object path，例如 `/Script/StateTreeModule.StateTreeFloatCombinaisonPropertyFunctionInstanceData`。`access` 支持 `StructInstance`、`ObjectInstance` 和 `Unset`，省略时默认为 `Unset`；当提供 `instanceStruct` 且省略 `access` 时，generator 使用 `StructInstance`。StateTree bindings 不支持 `"Root/Idle.delay-task.Duration"` 这类 string DSL；请使用显式 JSON endpoint objects 和 path arrays，让 generator 能校验每个 owner 和 segment。
```

- [ ] **Step 2: Update property function docs**

Replace the property function paragraph with:

```markdown
Property function bindings 使用包含 `type`、`output`、`inputs` 的 `function` object。function `type` 必须解析为受支持的 StateTree property function。`output` 是 function result 上的 binding path array。`inputs` 的 canonical shape 是 array；每个 entry 必须包含 `target` path array，并且只能包含 `source` 或 nested `function` 二者之一。旧版 object-shaped `inputs` 仍作为 compatibility input 接受，但 extraction 和新 fixtures 只输出 canonical array。

```json
"function": {
	"type": "/Script/StateTreeModule.StateTreeAddFloatPropertyFunction",
	"output": ["Result"],
	"inputs": [
		{
			"target": ["Left"],
			"source": { "kind": "rootParameter", "path": ["BaseDelay"] }
		},
		{
			"target": ["Right"],
			"source": { "kind": "rootParameter", "path": ["BonusDelay"] }
		}
	]
}
```
```

- [ ] **Step 3: Document extraction diagnostics**

Add this paragraph under extraction docs:

```markdown
Extraction may include read-only `diagnostics.bindings` entries when an editor-authored binding graph cannot be represented without losing information. Each diagnostic has `code`, `severity`, `path`, `bindingTarget`, and `message`. Diagnostics are not authoring input; round-trip verification ignores them by default and dedicated diagnostic checks assert the stable diagnostic codes.
```

- [ ] **Step 4: Update design doc status**

Append to `docs/superpowers/specs/2026-04-29-statetree-binding-rich-paths-function-diagnostics-design.md`:

```markdown
## Implementation Status

- Planned branch: `feature/statetree-rich-binding-paths`
- Canonical property function input shape: array entries with explicit `target`
- Legacy object-shaped input maps: generation compatibility only
- Rich path metadata: authorable for `instanceStruct` and `access`
- Extraction diagnostics: read-only `diagnostics.bindings`
```

- [ ] **Step 5: Run docs/static checks**

Run:

```bash
npm --prefix MCP test
```

Expected:

```text
# pass ...
```

- [ ] **Step 6: Commit Task 6**

Run:

```bash
git add MCP/schemas/StateTree.md docs/superpowers/specs/2026-04-29-statetree-binding-rich-paths-function-diagnostics-design.md
git commit -m "docs: document StateTree rich binding paths"
```

## Task 7: Real Editor/MCP Smoke And Final Commit

**Files:**
- Runtime artifacts only under `/tmp/assetfactory-statetree-roundtrip`
- Existing local Editor/MCP environment at `localhost:8559`

- [ ] **Step 1: Build MCP**

Run:

```bash
npm --prefix MCP run build
```

Expected:

```text
> @anthropic/ue-copilot-mcp@1.1.0 build
> tsc
```

- [ ] **Step 2: Compile project normally if C++ changes are not hot-loaded**

Run:

```bash
~/UnrealEngine/Engine/Build/BatchFiles/Mac/Build.sh ProjectRPGEditor Mac Development -Project="/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject" -WaitMutex
```

Expected:

```text
BUILD SUCCESSFUL
```

Do not run `RunUAT BuildPlugin`. If this command starts an unrelated cold Engine rebuild, stop it and launch GUI Editor from the already-built ProjectRPG editor path, then use the Editor compile/hot-reload button for plugin code if needed.

- [ ] **Step 3: Start or reuse the real GUI Editor with AssetFactory MCP**

Use the GUI Editor, not `UnrealEditor-Cmd`, `-NullRHI`, or headless:

```bash
open /Users/pengao/UnrealEngine/Engine/Binaries/Mac/UnrealEditor.app --args /Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject
```

Confirm MCP health:

```bash
curl -s http://127.0.0.1:8559/health
```

Expected:

```json
{"success":true}
```

- [ ] **Step 4: Run full StateTree round-trip smoke**

Run:

```bash
UE_API_BASE=http://127.0.0.1:8559 npm --prefix MCP run smoke:statetree-roundtrip
```

Expected summary:

```text
PREFLIGHT_BUILD skipped=true
HEALTH success=true service=AssetFactory port=8559
CLEANED_STATE_TREE_SMOKE_ROOT /Game/AFSmoke
GENERATE_INITIAL success=true succeeded=19 failed=0
EXTRACT1 success=true succeeded=19 failed=0
REGENERATE success=true succeeded=19 failed=0
EXTRACT2 success=true succeeded=19 failed=0
OPEN_ASSET success=true path=/Game/AFSmoke/ST_RoundTrip_Comprehensive
SUMMARY positive=19/19 roundTrip=19/19 negative=24/24 finalOpen=/Game/AFSmoke/ST_RoundTrip_Comprehensive
```

If the fixture counts differ because this task adds or removes a manifest entry during implementation, update the expected line to the exact manifest count before committing the smoke evidence.

- [ ] **Step 5: Verify extracted rich paths and canonical function inputs**

Run:

```bash
python3 - <<'PY'
import json
from pathlib import Path

out = Path("/tmp/assetfactory-statetree-roundtrip")
rich = json.loads((out / "ST_Bindings_Function_RichInputTargets.extract2.json").read_text())
functions = []

def walk(value):
	if isinstance(value, dict):
		if "function" in value:
			functions.append(value["function"])
		for child in value.values():
			walk(child)
	elif isinstance(value, list):
		for child in value:
			walk(child)

walk(rich)
assert functions, "no function bindings found"
for function in functions:
	assert isinstance(function.get("inputs"), list), "function.inputs is not canonical array"
	for item in function["inputs"]:
		assert isinstance(item.get("target"), list) and item["target"], "input target missing"
serialized = json.dumps(rich, sort_keys=True)
assert "instanceStruct" in serialized, "instanceStruct did not round-trip"
assert "access" in serialized, "access did not round-trip"
assert "diagnostics" not in rich, "normal smoke fixture emitted diagnostics"
print("RICH_BINDING_SMOKE canonicalInputs=true richPathMetadata=true diagnostics=false")
PY
```

Expected:

```text
RICH_BINDING_SMOKE canonicalInputs=true richPathMetadata=true diagnostics=false
```

- [ ] **Step 6: Verify negative fixture errors**

Run:

```bash
cat /tmp/assetfactory-statetree-roundtrip/summary.json
```

Expected JSON contains:

```json
{
	"negative": {
		"passed": 24,
		"failed": 0
	}
}
```

And the smoke output includes each expected error substring:

```text
must define required field 'target'
target must contain at least one path segment
unknown access
unknown instanceStruct
```

- [ ] **Step 7: Open the visual acceptance asset**

If smoke did not already bring it forward, open:

```bash
curl -s -X POST http://127.0.0.1:8559/open_asset -H 'Content-Type: application/json' -d '{"assetPath":"/Game/AFSmoke/ST_RoundTrip_Comprehensive"}'
```

Expected: GUI Editor shows `ST_RoundTrip_Comprehensive` and the user can visually verify the StateTree graph.

- [ ] **Step 8: Check git status**

Run:

```bash
git status --short
git -C /Volumes/Mac/GameDev/ProjectRPG status --short
```

Expected AssetFactory worktree shows only intended source/docs/test changes before final commit. ProjectRPG root may still show `?? Content/AFSmoke/`; do not delete or stage it unless the user explicitly confirms cleanup.

- [ ] **Step 9: Commit final smoke notes if any files changed after Task 6**

Run:

```bash
git add MCP/scripts/statetree_roundtrip_mcp_smoke.mjs docs/superpowers/verification/statetree_roundtrip_check.py docs/superpowers/verification/statetree_binding_roundtrip_check.py MCP/schemas/StateTree.md TestData/StateTreeSmokeManifest.json TestData/ST_Bindings_Function_RichInputTargets.json TestData/ST_Bindings_Function_LegacyInputsObject.json
git commit -m "test: smoke StateTree rich binding paths"
```

If there are no unstaged changes, skip this commit and record that all smoke changes were already committed in previous tasks.

## Task 8: Review, Merge, Push

**Files:**
- Git metadata only

- [ ] **Step 1: Request focused code review**

Use `superpowers:requesting-code-review` or a review subagent for the final diff. Ask for:

```text
Review AssetFactory StateTree rich binding path/function diagnostic changes. Focus on parser compatibility, function input target path correctness, extraction diagnostics not becoming authoring input, round-trip verifier normalization, and smoke manifest coverage.
```

- [ ] **Step 2: Fix Critical/Important findings**

For each Critical or Important finding, make the smallest code/test change, rerun:

```bash
npm --prefix MCP test
~/UnrealEngine/Engine/Build/BatchFiles/Mac/Build.sh ProjectRPGEditor Mac Development -Project="/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject" -WaitMutex
UE_API_BASE=http://127.0.0.1:8559 npm --prefix MCP run smoke:statetree-roundtrip
```

Expected: all pass with the same smoke summary counts established in Task 7.

- [ ] **Step 3: Record Minor/Nit findings**

If the reviewer returns Minor/Nit items that are not needed for this spec, append them to:

```text
docs/superpowers/notes/2026-04-29-statetree-rich-binding-paths-deferred-polish.md
```

Use this format:

```markdown
# StateTree Rich Binding Paths Deferred Polish

- [ ] Reviewer finding: <concise item>
  - Context: <file/function>
  - Reason deferred: Minor/Nit; not required for current smoke acceptance.
```

Commit that note if it is created.

- [ ] **Step 4: Merge AssetFactory branch to master**

Run:

```bash
git status --short
git checkout master
git pull --ff-only
git merge --ff-only feature/statetree-rich-binding-paths
```

Expected:

```text
Updating ...
Fast-forward
```

If fast-forward is not possible, stop and inspect with:

```bash
git log --oneline --decorate --graph --all -n 30
```

- [ ] **Step 5: Push AssetFactory master**

Run:

```bash
git push origin master
```

Expected:

```text
To ...
   <old>..<new>  master -> master
```

- [ ] **Step 6: Update ProjectRPG AGENTS and submodule pointer**

From `/Volumes/Mac/GameDev/ProjectRPG`:

Replace the current `AssetFactory StateTree final verification deferred limits` bullets in `AGENTS.md` with:

```markdown
- AssetFactory StateTree rich binding paths/function diagnostics (2026-04-29): resolved in AssetFactory branch `feature/statetree-rich-binding-paths`. `instanceStruct` / `access` path metadata is authorable, property function `inputs` canonical shape is an array with explicit `target` paths, extraction emits read-only diagnostics for unsupported/cyclic function graphs, and round-trip tools ignore diagnostics by default.
```

If final smoke proves one item has an engine API limitation, replace only that clause with the exact observed limitation and the fixture/test that proves it.

```bash
git status --short
git add Plugins/AssetFactory AGENTS.md
git commit -m "chore: update AssetFactory rich binding paths"
git push origin master
```

If `Plugins/AssetFactory` is not a submodule pointer in this checkout, commit only `AGENTS.md` if it changed.

## Self-Review Checklist

- Spec coverage:
	- Rich path generation is covered by Task 3 and smoke fixture `ST_Bindings_Function_RichInputTargets`.
	- Canonical multi-segment function input target paths are covered by Tasks 1, 2, 4, and the verifier updates in Task 5.
	- Legacy object input compatibility is covered by `ST_Bindings_Function_LegacyInputsObject`.
	- Extraction diagnostics are covered by Task 4 and documented in Task 6.
	- Normal smoke remains diagnostics-free via Task 5 script assertion.
	- AGENTS deferred note cleanup is covered by Task 8 in the ProjectRPG root repository.
- Placeholder scan:
	- No task contains deferred-work markers or open-ended validation language.
	- Conditional instructions are bounded to exact observed outcomes and commands.
- Type consistency:
	- `FAFStateTreeBindingFunctionInputSpec::TargetPath` is parsed in `StateTreeJsonTypes.h`, consumed in `StateTreeBindingBuilder.cpp`, and emitted by `StateTreeBindingExtract.cpp`.
	- `FAFStateTreeBindingExtractionResult` is declared in the header and consumed by `StateTreeGenerator.cpp`.
	- Diagnostics fields match static fixture and docs: `code`, `severity`, `path`, `bindingTarget`, `message`.
