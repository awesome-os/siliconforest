"""Actual paced Linux/slirp two-flow fixture. No Darwin/PID/API success claim."""
from collections import deque
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import time
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[3]
sys.path.insert(0,str(HERE))
from bindings import Codec
from model import Policy,PROFILE,GUEST,DNS,QUESTION
from frozen import parse_packet
from packet_fixture import udp,tcp
# Bind the actual production helper, not the fixture-only predecessor.
import types
HELPER=HERE.parent/'session/host/line_bridge.py'
HELPER_SHA='16dd2d684365c9d88e7e6aeb4b4dc6ec941078cc0c26b7c7e4ce482246a35bce'
helper_bytes=HELPER.read_bytes()
if hashlib.sha256(helper_bytes).hexdigest()!=HELPER_SHA:raise RuntimeError('actual helper source mismatch')
helper_module=types.ModuleType('actual_desktop_line_bridge');helper_module.__file__=str(HELPER)
exec(compile(helper_bytes,str(HELPER),'exec'),helper_module.__dict__)
Bridge=helper_module.Bridge
class Credit:
 def __init__(self,library,nonce,port):
  self.nonce=nonce;self.bridge=Bridge(nonce,port)
  self.bridge.feed_console(f'DL30_BEGIN_{nonce} profile={PROFILE} backend={PROFILE}\n'.encode())
  self.bridge.feed_console(f'DL30_READY_{nonce} interface=utun0 mtu=1500\n'.encode())
  self.codec=Codec(library,nonce,1);self.decoded=bytearray();self.queued=bytearray();self.whitespace=bytearray()
  self.bit=self.bits=self.line=self.consumed=0;self.due=None;self.timeout=2
  self.peak_pending=self.peak_raw=0;self.latencies=[];self.issued_at=None
 def send(self,raw):
  line=f'DL30_TX_{self.nonce} {raw.hex()}\n'.encode()
  for i in range(0,len(line),17):self.bridge.feed_console(line[i:i+17])
  self.pump()
 def pump(self):
  chunk=self.bridge.tick()
  self.peak_pending=max(self.peak_pending,len(self.bridge.pending));self.peak_raw=max(self.peak_raw,len(self.bridge.host.buffer))
  if self.bridge.error:raise RuntimeError(self.bridge.error)
  if chunk:
   assert not self.queued and len(chunk)<=192;self.bridge.authorize(chunk);self.queued.extend(chunk);self.whitespace.extend(chunk)
   self.issued_at=time.monotonic();self.due=self.issued_at+.1
  if self.queued and time.monotonic()>=self.due:
   raw=bytes(self.queued);self.queued.clear();decoded=bytearray()
   for b in raw:
    if b==32:self.line+=1;assert self.line<=2
    else:
     assert b==10 and self.line in(1,2);self.bit=(self.bit<<1)|(self.line-1);self.bits+=1;self.line=0
     if self.bits==8:decoded.append(self.bit);self.bit=self.bits=0
   self.codec.feed(bytes(decoded),lambda *_:None);self.decoded.extend(decoded);self.consumed+=len(raw)
   self.latencies.append(time.monotonic()-self.issued_at)
   self.bridge.feed_console(f'DL30_ACK_{self.nonce} bytes={self.consumed}\n'.encode())
   if self.bridge.error:raise RuntimeError(self.bridge.error)
 def recv(self,n):
  end=time.monotonic()+self.timeout
  while not self.decoded:
   if self.bridge.transport_complete:return b''
   if time.monotonic()>=end:raise TimeoutError('synthetic serial read wait')
   self.pump();time.sleep(.002)
  data=bytes(self.decoded[:n]);del self.decoded[:len(data)];return data
 def close(self):self.bridge.cancel();self.codec.close()
 def report(self):
  return {'bridge':self.bridge.report(),'max_pending_frames':self.peak_pending,'max_relay_raw_buffer':self.peak_raw,
   'whitespace_bytes':len(self.whitespace),'partial_line':self.line,'partial_bits':self.bits,
   'ACK_latency_max':max(self.latencies)if self.latencies else None,'synthetic_target_ACKs':True}
def run(library,output,quiet_seconds=31.5):
 output.mkdir(mode=0o700);records=output/'backend';nonce=os.urandom(24).hex();proc=codec=serial=None
 result={'status':'FAIL_ACTUAL_LINUX_TWO_FLOW_PACKET_FIXTURE','nonce':nonce,'actual_helper_SHA256':HELPER_SHA};started=time.monotonic();timings=[]
 log=open(output/'process.log','wb')
 try:
  proc=subprocess.Popen([sys.executable,'-B','-I',str(HERE/'backend.py'),'--profile',PROFILE,'--nonce',nonce,
   '--codec-library',str(library),'--codec-sha256',hashlib.sha256(library.read_bytes()).hexdigest(),
   '--records',str(records),'--listen-port','0','--seconds','180','--accept-seconds','240'],stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
  until=time.monotonic()+5;ready=None
  while time.monotonic()<until and proc.poll()is None:
   try:ready=json.loads((records/'ready.json').read_text());break
   except(FileNotFoundError,json.JSONDecodeError):time.sleep(.01)
  assert ready is not None
  policy=Policy(nonce);policy.open();codec=Codec(library,nonce,1);pending=deque();serial=Credit(library,nonce,ready['listen_port']);whole=time.monotonic()+175
  def send(kind,data=b''):
   if time.monotonic()>whole:raise TimeoutError('fixture whole175')
   if kind==2:policy.outgoing(data)
   serial.send(codec.encode(kind,data))
  def receive():
   while not pending:
    if time.monotonic()>whole:raise TimeoutError('fixture whole175')
    data=serial.recv(4096)
    if not data:raise RuntimeError('relay EOF before codec CLOSE')
    codec.feed(data,lambda *x:pending.append(x))
   kind,seq,data=pending.popleft()
   if kind==2:policy.incoming(data)
   return kind,seq,data
  send(1);assert receive()[:2]==(1,1)
  ident=1+int.from_bytes(os.urandom(2),'big')%65535
  question=struct.pack('!6H',ident,0x100,1,0,0,0)+QUESTION
  send(2,udp(GUEST,DNS,43001,53,question))
  while not policy.transactions[ident]['addresses']:receive()
  addresses=policy.transactions[ident]['addresses'];flows={}
  for slot,port,address,isn in ((2,43003,addresses[-1],30000),(1,43002,addresses[0],10000)):
   key=(port,address,80);flows[key]={'slot':slot,'ISN':isn,'sent_request':False,'FIN':False}
   send(2,tcp(GUEST,address,port,80,isn,0,0xc2));timings.append({'event':'SYN','slot':slot,'elapsed_seconds':time.monotonic()-started})
  def process(data):
   v=parse_packet(data)
   if v[0]!=6:return
   key=(v[4],v[1],v[3]);state=flows[key];flow=policy.flows[key];body,payload=v[5:]
   if body[13]&2:
    send(2,tcp(GUEST,key[1],key[0],80,state['ISN']+1,flow.server_isn+1,0x10))
    if not state['sent_request']:
     state['sent_request']=True;send(2,tcp(GUEST,key[1],key[0],80,state['ISN']+1,flow.server_isn+1,0x18,policy.request))
   elif payload or body[13]&1:
    ack=flow.server_isn+1+flow.contiguous+(1 if flow.fin_offset==flow.contiguous else 0)
    sequence=state['ISN']+1+len(policy.request)+(1 if state['FIN']else 0)
    send(2,tcp(GUEST,key[1],key[0],80,sequence,ack,0x10))
    if flow.complete()and not state['FIN']:
     state['FIN']=True;timings.append({'event':'HTTP200_EOF_client_FIN','slot':state['slot'],'elapsed_seconds':time.monotonic()-started});send(2,tcp(GUEST,key[1],key[0],80,sequence,ack,0x11))
  while not policy.complete():
   kind,seq,data=receive();assert kind==2;process(data)
  # Preserve/drop no late TCP retransmissions. Continue pumping the same
  # admitted tuples, then prove a real >30s quiet desktop-compatible interval.
  quiet_since=time.monotonic()
  while time.monotonic()-quiet_since<quiet_seconds:
   try:
    kind,seq,data=receive();assert kind==2;process(data);quiet_since=time.monotonic()
   except TimeoutError as error:
    if str(error)!='synthetic serial read wait':raise
    serial.pump()
  result['observed_quiet_seconds_before_CLOSE']=time.monotonic()-quiet_since
  timings.append({'event':'guest_CLOSE','elapsed_seconds':time.monotonic()-started});policy.begin_close();send(3)
  while True:
   kind,seq,data=receive()
   if kind==3:break
   assert kind==2 # No writes after guest CLOSE.
  assert serial.recv(1)==b'';codec.end();rc=proc.wait(5)
  b=json.loads((records/'result.json').read_text());assert rc==0 and b['complete'],b.get('first_error')
  assert len(b['egress']['flows'])==2 and all(f['request_covered_bytes']==186 and f['HTTP']['status']==200 for f in b['egress']['flows'])
  assert b['egress']['quiet_before_close_seconds']>30 and b['egress']['query_count']==1
  assert serial.bridge.transport_complete and serial.bridge.data_bytes==serial.bridge.acked_bytes and serial.bridge.guest_result is None
  assert serial.peak_pending<=8 and serial.peak_raw<=8192 and len(serial.whitespace)<=262144
  result.update(status='PASS_ACTUAL_PACED_LINUX_TWO_FLOWS_CACHE_AND_QUIET_OVER30',backend= b,
   synthetic_client_slots={str(state['slot']):{'port':key[0],'remote':'.'.join(map(str,key[1]))}for key,state in flows.items()},
   scope='Synthetic guest packets/ACKs; actual Linux slirp/DNS/HTTP two flows. No native API/PID/desktop/interface/route/target result.')
 except BaseException as error:result['error']=repr(error)
 finally:
  if serial is not None:result['elapsed_seconds']=time.monotonic()-started;result['fixture_phase_events']=timings;result['credit']=serial.report();(output/'ingress-whitespace.bin').write_bytes(bytes(serial.whitespace));serial.close()
  if codec is not None:codec.close()
  if proc is not None:
   if proc.poll()is None:
    proc.terminate()
    try:proc.wait(4)
    except subprocess.TimeoutExpired:proc.kill();proc.wait(2);result['forced_backend_KILL']=True
   result['backend_returncode']=proc.returncode
  log.close()
  if (records/'result.json').exists():result['closed_backend']=json.loads((records/'result.json').read_text())
  (output/'test-result.json').write_text(json.dumps(result,indent=2)+'\n')
 return result
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--library',required=True,type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--allow-public-example-http',required=True,action='store_true');a=p.parse_args()
 r=run(a.library.resolve(),a.output.resolve());print(json.dumps(r,indent=2));raise SystemExit(0 if r['status'].startswith('PASS')else 1)
