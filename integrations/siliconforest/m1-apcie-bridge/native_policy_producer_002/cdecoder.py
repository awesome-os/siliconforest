"""Host build of the unchanged C 908e facts decoder, for source-only tests.

Compiles the hash-pinned policy_snapshot.c and mcc_snapshot.c into a private
temporary shared object and calls nps_decode through ctypes. The 1024-byte
stack-frame guard used for kernel builds is applied unchanged.
"""
import ctypes as c
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

BRIDGE = Path(__file__).resolve().parents[1]
SOURCES = {
    "native_policy_snapshot/policy_snapshot.c": "5c3e5d94f3747b73a150462f6ee7c3dd8c3d385f9255718d33a6962f240767c4",
    "native_policy_snapshot/policy_snapshot.h": "1d2af52f4845ab647b83feff8a3728c003e57281af94572467030dcf4025861e",
    "mcc_snapshot/mcc_snapshot.c": "3ebeb0b56d308d84919dc489e7b5f47c4b201a7db62be763b9dd5a4ed27742b1",
    "mcc_snapshot/mcc_snapshot.h": "7c3284afb294cc8b2a2820b820a0a947d7d15821d889bdddccd063e4708feeb8",
}
FLAGS = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-Wframe-larger-than=1024",
         "-O2", "-fPIC", "-shared"]


class Range(c.Structure):
    _fields_ = [("base", c.c_uint64), ("bytes", c.c_uint64)]


class Expected(c.Structure):
    _fields_ = [("nonce", c.c_uint8 * 24), ("sha256", (c.c_uint8 * 32) * 6)]


class Source(c.Structure):
    _fields_ = [("tag", c.c_uint32), ("status", c.c_uint32), ("count", c.c_uint32), ("sha256", c.c_uint8 * 32)]


class Record(c.Structure):
    _fields_ = [("tag", c.c_uint32), ("ordinal", c.c_uint32), ("flags", c.c_uint32),
                ("auxiliary", c.c_uint32), ("range", Range)]


class Slot(c.Structure):
    _fields_ = [("start", c.c_uint32), ("end", c.c_uint32), ("enable", c.c_uint32),
                ("enabled", c.c_uint32), ("range", Range)]


class MCC(c.Structure):
    _fields_ = [(k, c.c_uint32) for k in ("profile", "coverage", "passes", "planes", "slots",
                                         "enabled_count", "union_count")] + \
               [("dram", Range), ("slot", Slot * 4), ("excluded", Range * 4)]


class Result(c.Structure):
    _fields_ = [(k, c.c_uint32) for k in ("version", "board_profile", "page_shift", "pa_bits", "wire_profile",
                                         "coverage", "ram_count", "record_count", "union_count")] + \
               [("identity", Expected), ("dram", Range), ("envelope", Range), ("retained_boot", Range),
                ("ram", Range * 8), ("source", Source * 6), ("raw_mcc", c.c_uint8 * 384), ("mcc", MCC),
                ("record", Record * 256), ("relevant_union", Range * 8)]


class Workspace(c.Structure):
    _fields_ = [("staged", Result), ("sorted", Range * 256)]


class Decoder:
    def __init__(self):
        for relative, digest in SOURCES.items():
            assert hashlib.sha256((BRIDGE / relative).read_bytes()).hexdigest() == digest, relative
        cc = shutil.which("clang") or shutil.which("cc")
        assert cc, "host C compiler required"
        self._temp = tempfile.TemporaryDirectory(prefix="producer002-c908e-")
        target = Path(self._temp.name) / "decoder.so"
        subprocess.run([cc, *FLAGS, "-I" + str(BRIDGE / "native_policy_snapshot"),
                        str(BRIDGE / "native_policy_snapshot/policy_snapshot.c"),
                        str(BRIDGE / "mcc_snapshot/mcc_snapshot.c"), "-o", str(target)],
                       check=True, capture_output=True, timeout=120)
        self.lib = c.CDLL(str(target))
        self.lib.nps_decode.argtypes = [c.c_void_p, c.c_size_t, c.POINTER(Expected),
                                        c.POINTER(Workspace), c.POINTER(Result)]
        self.lib.nps_decode.restype = c.c_int

    def close(self):
        self._temp.cleanup()

    def decode(self, wire, expected):
        """Return (status, Result). Result is meaningful only for status 0."""
        ex = Expected((c.c_uint8 * 24).from_buffer_copy(expected.nonce),
                      ((c.c_uint8 * 32) * 6)(*[(c.c_uint8 * 32).from_buffer_copy(x) for x in expected.identities]))
        buffer = c.create_string_buffer(wire, len(wire))
        work, out = Workspace(), Result()
        status = self.lib.nps_decode(buffer, len(wire), c.byref(ex), c.byref(work), c.byref(out))
        return status, out


def records(out):
    return [(r.tag, r.ordinal, r.flags, r.auxiliary, r.range.base, r.range.bytes)
            for r in out.record[:out.record_count]]


def identities(out):
    return tuple(bytes(out.identity.sha256[i]) for i in range(6))


def labels(out):
    return tuple(bytes(s.sha256) for s in out.source)


def union(out):
    return [(r.base, r.bytes) for r in out.relevant_union[:out.union_count]]
