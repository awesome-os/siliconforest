#!/usr/bin/env python3
import hashlib,json,os,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
SDK='/nix/store/6zpwyx6p9swad42f0vx6rl4z0dp2kj1g-puredarwin-oss-sdk-26.5/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk'
TOOL='/nix/store/v0x4y2cjbasgmrqsbka2bp8allw8j9ph-darwin-cross-toolchain-nix/bin/arm64-apple-darwin20.4-'
FLAGS=['-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O1']
def run(cmd,env=None):
    p=subprocess.run(cmd,text=True,capture_output=True,timeout=90,env=env)
    if p.returncode: raise RuntimeError(f'{cmd}: {p.returncode}\n{p.stdout}\n{p.stderr}')
    return p
def main():
    results={'scope':'host synthetic lifecycle plus Darwin ARM64 compile-only','host':[],'sdk_objects':[]}
    codec=HERE.parent/'codec/egress_validate.c'
    with tempfile.TemporaryDirectory(prefix='owned-utun-') as temp:
        temp=Path(temp)
        for name,cc,extra in [('asan-ubsan','clang',['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']),('ndebug','cc',['-DNDEBUG'])]:
            exe=temp/name
            run([cc,*FLAGS,*extra,str(HERE/'interface_state.c'),str(codec),str(HERE.parent/'route/route_wire.c'),str(HERE/'test_interfaces.c'),'-o',str(exe)])
            p=run([str(exe)],dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
            results['host'].append({'build':name,'stdout':p.stdout.strip(),'stderr':p.stderr})
        for source in [HERE/'interface_state.c',HERE/'interface_darwin.c',HERE.parent/'route/query_routes.c',HERE.parent/'route/route_wire.c',HERE.parent/'route/route_abi.c']:
            obj=temp/(source.stem+'.o')
            p=run([TOOL+'clang',*FLAGS,'-c',str(source),'-o',str(obj)],dict(os.environ,DARWIN_SDK_ROOT=SDK))
            header=run([TOOL+'otool','-hv',str(obj)]).stdout
            if 'ARM64' not in header or 'OBJECT' not in header: raise RuntimeError(header)
            results['sdk_objects'].append({'source':str(source.relative_to(HERE.parent)),'sha256':hashlib.sha256(obj.read_bytes()).hexdigest(),'stderr':p.stderr})
        # This compile refusal prevents accidentally linking the held mutator.
        blocked=subprocess.run([TOOL+'clang',*FLAGS,'-c',str(HERE.parent/'route/owned_routes.c'),'-o',str(temp/'forbidden.o')],env=dict(os.environ,DARWIN_SDK_ROOT=SDK),capture_output=True,text=True,timeout=90)
        if not blocked.returncode: raise RuntimeError('Held route mutator unexpectedly compiled')
        results['held_mutator_default_compile_refused']=True
    results['SDK']=SDK;results['compiler']=TOOL+'clang'
    print(json.dumps(results,indent=2))
if __name__=='__main__':main()
