#!/usr/bin/env node
import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { CallToolRequestSchema, ListToolsRequestSchema, } from "@modelcontextprotocol/sdk/types.js";
import { resolveClientProfile } from "./broker/clientProfile.js";
import { assertToolAllowed, buildVisibleTools, validateToolCatalogPolicy, } from "./broker/toolCatalog.js";
import { readFile } from "fs/promises";
import { platform, tmpdir } from "os";
import { join, dirname } from "path";
import { fileURLToPath } from "url";
import { execFile } from "child_process";
import { promisify } from "util";
import { randomUUID } from "crypto";
import { writeFile, unlink } from "fs/promises";
// Configuration
const UE_API_BASE = process.env.UE_API_BASE || "http://localhost:8559";
const API_PREFIX = "/assetfactory";
// Resolve schemas directory (relative to dist/index.js -> ../schemas/)
const __dirname = dirname(fileURLToPath(import.meta.url));
const SCHEMAS_DIR = join(__dirname, "..", "schemas");
const GENERATOR_ASSET_TYPES = [
    "Blueprint",
    "WidgetBlueprint",
    "StateTree",
    "BehaviorTree",
    "BlackboardData",
    "AnimSequence",
    "Material",
    "DataAsset",
    "DataTable",
    "CurveFloat",
    "CurveVector",
    "InputAction",
    "InputMappingContext",
    "GameplayTag",
];
const GENERATOR_ASSET_TYPE_LIST = GENERATOR_ASSET_TYPES.join(", ");
const SIDECAR_SCRIPT_DEFAULT = join(__dirname, "..", "assetfactory_mcp_server.py");
const SIDECAR_SCRIPT = process.env.UE_MCP_SIDECAR_SCRIPT || SIDECAR_SCRIPT_DEFAULT;
const DEFAULT_PYTHON_CMD = platform() === "win32" ? "py" : "python3";
const PYTHON_CMD = process.env.UE_MCP_PYTHON || DEFAULT_PYTHON_CMD;
const UE_EDITOR_CMD = process.env.UE_EDITOR_CMD || "";
const UE_PROJECT_PATH = process.env.UE_PROJECT_PATH || "";
const execFileAsync = promisify(execFile);
// Helper to make HTTP requests to UE
async function callUEApi(endpoint, method, body) {
    const url = `${UE_API_BASE}${API_PREFIX}${endpoint}`;
    const options = {
        method,
        headers: {
            "Content-Type": "application/json",
        },
    };
    if (body) {
        options.body = JSON.stringify(body);
    }
    try {
        const response = await fetch(url, options);
        const data = await response.json();
        return data;
    }
    catch (error) {
        return {
            success: false,
            error: `Failed to connect to UE API at ${url}: ${error}`,
        };
    }
}
async function callAssetDocumentApi(endpoint, method, body) {
    const url = `${UE_API_BASE}${API_PREFIX}${endpoint}`;
    const options = {
        method,
        headers: {
            "Content-Type": "application/json",
        },
    };
    if (body !== undefined) {
        options.body = JSON.stringify(body);
    }
    let response;
    try {
        response = await fetch(url, options);
    }
    catch (error) {
        throw new Error(`Failed to connect to UE AssetDocument API at ${url}: ${error}`);
    }
    const text = await response.text();
    let data;
    try {
        data = text.length > 0 ? JSON.parse(text) : {};
    }
    catch (error) {
        throw new Error(`UE AssetDocument API at ${url} returned invalid JSON: ${error}`);
    }
    if (!response.ok) {
        const detail = data && typeof data === "object" && "error" in data
            ? String(data.error)
            : response.statusText;
        throw new Error(`UE AssetDocument API ${method} ${endpoint} failed with HTTP ${response.status}: ${detail}`);
    }
    if (data && typeof data === "object" && data.success === false) {
        const detail = "error" in data
            ? String(data.error)
            : "AssetDocument request reported success=false";
        throw new Error(`UE AssetDocument API ${method} ${endpoint} failed: ${detail}`);
    }
    return data;
}
// Helper to load schema file
async function loadSchema(assetType) {
    try {
        const filePath = join(SCHEMAS_DIR, `${assetType}.md`);
        return await readFile(filePath, "utf-8");
    }
    catch {
        return `Schema not found for asset type: ${assetType}. Available types: ${GENERATOR_ASSET_TYPE_LIST}`;
    }
}
async function isEditorHttpAvailable() {
    try {
        const health = (await callUEApi("/health", "GET"));
        return health?.status === "ok" || health?.success === true;
    }
    catch {
        return false;
    }
}
function extractApplyResultFromStdout(stdout) {
    const lines = stdout.split(/\r?\n/).map((line) => line.trim()).filter((line) => line.length > 0);
    for (const line of lines.reverse()) {
        const marker = "AF_APPLY_RESULT:";
        const idx = line.indexOf(marker);
        if (idx >= 0) {
            const payload = line.slice(idx + marker.length).trim();
            if (payload.length > 0) {
                try {
                    return JSON.parse(payload);
                }
                catch {
                    return { success: false, error: "Failed to parse AF_APPLY_RESULT payload", raw: payload };
                }
            }
        }
    }
    return {
        success: false,
        error: "Offline apply commandlet finished but AF_APPLY_RESULT marker was not found in stdout",
        stdout_tail: lines.slice(-20).join("\n"),
    };
}
async function applyBlueprintChangeOffline(args) {
    if (!UE_EDITOR_CMD || !UE_PROJECT_PATH) {
        return {
            success: false,
            error: "Editor HTTP is unavailable and offline fallback is not configured. Please set UE_EDITOR_CMD and UE_PROJECT_PATH.",
        };
    }
    const requestPath = join(tmpdir(), `uecopilot_bp_apply_${randomUUID()}.json`);
    await writeFile(requestPath, args.blueprint_json ?? "", { encoding: "utf-8" });
    try {
        const cmdArgs = [
            UE_PROJECT_PATH,
            "-run=AssetFactoryApplyBlueprint",
            `-asset=${args.asset_path}`,
            `-json=${requestPath}`,
            args.merge === false ? "-nomerge" : "-merge",
            args.save_asset === false ? "-nosave" : "-save",
            "-unattended",
            "-nop4",
            "-nosplash",
            "-nullrhi",
            "-stdout",
            "-FullStdOutLogOutput",
        ];
        const { stdout, stderr } = await execFileAsync(UE_EDITOR_CMD, cmdArgs, { maxBuffer: 16 * 1024 * 1024 });
        if (stderr && stderr.trim().length > 0) {
            console.error(`[offline-apply-stderr] ${stderr.trim()}`);
        }
        return extractApplyResultFromStdout(stdout || "");
    }
    catch (error) {
        return {
            success: false,
            error: `Offline apply commandlet failed: ${error}`,
        };
    }
    finally {
        await unlink(requestPath).catch(() => undefined);
    }
}
async function callSidecarTool(toolName, args) {
    const requestPath = join(tmpdir(), `uecopilot_mcp_${randomUUID()}.json`);
    const req = {
        jsonrpc: "2.0",
        id: 1,
        method: "tools/call",
        params: {
            name: toolName,
            arguments: args,
        },
    };
    await writeFile(requestPath, JSON.stringify(req), { encoding: "utf-8" });
    try {
        const { stdout, stderr } = await execFileAsync(PYTHON_CMD, ["-u", SIDECAR_SCRIPT, "--request", requestPath]);
        if (stderr && stderr.trim().length > 0) {
            // Keep stderr as debug hint only; response parsing below is authoritative.
            console.error(`[sidecar-stderr] ${stderr.trim()}`);
        }
        const raw = stdout.trim();
        const parsed = JSON.parse(raw);
        if (parsed.error) {
            throw new Error(parsed.error.message || "Unknown sidecar error");
        }
        const content = parsed.result?.content;
        if (typeof content !== "string") {
            throw new Error("Sidecar response missing result.content");
        }
        return content;
    }
    catch (error) {
        throw new Error(`Sidecar call failed for ${toolName}: ${error}`);
    }
    finally {
        await unlink(requestPath).catch(() => undefined);
    }
}
// Define available tools
const tools = [
    {
        name: "generate_assets",
        description: `Generate Unreal Engine assets from JSON configuration. Supports ${GENERATOR_ASSET_TYPE_LIST}. Blueprint covers arbitrary UObject-derived parent classes such as Actor, Character, GameplayEffect, GameplayAbility, AnimInstance, and BTTaskNode. Supports Create, Update (field-level patch: only JSON-present fields are modified, missing fields are preserved), and CreateOrUpdate actions. IMPORTANT: Call get_generator_schema first to get the correct JSON field names and formats for the asset type you want to generate. IMPORTANT for WidgetBlueprint Update: use 'WidgetUpdates' array for safe incremental changes; do NOT use 'RootWidget' in Update mode unless you intend to destroy and fully rebuild the widget tree (requires '"RebuildTree": true').`,
        inputSchema: {
            type: "object",
            properties: {
                assets: {
                    type: "array",
                    description: "Array of asset configurations to generate",
                    items: {
                        type: "object",
                        properties: {
                            AssetType: {
                                type: "string",
                                description: `Type of asset: ${GENERATOR_ASSET_TYPE_LIST}. Blueprint covers arbitrary UObject-derived parent classes such as Actor, Character, GameplayEffect, GameplayAbility, AnimInstance, and BTTaskNode.`,
                            },
                            Name: {
                                type: "string",
                                description: "Name of the asset",
                            },
                            Path: {
                                type: "string",
                                description: "Path where the asset will be created (e.g., /Game/Blueprints)",
                            },
                            Action: {
                                type: "string",
                                enum: ["Create", "Update", "CreateOrUpdate"],
                                description: "Action to perform. Default is CreateOrUpdate. Use 'Update' for field-level patch: only fields present in JSON are modified, missing fields are preserved unchanged.",
                            },
                        },
                        required: ["AssetType", "Name", "Path"],
                    },
                },
            },
            required: ["assets"],
        },
    },
    {
        name: "get_generator_schema",
        description: `Get the JSON schema documentation for a specific asset generator. Available types: ${GENERATOR_ASSET_TYPE_LIST}. Returns field names, types, formats, and examples. Always call this before generate_assets to ensure correct field usage.`,
        inputSchema: {
            type: "object",
            properties: {
                asset_type: {
                    type: "string",
                    description: `The asset type to get schema for: ${GENERATOR_ASSET_TYPE_LIST}`,
                    enum: [...GENERATOR_ASSET_TYPES],
                },
            },
            required: ["asset_type"],
        },
    },
    {
        name: "get_asset_document_schema",
        description: "Read AssetDocument schema documentation, including sidecar Target rules, Properties forms, and inspect/extract/validate/diff workflows.",
        inputSchema: {
            type: "object",
            properties: {},
        },
    },
    {
        name: "inspect_asset_document_target",
        description: "Inspect writable reflected properties for an AssetDocument target class or existing asset by calling /assetfactory/assetdocument/inspect.",
        inputSchema: {
            type: "object",
            properties: {
                class_or_asset: {
                    type: "string",
                    description: "Class name/path or asset path to inspect (e.g., TestDataAsset, /Script/AssetFactory.TestDataAsset, or /Game/Data/DA_Test)",
                },
            },
            required: ["class_or_asset"],
        },
    },
    {
        name: "inspect_asset_document_profile",
        description: "Inspect the generic AssetDocument profile for a class or existing asset by calling /assetfactory/assetdocument/profile.",
        inputSchema: {
            type: "object",
            properties: {
                class_or_asset: {
                    type: "string",
                    description: "Class name/path or asset path to inspect (e.g., TestDataAsset, /Script/AssetFactory.TestDataAsset, or /Game/Data/DA_Test)",
                },
            },
            required: ["class_or_asset"],
        },
    },
    {
        name: "create_asset_document_template",
        description: "Create a generic AssetDocument template for a class and target by calling /assetfactory/assetdocument/template.",
        inputSchema: {
            type: "object",
            properties: {
                class: {
                    type: "string",
                    description: "Class name/path for the AssetDocument template (e.g., /Script/AssetFactory.TestDataAsset).",
                },
                target: {
                    type: "string",
                    description: "Target asset path for the template (e.g., /Game/Data/DA_Test).",
                },
            },
            required: ["class", "target"],
        },
    },
    {
        name: "validate_asset_document",
        description: "Validate exactly one AssetDocument JSON document or sidecar file by calling /assetfactory/assetdocument/validate.",
        inputSchema: {
            type: "object",
            properties: {
                document: {
                    type: "object",
                    description: "Inline AssetDocument JSON. When provided, this object is sent directly as the HTTP request body.",
                    additionalProperties: true,
                },
                file_path: {
                    type: "string",
                    description: "Absolute sidecar file path to validate. Use this instead of document when validating a file.",
                },
            },
        },
    },
    {
        name: "diff_asset_document",
        description: "Diff exactly one AssetDocument JSON document or sidecar file against the current asset by calling /assetfactory/assetdocument/diff.",
        inputSchema: {
            type: "object",
            properties: {
                document: {
                    type: "object",
                    description: "Inline AssetDocument JSON. When provided, this object is sent directly as the HTTP request body.",
                    additionalProperties: true,
                },
                file_path: {
                    type: "string",
                    description: "Absolute sidecar file path to diff. Use this instead of document when diffing a file.",
                },
            },
        },
    },
    {
        name: "extract_asset_document",
        description: "Extract an existing asset as an AssetDocument JSON draft by calling /assetfactory/assetdocument/extract.",
        inputSchema: {
            type: "object",
            properties: {
                asset_path: {
                    type: "string",
                    description: "Asset path to extract (e.g., /Game/Data/DA_Test)",
                },
                diff_only: {
                    type: "boolean",
                    description: "If true, include only properties that differ from defaults (default true).",
                },
                include_all_writable: {
                    type: "boolean",
                    description: "If true, include all writable reflected properties.",
                },
            },
            required: ["asset_path"],
        },
    },
    {
        name: "apply_asset_document",
        description: "Apply an inline AssetDocument JSON document by calling /assetfactory/assetdocument/apply. This tool does not route through generate_assets.",
        inputSchema: {
            type: "object",
            properties: {
                document: {
                    type: "object",
                    description: "Inline AssetDocument JSON to apply. The document object is sent directly as the HTTP request body.",
                    additionalProperties: true,
                },
            },
            required: ["document"],
        },
    },
    {
        name: "apply_asset_document_file",
        description: "Apply an AssetDocument sidecar file by calling /assetfactory/assetdocument/apply-file. Use validate_asset_document or diff_asset_document first when you need a preflight check.",
        inputSchema: {
            type: "object",
            properties: {
                file_path: {
                    type: "string",
                    description: "Absolute sidecar file path to apply. The sidecar Target must match the path-derived /Game asset target.",
                },
                save_asset: {
                    type: "boolean",
                    description: "Save the asset after applying (default true).",
                },
            },
            required: ["file_path"],
        },
    },
    {
        name: "extract_assets",
        description: "Extract Unreal Engine asset configurations as JSON. Use this to understand existing asset structure before modifying.",
        inputSchema: {
            type: "object",
            properties: {
                assets: {
                    type: "array",
                    description: "Array of asset paths to extract (e.g., /Game/Blueprints/BP_Player)",
                    items: {
                        type: "string",
                    },
                },
                diffOnly: {
                    type: "boolean",
                    description: "If true, only extract properties that differ from defaults",
                },
            },
            required: ["assets"],
        },
    },
    {
        name: "delete_assets",
        description: "Delete Unreal Engine assets",
        inputSchema: {
            type: "object",
            properties: {
                assets: {
                    type: "array",
                    description: "Array of asset paths to delete (e.g., /Game/Blueprints/BP_Old)",
                    items: {
                        type: "string",
                    },
                },
            },
            required: ["assets"],
        },
    },
    {
        name: "query_asset",
        description: "Query specific properties from an extracted asset using JSON path syntax. Supports Array[0], Array[*], and Parent.Child notation.",
        inputSchema: {
            type: "object",
            properties: {
                asset: {
                    type: "string",
                    description: "Asset path to query (e.g., /Game/Blueprints/BP_Player)",
                },
                path: {
                    type: "string",
                    description: "JSON path to query (e.g., Components[0].Properties, RootWidget.Children[*].Type)",
                },
            },
            required: ["asset", "path"],
        },
    },
    {
        name: "list_generators",
        description: "List all available asset generators and their supported types",
        inputSchema: {
            type: "object",
            properties: {},
        },
    },
    {
        name: "health_check",
        description: "Check if the Unreal Engine AssetFactory HTTP server is running",
        inputSchema: {
            type: "object",
            properties: {},
        },
    },
    {
        name: "get_editor_context",
        description: "Get the current Unreal Editor state: selected actors (with transform), selected content browser assets, current level, open asset editors, PIE status, and active editor modes. Use this to understand what the user is currently working on.",
        inputSchema: {
            type: "object",
            properties: {},
        },
    },
    {
        name: "execute_python",
        description: "Execute Python code in the Unreal Editor via PythonScriptPlugin. Supports multi-line scripts with imports. Wrapped in an undo transaction for safety. Use this for operations not covered by structured generators (e.g., renaming, bulk edits, editor automation).",
        inputSchema: {
            type: "object",
            properties: {
                code: {
                    type: "string",
                    description: "Python code to execute. Supports multi-line with imports (e.g., 'import unreal; unreal.log(\"hello\")')",
                },
                description: {
                    type: "string",
                    description: "Optional description for the undo history entry (shown in Edit > Undo)",
                },
            },
            required: ["code"],
        },
    },
    {
        name: "apply_blueprint_change",
        description: "Apply blueprint logic JSON directly to an existing Blueprint asset in Unreal Editor using UAIBlueprintFactory::ModifyBlueprint, then mark dirty and save.",
        inputSchema: {
            type: "object",
            properties: {
                asset_path: {
                    type: "string",
                    description: "Blueprint object path (e.g., /Game/Blueprints/BP_HelloWorld.BP_HelloWorld or /Game/Blueprints/BP_HelloWorld)",
                },
                blueprint_json: {
                    type: "string",
                    description: "Blueprint logic JSON to parse and apply",
                },
                merge: {
                    type: "boolean",
                    description: "Merge with existing graph if true; replace behavior if false",
                },
                save_asset: {
                    type: "boolean",
                    description: "Save asset after apply (default true)",
                },
            },
            required: ["asset_path", "blueprint_json"],
        },
    },
    {
        name: "update_datatable_rows",
        description: "Update specific rows in an existing DataTable without rewriting the entire CSV. Supports adding new rows, updating fields of existing rows, and deleting rows by name. Internally extracts the current CSV, applies changes, and regenerates the DataTable.",
        inputSchema: {
            type: "object",
            properties: {
                asset: {
                    type: "string",
                    description: "Asset path of the existing DataTable (e.g., /Game/Data/DT_Heroes)",
                },
                rows: {
                    type: "object",
                    description: "Map of RowName → {FieldName: Value} to add or update. If the row exists, only specified fields are modified. If the row doesn't exist, a new row is added with defaults for unspecified fields. Values should be simple types (number, string, bool). For complex UE types (FVector, FLinearColor, etc.), provide the UE text format as a string, e.g. \"(X=1,Y=2,Z=3)\".",
                    additionalProperties: {
                        type: "object",
                    },
                },
                deleteRows: {
                    type: "array",
                    description: "Array of row names to delete from the DataTable",
                    items: { type: "string" },
                },
            },
            required: ["asset"],
        },
    },
    {
        name: "chat_completion",
        description: "Call model chat completion via blueprint logic sidecar.",
        inputSchema: {
            type: "object",
            properties: {
                endpoint: { type: "string" },
                api_key: { type: "string" },
                model: { type: "string" },
                timeout_seconds: { type: "number" },
                max_retries: { type: "number" },
                max_tokens: { type: "number" },
                temperature: { type: "number" },
                messages: { type: "array", items: { type: "object" } },
            },
            required: ["endpoint", "model", "messages"],
        },
    },
    {
        name: "generate_blueprint_change",
        description: "Generate blueprint change JSON via sidecar AI request.",
        inputSchema: {
            type: "object",
            properties: {
                endpoint: { type: "string" },
                api_key: { type: "string" },
                model: { type: "string" },
                timeout_seconds: { type: "number" },
                max_retries: { type: "number" },
                max_tokens: { type: "number" },
                temperature: { type: "number" },
                messages: { type: "array", items: { type: "object" } },
            },
            required: ["endpoint", "model", "messages"],
        },
    },
    {
        name: "repair_blueprint_json",
        description: "Repair malformed blueprint JSON into one valid object.",
        inputSchema: {
            type: "object",
            properties: {
                endpoint: { type: "string" },
                api_key: { type: "string" },
                model: { type: "string" },
                timeout_seconds: { type: "number" },
                max_retries: { type: "number" },
                max_tokens: { type: "number" },
                temperature: { type: "number" },
                source_text: { type: "string" },
            },
            required: ["endpoint", "model", "source_text"],
        },
    },
    {
        name: "orchestrate_modify_request",
        description: "Generate, auto-repair, and layout blueprint JSON for /modify flow.",
        inputSchema: {
            type: "object",
            properties: {
                endpoint: { type: "string" },
                api_key: { type: "string" },
                model: { type: "string" },
                timeout_seconds: { type: "number" },
                max_retries: { type: "number" },
                max_tokens: { type: "number" },
                temperature: { type: "number" },
                apply_layout: { type: "boolean" },
                messages: { type: "array", items: { type: "object" } },
            },
            required: ["endpoint", "model", "messages"],
        },
    },
    {
        name: "validate_blueprint_json",
        description: "Validate blueprint JSON structure.",
        inputSchema: {
            type: "object",
            properties: {
                blueprint_json: { type: "string" },
            },
            required: ["blueprint_json"],
        },
    },
    {
        name: "layout_blueprint_graph",
        description: "Apply deterministic layout positions to blueprint nodes.",
        inputSchema: {
            type: "object",
            properties: {
                blueprint_json: { type: "string" },
                horizontal_spacing: { type: "number" },
                vertical_spacing: { type: "number" },
                start_x: { type: "number" },
                start_y: { type: "number" },
                graph_gap_y: { type: "number" },
            },
            required: ["blueprint_json"],
        },
    },
    {
        name: "get_viewport_screenshot",
        description: "Capture the current Level Editor Viewport as a JPEG image. Returns the screenshot so you can visually analyze the scene, inspect actor placement, check materials/lighting, or understand the current state of the level. The image is downsampled to max 1280px on the longest side.",
        inputSchema: {
            type: "object",
            properties: {},
        },
    },
    {
        name: "extract_blueprint_as_bsl",
        description: "Decompile an existing Unreal Engine Blueprint asset into BSL (Blueprint Script Language) text. Returns the BSL source that represents the Blueprint's event graph and functions. Useful for reading, understanding, or editing Blueprint logic as text.",
        inputSchema: {
            type: "object",
            properties: {
                assets: {
                    type: "array",
                    description: "Array of Blueprint asset paths to decompile (e.g., /Game/Blueprints/BP_Player)",
                    items: {
                        type: "string",
                    },
                },
            },
            required: ["assets"],
        },
    },
    {
        name: "apply_blueprint_as_bsl",
        description: "Compile BSL (Blueprint Script Language) source code and apply it to an existing Unreal Engine Blueprint asset. BSL is a human-readable scripting language that compiles to Blueprint graphs. Use extract_blueprint_as_bsl first to see existing BSL syntax, then modify and apply back.",
        inputSchema: {
            type: "object",
            properties: {
                asset: {
                    type: "string",
                    description: "Blueprint asset path (e.g., /Game/Blueprints/BP_Player or /Game/Blueprints/BP_Player.BP_Player)",
                },
                bsl: {
                    type: "string",
                    description: "BSL source code to compile and apply",
                },
                merge: {
                    type: "boolean",
                    description: "Merge with existing graph (true) or replace (false, default)",
                },
                save: {
                    type: "boolean",
                    description: "Save the asset after applying (default true)",
                },
            },
            required: ["asset", "bsl"],
        },
    },
    {
        name: "extract_blueprint_graph",
        description: "Export a Blueprint's node graph structure as JSON. Returns all nodes with their pins, connections, and default values. Useful for inspecting and debugging Blueprint graph structure.",
        inputSchema: {
            type: "object",
            properties: {
                asset: {
                    type: "string",
                    description: "Blueprint asset path (e.g., /Game/Blueprints/BP_Player)",
                },
            },
            required: ["asset"],
        },
    },
];
// Create MCP server
const server = new Server({
    name: "ue-copilot",
    version: "1.1.0",
}, {
    capabilities: {
        tools: {},
    },
});
validateToolCatalogPolicy(tools.map((tool) => tool.name));
// Handle list tools request
server.setRequestHandler(ListToolsRequestSchema, async () => {
    const profile = resolveClientProfile(server.getClientVersion());
    return { tools: buildVisibleTools(tools, profile) };
});
const applyBlueprintChangeHandler = async (rawArgs) => {
    const a = rawArgs || { asset_path: "", blueprint_json: "" };
    const merge = a.merge ?? true;
    const saveAsset = a.save_asset ?? true;
    const escapedAssetPath = JSON.stringify(a.asset_path ?? "");
    const escapedBlueprintJson = JSON.stringify(a.blueprint_json ?? "");
    const editorOnline = await isEditorHttpAvailable();
    if (!editorOnline) {
        return applyBlueprintChangeOffline({
            asset_path: a.asset_path,
            blueprint_json: a.blueprint_json,
            merge,
            save_asset: saveAsset,
        });
    }
    const pythonCode = `
import json
import unreal

asset_path = ${escapedAssetPath}
bp_json = ${escapedBlueprintJson}
merge = ${merge ? "True" : "False"}
save_asset = ${saveAsset ? "True" : "False"}

def _load_blueprint(path: str):
    obj = unreal.load_object(None, path)
    if obj:
        return obj
    if "." not in path:
        name = path.split("/")[-1]
        obj = unreal.load_object(None, f"{path}.{name}")
        if obj:
            return obj
    return None

bp = _load_blueprint(asset_path)
if not bp:
    raise RuntimeError(f"Blueprint not found: {asset_path}")

parser_cls = getattr(unreal, "BlueprintJSONParser", None)
factory_cls = getattr(unreal, "AIBlueprintFactory", None)
if parser_cls is None or factory_cls is None:
    raise RuntimeError("BlueprintJSONParser/AIBlueprintFactory is not exposed to Python")

parse_result = parser_cls.parse_blueprint_json(bp_json)
if not getattr(parse_result, "b_success", False):
    err = getattr(parse_result, "error_message", "unknown parse error")
    raise RuntimeError(f"ParseBlueprintJSON failed: {err}")

bp_data = getattr(parse_result, "blueprint_data", None)
if bp_data is None:
    raise RuntimeError("Parse result missing blueprint_data")

apply_result = factory_cls.modify_blueprint(bp, bp_data, merge)
if not getattr(apply_result, "b_success", False):
    err = getattr(apply_result, "error_message", "unknown apply error")
    raise RuntimeError(f"ModifyBlueprint failed: {err}")

bp.mark_package_dirty()

saved = False
if save_asset:
    try:
        saved = unreal.EditorAssetLibrary.save_loaded_asset(bp)
    except Exception:
        saved = False

summary = {
    "success": True,
    "asset_path": asset_path,
    "saved": bool(saved),
    "warnings": list(getattr(apply_result, "warnings", [])),
}
print(json.dumps(summary, ensure_ascii=False))
`;
    return callUEApi("/execute", "POST", {
        Code: pythonCode,
        Description: "Apply Blueprint Change via MCP",
    });
};
const viewportScreenshotHandler = async () => {
    const result = (await callUEApi("/screenshot", "GET"));
    if (!result?.success || !result.data) {
        const errMsg = result?.error ?? "Screenshot failed (no data returned)";
        return {
            content: [{ type: "text", text: `Error: ${errMsg}` }],
            isError: true,
        };
    }
    return {
        content: [
            {
                type: "image",
                data: result.data,
                mimeType: result.mimeType ?? "image/jpeg",
            },
            {
                type: "text",
                text: `Viewport screenshot captured: ${result.width ?? "?"}x${result.height ?? "?"} px`,
            },
        ],
    };
};
function requireStringArg(args, key) {
    const value = args[key];
    if (typeof value !== "string" || value.length === 0) {
        throw new Error(`'${key}' is required.`);
    }
    return value;
}
function requireObjectArg(args, key) {
    const value = args[key];
    if (!value || typeof value !== "object" || Array.isArray(value)) {
        throw new Error(`'${key}' is required.`);
    }
    return value;
}
function assetDocumentBodyArg(args) {
    const hasDocument = args.document !== undefined;
    const hasFilePath = typeof args.file_path === "string" && args.file_path.length > 0;
    if (hasDocument === hasFilePath) {
        throw new Error("Exactly one of 'document' or 'file_path' is required.");
    }
    if (hasDocument) {
        return requireObjectArg(args, "document");
    }
    return { file_path: args.file_path };
}
const toolHandlers = {
    generate_assets: async (args) => callUEApi("/generate", "POST", { Assets: args.assets }),
    get_generator_schema: async (args) => loadSchema(args.asset_type),
    get_asset_document_schema: async () => loadSchema("AssetDocument"),
    inspect_asset_document_target: async (args) => {
        const classOrAsset = requireStringArg(args, "class_or_asset");
        return callAssetDocumentApi(`/assetdocument/inspect?class_or_asset=${encodeURIComponent(classOrAsset)}`, "GET");
    },
    inspect_asset_document_profile: async (args) => {
        const classOrAsset = requireStringArg(args, "class_or_asset");
        return callAssetDocumentApi(`/assetdocument/profile?class_or_asset=${encodeURIComponent(classOrAsset)}`, "GET");
    },
    create_asset_document_template: async (args) => callAssetDocumentApi("/assetdocument/template", "POST", {
        Class: requireStringArg(args, "class"),
        Target: requireStringArg(args, "target"),
    }),
    validate_asset_document: async (args) => callAssetDocumentApi("/assetdocument/validate", "POST", assetDocumentBodyArg(args)),
    diff_asset_document: async (args) => callAssetDocumentApi("/assetdocument/diff", "POST", assetDocumentBodyArg(args)),
    extract_asset_document: async (args) => callAssetDocumentApi("/assetdocument/extract", "POST", {
        asset_path: requireStringArg(args, "asset_path"),
        diff_only: args.diff_only ?? true,
        include_all_writable: args.include_all_writable ?? false,
    }),
    apply_asset_document: async (args) => callAssetDocumentApi("/assetdocument/apply", "POST", requireObjectArg(args, "document")),
    apply_asset_document_file: async (args) => callAssetDocumentApi("/assetdocument/apply-file", "POST", {
        file_path: requireStringArg(args, "file_path"),
        save_asset: args.save_asset ?? true,
    }),
    extract_assets: async (args) => callUEApi("/extract", "POST", {
        Assets: args.assets,
        DiffOnly: args.diffOnly ?? true,
    }),
    delete_assets: async (args) => callUEApi("/delete", "POST", { Assets: args.assets }),
    query_asset: async (args) => callUEApi("/query", "POST", {
        Asset: args.asset,
        Path: args.path,
    }),
    list_generators: async () => callUEApi("/generators", "GET"),
    health_check: async () => callUEApi("/health", "GET"),
    get_editor_context: async () => callUEApi("/context", "GET"),
    execute_python: async (args) => callUEApi("/execute", "POST", {
        Code: args.code,
        Description: args.description,
    }),
    apply_blueprint_change: applyBlueprintChangeHandler,
    get_viewport_screenshot: viewportScreenshotHandler,
    update_datatable_rows: async (args) => {
        const { asset, rows: rowUpdates, deleteRows, } = args;
        if (!rowUpdates && !deleteRows) {
            throw new Error("At least one of 'rows' or 'deleteRows' must be provided.");
        }
        return callUEApi("/datatable/rows", "POST", {
            Asset: asset,
            Rows: rowUpdates,
            DeleteRows: deleteRows,
        });
    },
    extract_blueprint_as_bsl: async (args) => {
        const { assets } = args;
        if (!assets || assets.length === 0) {
            throw new Error("'assets' must be a non-empty array of Blueprint paths.");
        }
        // Decompile each asset and collect results
        const rawResults = await Promise.all(assets.map((assetPath) => callUEApi("/extract_bsl", "POST", { Asset: assetPath })));
        // Format output: each asset gets a section with path header + BSL block
        // Use assets[i] as fallback for r.asset in case of connection failure (callUEApi
        // returns {success:false, error:"..."} without an 'asset' field when offline)
        const parts = rawResults.map((raw, i) => {
            const r = raw;
            const displayPath = r.asset ?? assets[i];
            const header = `// === ${displayPath} ===`;
            if (!r.success) {
                const errList = (r.errors ?? (r.error ? [r.error] : [])).join("\n");
                return `${header}\n// Decompile failed:\n${errList}`;
            }
            const warnBlock = r.warnings && r.warnings.length > 0
                ? r.warnings.map((w) => `// WARNING: ${w}`).join("\n") + "\n"
                : "";
            return `${header}\n${warnBlock}${r.bsl ?? ""}`;
        });
        return parts.join("\n\n");
    },
    apply_blueprint_as_bsl: async (args) => {
        const { asset, bsl, merge, save } = args;
        if (!asset || !bsl) {
            throw new Error("'asset' and 'bsl' are required.");
        }
        return callUEApi("/apply_bsl", "POST", {
            Asset: asset,
            BSL: bsl,
            Merge: merge ?? false,
            Save: save ?? true,
        });
    },
    extract_blueprint_graph: async (args) => {
        const { asset } = args;
        if (!asset) {
            throw new Error("'asset' is required.");
        }
        return callUEApi("/extract_graph", "POST", { Asset: asset });
    },
};
for (const sidecarToolName of [
    "chat_completion",
    "generate_blueprint_change",
    "repair_blueprint_json",
    "orchestrate_modify_request",
    "validate_blueprint_json",
    "layout_blueprint_graph",
]) {
    toolHandlers[sidecarToolName] = async (args) => callSidecarTool(sidecarToolName, args);
}
// Handle tool calls
server.setRequestHandler(CallToolRequestSchema, async (request) => {
    const { name, arguments: args } = request.params;
    try {
        const profile = resolveClientProfile(server.getClientVersion());
        assertToolAllowed(profile, name);
        const handler = toolHandlers[name];
        if (!handler) {
            return {
                content: [
                    {
                        type: "text",
                        text: `Unknown tool: ${name}`,
                    },
                ],
                isError: true,
            };
        }
        const result = await handler((args || {}));
        // If the handler already returned a full MCP response (has a content array), pass it through directly
        if (result && typeof result === "object" && Array.isArray(result.content)) {
            return result;
        }
        return {
            content: [
                {
                    type: "text",
                    text: typeof result === "string" ? result : JSON.stringify(result, null, 2),
                },
            ],
        };
    }
    catch (error) {
        return {
            content: [
                {
                    type: "text",
                    text: `Error: ${error}`,
                },
            ],
            isError: true,
        };
    }
});
// Start server
async function main() {
    const transport = new StdioServerTransport();
    await server.connect(transport);
    console.error("UE Copilot MCP Server running on stdio");
}
main().catch(console.error);
