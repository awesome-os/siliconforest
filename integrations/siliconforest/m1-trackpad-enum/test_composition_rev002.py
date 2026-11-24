#!/usr/bin/env python3
"""Run the frozen adapter checks against the explicitly composed core002.

The original adapter receipt and tests remain unchanged. Only the in-memory
manifest's two core dependency hashes are replaced by the SDK002 input pins.
"""
from pathlib import Path
import copy,hashlib,importlib.util,json,sys,unittest
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent
inputs=json.loads((HERE/'sdk-build-inputs-002.json').read_text())['files']
root=HERE.parents[2]
for name,row in inputs.items():
    assert hashlib.sha256((root/row['source']).read_bytes()).hexdigest()==row['sha256'],name
spec=importlib.util.spec_from_file_location('composed_adapter002',HERE/'adapter/test_adapter_source.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
m.MANIFEST=copy.deepcopy(m.MANIFEST)
for name in ('m1tp_enum.c','m1tp_enum.h'):
    m.MANIFEST['dependencies']['core/'+name]=inputs[name]['sha256']
suite=unittest.defaultTestLoader.loadTestsFromModule(m)
result=unittest.TextTestRunner(verbosity=2).run(suite)
raise SystemExit(0 if result.wasSuccessful() else 1)
