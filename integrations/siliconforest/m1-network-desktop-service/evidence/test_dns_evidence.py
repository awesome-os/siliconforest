"""Synthetic policy-produced fixtures; qualifier is independent of that model."""
from pathlib import Path
import copy,importlib.util,json,socket,sys,unittest
sys.dont_write_bytecode=True
H=Path(__file__).resolve().parent;sys.path.insert(0,str(H));sys.path.insert(0,str(H.parent/'backend'))
from dns_evidence import dns_evidence,raw_ttl,wire
from test_model import query,reply,rr,A,B
from model import Policy,GUEST,DNS
from packet_fixture import udp,tcp
class Tests(unittest.TestCase):
 def fixture(self,renew=False):
  clock=[1000.];p=Policy('a'*48,clock=lambda:clock[0]);p.open();qs=[];rs=[]
  def q(i):
   packet=udp(GUEST,DNS,43001,53,query(i));p.outgoing(packet);qs.append(packet)
  def r(i,records):
   packet=udp(DNS,GUEST,53,43001,reply(i,records));p.incoming(packet);rs.append(packet)
  clock[0]=1001.;q(1);clock[0]=1002.;r(1,[rr(A,300),rr(B,300)])
  clock[0]=1003.;q(1);clock[0]=1004.;r(1,[rr(B,299),rr(A,299)])
  p.outgoing(tcp(GUEST,A,43002,80,10000,0,0xc2))
  if renew:
   clock[0]=1065.;q(2);clock[0]=1066.;r(2,[rr(A,20),rr(B,20)])
  clock[0]=1067.;p.outgoing(tcp(GUEST,B,43003,80,30000,0,2))
  return qs,rs,p.summary()
 def test_success_raw300_not_old60_and_permutation(self):
  q,r,e=self.fixture();a=dns_evidence(q,r,e);self.assertTrue(a['qualified_DNS_lease_records_only'],a);self.assertEqual(a['details']['transactions'][0]['raw_TTL'],300);self.assertEqual(a['details']['transactions'][0]['original_expiry_elapsed'],180)
 def test_actual_new_query_renewal_independent_id(self):
  q,r,e=self.fixture(True);a=dns_evidence(q,r,e);self.assertTrue(a['qualified_DNS_lease_records_only'],a);self.assertEqual(len(a['details']['transactions']),2)
 def test_thirty_backend_fact_perturbations(self):
  q,r,base=self.fixture();mutations=[]
  for key,value in [('query_count',True),('query_count',3),('DNS_source_port',43002),('created_clock',True),('created_clock',float('nan')),('active_deadline',1181.),('absolute_deadline',1419.),('profile','other'),('first_error','RST'),('parser_dependency_SHA256','0'*64)]:mutations.append(lambda e,k=key,v=value:e.__setitem__(k,v))
  for key,value in [('source_port',43002),('transaction_id',2),('query_hex','00'),('elapsed_seconds',True)]:mutations.append(lambda e,k=key,v=value:e['query_events'][0].__setitem__(k,v))
  for key,value in [('accepted',1),('duplicate',True),('elapsed_seconds',50.),('sorted_TTL_groups',[[1]]),('RRs',[]),('original_raw_chain_TTL',60),('original_lease_expires_elapsed_seconds',181.)]:mutations.append(lambda e,k=key,v=value:e['reply_events'][0].__setitem__(k,v))
  for key,value in [('original_expiry_elapsed',62.),('first_query_elapsed',2.),('first_reply_elapsed',3.),('first_A_order',['1.1.1.1']),('original_raw_TTL',60),('transaction_id',2)]:mutations.append(lambda e,k=key,v=value:e['leases'][0].__setitem__(k,v))
  for key,value in [('admitted_at',1200.),('original_lease_expires_at',1181.),('first_canonical_answer_index',True),('transaction_id',9)]:mutations.append(lambda e,k=key,v=value:e['flows'][0]['authorization'].__setitem__(k,v))
  for mutate in mutations:
   e=copy.deepcopy(base);mutate(e);a=dns_evidence(q,r,e);self.assertFalse(a['qualified_DNS_lease_records_only'],a)
 def test_wire_packet_and_duplicate_faults(self):
  q,r,e=self.fixture()
  for qq,rrr in [([],r),(q,[]),(q*3,r),(q,r*5),([q[0][:-1]],r),(q,[r[0][:-1]]),(q,[r[0],udp(DNS,GUEST,53,43001,reply(1,[rr(A,301),rr(B,301)]))])]:self.assertFalse(dns_evidence(qq,rrr,e)['qualified_DNS_lease_records_only'])
 def test_malformed_shapes_never_crash_or_qualify(self):
  q,r,e=self.fixture()
  for value in [None,False,[],{},0,'x']:
   a=dns_evidence(q,r,value);self.assertFalse(a['qualified_DNS_lease_records_only'])
  for key in ['leases','flows','query_events','reply_events']:
   for v in [None,False,[1],[None],{}]:
    x=copy.deepcopy(e);x[key]=v;self.assertFalse(dns_evidence(q,r,x)['qualified_DNS_lease_records_only'])
 def test_raw_chain_cname_ttl_no_profile_cap(self):
  q=query(4);cname=rr(b'\x04edge\x07example\x03com\0',85,kind=5);a=rr(A,200,owner=b'\x04edge\x07example\x03com\0');p=wire.parse_dns_reply(reply(4,[cname,a]),q);self.assertEqual(p['lease_seconds'],60);self.assertEqual(raw_ttl(p),85)
if __name__=='__main__':unittest.main()
