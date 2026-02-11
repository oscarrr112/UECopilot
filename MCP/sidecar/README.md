# UECopilot Blueprint Logic MCP Sidecar

This folder documents the Python MCP sidecar used by blueprint logic generation/modification flows.

## Scope

- Script: `MCP/assetfactory_mcp_server.py`
- Protocol: JSON-RPC 2.0 over file input (`--request <path>`)
- Invocation model: one process per request
- Consumer: `AssetFactoryEditor` (`SAIChatWindow` MCP-first request path)

## Why this sidecar exists

The Node MCP server in `MCP/dist/index.js` handles general asset workflows.
The Python sidecar provides a lightweight, local MCP bridge for blueprint logic chat and JSON validation in editor/runtime test flows.

## Supported Methods

- `tools/list`
- `tools/call`

See `SPEC.md` for full request/response shape.

## Supported Tools

- `chat_completion`
- `generate_blueprint_change`
- `repair_blueprint_json`
- `orchestrate_modify_request`
- `validate_blueprint_json`
- `layout_blueprint_graph`

See `tools/` for per-tool argument contracts.

## Quick Smoke Test

1. Create request JSON:
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/list",
  "params": {}
}
```

2. Call sidecar:
```powershell
py .\MCP\assetfactory_mcp_server.py --request <request.json>
```

3. Expected:
- exit code `0`
- `result.tools` contains the tool list

## Integration Notes

- `SAIChatWindow` builds MCP request JSON files under:
  - `Saved/AssetFactory/MCP/`
- Request files are written as UTF-8 without BOM.
- Sidecar reads request files using `utf-8-sig` for compatibility.
- Sidecar supports `max_retries` (default `2`) for transient HTTP failures.
- If `api_key` is omitted, sidecar attempts environment variables:
  - `ASSETFACTORY_API_KEY`
  - `DEEPSEEK_API_KEY`, `OPENAI_API_KEY`, `GLM_API_KEY` (provider-specific)

## Security Notes

- Do not hardcode API keys in committed config files.
- Use local machine config or environment variable based secret injection.
- For stricter handling, disable API key temp-file injection in plugin settings:
  - `bWriteApiKeyToMCPRequestFile = false`
