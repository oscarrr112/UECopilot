const SHARED_ASSET_TOOL_NAMES = new Set([
    "health_check",
    "list_generators",
    "get_generator_schema",
    "generate_assets",
    "get_asset_document_schema",
    "inspect_asset_document_target",
    "validate_asset_document",
    "diff_asset_document",
    "extract_asset_document",
    "apply_asset_document",
    "apply_asset_document_file",
    "extract_assets",
    "query_asset",
    "delete_assets",
    "update_datatable_rows",
]);
const SHARED_EDITOR_TOOL_NAMES = new Set([
    "get_editor_context",
    "execute_python",
    "get_viewport_screenshot",
]);
const CLAUDE_SIDECAR_COMPAT_TOOL_NAMES = new Set([
    "chat_completion",
]);
const BSL_BLUEPRINT_TOOL_NAMES = new Set([
    "extract_blueprint_graph",
    "extract_blueprint_as_bsl",
    "apply_blueprint_as_bsl",
]);
const LEGACY_BLUEPRINT_TOOL_NAMES = new Set([
    "apply_blueprint_change",
    "generate_blueprint_change",
    "repair_blueprint_json",
    "orchestrate_modify_request",
    "validate_blueprint_json",
    "layout_blueprint_graph",
]);
const CODEX_BSL_OVERRIDE = "Preferred Blueprint modification path for Codex.";
const CLASSIFIED_TOOL_NAMES = new Set([
    ...SHARED_ASSET_TOOL_NAMES,
    ...SHARED_EDITOR_TOOL_NAMES,
    ...CLAUDE_SIDECAR_COMPAT_TOOL_NAMES,
    ...BSL_BLUEPRINT_TOOL_NAMES,
    ...LEGACY_BLUEPRINT_TOOL_NAMES,
]);
export const ALL_TOOLS = [
    { name: "health_check", description: "Check broker health." },
    { name: "list_generators", description: "List available asset generators." },
    { name: "get_generator_schema", description: "Inspect a generator schema." },
    { name: "generate_assets", description: "Generate assets from a selected generator." },
    { name: "get_asset_document_schema", description: "Read AssetDocument schema documentation." },
    { name: "inspect_asset_document_target", description: "Inspect an AssetDocument target class or asset." },
    { name: "validate_asset_document", description: "Validate an AssetDocument document or sidecar file." },
    { name: "diff_asset_document", description: "Diff an AssetDocument document or sidecar file." },
    { name: "extract_asset_document", description: "Extract an asset as an AssetDocument draft." },
    { name: "apply_asset_document", description: "Apply an inline AssetDocument document." },
    { name: "apply_asset_document_file", description: "Apply an AssetDocument sidecar file." },
    { name: "extract_assets", description: "Extract assets from the project." },
    { name: "query_asset", description: "Query a single asset." },
    { name: "delete_assets", description: "Delete selected assets." },
    { name: "update_datatable_rows", description: "Update datatable rows." },
    { name: "get_editor_context", description: "Inspect the Unreal editor context." },
    { name: "execute_python", description: "Execute Python in the Unreal editor." },
    { name: "chat_completion", description: "Call model chat completion via blueprint logic sidecar." },
    { name: "get_viewport_screenshot", description: "Capture a viewport screenshot." },
    { name: "extract_blueprint_graph", description: "Extract a blueprint graph." },
    { name: "extract_blueprint_as_bsl", description: "Extract a blueprint as BSL." },
    { name: "apply_blueprint_as_bsl", description: "Apply a blueprint change as BSL." },
    { name: "apply_blueprint_change", description: "Apply a legacy JSON blueprint change." },
    { name: "generate_blueprint_change", description: "Generate a legacy JSON blueprint change." },
    { name: "repair_blueprint_json", description: "Repair legacy blueprint JSON." },
    { name: "orchestrate_modify_request", description: "Orchestrate a legacy modify request." },
    { name: "validate_blueprint_json", description: "Validate legacy blueprint JSON." },
    { name: "layout_blueprint_graph", description: "Layout a legacy blueprint graph." },
];
const ALL_TOOL_NAMES = new Set(ALL_TOOLS.map((tool) => tool.name));
function isVisibleToProfile(profile, toolName) {
    if (SHARED_ASSET_TOOL_NAMES.has(toolName) || SHARED_EDITOR_TOOL_NAMES.has(toolName)) {
        return true;
    }
    if (BSL_BLUEPRINT_TOOL_NAMES.has(toolName)) {
        return true;
    }
    if (CLAUDE_SIDECAR_COMPAT_TOOL_NAMES.has(toolName)) {
        return profile === "claude";
    }
    if (LEGACY_BLUEPRINT_TOOL_NAMES.has(toolName)) {
        return profile === "claude";
    }
    return false;
}
function formatLegacyBlueprintGuidance(toolName) {
    return `${toolName} is legacy JSON blueprint tooling. Use BSL blueprint tools instead.`;
}
export function buildVisibleTools(allTools, profile) {
    return allTools
        .filter((tool) => isVisibleToProfile(profile, tool.name))
        .map((tool) => {
        if (profile === "codex" && BSL_BLUEPRINT_TOOL_NAMES.has(tool.name)) {
            return {
                ...tool,
                description: [tool.description, CODEX_BSL_OVERRIDE].filter(Boolean).join(" "),
            };
        }
        return { ...tool };
    });
}
export function assertToolAllowed(profile, toolName) {
    if (!ALL_TOOL_NAMES.has(toolName)) {
        throw new Error(`unknown tool: ${toolName}`);
    }
    if (isVisibleToProfile(profile, toolName)) {
        return;
    }
    if (LEGACY_BLUEPRINT_TOOL_NAMES.has(toolName) && profile === "codex") {
        throw new Error(`${formatLegacyBlueprintGuidance(toolName)} Preferred Blueprint modification path.`);
    }
    throw new Error(`unknown tool: ${toolName}`);
}
export function validateToolCatalogPolicy(allToolNames) {
    const provided = new Set();
    for (const toolName of allToolNames) {
        if (provided.has(toolName)) {
            throw new Error(`duplicate tool: ${toolName}`);
        }
        provided.add(toolName);
        if (!ALL_TOOL_NAMES.has(toolName)) {
            throw new Error(`unknown tool: ${toolName}`);
        }
        if (!CLASSIFIED_TOOL_NAMES.has(toolName)) {
            throw new Error(`unknown tool: ${toolName}`);
        }
    }
    for (const toolName of ALL_TOOL_NAMES) {
        if (!provided.has(toolName)) {
            throw new Error(`unknown tool: ${toolName}`);
        }
    }
}
