"""Original synthetic saved-file shapes only. Never a Darwin/recovery fixture."""
from pathlib import Path
import copy,hashlib,importlib.util,json,shutil,struct,sys,tempfile,unittest
import audit as a

def sha(b):return hashlib.sha256(b).hexdigest()
def put(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(data if isinstance(data,bytes) else (json.dumps(data,sort_keys=True)+'\n').encode())
def reference(path):return {'path':str(path),'sha256':sha(path.read_bytes())}
def load_fixture():
    producer=a.module('native_policy_producer/producer.py','private_capture_test_producer');sys.modules['producer']=producer
    file=a.BRIDGE/'native_policy_producer/test_producer.py'
    m=importlib.util.module_from_spec(importlib.util.spec_from_loader('frozen_synthetic_producer_fixture',loader=None));m.__file__=str(file);exec(compile(file.read_bytes(),str(file),'exec'),m.__dict__)
    return producer,m
def fixture(directory):
    """Generate explicitly SYNTHETIC bytes and external-document stand-ins."""
    p,f=load_fixture();base,old,native,mcc,_,allocation=f.fixture();nodes=p.handoff._parse(base)
    def rebuild(path):
        name=nodes[path].get('name').value[:-1].decode();props=[(x.key,x.value) for x in nodes[path].properties if x.key!='name']
        children=[k for k in nodes if k and k.rsplit('/',1)[0]==path]
        childbytes=[rebuild(k) for k in children]
        if path=='/arm-io':
            props.extend([('#address-cells',struct.pack('<I',2)),('#size-cells',struct.pack('<I',2)),('ranges',struct.pack('<QQQ',0,0x200000000,0x10000000))])
            childbytes.append(f.node('mcc',(f.prop('compatible',b'mcc,t8103\0'),f.prop('reg',struct.pack('<6Q',0,0x1000,0x100000,0x400000,0x800000,0x1000)))))
        return f.node(name,tuple(f.prop(k,v) for k,v in props),tuple(childbytes))
    base=rebuild('');original=base;old=bytearray(old);struct.pack_into('<I',old,104,len(original));old=bytes(old)
    kernel=directory/'synthetic-kernel.bin';ram=directory/'synthetic-RAM.bin';put(kernel,b'SYNTHETIC_NOT_A_KERNEL');put(ram,b'SYNTHETIC_NOT_RAM_ROOT')
    nonce=bytes(range(1,25));ctx=p.Context(nonce,bytes.fromhex(sha(kernel.read_bytes())),bytes.fromhex(sha(ram.read_bytes())),p.HV_SHA256)
    measured=p.measure(base,old,mcc,ctx,profile=p.PROFILE,original_profile=p.ORIGINAL_PROFILE)
    values=p.bootargs.decode(native,wire_profile=p.bootargs.WIRE_PROFILE);values['devtree_size']=measured.final_adt_bytes;values['cmdline']='cpus=1 serial=3 pdbcmprivate=1';native=p.bootargs.encode(values,wire_profile=p.bootargs.WIRE_PROFILE)
    bundle=p.produce(base,old,native,mcc,ctx,allocation,profile=p.PROFILE,original_profile=p.ORIGINAL_PROFILE)
    cycle=directory/'SYNTHETIC_CYCLE';guest=cycle/'guest';guest.mkdir(parents=True)
    files={'original-adt.bin':original,'original-bootargs.bin':old,'policy-base-adt.bin':base,'guest-adt.bin':base,
      'guest-bootargs.bin':native,'guest-bootargs-readback.bin':native,'policy-mcc-full384.bin':mcc,'policy-mcc-prefix.bin':mcc,
      'policy-facts.bin':bundle.wire,'policy-planned-adt.bin':bundle.plan.payload,'policy-actual-adt-upload.bin':bundle.plan.payload,'policy-actual-adt-readback.bin':bundle.plan.payload}
    for name,data in files.items():put(guest/name,data)
    translation=a.mcc_translation(p.handoff._parse(original));metadata={'status':'RAW_COMPLETE_UNVALIDATED','attempted':96,'completed':96,'original_adt_sha256':sha(original),'policy_nonce':nonce.hex(),'reg_index':1,'region_base':translation[0],'region_bytes':translation[1],'reg_property_sha256':translation[2],'parent_ranges_sha256':translation[3],'whole_region_unique_parent_translation_checked':True}
    put(guest/'policy-mcc-capture.json',metadata);put(guest/'policy-mcc-validated.json',dict(metadata,raw_sha256=sha(mcc),full_snapshot_validated=True,compact_exact_slot_count_match=True))
    private_source={'profile':'t8103-pinned-hv-source-facts-v1','probe_member':'org.darwinlab.driver.PDBCMPrivateProbe','command':'/usr/bin/darwin-native-private-test','sources_sha256':a.assignments((a.HOST/'after/tools/m1_bringup_cycle.py').read_bytes())['PRIVATE_POLICY_SOURCES']}
    put(guest/'private-policy-source-context.json',{'policy_nonce':nonce.hex(),'design_only_not_runtime':True,'sources':private_source})
    put(guest/'policy-plan.json',bundle.metadata())
    put(guest/'private-policy-capacity-preflight.json',{'original_capacity':allocation.bytes,'no_resize':True,'current_base_bytes':len(base),'conservative_descriptor_growth':7*52,'max_facts_and_property_bytes':36+9472})
    put(guest/'policy-actual-upload.json',{'address':allocation.base,'bytes':len(bundle.plan.payload),'sha256':sha(bundle.plan.payload),'exact_actual_upload_and_readback':True,'attempts':1,'policy_nonce':nonce.hex()})
    start={'guest_base':values['phys_base'],'bootargs_off':0x10000,'address':values['phys_base']+0x10000,'entry':values['phys_base']+0x1000,'adt_base':allocation.base}
    put(guest/'guest-bootargs-record.json',{'readback_exact':True,'readback_sha256':sha(native),'payload_sha256':sha(native),'start_pointer_state':start,'checked_allocation':{'base':start['address'],'expected_address':start['address'],'bytes':16384}})
    put(guest/'before-start.json',{'layout':start,'bootargs':values['cmdline'],'image_sha256':sha(kernel.read_bytes()),'payloads':{'adt':{'address':allocation.base,'capacity':allocation.bytes,'sha256':sha(bundle.plan.payload)}}})
    for path,digest in a.snapshot_pins().items():put(cycle/('source-'+path.replace('/','__')),(a.HOST/'after'/path).read_bytes())
    artifacts={'kernel':sha(kernel.read_bytes()),'ramdisk':sha(ram.read_bytes())}
    put(cycle/'verified-inputs.json',{'candidate':{'artifacts':artifacts,'native_private_policy_sources':private_source},'controller_sha256':a.AFTER['tools/m1_bringup_cycle.py'],'launcher_sha256':a.AFTER['integrations/puredarwin/m1-bringup/host_progress.py']})
    put(guest/'original-observations.json',{'packet':{'artifacts':artifacts}})
    decoder=a.module('native_private_probe/collector/decode.py','capture_fixture_decoder')
    record=bytearray(768);record[:8]=b'BCMPRV01'
    def word(at,*words):struct.pack_into('<'+'I'*len(words),record,at,*words)
    word(8,1,768,0,10,255,0,1,1,0,0);record[48:264]=nonce+b''.join(bundle.expected.identities);record[264:424]=decoder.COMPONENTS
    word(456,1,1,1,4,3,6,16,1);word(496,*decoder.PRE);word(552,*decoder.POST)
    policy=nonce.hex();console_nonce='ab'*24
    lines=[f'DL32_PRIVATE_PROCESS_BEGIN {policy}',f'DL32_PRIVATE_RECORD_BEGIN {policy} PDBCMPrivateObservation bytes=768 chunk_bytes=64']
    for i in range(12):lines.append(f'DL32_PRIVATE_RECORD_HEX {policy} PDBCMPrivateObservation index={i} {record[i*64:(i+1)*64].hex()}')
    lines.extend([f'DL32_PRIVATE_RECORD_END {policy} PDBCMPrivateObservation bytes=768 chunks=12 sha256={sha(record)}',f'DL32_PRIVATE_VERDICT {policy} verdict=CONFIG_AND_PRIVATE_LIFECYCLE_SELF_REPORT result=0 stage=10 attempts=255 error=0 record_nonce_bound=true data_readback=false effect_commands=false trusted_host_labels=true',f'DL32_PRIVATE_PROCESS_END {policy} collector_exit=0 self_report_only=true'])
    raw=b'\nDL9_BEGIN_'+console_nonce.encode()+b'\nDarwin\narm64\n25.5.0\nDL9_END_'+console_nonce.encode()+b'\n'+('\n'.join(lines)+'\n').encode();put(guest/'console.bin',raw)
    command=a.challenge(console_nonce);private=a.launcher(policy);tx=b'/bin/sh\r'+command+private
    put(guest/'challenge-input.bin',command);put(guest/'private-policy-input.bin',private);put(guest/'console-tx.bin',tx)
    put(guest/'console-result.json',{'nonce':console_nonce,'policy_nonce':policy,'native_private_policy_enabled':True,'private_command_sent':True,'native_mode_host_write_phase':'sealed','desktop_command_sent':False,'keyboard_command_sent':False,'terminal_input_enabled':False,'native_keyboard_desktop_enabled':False,'native_trackpad_child_enabled':False,'error':None,'challenge_sent':True,'command_line_observed':True,'console_sha256':sha(raw),'console_bytes':len(raw),'console_tx_sha256':sha(tx),'console_tx_bytes':len(tx)})
    put(guest/'capture-core.json',{'console_capture_finalized':True,'console_close_error':None})
    put(guest/'exception-context.json',{'diagnostics_complete':True,'diagnostics':{'complete':True},'context_error':None})
    put(guest/'host-result.json',{'status':'HOST_WORKER_EXITED_WITHOUT_BOOT_VERDICT','worker_returncode':1})
    put(cycle/'guest-completed.json',{'cli_returncode':1,'host_result':{'status':'HOST_WORKER_EXITED_WITHOUT_BOOT_VERDICT','worker_returncode':1}})
    put(cycle/'baseline.json',{'boot_id':'SYNTHETIC_OLD'});put(cycle/'returned-linux.json',{'boot_id':'SYNTHETIC_NEW'});put(cycle/'result.json',{'linux_verified':True,'reset_attempts':1})
    manifest=directory/'synthetic-manifest.json';review=directory/'synthetic-review.json';binding=directory/'synthetic-packet-binding.json';cert=directory/'synthetic-external-recovery.json'
    artifact_meta={k:{'sha256':v} for k,v in artifacts.items()}
    put(manifest,{'artifacts':artifact_meta,'scope':'SYNTHETIC UNIT TEST'});put(review,{'status':'APPROVED_SYNTHETIC_UNIT_TEST_ONLY','blockers':[],'checks':[{'name':'four_artifacts','status':'PASS','evidence':artifact_meta}]})
    put(binding,{'kind':'PRIVATE_PACKET_EXTERNAL_BINDING_V1','status':'VERIFIED_EXTERNALLY','kernel_sha256':artifacts['kernel'],'ramdisk_sha256':artifacts['ramdisk'],'manifest':reference(manifest),'review':reference(review)})
    context={'kernel':reference(kernel),'ramdisk':reference(ram),'packet_binding':reference(binding)}
    put(cert,{'kind':'PRIVATE_CAPTURE_RECOVERY_EXTERNAL_V1','status':'VERIFIED_EXTERNALLY','console_sha256':sha(raw),'core_sha256':sha((guest/'capture-core.json').read_bytes()),'packet_binding_sha256':context['packet_binding']['sha256'],'baseline_boot_id':'SYNTHETIC_OLD','new_boot_id':'SYNTHETIC_NEW','reset_attempts':1,'latch_absent':True,'lock_free':True,'controller_complete':True,'cycle_result_sha256':sha((cycle/'result.json').read_bytes()),'baseline_sha256':sha((cycle/'baseline.json').read_bytes()),'returned_linux_sha256':sha((cycle/'returned-linux.json').read_bytes())})
    return cycle,context,reference(cert)
class Tests(unittest.TestCase):
    def setUp(self):self.temp=tempfile.TemporaryDirectory(prefix='private-capture-synthetic-');self.directory=Path(self.temp.name);self.cycle,self.context,self.recovery=fixture(self.directory)
    def tearDown(self):self.temp.cleanup()
    def audit(self):return a.audit(self.cycle,self.context,self.recovery)
    def assert_unqualified(self,result):self.assertFalse(result['qualified']);self.assertEqual(tuple(result['checks']),a.CHECKS)
    def test_synthetic_full_chain_and_external_not_auth(self):
        r=self.audit();self.assertTrue(r['qualified'],r['checks']);self.assertFalse(r['data_readback']);self.assertFalse(r['WiFi_ready']);self.assertIn('raw proxy7Qframe/start_calls not saved',r['details']['HV_START_guard_scope'])
        self.assertNotIn('raw_record_private',r['details']);self.assertNotIn('allocation_private',r['details'])
    def test_every_raw_byte_file_mandatory(self):
        names=['original-adt.bin','original-bootargs.bin','policy-base-adt.bin','guest-adt.bin','guest-bootargs.bin','guest-bootargs-readback.bin','policy-mcc-full384.bin','policy-mcc-prefix.bin','policy-facts.bin','policy-planned-adt.bin','policy-actual-adt-upload.bin','policy-actual-adt-readback.bin']
        for name in names:
            p=self.cycle/'guest'/name;b=p.read_bytes();p.unlink();self.assert_unqualified(self.audit());put(p,b)
    def test_changed_actual_bytes_and_source_snapshot(self):
        for name in ('policy-actual-adt-readback.bin','policy-facts.bin','guest-bootargs-readback.bin','policy-mcc-full384.bin'):
            p=self.cycle/'guest'/name;b=p.read_bytes();changed=bytearray(b);changed[-1]^=1;put(p,bytes(changed));self.assert_unqualified(self.audit());put(p,b)
        p=next(self.cycle.glob('source-*host_progress.py'));put(p,p.read_bytes()+b'\n');self.assert_unqualified(self.audit())
    def test_TX_extra_and_process_error(self):
        p=self.cycle/'guest/console-tx.bin';b=p.read_bytes();put(p,b+b'id\r');receipt=self.cycle/'guest/console-result.json';j=json.loads(receipt.read_text());j.update(console_tx_bytes=len(b)+3,console_tx_sha256=sha(b+b'id\r'));put(receipt,j)
        r=self.audit();self.assertEqual(r['checks'][a.CHECKS[8]]['status'],'FAILED');self.assertIn('sealed exact TX',r['checks'][a.CHECKS[8]]['errors'][0]);put(p,b)
        p=self.cycle/'guest/console.bin';put(p,p.read_bytes().replace(b'collector_exit=0',b'collector_exit=1'));self.assert_unqualified(self.audit())
    def test_missing_external_and_recovery_scope(self):
        self.assertEqual(a.audit(self.cycle,self.context)['checks'][a.CHECKS[9]]['status'],'NOT_EVALUATED')
        ctx=dict(self.context);ctx.pop('packet_binding');r=a.audit(self.cycle,ctx);self.assert_unqualified(r);self.assertEqual(r['checks'][a.CHECKS[0]]['status'],'NOT_EVALUATED')
        p=Path(self.recovery['path']);j=json.loads(p.read_text());j['new_boot_id']=j['baseline_boot_id'];put(p,j);self.recovery=reference(p);self.assert_unqualified(self.audit())
    def test_metadata_and_count_refusals(self):
        for name,key,value in [('policy-mcc-capture.json','attempted',95),('policy-mcc-capture.json','completed',True),('policy-actual-upload.json','attempts',2),('console-result.json','native_mode_host_write_phase','await_launcher'),('host-result.json','worker_returncode',0)]:
            p=self.cycle/'guest'/name;b=p.read_bytes();j=json.loads(b);j[key]=value;put(p,j);self.assert_unqualified(self.audit());put(p,b)
        self.assert_unqualified(a.audit(self.directory/'missing',{}))
    def test_duplicate_JSON_and_symlink_refusal(self):
        p=self.cycle/'guest/console-result.json';put(p,b'{"nonce":"a","nonce":"b"}');self.assert_unqualified(self.audit())
        p=self.cycle/'guest/policy-actual-adt-readback.bin';b=p.read_bytes();target=self.directory/'borrowed.bin';put(target,b);p.unlink();p.symlink_to(target);self.assert_unqualified(self.audit())
    def test_guest_identity216_independent_of_codec(self):
        p=self.cycle/'guest/console.bin';lines=p.read_bytes().splitlines(keepends=True)
        chunks=[i for i,s in enumerate(lines) if s.startswith(b'DL32_PRIVATE_RECORD_HEX ')];record=bytearray(b''.join(bytes.fromhex(lines[i].split()[-1].decode()) for i in chunks));record[72]^=1
        for n,i in enumerate(chunks):lines[i]=b' '.join(lines[i].split()[:-1])+b' '+record[n*64:(n+1)*64].hex().encode()+b'\n'
        i=next(i for i,s in enumerate(lines) if s.startswith(b'DL32_PRIVATE_RECORD_END '));lines[i]=lines[i].split(b'sha256=')[0]+b'sha256='+sha(record).encode()+b'\n';put(p,b''.join(lines))
        r=self.audit();self.assertEqual(r['checks'][a.CHECKS[6]]['status'],'PASS');self.assertEqual(r['checks'][a.CHECKS[3]]['status'],'FAILED');self.assert_unqualified(r)
if __name__=='__main__':unittest.main(verbosity=2)
