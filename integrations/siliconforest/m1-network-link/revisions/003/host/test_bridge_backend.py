"""Actual production Bridge + Ccodec + LinuxQEMU; synthetic guest IP only."""
from pathlib import Path
import importlib.util,json,sys,time,tempfile,subprocess
ROOT=Path(__file__).resolve().parents[6]
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE));from line_bridge import Bridge
sys.path.insert(0,str(ROOT/'integrations/puredarwin/m1-network-transport/host'))
from test_backend import Trial,roundtrip

def decode_space(raw):
 bits=[]
 for line in raw.splitlines():
  assert line in (b' ',b'  ')
  bits.append(len(line)-1)
 assert len(bits)%8==0
 return bytes(sum(bits[i+j]<<(7-j) for j in range(8)) for i in range(0,len(bits),8))
class CloseIntent:
 def __init__(self,trial):self.trial=trial
 def shutdown(self,_how):self.trial.close_intent=True
 def recv(self,_n):
  end=time.monotonic()+4
  while not self.trial.bridge.transport_complete:
   assert time.monotonic()<end
   self.trial.pump();time.sleep(.002)
  return b''
 def close(self):self.trial.bridge.cancel()
class BridgeTrial(Trial):
 def connect(self):
  self.bridge=Bridge(self.nonce,self.ready['listen_port']);self.close_intent=False;self.sock=CloseIntent(self)
  self.bridge.feed_console(f'DL26_LINK_READY_{self.nonce} interface=utun0 mtu=1500\r\r\n'.encode())
 def send(self,kind,data=b''):
  frame=self.codec.encode(kind,data)
  line=f'DL26_TX_{self.nonce} {frame.hex()}\r\r\n'.encode()
  for i in range(0,len(line),17):self.bridge.feed_console(line[i:i+17])
  self.pump()
 def pump(self):
  wire=self.bridge.tick();assert not self.bridge.error,self.bridge.error
  if wire:
   self.bridge.authorize(wire)
   self.codec.feed(decode_space(wire),lambda *args:self.pending.append(args))
 def receive(self):
  end=time.monotonic()+4
  while not self.pending:
   assert time.monotonic()<end,'bridge receive deadline'
   self.pump();time.sleep(.002)
  return self.pending.popleft()
if __name__=='__main__':
 out=ROOT/'runs/m1-desktop-010/net-link003-bridge-backend-host-test';out.mkdir(mode=0o700,exist_ok=False)
 codec=ROOT/'integrations/puredarwin/m1-network-transport'
 library=out/'libdlnet-host.so'
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O2','-fPIC','-shared',str(codec/'codec/transport.c'),str(codec/'host/codec_host.c'),'-o',str(library)],check=True)
 t=BridgeTrial(library,out,seconds=15,accept_seconds=8)
 try:
  result=roundtrip(t);report={'status':'PASS_BRIDGE_C_CODEC_QEMU_HOST_ONLY_SYNTHETIC_GUEST','bridge':t.bridge.report(),'backend':result,'not_claimed':['Darwin utun daemon runtime','M1/M5','External networking']}
  assert t.bridge.transport_complete and result['complete'] and result['echoes']=={'tcp':True,'udp':True}
  (out/'qualification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'status':report['status'],'data_bytes':t.bridge.data_bytes,'frames_to_guest':len(t.bridge.issued),'QEMU_returncode':result['cleanup']['qemu_returncode']}))
 finally:t.cleanup()
