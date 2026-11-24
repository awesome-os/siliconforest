from pathlib import Path
import ctypes,hashlib,importlib.util,json,re,socket,struct,subprocess,tempfile,time,unittest,sys
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
from line_bridge import Frames,Bridge,HEADER,crc32c,whitespace,ipv4
NONCE='1234567890abcdef'*3

def frame(kind,seq,body=b'',direction=1,nonce=NONCE):
 raw=HEADER.pack(b'DLNET001',kind,direction,0,len(body),56,seq,bytes.fromhex(nonce),0,0)+body
 return raw[:48]+struct.pack('!I',crc32c(raw))+raw[52:]
def packet():
 raw=bytearray.fromhex('4500001400010000401100000a00020f0a000202')
 n=sum(int.from_bytes(raw[i:i+2],'big') for i in range(0,20,2))
 while n>>16:n=(n&65535)+(n>>16)
 raw[10:12]=((~n)&65535).to_bytes(2,'big');return bytes(raw)
def decode_space(raw):
 if any(x not in (32,10) for x in raw) or not raw.endswith(b'\n'):raise ValueError('alphabet')
 bits=[]
 for line in raw.splitlines():
  if line not in (b' ',b'  '):raise ValueError('bit length')
  bits.append(len(line)-1)
 if len(bits)%8:raise ValueError('partial byte')
 return bytes(sum(bits[i+k]<<(7-k) for k in range(8)) for i in range(0,len(bits),8))
class FakeSocket:
 def __init__(self,*args):self.closed=False;self.input=[];self.written=bytearray();self.shut=False
 def setblocking(self,x):assert x is False
 def set_inheritable(self,x):assert x is False
 def connect_ex(self,peer):self.peer=peer;return 0
 def send(self,data):self.written.extend(data);return len(data)
 def recv(self,n):
  if self.input:return self.input.pop(0)
  raise BlockingIOError()
 def shutdown(self,how):self.shut=True
 def close(self):self.closed=True
class Tests(unittest.TestCase):
 def test_crc(self):self.assertEqual(crc32c(b'123456789'),0xe3069283)
 def test_actual_ARM_console_trailing_CR(self):
  for ending in (b'\n',b'\r\n',b'\r\r\n',b'\r\r\r\n'):
   b,s,t=self.bridge()
   b.feed_console(f'DL26_LINK_READY_{NONCE} interface=utun0 mtu=1500'.encode()+ending)
   b.feed_console(self.txline(frame(1,1)).rstrip(b'\n')+ending)
   self.assertIsNone(b.error);self.assertTrue(b.ready)
   b.tick();self.assertEqual(bytes(s.written),frame(1,1))
   self.assertEqual(b.trailing_cr_max,len(ending)-1)
 def test_interior_CR_cannot_hide_mutated_marker_or_frame(self):
  b,s,t=self.bridge()
  b.feed_console(f'DL26_LINK_READY_{NONCE} interface=utun\r0 mtu=1500\r\r\n'.encode())
  self.assertIsNotNone(b.error);self.assertFalse(b.ready)
  b,s,t=self.bridge();self.ready(b)
  raw=self.txline(frame(1,1));where=raw.index(b'444c')+4
  b.feed_console(raw[:where]+b'\r'+raw[where:])
  self.assertIsNotNone(b.error);self.assertFalse(s.written)
 def test_valid_frame_fragmentation(self):
  raw=frame(1,1)+frame(2,2,packet())+frame(3,3)
  for stride in (1,7,55,56,120,len(raw)):
   s=Frames(NONCE,1);got=[]
   for i in range(0,len(raw),stride):got.extend(s.feed(raw[i:i+stride]))
   s.end();self.assertEqual([x[0] for x in got],[1,2,3])
 def test_frame_negatives_sticky(self):
  valid=frame(1,1)
  cases=[valid[:i]+bytes([valid[i]^1])+valid[i+1:] for i in range(len(valid))]
  cases += [frame(1,2),frame(2,1,packet()),frame(1,1,direction=2)]
  for raw in cases:
   s=Frames(NONCE,1)
   with self.assertRaises(ValueError):s.feed(raw)
   with self.assertRaises(ValueError):s.feed(valid)
 def test_eof_and_post_close(self):
  for raw in (b'',frame(1,1),frame(1,1)+frame(2,2,packet())[:60]):
   s=Frames(NONCE,1);s.feed(raw)
   with self.assertRaises(ValueError):s.end()
  s=Frames(NONCE,1);s.feed(frame(1,1)+frame(3,2))
  with self.assertRaises(ValueError):s.feed(b'x')
 def test_space_roundtrip_and_every_suffix(self):
  payload=bytes(range(256));encoded=whitespace(payload);self.assertEqual(decode_space(encoded),payload)
  self.assertEqual(set(encoded),{32,10})
  for offset in range(len(encoded)):self.assertTrue(all(c in (32,10) for c in encoded[offset:]))
  for offset in (0,1,2,19,20,300,len(encoded)-1):
   p=subprocess.run(['/bin/sh'],input=encoded[offset:],capture_output=True,timeout=2)
   self.assertEqual((p.returncode,p.stdout,p.stderr),(0,b'',b''))
 def bridge(self):
  sock=FakeSocket();clock=[0.0];b=Bridge(NONCE,20000,clock=lambda:clock[0],socket_factory=lambda *x:sock)
  return b,sock,clock
 def ready(self,b):b.feed_console(f'kernel unrelated\nDL26_LINK_READY_{NONCE} interface=utun0 mtu=1500\n'.encode())
 def txline(self,f):return f'DL26_TX_{NONCE} {f.hex()}\n'.encode()
 def test_complete_handshake_and_result(self):
  b,s,t=self.bridge();self.ready(b);b.feed_console(self.txline(frame(1,1)));s.input.append(frame(1,1,direction=2));wire=b.tick();self.assertEqual(decode_space(wire),frame(1,1,direction=2));b.authorize(wire)
  b.feed_console(self.txline(frame(2,2,packet())));s.input.append(frame(2,2,packet(),direction=2));wire=b.tick();b.authorize(wire)
  b.feed_console(self.txline(frame(3,3)));s.input.append(frame(3,3,direction=2));wire=b.tick();b.authorize(wire);b.tick();self.assertTrue(s.shut);s.input.append(b'');b.tick();self.assertTrue(b.transport_complete)
  b.feed_console(f'DL26_LINK_RESULT_{NONCE} status=PASS tcp_echo=1 udp_echo=1 frames_out=3 frames_in=3 packets_out=1 packets_in=1 error=none errno=0\n'.encode());self.assertIsNone(b.error);self.assertEqual(b.guest_result['status'],'PASS');self.assertEqual(bytes(s.written),frame(1,1)+frame(2,2,packet())+frame(3,3))
 def test_no_send_before_ready_or_issue(self):
  b,s,t=self.bridge()
  for raw in (b'   \n',b'/bin/sh\n',b''):
   with self.assertRaises(ValueError):b.authorize(raw)
  self.assertIsNone(b.tick());self.assertFalse(s.written)
 def test_guest_error_keeps_no_suffix(self):
  b,s,t=self.bridge();self.ready(b);b.feed_console(self.txline(frame(2,1,packet())));self.assertIsNotNone(b.error);self.assertIsNone(b.tick());self.assertFalse(b.pending)
 def test_ready_and_session_mutations(self):
  for line in [f'DL26_LINK_READY_{"a"*48} interface=utun0 mtu=1500\n',f'DL26_LINK_READY_{NONCE} interface=eth0 mtu=1500\n',f'DL26_LINK_READY_{NONCE} interface=utun0 mtu=1499\n']:
   b,s,t=self.bridge();b.feed_console(line.encode());self.assertIsNotNone(b.error)
  b,s,t=self.bridge();self.ready(b);self.ready(b);self.assertIsNotNone(b.error)
 def test_H2_complete_line_and_tail_caps(self):
  b,s,t=self.bridge();b.feed_console(b'X'*4097+b'\n');self.assertIsNotNone(b.error)
  b,s,t=self.bridge();b.feed_console(b'X'*4097);self.assertIsNotNone(b.error)
 def test_h1_error_is_sticky_no_more_writes(self):
  b,s,t=self.bridge();self.ready(b);s.input.append(frame(1,2,direction=2));self.assertIsNone(b.tick());self.assertIsNotNone(b.error);b.feed_console(b'raw diagnostic tail\n');self.assertIsNone(b.tick());self.assertFalse(b.pending)
 def test_deadlines(self):
  b,s,t=self.bridge();t[0]=106;self.assertIsNone(b.tick());self.assertIn('readiness',b.error)
  b,s,t=self.bridge();self.ready(b);t[0]=61;self.assertIsNone(b.tick());self.assertIn('whole link',b.error)
 def test_guest_failed_result(self):
  b,s,t=self.bridge();self.ready(b);b.feed_console(f'DL26_LINK_RESULT_{NONCE} status=FAILED tcp_echo=0 udp_echo=0 frames_out=1 frames_in=0 packets_out=0 packets_in=0 error=deadline errno=0\n'.encode());self.assertIsNotNone(b.error)
 def test_real_loopback_partial_stream(self):
  server=socket.socket();server.bind(('127.0.0.1',0));server.listen(1);server.settimeout(2)
  b=Bridge(NONCE,server.getsockname()[1]);self.ready(b);peer,_=server.accept();peer.settimeout(2)
  try:
   b.feed_console(self.txline(frame(1,1)));b.tick();self.assertEqual(peer.recv(4096),frame(1,1))
   raw=frame(1,1,direction=2)
   for x in raw[:-1]:peer.sendall(bytes([x]));self.assertIsNone(b.tick())
   peer.sendall(raw[-1:]);wire=None
   for _ in range(100):
    wire=b.tick()
    if wire:break
    time.sleep(.002)
   self.assertEqual(decode_space(wire),raw);b.authorize(wire)
  finally:b.cancel();peer.close();server.close()
if __name__=='__main__':unittest.main(verbosity=2)
