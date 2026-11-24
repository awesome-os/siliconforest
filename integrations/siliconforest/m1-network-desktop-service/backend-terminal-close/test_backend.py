"""Local refusal/queue tests; no QEMU/target/socket is constructed."""
from pathlib import Path
import subprocess,sys,tempfile,unittest
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;sys.path.insert(0,str(HERE))
from backend import Backend,Queue,PROFILE,CODEC_SHA256
class Tests(unittest.TestCase):
 def test_legacy_and_widened_lifetimes_refused_before_paths(self):
  for seconds,accept in((120,240),(180,300),(179,240),(181,240),(180,239),(180,241)):
   with self.assertRaisesRegex(ValueError,'lifetime'):Backend('/missing',CODEC_SHA256,'a'*48,'/missing',seconds=seconds,accept_seconds=accept)
 def test_nonce_refused_before_paths(self):
  for nonce in('a'*47,'A'*48,'g'*48,'a'*49):
   with self.assertRaisesRegex(ValueError,'lifetime'):Backend('/missing',CODEC_SHA256,nonce,'/missing')
 def test_digest_refuses_before_records(self):
  with self.assertRaisesRegex(ValueError,'digest'):Backend('/missing','0'*64,'a'*48,'/missing')
 def cli(self,**overrides):
  values={'profile':PROFILE,'nonce':'a'*48,'codec-library':'/missing','codec-sha256':CODEC_SHA256,'records':'/missing','listen-port':'0','seconds':'180','accept-seconds':'240'};values.update(overrides)
  args=[sys.executable,'-B','-I',str(HERE/'backend.py')]
  for k,v in values.items():args.extend(('--'+k,v))
  return subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=3)
 def test_oldprofile_refused(self):
  for p in('native-resolver-example-http-v1','native-resolver-example-http-drain-v2','example-http-v1'):
   r=self.cli(profile=p);self.assertEqual(r.returncode,2);self.assertIn(b'invalid choice',r.stderr)
 def test_relative_paths_and_ports_refused(self):
  for kwargs in({'records':'relative'},{'codec-library':'relative'},{'listen-port':'-1'},{'listen-port':'65536'}):
   r=self.cli(**kwargs);self.assertEqual(r.returncode,2)
 def test_queue_bound_and_shortwrites_preserve_bytes(self):
  q=Queue()
  for i in range(8):q.append(bytes([i])*3)
  with self.assertRaises(RuntimeError):q.append(b'x')
  class S:
   data=bytearray()
   def send(self,b):self.data.extend(b[:1]);return 1
  sock=S()
  while q:q.write(sock)
  self.assertEqual(bytes(sock.data),b''.join(bytes([i])*3 for i in range(8)));self.assertEqual(q.written,24);self.assertIsNone(q.since)
if __name__=='__main__':unittest.main()
