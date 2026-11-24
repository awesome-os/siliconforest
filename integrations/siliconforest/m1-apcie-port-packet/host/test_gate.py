"""Original offline policy/lifecycle tests. All device/process boundaries blocked."""
import ast,contextlib,copy,importlib.util,io,json,os,struct,sys,tempfile,unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock,patch
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[3]
LAB=Path(os.environ.get('APCIE_TEST_LAB',str(ROOT/'runs/m1-desktop-010/apcie-port-host-001/lab')))
sys.path.insert(0,str(LAB/'integrations/puredarwin/m1-bringup'))
import host_progress as h
spec=importlib.util.spec_from_file_location('apcie_controller',LAB/'tools/m1_bringup_cycle.py')
controller=importlib.util.module_from_spec(spec);spec.loader.exec_module(controller)
E={'version':1,'observer':'PDApcieT8103Bridge','snapshot_bytes':84,'read_only':True,'shared_writes':False}
CHILD={'version':1,'driver':'PDM1SPIHID','child':'PDM1SPITrackpad','interface':2,'mode':'passive_child','input':'pdhid','feature_writes':False,'host_hid_injection':False,'diagnostic_property':'PDM1TrackpadChildStateV1'}
FIELD='apcie_port_observer_experiment';OBS=h.APCIE_OBSERVER_MEMBER
MEMBERS=list(h.APCIE_NATIVE_MEMBERS)+[OBS]
N='0123456789abcdef'*3

def fileset(names):
 c=bytearray()
 for name in names:
  b=name.encode()+b'\0';size=(32+len(b)+7)&~7
  c+=struct.pack('<IIQQII',0x80000035,size,0,0,32,0)+b+bytes(size-32-len(b))
 return struct.pack('<IiiIIIII',0xfeedfacf,0x100000c,0,12,len(names),len(c),0,0)+c

def fixture(record=None,names=None,report_names=None):
 if record is None:record={'desktop_experiment':{'version':1,'command':'/usr/bin/darwin-desktop-test'},'native_trackpad_child_experiment':dict(CHILD),FIELD:dict(E)}
 if names is None:names=MEMBERS
 if report_names is None:report_names=names
 return SimpleNamespace(contents={'kernel':fileset(names),'build_record':json.dumps(record).encode(),'ramdisk':b'SYNTHETIC'},report={'kernel':{'members':{n:{} for n in report_names}}})
def edit(i,change):
 r=json.loads(i.contents['build_record']);change(r);i.contents['build_record']=json.dumps(r).encode();return i
class Boundary(BaseException):pass

class Policy(unittest.TestCase):
 def test_exact_and_legacy_off(self):
  i=fixture();self.assertTrue(h.apcie_port_observer_enabled(i));self.assertEqual(h.guest_bootargs(i),h.BOOTARGS+' pdm1kbd=1 pdm1trackpadenum=1 pdm1trackpadchild=1 pdapciebridge=1')
  old=fixture(names=h.APCIE_NATIVE_MEMBERS);edit(old,lambda r:r.pop(FIELD))
  self.assertFalse(h.apcie_port_observer_enabled(old));self.assertEqual(h.guest_bootargs(old),h.BOOTARGS+' pdm1kbd=1 pdm1trackpadenum=1 pdm1trackpadchild=1')
  h.apcie_port_runtime(old,90,240)
 def test_scalar_types_and_extra_fields(self):
  for k,values in {'version':[True,1.0,'1',0,2,None],'snapshot_bytes':[True,84.0,'84',83,85,None],'read_only':[1,0,False,None],'shared_writes':[0,1,True,None],'observer':['Other','',None],'extra':[False]}.items():
   for value in values:
    with self.subTest(k=k,value=value):
     i=edit(fixture(),lambda r:r[FIELD].update({k:value}))
     with self.assertRaises(ValueError):h.guest_bootargs(i)
  for value in [None,False,1,[],{}]:
   i=edit(fixture(),lambda r:r.update({FIELD:value}))
   with self.assertRaises(ValueError):h.apcie_port_observer_enabled(i)
  i=fixture();i.contents['build_record']=b'{"x":1,"x":2}'
  with self.assertRaises(ValueError):h.apcie_port_observer_enabled(i)
 def test_all_conflict_presence_values(self):
  for key in ['network_link_experiment','network_egress_experiment','network_desktop_experiment','network_default_experiment','network_resolver_experiment','network_unknown','terminal_input_experiment','virtual_input_experiment','keyboard_experiment','dart_survey_experiment','survey_experiment','trackpad_enumeration_experiment','native_keyboard_desktop_experiment','gpu_preflight_experiment','unknown_experiment']:
   for value in [None,False,{},0]:
    with self.subTest(key=key,value=value),self.assertRaises(ValueError):h.apcie_port_observer_enabled(edit(fixture(),lambda r:r.update({key:value})))
 def test_required_child_ram_and_fixed_launcher(self):
  for change in [lambda r:r.pop('native_trackpad_child_experiment'),lambda r:r.pop('desktop_experiment'),lambda r:r['desktop_experiment'].update(command='/bin/sh'),lambda r:r['native_trackpad_child_experiment'].update(feature_writes=True)]:
   with self.assertRaises(ValueError):h.apcie_port_observer_enabled(edit(fixture(),change))
  i=fixture();i.contents.pop('ramdisk')
  with self.assertRaises(ValueError):h.apcie_port_observer_enabled(i)
 def test_exact_report_and_raw_member_mutations(self):
  for bad in [MEMBERS[:-1],MEMBERS+[OBS],MEMBERS[:-1]+['org.puredarwin.driver.PDSerialHID'],MEMBERS[:-1]+['org.puredarwin.driver.PDDARTSurvey'],MEMBERS[:-1]+['org.puredarwin.driver.PDGPUPreflight'],MEMBERS[:-2]+[OBS],MEMBERS+['unexpected']]:
   for i in [fixture(names=bad),fixture(names=bad,report_names=MEMBERS),fixture(names=MEMBERS,report_names=bad)]:
    # A dictionary cannot preserve duplicate report keys; raw duplicate case is covered.
    if len(set(bad))==51 and set(bad)==set(MEMBERS) and i.contents['kernel']==fileset(MEMBERS):continue
    with self.assertRaises(ValueError):h.apcie_port_observer_enabled(i)
  i=edit(fixture(),lambda r:r.pop(FIELD))
  with self.assertRaises(ValueError):h.guest_bootargs(i)
 def test_raw_member_truncation_name_and_kind(self):
  for offset,value in [(12,2),(32+24,31),(32+4,16)]:
   i=fixture();raw=bytearray(i.contents['kernel']);struct.pack_into('<I',raw,offset,value);i.contents['kernel']=bytes(raw)
   with self.assertRaises(ValueError):h.apcie_port_observer_enabled(i)
  i=fixture();i.contents['kernel']=i.contents['kernel'][:-1]
  with self.assertRaises(ValueError):h.apcie_port_observer_enabled(i)
 def test_exact_duration_no_extension(self):
  h.apcie_port_runtime(fixture(),120,240)
  for runtime,timeout in [(119,240),(121,240),(120,239),(120,241),(True,240),(120,True),(120.0,240),(120,240.0),(285,345)]:
   with self.subTest(runtime=runtime,timeout=timeout),self.assertRaises(ValueError):h.apcie_port_runtime(fixture(),runtime,timeout)
 def test_geometry_and_writer_source_unchanged(self):
  original=ast.parse((ROOT/'integrations/puredarwin/m1-bringup/host_progress.py').read_text());new=ast.parse(Path(h.__file__).read_text())
  for name in ['ConsoleSession','ConsoleProof','validate_input_geometry','desktop_command','configure_display_console']:
   a=next(x for x in original.body if getattr(x,'name',None)==name);b=next(x for x in new.body if getattr(x,'name',None)==name)
   self.assertEqual(ast.dump(a),ast.dump(b),name)
  h.validate_input_geometry(dict(width=2560,height=1600,stride=10240,depth=65566),native_trackpad_child=True)
  with self.assertRaises(ValueError):h.validate_input_geometry(dict(width=1920,height=1600,stride=10240,depth=65566),native_trackpad_child=True)

class OfflinePaths(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name);self.i=fixture()
 def args(self,runtime=120,timeout=240):
  return SimpleNamespace(manifest=Path('/offline/bootstrap'),bootstrap_manifest=Path('/offline/bootstrap'),candidate=Path('/offline/candidate'),host_source=Path('/offline/source'),inventory=Path('/offline/inventory'),execute=True,device='/dev/cu.usbmodemOFFLINE1',console_device='/dev/cu.usbmodemOFFLINE3',confirm_recovery=h.old.CONFIRMATION,private_directory=self.root/'capture',runtime=runtime,timeout=timeout,
   launcher_sha256=controller.base.sha(controller.base.read_file(LAB/controller.LAUNCHER)),inspector_sha256=controller.base.sha(controller.base.read_file(LAB/controller.INSPECTOR)))
 def proxy(self):return SimpleNamespace(report={},contents={},host_files={},inventory={},inventory_sha256='0'*64)
 def test_actual_verify_inputs_runs_new_gate(self):
  for good in [True,False]:
   i=fixture()
   if not good:edit(i,lambda r:r[FIELD].update(shared_writes=True))
   with patch.object(h.old,'verify_inputs',return_value=self.proxy()),patch.object(h.image,'verify_manifest',return_value=(copy.deepcopy(i.report),i.contents)):
    if good:self.assertTrue(h.apcie_port_observer_enabled(h.verify_inputs(self.args())))
    else:
     with self.assertRaises(ValueError):h.verify_inputs(self.args())
 def main_args(self,a):
  return ['--manifest',str(a.candidate),'--bootstrap-manifest',str(a.manifest),'--host-source',str(a.host_source),'--inventory',str(a.inventory),'--execute','--device',a.device,'--console-device',a.console_device,'--confirm-recovery',a.confirm_recovery,'--private-directory',str(a.private_directory),'--runtime',str(a.runtime),'--timeout',str(a.timeout)]
 def test_actual_supervisor_authorization_stops_before_spawn(self):
  for runtime,timeout in [(120,240),(119,240),(120,241)]:
   a=self.args(runtime,timeout)
   with patch.object(h,'verify_inputs',return_value=self.i),patch.object(h.multiprocessing,'get_context',side_effect=Boundary) as spawn,contextlib.redirect_stderr(io.StringIO()):
    if runtime==120 and timeout==240:
     with self.assertRaises(Boundary):h.main(self.main_args(a))
     spawn.assert_called_once();a.private_directory.rmdir()
    else:self.assertEqual(h.main(self.main_args(a)),1);spawn.assert_not_called()
 def test_actual_worker_refuses_before_device_boundary(self):
  self.args().private_directory.mkdir(mode=0o700)
  for runtime,timeout in [(120,240),(119,240),(120,241)]:
   a=self.args(runtime,timeout)
   with patch.object(h.subprocess,'run',side_effect=Boundary) as device:
    if runtime==120 and timeout==240:
     with self.assertRaises(Boundary):h.run_worker(a,self.i)
     device.assert_called_once()
    else:
     with self.assertRaises(ValueError):h.run_worker(a,self.i)
     device.assert_not_called()
  bad=edit(fixture(),lambda r:r[FIELD].update(read_only=False))
  with patch.object(h.subprocess,'run',side_effect=Boundary) as device:
   with self.assertRaises(ValueError):h.run_worker(self.args(),bad)
   device.assert_not_called()
 def test_actual_worker_rechecks_bootargs_before_backend_open(self):
  a=self.args();a.private_directory.mkdir(mode=0o700)
  with patch.object(h.subprocess,'run',return_value=SimpleNamespace(stdout=b'fake')),patch.object(h,'cdc_pair',return_value={'console':a.console_device}),patch.object(h,'ContinuingBackend',side_effect=Boundary) as backend,patch.object(h,'guest_bootargs',wraps=h.guest_bootargs) as boot:
   with self.assertRaises(Boundary):h.run_worker(a,self.i)
   boot.assert_called_once_with(self.i);backend.assert_called_once()
 def test_actual_controller_verify_with_synthetic_input_verifiers(self):
  original=importlib.util.spec_from_file_location;calls=[]
  def spec_for(name,path,*a,**kw):
   spec=original(name,path,*a,**kw);execute=spec.loader.exec_module
   def execute_with_fixture(mod):
    execute(mod)
    if name=='bound_continuing_bootstrap':mod.verify=lambda a:calls.append('bootstrap')
    if name=='bound_continuing_host':mod.old.verify_inputs=lambda *a:self.proxy()
   spec.loader.exec_module=execute_with_fixture;return spec
  for runtime in [120,119]:
   with patch.object(controller.base,'verify_local',return_value={}),patch.object(controller.importlib.util,'spec_from_file_location',side_effect=spec_for),patch.object(h.image,'verify_manifest',return_value=(copy.deepcopy(self.i.report),self.i.contents)):
    if runtime==120:
     _,report,mod=controller.verify(self.args(runtime));self.assertEqual(len(report['kernel']['members']),51);self.assertTrue(mod.apcie_port_observer_enabled(self.i))
    else:
     with self.assertRaisesRegex(ValueError,'runtime120'):controller.verify(self.args(runtime))
  self.assertEqual(calls,['bootstrap','bootstrap'])

if __name__=='__main__':unittest.main(verbosity=2)
