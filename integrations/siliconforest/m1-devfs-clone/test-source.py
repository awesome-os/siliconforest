#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise actual devfs/DRM lifecycle bodies with host vnode/OS boundaries."""
import os,re,subprocess,tempfile
from pathlib import Path
here=Path(__file__).resolve().parent
frozen=Path('/nix/store/wzqlii8czf0n64p01i3k9sv9agjhdl3m-source')
dri=Path('/nix/store/gj14r67462ndvmf7f2al6ah8isjhk4wc-puredarwin-dcp-tree')
def block(s,name):
    start=s.index(name)
    lex=re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',lambda m:' '*len(m[0]),s,flags=re.S)
    brace=lex.index('{',start)
    depth=0
    for i in range(brace,len(s)):
        depth+=(lex[i]=='{')-(lex[i]=='}')
        if not depth:return s[start:i+1]
    raise ValueError(name)
with tempfile.TemporaryDirectory(prefix='sf-devfs-clone-') as tmp:
    tmp=Path(tmp);prepared=tmp/'source'
    import shutil,json,hashlib
    manifest=json.loads((here/'manifest.json').read_text())
    for name,h in {**manifest['kernel'],**manifest['dri']}.items():
        source=(frozen if name.startswith('src/Kernel/xnu/') else dri)/name
        assert hashlib.sha256(source.read_bytes()).hexdigest()==h['before'],name
        dest=prepared/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,dest)
    subprocess.run(['patch','--batch','--forward','--fuzz=0','-p1','-i',str(here/'kernel.patch')],cwd=prepared,check=True)
    subprocess.run(['patch','--batch','--forward','--fuzz=0','-p1','-i',str(here/'dri.patch')],cwd=prepared,check=True)
    for name,h in {**manifest['kernel'],**manifest['dri']}.items():assert hashlib.sha256((prepared/name).read_bytes()).hexdigest()==h['after'],name
    subprocess.run(['patch','--batch','--forward','--fuzz=0','-p1','-i',str(here/'dri-stats.patch')],cwd=prepared,check=True)
    stats=json.loads((here/'stats-manifest.json').read_text())['dri']
    for name,h in stats.items():assert hashlib.sha256((prepared/name).read_bytes()).hexdigest()==h['after'],name
    for variant,root,driver,args,expected in [('original',frozen,dri,['--counterexample'],1),('fixed',prepared,prepared,[],0)]:
        tree=(root/'src/Kernel/xnu/bsd/miscfs/devfs/devfs_tree.c').read_text()
        vnops=(root/'src/Kernel/xnu/bsd/miscfs/devfs/devfs_vnops.c').read_text()
        native=(driver/'src/Kernel/Extensions/SFDRMDri/SFDRMDri.cpp').read_text()
        actual='\n'.join([block(native,'static int\ndri_clone('),block(native,'static int\ndri_open('),block(native,'static int\ndri_close('),block(tree,'int\ndevfs_dntovn('),block(vnops,'static int\ndevfs_inactive('),block(vnops,'static int\ndevfs_reclaim(')])
        # C permits integer-zero enum initialization; cast only that declaration for C++ hosting.
        actual=actual.replace('enum vtype vtype = 0;', 'enum vtype vtype = (enum vtype)0;')
        header=tmp/(variant+'.h');header.write_text(actual+'\n');binary=tmp/variant
        subprocess.run([os.environ.get('CXX','clang++'),'-std=c++14','-O1','-g','-Wall','-Wextra','-Werror',
                        '-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                        '-DSF_DEVFS_SOURCE="'+str(header)+'"',str(here/'host-source.cpp'),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary),*args],timeout=30,capture_output=True,text=True)
        print(result.stdout,end='',flush=True)
        if result.returncode!=expected:
            print(result.stderr,flush=True);raise SystemExit(variant+' unexpected exit '+str(result.returncode))
        print('DEVFS_SOURCE '+variant+' expected exit '+str(expected),flush=True)
