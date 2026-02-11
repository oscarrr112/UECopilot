#!/usr/bin/env node
import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { CallToolRequestSchema, ListToolsRequestSchema, } from "@modelcontextprotocol/sdk/types.js";
import { readFile } from "fs/promises";
import { tmpdir } from "os";
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
const SIDECAR_SCRIPT_DEFAULT = join(__dirname, "..", "assetfactory_mcp_server.py");
const SIDECAR_SCRIPT = process.env.UE_MCP_SIDECAR_SCRIPT || SIDECAR_SCRIPT_DEFAULT;
const PYTHON_CMD = process.env.UE_MCP_PYTHON || "py";
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
// Helper to load schema file
async function loadSchema(assetType) {
    try {
        const filePath = join(SCHEMAS_DIR, `${assetType}.md`);
        return await readFile(filePath, "utf-8");
    }
    catch {
        return `Schema not found for asset type: ${assetType}. Available types: Blueprint, WidgetBlueprint, Material, DataAsset, DataTable, CurveFloat, CurveVector, InputAction, InputMappingContext, GameplayTag`;
    }
}
async function isEditorHttpAvailable() {
    try {
        const health = (await callUEApi("/health", "GET"));
        return health?.success === true;
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
        description: "Generate Unreal Engine assets from JSON configuration. Supports Blueprint, WidgetBlueprint, DataAsset, DataTable, Material, CurveFloat, CurveVector, InputAction, InputMappingContext, GameplayTag. IMPORTANT: Call get_generator_schema first to get the correct JSON field names and formats for the asset type you want to generate.",
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
                                description: "Type of asset: Blueprint, WidgetBlueprint, DataAsset, DataTable, Material, CurveFloat, CurveVector, InputAction, InputMappingContext, GameplayTag",
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
                                description: "Action to perform. Default is CreateOrUpdate",
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
        description: "Get the JSON schema documentation for a specific asset generator. Returns field names, types, formats, and examples. Always call this before generate_assets to ensure correct field usage.",
        inputSchema: {
            type: "object",
            properties: {
                asset_type: {
                    type: "string",
                    description: "The asset type to get schema for: Blueprint, WidgetBlueprint, Material, DataAsset, DataTable, CurveFloat, CurveVector, InputAction, InputMappingContext, GameplayTag",
                    enum: [
                        "Blueprint",
                        "WidgetBlueprint",
                        "Material",
                        "DataAsset",
                        "DataTable",
                        "CurveFloat",
                        "CurveVector",
                        "InputAction",
                        "InputMappingContext",
                        "GameplayTag",
                    ],
                },
            },
            required: ["asset_type"],
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
// Handle list tools request
server.setRequestHandler(ListToolsRequestSchema, async () => {
    return { tools };
});
// Handle tool calls
server.setRequestHandler(CallToolRequestSchema, async (request) => {
    const { name, arguments: args } = request.params;
    try {
        let result;
        switch (name) {
            case "generate_assets":
                result = await callUEApi("/generate", "POST", {
                    Assets: args.assets,
                });
                break;
            case "get_generator_schema": {
                const schema = await loadSchema(args.asset_type);
                return {
                    content: [
                        {
                            type: "text",
                            text: schema,
                        },
                    ],
                };
            }
            case "extract_assets":
                result = await callUEApi("/extract", "POST", {
                    Assets: args.assets,
                    DiffOnly: args.diffOnly ?? true,
                });
                break;
            case "delete_assets":
                result = await callUEApi("/delete", "POST", {
                    Assets: args.assets,
                });
                break;
            case "query_asset":
                result = await callUEApi("/query", "POST", {
                    Asset: args.asset,
                    Path: args.path,
                });
                break;
            case "list_generators":
                result = await callUEApi("/generators", "GET");
                break;
            case "health_check":
                result = await callUEApi("/health", "GET");
                break;
            case "get_editor_context":
                result = await callUEApi("/context", "GET");
                break;
            case "execute_python":
                result = await callUEApi("/execute", "POST", {
                    Code: args.code,
                    Description: args.description,
                });
                break;
            case "apply_blueprint_change": {
                const a = args || { asset_path: "", blueprint_json: "" };
                const merge = a.merge ?? true;
                const saveAsset = a.save_asset ?? true;
                const escapedAssetPath = JSON.stringify(a.asset_path ?? "");
                const escapedBlueprintJson = JSON.stringify(a.blueprint_json ?? "");
                const editorOnline = await isEditorHttpAvailable();
                if (!editorOnline) {
                    result = await applyBlueprintChangeOffline({
                        asset_path: a.asset_path,
                        blueprint_json: a.blueprint_json,
                        merge,
                        save_asset: saveAsset,
                    });
                    break;
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
                result = await callUEApi("/execute", "POST", {
                    Code: pythonCode,
                    Description: "Apply Blueprint Change via MCP",
                });
                break;
            }
            case "chat_completion":
            case "generate_blueprint_change":
            case "repair_blueprint_json":
            case "orchestrate_modify_request":
            case "validate_blueprint_json":
            case "layout_blueprint_graph":
                result = await callSidecarTool(name, args || {});
                break;
            default:
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
