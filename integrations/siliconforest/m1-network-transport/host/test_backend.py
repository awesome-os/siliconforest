#!/usr/bin/env python3
"""Actual local backend + shared C codec + QEMU tests; synthetic guest packets."""
import argparse
from collections import deque
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from codec import Codec
from backend import Queue, checked_socket
from test_slirp_backend import ip_packet, tcp_body, checksum, GUEST_IP, HOST_IP


class Trial:
    def __init__(self, library, directory, **limits):
        self.nonce = os.urandom(24).hex()
        self.records = Path(directory) / "records"
        self.stdout = open(Path(directory) / "process.log", "wb")
        self.proc = subprocess.Popen([sys.executable, "-I", str(Path(__file__).with_name("backend.py")),
            "--nonce", self.nonce, "--codec-library", str(library), "--records", str(self.records),
            "--listen-port", "0", "--tcp-echo-port", "0", "--udp-echo-port", "0",
            "--seconds", str(limits.get("seconds", 10)),
            "--accept-seconds", str(limits.get("accept_seconds", 8))],
            stdout=self.stdout, stderr=subprocess.STDOUT, env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"))
        self.sock, self.codec = None, None
        self.ready = None
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                break
            try:
                self.ready = json.loads((self.records / "ready.json").read_text())
                break
            except (FileNotFoundError, json.JSONDecodeError):
                time.sleep(0.01)
        if self.ready is None:
            self.cleanup()
            raise AssertionError("backend not ready: " + (Path(directory)/"process.log").read_text())
        self.codec = Codec(library, self.nonce, 1)
        self.pending = deque()

    def connect(self):
        self.sock = socket.create_connection(("127.0.0.1", self.ready["listen_port"]), timeout=2)
        self.sock.settimeout(3)

    def send(self, kind, data=b""):
        frame = self.codec.encode(kind, data)
        # Fragment every outbound frame across actual stream writes.
        for start in range(0, len(frame), 13):
            self.sock.sendall(frame[start:start+13])

    def receive(self):
        while not self.pending:
            data = self.sock.recv(4096)
            assert data
            self.codec.feed(data, lambda *args: self.pending.append(args))
        return self.pending.popleft()

    def packet(self):
        kind, _seq, packet = self.receive()
        assert kind == 2
        ihl = (packet[0] & 15) * 4
        assert packet[0] >> 4 == 4 and checksum(packet[:ihl]) == 0
        assert packet[12:16] == HOST_IP and packet[16:20] == GUEST_IP
        return packet[9], packet[ihl:]

    def result(self, expected_code):
        rc = self.proc.wait(timeout=5)
        assert rc == expected_code, (rc, (self.records.parent/"process.log").read_text())
        result = json.loads((self.records / "result.json").read_text())
        assert result["nonce"] == self.nonce
        assert result["cleanup"]["qemu_reaped"] and result["cleanup"]["qemu_returncode"] == 0
        assert not result["cleanup"].get("kill_sent") and not result["cleanup"]["errors"]
        for name, binding in result["closed_records"].items():
            data = (self.records/name).read_bytes()
            assert len(data) == binding["bytes"] and hashlib.sha256(data).hexdigest() == binding["sha256"]
        return result

    def cleanup(self):
        if self.sock is not None:
            self.sock.close()
        if self.codec is not None:
            self.codec.close()
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(4)
            except subprocess.TimeoutExpired:
                self.proc.kill(); self.proc.wait(2)
                raise AssertionError("forced backend KILL; fixture failed")
        self.stdout.close()


def roundtrip(trial):
    trial.connect(); trial.send(1)
    assert trial.receive()[:2] == (1, 1)
    udp = b"DL26-UDP " + trial.nonce.encode()
    port = trial.ready["udp_port"]
    body = struct.pack("!4H", 42001, port, len(udp) + 8, 0) + udp
    trial.send(2, ip_packet(17, body))
    proto, body = trial.packet()
    assert proto == 17 and struct.unpack_from("!HHH", body) == (port, 42001, len(udp) + 8)
    assert body[8:] == udp
    port, seq, sport = trial.ready["tcp_port"], 20000, 42002
    def send_tcp(sequence, ack, flags, data=b""):
        trial.send(2, ip_packet(6, tcp_body(sport, port, sequence, ack, flags, data)))
    send_tcp(seq, 0, 2)
    proto, body = trial.packet()
    assert proto == 6 and body[13] & 0x12 == 0x12
    sp, dp, remote_seq, ack = struct.unpack_from("!HHII", body)
    assert (sp, dp, ack) == (port, sport, seq + 1)
    remote_next = remote_seq + 1
    send_tcp(seq + 1, remote_next, 0x10)
    tcp = b"DL26-TCP " + trial.nonce.encode()
    send_tcp(seq + 1, remote_next, 0x18, tcp)
    echo = bytearray()
    while len(echo) < len(tcp):
        proto, body = trial.packet()
        assert proto == 6 and not body[13] & 4
        sp, dp, rseq, ack = struct.unpack_from("!HHII", body)
        assert (sp, dp) == (port, sport)
        offset = (body[12] >> 4) * 4
        pseudo = HOST_IP + GUEST_IP + struct.pack("!BBH", 0, 6, len(body))
        assert checksum(pseudo + body) == 0
        data = body[offset:]
        if data:
            assert rseq == remote_next
            echo.extend(data); remote_next += len(data)
            send_tcp(seq + 1 + len(tcp), remote_next, 0x10)
    assert echo == tcp
    send_tcp(seq + 1 + len(tcp), remote_next, 0x14)
    trial.send(3); trial.sock.shutdown(socket.SHUT_WR)
    assert trial.receive()[0] == 3
    assert trial.sock.recv(1) == b""
    trial.codec.end()
    result = trial.result(0)
    assert result["complete"] and result["echoes"] == {"tcp":True, "udp":True}
    assert result["guest_close"] and result["host_close"] and result["peer_eof"]
    return result


def queue_checks():
    a, b = socket.socketpair()
    try:
        checked_socket(a); checked_socket(b)
        a.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1024)
        q = Queue()
        payload = [bytes([i]) * 1556 for i in range(8)]
        for p in payload:
            q.append(p)
        try:
            q.append(b"x")
            raise AssertionError("queue cap absent")
        except RuntimeError:
            pass
        for _ in range(16):
            q.write(a)
        assert q, "actual small socket send buffer must saturate"
        got = bytearray()
        deadline = time.monotonic() + 2
        while q or len(got) < sum(map(len,payload)):
            assert time.monotonic() < deadline
            q.write(a)
            try:
                got.extend(b.recv(4096))
            except BlockingIOError:
                pass
        assert got == b"".join(payload)
        b.close(); q.append(b"broken")
        try:
            q.write(a)
            raise AssertionError("broken peer accepted")
        except BrokenPipeError:
            pass
    finally:
        a.close(); b.close()


def run(library, output):
    output.mkdir(mode=0o700)
    reports = []
    cases = ["roundtrip", "wrong_nonce", "bad_crc", "early_eof", "partial_timeout", "address_policy", "accept_timeout", "term"]
    for case in cases:
        directory = output / case; directory.mkdir(mode=0o700)
        t = Trial(library, directory, accept_seconds=1 if case == "accept_timeout" else 8)
        try:
            if case == "roundtrip":
                r = roundtrip(t)
            elif case == "accept_timeout":
                r = t.result(1); assert "accept lifetime" in r["first_error"]
            elif case == "term":
                t.proc.terminate(); r = t.result(1); assert "signal15" in r["first_error"]
            else:
                t.connect()
                if case == "wrong_nonce":
                    other = Codec(library, "0"*48, 1)
                    t.sock.sendall(other.encode(1)); other.close()
                elif case == "bad_crc":
                    f = bytearray(t.codec.encode(1)); f[48] ^= 1; t.sock.sendall(f)
                elif case == "early_eof":
                    t.sock.shutdown(socket.SHUT_WR)
                elif case == "partial_timeout":
                    t.sock.sendall(t.codec.encode(1)[:17])
                elif case == "address_policy":
                    t.send(1); assert t.receive()[0] == 1
                    body = struct.pack("!4H", 42001, t.ready["udp_port"], 8, 0)
                    packet = bytearray(ip_packet(17, body))
                    packet[16:20] = socket.inet_aton("192.0.2.1")
                    packet[10:12] = b"\0\0"
                    packet[10:12] = struct.pack("!H", checksum(packet[:20]))
                    t.send(2, bytes(packet))
                r = t.result(1)
                assert not r["complete"] and r["first_error"]
                expected = {"wrong_nonce":"session", "bad_crc":"integrity",
                    "early_eof":"truncated", "partial_timeout":"partial frame",
                    "address_policy":"address policy"}[case]
                assert expected in r["first_error"], r["first_error"]
            reports.append({"case":case, "result":r})
        finally:
            t.cleanup()
    queue_checks()
    return {"status":"PASS_HOST_ONLY_SYNTHETIC_GUEST_PACKETS", "cases":reports,
            "real_queue_saturation_partial_resume_broken_peer":True,
            "not_claimed":["Darwin utun integration", "external network/DNS", "M1/M5 execution"]}


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--library", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    a = p.parse_args()
    print(json.dumps(run(a.library.resolve(), a.output.resolve()), indent=2))
