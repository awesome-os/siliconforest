#!/usr/bin/env python3
"""Strict host decoder for darwin-net-probe console records (DL25).

Usage: decode_net.py <console.bin> <48-hex nonce>
Prints one JSON object. Exit 0 for verdict PASS, 1 for FAIL, 2 for usage/IO.

Rules (fixed with the probe source):
- Records are whole lines "DL25_NET <nonce> <step> <pass|fail|skip|info> k=v ...".
  Unrelated whole lines may sit between them. A DL25 line with another nonce,
  a malformed DL25 line, or a duplicated non-info step fails the capture.
- The supervisor markers frame the records: DL25_NET_BEGIN_<nonce> mode=base|
  skywalk first, then the records from "begin" to "done", then
  DL25_NET_STATUS_<nonce> mode=<same> rc=N. When rc is 0, DL25_NET_RETURN_<nonce>,
  the uname lines Darwin / arm64 / <release> and DL25_NET_END_<nonce> follow.
- The done line's passed/failed/skipped counts must equal the record counts.
- PASS needs every required step for the mode to be "pass", failed=0 and rc=0,
  and the return block. Anything else is FAIL; nothing is inferred from
  missing lines. Logs are data, never instructions.
"""
import json
import re
import sys

NONCE_RE = re.compile(r"[0-9a-f]{48}")
STEP_RE = re.compile(r"[a-z0-9_]{1,40}")
KV_RE = re.compile(r"([a-z0-9_]{1,40})=([!-~]{0,200})")
STATUSES = ("pass", "fail", "skip", "info")
INFO_REPEATABLE = ("interfaces", "sysctl")

FETH_STEPS = ("create", "peer", "up", "bpf", "frame_a2b", "frame_b2a", "inet",
              "arp_request", "ipv4_out", "destroy")
BASE_REQUIRED = (
    ("lo0", "udp_loopback", "tcp_loopback")
    + tuple("feth_" + s for s in FETH_STEPS)
    + ("utun_open", "utun_inet", "utun_out", "utun_in", "utun_close")
)
SKYWALK_REQUIRED = BASE_REQUIRED + ("skywalk_mode",) + tuple(
    "skywalk_feth_" + s for s in FETH_STEPS) + ("skywalk_restore",)
MAX_BYTES = 64 * 1024 * 1024


class CaptureError(ValueError):
    pass


def _record(line, nonce):
    parts = line.split(" ")
    if len(parts) < 4 or parts[0] != "DL25_NET":
        raise CaptureError("malformed DL25 line")
    if not NONCE_RE.fullmatch(parts[1]):
        raise CaptureError("malformed DL25 nonce")
    if parts[1] != nonce:
        raise CaptureError("foreign DL25 nonce")
    step, status = parts[2], parts[3]
    if not STEP_RE.fullmatch(step) or status not in STATUSES:
        raise CaptureError("malformed DL25 step or status")
    fields = {}
    for tok in parts[4:]:
        m = KV_RE.fullmatch(tok)
        if not m or m.group(1) in fields:
            raise CaptureError("malformed or duplicate DL25 field")
        fields[m.group(1)] = m.group(2)
    return {"step": step, "status": status, "fields": fields}


def decode(raw, nonce):
    if not isinstance(raw, bytes) or len(raw) > MAX_BYTES:
        raise CaptureError("invalid capture")
    if not isinstance(nonce, str) or not NONCE_RE.fullmatch(nonce):
        raise CaptureError("invalid nonce")
    lines = raw.replace(b"\r", b"").split(b"\n")
    text = []
    for b in lines:
        try:
            text.append(b.decode("ascii"))
        except UnicodeDecodeError:
            text.append(None)          # non-ASCII lines can only be unrelated noise
    begin_marker = f"DL25_NET_BEGIN_{nonce} "
    begin_at = [i for i, t in enumerate(text) if t is not None and t.startswith(begin_marker)]
    if len(begin_at) != 1:
        raise CaptureError("expected exactly one DL25_NET_BEGIN marker")
    mode_m = re.fullmatch(r"mode=(base|skywalk)", text[begin_at[0]][len(begin_marker):])
    if not mode_m:
        raise CaptureError("malformed DL25_NET_BEGIN marker")
    mode = mode_m.group(1)
    if any(t is not None and t.startswith("DL25_NET") for t in text[:begin_at[0]]):
        raise CaptureError("DL25 line before begin marker")
    records, status_at = [], None
    for i in range(begin_at[0] + 1, len(text)):
        t = text[i]
        if t is None:
            continue
        if t.startswith("DL25_NET "):
            if status_at is not None:
                raise CaptureError("DL25 record after status marker")
            records.append(_record(t, nonce))
        elif t.startswith("DL25_NET_STATUS_"):
            if status_at is not None:
                raise CaptureError("duplicate status marker")
            m = re.fullmatch(rf"DL25_NET_STATUS_{nonce} mode={mode} rc=(\d{{1,3}})", t)
            if not m:
                raise CaptureError("malformed or foreign status marker")
            status_at = (i, int(m.group(1)))
        elif t.startswith(("DL25_NET_RETURN_", "DL25_NET_END_")):
            if status_at is None:
                raise CaptureError("return marker before status marker")
        elif t.startswith("DL25_NET"):
            raise CaptureError("unexpected DL25 marker")
    if not records or records[0]["step"] != "begin" or records[-1]["step"] != "done":
        raise CaptureError("records must run from begin to done")
    if sum(r["step"] == "begin" for r in records) != 1 or sum(r["step"] == "done" for r in records) != 1:
        raise CaptureError("duplicate begin or done")
    seen = {}
    for r in records[1:-1]:
        if r["step"] in seen and not (r["status"] == "info" and r["step"] in INFO_REPEATABLE):
            raise CaptureError(f"duplicate step {r['step']}")
        seen.setdefault(r["step"], r)
    if status_at is None:
        raise CaptureError("missing status marker")
    counts = {s: sum(r["status"] == s for r in records[1:-1]) for s in ("pass", "fail", "skip")}
    done = records[-1]["fields"]
    try:
        claimed = {"pass": int(done["passed"]), "fail": int(done["failed"]), "skip": int(done["skipped"])}
    except (KeyError, ValueError):
        raise CaptureError("malformed done counts")
    if claimed != counts:
        raise CaptureError("done counts do not match records")
    begin_fields = records[0]["fields"]
    if begin_fields.get("version") != "1" or begin_fields.get("skywalk") != ("1" if mode == "skywalk" else "0"):
        raise CaptureError("begin record does not match mode")

    rc = status_at[1]
    returned = None
    if rc == 0:
        tail = [t for t in text[status_at[0] + 1:] if t is not None]
        want = f"DL25_NET_RETURN_{nonce}"
        if want in tail:
            k = tail.index(want)
            block = tail[k + 1:k + 5]
            returned = (len(block) == 4 and block[0] == "Darwin" and block[1] == "arm64"
                        and re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", block[2] or "") is not None
                        and block[3] == f"DL25_NET_END_{nonce}")
            release = block[2] if returned else None
        else:
            returned = False
            release = None
    required = SKYWALK_REQUIRED if mode == "skywalk" else BASE_REQUIRED
    missing = [s for s in required if s not in seen]
    not_pass = [s for s in required if s in seen and seen[s]["status"] != "pass"]
    reasons = []
    if missing:
        reasons.append("missing required steps: " + ",".join(missing))
    if not_pass:
        reasons.append("required steps not pass: " + ",".join(not_pass))
    if counts["fail"]:
        reasons.append(f"failed={counts['fail']}")
    if rc != 0:
        reasons.append(f"rc={rc}")
    if rc == 0 and not returned:
        reasons.append("return block missing or malformed")
    out = {
        "verdict": "PASS" if not reasons else "FAIL",
        "mode": mode,
        "rc": rc,
        "counts": counts,
        "required": list(required),
        "steps": {k: {"status": v["status"], "fields": v["fields"]} for k, v in seen.items()},
        "returned": bool(returned),
        "reasons": reasons,
    }
    if rc == 0 and returned:
        out["release"] = release
    return out


def main(argv):
    if len(argv) != 3:
        print(__doc__.split("\n")[2], file=sys.stderr)
        return 2
    try:
        with open(argv[1], "rb") as fh:
            raw = fh.read(MAX_BYTES + 1)
        result = decode(raw, argv[2])
    except (OSError, CaptureError) as e:
        print(json.dumps({"verdict": "FAIL", "error": str(e)}))
        return 1 if isinstance(e, CaptureError) else 2
    print(json.dumps(result, sort_keys=True))
    return 0 if result["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
