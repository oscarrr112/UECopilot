import test from "node:test";
import assert from "node:assert/strict";
import { existsSync, readFileSync } from "fs";
import { dirname, join } from "path";
import { fileURLToPath } from "url";

const DIST_DIR = dirname(dirname(fileURLToPath(import.meta.url)));
const PROJECT_DIR = dirname(DIST_DIR);
const REPO_DIR = dirname(PROJECT_DIR);
const TEST_DATA_DIR = join(REPO_DIR, "TestData");

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
		const functionObject = asObject(object.function, `${path}.function`);
		result.push({ path: `${path}.function`, functionObject });
		result.push(...collectFunctionObjects(functionObject, `${path}.function`));
	}
	for (const [key, child] of Object.entries(object)) {
		if (key !== "function") {
			result.push(...collectFunctionObjects(child, `${path}.${key}`));
		}
	}
	return result;
}

function collectTargetSegmentObjects(fixtureName: string): JsonObject[] {
	const fixture = readFixture(fixtureName);
	const targetSegmentObjects: JsonObject[] = [];
	for (const entry of collectFunctionObjects(fixture)) {
		const inputs = asArray(entry.functionObject.inputs, `${fixtureName}${entry.path}.inputs`);
		for (const [inputIndex, inputValue] of inputs.entries()) {
			const input = asObject(inputValue, `${fixtureName}${entry.path}.inputs[${inputIndex}]`);
			assert.equal(Object.prototype.hasOwnProperty.call(input, "instanceStruct"), false, `${fixtureName}${entry.path}.inputs[${inputIndex}] should not define instanceStruct directly`);
			assert.equal(Object.prototype.hasOwnProperty.call(input, "access"), false, `${fixtureName}${entry.path}.inputs[${inputIndex}] should not define access directly`);
			const target = asArray(input.target, `${fixtureName}${entry.path}.inputs[${inputIndex}].target`);
			for (const [segmentIndex, segmentValue] of target.entries()) {
				if (segmentValue && typeof segmentValue === "object" && !Array.isArray(segmentValue)) {
					targetSegmentObjects.push(asObject(segmentValue, `${fixtureName}${entry.path}.inputs[${inputIndex}].target[${segmentIndex}]`));
				}
			}
		}
	}
	return targetSegmentObjects;
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

test("collectFunctionObjects includes nested input functions once", () => {
	const functions = collectFunctionObjects({
		bindings: [
			{
				function: {
					inputs: [
						{
							target: ["Left"],
							function: {
								inputs: [
									{
										target: ["Value"],
										source: { kind: "rootParameter", path: ["BaseDelay"] },
									},
								],
							},
						},
					],
				},
			},
		],
	});
	assert.deepEqual(functions.map((entry) => entry.path), [
		"$.bindings[0].function",
		"$.bindings[0].function.inputs[0].function",
	]);
});

test("rich binding path segment metadata appears only in canonical path arrays", () => {
	const targetSegmentObjects = collectTargetSegmentObjects("ST_Bindings_Function_RichInputTargets");
	const richSegments = targetSegmentObjects.filter((segment) => {
		const hasInstanceStruct = Object.prototype.hasOwnProperty.call(segment, "instanceStruct");
		const hasAccess = Object.prototype.hasOwnProperty.call(segment, "access");
		assert.equal(hasInstanceStruct, hasAccess, "rich target path segment metadata should define both instanceStruct and access");
		return hasInstanceStruct && hasAccess;
	});
	assert.ok(richSegments.length > 0, "rich target path metadata should appear in at least one function input target segment object");
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
