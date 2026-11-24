"""Compile/execute independent evidence mutants without touching frozen sources."""
from pathlib import Path
import json,sys,types,unittest
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;sys.path.insert(0,str(HERE))
SOURCE=(HERE/'tcp_reset_evidence.py').read_text();TESTS=(HERE/'test_tcp_reset_evidence.py').read_text()
MUTANTS={
 'packet_count_expansion':[('<=126','<=127')],
 'skip_IP_checksum':[('_checksum(raw[:20])==0','True')],
 'skip_TCP_checksum':[('_checksum(pseudo+body)==0','True')],
 'TCP_reserved_NS_bits':[('and body[12]&15==0','')],
 'wrong_guest_origin':[('local==GUEST and','True and')],
 'missing_GET_coverage': [('all(coverage)','True')],
 'wrong_GET_bytes':[("p['payload']==expected[start:start+len(p['payload'])]","True")],
 'HTTP_without_example':[("b'Example Domain'in body","True")],
 'guest_control_direction_order':[("any(fi<ai for fi,_ in s['client_FINs']for ai,_ in s['final_ACKs'])","True")],
 'host_control_direction_order':[("any(fi<ai<ri for fi in s['server_FIN_indices']for ai,_ in s['server_FIN_ACKs'])","True")],
 'RST_wrong_sequence':[("p['ack']==0 and p['seq']==s['final_server']","p['ack']==0 and True")],
 'RST_wrong_ACK':[("p['ack']==0 and p['seq']==s['final_server']","True and p['seq']==s['final_server']")],
 'RST_payload':[("need(not p['payload']and p['ack']==0","need(True and p['ack']==0")],
 'RST_budget_expansion':[('and count<=8','and count<=9'),('len(events)<=8','len(events)<=9')],
 'RST_raw_mapping':[("event['packet_hex']==p['raw'].hex()and","True and")],
 'RST_event_scalar_mapping':[("type(event[field])is int and event[field]==value","True")],
 'RST_nonfinite_clock':[("type(value)in(int,float)and math.isfinite(value)and value>=0","True")],
 'producer_FIN_grant':[("s['record'].get(field)is True","True")],
 'missing_prior_repeated_FIN':[("len(s['client_FINs'])>=2","len(s['client_FINs'])>=1"),("any(fi<ri for fi,_ in s['client_FINs']for ri,_ in s['client_FINs'])","True")],
 'missing_alternate_source_grant':[("s['record'].get(field)is True,'source-bound exact alternate grant '","True,'source-bound exact alternate grant '")],
 'credit_reuse':[("credit in credits and credit not in used","credit in credits and True")],
 'credit_consumed_flag':[("event['peer_reply_consumed']is(credit in used)","True")],
 'guest_credit_id':[("type(event['credit_id'])is int and event['credit_id']==ordinal","True")],
 'source_credit_clock_order':[("t>=source['elapsed_seconds']","True")],
 'guest_raw_correspondence':[("event['packet_hex']==p['raw'].hex()and event['policy_accepted']is True and type(event['peer_reply_consumed'])is bool","True and event['policy_accepted']is True and type(event['peer_reply_consumed'])is bool")],
 'peer_raw_correspondence':[("event['packet_hex']==p['raw'].hex()and event['policy_accepted']is True and p['ack']==guest['seq']","True and event['policy_accepted']is True and p['ack']==guest['seq']")],
 'terminal_declared_budgets':[("record.get('terminal_budgets')==dict(bare_peer=8,guest=4,stateless_peer=4,total_peer=12)","True")],
 'guest_and_stateless_budgets5':[("len(guests)==len(guest_events)<=4 and len(peers)==len(peer_events)<=4","len(guests)==len(guest_events)<=5 and len(peers)==len(peer_events)<=5")],
 'producer_revision_binding':[("type(egress.get(field))is int and egress[field]==value","True")],
}
def execute(source):
 m=types.ModuleType('tcp_reset_evidence');m.__file__=str(HERE/'tcp_reset_evidence.py');sys.modules['tcp_reset_evidence']=m
 exec(compile(source,m.__file__,'exec'),m.__dict__)
 t=types.ModuleType('isolated_TCP6_tests');t.__file__=str(HERE/'test_tcp_reset_evidence.py');exec(compile(TESTS,t.__file__,'exec'),t.__dict__)
 result=unittest.TestResult();unittest.defaultTestLoader.loadTestsFromTestCase(t.Tests).run(result)
 return dict(tests=result.testsRun,failures=len(result.failures),errors=len(result.errors))
def run():
 baseline=execute(SOURCE);assert baseline['failures']==baseline['errors']==0,baseline;results={}
 for name,changes in MUTANTS.items():
  changed=SOURCE
  for old,new in changes:assert old in changed,name;changed=changed.replace(old,new)
  result=execute(changed);assert result['failures']>0,name;results[name]=result
 return dict(status='PASS_ALL_INDEPENDENT_TCP6_PYTHON_MUTANTS_KILLED',baseline=baseline,compiled_and_executed=len(results),mutants=results,all_killed_by_assertions=True,production_source_imported=False)
if __name__=='__main__':print(json.dumps(run(),indent=2))
