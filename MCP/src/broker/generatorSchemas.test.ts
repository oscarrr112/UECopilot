import test from "node:test";
import assert from "node:assert/strict";
import { createServer, IncomingMessage, ServerResponse } from "node:http";
import { dirname, join } from "path";
import { fileURLToPath } from "url";

import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";

const DIST_DIR = dirname(dirname(fileURLToPath(import.meta.url)));
const PROJECT_DIR = dirname(DIST_DIR);
const SERVER_ENTRY = join(DIST_DIR, "index.js");
const CHILD_ENV = Object.fromEntries(
	Object.entries(process.env).filter((entry): entry is [string, string] => typeof entry[1] === "string"),
);

async function withClient(
	run: (client: Client) => Promise<void>,
	envOverrides: Record<string, string> = {},
): Promise<void> {
	const transport = new StdioClientTransport({
		command: process.execPath,
		args: [SERVER_ENTRY],
		cwd: PROJECT_DIR,
		env: { ...CHILD_ENV, ...envOverrides },
		stderr: "pipe",
	});
	const client = new Client({ name: "Codex", version: "desktop" }, { capabilities: {} });

	try {
		await client.connect(transport);
		await run(client);
	} finally {
		await client.close().catch(() => undefined);
		await transport.close().catch(() => undefined);
	}
}

async function withJsonServer(
	handler: (request: IncomingMessage, response: ServerResponse) => void,
	run: (baseUrl: string) => Promise<void>,
): Promise<void> {
	const server = createServer(handler);
	await new Promise<void>((resolve, reject) => {
		server.once("error", reject);
		server.listen(0, "127.0.0.1", () => resolve());
	});

	try {
		const address = server.address();
		assert.ok(address && typeof address === "object");
		await run(`http://127.0.0.1:${address.port}`);
	} finally {
		await new Promise<void>((resolve, reject) => {
			server.close((error) => (error ? reject(error) : resolve()));
		});
	}
}

async function closedBaseUrl(): Promise<string> {
	const server = createServer();
	await new Promise<void>((resolve, reject) => {
		server.once("error", reject);
		server.listen(0, "127.0.0.1", () => resolve());
	});
	const address = server.address();
	assert.ok(address && typeof address === "object");
	const baseUrl = `http://127.0.0.1:${address.port}`;
	await new Promise<void>((resolve, reject) => {
		server.close((error) => (error ? reject(error) : resolve()));
	});
	return baseUrl;
}

function textContent(result: unknown): string {
	assert.ok(typeof result === "object" && result !== null && "content" in result && Array.isArray(result.content));
	return result.content
		.filter(
			(entry): entry is { type: "text"; text: string } =>
				typeof entry === "object" && entry !== null && "type" in entry && entry.type === "text" && "text" in entry,
		)
		.map((entry) => entry.text)
		.join("\n");
}

test("generator schemas are readable for StateTree, BehaviorTree, BlackboardData, and AnimSequence", async () => {
	await withClient(async (client) => {
		const expected = [
			{
				assetType: "StateTree",
				patterns: [/StateTree/, /RootParameters/, /bindings/, /round-trip/i],
			},
			{
				assetType: "BehaviorTree",
				patterns: [/BehaviorTree/, /BlackboardInline/, /Root/, /BTTask_RunBehavior/],
			},
			{
				assetType: "BlackboardData",
				patterns: [/BlackboardData/, /Keys/, /BaseClass/, /bInstanceSynced/],
			},
			{
				assetType: "AnimSequence",
				patterns: [
					/AnimSequence/,
					/Skeleton/,
					/FloatCurves/,
					/Notifies/,
					/SyncMarkers/,
					/does not support raw animation/i,
				],
			},
		] as const;

		for (const entry of expected) {
			const result = await client.callTool({
				name: "get_generator_schema",
				arguments: { asset_type: entry.assetType },
			});

			assert.notEqual(result.isError, true, `${entry.assetType} schema call should succeed`);
			const text = textContent(result);
			for (const pattern of entry.patterns) {
				assert.match(text, pattern, `${entry.assetType} schema should include ${pattern}`);
			}
		}
	});
});

test("generate_assets and get_generator_schema advertise StateTree, BehaviorTree, BlackboardData, and AnimSequence", async () => {
	await withClient(async (client) => {
		const listed = await client.listTools();
		const generateAssets = listed.tools.find((tool) => tool.name === "generate_assets");
		const getSchema = listed.tools.find((tool) => tool.name === "get_generator_schema");

		assert.ok(generateAssets, "generate_assets should be visible");
		assert.ok(getSchema, "get_generator_schema should be visible");

		const generateSchema = JSON.stringify(generateAssets.inputSchema);
		const getSchemaInput = JSON.stringify(getSchema.inputSchema);
		for (const assetType of ["StateTree", "BehaviorTree", "BlackboardData", "AnimSequence"]) {
			assert.match(generateSchema, new RegExp(assetType), `generate_assets should mention ${assetType}`);
			assert.match(getSchemaInput, new RegExp(assetType), `get_generator_schema should mention ${assetType}`);
		}
	});
});

test("AssetDocument tools are listed and schema documentation is readable", async () => {
	await withClient(async (client) => {
		const listed = await client.listTools();
		const toolNames = listed.tools.map((tool) => tool.name);

		for (const toolName of [
			"apply_asset_document",
			"apply_asset_document_file",
			"get_asset_document_schema",
			"inspect_asset_document_target",
			"inspect_asset_document_profile",
			"create_asset_document_template",
			"extract_asset_document",
			"validate_asset_document",
			"diff_asset_document",
		]) {
			assert.ok(toolNames.includes(toolName), `${toolName} should be visible`);
		}

		const result = await client.callTool({
			name: "get_asset_document_schema",
			arguments: {},
		});

		assert.notEqual(result.isError, true);
		const text = textContent(result);
		for (const pattern of [
			/Target/,
			/Name/,
			/Path/,
			/Properties/,
			/typed/i,
			/untyped/i,
			/no subtype/i,
			/inspect_asset_document_target/,
			/RegionPolicies/,
			/RegionPolicyPresets/,
			/\/Script\/Engine\.AnimSequence/,
			/post-import only/i,
			/RawTracks/,
			/CompressedData/,
			/_Skipped/,
			/extract_asset_document/,
			/validate_asset_document/,
			/diff_asset_document/,
			/file watcher/i,
			/\/Script\/UMGEditor\.WidgetBlueprint/,
			/Body\.WidgetTree/,
			/Body\.Bindings/,
			/Body\.Animations/,
			/FunctionGraphs/,
			/WidgetVariableGuids/,
			/UnsupportedWidgetAnimationTrack/,
		]) {
			assert.match(text, pattern, `AssetDocument schema should include ${pattern}`);
		}
		assert.doesNotMatch(text, /"AssetType"\s*:\s*"WidgetBlueprint"/, "AssetDocument schema should not present generator-only WidgetBlueprint AssetType input");
		assert.doesNotMatch(text, /empty until WidgetBlueprint interface adapter lands/i, "WidgetBlueprint ImplementedInterfaces must not be documented as an unimplemented placeholder");
	});
});

test("AssetDocument validate and diff require exactly one document source", async () => {
	await withClient(async (client) => {
		const listed = await client.listTools();
		for (const toolName of ["validate_asset_document", "diff_asset_document"]) {
			const tool = listed.tools.find((candidate) => candidate.name === toolName);
			assert.ok(tool, `${toolName} should be visible`);
			assert.match(JSON.stringify(tool.inputSchema), /oneOf/, `${toolName} should advertise oneOf source constraints`);

			const bothSources = await client.callTool({
				name: toolName,
				arguments: {
					document: {
						SchemaVersion: 1,
						AssetType: "GenericAsset",
						Target: "/Game/Data/DA_Test",
						Properties: {},
					},
					file_path: "E:/GameDev/Project/Saved/AssetFactory/Sidecars/DA_Test.assetdocument.json",
				},
			});
			assert.equal(bothSources.isError, true, `${toolName} should reject both document and file_path`);

			const noSource = await client.callTool({
				name: toolName,
				arguments: {},
			});
			assert.equal(noSource.isError, true, `${toolName} should reject missing document and file_path`);
		}
	});
});

test("AssetDocument tools report UE HTTP failures as MCP errors", async () => {
	await withJsonServer((request, response) => {
		assert.equal(request.url, "/assetfactory/assetdocument/validate");
		response.writeHead(200, { "Content-Type": "application/json" });
		response.end(JSON.stringify({ success: false, error: "validation failed in UE" }));
	}, async (baseUrl) => {
		await withClient(async (client) => {
			const result = await client.callTool({
				name: "validate_asset_document",
				arguments: {
					document: {
						SchemaVersion: 1,
						AssetType: "GenericAsset",
						Target: "/Game/Data/DA_Test",
						Properties: {},
					},
				},
			});

			assert.equal(result.isError, true);
			assert.match(textContent(result), /validation failed in UE/);
		}, { UE_API_BASE: baseUrl });
	});
});

test("AssetDocument tools report non-2xx UE responses as MCP errors", async () => {
	await withJsonServer((request, response) => {
		assert.equal(request.url, "/assetfactory/assetdocument/diff");
		response.writeHead(500, { "Content-Type": "application/json" });
		response.end(JSON.stringify({ error: "backend exploded" }));
	}, async (baseUrl) => {
		await withClient(async (client) => {
			const result = await client.callTool({
				name: "diff_asset_document",
				arguments: {
					file_path: "E:/GameDev/Project/Saved/AssetFactory/Sidecars/DA_Test.assetdocument.json",
				},
			});

			assert.equal(result.isError, true);
			assert.match(textContent(result), /HTTP 500/);
			assert.match(textContent(result), /backend exploded/);
		}, { UE_API_BASE: baseUrl });
	});
});

test("AssetDocument tools report unavailable UE server as MCP errors", async () => {
	const offlineUrl = await closedBaseUrl();
	await withClient(async (client) => {
		const result = await client.callTool({
			name: "diff_asset_document",
			arguments: {
				file_path: "E:/GameDev/Project/Saved/AssetFactory/Sidecars/DA_Test.assetdocument.json",
			},
		});

		assert.equal(result.isError, true);
		assert.match(textContent(result), /Failed to connect to UE AssetDocument API/);
	}, { UE_API_BASE: offlineUrl });
});

test("missing schema fallback lists StateTree, BehaviorTree, BlackboardData, and AnimSequence", async () => {
	await withClient(async (client) => {
		const result = await client.callTool({
			name: "get_generator_schema",
			arguments: { asset_type: "DefinitelyMissingGenerator" },
		});

		assert.notEqual(result.isError, true);
		const text = textContent(result);
		for (const assetType of ["StateTree", "BehaviorTree", "BlackboardData", "AnimSequence"]) {
			assert.match(text, new RegExp(assetType), `fallback should mention ${assetType}`);
		}
	});
});
