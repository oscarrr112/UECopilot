#!/usr/bin/env node

import { mkdirSync, readFileSync, writeFileSync } from "fs";
import { dirname, join, resolve } from "path";
import { fileURLToPath } from "url";

import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";


const scriptDir = dirname(fileURLToPath(import.meta.url));
const mcpDir = resolve(scriptDir, "..");
const FAILURE_BUCKETS = ["changed", "added", "removed", "missing", "extra", "skipped", "failed", "errors"];

function usage() {
	return `Usage: node ${fileURLToPath(import.meta.url)} --manifest FILE --evidence-dir DIR [--base-url URL]

Live stdio-MCP smoke for the primary BlackboardData and BehaviorTree documents.
The UE HTTP service must already be running. This script never starts/stops Editor.`;
}

function parseArgs(argv) {
	const options = { baseUrl: "http://127.0.0.1:8562" };
	for (let index = 0; index < argv.length; index += 1) {
		const arg = argv[index];
		if (arg === "--help" || arg === "-h") {
			options.help = true;
		} else if (arg === "--manifest") {
			options.manifest = argv[++index];
		} else if (arg === "--evidence-dir") {
			options.evidenceDir = argv[++index];
		} else if (arg === "--base-url") {
			options.baseUrl = argv[++index];
		} else {
			throw new Error(`Unknown argument: ${arg}`);
		}
	}
	if (!options.help && (!options.manifest || !options.evidenceDir)) {
		throw new Error("--manifest and --evidence-dir are required");
	}
	return options;
}

function readJson(path) {
	return JSON.parse(readFileSync(path, "utf8"));
}

function writeJson(path, value) {
	writeFileSync(path, `${JSON.stringify(value, null, 2)}\n`, "utf8");
}

function assertCondition(condition, message) {
	if (!condition) {
		throw new Error(message);
	}
}

function payload(response) {
	return response && typeof response.payload === "object" && !Array.isArray(response.payload) ? response.payload : {};
}

function assertCleanResponse(response, label) {
	assertCondition(response && response.success === true, `${label} success was not true: ${JSON.stringify(response)}`);
	assertCondition(!Array.isArray(response.diagnostics) || response.diagnostics.length === 0, `${label} diagnostics were nonempty`);
	for (const [containerName, container] of [["response", response], ["payload", payload(response)]]) {
		for (const bucket of FAILURE_BUCKETS) {
			assertCondition(!container[bucket] || container[bucket].length === 0, `${label} ${containerName}.${bucket} was nonempty`);
		}
		assertCondition(!container.sidecar_sync_update_skipped, `${label} sync was skipped`);
		assertCondition(!container.sidecar_sync_update_skip_reason, `${label} sync skip reason was present`);
	}
	return payload(response);
}

function childEnv(baseUrl) {
	return {
		...Object.fromEntries(Object.entries(process.env).filter((entry) => typeof entry[1] === "string")),
		UE_API_BASE: baseUrl.replace(/\/$/, ""),
	};
}

function textResult(result) {
	return (result.content || [])
		.filter((entry) => entry && entry.type === "text" && typeof entry.text === "string")
		.map((entry) => entry.text)
		.join("\n");
}

async function main() {
	const options = parseArgs(process.argv.slice(2));
	if (options.help) {
		console.log(usage());
		return;
	}

	const evidenceDir = resolve(options.evidenceDir);
	mkdirSync(evidenceDir, { recursive: true });
	const manifest = readJson(resolve(options.manifest));
	const documents = manifest.documents || {};
	for (const name of ["blackboard", "behavior_tree"]) {
		assertCondition(documents[name], `manifest missing ${name}`);
	}

	const transport = new StdioClientTransport({
		command: "node",
		args: [join(mcpDir, "dist", "index.js")],
		cwd: mcpDir,
		env: childEnv(options.baseUrl),
		stderr: "pipe",
	});
	const client = new Client({ name: "assetfactory-btbb-live-smoke", version: "1.0.0" });
	const brokerStderr = [];
	let callIndex = 0;
	const calls = [];

	async function call(tool, args = {}, label = tool) {
		callIndex += 1;
		const prefix = `${String(callIndex).padStart(3, "0")}_${label.replace(/[^A-Za-z0-9_-]/g, "_")}`;
		const request = { tool, arguments: args };
		writeJson(join(evidenceDir, `${prefix}.request.json`), request);
		const result = await client.callTool({ name: tool, arguments: args });
		const raw = textResult(result);
		let response;
		try {
			response = JSON.parse(raw);
		} catch (error) {
			throw new Error(`${label} returned invalid JSON: ${raw.slice(0, 2000)} (${error})`);
		}
		writeJson(join(evidenceDir, `${prefix}.response.json`), { isError: result.isError === true, body: response });
		assertCondition(result.isError !== true, `${label} MCP result was marked isError`);
		calls.push({ label, tool, status: "passed" });
		return response;
	}

	try {
		if (transport.stderr && typeof transport.stderr.on === "function") {
			transport.stderr.on("data", (chunk) => brokerStderr.push(String(chunk)));
		}
		await client.connect(transport);

		const health = await call("health_check", {}, "health");
		assertCondition(health.status === "ok", `health status was not ok: ${JSON.stringify(health)}`);

		const seenClasses = new Set();
		for (const name of ["blackboard", "behavior_tree"]) {
			const entry = documents[name];
			if (!seenClasses.has(entry.class)) {
				seenClasses.add(entry.class);
				const profile = await call(
					"inspect_asset_document_profile",
					{ class_or_asset: entry.class },
					`${name}_profile`,
				);
				const profilePayload = assertCleanResponse(profile, `${name} profile`);
				assertCondition(profilePayload.Class === entry.class, `${name} profile Class mismatch`);

				const template = await call(
					"create_asset_document_template",
					{ class: entry.class, target: `${entry.target}_MCPTemplateProbe` },
					`${name}_template`,
				);
				const templatePayload = assertCleanResponse(template, `${name} template`);
				assertCondition(templatePayload.Class === entry.class, `${name} template Class mismatch`);
			}

			const validate = await call(
				"validate_asset_document",
				{ file_path: entry.sidecar },
				`${name}_validate`,
			);
			assertCleanResponse(validate, `${name} validate`);

			const apply = await call(
				"apply_asset_document_file",
				{ file_path: entry.sidecar, save_asset: true },
				`${name}_apply_file_save`,
			);
			assertCleanResponse(apply, `${name} apply-file/save`);
			assertCondition(apply.saved_asset === true, `${name} apply did not report saved_asset=true`);
			assertCondition(apply.wrote_sidecar === true, `${name} apply did not report wrote_sidecar=true`);

			const extract = await call(
				"extract_asset_document",
				{ asset_path: entry.target, diff_only: false, include_all_writable: true },
				`${name}_extract`,
			);
			const extractPayload = assertCleanResponse(extract, `${name} extract`);
			assertCondition(extractPayload.Target === entry.target, `${name} extract Target mismatch`);

			const diff = await call(
				"diff_asset_document",
				{ file_path: entry.sidecar },
				`${name}_diff`,
			);
			assertCleanResponse(diff, `${name} diff`);
		}

		const summary = {
			status: "passed",
			base_url: options.baseUrl,
			manifest: resolve(options.manifest),
			calls,
			missing: [],
			extra: [],
			skipped: [],
			failed: [],
		};
		writeJson(join(evidenceDir, "summary.json"), summary);
		console.log(JSON.stringify(summary, null, 2));
	} catch (error) {
		const message = error instanceof Error ? error.stack || error.message : String(error);
		writeJson(join(evidenceDir, "summary.json"), {
			status: "failed",
			error: message,
			calls,
			failed: [message],
		});
		throw error;
	} finally {
		writeFileSync(join(evidenceDir, "broker.stderr.log"), brokerStderr.join(""), "utf8");
		await client.close().catch(() => {});
	}
}

main().catch((error) => {
	console.error(error instanceof Error ? error.stack || error.message : String(error));
	process.exitCode = 1;
});
