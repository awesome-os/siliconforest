"""Original TCP7 synthetic/negative corpus; no production model imports.

Captured008 data is used only for direction2 single-frame parser controls,
never to regrade its failed lifecycle. Final TCP7 actual fixture is separate.
"""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import unittest
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[5]
spec=importlib.util.spec_from_file_location('independent_TCP7_under_test',HERE/'tcp_reset_evidence.py');check=importlib.util.module_from_spec(spec);spec.loader.exec_module(check)
G=bytes([10,0,2,15]);A=bytes([93,184,216,34]);B=bytes([1,1,1,1]);NONCE='a'*48
REQUEST=(b'GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: darwin-lab-egress/1\r\nAccept-Encoding: identity\r\nX-Darwin-Lab-Nonce: '+NONCE.encode()+b'\r\nConnection: close\r\n\r\n')
RESPONSE=b'HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 14\r\n\r\nExample Domain'

def checksum(raw):
 if len(raw)%2:raw+=bytes(1)
 total=sum(struct.unpack('!%dH'%(len(raw)//2),raw))
 while total>>16:total=(total&65535)+(total>>16)
 return (~total)&65535

def tcp(src,dst,sp,dp,seq,ack,flags,payload=b'',reserved=0,options=b''):
 body=struct.pack('!HHIIBBHHH',sp,dp,seq&0xffffffff,ack&0xffffffff,((20+len(options))//4)<<4|reserved,flags,32768,0,0)+options+payload
 c=checksum(src+dst+b'\0\x06'+len(body).to_bytes(2,'big')+body);body=body[:16]+c.to_bytes(2,'big')+body[18:]
 header=struct.pack('!BBHHHBBH4s4s',0x45,0,20+len(body),1,0x4000,64,6,0,src,dst)
 return header[:10]+checksum(header).to_bytes(2,'big')+header[12:]+body

def crc32c(raw):
 value=0xffffffff
 for byte in raw:
  value^=byte
  for _ in range(8):value=(value>>1)^(0x82f63b78 if value&1 else 0)
 return value^0xffffffff

def frame(kind,seq,packet=b'',direction=2,nonce=NONCE):
 raw=struct.pack('!8sBBHHHQ24sII',b'DLNET001',kind,direction,0,len(packet),56,seq,bytes.fromhex(nonce),0,0)+packet
 return raw[:48]+crc32c(raw).to_bytes(4,'big')+raw[52:]

def frames(packets,direction):return [frame(1,1,direction=direction)]+[frame(2,i+2,p,direction)for i,p in enumerate(packets)]+[frame(3,len(packets)+2,direction=direction)]

def event(packet,elapsed,**extra):
 seq,ack=struct.unpack_from('!II',packet,24)
 return dict(elapsed_seconds=elapsed,packet_hex=packet.hex(),sequence=seq,acknowledgement=ack,flags=packet[33],payload_bytes=len(packet)-20-(packet[32]>>4)*4,policy_accepted=True,**extra)

def fixture(n=5,credits=5,peers=None,bare=1):
 if peers is None:peers=n
 gp=[];hp=[];flows=[]
 for ordinal,(address,port,cisn,sisn)in enumerate(((A,43002,10000,20000),(B,43003,30000,40000)),1):
  final_server=sisn+len(RESPONSE)+2;final_client=cisn+188
  guest=tcp(G,address,port,80,final_client,0,4);peer=tcp(address,G,80,port,0,final_client,0x14)
  finack=tcp(address,G,80,port,final_server,final_client,0x10);reset=tcp(address,G,80,port,final_server,0,4)
  gp.extend([tcp(G,address,port,80,cisn,0,0xc2),tcp(G,address,port,80,cisn+1,sisn+1,0x18,REQUEST),tcp(G,address,port,80,cisn+187,final_server,0x11),tcp(G,address,port,80,cisn+187,final_server,0x11)]+[guest]*n)
  hp.extend([tcp(address,G,80,port,sisn,cisn+1,0x12),tcp(address,G,80,port,sisn+1,cisn+187,0x19,RESPONSE)]+[finack]*(credits+1)+[reset]*bare+[peer]*peers)
  flow=dict(flow_id=ordinal,local_ipv4='10.0.2.15',local_port=port,remote_ipv4='.'.join(map(str,address)),remote_port=80,
    client_ISN=cisn,server_ISN=sisn,initial_tcp_flags=0xc2,request_expected_hex=REQUEST.hex(),request_observed_hex=REQUEST.hex(),request_covered_bytes=186,
    response_contiguous_bytes=len(RESPONSE),response_highest_bytes=len(RESPONSE),response_FIN_offset=len(RESPONSE),
    HTTP={'status':200,'body_bytes':14,'header_bytes':len(RESPONSE)-14},wire_complete=True,TCP_policy_revision=7,
    repeated_client_FIN_after_server_FIN_ACK=True,client_FIN_seen=True,server_FIN_ACK_seen=True,final_ACK_after_server_FIN_ACK=False,
    terminal_reset_count=bare,terminal_reset_events=[event(reset,4.5+i/100,trigger='repeated_client_FIN')for i in range(bare)],
    guest_terminal_RST_count=n,guest_terminal_RST_events=[event(guest,5+i/100,credit_id=i+1,peer_reply_consumed=i<peers,
      finACK_credit_id=i+1,finACK_frame_sequence=0,finACK_frame_sha256='')for i in range(n)],
    peer_stateless_RST_count=peers,peer_stateless_RST_events=[event(peer,6+i/100,guest_RST_credit_id=i+1)for i in range(peers)],
    peer_RST_total_count=bare+peers,terminal_budgets=dict(bare_peer=8,guest=127,stateless_peer=127,total_peer=128),
    finACK_emission_count=credits+1,finACK_credit_count=credits,finACK_credits_consumed=n,finACK_credits_available=credits-n,finACK_emission_events=[])
  flows.append(flow)
 e=dict(profile='desktop-native-resolver-two-http-v1',first_error=None,backend_policy_revision=5,tcp_policy_revision=7,dns_policy_revision=2,
  parser_dependency_SHA256='20a0b6d5dbdff17b748f241623fc09989dd83b46957ab68862ae8799fecc6d8b',flows=flows,request_expected_hex=REQUEST.hex())
 case={'gp':gp,'hp':hp,'egress':e,'nonce':NONCE};bind(case);return case

def bind(case,host_ip=None,eligible_options=False):
 if host_ip is None:host_ip=case['hp']
 case['gf']=frames(case['gp'],1);case['hf']=frames(host_ip,2)
 for flow in case['egress']['flows']:
  final_server=(flow['server_ISN']+2+flow['response_FIN_offset'])&0xffffffff;final_client=(flow['client_ISN']+188)&0xffffffff
  emissions=[]
  for raw in case['hf']:
   p=raw[56:]
   if len(p)<40 or p[9]!=6 or p[33]!=0x10 or (not eligible_options and(p[32]!=0x50 or len(p)!=40)):continue
   if int.from_bytes(p[22:24],'big')!=flow['local_port']or int.from_bytes(p[24:28],'big')!=final_server or int.from_bytes(p[28:32],'big')!=final_client:continue
   index=len(emissions)+1;seq=int.from_bytes(raw[16:24],'big')
   emissions.append(dict(elapsed_seconds=3+index/100,packet_hex=p.hex(),packet_sha256=hashlib.sha256(p).hexdigest(),frame_direction=2,
      frame_sequence=seq,frame_sha256=hashlib.sha256(raw).hexdigest(),frame_bytes=len(raw),commit_index=seq-1,
      emission_index=index,baseline=index==1,credit_id=None if index==1 else index-1,guest_RST_consumed=1<index<=flow['guest_terminal_RST_count']+1))
  flow['finACK_emission_events']=emissions
  for guest in flow['guest_terminal_RST_events']:
   credit=guest['finACK_credit_id']
   if 0<credit<len(emissions):guest['finACK_frame_sequence']=emissions[credit]['frame_sequence'];guest['finACK_frame_sha256']=emissions[credit]['frame_sha256']
 return case

def grade(case):return check.terminal_reset_evidence(case['gp'],case['hp'],case['egress'],host_frames=case['hf'])

class Tests(unittest.TestCase):
 def ok(self,case):
  result=grade(case);self.assertEqual(tuple(result['checks']),check.CHECKS);self.assertTrue(all(result['checks'].values()),result);return result
 def bad(self,case):
  result=grade(case);self.assertFalse(result['qualified_TCP7_terminal_reset_records_only'],result);self.assertEqual(tuple(result['checks']),check.CHECKS);self.assertTrue(result['details']['errors']);return result
 def test_fifth_credit_positive_and_fullstream_API(self):
  case=fixture();r=self.ok(case);self.assertTrue(all(len(f['FINACK_emissions'])==6 and f['guest_terminal_RST_count']==5 for f in r['details']['flows']))
  result=check.terminal_reset_stream_evidence(b''.join(case['gf']),b''.join(case['hf']),case['egress'],NONCE);self.assertTrue(all(result['checks'].values()),result)
  self.assertFalse(r['details']['global_chronology_independently_observed'])
 def test_exhausted_sixth_and_baseline_cannot_grant(self):
  self.bad(fixture(6,5))
  case=fixture();g=case['egress']['flows'][0]['guest_terminal_RST_events'][0];g['finACK_credit_id']=0;self.bad(case)
  for key,value in [('baseline',False),('credit_id',0),('guest_RST_consumed',True)]:
   case=fixture();case['egress']['flows'][0]['finACK_emission_events'][0][key]=value;self.bad(case)
 def test_unused_committed_and_guest_credits_allowed(self):
  self.ok(fixture(2,5,1));self.ok(fixture(0,5,0))
 def test_FIFO_reused_skipped_or_crossflow_ACK_credit_refused(self):
  for bad in(0,1,3,True,127):
   case=fixture();case['egress']['flows'][0]['guest_terminal_RST_events'][1]['finACK_credit_id']=bad;self.bad(case)
  case=fixture();case['egress']['flows'][0]['guest_terminal_RST_events'][0]['finACK_frame_sha256']=case['egress']['flows'][1]['finACK_emission_events'][1]['frame_sha256'];self.bad(case)
 def test_peer_credit_reuse_reorder_unknown_refused(self):
  for value in(0,1,3,True,127):
   case=fixture();case['egress']['flows'][0]['peer_stateless_RST_events'][1]['guest_RST_credit_id']=value;self.bad(case)
 def test_emission_identity_fields_and_exact_schema(self):
  for key,value in [('frame_direction',1),('frame_sequence',1),('frame_sha256','0'*64),('packet_sha256','0'*64),('packet_hex','00'),('frame_bytes',40),('commit_index',0),('emission_index',0),('credit_id',True),('baseline',True),('elapsed_seconds',float('nan')),('unrecorded',1)]:
   case=fixture();case['egress']['flows'][0]['finACK_emission_events'][1][key]=value;self.bad(case)
 def test_duplicate_reordered_missing_and_uncommitted_emissions(self):
  for change in [lambda x:x.pop(),lambda x:x.append(copy.deepcopy(x[-1])),lambda x:x.reverse(),lambda x:x.__setitem__(1,copy.deepcopy(x[0]))]:
   case=fixture();change(case['egress']['flows'][0]['finACK_emission_events']);self.bad(case)
  case=fixture();f=case['egress']['flows'][0];extra=copy.deepcopy(f['finACK_emission_events'][-1])
  extra.update(frame_sequence=100,commit_index=99,emission_index=7,credit_id=6,guest_RST_consumed=False)
  f['finACK_emission_events'].append(extra);f['finACK_emission_count']=7;f['finACK_credit_count']=6
  self.bad(case) # Encoding/claiming an absent unqueued frame cannot grant.
 def test_source_commit_consume_clock_order(self):
  for section,index,value in [('finACK_emission_events',1,9),('guest_terminal_RST_events',1,2),('peer_stateless_RST_events',1,4)]:
   case=fixture();case['egress']['flows'][0][section][index]['elapsed_seconds']=value;self.bad(case)
  for section,start in [('guest_terminal_RST_events',2),('peer_stateless_RST_events',4)]:
   case=fixture()
   for i,event in enumerate(case['egress']['flows'][0][section]):event['elapsed_seconds']=start+i/100
   self.bad(case)
 def test_coherent_FIFO_swaps_and_reuses_cannot_hide_in_balances(self):
  case=fixture();f=case['egress']['flows'][0]
  a,b=f['guest_terminal_RST_events'][1:3]
  for key in('finACK_credit_id','finACK_frame_sequence','finACK_frame_sha256'):a[key],b[key]=b[key],a[key]
  self.bad(case)
  case=fixture();f=case['egress']['flows'][0]
  f['peer_stateless_RST_events'][1]['guest_RST_credit_id']=3;f['peer_stateless_RST_events'][2]['guest_RST_credit_id']=2;self.bad(case)
  case=fixture();f=case['egress']['flows'][0];a,b=f['guest_terminal_RST_events'][:2]
  for key in('finACK_credit_id','finACK_frame_sequence','finACK_frame_sha256'):b[key]=a[key]
  f['finACK_emission_events'][2]['guest_RST_consumed']=False;f['finACK_credits_consumed']=4;f['finACK_credits_available']=1;self.bad(case)
  case=fixture();f=case['egress']['flows'][0];f['peer_stateless_RST_events'][1]['guest_RST_credit_id']=1;f['guest_terminal_RST_events'][1]['peer_reply_consumed']=False;self.bad(case)
 def test_baseline_raw_frame_hash_and_source_scope_required(self):
  case=fixture();case['egress']['flows'][0]['finACK_emission_events'][0]['frame_sha256']='0'*64;self.bad(case)
  for field,value in [('TCP_policy_revision',6),('client_FIN_seen',False),('server_FIN_ACK_seen',False),('repeated_client_FIN_after_server_FIN_ACK',False)]:
   case=fixture();case['egress']['flows'][0][field]=value;self.bad(case)
  for field,value in [('first_error','failed'),('parser_dependency_SHA256','0'*64),('backend_policy_revision',4),('tcp_policy_revision',6),('dns_policy_revision',1)]:
   case=fixture();case['egress'][field]=value;self.bad(case)
 def test_normal20_FINACK_is_eligibility_boundary(self):
  case=fixture()
  for i,p in enumerate(case['hp']):
   if p[33]==0x10:
    sp,dp,seq,ack=struct.unpack_from('!HHII',p,20)
    case['hp'][i]=tcp(p[12:16],p[16:20],sp,dp,seq,ack,0x10,options=b'\0'*4)
  bind(case,eligible_options=True);self.bad(case)
 def test_balance_counts_and_consumed_flags(self):
  for key,value in [('finACK_emission_count',True),('finACK_credit_count',6),('finACK_credits_consumed',4),('finACK_credits_available',1),('guest_terminal_RST_count',6),('peer_stateless_RST_count',6),('peer_RST_total_count',7)]:
   case=fixture();case['egress']['flows'][0][key]=value;self.bad(case)
  for section,key in [('finACK_emission_events','guest_RST_consumed'),('guest_terminal_RST_events','peer_reply_consumed')]:
   case=fixture();case['egress']['flows'][0][section][1][key]=False;self.bad(case)
 def test_metadata_only_capacity_cannot_grant_and_bare8_unchanged(self):
  self.ok(fixture(5,5,bare=8));self.bad(fixture(0,5,bare=9))
  for key in('bare_peer','guest','stateless_peer','total_peer'):
   case=fixture();case['egress']['flows'][0]['terminal_budgets'][key]+=1;self.bad(case)
   case=fixture();case['egress']['flows'][0]['terminal_budgets'][key]=float(case['egress']['flows'][0]['terminal_budgets'][key]);self.bad(case)
 def test_guest_event_raw_scalar_and_frame_link_mutants(self):
  for key,value in [('packet_hex','00'),('sequence',1),('acknowledgement',1),('flags',20),('payload_bytes',1),('policy_accepted',False),('credit_id',True),('finACK_frame_sequence',1),('finACK_frame_sha256','0'*64),('extra',1)]:
   case=fixture();case['egress']['flows'][0]['guest_terminal_RST_events'][0][key]=value;self.bad(case)
 def test_peer_event_scalar_mutants(self):
  for key,value in [('packet_hex','00'),('sequence',1),('acknowledgement',1),('flags',4),('payload_bytes',1),('policy_accepted',False),('extra',1)]:
   case=fixture();case['egress']['flows'][0]['peer_stateless_RST_events'][0][key]=value;self.bad(case)
 def test_duplicate_samepacket_emission_distinguished_by_frameSHA(self):
  case=fixture();events=case['egress']['flows'][0]['finACK_emission_events'];self.assertEqual(events[1]['packet_hex'],events[2]['packet_hex']);self.assertNotEqual(events[1]['frame_sha256'],events[2]['frame_sha256']);self.ok(case)
  events[2]['frame_sequence']=events[1]['frame_sequence'];events[2]['frame_sha256']=events[1]['frame_sha256'];self.bad(case)
 def test_missing_raw_frame_is_not_a_committed_receipt(self):
  case=fixture();del case['hf'][4];self.bad(case)
  case=fixture();case['hf'][4]=frame(2,5,case['hp'][3],direction=1);self.bad(case)
 def test_mandatory_actual_hostframes_API(self):
  case=fixture();r=check.terminal_reset_evidence(case['gp'],case['hp'],case['egress']);self.assertFalse(any(r['checks'].values()))
 def test_new_policy_does_not_upgrade_old_sources(self):
  case=fixture();case['egress']['backend_policy_revision']=4;case['egress']['tcp_policy_revision']=6;self.bad(case)
  case=fixture();del case['egress']['flows'][0]['finACK_emission_events'];self.bad(case)
 def test_raw_forms_checksums_HTTP_and_request_coverage(self):
  for side,index,offset in [('gp',0,0),('gp',0,36),('hp',0,36),('gp',4,33),('hp',9,33)]:
   case=fixture();p=bytearray(case[side][index]);p[offset]^=1;case[side][index]=bytes(p);bind(case);self.bad(case)
  case=fixture();case['gp'][1]=tcp(G,A,43002,80,10001,20001,0x18,REQUEST[:-1]);bind(case);self.bad(case)
  case=fixture();case['hp'][1]=tcp(A,G,80,43002,20001,10187,0x19,RESPONSE.replace(b'200 OK',b'201 OK'));bind(case);self.bad(case)
 def test_fullstream_empty_truncated_wrongdirection_and_nonce(self):
  case=fixture()
  for g,h,nonce in [(b'',b'',NONCE),(b''.join(case['gf'])[:-1],b''.join(case['hf']),NONCE),(b''.join(case['hf']),b''.join(case['gf']),NONCE),(b''.join(case['gf']),b''.join(case['hf']),'b'*48)]:
   r=check.terminal_reset_stream_evidence(g,h,case['egress'],nonce);self.assertFalse(any(r['checks'].values()))
 def test_global_frame_byte_and_IP_bounds(self):
  case=fixture();case['gp']+=case['gp'][:1]*126;bind(case);self.bad(case)
  case=fixture();case['hf'].extend([case['hf'][-1]]*128);self.bad(case)
  bad=b''.join(frame(2,i+1,b'x'*1500)for i in range(128))
  self.assertFalse(any(check.terminal_reset_stream_evidence(bad,bad,case['egress'],NONCE)['checks'].values()))
 def test_actual008_direction2_FINACK_frame_parser_controls_only(self):
  base=ROOT/'runs/m1-desktop-010/desktop-network-live008/backend';backend=json.loads((base/'result.json').read_text());raw=(base/'relay-tx.bin').read_bytes();nonce=backend['nonce'];offset=0;ordinal=1;found=0
  while offset+56<=len(raw):
   size=int.from_bytes(raw[offset+12:offset+14],'big')
   if offset+56+size>len(raw):break
   f=raw[offset:offset+56+size];identity=check._host_frame(f,ordinal,nonce);self.assertEqual(f[9],2);p=identity['packet']
   if len(p)==40 and p[9]==6 and p[33]==0x10:
    for flow in backend['egress']['flows']:
     if int.from_bytes(p[22:24],'big')==flow['local_port']and int.from_bytes(p[24:28],'big')==(flow['server_ISN']+2+flow['response_FIN_offset'])&0xffffffff and int.from_bytes(p[28:32],'big')==(flow['client_ISN']+188)&0xffffffff:
      found+=1;self.assertEqual(identity['frame_sha256'],hashlib.sha256(f).hexdigest())
   offset+=len(f);ordinal+=1
  self.assertGreaterEqual(found,6)
  # This parser-only control does not assert CLOSE or upgrade failed008.
 def test_crc_known_vector_and_raw_header_negatives(self):
  self.assertEqual(check._crc32c(b'123456789'),0xe3069283)
  case=fixture();raw=case['hf'][1]
  for offset in(0,9,10,14,16,24,48,52):
   changed=bytearray(raw);changed[offset]^=1
   with self.assertRaises(ValueError):check._host_frame(bytes(changed),2,NONCE)
  with self.assertRaises(ValueError):check._host_frame(frame(2,2,case['hp'][0],direction=1),2,NONCE)
  with self.assertRaises(ValueError):check._host_frame(frame(2,3,case['hp'][0],direction=2),2,NONCE)

if __name__=='__main__':unittest.main(verbosity=2)
