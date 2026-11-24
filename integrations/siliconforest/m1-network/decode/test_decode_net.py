#!/usr/bin/env python3
"""Host-only tests for decode_net.py. Synthetic consoles only; no runtime claim."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import decode_net as d  # noqa: E402

N = "0123456789abcdef0123456789abcdef0123456789abcdef"
OTHER = "f" * 48


def records(mode="base", fail=None, drop=None, extra=None):
    steps = ["lo0", "udp_loopback", "tcp_loopback"]
    steps += ["feth_" + s for s in d.FETH_STEPS]
    steps += ["utun_open", "utun_inet", "utun_out", "utun_in", "utun_close"]
    if mode == "skywalk":
        steps += ["skywalk_mode"] + ["skywalk_feth_" + s for s in d.FETH_STEPS] + ["skywalk_restore"]
    out = [f"DL25_NET {N} begin info version=1 skywalk={1 if mode == 'skywalk' else 0} wait_ms=2000",
           f"DL25_NET {N} identity info sysname=Darwin release=25.5.0 machine=arm64 euid=0 pid=31",
           f"DL25_NET {N} sysctl info name=net.link.fake.bsd_mode present=1 value=1",
           f"DL25_NET {N} sysctl info name=kern.skywalk.priv_check present=1 value=1",
           f"DL25_NET {N} interfaces info when=before index=0 name=lo0 family=link flags=0x8048 iftype=24 inet=-"]
    p = f = s = 0
    for st in steps:
        if st == drop:
            continue
        if st == fail:
            out.append(f"DL25_NET {N} {st} fail status=0 required=1")
            f += 1
        else:
            out.append(f"DL25_NET {N} {st} pass errno=0 required=1")
            p += 1
    if mode == "base":
        out.append(f"DL25_NET {N} skywalk_feth skip reason=not_requested")
        s += 1
    out += extra or []
    out.append(f"DL25_NET {N} done info passed={p} failed={f} skipped={s}")
    return out, f


def console(mode="base", fail=None, drop=None, extra=None, rc=None, ret=True, noise=True):
    recs, failed = records(mode, fail, drop, extra)
    rc = (1 if failed else 0) if rc is None else rc
    lines = ["boot noise", "", f"DL25_NET_BEGIN_{N} mode={mode}"]
    for i, r in enumerate(recs):
        lines.append(r)
        if noise and i == 3:
            lines.append("PDM1SPIHID: unrelated kernel line")
    lines.append(f"DL25_NET_STATUS_{N} mode={mode} rc={rc}")
    if rc == 0 and ret:
        lines += ["", "DARWIN NETWORK PROBE RETURNED", f"DL25_NET_RETURN_{N}", "Darwin", "arm64", "25.5.0",
                  f"DL25_NET_END_{N}", "# "]
    return ("\r\n".join(lines) + "\r\n").encode()


class DecodeNet(unittest.TestCase):
    def test_base_pass_with_noise_and_crlf(self):
        r = d.decode(console(), N)
        self.assertEqual(r["verdict"], "PASS", r["reasons"])
        self.assertEqual(r["mode"], "base")
        self.assertEqual(r["release"], "25.5.0")
        self.assertEqual(r["counts"], {"pass": len(d.BASE_REQUIRED), "fail": 0, "skip": 1})

    def test_skywalk_pass(self):
        r = d.decode(console("skywalk"), N)
        self.assertEqual(r["verdict"], "PASS", r["reasons"])
        self.assertIn("skywalk_feth_ipv4_out", r["steps"])

    def test_failed_step_is_fail_not_hidden(self):
        r = d.decode(console(fail="feth_arp_request"), N)
        self.assertEqual(r["verdict"], "FAIL")
        self.assertIn("feth_arp_request", r["reasons"][0])
        self.assertEqual(r["rc"], 1)

    def test_missing_required_step(self):
        r = d.decode(console(drop="utun_in"), N)
        self.assertEqual(r["verdict"], "FAIL")
        self.assertIn("missing required steps: utun_in", r["reasons"])

    def test_skywalk_mode_needs_skywalk_steps(self):
        raw = console("base").replace(b"mode=base", b"mode=skywalk").replace(b"skywalk=0", b"skywalk=1")
        r = d.decode(raw, N)
        self.assertEqual(r["verdict"], "FAIL")
        self.assertIn("skywalk_restore", r["reasons"][0])

    def test_rc_zero_without_return_block(self):
        r = d.decode(console(ret=False), N)
        self.assertEqual(r["verdict"], "FAIL")
        self.assertIn("return block missing or malformed", r["reasons"])

    def test_rc_nonzero_even_if_steps_pass(self):
        r = d.decode(console(rc=1), N)
        self.assertEqual(r["verdict"], "FAIL")
        self.assertIn("rc=1", r["reasons"])

    def test_rejections(self):
        good = console()
        cases = {
            "foreign nonce": good.replace(f"DL25_NET {N} identity".encode(), f"DL25_NET {OTHER} identity".encode()),
            "count mismatch": good.replace(b"skipped=1", b"skipped=0"),
            "duplicate step": good.replace(f"DL25_NET {N} lo0 pass errno=0 required=1".encode(),
                                           f"DL25_NET {N} lo0 pass errno=0 required=1\r\nDL25_NET {N} lo0 pass errno=0 required=1".encode()),
            "bad field": good.replace(b"errno=0 required=1\r\nDL25_NET " + N.encode() + b" udp_loopback",
                                      b"errno 0 required=1\r\nDL25_NET " + N.encode() + b" udp_loopback"),
            "bad status": good.replace(b" lo0 pass ", b" lo0 maybe "),
            "no begin marker": good.replace(f"DL25_NET_BEGIN_{N} mode=base".encode(), b"nothing"),
            "two begin markers": good.replace(b"boot noise", f"DL25_NET_BEGIN_{N} mode=base".encode()),
            "record after status": good.replace(f"DL25_NET_RETURN_{N}".encode(),
                                                f"DL25_NET {N} lo0 pass errno=0 required=1\r\nDL25_NET_RETURN_{N}".encode()),
            "status mode mismatch": good.replace(f"DL25_NET_STATUS_{N} mode=base".encode(),
                                                 f"DL25_NET_STATUS_{N} mode=skywalk".encode()),
            "begin record mode mismatch": good.replace(b"skywalk=0 wait_ms", b"skywalk=1 wait_ms"),
            "no done": good.replace(f"DL25_NET {N} done".encode(), b"truncated"),
            "unknown marker": good.replace(b"boot noise", f"DL25_NET_EXTRA_{N}".encode()),
        }
        for name, raw in cases.items():
            with self.subTest(name):
                with self.assertRaises(d.CaptureError):
                    d.decode(raw, N)

    def test_return_marker_before_status_rejected(self):
        raw = console().replace(f"DL25_NET_BEGIN_{N} mode=base".encode(),
                                f"DL25_NET_BEGIN_{N} mode=base\r\nDL25_NET_RETURN_{N}".encode())
        with self.assertRaises(d.CaptureError):
            d.decode(raw, N)

    def test_input_validation(self):
        with self.assertRaises(d.CaptureError):
            d.decode("text", N)
        with self.assertRaises(d.CaptureError):
            d.decode(console(), N.upper())
        with self.assertRaises(d.CaptureError):
            d.decode(b"", N)

    def test_non_ascii_noise_is_ignored(self):
        raw = console().replace(b"boot noise", b"\xff\xfe binary noise")
        self.assertEqual(d.decode(raw, N)["verdict"], "PASS")


if __name__ == "__main__":
    unittest.main(verbosity=1)
