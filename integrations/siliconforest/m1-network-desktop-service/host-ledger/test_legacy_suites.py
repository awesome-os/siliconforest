"""Run existing fake-only regression suites against this isolated host overlay."""
from pathlib import Path
import importlib.util
import io
import json
import sys
import unittest
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[4]
LAB=ROOT/'runs/m1-desktop-010/net-desktop-ledger-001/lab'
sys.path.insert(0,str(ROOT))

def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path)
 module=importlib.util.module_from_spec(spec);sys.modules[name]=module;spec.loader.exec_module(module);return module

def run():
 h=load('isolated_regression_host',LAB/'integrations/puredarwin/m1-bringup/host_progress.py')
 sys.modules['host_progress']=h
 host_tests=load('resolver_legacy_host_tests',ROOT/'integrations/puredarwin/m1-bringup/test_host_progress.py')
 c=load('isolated_regression_controller',LAB/'tools/m1_bringup_cycle.py')
 controller_tests=load('resolver_legacy_controller_tests',ROOT/'tests/test_m1_bringup_cycle.py')
 controller_tests.c=c
 reports=[]
 for name,module in [('legacy_host',host_tests),('controller',controller_tests)]:
  stream=io.StringIO();result=unittest.TextTestRunner(stream=stream,verbosity=1).run(unittest.defaultTestLoader.loadTestsFromModule(module))
  reports.append({'suite':name,'tests':result.testsRun,'successful':result.wasSuccessful(),'output':stream.getvalue()})
 assert all(x['successful'] for x in reports),json.dumps(reports)
 return {'status':'PASS_FAKE_ONLY_LEGACY_SUITES_ON_NEW_OVERLAY','suites':reports,'actor':False}

if __name__=='__main__':print(json.dumps(run(),indent=2))
