#!/usr/bin/env python3
"""Compile and execute focused semantic mutants without editing isolated source."""
import ast
from pathlib import Path
import tempfile
from unittest.mock import Mock
import test_native_host as t

SOURCE=(t.AFTER/'integrations/puredarwin/m1-bringup/host_progress.py').read_text()
NAMES={'_native_bootargs_pointer_state','_native_bootargs_start_wrapper','_write_native_bootargs'}

def load(source):
    nodes=[n for n in ast.parse(source).body if isinstance(n,ast.FunctionDef) and n.name in NAMES]
    scope=dict(t.h.__dict__)
    exec(compile(ast.Module(body=nodes,type_ignores=[]),'<pointer-mutant>','exec'),scope)
    return scope

def check(scope):
    def refused(call):
        try:call()
        except ValueError:return
        assert False,'accepted pointer-state drift'
    state=scope['_native_bootargs_pointer_state'];writer=scope['_write_native_bootargs'];wrap=scope['_native_bootargs_start_wrapper']
    for name in ('guest_base','bootargs_off','adt_base','entry','same_sum'):
        hv,b,r,c=t.fixture();hv.adt_base=hv.guest_base+hv.bootargs_off+0x4000
        expected=state(hv)
        assert set(expected)=={'guest_base','bootargs_off','address','adt_base','entry'}
        def change():
            if name=='same_sum':hv.guest_base+=0x4000;hv.bootargs_off-=0x4000
            else:setattr(hv,name,getattr(hv,name)+0x4000)
        b.iface.change=change
        with tempfile.TemporaryDirectory() as temp:
            refused(lambda:writer(hv,b,t.inputs(),Path(temp),r,c,expected['adt_base']))
        hv,b,r,c=t.fixture();hv.adt_base=hv.guest_base+hv.bootargs_off+0x4000;hv.start=Mock()
        original=hv.start;metadata={'start_pointer_state':state(hv)};checked=wrap(hv,metadata)
        change()  # Closure uses the new local hv.
        refused(checked);assert original.call_count==0
    hv,b,r,c=t.fixture();hv.adt_base=hv.guest_base+hv.bootargs_off+0x8000
    with tempfile.TemporaryDirectory() as temp:
        refused(lambda:writer(hv,b,t.inputs(),Path(temp),r,c,hv.adt_base-0x4000))
    assert not b.iface.writes and not b.iface.reads
    hv,b,r,c=t.fixture();hv.adt_base=hv.guest_base+hv.bootargs_off+0x4000;hv.start=Mock(return_value=17)
    metadata={'start_pointer_state':state(hv)};checked=wrap(hv,metadata)
    metadata['start_pointer_state']['guest_base']+=0x4000
    try:result=checked()
    except ValueError:assert False,'mutated evidence changed captured expected state'
    assert result==17 and hv.start.call_count==1

check(load(SOURCE))
MUTANTS={
 'omit_postreadback_state':("_native_bootargs_pointer_state(hv) == pointer_state and",'True and'),
 'omit_prewrite_ADT_check':("pointer_state['adt_base'] == adt_address",'True'),
 'omit_prestart_guard':("_native_bootargs_pointer_state(hv) == expected",'True'),
 'alias_expected_state':("expected = dict(metadata['start_pointer_state'])","expected = metadata['start_pointer_state']"),
 'omit_guest_component':('dict(guest_base=guest_base, bootargs_off=bootargs_off, address=address,','dict(bootargs_off=bootargs_off, address=address,'),
 'omit_offset_component':('dict(guest_base=guest_base, bootargs_off=bootargs_off, address=address,','dict(guest_base=guest_base, address=address,'),
 'omit_entry_component':("entry=_native_integer(hv.entry, 64, 'entry')","removed_entry=_native_integer(hv.entry, 64, 'entry')"),
}
for name,(before,after) in MUTANTS.items():
    assert SOURCE.count(before)==1,name
    try:check(load(SOURCE.replace(before,after)))
    except AssertionError:print(name,'ASSERTION_KILLED')
    else:raise AssertionError(name+' survived')
print('golden PASS; 7 compiled executed mutants assertion-killed; errors 0')
