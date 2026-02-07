#!/usr/bin/env node

import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
  Tool,
} from "@modelcontextprotocol/sdk/types.js";
import { readFile } from "fs/promises";
import { join, dirname } from "path";
import { fileURLToPath } from "url";

// Configuration
const UE_API_BASE = process.env.UE_API_BASE || "http://localhost:8559";
const API_PREFIX = "/assetfactory";

// Resolve schemas directory (relative to dist/index.js -> ../schemas/)
const __dirname = dirname(fileURLToPath(import.meta.url));
const SCHEMAS_DIR = join(__dirname, "..", "schemas");

// Helper to make HTTP requests to UE
async function callUEApi(endpoint: string, method: string, body?: unknown): Promise<unknown> {
  const url = `${UE_API_BASE}${API_PREFIX}${endpoint}`;

  const options: RequestInit = {
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
  } catch (error) {
    return {
      success: false,
      error: `Failed to connect to UE API at ${url}: ${error}`,
    };
  }
}

// Helper to load schema file
async function loadSchema(assetType: string): Promise<string> {
  try {
    const filePath = join(SCHEMAS_DIR, `${assetType}.md`);
    return await readFile(filePath, "utf-8");
  } catch {
    return `Schema not found for asset type: ${assetType}. Available types: Blueprint, WidgetBlueprint, Material, DataAsset, CurveFloat, CurveVector, InputAction, InputMappingContext`;
  }
}

// Define available tools
const tools: Tool[] = [
  {
    name: "generate_assets",
    description:
      "Generate Unreal Engine assets from JSON configuration. Supports Blueprint, WidgetBlueprint, DataAsset, Material, CurveFloat, CurveVector, InputAction, InputMappingContext. IMPORTANT: Call get_generator_schema first to get the correct JSON field names and formats for the asset type you want to generate.",
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
                description:
                  "Type of asset: Blueprint, WidgetBlueprint, DataAsset, Material, CurveFloat, CurveVector, InputAction, InputMappingContext",
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
    description:
      "Get the JSON schema documentation for a specific asset generator. Returns field names, types, formats, and examples. Always call this before generate_assets to ensure correct field usage.",
    inputSchema: {
      type: "object",
      properties: {
        asset_type: {
          type: "string",
          description:
            "The asset type to get schema for: Blueprint, WidgetBlueprint, Material, DataAsset, CurveFloat, CurveVector, InputAction, InputMappingContext",
          enum: [
            "Blueprint",
            "WidgetBlueprint",
            "Material",
            "DataAsset",
            "CurveFloat",
            "CurveVector",
            "InputAction",
            "InputMappingContext",
          ],
        },
      },
      required: ["asset_type"],
    },
  },
  {
    name: "extract_assets",
    description:
      "Extract Unreal Engine asset configurations as JSON. Use this to understand existing asset structure before modifying.",
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
    description:
      "Query specific properties from an extracted asset using JSON path syntax. Supports Array[0], Array[*], and Parent.Child notation.",
    inputSchema: {
      type: "object",
      properties: {
        asset: {
          type: "string",
          description: "Asset path to query (e.g., /Game/Blueprints/BP_Player)",
        },
        path: {
          type: "string",
          description:
            "JSON path to query (e.g., Components[0].Properties, RootWidget.Children[*].Type)",
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
];

// Create MCP server
const server = new Server(
  {
    name: "ue-copilot",
    version: "1.1.0",
  },
  {
    capabilities: {
      tools: {},
    },
  }
);

// Handle list tools request
server.setRequestHandler(ListToolsRequestSchema, async () => {
  return { tools };
});

// Handle tool calls
server.setRequestHandler(CallToolRequestSchema, async (request) => {
  const { name, arguments: args } = request.params;

  try {
    let result: unknown;

    switch (name) {
      case "generate_assets":
        result = await callUEApi("/generate", "POST", {
          Assets: (args as { assets: unknown[] }).assets,
        });
        break;

      case "get_generator_schema": {
        const schema = await loadSchema((args as { asset_type: string }).asset_type);
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
          Assets: (args as { assets: string[] }).assets,
          DiffOnly: (args as { diffOnly?: boolean }).diffOnly ?? true,
        });
        break;

      case "delete_assets":
        result = await callUEApi("/delete", "POST", {
          Assets: (args as { assets: string[] }).assets,
        });
        break;

      case "query_asset":
        result = await callUEApi("/query", "POST", {
          Asset: (args as { asset: string }).asset,
          Path: (args as { path: string }).path,
        });
        break;

      case "list_generators":
        result = await callUEApi("/generators", "GET");
        break;

      case "health_check":
        result = await callUEApi("/health", "GET");
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
          text: JSON.stringify(result, null, 2),
        },
      ],
    };
  } catch (error) {
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
