#!/usr/bin/env python3
"""Run frozen bootstrap policy tests against the separate continuing copy.

The old metadata-only fixtures omit ABI fields. Supply those valid fields for
its stricter bidirectional translator; production always has complete BootArgs.
No original source/test file is changed and no hardware is accessed.
"""
import importlib.util
from pathlib import Path
import sys
import unittest
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
import bootstrap_progress
spec=importlib.util.spec_from_file_location('bootstrap_policy_suite_for_progress',HERE.parents[2]/'integrations/m1-boot-window/test_bootstrap.py')
policies=importlib.util.module_from_spec(spec)
spec.loader.exec_module(policies)
policies.b=bootstrap_progress
_original_fixture=policies.fixture

def complete_fixture():
    value=_original_fixture()
    ba=value['ba']
    ba.update(revision=2,version=2,devtree=value['dtaddr']-ba.phys_base+ba.virt_base)
    return value
policies.fixture=complete_fixture

class BootstrapProgressChecks(policies.BootstrapTests):
    pass

if __name__=='__main__': unittest.main()
