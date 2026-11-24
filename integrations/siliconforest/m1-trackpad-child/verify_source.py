#!/usr/bin/env python3
"""Verify frozen composition and replay patch in disposable host storage only."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[2]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    manifest = json.loads((HERE / "source-manifest.json").read_text())
    files = manifest["files"]
    for destination, record in files.items():
        assert not Path(destination).is_absolute() and ".." not in Path(destination).parts
        assert digest((LAB / record["source"]).read_bytes()) == record["sha256"], destination
    encoded = "".join(k + "\0" + v["sha256"] + "\n" for k, v in sorted(files.items()))
    assert digest(encoded.encode()) == manifest["source_set_sha256"]
    patch = LAB / manifest["patch"]["path"]
    assert digest(patch.read_bytes()) == manifest["patch"]["sha256"]
    baseline = json.loads((LAB / "integrations/siliconforest/m1-trackpad-enum/sdk-build-inputs-002.json").read_text())["files"]
    with tempfile.TemporaryDirectory(prefix="t2-patch-replay-") as directory:
        target = Path(directory)
        for name, record in baseline.items():
            raw = (LAB / record["source"]).read_bytes()
            assert digest(raw) == record["sha256"], name
            p = target / name; p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(raw)
        subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch)],
                       cwd=target, check=True, capture_output=True, timeout=10)
        for name, record in files.items():
            if name in ("child_state.h", "trackpad_reference.h"):
                continue  # separately hash-verified root-owned staged inputs
            assert digest((target / name).read_bytes()) == record["sha256"], name
    print(json.dumps({"source_set_sha256": manifest["source_set_sha256"],
                      "files_verified": len(files), "patch_replayed": True,
                      "SDK_or_hardware": False}, sort_keys=True))


if __name__ == "__main__": main()
