# StateTree Final Verification Smoke Evidence

- Timestamp: 2026-04-29 03:06:01 +0800
- Editor mode: GUI `UnrealEditor.app`, launched with `/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject -log`; not `UnrealEditor-Cmd` and not `-NullRHI`.
- Health check: `http://127.0.0.1:8559/assetfactory/health` returned `status=ok`, `service=AssetFactory`, `port=8559`, `subsystemAvailable=true`.
- Smoke command: `UE_API_BASE=http://127.0.0.1:8559 npm --prefix MCP run smoke:statetree-final`
- Editor plugin checkout: ProjectRPG `Plugins/AssetFactory` was detached at `61f5242`; the runner worktree was `70913f7`, whose only delta after `61f5242` is the cleanup stdout log in `MCP/scripts/statetree_roundtrip_mcp_smoke.mjs`.
- Preflight: skipped by design for this run (`PREFLIGHT_BUILD skipped=true`).
- Summary path: `/tmp/assetfactory-statetree-roundtrip/summary.json`
- Final extract path: `/tmp/assetfactory-statetree-roundtrip/ST_RoundTrip_Comprehensive.extract2.json`
- Final Editor asset: `/Game/AFSmoke/ST_RoundTrip_Comprehensive`

## Stdout Evidence

```text
PREFLIGHT_BUILD skipped=true
HEALTH success=true service=AssetFactory port=8559
CLEANED_STATE_TREE_SMOKE_ROOT /Game/AFSmoke
GENERATE_INITIAL success=true succeeded=17 failed=0
EXTRACT1 success=true succeeded=17 failed=0
REGENERATE success=true succeeded=17 failed=0
EXTRACT2 success=true succeeded=17 failed=0
StateTree round-trip stable: ST_Bindings_GuidNodeIds states=2 nodes=1 parameters=1 bindings=1 transitions=0
StateTree round-trip stable: ST_RoundTrip_Comprehensive states=4 nodes=5 parameters=4 bindings=2 transitions=3
INVALID ST_Core_InvalidSchema success=false failed=1 message=Assets[0]: Unknown StateTree SchemaClass: /Script/Engine.Actor
INVALID ST_Dynamic_Invalid_UnknownNode success=false failed=1 message=Unknown StateTree node type: /Script/StateTreeModule.StateTreeDefinitelyMissingTask
INVALID ST_Parameters_Invalid_LinkedOverrideTypeMismatch success=false failed=1 message=StateTree parameter override 'Root/UseLinkedAsset.LinkedSpeed' type mismatch: expected 'Float', got 'String'
INVALID ST_Bindings_Invalid_TypeMismatch success=false failed=1 message=StateTree compile failed after applying property bindings: ... properties are incompatible.
OPEN_ASSET success=true path=/Game/AFSmoke/ST_RoundTrip_Comprehensive logs=OPENED_ST_RoundTrip_Comprehensive True
SUMMARY positive=17/17 roundTrip=17/17 negative=20/20 finalOpen=/Game/AFSmoke/ST_RoundTrip_Comprehensive
```

## Summary Verification

`python3 -m json.tool /tmp/assetfactory-statetree-roundtrip/summary.json` succeeded.

Coverage buckets found in `/tmp/assetfactory-statetree-roundtrip/summary.pretty.json`:

- `core`
- `dynamic`
- `structure`
- `parameters`
- `bindings`
- `roundtrip`

Key fixtures present in summary:

- `ST_Bindings_GuidNodeIds`
- `ST_RoundTrip_Comprehensive`

Counts from summary:

- positive: `17/17`
- roundTrip: `17/17`
- negative: `20/20`
- finalOpenResult: `opened`

`test -f /tmp/assetfactory-statetree-roundtrip/ST_RoundTrip_Comprehensive.extract2.json` passed.

Computer Use confirmed the active UnrealEditor window title is `ST_RoundTrip_Comprehensive`; the visible StateTree editor shows Root, Idle, Attack, Recover, parameter values, and transitions.

UE log timestamps for the same run included AssetFactory server startup around `2026.04.28-19.01.23`, asset generation around `2026.04.28-19.03.37`, extraction of 17 StateTree assets around `2026.04.28-19.03.39`, and opening `ST_RoundTrip_Comprehensive` around `2026.04.28-19.03.40`. Negative fixture failures in UE logs are expected and correspond to the `negative=20/20` smoke result.
