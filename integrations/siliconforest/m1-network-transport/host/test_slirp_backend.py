#!/usr/bin/env python3
"""Explicit local Linux-only QEMU/slirp test; no guest, SSH, routes or firewall.

Original packet construction. Not a TCP implementation or production bridge.
Only the selected QEMU child and owned loopback test sockets are used.
"""
import hashlib
import ipaddress
import json
import os
import shutil
import socket
import struct
import subprocess
import tempfile
import threading
import time

GUEST_IP = socket.inet_aton("10.0.2.15")
HOST_IP = socket.inet_aton("10.0.2.2")
GUEST_MAC = bytes.fromhex("525400123456")


def checksum(data):
    if len(data) % 2:
        data += b"\0"
    total = sum(struct.unpack("!%dH" % (len(data) // 2), data))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return (~total) & 65535


def ip_packet(proto, body):
    header = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(body), 1,
                         0x4000, 64, proto, 0, GUEST_IP, HOST_IP)
    return header[:10] + struct.pack("!H", checksum(header)) + header[12:] + body


def parse_ip(frame):
    if len(frame) < 34 or frame[12:14] != b"\x08\x00":
        return None
    p = frame[14:]
    ihl = (p[0] & 15) * 4
    size = struct.unpack_from("!H", p, 2)[0]
    assert p[0] >> 4 == 4 and 20 <= ihl <= size <= len(p) <= 1514
    assert checksum(p[:ihl]) == 0 and not (struct.unpack_from("!H", p, 6)[0] & 0x3fff)
    assert p[12:16] == HOST_IP and p[16:20] == GUEST_IP
    return p[9], p[ihl:size]


class Wire:
    def __init__(self, sock, deadline):
        self.sock, self.deadline = sock, deadline
        self.buffer = bytearray()
        self.sent, self.received, self.frames = [], [], 0

    def remaining(self):
        left = self.deadline - time.monotonic()
        if left <= 0:
            raise TimeoutError("whole local slirp test deadline")
        return min(left, 1.0)

    def send(self, frame):
        assert 14 <= len(frame) <= 1514
        self.sock.settimeout(self.remaining())
        self.sock.sendall(struct.pack("!I", len(frame)) + frame)
        self.sent.append(frame)

    def recv(self):
        while True:
            if len(self.buffer) >= 4:
                n = struct.unpack_from("!I", self.buffer)[0]
                assert 14 <= n <= 1514, "QEMU socket frame length"
                if len(self.buffer) >= n + 4:
                    frame = bytes(self.buffer[4:4+n])
                    del self.buffer[:4+n]
                    self.frames += 1
                    assert self.frames <= 128, "frame budget"
                    self.received.append(frame)
                    return frame
            self.sock.settimeout(self.remaining())
            chunk = self.sock.recv(4096)
            assert chunk, "QEMU socket EOF"
            self.buffer.extend(chunk)
            assert len(self.buffer) <= 8192

    def ipv4(self):
        while True:
            frame = self.recv()
            if frame[12:14] == b"\x08\x06":
                assert len(frame) >= 42
                arp = frame[14:42]
                assert arp[:6] == b"\0\1\x08\0\6\4"
                if arp[6:8] == b"\0\1" and arp[24:28] == GUEST_IP:
                    # Only answer an ARP request for this single owned fake IP.
                    reply = (b"\0\1\x08\0\6\4\0\2" + GUEST_MAC + GUEST_IP +
                             arp[8:14] + arp[14:18])
                    self.send(arp[8:14] + GUEST_MAC + b"\x08\x06" + reply)
                continue
            result = parse_ip(frame)
            if result is not None:
                return result


def tcp_body(srcport, dstport, seq, ack, flags, payload=b""):
    p = struct.pack("!HHIIBBHHH", srcport, dstport, seq, ack, 5 << 4,
                    flags, 16384, 0, 0) + payload
    pseudo = GUEST_IP + HOST_IP + struct.pack("!BBH", 0, 6, len(p))
    return p[:16] + struct.pack("!H", checksum(pseudo + p)) + p[18:]


def tcp_server(listener, payload, errors):
    conn = None
    try:
        listener.settimeout(8)
        conn, address = listener.accept()
        assert ipaddress.ip_address(address[0]).is_loopback
        conn.settimeout(4)
        data = bytearray()
        while len(data) < len(payload):
            part = conn.recv(len(payload) - len(data))
            assert part
            data.extend(part)
        assert data == payload
        conn.sendall(payload)
    except BaseException as error:
        errors.append(repr(error))
    finally:
        if conn is not None:
            conn.close()


def run():
    qemu = shutil.which("qemu-system-aarch64")
    if not qemu:
        raise RuntimeError("explicit prerequisite absent: qemu-system-aarch64")
    version = subprocess.run([qemu, "--version"], check=True, capture_output=True,
                             text=True, timeout=3).stdout.splitlines()[0]
    nonce = os.urandom(24).hex()
    payload = b"DLNET-local-TCP-" + nonce.encode()
    sockets, proc, thread = [], None, None
    errors, cleanup, result = [], {}, {}
    with tempfile.TemporaryFile() as diagnostics:
        try:
            wire_sock, child_sock = socket.socketpair()
            sockets.extend([wire_sock, child_sock])
            udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            udp.bind(("127.0.0.1", 0)); udp.settimeout(2); sockets.append(udp)
            tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            tcp.bind(("127.0.0.1", 0)); tcp.listen(1); sockets.append(tcp)
            child_fd = child_sock.fileno()
            args = [qemu, "-machine", "none", "-m", "32M", "-nodefaults",
                    "-no-user-config", "-display", "none", "-serial", "none",
                    "-monitor", "none", "-netdev",
                    "user,id=egress,net=10.0.2.0/24,host=10.0.2.2,dns=10.0.2.3,ipv6=off",
                    "-netdev", f"socket,id=wire,fd={child_fd}",
                    "-netdev", "hubport,id=to-user,hubid=0,netdev=egress",
                    "-netdev", "hubport,id=to-wire,hubid=0,netdev=wire"]
            proc = subprocess.Popen(args, stdin=subprocess.DEVNULL,
                                    stdout=diagnostics, stderr=diagnostics,
                                    pass_fds=(child_sock.fileno(),), close_fds=True)
            child_sock.close()
            wire = Wire(wire_sock, time.monotonic() + 15)
            arp = b"\0\1\x08\0\6\4\0\1" + GUEST_MAC + GUEST_IP + b"\0" * 6 + HOST_IP
            wire.send(b"\xff" * 6 + GUEST_MAC + b"\x08\x06" + arp)
            frame = wire.recv()
            assert len(frame) >= 42 and frame[:6] == GUEST_MAC
            assert frame[12:22] == b"\x08\x06\0\1\x08\0\6\4\0\2"
            assert frame[28:32] == HOST_IP and frame[38:42] == GUEST_IP
            host_mac = frame[22:28]

            # UDP carries an actual DNS-shaped question to a controlled host
            # port. The response is generated here, never by external DNS.
            name = nonce[:16].encode() + b".dlnet.invalid"
            question = b"".join(bytes([len(x)]) + x for x in name.split(b".")) + b"\0\0\1\0\1"
            query = struct.pack("!6H", 0x444c, 0x0100, 1, 0, 0, 0) + question
            srcport, dstport = 41001, udp.getsockname()[1]
            body = struct.pack("!4H", srcport, dstport, len(query) + 8, 0) + query
            wire.send(host_mac + GUEST_MAC + b"\x08\x00" + ip_packet(17, body))
            server_bytes, address = udp.recvfrom(2048)
            assert server_bytes == query and ipaddress.ip_address(address[0]).is_loopback
            response = (struct.pack("!6H", 0x444c, 0x8180, 1, 1, 0, 0) + question +
                        b"\xc0\x0c\0\1\0\1\0\0\0\0\0\4\xc0\0\x02\x01")
            udp.sendto(response, address)
            proto, body = wire.ipv4()
            assert proto == 17 and struct.unpack_from("!HHH", body) == (dstport, srcport, len(response) + 8)
            assert body[8:] == response

            # Handcrafted bounded TCP fixture crosses real slirp -> real
            # loopback TCP listener and back. Not a production TCP stack.
            thread = threading.Thread(target=tcp_server, args=(tcp, payload, errors), daemon=True)
            thread.start()
            srcport, dstport, seq = 41002, tcp.getsockname()[1], 10000
            def send_tcp(sequence, ack, flags, data=b""):
                segment = tcp_body(srcport, dstport, sequence, ack, flags, data)
                wire.send(host_mac + GUEST_MAC + b"\x08\x00" + ip_packet(6, segment))
            send_tcp(seq, 0, 2)
            proto, body = wire.ipv4()
            assert proto == 6 and len(body) >= 20
            sp, dp, remote_seq, ack = struct.unpack_from("!HHII", body)
            assert (sp, dp, ack) == (dstport, srcport, seq + 1) and body[13] & 0x12 == 0x12
            remote_next = remote_seq + 1
            send_tcp(seq + 1, remote_next, 0x10)
            send_tcp(seq + 1, remote_next, 0x18, payload)
            received = bytearray()
            while len(received) < len(payload):
                proto, body = wire.ipv4()
                assert proto == 6 and len(body) >= 20
                sp, dp, remote_seq, ack = struct.unpack_from("!HHII", body)
                assert (sp, dp) == (dstport, srcport) and not body[13] & 4
                offset = (body[12] >> 4) * 4
                assert 20 <= offset <= len(body)
                pseudo = HOST_IP + GUEST_IP + struct.pack("!BBH", 0, 6, len(body))
                assert checksum(pseudo + body) == 0
                data = body[offset:]
                if data:
                    assert remote_seq == remote_next and len(received) + len(data) <= len(payload)
                    received.extend(data); remote_next += len(data)
                    if body[13] & 1:
                        remote_next += 1
                    send_tcp(seq + 1 + len(payload), remote_next, 0x10)
            assert received == payload
            send_tcp(seq + 1 + len(payload), remote_next, 0x14)
            thread.join(2)
            assert not thread.is_alive() and not errors, errors
            result = {"status": "PASS_LOCAL_LINUX_QEMU_SLIRP_ONLY", "qemu_version": version,
                      "argv_template": ["socket,id=wire,fd=FD" if x.startswith("socket,") else x for x in args],
                      "nonce": nonce, "gateway_arp": True,
                      "controlled_udp_dns_query_bytes": len(query),
                      "controlled_udp_dns_query_hex": query.hex(),
                      "controlled_udp_dns_reply_hex": response.hex(),
                      "controlled_tcp_echo_bytes": len(payload),
                      "controlled_tcp_echo_hex": payload.hex(),
                      "wire_tx_frames": len(wire.sent), "wire_rx_frames": len(wire.received),
                      "wire_tx_frames_hex": [x.hex() for x in wire.sent],
                      "wire_rx_frames_hex": [x.hex() for x in wire.received],
                      "wire_tx_sha256": hashlib.sha256(b"".join(struct.pack("!I", len(x)) + x for x in wire.sent)).hexdigest(),
                      "wire_rx_sha256": hashlib.sha256(b"".join(struct.pack("!I", len(x)) + x for x in wire.received)).hexdigest(),
                      "not_claimed": ["Darwin/M1 utun integration", "external network or DNS", "production bridge/TCP implementation"],
                      "route_firewall_mutations": False}
        except BaseException as error:
            result = {"status": "FAIL_LOCAL_LINUX_QEMU_SLIRP", "error": repr(error)}
            raise
        finally:
            for sock in sockets:
                sock.close()
            if proc is not None:
                if proc.poll() is None:
                    proc.terminate(); cleanup["term_sent"] = True
                    try:
                        proc.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        proc.kill(); cleanup["kill_sent"] = True; proc.wait(timeout=2)
                cleanup["returncode"] = proc.returncode
                cleanup["reaped"] = proc.returncode is not None
            if thread is not None:
                thread.join(2); cleanup["server_thread_joined"] = not thread.is_alive()
            diagnostics.seek(0)
            stderr = diagnostics.read(16385)
            assert len(stderr) <= 16384, "diagnostics cap"
            cleanup["qemu_diagnostics"] = stderr.decode("utf-8", "replace")
            result["cleanup"] = cleanup
            if result.get("status") == "FAIL_LOCAL_LINUX_QEMU_SLIRP":
                print(json.dumps(result, indent=2), flush=True)
    assert result.get("status", "").startswith("PASS"), result
    assert cleanup.get("reaped") and cleanup.get("server_thread_joined")
    assert not errors and not cleanup.get("kill_sent") and cleanup.get("returncode") == 0, errors
    return result


if __name__ == "__main__":
    print(json.dumps(run(), indent=2))
