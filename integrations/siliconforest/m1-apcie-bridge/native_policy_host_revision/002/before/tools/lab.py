#!/usr/bin/env python3
"""Offline metadata, artifact, and log utilities for a proposed Darwin lab.

This module does not execute subprocesses, access a network or device, or boot
anything. Log observations never count as proof of hardware or kernel success.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
SHA1 = re.compile(r"^[0-9a-f]{40}$")
REPOSITORY = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
MARKERS = ("KERNEL_ENTRY", "EARLY_CONSOLE", "VM_READY", "INIT_READY")
RAW_FAILURE = re.compile(r"^\s*(?:panic\s*(?:\(|:)|synchronous exception\b)", re.I)
MAX_LOG_BYTES = 16 * 1024 * 1024
MAX_JSON_BYTES = 4 * 1024 * 1024


class ValidationError(ValueError):
    """Input is missing, malformed, or inconsistent with the offline contract."""


def regular_file(path: Path) -> Path:
    """Reject device nodes, FIFOs, directories and symlinks before reading."""
    try:
        mode = path.lstat().st_mode
    except OSError as exc:
        raise ValidationError(f"Cannot inspect {path}: {exc}") from exc
    if not stat.S_ISREG(mode):
        raise ValidationError(f"Expected a regular non-symlink file: {path}")
    return path


def unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValidationError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def read_json(path: Path) -> dict[str, Any]:
    path = regular_file(path)
    if path.stat().st_size > MAX_JSON_BYTES:
        raise ValidationError(f"JSON file exceeds {MAX_JSON_BYTES} bytes: {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise ValidationError(f"Invalid JSON in {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValidationError(f"Expected a top-level JSON object: {path}")
    return value


def validate_registry(data: dict[str, Any]) -> dict[str, Any]:
    if data.get("schema_version") != 1:
        raise ValidationError("Unsupported registry schema_version")
    entries = data.get("repositories")
    if not isinstance(entries, list) or not entries:
        raise ValidationError("Registry must contain a nonempty repositories list")
    seen: set[str] = set()
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValidationError("Repository entry must be an object")
        repo = entry.get("repository")
        if not isinstance(repo, str) or not REPOSITORY.fullmatch(repo):
            raise ValidationError(f"Invalid repository identifier: {repo!r}")
        if repo in seen:
            raise ValidationError(f"Duplicate repository: {repo}")
        seen.add(repo)
        commit = entry.get("commit")
        if not isinstance(commit, str) or not SHA1.fullmatch(commit):
            raise ValidationError(f"{repo}: expected a full lowercase 40-character commit")
        if not isinstance(entry.get("branch"), str) or not entry["branch"].strip():
            raise ValidationError(f"{repo}: missing branch")
        if entry.get("provenance") != "inherited_from_prior_audit":
            raise ValidationError(f"{repo}: unexpected provenance for this phase-zero registry")
        # This schema cannot certify external evidence. A later promotion needs
        # a new schema with actual verification records, not a flipped boolean.
        if entry.get("remote_verified") is not False or entry.get("build_tested") is not False:
            raise ValidationError(f"{repo}: phase-zero entries must remain explicitly unverified/untested")
    return {
        "status": "VALID_METADATA",
        "repository_count": len(entries),
        "remote_repositories_checked": 0,
        "upstream_builds_tested": 0,
        "complete_build_lock": False,
    }


def validate_tasks(data: dict[str, Any]) -> dict[str, Any]:
    if data.get("schema_version") != 1:
        raise ValidationError("Unsupported task schema_version")
    tasks = data.get("tasks")
    if not isinstance(tasks, list) or not tasks:
        raise ValidationError("Expected a nonempty task list")
    by_id: dict[str, dict[str, Any]] = {}
    for task in tasks:
        if not isinstance(task, dict):
            raise ValidationError("Task must be an object")
        ident = task.get("id")
        if not isinstance(ident, str) or not ident.strip() or ident in by_id:
            raise ValidationError(f"Missing or duplicate task id: {ident!r}")
        if task.get("status") != "planned" or task.get("evidence") != []:
            raise ValidationError(f"{ident}: this preparation backlog has no completed OS tasks")
        if not isinstance(task.get("acceptance"), str) or not task["acceptance"].strip():
            raise ValidationError(f"{ident}: missing acceptance criterion")
        if not isinstance(task.get("depends_on"), list):
            raise ValidationError(f"{ident}: dependencies must be a list")
        by_id[ident] = task
    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(ident: str) -> None:
        if ident in visiting:
            raise ValidationError(f"Dependency cycle at {ident}")
        if ident in visited:
            return
        visiting.add(ident)
        for dep in by_id[ident]["depends_on"]:
            if not isinstance(dep, str) or dep not in by_id:
                raise ValidationError(f"{ident}: unknown dependency {dep!r}")
            visit(dep)
        visiting.remove(ident)
        visited.add(ident)

    for ident in by_id:
        visit(ident)
    return {"status": "VALID_METADATA", "task_count": len(tasks), "completed_os_tasks": 0}


def hardware_plan(config: dict[str, Any]) -> dict[str, Any]:
    if config.get("schema_version") != 1:
        raise ValidationError("Unsupported configuration schema_version")
    if config.get("execution_mode") != "offline_only":
        raise ValidationError("Only offline_only is implemented")
    if config.get("live_backend_implemented") is not False:
        raise ValidationError("No live backend exists in this bundle")
    safety = config.get("safety")
    required = (
        "internal_storage_writes", "boot_policy_changes", "partition_changes",
        "automatic_reboot", "automatic_dfu_restore", "automatic_source_updates",
    )
    if not isinstance(safety, dict) or any(safety.get(key) is not False for key in required):
        raise ValidationError("All phase-zero hardware/update actions must remain disabled")
    roles = config.get("roles")
    if not isinstance(roles, dict) or not isinstance(roles.get("target"), dict):
        raise ValidationError("Missing target template")
    target = roles["target"]
    missing = [key for key in ("model", "board_id", "soc_id", "firmware_profile")
               if not isinstance(target.get(key), str) or not target[key].strip()]
    reasons = ["No hardware execution backend is implemented"]
    if missing:
        reasons.append("Target fields are unrecorded: " + ", ".join(missing))
    if target.get("recovery_verified") is not True:
        reasons.append("Target recovery has not been verified")
    return {
        "status": "BLOCKED", "execution_permitted": False,
        "reasons": reasons,
        "proposed_sequence": ["precheck", "human approval", "verify artifacts", "control baseline", "load", "observe", "collect", "stop"],
        "actions_executed": [],
    }


def hash_artifact(path: Path) -> dict[str, Any]:
    path = regular_file(path)
    digest = hashlib.sha256()
    length = 0
    with path.open("rb") as handle:
        descriptor = handle.fileno()
        initial = os.fstat(descriptor)
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            length += len(block)
            digest.update(block)
        final = os.fstat(descriptor)
    if initial.st_size != final.st_size or initial.st_mtime_ns != final.st_mtime_ns or length != final.st_size:
        raise ValidationError("Artifact changed while hashing; retry with an immutable copy")
    return {"name": path.name, "size_bytes": length, "sha256": digest.hexdigest(),
            "meaning": "Byte identity only; not authenticity, build reproducibility, or bootability"}


def inspect_events(text: str, run_id: str, kind: str) -> dict[str, Any]:
    if kind not in {"fixture", "capture"}:
        raise ValidationError("Log kind must be fixture or capture")
    if not run_id.strip():
        raise ValidationError("run_id must be nonblank")
    expected_origin = "synthetic" if kind == "fixture" else "capture"
    observed: list[str] = []
    warnings: list[str] = []
    failures: list[str] = []
    ignored_runs = 0
    for number, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            continue
        if RAW_FAILURE.match(line):
            failures.append(f"Line {number}: raw failure pattern; attribution unverified")
            continue
        if not line.lstrip().startswith("{"):
            continue  # Unstructured text is not a milestone.
        try:
            event = json.loads(line, object_pairs_hook=unique_object)
        except (json.JSONDecodeError, ValidationError):
            warnings.append(f"Line {number}: malformed event")
            continue
        if not isinstance(event, dict) or event.get("schema_version") != 1:
            warnings.append(f"Line {number}: unsupported event schema")
            continue
        if event.get("run_id") != run_id:
            ignored_runs += 1
            continue
        if event.get("origin") != expected_origin:
            warnings.append(f"Line {number}: origin does not match declared log kind")
            continue
        marker = event.get("marker")
        if marker in ("PANIC", "EXCEPTION"):
            failures.append(f"Line {number}: {marker}")
            continue
        if marker not in MARKERS:
            warnings.append(f"Line {number}: unknown marker")
            continue
        if marker in observed:
            continue  # Duplicate event; do not double-count.
        if len(observed) < len(MARKERS) and marker == MARKERS[len(observed)]:
            observed.append(marker)
        else:
            warnings.append(f"Line {number}: out-of-order marker {marker}")
    if failures:
        observation = "FAILURE_OBSERVED"
    elif observed:
        observation = "ORDERED_MARKERS_OBSERVED"
    else:
        observation = "NO_ORDERED_MARKERS"
    return {
        "observation": observation,
        "evidence_type": "SYNTHETIC_FIXTURE" if kind == "fixture" else "UNVERIFIED_LOG_CAPTURE",
        "run_id": run_id, "ordered_markers": observed,
        "highest_ordered_marker": observed[-1] if observed else None,
        "failures": failures, "warnings": warnings, "ignored_other_run_events": ignored_runs,
        "hardware_pass": False, "kernel_boot_proven": False,
        "note": "Observations only. This tool does not authenticate logs, verify target/artifact identity, or execute acceptance tests.",
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    validate = sub.add_parser("validate", help="Validate local registry and backlog, without fetching sources")
    validate.add_argument("--registry", type=Path, default=ROOT / "config/source-registry.json")
    validate.add_argument("--tasks", type=Path, default=ROOT / "planning/tasks.json")
    plan = sub.add_parser("plan", help="Describe a hardware plan that remains blocked")
    plan.add_argument("--config", type=Path, default=ROOT / "config/lab.example.json")
    hash_command = sub.add_parser("hash", help="Hash an existing regular file; does not build it")
    hash_command.add_argument("path", type=Path)
    log = sub.add_parser("inspect-log", help="Summarize proposed events; never return hardware PASS")
    log.add_argument("path", type=Path)
    log.add_argument("--run-id", required=True)
    log.add_argument("--kind", choices=("fixture", "capture"), required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "validate":
            output = {"sources": validate_registry(read_json(args.registry)),
                      "backlog": validate_tasks(read_json(args.tasks))}
        elif args.command == "plan":
            output = hardware_plan(read_json(args.config))
        elif args.command == "hash":
            output = hash_artifact(args.path)
        else:
            path = regular_file(args.path)
            if path.stat().st_size > MAX_LOG_BYTES:
                raise ValidationError(f"Log exceeds {MAX_LOG_BYTES} bytes; provide a bounded extract")
            output = inspect_events(path.read_text(encoding="utf-8", errors="replace"), args.run_id, args.kind)
        print(json.dumps(output, indent=2))
        # Exit zero means the offline command ran, not that a boot succeeded.
        return 0
    except (ValidationError, OSError) as exc:
        print(json.dumps({"status": "INPUT_ERROR", "error": str(exc)}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
