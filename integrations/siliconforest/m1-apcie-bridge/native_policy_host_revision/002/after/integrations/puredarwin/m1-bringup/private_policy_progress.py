"""Original isolated host-policy mode. No default actor or hardware permission.

The probe member and fixed command are design names, not built-driver evidence.
All operations run only through the verified host's explicit worker path. Tests
supply fake backends; no transport is opened by importing or verifying this file.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import sys

LAB=Path(__file__).resolve().parents[3]
FIELD='native_policy_experiment'
PROFILE='t8103-original-prepared-subset-source-facts-v2'
EXPERIMENT={'version':2,'producer_profile':PROFILE}
MEMBER='org.darwinlab.driver.PDBCMPrivateProbe'
COMMAND='/usr/bin/darwin-native-private-test'
# UNFROZEN PREPARATION: no runnable source authorization until producer002 freezes.
SOURCES={
 'integrations/puredarwin/m1-apcie-bridge/native_policy_producer_002/producer.py':None,
 'integrations/puredarwin/m1-apcie-bridge/native_policy_handoff_002/handoff.py':'087ece7919a22e5afbbd9246580bc981356ff1b1db9ea0736f46c08e364858ec',
 'integrations/puredarwin/m1-apcie-bridge/adt_source_inventory/inventory.py':'41863f689ae54e9c5d0d625f196c3f4242bd24beb1644e1fb08782a0b2e84b7b',
 'integrations/puredarwin/m1-apcie-bridge/mcc_capture/capture.py':'ab00b1d09d0f78a443ef0fff2f3050a052eb96995da1c47f57e1a578e8f42558',
 'integrations/puredarwin/m1-apcie-bridge/native_policy_snapshot/facts_wire.py':'473c1513110350a0b959500124a159dd726d2b7cc0cbcce79eae087d7684528a',
 'integrations/puredarwin/m1-apcie-bridge/mcc_snapshot/mcc_snapshot.c':'3ebeb0b56d308d84919dc489e7b5f47c4b201a7db62be763b9dd5a4ed27742b1',
 'integrations/puredarwin/m1-apcie-bridge/mcc_snapshot/mcc_snapshot.h':'7c3284afb294cc8b2a2820b820a0a947d7d15821d889bdddccd063e4708feeb8',
}

def require(ok,why):
    if not ok:raise ValueError(why)
def sha(raw):return hashlib.sha256(raw).hexdigest()
def unique(pairs):
    out={}
    for k,v in pairs:
        require(k not in out,'Duplicate build record key');out[k]=v
    return out
def record(inputs):
    value=json.loads(inputs.contents['build_record'],object_pairs_hook=unique)
    require(type(value) is dict,'Build record must be object');return value

def member_count(raw,name):
    require(type(raw) is bytes and len(raw)>=32,'Invalid fileset bytes')
    magic,cpu,_,kind,ncmds,total,_,_=struct.unpack_from('<8I',raw)
    require(magic==0xfeedfacf and cpu==0x100000c and kind==12 and 0<ncmds<=4096 and total<=len(raw)-32,'Invalid fileset header')
    off=32;end=off+total;found=0
    for _ in range(ncmds):
        require(off<=end-8,'Short fileset command');cmd,size=struct.unpack_from('<II',raw,off)
        require(size>=8 and size<=end-off,'Invalid fileset command')
        if cmd==0x80000035:
            require(size>=32,'Short fileset entry');at=struct.unpack_from('<I',raw,off+24)[0]
            require(32<=at<size,'Invalid fileset name offset');value=raw[off+at:off+size]
            require(b'\0' in value,'Unterminated fileset name')
            found+=value.split(b'\0',1)[0]==name.encode('ascii')
        off+=size
    require(off==end,'Fileset command extent mismatch');return found

def typed_mode(inputs):
    data=record(inputs)
    if FIELD not in data:return False
    experiment=data[FIELD]
    require(type(experiment) is dict and experiment.keys()==EXPERIMENT.keys() and
            all(type(experiment[k]) is type(v) and experiment[k]==v for k,v in EXPERIMENT.items()),'Invalid private-policy experiment')
    require(not any(k.endswith('_experiment') and k not in (FIELD,'native_bootargs_wire_experiment') for k in data),'Private policy forbids other experiment modes')
    native={'version':1,'wire_profile':'darwin-xnu-e7-arm64-bootargs1152-r2v2-v1','source_revision':2,'source_version':2}
    value=data.get('native_bootargs_wire_experiment')
    require(type(value) is dict and value.keys()==native.keys() and all(type(value[k]) is type(v) and value[k]==v for k,v in native.items()),'Private policy requires exact native-wire opt-in')
    require(type(inputs.contents.get('ramdisk')) is bytes and inputs.contents['ramdisk'],'Private command requires hash-bound RAM root')
    require(MEMBER in inputs.report['kernel']['members'] and member_count(inputs.contents['kernel'],MEMBER)==1,'Private probe design member absent/ambiguous; no probe artifact admitted')
    return True

def check_sources():
    for path,digest in SOURCES.items():
        with (LAB/path).open('rb') as source:raw=source.read((128<<10)+1)
        require(len(raw)<=128<<10 and sha(raw)==digest,'Private-policy helper changed: '+path)

def module(path,name):
    with (LAB/path).open('rb') as source:raw=source.read((128<<10)+1)
    require(len(raw)<=128<<10 and sha(raw)==SOURCES[path],'Private-policy helper changed: '+path)
    spec=importlib.util.spec_from_loader(name,loader=None,origin=str(LAB/path));value=importlib.util.module_from_spec(spec)
    value.__file__=str(LAB/path);sys.modules[name]=value;exec(compile(raw,value.__file__,'exec'),value.__dict__);return value

def helpers():
    check_sources()
    return (module('integrations/puredarwin/m1-apcie-bridge/native_policy_producer_002/producer.py','bound_private_policy_producer_v2'),
            module('integrations/puredarwin/m1-apcie-bridge/mcc_capture/capture.py','bound_private_mcc_capture'))

def verified_mode(inputs,native_check):
    if not typed_mode(inputs):return None
    producer,_=helpers();proof=native_check(inputs)
    require(proof is not None and producer.PROFILE==PROFILE,'Private policy needs selected compiled ABI proof')
    return dict(profile=PROFILE,probe_member=MEMBER,member_design_only_not_built_driver_proof=True,
                command=COMMAND,sources_sha256=dict(SOURCES),native_wire=proof,runtime_or_boot_admitted=False)

def command(nonce):
    require(type(nonce) is str and re.fullmatch('[0-9a-f]{48}',nonce),'Invalid private policy nonce')
    return (COMMAND+" '"+nonce+"'\r").encode('ascii')

def integer(value,label):
    require(isinstance(value,int) and not isinstance(value,bool),'Invalid '+label)
    value=int.__int__(value);require(type(value) is int and 0<=value<1<<64,'Invalid '+label);return value

def translated_mcc_reg(parsed):
    # Fixed T8103 hierarchy only. Pinned get_reg translates a base, so verify
    # the ENTIRE reg1 aperture has exactly one non-overlapping parent mapping.
    def value(path,key):return parsed[path].get(key).value
    for path in ('','/arm-io'):
        for key in ('#address-cells','#size-cells'):
            raw=value(path,key);require(len(raw)==4 and struct.unpack('<I',raw)[0]==2,'Unsupported MCC parent cell format')
    require(not any(p.key in ('ranges','dma-ranges') for p in parsed[''].properties),'Unsupported root translation')
    raw=value('/arm-io/mcc','reg');require(len(raw)==3*16,'Unsupported T8103 MCC register array')
    bus,extent=struct.unpack_from('<QQ',raw,16)
    require(extent>0 and bus+extent<1<<64,'Invalid MCC bus extent')
    ranges=value('/arm-io','ranges');require(ranges and len(ranges)%24==0,'Unsupported MCC parent ranges')
    hits=[]
    for child,parent,size in struct.iter_unpack('<QQQ',ranges):
        require(size>0 and child+size<1<<64 and parent+size<=1<<36,'Invalid MCC parent translation range')
        if max(bus,child)<min(bus+extent,child+size):
            require(child<=bus and bus+extent<=child+size,'MCC aperture crosses parent translation')
            hits.append(parent+bus-child)
    require(len(hits)==1,'MCC aperture lacks one unambiguous full parent translation')
    return hits[0],extent,sha(raw),sha(ranges)


def capture(backend,original_adt,original_bootargs,snapshot,compact,nonce,directory,write_private):
    require(not getattr(backend, 'private_policy_capture_started', False), 'MCC capture is single-use; no retry')
    backend.private_policy_capture_started = True
    producer,mcc=helpers()
    require(type(original_adt) is bytes and type(original_bootargs) is bytes and len(original_bootargs)==736 and struct.unpack_from('<HH', original_bootargs)==(2,2),'Missing exact original source bytes')
    command(nonce)
    require(backend.u.adt_data==original_adt,'Original ADT source changed')
    require(snapshot['product']['model']=='MacBookPro17,1' and snapshot['product']['target_type']=='J293' and
            snapshot['chip']['adt_chip_id']==snapshot['chip']['proxy_chip_id']==0x8103,'Private policy requires observed J293/T8103 preflight')
    tree=backend.adt_module.load_adt(original_adt)
    require(tree.model=='MacBookPro17,1' and tree.target_type=='J293' and integer(tree['chosen'].chip_id,'original chip')==0x8103,'Original ADT board mismatch')
    parsed=producer.handoff._parse(original_adt)
    require(parsed[mcc.NODE_PATH].get('compatible').value==b'mcc,t8103\0','Unsupported original MCC compatible')
    inv=module('integrations/puredarwin/m1-apcie-bridge/adt_source_inventory/inventory.py','bound_policy_original_inventory').extract(original_adt,profile='t8103-base-adt-source-inventory-v1')
    require(inv.dram.base==producer.facts.DRAM_BASE and inv.dram.bytes%4096==0, 'Invalid T8103 DRAM source')
    derived_base,derived_extent,reg_sha,ranges_sha=translated_mcc_reg(parsed)
    node=tree[mcc.NODE_PATH];base,extent=node.get_reg(mcc.REG_INDEX)
    base,extent=integer(base,'MCC reg1 base'),integer(extent,'MCC reg1 extent')
    require((base,extent)==(derived_base,derived_extent),'Pinned get_reg disagrees with full-window raw translation')
    source_sha=bytes.fromhex(sha(original_adt));policy_nonce=bytes.fromhex(nonce)
    region=mcc.Region(1,mcc.NODE_PATH,mcc.COMPATIBLE,1,base,extent,source_sha,policy_nonce)
    expected=mcc.Expected(source_sha,policy_nonce);proxy=backend.p;read32=proxy.read32
    collector=mcc.Collector(region,expected,lambda offset:read32(base+offset))
    def save_capture(result):
        write_private(directory,'policy-mcc-prefix.bin',result.prefix)
        metadata=dict(status=result.status,attempted=result.attempted,completed=result.completed,
                      original_adt_sha256=source_sha.hex(),policy_nonce=nonce,reg_index=1,
                      relative_plan='two passes/four planes/four TZ/three words=96 read32; no retry',
                      region_base=base,region_bytes=extent,reg_property_sha256=reg_sha,parent_ranges_sha256=ranges_sha,
                      whole_region_unique_parent_translation_checked=True,source_sha256=dict(SOURCES),
                      global_exclusion_coverage=False,mapping_or_read_safety_proven=False)
        if result.raw is not None:write_private(directory,'policy-mcc-full384.bin',result.raw)
        write_private(directory,'policy-mcc-capture.json',metadata)
        return metadata
    try:
        result=collector.capture()
    except BaseException:
        if collector.last is not None:save_capture(collector.last)
        raise
    metadata=save_capture(result)
    require(result.status=='RAW_COMPLETE_UNVALIDATED' and result.raw is not None and len(result.raw)==384,'Full MCC capture refused; no retry')
    require(backend.p is proxy and proxy.read32==read32 and backend.u.adt_data==original_adt,'Capture source/read binding changed')
    slots=producer.facts._mcc(result.raw,producer.facts.Range(inv.dram.base,inv.dram.bytes))
    full=sorted((span.base,span.bytes) for _,span in slots)
    require(type(compact) is list and sorted(tuple(x) for x in compact)==full,'Full MCC slots differ from old compact crosscheck')
    metadata.update(raw_sha256=sha(result.raw),full_snapshot_validated=True,compact_exact_slot_count_match=True,
                    compact_scope='old compact plane0 list crosscheck only, not replacement for384/full/global coverage')
    write_private(directory,'policy-mcc-validated.json',metadata)
    return dict(original_adt=original_adt,original_bootargs=original_bootargs,mcc_raw=result.raw,
                policy_nonce=nonce,metadata=metadata)

def preflight_capacity(hv,backend):
    # load_raw only changes/adds these six fixed16B memory-map descriptors;
    # final payload staging may add RAMDisk. OS_LOG removal only shrinks bytes.
    # Conservatively reserve all seven headers even when already present.
    old_limit=(integer(backend.ba.devtree_size,'original ADT bytes')+0x3fff)&~0x3fff
    base=hv.adt.build();growth=7*(36+16)
    require(type(base) is bytes and len(base)+growth+36+9472<=old_limit,
            'Maximum policy facts/known descriptor growth exceeds unchanged original ADT capacity')
    return dict(original_capacity=old_limit,current_base_bytes=len(base),
                conservative_descriptor_growth=growth,max_facts_and_property_bytes=36+9472,
                no_resize=True,source_contract_only_not_hardware_authorization=True)


class SealedADT:
    """Per-instance read view; underlying builder must retain exact base bytes."""
    def __init__(self,tree,base,payload):
        self._tree,self._base,self._payload=tree,base,payload
        self.active=False;self.await_upload=False;self.uploaded=False
    def __getitem__(self,key):return self._tree[key]
    def __iter__(self):return iter(self._tree)
    def __getattr__(self,key):return getattr(self._tree,key)
    def check_base(self):require(self._tree.build()==self._base,'Base ADT drift after producer plan')
    def build(self):
        self.check_base()
        if self.active:
            require(not self.await_upload and not self.uploaded,'Repeated final ADT build')
            self.await_upload=True
        return self._payload

def source_selection(hv,backend):
    # The runtime module is already bound to the pinned source tree. Normalize
    # only its actual enum instance; plain ints/foreign enums are not witnesses.
    proxy_module=backend.proxy_module;source_type=proxy_module.IODEV;source_object=hv.iodev
    require(type(source_object) is source_type and source_object is source_type.USB0,'Private source join requires exact pinned IODEV.USB0 enum')
    source_iodev=integer(source_object,'pinned HV source iodev')
    require(type(source_iodev) is int and source_iodev==4,'Private source join requires actual pinned HV USB0 selection')
    return proxy_module,source_type,source_object,source_iodev

def prepare(hv,backend,inputs,directory,reservation,capture_data,base_blob,address,capacity,native_values,write_private):
    producer,_=helpers();nonce=capture_data['policy_nonce']
    context=producer.Context(bytes.fromhex(nonce),bytes.fromhex(sha(inputs.contents['kernel'])),
                             bytes.fromhex(sha(inputs.contents['ramdisk'])),producer.HV_SHA256)
    original_adt=capture_data['original_adt']
    proxy_module,source_type,source_object,source_iodev=source_selection(hv,backend)
    require(type(original_adt) is bytes and original_adt and backend.u.adt_data==original_adt,'Original ADT source changed before source join')
    def source_unchanged():
        require(backend.proxy_module is proxy_module and proxy_module.IODEV is source_type and hv.iodev is source_object and type(hv.iodev) is source_type and integer(hv.iodev,'pinned HV source iodev')==source_iodev and capture_data['original_adt']==original_adt and backend.u.adt_data==original_adt,'Source selection/original ADT changed during source join')
    measured=producer.measure(base_blob,capture_data['original_bootargs'],capture_data['mcc_raw'],context,
                              profile=PROFILE,original_profile=producer.ORIGINAL_PROFILE,
                              original_adt=original_adt,source_iodev=source_iodev)
    source_unchanged()
    old_limit=integer(backend.ba.devtree_size,'original ADT bytes');old_limit=(old_limit+0x3fff)&~0x3fff
    require(capacity==old_limit and measured.final_adt_bytes<=capacity,'Final policy ADT exceeds unchanged original capacity')
    require(hv.adt_base==address and hv.tba.top_of_kernel_data==reservation['end'],'Prepared ADT/top changed from old placement')
    hv.tba.devtree_size=measured.final_adt_bytes
    native=producer.bootargs.encode(native_values(hv.tba),wire_profile=producer.bootargs.WIRE_PROFILE)
    bundle=producer.produce(base_blob,capture_data['original_bootargs'],native,capture_data['mcc_raw'],context,
                            producer.handoff.Allocation(address,capacity),profile=PROFILE,original_profile=producer.ORIGINAL_PROFILE,
                            original_adt=original_adt,source_iodev=source_iodev)
    source_unchanged()
    require(bundle.measurement==measured and len(bundle.plan.payload)==measured.final_adt_bytes,'Policy measurement changed')
    receipt=measured.source_join_receipt
    require(type(receipt) is bytes and 0<len(receipt)<=65536 and measured.original_adt_sha256==sha(original_adt) and type(measured.source_iodev) is int and measured.source_iodev==source_iodev,'Invalid exact source-join receipt/binding')
    write_private(directory,'policy-source-selection.json',dict(version=2,profile=PROFILE,
        selection_source='pinned_HV.iodev',source_enum_class=source_type.__name__,source_enum_name=source_object.name,
        source_iodev=source_iodev,hv_source_sha256=producer.HV_SHA256.hex(),
        original_adt_sha256=sha(original_adt),prepared_adt_sha256=sha(base_blob),policy_nonce=nonce,
        source_join_receipt_sha256=sha(receipt),source_join_receipt_bytes=len(receipt),
        source_binding_only_not_independent_origin_authentication=True))
    write_private(directory,'policy-source-join.json',receipt)
    source_unchanged()
    view=SealedADT(hv.adt,base_blob,bundle.plan.payload);hv.adt=view
    state=dict(view=view,bundle=bundle,native=native,address=address,capacity=capacity,
               attempts=0,readback_verified=False,start_calls=0,active=False,policy_nonce=nonce)
    backend.private_policy_state=state
    write_private(directory,'policy-facts.bin',bundle.wire)
    write_private(directory,'policy-base-adt.bin',base_blob)
    write_private(directory,'policy-planned-adt.bin',bundle.plan.payload)
    write_private(directory,'policy-plan.json',bundle.metadata())
    return bundle.plan.payload,dict(bundle.metadata(),unchanged_ADT_capacity=capacity,
                                   planned_bytes_are_not_actual_upload=True,policy_nonce=nonce)

def start_wrapper(hv,backend,directory,state,original_start,write_private,rehash):
    view=state['view'];payload=state['bundle'].plan.payload;address=state['address']
    def checked_start(*args,**kwargs):
        require(not state['active'],'Private policy start is single-use')
        state['active']=True;rehash();check_sources();view.check_base()
        require(hv.adt is view and hv.adt_base==address,'Private policy ADT view/destination changed')
        saved_pointer = tuple(integer(getattr(hv,key),key) for key in ('entry','guest_base','bootargs_off'))
        require(saved_pointer[1]+saved_pointer[2] < 1<<64, 'Private start pointer overflow')
        def unchanged_pointer():
            require(tuple(integer(getattr(hv,key),key) for key in ('entry','guest_base','bootargs_off')) == saved_pointer, 'Private start arguments changed')
        original_write=backend.iface.writemem;original_jump=backend.p.hv_start
        view.active=True
        def guarded_write(at,raw,*a,**k):
            if view.await_upload:
                require(state['attempts']==0 and at==address and type(raw) is bytes and raw==payload,'Unexpected actual final ADT upload')
                state['attempts']=1  # Journal before calling original transport.
                result=original_write(at,raw,*a,**k)
                actual=backend.iface.readmem(address,len(payload))
                if type(actual) is bytes and 0<len(actual)<=len(payload):write_private(directory,'policy-actual-adt-readback.bin',actual)
                require(type(actual) is bytes and actual==payload,'Actual final ADT readback short/corrupt')
                unchanged_pointer();view.check_base();require(hv.adt is view and hv.adt_base==address,'Actual ADT destination/view changed during readback')
                state['readback_verified']=True;view.await_upload=False;view.uploaded=True
                write_private(directory,'policy-actual-adt-upload.bin',actual)
                write_private(directory,'policy-actual-upload.json',dict(address=address,bytes=len(actual),sha256=sha(actual),
                    exact_actual_upload_and_readback=True,attempts=1,policy_nonce=state['policy_nonce'],runtime_or_boot_admitted=False))
                return result
            # Existing pinned HV start may upload utility instruction buffers
            # before/after ADT build. This guard adds no such write or authority.
            require(at!=address and raw!=payload and not (type(at) is int and type(raw) is bytes and
                    max(at,address)<min(at+len(raw),address+state['capacity'])),'Duplicate/overlapping ADT write outside final upload')
            return original_write(at,raw,*a,**k)
        def guarded_jump(entry,bootargs,*a,**k):
            require(state['readback_verified'] and state['attempts']==1 and view.uploaded and not view.await_upload,
                    'HV_START before exact actual final ADT upload/readback')
            unchanged_pointer();view.check_base();require(hv.adt is view and hv.adt_base==address,'ADT changed before actual HV_START')
            require(not a and not k and (integer(entry,'actual entry'),integer(bootargs,'actual BootArgs')) == (saved_pointer[0],saved_pointer[1]+saved_pointer[2]), 'Private actual HV_START arguments differ')
            state['start_calls']+=1;require(state['start_calls']==1,'Repeated policy HV_START')
            return original_jump(entry,bootargs,*a,**k)
        backend.iface.writemem=guarded_write;backend.p.hv_start=guarded_jump
        return original_start(*args,**kwargs)
    return checked_start
