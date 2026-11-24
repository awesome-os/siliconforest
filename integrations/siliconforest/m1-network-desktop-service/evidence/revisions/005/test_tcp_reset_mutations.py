"""Compile and execute targeted TCP7 decision mutants; no producer/target APIs."""
from pathlib import Path
import importlib.util
import io
import json
import sys
import types
import unittest
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent

def load_tests():
 spec=importlib.util.spec_from_file_location('TCP7_mutation_corpus',HERE/'test_tcp_reset_evidence.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def run():
 source=(HERE/'tcp_reset_evidence.py').read_text();tests=load_tests();original=tests.check
 # Each mutant preserves a real fifth-credit golden before its negative corpus.
 cases=[
 ('wire_direction',"direction==expected_direction","True",'test_crc_known_vector_and_raw_header_negatives'),
 ('wire_sequence',"sequence==ordinal and session.hex()==nonce","True and session.hex()==nonce",'test_crc_known_vector_and_raw_header_negatives'),
 ('wire_nonce',"session.hex()==nonce","True",'test_fullstream_empty_truncated_wrongdirection_and_nonce'),
 ('wire_CRC',"_crc32c(raw[:48]+b'\\0\\0\\0\\0'+raw[52:])==crc","True",'test_crc_known_vector_and_raw_header_negatives'),
 ('one_emission_per_actual_frame',"len(emissions)==len(s['server_FIN_ACKs'])<=128","True",'test_duplicate_reordered_missing_and_uncommitted_emissions'),
 ('emission_packet_hex',"event['packet_hex']==packet['raw'].hex()and event['packet_sha256']==frame['packet_sha256']","True and event['packet_sha256']==frame['packet_sha256']",'test_emission_identity_fields_and_exact_schema'),
 ('emission_packet_SHA',"event['packet_hex']==packet['raw'].hex()and event['packet_sha256']==frame['packet_sha256']","event['packet_hex']==packet['raw'].hex()and True",'test_emission_identity_fields_and_exact_schema'),
 ('emission_frame_scalar_binding',"type(event[key])is int and event[key]==value","True",'test_emission_identity_fields_and_exact_schema'),
 ('emission_frame_SHA',"event['frame_sha256']==frame['frame_sha256']","(True)",'test_baseline_raw_frame_hash_and_source_scope_required'),
 ('first_baseline_only',"event['baseline']is(ordinal==1)","True",'test_exhausted_sixth_and_baseline_cannot_grant'),
 ('baseline_no_credit',"event['credit_id']is None","True",'test_exhausted_sixth_and_baseline_cannot_grant'),
 ('baseline_never_consumed',"event['guest_RST_consumed']is False","True",'test_exhausted_sixth_and_baseline_cannot_grant'),
 ('numbered_emission_credit',"type(event['credit_id'])is int and event['credit_id']==ordinal-1","True",'test_emission_identity_fields_and_exact_schema'),
 ('FINACK_FIFO',"credit==ordinal and credit in credits and credit not in used_FINACK","credit in credits and credit not in used_FINACK",'test_coherent_FIFO_swaps_and_reuses_cannot_hide_in_balances'),
 ('FINACK_FIFO_and_singleuse',"credit==ordinal and credit in credits and credit not in used_FINACK","credit in credits",'test_coherent_FIFO_swaps_and_reuses_cannot_hide_in_balances'),
 ('peer_FIFO',"credit==ordinal and credit in guest_credits and credit not in used_guest","credit in guest_credits and credit not in used_guest",'test_coherent_FIFO_swaps_and_reuses_cannot_hide_in_balances'),
 ('peer_FIFO_and_singleuse',"credit==ordinal and credit in guest_credits and credit not in used_guest","credit in guest_credits",'test_coherent_FIFO_swaps_and_reuses_cannot_hide_in_balances'),
 ('source_commit_before_consume',"elapsed>=source['elapsed_seconds']","True",'test_source_commit_consume_clock_order'),
 ('guest_frame_sequence',"event['finACK_frame_sequence']==source['frame_sequence']","True",'test_guest_event_raw_scalar_and_frame_link_mutants'),
 ('guest_frame_SHA',"event['finACK_frame_sha256']==source['frame_sha256']","True",'test_guest_event_raw_scalar_and_frame_link_mutants'),
 ('FINACK_consumption_flag',"event['guest_RST_consumed']is(credit in used_FINACK)","True",'test_balance_counts_and_consumed_flags'),
 ('guest_consumption_flag',"event['peer_reply_consumed']is(credit in used_guest)","True",'test_balance_counts_and_consumed_flags'),
 ('credit_balance',"type(record.get(field))is int and record[field]==value,'source credit balance '","True,'source credit balance '",'test_balance_counts_and_consumed_flags'),
 ('normal20_FINACK',"p['flags']==0x10 and p['header']==20 and not p['payload']and p['seq']==s['final_server']","p['flags']==0x10 and not p['payload']and p['seq']==s['final_server']",'test_normal20_FINACK_is_eligibility_boundary'),
 ('crossdirection_source_grants',"s['record'].get(field)is True,'source-bound cross-direction grant '","True,'source-bound cross-direction grant '",'test_baseline_raw_frame_hash_and_source_scope_required'),
 ('perflow_policy_version',"type(s['record'].get('TCP_policy_revision'))is int and s['record']['TCP_policy_revision']==7","True",'test_baseline_raw_frame_hash_and_source_scope_required'),
 ('global_policy_versions',"type(egress.get(field))is int and egress[field]==value,'source policy revision'","True,'source policy revision'",'test_baseline_raw_frame_hash_and_source_scope_required'),
 ]
 results=[]
 try:
  for name,before,after,method in cases:
   count=source.count(before)
   if not count:raise RuntimeError('missing mutation '+name)
   mutant=types.ModuleType('TCP7_mutant_'+name);mutant.__file__=str(HERE/'tcp_reset_evidence.py')
   exec(compile(source.replace(before,after),'<TCP7-mutant-'+name+'>','exec'),mutant.__dict__)
   tests.check=mutant;golden=tests.grade(tests.fixture())
   if not all(golden['checks'].values()):raise RuntimeError('mutant broke golden '+name)
   output=io.StringIO();result=unittest.TextTestRunner(stream=output,verbosity=0).run(unittest.TestSuite([tests.Tests(method)]))
   if not result.failures or result.errors:raise RuntimeError('not assertion killed '+name+'\n'+output.getvalue())
   results.append({'name':name,'compiled_and_executed':True,'golden_preserved':True,'changed_guard_occurrences':count,'target_test':method,'assertion_failures':len(result.failures),'errors':len(result.errors)})
 finally:tests.check=original
 return {'status':'PASS_COMPILED_EXECUTED_TCP7_MUTANTS','mutants':results,'scope':'Original independentchecker host tests only; no producer model or target execution.'}

if __name__=='__main__':print(json.dumps(run(),indent=2))
