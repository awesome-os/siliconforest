"""Replay the failed resolver002 first TCP SYN bytes; no actor or regrade."""
from collections import deque
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import types
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[5]
sys.path.insert(0,str(HERE))
from policy import Policy,parse_packet
HELPER=ROOT/'integrations/puredarwin/m1-network-resolver-service/host/line_bridge.py'
HELPER_SHA='bdb063da457fc7e22ace5d5abaf8a256d407b508b62ddf2f78988dadd72f4438'
BASE=HERE.parents[1]/'002/host/policy.py'
BASE_SHA='1f41d81cf9a01e79c79ec83f107c58fca03ce8c73b487992e5f6b7352f90b7f1'
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def run(records):
 assert digest(HELPER)==HELPER_SHA and digest(BASE)==BASE_SHA
 rawh=HELPER.read_bytes();h=types.ModuleType('frozen_DL29_ECN');exec(compile(rawh,str(HELPER),'exec'),h.__dict__)
 rawold=BASE.read_bytes();old=types.ModuleType('frozen_policy002_ECN');exec(compile(rawold,str(BASE),'exec'),old.__dict__)
 inputs={n:digest(records/n) for n in ('result.json','relay-rx.bin','relay-tx.bin')}
 result=json.loads((records/'result.json').read_text());e=result['egress'];nonce=result['nonce']
 assert not result['complete'] and 'first TCP packet must be SYN' in result['first_error']
 udp={};first_tcp=None
 for direction,name in ((1,'relay-rx.bin'),(2,'relay-tx.bin')):
  frames=h.Frames(nonce,direction);parsed=frames.feed((records/name).read_bytes())
  assert not frames.buffer # Failed stream need not contain CLOSE; do not fabricate it.
  chosen=[]
  for kind,seq,frame in parsed:
   if kind!=2:continue
   packet=frame[56:];view=parse_packet(packet)
   if view[0]==17:chosen.append(packet)
   elif direction==1 and first_tcp is None:first_tcp=packet
  udp[direction]=deque(chosen)
 assert first_tcp is not None
 view=parse_packet(first_tcp);body=view[5]
 assert view[0]==6 and view[3]==49152 and view[4]==80 and body[13]==0xc2 and not view[6]
 assert (body[12]>>4)*4==44 and body[12]&15==0
 clock=[0.];p=Policy(nonce,clock=lambda:clock[0]);q=old.Policy(nonce,clock=lambda:clock[0]);timeline=[]
 for direction,key,field in ((1,'dns_query_events','query_hex'),(2,'dns_reply_events','reply_hex')):
  for event in e[key]:timeline.append((event['elapsed_seconds'],direction,field,event))
 for elapsed,direction,field,event in sorted(timeline,key=lambda z:z[:2]):
  packet=udp[direction].popleft();assert parse_packet(packet)[6].hex()==event[field]
  clock[0]=elapsed
  for state in (p,q):(state.outgoing if direction==1 else state.incoming)(packet)
 assert not any(udp.values())
 clock[0]=max(x[0]for x in timeline)+.001 # Explicit replay clock, not a historical TCP timestamp.
 old_error=None
 try:q.outgoing(first_tcp)
 except ValueError as error:old_error=str(error)
 assert old_error=='first TCP packet must be SYN'
 p.outgoing(first_tcp)
 assert p.initial_tcp_flags==0xc2 and p.tcp_port==49152 and p.summary()['backend_policy_revision']==3
 assert p.summary()['dns_policy_revision']==2
 assert inputs=={n:digest(records/n) for n in inputs}
 return {'status':'PASS_ACTUAL_M1_RESOLVER002_FIRST_ECN_SYN_SOURCE_REPLAY_ONLY',
  'scope':'CRC/IP/TCP-checksum validated recorded packet; explicit replay clock; no target execution, historical verdict or recovery upgrade',
  'inputs_sha256':inputs,'old_policy_sha256':BASE_SHA,'final_policy_sha256':digest(HERE/'policy.py'),
  'old_source_rejection':old_error,'actual_packet_sha256':hashlib.sha256(first_tcp).hexdigest(),
  'IPv4_bytes':len(first_tcp),'TCP_header_bytes':44,'TCP_flags':body[13],'TCP_reserved_bits':body[12]&15,
  'payload_bytes':len(view[6]),'source_ipv4':'.'.join(map(str,view[1])),
  'destination_ipv4':'.'.join(map(str,view[2])),'source_port':view[3],'destination_port':view[4],
  'selected_first_canonical_answer_index':p.selected_answer_index,'replay_clock':clock[0],
  'backend_policy_revision':3,'dns_policy_revision':2,'capture_unchanged':True,
  'no_CLOSE_or_HTTP_or_new_Darwin_PASS_claim':True}
if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--records',type=Path,required=True);args=parser.parse_args()
 print(json.dumps(run(args.records.resolve()),indent=2))
