"""Replay saved real Linux DNS frame bytes through revision002; no actor.

This is packet-policy regression only. It neither rewrites the capture nor
re-verdicts an M1 attempt, native resolver or Linux recovery result.
"""
from collections import deque
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[5]
sys.path.insert(0,str(HERE))
from policy import Policy,parse_packet
HELPER=ROOT/'integrations/puredarwin/m1-network-resolver-service/host/line_bridge.py'
HELPER_SHA='bdb063da457fc7e22ace5d5abaf8a256d407b508b62ddf2f78988dadd72f4438'

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def run(records):
 assert digest(HELPER)==HELPER_SHA
 spec=importlib.util.spec_from_file_location('frozen_DL29_replay',HELPER)
 helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
 result=json.loads((records/'result.json').read_text());old=result['egress'];nonce=result['nonce']
 assert result['complete'] and result['profile']=='native-resolver-example-http-v1'
 packets={};original_packets={}
 for direction,name in ((1,'relay-rx.bin'),(2,'relay-tx.bin')):
  parser=helper.Frames(nonce,direction);frames=parser.feed((records/name).read_bytes());parser.end()
  chosen=[]
  for kind,_sequence,frame in frames:
   if kind==2:
    packet=frame[56:];parsed=parse_packet(packet)
    if parsed[0]==17:chosen.append(packet)
  packets[direction]=deque(chosen);original_packets[direction]=chosen
 clock=[0.];policy=Policy(nonce,clock=lambda:clock[0]);timeline=[]
 for direction,key in ((1,'dns_query_events'),(2,'dns_reply_events')):
  field='query_hex' if direction==1 else 'reply_hex'
  for event in old[key]:timeline.append((event['elapsed_seconds'],direction,field,event))
 for elapsed,direction,field,event in sorted(timeline,key=lambda x:x[:2]):
  packet=packets[direction].popleft();parsed=parse_packet(packet)
  assert parsed[6].hex()==event[field]
  clock[0]=elapsed
  (policy.outgoing if direction==1 else policy.incoming)(packet)
 assert not any(packets.values())
 events=policy.reply_events
 assert len(events)>=2 and all(e['accepted'] for e in events)
 assert all(e['comparison_mode']=='a_only_answer_multiset' for e in events)
 assert any(e['answer_order_changed_from_first'] for e in events)
 assert len({e['comparison_identity_sha256'] for e in events})==1
 assert len({e['original_lease_expires_elapsed_seconds'] for e in events})==1
 assert policy.reply.hex()==old['dns_reply_hex']
 assert policy.summary()['answer_ipv4']==old['answer_ipv4']
 assert policy.endpoint is None # DNS-only replay creates no new TCP flow.
 return {'status':'PASS_ACTUAL_SAVED_ROTATION_DNS_BYTES_REPLAY_FINAL_POLICY_ONLY',
  'scope':'Actual saved Linux003 DNS bytes; no network/target activity or replacement historical verdict',
  'inputs_sha256':{n:digest(records/n) for n in ('result.json','relay-rx.bin','relay-tx.bin')},
  'final_policy_sha256':digest(HERE/'policy.py'),'DNS_queries':policy.dns_attempts,
  'DNS_duplicates':policy.dns_duplicates,'first_A_order':policy.summary()['answer_ipv4'],
  'raw_reply_RR_TTLs':[e['rr_ttls'] for e in events],
  'actual_order_changed':[e['answer_order_changed_from_first'] for e in events],
  'fixed_replay_expiry':policy.original_lease_expires_elapsed_seconds,
  'DNS_reply_events':events,'replay_egress':policy.summary(),
  'queries_ipv4_hex':[p.hex() for p in original_packets[1]],
  'replies_ipv4_hex':[p.hex() for p in original_packets[2]],'captures_unchanged':True,'HTTP_or_Darwin_requalified':False}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--records',type=Path,required=True);a=p.parse_args()
 print(json.dumps(run(a.records.resolve()),indent=2))
