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

async function withClient(
  clientInfo: { name: string; version: string },
  run: (client: Client, transport: StdioClientTransport) => Promise<void>,
): Promise<void> {
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: [SERVER_ENTRY],
    cwd: PROJECT_DIR,
    env: CHILD_ENV,
    stderr: "pipe",
  });
  const client = new Client(clientInfo, { capabilities: {} });

  try {
    await client.connect(transport);
    await run(client, transport);
  } finally {
    await client.close().catch(() => undefined);
    await transport.close().catch(() => undefined);
  }
}

test("Codex stdio clients only see BSL blueprint tools and receive guidance for blocked legacy calls", async () => {
  await withClient({ name: "Codex", version: "desktop" }, async (client) => {
    const listedTools = await client.listTools();
    const toolNames = listedTools.tools.map((tool) => tool.name);

    assert.ok(toolNames.includes("apply_blueprint_as_bsl"));
    assert.ok(!toolNames.includes("chat_completion"));
    assert.ok(!toolNames.includes("apply_blueprint_change"));
    assert.ok(!toolNames.includes("generate_blueprint_change"));

    const schemaResult = await client.callTool({
      name: "get_generator_schema",
      arguments: {
        asset_type: "Blueprint",
      },
    });

    assert.notEqual(schemaResult.isError, true);
    assert.ok("content" in schemaResult && Array.isArray(schemaResult.content));
    assert.match(
      schemaResult.content
        .filter(
          (entry): entry is { type: "text"; text: string } =>
            typeof entry === "object" && entry !== null && "type" in entry && entry.type === "text" && "text" in entry,
        )
        .map((entry) => entry.text)
        .join("\n"),
      /Blueprint Generator Schema|ParentClass/i,
    );

    const result = await client.callTool({
      name: "apply_blueprint_change",
      arguments: {
        asset_path: "/Game/Test/BP_Test",
        blueprint_json: "{}",
      },
    });

    assert.equal(result.isError, true);
    assert.ok("content" in result && Array.isArray(result.content));
    assert.match(
      result.content
        .filter(
          (entry): entry is { type: "text"; text: string } =>
            typeof entry === "object" && entry !== null && "type" in entry && entry.type === "text" && "text" in entry,
        )
        .map((entry) => entry.text)
        .join("\n"),
      /BSL blueprint tools/i,
    );
  });
});

test("Claude Desktop stdio clients still see legacy blueprint tools", async () => {
  await withClient({ name: "Claude Desktop", version: "1.0.0" }, async (client) => {
    const listedTools = await client.listTools();
    const toolNames = listedTools.tools.map((tool) => tool.name);

    assert.ok(toolNames.includes("chat_completion"));
    assert.ok(toolNames.includes("apply_blueprint_change"));
    assert.ok(toolNames.includes("generate_blueprint_change"));
    assert.ok(toolNames.includes("apply_blueprint_as_bsl"));

    const legacyCompatResult = await client.callTool({
      name: "validate_blueprint_json",
      arguments: {
        blueprint_json: JSON.stringify({ name: "BP_Test", parent_class: "Actor", functions: [] }),
      },
    });

    assert.notEqual(legacyCompatResult.isError, true);
    assert.ok("content" in legacyCompatResult && Array.isArray(legacyCompatResult.content));
    assert.match(
      legacyCompatResult.content
        .filter(
          (entry): entry is { type: "text"; text: string } =>
            typeof entry === "object" && entry !== null && "type" in entry && entry.type === "text" && "text" in entry,
        )
        .map((entry) => entry.text)
        .join("\n"),
      /"valid"\s*:\s*true/,
    );
  });
});
