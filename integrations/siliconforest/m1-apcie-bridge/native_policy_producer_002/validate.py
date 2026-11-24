"""Host-only validation; output directories are created exclusively."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
SCRIPTS = ("test_source_selection.py", "test_producer.py", "test_c_decoder.py",
           "test_mutations.py", "audit_saved_pair.py")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, default=HERE / "validation" / "final")
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    rows = []
    for script in SCRIPTS:
        command = [sys.executable, "-B", script]
        result = subprocess.run(command, cwd=HERE, capture_output=True, timeout=3600)
        log = args.output_dir / (Path(script).stem + ".log")
        log.write_bytes(result.stdout + result.stderr)
        rows.append(dict(command=command, returncode=result.returncode, log=log.name,
                         sha256=hashlib.sha256(log.read_bytes()).hexdigest(), bytes=log.stat().st_size))
    record = dict(status="PASS" if all(r["returncode"] == 0 for r in rows) else "FAILED_PRESERVED",
                  commands=rows, hardware_or_native_write=False, old_capture_regraded=False)
    (args.output_dir / "result.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record, sort_keys=True))
    return int(any(r["returncode"] for r in rows))


if __name__ == "__main__":
    raise SystemExit(main())
