#!/usr/bin/env python3
"""External AnimSequence AssetDocument smoke for a running editor HTTP server."""

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


TARGET = "/Game/AssetDocumentSmoke/AS_PostImportSidecarSmoke"
ASSET_NAME = "AS_PostImportSidecarSmoke"
SIDECAR_RELATIVE = Path("Content") / "AssetDocumentSmoke" / f"{ASSET_NAME}.assetdoc.json"
EXPECTED_REGIONS = [
    "References",
    "Preview",
    "Playback",
    "Additive",
    "RootMotion",
    "Compression",
    "Curves",
    "Notifies",
    "NotifyStates",
    "NotifyTracks",
    "SyncMarkers",
    "Metadata",
    "AssetUserData",
]


def request_json(base_url: str, method: str, path: str, payload: dict[str, Any] | None = None) -> dict[str, Any]:
    data = None
    headers = {"Accept": "application/json"}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"

    request = urllib.request.Request(f"{base_url}{path}", data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            body = response.read().decode("utf-8")
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"{method} {path} failed with HTTP {error.code}: {detail}") from error
    except urllib.error.URLError as error:
        raise RuntimeError(f"{method} {path} failed: {error}") from error

    try:
        decoded = json.loads(body)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"{method} {path} returned invalid JSON: {body[:500]}") from error
    if not isinstance(decoded, dict):
        raise RuntimeError(f"{method} {path} returned non-object JSON: {decoded!r}")
    return decoded


def wait_for_health(base_url: str, timeout_seconds: float) -> None:
    deadline = time.monotonic() + timeout_seconds
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            response = request_json(base_url, "GET", "/assetfactory/health")
            if response.get("status") == "ok":
                return
        except Exception as error:  # noqa: BLE001 - keep polling with useful final error.
            last_error = error
        time.sleep(1.0)
    raise RuntimeError(f"Editor HTTP server did not become healthy at {base_url}: {last_error}")


def ensure_fixture(base_url: str) -> None:
    code = r'''
import unreal

target = "/Game/AssetDocumentSmoke/AS_PostImportSidecarSmoke"
package_path = "/Game/AssetDocumentSmoke"
asset_name = "AS_PostImportSidecarSmoke"
skeleton_path = "/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton"
preview_mesh_path = "/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP"

skeleton = unreal.load_asset(skeleton_path)
if skeleton is None:
    raise RuntimeError("Tutorial skeleton fixture is not available: " + skeleton_path)
preview_mesh = unreal.load_asset(preview_mesh_path)

asset = unreal.load_asset(target)
if asset is None:
    unreal.EditorAssetLibrary.make_directory(package_path)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = None
    factory_cls = getattr(unreal, "AnimSequenceFactory", None)
    if factory_cls is not None:
        factory = factory_cls()
        for prop_name in ("target_skeleton", "skeleton"):
            try:
                factory.set_editor_property(prop_name, skeleton)
                break
            except Exception:
                pass
    asset = asset_tools.create_asset(asset_name, package_path, unreal.AnimSequence, factory)
    if asset is None:
        raise RuntimeError("Failed to create AnimSequence fixture with AssetTools")

if not isinstance(asset, unreal.AnimSequence):
    raise RuntimeError(f"{target} exists but is not an AnimSequence: {asset.get_class().get_name()}")

asset_skeleton = None
try:
    asset_skeleton = asset.get_editor_property("skeleton")
except Exception:
    try:
        asset_skeleton = asset.get_skeleton()
    except Exception:
        asset_skeleton = None
if asset_skeleton is None:
    raise RuntimeError(
        "AnimSequence fixture has no Skeleton. Delete the fixture and rerun so AnimSequenceFactory can create it with "
        + skeleton_path
    )

if preview_mesh is not None:
    try:
        asset.set_preview_mesh(preview_mesh)
    except Exception:
        try:
            asset.set_editor_property("preview_mesh", preview_mesh)
        except Exception:
            pass

try:
    controller = asset.get_controller()
    controller.open_bracket("AssetDocument AnimSequence smoke setup", False)
    controller.initialize_model()
    controller.set_frame_rate(unreal.FrameRate(30, 1), False)
    controller.set_number_of_frames(unreal.FrameNumber(30), False)
    controller.notify_populated()
    controller.close_bracket(False)
except Exception as error:
    unreal.log_warning(f"AnimSequence smoke fixture frame setup skipped: {error}")

unreal.EditorAssetLibrary.save_loaded_asset(asset)
print(asset.get_path_name())
'''
    response = request_json(
        base_url,
        "POST",
        "/assetfactory/execute",
        {"Code": code, "Description": "Prepare AnimSequence AssetDocument smoke fixture"},
    )
    if response.get("success") is not True:
        raise RuntimeError(f"Fixture preparation failed: {json.dumps(response, indent=2)}")


def make_sidecar() -> dict[str, Any]:
    return {
        "SchemaVersion": 1,
        "Target": TARGET,
        "Class": "/Script/Engine.AnimSequence",
        "Action": "Update",
        "Definitions": {},
        "Properties": {},
        "Body": {
            "References": {
                "Skeleton": {
                    "Kind": "AssetRef",
                    "Path": "/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton",
                },
                "RetargetSource": "Default",
            },
            "Preview": {
                "PreviewMesh": {
                    "Kind": "AssetRef",
                    "Path": "/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP",
                }
            },
            "Playback": {"RateScale": 1.5},
            "Additive": {
                "AdditiveAnimType": "AAT_None",
                "RefPoseType": "ABPT_None",
                "RefPoseSeq": None,
            },
            "RootMotion": {
                "bEnableRootMotion": False,
                "RootMotionRootLock": "RefPose",
                "bForceRootLock": False,
                "bUseNormalizedRootMotionScale": False,
            },
            "Compression": {
                "CompressionErrorThresholdScale": 0.5,
                "bDoNotOverrideCompression": False,
            },
            "Curves": [],
            "Notifies": [],
            "NotifyStates": [],
            "NotifyTracks": [],
            "SyncMarkers": [],
            "Metadata": [],
            "AssetUserData": [],
        },
    }


def assert_success(response: dict[str, Any], label: str) -> dict[str, Any]:
    if response.get("success") is not True:
        raise RuntimeError(f"{label} failed: {json.dumps(response, indent=2)}")
    payload = response.get("payload")
    return payload if isinstance(payload, dict) else {}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", default="C:/AVH1/AVH1.uproject")
    parser.add_argument("--port", type=int, default=8559)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--health-timeout", type=float, default=120.0)
    args = parser.parse_args()

    project_path = Path(args.project)
    project_dir = project_path.parent
    sidecar_path = project_dir / SIDECAR_RELATIVE
    sidecar_path.parent.mkdir(parents=True, exist_ok=True)

    base_url = f"http://{args.host}:{args.port}"
    wait_for_health(base_url, args.health_timeout)
    ensure_fixture(base_url)

    sidecar = make_sidecar()
    sidecar_path.write_text(json.dumps(sidecar, indent=2), encoding="utf-8")

    apply_payload = assert_success(
        request_json(
            base_url,
            "POST",
            "/assetfactory/assetdocument/apply-file",
            {"file_path": str(sidecar_path).replace("\\", "/"), "save_asset": True},
        ),
        "apply-file",
    )

    extract_payload = assert_success(
        request_json(
            base_url,
            "POST",
            "/assetfactory/assetdocument/extract",
            {"asset_path": TARGET, "diff_only": False, "include_all_writable": True},
        ),
        "extract",
    )
    body = extract_payload.get("Body")
    if not isinstance(body, dict):
        raise RuntimeError(f"extract payload does not include Body: {json.dumps(extract_payload, indent=2)[:1000]}")
    missing = [region for region in EXPECTED_REGIONS if region not in body]
    if missing:
        raise RuntimeError(f"extract Body missing regions: {missing}")

    diff_payload = assert_success(
        request_json(
            base_url,
            "POST",
            "/assetfactory/assetdocument/diff",
            {"file_path": str(sidecar_path).replace("\\", "/")},
        ),
        "diff",
    )
    changed = diff_payload.get("changed") if isinstance(diff_payload.get("changed"), list) else []
    failed = diff_payload.get("failed") if isinstance(diff_payload.get("failed"), list) else []
    unchanged = diff_payload.get("unchanged") if isinstance(diff_payload.get("unchanged"), list) else []

    if failed:
        raise RuntimeError(f"diff reported failed entries: {json.dumps(failed, indent=2)}")
    changed_paths = {entry.get("path") for entry in changed if isinstance(entry, dict)}
    authored_paths = {f"/Body/{region}" for region in EXPECTED_REGIONS}
    bad_changed = sorted(path for path in authored_paths if path in changed_paths)
    if bad_changed:
        raise RuntimeError(f"authored regions changed after apply: {bad_changed}")
    unchanged_paths = {entry.get("path") for entry in unchanged if isinstance(entry, dict)}
    missing_unchanged = sorted(path for path in authored_paths if path not in unchanged_paths)
    if missing_unchanged:
        raise RuntimeError(f"authored regions missing unchanged diff entries: {missing_unchanged}")

    asset_file = project_dir / "Content" / "AssetDocumentSmoke" / f"{ASSET_NAME}.uasset"
    print("AnimSequence AssetDocument HTTP smoke passed")
    print(f"asset: {TARGET} ({asset_file.as_posix()})")
    print(f"sidecar: {sidecar_path.as_posix()}")
    print(f"apply payload: {json.dumps(apply_payload, sort_keys=True)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
