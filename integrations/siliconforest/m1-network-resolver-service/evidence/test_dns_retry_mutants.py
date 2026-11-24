"""Compile and execute bounded source mutations in memory; no source edits."""
import io
from pathlib import Path
import types
import unittest
import test_dns_retry_evidence as tests

MUTANTS={
 'allow_ttl_raise':('all(x<=y for x,y in zip(a,b))','all(True for x,y in zip(a,b))'),
 'ignore_non_ttl_identity':("check(active,all(_same_json(c[0],comparisons[0][0]) for c in comparisons) and", "check(active,True or all(_same_json(c[0],comparisons[0][0]) for c in comparisons) and"),
 'allow_nonpure_permutation':("if parsed['pure_example_a']:","if True:"),
 'ignore_per_key_wire_bytes':("'per_key_normalized_wire':[sorted(wire[k]) for k in keys]", "'per_key_normalized_wire':[]"),
 'ignore_udp_checksum':("if crc and _checksum(pseudo+udp):", "if False:"),
 'ignore_ip_checksum':("or _checksum(raw[:20]) or", "or False or"),
 'ignore_lease_boundary':("all(t<expires for t in rt)","True"),
 'ignore_reply_deadline':("first_query<=t<=first_query+10", "first_query<=t"),
 'ignore_original_answer_list':("_same_json(backend_egress.get('answer_ipv4'),initial['answer_ipv4'])", "True"),
 'ignore_event_raw_reply':("reply_hex=packet['payload'].hex(),duplicate=i>0", "reply_hex=event.get('reply_hex'),duplicate=i>0"),
 'ignore_original_records':("dns_first_reply_rrs=initial['records']", "dns_first_reply_rrs=backend_egress.get('dns_first_reply_rrs')"),
 'ignore_revision':("dns_policy_revision=2", "dns_policy_revision=backend_egress.get('dns_policy_revision')"),
}


class Mutants(unittest.TestCase):
    def test_each_mutant_compiles_executes_and_is_killed(self):
        original=tests.subject
        source=Path(original.__file__).read_text()
        for name,(before,after) in MUTANTS.items():
            with self.subTest(mutant=name):
                self.assertEqual(source.count(before),1)
                mutant=types.ModuleType('mutant_'+name)
                exec(compile(source.replace(before,after),'<'+name+'>','exec'),mutant.__dict__)
                tests.subject=mutant
                try:
                    result=unittest.TextTestRunner(stream=io.StringIO()).run(unittest.defaultTestLoader.loadTestsFromTestCase(tests.RetryEvidence))
                finally:tests.subject=original
                self.assertGreater(result.testsRun,0)
                self.assertFalse(result.wasSuccessful(),name+' survived')
                self.assertFalse(result.errors,name+' crashed rather than failed a test')


if __name__=='__main__':unittest.main()
