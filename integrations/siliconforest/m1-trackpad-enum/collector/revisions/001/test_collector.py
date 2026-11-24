#!/usr/bin/env python3
"""Host fake-registry state checks and real output decoder. No device backend."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
import decode_records as decoder

HERE=Path(__file__).resolve().parent
ROOT=HERE.parent
N="0123456789abcdef"*3
CFLAGS=["cc","-std=c11","-Wall","-Wextra","-Wpedantic","-Werror","-O1","-g",
        "-fno-omit-frame-pointer","-fsanitize=address,undefined","-I"+str(HERE),"-I"+str(ROOT)]
ENV={**os.environ,"ASAN_OPTIONS":"detect_leaks=0:abort_on_error=1","UBSAN_OPTIONS":"halt_on_error=1"}
def build(source, output):
    subprocess.run([*CFLAGS,str(source),str(HERE/"test_collector.c"),"-lcrypto","-o",str(output)],check=True,capture_output=True)
class Collector(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix="trackpad-collector-")
        cls.directory=Path(cls.temp.name);cls.binary=cls.directory/"test"
        build(HERE/"trackpad_collector.c",cls.binary)
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def test_states_flow_and_actual_output(self):
        p=subprocess.run([str(self.binary)],env=ENV,capture_output=True)
        self.assertEqual(p.returncode,0,p.stderr.decode())
        for mode in range(9):
            p=subprocess.run([str(self.binary),"--emit",str(mode)],env=ENV,capture_output=True)
            self.assertEqual(p.returncode,0 if mode==0 else 77 if mode<=5 else 1,p.stderr.decode())
            if mode<=5:
                r=decoder.decode(p.stdout,N)
                self.assertEqual(r["summary"]["result"],0 if mode==0 else 1 if mode==1 else -(mode-1))
                self.assertEqual(len(r["records"]["provider"]),556)
            else:
                with self.assertRaises(ValueError):decoder.decode(p.stdout,N)
    def test_integrity_order_scope_and_nonce(self):
        raw=subprocess.check_output([str(self.binary),"--emit","0"],env=ENV)
        lines=raw.splitlines(keepends=True)
        cases=[raw+raw,raw[:150],b"".join(lines[-1:]+lines[:-1]),
               raw.replace(b"index=1 ",b"index=2 ",1),raw.replace(b"sha256=",b"sha256=0",1),
               raw.replace(N.encode(),b"f"*48,1),raw.replace(b"input_proven=false",b"input_proven=true"),
               raw.replace(b"feature_writes=0",b"feature_writes=1"),
               raw.replace(b"verdict=ENUMERATION_CAPTURED",b"verdict=REFUSED"),
               b"".join(lines[11:22]+lines[:11]+lines[22:])]
        for i,b in enumerate(cases):
            with self.subTest(i=i),self.assertRaises(ValueError):decoder.decode(b,N)
    def test_assertions_required(self):
        p=subprocess.run(["cc","-DNDEBUG","-E","-I"+str(HERE),"-I"+str(ROOT),str(HERE/"test_collector.c")],capture_output=True)
        self.assertNotEqual(p.returncode,0)
        self.assertIn(b"M1TP_COLLECTOR_TEST_REQUIRES_ASSERTIONS",p.stderr)
    def test_semantic_mutants(self):
        source=(HERE/"trackpad_collector.c").read_text()
        mutations={
            "status_ignored":("result!=M1TP_OBS_ENUM_OK ||","0 ||"),
            "empty_descriptor":("||!stored||","||0||"),
            "interface_ignored":("header[1]==2","1"),
            "missing_as_refused":("int code = TPEC_EXIT_FAIL,","int code = TPEC_EXIT_REFUSED,"),
            "instance_mismatch_ignored":('if (strcmp(inst, "absent") && strcmp(inst, "equal"))','if (0)'),
            "half_descriptor_tail":("!zero(desc+stored,512-stored)","!zero(desc+stored,(512-stored)/2)"),
            "header_tail_ignored":("if(m<16 && !zero(header+m,16-m))return -1;","if(0)return -1;"),
        }
        for name,(a,b) in mutations.items():
            with self.subTest(mutant=name):
                self.assertEqual(source.count(a),1)
                f=self.directory/(name+".c");f.write_text(source.replace(a,b))
                exe=self.directory/name;build(f,exe)
                p=subprocess.run([str(exe)],env=ENV,capture_output=True)
                self.assertNotEqual(p.returncode,0)
                self.assertTrue(b"Assertion" in p.stderr or b"assertion" in p.stderr,p.stderr.decode())
                self.assertNotIn(b"AddressSanitizer",p.stderr)
                self.assertNotIn(b"runtime error:",p.stderr)
if __name__=="__main__":unittest.main()
