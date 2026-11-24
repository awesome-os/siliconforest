import copy
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tools.source_inventory import SourceError
from tools.uapi_contract import integer_expression, parse_enum, compare_enum, generate_probe, compare_layouts

class EnumTests(unittest.TestCase):
    def test_sequential(self):
        self.assertEqual(parse_enum('enum x { A=0, B, C };','x'),{'A':0,'B':1,'C':2})
    def test_expressions(self):
        self.assertEqual(parse_enum('enum x { A=0x10u, B=A|(1<<5), C, D=~0 };','x'),{'A':16,'B':48,'C':49,'D':-1})
    def test_removed_and_changed(self):
        r=compare_enum('enum x{A,B,C};','enum x{A,C};','x')
        self.assertEqual(r['status'],'DIFFERENT_ENUM_VALUES'); self.assertFalse(r['binary_abi_proven'])
        self.assertEqual(len(r['differences']),2)
    def test_match_does_not_claim_abi(self):
        r=compare_enum('enum x{A=0, B};','enum x { A, B };','x')
        self.assertEqual(r['differences'],[]);self.assertFalse(r['binary_abi_proven'])
    def test_reject_preprocessor(self):
        with self.assertRaises(SourceError): parse_enum('enum x{A,\n#if X\nB\n#endif\n};','x')
    def test_reject_unsafe_expression(self):
        for expression in ['__import__("os")','1**8','1/2','MISSING','1<<99','True']:
            with self.subTest(expression=expression), self.assertRaises(SourceError): integer_expression(expression,{})
    def test_reject_ambiguous_or_partial(self):
        for s in ['enum x{A','enum x{A}; enum x{B};','enum x {A,A};','enum x{};']:
            with self.subTest(s=s), self.assertRaises(SourceError): parse_enum(s,'x')
    def test_comments_do_not_change_values(self):
        self.assertEqual(parse_enum('/* enum x{BAD}; */ enum x {A, // B\n C };','x'),{'A':0,'C':1})
    def test_overflow_rejected(self):
        with self.assertRaises(SourceError): parse_enum('enum x{A=0xffffffffffffffffULL,B};','x')

SPEC={'header':'fixture.h','abi_tag':'host-synthetic','structs':[{'name':'sample','fields':['a','b']}],'constants':['FIXTURE_IOCTL']}
REPORT={'schema_version':1,'abi_tag':'host-synthetic','structs':{'sample':{'size':16,'align':8,'offsets':{'a':0,'b':8}}},'constants':{'FIXTURE_IOCTL':9}}

class ProbeTests(unittest.TestCase):
    def test_injection_rejected(self):
        for key,value in [('header','../../secret.h'),('header','x"\n#error pwn'),('abi_tag','x"')]:
            s=copy.deepcopy(SPEC);s[key]=value
            with self.assertRaises(SourceError): generate_probe(s)
        s=copy.deepcopy(SPEC);s['structs'][0]['name']='x); system("no")'
        with self.assertRaises(SourceError): generate_probe(s)
    def test_duplicate_fields_rejected(self):
        s=copy.deepcopy(SPEC);s['structs'][0]['fields']=['a','a']
        with self.assertRaises(SourceError): generate_probe(s)
    def test_compiled_fixture_probe(self):
        cc=shutil.which('clang') or shutil.which('cc')
        if not cc: self.skipTest('No native C compiler; layout probe not executed')
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            (root/'fixture.h').write_text('#include <stdint.h>\nstruct sample {uint32_t a; uint64_t b;};\n#define FIXTURE_IOCTL 9\n')
            (root/'probe.c').write_text(generate_probe(SPEC))
            subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Werror',str(root/'probe.c'),'-o',str(root/'probe')],check=True,capture_output=True,timeout=20)
            completed=subprocess.run([str(root/'probe')],check=True,capture_output=True,text=True,timeout=10)
            report=json.loads(completed.stdout)
            self.assertEqual(report,REPORT)
    def test_compare_layout_diff(self):
        b=copy.deepcopy(REPORT);b['structs']['sample']['offsets']['b']=4
        self.assertEqual(compare_layouts(REPORT,b)['status'],'DIFFERENT_LAYOUTS')
    def test_compare_identical_is_not_runtime_proof(self):
        r=compare_layouts(REPORT,copy.deepcopy(REPORT))
        self.assertEqual(r['status'],'MATCHED_LAYOUTS');self.assertFalse(r['runtime_compatibility_proven'])
    def test_wrong_target_rejected(self):
        b=copy.deepcopy(REPORT);b['abi_tag']='different'
        with self.assertRaises(SourceError): compare_layouts(REPORT,b)
    def test_invalid_probe_reports(self):
        for bad in [0,True,-1,'16']:
            b=copy.deepcopy(REPORT);b['structs']['sample']['size']=bad
            with self.subTest(bad=bad), self.assertRaises(SourceError): compare_layouts(REPORT,b)
        b=copy.deepcopy(REPORT);b['structs']={}
        with self.assertRaises(SourceError): compare_layouts(REPORT,b)
