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
 'RST_without_server_FIN_ACK':('and self.server_FIN_ACK_seen and self.final_ACK_after_server_FIN_ACK','and True and self.final_ACK_after_server_FIN_ACK'),
 'RST_without_later_ACK':('and self.final_ACK_after_server_FIN_ACK):','and True):'),
 'RST_early_HTTP':('self.complete() and self.client_FIN_seen','True and self.client_FIN_seen'),
 'RST_badflags':('flags==4 and','True and'),
 'RST_badack':('and ack==0 and seq==final_server','and True and seq==final_server'),
 'RST_badseq':('and seq==final_server and','and True and'),
 'RST_payload':('and not payload and ack==0','and True and ack==0'),
 'RST_unbounded':('len(self.terminal_reset_events)>=MAX_TERMINAL_RESETS','len(self.terminal_reset_events)>MAX_TERMINAL_RESETS'),
 'RST_newpayload':('self.terminal_reset_events and payload and start+len(payload)>self.fin_offset','False'),
}
def run(code):
 module=types.ModuleType('model');module.__file__=str(HERE/'model.py');sys.modules['model']=module
 exec(compile(code,module.__file__,'exec'),module.__dict__)
 t=types.ModuleType('isolated_tests');t.__file__=str(HERE/'test_model.py');exec(compile(tests,t.__file__,'exec'),t.__dict__)
 result=unittest.TestResult();unittest.defaultTestLoader.loadTestsFromTestCase(t.Tests).run(result)
 return {'tests':result.testsRun,'failures':len(result.failures),'errors':len(result.errors)}
def main():
 baseline=run(source);assert baseline['failures']==baseline['errors']==0,baseline
 results={}
 for name,(old,new)in mutations.items():
  assert old in source,name;result=run(source.replace(old,new,1));assert result['failures']or result['errors'],name
  results[name]=result
 return {'status':'PASS_COMPILED_PYTHON_MUTANTS_KILLED','baseline':baseline,'mutants':results,'compiled_and_executed':len(results),'production_source_unchanged':True}
if __name__=='__main__':print(json.dumps(main(),indent=2))
