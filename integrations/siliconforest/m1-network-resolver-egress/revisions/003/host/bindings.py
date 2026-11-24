"""Read-only dependency on frozen original transport001, never a fallback."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
TRANSPORT = Path(__file__).resolve().parents[4] / "m1-network-transport"
RECEIPT_SHA256 = "08843ad4eda89d9b91015a446389f2a9a41cb7a114f456e551e32ee21cdefeef"


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verify_transport():
    receipt = TRANSPORT / "source-validation.json"
    if sha(receipt) != RECEIPT_SHA256:
        raise RuntimeError("frozen transport001 receipt mismatch")
    manifest = json.loads(receipt.read_text())
    for name, digest in manifest["files_sha256"].items():
        if sha(TRANSPORT / name) != digest:
            raise RuntimeError("frozen transport001 source mismatch: " + name)
    return manifest


verify_transport()
spec = importlib.util.spec_from_file_location("dlnet001_codec", TRANSPORT / "host/codec.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
Codec = module.Codec
