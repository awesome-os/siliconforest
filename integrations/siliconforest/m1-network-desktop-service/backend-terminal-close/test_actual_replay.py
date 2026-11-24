"""Source replay of retained Linux002 packets, not historical timing approval."""
from pathlib import Path
import hashlib,json,sys
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[3];sys.path.insert(0,str(HERE))
from model import Policy
from frozen import parse_packet
CAPTURE=ROOT/'runs/m1-desktop-010/desktop-network-backend-linux-002/pipeline'
def relay(raw):
 out=[];o=0
 while o<len(raw):
  assert len(raw)-o>=56;n=int.from_bytes(raw[o+12:o+14],'big');kind=raw[o+8];p=raw[o+56:o+56+n];assert len(p)==n
  if kind==2:out.append(p)
  o+=56+n
 return out
def ethernet(raw):
 out=[];o=0
 while o<len(raw):
  assert len(raw)-o>=4;n=int.from_bytes(raw[o:o+4],'big');f=raw[o+4:o+4+n];assert len(f)==n;o+=4+n
  if f[12:14]==b'\x08\x00':
   ip=f[14:];out.append(ip[:int.from_bytes(ip[2:4],'big')])
 return out
def run():
 record=json.loads((CAPTURE/'test-result.json').read_text());outgoing=relay((CAPTURE/'backend/relay-rx.bin').read_bytes());incoming=ethernet((CAPTURE/'backend/ethernet-rx.bin').read_bytes())
 clock=[0.];p=Policy(record['nonce'],clock=lambda:clock[0]);p.open();oi=0
 def drain():
  nonlocal oi
  while oi<len(outgoing):
   q=outgoing[oi];v=parse_packet(q)
   if v[0]==6:
    key=(v[3],v[2],v[4]);f=p.flows.get(key);body=v[5];flags=body[13];seq=int.from_bytes(body[4:8],'big')
    if flags&2 and not any(v[2]in tx['addresses']for tx in p.transactions.values()):break
    if not flags&2:
     if f is None or f.server_isn is None:break
     if flags&1 and not f.complete():break
     if seq==(f.client_isn+len(f.request)+2)&0xffffffff and not f.server_FIN_ACK_seen:break
   clock[0]+=.001;p.outgoing(q);oi+=1
 resets=[]
 for ip in incoming:
  drain();clock[0]+=.001;p.incoming(ip)
  v=parse_packet(ip)
  if v[0]==6 and v[5][13]&4:resets.append(ip.hex())
 drain();assert oi==len(outgoing),(oi,len(outgoing));assert len(resets)==1;assert p.complete()
 events=[event for flow in p.flows.values()for event in flow.terminal_reset_events]
 assert len(events)==1 and events[0]['packet_hex']==resets[0]
 return {'status':'PASS_ACTUAL_LINUX002_BYTES_REPLAY_TCP4','scope':'Source replay causal interleaving; synthetic increasing clock, not historical acceptance timestamps','outgoing_packets':len(outgoing),'incoming_packets':len(incoming),'terminal_reset_packets':len(resets),'all_three_close_prerequisites_observed':all(f.client_FIN_seen and f.server_FIN_ACK_seen for f in p.flows.values()),'RST_event':events[0], 'source_capture_sha256':{str(x.relative_to(ROOT)):hashlib.sha256(x.read_bytes()).hexdigest()for x in(CAPTURE/'test-result.json',CAPTURE/'backend/relay-rx.bin',CAPTURE/'backend/ethernet-rx.bin')}}
if __name__=='__main__':print(json.dumps(run(),indent=2))
