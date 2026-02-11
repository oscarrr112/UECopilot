# Sidecar Changelog

## 2026-02-11

### Added
- Python MCP sidecar entrypoint:
  - `MCP/assetfactory_mcp_server.py`
- Tools:
  - `chat_completion`
  - `generate_blueprint_change`
  - `repair_blueprint_json`
  - `orchestrate_modify_request`
  - `validate_blueprint_json`

### Added (docs)
- `MCP/sidecar/README.md`
- `MCP/sidecar/SPEC.md`
- `MCP/sidecar/tools/*.md`
- `MCP/sidecar/examples/*.json`

### Notes
- Sidecar is integrated into editor `/modify` MCP-first flow.
- Requests are file-based JSON-RPC and process-scoped.
