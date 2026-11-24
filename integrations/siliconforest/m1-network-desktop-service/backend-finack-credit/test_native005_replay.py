"""Actual failed005 bytes through NEW TCP7 source, NOT historical regrading.

Cross-direction scheduling is an explicitly compatible source replay, not an
independently observed shared wire clock. Original005 remains failed TCP4.
"""
from pathlib import Path
import hashlib,json,struct,sys
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[3];sys.path.insert(0,str(HERE))
from packet_fixture import frame
from model import Policy
from frozen import parse_packet
CAPTURE=ROOT/'runs/m1-desktop-010/desktop-network-live005/backend'
def crc32c(raw):
 crc=0xffffffff
 for byte in raw:
  crc^=byte
  for _ in range(8):crc=(crc>>1)^(0x82f63b78 if crc&1 else 0)
 return crc^0xffffffff
def relay(raw):
 out=[];o=0;seq=1;nonce=None
 while o<len(raw):
  h=raw[o:o+56];assert len(h)==56
  magic,kind,d,r,n,hlen,actualseq,session,crc,tail=struct.unpack('!8sBBHHHQ24sII',h);frame=raw[o:o+56+n]
  assert magic==b'DLNET001'and d==1 and r==tail==0 and hlen==56 and actualseq==seq and len(frame)==56+n
  assert crc32c(frame[:48]+b'\0'*4+frame[52:])==crc and n<=1500
  if nonce is None:nonce=session
  assert session==nonce
  if kind==2:out.append(frame[56:])
  o+=56+n;seq+=1
 return out,nonce.hex()
def ethernet(raw):
 out=[];o=0
 while o<len(raw):
  n=int.from_bytes(raw[o:o+4],'big');frame=raw[o+4:o+4+n];assert len(frame)==n;o+=4+n
  if frame[12:14]==b'\x08\x00':
   ip=frame[14:];size=int.from_bytes(ip[2:4],'big');assert len(ip)>=size and not any(ip[size:]);out.append(ip[:size])
 return out
def run():
 original=json.loads((CAPTURE/'result.json').read_text());assert original['first_error']=="ValueError('external TCP reset outside exact terminal case')"
 outgoing,nonce=relay((CAPTURE/'relay-rx.bin').read_bytes());incoming=ethernet((CAPTURE/'ethernet-rx.bin').read_bytes())
 hostseq=1;clock=[0.];p=Policy(nonce,clock=lambda:clock[0]);p.open();oi=0
 def drain():
  nonlocal oi
  while oi<len(outgoing):
   q=outgoing[oi];v=parse_packet(q)
   if v[0]==6:
    key=(v[3],v[2],v[4]);f=p.flows.get(key);body=v[5];flags=body[13]
    if flags&2 and not any(v[2]in tx['addresses']for tx in p.transactions.values()):break
    if not flags&2:
     if f is None or f.server_isn is None:break
     if flags&1 and (not f.complete()or f.client_FIN_seen and not f.server_FIN_ACK_seen):break
   if v[0]==6 and v[5][13]&4 and not any(not e['baseline']and not e['guest_RST_consumed']for e in f.finACK_emission_events):break
   clock[0]+=.001;p.outgoing(q);oi+=1
 resets=[]
 for raw in incoming:
  drain();clock[0]+=.001;p.incoming(raw);hostseq+=1;p.commit_forwarded(raw,frame(nonce,hostseq,raw))
  v=parse_packet(raw)
  if v[0]==6 and v[5][13]&4:resets.append(raw.hex())
 drain();assert oi==len(outgoing)and p.complete()
 events=[e for f in p.flows.values()for e in f.terminal_reset_events]
 assert len(events)==len(resets)==1 and events[0]['packet_hex']==resets[0]and events[0]['trigger']=='repeated_client_FIN'
 assert all(not f.final_ACK_after_server_FIN_ACK for f in p.flows.values())
 return dict(status='PASS_NEW_TCP7_SOURCE_REPLAY_OF_ACTUAL_FAILED005_BYTES',scope='Synthetic source replay clock and compatible cross-direction scheduling only; not historical acceptance/grading, packet edits, new target activity or old005 upgrade.',outgoing_packets=len(outgoing),incoming_packets=len(incoming),CRC32C_guest_stream_verified=True,no_synthetic_ACK_of_ACK=True,terminal_RST_events=events,original_failure_unchanged=True,input_sha256={str(x.relative_to(ROOT)):hashlib.sha256(x.read_bytes()).hexdigest()for x in(CAPTURE/'result.json',CAPTURE/'relay-rx.bin',CAPTURE/'ethernet-rx.bin')})
if __name__=='__main__':print(json.dumps(run(),indent=2))
