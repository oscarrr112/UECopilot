#!/usr/bin/env python3
"""Offline contract tests for the production BT/BB live smoke harness."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path


THIS_DIR = Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

import btbb_asset_document_live_smoke as smoke


class FixtureContractTests(unittest.TestCase):
    def test_rendered_fixtures_cover_required_btbb_surface(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            project = Path(temp_dir) / "SmokeProject.uproject"
            project.write_text("{}\n", encoding="utf-8")

            manifest = smoke.materialize_fixtures(
                project=project,
                fixture_dir=smoke.DEFAULT_FIXTURE_DIR,
                asset_root="/Game/AssetDocumentSmoke/BTBB",
            )

            report = smoke.validate_fixture_manifest(manifest)

        self.assertEqual(report["missing"], [])
        self.assertEqual(report["extra"], [])
        self.assertEqual(report["failed"], [])
        self.assertEqual(set(manifest["documents"]), {"blackboard_parent", "blackboard", "behavior_tree_subtree", "behavior_tree"})

    def test_blackboard_fixture_has_parent_local_keys_and_reference_types(self) -> None:
        source = json.loads((smoke.DEFAULT_FIXTURE_DIR / "blackboard.assetdoc.json").read_text(encoding="utf-8"))
        body = source["Body"]
        keys = body["Keys"]

        self.assertEqual(body["Parent"]["Kind"], "AssetRef")
        self.assertIn("__ASSET_ROOT__", body["Parent"]["Path"])
        key_classes = {key["KeyTypeClass"]["Path"] for key in keys}
        self.assertIn("/Script/AIModule.BlackboardKeyType_Enum", key_classes)
        self.assertIn("/Script/AIModule.BlackboardKeyType_Class", key_classes)
        self.assertTrue(any("BaseClass" in key["KeyTypeProperties"] for key in keys))
        self.assertTrue(any("EnumType" in key["KeyTypeProperties"] for key in keys))

    def test_behavior_tree_fixture_has_nested_topology_logic_and_editor_authorship(self) -> None:
        source = json.loads((smoke.DEFAULT_FIXTURE_DIR / "behavior_tree.assetdoc.json").read_text(encoding="utf-8"))
        tree = source["Body"]["Tree"]
        root = tree["Root"]

        self.assertEqual(root["Class"], "/Script/AIModule.BTComposite_Selector")
        self.assertGreaterEqual(len(root["Children"]), 2)
        self.assertTrue(root["Services"])
        self.assertTrue(root["Decorators"])
        nested = next(child for child in root["Children"] if child["Children"])
        self.assertTrue(nested["Children"])
        all_decorators = [decorator for child in nested["Children"] for decorator in child["Decorators"]]
        composite = next(decorator for decorator in all_decorators if decorator.get("Kind") == "Composite")
        kinds = {node["Kind"] for node in composite["BoundGraph"]["Nodes"]}
        self.assertTrue({"Sink", "Or", "Not", "Test"}.issubset(kinds))
        self.assertTrue(composite["BoundGraph"]["Links"])
        self.assertTrue(tree["Comments"])
        self.assertTrue(all("Editor" in node for node in smoke.walk_tree_nodes(root)))
        self.assertTrue(any(node["Class"] == "/Script/AIModule.BTTask_RunBehavior" for node in smoke.walk_tree_nodes(root)))


class FailureGateTests(unittest.TestCase):
    def test_response_gate_rejects_nonempty_diff_or_sync_skip(self) -> None:
        bad = {
            "success": True,
            "wrote_sidecar": True,
            "payload": {
                "changed": [{"path": "/Body/Tree"}],
                "sidecar_sync_update_skipped": True,
            },
        }

        with self.assertRaisesRegex(smoke.SmokeFailure, "changed|sync"):
            smoke.assert_clean_response(bad, "diff")

    def test_response_gate_rejects_missing_extra_skipped_or_failed(self) -> None:
        for bucket in ("missing", "extra", "skipped", "failed", "added", "removed"):
            with self.subTest(bucket=bucket):
                with self.assertRaisesRegex(smoke.SmokeFailure, bucket):
                    smoke.assert_clean_response({"success": True, "payload": {bucket: [bucket]}}, "gate")

    def test_restart_gate_requires_new_listener_pid_and_new_service_file(self) -> None:
        token = {"listener": {"pid": 1200}, "service_json_mtime_ns": 100}

        with self.assertRaisesRegex(smoke.SmokeFailure, "PID"):
            smoke.assert_fresh_restart(token, {"listener": {"pid": 1200}, "service_json_mtime_ns": 101})
        with self.assertRaisesRegex(smoke.SmokeFailure, "service.json"):
            smoke.assert_fresh_restart(token, {"listener": {"pid": 1201}, "service_json_mtime_ns": 100})


if __name__ == "__main__":
    unittest.main()
