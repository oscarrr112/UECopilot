import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import asset_document_delta_sidecar_smoke as smoke


class _FakeResponse:
    def __init__(self, body):
        self._body = body

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, traceback):
        return False

    def read(self):
        return self._body


class SmokeClientTests(unittest.TestCase):
    def test_default_sidecar_file_matches_apply_file_contract(self):
        self.assertIn("/Content/AssetDocumentSmoke/", smoke.DEFAULT_SIDECAR_FILE.replace("\\", "/"))
        self.assertTrue(smoke.DEFAULT_SIDECAR_FILE.endswith("AM_DeltaSidecarSmoke.assetdoc.json"))

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
        extracted_document = smoke.montage_document(
            montage_target="/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke",
            anim_object_path="/Game/Generated/Animation/AS_AssetDocSmoke.AS_AssetDocSmoke",
        )

        def fake_request(method, path, payload=None):
            calls.append((method, path, payload))
            if path == "/health":
                return {"success": True, "status": "ok"}
            if path == "/generate":
                asset = payload["Assets"][0]
                self.assertEqual(asset["AssetType"], "AnimSequence")
                self.assertEqual(asset["Name"], "AS_AssetDocSmoke")
                self.assertEqual(asset["Path"], "/Game/Generated/Animation")
                self.assertEqual(asset["Action"], "CreateOrUpdate")
                return {"success": True, "SuccessCount": 1, "UpdatedCount": 0, "FailedCount": 0}
            if path == "/assetdocument/apply-file":
                self.assertTrue(payload["file_path"].endswith("AM_DeltaSidecarSmoke.assetdoc.json"))
                self.assertTrue(payload["save_asset"])
                return {"success": True, "payload": {"Target": "/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke"}}
            if path == "/assetdocument/extract":
                self.assertEqual(payload["asset_path"], "/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke")
                self.assertFalse(payload["diff_only"])
                self.assertTrue(payload["include_all_writable"])
                return {"success": True, "payload": extracted_document}
            if path == "/assetdocument/diff":
                self.assertIs(payload, extracted_document)
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

    def test_run_smoke_keeps_sidecar_file_by_default(self):
        def fake_request(method, path, payload=None):
            if path == "/health":
                return {"success": True, "status": "ok"}
            if path == "/generate":
                return {"success": True}
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
            sidecar_path = Path(temp_dir) / "AM_DeltaSidecarSmoke.assetdoc.json"
            options = smoke.SmokeOptions(
                base_url="http://127.0.0.1:8559/assetfactory",
                sidecar_file=str(sidecar_path),
                request_timeout=0.1,
                wait_timeout=0.1,
                poll_interval=0.01,
                save_asset=True,
                keep_sidecar=False,
                montage_target="/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke",
                anim_name="AS_AssetDocSmoke",
                anim_package_path="/Game/Generated/Animation",
            )
            with mock.patch.object(smoke.HttpClient, "request", side_effect=fake_request):
                smoke.run_smoke(options)

            self.assertTrue(sidecar_path.exists())

    def test_http_client_wraps_invalid_json_with_route_context(self):
        client = smoke.HttpClient("http://127.0.0.1:8559/assetfactory", timeout=0.1)
        with mock.patch("urllib.request.urlopen", return_value=_FakeResponse(b"not json")):
            with self.assertRaisesRegex(RuntimeError, "GET /health"):
                client.request("GET", "/health")

    def test_wait_for_health_retries_runtime_errors(self):
        class FakeClient:
            base_url = "http://127.0.0.1:8559/assetfactory"

            def __init__(self):
                self.calls = 0

            def request(self, method, path):
                self.calls += 1
                if self.calls == 1:
                    raise RuntimeError("GET /health returned invalid JSON")
                return {"success": True, "status": "ok"}

        client = FakeClient()
        result = smoke.wait_for_health(client, wait_timeout=1.0, poll_interval=0.01)

        self.assertEqual(result["status"], "ok")
        self.assertEqual(client.calls, 2)

    def test_run_smoke_rejects_failed_generate_or_apply_file(self):
        def fake_generate_failure(method, path, payload=None):
            if path == "/health":
                return {"success": True, "status": "ok"}
            if path == "/generate":
                return {"success": False, "error": "generate failed"}
            raise AssertionError("unexpected route {0}".format(path))

        with tempfile.TemporaryDirectory() as temp_dir:
            options = smoke.SmokeOptions(
                base_url="http://127.0.0.1:8559/assetfactory",
                sidecar_file=str(Path(temp_dir) / "AM_DeltaSidecarSmoke.assetdoc.json"),
                request_timeout=0.1,
                wait_timeout=0.1,
                poll_interval=0.01,
                save_asset=True,
                keep_sidecar=False,
                montage_target="/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke",
                anim_name="AS_AssetDocSmoke",
                anim_package_path="/Game/Generated/Animation",
            )
            with mock.patch.object(smoke.HttpClient, "request", side_effect=fake_generate_failure):
                with self.assertRaisesRegex(RuntimeError, "/generate"):
                    smoke.run_smoke(options)

        def fake_apply_failure(method, path, payload=None):
            if path == "/health":
                return {"success": True, "status": "ok"}
            if path == "/generate":
                return {"success": True}
            if path == "/assetdocument/apply-file":
                return {"success": False, "error": "apply failed"}
            raise AssertionError("unexpected route {0}".format(path))

        with tempfile.TemporaryDirectory() as temp_dir:
            options = smoke.SmokeOptions(
                base_url="http://127.0.0.1:8559/assetfactory",
                sidecar_file=str(Path(temp_dir) / "AM_DeltaSidecarSmoke.assetdoc.json"),
                request_timeout=0.1,
                wait_timeout=0.1,
                poll_interval=0.01,
                save_asset=True,
                keep_sidecar=False,
                montage_target="/Game/AssetDocumentSmoke/AM_DeltaSidecarSmoke",
                anim_name="AS_AssetDocSmoke",
                anim_package_path="/Game/Generated/Animation",
            )
            with mock.patch.object(smoke.HttpClient, "request", side_effect=fake_apply_failure):
                with self.assertRaisesRegex(RuntimeError, "/assetdocument/apply-file"):
                    smoke.run_smoke(options)

    def test_diff_assertion_rejects_changed_complete_regions(self):
        with self.assertRaisesRegex(RuntimeError, "/Body/Curves"):
            smoke.assert_no_changed_complete_regions({"changed": [{"path": "/Body/Curves"}]})


if __name__ == "__main__":
    unittest.main()
