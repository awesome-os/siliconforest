"""Exercise both actual authorization entry paths; no target, proxy or worker spawn."""
from pathlib import Path
import argparse,importlib.util,json,os,tempfile,unittest,sys
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[4]
PATH=ROOT/'runs/m1-desktop-010/net-desktop-ledger-001/lab/integrations/puredarwin/m1-bringup/host_progress.py'
spec=importlib.util.spec_from_file_location('lifetime_authorization_002',PATH)
h=importlib.util.module_from_spec(spec);sys.modules[spec.name]=h;spec.loader.exec_module(h)
class Barrier(BaseException):pass
class AuthTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory(prefix='darwin-auth-offline-');self.root=Path(self.tmp.name);self.calls=[];self.reached=[];self.real=h.old.authorize
 def tearDown(self):self.tmp.cleanup()
 def args(self,runtime=760,timeout=820):
  return argparse.Namespace(manifest=Path('/offline/manifest'),bootstrap_manifest=Path('/offline/bootstrap'),host_source=Path('/offline/source'),inventory=Path('/offline/inventory'),execute=True,device='/dev/cu.usbmodemOFFLINE1',console_device='/dev/cu.usbmodemOFFLINE3',confirm_recovery=h.old.CONFIRMATION,private_directory=self.root/'capture',runtime=runtime,timeout=timeout)
 def auth(self,args):
  self.calls.append(vars(args).copy());self.real(args)
 def barrier(self,*args,**kw):self.reached.append(True);raise Barrier()
 def main_args(self,args):
  return ['--manifest',str(args.manifest),'--bootstrap-manifest',str(args.bootstrap_manifest),'--host-source',str(args.host_source),'--inventory',str(args.inventory),'--execute','--device',args.device,'--console-device',args.console_device,'--confirm-recovery',args.confirm_recovery,'--private-directory',str(args.private_directory),'--runtime',str(args.runtime),'--timeout',str(args.timeout)]
 def exercise(self,kind,args,allowed):
  if kind=='worker':args.private_directory.mkdir(mode=0o700)
  with patch.object(h.old,'authorize',self.auth),patch.object(h,'verify_inputs',return_value=object()),patch.object(h,'network_link_profile',return_value=None),patch.object(h.multiprocessing,'get_context',self.barrier),patch.object(h.subprocess,'run',self.barrier):
   if allowed:
    with self.assertRaises(Barrier):
     if kind=='worker':h.run_worker(args,object())
     else:h.main(self.main_args(args))
   elif kind=='worker':
    with self.assertRaises(Exception):h.run_worker(args,object())
   else:self.assertEqual(h.main(self.main_args(args)),1)
  self.assertEqual(bool(self.reached),allowed)
  if allowed:
   self.assertEqual(len(self.calls),1)
   expected=vars(args).copy();expected['timeout']=min(args.timeout,300)
   self.assertEqual(self.calls[0],expected)
   self.assertEqual(args.timeout,300 if args.runtime==285 else (820 if args.runtime==760 else 240))
 def test_supervisor_long(self):self.exercise('supervisor',self.args(),True)
 def test_worker_long(self):self.exercise('worker',self.args(),True)
 def test_supervisor_legacy(self):self.exercise('supervisor',self.args(180,240),True)
 def test_worker_legacy(self):self.exercise('worker',self.args(180,240),True)
 def test_supervisor_runtime_excess(self):self.exercise('supervisor',self.args(761,820),False)
 def test_worker_runtime_excess(self):self.exercise('worker',self.args(761,820),False)
 def test_supervisor_timeout_excess(self):self.exercise('supervisor',self.args(760,821),False)
 def test_worker_timeout_excess(self):self.exercise('worker',self.args(760,821),False)
 def test_supervisor_missing_margin(self):self.exercise('supervisor',self.args(760,789),False)
 def test_worker_missing_margin(self):self.exercise('worker',self.args(760,789),False)
 def test_supervisor_recovery_confirmation(self):
  a=self.args();a.confirm_recovery='wrong';self.exercise('supervisor',a,False)
 def test_worker_recovery_confirmation(self):
  a=self.args();a.confirm_recovery='wrong';self.exercise('worker',a,False)
 def test_worker_execute_guard(self):
  a=self.args();a.execute=False;self.exercise('worker',a,False)
 def test_worker_device_guard(self):
  a=self.args();a.device='/dev/wrong';self.exercise('worker',a,False)
 def test_worker_private_directory_guard(self):
  a=self.args();a.private_directory=PATH.parent/'offline-refuse';a.private_directory.mkdir(exist_ok=False)
  try:
   with patch.object(h.subprocess,'run',self.barrier):
    with self.assertRaises(Exception):h.run_worker(a,object())
   self.assertFalse(self.reached)
  finally:a.private_directory.rmdir()
 def test_old_authorization_regression_reproduced(self):
  a=self.args()
  with self.assertRaisesRegex(Exception,'300'):self.real(a)
class DesktopAuthorization(unittest.TestCase):
 setUp=AuthTests.setUp
 tearDown=AuthTests.tearDown
 args=AuthTests.args
 barrier=AuthTests.barrier
 main_args=AuthTests.main_args
 def test_new_supervisor_exact_profile(self):
  a=self.args(285,345)
  with patch.object(h,'network_link_profile',return_value={'profile':'desktop-native-resolver-two-http-v1'}),patch.object(h,'verify_inputs',return_value=object()),patch.object(h.multiprocessing,'get_context',self.barrier),patch.object(h.subprocess,'run',self.barrier):
   with self.assertRaises(Barrier):h.main(self.main_args(a))
  self.assertTrue(self.reached);self.assertEqual(a.timeout,345)
 def test_new_worker_exact_profile(self):
  a=self.args(285,345);a.private_directory.mkdir(mode=0o700)
  with patch.object(h,'network_link_profile',return_value={'profile':'desktop-native-resolver-two-http-v1'}),patch.object(h.subprocess,'run',self.barrier):
   with self.assertRaises(Barrier):h.run_worker(a,object())
  self.assertTrue(self.reached);self.assertEqual(a.timeout,345)
 def test_new_profile_mismatched_budgets_both_paths(self):
  for runtime,timeout in ((284,345),(286,345),(285,344),(285,346),(760,820)):
   for path in ('supervisor','worker'):
    a=self.args(runtime,timeout)
    if path=='worker':a.private_directory.mkdir(mode=0o700,exist_ok=True)
    with patch.object(h,'network_link_profile',return_value={'profile':'desktop-native-resolver-two-http-v1'}),patch.object(h,'verify_inputs',return_value=object()),patch.object(h.multiprocessing,'get_context',self.barrier),patch.object(h.subprocess,'run',self.barrier):
     if path=='worker':
      with self.assertRaises(Exception):h.run_worker(a,object())
     else:self.assertEqual(h.main(self.main_args(a)),1)
    self.assertFalse(self.reached)
if __name__=='__main__':unittest.main(verbosity=2)
