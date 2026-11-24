"""Tests of helper behavior only. No upstream build or hardware is tested."""
from __future__ import annotations

import copy
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from contextlib import redirect_stdout, redirect_stderr

from tools import lab


def event(marker: str, run_id: str = "r1", origin: str = "synthetic") -> str:
    return json.dumps({"schema_version": 1, "run_id": run_id, "marker": marker, "origin": origin})


class RegistryTests(unittest.TestCase):
    def setUp(self):
        self.data = lab.read_json(lab.ROOT / "config/source-registry.json")

    def test_inherited_registry_valid_metadata_only(self):
        result = lab.validate_registry(self.data)
        self.assertEqual(result["repository_count"], 12)
        self.assertFalse(result["complete_build_lock"])
        self.assertEqual(result["remote_repositories_checked"], 0)

    def test_short_sha_rejected(self):
        self.data["repositories"][0]["commit"] = "abc123"
        with self.assertRaises(lab.ValidationError):
            lab.validate_registry(self.data)

    def test_nonhex_sha_rejected(self):
        self.data["repositories"][0]["commit"] = "z" * 40
        with self.assertRaises(lab.ValidationError):
            lab.validate_registry(self.data)

    def test_duplicate_repository_rejected(self):
        self.data["repositories"].append(copy.deepcopy(self.data["repositories"][0]))
        with self.assertRaises(lab.ValidationError):
            lab.validate_registry(self.data)

    def test_manual_verified_flag_rejected(self):
        self.data["repositories"][0]["remote_verified"] = True
        with self.assertRaises(lab.ValidationError):
            lab.validate_registry(self.data)

    def test_wrong_provenance_rejected(self):
        self.data["repositories"][0]["provenance"] = "hardware_test"
        with self.assertRaises(lab.ValidationError):
            lab.validate_registry(self.data)


class TaskTests(unittest.TestCase):
    def setUp(self):
        self.data = lab.read_json(lab.ROOT / "planning/tasks.json")

    def test_backlog_has_no_completed_os_tasks(self):
        self.assertEqual(lab.validate_tasks(self.data)["completed_os_tasks"], 0)

    def test_unknown_dependency_rejected(self):
        self.data["tasks"][0]["depends_on"] = ["missing"]
        with self.assertRaises(lab.ValidationError):
            lab.validate_tasks(self.data)

    def test_cycle_rejected(self):
        self.data["tasks"][0]["depends_on"] = ["P02"]
        with self.assertRaises(lab.ValidationError):
            lab.validate_tasks(self.data)

    def test_completed_claim_rejected(self):
        self.data["tasks"][0]["status"] = "complete"
        with self.assertRaises(lab.ValidationError):
            lab.validate_tasks(self.data)


class PlanTests(unittest.TestCase):
    def setUp(self):
        self.config = lab.read_json(lab.ROOT / "config/lab.example.json")

    def test_example_is_blocked(self):
        result = lab.hardware_plan(self.config)
        self.assertEqual(result["status"], "BLOCKED")
        self.assertFalse(result["execution_permitted"])
        self.assertEqual(result["actions_executed"], [])

    def test_populating_target_does_not_enable_hardware(self):
        self.config["roles"]["target"] = {
            "model": "example", "board_id": "example", "soc_id": "example",
            "firmware_profile": "example", "recovery_verified": True,
        }
        self.assertFalse(lab.hardware_plan(self.config)["execution_permitted"])

    def test_write_flag_rejected(self):
        self.config["safety"]["internal_storage_writes"] = True
        with self.assertRaises(lab.ValidationError):
            lab.hardware_plan(self.config)

    def test_live_mode_rejected(self):
        self.config["execution_mode"] = "live"
        with self.assertRaises(lab.ValidationError):
            lab.hardware_plan(self.config)

    def test_claimed_backend_rejected(self):
        self.config["live_backend_implemented"] = True
        with self.assertRaises(lab.ValidationError):
            lab.hardware_plan(self.config)


class LogTests(unittest.TestCase):
    def parse(self, text: str, kind: str = "fixture"):
        return lab.inspect_events(text, "r1", kind)

    def test_empty_log_no_progress(self):
        result = self.parse("")
        self.assertIsNone(result["highest_ordered_marker"])
        self.assertFalse(result["kernel_boot_proven"])

    def test_all_markers_still_not_hardware_pass(self):
        result = self.parse("\n".join(event(m) for m in lab.MARKERS))
        self.assertEqual(result["highest_ordered_marker"], "INIT_READY")
        self.assertFalse(result["hardware_pass"])
        self.assertFalse(result["kernel_boot_proven"])
        self.assertEqual(result["evidence_type"], "SYNTHETIC_FIXTURE")

    def test_out_of_order_marker_not_credited(self):
        result = self.parse(event("VM_READY") + "\n" + event("KERNEL_ENTRY"))
        self.assertEqual(result["ordered_markers"], ["KERNEL_ENTRY"])
        self.assertTrue(result["warnings"])

    def test_wrong_run_ignored(self):
        result = self.parse(event("KERNEL_ENTRY", run_id="other"))
        self.assertEqual(result["ignored_other_run_events"], 1)
        self.assertEqual(result["ordered_markers"], [])

    def test_duplicate_marker_idempotent(self):
        result = self.parse(event("KERNEL_ENTRY") + "\n" + event("KERNEL_ENTRY"))
        self.assertEqual(result["ordered_markers"], ["KERNEL_ENTRY"])

    def test_panic_wins_over_prior_markers(self):
        result = self.parse(event("KERNEL_ENTRY") + "\n" + event("PANIC"))
        self.assertEqual(result["observation"], "FAILURE_OBSERVED")
        self.assertFalse(result["hardware_pass"])

    def test_raw_panic_is_unattributed_failure(self):
        result = self.parse("panic(cpu 0 caller ...): synthetic test failure")
        self.assertEqual(result["observation"], "FAILURE_OBSERVED")

    def test_malformed_json_warns(self):
        result = self.parse('{"schema_version":')
        self.assertTrue(result["warnings"])
        self.assertEqual(result["ordered_markers"], [])

    def test_wrong_origin_not_promoted_to_capture(self):
        result = self.parse(event("KERNEL_ENTRY"), kind="capture")
        self.assertEqual(result["ordered_markers"], [])
        self.assertTrue(result["warnings"])

    def test_capture_never_authenticated(self):
        result = self.parse(event("KERNEL_ENTRY", origin="capture"), kind="capture")
        self.assertEqual(result["evidence_type"], "UNVERIFIED_LOG_CAPTURE")
        self.assertFalse(result["kernel_boot_proven"])

    def test_unstructured_success_string_not_progress(self):
        result = self.parse("SUCCESS: KERNEL_ENTRY EARLY_CONSOLE VM_READY INIT_READY")
        self.assertEqual(result["ordered_markers"], [])

    def test_unknown_marker_warns(self):
        self.assertTrue(self.parse(event("MADE_UP"))["warnings"])

    def test_duplicate_json_key_warns(self):
        result = self.parse('{"schema_version":1,"run_id":"r1","run_id":"r2"}')
        self.assertTrue(result["warnings"])


class FileAndCliTests(unittest.TestCase):
    def test_hash_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sample"
            path.write_bytes(b"abc")
            result = lab.hash_artifact(path)
            self.assertEqual(result["sha256"], hashlib.sha256(b"abc").hexdigest())
            self.assertEqual(result["size_bytes"], 3)

    def test_directory_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(lab.ValidationError):
                lab.hash_artifact(Path(tmp))

    def test_missing_file_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(lab.ValidationError):
                lab.read_json(Path(tmp) / "absent.json")

    def test_duplicate_config_keys_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.json"
            path.write_text('{"x":1,"x":2}')
            with self.assertRaises(lab.ValidationError):
                lab.read_json(path)

    def test_validate_cli(self):
        stdout = io.StringIO()
        with redirect_stdout(stdout):
            code = lab.main(["validate"])
        self.assertEqual(code, 0)
        self.assertEqual(json.loads(stdout.getvalue())["sources"]["remote_repositories_checked"], 0)

    def test_missing_config_exit_code(self):
        with tempfile.TemporaryDirectory() as tmp:
            with redirect_stderr(io.StringIO()):
                code = lab.main(["plan", "--config", str(Path(tmp) / "absent.json")])
        self.assertEqual(code, 2)


if __name__ == "__main__":
    unittest.main()
