#!/usr/bin/env python3
"""Original opt-in Linux DLNET001 <-> Ethernet/QEMU/slirp bounded backend.

No SSH, host routes, firewall, TAP, guest control, shell or public listener.
This version permits only the two nonce echo endpoints on slirp host alias.
"""
import argparse
from collections import deque
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import signal
import shutil
import socket
import struct
import subprocess
import sys
import time
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from codec import Codec

GUEST_IP, HOST_IP = socket.inet_aton("10.0.2.15"), socket.inet_aton("10.0.2.2")
GUEST_MAC = bytes.fromhex("525400123456")
MAX_QUEUE, MAX_FRAMES, MAX_BYTES = 8, 4096, 4 * 1024 * 1024
CLOSE_EOF_SECONDS = 15
ACTIVE_IDLE_SECONDS = 10
STOP_REQUESTED = 0


def checksum(data):
    if len(data) & 1:
        data += b"\0"
    n = sum(struct.unpack("!%dH" % (len(data) // 2), data))
    while n >> 16:
        n = (n & 65535) + (n >> 16)
    return (~n) & 65535


def checked_socket(sock):
    sock.setblocking(False)
    sock.set_inheritable(False)
    if not (fcntl.fcntl(sock, fcntl.F_GETFL) & os.O_NONBLOCK and
            fcntl.fcntl(sock, fcntl.F_GETFD) & fcntl.FD_CLOEXEC):
        raise RuntimeError("socket flags readback")
    return sock


def listener(kind, port):
    sock = socket.socket(socket.AF_INET, kind)
    try:
        sock.bind(("127.0.0.1", port))
        if kind == socket.SOCK_STREAM:
            sock.listen(1)
        return checked_socket(sock)
    except BaseException:
        sock.close()
        raise


class Queue:
    def __init__(self):
        self.parts = deque()
        self.since = None
        self.written = 0

    def append(self, data):
        if len(self.parts) >= MAX_QUEUE:
            raise RuntimeError("bounded queue overflow")
        if not self.parts:
            self.since = time.monotonic()
        self.parts.append(memoryview(data))

    def write(self, sock):
        if not self.parts:
            return
        try:
            n = sock.send(self.parts[0][:4096])
        except BlockingIOError:
            return
        if n <= 0:
            raise RuntimeError("zero stream write")
        self.written += n
        self.parts[0] = self.parts[0][n:]
        if not self.parts[0]:
            self.parts.popleft()
        # Deadline measures continuous nonempty queue, not reset by a trickle.
        if not self.parts:
            self.since = None

    def __bool__(self):
        return bool(self.parts)


class Backend:
    def __init__(self, library, nonce, records, ports=(0, 0, 0), seconds=60, accept_seconds=300):
        if (not re.fullmatch(r"[0-9a-f]{48}", nonce) or not 1 <= seconds <= 120 or
                not 1 <= accept_seconds <= 300):
            raise ValueError("nonce/lifetime")
        self.nonce, self.seconds, self.library = nonce, seconds, Path(library).resolve()
        self.accept_seconds, self.active_started = accept_seconds, None
        self.records = Path(records)
        self.records.mkdir(mode=0o700)  # new private directory, never reuse
        if self.records.stat().st_mode & 0o777 != 0o700:
            raise RuntimeError("private record mode")
        self.logs, self.sockets, self.codec, self.proc = {}, [], None, None
        self.closed = False
        self.first_error, self.cleanup = None, {}
        self.qwire, self.client = None, None
        self.echo = None
        self.tcp_payload_complete = False
        self.server_in, self.server_out = bytearray(), Queue()
        self.client_out, self.qemu_out = Queue(), Queue()
        self.qemu_buffer = bytearray()
        self.flows = set()
        self.opened = self.peer_close = self.sent_close = self.client_eof = False
        self.frame_counts, self.byte_counts = [0, 0], [0, 0]
        self.accepted = self.forwarded = 0
        self.started = time.monotonic()
        self.last_activity = self.started
        self.partial_since = self.eof_since = None
        self.client_frame_buffer = bytearray()
        self.expected = {"tcp": b"DL26-TCP " + nonce.encode(), "udp": b"DL26-UDP " + nonce.encode()}
        self.echoes = {"tcp": False, "udp": False}
        try:
            self.codec = Codec(self.library, nonce, 2)
            for name in ("relay-rx.bin", "relay-tx.bin", "ethernet-rx.bin", "ethernet-tx.bin"):
                self.logs[name] = self.open_record(name)
            self.control = self.own(listener(socket.SOCK_STREAM, ports[0]))
            self.tcp = self.own(listener(socket.SOCK_STREAM, ports[1]))
            self.udp = self.own(listener(socket.SOCK_DGRAM, ports[2]))
            self.ports = (self.control.getsockname()[1], self.tcp.getsockname()[1], self.udp.getsockname()[1])
            self.qwire, child = socket.socketpair()
            self.own(self.qwire); self.own(child)
            qemu = shutil.which("qemu-system-aarch64")
            if not qemu:
                raise RuntimeError("qemu-system-aarch64 missing")
            self.qemu_path = str(Path(qemu).resolve())
            self.qemu_hash = hashlib.sha256(Path(qemu).read_bytes()).hexdigest()
            self.diagnostics = self.open_record("qemu.log")
            self.args = [qemu, "-machine", "none", "-m", "32M", "-nodefaults", "-no-user-config",
                         "-run-with", "exit-with-parent=on",
                         "-display", "none", "-serial", "none", "-monitor", "none", "-netdev",
                         "user,id=egress,net=10.0.2.0/24,host=10.0.2.2,dns=10.0.2.3,ipv6=off",
                         "-netdev", f"socket,id=wire,fd={child.fileno()}",
                         "-netdev", "hubport,id=to-user,hubid=0,netdev=egress",
                         "-netdev", "hubport,id=to-wire,hubid=0,netdev=wire"]
            self.proc = subprocess.Popen(self.args, stdin=subprocess.DEVNULL,
                                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                         pass_fds=(child.fileno(),), close_fds=True)
            # Positive PID is immediately owned, including later setup errors.
            child.close()
            os.set_blocking(self.proc.stdout.fileno(), False)
            if not (fcntl.fcntl(self.proc.stdout, fcntl.F_GETFL) & os.O_NONBLOCK and
                    fcntl.fcntl(self.proc.stdout, fcntl.F_GETFD) & fcntl.FD_CLOEXEC):
                raise RuntimeError("QEMU diagnostics pipe flags")
            checked_socket(self.qwire)
            self.gateway_mac = None
            arp = b"\0\1\x08\0\6\4\0\1" + GUEST_MAC + GUEST_IP + b"\0"*6 + HOST_IP
            self.qemu_out.append(self.ether_frame(b"\xff"*6 + GUEST_MAC + b"\x08\x06" + arp))
        except BaseException as error:
            self.first_error = repr(error)
            self.close()
            raise

    def own(self, sock):
        self.sockets.append(sock)
        return sock

    def open_record(self, name):
        fd = os.open(self.records / name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o600)
        return os.fdopen(fd, "wb", buffering=0)

    def write_json(self, name, value):
        temporary = name + ".tmp"
        with self.open_record(temporary) as f:
            data = (json.dumps(value, indent=2) + "\n").encode()
            if f.write(data) != len(data):
                raise RuntimeError("record short write")
            os.fsync(f.fileno())
        os.replace(self.records / temporary, self.records / name)
        directory = os.open(self.records, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)

    def record(self, name, data):
        log = self.logs[name]
        if log.tell() + len(data) > 5 * 1024 * 1024:
            raise RuntimeError("transcript cap")
        if log.write(data) != len(data):
            raise RuntimeError("transcript short write")

    def ether_frame(self, frame):
        if not 14 <= len(frame) <= 1514:
            raise RuntimeError("Ethernet frame length")
        data = struct.pack("!I", len(frame)) + frame
        self.record("ethernet-tx.bin", data)
        return data

    def encode(self, kind, payload=b""):
        frame = self.codec.encode(kind, payload)
        self.frame_counts[1] += 1
        self.byte_counts[1] += len(payload)
        self.check_budget()
        self.record("relay-tx.bin", frame)
        self.client_out.append(frame)

    def check_budget(self):
        if max(self.frame_counts) > MAX_FRAMES or max(self.byte_counts) > MAX_BYTES:
            raise RuntimeError("session packet/byte budget")

    def drain_diagnostics(self, reads=1):
        for _ in range(reads):
            try:
                data = os.read(self.proc.stdout.fileno(), 4096)
            except BlockingIOError:
                return
            if not data:
                return
            remaining = 16384 - self.diagnostics.tell()
            if len(data) > remaining:
                if remaining:
                    self.diagnostics.write(data[:remaining])
                raise RuntimeError("QEMU diagnostics cap exceeded; tail unretained")
            if self.diagnostics.write(data) != len(data):
                raise RuntimeError("QEMU diagnostics short write")

    def packet_flow(self, packet, incoming):
        if not 20 <= len(packet) <= 1500 or packet[0] >> 4 != 4:
            raise RuntimeError("IPv4 length/version")
        ihl = (packet[0] & 15) * 4
        if ihl != 20 or struct.unpack_from("!H", packet, 2)[0] != len(packet):
            raise RuntimeError("IPv4 exact total length/IHL")
        if checksum(packet[:ihl]) or struct.unpack_from("!H", packet, 6)[0] & 0xbfff:
            raise RuntimeError("IPv4 checksum/fragment policy")
        src, dst, proto = packet[12:16], packet[16:20], packet[9]
        if (src, dst) != ((HOST_IP, GUEST_IP) if incoming else (GUEST_IP, HOST_IP)):
            raise RuntimeError("controlled host-alias address policy")
        body = packet[ihl:]
        if proto not in (6, 17) or len(body) < (20 if proto == 6 else 8):
            raise RuntimeError("TCP/UDP policy")
        sport, dport = struct.unpack_from("!HH", body)
        if proto == 6 and not 20 <= (body[12] >> 4)*4 <= len(body):
            raise RuntimeError("TCP header")
        if proto == 17 and struct.unpack_from("!H", body, 4)[0] != len(body):
            raise RuntimeError("UDP length")
        pseudo = src + dst + struct.pack("!BBH", 0, proto, len(body))
        if (proto == 6 or body[6:8] != b"\0\0") and checksum(pseudo + body):
            raise RuntimeError("transport checksum")
        if incoming:
            if (proto, dport, sport) not in self.flows:
                raise RuntimeError("unsolicited reverse flow")
        else:
            expected = self.ports[1] if proto == 6 else self.ports[2]
            if dport != expected or not sport:
                raise RuntimeError("controlled port policy")
            self.flows.add((proto, sport, dport))
            if len(self.flows) > 64:
                raise RuntimeError("flow budget")

    def receive(self, kind, sequence, packet):
        self.frame_counts[0] += 1; self.byte_counts[0] += len(packet); self.check_budget()
        if kind == 1:
            if self.gateway_mac is None:
                raise RuntimeError("OPEN before backend readiness")
            self.opened = True; self.encode(1)
        elif kind == 2:
            if self.peer_close:
                raise RuntimeError("packet after CLOSE")
            self.packet_flow(packet, False)
            self.accepted += 1
            self.qemu_out.append(self.ether_frame(self.gateway_mac + GUEST_MAC + b"\x08\x00" + packet))
        elif kind == 3:
            self.peer_close = True
        else:
            raise RuntimeError("unknown validated frame type")

    def ethernet(self, frame):
        if frame[12:14] == b"\x08\x06":
            if len(frame) < 42 or any(frame[42:]):
                raise RuntimeError("ARP length/padding")
            a = frame[14:42]
            if a[:6] != b"\0\1\x08\0\6\4":
                raise RuntimeError("ARP shape")
            if a[6:8] == b"\0\2" and a[14:18] == HOST_IP and a[24:28] == GUEST_IP:
                if frame[:6] != GUEST_MAC or a[18:24] != GUEST_MAC or frame[6:12] != a[8:14]:
                    raise RuntimeError("ARP reply identities")
                if self.gateway_mac is not None and self.gateway_mac != a[8:14]:
                    raise RuntimeError("gateway MAC changed")
                self.gateway_mac = a[8:14]
            elif a[6:8] == b"\0\1" and a[24:28] == GUEST_IP and a[14:18] == HOST_IP:
                if (frame[6:12] != a[8:14] or a[8:14] != self.gateway_mac or
                        frame[:6] not in (GUEST_MAC, b"\xff"*6)):
                    raise RuntimeError("ARP request identities")
                reply = b"\0\1\x08\0\6\4\0\2" + GUEST_MAC + GUEST_IP + a[8:14] + HOST_IP
                self.qemu_out.append(self.ether_frame(a[8:14] + GUEST_MAC + b"\x08\x06" + reply))
            else:
                raise RuntimeError("unexpected ARP")
            return
        if frame[12:14] != b"\x08\x00" or frame[:6] != GUEST_MAC or frame[6:12] != self.gateway_mac:
            raise RuntimeError("Ethernet type/MAC policy")
        packet = frame[14:]
        if len(packet) < 20:
            raise RuntimeError("short IPv4")
        size = struct.unpack_from("!H", packet, 2)[0]
        if size > len(packet) or any(packet[size:]):
            raise RuntimeError("IPv4 padding/size")
        packet = packet[:size]
        self.packet_flow(packet, True)
        if self.sent_close:
            raise RuntimeError("packet after local CLOSE")
        self.forwarded += 1
        self.encode(2, packet)

    def qemu_read(self):
        data = self.qwire.recv(4096)
        if not data:
            raise RuntimeError("QEMU endpoint EOF")
        self.record("ethernet-rx.bin", data)
        self.qemu_buffer.extend(data)
        if len(self.qemu_buffer) > 8192:
            raise RuntimeError("QEMU receive cap")
        self.drain_qemu()

    def drain_qemu(self):
        while len(self.qemu_buffer) >= 4 and len(self.client_out.parts) < MAX_QUEUE:
            size = struct.unpack_from("!I", self.qemu_buffer)[0]
            if not 14 <= size <= 1514:
                raise RuntimeError("QEMU advertised Ethernet length")
            if len(self.qemu_buffer) < 4 + size:
                break
            frame = bytes(self.qemu_buffer[4:4+size]); del self.qemu_buffer[:4+size]
            self.ethernet(frame)

    def client_read(self):
        data = self.client.recv(4096)
        if not data:
            self.codec.end(); self.client_eof = True
            return
        self.record("relay-rx.bin", data)
        # Track only a bounded incomplete frame for the two-second deadline.
        self.client_frame_buffer.extend(data)
        while len(self.client_frame_buffer) >= 56:
            size = 56 + struct.unpack_from("!H", self.client_frame_buffer, 12)[0]
            if size > 1556:
                break  # C codec fails immediately below
            if len(self.client_frame_buffer) < size:
                break
            del self.client_frame_buffer[:size]
            self.partial_since = None
        if self.client_frame_buffer:
            if self.partial_since is None:
                self.partial_since = time.monotonic()
        else:
            self.partial_since = None
        self.codec.feed(data, self.receive)

    def run(self):
        normal = False
        try:
            ready = False
            while True:
                now = time.monotonic()
                if STOP_REQUESTED:
                    raise RuntimeError(f"backend signal{STOP_REQUESTED}")
                if self.proc.poll() is not None:
                    raise RuntimeError("QEMU exited before backend cleanup")
                self.drain_diagnostics()
                if self.active_started is None:
                    if now - self.started >= self.accept_seconds:
                        raise TimeoutError("bounded relay accept lifetime")
                elif now - self.active_started >= self.seconds:
                    raise TimeoutError("whole active backend lifetime")
                if not ready and now - self.started > 5:
                    raise TimeoutError("gateway readiness")
                if self.eof_since is None and self.active_started is not None and now - self.last_activity > ACTIVE_IDLE_SECONDS:
                    raise TimeoutError("idle channel")
                for queue in (self.client_out, self.qemu_out, self.server_out):
                    if queue.since is not None and now - queue.since > 2:
                        raise TimeoutError("nonempty write queue")
                if self.partial_since is not None and now - self.partial_since > 2:
                    raise TimeoutError("partial frame")
                if self.gateway_mac is not None and not ready:
                    maps = Path(f"/proc/{self.proc.pid}/maps").read_text().splitlines()
                    slirp = {row.split(maxsplit=5)[5] for row in maps
                             if len(row.split(maxsplit=5)) == 6 and "/libslirp.so" in row.split(maxsplit=5)[5]}
                    if len(slirp) != 1:
                        raise RuntimeError("actual child libslirp binding missing/ambiguous")
                    self.slirp_binding = {path:hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in slirp}
                    self.write_json("ready.json", {"schema":1,"nonce":self.nonce,
                        "listen_host":"127.0.0.1","listen_port":self.ports[0],
                        "tcp_port":self.ports[1],"udp_port":self.ports[2],
                        "guest_ipv4":"10.0.2.15","host_alias":"10.0.2.2","mtu":1500,
                        "codec_library_sha256":hashlib.sha256(self.library.read_bytes()).hexdigest(),
                        "qemu_sha256":self.qemu_hash,"seconds":self.seconds,
                        "close_eof_seconds":CLOSE_EOF_SECONDS,"active_idle_seconds":ACTIVE_IDLE_SECONDS,
                        "actual_child_libslirp":self.slirp_binding,
                        "accept_seconds":self.accept_seconds,"backend_pid":os.getpid(),"qemu_pid":self.proc.pid})
                    ready = True
                if not self.sent_close:
                    self.drain_qemu()
                if self.peer_close and not self.qemu_out and not self.sent_close:
                    self.encode(3); self.sent_close = True; self.eof_since = now
                if self.sent_close and not self.client_out and self.client_eof:
                    if not all(self.echoes.values()):
                        raise RuntimeError("closed without both controlled echoes")
                    normal = True; break
                if self.eof_since is not None and now - self.eof_since > CLOSE_EOF_SECONDS:
                    raise TimeoutError("closed peer EOF")
                with selectors.DefaultSelector() as sel:
                    def reg(sock, events, name):
                        if events and sock.fileno() >= 0:
                            sel.register(sock, events, name)
                    reg(self.control, selectors.EVENT_READ if ready and self.client is None else 0, "accept")
                    reg(self.tcp, selectors.EVENT_READ if self.echo is None else 0, "tcp_accept")
                    reg(self.udp, selectors.EVENT_READ, "udp")
                    reg(self.qwire, (selectors.EVENT_READ if not self.sent_close and len(self.client_out.parts) < MAX_QUEUE else 0) |
                        (selectors.EVENT_WRITE if self.qemu_out else 0), "qemu")
                    if self.client is not None:
                        reg(self.client, (selectors.EVENT_READ if not self.client_eof and len(self.qemu_out.parts) < MAX_QUEUE else 0) |
                            (selectors.EVENT_WRITE if self.client_out else 0), "client")
                    if self.echo is not None:
                        reg(self.echo, (selectors.EVENT_READ if not self.tcp_payload_complete else 0) |
                            (selectors.EVENT_WRITE if self.server_out else 0), "echo")
                    events = sel.select(0.05)
                for key, mask in events:
                    self.last_activity = time.monotonic()
                    tag = key.data
                    if tag == "accept":
                        sock, addr = self.control.accept()
                        if addr[0] != "127.0.0.1":
                            sock.close(); raise RuntimeError("non-loopback peer")
                        self.client = self.own(checked_socket(sock)); self.control.close()
                        self.active_started = self.last_activity = time.monotonic()
                    elif tag == "tcp_accept":
                        sock, addr = self.tcp.accept()
                        if addr[0] != "127.0.0.1":
                            sock.close(); raise RuntimeError("non-loopback echo")
                        self.echo = self.own(checked_socket(sock)); self.tcp.close()
                    elif tag == "udp":
                        data, addr = self.udp.recvfrom(2048)
                        if addr[0] != "127.0.0.1" or data != self.expected["udp"] or self.echoes["udp"]:
                            raise RuntimeError("controlled UDP payload/peer/once")
                        if self.udp.sendto(data, addr) != len(data):
                            raise RuntimeError("UDP reply short write")
                        self.echoes["udp"] = True
                    elif tag == "qemu":
                        if mask & selectors.EVENT_WRITE:
                            self.qemu_out.write(self.qwire)
                        if mask & selectors.EVENT_READ:
                            self.qemu_read()
                    elif tag == "client":
                        if mask & selectors.EVENT_WRITE:
                            self.client_out.write(self.client)
                        if mask & selectors.EVENT_READ:
                            self.client_read()
                    elif tag == "echo":
                        if mask & selectors.EVENT_READ:
                            data = self.echo.recv(512)
                            if not data:
                                raise RuntimeError("TCP echo premature EOF")
                            self.server_in.extend(data)
                            if not self.expected["tcp"].startswith(self.server_in):
                                raise RuntimeError("controlled TCP exact payload")
                            if self.server_in == self.expected["tcp"]:
                                self.tcp_payload_complete = True; self.server_out.append(bytes(self.server_in))
                        if mask & selectors.EVENT_WRITE:
                            self.server_out.write(self.echo)
                            if self.tcp_payload_complete and not self.server_out:
                                self.echoes["tcp"] = True
        except BaseException as error:
            self.first_error = repr(error)
        finally:
            self.close()
            complete = normal and self.first_error is None and not self.cleanup.get("errors")
            record_hashes = {name: {"bytes":(self.records/name).stat().st_size,
                "sha256":hashlib.sha256((self.records/name).read_bytes()).hexdigest()}
                for name in list(self.logs) + ["qemu.log", "ready.json"]
                if (self.records/name).is_file()}
            self.write_json("result.json", {"schema":1,"nonce":self.nonce,
                "complete":complete,"closed_records":record_hashes,
                "close_eof_seconds":CLOSE_EOF_SECONDS,"active_idle_seconds":ACTIVE_IDLE_SECONDS,
                "active_seconds":self.seconds,"accept_seconds":self.accept_seconds,
                "first_error":self.first_error,"cleanup":self.cleanup,"echoes":self.echoes,
                "controlled_tcp_hex":bytes(self.server_in).hex(),"controlled_udp_hex":self.expected["udp"].hex() if self.echoes["udp"] else None,
                "frame_counts_guest_host":self.frame_counts,"packet_bytes_guest_host":self.byte_counts,
                "guest_packets_accepted":self.accepted,"host_packets_forwarded":self.forwarded,
                "queued_bytes_client_qemu_echo":[sum(map(len,q.parts)) for q in (self.client_out,self.qemu_out,self.server_out)],
                "stream_bytes_written_client_qemu_echo":[q.written for q in (self.client_out,self.qemu_out,self.server_out)],
                "guest_close":self.peer_close,"host_close":self.sent_close,"peer_eof":self.client_eof,
                "qemu_sha256":self.qemu_hash,"actual_child_libslirp":getattr(self,"slirp_binding",None),"route_firewall_mutations":False,
                "scope":"Linux backend controlled host-alias TCP/UDP only; not Darwin or external connectivity"})
        return 0 if complete else 1

    def close(self):
        if self.closed:
            return
        self.closed = True
        errors = []
        for sock in self.sockets:
            try:
                sock.close()
            except OSError as error:
                errors.append(repr(error))
        if self.proc is not None:
            try:
                if self.proc.poll() is None:
                    self.proc.terminate(); self.cleanup["term_sent"] = True
                    try:
                        self.proc.wait(2)
                    except subprocess.TimeoutExpired:
                        self.proc.kill(); self.cleanup["kill_sent"] = True; self.proc.wait(2)
                self.cleanup["qemu_returncode"] = self.proc.returncode
                self.cleanup["qemu_reaped"] = self.proc.returncode is not None
                if self.proc.returncode != 0 or self.cleanup.get("kill_sent"):
                    errors.append("QEMU abnormal/forced cleanup")
            except BaseException as error:
                errors.append(repr(error))
            try:
                self.drain_diagnostics(5)
            except BaseException as error:
                errors.append(repr(error))
            try:
                self.proc.stdout.close()
            except OSError as error:
                errors.append(repr(error))
        for f in self.logs.values():
            try:
                os.fsync(f.fileno()); f.close()
            except OSError as error:
                errors.append(repr(error))
        if hasattr(self, "diagnostics"):
            try:
                os.fsync(self.diagnostics.fileno()); self.diagnostics.close()
            except OSError as error:
                errors.append(repr(error))
        if self.codec is not None:
            self.codec.close()
        self.cleanup["errors"] = errors


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("nonce", "codec-library", "records"):
        p.add_argument("--" + name, required=True)
    for name in ("listen-port", "tcp-echo-port", "udp-echo-port", "seconds", "accept-seconds"):
        p.add_argument("--" + name, required=True, type=int)
    a = p.parse_args()
    ports = (a.listen_port, a.tcp_echo_port, a.udp_echo_port)
    if any(not 0 <= n <= 65535 for n in ports):
        p.error("ports0..65535 (0 selects an owned ephemeral port)")
    if not Path(a.records).is_absolute() or not Path(a.codec_library).is_absolute():
        p.error("absolute records and codec-library paths required")
    def interrupted(signum, _frame):
        # selectors may absorb InterruptedError. Record a sticky request and
        # leave waits/cleanup uninterrupted even if another signal arrives.
        global STOP_REQUESTED
        STOP_REQUESTED = signum
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)
    signal.signal(signal.SIGPIPE, signal.SIG_IGN)
    b = Backend(a.codec_library, a.nonce, a.records, ports, a.seconds, a.accept_seconds)
    raise SystemExit(b.run())


if __name__ == "__main__":
    main()
