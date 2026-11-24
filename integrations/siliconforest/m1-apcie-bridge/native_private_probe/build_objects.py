#!/usr/bin/env python3
"""Extend our exact frozen NPP object harness; no bundle/KC/target execution."""
from pathlib import Path
import hashlib,json,types
HERE=Path(__file__).resolve().parent
PARENT=HERE.parent/'native_private_policy'
receipt=(PARENT/'source-validation-001.json').read_bytes()
assert hashlib.sha256(receipt).hexdigest()=='7d41f394d77e20bd5776964c5a46194b120a3df705ccb996889cb6d852a09c72'
raw=(PARENT/'build_objects.py').read_bytes()
assert hashlib.sha256(raw).hexdigest()==json.loads(receipt)['files_sha256']['build_objects.py']
module=types.ModuleType('frozen_npp_build');module.__file__=str(PARENT/'build_objects.py')
# Expected IOService class metaregistration is confined to the new service TU.
# The frozen10TU closure remains initializer-free; check its compiled objects
# independently after linking rather than weakening that invariant.
old="if any(s in sections for s in ('__mod_init_func','__mod_term_func')):raise RuntimeError('unexpected initializer/finalizer')"
new="""initializers=re.findall(r'^\\s*\\d+\\s+(__mod_(?:init|term)_func)\\s+([0-9a-fA-F]+)',sections,re.M)
        if initializers!=[('__mod_init_func','00000010')]:raise RuntimeError('unexpected service initializer shape')
        for obj in objects:
            own=run(obj.stem+'-sections',['/usr/bin/llvm-objdump','--macho','--section-headers',obj])
            init=re.findall(r'^\\s*\\d+\\s+(__mod_(?:init|term)_func)\\s+([0-9a-fA-F]+)',own,re.M)
            if obj.stem.startswith('PDBCMPrivateProbe-'):
                if init!=initializers:raise RuntimeError('unexpected service metadata sections')
            elif init:raise RuntimeError('unexpected core initializer/finalizer')"""
assert raw.decode().count(old)==1
text=raw.decode().replace(old,new)
exec(compile(text,module.__file__,'exec'),module.__dict__)
module.HERE=HERE
module.SOURCES=['native_private_probe/protocol.c','native_private_probe/probe.c',
                'native_private_probe/PDBCMPrivateProbe.cpp',*module.SOURCES]
if __name__=='__main__':module.main()
