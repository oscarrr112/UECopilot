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

type FixtureKind = "positive" | "negative";

type FixtureEntry = {
	name: string;
	kind: FixtureKind;
	spec: string;
	description: string;
	dependsOn?: string[];
	roundTrip?: boolean;
	expectedError?: string;
	openOnSuccess?: boolean;
};

type Manifest = {
	assetRoot: string;
	finalOpenAsset: string;
	fixtures: FixtureEntry[];
};

function readManifest(): Manifest {
	const parsed = JSON.parse(readFileSync(MANIFEST_PATH, "utf8")) as Manifest;
	assert.equal(parsed.assetRoot, "/Game/AFSmoke");
	assert.equal(parsed.finalOpenAsset, "ST_RoundTrip_Comprehensive");
	assert.ok(Array.isArray(parsed.fixtures));
	return parsed;
}

function fixturePath(name: string): string {
	return join(TEST_DATA_DIR, `${name}.json`);
}

function assertUnique(values: string[], label: string): void {
	const seen = new Set<string>();
	for (const value of values) {
		assert.ok(!seen.has(value), `${label} contains duplicate '${value}'`);
		seen.add(value);
	}
}

function assertNoDependencyCycles(entries: FixtureEntry[]): void {
	const byName = new Map(entries.map((entry) => [entry.name, entry]));
	const visiting = new Set<string>();
	const visited = new Set<string>();

	function visit(name: string): void {
		if (visited.has(name)) {
			return;
		}
		assert.ok(!visiting.has(name), `manifest dependency cycle at '${name}'`);
		visiting.add(name);
		for (const dependency of byName.get(name)?.dependsOn ?? []) {
			assert.ok(byName.has(dependency), `manifest dependency '${dependency}' for '${name}' is missing`);
			visit(dependency);
		}
		visiting.delete(name);
		visited.add(name);
	}

	for (const entry of entries) {
		visit(entry.name);
	}
}

test("StateTree smoke manifest exists and references real StateTree fixtures", () => {
	assert.ok(existsSync(MANIFEST_PATH), "StateTreeSmokeManifest.json should exist");
	const manifest = readManifest();
	assertUnique(manifest.fixtures.map((entry) => entry.name), "fixtures");

	for (const entry of manifest.fixtures) {
		assert.match(entry.name, /^ST_/);
		assert.ok(entry.description.length > 0, `${entry.name} should describe coverage intent`);
		assert.ok(existsSync(fixturePath(entry.name)), `${entry.name}.json should exist`);
		const fixture = JSON.parse(readFileSync(fixturePath(entry.name), "utf8"));
		assert.equal(fixture.AssetType, "StateTree", `${entry.name} should be a StateTree fixture`);
	}
});

test("StateTree smoke manifest covers every final verification spec bucket", () => {
	const manifest = readManifest();
	const specs = new Map<string, FixtureEntry[]>();
	for (const entry of manifest.fixtures) {
		specs.set(entry.spec, [...(specs.get(entry.spec) ?? []), entry]);
	}

	for (const spec of ["core", "dynamic", "structure", "parameters", "bindings"]) {
		const entries = specs.get(spec) ?? [];
		assert.ok(entries.some((entry) => entry.kind === "positive"), `${spec} should have a positive fixture`);
		assert.ok(entries.some((entry) => entry.kind === "negative"), `${spec} should have a negative fixture`);
	}

	const roundTripEntries = specs.get("roundtrip") ?? [];
	assert.ok(roundTripEntries.some((entry) => entry.kind === "positive"), "roundtrip should have a positive fixture");
	assert.ok(manifest.fixtures.filter((entry) => entry.kind === "positive").length >= 17);
	assert.ok(manifest.fixtures.filter((entry) => entry.kind === "negative").length >= 20);
	assert.ok(manifest.fixtures.filter((entry) => entry.roundTrip === true).length >= 17);
});

test("StateTree smoke manifest has stable invalid expectations and dependency graph", () => {
	const manifest = readManifest();
	assertNoDependencyCycles(manifest.fixtures);

	for (const entry of manifest.fixtures) {
		if (entry.kind === "negative") {
			assert.ok(entry.expectedError && entry.expectedError.length >= 4, `${entry.name} should have expectedError`);
			assert.notEqual(entry.roundTrip, true, `${entry.name} should not round-trip`);
			assert.notEqual(entry.openOnSuccess, true, `${entry.name} should not be final-open asset`);
		}
		if (entry.kind === "positive") {
			assert.equal(entry.expectedError, undefined, `${entry.name} should not have expectedError`);
		}
	}
});

test("StateTree smoke manifest has exactly one final-open round-trip fixture", () => {
	const manifest = readManifest();
	const openFixtures = manifest.fixtures.filter((entry) => entry.openOnSuccess === true);
	assert.deepEqual(openFixtures.map((entry) => entry.name), ["ST_RoundTrip_Comprehensive"]);
	assert.equal(openFixtures[0].kind, "positive");
	assert.equal(openFixtures[0].roundTrip, true);
	assert.equal(manifest.finalOpenAsset, openFixtures[0].name);
});
