import test from "node:test";
import assert from "node:assert/strict";
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

async function withClient(run: (client: Client) => Promise<void>): Promise<void> {
	const transport = new StdioClientTransport({
		command: process.execPath,
		args: [SERVER_ENTRY],
		cwd: PROJECT_DIR,
		env: CHILD_ENV,
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

test("generator schemas are readable for StateTree, BehaviorTree, and BlackboardData", async () => {
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

test("generate_assets and get_generator_schema advertise StateTree, BehaviorTree, and BlackboardData", async () => {
	await withClient(async (client) => {
		const listed = await client.listTools();
		const generateAssets = listed.tools.find((tool) => tool.name === "generate_assets");
		const getSchema = listed.tools.find((tool) => tool.name === "get_generator_schema");

		assert.ok(generateAssets, "generate_assets should be visible");
		assert.ok(getSchema, "get_generator_schema should be visible");

		const generateSchema = JSON.stringify(generateAssets.inputSchema);
		const getSchemaInput = JSON.stringify(getSchema.inputSchema);
		for (const assetType of ["StateTree", "BehaviorTree", "BlackboardData"]) {
			assert.match(generateSchema, new RegExp(assetType), `generate_assets should mention ${assetType}`);
			assert.match(getSchemaInput, new RegExp(assetType), `get_generator_schema should mention ${assetType}`);
		}
	});
});

test("missing schema fallback lists StateTree, BehaviorTree, and BlackboardData", async () => {
	await withClient(async (client) => {
		const result = await client.callTool({
			name: "get_generator_schema",
			arguments: { asset_type: "DefinitelyMissingGenerator" },
		});

		assert.notEqual(result.isError, true);
		const text = textContent(result);
		for (const assetType of ["StateTree", "BehaviorTree", "BlackboardData"]) {
			assert.match(text, new RegExp(assetType), `fallback should mention ${assetType}`);
		}
	});
});
