#!/usr/bin/env python3
"""Offline tests of isolated gate/controller and extracted real payload writer."""
import ast
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
AFTER = HERE / 'after'
REPO = HERE.parents[4]
sys.path[:0] = [str(AFTER), str(AFTER / 'integrations/puredarwin/m1-bringup')]
import host_progress as h
from tools import m1_bringup_cycle as controller

KERNEL = (REPO / 'artifacts/m1-desktop-010/apcie-port-candidate-003/kernel.macho').read_bytes()
HV = (REPO / 'sources/m1n1-baseline/proxyclient/m1n1/hv/__init__.py').read_bytes()
FIELD = 'native_bootargs_wire_experiment'


def inputs(record=None, kernel=KERNEL):
    return SimpleNamespace(contents={'build_record': json.dumps({FIELD: dict(h.NATIVE_BOOTARGS_EXPERIMENT)} if record is None else record).encode(),
                                     'kernel': kernel, 'ramdisk': b'OFFLINE_RAM'},
                           host_files={'proxyclient/m1n1/hv/__init__.py': HV},
                           report={'kernel': {'members': {}}})


def legacy_bytes(tba):
    b = bytearray(736)
    struct.pack_into('<HH4x4Q6QI4xQI', b, 0, tba.revision, tba.version, tba.virt_base,
                     tba.phys_base, tba.mem_size, tba.top_of_kernel_data,
                     *[getattr(tba.video, k) for k in ('base','display','stride','width','height','depth')],
                     tba.machine_type, tba.devtree, tba.devtree_size)
    command = tba.cmdline.encode('ascii'); assert len(command) <= 607
    b[108:108+len(command)] = command
    struct.pack_into('<QQ', b, 720, tba.boot_flags, tba.mem_size_actual)
    return bytes(b)


class Tree(dict):
    def build(self): return b'OFFLINE_FAKE_ADT_BYTES'


class Transport:
    def __init__(self): self.writes=[]; self.reads=[]; self.memory={}; self.change=None; self.bad=None
    def writemem(self, address, payload): self.writes.append((address, payload)); self.memory[address]=payload
    def readmem(self, address, size):
        self.reads.append((address,size))
        if self.change: self.change()
        raw=self.memory[address]
        if self.bad=='short': return raw[:-1]
        if self.bad=='corrupt': return bytes([raw[0]^1])+raw[1:]
        if self.bad=='mutable': return bytearray(raw)
        return raw


def fixture():
    low=0x801000000; high=0x810000000; guest=0x802000000; offset=0x800000; address=guest+offset
    mm=SimpleNamespace(TrustCache=(guest+0x100000,0x4000),BootArgs=(address,0x4000),
                       _types={'TrustCache':(SimpleNamespace(build=lambda pair:struct.pack('<QQ',*pair)),False)})
    video=SimpleNamespace(base=0x8f0000000,display=2,stride=10240,width=2560,height=1600,depth=0x1001e)
    tba=SimpleNamespace(revision=2,version=2,virt_base=0xfffffe0010000000,phys_base=low,
                        mem_size=high-low,top_of_kernel_data=address+0x4000,video=video,
                        machine_type=0x1234,devtree=0xfffffe0010004000,devtree_size=0x8000,
                        cmdline=h.BOOTARGS,boot_flags=0x123456789abcdef0,mem_size_actual=0x400000000)
    hv=SimpleNamespace(adt=Tree(chosen={'memory-map':mm}),guest_base=guest,bootargs_off=offset,tba=tba)
    tr=Transport()
    backend=SimpleNamespace(ba=SimpleNamespace(phys_base=0x800000000,mem_size=0x10000000,devtree_size=0x8000),
                            iface=tr,p=SimpleNamespace(memcpy8=Mock(),dc_cvau=Mock()),
                            u=SimpleNamespace(compressed_writemem=Mock()),
                            types=SimpleNamespace(BootArgs_r2=SimpleNamespace(build=legacy_bytes)))
    reservation={'start':low,'end':address+0x14000,'bytes':address+0x14000-low}
    return hv,backend,reservation,[(0x8e0000000,0x4000)]


def baseline_writer():
    path=HERE/'before/integrations/puredarwin/m1-bringup/host_progress.py'
    node=next(n for n in ast.parse(path.read_text()).body if isinstance(n,ast.FunctionDef) and n.name=='load_payloads')
    scope={'old':h.old,'image':h.image,'struct':struct}
    exec(compile(ast.Module(body=[node],type_ignores=[]),str(path),'exec'),scope)
    return scope['load_payloads']


class NativeHostTests(unittest.TestCase):
    def test_absence_does_not_read_new_dependencies_or_verify_kernel(self):
        i=inputs({})
        with patch.object(h,'_native_bootargs_helpers',side_effect=AssertionError('must stay legacy')):
            self.assertIsNone(h.native_bootargs_enabled(i))
        self.assertNotIn('native_bootargs_wire_abi',i.report)

    def test_exact_gate_actual_kernel_bytes(self):
        result=h.native_bootargs_enabled(inputs())
        self.assertEqual(result['abi']['status'],'COMPILED_KERNEL_ABI_MATCH_ONLY')
        self.assertEqual(result['abi']['image_sha256'],hashlib.sha256(KERNEL).hexdigest())
        self.assertFalse(result['abi']['runtime_or_boot_admitted'])
        with self.assertRaises(ValueError): h.native_bootargs_enabled(inputs(kernel=KERNEL[:-1]))
        changed=bytearray(KERNEL);changed[3358720]^=1
        with self.assertRaises(ValueError): h.native_bootargs_enabled(inputs(kernel=bytes(changed)))

    def test_reviewed_verifier002_rejects_interior_and_zerofill_shadow(self):
        profile=json.loads((AFTER/'integrations/puredarwin/m1-apcie-bridge/native_bootargs_abi/PROFILE.json').read_text())
        vm=profile['executable']['vmaddr']+0x4000
        for filesize in (0x4000,0):
            raw=bytearray(KERNEL);count,size=struct.unpack_from('<II',raw,16);end=32+size
            self.assertLessEqual(end+72,0x4000)
            raw[end:end+72]=struct.pack('<II16s4Q4I',0x19,72,b'__ABI_SHADOW',vm,0x4000,0,filesize,7,1,0,0)
            struct.pack_into('<II',raw,16,count+1,size+72)
            with self.assertRaises(ValueError):h.native_bootargs_enabled(inputs(kernel=bytes(raw)))

    def test_malformed_presence_duplicates_and_assertions_refused(self):
        for value in (None,False,[],1,dict(h.NATIVE_BOOTARGS_EXPERIMENT,extra=True)):
            with self.assertRaises(ValueError): h.native_bootargs_enabled(inputs({FIELD:value}))
        for key,bad in (('version',True),('version',1.0),('source_revision',True),('source_revision',3),
                        ('source_version','2'),('wire_profile','legacy736')):
            e=dict(h.NATIVE_BOOTARGS_EXPERIMENT);e[key]=bad
            with self.assertRaises(ValueError): h.native_bootargs_enabled(inputs({FIELD:e}))
        i=inputs();i.contents['build_record']=b'{"native_bootargs_wire_experiment":null,"native_bootargs_wire_experiment":null}'
        with self.assertRaises(ValueError):h.native_bootargs_enabled(i)
        e=dict(h.NATIVE_BOOTARGS_EXPERIMENT);e['compiled_abi_matches']=True
        with self.assertRaises(ValueError):h.native_bootargs_enabled(inputs({FIELD:e}))
        self.assertIsNone(h.native_bootargs_enabled(inputs({'native_bootargs_wire_abi':{'status':'fake'}})))

    def test_source_profile_and_HV_drift_fail(self):
        i=inputs();i.host_files[next(iter(i.host_files))]=b'changed'
        with self.assertRaises(ValueError): h.native_bootargs_enabled(i)
        for key in h.NATIVE_BOOTARGS_SOURCES:
            with patch.dict(h.NATIVE_BOOTARGS_SOURCES,{key:'0'*64}):
                with self.assertRaises(ValueError):h.native_bootargs_enabled(inputs())

    def test_verified_bytes_executed_without_reread(self):
        relative='integrations/puredarwin/m1-apcie-bridge/native_bootargs/native_bootargs.py'
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);p=root/relative;p.parent.mkdir(parents=True);raw=(AFTER/relative).read_bytes();p.write_bytes(raw)
            def read_then_change(path,maximum):
                value=Path(path).read_bytes();Path(path).write_text('raise RuntimeError("unverified replacement")');return value
            with patch.object(h,'LAB',root),patch.object(h.image,'bounded_file',side_effect=read_then_change):
                module=h._native_bootargs_module(relative,'same_verified_codec')
            self.assertEqual(module.WIRE_SIZE,1152)

    def test_preboot_and_worker_fail_before_backend(self):
        i=inputs({FIELD:None});proxy=SimpleNamespace(report={},contents={},host_files=i.host_files,inventory={},inventory_sha256='0'*64)
        args=SimpleNamespace(manifest='unused',bootstrap_manifest='unused',host_source='unused',inventory='unused')
        with patch.object(h.old,'verify_inputs',return_value=proxy),patch.object(h.image,'verify_manifest',return_value=({'kernel':{}},i.contents)):
            with self.assertRaises(ValueError):h.verify_inputs(args)
        with tempfile.TemporaryDirectory() as temp,patch.object(h.old,'authorize'),patch.object(h.subprocess,'run',side_effect=AssertionError('no USB/process observation')),patch.object(h,'ContinuingBackend') as constructor:
            args.private_directory=Path(temp)
            with self.assertRaises(ValueError):h.run_worker(args,i)
            constructor.assert_not_called()

    def test_actual_final_writer_native_oracle_and_saved_evidence(self):
        hv,b,r,c=fixture();i=inputs()
        with tempfile.TemporaryDirectory() as temp:
            result=h.load_payloads(hv,b,b'RAM',Path(temp),native_wire=h.native_bootargs_enabled(i),inputs=i,reservation=r,carveouts=c)
            address=hv.guest_base+hv.bootargs_off
            writes=[v for a,v in b.iface.writes if a==address]
            self.assertEqual(len(writes),1);self.assertEqual(len(writes[0]),1152)
            self.assertEqual(b.iface.reads,[(address,1152)])
            raw=(Path(temp)/'guest-bootargs.bin').read_bytes();self.assertEqual(raw,writes[0])
            self.assertEqual(struct.unpack_from('<QQ',raw,1136),(hv.tba.boot_flags,hv.tba.mem_size_actual))
            self.assertEqual(raw[720:736],bytes(16))
            codec,_=h._native_bootargs_helpers();self.assertEqual(codec.decode(raw,wire_profile=h.NATIVE_BOOTARGS_PROFILE),h._native_values(hv.tba))
            meta=json.loads((Path(temp)/'guest-bootargs-record.json').read_text())
            self.assertEqual(result['native_bootargs'],meta)
            self.assertEqual(meta['readback_sha256'],hashlib.sha256(raw).hexdigest())
            self.assertIn('NOT hardware authorization',meta['source_contract']['scope'])

    def test_legacy_real_writer_branch_identical_to_before(self):
        baseline=baseline_writer()
        hv1,b1,_,_=fixture();hv2,b2,_,_=fixture()
        with tempfile.TemporaryDirectory() as a,tempfile.TemporaryDirectory() as b:
            x=baseline(hv1,b1,b'RAM',Path(a));y=h.load_payloads(hv2,b2,b'RAM',Path(b))
        self.assertEqual(x,y);self.assertEqual(b1.iface.writes,b2.iface.writes);self.assertEqual(b2.iface.reads,[])
        address=hv2.guest_base+hv2.bootargs_off
        self.assertEqual(len([raw for at,raw in b2.iface.writes if at==address][0]),736)

    def test_allocation_and_field_bounds_before_BA_write(self):
        for change in ('base','size','MCC','reserve','revision','command','overflow','bool'):
            hv,b,r,c=fixture();address=hv.guest_base+hv.bootargs_off
            if change=='base':hv.adt['chosen']['memory-map'].BootArgs=(address+0x4000,0x4000)
            elif change=='size':hv.adt['chosen']['memory-map'].BootArgs=(address,1152)
            elif change=='MCC':c=[(address,0x4000)]
            elif change=='reserve':r['end']=address+1152
            elif change=='revision':hv.tba.revision=3
            elif change=='command':hv.tba.cmdline='x'*608
            elif change=='overflow':hv.tba.mem_size=1<<64
            else:hv.tba.boot_flags=True
            with tempfile.TemporaryDirectory() as temp,self.assertRaises((ValueError,UnicodeError)):
                h.load_payloads(hv,b,b'RAM',Path(temp),native_wire={},inputs=inputs(),reservation=r,carveouts=c)
            self.assertFalse([v for a,v in b.iface.writes if a==address],change)
            self.assertFalse(b.iface.reads,change)

    def test_short_corrupt_and_mutable_readback_no_retry(self):
        for bad in ('short','corrupt','mutable'):
            hv,b,r,c=fixture();b.iface.bad=bad;address=hv.guest_base+hv.bootargs_off
            with tempfile.TemporaryDirectory() as temp:
                with self.assertRaises(ValueError):h.load_payloads(hv,b,b'RAM',Path(temp),native_wire={},inputs=inputs(),reservation=r,carveouts=c)
                self.assertFalse((Path(temp)/'guest-bootargs.bin').exists())
            self.assertEqual(len([v for a,v in b.iface.writes if a==address]),1);self.assertEqual(b.iface.reads,[(address,1152)])

    def test_changed_fields_and_mutable_allocation_refused(self):
        for name in ('tail','video','allocation','reservation','MCC'):
            hv,b,r,c=fixture();mm=hv.adt['chosen']['memory-map'];mm.BootArgs=list(mm.BootArgs)
            def change():
                if name=='tail':hv.tba.mem_size_actual+=1
                elif name=='video':hv.tba.video.depth+=1
                elif name=='allocation':mm.BootArgs[0]+=0x4000
                elif name=='reservation':r['end']+=0x4000
                else:c.append((0x8d0000000,0x4000))
            b.iface.change=change
            with tempfile.TemporaryDirectory() as temp,self.assertRaises(ValueError):
                h.load_payloads(hv,b,b'RAM',Path(temp),native_wire={},inputs=inputs(),reservation=r,carveouts=c)

    def test_Construct_integer_normalization_and_bool_refusal(self):
        class Hex(int):
            def __int__(self):raise AssertionError('do not coerce before validation')
        self.assertEqual(h._native_integer(Hex(123),64,'fixture'),123)
        self.assertIs(type(h._native_integer(Hex(123),64,'fixture')),int)
        with self.assertRaises(ValueError):h._native_integer(True,64,'fixture')

    def test_controller_rechecks_dependencies_only_when_opted_in(self):
        args=SimpleNamespace(launcher_sha256=hashlib.sha256((AFTER/controller.LAUNCHER).read_bytes()).hexdigest(),
                             inspector_sha256=hashlib.sha256((AFTER/controller.INSPECTOR).read_bytes()).hexdigest(),
                             candidate='offline',manifest='offline',host_source='offline',inventory='offline')
        spec=SimpleNamespace(name='offline_bound_verify',loader=SimpleNamespace(exec_module=Mock()))
        boot=SimpleNamespace(verify=Mock())
        original_read=controller.base.read_file
        for opted in (False,True):
            candidate=SimpleNamespace(report={'native_bootargs_wire_abi':{}} if opted else {})
            for changed in (None,next(iter(controller.NATIVE_BOOTARGS_SOURCES))):
                def read(path,*rest):
                    return b'changed' if changed and path==AFTER/changed else original_read(path,*rest)
                with patch.object(controller.base,'verify_local',return_value={}),\
                     patch.object(controller.importlib.util,'spec_from_file_location',return_value=spec),\
                     patch.object(controller.importlib.util,'module_from_spec',side_effect=[boot,h]),\
                     patch.object(h,'verify_inputs',return_value=candidate),\
                     patch.object(controller.base,'read_file',side_effect=read):
                    if opted and changed:
                        with self.assertRaises(ValueError):controller.verify(args)
                    else:self.assertIs(controller.verify(args)[1],candidate.report)

    def test_actual_snapshot_loop_includes_and_rehashes_dependencies(self):
        tree=ast.parse((AFTER/'tools/m1_bringup_cycle.py').read_text())
        run=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='run_attempt')
        loop=next(n for n in ast.walk(run) if isinstance(n,ast.For) and isinstance(n.target,ast.Name) and n.target.id=='name')
        script=compile(ast.Module(body=[loop],type_ignores=[]),'extracted-controller-source-snapshot','exec')
        saved={};ops=SimpleNamespace(save=lambda k,v:saved.setdefault(k,v))
        scope=dict(controller.__dict__,ops=ops,native_sources=dict(controller.NATIVE_BOOTARGS_SOURCES))
        exec(script,scope)
        for name,sha in controller.NATIVE_BOOTARGS_SOURCES.items():
            self.assertEqual(hashlib.sha256(saved['source-'+name.replace('/','__')]).hexdigest(),sha)
        original_read=controller.base.read_file;changed=next(iter(controller.NATIVE_BOOTARGS_SOURCES))
        with patch.object(controller.base,'read_file',side_effect=lambda path,*rest:b'changed' if path==AFTER/changed else original_read(path,*rest)):
            with self.assertRaises(ValueError):exec(script,scope)

    def test_worker_and_final_writer_remain_wired(self):
        tree=ast.parse((AFTER/'integrations/puredarwin/m1-bringup/host_progress.py').read_text())
        worker=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='run_worker')
        source=ast.unparse(worker)
        self.assertLess(source.index('native_bootargs_enabled(inputs)'),source.index('subprocess.run('))
        calls=[n for n in ast.walk(worker) if isinstance(n,ast.Call) and isinstance(n.func,ast.Name) and n.func.id=='load_payloads']
        self.assertEqual(len(calls),1)
        for k,v in (('native_wire','native_wire'),('inputs','inputs'),('reservation','reservation'),('carveouts','carveouts')):
            self.assertTrue(any(x.arg==k and isinstance(x.value,ast.Name) and x.value.id==v for x in calls[0].keywords))
        hv=ast.parse(HV)
        cls=next(n for n in hv.body if isinstance(n,ast.ClassDef) and n.name=='HV')
        start=next(n for n in cls.body if isinstance(n,ast.FunctionDef) and n.name=='start')
        writes=[n for n in ast.walk(start) if isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute) and n.func.attr=='writemem']
        self.assertEqual(len(writes),1)
        self.assertEqual(ast.unparse(writes[0].args[0]),'self.adt_base')
        self.assertNotIn('BootArgs_r',ast.unparse(start))

    def test_initial_command_limit_and_controller_source_closure(self):
        with patch.object(h,'guest_bootargs',return_value='x'*608):
            with self.assertRaises(ValueError):h.native_bootargs_enabled(inputs())
        self.assertEqual(controller.NATIVE_BOOTARGS_SOURCES,h.NATIVE_BOOTARGS_SOURCES)
        tree=ast.parse((AFTER/'tools/m1_bringup_cycle.py').read_text())
        verify=ast.unparse(next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='verify'))
        run=ast.unparse(next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='run_attempt'))
        self.assertIn('NATIVE_BOOTARGS_SOURCES',verify);self.assertIn('set(native_sources)',run)
        self.assertIn('**native_sources',run);self.assertIn('native-bootargs-sources.json',run)


if __name__=='__main__':unittest.main(verbosity=2)
