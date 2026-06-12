import test from "node:test";
import assert from "node:assert/strict";

import {
  ALL_TOOLS,
  assertToolAllowed,
  buildVisibleTools,
  validateToolCatalogPolicy,
} from "./toolCatalog.js";

type Tool = {
  name: string;
  description?: string;
};

test("buildVisibleTools returns shared asset tools, shared editor tools, and BSL blueprint tools for codex in order", () => {
  const visibleTools = buildVisibleTools(ALL_TOOLS, "codex") as Tool[];

  assert.deepEqual(
    visibleTools.map((tool: Tool) => tool.name),
    [
      "health_check",
      "list_generators",
      "get_generator_schema",
      "generate_assets",
      "get_asset_document_schema",
      "inspect_asset_document_target",
      "inspect_asset_document_profile",
      "create_asset_document_template",
      "validate_asset_document",
      "diff_asset_document",
      "extract_asset_document",
      "apply_asset_document",
      "apply_asset_document_file",
      "extract_assets",
      "query_asset",
      "delete_assets",
      "update_datatable_rows",
      "get_editor_context",
      "execute_python",
      "get_viewport_screenshot",
      "extract_blueprint_graph",
      "extract_blueprint_as_bsl",
      "apply_blueprint_as_bsl",
    ],
  );
});

test("buildVisibleTools keeps legacy JSON blueprint tools for claude and includes BSL tools", () => {
  const visibleTools = buildVisibleTools(ALL_TOOLS, "claude") as Tool[];
  const toolNames = visibleTools.map((tool: Tool) => tool.name);

  assert.ok(toolNames.includes("chat_completion"));
  assert.ok(toolNames.includes("apply_blueprint_change"));
  assert.ok(toolNames.includes("generate_blueprint_change"));
  assert.ok(toolNames.includes("repair_blueprint_json"));
  assert.ok(toolNames.includes("orchestrate_modify_request"));
  assert.ok(toolNames.includes("validate_blueprint_json"));
  assert.ok(toolNames.includes("layout_blueprint_graph"));
  assert.ok(toolNames.includes("apply_blueprint_as_bsl"));
});

test("buildVisibleTools returns the modern BSL-only catalog for generic", () => {
  const visibleTools = buildVisibleTools(ALL_TOOLS, "generic") as Tool[];

  assert.deepEqual(
    visibleTools.map((tool: Tool) => tool.name),
    (buildVisibleTools(ALL_TOOLS, "codex") as Tool[]).map((tool: Tool) => tool.name),
  );
  assert.ok(!visibleTools.some((tool: Tool) => tool.name === "apply_blueprint_change"));
  assert.ok(!visibleTools.some((tool: Tool) => tool.name === "chat_completion"));
});

test("AssetDocument catalog exposes only generic AssetDocument tools", () => {
  const visibleTools = buildVisibleTools(ALL_TOOLS, "codex") as Tool[];
  const toolNames = visibleTools.map((tool: Tool) => tool.name);

  for (const toolName of [
    "inspect_asset_document_profile",
    "create_asset_document_template",
    "validate_asset_document",
    "diff_asset_document",
    "apply_asset_document",
  ]) {
    assert.ok(toolNames.includes(toolName), `${toolName} should be visible`);
  }

  for (const forbidden of [
    "inspect_anim_montage_document",
    "create_anim_montage_document",
    "diff_anim_montage_document",
    "apply_anim_montage_document",
  ]) {
    assert.ok(!toolNames.includes(forbidden), `${forbidden} should not be visible`);
  }
});

test("Codex blueprint descriptions prefer the BSL path", () => {
  const visibleTools = buildVisibleTools(ALL_TOOLS, "codex") as Tool[];
  const tool = visibleTools.find((entry: Tool) => entry.name === "apply_blueprint_as_bsl");

  assert.ok(tool);
  assert.match(tool.description ?? "", /Preferred Blueprint modification path/i);
  assert.equal((tool.description ?? "").match(/Preferred Blueprint modification path/gi)?.length, 1);
});

test("Codex blueprint description override does not prepend undefined when the source description is missing", () => {
  const visibleTools = buildVisibleTools([{ name: "apply_blueprint_as_bsl" }], "codex") as Tool[];

  assert.deepEqual(visibleTools, [
    {
      name: "apply_blueprint_as_bsl",
      description: "Preferred Blueprint modification path for Codex.",
    },
  ]);
});

test("non-Codex profiles do not leak Codex-specific BSL wording", () => {
  for (const profile of ["claude", "generic"] as const) {
    const tool = (buildVisibleTools(ALL_TOOLS, profile) as Tool[]).find(
      (entry: Tool) => entry.name === "apply_blueprint_as_bsl",
    );

    assert.ok(tool);
    assert.doesNotMatch(tool.description ?? "", /for Codex/i);
    assert.doesNotMatch(tool.description ?? "", /Preferred Blueprint modification path/i);
  }
});

test("assertToolAllowed rejects legacy blueprint tools for codex with a BSL hint", () => {
  assert.throws(
    () => assertToolAllowed("codex", "apply_blueprint_change"),
    /BSL blueprint tools/i,
  );
});

test("validateToolCatalogPolicy accepts the full tool list and rejects missing tools", () => {
  assert.doesNotThrow(() => validateToolCatalogPolicy(ALL_TOOLS.map((tool: Tool) => tool.name)));
  assert.throws(
    () =>
      validateToolCatalogPolicy(
        ALL_TOOLS.filter((tool: Tool) => tool.name !== "apply_blueprint_change").map(
          (tool: Tool) => tool.name,
        ),
      ),
    /unknown tool/i,
  );
});

test("validateToolCatalogPolicy rejects duplicate canonical tool names", () => {
  const names = ALL_TOOLS.map((tool: Tool) => tool.name);
  assert.throws(() => validateToolCatalogPolicy([...names, names[0]]), /duplicate tool/i);
});

test("validateToolCatalogPolicy rejects canonical tools outside any policy bucket", () => {
  const names = ALL_TOOLS.map((tool: Tool) => tool.name);
  assert.throws(() => validateToolCatalogPolicy([...names, "unclassified_tool"]), /unknown tool/i);
});
