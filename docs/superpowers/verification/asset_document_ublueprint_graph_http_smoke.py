#!/usr/bin/env python3
"""External UBlueprint graph AssetDocument smoke for a running editor HTTP server."""

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


TARGET = "/Game/AssetDocumentSmoke/BP_GraphSidecarSmoke"
ASSET_NAME = "BP_GraphSidecarSmoke"
DEFAULT_PROJECT = "C:/AVH1/AVH1.uproject"
DEFAULT_BASE_URL = "http://127.0.0.1:8559"
SIDECAR_RELATIVE = Path("Content") / "AssetDocumentSmoke" / f"{ASSET_NAME}.assetdoc.json"
GRAPH_NAME = "EventGraph"
BEGIN_PLAY_NODE = "ReceiveBeginPlay"
PRINT_NODE = "PrintString"
PRINT_TEXT = "Hello from AssetDocument graph smoke"


def _normalize_base_url(base_url: str) -> str:
    return base_url.rstrip("/")


def _route(base_url: str, path: str) -> str:
    if base_url.endswith("/assetfactory") and path.startswith("/assetfactory/"):
        return base_url + path[len("/assetfactory") :]
    return base_url + path


def request_json(base_url: str, method: str, path: str, payload: dict[str, Any] | None = None) -> dict[str, Any]:
    data = None
    headers = {"Accept": "application/json"}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"

    request = urllib.request.Request(_route(base_url, path), data=data, headers=headers, method=method)
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


def wait_for_health(base_url: str, timeout_seconds: float, poll_interval: float) -> None:
    deadline = time.monotonic() + timeout_seconds
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            response = request_json(base_url, "GET", "/assetfactory/health")
            if response.get("status") == "ok" or response.get("success") is True:
                return
        except Exception as error:  # noqa: BLE001 - keep polling with useful final error.
            last_error = error
        time.sleep(poll_interval)
    raise RuntimeError(f"Editor HTTP server did not become healthy at {base_url}: {last_error}")


def make_class_ref(class_path: str) -> dict[str, Any]:
    return {"Kind": "ClassRef", "Class": class_path}


def make_member_ref(owner_class: str, name: str) -> dict[str, Any]:
    return {"Kind": "MemberRef", "OwnerClass": owner_class, "Name": name}


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
            "Variables": [],
            "Components": [],
            "ClassDefaults": {},
            "UbergraphPages": [
                {
                    "Name": GRAPH_NAME,
                    "Schema": "/Script/BlueprintGraph.EdGraphSchema_K2",
                    "Nodes": [
                        {
                            "Id": BEGIN_PLAY_NODE,
                            "Class": "/Script/BlueprintGraph.K2Node_Event",
                            "Member": make_member_ref("/Script/Engine.Actor", "ReceiveBeginPlay"),
                            "Position": {"X": 0, "Y": 0},
                        },
                        {
                            "Id": PRINT_NODE,
                            "Class": "/Script/BlueprintGraph.K2Node_CallFunction",
                            "Member": make_member_ref("/Script/Engine.KismetSystemLibrary", "PrintString"),
                            "PinOverrides": [
                                {
                                    "Pin": "InString",
                                    "DefaultValue": PRINT_TEXT,
                                }
                            ],
                            "Position": {"X": 320, "Y": 0},
                        },
                    ],
                    "Links": [
                        {
                            "From": {"Node": BEGIN_PLAY_NODE, "Pin": "then"},
                            "To": {"Node": PRINT_NODE, "Pin": "execute"},
                        }
                    ],
                }
            ],
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


def _find_graph(body: dict[str, Any], name: str) -> dict[str, Any]:
    graphs = body.get("UbergraphPages")
    if not isinstance(graphs, list):
        raise RuntimeError(f"extract Body.UbergraphPages is not a list: {json.dumps(graphs, indent=2)}")
    for graph in graphs:
        if isinstance(graph, dict) and graph.get("Name") == name:
            return graph
    raise RuntimeError(f"extract Body.UbergraphPages missing graph {name}: {json.dumps(graphs, indent=2)}")


def _find_node(graph: dict[str, Any], node_id: str) -> dict[str, Any]:
    nodes = graph.get("Nodes")
    if not isinstance(nodes, list):
        raise RuntimeError(f"extract graph Nodes is not a list: {json.dumps(nodes, indent=2)}")
    for node in nodes:
        if isinstance(node, dict) and node.get("Id") == node_id:
            return node
    raise RuntimeError(f"extract graph missing node {node_id}: {json.dumps(nodes, indent=2)}")


def assert_extracted_graph(extracted: dict[str, Any]) -> None:
    if extracted.get("Target") != TARGET:
        raise RuntimeError(f"extract returned wrong Target: {json.dumps(extracted, indent=2)[:1000]}")
    body = extracted.get("Body")
    if not isinstance(body, dict):
        raise RuntimeError(f"extract payload does not include Body: {json.dumps(extracted, indent=2)[:1000]}")
    graph = _find_graph(body, GRAPH_NAME)
    begin_play = _find_node(graph, BEGIN_PLAY_NODE)
    print_node = _find_node(graph, PRINT_NODE)
    if begin_play.get("Class") != "/Script/BlueprintGraph.K2Node_Event":
        raise RuntimeError(f"BeginPlay node has unexpected Class: {json.dumps(begin_play, indent=2)}")
    if print_node.get("Class") != "/Script/BlueprintGraph.K2Node_CallFunction":
        raise RuntimeError(f"Print node has unexpected Class: {json.dumps(print_node, indent=2)}")
    begin_play_member = begin_play.get("Member") if isinstance(begin_play.get("Member"), dict) else {}
    print_member = print_node.get("Member") if isinstance(print_node.get("Member"), dict) else {}
    if begin_play_member.get("Name") != "ReceiveBeginPlay":
        raise RuntimeError(f"BeginPlay node has unexpected Member: {json.dumps(begin_play, indent=2)}")
    if print_member.get("Name") != "PrintString":
        raise RuntimeError(f"Print node has unexpected Member: {json.dumps(print_node, indent=2)}")


def _entries(payload: dict[str, Any], bucket: str) -> list[dict[str, Any]]:
    value = payload.get(bucket)
    if not isinstance(value, list):
        return []
    return [entry for entry in value if isinstance(entry, dict)]


def _entry_path(entry: dict[str, Any]) -> str:
    value = entry.get("path", entry.get("Path"))
    return value if isinstance(value, str) else ""


def _entry_status(entry: dict[str, Any]) -> str:
    value = entry.get("status", entry.get("Status"))
    return value if isinstance(value, str) else ""


def _strip_generated_graph_metadata(value: Any) -> Any:
    if isinstance(value, dict):
        stripped: dict[str, Any] = {}
        for key, child in value.items():
            if key in {"GraphGuid", "NodeGuid", "Capability"}:
                continue
            stripped[key] = _strip_generated_graph_metadata(child)
        return stripped
    if isinstance(value, list):
        return [_strip_generated_graph_metadata(child) for child in value]
    return value


def _is_generated_metadata_only_change(entry: dict[str, Any]) -> bool:
    desired = entry.get("desired", entry.get("Desired"))
    current = entry.get("current", entry.get("Current"))
    if desired is None or current is None:
        return False
    return _strip_generated_graph_metadata(desired) == _strip_generated_graph_metadata(current)


def assert_no_unexpected_graph_changes(diff_payload: dict[str, Any]) -> None:
    failed = _entries(diff_payload, "failed")
    skipped = _entries(diff_payload, "skipped")
    changed = _entries(diff_payload, "changed")

    if failed:
        raise RuntimeError(f"diff reported failed entries: {json.dumps(failed, indent=2)}")
    graph_skipped = [entry for entry in skipped if _entry_path(entry).startswith("/Body/UbergraphPages")]
    if graph_skipped:
        raise RuntimeError(f"diff reported skipped graph entries: {json.dumps(graph_skipped, indent=2)}")

    bad_changed = []
    for entry in changed:
        path = _entry_path(entry)
        if path.startswith("/Body/UbergraphPages"):
            if not _is_generated_metadata_only_change(entry):
                bad_changed.append(entry)
        elif path in {"/Body/ParentClass", "/Body/FunctionGraphs", "/Body/MacroGraphs", "/Body/Timelines"}:
            bad_changed.append(entry)
    if bad_changed:
        raise RuntimeError(f"diff reported changed graph sidecar entries after apply: {json.dumps(bad_changed, indent=2)}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the external UBlueprint graph AssetDocument HTTP smoke.")
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL)
    parser.add_argument("--project", default=DEFAULT_PROJECT)
    parser.add_argument("--health-timeout", type=float, default=120.0)
    parser.add_argument("--poll-interval", type=float, default=1.0)
    parser.add_argument("--no-save-asset", action="store_true")
    args = parser.parse_args()

    base_url = _normalize_base_url(args.base_url)
    project_path = Path(args.project)
    project_dir = project_path.parent
    sidecar_path = project_dir / SIDECAR_RELATIVE
    sidecar_path.parent.mkdir(parents=True, exist_ok=True)

    try:
        wait_for_health(base_url, args.health_timeout, args.poll_interval)

        sidecar = make_sidecar()
        sidecar_path.write_text(json.dumps(sidecar, indent=2), encoding="utf-8")

        apply_payload = assert_success(
            request_json(
                base_url,
                "POST",
                "/assetfactory/assetdocument/apply-file",
                {"file_path": sidecar_path.as_posix(), "save_asset": not args.no_save_asset},
            ),
            "apply-file",
        )
        if apply_payload.get("sidecar_sync_update_skipped") is True:
            raise RuntimeError(f"apply-file skipped sidecar sync update: {json.dumps(apply_payload, indent=2)}")

        extract_payload = assert_success(
            request_json(
                base_url,
                "POST",
                "/assetfactory/assetdocument/extract",
                {"asset_path": TARGET, "diff_only": False, "include_all_writable": True},
            ),
            "extract",
        )
        assert_extracted_graph(extract_payload)

        diff_payload = assert_success(
            request_json(
                base_url,
                "POST",
                "/assetfactory/assetdocument/diff",
                {"file_path": sidecar_path.as_posix()},
            ),
            "diff",
        )
        assert_no_unexpected_graph_changes(diff_payload)
    except Exception as error:  # noqa: BLE001 - CLI smoke should print a concise actionable failure.
        print(str(error), file=sys.stderr)
        return 1

    asset_file = project_dir / "Content" / "AssetDocumentSmoke" / f"{ASSET_NAME}.uasset"
    print("UBlueprint graph AssetDocument HTTP smoke passed")
    print(f"asset: {TARGET} ({asset_file.as_posix()})")
    print(f"sidecar: {sidecar_path.as_posix()}")
    print(f"apply payload: {json.dumps(apply_payload, sort_keys=True)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
