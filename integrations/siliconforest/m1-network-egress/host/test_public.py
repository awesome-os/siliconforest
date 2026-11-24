#!/usr/bin/env python3
"""Explicit bounded real public example.com DNS/HTTP via Linux QEMU/slirp.

Synthetic guest packets, actual host networking. No Darwin/M1, SSH or routes.
One selected answer, one public TCP flow, no endpoint retry/scan or redirect.
"""
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
import time
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from bindings import Codec
from policy import Policy, GUEST, DNS, QUESTION, checksum, parse_packet


def packet(src, dst, proto, body):
    header = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20+len(body), 1, 0x4000,
                         64, proto, 0, src, dst)
    return header[:10] + struct.pack("!H", checksum(header)) + header[12:] + body


def udp(src, dst, sport, dport, payload):
    body = struct.pack("!4H", sport, dport, len(payload)+8, 0) + payload
    pseudo = src + dst + struct.pack("!BBH", 0, 17, len(body))
    value = checksum(pseudo+body) or 65535
    body = body[:6] + struct.pack("!H", value) + body[8:]
    return packet(src, dst, 17, body)


def tcp(src, dst, sport, dport, seq, ack, flags, payload=b""):
    body = struct.pack("!HHIIBBHHH", sport, dport, seq, ack, 5<<4,
                       flags, 32768, 0, 0) + payload
    pseudo = src + dst + struct.pack("!BBH", 0, 6, len(body))
    body = body[:16] + struct.pack("!H", checksum(pseudo+body)) + body[18:]
    return packet(src, dst, 6, body)


def run(library, output):
    output.mkdir(mode=0o700)
    nonce, proc, codec, sock = os.urandom(24).hex(), None, None, None
    result = {"status":"FAIL_HOST_ONLY_PUBLIC_TEST", "nonce":nonce}
    log = os.fdopen(os.open(output/"process.log", os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC, 0o600), "wb", buffering=0)
    records = output/"backend"
    try:
        proc = subprocess.Popen([sys.executable, "-I", str(Path(__file__).with_name("backend.py")),
            "--profile", "example-http-v1", "--nonce", nonce,
            "--codec-library", str(library), "--codec-sha256", hashlib.sha256(library.read_bytes()).hexdigest(),
            "--records", str(records), "--listen-port", "0", "--accept-seconds", "8", "--seconds", "30"],
            stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 5
        ready = None
        while time.monotonic() < deadline and proc.poll() is None:
            try:
                ready = json.loads((records/"ready.json").read_text()); break
            except (FileNotFoundError, json.JSONDecodeError):
                time.sleep(0.01)
        if ready is None:
            raise RuntimeError("backend readiness missing")
        policy, codec, pending = Policy(nonce), Codec(library, nonce, 1), deque()
        sock = socket.create_connection(("127.0.0.1", ready["listen_port"]), timeout=2)
        deadline = time.monotonic() + 25

        def send(kind, data=b""):
            if time.monotonic() >= deadline:
                raise TimeoutError("whole public fixture deadline")
            if kind == 2:
                policy.outgoing(data)
            sock.settimeout(min(2, max(0.01, deadline-time.monotonic())))
            sock.sendall(codec.encode(kind, data))

        def receive():
            while not pending:
                if time.monotonic() >= deadline:
                    raise TimeoutError("whole public fixture deadline")
                sock.settimeout(min(2, deadline-time.monotonic()))
                data = sock.recv(4096)
                if not data:
                    raise RuntimeError("backend EOF; inspect closed failure record")
                codec.feed(data, lambda *x:pending.append(x))
            kind, seq, data = pending.popleft()
            if kind == 2:
                policy.incoming(data)
            return kind, seq, data

        send(1); assert receive()[:2] == (1, 1)
        query = os.urandom(2) + struct.pack("!5H", 0x0100, 1, 0, 0, 0) + QUESTION
        dns = udp(GUEST, DNS, 43001, 53, query)
        for attempt in range(3):
            send(2, dns)
            try:
                kind, seq, data = receive()
                assert kind == 2 and parse_packet(data)[0] == 17
                break
            except TimeoutError:
                if attempt == 2:
                    raise
        assert policy.endpoint is not None
        endpoint, sport, initial = policy.endpoint, 43002, 30000

        def send_tcp(sequence, ack, flags, body=b""):
            send(2, tcp(GUEST, endpoint, sport, 80, sequence & 0xffffffff,
                        ack & 0xffffffff, flags, body))

        for attempt in range(3):
            send_tcp(initial, 0, 2)
            try:
                kind, seq, data = receive()
                assert kind == 2 and parse_packet(data)[0] == 6
                break
            except TimeoutError:
                if attempt == 2:
                    raise
        assert policy.server_isn is not None
        send_tcp(initial+1, policy.server_isn+1, 0x10)
        send_tcp(initial+1, policy.server_isn+1, 0x18, policy.request)
        request_retries = 0
        while not policy.complete():
            try:
                kind, seq, data = receive()
                assert kind == 2
                _proto, _src, _dst, _sp, _dp, body, payload = parse_packet(data)
                if payload or body[13] & 1:
                    ack = policy.server_isn + 1 + policy.contiguous
                    if policy.fin_offset == policy.contiguous:
                        ack += 1
                    send_tcp(initial+1+len(policy.request), ack, 0x10)
            except TimeoutError:
                request_retries += 1
                if request_retries > 2:
                    raise
                send_tcp(initial+1, policy.server_isn+1, 0x18, policy.request)
        final_ack = policy.server_isn + 2 + policy.contiguous
        send_tcp(initial+1+len(policy.request), final_ack, 0x11)
        # Observe an ACK covering the client FIN before closing the relay.
        while True:
            kind, seq, data = receive(); assert kind == 2
            body = parse_packet(data)[5]
            if struct.unpack_from("!I", body, 8)[0] == initial+2+len(policy.request):
                break
        send(3); sock.shutdown(socket.SHUT_WR)
        assert receive()[0] == 3 and sock.recv(1) == b""
        codec.end()
        rc = proc.wait(5)
        backend = json.loads((records/"result.json").read_text())
        if rc != 0 or not backend["complete"]:
            raise RuntimeError("backend incomplete/failed")
        for name, binding in backend["closed_records"].items():
            data = (records/name).read_bytes()
            assert len(data) == binding["bytes"] and hashlib.sha256(data).hexdigest() == binding["sha256"]
        assert backend["queued_bytes_client_qemu"] == [0,0]
        assert backend["stream_bytes_written_client_qemu"] == [
            (records/"relay-tx.bin").stat().st_size,(records/"ethernet-tx.bin").stat().st_size]
        result.update(status="PASS_HOST_ONLY_PUBLIC_DNS_HTTP", backend_returncode=rc,
                      egress=policy.summary(), client_fin_acked=True,
                      scope="Synthetic guest packets via real Linux slirp; no Darwin/M1 execution")
    except BaseException as error:
        result["error"] = repr(error)
    finally:
        if sock is not None:
            sock.close()
        if codec is not None:
            codec.close()
        if proc is not None:
            if proc.poll() is None:
                try:
                    proc.wait(2)
                except subprocess.TimeoutExpired:
                    proc.terminate()
                    try:
                        proc.wait(4)
                    except subprocess.TimeoutExpired:
                        proc.kill(); proc.wait(2); result["forced_backend_kill"] = True
            result["actual_backend_returncode"] = proc.returncode
        log.close()
        if (records/"result.json").exists():
            backend = json.loads((records/"result.json").read_text())
            result["closed_backend"] = backend
            if not backend["complete"] or backend["cleanup"]["errors"]:
                result["status"] = "FAIL_HOST_ONLY_PUBLIC_TEST"
        result["files_sha256"] = {str(p.relative_to(output)):hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in sorted(output.rglob('*')) if p.is_file()}
        with os.fdopen(os.open(output/"test-result.json", os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC, 0o600),"w") as report:
            report.write(json.dumps(result,indent=2)+"\n");report.flush();os.fsync(report.fileno())
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--allow-public-example-http", action="store_true", required=True)
    args = parser.parse_args()
    result = run(args.library.resolve(), args.output.resolve())
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result["status"].startswith("PASS") else 1)
