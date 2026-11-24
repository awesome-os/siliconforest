"""Independent synthetic negatives plus CRC-checked actual Linux004 replay."""
import copy,hashlib,json,struct,sys,unittest
from pathlib import Path
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[5];sys.path.insert(0,str(HERE))
import tcp_reset_evidence as check
G=b'\x0a\0\x02\x0f';A=b'\x5d\xb8\xd8\x22';B=b'\x01\x01\x01\x01'
REQUEST=(b'GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: darwin-lab-egress/1\r\nAccept-Encoding: identity\r\nX-Darwin-Lab-Nonce: '+b'a'*48+b'\r\nConnection: close\r\n\r\n')
RESPONSE=b'HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 14\r\n\r\nExample Domain'
def checksum(raw):
 if len(raw)%2:raw+=b'\0'
 words=struct.unpack('!%dH'%(len(raw)//2),raw);n=sum(words)
 while n>>16:n=(n&65535)+(n>>16)
 return (~n)&65535
def tcp(src,dst,sp,dp,seq,ack,flags,payload=b'',reserved=0):
 options=b'\x02\x04\x05\xb4'if flags&2 else b''
 b=struct.pack('!HHIIBBHHH',sp,dp,seq&0xffffffff,ack&0xffffffff,((20+len(options))//4)<<4|reserved,flags,32768,0,0)+options+payload
 c=checksum(src+dst+b'\0\x06'+len(b).to_bytes(2,'big')+b);b=b[:16]+c.to_bytes(2,'big')+b[18:]
 h=struct.pack('!BBHHHBBH4s4s',0x45,0,20+len(b),1,0x4000,64,6,0,src,dst);return h[:10]+checksum(h).to_bytes(2,'big')+h[12:]+b
def fixture(reset=True):
 gp=[];hp=[];flows=[]
 for ordinal,(addr,port,cisn,sisn)in enumerate(((A,43002,10000,20000),(B,43003,30000,40000)),1):
  final=sisn+2+len(RESPONSE);cf=cisn+187
  gp.extend([tcp(G,addr,port,80,cisn,0,0xc2),tcp(G,addr,port,80,cisn+1,sisn+1,0x18,REQUEST),tcp(G,addr,port,80,cf,final,0x11),tcp(G,addr,port,80,cf+1,final,0x10)])
  hp.extend([tcp(addr,G,80,port,sisn,cisn+1,0x12),tcp(addr,G,80,port,sisn+1,cf,0x19,RESPONSE),tcp(addr,G,80,port,final,cf+1,0x10)])
  rst=tcp(addr,G,80,port,final,0,4)
  if reset:hp.append(rst)
  flows.append({'flow_id':ordinal,'local_ipv4':'10.0.2.15','local_port':port,'remote_ipv4':'.'.join(map(str,addr)),'remote_port':80,
   'client_ISN':cisn,'server_ISN':sisn,'initial_tcp_flags':194,'request_expected_hex':REQUEST.hex(),'request_observed_hex':REQUEST.hex(),'request_covered_bytes':186,
   'response_contiguous_bytes':len(RESPONSE),'response_highest_bytes':len(RESPONSE),'response_FIN_offset':len(RESPONSE),
   'HTTP':{'status':200,'body_bytes':14,'header_bytes':len(RESPONSE)-14},'wire_complete':True,'TCP_policy_revision':5,
   'repeated_client_FIN_after_server_FIN_ACK':False,'client_FIN_seen':reset,'server_FIN_ACK_seen':reset,'final_ACK_after_server_FIN_ACK':reset,'terminal_reset_count':int(reset),
   'terminal_reset_events':[dict(elapsed_seconds=4.5,packet_hex=rst.hex(),sequence=final,acknowledgement=0,flags=4,payload_bytes=0,policy_accepted=True,trigger='pure_final_ACK')]if reset else[]})
 return gp,hp,dict(profile='desktop-native-resolver-two-http-v1',first_error=None,backend_policy_revision=3,tcp_policy_revision=5,dns_policy_revision=2,
  parser_dependency_SHA256='20a0b6d5dbdff17b748f241623fc09989dd83b46957ab68862ae8799fecc6d8b',flows=flows,request_expected_hex=REQUEST.hex())
def crc32c(raw):
 crc=0xffffffff
 for byte in raw:
  crc^=byte
  for _ in range(8):crc=(crc>>1)^(0x82f63b78 if crc&1 else 0)
 return crc^0xffffffff
def relay_packets(raw,direction,nonce):
 offset=0;packets=[];sequence=1;opened=False;closed=False
 while offset<len(raw):
  assert not closed and len(raw)-offset>=56
  header=raw[offset:offset+56];magic,kind,d,reserved,size,hlen,seq,n,crc,tail=struct.unpack('!8sBBHHHQ24sII',header)
  assert magic==b'DLNET001'and d==direction and reserved==tail==0 and hlen==56 and seq==sequence and n==bytes.fromhex(nonce)and size<=1500
  frame=raw[offset:offset+56+size];assert len(frame)==56+size and crc32c(frame[:48]+b'\0'*4+frame[52:])==crc
  if not opened:assert kind==1 and size==0;opened=True
  elif kind==2:
   assert size>=20
   if frame[65]==6:packets.append(frame[56:])
  else:assert kind==3 and size==0;closed=True
  sequence+=1;offset+=56+size
 assert opened and closed and sequence<=129
 return packets
def actual_fin001():
 base=ROOT/'runs/m1-desktop-010/desktop-network-backend-fin-linux-001/pipeline';result=json.loads((base/'test-result.json').read_text());nonce=result['nonce']
 gp=relay_packets((base/'backend/relay-rx.bin').read_bytes(),1,nonce);hp=relay_packets((base/'backend/relay-tx.bin').read_bytes(),2,nonce)
 backend=json.loads((base/'backend/result.json').read_text());assert backend==result['closed_backend']
 return gp,hp,backend['egress']
def fin_fixture():
 gp,hp,e=fixture();new=[]
 for p in gp:
  if p[33]==0x10:continue
  new.append(p)
  if p[33]==0x11:new.append(p)
 for flow in e['flows']:
  flow['final_ACK_after_server_FIN_ACK']=False;flow['repeated_client_FIN_after_server_FIN_ACK']=True
  for event in flow['terminal_reset_events']:event['trigger']='repeated_client_FIN'
 return new,hp,e
class Tests(unittest.TestCase):
 def ok(self,inputs):
  r=check.terminal_reset_evidence(*inputs);self.assertTrue(all(r['checks'].values()),r);self.assertTrue(r['qualified_TCP5_terminal_reset_records_only']);return r
 def bad(self,inputs):
  r=check.terminal_reset_evidence(*inputs);self.assertEqual(tuple(r['checks']),check.CHECKS);self.assertFalse(r['qualified_TCP5_terminal_reset_records_only'],r);self.assertTrue(r['details']['errors']);return r
 def test_repeated_FIN_trigger_without_ACK_of_ACK(self):
  r=self.ok(fin_fixture());self.assertTrue(all(f['terminal_RST_triggers']==['repeated_client_FIN']for f in r['details']['flows']))
 def test_repeated_FIN_missing_prior_repeat_or_source_grant(self):
  gp,hp,e=fin_fixture();del gp[3];self.bad((gp,hp,e))
  for value in(False,None,1):
   gp,hp,e=fin_fixture();e['flows'][0]['repeated_client_FIN_after_server_FIN_ACK']=value;self.bad((gp,hp,e))
 def test_repeated_FIN_wrong_control_does_not_count(self):
  final=20002+len(RESPONSE)
  for seq,ack,flags in((10188,final,0x11),(10187,final-1,0x11),(10187,final,0x19)):
   gp,hp,e=fin_fixture();gp[3]=tcp(G,A,43002,80,seq,ack,flags);self.bad((gp,hp,e))
 def test_unknown_or_missing_trigger_refused(self):
  for value in(None,'FIN','',True):
   gp,hp,e=fin_fixture();e['flows'][0]['terminal_reset_events'][0]['trigger']=value;self.bad((gp,hp,e))
 def test_old_TCP4_version_and_missing_new_flag_are_not_upgraded(self):
  gp,hp,e=fin_fixture();e['backend_policy_revision']=2;e['tcp_policy_revision']=4
  for f in e['flows']:f['TCP_policy_revision']=4;del f['repeated_client_FIN_after_server_FIN_ACK']
  self.bad((gp,hp,e))
 def test_positive_original_synthetic(self):
  r=self.ok(fixture());self.assertFalse(r['details']['global_chronology_independently_observed']);self.assertEqual([x['terminal_RST_count']for x in r['details']['flows']],[1,1])
 def test_no_reset_needs_no_cross_direction_reset_grant(self):
  gp,hp,e=fixture(False);gp=[p for p in gp if p[33]!=0x10];hp=[p for p in hp if p[33]!=0x10];self.ok((gp,hp,e))
 def test_actual_FIN_fixture_independent_CRC_and_TCP(self):
  r=self.ok(actual_fin001());self.assertEqual([x['terminal_RST_count']for x in r['details']['flows']],[1,1]);self.assertEqual([x['request_bytes']for x in r['details']['flows']],[186,186])
 def test_input_missing_malformed_bounds(self):
  for gp,hp,e in((None,None,None),([],[],{}),([None],[b''],{}),([b'x']*127,[b'x'],{})):
   self.bad((gp,hp,e))
  for raw in(b'',b'\x45'*39,b'\x45'*1501):
   gp,hp,e=fixture();gp[0]=raw;self.bad((gp,hp,e))
 def test_checksums_and_reserved_fragments_protocol(self):
  self.assertEqual(check._checksum(bytes.fromhex('0001f203f4f5f6f7')),0x220d)
  for offset in(0,2,6,9,10,12,20,24,32,33,36):
   gp,hp,e=fixture();p=bytearray(gp[0]);p[offset]^=1;gp[0]=bytes(p);self.bad((gp,hp,e))
  gp,hp,e=fixture();gp[0]=tcp(G,A,43002,80,10000,0,0xc2,reserved=1);self.bad((gp,hp,e))
 def test_unknown_tuple_and_wrong_ISN(self):
  for field,value in(('remote_ipv4','8.8.8.8'),('local_port',43004),('remote_port',81),('client_ISN',10001),('server_ISN',20001),('flow_id',2),('initial_tcp_flags',2)):
   gp,hp,e=fixture();e['flows'][0][field]=value;self.bad((gp,hp,e))
  gp,hp,e=fixture();gp[0]=tcp(G,A,43002,80,10000,0,0x42);self.bad((gp,hp,e))
 def test_exact_GET_nonce_suffix_coverage(self):
  for payload in(REQUEST.replace(b'a'*48,b'b'*48),REQUEST+b'x',REQUEST[:-1]):
   gp,hp,e=fixture();gp[1]=tcp(G,A,43002,80,10001,20001,0x18,payload);self.bad((gp,hp,e))
  gp,hp,e=fixture();e['flows'][0]['request_covered_bytes']=True;self.bad((gp,hp,e))
 def test_response_changed_HTTP_FIN_overlap_extent(self):
  for payload in(RESPONSE.replace(b'200 OK',b'201 OK'),RESPONSE.replace(b'Example Domain',b'Xxample Domain'),RESPONSE+b'x',RESPONSE.replace(b'text/html',b'text/plain')):
   gp,hp,e=fixture();hp[1]=tcp(A,G,80,43002,20001,10187,0x19,payload);self.bad((gp,hp,e))
  gp,hp,e=fixture();hp[1]=tcp(A,G,80,43002,20001,10187,0x18,RESPONSE);self.bad((gp,hp,e))
  gp,hp,e=fixture();hp.insert(2,tcp(A,G,80,43002,20001,10187,0x18,b'X'));self.bad((gp,hp,e))
 def test_missing_each_raw_control(self):
  for side,index in(('g',2),('g',3),('h',2)):
   gp,hp,e=fixture();del(gp if side=='g'else hp)[index];self.bad((gp,hp,e))
 def test_directional_order_not_retroactive(self):
  gp,hp,e=fixture();gp[2],gp[3]=gp[3],gp[2];self.bad((gp,hp,e))
  gp,hp,e=fixture();hp[2],hp[3]=hp[3],hp[2];self.bad((gp,hp,e))
  gp,hp,e=fixture();hp[1],hp[3]=hp[3],hp[1];self.bad((gp,hp,e))
 def test_changed_control_sequences_and_ACKs_do_not_grant(self):
  final=20002+len(RESPONSE)
  for side,index,packet in(('g',2,tcp(G,A,43002,80,10187,final-1,0x11)),
    ('g',2,tcp(G,A,43002,80,10186,final,0x11)),('g',3,tcp(G,A,43002,80,10187,final,0x10)),
    ('g',3,tcp(G,A,43002,80,10188,final-1,0x10)),('h',2,tcp(A,G,80,43002,final,10187,0x10)),
    ('h',2,tcp(A,G,80,43002,final-1,10188,0x10))):
   gp,hp,e=fixture();(gp if side=='g'else hp)[index]=packet;self.bad((gp,hp,e))
  gp,hp,e=fixture();hp[1],hp[2]=hp[2],hp[1];self.bad((gp,hp,e))
 def test_duplicate_event_clocks_cannot_go_backwards(self):
  gp,hp,e=fixture();hp.insert(4,hp[3]);event=copy.deepcopy(e['flows'][0]['terminal_reset_events'][0]);event['elapsed_seconds']=3
  e['flows'][0]['terminal_reset_count']=2;e['flows'][0]['terminal_reset_events'].append(event);self.bad((gp,hp,e))
 def test_RST_flags_ack_seq_payload_tuple_reserved(self):
  final=20002+len(RESPONSE)
  for flags,seq,ack,payload,reserved in((0x14,final,0,b'',0),(5,final,0,b'',0),(4,final+1,0,b'',0),(4,final-1,0,b'',0),(4,final,1,b'',0),(4,final,0,b'x',0),(4,final,0,b'',1)):
   gp,hp,e=fixture();hp[3]=tcp(A,G,80,43002,seq,ack,flags,payload,reserved);self.bad((gp,hp,e))
 def test_bad_RST_with_forged_consistent_packet_record_still_refused(self):
  final=20002+len(RESPONSE)
  for flags,seq,ack,payload in((0x14,final,0,b''),(4,final+1,0,b''),(4,final,1,b''),(4,final,0,b'x')):
   gp,hp,e=fixture();hp[3]=tcp(A,G,80,43002,seq,ack,flags,payload);event=e['flows'][0]['terminal_reset_events'][0]
   event['packet_hex']=hp[3].hex();event['sequence']=seq;self.bad((gp,hp,e))
 def test_packet_count_and_origin_caps_cannot_be_forged(self):
  gp,hp,e=fixture();gp=gp+[gp[0]]*119;self.assertEqual(len(gp),127);self.bad((gp,hp,e))
  wrong=bytes([10,0,2,16]);gp,hp,e=fixture()
  def change_origin(packet,guest):
   src,dst=packet[12:16],packet[16:20];body=packet[20:];sp,dp,seq,ack=struct.unpack_from('!HHII',body);h=(body[12]>>4)*4
   return tcp(wrong if guest else src,dst if guest else wrong,sp,dp,seq,ack,body[13],body[h:])
  gp=[change_origin(p,True)for p in gp];hp=[change_origin(p,False)for p in hp]
  for flow,packet in zip(e['flows'],[p for p in hp if p[33]==4]):flow['terminal_reset_events'][0]['packet_hex']=packet.hex()
  self.bad((gp,hp,e))
 def test_RST_mapping_count_max4(self):
  gp,hp,e=fixture();rst=hp[3];event=e['flows'][0]['terminal_reset_events'][0]
  for count in(4,5):
   g2=copy.deepcopy(gp);h2=hp[:3]+[rst]*count+hp[4:];e2=copy.deepcopy(e);e2['flows'][0]['terminal_reset_count']=count;e2['flows'][0]['terminal_reset_events']=[copy.deepcopy(event)for _ in range(count)]
   (self.ok if count==4 else self.bad)((g2,h2,e2))
  for field,value in(('terminal_reset_count',0),('terminal_reset_count',True),('terminal_reset_events',[])):
   gp,hp,e=fixture();e['flows'][0][field]=value;self.bad((gp,hp,e))
 def test_RST_record_scalars_raw_clock_and_schema(self):
  for field,value in(('packet_hex','00'),('sequence',1),('acknowledgement',1),('flags',20),('payload_bytes',1),('policy_accepted',False),('elapsed_seconds',float('nan')),('elapsed_seconds',-1)):
   gp,hp,e=fixture();e['flows'][0]['terminal_reset_events'][0][field]=value;self.bad((gp,hp,e))
  gp,hp,e=fixture();e['flows'][0]['terminal_reset_events'][0]['forged']=1;self.bad((gp,hp,e))
 def test_each_source_bound_grant_required(self):
  for field in('client_FIN_seen','server_FIN_ACK_seen','final_ACK_after_server_FIN_ACK'):
   gp,hp,e=fixture();e['flows'][0][field]=False;self.bad((gp,hp,e))
  for field,value in(('backend_policy_revision',1),('tcp_policy_revision',3),('dns_policy_revision',1),('profile','old'),('parser_dependency_SHA256','00'),('first_error','bad')):
   gp,hp,e=fixture();e[field]=value;self.bad((gp,hp,e))
 def test_HTTP_framing_duplicates_and_chunked(self):
  for raw in(RESPONSE.replace(b'Content-Length: 14',b'Content-Length: 14\r\nContent-Length: 14'),RESPONSE.replace(b'Content-Length: 14',b'Transfer-Encoding: chunked\r\nContent-Length: 14')):
   with self.assertRaises(ValueError):check._http(raw)
  h=b'HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\n\r\n'
  self.assertEqual(check._http(h+b'e\r\nExample Domain\r\n0\r\n\r\n')['body_bytes'],14)
  for suffix in(b'e\r\nExample Domain\r\n0\r\nX\r\n',b'f\r\nExample Domain\r\n0\r\n\r\n'):
   with self.assertRaises(ValueError):check._http(h+suffix)
if __name__=='__main__':unittest.main()
