#!/usr/bin/env python3
"""Production two-phase HTTP + MCP smoke for BT/BB AssetDocument.

This client never starts or stops Unreal Editor. ``pre-restart`` authors and
verifies the fixtures, then emits a resume token. ``post-restart`` requires a
new listener PID and a rewritten service.json before doing fresh extract/diff.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shlex
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Iterator


THIS_DIR = Path(__file__).resolve().parent
REPO_DIR = THIS_DIR.parents[2]
DEFAULT_FIXTURE_DIR = THIS_DIR / "fixtures" / "btbb"
DEFAULT_MCP_SCRIPT = REPO_DIR / "MCP" / "scripts" / "btbb_asset_document_live_smoke.mjs"
DEFAULT_ASSET_ROOT = "/Game/AssetDocumentSmoke/BTBB"
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8562

FIXTURE_SPECS = {
    "blackboard_parent": ("blackboard_parent.assetdoc.json", "BB_BTBB_Parent", "/Script/AIModule.BlackboardData"),
    "blackboard": ("blackboard.assetdoc.json", "BB_BTBB_Main", "/Script/AIModule.BlackboardData"),
    "behavior_tree_subtree": ("behavior_tree_subtree.assetdoc.json", "BT_BTBB_Subtree", "/Script/AIModule.BehaviorTree"),
    "behavior_tree": ("behavior_tree.assetdoc.json", "BT_BTBB_Main", "/Script/AIModule.BehaviorTree"),
}
APPLY_ORDER = ["blackboard_parent", "blackboard", "behavior_tree_subtree", "behavior_tree"]
PRIMARY_DOCUMENTS = ["blackboard", "behavior_tree"]
FAILURE_BUCKETS = ("changed", "added", "removed", "missing", "extra", "skipped", "failed", "errors")
LOOPBACK_HOSTS = {"localhost", "127.0.0.1"}
REQUIRED_SERVICE_ENDPOINTS = {
    ("GET", "/assetfactory/health"),
    ("POST", "/assetfactory/assetdocument/apply"),
    ("POST", "/assetfactory/assetdocument/apply-file"),
    ("GET", "/assetfactory/assetdocument/schema"),
    ("GET", "/assetfactory/assetdocument/inspect"),
    ("GET", "/assetfactory/assetdocument/profile"),
    ("POST", "/assetfactory/assetdocument/template"),
    ("POST", "/assetfactory/assetdocument/extract"),
    ("POST", "/assetfactory/assetdocument/validate"),
    ("POST", "/assetfactory/assetdocument/diff"),
}


class SmokeFailure(RuntimeError):
    """A live-smoke contract gate failed."""


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SmokeFailure(f"Failed to read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise SmokeFailure(f"Expected JSON object in {path}")
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return "sha256:" + digest.hexdigest()


def replace_token(value: Any, token: str, replacement: str) -> Any:
    if isinstance(value, str):
        return value.replace(token, replacement)
    if isinstance(value, list):
        return [replace_token(entry, token, replacement) for entry in value]
    if isinstance(value, dict):
        return {key: replace_token(entry, token, replacement) for key, entry in value.items()}
    return value


def target_to_project_file(project: Path, target: str, suffix: str) -> Path:
    if not target.startswith("/Game/") or target.endswith("/"):
        raise SmokeFailure(f"Smoke target must be a /Game package path: {target}")
    relative = target.removeprefix("/Game/")
    return project.parent / "Content" / f"{relative}{suffix}"


def materialize_fixtures(project: Path, fixture_dir: Path, asset_root: str) -> dict[str, Any]:
    asset_root = asset_root.rstrip("/")
    documents: dict[str, Any] = {}
    for name, (fixture_name, asset_name, class_path) in FIXTURE_SPECS.items():
        source_path = fixture_dir / fixture_name
        source = read_json(source_path)
        rendered = replace_token(source, "__ASSET_ROOT__", asset_root)
        target = f"{asset_root}/{asset_name}"
        if rendered.get("Target") != target:
            raise SmokeFailure(f"{fixture_name} rendered Target mismatch: {rendered.get('Target')} != {target}")
        sidecar = target_to_project_file(project, target, ".assetdoc.json")
        asset_file = target_to_project_file(project, target, ".uasset")
        write_json(sidecar, rendered)
        documents[name] = {
            "name": name,
            "class": class_path,
            "target": target,
            "object_path": f"{target}.{asset_name}",
            "source_fixture": str(source_path),
            "sidecar": str(sidecar),
            "asset_file": str(asset_file),
        }
    return {
        "schema_version": 1,
        "asset_root": asset_root,
        "project": str(project),
        "documents": documents,
        "apply_order": list(APPLY_ORDER),
        "primary_documents": list(PRIMARY_DOCUMENTS),
    }


def walk_tree_nodes(root: dict[str, Any]) -> Iterator[dict[str, Any]]:
    yield root
    for child in root.get("Children", []):
        if isinstance(child, dict):
            yield from walk_tree_nodes(child)


def _collect_fixture_failures(manifest: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    documents = manifest.get("documents")
    if not isinstance(documents, dict):
        return ["manifest documents must be an object"]

    for name, entry in documents.items():
        try:
            sidecar = Path(entry["sidecar"])
            document = read_json(sidecar)
            if "__ASSET_ROOT__" in sidecar.read_text(encoding="utf-8"):
                failures.append(f"{name}: unresolved __ASSET_ROOT__ token")
            if document.get("Target") != entry["target"]:
                failures.append(f"{name}: Target does not match manifest")
            if document.get("Class") != entry["class"]:
                failures.append(f"{name}: Class does not match manifest")
            if document.get("Action") != "CreateOrUpdate":
                failures.append(f"{name}: Action must be CreateOrUpdate")
        except (KeyError, OSError, SmokeFailure) as error:
            failures.append(f"{name}: {error}")

    try:
        parent = read_json(Path(documents["blackboard_parent"]["sidecar"]))
        blackboard = read_json(Path(documents["blackboard"]["sidecar"]))
        parent_keys = parent["Body"]["Keys"]
        local_keys = blackboard["Body"]["Keys"]
        if blackboard["Body"].get("Parent") is None:
            failures.append("blackboard: Parent must be authored")
        if not {"TargetActor", "HasTarget"}.issubset({entry.get("Name") for entry in parent_keys}):
            failures.append("blackboard_parent: inherited Object/Bool keys missing")
        key_classes = {entry.get("KeyTypeClass", {}).get("Path") for entry in local_keys}
        expected_key_classes = {
            "/Script/AIModule.BlackboardKeyType_Vector",
            "/Script/AIModule.BlackboardKeyType_Name",
            "/Script/AIModule.BlackboardKeyType_Float",
            "/Script/AIModule.BlackboardKeyType_Class",
            "/Script/AIModule.BlackboardKeyType_Enum",
        }
        missing_classes = expected_key_classes - key_classes
        if missing_classes:
            failures.append(f"blackboard: missing key classes {sorted(missing_classes)}")

        behavior_tree = read_json(Path(documents["behavior_tree"]["sidecar"]))
        tree = behavior_tree["Body"]["Tree"]
        root = tree["Root"]
        nodes = list(walk_tree_nodes(root))
        if root.get("Class") != "/Script/AIModule.BTComposite_Selector":
            failures.append("behavior_tree: root is not selector")
        if not any(node is not root and node.get("Children") for node in nodes):
            failures.append("behavior_tree: nested composite/children missing")
        if not any(node.get("Class") == "/Script/AIModule.BTTask_RunBehavior" for node in nodes):
            failures.append("behavior_tree: subtree task missing")
        if not root.get("Services"):
            failures.append("behavior_tree: service missing")
        decorators = [decorator for node in nodes for decorator in node.get("Decorators", [])]
        if not any(entry.get("Class") == "/Script/AIModule.BTDecorator_Blackboard" for entry in decorators):
            failures.append("behavior_tree: ordinary decorator missing")
        composite = next((entry for entry in decorators if entry.get("Kind") == "Composite"), None)
        if not composite:
            failures.append("behavior_tree: composite decorator missing")
        else:
            kinds = {entry.get("Kind") for entry in composite.get("BoundGraph", {}).get("Nodes", [])}
            if not {"Sink", "Not", "Or", "Test"}.issubset(kinds):
                failures.append("behavior_tree: decorator logic coverage missing")
        if not tree.get("Comments") or any("Editor" not in node for node in nodes):
            failures.append("behavior_tree: layout/comment authorship missing")
    except (KeyError, TypeError, SmokeFailure) as error:
        failures.append(f"fixture coverage: {error}")
    return failures


def validate_fixture_manifest(manifest: dict[str, Any]) -> dict[str, list[str]]:
    documents = manifest.get("documents", {})
    actual = set(documents) if isinstance(documents, dict) else set()
    expected = set(FIXTURE_SPECS)
    return {
        "missing": sorted(expected - actual),
        "extra": sorted(actual - expected),
        "failed": _collect_fixture_failures(manifest),
    }


def assert_zero_gate(report: dict[str, Any], label: str) -> None:
    nonempty = {key: value for key, value in report.items() if key in FAILURE_BUCKETS and value}
    if nonempty:
        raise SmokeFailure(f"{label} gate failed: {json.dumps(nonempty, sort_keys=True)}")


def response_payload(response: dict[str, Any]) -> dict[str, Any]:
    payload = response.get("payload")
    return payload if isinstance(payload, dict) else {}


def assert_clean_response(response: dict[str, Any], label: str) -> dict[str, Any]:
    if response.get("success") is not True:
        raise SmokeFailure(f"{label} success was not true: {json.dumps(response, sort_keys=True)[:2000]}")
    diagnostics = response.get("diagnostics")
    if diagnostics:
        raise SmokeFailure(f"{label} diagnostics were nonempty: {json.dumps(diagnostics, sort_keys=True)}")
    for container_name, container in (("response", response), ("payload", response_payload(response))):
        for bucket in FAILURE_BUCKETS:
            if container.get(bucket):
                raise SmokeFailure(f"{label} {container_name}.{bucket} was nonempty: {json.dumps(container[bucket], sort_keys=True)}")
        if container.get("sidecar_sync_update_skipped"):
            raise SmokeFailure(f"{label} sync was skipped")
        if container.get("sidecar_sync_update_skip_reason"):
            raise SmokeFailure(f"{label} sync skip reason was present: {container['sidecar_sync_update_skip_reason']}")
    return response_payload(response)


@dataclass
class EvidenceWriter:
    root: Path
    counter: int = 0

    def __post_init__(self) -> None:
        self.root.mkdir(parents=True, exist_ok=True)

    def next_path(self, prefix: str, suffix: str = ".json") -> Path:
        self.counter += 1
        safe = "".join(character if character.isalnum() or character in "-_" else "_" for character in prefix)
        return self.root / f"{self.counter:03d}_{safe}{suffix}"

    def record(self, prefix: str, value: Any) -> Path:
        path = self.next_path(prefix)
        write_json(path, value)
        return path


class HttpClient:
    def __init__(self, host: str, port: int, timeout: float, evidence: EvidenceWriter):
        self.base_url = f"http://{host}:{port}"
        self.timeout = timeout
        self.evidence = evidence

    def request(self, method: str, path: str, payload: dict[str, Any] | None = None, label: str | None = None) -> dict[str, Any]:
        data = json.dumps(payload).encode("utf-8") if payload is not None else None
        headers = {"Accept": "application/json"}
        if data is not None:
            headers["Content-Type"] = "application/json"
        request_label = label or path.strip("/").replace("/", "_")
        request_evidence = {
            "timestamp_utc": utc_now(),
            "method": method,
            "url": self.base_url + path,
            "payload": payload,
        }
        self.evidence.record(f"http_{request_label}_request", request_evidence)
        request = urllib.request.Request(self.base_url + path, data=data, headers=headers, method=method)
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                raw = response.read().decode("utf-8")
                status = response.status
        except urllib.error.HTTPError as error:
            detail = error.read().decode("utf-8", errors="replace")
            self.evidence.record(f"http_{request_label}_response", {"status": error.code, "raw": detail})
            raise SmokeFailure(f"{method} {path} failed with HTTP {error.code}: {detail[:2000]}") from error
        except urllib.error.URLError as error:
            raise SmokeFailure(f"{method} {path} failed: {error}") from error
        try:
            decoded = json.loads(raw)
        except json.JSONDecodeError as error:
            raise SmokeFailure(f"{method} {path} returned invalid JSON: {raw[:2000]}") from error
        if not isinstance(decoded, dict):
            raise SmokeFailure(f"{method} {path} returned non-object JSON")
        self.evidence.record(f"http_{request_label}_response", {"status": status, "body": decoded})
        return decoded

    def wait_for_health(self, timeout: float) -> dict[str, Any]:
        deadline = time.monotonic() + timeout
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            try:
                health = self.request("GET", "/assetfactory/health", label="health")
                if health.get("status") == "ok":
                    return health
                last_error = SmokeFailure(f"health status was {health.get('status')!r}")
            except Exception as error:  # noqa: BLE001 - preserve last polling failure.
                last_error = error
            time.sleep(1.0)
        raise SmokeFailure(f"Health did not become ready at {self.base_url}: {last_error}")


def parse_listener_pids(output: str) -> list[dict[str, Any]]:
    by_pid: dict[int, dict[str, Any]] = {}
    current_pid: int | None = None
    for line in output.splitlines():
        if not line:
            continue
        kind, value = line[0], line[1:]
        if kind == "p":
            try:
                current_pid = int(value)
            except ValueError:
                current_pid = None
            if current_pid is not None:
                by_pid.setdefault(current_pid, {"pid": current_pid, "names": []})
        elif current_pid is not None and kind == "c":
            by_pid[current_pid]["process_name"] = value
        elif current_pid is not None and kind == "n":
            by_pid[current_pid]["names"].append(value)
    return list(by_pid.values())


def assert_service_identity(service: dict[str, Any], requested_host: str, requested_port: int) -> None:
    if service.get("service") != "AssetFactory":
        raise SmokeFailure(f"service.json service identity is not AssetFactory: {service.get('service')!r}")
    try:
        service_port = int(service.get("port", -1))
    except (TypeError, ValueError) as error:
        raise SmokeFailure(f"service.json port is invalid: {service.get('port')!r}") from error
    if service_port != requested_port:
        raise SmokeFailure(f"service.json port {service_port} != requested {requested_port}")

    normalized_requested_host = requested_host.strip().lower()
    normalized_service_host = str(service.get("host", "")).strip().lower()
    if normalized_requested_host not in LOOPBACK_HOSTS:
        raise SmokeFailure(f"Requested host must be localhost or 127.0.0.1, got {requested_host!r}")
    if normalized_service_host not in LOOPBACK_HOSTS:
        raise SmokeFailure(f"service.json host must be localhost or 127.0.0.1, got {service.get('host')!r}")

    base_url = service.get("baseUrl")
    if not isinstance(base_url, str):
        raise SmokeFailure(f"service.json baseUrl must be a string, got {base_url!r}")
    parsed_url = urllib.parse.urlparse(base_url)
    try:
        base_url_port = parsed_url.port
    except ValueError as error:
        raise SmokeFailure(f"service.json baseUrl has an invalid port: {base_url!r}") from error
    if (
        parsed_url.scheme != "http"
        or (parsed_url.hostname or "").lower() not in LOOPBACK_HOSTS
        or base_url_port != requested_port
        or parsed_url.path not in ("", "/")
        or parsed_url.params
        or parsed_url.query
        or parsed_url.fragment
        or parsed_url.username
        or parsed_url.password
    ):
        raise SmokeFailure(
            f"service.json baseUrl must point exactly at loopback port {requested_port}, got {base_url!r}"
        )

    endpoints = service.get("endpoints")
    if not isinstance(endpoints, list):
        raise SmokeFailure("service.json endpoints must be an array")
    actual_endpoints = {
        (str(endpoint.get("method", "")).strip().upper(), endpoint.get("path"))
        for endpoint in endpoints
        if isinstance(endpoint, dict)
    }
    missing_endpoints = REQUIRED_SERVICE_ENDPOINTS - actual_endpoints
    if missing_endpoints:
        raise SmokeFailure(f"service.json endpoints missing required routes: {sorted(missing_endpoints)}")

    start_time = service.get("startTime")
    if not isinstance(start_time, str) or not start_time.strip():
        raise SmokeFailure("service.json startTime is missing")
    try:
        parsed_start_time = datetime.fromisoformat(start_time.replace("Z", "+00:00"))
    except ValueError as error:
        raise SmokeFailure(f"service.json startTime is not ISO-8601: {start_time!r}") from error
    if parsed_start_time.tzinfo is None:
        raise SmokeFailure(f"service.json startTime must include a timezone: {start_time!r}")


def assert_listener_identity(command: str, engine: Path, project: Path) -> dict[str, Any]:
    try:
        tokens = shlex.split(command)
    except ValueError as error:
        raise SmokeFailure(f"Listener command could not be parsed: {error}") from error
    if not tokens:
        raise SmokeFailure("Listener command is empty")

    engine_root = engine.expanduser().resolve()
    editor_path = Path(tokens[0]).expanduser().resolve()
    editor_kind = editor_path.name.removesuffix(".exe")
    if editor_kind not in {"UnrealEditor", "UnrealEditor-Cmd"}:
        raise SmokeFailure(f"Listener executable must be UnrealEditor or UnrealEditor-Cmd, got {editor_path}")
    try:
        relative_editor_path = editor_path.relative_to(engine_root)
    except ValueError as error:
        raise SmokeFailure(f"Listener UnrealEditor is not from requested engine {engine_root}: {editor_path}") from error
    if len(relative_editor_path.parts) < 3 or relative_editor_path.parts[:2] != ("Engine", "Binaries"):
        raise SmokeFailure(
            f"Listener UnrealEditor is not under requested engine's Engine/Binaries directory: {editor_path}"
        )

    expected_project = project.expanduser().resolve()
    command_projects: list[Path] = []
    for token in tokens[1:]:
        candidate = token
        if token.lower().startswith("-project="):
            candidate = token.split("=", 1)[1]
        if candidate.lower().endswith(".uproject"):
            command_projects.append(Path(candidate).expanduser().resolve())
    if expected_project not in command_projects:
        raise SmokeFailure(
            f"Listener command does not contain requested project {expected_project}; found {[str(path) for path in command_projects]}"
        )
    return {
        "editor_path": str(editor_path),
        "editor_kind": editor_kind,
        "project_path": str(expected_project),
    }


def capture_service_evidence(
    project: Path,
    engine: Path,
    host: str,
    port: int,
    client: HttpClient,
    health_timeout: float,
    evidence: EvidenceWriter,
    label: str,
) -> dict[str, Any]:
    service_path = project.parent / "Saved" / "AssetFactory" / "service.json"
    if not service_path.is_file():
        raise SmokeFailure(f"service.json missing: {service_path}")
    service = read_json(service_path)
    assert_service_identity(service, host, port)

    lsof = subprocess.run(
        ["lsof", "-nP", f"-iTCP:{port}", "-sTCP:LISTEN", "-Fpctn"],
        text=True,
        capture_output=True,
        check=False,
    )
    if lsof.returncode != 0:
        raise SmokeFailure(f"lsof found no listener on {port}: {lsof.stderr.strip()}")
    listeners = parse_listener_pids(lsof.stdout)
    if len(listeners) != 1:
        raise SmokeFailure(f"Expected exactly one listener PID on {port}, got {listeners}")
    listener = listeners[0]
    ps = subprocess.run(
        ["ps", "-p", str(listener["pid"]), "-o", "command="],
        text=True,
        capture_output=True,
        check=False,
    )
    if ps.returncode != 0 or not ps.stdout.strip():
        raise SmokeFailure(f"Failed to read listener command for PID {listener['pid']}: {ps.stderr.strip()}")
    listener["command"] = ps.stdout.strip()
    listener["identity"] = assert_listener_identity(listener["command"], engine, project)

    health = client.wait_for_health(health_timeout)
    if int(health.get("port", -1)) != port or health.get("subsystemAvailable") is not True:
        raise SmokeFailure(f"Health identity/subsystem mismatch: {json.dumps(health, sort_keys=True)}")
    snapshot = {
        "timestamp_utc": utc_now(),
        "project": str(project),
        "engine": str(engine),
        "expected_editor_binaries": [
            str(engine / "Engine" / "Binaries" / "Mac" / "UnrealEditor.app" / "Contents" / "MacOS" / "UnrealEditor"),
            str(engine / "Engine" / "Binaries" / "Mac" / "UnrealEditor"),
            str(engine / "Engine" / "Binaries" / "Mac" / "UnrealEditor-Cmd.app" / "Contents" / "MacOS" / "UnrealEditor-Cmd"),
            str(engine / "Engine" / "Binaries" / "Mac" / "UnrealEditor-Cmd"),
        ],
        "service_json_path": str(service_path),
        "service_json_mtime_ns": service_path.stat().st_mtime_ns,
        "service_start_time": service["startTime"],
        "service_json": service,
        "listener": listener,
        "health": health,
    }
    evidence.record(f"service_{label}", snapshot)
    return snapshot


def assert_fresh_restart(token: dict[str, Any], current: dict[str, Any]) -> None:
    previous_pid = token.get("listener", {}).get("pid")
    current_pid = current.get("listener", {}).get("pid")
    if not previous_pid or current_pid == previous_pid:
        raise SmokeFailure(f"Post-restart listener PID must differ from pre-restart PID {previous_pid}; got {current_pid}")
    previous_mtime = int(token.get("service_json_mtime_ns", 0))
    current_mtime = int(current.get("service_json_mtime_ns", 0))
    if current_mtime <= previous_mtime:
        raise SmokeFailure(f"Post-restart service.json must be newer than token ({current_mtime} <= {previous_mtime})")
    previous_start_time = token.get("service_start_time")
    current_start_time = current.get("service_start_time")
    if not previous_start_time or not current_start_time or current_start_time == previous_start_time:
        raise SmokeFailure(
            f"Post-restart service.json startTime must change; before={previous_start_time!r}, after={current_start_time!r}"
        )


def assert_schema(response: dict[str, Any]) -> None:
    if response.get("success") is not True:
        raise SmokeFailure(f"schema success was not true: {json.dumps(response, sort_keys=True)}")
    routes = {(route.get("method"), route.get("path")) for route in response.get("routes", []) if isinstance(route, dict)}
    expected = {
        ("GET", "/assetfactory/assetdocument/schema"),
        ("GET", "/assetfactory/assetdocument/profile"),
        ("POST", "/assetfactory/assetdocument/template"),
        ("POST", "/assetfactory/assetdocument/validate"),
        ("POST", "/assetfactory/assetdocument/apply-file"),
        ("POST", "/assetfactory/assetdocument/extract"),
        ("POST", "/assetfactory/assetdocument/diff"),
    }
    missing = expected - routes
    if missing:
        raise SmokeFailure(f"schema missing routes: {sorted(missing)}")


def assert_profile_or_template(response: dict[str, Any], class_path: str, label: str) -> dict[str, Any]:
    payload = assert_clean_response(response, label)
    if payload.get("Class") != class_path:
        raise SmokeFailure(f"{label} Class mismatch: {payload.get('Class')} != {class_path}")
    if not isinstance(payload.get("Body" if label.endswith("template") else "BodySections"), (dict, list)):
        raise SmokeFailure(f"{label} missing Body/BodySections")
    return payload


def assert_apply_saved(response: dict[str, Any], label: str) -> None:
    assert_clean_response(response, label)
    if response.get("saved_asset") is not True:
        raise SmokeFailure(f"{label} did not report saved_asset=true")
    if response.get("wrote_sidecar") is not True:
        raise SmokeFailure(f"{label} did not report wrote_sidecar=true")


def assert_sidecar_sync(path: Path) -> dict[str, Any]:
    sidecar = read_json(path)
    regions = sidecar.get("_meta", {}).get("sync", {}).get("regions")
    if not isinstance(regions, dict) or not regions:
        raise SmokeFailure(f"Canonical sidecar sync regions missing: {path}")
    for region_id, state in regions.items():
        if not isinstance(state, dict):
            raise SmokeFailure(f"Invalid sync state for {region_id} in {path}")
        sidecar_hash = state.get("sidecarHash")
        evidence_hash = state.get("assetEvidenceHash")
        if not sidecar_hash or sidecar_hash != evidence_hash:
            raise SmokeFailure(f"Sync hash mismatch for {region_id} in {path}")
    return sidecar


def assert_materialized_files(manifest: dict[str, Any]) -> dict[str, Any]:
    files: dict[str, Any] = {}
    for name, entry in manifest["documents"].items():
        sidecar = Path(entry["sidecar"])
        asset_file = Path(entry["asset_file"])
        missing = [str(path) for path in (sidecar, asset_file) if not path.is_file()]
        if missing:
            raise SmokeFailure(f"{name} missing materialized files: {missing}")
        assert_sidecar_sync(sidecar)
        files[name] = {
            "sidecar": str(sidecar),
            "sidecar_sha256": sha256_file(sidecar),
            "asset": str(asset_file),
            "asset_sha256": sha256_file(asset_file),
        }
    return files


def assert_extract_coverage(extracts: dict[str, dict[str, Any]]) -> dict[str, list[str]]:
    missing: list[str] = []
    extra: list[str] = []
    failed: list[str] = []
    try:
        parent = response_payload(extracts["blackboard_parent"])
        bb = response_payload(extracts["blackboard"])
        if parent.get("Target") != extracts["blackboard_parent"].get("target", parent.get("Target")):
            failed.append("parent blackboard target mismatch")
        parent_names = {entry.get("Name") for entry in parent.get("Body", {}).get("Keys", [])}
        local_keys = bb.get("Body", {}).get("Keys", [])
        local_names = {entry.get("Name") for entry in local_keys}
        for required in ("TargetActor", "HasTarget"):
            if required not in parent_names:
                missing.append(f"inherited key {required}")
        for required in ("MoveLocation", "AlertName", "Speed", "TargetClass", "Mode"):
            if required not in local_names:
                missing.append(f"local key {required}")
        if bb.get("Body", {}).get("Parent") is None:
            missing.append("blackboard Parent")
        if "TargetActor" in local_names or "HasTarget" in local_names:
            extra.append("inherited keys leaked into local Keys")
        key_classes = {entry.get("KeyTypeClass", {}).get("Path") for entry in local_keys}
        for required in ("BlackboardKeyType_Class", "BlackboardKeyType_Enum"):
            if not any(isinstance(path, str) and path.endswith(required) for path in key_classes):
                missing.append(required)
        if not any("BaseClass" in entry.get("KeyTypeProperties", {}) for entry in local_keys):
            missing.append("BaseClass")
        if not any("EnumType" in entry.get("KeyTypeProperties", {}) for entry in local_keys):
            missing.append("EnumType")

        bt = response_payload(extracts["behavior_tree"])
        tree = bt.get("Body", {}).get("Tree", {})
        root = tree.get("Root", {})
        nodes = list(walk_tree_nodes(root)) if isinstance(root, dict) else []
        if root.get("Class") != "/Script/AIModule.BTComposite_Selector":
            missing.append("BT root selector")
        if not any(node is not root and node.get("Children") for node in nodes):
            missing.append("BT nested children")
        if not any(node.get("Class") == "/Script/AIModule.BTTask_RunBehavior" for node in nodes):
            missing.append("BT subtree task")
        if not any("BTTask_" in str(node.get("Class")) for node in nodes):
            missing.append("BT task")
        if not root.get("Services"):
            missing.append("BT service")
        decorators = [entry for node in nodes for entry in node.get("Decorators", [])]
        if not any(entry.get("Class") == "/Script/AIModule.BTDecorator_Blackboard" for entry in decorators):
            missing.append("BT decorator")
        composite = next((entry for entry in decorators if entry.get("Kind") == "Composite"), None)
        if not composite:
            missing.append("BT composite decorator")
        else:
            kinds = {entry.get("Kind") for entry in composite.get("BoundGraph", {}).get("Nodes", [])}
            if not {"Sink", "Not", "Or", "Test"}.issubset(kinds):
                missing.append("BT decorator logic")
        if not tree.get("Comments"):
            missing.append("BT comments")
        if any("Editor" not in node for node in nodes):
            missing.append("BT inline layout")
    except (KeyError, TypeError, AttributeError) as error:
        failed.append(str(error))
    return {"missing": missing, "extra": extra, "failed": failed}


def http_profile_template_schema(client: HttpClient, manifest: dict[str, Any], phase: str) -> None:
    schema = client.request("GET", "/assetfactory/assetdocument/schema", label=f"{phase}_schema")
    assert_schema(schema)
    seen_classes: set[str] = set()
    for name in PRIMARY_DOCUMENTS:
        entry = manifest["documents"][name]
        class_path = entry["class"]
        if class_path in seen_classes:
            continue
        seen_classes.add(class_path)
        query = urllib.parse.urlencode({"class_or_asset": class_path})
        profile = client.request("GET", f"/assetfactory/assetdocument/profile?{query}", label=f"{phase}_{name}_profile")
        assert_profile_or_template(profile, class_path, f"{name} profile")
        template = client.request(
            "POST",
            "/assetfactory/assetdocument/template",
            {"Class": class_path, "Target": entry["target"] + "_TemplateProbe"},
            label=f"{phase}_{name}_template",
        )
        assert_profile_or_template(template, class_path, f"{name} template")


def http_apply_extract_diff(client: HttpClient, manifest: dict[str, Any], phase: str, apply: bool) -> dict[str, dict[str, Any]]:
    extracts: dict[str, dict[str, Any]] = {}
    for name in APPLY_ORDER:
        entry = manifest["documents"][name]
        sidecar_payload = {"file_path": entry["sidecar"]}
        if apply:
            validate = client.request(
                "POST",
                "/assetfactory/assetdocument/validate",
                sidecar_payload,
                label=f"{phase}_{name}_validate",
            )
            assert_clean_response(validate, f"{name} validate")
            applied = client.request(
                "POST",
                "/assetfactory/assetdocument/apply-file",
                {"file_path": entry["sidecar"], "save_asset": True},
                label=f"{phase}_{name}_apply_file_save",
            )
            assert_apply_saved(applied, f"{name} apply-file/save")
        extract = client.request(
            "POST",
            "/assetfactory/assetdocument/extract",
            {"asset_path": entry["target"], "diff_only": False, "include_all_writable": True},
            label=f"{phase}_{name}_extract",
        )
        assert_clean_response(extract, f"{name} extract")
        if response_payload(extract).get("Target") != entry["target"]:
            raise SmokeFailure(f"{name} extract Target mismatch")
        extracts[name] = extract
        diff = client.request(
            "POST",
            "/assetfactory/assetdocument/diff",
            sidecar_payload,
            label=f"{phase}_{name}_diff",
        )
        assert_clean_response(diff, f"{name} diff")
    coverage = assert_extract_coverage(extracts)
    assert_zero_gate(coverage, f"{phase} extract coverage")
    return extracts


def run_mcp_smoke(
    manifest_path: Path,
    evidence_dir: Path,
    host: str,
    port: int,
    node_command: str,
    script_path: Path,
) -> dict[str, Any]:
    command = [
        node_command,
        str(script_path),
        "--manifest",
        str(manifest_path),
        "--evidence-dir",
        str(evidence_dir),
        "--base-url",
        f"http://{host}:{port}",
    ]
    result = subprocess.run(command, cwd=REPO_DIR / "MCP", text=True, capture_output=True, check=False)
    (evidence_dir / "driver.stdout.log").write_text(result.stdout, encoding="utf-8")
    (evidence_dir / "driver.stderr.log").write_text(result.stderr, encoding="utf-8")
    process = {"command": command, "exit_code": result.returncode}
    write_json(evidence_dir / "driver.process.json", process)
    if result.returncode != 0:
        raise SmokeFailure(f"MCP live smoke failed with {result.returncode}: {result.stderr[-2000:]}")
    summary = read_json(evidence_dir / "summary.json")
    if summary.get("status") != "passed":
        raise SmokeFailure(f"MCP summary did not pass: {json.dumps(summary, sort_keys=True)}")
    return summary


def validate_paths(engine: Path | None, project: Path | None, phase: str) -> tuple[Path, Path]:
    if engine is None or project is None:
        raise SmokeFailure(f"--engine and --project are required for {phase}")
    engine = engine.expanduser().resolve()
    project = project.expanduser().resolve()
    if not engine.is_dir():
        raise SmokeFailure(f"Engine root does not exist: {engine}")
    if not project.is_file() or project.suffix != ".uproject":
        raise SmokeFailure(f"Project .uproject does not exist: {project}")
    return engine, project


def default_evidence_dir(project: Path) -> Path:
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    return project.parent / "Saved" / "AssetFactory" / "Verification" / f"BTBB-{stamp}"


def pre_restart(args: argparse.Namespace) -> dict[str, Any]:
    engine, project = validate_paths(args.engine, args.project, "pre-restart")
    evidence_dir = (args.evidence_dir or default_evidence_dir(project)).expanduser().resolve()
    args.evidence_dir = evidence_dir
    writer = EvidenceWriter(evidence_dir / "http")
    client = HttpClient(args.host, args.port, args.request_timeout, writer)
    manifest = materialize_fixtures(project, args.fixture_dir, args.asset_root)
    fixture_report = validate_fixture_manifest(manifest)
    assert_zero_gate(fixture_report, "fixture")
    manifest_path = evidence_dir / "runtime_manifest.json"
    write_json(manifest_path, manifest)
    write_json(evidence_dir / "fixture_validation.json", fixture_report)

    service = capture_service_evidence(project, engine, args.host, args.port, client, args.health_timeout, writer, "pre_restart")
    http_profile_template_schema(client, manifest, "pre_restart")
    http_apply_extract_diff(client, manifest, "pre_restart", apply=True)
    files = assert_materialized_files(manifest)
    write_json(evidence_dir / "materialized_files.pre-restart.json", files)
    mcp_summary = run_mcp_smoke(
        manifest_path,
        evidence_dir / "mcp",
        args.host,
        args.port,
        args.node,
        args.mcp_script,
    )
    # Prove MCP's final apply did not silently skip sync and capture final hashes.
    files_after_mcp = assert_materialized_files(manifest)
    write_json(evidence_dir / "materialized_files.after-mcp.json", files_after_mcp)
    service_end = capture_service_evidence(
        project, engine, args.host, args.port, client, args.health_timeout, writer, "pre_restart_final"
    )
    if service_end["listener"]["pid"] != service["listener"]["pid"]:
        raise SmokeFailure(
            f"Listener PID changed during pre-restart smoke: {service['listener']['pid']} -> {service_end['listener']['pid']}"
        )

    token = {
        "schema_version": 1,
        "run_id": str(uuid.uuid4()),
        "created_at_utc": utc_now(),
        "status": "restart_required",
        "engine": str(engine),
        "project": str(project),
        "host": args.host,
        "port": args.port,
        "asset_root": args.asset_root.rstrip("/"),
        "evidence_dir": str(evidence_dir),
        "manifest_path": str(manifest_path),
        "listener": service_end["listener"],
        "service_json_mtime_ns": service_end["service_json_mtime_ns"],
        "service_start_time": service_end["service_start_time"],
        "mcp_summary": mcp_summary,
        "requires_new_listener_pid": True,
    }
    token_path = evidence_dir / "resume_token.json"
    write_json(token_path, token)
    summary = {
        "status": "restart_required",
        "phase": "pre-restart",
        "run_id": token["run_id"],
        "resume_token": str(token_path),
        "listener_pid_before_restart": service_end["listener"]["pid"],
        "http": "passed",
        "mcp": "passed",
        "fixture_validation": fixture_report,
        "missing": [],
        "extra": [],
        "skipped": [],
        "failed": [],
        "next_step": "Stop this Editor, restart the same project on the same port, then run post-restart with this resume token.",
    }
    write_json(evidence_dir / "summary.pre-restart.json", summary)
    write_json(evidence_dir / "summary.json", summary)
    return summary


def post_restart(args: argparse.Namespace) -> dict[str, Any]:
    engine, project = validate_paths(args.engine, args.project, "post-restart")
    if args.resume_token is None:
        raise SmokeFailure("--resume-token is required for post-restart")
    token_path = args.resume_token.expanduser().resolve()
    token = read_json(token_path)
    supplied_evidence_dir = args.evidence_dir.expanduser().resolve() if args.evidence_dir else None
    evidence_dir = Path(token["evidence_dir"])
    args.evidence_dir = evidence_dir
    expected = {
        "engine": str(engine),
        "project": str(project),
        "host": args.host,
        "port": args.port,
        "asset_root": args.asset_root.rstrip("/"),
    }
    mismatches = {key: (token.get(key), value) for key, value in expected.items() if token.get(key) != value}
    if mismatches:
        raise SmokeFailure(f"Resume token/options mismatch: {json.dumps(mismatches, sort_keys=True)}")
    if supplied_evidence_dir and supplied_evidence_dir != evidence_dir:
        raise SmokeFailure(f"--evidence-dir must match token: {evidence_dir}")
    manifest = read_json(Path(token["manifest_path"]))
    fixture_report = validate_fixture_manifest(manifest)
    assert_zero_gate(fixture_report, "post-restart fixture")
    writer = EvidenceWriter(evidence_dir / "http-post-restart")
    client = HttpClient(args.host, args.port, args.request_timeout, writer)
    service = capture_service_evidence(project, engine, args.host, args.port, client, args.health_timeout, writer, "post_restart")
    assert_fresh_restart(token, service)

    # No apply is allowed here: these are fresh reads from saved packages.
    http_profile_template_schema(client, manifest, "post_restart")
    http_apply_extract_diff(client, manifest, "post_restart", apply=False)
    files = assert_materialized_files(manifest)
    write_json(evidence_dir / "materialized_files.post-restart.json", files)
    summary = {
        "status": "passed",
        "phase": "post-restart",
        "run_id": token["run_id"],
        "completed_at_utc": utc_now(),
        "engine": str(engine),
        "project": str(project),
        "host": args.host,
        "port": args.port,
        "asset_root": args.asset_root.rstrip("/"),
        "listener_pid_before_restart": token["listener"]["pid"],
        "listener_pid_after_restart": service["listener"]["pid"],
        "service_start_time_before_restart": token["service_start_time"],
        "service_start_time_after_restart": service["service_start_time"],
        "http_pre_restart": "passed",
        "mcp_pre_restart": "passed",
        "fresh_http_post_restart": "passed",
        "missing": [],
        "extra": [],
        "skipped": [],
        "failed": [],
    }
    write_json(evidence_dir / "summary.json", summary)
    return summary


def validate_fixtures_only(args: argparse.Namespace) -> dict[str, Any]:
    if args.project:
        project = args.project.expanduser().resolve()
    else:
        project = Path.cwd() / "Saved" / "BTBBFixtureValidation" / "FixtureProject.uproject"
        project.parent.mkdir(parents=True, exist_ok=True)
        if not project.exists():
            project.write_text("{}\n", encoding="utf-8")
    manifest = materialize_fixtures(project, args.fixture_dir, args.asset_root)
    report = validate_fixture_manifest(manifest)
    assert_zero_gate(report, "fixture")
    return {"status": "passed", "phase": "validate-fixtures", **report}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Two-phase production BT/BB AssetDocument HTTP + stdio MCP live smoke (does not start/stop Editor)."
    )
    parser.add_argument("--phase", choices=("pre-restart", "post-restart", "validate-fixtures"), required=True)
    parser.add_argument("--engine", type=Path, help="Unreal Engine root (parameter is recorded in evidence).")
    parser.add_argument("--project", type=Path, help="Validation .uproject path.")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--evidence-dir", type=Path)
    parser.add_argument("--resume-token", type=Path)
    parser.add_argument("--asset-root", default=DEFAULT_ASSET_ROOT)
    parser.add_argument("--fixture-dir", type=Path, default=DEFAULT_FIXTURE_DIR)
    parser.add_argument("--mcp-script", type=Path, default=DEFAULT_MCP_SCRIPT)
    parser.add_argument("--node", default=os.environ.get("NODE", "node"))
    parser.add_argument("--request-timeout", type=float, default=120.0)
    parser.add_argument("--health-timeout", type=float, default=120.0)
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.phase == "pre-restart":
            summary = pre_restart(args)
        elif args.phase == "post-restart":
            summary = post_restart(args)
        else:
            summary = validate_fixtures_only(args)
        print(json.dumps(summary, indent=2, sort_keys=True))
        return 0
    except Exception as error:  # noqa: BLE001 - always turn harness failures into machine-readable nonzero evidence.
        failure = {"status": "failed", "phase": args.phase, "error": str(error), "failed": [str(error)]}
        if args.evidence_dir:
            write_json(args.evidence_dir.expanduser().resolve() / "summary.failed.json", failure)
        print(json.dumps(failure, indent=2, sort_keys=True), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
