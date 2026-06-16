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
    return client.request(
        "POST",
        "/generate",
        {
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
        },
    )


def payload(result):
    value = result.get("payload")
    if not isinstance(value, dict):
        raise RuntimeError("Missing payload in result: {0}".format(result))
    return value


def _assert_equal(label, actual, expected):
    if actual != expected:
        raise RuntimeError("{0}: expected {1!r}, got {2!r}".format(label, expected, actual))


def _assert_close(label, actual, expected, tolerance=0.0001):
    if abs(float(actual) - float(expected)) > tolerance:
        raise RuntimeError("{0}: expected {1!r}, got {2!r}".format(label, expected, actual))


def _assert_extracted_regions(extracted, expected_document):
    body = extracted.get("Body")
    expected_body = expected_document.get("Body", {})
    if not isinstance(body, dict):
        raise RuntimeError("Extracted document has no Body object")

    sync = body.get("Sync", {})
    expected_sync = expected_body.get("Sync", {})
    _assert_equal("Body.Sync.SyncGroup", sync.get("SyncGroup"), expected_sync.get("SyncGroup"))
    _assert_equal("Body.Sync.SyncSlotIndex", int(sync.get("SyncSlotIndex", -1)), int(expected_sync.get("SyncSlotIndex", -1)))

    root_motion = body.get("RootMotion", {})
    expected_root_motion = expected_body.get("RootMotion", {})
    for field in ("bEnableRootMotionTranslation", "bEnableRootMotionRotation", "RootMotionRootLock"):
        _assert_equal("Body.RootMotion.{0}".format(field), root_motion.get(field), expected_root_motion.get(field))

    time_stretch = body.get("TimeStretch", {})
    expected_time_stretch = expected_body.get("TimeStretch", {})
    _assert_equal(
        "Body.TimeStretch.TimeStretchCurveName",
        time_stretch.get("TimeStretchCurveName"),
        expected_time_stretch.get("TimeStretchCurveName"),
    )
    _assert_close("Body.TimeStretch.SamplingRate", time_stretch.get("SamplingRate"), expected_time_stretch.get("SamplingRate"))
    _assert_close(
        "Body.TimeStretch.CurveValueMinPrecision",
        time_stretch.get("CurveValueMinPrecision"),
        expected_time_stretch.get("CurveValueMinPrecision"),
    )
    if "Markers" in time_stretch or "Sum_dT_i_by_C_i" in time_stretch:
        raise RuntimeError("Body.TimeStretch should omit baked marker/cache data")

    curves = body.get("Curves")
    expected_curves = expected_body.get("Curves")
    if not isinstance(curves, list) or not isinstance(expected_curves, list):
        raise RuntimeError("Expected Body.Curves to be a list, got: {0!r}".format(curves))
    _assert_equal("Body.Curves length", len(curves), len(expected_curves))
    for curve_index, expected_curve in enumerate(expected_curves):
        curve = curves[curve_index]
        _assert_equal("Body.Curves[{0}].Name".format(curve_index), curve.get("Name"), expected_curve.get("Name"))
        _assert_equal("Body.Curves[{0}].Flags".format(curve_index), curve.get("Flags"), expected_curve.get("Flags"))
        keys = curve.get("Keys")
        expected_keys = expected_curve.get("Keys")
        if not isinstance(keys, list) or not isinstance(expected_keys, list):
            raise RuntimeError("Expected Body.Curves[{0}].Keys to be a list, got: {1!r}".format(curve_index, keys))
        _assert_equal("Body.Curves[{0}].Keys length".format(curve_index), len(keys), len(expected_keys))
        for key_index, expected_key in enumerate(expected_keys):
            _assert_close(
                "Body.Curves[{0}].Keys[{1}].Time".format(curve_index, key_index),
                keys[key_index].get("Time"),
                expected_key.get("Time"),
            )
            _assert_close(
                "Body.Curves[{0}].Keys[{1}].Value".format(curve_index, key_index),
                keys[key_index].get("Value"),
                expected_key.get("Value"),
            )


def assert_no_changed_complete_regions(diff_payload):
    changed = diff_payload.get("changed", [])
    changed_paths = {entry.get("path") for entry in changed if isinstance(entry, dict)}
    for path in ("/Body/Sync", "/Body/RootMotion", "/Body/TimeStretch", "/Body/Curves"):
        if path in changed_paths:
            raise RuntimeError("Unexpected changed diff entry after re-extract: {0}".format(path))


def run_smoke(options):
    client = HttpClient(options.base_url, options.request_timeout)
    wait_for_health(client, options.wait_timeout, options.poll_interval)
    generate_anim_sequence(client, options.anim_package_path, options.anim_name)

    generated_anim_path = anim_object_path(options.anim_package_path, options.anim_name)
    document = montage_document(options.montage_target, generated_anim_path)
    sidecar_path = write_sidecar(options.sidecar_file, document)

    try:
        client.request(
            "POST",
            "/assetdocument/apply-file",
            {"file_path": str(sidecar_path), "save_asset": options.save_asset},
        )
        extracted = payload(
            client.request(
                "POST",
                "/assetdocument/extract",
                {
                    "asset_path": options.montage_target,
                    "diff_only": False,
                    "include_all_writable": True,
                },
            )
        )
        _assert_extracted_regions(extracted, document)

        diff = payload(client.request("POST", "/assetdocument/diff", extracted))
        assert_no_changed_complete_regions(diff)

        return {
            "success": True,
            "montage_target": options.montage_target,
            "anim_object_path": generated_anim_path,
            "sidecar_file": str(sidecar_path),
            "base_url": options.base_url,
        }
    finally:
        if not options.keep_sidecar:
            sidecar_path.unlink(missing_ok=True)


def _parse_args(argv):
    parser = argparse.ArgumentParser(description="Run the external AssetDocument delta sidecar smoke.")
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL)
    parser.add_argument("--sidecar-file", default=DEFAULT_SIDECAR_FILE)
    parser.add_argument("--request-timeout", type=float, default=30.0)
    parser.add_argument("--wait-timeout", type=float, default=120.0)
    parser.add_argument("--poll-interval", type=float, default=1.0)
    parser.add_argument("--no-save-asset", action="store_true")
    parser.add_argument("--keep-sidecar", action="store_true")
    parser.add_argument("--montage-target", default=DEFAULT_MONTAGE_TARGET)
    parser.add_argument("--anim-name", default=DEFAULT_ANIM_NAME)
    parser.add_argument("--anim-package-path", default=DEFAULT_ANIM_PACKAGE_PATH)
    return parser.parse_args(argv)


def main(argv=None):
    args = _parse_args(argv)
    options = SmokeOptions(
        base_url=args.base_url,
        sidecar_file=args.sidecar_file,
        request_timeout=args.request_timeout,
        wait_timeout=args.wait_timeout,
        poll_interval=args.poll_interval,
        save_asset=not args.no_save_asset,
        keep_sidecar=args.keep_sidecar,
        montage_target=args.montage_target,
        anim_name=args.anim_name,
        anim_package_path=args.anim_package_path,
    )

    try:
        summary = run_smoke(options)
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        return 1

    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
