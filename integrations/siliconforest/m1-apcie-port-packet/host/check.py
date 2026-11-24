"""Run bounded offline checks; no services, target or packet assembly."""
import ast,hashlib,importlib.util,json,os,pathlib,re,shutil,subprocess,tempfile
HERE=pathlib.Path(__file__).resolve().parent;ROOT=HERE.parents[3]
LAB=ROOT/'runs/m1-desktop-010/apcie-port-host-001/lab'
HOST='integrations/puredarwin/m1-bringup/host_progress.py';CTRL='tools/m1_bringup_cycle.py'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(args,env=None,okay=True):
 p=subprocess.run(list(map(str,args)),capture_output=True,text=True,timeout=90,env=env)
 if okay and p.returncode:raise RuntimeError(p.stdout+p.stderr)
 return p

def main():
 binding=json.loads((HERE/'host-binding-001.json').read_text())
 for name,h in binding['canonical_inputs_sha256'].items():
  if sha(ROOT/name)!=h:raise ValueError('canonical input changed '+name)
 actual={str(p.relative_to(LAB)) for p in LAB.rglob('*') if p.is_file()}
 if actual!=set(binding['normalized_files_sha256']):raise ValueError('isolated file inventory drift')
 for name,h in binding['normalized_files_sha256'].items():
  if sha(LAB/name)!=h:raise ValueError('isolated source changed '+name)
 spec=importlib.util.spec_from_file_location('apcie_prepare_check',HERE/'prepare.py');prepare=importlib.util.module_from_spec(spec);spec.loader.exec_module(prepare)
 for name,raw in prepare.generate().items():
  if raw!=(LAB/name).read_bytes() or raw!=(HERE/'overlay'/name).read_bytes():raise ValueError('overlay delta mismatch')
 result={'scope':'Host-only synthetic policy and actual Python control-flow tests with device/process boundaries blocked','hardware_contacted':False,'packet_assembled':False,'canonical_inputs_unchanged':True,'isolated_files':len(actual),'host_tests':[],'mutants':[]}
 p=run(['python3','-B',HERE/'test_gate.py']);result['host_tests'].append({'suite':'new_gate_and_offline_paths','stdout':p.stdout,'stderr':p.stderr})
 script='''import sys,unittest,importlib.util
from pathlib import Path
lab=Path(sys.argv[1]);root=Path(sys.argv[2]);sys.path.insert(0,str(lab/'integrations/puredarwin/m1-bringup'))
import host_progress
assert Path(host_progress.__file__).is_relative_to(lab)
sys.path.insert(0,str(root/'integrations/puredarwin/m1-bringup'))
suite=unittest.TestSuite()
for filename in ['test_host_progress.py','test_native_desktop_progress.py','test_native_trackpad_child_progress.py']:
 path=root/'integrations/puredarwin/m1-bringup'/filename;name=path.stem
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);sys.modules[name]=m;spec.loader.exec_module(m)
 suite.addTests(unittest.defaultTestLoader.loadTestsFromModule(m))
r=unittest.TextTestRunner(verbosity=1).run(suite)
raise SystemExit(0 if r.wasSuccessful() else 1)
'''
 p=run(['python3','-B','-c',script,LAB,ROOT]);result['host_tests'].append({'suite':'unchanged_legacy_host_native_and_sealed_child_writer','stdout':p.stdout,'stderr':p.stderr})
 mutations=[
 ('read_only_boolean',HOST,"type(experiment.get('read_only')) is bool and","True and"),
 ('version_integer',HOST,"type(experiment.get('version')) is int and\n                  type(experiment.get('snapshot_bytes'))", "True and\n                  type(experiment.get('snapshot_bytes'))"),
 ('snapshot_integer',HOST,"type(experiment.get('snapshot_bytes')) is int and","True and"),
 ('shared_boolean',HOST,"type(experiment.get('shared_writes')) is bool and experiment == expected,","True and experiment == expected,"),
 ('report_names',HOST,"type(members) is dict and len(members) == 51 and set(members) == expected_members,","True,"),
 ('raw_duplicate',HOST,"len(actual) == 51 and len(set(actual)) == 51 and", "len(set(actual)) == 51 and"),
 ('child_required',HOST,"image.require(native_trackpad_child_enabled(inputs), 'APCIE observer requires sealed native child')","image.require(True, 'APCIE observer requires sealed native child')"),
 ('legacy_bootarg',HOST,"(' pdapciebridge=1' if apcie else '')","(' pdapciebridge=1' if apcie or not apcie else '')"),
 ('verify_inputs_gate',HOST,"    apcie_port_observer_enabled(inputs)\n    return inputs","    return inputs"),
 ('supervisor_runtime',HOST,"            apcie_port_runtime(inputs, args.runtime, args.timeout)\n", ""),
 ('worker_runtime',HOST,"    apcie_port_runtime(inputs, args.runtime, args.timeout)\n    old.authorize(args)","    old.authorize(args)"),
 ('controller_runtime',CTRL,"    module.apcie_port_runtime(candidate, args.runtime, 240)\n", ""),
 ]
 with tempfile.TemporaryDirectory(prefix='apcie-host-mutants-') as tmp:
  tmp=pathlib.Path(tmp)
  for name,file,before,after in mutations:
   tree=tmp/name;shutil.copytree(LAB,tree);p=tree/file;s=p.read_text()
   if s.count(before)!=1:raise ValueError('mutation anchor not unique '+name)
   s=s.replace(before,after);ast.parse(s);p.write_text(s)
   r=run(['python3','-B',HERE/'test_gate.py'],env=dict(os.environ,APCIE_TEST_LAB=str(tree)),okay=False)
   if r.returncode!=1 or 'FAILED (' not in r.stderr:raise ValueError('mutant survived or harness failed '+name+'\n'+r.stderr)
   result['mutants'].append({'name':name,'syntax_valid':True,'detected_by_unittest':True,'failure_summary':r.stderr[-1600:]})
 result['tests_source_sha256']={str(p.relative_to(ROOT)):sha(p) for p in [HERE/'test_gate.py',ROOT/'integrations/puredarwin/m1-bringup/test_host_progress.py',ROOT/'integrations/puredarwin/m1-bringup/test_native_desktop_progress.py',ROOT/'integrations/puredarwin/m1-bringup/test_native_trackpad_child_progress.py']}
 print(json.dumps(result,indent=2))
if __name__=='__main__':main()
