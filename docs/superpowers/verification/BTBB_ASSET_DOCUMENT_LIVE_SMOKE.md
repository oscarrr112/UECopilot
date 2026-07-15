# BT/BB AssetDocument production live smoke

`btbb_asset_document_live_smoke.py` is a portable, external verification client for a running Unreal Editor. It does not build, start, stop, or restart Editor. The smoke uses `127.0.0.1:8562` by default and records every HTTP request/response plus the stdio MCP transcript in one evidence directory.

The smoke is intentionally split across an actual Editor restart:

1. `pre-restart` records `service.json`, the single listener PID/command, and health; renders four canonical sidecars into the validation project; runs HTTP schema/profile/template/validate/apply-file with `save_asset=true`/extract/diff; runs the same live profile/template/validate/apply-file/extract/diff path through the stdio MCP broker; verifies sidecar sync hashes and `.uasset` existence; then writes `resume_token.json`.
2. Stop that Editor and restart the same project with the same plugin build and port. Do not reuse the old process.
3. `post-restart` rejects the run unless the listener PID changed and the project's `Saved/AssetFactory/service.json` was rewritten with a different `startTime`. It performs fresh HTTP profile/template/schema/extract/diff from the saved packages without applying again and writes the final `summary.json`.

The fixtures cover parent and ordered local Blackboard keys, Object/Bool/Vector/Name/Float/Class/Enum key types, `BaseClass`, `EnumType`, explicit `KeyTypeClass`, inheritance, a selector root, nested composite children, tasks, ordinary and composite decorators, service, subtree task/reference, decorator BoundGraph logic, inline node layout, and comments.

## Offline checks

These checks do not contact Unreal:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 docs/superpowers/verification/test_btbb_asset_document_live_smoke.py
PYTHONDONTWRITEBYTECODE=1 python3 docs/superpowers/verification/btbb_asset_document_live_smoke.py --help
node --check MCP/scripts/btbb_asset_document_live_smoke.mjs
node MCP/scripts/btbb_asset_document_live_smoke.mjs --help
```

Fixture validation can use a disposable project path:

```bash
mkdir -p /tmp/assetfactory-btbb-fixture-check
printf '{}\n' > /tmp/assetfactory-btbb-fixture-check/FixtureCheck.uproject
PYTHONDONTWRITEBYTECODE=1 python3 docs/superpowers/verification/btbb_asset_document_live_smoke.py \
  --phase validate-fixtures \
  --project /tmp/assetfactory-btbb-fixture-check/FixtureCheck.uproject
```

## Main validation project

Start the Editor separately with the production plugin on port `8562`. Then run:

```bash
cd /Users/pengao/Documents/AssetFactory/UECopilot/.worktrees/finish-bt-bb-assetdocument

PYTHONDONTWRITEBYTECODE=1 python3 docs/superpowers/verification/btbb_asset_document_live_smoke.py \
  --phase pre-restart \
  --engine /Volumes/External/Unreal/Engines/UnrealEngine \
  --project /Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/AssetFactorySandbox.uproject \
  --host 127.0.0.1 \
  --port 8562 \
  --evidence-dir /Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/Saved/AssetFactory/Verification/BTBB-production
```

The command ends successfully only after writing a `restart_required` summary and prints the exact resume-token path. Stop the current Editor, confirm its listener is gone, restart the same project on `8562`, and run:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 docs/superpowers/verification/btbb_asset_document_live_smoke.py \
  --phase post-restart \
  --engine /Volumes/External/Unreal/Engines/UnrealEngine \
  --project /Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/AssetFactorySandbox.uproject \
  --host 127.0.0.1 \
  --port 8562 \
  --asset-root /Game/AssetDocumentSmoke/BTBB \
  --resume-token /Volumes/External/Unreal/Projects/AssetFactorySandbox-BTBB/Saved/AssetFactory/Verification/BTBB-production/resume_token.json
```

`--engine`, `--project`, `--host`, `--port`, `--asset-root`, fixture directory, MCP script/Node command, timeouts, and evidence directory are parameterized. Post-restart options must match the token so evidence cannot silently switch host, port, project, engine, or asset root.

## Failure semantics and evidence

The harness returns nonzero for any failed HTTP/MCP call, diagnostic, nonempty `changed`/`added`/`removed`/`missing`/`extra`/`skipped`/`failed` bucket, sidecar sync skip, missing sidecar/asset, mismatched sync hash, incomplete authored-surface coverage, stale or incomplete `service.json`, reused listener PID/start time, wrong-engine listener, or listener command targeting another project. Service discovery must identify loopback on the requested port and contain health plus all nine AssetDocument routes.

The evidence directory contains:

- `runtime_manifest.json` and rendered sidecar/asset paths;
- `http/` and `http-post-restart/` request/response records;
- service discovery JSON, listener PID/command, and health snapshots;
- `mcp/` tool requests/responses, broker stderr, driver stdout/stderr, and MCP summary;
- sidecar and `.uasset` SHA-256/existence records;
- `resume_token.json`, phase summaries, and final `summary.json`.

`summary.json` is final only when its status is `passed`. A pre-restart `restart_required` summary is a deliberate resume boundary, not completion evidence.
