# AssetDocument External HTTP Smoke Harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the in-process AssetDocument AnimMontage smoke with an external HTTP harness that can drive a running Editor, apply a real sidecar file, verify extract/diff, and leave `/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke` as a inspectable smoke asset.

**Architecture:** The Python smoke script becomes a pure external client with no `unreal` import. It waits for `/assetfactory/health`, uses `/assetfactory/generate` to create the AnimSequence fixture, writes a sidecar `.assetdoc.json`, applies it through `/assetfactory/assetdocument/apply-file`, then verifies `/extract` and `/diff`. A small PowerShell runner optionally starts the validation-host Editor and invokes the Python client from outside the UE process.

**Tech Stack:** Python standard library (`argparse`, `json`, `urllib`, `pathlib`, `unittest`), PowerShell, Unreal Editor HTTP routes on port `8559`, validation host `C:/AVH1/AVH1.uproject`.

---

## Scope And Boundaries

This task fixes the smoke harness only. It should not change `FAnimMontageAssetDocumentCapability`, region policies, sync engine behavior, or MCP tool schemas.

The smoke must validate this real external-user path:

```text
external Python process
  -> GET /assetfactory/health
  -> POST /assetfactory/generate
  -> write sidecar file
  -> POST /assetfactory/assetdocument/apply-file
  -> POST /assetfactory/assetdocument/extract
  -> POST /assetfactory/assetdocument/diff
  -> leave /Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke
```

The smoke sidecar target and expected asset path are fixed by default:

```text
Montage target: /Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke
Generated animation: /Game/Generated/Animation/AS_AssetDocSmoke
Default sidecar file: C:/AVH1/Saved/AssetDocumentSmoke/AM_DeltaSidecarSmoke.assetdoc.json
```

## File Structure

- Modify: `docs/superpowers/verification/asset_document_delta_sidecar_smoke.py`
  - Convert to a pure external HTTP client.
  - Add CLI flags and health polling.
  - Write and apply a sidecar file through `apply-file`.
  - Print a compact JSON summary on success.
- Create: `docs/superpowers/verification/test_asset_document_delta_sidecar_smoke.py`
  - Stdlib `unittest` coverage for request sequence, sidecar writing, payload validation, and diff assertion behavior.
- Create: `docs/superpowers/verification/run_asset_document_delta_sidecar_smoke.ps1`
  - Optional runner that starts Editor, waits for health through the Python script, invokes smoke, and can terminate the spawned Editor process.
- Modify: `docs/reports/asset-document-animmontage-complete-region-benchmark.md`
  - Replace the blocked smoke result with the new external smoke command and result after live validation passes.

---

## Task 1: Unit-Test The External HTTP Client Contract

**Files:**
- Create: `docs/superpowers/verification/test_asset_document_delta_sidecar_smoke.py`
- Modify: `docs/superpowers/verification/asset_document_delta_sidecar_smoke.py`

- [ ] **Step 1: Add unit tests for the future external client**

Create `docs/superpowers/verification/test_asset_document_delta_sidecar_smoke.py` with this initial content:

```python
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import asset_document_delta_sidecar_smoke as smoke


class SmokeClientTests(unittest.TestCase):
    def test_montage_document_contains_complete_regions(self):
        document = smoke.montage_document(
            montage_target="/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke",
            anim_object_path="/Game/Generated/Animation/AS_AssetDocSmoke.AS_AssetDocSmoke",
        )

        body = document["Body"]

        self.assertEqual(document["Target"], "/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke")
        self.assertIn("References", body)
        self.assertIn("Preview", body)
        self.assertIn("SlotAnimTracks", body)
        self.assertIn("CompositeSections", body)
        self.assertIn("Blend", body)
        self.assertIn("Sync", body)
        self.assertIn("RootMotion", body)
        self.assertIn("TimeStretch", body)
        self.assertIn("Curves", body)

    def test_write_sidecar_creates_parent_directory_and_json_file(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            sidecar_path = Path(temp_dir) / "nested" / "AM_DeltaSidecarSmoke.assetdoc.json"
            document = {"Target": "/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke", "Body": {}}

            smoke.write_sidecar(sidecar_path, document)

            loaded = json.loads(sidecar_path.read_text(encoding="utf-8"))
            self.assertEqual(loaded["Target"], document["Target"])

    def test_run_smoke_uses_external_http_routes_in_order(self):
        calls = []

        def fake_request(method, path, payload=None):
            calls.append((method, path, payload))
            if path == "/health":
                return {"success": True, "status": "ok"}
            if path == "/generate":
                return {"success": True, "SuccessCount": 1, "UpdatedCount": 0, "FailedCount": 0}
            if path == "/assetdocument/apply-file":
                return {"success": True, "payload": {"Target": "/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke"}}
            if path == "/assetdocument/extract":
                return {"success": True, "payload": smoke.montage_document(
                    montage_target="/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke",
                    anim_object_path="/Game/Generated/Animation/AS_AssetDocSmoke.AS_AssetDocSmoke",
                )}
            if path == "/assetdocument/diff":
                return {"success": True, "payload": {"changed": []}}
            raise AssertionError("unexpected route {0}".format(path))

        with tempfile.TemporaryDirectory() as temp_dir:
            options = smoke.SmokeOptions(
                base_url="http://127.0.0.1:8559/assetfactory",
                sidecar_file=str(Path(temp_dir) / "AM_DeltaSidecarSmoke.assetdoc.json"),
                request_timeout=0.1,
                wait_timeout=0.1,
                poll_interval=0.01,
                save_asset=True,
                keep_sidecar=True,
                montage_target="/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke",
                anim_name="AS_AssetDocSmoke",
                anim_package_path="/Game/Generated/Animation",
            )
            with mock.patch.object(smoke.HttpClient, "request", side_effect=fake_request):
                summary = smoke.run_smoke(options)

        self.assertEqual(summary["montage_target"], "/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke")
        self.assertEqual(
            [(method, path) for method, path, _payload in calls],
            [
                ("GET", "/health"),
                ("POST", "/generate"),
                ("POST", "/assetdocument/apply-file"),
                ("POST", "/assetdocument/extract"),
                ("POST", "/assetdocument/diff"),
            ],
        )
        apply_payload = calls[2][2]
        self.assertTrue(apply_payload["file_path"].endswith("AM_DeltaSidecarSmoke.assetdoc.json"))
        self.assertTrue(apply_payload["save_asset"])

    def test_diff_assertion_rejects_changed_complete_regions(self):
        with self.assertRaisesRegex(RuntimeError, "/Body/Curves"):
            smoke.assert_no_changed_complete_regions({"changed": [{"path": "/Body/Curves"}]})


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the new tests and verify RED**

Run:

```powershell
py -3 docs/superpowers/verification/test_asset_document_delta_sidecar_smoke.py
```

Expected: FAIL because `SmokeOptions`, `montage_document`, `write_sidecar`, `HttpClient`, `run_smoke`, and `assert_no_changed_complete_regions` do not exist yet.

- [ ] **Step 3: Commit the RED tests**

```powershell
git add docs/superpowers/verification/test_asset_document_delta_sidecar_smoke.py
git commit -m "test(assetdoc): specify external smoke harness"
```

---

## Task 2: Convert The Smoke Script Into An External HTTP Client

**Files:**
- Modify: `docs/superpowers/verification/asset_document_delta_sidecar_smoke.py`
- Test: `docs/superpowers/verification/test_asset_document_delta_sidecar_smoke.py`

- [ ] **Step 1: Replace the in-process script with external-client structure**

Rewrite `docs/superpowers/verification/asset_document_delta_sidecar_smoke.py` so it contains no `import unreal`. The top-level structure should use this public surface, which matches Task 1 tests:

```python
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path


DEFAULT_PORT = int(os.environ.get("ASSETFACTORY_HTTP_PORT", "8559"))
DEFAULT_BASE_URL = os.environ.get("ASSETFACTORY_BASE_URL", "http://127.0.0.1:{0}/assetfactory".format(DEFAULT_PORT))
DEFAULT_MONTAGE_TARGET = os.environ.get("ASSETDOC_SMOKE_MONTAGE_TARGET", "/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke")
DEFAULT_ANIM_NAME = os.environ.get("ASSETDOC_SMOKE_ANIM_NAME", "AS_AssetDocSmoke")
DEFAULT_ANIM_PACKAGE_PATH = os.environ.get("ASSETDOC_SMOKE_ANIM_PATH", "/Game/Generated/Animation")
DEFAULT_SIDECAR_FILE = os.environ.get(
    "ASSETDOC_SMOKE_SIDECAR_FILE",
    "C:/AVH1/Saved/AssetDocumentSmoke/AM_DeltaSidecarSmoke.assetdoc.json",
)


@dataclass
class SmokeOptions:
    base_url: str
    sidecar_file: str
    request_timeout: float
    wait_timeout: float
    poll_interval: float
    save_asset: bool
    keep_sidecar: bool
    montage_target: str
    anim_name: str
    anim_package_path: str


class HttpClient:
    def __init__(self, base_url, timeout):
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout

    def request(self, method, path, payload=None):
        data = None
        headers = {}
        if payload is not None:
            data = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(
            self.base_url + path,
            data=data,
            headers=headers,
            method=method,
        )
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                body = response.read().decode("utf-8")
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise RuntimeError("{0} {1} failed with HTTP {2}: {3}".format(method, path, exc.code, detail))
        except urllib.error.URLError as exc:
            raise RuntimeError("{0} {1} failed: {2}".format(method, path, exc))
        return json.loads(body) if body else {}
```

- [ ] **Step 2: Implement document creation and sidecar writing**

Add these functions:

```python
def anim_object_path(anim_package_path, anim_name):
    return "{0}/{1}.{1}".format(anim_package_path.rstrip("/"), anim_name)


def asset_ref(path):
    return {"Kind": "AssetRef", "Path": path}


def montage_document(montage_target, anim_object_path):
    return {
        "SchemaVersion": 1,
        "Target": montage_target,
        "Class": "/Script/Engine.AnimMontage",
        "Action": "CreateOrUpdate",
        "Definitions": {},
        "Properties": {},
        "Body": {
            "References": {
                "Skeleton": asset_ref("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton"),
            },
            "Preview": {
                "PreviewMesh": asset_ref("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP"),
                "PreviewBasePose": asset_ref(anim_object_path),
            },
            "SlotAnimTracks": [
                {
                    "SlotName": "DefaultSlot",
                    "AnimTrack": {
                        "AnimSegments": [
                            {
                                "AnimReference": asset_ref(anim_object_path),
                                "StartPos": 0.0,
                                "AnimStartTime": 0.0,
                                "AnimEndTime": 0.25,
                                "AnimPlayRate": 1.0,
                                "LoopingCount": 1.0,
                            }
                        ]
                    },
                }
            ],
            "CompositeSections": [
                {"SectionName": "Start", "LinkableTime": 0.0, "NextSectionName": "End"},
                {"SectionName": "End", "LinkableTime": 0.25},
            ],
            "Blend": {
                "BlendInTime": 0.1,
                "BlendOutTime": 0.2,
                "BlendModeIn": "Standard",
                "BlendModeOut": "Standard",
                "BlendOutTriggerTime": -1.0,
                "bEnableAutoBlendOut": True,
            },
            "Sync": {"SyncGroup": "AssetDocSmoke", "SyncSlotIndex": 0},
            "RootMotion": {
                "bEnableRootMotionTranslation": True,
                "bEnableRootMotionRotation": True,
                "RootMotionRootLock": "Zero",
            },
            "TimeStretch": {
                "TimeStretchCurveName": "MontageTimeStretchCurve",
                "SamplingRate": 30.0,
                "CurveValueMinPrecision": 0.02,
            },
            "Curves": [
                {
                    "Name": "MontageTimeStretchCurve",
                    "Flags": ["Default"],
                    "Keys": [
                        {"Time": 0.0, "Value": 0.0},
                        {"Time": 0.5, "Value": 1.0},
                        {"Time": 1.0, "Value": 0.0},
                    ],
                }
            ],
        },
    }


def write_sidecar(path, document):
    sidecar_path = Path(path)
    sidecar_path.parent.mkdir(parents=True, exist_ok=True)
    sidecar_path.write_text(json.dumps(document, indent=2, sort_keys=True), encoding="utf-8")
    return sidecar_path
```

- [ ] **Step 3: Implement HTTP sequence, assertions, and CLI**

Add functions for:

```python
def wait_for_health(client, wait_timeout, poll_interval):
    deadline = time.time() + wait_timeout
    last_error = None
    while time.time() < deadline:
        try:
            result = client.request("GET", "/health")
            if result.get("status") == "ok" or result.get("success") is True:
                return result
        except RuntimeError as exc:
            last_error = exc
        time.sleep(poll_interval)
    raise RuntimeError("Timed out waiting for AssetFactory health at {0}: {1}".format(client.base_url, last_error))


def generate_anim_sequence(client, anim_package_path, anim_name):
    return client.request("POST", "/generate", {
        "Assets": [
            {
                "AssetType": "AnimSequence",
                "Name": anim_name,
                "Path": anim_package_path,
                "Action": "CreateOrUpdate",
                "Skeleton": "/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton",
                "PreviewMesh": "/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP",
                "FrameRate": {"Numerator": 30, "Denominator": 1},
                "NumberOfFrames": 12,
            }
        ]
    })


def payload(result):
    value = result.get("payload")
    if not isinstance(value, dict):
        raise RuntimeError("Missing payload in result: {0}".format(result))
    return value


def assert_no_changed_complete_regions(diff_payload):
    changed = diff_payload.get("changed", [])
    changed_paths = {entry.get("path") for entry in changed if isinstance(entry, dict)}
    for path in ("/Body/Sync", "/Body/RootMotion", "/Body/TimeStretch", "/Body/Curves"):
        if path in changed_paths:
            raise RuntimeError("Unexpected changed diff entry after re-extract: {0}".format(path))
```

The `run_smoke(options)` function must:

1. Create `HttpClient(options.base_url, options.request_timeout)`.
2. Call `wait_for_health(...)`.
3. Call `generate_anim_sequence(...)`.
4. Build `document = montage_document(options.montage_target, anim_object_path(...))`.
5. Write sidecar file with `write_sidecar(...)`.
6. POST `{"file_path": str(sidecar_path), "save_asset": options.save_asset}` to `/assetdocument/apply-file`.
7. POST extract request:
   ```python
   {"asset_path": options.montage_target, "diff_only": False, "include_all_writable": True}
   ```
8. Assert extracted `Sync`, `RootMotion`, `TimeStretch`, and `Curves` match the sidecar.
9. POST the extracted document to `/assetdocument/diff`.
10. Assert no changed entries exist for `/Body/Sync`, `/Body/RootMotion`, `/Body/TimeStretch`, `/Body/Curves`.
11. Return:
   ```python
   {
       "success": True,
       "montage_target": options.montage_target,
       "anim_object_path": anim_object_path(options.anim_package_path, options.anim_name),
       "sidecar_file": str(sidecar_path),
       "base_url": options.base_url,
   }
   ```

The `main()` function must parse:

```text
--base-url
--sidecar-file
--request-timeout
--wait-timeout
--poll-interval
--no-save-asset
--keep-sidecar
--montage-target
--anim-name
--anim-package-path
```

On success, print the summary as JSON and exit `0`. On failure, print the error to stderr and exit `1`.

- [ ] **Step 4: Run unit tests and verify GREEN**

Run:

```powershell
py -3 docs/superpowers/verification/test_asset_document_delta_sidecar_smoke.py
```

Expected: `OK`.

- [ ] **Step 5: Commit the external Python client**

```powershell
git add docs/superpowers/verification/asset_document_delta_sidecar_smoke.py docs/superpowers/verification/test_asset_document_delta_sidecar_smoke.py
git commit -m "feat(assetdoc): add external http smoke client"
```

---

## Task 3: Add A One-Command Validation Runner

**Files:**
- Create: `docs/superpowers/verification/run_asset_document_delta_sidecar_smoke.ps1`
- Test: manual PowerShell syntax and dry path checks

- [ ] **Step 1: Create the runner script**

Create `docs/superpowers/verification/run_asset_document_delta_sidecar_smoke.ps1`:

```powershell
param(
    [string]$Project = "C:/AVH1/AVH1.uproject",
    [string]$EditorExe = "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe",
    [string]$PythonExe = "py",
    [int]$Port = 8559,
    [int]$WaitTimeoutSeconds = 120,
    [switch]$KeepEditor,
    [switch]$KeepSidecar
)

$ErrorActionPreference = "Stop"

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$SmokeScript = Join-Path $ScriptRoot "asset_document_delta_sidecar_smoke.py"

if (-not (Test-Path -LiteralPath $Project)) {
    throw "Project file not found: $Project"
}
if (-not (Test-Path -LiteralPath $EditorExe)) {
    throw "Editor exe not found: $EditorExe"
}
if (-not (Test-Path -LiteralPath $SmokeScript)) {
    throw "Smoke script not found: $SmokeScript"
}

$BaseUrl = "http://127.0.0.1:$Port/assetfactory"
$EditorProcess = $null

try {
    Write-Host "Starting Unreal Editor for smoke: $Project"
    $EditorProcess = Start-Process -FilePath $EditorExe -ArgumentList @(
        $Project,
        "-NoSplash",
        "-Unattended"
    ) -PassThru -WindowStyle Hidden

    Write-Host "Waiting for AssetFactory health at $BaseUrl"
    & $PythonExe -3 $SmokeScript `
        --base-url $BaseUrl `
        --wait-timeout $WaitTimeoutSeconds `
        --request-timeout 60 `
        --keep-sidecar:([bool]$KeepSidecar)

    if ($LASTEXITCODE -ne 0) {
        throw "Smoke script failed with exit code $LASTEXITCODE"
    }
}
finally {
    if ($EditorProcess -and -not $KeepEditor) {
        Write-Host "Stopping Unreal Editor process $($EditorProcess.Id)"
        Stop-Process -Id $EditorProcess.Id -Force -ErrorAction SilentlyContinue
    }
}
```

- [ ] **Step 2: Fix boolean switch invocation if needed**

PowerShell switch forwarding can be awkward. If `--keep-sidecar:([bool]$KeepSidecar)` does not invoke Python as expected, replace the smoke invocation with explicit argument assembly:

```powershell
$Args = @(
    "-3",
    $SmokeScript,
    "--base-url", $BaseUrl,
    "--wait-timeout", "$WaitTimeoutSeconds",
    "--request-timeout", "60"
)
if ($KeepSidecar) {
    $Args += "--keep-sidecar"
}
& $PythonExe @Args
```

- [ ] **Step 3: Validate script syntax without launching Editor**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File docs/superpowers/verification/run_asset_document_delta_sidecar_smoke.ps1 -Project C:/Path/That/Does/Not/Exist.uproject
```

Expected: FAIL with `Project file not found`, not a PowerShell parse error.

- [ ] **Step 4: Commit the runner**

```powershell
git add docs/superpowers/verification/run_asset_document_delta_sidecar_smoke.ps1
git commit -m "feat(assetdoc): add external smoke runner"
```

---

## Task 4: Live Validation Against The Validation Host

**Files:**
- Modify: `docs/reports/asset-document-animmontage-complete-region-benchmark.md`

- [ ] **Step 1: Run UBT before live smoke**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
```

Expected: exit `0`, `Result: Succeeded`.

- [ ] **Step 2: Run the external smoke through the runner**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File docs/superpowers/verification/run_asset_document_delta_sidecar_smoke.ps1 -Project C:/AVH1/AVH1.uproject -KeepSidecar
```

Expected: exit `0`, JSON summary includes:

```json
{
  "success": true,
  "montage_target": "/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke",
  "anim_object_path": "/Game/Generated/Animation/AS_AssetDocSmoke.AS_AssetDocSmoke"
}
```

- [ ] **Step 3: Verify the asset remains loadable through HTTP extract**

If the runner used `-KeepEditor`, run this from PowerShell:

```powershell
$Body = @{
    asset_path = "/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke"
    diff_only = $false
    include_all_writable = $true
} | ConvertTo-Json -Depth 20
Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:8559/assetfactory/assetdocument/extract" -Body $Body -ContentType "application/json"
```

Expected: response has `success: true` and payload `Body.Sync.SyncGroup == "AssetDocSmoke"`.

If the runner stopped the Editor, rely on the smoke summary and the saved asset path; the next Editor launch can inspect `/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke`.

- [ ] **Step 4: Update final report**

In `docs/reports/asset-document-animmontage-complete-region-benchmark.md`, replace the smoke `BLOCKED` wording with the external harness command and passing result. Preserve the old blocked note as historical context only if it is useful, phrased as:

```markdown
- Previous in-process `-ExecutePythonScript` smoke was blocked because the script called the same Editor process over HTTP.
- External smoke command: `powershell -NoProfile -ExecutionPolicy Bypass -File docs/superpowers/verification/run_asset_document_delta_sidecar_smoke.ps1 -Project C:/AVH1/AVH1.uproject -KeepSidecar`
- Result: exit 0, created/updated `/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke`, wrote sidecar `C:/AVH1/Saved/AssetDocumentSmoke/AM_DeltaSidecarSmoke.assetdoc.json`, extract/diff passed for `Body.Sync`, `Body.RootMotion`, `Body.TimeStretch`, and `Body.Curves`.
```

- [ ] **Step 5: Run final local verification**

```powershell
py -3 docs/superpowers/verification/test_asset_document_delta_sidecar_smoke.py
npm test
git diff --check
git status --short
```

Run `npm test` from:

```text
E:/GameDev/PluginsWarehouse/.worktrees/UECopilot/asset-document-structured-capabilities-spec/MCP
```

Expected:

- Python unit tests: `OK`
- MCP tests: `39 pass, 0 fail`
- `git diff --check`: no errors
- `git status --short`: only intended files before commit

- [ ] **Step 6: Commit live validation docs**

```powershell
git add docs/reports/asset-document-animmontage-complete-region-benchmark.md
git commit -m "docs(assetdoc): record external animmontage smoke pass"
```

---

## Final Review And Evidence

- [ ] **Step 1: Run read-only spec/code-quality review**

Review range:

```text
89225f6..HEAD
```

Review focus:

- The smoke no longer imports `unreal`.
- The sidecar path uses `/assetdocument/apply-file`, not inline `/assetdocument/apply`.
- The harness waits for `/health` before issuing write requests.
- The smoke leaves `/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke` inspectable.
- The report does not claim the old in-process smoke passed.

- [ ] **Step 2: Fix accepted review findings**

For every accepted finding, add or update a Python unit test first, run it red when possible, patch the script, then run it green.

- [ ] **Step 3: Final verification**

```powershell
& "E:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" AVH1Editor Win64 Development "-Project=C:/AVH1/AVH1.uproject" -NoHotReload
py -3 docs/superpowers/verification/test_asset_document_delta_sidecar_smoke.py
powershell -NoProfile -ExecutionPolicy Bypass -File docs/superpowers/verification/run_asset_document_delta_sidecar_smoke.ps1 -Project C:/AVH1/AVH1.uproject -KeepSidecar
Push-Location MCP; npm test; Pop-Location
git diff --check
git status --short
```

Expected:

- UBT: `Result: Succeeded`
- Python smoke unit tests: `OK`
- External smoke runner: exit `0`, JSON success summary
- MCP tests: `39 pass, 0 fail`
- `git diff --check`: no errors
- `git status --short`: clean after final commit

---

## Self-Review Checklist

- Spec coverage: The plan covers external process smoke, health wait, sidecar file apply, extract/diff verification, and inspectable smoke asset.
- Placeholder scan: The plan contains no unfinished placeholder markers or incomplete implementation notes.
- Scope check: The plan is limited to smoke harness and report updates; production capability behavior is out of scope.
- Verification coverage: Unit tests cover request sequencing; live validation covers real Editor HTTP and saved asset.
