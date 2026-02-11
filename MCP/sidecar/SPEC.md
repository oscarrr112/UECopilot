# Sidecar MCP Spec (Blueprint Logic)

## Transport

- Process execution:
  - `py MCP/assetfactory_mcp_server.py --request <json_file_path>`
- Input:
  - JSON-RPC request file path
- Output:
  - One JSON-RPC response printed to stdout
- Exit code:
  - `0` success
  - non-zero on request/tool/runtime failure

## Request Envelope

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/list | tools/call",
  "params": {}
}
```

## Response Envelope

Success:
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "content": "..."
  }
}
```

Error:
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "error": {
    "code": -32010,
    "message": "..."
  }
}
```

## Method: `tools/list`

### Request
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/list",
  "params": {}
}
```

### Result
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "tools": [
      { "name": "chat_completion" },
      { "name": "generate_blueprint_change" },
      { "name": "repair_blueprint_json" },
      { "name": "orchestrate_modify_request" },
      { "name": "validate_blueprint_json" }
    ]
  }
}
```

## Method: `tools/call`

### Request
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "<tool_name>",
    "arguments": {}
  }
}
```

### Result
- `result.content` contains tool output as string.

## Error Codes

- `-32600`: invalid JSON-RPC envelope
- `-32601`: unsupported method/tool
- `-32000`: validation error in tool arguments
- `-32010`: runtime/network/model call failure
- `-32050`: unhandled sidecar exception
