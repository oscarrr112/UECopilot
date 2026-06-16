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
