# Production Hardening Guide (MCP Sidecar)

## Recommended Plugin Settings

- `bEnableMCPOnlyMode = true`
- `bEnableMCPForModify = true`
- `bEnableMCPForChatRequests = true`
- `bMCPFallbackToDirectAI = false`
- `bAllowDirectAIHttpForDebug = false`
- `bPreferEnvironmentApiKey = true`
- `bWriteApiKeyToMCPRequestFile = false`
- `MCPNetworkRetries = 2` (increase to `3` for unstable networks)

## Secret Management

- Do not commit API keys in `DefaultEditor.ini`.
- Set environment variable(s) on deployment machines:
  - `DEEPSEEK_API_KEY` or
  - `ASSETFACTORY_API_KEY`

## Observability

- Capture `Saved/Logs/TestProject.log` on failure.
- Keep MCP request folder for diagnostics only:
  - `Saved/AssetFactory/MCP/`
- Redact user prompts and credentials before sharing logs externally.

## Pre-Release Validation

- Build:
  - `Build.bat TestProjectEditor ...`
- Runtime MCP check:
  - run `tools/list` example
- Editor flow:
  - run `/modify` MCP automation tests
- Live provider:
  - run bulk live test set and verify success threshold
