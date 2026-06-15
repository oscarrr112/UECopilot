import json
import os
import urllib.error
import urllib.request
import uuid

import unreal


HTTP_PORT = int(os.environ.get("ASSETFACTORY_HTTP_PORT", "8559"))
BASE_URL = "http://127.0.0.1:{0}/assetfactory".format(HTTP_PORT)

ASSET_NAME = os.environ.get("ASSETDOC_SMOKE_ANIM_NAME", "AS_AssetDocSmoke")
ANIM_PACKAGE_PATH = os.environ.get("ASSETDOC_SMOKE_ANIM_PATH", "/Game/Generated/Animation")
ANIM_OBJECT_PATH = "{0}/{1}.{1}".format(ANIM_PACKAGE_PATH, ASSET_NAME)
MONTAGE_TARGET = os.environ.get("ASSETDOC_SMOKE_MONTAGE_TARGET", "/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke")


def _post(path, payload):
    data = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        BASE_URL + path,
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            result = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError("{0} failed with HTTP {1}: {2}".format(path, exc.code, detail))

    if not result.get("success", False):
        raise RuntimeError("{0} reported failure: {1}".format(path, result))
    return result


def _generate_anim_sequence():
    subsystem = unreal.get_editor_subsystem(unreal.AssetFactorySubsystem)
    if subsystem is None:
        raise RuntimeError("AssetFactorySubsystem is unavailable")

    payload = {
        "Assets": [
            {
                "AssetType": "AnimSequence",
                "Name": ASSET_NAME,
                "Path": ANIM_PACKAGE_PATH,
                "Action": "CreateOrUpdate",
                "Skeleton": "/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton",
                "PreviewMesh": "/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP",
                "FrameRate": {"Numerator": 30, "Denominator": 1},
                "NumberOfFrames": 12,
            }
        ]
    }
    report = subsystem.generate_from_string(json.dumps(payload))
    failed_count = getattr(report, "failed_count", 0)
    if failed_count:
        raise RuntimeError("AnimSequence fixture generation failed: {0}".format(report))


def _asset_ref(path):
    return {"Kind": "AssetRef", "Path": path}


def _montage_document():
    return {
        "SchemaVersion": 1,
        "Target": MONTAGE_TARGET,
        "Class": "/Script/Engine.AnimMontage",
        "Action": "CreateOrUpdate",
        "Definitions": {},
        "Properties": {},
        "Body": {
            "References": {
                "Skeleton": _asset_ref("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton"),
            },
            "Preview": {
                "PreviewMesh": _asset_ref("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP"),
                "PreviewBasePose": _asset_ref(ANIM_OBJECT_PATH),
            },
            "SlotAnimTracks": [
                {
                    "SlotName": "DefaultSlot",
                    "AnimTrack": {
                        "AnimSegments": [
                            {
                                "AnimReference": _asset_ref(ANIM_OBJECT_PATH),
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


def _payload(result):
    payload = result.get("payload")
    if not isinstance(payload, dict):
        raise RuntimeError("Missing payload in result: {0}".format(result))
    return payload


def _assert_equal(label, actual, expected):
    if actual != expected:
        raise RuntimeError("{0}: expected {1!r}, got {2!r}".format(label, expected, actual))


def _assert_close(label, actual, expected, tolerance=0.0001):
    if abs(float(actual) - float(expected)) > tolerance:
        raise RuntimeError("{0}: expected {1!r}, got {2!r}".format(label, expected, actual))


def _assert_extracted_regions(extracted):
    body = extracted.get("Body")
    if not isinstance(body, dict):
        raise RuntimeError("Extracted document has no Body object")

    sync = body.get("Sync", {})
    _assert_equal("Body.Sync.SyncGroup", sync.get("SyncGroup"), "AssetDocSmoke")
    _assert_equal("Body.Sync.SyncSlotIndex", int(sync.get("SyncSlotIndex", -1)), 0)

    root_motion = body.get("RootMotion", {})
    _assert_equal("Body.RootMotion.bEnableRootMotionTranslation", root_motion.get("bEnableRootMotionTranslation"), True)
    _assert_equal("Body.RootMotion.bEnableRootMotionRotation", root_motion.get("bEnableRootMotionRotation"), True)
    _assert_equal("Body.RootMotion.RootMotionRootLock", root_motion.get("RootMotionRootLock"), "Zero")

    time_stretch = body.get("TimeStretch", {})
    _assert_equal("Body.TimeStretch.TimeStretchCurveName", time_stretch.get("TimeStretchCurveName"), "MontageTimeStretchCurve")
    _assert_close("Body.TimeStretch.SamplingRate", time_stretch.get("SamplingRate"), 30.0)
    _assert_close("Body.TimeStretch.CurveValueMinPrecision", time_stretch.get("CurveValueMinPrecision"), 0.02)
    if "Markers" in time_stretch or "Sum_dT_i_by_C_i" in time_stretch:
        raise RuntimeError("Body.TimeStretch should omit baked marker/cache data")

    curves = body.get("Curves")
    if not isinstance(curves, list) or len(curves) != 1:
        raise RuntimeError("Expected one Body.Curves entry, got: {0!r}".format(curves))
    curve = curves[0]
    _assert_equal("Body.Curves[0].Name", curve.get("Name"), "MontageTimeStretchCurve")
    _assert_equal("Body.Curves[0].Flags", curve.get("Flags"), ["Default"])
    keys = curve.get("Keys")
    if not isinstance(keys, list) or len(keys) != 3:
        raise RuntimeError("Expected three Body.Curves[0].Keys entries, got: {0!r}".format(keys))
    expected_keys = [(0.0, 0.0), (0.5, 1.0), (1.0, 0.0)]
    for index, (expected_time, expected_value) in enumerate(expected_keys):
        _assert_close("Body.Curves[0].Keys[{0}].Time".format(index), keys[index].get("Time"), expected_time)
        _assert_close("Body.Curves[0].Keys[{0}].Value".format(index), keys[index].get("Value"), expected_value)


def main():
    _generate_anim_sequence()

    document = _montage_document()
    _post("/assetdocument/apply", document)

    extracted = _payload(
        _post(
            "/assetdocument/extract",
            {
                "asset_path": MONTAGE_TARGET,
                "diff_only": False,
                "include_all_writable": True,
            },
        )
    )
    _assert_extracted_regions(extracted)

    diff = _payload(_post("/assetdocument/diff", extracted))
    changed = diff.get("changed", [])
    changed_paths = {entry.get("path") for entry in changed if isinstance(entry, dict)}
    for path in ("/Body/Sync", "/Body/RootMotion", "/Body/TimeStretch", "/Body/Curves"):
        if path in changed_paths:
            raise RuntimeError("Unexpected changed diff entry after re-extract: {0}".format(path))

    unreal.log("AssetDocument delta sidecar smoke passed for {0}".format(MONTAGE_TARGET))


main()
