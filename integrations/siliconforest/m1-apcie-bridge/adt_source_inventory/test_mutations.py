#!/usr/bin/env python3
"""Compile and execute Python semantic mutants; require assertion-only kills."""
import importlib.util
import io
import json
from pathlib import Path
import sys
import types
import unittest
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent
source=(HERE/'inventory.py').read_bytes();tests=(HERE/'test_inventory.py').read_bytes()
mutants={
 'vram_type_ignored':('vram_type.value == b"vram\\0"','True'),
 'vram_replaced_by_dram':('vram = _span(*struct.unpack("<QQ", vram_reg.value))','vram = dram'),
 'root_cells_value_ignored':('struct.unpack("<I", p.value)[0] == 2','True'),
 'root_translation_ignored':('not any(p.key in ("ranges", "dma-ranges") for p in nodes[""].properties)','True'),
 'parser_pin_ignored':('_digest(raw) == PARSER_SHA256','True'),
 'profile_ignored':('type(profile) is str and profile == PROFILE','True'),
 'pmap_capacity_ignored':('len(prop.value) // 24 + 1 <= SOURCE_RECORD_LIMIT','True'),
 'remaps_missing_from_capacity':('len(pmap) + 2 * (len(segments) + count) + 1','len(pmap) + (len(segments) + count) + 1'),
 'zero_remap_dropped':('            remap.append(SourceSpan','            if mapped == 0: continue\n            remap.append(SourceSpan'),
 'zero_remap_called_absent':('ZERO_NUMERICAL_VALUE_NOT_ABSENCE','ABSENT'),
 'IOVA_treated_as_PA':('            physical_span = _span(phys, size)','            _span(iova, size)\n            physical_span = _span(phys, size)'),
 'duplicate_labels_refused':('        properties.extend(', '        _require(len(labels) == len(set(labels)), "dedup labels")\n        properties.extend('),
 'duplicate_rows_refused':('            phys, iova, mapped, size, opaque = struct.unpack("<QQQII", raw)','            _require(raw not in [r.raw for r in segments if r.path == path], "dedup rows")\n            phys, iova, mapped, size, opaque = struct.unpack("<QQQII", raw)'),
 'wimg_signature_swapped':('_span(base, size), wimg, signature))','_span(base, size), signature, wimg))'),
 'physical_uses_remap':('physical_span = _span(phys, size)','physical_span = _span(mapped, size)'),
 'zero_size_allowed':('size > 0 and','True and'),
 'extent_end_bound_ignored':('size <= PA_LIMIT - base','True'),
 'terminal_NUL_ignored':('name_bytes[-1:] == b"\\0" and','True and'),
 'unknown_segment_keys_ignored':('set(candidates) == {"segment-ranges", "segment-names"}','{"segment-ranges", "segment-names"}.issubset(candidates)'),
 'labels_lowercased':('label.decode("ascii")','label.decode("ascii").lower()'),
 'tree_input_order_used':('for path, node in sorted(nodes.items()):','for path, node in nodes.items():'),
 'serialized_header_padding_lost':('serialized = base[prop.begin:prop.end]','serialized = prop.value'),
 'inserted_facts_silently_accepted':('not any(p.key == parser.PROPERTY for p in nodes["/chosen"].properties)','True'),
}
def run(code):
    module=types.ModuleType('inventory');module.__file__=str(HERE/'inventory.py');sys.modules['inventory']=module
    exec(compile(code,str(HERE/'inventory.py'),'exec'),module.__dict__)
    testmodule=types.ModuleType('_inventory_mutation_tests');testmodule.__file__=str(HERE/'test_inventory.py');sys.modules[testmodule.__name__]=testmodule
    exec(compile(tests,str(HERE/'test_inventory.py'),'exec'),testmodule.__dict__)
    return unittest.TextTestRunner(stream=io.StringIO(),verbosity=0).run(unittest.defaultTestLoader.loadTestsFromModule(testmodule))
golden=run(source);assert golden.wasSuccessful(),(golden.failures,golden.errors)
rows=[]
for name,(before,after) in mutants.items():
    assert source.count(before.encode())==1,name
    result=run(source.replace(before.encode(),after.encode()))
    assert not result.wasSuccessful() and result.failures and not result.errors,(name,result.failures,result.errors)
    rows.append(dict(name=name,compiled=True,executed=True,assertion_killed=True,errors=0))
print(json.dumps(dict(status='PASS',golden_groups=golden.testsRun,mutants=rows,compiled_assertion_killed=len(rows),errors=0),sort_keys=True))
