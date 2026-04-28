#!/usr/bin/env node

import { spawnSync } from "child_process";
import { mkdirSync, readFileSync, writeFileSync } from "fs";
import { dirname, join, resolve } from "path";
import { fileURLToPath } from "url";

import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";

const scriptDir = dirname(fileURLToPath(import.meta.url));
const mcpDir = resolve(scriptDir, "..");
const repoDir = resolve(mcpDir, "..");
const testDataDir = join(repoDir, "TestData");
const checkerPath = join(repoDir, "docs", "superpowers", "verification", "statetree_roundtrip_check.py");
const outDir = process.env.STATETREE_ROUNDTRIP_OUT || "/tmp/assetfactory-statetree-roundtrip";
const ueApiBase = process.env.UE_API_BASE || "http://127.0.0.1:8559";

const positiveFixtures = [
	"ST_Core_Minimal",
	"ST_Core_AIComponentSchema",
	"ST_Dynamic_Delay_Minimal",
	"ST_Dynamic_DebugText_WithInstance",
	"ST_Dynamic_Condition_CompareInt",
	"ST_Structure_Transitions",
	"ST_Structure_LinkedSubtree",
	"ST_Structure_LinkedAsset_Target",
	"ST_Structure_LinkedAsset_Referencer",
	"ST_Parameters_Basic",
	"ST_Parameters_Complex",
	"ST_Parameters_LinkedTarget",
	"ST_Parameters_LinkedReferencer",
	"ST_Bindings_Ordinary",
	"ST_Bindings_Function",
	"ST_RoundTrip_Comprehensive",
];

const invalidFixtures = [
	"ST_Core_InvalidSchema",
	"ST_Dynamic_Invalid_UnknownNode",
	"ST_Dynamic_Invalid_Category_TaskSlotCondition",
	"ST_Dynamic_Invalid_SchemaAIMoveToInComponent",
	"ST_Structure_Invalid_MissingTransitionTarget",
	"ST_Structure_Invalid_AmbiguousTransitionTarget",
	"ST_Structure_Invalid_LinkedSubtreeTargetNotSubtree",
	"ST_Structure_Invalid_EventMissingTag",
	"ST_Parameters_Invalid_BadGuid",
	"ST_Parameters_Invalid_BadValue",
	"ST_Parameters_Invalid_UnknownType",
	"ST_Parameters_Invalid_MapUnsupported",
	"ST_Parameters_Invalid_LinkedOverrideUnknown",
	"ST_Parameters_Invalid_LinkedOverrideTypeMismatch",
	"ST_Bindings_Invalid_UnknownSource",
	"ST_Bindings_Invalid_UnknownTarget",
	"ST_Bindings_Invalid_BadPath",
	"ST_Bindings_Invalid_DuplicateTarget",
	"ST_Bindings_Invalid_BadFunctionType",
	"ST_Bindings_Invalid_TypeMismatch",
];

function readFixture(name) {
	return JSON.parse(readFileSync(join(testDataDir, `${name}.json`), "utf8"));
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

function writeJson(path, value) {
	writeFileSync(path, `${JSON.stringify(value, null, 2)}\n`, "utf8");
}

function firstMessage(result) {
	if (typeof result.error === "string" && result.error.length > 0) {
		return result.error;
	}
	if (Array.isArray(result.results)) {
		const failed = result.results.find((item) => item && item.status === "Failed");
		if (failed && typeof failed.message === "string" && failed.message.length > 0) {
			return failed.message;
		}
		const any = result.results.find((item) => item && typeof item.message === "string" && item.message.length > 0);
		if (any) {
			return any.message;
		}
	}
	return "";
}

function assetPath(config) {
	return `${config.Path}/${config.Name}`;
}

function childEnv() {
	return {
		...Object.fromEntries(Object.entries(process.env).filter((entry) => typeof entry[1] === "string")),
		UE_API_BASE: ueApiBase,
	};
}

async function main() {
	mkdirSync(outDir, { recursive: true });

	const positiveConfigs = positiveFixtures.map(readFixture);
	const invalidConfigs = invalidFixtures.map(readFixture);

	const transport = new StdioClientTransport({
		command: "node",
		args: [join(mcpDir, "dist", "index.js")],
		cwd: mcpDir,
		env: childEnv(),
		stderr: "pipe",
	});
	const client = new Client({ name: "assetfactory-statetree-roundtrip-smoke", version: "1.0.0" });

	try {
		await client.connect(transport);

		const health = await callJson(client, "health_check");
		const healthSuccess = health.status === "ok";
		console.log(`HEALTH success=${healthSuccess} service=${health.service} port=${health.port}`);
		assertCondition(health.status === "ok", `health_check did not return ok: ${JSON.stringify(health)}`);

		const generateInitial = await callJson(client, "generate_assets", { assets: positiveConfigs });
		writeJson(join(outDir, "generate.initial.json"), generateInitial);
		console.log(
			`GENERATE_INITIAL success=${generateInitial.success} succeeded=${generateInitial.succeeded} failed=${generateInitial.failed}`,
		);
		assertCondition(
			generateInitial.success === true && generateInitial.failed === 0,
			`initial generate failed: ${JSON.stringify(generateInitial)}`,
		);

		const assets = positiveConfigs.map(assetPath);
		const extract1 = await callJson(client, "extract_assets", { assets });
		writeJson(join(outDir, "extract.1.json"), extract1);
		console.log(`EXTRACT1 success=${extract1.success} succeeded=${extract1.succeeded} failed=${extract1.failed}`);
		assertCondition(
			extract1.success === true && extract1.failed === 0,
			`first extract failed: ${JSON.stringify(extract1)}`,
		);

		const extractedByName = new Map();
		for (const item of extract1.results || []) {
			const config = item && item.config;
			assertCondition(config && typeof config.Name === "string", `extract1 result missing config.Name: ${JSON.stringify(item)}`);
			config.Action = "CreateOrUpdate";
			extractedByName.set(config.Name, config);
			writeJson(join(outDir, `${config.Name}.extract1.json`), config);
		}

		const extractedConfigs = positiveFixtures.map((name) => {
			const config = extractedByName.get(name);
			assertCondition(config, `missing extracted config for ${name}`);
			return config;
		});

		const regenerate = await callJson(client, "generate_assets", { assets: extractedConfigs });
		writeJson(join(outDir, "generate.from_extract.json"), regenerate);
		console.log(`REGENERATE success=${regenerate.success} succeeded=${regenerate.succeeded} failed=${regenerate.failed}`);
		assertCondition(
			regenerate.success === true && regenerate.failed === 0,
			`regenerate from extract failed: ${JSON.stringify(regenerate)}`,
		);

		const extract2 = await callJson(client, "extract_assets", { assets });
		writeJson(join(outDir, "extract.2.json"), extract2);
		console.log(`EXTRACT2 success=${extract2.success} succeeded=${extract2.succeeded} failed=${extract2.failed}`);
		assertCondition(
			extract2.success === true && extract2.failed === 0,
			`second extract failed: ${JSON.stringify(extract2)}`,
		);

		for (const item of extract2.results || []) {
			const config = item && item.config;
			assertCondition(config && typeof config.Name === "string", `extract2 result missing config.Name: ${JSON.stringify(item)}`);
			config.Action = "CreateOrUpdate";
			writeJson(join(outDir, `${config.Name}.extract2.json`), config);
		}

		for (const name of positiveFixtures) {
			const left = join(outDir, `${name}.extract1.json`);
			const right = join(outDir, `${name}.extract2.json`);
			const check = spawnSync("python3", [checkerPath, left, right, "--fixture", name], {
				cwd: repoDir,
				stdio: "inherit",
			});
			assertCondition(check.status === 0, `round-trip check failed for ${name} with status ${check.status}`);
		}

		for (const [index, name] of invalidFixtures.entries()) {
			const invalidResult = await callJson(client, "generate_assets", { assets: [invalidConfigs[index]] });
			const message = firstMessage(invalidResult);
			console.log(`INVALID ${name} success=${invalidResult.success} failed=${invalidResult.failed} message=${message}`);
			assertCondition(
				invalidResult.success === false && invalidResult.failed === 1 && message.length > 0,
				`invalid fixture did not fail as expected for ${name}: ${JSON.stringify(invalidResult)}`,
			);
		}

		const openAsset = await callJson(client, "execute_python", {
			code: `import unreal
asset = unreal.load_asset('/Game/AFSmoke/ST_RoundTrip_Comprehensive')
unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([asset])
print('OPENED_ST_ROUNDTRIP_COMPREHENSIVE', bool(asset))`,
			description: "Open StateTree round-trip smoke asset",
		});
		const logs = Array.isArray(openAsset.logs)
			? openAsset.logs.map((entry) => entry.message || "").filter(Boolean).join(" | ")
			: "";
		console.log(`OPEN_ASSET success=${openAsset.success} logs=${logs}`);
		assertCondition(openAsset.success === true, `open asset failed: ${JSON.stringify(openAsset)}`);
	} finally {
		await client.close().catch(() => undefined);
		await transport.close().catch(() => undefined);
	}
}

main().catch((error) => {
	console.error(error && error.stack ? error.stack : error);
	process.exit(1);
});
