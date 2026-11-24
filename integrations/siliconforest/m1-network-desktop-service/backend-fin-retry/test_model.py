"""Synthetic packet/state tests, never actual API/PID/network/desktop evidence."""
from pathlib import Path
import socket
import struct
import sys
import unittest
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from model import Policy,GUEST,DNS,QUESTION,raw_chain_ttl
from packet_fixture import udp,tcp
NONCE='a'*48
A=socket.inet_aton('93.184.216.34');B=socket.inet_aton('1.1.1.1')
RESPONSE=b'HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 14\r\n\r\nExample Domain'
def query(ident=1):return struct.pack('!6H',ident,0x100,1,0,0,0)+QUESTION
def rr(address=A,ttl=300,owner=b'\xc0\x0c',kind=1):return owner+struct.pack('!HHIH',kind,1,ttl,len(address))+address
def reply(ident=1,records=None):
 records=[rr()]if records is None else records
 return struct.pack('!6H',ident,0x8180,1,len(records),0,0)+QUESTION+b''.join(records)
class Tests(unittest.TestCase):
 def policy(self):
  self.t=[0.];p=Policy(NONCE,clock=lambda:self.t[0]);p.open();return p
 def q(self,p,ident=1,port=43001):p.outgoing(udp(GUEST,DNS,port,53,query(ident)))
 def r(self,p,ident=1,records=None,port=43001):p.incoming(udp(DNS,GUEST,53,port,reply(ident,records)))
 def authorized(self,ttl=300,addresses=None):
  p=self.policy();self.q(p);self.r(p,records=[rr(x,ttl)for x in(addresses or[A])]);return p
 def syn(self,p,port=43002,address=A,isn=10000,sisn=20000):
  p.outgoing(tcp(GUEST,address,port,80,isn,0,0xc2));p.incoming(tcp(address,GUEST,80,port,sisn,isn+1,0x12))
 def finish(self,p,port=43002,address=A,isn=10000,sisn=20000):
  p.outgoing(tcp(GUEST,address,port,80,isn+1,sisn+1,0x18,p.request))
  p.incoming(tcp(address,GUEST,80,port,sisn+1,isn+187,0x19,RESPONSE))
 def terminal(self):
  p=self.authorized();self.syn(p);self.finish(p);key=(43002,A,80);flow=p.flows[key]
  fs=20000+2+len(RESPONSE);fc=10000+1+186
  p.outgoing(tcp(GUEST,A,43002,80,fc,fs,0x11))
  p.incoming(tcp(A,GUEST,80,43002,fs,fc+1,0x10))
  p.outgoing(tcp(GUEST,A,43002,80,fc+1,fs,0x10))
  return p,flow,fs,fc
 def fin_retry_terminal(self):
  p=self.authorized();self.syn(p);self.finish(p);f=p.flows[(43002,A,80)];fs=20002+len(RESPONSE);fc=10187
  p.outgoing(tcp(GUEST,A,43002,80,fc,fs,0x11));self.assertFalse(f.repeated_client_FIN_after_server_FIN_ACK)
  p.incoming(tcp(A,GUEST,80,43002,fs,fc+1,0x10));p.outgoing(tcp(GUEST,A,43002,80,fc,fs,0x11))
  return p,f,fs,fc
 def test_exact_FIN_retry_alternate_not_ACK_of_ACK(self):
  p,f,fs,fc=self.fin_retry_terminal();self.assertFalse(f.final_ACK_after_server_FIN_ACK);self.assertTrue(f.repeated_client_FIN_after_server_FIN_ACK)
  p.incoming(tcp(A,GUEST,80,43002,fs,0,4));self.assertEqual(f.terminal_reset_events[0]['trigger'],'repeated_client_FIN')
 def test_FIN_retry_requires_prior_FIN_and_source_observed_FINACK(self):
  p=self.authorized();self.syn(p);self.finish(p);f=p.flows[(43002,A,80)];fs=20002+len(RESPONSE);fc=10187
  f.server_FIN_ACK_seen=True;p.outgoing(tcp(GUEST,A,43002,80,fc,fs,0x11));self.assertFalse(f.repeated_client_FIN_after_server_FIN_ACK)
  f.server_FIN_ACK_seen=False;p.outgoing(tcp(GUEST,A,43002,80,fc,fs,0x11));self.assertFalse(f.repeated_client_FIN_after_server_FIN_ACK)
  p.incoming(tcp(A,GUEST,80,43002,fs,fc+1,0x10));self.assertFalse(f.repeated_client_FIN_after_server_FIN_ACK)
  with self.assertRaisesRegex(ValueError,'outside exact'):p.incoming(tcp(A,GUEST,80,43002,fs,0,4))
 def test_FIN_retry_wrong_control_never_grants(self):
  for delta,ack_delta,flags in((1,0,0x11),(-1,0,0x11),(0,1,0x11),(0,-1,0x11),(0,0,0x19),(0,0,0x10)):
   p=self.authorized();self.syn(p);self.finish(p);f=p.flows[(43002,A,80)];fs=20002+len(RESPONSE);fc=10187
   p.outgoing(tcp(GUEST,A,43002,80,fc,fs,0x11));p.incoming(tcp(A,GUEST,80,43002,fs,fc+1,0x10))
   p.outgoing(tcp(GUEST,A,43002,80,fc+delta,fs+ack_delta,flags));self.assertFalse(f.repeated_client_FIN_after_server_FIN_ACK)
 def test_FIN_retry_missing_recorded_trigger_still_rejects(self):
  p,f,fs,fc=self.fin_retry_terminal();f.repeated_client_FIN_after_server_FIN_ACK=False
  with self.assertRaisesRegex(ValueError,'outside exact'):p.incoming(tcp(A,GUEST,80,43002,fs,0,4))
 def test_exact_terminal_reset_is_recorded_after_all_three_fin_steps(self):
  p,f,fs,fc=self.terminal()
  for _ in range(4):p.incoming(tcp(A,GUEST,80,43002,fs,0,4))
  self.assertEqual(len(f.terminal_reset_events),4);self.assertTrue(f.complete());self.assertEqual(f.terminal_reset_events[0]['trigger'],'pure_final_ACK')
  self.assertEqual(bytes.fromhex(f.terminal_reset_events[0]['packet_hex']),tcp(A,GUEST,80,43002,fs,0,4))
  with self.assertRaisesRegex(ValueError,'count4'):p.incoming(tcp(A,GUEST,80,43002,fs,0,4))
 def test_terminal_reset_missing_each_prerequisite_refused(self):
  for field in ('client_FIN_seen','server_FIN_ACK_seen','final_ACK_after_server_FIN_ACK'):
   p,f,fs,fc=self.terminal();setattr(f,field,False)
   with self.assertRaisesRegex(ValueError,'outside exact'):p.incoming(tcp(A,GUEST,80,43002,fs,0,4))
  for field,value in (('fin_offset',None),('http',None),('coverage',bytearray(186))):
   p,f,fs,fc=self.terminal();setattr(f,field,value)
   with self.assertRaisesRegex(ValueError,'outside exact'):p.incoming(tcp(A,GUEST,80,43002,fs,0,4))
 def test_terminal_reset_wrong_flags_seq_ack_payload_tuple_refused(self):
  for flags,delta,ack,payload in ((0x14,0,0,b''),(5,0,0,b''),(4,1,0,b''),(4,-1,0,b''),(4,0,1,b''),(4,0,0,b'x')):
   p,f,fs,fc=self.terminal()
   with self.assertRaises(ValueError):p.incoming(tcp(A,GUEST,80,43002,fs+delta,ack,flags,payload))
  p,f,fs,fc=self.terminal()
  with self.assertRaises(ValueError):p.incoming(tcp(A,GUEST,80,43003,fs,0,4))
 def test_wrong_fin_and_ack_cannot_establish_terminal_prerequisites(self):
  p=self.authorized();self.syn(p);self.finish(p);f=p.flows[(43002,A,80)];fs=20002+len(RESPONSE);fc=10187
  p.outgoing(tcp(GUEST,A,43002,80,fc,fs-1,0x11));self.assertFalse(f.client_FIN_seen)
  p.outgoing(tcp(GUEST,A,43002,80,fc,fs,0x11))
  p.incoming(tcp(A,GUEST,80,43002,fs,fc,0x10));self.assertFalse(f.server_FIN_ACK_seen)
  p.outgoing(tcp(GUEST,A,43002,80,fc+1,fs,0x10));self.assertFalse(f.final_ACK_after_server_FIN_ACK)
  p.incoming(tcp(A,GUEST,80,43002,fs,fc+1,0x10));self.assertTrue(f.server_FIN_ACK_seen)
  self.assertFalse(f.final_ACK_after_server_FIN_ACK)
 def test_terminal_rst_does_not_create_new_flow_or_payload_authority(self):
  p,f,fs,fc=self.terminal();p.incoming(tcp(A,GUEST,80,43002,fs,0,4));self.assertEqual(len(p.flows),1)
  with self.assertRaises(ValueError):p.incoming(tcp(A,GUEST,80,43002,fs,fc+1,0x18,b'x'))
 def test_duplicate_short_lease_never_refreshes_original_expiry(self):
  p=self.authorized(5);self.t[0]=2;self.r(p,records=[rr(A,4)]);self.assertEqual(p.transactions[1]['expires'],5)
 def test_established_completion_deadline_is_whole_not_DNS(self):
  p=self.authorized(2);self.syn(p);self.assertEqual(p.flows[(43002,A,80)].expires,180)
 def test_fixed_profile_bounds(self):
  import model
  self.assertEqual((model.ABSOLUTE_SECONDS,model.ACCEPT_SECONDS,model.ACTIVE_SECONDS,model.QUIET_SECONDS,model.CLOSE_SECONDS),(420,240,180,150,40))
  self.assertEqual((model.MAX_QUERY_PACKETS,model.MAX_FLOWS,model.MAX_PACKETS,model.MAX_PACKET_BYTES,model.MAX_RESPONSE,model.MAX_TERMINAL_RESETS),(4,2,126,65536,2048,4))
 def test_uncapped_raw_TTL_allows_real_cache_after60(self):
  p=self.authorized();self.assertEqual(p.transactions[1]['raw_ttl'],300);self.assertEqual(p.transactions[1]['expires'],180)
  self.t[0]=90;self.syn(p);self.assertEqual(len(p.queries),1)
 def test_raw_TTL_and_CNAME_minimum(self):
  p=self.policy();self.q(p);alias=b'\x03www\x07example\x03com\0'
  self.r(p,records=[rr(alias,45,kind=5),rr(A,300,owner=alias)])
  self.assertEqual(p.transactions[1]['raw_ttl'],45);self.assertEqual(p.transactions[1]['expires'],45)
 def test_expired_cache_cannot_authorize_newflow(self):
  p=self.authorized(2);self.t[0]=2
  with self.assertRaisesRegex(ValueError,'unexpired'):self.syn(p)
 def test_actual_newID_renews_without_extending_oldID(self):
  p=self.authorized(2);old=p.transactions[1]['expires'];self.t[0]=3
  self.q(p,2);self.r(p,2,[rr(B,100)]);self.syn(p,address=B)
  self.assertEqual(p.transactions[1]['expires'],old);self.assertEqual(p.flows[(43002,B,80)].authorization['transaction_id'],2)
 def test_established_flow_can_finish_after_DNSexpiry(self):
  p=self.authorized(2);self.syn(p);self.t[0]=3;self.finish(p)
  self.assertTrue(p.flows[(43002,A,80)].complete())
  with self.assertRaisesRegex(ValueError,'unexpired'):self.syn(p,43003)
 def test_two_flows_independent_order_and_response_reassembly(self):
  p=self.authorized();self.syn(p,43003,isn=30000,sisn=40000);self.syn(p)
  self.finish(p);self.assertFalse(p.complete());self.finish(p,43003,isn=30000,sisn=40000)
  self.assertTrue(p.complete());self.assertEqual([f['local_port']for f in p.summary()['flows']],[43003,43002])
  self.assertTrue(all(f['request_covered_bytes']==186 for f in p.summary()['flows']))
 def test_full_tuple_not_sourceport_alone(self):
  p=self.authorized(addresses=[A,B]);self.syn(p,address=A);self.syn(p,address=B,isn=30000,sisn=40000)
  self.assertEqual(len(p.flows),2)
 def test_third_flow_and_newISN_for_oldtuple_refused(self):
  p=self.authorized();self.syn(p);self.syn(p,43003,isn=30000,sisn=40000)
  with self.assertRaisesRegex(ValueError,'two TCP flow'):self.syn(p,43004,isn=50000,sisn=60000)
  p=self.authorized();self.syn(p)
  with self.assertRaisesRegex(ValueError,'SYN changed'):p.outgoing(tcp(GUEST,A,43002,80,99999,0,2))
 def test_samesession_exactGET_bothflows_no_suffix_or_othernonce(self):
  p=self.authorized();self.syn(p)
  with self.assertRaises(ValueError):p.outgoing(tcp(GUEST,A,43002,80,10001,20001,0x18,p.request.replace(b'a'*48,b'b'*48)))
  p=self.authorized();self.syn(p)
  with self.assertRaises(ValueError):p.outgoing(tcp(GUEST,A,43002,80,10001,20001,0x18,p.request+b'x'))
 def test_perflow_response2048_cap(self):
  p=self.authorized();self.syn(p)
  with self.assertRaisesRegex(ValueError,'2048'):p.incoming(tcp(A,GUEST,80,43002,22001,10001,0x18,b'x'*49))
 def test_four_query_packets_include_three_retries(self):
  p=self.policy()
  for i in range(4):self.t[0]=i;self.q(p)
  self.assertEqual(len(p.transactions),1)
  with self.assertRaisesRegex(ValueError,'four actual'):self.q(p)
 def test_newIDs_sticky_sharedport_and_reply_ID_correlation(self):
  p=self.policy();self.q(p)
  with self.assertRaisesRegex(ValueError,'shared port'):self.q(p,2,43002)
  p=self.policy();self.q(p)
  with self.assertRaisesRegex(ValueError,'correlation'):self.r(p,2)
 def test_expiredID_reuse_is_not_new_lease(self):
  p=self.authorized(2);self.t[0]=3
  with self.assertRaisesRegex(ValueError,'reused'):self.q(p)
 def test_duplicateTTL_rotation_does_not_extend_firstlease(self):
  p=self.authorized(addresses=[A,B]);expires=p.transactions[1]['expires'];self.t[0]=1
  self.q(p);self.r(p,records=[rr(B,299),rr(A,299)])
  self.assertEqual(p.transactions[1]['expires'],expires);self.assertEqual(p.transactions[1]['addresses'],[A,B])
  with self.assertRaisesRegex(ValueError,'TTL increased'):self.r(p,records=[rr(B,300),rr(A,299)])
 def test_duplicate_budget_and_changed_A_refused(self):
  p=self.authorized();self.r(p);self.r(p)
  with self.assertRaisesRegex(ValueError,'duplicate budget'):self.r(p)
  p=self.authorized()
  with self.assertRaisesRegex(ValueError,'identity'):self.r(p,records=[rr(B,299)])
 def test_private_or_unsupported_origins_questions_servers_refused(self):
  for packet in (udp(B,DNS,43001,53,query()),udp(GUEST,B,43001,53,query()),
      udp(GUEST,DNS,43001,53,query().replace(b'example',b'EXAMPLE'))):
   p=self.policy()
   with self.assertRaises(ValueError):p.outgoing(packet)
  p=self.policy();self.q(p)
  with self.assertRaises(ValueError):self.r(p,records=[rr(socket.inet_aton('10.0.2.2'))])
 def test_quiet_desktop120_does_not_trip_old30(self):
  p=self.policy();self.t[0]=120;p.check_limits();self.t[0]=150;p.check_limits();self.t[0]=150.001
  with self.assertRaisesRegex(ValueError,'quiet150'):p.check_limits()
 def test_absolute_accept_active_and_close_are_finite(self):
  self.t=[0.];p=Policy(NONCE,clock=lambda:self.t[0]);self.t[0]=240
  with self.assertRaisesRegex(ValueError,'accept240'):p.open()
  self.t=[0.];p=Policy(NONCE,clock=lambda:self.t[0]);self.t[0]=239;p.open();self.assertEqual(p.active_deadline,419)
  self.t[0]=420
  with self.assertRaisesRegex(ValueError,'absolute420'):p.check_limits()
  p=self.policy();self.t[0]=180
  with self.assertRaisesRegex(ValueError,'active180'):p.check_limits()
  p=self.policy();p.begin_close();self.t[0]=40.001
  with self.assertRaisesRegex(ValueError,'CLOSE40'):p.check_limits()
 def test_error_sticky_and_no_success_after_deadline(self):
  p=self.authorized();self.syn(p);self.finish(p);self.syn(p,43003,isn=30000,sisn=40000);self.finish(p,43003,isn=30000,sisn=40000)
  self.assertTrue(p.complete());self.t[0]=181;self.assertFalse(p.complete())
  p=self.policy()
  with self.assertRaises(ValueError):p.outgoing(None)
  with self.assertRaises(ValueError):self.q(p)
  self.assertFalse(p.complete())
 def test_no_success_without_both_requests_and_actualFIN(self):
  p=self.authorized();self.syn(p);self.finish(p);self.assertFalse(p.complete())
  self.syn(p,43003,isn=30000,sisn=40000)
  p.outgoing(tcp(GUEST,A,43003,80,30001,40001,0x18,p.request));p.incoming(tcp(A,GUEST,80,43003,40001,30187,0x18,RESPONSE))
  self.assertFalse(p.complete())
if __name__=='__main__':unittest.main()
