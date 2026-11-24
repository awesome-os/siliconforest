"""Build a new isolated source tree only; no packet, process or device action."""
import hashlib,json,pathlib
HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parents[3]
LAB=ROOT/'runs/m1-desktop-010/apcie-port-host-001/lab'
HOST='integrations/puredarwin/m1-bringup/host_progress.py'
CTRL='tools/m1_bringup_cycle.py'
PINS={HOST:'0cf9ccc9bbb966dbf43d707488956baede0cd917ffce08ce3260ff94cc9a946f',CTRL:'cce9803f0c63b6c6b3a8cf62d0e7374566e01429e04877945ccad09f1184077a'}
SOURCES=['tools/__init__.py','tools/lab.py','tools/m1_kernel_cycle.py','tools/m1_firstboot.py','tools/m1_proxy_preflight.py',CTRL,
'integrations/m1-boot-window/bootstrap.py','integrations/m1-boot-window/bootstrap-trampoline.bin',
'integrations/puredarwin/m1-firstboot/host_console.py','integrations/puredarwin/m1-firstboot/manifest.json',
'integrations/puredarwin/m1-bringup/normal_image.py','integrations/puredarwin/m1-bringup/bootstrap_progress.py',
'integrations/puredarwin/m1-bringup/bootargs_progress.py',HOST,
'integrations/puredarwin/m1-bringup/virtual_input_progress.py','integrations/puredarwin/m1-bringup/terminal_input_progress.py']
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def replace(s,a,b):
 if s.count(a)!=1:raise ValueError('nonunique patch anchor '+a)
 return s.replace(a,b)
def generate():
 for n,h in PINS.items():
  if sha(ROOT/n)!=h:raise ValueError('canonical source changed '+n)
 names=json.loads((HERE/'native-members.json').read_text())['native_member_names']
 if len(names)!=50 or len(set(names))!=50:raise ValueError('native50 baseline')
 fragment=(HERE/'gate_fragment.py').read_text().replace('APCIE_NATIVE_MEMBERS = ()','APCIE_NATIVE_MEMBERS = '+repr(tuple(names)))
 host=(ROOT/HOST).read_text()
 host=replace(host,'    native_trackpad_child_enabled(inputs)\n    return inputs','    native_trackpad_child_enabled(inputs)\n    apcie_port_observer_enabled(inputs)\n    return inputs')
 host=replace(host,'def guest_bootargs(inputs):\n',''+fragment+'\n\ndef guest_bootargs(inputs):\n    apcie = apcie_port_observer_enabled(inputs)\n')
 host=replace(host,"(' pdm1trackpadchild=1' if child else ''))","(' pdm1trackpadchild=1' if child else '') +\n            (' pdapciebridge=1' if apcie else ''))")
 host=replace(host,'def run_worker(args, inputs):\n    old.authorize(args)','def run_worker(args, inputs):\n    apcie_port_runtime(inputs, args.runtime, args.timeout)\n    old.authorize(args)')
 host=replace(host,'            inputs = verify_inputs(args)\n        if not args.execute:','            inputs = verify_inputs(args)\n            apcie_port_runtime(inputs, args.runtime, args.timeout)\n        if not args.execute:')
 ctrl=(ROOT/CTRL).read_text()
 ctrl=replace(ctrl,'    candidate = module.verify_inputs(values)\n    return bootstrap, candidate.report, module','    candidate = module.verify_inputs(values)\n    module.apcie_port_runtime(candidate, args.runtime, 240)\n    return bootstrap, candidate.report, module')
 return {HOST:host.encode(),CTRL:ctrl.encode()}
def main():
 changed=generate()
 if LAB.exists():raise ValueError('isolated destination must be new')
 LAB.mkdir(parents=True,mode=0o700)
 originals={};normalized={}
 for n in SOURCES:
  originals[n]=sha(ROOT/n);p=LAB/n;p.parent.mkdir(parents=True,exist_ok=True)
  p.write_bytes(changed.get(n,(ROOT/n).read_bytes()));normalized[n]=sha(p)
 for n,b in changed.items():
  p=HERE/'overlay'/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
 binding={'schema_version':1,'status':'ISOLATED_SOURCE_ONLY_NOT_DEPLOYED_NOT_PACKET_ADMISSION','isolated_lab':str(LAB.relative_to(ROOT)),
 'canonical_inputs_sha256':originals,'normalized_files_sha256':normalized,
 'host_input_sources':{n:normalized[n] for n in [HOST,CTRL,'integrations/puredarwin/m1-bringup/virtual_input_progress.py','integrations/puredarwin/m1-bringup/terminal_input_progress.py']},
 'metadata_field':'apcie_port_observer_experiment','fixed_experiment_fields':{'version':1,'observer':'PDApcieT8103Bridge','snapshot_bytes':84,'read_only':True,'shared_writes':False},
 'runtime_seconds':120,'worker_seconds':240,'native_member_count':50,'observer_member_count':1,
 'scope':'Only two source overlays; sealed native-child writer/geometry/helpers unchanged. No DATA bridge or hardware invocation. Canonical controller timeout240 unchanged; max(240,120+60)=240.',
 'external_packet_dependencies':'Exact KC/RAM/manifest/bootstrap/inventory/m1n1 host-source artifacts still require separately reviewed packet; this tree does not provide them.'}
 (HERE/'host-binding-001.json').write_text(json.dumps(binding,indent=2)+'\n')
 print(json.dumps({'files':len(normalized),'host':normalized[HOST],'controller':normalized[CTRL]}))
if __name__=='__main__':main()
