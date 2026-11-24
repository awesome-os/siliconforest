#!/usr/bin/env python3
"""Cached userspace executable only; no kernel bundle, installation or run."""
from pathlib import Path
import argparse,hashlib,json,os,shlex,shutil,subprocess
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[4]
TOOL=Path('/nix/store/v0x4y2cjbasgmrqsbka2bp8allw8j9ph-darwin-cross-toolchain-nix/bin')
SDK=Path('/nix/store/6zpwyx6p9swad42f0vx6rl4z0dp2kj1g-puredarwin-oss-sdk-26.5/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk')
SYSTEM=Path('/nix/store/9i1vaph8rfh102h1x4v7gj7fl7q4x7rm-puredarwin-libsystem-spawnattr-0.1/usr/lib/libSystem.B.dylib')
DYLD=Path('/nix/store/jll17vb2bl849knd0b68n358c53wh1gl-puredarwin-libsystem-0.1/usr/lib/system/libdyld.dylib')
IOKIT=Path('/nix/store/iy73f9hyr3hcq2n1yhmbsyx2pkmblw09-puredarwin-tiny-iokit-mig-header-0.1')
LD='/nix/store/n19v2yzd05pirxz38xjhghknchx3fg1i-puredarwin-native-ld-0.1/bin/ld'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--output-dir',required=True,type=Path);a=p.parse_args();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=False)
    pins={SYSTEM:'9fc03c87e929310d881bd5b440bf0f3d7f0b230d79521b07cea539cf5c7e481e',DYLD:'428e224ec2621bf3a2a7859402603a9a0fd6abe8b3e26c38edeb869e0ce58a0d',IOKIT/'include/device_user.h':'2315eb7aaa9395eab01777f8bf5b22b6c1ae653492300d9ffa8fd18b8f413795',IOKIT/'lib/libIOKit.a':'c099fba1a5376cffd8d907e82aff4d6ee077764ac4eb144c45288da5948e6491'}
    for f,h in pins.items():assert sha(f)==h,str(f)
    record={'scope':'ARM64 userspace collector executable only, not run','pins':{str(p):h for p,h in pins.items()},'commands':[],'dependencies':{},'source':{}}
    def save():(out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    def run(name,args,cwd=None):
        (out/(name+'.command.json')).write_text(json.dumps(list(map(str,args)),indent=2)+'\n')
        r=subprocess.run(list(map(str,args)),capture_output=True,timeout=120,cwd=cwd,env=dict(os.environ,DARWIN_SDK_ROOT=str(SDK)))
        (out/(name+'.log')).write_bytes(r.stdout+r.stderr);record['commands'].append({'name':name,'returncode':r.returncode,'log_sha256':sha(out/(name+'.log'))});save()
        if r.returncode:raise RuntimeError(name+' failed; preserved log')
        return r.stdout.decode()
    src=out/'source';(src/'collector').mkdir(parents=True)
    for f in [HERE/'collector.c',HERE/'collector.h',HERE/'main_mig.c',HERE.parent/'protocol.c',HERE.parent/'protocol.h']:
        rel=f.relative_to(HERE.parent);dst=src/rel;shutil.copyfile(f,dst);record['source'][str(f.relative_to(ROOT))]=sha(f)
    extracted=out/'tiny-objects';extracted.mkdir();run('extract',[TOOL/'arm64-apple-darwin20.4-ar','x',IOKIT/'lib/libIOKit.a'],extracted)
    archive=out/'libIOKit.a';run('rearchive',[TOOL/'arm64-apple-darwin20.4-ar','--format=darwin','crs',archive,*sorted(extracted.glob('*.o'))])
    objects=[]
    for rel in ['collector/collector.c','collector/main_mig.c','protocol.c']:
        s=src/rel;obj=out/(s.stem+'.o');dep=out/(s.stem+'.d')
        run(s.stem,[TOOL/'arm64-apple-darwin20.4-clang','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O1','-DIOKIT=1','-DKERNEL=0','-UKERNEL','-isystem',SDK/'usr/include','-I'+str(IOKIT/'include'),'-MD','-MF',dep,'-c',s,'-o',obj]);objects.append(obj)
        for item in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1]):record['dependencies'][item]=sha(Path(item))
    binary=out/'darwin-native-private-test'
    run('link',[TOOL/'arm64-apple-darwin20.4-clang',*objects,archive,'--ld-path='+LD,'-nostdlib','-L'+str(SYSTEM.parent),'-L'+str(SYSTEM.parent/'system'),'-Wl,-dylib_file,/usr/lib/system/libdyld.dylib:'+str(DYLD),'-Wl,-dead_strip','-Wl,-platform_version,macos,26.5,26.5','-Wl,-fixup_chains','-lSystem','-o',binary])
    imports=run('imports',['/usr/bin/llvm-nm','--undefined-only','--just-symbol-name',binary]).splitlines()
    for symbol in imports:
        if any(s in symbol for s in ('IOServiceOpen','IOConnect','set_property','IORegistryEntrySet')):raise RuntimeError('unexpected userclient/write import')
    run('header',[TOOL/'arm64-apple-darwin20.4-otool','-hv',binary]);run('libraries',[TOOL/'arm64-apple-darwin20.4-otool','-L',binary]);run('dyld-imports',['/usr/bin/llvm-objdump','--macho','--bind','--lazy-bind',binary])
    record.update(status='PASS_ARM64_USERSPACE_LINK_NOT_EXECUTED',binary={'path':str(binary),'sha256':sha(binary),'bytes':binary.stat().st_size},imports=imports);save();print(out/'result.json')
if __name__=='__main__':main()
