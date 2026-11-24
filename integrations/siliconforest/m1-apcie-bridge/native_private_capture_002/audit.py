"""Original OFFLINE saved-byte auditor. No controller/target entrypoint.

P1 packet and P10 recovery documents remain explicit external trust inputs.
No source identity label authenticates its caller; no missing capture is rebuilt.
"""
from pathlib import Path
import ast,hashlib,importlib.util,json,os,re,stat,struct,sys

ROOT=Path(__file__).resolve().parents[4]
BRIDGE=ROOT/'integrations/puredarwin/m1-apcie-bridge'
HOST=BRIDGE/'native_policy_host_revision/001'
HOST_RECEIPT='ad3242cf2d36f0dd9686b1aa030535269ddc74b58f08cd44c618a29186199524'
PINS={
 'native_policy_producer/producer.py':'9dcfe81316089e3328469cc26d1320cfec3d5b4042ed20da605fb7577368346d',
 'native_policy_handoff_002/handoff.py':'087ece7919a22e5afbbd9246580bc981356ff1b1db9ea0736f46c08e364858ec',
 'adt_source_inventory/inventory.py':'41863f689ae54e9c5d0d625f196c3f4242bd24beb1644e1fb08782a0b2e84b7b',
 'mcc_capture/capture.py':'ab00b1d09d0f78a443ef0fff2f3050a052eb96995da1c47f57e1a578e8f42558',
 'native_policy_snapshot/facts_wire.py':'473c1513110350a0b959500124a159dd726d2b7cc0cbcce79eae087d7684528a',
 'mcc_snapshot/mcc_snapshot.c':'3ebeb0b56d308d84919dc489e7b5f47c4b201a7db62be763b9dd5a4ed27742b1',
 'mcc_snapshot/mcc_snapshot.h':'7c3284afb294cc8b2a2820b820a0a947d7d15821d889bdddccd063e4708feeb8',
 'native_bootargs/native_bootargs.py':'f168d523eacf1c8fe1228ca48e467fb98fea47b4675bb942ca945aae63233fae',
 'native_private_probe/collector/decode.py':'d1d97666915eb29c6d83e0310360dc4287a138f11f5879768604266cb1bf2bfa',
}
AFTER={
 'integrations/puredarwin/m1-bringup/host_progress.py':'dd02e8c2f737571aaff8fb7db38f2c926b86aedac91ff3e7373e27235b1665cf',
 'integrations/puredarwin/m1-bringup/private_policy_progress.py':'e099e953fbefe323e03e097b861606781762a53b243fec2039d0c2ee2cbb5c8c',
 'tools/m1_bringup_cycle.py':'38867b93c985f22710ffb3efcbb173e30921a16b91f6bc9bd0d04ab4721ae9da',
}
CHECKS=('P1_packet_and_sources','P2_original_sources_and_replay','P3_actual_upload_and_placement',
 'P4_host_guest_identity216','P5_single_private_order','P6_exact_lifecycle','P7_strict_record_and_exit',
 'P8_self_report_scope','P9_sealed_TX_and_capture','P10_external_recovery')
class Missing(ValueError):pass
def require(ok,msg):
    if not ok:raise ValueError(msg)
def sha(raw):return hashlib.sha256(raw).hexdigest()
def read(path,cap):
    fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
    try:
        a=os.fstat(fd);require(stat.S_ISREG(a.st_mode) and 0<=a.st_size<=cap,'regular file/size bound')
        chunks=[];left=cap+1
        while left:
            b=os.read(fd,min(left,1<<20))
            if not b:break
            chunks.append(b);left-=len(b)
        b=os.fstat(fd);raw=b''.join(chunks)
        require(len(raw)==a.st_size and (a.st_dev,a.st_ino,a.st_size,a.st_mtime_ns,a.st_ctime_ns)==(b.st_dev,b.st_ino,b.st_size,b.st_mtime_ns,b.st_ctime_ns),'changed file')
        return raw
    finally:os.close(fd)
def unique(pairs):
    d={}
    for k,v in pairs:require(k not in d,'duplicate JSON key');d[k]=v
    return d
def js(raw):
    value=json.loads(raw,object_pairs_hook=unique,parse_constant=lambda _:(_ for _ in ()).throw(ValueError('nonfinite JSON')))
    require(type(value) is dict,'JSON object required');return value
def document(ref):
    require(type(ref) is dict and set(ref)=={'path','sha256'} and type(ref['path']) is str and type(ref['sha256']) is str and re.fullmatch('[0-9a-f]{64}',ref['sha256']),'document reference')
    raw=read(Path(ref['path']),1<<20);require(sha(raw)==ref['sha256'],'external document hash');return js(raw)
def file_sha(path,cap):
    fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
    try:
        a=os.fstat(fd);require(stat.S_ISREG(a.st_mode) and 0<a.st_size<=cap,'artifact size/type');digest=hashlib.sha256();total=0
        while True:
            b=os.read(fd,1<<20)
            if not b:break
            total+=len(b);require(total<=cap,'artifact overflow');digest.update(b)
        b=os.fstat(fd);require(total==a.st_size and (a.st_dev,a.st_ino,a.st_size,a.st_mtime_ns,a.st_ctime_ns)==(b.st_dev,b.st_ino,b.st_size,b.st_mtime_ns,b.st_ctime_ns),'artifact changed')
        return digest.hexdigest()
    finally:os.close(fd)
def module(relative,name):
    p=BRIDGE/relative;raw=read(p,128<<10);require(sha(raw)==PINS[relative],'frozen replay source drift: '+relative)
    m=importlib.util.module_from_spec(importlib.util.spec_from_loader(name,loader=None,origin=str(p)));m.__file__=str(p);sys.modules[name]=m;exec(compile(raw,str(p),'exec'),m.__dict__);return m
def assignments(raw):
    """Read literal constants, including prior literal names, without imports."""
    values={}
    def value(node):
        if isinstance(node,ast.Name):return values[node.id]
        if isinstance(node,ast.Dict):return {value(k):value(v) for k,v in zip(node.keys,node.values)}
        if isinstance(node,(ast.Tuple,ast.List,ast.Set)):return [value(n) for n in node.elts]
        return ast.literal_eval(node)
    for node in ast.parse(raw).body:
        if isinstance(node,ast.Assign) and len(node.targets)==1 and isinstance(node.targets[0],ast.Name):
            try:values[node.targets[0].id]=value(node.value)
            except (ValueError,KeyError,TypeError):pass
    return values
def snapshot_pins():
    receipt=read(HOST/'source-validation-001.json',1<<20);require(sha(receipt)==HOST_RECEIPT,'host receipt drift');freeze=js(receipt)['files_sha256']
    # The immutable after-tree anchors actual controller constants/snapshot names.
    for n,h in AFTER.items():require(freeze['after/'+n]==h and sha(read(HOST/'after'/n,128<<10))==h,'host after-tree drift')
    controller=assignments(read(HOST/'after/tools/m1_bringup_cycle.py',128<<10))
    base_raw=read(HOST/'after/tools/m1_kernel_cycle.py',128<<10)
    require(sha(base_raw)==freeze['after/tools/m1_kernel_cycle.py'],'base snapshot-list source drift')
    base=assignments(base_raw)
    names=set(base['SOURCE_SHA'])|{controller[n] for n in ('LAUNCHER','INSPECTOR','PROGRESS_BOOTSTRAP','BOOTARGS_HELPER','INPUT_HELPER','TERMINAL_HELPER')}|{'tools/m1_kernel_cycle.py','tools/m1_bringup_cycle.py'}|set(controller['NATIVE_BOOTARGS_SOURCES'])|{controller['PRIVATE_POLICY_HELPER']}|set(controller['PRIVATE_POLICY_SOURCES'])
    return {n:freeze['after/'+n] for n in sorted(names)}
def mcc_translation(parsed):
    def val(path,key):return parsed[path].get(key).value
    for path in ('','/arm-io'):
        for key in ('#address-cells','#size-cells'):require(val(path,key)==struct.pack('<I',2),'original MCC parent cells')
    require(not any(p.key in ('ranges','dma-ranges') for p in parsed[''].properties),'original root translation')
    require(val('/arm-io/mcc','compatible')==b'mcc,t8103\0','original MCC compatible')
    reg=val('/arm-io/mcc','reg');ranges=val('/arm-io','ranges');require(len(reg)==48 and ranges and len(ranges)%24==0,'original MCC reg/ranges shape')
    bus,extent=struct.unpack_from('<QQ',reg,16);require(extent>0 and bus+extent<1<<64,'original MCC extent');hits=[]
    for child,parent,size in struct.iter_unpack('<QQQ',ranges):
        require(size>0 and child+size<1<<64 and parent+size<=1<<36,'original MCC range')
        if max(bus,child)<min(bus+extent,child+size):
            require(child<=bus and bus+extent<=child+size,'MCC crosses range');hits.append(parent+bus-child)
    require(len(hits)==1,'unambiguous full MCC aperture');return hits[0],extent,sha(reg),sha(ranges)
def challenge(nonce):
    require(type(nonce) is str and re.fullmatch('[0-9a-f]{48}',nonce),'console nonce')
    a,b=nonce[:24],nonce[24:]
    return ("PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH; "+f"printf '\\nDL9_BEGIN_%s%s\\n' '{a}' '{b}'; "+"{ uname -s; uname -m; uname -r; } 2>/dev/null; "+f"printf 'DL9_END_%s%s\\n' '{a}' '{b}'\r").encode()
def launcher(nonce):
    require(type(nonce) is str and re.fullmatch('[0-9a-f]{48}',nonce),'policy nonce')
    return ("/usr/bin/darwin-native-private-test '"+nonce+"'\r").encode()
def kernel_geometry(raw):
    path=HOST/'after/integrations/puredarwin/m1-bringup/normal_image.py'
    source=read(path,128<<10);require(sha(source)=='ea36952b5d68118583f03bdf756683a4a83b534fd4d1a96a62a23e2e54a8b062','frozen kernel inspector drift')
    m=importlib.util.module_from_spec(importlib.util.spec_from_loader('private_capture_actual_image',loader=None));m.__file__=str(path);exec(compile(source,str(path),'exec'),m.__dict__)
    report=m.inspect(raw)
    return {k:report[k] for k in ('entry','vmin')}
def image_context(context):
    require(type(context) is dict and set(context)<= {'kernel','ramdisk','packet_binding'},'context keys')
    out={}
    for key in ('kernel','ramdisk'):
        ref=context.get(key);require(type(ref) is dict and set(ref)=={'path','sha256'},'actual '+key+' reference')
        actual=file_sha(Path(ref['path']),128<<20 if key=='kernel' else 4<<30)
        require(actual==ref['sha256'],'actual '+key+' hash');out[key+'_sha256']=actual
    kernel=read(Path(context['kernel']['path']),128<<20);require(sha(kernel)==out['kernel_sha256'],'kernel changed before inspection')
    out['actual_kernel_layout_private']=kernel_geometry(kernel)
    return out
def external_packet(context,images):
    if 'packet_binding' not in context:raise Missing('external hash-bound packet binding absent')
    binding=document(context['packet_binding'])
    require(binding.get('kind')=='PRIVATE_PACKET_EXTERNAL_BINDING_V1' and binding.get('status')=='VERIFIED_EXTERNALLY','external packet binding status/schema')
    require(binding.get('kernel_sha256')==images['kernel_sha256'] and binding.get('ramdisk_sha256')==images['ramdisk_sha256'],'packet/artifact binding')
    manifest=document(binding['manifest']);review=document(binding['review'])
    require(set(manifest['artifacts'])=={'kernel','ramdisk','source_patch','build_record'},'exact approved four-artifact set')
    approved={k:v['sha256'] for k,v in manifest['artifacts'].items()}
    require(all(type(v) is str and re.fullmatch('[0-9a-f]{64}',v) for v in approved.values()),'approved artifact hash shape')
    require(type(review.get('status')) is str and review['status'].startswith(('APPROVED','ADMIT')),'external review not approved')
    require(manifest['artifacts']['kernel']['sha256']==images['kernel_sha256'] and manifest['artifacts']['ramdisk']['sha256']==images['ramdisk_sha256'],'actual manifest/artifact link')
    require(review.get('blockers')==[] and type(review.get('checks')) is list and 0<len(review['checks'])<=64 and all(r.get('status')=='PASS' for r in review['checks']),'external packet review incomplete')
    rows=[r for r in review['checks'] if r.get('name')=='four_artifacts'];require(len(rows)==1 and rows[0]['evidence']==manifest['artifacts'],'review exact manifest artifact metadata')
    return {'packet_binding_sha256':context['packet_binding']['sha256'],'packet_manifest_sha256':binding['manifest']['sha256'],'packet_review_sha256':binding['review']['sha256'],'approved_artifacts_private':approved}
def audit(cycle_directory,context,recovery=None):
    checks={k:{'status':'NOT_EVALUATED','errors':[]} for k in CHECKS};details={'scope':'saved-byte chain + guest SELF-REPORT only; no WiFi/DMA/data readback','data_readback':False,'source_authentication':False};cache={};hashes={}
    try:cycle=Path(cycle_directory);guest=cycle/'guest'
    except TypeError:
        for k in checks:checks[k].update(status='FAILED',errors=['invalid explicit cycle directory'])
        return {'schema_version':1,'status':'UNQUALIFIED_SAVED_CAPTURE_CHAIN','qualified':False,'checks':checks,'details':details,'input_sha256':{},'data_readback':False,'WiFi_ready':False,'external_prerequisites_are_trusted_not_authenticated':True}
    def check(key,fn):
        try:fn();checks[key]['status']='PASS'
        except Missing as e:checks[key].update(status='NOT_EVALUATED',errors=[str(e)])
        except Exception as e:checks[key].update(status='FAILED',errors=[type(e).__name__+': '+str(e)])
    def raw(name,cap=16<<20,parent=False):
        p=(cycle if parent else guest)/name
        if p not in cache:cache[p]=read(p,cap);hashes[str(p.relative_to(cycle))]=sha(cache[p])
        return cache[p]
    def obj(name,parent=False):return js(raw(name,1<<20,parent))
    def need(name):
        if name not in details:raise Missing('prerequisite not verified: '+name)
        return details[name]
    # Missing context cannot shrink the required checks or cause an exception.
    if type(context) is not dict:context={}
    def sources():
        for name,digest in PINS.items():require(sha(read(BRIDGE/name,128<<10))==digest,'replay source drift: '+name)
        pins=snapshot_pins()
        for name,digest in pins.items():require(sha(raw('source-'+name.replace('/','__'),128<<10,True))==digest,'executed source snapshot: '+name)
        details['executed_sources_verified']=len(pins)
    # Evaluate sources independently of packet availability: later replay must
    # not silently run unpinned code after an external prerequisite is missing.
    source_error=None
    try:sources()
    except Exception as e:source_error=type(e).__name__+': '+str(e)
    def packet():
        require(source_error is None,source_error or '')
        details.update(image_context(context));details.update(external_packet(context,details))
    check(CHECKS[0],packet)
    def replay():
        require(source_error is None,source_error or '')
        producer=module('native_policy_producer/producer.py','private_capture_frozen_producer')
        original=raw('original-adt.bin');old=raw('original-bootargs.bin',736);native=raw('guest-bootargs.bin',1152)
        require(len(old)==736 and struct.unpack_from('<HH',old)==(2,2),'actual original736/R2V2')
        require(len(native)==1152 and raw('guest-bootargs-readback.bin',1152)==native,'native1152 exact actual readback')
        full=raw('policy-mcc-full384.bin',384);prefix=raw('policy-mcc-prefix.bin',384);require(len(full)==384 and prefix==full,'full384/prefix actual bytes')
        metadata=obj('policy-mcc-capture.json');validated=obj('policy-mcc-validated.json');source=obj('private-policy-source-context.json');nonce=source['policy_nonce'];launcher(nonce)
        require(metadata.get('status')=='RAW_COMPLETE_UNVALIDATED' and type(metadata.get('attempted')) is int and metadata['attempted']==96 and type(metadata.get('completed')) is int and metadata['completed']==96,'MCC attempt/completion96')
        require(metadata.get('policy_nonce')==nonce and metadata.get('original_adt_sha256')==sha(original) and validated.get('raw_sha256')==sha(full) and validated.get('full_snapshot_validated') is True and validated.get('compact_exact_slot_count_match') is True,'MCC raw/source validation linkage')
        require(all(validated.get(k)==v for k,v in metadata.items()) and validated.get('policy_nonce')==nonce,'validated MCC metadata same exact capture context')
        translated=mcc_translation(producer.handoff._parse(original))
        require(tuple(metadata[k] for k in ('region_base','region_bytes','reg_property_sha256','parent_ranges_sha256'))==translated and metadata.get('whole_region_unique_parent_translation_checked') is True and metadata.get('reg_index')==1,'actual original-ADT MCC translation binding')
        mcc=module('mcc_capture/capture.py','private_capture_bound_mcc_plan')
        region=mcc.Region(mcc.PROFILE,mcc.NODE_PATH,mcc.COMPATIBLE,mcc.REG_INDEX,translated[0],translated[1],bytes.fromhex(sha(original)),bytes.fromhex(nonce))
        bound,_=mcc._snap(region,mcc.Expected(bytes.fromhex(sha(original)),bytes.fromhex(nonce)))
        offsets=mcc.relative_plan(bound.profile,bound.extent)
        require(len(offsets)==96 and all(0<=off<=bound.extent-4 for off in offsets),'actual frozen96read aperture bounds')
        base=raw('policy-base-adt.bin');require(raw('guest-adt.bin')==base,'actual prepared base ADT')
        inv=module('adt_source_inventory/inventory.py','private_capture_inventory')
        original_inv=inv.extract(original,profile='t8103-base-adt-source-inventory-v1');base_inv=inv.extract(base,profile='t8103-base-adt-source-inventory-v1')
        selected=lambda i:tuple((p.path,p.key,p.value) for p in i.properties)
        require(selected(original_inv)==selected(base_inv),'original/base actual physical source properties differ')
        values=producer.bootargs.decode(native,wire_profile=producer.bootargs.WIRE_PROFILE)
        parsed=producer.handoff._parse(base);pair=parsed['/chosen/memory-map'].get('DeviceTree').value;require(len(pair)==16,'ADT allocation shape');address,capacity=struct.unpack('<QQ',pair)
        # Original reservation capacity must stay exact; no derived resize.
        original_size=struct.unpack_from('<I',old,104)[0];require(capacity==((original_size+16383)&~16383),'unchanged original ADT capacity')
        ctx=producer.Context(bytes.fromhex(nonce),bytes.fromhex(need('kernel_sha256')),bytes.fromhex(need('ramdisk_sha256')),producer.HV_SHA256)
        bundle=producer.produce(base,old,native,full,ctx,producer.handoff.Allocation(address,capacity),profile=producer.PROFILE,original_profile=producer.ORIGINAL_PROFILE)
        require(raw('policy-facts.bin',9472)==bundle.wire and raw('policy-planned-adt.bin')==bundle.plan.payload,'exact producer/handoff replay')
        require(obj('policy-plan.json')==json.loads(json.dumps(bundle.metadata())),'saved producer/handoff metadata replay')
        require(sha(original)==metadata['original_adt_sha256'],'original raw ADT identity')
        details['policy_nonce']=nonce;details['expected216']=(ctx.nonce+b''.join(bundle.expected.identities)).hex();details['replay_done']=True
        details['replay_counts']=list(bundle.measurement.source_counts);details['allocation_private']=(address,capacity,values)
    check(CHECKS[1],replay)
    def actual_upload():
        need('replay_done');address,capacity,values=details['allocation_private'];planned=raw('policy-planned-adt.bin')
        require(raw('policy-actual-adt-upload.bin')==planned and raw('policy-actual-adt-readback.bin')==planned,'actual final upload/readback bytes')
        upload=obj('policy-actual-upload.json');require(upload.get('exact_actual_upload_and_readback') is True and type(upload.get('attempts')) is int and upload['attempts']==1 and upload.get('address')==address and upload.get('bytes')==len(planned) and upload.get('sha256')==sha(planned) and upload.get('policy_nonce')==details['policy_nonce'],'actual upload journal')
        ba=obj('guest-bootargs-record.json');state=ba['start_pointer_state'];alloc=ba['checked_allocation'];before=obj('before-start.json');layout=before['layout']
        require(ba.get('readback_exact') is True and ba.get('readback_sha256')==sha(raw('guest-bootargs.bin',1152)) and ba.get('payload_sha256')==sha(raw('guest-bootargs.bin',1152)),'native readback metadata')
        require(state['guest_base']==layout['guest_base'] and state['bootargs_off']==layout['bootargs_off'] and state['entry']==layout['entry'] and state['address']==state['guest_base']+state['bootargs_off'] and state['adt_base']==address,'saved start-pointer components')
        # Bind saved metadata to ACTUAL retained raw allocation and native RAM,
        # rather than accepting several mutually consistent forged zero fields.
        producer=module('native_policy_producer/producer.py','private_capture_pointer_wire')
        parsed=producer.handoff._parse(raw('policy-base-adt.bin'))
        boot=parsed['/chosen/memory-map'].get('BootArgs').value;require(len(boot)==16,'actual BootArgs descriptor shape')
        actual_base,actual_bytes=struct.unpack('<QQ',boot)
        require(all(type(state.get(k)) is int and 0<state[k]<1<<64 for k in ('guest_base','bootargs_off','address','entry','adt_base')),'nonzero typed start fields')
        require(state['guest_base']+state['bootargs_off']<1<<64 and state['address']==actual_base and actual_bytes==16384 and actual_base%16384==0 and address==actual_base+16384,'actual BootArgs descriptor/start/address coherence')
        low=values['phys_base'];high=low+values['mem_size'];top=values['top_of_kernel_data']
        require(0<low<=state['guest_base']<=state['entry']<actual_base and actual_base+actual_bytes<=address and address+capacity<=top<=high<=1<<36 and state['guest_base']%16384==0 and state['entry']%4==0,'source-bound entry/BootArgs inside native RAM/retained prefix')
        observations=obj('original-observations.json');predicted=observations['layout'];selected=obj('verified-inputs.json',True)['candidate']['kernel']
        require(all(type(predicted.get(k)) is int and type(layout.get(k)) is int and predicted[k]==layout[k] for k in ('phys_base','guest_base','bootargs_off','entry')) and layout['phys_base']==low,'original/before/native layout coherence')
        require(selected.get('vmin')==need('actual_kernel_layout_private')['vmin'] and selected.get('entry')==need('actual_kernel_layout_private')['entry'],'saved kernel entry/vmin differs from actual selected kernel bytes')
        require(type(selected.get('vmin')) is int and type(selected.get('entry')) is int and 0<=selected['vmin']<=selected['entry']<1<<64 and state['entry']==state['guest_base']+selected['entry']-selected['vmin'],'selected-source kernel entry translation consistency')
        require(alloc['base']==state['address'] and alloc['expected_address']==state['address'] and alloc['bytes']==16384 and alloc['base']+16384<=address and values['devtree_size']==len(planned) and len(planned)<=capacity,'unchanged placement/extents')
        require(before['payloads']['adt']['address']==address and before['payloads']['adt']['capacity']==capacity and before['payloads']['adt']['sha256']==sha(planned),'before-start final ADT')
        cap=obj('private-policy-capacity-preflight.json')
        require(cap.get('original_capacity')==capacity and cap.get('no_resize') is True and cap.get('conservative_descriptor_growth')==7*52 and cap.get('max_facts_and_property_bytes')==36+9472 and type(cap.get('current_base_bytes')) is int and 0<cap['current_base_bytes']<=capacity-7*52-36-9472,'saved original-capacity preflight')
        require(values['cmdline']==before['bootargs'] and values['cmdline'].split().count('pdbcmprivate=1')==1,'actual selected native private bootarg')
        details['actual_upload_verified']=True
        details['HV_START_guard_scope']='Saved pointer/layout/native consistency + hash-bound executed7Qguard; raw proxy7Qframe/start_calls not saved, never reconstructed or independently claimed.'
    check(CHECKS[2],actual_upload)
    def record():
        require(source_error is None,source_error or '');need('replay_done');nonce=details['policy_nonce'];console=raw('console.bin',32<<20)
        lines=console.splitlines(keepends=True);norm=[line.rstrip(b'\r\n') for line in lines]
        starts=[i for i,s in enumerate(norm) if s.startswith(b'DL32_PRIVATE_PROCESS_BEGIN ')];ends=[i for i,s in enumerate(norm) if s.startswith(b'DL32_PRIVATE_PROCESS_END ')]
        require(len(starts)==len(ends)==1 and starts[0]<ends[0],'one complete process block')
        require(norm[starts[0]]==b'DL32_PRIVATE_PROCESS_BEGIN '+nonce.encode(),'matching process BEGIN')
        require(norm[ends[0]]==b'DL32_PRIVATE_PROCESS_END '+nonce.encode()+b' collector_exit=0 self_report_only=true','actual matching collector_exit0')
        span=b''.join(lines[starts[0]+1:ends[0]]);decoder=module('native_private_probe/collector/decode.py','private_capture_frozen_decoder');summary=decoder.decode_span(span,nonce)
        require(summary['classification']=='CONFIG_AND_PRIVATE_LIFECYCLE_SELF_REPORT' and summary['record_nonce_bound'] is True,'enabled self-report only')
        rawrecord=b''.join(bytes.fromhex(line.rstrip(b'\r\n').split(b' ')[-1].decode()) for line in lines[starts[0]+2:ends[0]-2])
        require(len(rawrecord)==768 and sha(rawrecord)==summary['raw_sha256'],'exact decoded raw record')
        details['raw_record_private']=rawrecord;details['record_summary']=summary;details['process_exit_verified']=True
    check(CHECKS[6],record)
    def identity():require(need('raw_record_private')[48:264].hex()==need('expected216'),'actual guest216 differs from independently hashed host context')
    check(CHECKS[3],identity)
    def order():
        summary=need('record_summary');require(summary['attempts']==255 and summary['operation_statuses']==[0]*8,'all8 bounded ordered attempts/source-defined statuses')
        require(details['process_exit_verified'],'missing normal collector completion')
    check(CHECKS[4],order)
    def lifecycle():
        s=need('record_summary');require(s['pre_snapshot']==[3,0,0,1,0,0,3,3,0,0,0,0,0,0] and s['post_snapshot']==[6,0,0,1,1,0,0,3,3,3,0,0,0,0],'actual source lifecycle snapshots')
    check(CHECKS[5],lifecycle)
    def scope():
        s=need('record_summary');require(s['data_readback'] is False and s['effect_commands'] is False and s['trusted_host_labels'] is True,'forbidden readback/effect/authority claim')
    check(CHECKS[7],scope)
    def capture():
        result=obj('console-result.json');console=raw('console.bin',32<<20);tx=raw('console-tx.bin',16384);nonce=result['nonce'];expected=challenge(nonce);policy=details.get('policy_nonce',obj('private-policy-source-context.json')['policy_nonce']);private=launcher(policy)
        require(nonce!=policy and raw('challenge-input.bin',4096)==expected and raw('private-policy-input.bin',4096)==private,'independent nonce/exact fixed inputs')
        require(tx in (expected+private,b'/bin/sh\r'+expected+private),'sealed exact TX, no extra input')
        require(result.get('native_private_policy_enabled') is True and result.get('private_command_sent') is True and result.get('policy_nonce')==policy and result.get('native_mode_host_write_phase')=='sealed','exclusive private mode/sealed flags')
        for k in ('desktop_command_sent','keyboard_command_sent','terminal_input_enabled','native_keyboard_desktop_enabled','native_trackpad_child_enabled'):require(result.get(k) is False,'foreign experiment flag '+k)
        require(result.get('error') is None and result.get('challenge_sent') is True and result.get('command_line_observed') is True and result.get('console_sha256')==sha(console) and result.get('console_bytes')==len(console) and result.get('console_tx_sha256')==sha(tx) and result.get('console_tx_bytes')==len(tx),'console/TX receipt')
        pattern=b'\nDL9_BEGIN_'+nonce.encode()+rb'\nDarwin\narm64\n[0-9][0-9A-Za-z_.+-]{0,100}\nDL9_END_'+nonce.encode()+b'\n';require(len(re.findall(pattern,console.replace(b'\r',b'')))==1,'actual strict challenge response')
        normalized=console.replace(b'\r',b'');response=re.search(pattern,normalized);require(response.end()<=normalized.index(b'DL32_PRIVATE_PROCESS_BEGIN '+policy.encode()),'challenge completes before private process')
        core=obj('capture-core.json');end=obj('exception-context.json');worker=obj('host-result.json')
        require(core.get('console_capture_finalized') is True and core.get('console_close_error') is None and end.get('diagnostics_complete') is True and end.get('diagnostics',{}).get('complete') is True and end['diagnostics'].get('error') is None and end.get('context_error') is None,'actual capture/diagnostic completion')
        require(worker.get('status')=='HOST_WORKER_EXITED_WITHOUT_BOOT_VERDICT' and type(worker.get('worker_returncode')) is int and worker['worker_returncode']==1,'actual expected bounded worker1 completion')
        completed=obj('guest-completed.json',True);require(type(completed.get('cli_returncode')) is int and completed['cli_returncode']==1 and completed.get('host_result')==worker,'actual parent-observed guest completion')
        verified=obj('verified-inputs.json',True);observed=obj('original-observations.json');before=obj('before-start.json');artifacts=verified['candidate']['artifacts']
        require(artifacts==need('approved_artifacts_private') and observed['packet']['artifacts']==need('approved_artifacts_private'),'launched full four-artifact map differs from approved external manifest')
        require(artifacts['kernel']==need('kernel_sha256') and artifacts['ramdisk']==need('ramdisk_sha256') and observed['packet']['artifacts']==artifacts and before['image_sha256']==artifacts['kernel'],'actual launched image/input provenance')
        require(verified.get('controller_sha256')==AFTER['tools/m1_bringup_cycle.py'] and verified.get('launcher_sha256')==AFTER['integrations/puredarwin/m1-bringup/host_progress.py'],'actual executed controller/worker hashes')
        source=obj('private-policy-source-context.json');private_source=source['sources'];expected_sources=assignments(read(HOST/'after/tools/m1_bringup_cycle.py',128<<10))['PRIVATE_POLICY_SOURCES']
        require(source.get('design_only_not_runtime') is True and private_source.get('profile')=='t8103-pinned-hv-source-facts-v1' and private_source.get('probe_member')=='org.darwinlab.driver.PDBCMPrivateProbe' and private_source.get('command')=='/usr/bin/darwin-native-private-test' and private_source.get('sources_sha256')==expected_sources and private_source==verified['candidate']['native_private_policy_sources'],'private source-context/typed-mode bindings')
        details['capture_verified']=True
    check(CHECKS[8],capture)
    def recovered():
        if recovery is None:raise Missing('separate external recovery certificate not supplied')
        cert=document(recovery)
        if cert.get('kind')!='PRIVATE_CAPTURE_RECOVERY_EXTERNAL_V1':raise Missing('external certificate schema unsupported; not inferred')
        require(cert.get('status')=='VERIFIED_EXTERNALLY' and cert.get('console_sha256')==sha(raw('console.bin',32<<20)) and cert.get('core_sha256')==sha(raw('capture-core.json',1<<20)),'recovery/capture binding')
        require(cert.get('packet_binding_sha256')==need('packet_binding_sha256'),'recovery/packet binding')
        baseline=obj('baseline.json',True);returned=obj('returned-linux.json',True);result=obj('result.json',True)
        old,new=baseline['boot_id'],returned['boot_id'];require(type(old) is str and type(new) is str and old and new and old!=new and cert.get('baseline_boot_id')==old and cert.get('new_boot_id')==new,'actual baseline/new boot inequality')
        require(result.get('linux_verified') is True and type(result.get('reset_attempts')) is int and 0<=result['reset_attempts']<=1 and type(cert.get('reset_attempts')) is int and cert['reset_attempts']==result['reset_attempts'] and cert.get('latch_absent') is True and cert.get('lock_free') is True and cert.get('controller_complete') is True,'external recovery facts')
        require(cert.get('cycle_result_sha256')==sha(raw('result.json',1<<20,True)) and cert.get('baseline_sha256')==sha(raw('baseline.json',1<<20,True)) and cert.get('returned_linux_sha256')==sha(raw('returned-linux.json',1<<20,True)),'external recovery actual file hashes')
        details['external_recovery_sha256']=recovery['sha256']
    check(CHECKS[9],recovered)
    # Never leak raw record/private physical placement into the public audit.
    for k in ('raw_record_private','allocation_private','expected216','approved_artifacts_private','actual_kernel_layout_private'):details.pop(k,None)
    qualified=all(checks[k]['status']=='PASS' for k in CHECKS)
    return {'schema_version':1,'status':'QUALIFIED_SAVED_CAPTURE_CHAIN_WITH_EXTERNAL_PREREQUISITES_SELF_REPORT_ONLY' if qualified else 'UNQUALIFIED_SAVED_CAPTURE_CHAIN','qualified':qualified,'checks':checks,'details':details,'input_sha256':hashes,'data_readback':False,'WiFi_ready':False,'external_prerequisites_are_trusted_not_authenticated':True}
