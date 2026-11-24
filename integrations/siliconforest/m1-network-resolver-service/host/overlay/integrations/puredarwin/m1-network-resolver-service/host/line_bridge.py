"""Finite typed data bridge. Host-to-guest writes contain SPACE/LF only.

No shell command, serial opening, subprocess, proxy or device control lives here.
The existing console owner supplies the exact recorded writer after READY.
"""
from collections import deque
import errno
import hashlib
import re
import socket
import struct
import time

HEADER = struct.Struct('!8sBBHHHQ24sII')
MAGIC = b'DLNET001'
MAX_FRAME = 1556
MAX_LINE = 4096
MAX_QUEUE = 8
MAX_DATA_BYTES = 131072
MAX_CHUNK_BYTES = 192
MAX_CHUNKS = 1024
MAX_ACKS = 4096
ACK_SECONDS = 3


def crc32c(raw):
    crc = 0xffffffff
    for byte in raw:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82f63b78 if crc & 1 else 0)
    return crc ^ 0xffffffff


def ipv4(raw):
    if not 20 <= len(raw) <= 1500 or raw[0] >> 4 != 4:
        return False
    ihl = (raw[0] & 15) * 4
    if not 20 <= ihl <= len(raw) or int.from_bytes(raw[2:4], 'big') != len(raw):
        return False
    total = sum(int.from_bytes(raw[i:i+2], 'big') for i in range(0, ihl, 2))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return total == 65535


class Frames:
    def __init__(self, nonce, direction):
        if not re.fullmatch('[0-9a-f]{48}', nonce) or direction not in (1, 2):
            raise ValueError('frame profile')
        self.nonce = bytes.fromhex(nonce)
        self.direction, self.next = direction, 1
        self.opened = self.closed = False
        self.buffer = bytearray()
        self.count = 0
        self.error = None

    def feed(self, raw):
        if self.error:
            raise ValueError(self.error)
        try:
            if not isinstance(raw, bytes) or len(self.buffer) + len(raw) > 8192:
                raise ValueError('frame buffer cap')
            self.buffer.extend(raw)
            frames = []
            while self.buffer:
                if self.closed:
                    raise ValueError('data after CLOSE')
                if len(self.buffer) < 56:
                    break
                magic, kind, direction, reserved, size, header, sequence, nonce, crc, tail = HEADER.unpack(self.buffer[:56])
                if magic != MAGIC or reserved or tail or header != 56:
                    raise ValueError('frame header')
                if nonce != self.nonce or direction != self.direction or sequence != self.next:
                    raise ValueError('frame session/direction/sequence')
                if size > 1500 or (not self.opened and (kind != 1 or size)):
                    raise ValueError('frame length/open phase')
                if self.opened and not ((kind == 2 and size >= 20) or (kind == 3 and size == 0)):
                    raise ValueError('frame phase')
                if self.next == (1 << 64) - 1 and kind != 3:
                    raise ValueError('sequence exhaustion')
                if len(self.buffer) < 56 + size:
                    break
                frame = bytes(self.buffer[:56+size])
                if crc32c(frame[:48] + b'\0' * 4 + frame[52:]) != crc:
                    raise ValueError('frame CRC32C')
                if kind == 2 and not ipv4(frame[56:]):
                    raise ValueError('frame IPv4')
                if self.count == 128:
                    raise ValueError('frame count cap')
                del self.buffer[:56+size]
                self.count += 1
                self.opened = True
                self.closed = kind == 3
                if not self.closed:
                    self.next += 1
                frames.append((kind, sequence, frame))
            return frames
        except Exception as error:
            self.error = str(error)
            raise

    def end(self):
        if self.error or self.buffer or not self.closed:
            raise ValueError(self.error or 'frame EOF without exact CLOSE')


def whitespace(frame):
    # Any suffix of this alphabet is still shell whitespace; no TAB or controls.
    return b''.join(b'  \n' if byte & (1 << bit) else b' \n'
                    for byte in frame for bit in range(7, -1, -1))


class Bridge:
    def __init__(self, nonce, relay_port, clock=time.monotonic, socket_factory=socket.socket):
        if not re.fullmatch('[0-9a-f]{48}', nonce) or type(relay_port) is not int or not 1024 <= relay_port <= 65535:
            raise ValueError('link profile')
        self.nonce, self.port, self.clock, self.socket_factory = nonce, relay_port, clock, socket_factory
        self.guest, self.host = Frames(nonce, 1), Frames(nonce, 2)
        self.created = clock()
        self.started = None
        self.socket = None
        self.connected = self.ready = self.write_shutdown = self.transport_complete = False
        self.cancelled = False
        self.error = None
        self.close_error = None
        self.tail = bytearray()
        self.to_relay = bytearray()
        self.relay_queue_since = None
        self.pending = deque()
        self.pending_offset = 0
        self.issued = []
        self.guest_frames = []
        self.guest_result = None
        self.data_bytes = 0
        self.acked_bytes = 0
        self.outstanding_since = None
        self.chunk_ledger = []
        self.ack_ledger = []
        self.trailing_cr_lines = self.trailing_cr_max = 0
        self.observation_bytes = self.observation_lines = 0

    def _close_socket(self):
        sock, self.socket = self.socket, None
        if sock is not None:
            try:
                sock.close()
            except OSError as error:
                self.close_error = self.close_error or str(error)
                self.error = self.error or 'relay close failed'

    def fail(self, error):
        self.error = self.error or str(error)
        self.cancelled = True
        self.transport_complete = False
        self.pending.clear()
        self.pending_offset = 0
        self._close_socket()

    def _ready(self, line):
        expected = rb'DL29_READY_' + self.nonce.encode() + rb' interface=utun[0-9]{1,5} mtu=1500'
        if self.ready or re.fullmatch(expected, line) is None:
            raise ValueError('readiness marker')
        self.ready = True
        self.started = self.clock()
        self.socket = self.socket_factory(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.setblocking(False)
        self.socket.set_inheritable(False)
        code = self.socket.connect_ex(('127.0.0.1', self.port))
        if code not in (0, errno.EINPROGRESS, errno.EWOULDBLOCK, errno.EALREADY):
            raise OSError(code, 'relay connect')
        self.connected = code == 0

    def _result(self, line):
        # This exact ordered grammar is the net_resolver.c serializer, not a
        # host-generated resolver verdict. Failed lines remain in raw capture.
        keys = ('status resolver_ok http_ok frames_out frames_in packets_out packets_in '
                'error errno console_bytes_in partial_line partial_bits daemon_cleanup '
                'probe_pid mdns_pid resolver_addresses selected cleanup_complete phases '
                'http_bytes if_error if_stage default_add_attempted route_error route_stage').split()
        prefix = b'DL29_RESULT_' + self.nonce.encode() + b' '
        if not line.startswith(prefix) or not self.ready or self.guest_result is not None:
            raise ValueError('guest result marker')
        pairs = line[len(prefix):].split(b' ')
        if len(pairs) != len(keys):
            raise ValueError('guest result marker')
        result = {}
        for key, pair in zip(keys, pairs):
            name, sep, value = pair.partition(b'=')
            if not sep or name != key.encode():
                raise ValueError('guest result marker')
            if key in ('status','error'):
                if re.fullmatch(rb'[a-zA-Z0-9_]{1,64}',value) is None:
                    raise ValueError('guest result marker')
                result[key] = value.decode()
            else:
                if re.fullmatch(rb'(?:0|[1-9][0-9]{0,19})',value) is None:
                    raise ValueError('guest result marker')
                result[key] = int(value)
                if result[key] > (1<<64)-1:
                    raise ValueError('guest result numeric bound')
        self.guest_result = result
        if result['if_error'] != 0 or result['if_stage'] != 7:
            raise ValueError('guest interface error/stage')
        if (result['cleanup_complete'] != 1 or result['phases'] != 1 or
                not 1 <= result['http_bytes'] <= 2048):
            raise ValueError('guest cleanup/phases/HTTP bounds')
        if result['console_bytes_in'] > MAX_DATA_BYTES:
            raise ValueError('guest console byte cap')
        if (result['status'] != 'PASS' or result['resolver_ok'] != 1 or
                result['http_ok'] != 1 or result['error'] != 'none' or result['errno'] != 0):
            raise ValueError('guest resolver service failed')
        if (result['daemon_cleanup'] != 1 or
                not 2 <= result['probe_pid'] <= (1<<31)-1 or
                not 2 <= result['mdns_pid'] <= (1<<31)-1 or
                result['probe_pid'] == result['mdns_pid'] or
                not 1 <= result['resolver_addresses'] <= 32 or
                not 1 <= result['selected'] <= (1<<32)-1):
            raise ValueError('guest resolver child/selected-address bounds')
        if (result['console_bytes_in'] != self.data_bytes or self.acked_bytes != self.data_bytes or
                self.pending or self.pending_offset or self.outstanding_since is not None or
                result['partial_line'] != 0 or result['partial_bits'] != 0):
            raise ValueError('guest result credit/partial mismatch')
        if (result['default_add_attempted'] != 1 or result['route_error'] != 0 or
                result['route_stage'] != 5):
            raise ValueError('guest default route cleanup/stage')
        if (not self.host.closed or not self.guest.closed or
                result['frames_out'] != self.guest.count or result['frames_in'] != self.host.count or
                result['packets_out'] != sum(x['type']==2 for x in self.guest_frames) or
                result['packets_in'] != sum(x['type']==2 for x in self.issued)):
            raise ValueError('guest result before exact CLOSE/frame accounting')

    def _line(self, line):
        if line.endswith(b'\r'):
            # ARM console translation may append multiple CR before LF even
            # after the guest tty is raw. Strip line-ending CR only. Interior
            # bytes, raw capture and frame CRC/nonce/sequence stay exact.
            clean = line.rstrip(b'\r')
            self.trailing_cr_lines += 1
            self.trailing_cr_max = max(self.trailing_cr_max, len(line)-len(clean))
            line = clean
        if line.startswith((b'DL29_BEGIN_', b'DL29_STATUS_', b'DL29_RETURN_', b'DL29_END_')):
            # Wrapper diagnostics are independently ordered/qualified from the
            # saved capture. They never authorize link progress or success and
            # may arrive before relay EOF has been observed by tick().
            wrapper = (rb'DL29_(?:BEGIN_' + self.nonce.encode() +
                rb' profile=native-resolver-example-http-v1 backend=native-resolver-example-http-v1|STATUS_' +
                self.nonce.encode() + rb' rc=[01]|RETURN_' + self.nonce.encode() +
                rb'|END_' + self.nonce.encode() + rb')')
            if re.fullmatch(wrapper, line) is None:
                raise ValueError('wrapper diagnostic marker')
        elif line.startswith(b'DL29_READY_'):
            self._ready(line)
        elif line.startswith(b'DL29_TX_'):
            match = re.fullmatch(rb'DL29_TX_' + self.nonce.encode() + rb' ([0-9a-f]{112,3112})', line)
            if not self.ready or not match or len(match.group(1)) % 2:
                raise ValueError('guest frame line')
            raw = bytes.fromhex(match.group(1).decode())
            if len(raw) != 56 + int.from_bytes(raw[12:14], 'big'):
                raise ValueError('one guest frame per line')
            frames = self.guest.feed(raw)
            if len(frames) != 1 or self.guest.buffer:
                raise ValueError('incomplete guest frame line')
            if len(self.to_relay) + len(raw) > 8 * MAX_FRAME:
                raise ValueError('relay write queue cap')
            if not self.to_relay:
                self.relay_queue_since = self.clock()
            self.to_relay.extend(raw)
            self.guest_frames.append({'sequence': frames[0][1], 'type': frames[0][0],
                                      'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()})
        elif line.startswith(b'DL29_ACK_'):
            match = re.fullmatch(rb'DL29_ACK_' + self.nonce.encode() + rb' bytes=([1-9][0-9]{0,5})', line)
            if not match or not self.ready or self.outstanding_since is None:
                raise ValueError('unexpected guest ACK')
            now = self.clock()
            if now - self.outstanding_since > ACK_SECONDS:
                raise TimeoutError('outstanding guest ACK deadline')
            count = int(match[1])
            if not self.acked_bytes < count <= self.data_bytes:
                raise ValueError('guest ACK cumulative credit')
            if len(self.ack_ledger) >= MAX_ACKS:
                raise ValueError('guest ACK ledger cap')
            self.acked_bytes = count
            self.ack_ledger.append({'cumulativebytes': count})
            # Partial ACKs do not reset the outstanding chunk's deadline.
            if count == self.data_bytes:
                self.outstanding_since = None
        elif line.startswith(b'DL29_RESULT_'):
            self._result(line)

        elif line.startswith(b'DL29_OBSERVE_'):
            # Diagnostics remain raw-console evidence, never a success predicate.
            match = re.fullmatch(rb'DL29_OBSERVE_' + self.nonce.encode() +
                rb' kind=([a-z0-9_]{1,64}) total=([0-9]{1,5}) offset=([0-9]{1,5}) hex=([0-9a-f]{0,128})', line)
            if not match or len(match[4]) % 2:
                raise ValueError('guest diagnostic line')
            cap = {b'mdns_child_stderr':65536,b'probe_child_stderr':65536,
                   b'mdns_child_stdout':8192,b'probe_child_stdout':8192}.get(match[1],4096)
            if int(match[2]) > cap or int(match[3]) + len(match[4]) // 2 > int(match[2]):
                raise ValueError('guest diagnostic line')
            self.observation_bytes += len(match[4]) // 2
            self.observation_lines += 1
            if self.observation_bytes > 512*1024 or self.observation_lines > 8192:
                raise ValueError('guest diagnostic aggregate cap')
        elif line.startswith((b'DL29_', b'DL28_', b'DL27_', b'DL26_')):
            raise ValueError('unexpected network experiment marker')

    def feed_console(self, data):
        if self.cancelled:
            return
        try:
            if not isinstance(data, bytes):
                raise ValueError('console bytes')
            self.tail.extend(data)
            while b'\n' in self.tail:
                line, _, remainder = self.tail.partition(b'\n')
                if len(line) > MAX_LINE:
                    raise ValueError('console complete line cap')
                self.tail = bytearray(remainder)
                self._line(bytes(line))
            if len(self.tail) > MAX_LINE:
                raise ValueError('console tail cap')
        except Exception as error:
            self.fail(error)

    def tick(self):
        if self.cancelled:
            return None
        try:
            now = self.clock()
            if not self.ready:
                if now - self.created > 105:
                    raise TimeoutError('guest readiness deadline')
                return None
            if self.transport_complete:
                return None
            if now - self.started > 75:
                raise TimeoutError('whole link deadline')
            if self.outstanding_since is not None and now - self.outstanding_since > ACK_SECONDS:
                raise TimeoutError('outstanding guest ACK deadline')
            if not self.connected:
                error = self.socket.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
                if error not in (0, errno.EINPROGRESS, errno.EWOULDBLOCK, errno.EALREADY):
                    raise OSError(error, 'relay connection')
                if not error:
                    try:
                        peer = self.socket.getpeername()
                    except OSError as pending:
                        if pending.errno != errno.ENOTCONN:
                            raise
                    else:
                        if peer != ('127.0.0.1', self.port):
                            raise ValueError('relay peer mismatch')
                        self.connected = True
                if not self.connected and now - self.started > 5:
                    raise TimeoutError('relay connect deadline')
                if not self.connected:
                    return None
            if self.to_relay:
                if now - self.relay_queue_since > 5:
                    raise TimeoutError('continuous relay queue deadline')
                try:
                    n = self.socket.send(self.to_relay)
                except (BlockingIOError, InterruptedError):
                    n = None
                if n == 0:
                    raise OSError('zero relay write')
                if n:
                    del self.to_relay[:n]
                    if not self.to_relay:
                        self.relay_queue_since = None
            for _ in range(4):
                try:
                    data = self.socket.recv(4096)
                except (BlockingIOError, InterruptedError):
                    break
                if not data:
                    self.host.end()
                    if (not self.guest.closed or self.to_relay or self.pending or self.pending_offset or
                            not self.write_shutdown or self.acked_bytes != self.data_bytes or
                            self.outstanding_since is not None):
                        raise ValueError('premature relay EOF')
                    self.transport_complete = True
                    self._close_socket()
                    break
                for kind, sequence, frame in self.host.feed(data):
                    if len(self.pending) == MAX_QUEUE:
                        raise ValueError('guest data queue cap')
                    encoded = whitespace(frame)
                    self.pending.append((encoded, {'sequence': sequence, 'type': kind,
                        'frame_bytes': len(frame), 'frame_sha256': hashlib.sha256(frame).hexdigest(),
                        'wire_bytes': len(encoded), 'wire_sha256': hashlib.sha256(encoded).hexdigest()}))
            if (self.guest.closed and self.host.closed and not self.to_relay and not self.pending and
                    not self.pending_offset and self.acked_bytes == self.data_bytes and
                    self.outstanding_since is None and not self.write_shutdown):
                self.socket.shutdown(socket.SHUT_WR)
                self.write_shutdown = True
            if self.pending and self.acked_bytes == self.data_bytes and self.outstanding_since is None:
                return self.pending[0][0][self.pending_offset:self.pending_offset + MAX_CHUNK_BYTES]
            return None
        except Exception as error:
            self.fail(error)
            return None

    def authorize(self, data):
        # Called by the one recorded console writer, immediately before its write.
        if (self.cancelled or not self.ready or not self.pending or
                self.acked_bytes != self.data_bytes or self.outstanding_since is not None or
                type(data) is not bytes or
                data != self.pending[0][0][self.pending_offset:self.pending_offset + MAX_CHUNK_BYTES]):
            raise ValueError('unissued network data')
        if (not data or len(data) > MAX_CHUNK_BYTES or any(byte not in (32, 10) for byte in data) or
                self.data_bytes + len(data) > MAX_DATA_BYTES or len(self.chunk_ledger) >= MAX_CHUNKS):
            raise ValueError('network data alphabet/cap')
        self.data_bytes += len(data)
        self.outstanding_since = self.clock()
        self.chunk_ledger.append({'length': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                                  'cumulativebytes': self.data_bytes})
        self.pending_offset += len(data)
        if self.pending_offset == len(self.pending[0][0]):
            _, receipt = self.pending.popleft()
            self.issued.append(receipt)
            self.pending_offset = 0

    def cancel(self):
        self.cancelled = True
        self._close_socket()

    def report(self):
        credit_complete = (self.acked_bytes == self.data_bytes and self.outstanding_since is None and
                           not self.pending and not self.pending_offset)
        return {'version': 4, 'nonce': self.nonce, 'ready': self.ready,
                'transport_complete': self.transport_complete and credit_complete, 'error': self.error,
                'close_error': self.close_error,
                'guest_result': self.guest_result, 'host_frames_issued': self.issued,
                'guest_frames': self.guest_frames, 'data_bytes': self.data_bytes,
                'diagnostic_bytes': self.observation_bytes,'diagnostic_lines':self.observation_lines,
                'acked_bytes': self.acked_bytes, 'credit_complete': credit_complete,
                'chunk_ledger': self.chunk_ledger, 'ack_ledger': self.ack_ledger,
                'max_chunk_bytes': MAX_CHUNK_BYTES, 'max_chunks': MAX_CHUNKS, 'max_acks': MAX_ACKS,
                'ack_deadline_seconds': ACK_SECONDS,
                'pending_frames': len(self.pending), 'pending_offset': self.pending_offset,
                'trailing_cr_lines': self.trailing_cr_lines,
                'trailing_cr_max': self.trailing_cr_max,
                'ingress_alphabet': 'SPACE_LF_ONLY_ALL_SUFFIXES_WHITESPACE',
                'public_profile': 'native-resolver-example-http-v1',
                'network_resolver_enabled': True, 'network_default_enabled': False, 'backend_profile': 'native-resolver-example-http-v1', 'evidence': 'guest_record_requires_actual_backend_and_recovery_binding'}
