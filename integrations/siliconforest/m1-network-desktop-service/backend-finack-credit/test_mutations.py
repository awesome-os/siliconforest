"""Compile and execute isolated Python policy mutants; never edit production files."""
from pathlib import Path
import io,json,sys,types,unittest
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;sys.path.insert(0,str(HERE))
source=(HERE/'model.py').read_text();tests=(HERE/'test_model.py').read_text()
mutations={
 'third_flow':('len(self.flows)>=MAX_FLOWS','len(self.flows)>MAX_FLOWS'),
 'expired_lease':("now<tx['expires']and dst in tx['addresses']","True and dst in tx['addresses']"),
 'old_60cap':('return min(ttls)','return min(ttls+[60])'),
 'CNAME_max_not_min':('return min(ttls)','return max(ttls)'),
 'query_count_expand':('len(self.queries)>=MAX_QUERY_PACKETS','len(self.queries)>MAX_QUERY_PACKETS'),
 'DNS_sharedport':('sp!=self.dns_port','False'),
 'DNS_changed_answers':("mode!=tx['mode']or identity!=tx['identity']","False"),
 'TTL_raise':('any(x>y for x,y in zip(a,b))','(False)'),
 'duplicate_refresh':("tx['groups']=groups;tx['reply_count']+=1", "tx['expires']=min(now+raw_chain_ttl(records),self.active_deadline);tx['groups']=groups;tx['reply_count']+=1"),
 'completion_uses_DNS_lease':('auth,self.active_deadline,self.clock','auth,tx[\'expires\'],self.clock'),
 'one_flow_complete':('len(self.flows)==MAX_FLOWS','len(self.flows)>=1'),
 'response_cap_expand':('MAX_RESPONSE=2048','MAX_RESPONSE=4096'),
 'old_idle30':('180,150,40','180,30,40'),
 'close_expand':('180,150,40','180,150,45'),
 'RST_without_client_FIN':('self.complete() and self.client_FIN_seen and','self.complete() and True and'),
 'RST_without_server_FIN_ACK':('and self.server_FIN_ACK_seen and (','and True and ('),
 'RST_without_later_ACK':('(self.final_ACK_after_server_FIN_ACK or self.repeated_client_FIN_after_server_FIN_ACK)','True'),
 'FIN_retry_no_prior_FIN':('if self.client_FIN_seen and self.server_FIN_ACK_seen:self.repeated','if True and self.server_FIN_ACK_seen:self.repeated'),
 'FIN_retry_no_source_FIN_ACK':('if self.client_FIN_seen and self.server_FIN_ACK_seen:self.repeated','if self.client_FIN_seen and True:self.repeated'),
 'FIN_retry_wrong_flags':('self.complete() and flags==0x11','self.complete() and True'),
 'FIN_retry_wrong_seq':('seq==(self.client_isn+1+len(self.request))&0xffffffff and','True and'),
 'FIN_retry_wrong_ACK':('ack==final_server):','True):'),
 'guest_RST_missing_closed_fact':('self.complete() and self.client_FIN_seen and self.server_FIN_ACK_seen and flags==4','True and self.client_FIN_seen and self.server_FIN_ACK_seen and flags==4'),
 'guest_RST_missing_prior_FIN':('self.complete() and self.client_FIN_seen and self.server_FIN_ACK_seen and flags==4','self.complete() and True and self.server_FIN_ACK_seen and flags==4'),
 'guest_RST_missing_source_ACK':('self.complete() and self.client_FIN_seen and self.server_FIN_ACK_seen and flags==4','self.complete() and self.client_FIN_seen and True and flags==4'),
 'guest_RST_flags':('self.server_FIN_ACK_seen and flags==4','self.server_FIN_ACK_seen and True'),
 'guest_RST_seq':('seq==(self.client_isn+2+len(self.request))&0xffffffff):','True):'),
 'guest_RST_ACK':('not payload and ack==0 and\n                    seq==','not payload and True and\n                    seq=='),
 'stateless_no_credit':("not e['peer_reply_consumed']and e['sequence']==ack","True and e['sequence']==ack"),
 'stateless_wrong_ACK':("not e['peer_reply_consumed']and e['sequence']==ack","not e['peer_reply_consumed']and True"),
 'stateless_credit_not_consumed':("credit['peer_reply_consumed']=True","credit['peer_reply_consumed']=False"),
 'stateless_seq':('not payload and seq==0 and credits','not payload and True and credits'),
 'stateless_flags':('if flags==0x14:','if flags&0x14==0x14:'),
 'explicit_storage129':('MAX_GUEST_TERMINAL_RESETS,MAX_STATELESS_RESETS,MAX_TOTAL_PEER_RESETS=127,127,128','MAX_GUEST_TERMINAL_RESETS,MAX_STATELESS_RESETS,MAX_TOTAL_PEER_RESETS=127,127,129'),
 'FINACK_credit_baseline_authorizes':("not e['baseline']and not e['guest_RST_consumed']","not e['guest_RST_consumed']"),
 'FINACK_credit_reuse':("not e['baseline']and not e['guest_RST_consumed']","not e['baseline']and True"),
 'commit_crc_ignored':("crc32c(frame[:48]+b'\\0'*4+frame[52:])!=crc","False"),
 'commit_replayed_sequence':('self.last_committed_host_frame_sequence<sequence<=127','sequence<=127'),
 'commit_missing_validation':('packet!=self.last_validated_host_packet','False'),
 'RST_early_HTTP':('self.complete() and self.client_FIN_seen','True and self.client_FIN_seen'),
 'RST_badflags':('flags==4 and','True and'),
 'RST_badack':('and ack==0 and seq==final_server','and True and seq==final_server'),
 'RST_badseq':('and seq==final_server and','and True and'),
 'RST_payload':('and not payload and ack==0','and True and ack==0'),
 'RST_unbounded':('len(self.terminal_reset_events)>=MAX_TERMINAL_RESETS','len(self.terminal_reset_events)>MAX_TERMINAL_RESETS'),
 'RST_newpayload':('(self.terminal_reset_events or self.guest_terminal_RST_events or self.peer_stateless_RST_events) and payload and start+len(payload)>self.fin_offset','False'),
}
def run(code,backend_code=None):
 module=types.ModuleType('model');module.__file__=str(HERE/'model.py');sys.modules['model']=module
 exec(compile(code,module.__file__,'exec'),module.__dict__)
 saved_backend=sys.modules.pop('backend',None)
 if backend_code is not None:
  bm=types.ModuleType('backend');bm.__file__=str(Path(__file__).resolve().parent/'backend.py');sys.modules['backend']=bm;exec(compile(backend_code,bm.__file__,'exec'),bm.__dict__)
 t=types.ModuleType('isolated_tests');t.__file__=str(HERE/'test_model.py');exec(compile(tests,t.__file__,'exec'),t.__dict__)
 result=unittest.TestResult();unittest.defaultTestLoader.loadTestsFromTestCase(t.Tests).run(result)
 sys.modules.pop('backend',None)
 if saved_backend is not None:sys.modules['backend']=saved_backend
 return {'tests':result.testsRun,'failures':len(result.failures),'errors':len(result.errors)}
def main():
 baseline=run(source);assert baseline['failures']==baseline['errors']==0,baseline
 results={}
 for name,(old,new)in mutations.items():
  assert old in source,name;result=run(source.replace(old,new,1));assert result['failures']or result['errors'],name
  results[name]=result
 backend_source=(Path(__file__).resolve().parent/'backend.py').read_text()
 cases={
  'backend_missing_commit':backend_source.replace('        self.policy.commit_forwarded(packet,encoded)\n',''),
  'backend_commit_before_queue':backend_source.replace('        self.client_out.append(frame)\n','        if kind==2:self.policy.commit_forwarded(payload,frame)\n        self.client_out.append(frame)\n').replace('        self.policy.commit_forwarded(packet,encoded)\n','')}
 for name,changed in cases.items():
  result=run(source,changed);assert result['failures']>0,name;results[name]=result
 return {'status':'PASS_COMPILED_PYTHON_MUTANTS_KILLED','baseline':baseline,'mutants':results,'compiled_and_executed':len(results),'production_source_unchanged':True}
if __name__=='__main__':print(json.dumps(main(),indent=2))
