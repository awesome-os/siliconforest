#!/usr/bin/env python3
"""Cached genuine ARM64 object closure only; never runs target code or fetches."""
import argparse,hashlib,json,os,re,shlex,shutil,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
TOOL=Path('/nix/store/v0x4y2cjbasgmrqsbka2bp8allw8j9ph-darwin-cross-toolchain-nix/bin')
SDK='/nix/store/6zpwyx6p9swad42f0vx6rl4z0dp2kj1g-puredarwin-oss-sdk-26.5/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk'
FW=Path('/nix/store/rxi8m8ki35zs9pdj2vnis26wjb1cdfcb-puredarwin-xnu-headers-arm64-0.1/System/Library/Frameworks/Kernel.framework/Versions/A')
LD='/nix/store/n19v2yzd05pirxz38xjhghknchx3fg1i-puredarwin-native-ld-0.1/bin/ld'
SOURCES=['native_private_policy/private_policy.c','native_policy_reader/policy_reader.c',
 'native_policy_reader/policy_reader_native.cpp','native_policy_snapshot/policy_snapshot.c',
 'mcc_snapshot/mcc_snapshot.c','owned_control/owned_control.c','owned_control/owned_control_native.cpp',
 'native_owned/owned.c','native_owned/native_owned.cpp','private_lifecycle/lifecycle.c']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--output-dir',required=True,type=Path);a=p.parse_args()
    out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=False)
    env=dict(os.environ,DARWIN_SDK_ROOT=SDK)
    commands=[];result={'scope':'real ARM64 objects/partial links only; no kernel API execution, kext or load',
                        'commands':commands,'sources':{},'modes':{}}
    def save(): (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    def run(name,argv):
        (out/(name+'.command.json')).write_text(json.dumps({'argv':list(map(str,argv)),'environment':{'DARWIN_SDK_ROOT':SDK}},indent=2)+'\n')
        r=subprocess.run(list(map(str,argv)),capture_output=True,timeout=180,env=env)
        (out/(name+'.log')).write_bytes(r.stdout+r.stderr)
        commands.append({'name':name,'returncode':r.returncode,'log_sha256':digest(out/(name+'.log'))});save()
        if r.returncode:raise RuntimeError('failed '+name+'; preserved logs')
        return r.stdout.decode()
    for leaf in sorted({Path(s).parts[0] for s in SOURCES}):
        dst=out/'source'/leaf;dst.mkdir(parents=True)
        for path in sorted((HERE.parent/leaf).iterdir()):
            if path.is_file() and path.suffix in ('.c','.h','.cpp'):
                shutil.copyfile(path,dst/path.name);result['sources'][str(path.relative_to(ROOT))]=digest(path)
    includes=['-isystem',str(FW/'Headers'),'-isystem',str(FW/'PrivateHeaders'),'-isystem',str(ROOT/'sources/puredarwin-baseline/src/Libraries/AvailabilityVersions/include')]
    common=['-ffreestanding','-fno-builtin','-DKERNEL','-DKERNEL_PRIVATE','-DTARGET_OS_OSX',*includes,
            '-Wall','-Wextra','-Werror','-Wpedantic','-Wframe-larger-than=1024','-fstack-usage','-fno-stack-protector','-fno-common']
    for opt in ('O0','O2'):
        objects=[];dependencies={};frames=[]
        for s in SOURCES:
            source=out/'source'/s;name=source.stem+'-'+opt;obj=out/(name+'.o');dep=out/(name+'.d')
            cpp=source.suffix=='.cpp';compiler=TOOL/('arm64-apple-darwin20.4-clang++' if cpp else 'arm64-apple-darwin20.4-clang')
            flags=['-std=c++17','-fno-exceptions','-fno-rtti','-fapple-kext'] if cpp else ['-std=c11']
            if s.endswith('owned_control_native.cpp'):
                flags+=['-include',str(FW/'PrivateHeaders/mach/vm_types.h'),'-include',str(FW/'PrivateHeaders/arm/locks.h')]
            run(name,[compiler,*flags,*common,'-'+opt,'-MD','-MF',dep,'-c',source,'-o',obj]);objects.append(obj)
            for item in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1]):
                q=Path(item);dependencies[str(q)]=digest(q)
            usage=out/(name+'.su')
            for line in usage.read_text().splitlines():
                fields=line.split('\t');size=int(fields[1]);frames.append({'function':fields[0],'bytes':size})
                if size>1024:raise RuntimeError('frame exceeds1024')
        linked=out/('combined-'+opt+'.o')
        run('link-'+opt,[LD,'-r','-arch','arm64','-Z','-platform_version','macos','26.5','26.5','-o',linked,*objects])
        imports=run('undefined-'+opt,['/usr/bin/llvm-nm','--undefined-only','--just-symbol-name',linked]).splitlines()
        sections=run('sections-'+opt,['/usr/bin/llvm-objdump','--macho','--section-headers',linked])
        if any(s in sections for s in ('__mod_init_func','__mod_term_func')):raise RuntimeError('unexpected initializer/finalizer')
        if any(re.match(r'_(npp|npr|nps|mcp|boc|bno|bpl)_',s) for s in imports):raise RuntimeError('unresolved internal join')
        if any('atomic' in s.lower() for s in imports):raise RuntimeError('atomic runtime import')
        dis=run('atomic-disassembly-'+opt,['/usr/bin/llvm-objdump','--macho','--disassemble',out/('private_policy-'+opt+'.o')])
        instructions={i:bool(re.search(r'\b'+i+r'\b',dis)) for i in ('ldaxr','stlxr','casal','ldar','stlr')}
        if not ((instructions['ldaxr'] and instructions['stlxr']) or instructions['casal']):raise RuntimeError('no inline ARM64 CAS evidence')
        if any('test_sdk' in s or 'combined_sdk' in s for s in dependencies):raise RuntimeError('TEST SDK dependency')
        result['modes'][opt]={'object_sha256':digest(linked),'imports':imports,'max_individual_frame':max(x['bytes'] for x in frames),
            'frames':frames,'dependencies':dependencies,'inline_atomic_instructions':instructions};save()
    result['status']='PASS_GENUINE_ARM64_OBJECTS_ONLY';save();print(out/'result.json')
if __name__=='__main__':main()
