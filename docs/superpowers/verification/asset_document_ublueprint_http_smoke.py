#!/usr/bin/env python3
"""External UBlueprint AssetDocument smoke for a running editor HTTP server."""

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


TARGET = "/Game/AssetDocumentSmoke/BP_BlueprintSidecarSmoke"
ASSET_NAME = "BP_BlueprintSidecarSmoke"
SIDECAR_RELATIVE = Path("Content") / "AssetDocumentSmoke" / f"{ASSET_NAME}.assetdoc.json"
EXPECTED_VARIABLE = "Health"
EXPECTED_COMPONENT = "Sensor"
EXPECTED_COMPONENT_DIFF_PATH = "/Body/Components/Self:Sensor"
EXPECTED_VARIABLE_DIFF_PATH = "/Body/Variables/Health"


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


def make_class_ref(class_path: str) -> dict[str, Any]:
    return {"Kind": "ClassRef", "Class": class_path}


def make_component_key(name: str, owner_class: str = "Self") -> dict[str, Any]:
    return {"Name": name, "OwnerClass": owner_class}


def make_sidecar() -> dict[str, Any]:
    return {
        "SchemaVersion": 1,
        "Target": TARGET,
        "Class": "/Script/Engine.Blueprint",
        "Action": "CreateOrUpdate",
        "Definitions": {},
        "Properties": {},
        "Body": {
            "ParentClass": make_class_ref("/Script/Engine.Actor"),
            "ImplementedInterfaces": [],
            "Variables": [
                {
                    "Name": EXPECTED_VARIABLE,
                    "Type": {
                        "PinCategory": "real",
                        "PinSubCategory": "float",
                    },
                    "DefaultValue": "100.0",
                    "Category": "AssetDocument",
                    "Tooltip": "Created by UBlueprint AssetDocument smoke.",
                }
            ],
            "Components": [
                {
                    "Scope": "OwnedSCS",
                    "Key": make_component_key(EXPECTED_COMPONENT),
                    "Class": "/Script/Engine.SphereComponent",
                    "AttachTo": make_component_key("DefaultSceneRoot"),
                    "Properties": {
                        "SphereRadius": 500.0,
                    },
                }
            ],
            "ClassDefaults": {},
            "UbergraphPages": [],
            "FunctionGraphs": [],
            "MacroGraphs": [],
            "Timelines": [],
        },
    }


def assert_success(response: dict[str, Any], label: str) -> dict[str, Any]:
    if response.get("success") is not True:
        raise RuntimeError(f"{label} failed: {json.dumps(response, indent=2)}")
    payload = response.get("payload")
    return payload if isinstance(payload, dict) else {}


def find_object_by_name(entries: Any, name: str) -> dict[str, Any] | None:
    if not isinstance(entries, list):
        return None
    for entry in entries:
        if isinstance(entry, dict) and entry.get("Name") == name:
            return entry
        if isinstance(entry, dict):
            key = entry.get("Key")
            if isinstance(key, dict) and key.get("Name") == name:
                return entry
    return None


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

    sidecar = make_sidecar()
    sidecar_path.write_text(json.dumps(sidecar, indent=2), encoding="utf-8")

    apply_response = request_json(
        base_url,
        "POST",
        "/assetfactory/assetdocument/apply-file",
        {"file_path": str(sidecar_path).replace("\\", "/"), "save_asset": True},
    )
    apply_payload = assert_success(apply_response, "apply-file")
    sync_note = ""
    if apply_response.get("wrote_sidecar") is not True:
        sync_note = f"sidecar sync not rewritten: {json.dumps(apply_payload, sort_keys=True)}"

    extract_payload = assert_success(
        request_json(
            base_url,
            "POST",
            "/assetfactory/assetdocument/extract",
            {"asset_path": TARGET, "diff_only": False, "include_all_writable": True},
        ),
        "extract",
    )
    if extract_payload.get("Target") != TARGET:
        raise RuntimeError(f"extract returned wrong Target: {json.dumps(extract_payload, indent=2)[:1000]}")
    body = extract_payload.get("Body")
    if not isinstance(body, dict):
        raise RuntimeError(f"extract payload does not include Body: {json.dumps(extract_payload, indent=2)[:1000]}")

    health = find_object_by_name(body.get("Variables"), EXPECTED_VARIABLE)
    if health is None:
        raise RuntimeError(f"extract Variables missing {EXPECTED_VARIABLE}: {json.dumps(body.get('Variables'), indent=2)}")

    sensor = find_object_by_name(body.get("Components"), EXPECTED_COMPONENT)
    if sensor is None:
        raise RuntimeError(f"extract Components missing {EXPECTED_COMPONENT}: {json.dumps(body.get('Components'), indent=2)}")
    if sensor.get("Scope") != "OwnedSCS":
        raise RuntimeError(f"extract component has unexpected Scope: {json.dumps(sensor, indent=2)}")

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
    authored_paths = {EXPECTED_VARIABLE_DIFF_PATH, EXPECTED_COMPONENT_DIFF_PATH}
    bad_changed = sorted(path for path in authored_paths if path in changed_paths)
    if bad_changed:
        raise RuntimeError(f"authored regions changed after apply: {bad_changed}")
    unchanged_paths = {entry.get("path") for entry in unchanged if isinstance(entry, dict)}
    missing_unchanged = sorted(path for path in authored_paths if path not in unchanged_paths)
    if missing_unchanged:
        raise RuntimeError(f"authored regions missing unchanged diff entries: {missing_unchanged}")

    asset_file = project_dir / "Content" / "AssetDocumentSmoke" / f"{ASSET_NAME}.uasset"
    print("UBlueprint AssetDocument HTTP smoke passed")
    print(f"asset: {TARGET} ({asset_file.as_posix()})")
    print(f"sidecar: {sidecar_path.as_posix()}")
    if sync_note:
        print(sync_note)
    print(f"apply payload: {json.dumps(apply_payload, sort_keys=True)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
