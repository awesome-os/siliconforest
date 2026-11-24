#!/usr/bin/env python3
"""Bounded offline checks; never imports or executes m1n1 hardware code."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
REF = ROOT / 'sources/m1n1-baseline'
COPY = ROOT / 'runs/m1-desktop-010/network-resolver-attempt-004/guest/host-source'
PIN = '121fcbf35d4501814b58ca5347926a4173b6a512'
SDK = '/nix/store/6zpwyx6p9swad42f0vx6rl4z0dp2kj1g-puredarwin-oss-sdk-26.5/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk'
TOOL = '/nix/store/v0x4y2cjbasgmrqsbka2bp8allw8j9ph-darwin-cross-toolchain-nix/bin/arm64-apple-darwin20.4-'
FLAGS = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-O2', '-Wframe-larger-than=2048']
ENV = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')

def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def run(argv, okay=True, env=None):
    p = subprocess.run(list(map(str, argv)), capture_output=True, text=True, timeout=60, env=env)
    if okay and p.returncode:
        raise RuntimeError(f'{argv}: {p.returncode}\n{p.stdout}\n{p.stderr}')
    return p

def main():
    result = {'scope': 'Detached source-layout codec and fake protocol only',
              'hardware_contacted': False, 'Darwin_loaded_GPU_ABI': 'UNKNOWN',
              'pin': PIN, 'references': {}, 'host': [], 'mutants': []}
    for name in ['proxyclient/m1n1/hw/asc.py', 'proxyclient/m1n1/fw/asc/base.py',
                 'proxyclient/m1n1/fw/asc/__init__.py', 'proxyclient/m1n1/fw/agx/__init__.py',
                 'proxyclient/m1n1/agx/__init__.py', 'proxyclient/m1n1/utils.py',
                 'src/asc.c', 'src/asc.h', 'src/rtkit.c']:
        b = subprocess.check_output(['git', '-C', str(REF), 'show', PIN+':'+name])
        if (REF/name).read_bytes() != b:
            raise RuntimeError('Reference differs from Git pin: '+name)
        copy = COPY/name
        if name.startswith('proxyclient/') and copy.read_bytes() != b:
            raise RuntimeError('Private captured source differs: '+name)
        result['references'][name] = {'sha256': sha(REF/name),
            'git_object_equal': True, 'captured_copy_equal': True if name.startswith('proxyclient/') else None}
    rtc = HERE.parent/'m1-gpu-rtkit-control'
    receipt = rtc/'source-validation-001.json'
    dependency = json.loads(receipt.read_text())
    for name, digest in dependency['files_sha256'].items():
        if sha(rtc/name) != digest: raise RuntimeError('Frozen RTKit dependency changed: '+name)
    result['frozen_RTKit'] = {'receipt_sha256':sha(receipt), 'source_set_sha256':dependency['source_set_sha256']}
    with tempfile.TemporaryDirectory(prefix='asc-envelope-') as td:
        td = Path(td)
        for name, cc, extra in [('asan-ubsan','clang',['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']),
                                ('ndebug','cc',['-DNDEBUG'])]:
            exe=td/name
            run([cc,*FLAGS,*extra,HERE/'envelope.c',HERE/'test_envelope.c',rtc/'control.c','-o',exe])
            p=run([exe],env=ENV)
            if p.stderr or 'host checks PASS' not in p.stdout: raise RuntimeError(p.stdout+p.stderr)
            result['host'].append({'configuration':name,'stdout':p.stdout.strip()})
        source=(HERE/'envelope.c').read_text()
        mutations=[
            ('profile','p != ACE_PIN121FCBF_LEGACY','p == ACE_PIN121FCBF_LEGACY'),
            ('length','n != ACE_BYTES','n < ACE_BYTES'),
            ('endpoint','ep > 255','ep > 256'),
            ('low32_policy','m & UINT64_C(0xffffff00)','m & UINT64_C(0xfffffe00)'),
            ('reject_opaque_high','m & UINT64_C(0xffffff00)','m & UINT64_C(0xffffffffffffff00)'),
            ('outcnt_width','(m >> 52) & 31','(m >> 52) & 15'),
            ('incnt_offset','(m >> 48) & 15','(m >> 49) & 15'),
            ('outptr_offset','(m >> 44) & 15','(m >> 45) & 15'),
            ('inptr_offset','(m >> 40) & 15','(m >> 41) & 15'),
            ('opaque_mask','0x01ffff00000000ff','0x03ffff00000000ff'),
            ('rx_endian','p[i] << (8u * i)','p[i] << (8u * (7u-i))'),
            ('tx_endian','v >> (8u * i)','v >> (8u * (7u-i))'),
            ('drop_overlap','if (overlap(b, n, out, z))','if (overlap(b, n, out, z) && n == 0)'),
            ('overlap_boundary','y - x < an','y - x <= an'),
            ('tx_metadata_pollution','put64(out + 8, ep);','put64(out + 8, ep | UINT64_C(0x100));'),
        ]
        for name,before,after in mutations:
            if before not in source: raise RuntimeError('Missing mutation anchor: '+name)
            f=td/(name+'.c');f.write_text(source.replace(before,after));exe=td/name
            run(['clang',*FLAGS,'-I',HERE,f,HERE/'test_envelope.c',rtc/'control.c','-o',exe])
            p=run([exe],okay=False)
            if p.returncode!=1 or not p.stderr.startswith('check line '):
                raise RuntimeError('Mutant survived or failed outside test: '+name+' '+p.stderr)
            result['mutants'].append({'name':name,'compiled':True,'failed_check':p.stderr.strip()})
        p=run(['clang','--analyze',*FLAGS,HERE/'envelope.c','-o',td/'analyze.plist'])
        if p.stderr: raise RuntimeError(p.stderr)
        result['analyzer_diagnostics']=0
        obj=td/'envelope.o';dep=td/'envelope.d'
        args=[*FLAGS,'-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-common']
        run([TOOL+'clang',*args,'-MD','-MF',dep,'-c',HERE/'envelope.c','-o',obj],
            env=dict(os.environ,DARWIN_SDK_ROOT=SDK))
        header=run([TOOL+'otool','-hv',obj]).stdout
        undefined=run([TOOL+'nm','-u',obj]).stdout
        if 'ARM64' not in header or 'OBJECT' not in header or undefined.strip():
            raise RuntimeError('Expected ARM64 object with zero imports: '+header+undefined)
        result['ARM64_object']={'sha256':sha(obj),'SDK':SDK,'compiler':TOOL+'clang',
            'flags':args,'header':header,'undefined_symbols':[],'executed':False,
            'stack_frame_warning_limit_bytes':2048}
        deps=dep.read_text().replace('\\\n',' ').split(':',1)[1].split()
        result['compile_inputs_sha256']={str(Path(p).resolve()):sha(p) for p in sorted(set(deps))}
    print(json.dumps(result,indent=2))

if __name__=='__main__': main()
