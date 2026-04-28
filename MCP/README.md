# UE Copilot MCP Server

MCP (Model Context Protocol) Server for Unreal Engine Copilot - enables AI-assisted asset generation in Unreal Engine.

## Prerequisites

- Node.js 18+
- Unreal Engine Editor running with UECopilot plugin loaded
- UECopilot HTTP Server running (default port: 8559)

## Installation

```bash
cd MCP
npm install
npm run build
```

## Configuration

### Claude Code

Add to your Claude Code MCP settings (`~/.claude/claude_desktop_config.json` or via `/mcp` command):

```json
{
  "mcpServers": {
    "ue-copilot": {
      "command": "node",
      "args": ["E:/GameDev/PluginsWarehouse/Plugins/UECopilot/MCP/dist/index.js"],
      "env": {
        "UE_API_BASE": "http://localhost:8559",
        "UE_MCP_PYTHON": "py",
        "UE_MCP_SIDECAR_SCRIPT": "E:/GameDev/PluginsWarehouse/Plugins/UECopilot/MCP/assetfactory_mcp_server.py"
      }
    }
  }
}
```

### Claude Desktop

Add to `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "ue-copilot": {
      "command": "node",
      "args": ["E:/GameDev/PluginsWarehouse/Plugins/UECopilot/MCP/dist/index.js"],
      "env": {
        "UE_API_BASE": "http://localhost:8559",
        "UE_MCP_PYTHON": "py",
        "UE_MCP_SIDECAR_SCRIPT": "E:/GameDev/PluginsWarehouse/Plugins/UECopilot/MCP/assetfactory_mcp_server.py"
      }
    }
  }
}
```

## Agent-Aware Tool Catalog

This broker has a single canonical implementation in `MCP/dist/index.js`. It exposes tools based on the client that connects to it:

- `generic` clients see the shared asset tools, the editor-assist tools, and the BSL Blueprint tool set, but not the legacy JSON Blueprint tools or Claude-only sidecar compatibility tools.
- `Codex` sees the shared asset tools, the editor-assist tools, and the BSL Blueprint tool set only.
- `Claude` sees the same shared tools, the BSL Blueprint tools, and the legacy JSON Blueprint compatibility tools.
- `chat_completion` is a Claude-only sidecar compatibility tool, not part of the shared editor tool surface.

The intent is to keep the implementation unified while presenting the smallest useful tool surface to each agent.

## Available Tools

### generate_assets
Generate Unreal Engine assets from JSON configuration.

```json
{
  "assets": [
    {
      "AssetType": "Blueprint",
      "Name": "BP_Player",
      "Path": "/Game/Blueprints",
      "ParentClass": "Character",
      "Components": [...]
    }
  ]
}
```

Supported asset types:
- Blueprint
- WidgetBlueprint
- DataAsset
- Material
- CurveFloat
- CurveVector
- InputAction
- InputMappingContext

### extract_assets
Extract existing asset configurations as JSON.

```json
{
  "assets": ["/Game/Blueprints/BP_Player"],
  "diffOnly": true
}
```

### delete_assets
Delete assets from the project.

```json
{
  "assets": ["/Game/Blueprints/BP_Old"]
}
```

### query_asset
Query specific properties from an asset using JSON path syntax.

```json
{
  "asset": "/Game/Blueprints/BP_Player",
  "path": "Components[0].Properties"
}
```

### list_generators
List all available asset generators.

### health_check
Check if the UE HTTP server is running.

### chat_completion / generate_blueprint_change
Claude compatibility / legacy sidecar flow.

### repair_blueprint_json
Repair malformed blueprint JSON into one valid object.

### orchestrate_modify_request
Generate blueprint JSON and auto-repair if needed. Now includes layout post-processing by default.

### validate_blueprint_json
Validate blueprint JSON structure before apply.

### layout_blueprint_graph
Apply deterministic node positions to functions/event graphs to reduce overlap and wiring chaos.

### apply_blueprint_change
Apply blueprint logic JSON directly to an existing Blueprint asset.

Execution mode:
- Editor online: uses `/assetfactory/execute` HTTP path.
- Editor offline: auto-fallback to `UnrealEditor-Cmd -run=AssetFactoryApplyBlueprint`.

```json
{
  "asset_path": "/Game/Blueprints/BP_HelloWorld.BP_HelloWorld",
  "blueprint_json": "{\"name\":\"BP_HelloWorld\",\"parent_class\":\"Actor\",\"functions\":[]}",
  "merge": true,
  "save_asset": true
}
```

## Environment Variables

- `UE_API_BASE`: Base URL for UE API (default: `http://localhost:8559`)
- `UE_MCP_PYTHON`: Python launcher for sidecar (default: `py` on Windows, `python3` on macOS/Linux)
- `UE_MCP_SIDECAR_SCRIPT`: Absolute path to `assetfactory_mcp_server.py`
- `UE_EDITOR_CMD`: Absolute path to `UnrealEditor-Cmd.exe` (required for offline fallback)
- `UE_PROJECT_PATH`: Absolute path to `.uproject` (required for offline fallback)

## Development

```bash
npm run dev  # Build and run
```

## Blueprint Logic Sidecar (Python)

This plugin also includes a local Python MCP sidecar used by the in-editor `/modify` flow:

- Entry: `MCP/assetfactory_mcp_server.py`
- Docs: `MCP/sidecar/README.md`
- Tool specs: `MCP/sidecar/tools/`
- JSON-RPC examples: `MCP/sidecar/examples/`

The Python sidecar is used by `SAIChatWindow` for MCP-first blueprint logic requests and validation.
