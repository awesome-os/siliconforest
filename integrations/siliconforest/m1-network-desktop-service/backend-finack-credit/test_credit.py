"""Synthetic credit/backpressure tests; no target RESULT or networking."""
from pathlib import Path
import hashlib
import socket
import struct
import sys
import unittest
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from credit_bridge import Bridge,Frames,HEADER,crc32c,whitespace,MAX_DATA_BYTES,MAX_CHUNKS
NONCE = '1234567890abcdef' * 3


def frame(kind, seq, payload=b'', direction=1, nonce=NONCE):
    raw = HEADER.pack(b'DLNET001', kind, direction, 0, len(payload), 56,
                      seq, bytes.fromhex(nonce), 0, 0) + payload
    return raw[:48] + struct.pack('!I', crc32c(raw)) + raw[52:]


def packet():
    raw = bytearray.fromhex('4500001400010000401100000a00020f0a000202')
    total = sum(int.from_bytes(raw[i:i+2], 'big') for i in range(0, 20, 2))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    raw[10:12] = ((~total)&65535).to_bytes(2, 'big')
    return bytes(raw)


class FakeSocket:
    def __init__(self):
        self.closed = self.shut = False
        self.input = []
        self.written = bytearray()
    def setblocking(self, value): assert value is False
    def set_inheritable(self, value): assert value is False
    def connect_ex(self, peer): self.peer = peer; return 0
    def send(self, data): self.written.extend(data); return len(data)
    def recv(self, _n, flags=0):
        if self.input: return self.input[0][:_n] if flags else self.input.pop(0)
        raise BlockingIOError()
    def shutdown(self, how):
        assert how == socket.SHUT_WR
        self.shut = True
    def close(self): self.closed = True


class FakeTTY:
    """Bounded1024-byte tty input; fake consumer ACKs only parsed reads."""
    def __init__(self, bridge, capacity=1024):
        self.bridge, self.capacity = bridge, capacity
        self.queued = bytearray()
        self.high_water = self.total = self.bit_count = self.value = 0
        self.line = bytearray()
        self.frames = Frames(NONCE, 2)
        self.received_frames = []
    def write(self, data):
        if len(self.queued) + len(data) > self.capacity:
            raise OverflowError('simulated tty1024 overflow')
        self.queued.extend(data)
        self.high_water = max(self.high_water, len(self.queued))
    def read_ack(self, maximum=192, split_marker=False):
        data = bytes(self.queued[:maximum]); del self.queued[:len(data)]
        assert data
        decoded = bytearray()
        for byte in data:
            if byte == 32:
                self.line.append(byte)
                assert len(self.line) <= 2
            elif byte == 10:
                assert len(self.line) in (1,2)
                self.value = (self.value << 1) | (len(self.line)-1)
                self.bit_count += 1; self.line.clear()
                if self.bit_count == 8:
                    decoded.append(self.value); self.bit_count = self.value = 0
            else:
                raise ValueError('fake guest alphabet')
        self.received_frames.extend(self.frames.feed(bytes(decoded)))
        self.total += len(data)
        marker = f'DL30_ACK_{NONCE} bytes={self.total}\r\r\n'.encode()
        if split_marker:
            for at in range(0,len(marker),3): self.bridge.feed_console(marker[at:at+3])
        else: self.bridge.feed_console(marker)
        return data

class Tests(unittest.TestCase):
 def bridge(self):
  sock,clock=FakeSocket(),[0.];b=Bridge(NONCE,20000,clock=lambda:clock[0],socket_factory=lambda *_:sock)
  return b,sock,clock
 def ready(self,b):b.feed_console(f'DL30_READY_{NONCE} interface=utun0 mtu=1500\n'.encode())
 def tx(self,b,raw):b.feed_console(f'DL30_TX_{NONCE} {raw.hex()}\n'.encode())
 def opened(self):
  b,s,t=self.bridge();self.ready(b);self.tx(b,frame(1,1));s.input.append(frame(1,1,direction=2));return b,s,t
 def drain(self,b,tty,t):
  peak=0
  for _ in range(10000):
   wire=b.tick();self.assertIsNone(b.error);peak=max(peak,len(b.pending))
   if wire:b.authorize(wire);tty.write(wire)
   if tty.queued:tty.read_ack(split_marker=True)
   t[0]+=.001
   if not b.pending and not b.relay_buffer and b.outstanding_since is None and not s_input(tty,b):break
  else:self.fail('bounded drain did not finish')
  return peak
 def test_backpressure_coalesced_frames_never_exceed8_or_drop(self):
  b,s,t=self.opened();tty=FakeTTY(b)
  # Twenty-one codec frames arrive together, beyond the queue8. Raw bytes
  # stay in a bounded buffer until credit opens a slot; all frames survive.
  s.input=[frame(1,1,direction=2)+b''.join(frame(2,i,packet(),direction=2)for i in range(2,22))]
  peak=self.drain(b,tty,t)
  self.assertEqual(peak,8);self.assertEqual(len(tty.received_frames),21)
  self.assertEqual(b.host.count,21);self.assertEqual(b.data_bytes,b.acked_bytes);self.assertLessEqual(len(b.relay_buffer),8192)
 def test_192_fixed3s_partial_ACK_no_refresh(self):
  b,s,t=self.opened();wire=b.tick();self.assertLessEqual(len(wire),192);b.authorize(wire)
  t[0]=2.9;b.feed_console(f'DL30_ACK_{NONCE} bytes=17\n'.encode());self.assertIsNone(b.error)
  t[0]=3.001;b.tick();self.assertIn('ACK deadline',b.error)
 def test_new_exact_byte_chunk_caps(self):
  self.assertEqual((MAX_DATA_BYTES,MAX_CHUNKS),(262144,2048))
  b,s,t=self.opened();wire=b.tick();b.data_bytes=b.acked_bytes=MAX_DATA_BYTES
  with self.assertRaises(ValueError):b.authorize(wire)
 def test_180_whole_and_readiness_remain_finite(self):
  b,s,t=self.opened();t[0]=180.001;b.tick();self.assertIn('whole link',b.error)
  b,s,t=self.bridge();t[0]=105.001;b.tick();self.assertIn('readiness',b.error)
 def test_no_target_RESULT_admission_or_oldnamespace(self):
  for line in (f'DL30_RESULT_{NONCE} status=PASS',f'DL29_READY_{NONCE} interface=utun0 mtu=1500'):
   b,s,t=self.bridge();b.feed_console((line+'\n').encode());self.assertIsNotNone(b.error)
 def test_both_CLOSE_fullcredit_halfclose_EOF_still_required(self):
  b,s,t=self.opened();tty=FakeTTY(b);self.drain(b,tty,t)
  self.tx(b,frame(3,2));s.input.append(frame(3,22 if b.host.count>1 else 2,direction=2));self.drain(b,tty,t)
  b.tick();self.assertTrue(s.shut);self.assertEqual(b.data_bytes,b.acked_bytes)
  s.input.append(b'');b.tick();self.assertTrue(b.report()['transport_complete']);self.assertIsNone(b.guest_result)
 def test_fragmented_raw_frames_and_trailing_bytes_refused(self):
  b,s,t=self.opened();s.input=[frame(1,1,direction=2)[:30],frame(1,1,direction=2)[30:]];tty=FakeTTY(b);self.drain(b,tty,t);self.assertEqual(len(tty.received_frames),1)
  b,s,t=self.opened();s.input=[frame(1,1,direction=2)+frame(3,2,direction=2)+b'x'];b.tick();self.assertIsNotNone(b.error)
 def test_failure_snapshot_survives_clear(self):
  b,s,t=self.opened();b.authorize(b.tick());b.feed_console(f'DL30_ACK_{NONCE} bytes=193\n'.encode())
  self.assertIsNotNone(b.error);self.assertEqual(b.close_timeline['failure_snapshot']['issued_bytes'],192)
 def test_credit_partial_stream_bad_crc_wrong_nonce(self):
  for bad in (frame(1,1,nonce='b'*48,direction=2),frame(1,1,direction=2)[:-1]+b'x'):
   b,s,t=self.opened();s.input=[bad];b.tick();self.assertIsNotNone(b.error)

def s_input(tty,b):
 return bool(getattr(b.socket,'input',[]))
if __name__=='__main__':unittest.main()
