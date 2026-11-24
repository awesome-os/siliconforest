#!/usr/bin/env python3
"""Bounded original API-fault/byte/span/actual TEST-SDK lifecycle validation."""
from pathlib import Path
import hashlib,importlib.util,os,shutil,subprocess,tempfile,unittest
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('independent_decode',HERE/'collector/decode.py');dec=importlib.util.module_from_spec(spec);spec.loader.exec_module(dec)
NONCE=''.join(f'{i:02x}' for i in range(1,25))
class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc=shutil.which('clang');cls.cxx=shutil.which('clang++');assert cls.cc and cls.cxx
        cls.tmp=tempfile.TemporaryDirectory(prefix='bcm-private-probe-');cls.out=Path(cls.tmp.name)
        cls.flags=['-Wall','-Wextra','-Werror','-Wpedantic','-I'+str(HERE),'-O1']
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def tool(self,args,good=True):
        r=subprocess.run([str(x) for x in args],capture_output=True,timeout=90,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))
        if good:self.assertEqual(r.returncode,0,r.stdout.decode()+r.stderr.decode())
        return r
    def unit(self,name,probe=None,protocol=None,extra=()):
        out=self.out/name
        self.tool([self.cc,'-std=c11',*self.flags,*extra,probe or HERE/'probe.c',protocol or HERE/'protocol.c',HERE/'test_probe.c',HERE/'collector/collector.c','-lcrypto','-o',out]);return out
    def test_sanitized_counters_and_lifecycle(self):
        for mode,flags in [('host',[]),('KERNEL-defined-host',['-DKERNEL'])]:
            r=self.tool([self.unit(mode,extra=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',*flags])]);print(r.stdout.decode().strip())
    def test_actual_collector_and_independent_span(self):
        binary=self.unit('collector')
        for mode,rc in ((0,0),(1,1),(2,1),(3,1),(4,1),(5,77)):
            r=self.tool([binary,'emit',mode],False);self.assertEqual(r.returncode,rc)
            if mode in (1,2):
                self.assertNotIn(b'RECORD_BEGIN',r.stdout);self.assertIn(b'verdict=MISSING' if mode==1 else b'verdict=RPC_ERROR',r.stdout);continue
            summary=dec.decode_span(r.stdout,NONCE)
            self.assertEqual(summary['classification'],{0:'CONFIG_AND_PRIVATE_LIFECYCLE_SELF_REPORT',3:'CLEANUP_ERROR',4:'FAILED_OR_INVALID',5:'GATED_NO_ATTEMPT'}[mode])
            self.assertFalse(summary['data_readback']);self.assertEqual(summary['record_nonce_bound'],mode!=5)
        good=self.tool([binary,'emit',0]).stdout
        for old,new in [(b'index=0 ',b'index=1 '),(b'chunk_bytes=64',b'chunk_bytes=32'),(b'data_readback=false',b'data_readback=true'),(b'bytes=768',b'bytes=769')]:
            with self.assertRaises(ValueError):dec.decode_span(good.replace(old,new,1),NONCE)
        for foreign in (b'foreign\n',b'DL31_APCIE noise\n',b'\n'):
            lines=good.splitlines(keepends=True)
            with self.assertRaises(ValueError):dec.decode_span(b''.join(lines[:2])+foreign+b''.join(lines[2:]),NONCE)
        for suffix in (b'\r\n',b'\r\r\n',b'\r\r\r\n'):
            self.assertEqual(dec.decode_span(good.replace(b'\n',suffix),NONCE)['classification'],'CONFIG_AND_PRIVATE_LIFECYCLE_SELF_REPORT')
        for bad in (good[:-1],good+b'noise\n',good.replace(b'index=0',b'index=\r0')):
            with self.assertRaises(ValueError):dec.decode_span(bad,NONCE)
        with self.assertRaises(ValueError):dec.decode_span(good,'ff'*24)
        print('actual collector emission/hash+independent strict span/failure/CR tests PASS')
    def test_same_owner_actual_sources(self):
        root=HERE.parent;objs=[];flags=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
        sources=[HERE/'probe.c',HERE/'protocol.c',root/'native_private_policy/private_policy.c',root/'native_private_policy/test_reader_fixture.c',root/'native_policy_reader/policy_reader.c',root/'native_policy_snapshot/policy_snapshot.c',root/'mcc_snapshot/mcc_snapshot.c',root/'owned_control/owned_control.c',root/'native_owned/owned.c',root/'private_lifecycle/lifecycle.c']
        for p in sources:
            o=self.out/(p.stem+'.o');self.tool([self.cc,'-std=c11',*self.flags,*flags,'-c',p,'-o',o]);objs.append(o)
        binary=self.out/'same-owner'
        self.tool([self.cxx,'-std=c++17','-fno-exceptions','-fno-rtti','-pthread',*self.flags,*flags,'-I'+str(root/'owned_control/combined_sdk'),root/'owned_control/owned_control_native.cpp',root/'native_owned/native_owned.cpp',HERE/'test_same_owner.cpp',*objs,'-o',binary])
        for scenario in range(5):
            r=self.tool([binary,scenario]);print('scenario',scenario,r.stdout.decode().strip())
    def test_compiled_mutants(self):
        mutations={
          'ignore-zero-error':('if(!rc) {\n        issue(r,BCP_COPY_OP','if(1) {\n        issue(r,BCP_COPY_OP'),
          'no-guarded-retire':('rc=ops.retire(ops.context,&w->token);','rc=0;'),
          'clear-sticky-error':('if(rc && !bcp_word(r,BCP_ERROR_AT))','if(1)'),
          'copy-not16':('w->expected.identity.nonce,16','w->expected.identity.nonce,15'),
          'skip-copy':('rc=ops.copy(ops.context,&w->token,0,w->expected.identity.nonce,16);','rc=0;'),
          'wrong-synthetic-request':('request={0x100,0,1}','request={0x101,0,1}'),
        }
        src=(HERE/'probe.c').read_text()
        for name,(a,b) in mutations.items():
            self.assertEqual(src.count(a),1);p=self.out/(name+'.c');p.write_text(src.replace(a,b));binary=self.unit(name,probe=p)
            r=self.tool([binary],False);self.assertNotEqual(r.returncode,0,name+' survived');self.assertIn(b'Assertion',r.stderr);print(name,'compiled assertion-killed')
        proto=(HERE/'protocol.c').read_text()
        for name,a,b in [('readback-claim','bcp_word(r,BCP_READBACK_AT) ||','0 ||'),('gated-attempts','BCP_STAGE_AT) || attempts ||','BCP_STAGE_AT) || 0 ||'),('post-phase','retired?6u:3u','retired?7u:3u')]:
            self.assertEqual(proto.count(a),1);p=self.out/(name+'.c');p.write_text(proto.replace(a,b));binary=self.unit(name,protocol=p)
            r=self.tool([binary],False);self.assertNotEqual(r.returncode,0,name+' survived');self.assertIn(b'Assertion',r.stderr);print(name,'compiled assertion-killed')
    def test_source_bound_driver_and_refusals(self):
        source=(HERE/'PDBCMPrivateProbe.cpp').read_text()
        self.assertEqual(source.count('OSCompareAndSwap(0,1,&claimed)'),1)
        self.assertLess(source.index('OSCompareAndSwap(0,1,&claimed)'),source.index('PE_parse_boot_argn'))
        self.assertLess(source.index('OSCompareAndSwap(0,1,&claimed)'),source.index('workspace=static_cast'))
        for word in ('bno_native_run(', 'IOServiceOpen(', 'IOMemoryMap','IOFree(','release();'):
            self.assertNotIn(word,source)
        r=self.tool([self.cc,'-std=c11',*self.flags,'-DNDEBUG','-c',HERE/'test_probe.c','-o',self.out/'no.o'],False);self.assertNotEqual(r.returncode,0);self.assertIn(b'tests require assertions',r.stderr)
        r=self.tool([self.cxx,'-std=c++17','-fsyntax-only',HERE/'PDBCMPrivateProbe.cpp'],False);self.assertNotEqual(r.returncode,0);self.assertIn(b'genuine ARM64 Darwin',r.stderr)
if __name__=='__main__':unittest.main(verbosity=2)
