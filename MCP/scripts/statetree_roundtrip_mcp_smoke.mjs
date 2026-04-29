#!/usr/bin/env node

import { spawnSync } from "child_process";
import { mkdirSync, readFileSync, rmSync, writeFileSync } from "fs";
import { dirname, join, resolve } from "path";
import { fileURLToPath } from "url";

import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";

const scriptDir = dirname(fileURLToPath(import.meta.url));
const mcpDir = resolve(scriptDir, "..");
const repoDir = resolve(mcpDir, "..");
const testDataDir = join(repoDir, "TestData");
const manifestPath = join(testDataDir, "StateTreeSmokeManifest.json");
const checkerPath = join(repoDir, "docs", "superpowers", "verification", "statetree_roundtrip_check.py");
const outDir = process.env.STATETREE_ROUNDTRIP_OUT || "/tmp/assetfactory-statetree-roundtrip";
const ueApiBase = process.env.UE_API_BASE || "http://127.0.0.1:8559";
const preflightBuild =
	process.argv.includes("--preflight-build") || process.env.STATETREE_SMOKE_PREFLIGHT_BUILD === "1";

function readFixture(name) {
	return JSON.parse(readFileSync(join(testDataDir, `${name}.json`), "utf8"));
}

function readManifest() {
	const manifest = JSON.parse(readFileSync(manifestPath, "utf8"));
	assertCondition(manifest && Array.isArray(manifest.fixtures), "StateTreeSmokeManifest.json missing fixtures array");
	assertCondition(manifest.assetRoot === "/Game/AFSmoke", `unexpected assetRoot ${manifest.assetRoot}`);
	return manifest;
}

function fixtureEntries(manifest, kind) {
	return manifest.fixtures.filter((entry) => entry.kind === kind);
}

function topologicalFixtureOrder(entries) {
	const byName = new Map(entries.map((entry) => [entry.name, entry]));
	const ordered = [];
	const visiting = new Set();
	const visited = new Set();

	function visit(name) {
		if (visited.has(name)) {
			return;
		}
		assertCondition(!visiting.has(name), `fixture dependency cycle at ${name}`);
		const entry = byName.get(name);
		assertCondition(entry, `fixture dependency missing from smoke set: ${name}`);
		visiting.add(name);
		for (const dependency of entry.dependsOn || []) {
			if (byName.has(dependency)) {
				visit(dependency);
			}
		}
		visiting.delete(name);
		visited.add(name);
		ordered.push(entry);
	}

	for (const entry of entries) {
		visit(entry.name);
	}
	return ordered;
}

function cloneJson(value) {
	return JSON.parse(JSON.stringify(value));
}

function smokeObjectPath(name, assetRoot) {
	return `${assetRoot}/${name}.${name}`;
}

function rewriteSmokeReferences(value, manifest) {
	if (Array.isArray(value)) {
		return value.map((entry) => rewriteSmokeReferences(entry, manifest));
	}
	if (value && typeof value === "object") {
		for (const [key, child] of Object.entries(value)) {
			value[key] = rewriteSmokeReferences(child, manifest);
		}
		return value;
	}
	if (typeof value !== "string") {
		return value;
	}

	const fixtureNames = new Set(manifest.fixtures.map((entry) => entry.name));
	const match = value.match(/^\/Game\/(?:Generated\/StateTree|AFSmoke)\/(ST_[A-Za-z0-9_]+)\.(ST_[A-Za-z0-9_]+)$/);
	if (match && match[1] === match[2] && fixtureNames.has(match[1])) {
		return smokeObjectPath(match[1], manifest.assetRoot);
	}
	return value;
}

function prepareSmokeConfig(name, manifest) {
	const config = cloneJson(readFixture(name));
	config.Path = manifest.assetRoot;
	config.Action = "CreateOrUpdate";
	return rewriteSmokeReferences(config, manifest);
}

function assetPathFromName(name, manifest) {
	return `${manifest.assetRoot}/${name}`;
}

async function callJson(client, tool, args = {}) {
	const result = await client.callTool({ name: tool, arguments: args });
	const raw = (result.content || [])
		.filter((entry) => entry && entry.type === "text" && typeof entry.text === "string")
		.map((entry) => entry.text)
		.join("\n");

	try {
		return JSON.parse(raw);
	} catch (error) {
		return {
			parseError: error instanceof Error ? error.message : String(error),
			raw,
		};
	}
}

function assertCondition(condition, message) {
	if (!condition) {
		throw new Error(message);
	}
}

function assertNoUnexpectedDiagnostics(config, phase) {
	const diagnostics = config && config.diagnostics;
	const bindingDiagnostics = diagnostics && diagnostics.bindings;
	if (Array.isArray(bindingDiagnostics) && bindingDiagnostics.length > 0) {
		throw new Error(
			`${phase} extracted ${config.Name} had unexpected binding diagnostics: ${JSON.stringify(bindingDiagnostics)}`,
		);
	}
}

function writeJson(path, value) {
	writeFileSync(path, `${JSON.stringify(value, null, 2)}\n`, "utf8");
}

function cleanupExtractOutputs(fixtures) {
	for (const name of ["generate.initial.json", "generate.from_extract.json", "extract.1.json", "extract.2.json", "summary.json"]) {
		rmSync(join(outDir, name), { force: true });
	}
	for (const name of fixtures) {
		rmSync(join(outDir, `${name}.extract1.json`), { force: true });
		rmSync(join(outDir, `${name}.extract2.json`), { force: true });
	}
}

function writeExtractedConfigs(result, phase, fixtures) {
	assertCondition(Array.isArray(result.results), `${phase} result missing results list: ${JSON.stringify(result)}`);

	const expectedNames = new Set(fixtures);
	const extractedByName = new Map();
	for (const item of result.results) {
		const config = item && item.config;
		assertCondition(config && typeof config.Name === "string", `${phase} result missing config.Name: ${JSON.stringify(item)}`);
		assertCondition(expectedNames.has(config.Name), `${phase} result unexpected config.Name: ${config.Name}`);
		assertCondition(!extractedByName.has(config.Name), `${phase} result duplicated config.Name: ${config.Name}`);
		config.Action = "CreateOrUpdate";
		assertNoUnexpectedDiagnostics(config, phase);
		extractedByName.set(config.Name, config);
	}

	for (const name of fixtures) {
		assertCondition(extractedByName.has(name), `${phase} missing extracted config for ${name}`);
	}
	assertCondition(
		extractedByName.size === fixtures.length,
		`${phase} result extracted ${extractedByName.size} configs, expected ${fixtures.length}`,
	);

	for (const config of extractedByName.values()) {
		writeJson(join(outDir, `${config.Name}.${phase}.json`), config);
	}

	return extractedByName;
}

function firstMessage(result) {
	const messages = [];
	if (Array.isArray(result.results)) {
		for (const item of result.results) {
			if (item && item.status === "Failed" && typeof item.message === "string" && item.message.length > 0) {
				messages.push(item.message);
			}
		}
		for (const item of result.results) {
			if (item && item.status !== "Failed" && typeof item.message === "string" && item.message.length > 0) {
				messages.push(item.message);
			}
		}
	}
	if (typeof result.error === "string" && result.error.length > 0) {
		messages.push(result.error);
	}
	if (typeof result.raw === "string" && result.raw.length > 0) {
		messages.push(result.raw);
	}
	return messages.join("\n");
}

function assetPath(config) {
	return `${config.Path}/${config.Name}`;
}

function validateGenerationResult(result, phase, configs) {
	const expectedFullPaths = configs.map(assetPath);
	const expectedNames = configs.map((config) => config.Name);
	const expectedFullPathSet = new Set(expectedFullPaths);
	const expectedNameSet = new Set(expectedNames);

	assertCondition(result.success === true, `${phase} success expected true, got ${result.success}: ${JSON.stringify(result)}`);
	assertCondition(result.total === configs.length, `${phase} total expected ${configs.length}, got ${result.total}`);
	assertCondition(
		result.succeeded === configs.length,
		`${phase} succeeded expected ${configs.length}, got ${result.succeeded}`,
	);
	assertCondition(result.skipped === 0, `${phase} skipped expected 0, got ${result.skipped}`);
	assertCondition(result.failed === 0, `${phase} failed expected 0, got ${result.failed}`);
	assertCondition(Array.isArray(result.results), `${phase} results expected array: ${JSON.stringify(result)}`);
	assertCondition(
		result.results.length === configs.length,
		`${phase} results.length expected ${configs.length}, got ${result.results.length}`,
	);

	const actualFullPaths = new Set();
	const actualNames = new Set();
	for (const item of result.results) {
		assertCondition(item && typeof item === "object", `${phase} result item expected object: ${JSON.stringify(item)}`);
		assertCondition(
			item.status === "Success" || item.status === "Updated",
			`${phase} status expected Success or Updated, got ${item.status}: ${JSON.stringify(item)}`,
		);
		assertCondition(expectedNameSet.has(item.name), `${phase} name unexpected: ${item.name}`);
		assertCondition(expectedFullPathSet.has(item.fullPath), `${phase} fullPath unexpected: ${item.fullPath}`);
		assertCondition(!actualNames.has(item.name), `${phase} name duplicated: ${item.name}`);
		assertCondition(!actualFullPaths.has(item.fullPath), `${phase} fullPath duplicated: ${item.fullPath}`);
		actualNames.add(item.name);
		actualFullPaths.add(item.fullPath);
	}

	for (const name of expectedNames) {
		assertCondition(actualNames.has(name), `${phase} name missing: ${name}`);
	}
	for (const fullPath of expectedFullPaths) {
		assertCondition(actualFullPaths.has(fullPath), `${phase} fullPath missing: ${fullPath}`);
	}
	assertCondition(
		actualNames.size === expectedNameSet.size,
		`${phase} name set size expected ${expectedNameSet.size}, got ${actualNames.size}`,
	);
	assertCondition(
		actualFullPaths.size === expectedFullPathSet.size,
		`${phase} fullPath set size expected ${expectedFullPathSet.size}, got ${actualFullPaths.size}`,
	);
}

function validateExtractedPaths(extractedByName, expectedConfigs, phase) {
	for (const expectedConfig of expectedConfigs) {
		const config = extractedByName.get(expectedConfig.Name);
		assertCondition(config, `${phase} missing extracted config for ${expectedConfig.Name}`);
		assertCondition(
			assetPath(config) === assetPath(expectedConfig),
			`${phase} extracted asset path mismatch for ${expectedConfig.Name}: expected ${assetPath(expectedConfig)}, got ${assetPath(config)}`,
		);
	}
}

async function generateAssetsSequentially(client, configs, phase) {
	const aggregate = {
		success: true,
		total: 0,
		succeeded: 0,
		skipped: 0,
		failed: 0,
		summary: "",
		results: [],
	};
	const summaries = [];

	for (const config of configs) {
		const response = await callJson(client, "generate_assets", { assets: [config] });
		validateGenerationResult(response, `${phase} ${config.Name}`, [config]);

		aggregate.success = aggregate.success && response.success === true;
		aggregate.total += typeof response.total === "number" ? response.total : 0;
		aggregate.succeeded += typeof response.succeeded === "number" ? response.succeeded : 0;
		aggregate.skipped += typeof response.skipped === "number" ? response.skipped : 0;
		aggregate.failed += typeof response.failed === "number" ? response.failed : 0;
		if (typeof response.summary === "string" && response.summary.length > 0) {
			summaries.push(response.summary);
		}
		aggregate.results.push(...response.results);
	}

	aggregate.summary = summaries.join("\n");
	return aggregate;
}

function childEnv() {
	return {
		...Object.fromEntries(Object.entries(process.env).filter((entry) => typeof entry[1] === "string")),
		UE_API_BASE: ueApiBase,
	};
}

async function cleanSmokeAssets(client, manifest) {
	const cleanResult = await callJson(client, "execute_python", {
		code: `import unreal
asset_root = '${manifest.assetRoot}'
library = unreal.EditorAssetLibrary
if library.does_directory_exist(asset_root):
    if not library.delete_directory(asset_root):
        raise RuntimeError(f'Failed to delete {asset_root}')
if hasattr(library, 'make_directory'):
    library.make_directory(asset_root)
print('CLEANED_STATE_TREE_SMOKE_ROOT', asset_root)`,
		description: `Clean ${manifest.assetRoot} before StateTree final smoke`,
	});
	assertCondition(cleanResult.success === true, `clean smoke assets failed: ${JSON.stringify(cleanResult)}`);
	const logs = Array.isArray(cleanResult.logs)
		? cleanResult.logs.map((entry) => entry.message || "").join("\n")
		: "";
	const result = typeof cleanResult.result === "string" ? cleanResult.result : JSON.stringify(cleanResult.result || "");
	assertCondition(
		`${result}\n${logs}`.includes("CLEANED_STATE_TREE_SMOKE_ROOT"),
		`clean smoke assets did not confirm cleanup: ${JSON.stringify(cleanResult)}`,
	);
	console.log(`CLEANED_STATE_TREE_SMOKE_ROOT ${manifest.assetRoot}`);
}

function runPreflightBuild() {
	if (!preflightBuild) {
		console.log("PREFLIGHT_BUILD skipped=true");
		return { enabled: false, status: "skipped" };
	}

	const buildScript = process.env.UE_BUILD_SCRIPT || join(process.env.HOME || "", "UnrealEngine/Engine/Build/BatchFiles/Mac/Build.sh");
	const projectPath = process.env.UE_PROJECT_PATH || "/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject";
	const result = spawnSync(buildScript, ["ProjectRPGEditor", "Mac", "Development", `-Project=${projectPath}`, "-WaitMutex"], {
		cwd: repoDir,
		stdio: "inherit",
	});
	assertCondition(result.status === 0, `preflight build failed with status ${result.status}`);
	console.log("PREFLIGHT_BUILD skipped=false status=0");
	return { enabled: true, status: "passed" };
}

async function openFinalAsset(client, manifest) {
	const finalPath = assetPathFromName(manifest.finalOpenAsset, manifest);
	const marker = `OPENED_${manifest.finalOpenAsset}`;
	const openAsset = await callJson(client, "execute_python", {
		code: `import unreal
asset = unreal.load_asset('${finalPath}')
if asset is None:
    raise RuntimeError('Failed to load ${finalPath}')
subsystem = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
opened = subsystem.open_editor_for_assets([asset])
if not opened:
    raise RuntimeError('open_editor_for_assets returned false for ${finalPath}')
print('${marker}', opened)`,
		description: `Open StateTree final smoke asset ${finalPath}`,
	});
	const logs = Array.isArray(openAsset.logs)
		? openAsset.logs.map((entry) => entry.message || "").filter(Boolean).join(" | ")
		: "";
	console.log(`OPEN_ASSET success=${openAsset.success} path=${finalPath} logs=${logs}`);
	assertCondition(openAsset.success === true, `open asset failed: ${JSON.stringify(openAsset)}`);
	const openResult = typeof openAsset.result === "string" ? openAsset.result : JSON.stringify(openAsset.result || "");
	assertCondition(`${openResult}\n${logs}`.includes(marker), `open asset output missing ${marker}: ${JSON.stringify(openAsset)}`);
	return openAsset;
}

async function main() {
	mkdirSync(outDir, { recursive: true });

	const manifest = readManifest();
	const preflight = runPreflightBuild();
	const positiveEntries = topologicalFixtureOrder(fixtureEntries(manifest, "positive"));
	const invalidEntries = topologicalFixtureOrder([...positiveEntries, ...fixtureEntries(manifest, "negative")])
		.filter((entry) => entry.kind === "negative");
	const roundTripEntries = positiveEntries.filter((entry) => entry.roundTrip === true);
	const positiveNames = positiveEntries.map((entry) => entry.name);
	const roundTripNames = roundTripEntries.map((entry) => entry.name);
	const positiveConfigs = positiveNames.map((name) => prepareSmokeConfig(name, manifest));
	const invalidConfigs = invalidEntries.map((entry) => prepareSmokeConfig(entry.name, manifest));
	cleanupExtractOutputs(roundTripNames);

	const transport = new StdioClientTransport({
		command: "node",
		args: [join(mcpDir, "dist", "index.js")],
		cwd: mcpDir,
		env: childEnv(),
		stderr: "pipe",
	});
	const client = new Client({ name: "assetfactory-statetree-final-smoke", version: "1.0.0" });

	const summary = {
		assetRoot: manifest.assetRoot,
		preflight,
		positive: { expected: positiveEntries.length, generated: 0 },
		roundTrip: { expected: roundTripEntries.length, checked: 0 },
		negative: { expected: invalidEntries.length, failed: 0 },
		coverage: {},
		finalOpenAsset: manifest.finalOpenAsset,
		finalOpenPath: assetPathFromName(manifest.finalOpenAsset, manifest),
	};

	try {
		await client.connect(transport);

		const health = await callJson(client, "health_check");
		const healthSuccess = health.status === "ok";
		console.log(`HEALTH success=${healthSuccess} service=${health.service} port=${health.port}`);
		assertCondition(health.status === "ok", `health_check did not return ok: ${JSON.stringify(health)}`);

		await cleanSmokeAssets(client, manifest);

		const generateInitial = await generateAssetsSequentially(client, positiveConfigs, "initial generate");
		writeJson(join(outDir, "generate.initial.json"), generateInitial);
		console.log(
			`GENERATE_INITIAL success=${generateInitial.success} succeeded=${generateInitial.succeeded} failed=${generateInitial.failed}`,
		);
		validateGenerationResult(generateInitial, "initial generate", positiveConfigs);
		summary.positive.generated = generateInitial.succeeded;

		const assets = roundTripNames.map((name) => assetPathFromName(name, manifest));
		const extract1 = await callJson(client, "extract_assets", { assets });
		writeJson(join(outDir, "extract.1.json"), extract1);
		console.log(`EXTRACT1 success=${extract1.success} succeeded=${extract1.succeeded} failed=${extract1.failed}`);
		assertCondition(
			extract1.success === true && extract1.failed === 0,
			`first extract failed: ${JSON.stringify(extract1)}`,
		);

		const extractedByName = writeExtractedConfigs(extract1, "extract1", roundTripNames);
		validateExtractedPaths(extractedByName, roundTripNames.map((name) => prepareSmokeConfig(name, manifest)), "extract1");

		const extractedConfigs = roundTripNames.map((name) => {
			const config = extractedByName.get(name);
			assertCondition(config, `missing extracted config for ${name}`);
			config.Path = manifest.assetRoot;
			config.Action = "CreateOrUpdate";
			return config;
		});

		const regenerate = await generateAssetsSequentially(client, extractedConfigs, "regenerate from extract");
		writeJson(join(outDir, "generate.from_extract.json"), regenerate);
		console.log(`REGENERATE success=${regenerate.success} succeeded=${regenerate.succeeded} failed=${regenerate.failed}`);
		validateGenerationResult(regenerate, "regenerate from extract", extractedConfigs);

		const extract2 = await callJson(client, "extract_assets", { assets });
		writeJson(join(outDir, "extract.2.json"), extract2);
		console.log(`EXTRACT2 success=${extract2.success} succeeded=${extract2.succeeded} failed=${extract2.failed}`);
		assertCondition(
			extract2.success === true && extract2.failed === 0,
			`second extract failed: ${JSON.stringify(extract2)}`,
		);

		const regeneratedByName = writeExtractedConfigs(extract2, "extract2", roundTripNames);
		validateExtractedPaths(regeneratedByName, extractedConfigs, "extract2");

		for (const name of roundTripNames) {
			const left = join(outDir, `${name}.extract1.json`);
			const right = join(outDir, `${name}.extract2.json`);
			const check = spawnSync("python3", [checkerPath, left, right, "--fixture", name], {
				cwd: repoDir,
				stdio: "inherit",
			});
			assertCondition(check.status === 0, `round-trip check failed for ${name} with status ${check.status}`);
			summary.roundTrip.checked += 1;
		}

		for (const [index, entry] of invalidEntries.entries()) {
			const invalidResult = await callJson(client, "generate_assets", { assets: [invalidConfigs[index]] });
			const message = firstMessage(invalidResult);
			console.log(`INVALID ${entry.name} success=${invalidResult.success} failed=${invalidResult.failed} message=${message}`);
			assertCondition(
				invalidResult.success === false && invalidResult.failed === 1 && message.includes(entry.expectedError),
				`invalid fixture did not match expected error for ${entry.name}: expected '${entry.expectedError}', got ${JSON.stringify(invalidResult)}`,
			);
			summary.negative.failed += 1;
		}

		for (const entry of manifest.fixtures) {
			const bucket = summary.coverage[entry.spec] || { positive: [], negative: [] };
			bucket[entry.kind].push(entry.name);
			summary.coverage[entry.spec] = bucket;
		}

		const openAsset = await openFinalAsset(client, manifest);
		summary.finalOpenResult = openAsset.success === true ? "opened" : "failed";
		writeJson(join(outDir, "summary.json"), summary);
		console.log(`SUMMARY positive=${summary.positive.generated}/${summary.positive.expected} roundTrip=${summary.roundTrip.checked}/${summary.roundTrip.expected} negative=${summary.negative.failed}/${summary.negative.expected} finalOpen=${summary.finalOpenPath}`);
	} finally {
		await client.close().catch(() => undefined);
		await transport.close().catch(() => undefined);
	}
}

main().catch((error) => {
	console.error(error && error.stack ? error.stack : error);
	process.exit(1);
});
