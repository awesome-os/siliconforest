#!/usr/bin/env python3
"""All backends/transports synthetic. No new probe binary or packet admitted."""
import ast
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;AFTER=HERE/'after';REPO=HERE.parents[4]
sys.path[:0]=[str(AFTER/'integrations/puredarwin/m1-bringup'),str(AFTER)]
import host_progress as h
from tools import m1_bringup_cycle as controller
policy=h.private_policy_helper();producer,mcc=policy.helpers()
N='a'*48;PROOF='b'*48
# Only fixture source is reused, no test or native API invoked here.
fixture_path=REPO/'integrations/puredarwin/m1-apcie-bridge/native_policy_producer/test_producer.py'
sys.modules['producer']=producer
fixture_module=SimpleNamespace(__name__='_private_source_fixture',__file__=str(fixture_path));scope=fixture_module.__dict__
exec(compile(fixture_path.read_bytes(),str(fixture_path),'exec'),scope)
fixture=scope['fixture'];node=scope['node'];prop=scope['prop']

def fileset(names):
    commands=b''
    for name in names:
        text=name.encode()+b'\0';size=(32+len(text)+7)&~7
        commands+=struct.pack('<IIQQII',0x80000035,size,0,0,32,0)+text+bytes(size-32-len(text))
    return struct.pack('<8I',0xfeedfacf,0x100000c,0,12,len(names),len(commands),0,0)+commands

def inputs(record=None,*,real=False):
    record=({'native_policy_experiment':dict(policy.EXPERIMENT),'native_bootargs_wire_experiment':dict(h.NATIVE_BOOTARGS_EXPERIMENT)} if record is None else record)
    kernel=(REPO/'artifacts/m1-desktop-010/apcie-port-candidate-003/kernel.macho').read_bytes() if real else fileset([policy.MEMBER])
    return SimpleNamespace(contents={'build_record':json.dumps(record).encode(),'kernel':kernel,'ramdisk':b'FAKE_RAM'},host_files={'proxyclient/m1n1/hv/__init__.py':(REPO/'sources/m1n1-baseline/proxyclient/m1n1/hv/__init__.py').read_bytes()},report={'kernel':{'members':{policy.MEMBER:{}}}})

class Tree(dict):
    def __init__(self,base,mm):super().__init__(chosen={'memory-map':mm});self.base=base;self.mm=mm;self.drift=False
    def build(self):
        raw=bytearray(self.base);mmnode=producer.handoff._parse(self.base)['/chosen/memory-map']
        for key in ('BootArgs','DeviceTree','TrustCache','RAMDisk'):
            p=mmnode.get(key);raw[p.begin+36:p.begin+52]=struct.pack('<QQ',*getattr(self.mm,key))
        if self.drift:raw[-1]^=1
        return bytes(raw)
class Transport:
    def __init__(self):self.writes=[];self.reads=[];self.memory={};self.bad=None
    def writemem(self,at,raw):self.writes.append((at,raw));self.memory[at]=raw
    def readmem(self,at,n):
        self.reads.append((at,n));value=self.memory.get(at,b'\0'*n)
        if self.bad=='short':return value[:-1]
        if self.bad=='corrupt':return value[:-1]+bytes([value[-1]^1])
        return value

def payload_fixture():
    base,original,native,raw,context,allocation=fixture()
    values=producer.bootargs.decode(native,wire_profile=producer.bootargs.WIRE_PROFILE)
    phys=values['phys_base'];offset=0x10000;address=phys+offset;capacity=0x4000
    mm=SimpleNamespace(BootArgs=(address,0x4000),DeviceTree=(address+0x4000,capacity),TrustCache=(phys+0x1000,0x4000),RAMDisk=(phys+0x40000,0x4000),_types={'TrustCache':(SimpleNamespace(build=lambda x:struct.pack('<QQ',*x)),False)})
    tree=Tree(base,mm);values['video']=SimpleNamespace(**values['video']);values['cmdline']=h.BOOTARGS
    tba=SimpleNamespace(**values);hv=SimpleNamespace(adt=tree,guest_base=phys,bootargs_off=offset,entry=phys+0x800,tba=tba)
    tr=Transport();backend=SimpleNamespace(ba=SimpleNamespace(phys_base=struct.unpack_from('<Q',original,16)[0],mem_size=struct.unpack_from('<Q',original,24)[0],devtree_size=0x3000),iface=tr,p=SimpleNamespace(memcpy8=Mock(),dc_cvau=Mock()),u=SimpleNamespace(compressed_writemem=Mock()))
    end=address+0x4000+capacity+0x4000+0x4000
    reservation={'start':phys,'end':end,'bytes':end-phys}
    compact=[(span.base,span.bytes) for _,span in producer.facts._mcc(raw,producer.facts.Range(producer.facts.DRAM_BASE,0x400000000))]
    captured=dict(original_adt=b'NOT_USED_BY_PRODUCE',original_bootargs=original,mcc_raw=raw,policy_nonce=N)
    i=inputs({'native_bootargs_wire_experiment':dict(h.NATIVE_BOOTARGS_EXPERIMENT)},real=True)
    return hv,backend,reservation,compact,captured,i

class PrivatePolicyTests(unittest.TestCase):
    def test_absence_no_dependency_load_and_strict_fake_gate(self):
        with patch.object(h,'private_policy_helper',side_effect=AssertionError('absence must not load')):
            self.assertIsNone(h.private_policy_enabled(inputs({})))
        # Positive structural gate has a FABRICATED fileset and stubbed ABI check;
        # it is deliberately not artifact/runtime admission for the new design.
        native=Mock(return_value={'SYNTHETIC_ABI_FIXTURE_ONLY':True})
        proof=policy.verified_mode(inputs(),native)
        self.assertTrue(proof['member_design_only_not_built_driver_proof']);self.assertFalse(proof['runtime_or_boot_admitted'])
        self.assertEqual(h.guest_bootargs(inputs()),h.BOOTARGS+' pdbcmprivate=1')
        with self.assertRaises(ValueError):policy.typed_mode(inputs(real=True)) # Actual af9 lacks proposed probe.
    def test_malformed_conflicts_native_requirement_and_member_raw_bytes(self):
        for value in (None,True,{'version':True,'producer_profile':policy.PROFILE},{'version':1.0,'producer_profile':policy.PROFILE},{'version':1,'producer_profile':'other'},{**policy.EXPERIMENT,'extra':0}):
            i=inputs();record=json.loads(i.contents['build_record']);record[policy.FIELD]=value;i.contents['build_record']=json.dumps(record).encode()
            with self.assertRaises(ValueError):policy.typed_mode(i)
        for field in ('desktop_experiment','network_link_experiment','network_default_experiment','network_resolver_experiment','native_keyboard_desktop_experiment','apcie_port_experiment','dart_survey_experiment','keyboard_experiment','unknown_experiment'):
            i=inputs();r=json.loads(i.contents['build_record']);r[field]=None;i.contents['build_record']=json.dumps(r).encode()
            with self.assertRaises(ValueError):policy.typed_mode(i)
        for mode in ('missingnative','missingmember','doublemember','missingram','duplicateJSON'):
            i=inputs()
            if mode=='missingnative':i.contents['build_record']=json.dumps({policy.FIELD:policy.EXPERIMENT}).encode()
            elif mode=='missingmember':i.report['kernel']['members']={}
            elif mode=='doublemember':i.contents['kernel']=fileset([policy.MEMBER,policy.MEMBER])
            elif mode=='missingram':del i.contents['ramdisk']
            else:i.contents['build_record']=b'{"native_policy_experiment":{},"native_policy_experiment":{}}'
            with self.assertRaises(ValueError):policy.typed_mode(i)
    def test_code_pins_and_verified_exactbytes(self):
        policy.check_sources();self.assertEqual(controller.PRIVATE_POLICY_SOURCES,policy.SOURCES)
        self.assertEqual(controller.PRIVATE_POLICY_HELPER_SHA,h.PRIVATE_POLICY_HELPER_SHA)
        self.assertEqual(hashlib.sha256((AFTER/h.PRIVATE_POLICY_HELPER_PATH).read_bytes()).hexdigest(),h.PRIVATE_POLICY_HELPER_SHA)
        with patch.dict(policy.SOURCES,{next(iter(policy.SOURCES)):'0'*64}):
            with self.assertRaises(ValueError):policy.helpers()
    def test_actual_final_writer_plan_bytes_unchanged_placements(self):
        hv,b,r,c,capture,i=payload_fixture();before=(hv.guest_base,hv.bootargs_off,hv.entry,b.ba.devtree_size)
        with tempfile.TemporaryDirectory() as temp:
            out=h.load_payloads(hv,b,i.contents['ramdisk'],Path(temp),native_wire={},inputs=i,reservation=r,carveouts=c,private_policy=capture)
            state=b.private_policy_state
            self.assertEqual(hv.adt.build(),state['bundle'].plan.payload)
            self.assertEqual(out['end'],r['end']);self.assertEqual(out['adt']['capacity'],0x4000)
            self.assertEqual(hv.tba.top_of_kernel_data,r['end'])
            self.assertEqual(before,(hv.guest_base,hv.bootargs_off,hv.entry,b.ba.devtree_size))
            self.assertEqual(out['native_bootargs']['payload_sha256'],hashlib.sha256(state['native']).hexdigest())
            self.assertEqual((Path(temp)/'guest-bootargs.bin').read_bytes(),state['native'])
            self.assertFalse((Path(temp)/'policy-actual-adt-upload.bin').exists())
            self.assertEqual(len(b.iface.reads),1)
    def test_capacity_refusal_before_new_policy_RAM_writes(self):
        hv,b,r,c,capture,i=payload_fixture();hv.adt.base=fixture(opaque=0x4000)[0]
        with self.assertRaises(ValueError):policy.preflight_capacity(hv,b)
        self.assertFalse(b.iface.writes)
        hv,b,r,c,capture,i=payload_fixture();hv.adt.base=fixture(opaque=0x4000)[0]
        with tempfile.TemporaryDirectory() as temp,self.assertRaises(ValueError):
            h.load_payloads(hv,b,i.contents['ramdisk'],Path(temp),native_wire={},inputs=i,reservation=r,carveouts=c,private_policy=capture)
        self.assertFalse(b.iface.writes);b.p.memcpy8.assert_not_called();b.u.compressed_writemem.assert_not_called()
    def test_real_pinned_start_order_actual_upload_readback_and_7Q_guard(self):
        # Extract the actual frozen HV.start body. Everything it calls is fake;
        # ordering/assertion/source body remains the real18da function.
        source=ast.parse((REPO/'sources/m1n1-baseline/proxyclient/m1n1/hv/__init__.py').read_bytes())
        cls=next(n for n in source.body if isinstance(n,ast.ClassDef) and n.name=='HV');fn=next(n for n in cls.body if isinstance(n,ast.FunctionDef) and n.name=='start')
        for failure in ('none','base_drift','wrongaddress','wrongpayload','short','corrupt','no_upload'):
            tree=SimpleNamespace(build=lambda:b'BASE',__getitem__=None)
            class Base:
                def __init__(self):self.drift=False
                def build(self):return b'DRIFT' if self.drift else b'BASE'
                def __getitem__(self,k):return [SimpleNamespace(state='running',cpu_id=0,cluster_id=0)]
            base=Base();view=policy.SealedADT(base,b'BASE',b'FINAL_POLICY_ADT')
            tr=Transport();tr.dev=SimpleNamespace(timeout=1);jump=Mock(return_value='FAKE_JUMP');events=[]
            p=SimpleNamespace(iodev_set_usage=lambda *x:events.append('iodev'),fb_improve_logo=lambda:events.append('logo'),fb_shutdown=lambda *x:events.append('fb'),hv_start=jump)
            hv=SimpleNamespace(adt=view,adt_base=0x800010000,u=SimpleNamespace(ba=SimpleNamespace(devtree_size=0x4000),cpu_features=SimpleNamespace(apple_sysregs_unlocked=False)),p=p,iface=tr,iodev=0,map_essential=lambda:events.append('map'),pt_update=lambda:events.append('pt'),emulate_sprr=False,entry=0x800020000,guest_base=0x800020000,bootargs_off=0x10000,_handle_sigint=lambda:None,wdt_cpu=None,started_cpus={},sysreg={})
            globals={'IODEV':(0,1),'align':lambda x:(x+0x3fff)&~0x3fff,'signal':SimpleNamespace(SIGINT=2,signal=lambda *x:None),'set_sigquit_stackdump_handler':lambda:None}
            exec(compile(ast.Module(body=[fn],type_ignores=[]),'actual-pinned-HV-start','exec'),globals)
            original=lambda:globals['start'](hv)
            if failure=='base_drift':base.drift=True
            if failure in ('short','corrupt'):tr.bad=failure
            if failure=='wrongaddress':
                def altered_build():view.check_base();view.await_upload=True;hv.adt_base+=0x4000;return b'FINAL_POLICY_ADT'
                view.build=altered_build
            if failure=='wrongpayload':
                def altered_build():view.check_base();view.await_upload=True;return b'changed'
                view.build=altered_build
            if failure=='no_upload':original=lambda:p.hv_start(hv.entry,hv.guest_base+hv.bootargs_off)
            state=dict(view=view,bundle=SimpleNamespace(plan=SimpleNamespace(payload=b'FINAL_POLICY_ADT')),address=hv.adt_base,capacity=0x4000,attempts=0,readback_verified=False,start_calls=0,active=False,policy_nonce=N)
            b=SimpleNamespace(iface=tr,p=p)
            with tempfile.TemporaryDirectory() as temp:
                checked=policy.start_wrapper(hv,b,Path(temp),state,original,h.old.write_private,lambda:None)
                if failure=='none':
                    self.assertIsNone(checked());jump.assert_called_once()
                    self.assertEqual(events[:3],['iodev','map','pt'])
                    self.assertEqual(tr.writes,[(state['address'],b'FINAL_POLICY_ADT')]);self.assertEqual(tr.reads,[(state['address'],16)])
                    self.assertTrue((Path(temp)/'policy-actual-upload.json').exists())
                    initial_writes=list(tr.writes)
                    for at,raw in ((state['address']+16,b'X'),(state['address']+state['capacity']-1,b'X'),(state['address']-1,b'XX'),(state['address']+1,b'X')):
                        with self.assertRaises(ValueError):tr.writemem(at,raw)
                        self.assertEqual(tr.writes,initial_writes)
                else:
                    with self.assertRaises(ValueError):checked()
                    jump.assert_not_called()
                    if failure in ('base_drift','wrongaddress','wrongpayload','no_upload'):
                        self.assertEqual(tr.writes,[])
                prior_events=list(events);prior_writes=list(tr.writes);prior_reads=list(tr.reads)
                with self.assertRaises(ValueError):checked()
                self.assertEqual(events,prior_events);self.assertEqual(tr.writes,prior_writes);self.assertEqual(tr.reads,prior_reads)
                if failure=='none':
                    with self.assertRaises(ValueError):p.hv_start(hv.entry,hv.guest_base+hv.bootargs_off)
                    jump.assert_called_once()
    def test_full_MCC_capture_exact96_raw_source_translation_and_no_retry(self):
        base,original,_,raw,_,_=fixture()
        parsed=producer.handoff._parse(base);arm=parsed['/arm-io']
        translated=0x200800000;bus=0x100800000;extent=0x100000
        reg=struct.pack('<6Q',0x100000000,0x4000,bus,extent,0x100c00000,0x4000)
        additions=prop('#address-cells',struct.pack('<I',2))+prop('#size-cells',struct.pack('<I',2))+prop('ranges',struct.pack('<QQQ',0x100000000,0x200000000,0x10000000))
        child=node('mcc',(prop('compatible',b'mcc,t8103\0'),prop('reg',reg)))
        data=bytearray(base[:arm.properties_end]+additions+child+base[arm.properties_end:])
        struct.pack_into('<I',data,arm.begin,arm.property_count+3)
        oldchildren=struct.unpack_from('<I',base,arm.begin+4)[0];struct.pack_into('<I',data,arm.begin+4,oldchildren+1)
        originalADT=bytes(data)
        compact=[(span.base,span.bytes) for _,span in producer.facts._mcc(raw,producer.facts.Range(0x800000000,0x400000000))]
        snapshot={'product':{'model':'MacBookPro17,1','target_type':'J293'},'chip':{'adt_chip_id':0x8103,'proxy_chip_id':0x8103}}
        for failure in ('none','throw','bool','compact','get_reg','parent_shadow','base_change','interrupt'):
            adt=originalADT;calls=[];getreg=Mock(return_value=(translated+(4 if failure=='get_reg' else 0),extent))
            class OriginalTree:
                model='MacBookPro17,1';target_type='J293'
                def __getitem__(self,key):return SimpleNamespace(chip_id=0x8103) if key=='chosen' else SimpleNamespace(get_reg=getreg)
            if failure=='parent_shadow':
                p=producer.handoff._parse(adt)['/arm-io'].get('ranges')
                adt=adt[:p.begin+36]+p.value*2+adt[p.end:]
                changed=bytearray(adt);struct.pack_into('<I',changed,p.begin+32,len(p.value)*2);adt=bytes(changed)
            b=SimpleNamespace(u=SimpleNamespace(adt_data=adt),adt_module=SimpleNamespace(load_adt=lambda _:OriginalTree()),p=SimpleNamespace())
            def read32(address):
                index=len(calls);calls.append(address)
                if index==16:
                    if failure=='throw':raise RuntimeError('synthetic read failure')
                    if failure=='bool':return True
                    if failure=='interrupt':raise KeyboardInterrupt('synthetic abort')
                    if failure=='base_change':b.u.adt_data=b'changed'
                return struct.unpack_from('<I',raw,index*4)[0]
            b.p.read32=read32
            with tempfile.TemporaryDirectory() as temp:
                args=(b,adt,original,snapshot,[] if failure=='compact' else compact,N,Path(temp),h.old.write_private)
                if failure=='none':
                    result=policy.capture(*args);self.assertEqual(result['mcc_raw'],raw)
                    self.assertEqual(calls,[translated+x for x in mcc.relative_plan(1,extent)])
                    self.assertEqual((Path(temp)/'policy-mcc-full384.bin').read_bytes(),raw)
                    self.assertTrue((Path(temp)/'policy-mcc-validated.json').exists())
                    self.assertEqual(getreg.call_args.args,(1,))
                elif failure=='interrupt':
                    with self.assertRaises(KeyboardInterrupt):policy.capture(*args)
                    self.assertEqual(len(calls),17)
                    self.assertEqual(len((Path(temp)/'policy-mcc-prefix.bin').read_bytes()),64)
                    self.assertEqual(json.loads((Path(temp)/'policy-mcc-capture.json').read_text())['status'],'CALLBACK_ABORTED')
                else:
                    with self.assertRaises(ValueError):policy.capture(*args)
                    self.assertFalse((Path(temp)/'policy-mcc-validated.json').exists())
                    if failure in ('throw','bool'):self.assertEqual(len(calls),17)
                    if failure in ('get_reg','parent_shadow'):self.assertEqual(calls,[])
                prior=list(calls)
                with self.assertRaises(ValueError):policy.capture(*args)
                self.assertEqual(calls,prior)

    def test_historical_raw_MCC_translation_only_not_read_safety(self):
        raw=(REPO/'runs/m1-desktop-010/apcie-port-attempt-001/guest/original-adt.bin').read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),'561af359c45e511759c7e2bd835482a3aac7cb7f4404d9d9ba026b967f8ea3bc')
        base,extent,regsha,rangessha=policy.translated_mcc_reg(producer.handoff._parse(raw))
        self.assertEqual(extent,0x100000);self.assertEqual(regsha,'539e810689b930f8c0300e32fd8eeef90e0aa490b00ab6b2e8ef6a54a5db6545')
        self.assertTrue(base>0 and base%4==0 and base+extent<=1<<36)
        self.assertEqual(len(rangessha),64)

    def test_worker_wiring_early_gates_capture_capacity_and_optional_FB(self):
        tree=ast.parse((AFTER/'integrations/puredarwin/m1-bringup/host_progress.py').read_text());worker=ast.unparse(next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='run_worker'))
        self.assertLess(worker.index('private_policy_enabled(inputs)'),worker.index('subprocess.run('))
        self.assertLess(worker.index('policy_nonce = secrets.token_hex'),worker.index('subprocess.run('))
        self.assertLess(worker.index('.capture('),worker.index('hv.init()'))
        self.assertLess(worker.index('.preflight_capacity('),worker.index('hv.load_raw('))
        self.assertIn('capture_framebuffer=not keyboard and private_mode is None',worker)
        self.assertIn('private_policy=policy_capture',worker)
        self.assertEqual(worker.count('.capture('),1)
    def test_private_console_TX_seals_and_keeps_collector_error_tail(self):
        with tempfile.TemporaryDirectory() as temp,patch.object(h.threading,'Thread'),patch.object(h.secrets,'token_hex',return_value=PROOF):
            dev=Mock();dev.write.side_effect=len
            s=h.ConsoleSession('FAKE',Path(temp),serial_factory=lambda *a,**k:dev,private_policy=True,policy_nonce=N)
            response=f'\nDL9_BEGIN_{PROOF}\nDarwin\narm64\n25.5.0\nDL9_END_{PROOF}\n# '.encode()
            chunks=[b'Enter full pathname of shell',b'\n# ',response,b'COLLECTOR_ERROR diagnostic only\n# ',b'AFTER_ERROR_TAIL\n']
            def read(*args):
                if chunks:return chunks.pop(0)
                s.finish.set();return b''
            dev.read.side_effect=read;s.read()
            self.assertIsNone(s.error);self.assertTrue(s.private_sent);self.assertEqual(s.native_tx_phase,'sealed')
            expected=[b'/bin/sh\r',s.proof.command(),policy.command(N)]
            self.assertEqual([c.args[0] for c in dev.write.call_args_list],expected)
            self.assertIn(b'AFTER_ERROR_TAIL',(Path(temp)/'console.bin').read_bytes())
            self.assertEqual((Path(temp)/'console-tx.bin').read_bytes(),b''.join(expected))
            self.assertNotEqual(s.proof.nonce,N)
    def test_controller_private_dependency_verify_and_snapshot_rehash(self):
        args=SimpleNamespace(launcher_sha256=hashlib.sha256((AFTER/controller.LAUNCHER).read_bytes()).hexdigest(),
                             inspector_sha256=hashlib.sha256((AFTER/controller.INSPECTOR).read_bytes()).hexdigest(),
                             candidate='FAKE',manifest='FAKE',host_source='FAKE',inventory='FAKE')
        spec=SimpleNamespace(name='fake_verification_module',loader=SimpleNamespace(exec_module=Mock()))
        boot=SimpleNamespace(verify=Mock());candidate=SimpleNamespace(report={'native_bootargs_wire_abi':{},'native_private_policy_sources':{}})
        sources={controller.PRIVATE_POLICY_HELPER:controller.PRIVATE_POLICY_HELPER_SHA,**controller.PRIVATE_POLICY_SOURCES}
        read=controller.base.read_file
        for changed in (None,*sources):
            with patch.object(controller.base,'verify_local',return_value={}),patch.object(controller.importlib.util,'spec_from_file_location',return_value=spec),\
                 patch.object(controller.importlib.util,'module_from_spec',side_effect=[boot,h]),patch.object(h,'verify_inputs',return_value=candidate),\
                 patch.object(h,'private_policy_helper',return_value=policy),\
                 patch.object(controller.base,'read_file',side_effect=lambda path,*a:b'drift' if changed and path==AFTER/changed else read(path,*a)):
                if changed:
                    with self.assertRaises(ValueError):controller.verify(args)
                else:self.assertEqual(controller.verify(args)[1],candidate.report)
        tree=ast.parse((AFTER/'tools/m1_bringup_cycle.py').read_text());fn=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='run_attempt')
        loop=next(n for n in ast.walk(fn) if isinstance(n,ast.For) and isinstance(n.target,ast.Name) and n.target.id=='name')
        script=compile(ast.Module(body=[loop],type_ignores=[]),'actual-source-snapshot-loop','exec')
        saved={};scope=dict(controller.__dict__,ops=SimpleNamespace(save=lambda key,value:saved.setdefault(key,value)),private_sources=sources,native_sources=controller.NATIVE_BOOTARGS_SOURCES)
        exec(script,scope)
        for path,digest in sources.items():self.assertEqual(hashlib.sha256(saved['source-'+path.replace('/','__')]).hexdigest(),digest)
        changed=next(iter(sources))
        with patch.object(controller.base,'read_file',side_effect=lambda path,*a:b'drift' if path==AFTER/changed else read(path,*a)):
            with self.assertRaises(ValueError):exec(script,scope)
    def test_bad_worker_mode_refuses_before_USB_or_backend(self):
        i=inputs();r=json.loads(i.contents['build_record']);r[policy.FIELD]=None;i.contents['build_record']=json.dumps(r).encode()
        with patch.object(h.old,'authorize'),patch.object(h,'native_bootargs_enabled',return_value={}),\
             patch.object(h.subprocess,'run',side_effect=AssertionError('no subprocess actor allowed')),\
             patch.object(h,'ContinuingBackend',side_effect=AssertionError('no backend allowed')):
            with self.assertRaises(ValueError):h.run_worker(SimpleNamespace(),i)

    def test_nonce_collision_refuses_before_serial_open(self):
        factory=Mock()
        with tempfile.TemporaryDirectory() as temp,patch.object(h.secrets,'token_hex',return_value=N):
            with self.assertRaises(ValueError):h.ConsoleSession('FAKE',Path(temp),serial_factory=factory,private_policy=True,policy_nonce=N)
        factory.assert_not_called()

if __name__=='__main__':unittest.main(verbosity=2)
