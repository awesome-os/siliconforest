#!/usr/bin/env python3
"""Bound pinned source facts, synthetic codec tests, mutants and ARM64 object."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from source_facts import audit
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SDK = '/nix/store/6zpwyx6p9swad42f0vx6rl4z0dp2kj1g-puredarwin-oss-sdk-26.5/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk'
TOOL = '/nix/store/v0x4y2cjbasgmrqsbka2bp8allw8j9ph-darwin-cross-toolchain-nix/bin/arm64-apple-darwin20.4-'
FLAGS = ['-Wall','-Wextra','-Werror','-Wpedantic','-O1']
ENV = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(cmd, okay=True, env=None):
    p = subprocess.run([str(x) for x in cmd], capture_output=True, text=True, timeout=60, env=env)
    if okay and p.returncode: raise RuntimeError(f'{cmd}: {p.returncode}\n{p.stdout}\n{p.stderr}')
    return p
def main():
    result = {'scope':'Pinned SOURCE-profile detached bytes only; loaded firmware ABI UNKNOWN',
        'source_facts':audit(ROOT,HERE),'host':[],'mutations':[],'frozen_predecessors':{},'target_executed':False}
    for name in ['m1-gpu-uat-codec','m1-gpu-uat-private','m1-gpu-uat-builder','m1-gpu-uat-data','m1-gpu-rtkit-control']:
        leaf=HERE.parent/name; receipt=json.loads((leaf/'source-validation-001.json').read_text())
        for f,h in receipt['files_sha256'].items():
            if sha(leaf/f)!=h: raise RuntimeError('Frozen predecessor changed: '+name+'/'+f)
        result['frozen_predecessors'][name]=receipt['source_set_sha256']
    with tempfile.TemporaryDirectory(prefix='agx-channel-') as tmp:
        temp=Path(tmp)
        for name,cc,extra in [('asan-ubsan','clang',['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']),('ndebug','cc',['-DNDEBUG'])]:
            exe=temp/name
            run([cc,'-std=c11',*FLAGS,*extra,HERE/'channel.c',HERE/'test_channel.c','-o',exe])
            p=run([exe],env=ENV); result['host'].append({'build':name,'stdout':p.stdout.strip(),'stderr':p.stderr})
        source=(HERE/'channel.c').read_text()
        mutations=[
            ('allow_unknown_profile','return p == AGXC_V13_5_G13_SOURCE_ONLY;','return p != (enum agxc_profile)99;'),
            ('allow_loaded_image','evidence != AGXC_PINNED_SOURCE_TEST','evidence == AGXC_BOOT_METADATA'),
            ('allow_unknown_version','v != AGXC_V13_5','v < 0'),
            ('allow_unknown_GPU','g != AGXC_G13','g < 0'),
            ('decode_endian','(uint64_t)p[i] << (8 * i)','(uint64_t)p[i] << (8 * (n - 1 - i))'),
            ('encode_endian','v >> (8 * i)','v >> (8 * (n - 1 - i))'),
            ('encode_dirty_tail','for (size_t i = 4; i < n; ++i) out[i] = 0;','for (size_t i = 5; i < n; ++i) out[i] = 0;'),
            ('decode_dirty_tail','for (size_t i = 4; i < n; ++i) if (in[i])','for (size_t i = 5; i < n; ++i) if (in[i])'),
            ('unknown_opcode','kind != AGXC_DC_INIT && kind != AGXC_DC_UPDATE_IDLE_TS','kind == 0',2),
            ('FWCtl_zero_word','put(out + 8, 4, 0)','put(out + 8, 4, 1)'),
            ('FWCtl_context_offset','put(out + 12, 4, in->context_id)','put(out + 8, 4, in->context_id)'),
            ('FWCtl_tag1','put(out + 16, 2, 1)','put(out + 16, 2, 0)'),
            ('FWCtl_tag2','put(out + 18, 2, 2)','put(out + 18, 2, 1)'),
            ('FWCtl_ignore_zero','if (get(in + 8, 4) ||','if ('),
            ('FWCtl_ignore_tag','get(in + 16, 2) != 1 ||',''),
            ('range_budget','f.count > AGXC_RANGE_MAX','f.count > AGXC_RANGE_MAX + 1'),
            ('range_alignment','!(a % PAGE)','1'),
            ('range_length','!(n % PAGE)','1'),
            ('range_overlap','if (a <= b + m - 1 && b <= a + n - 1)','if (a == b + m && b == a + n)'),
            ('canonical_address','return a == (low & (UINT64_C(1) << 39) ? low | ~mask : low);','return (a & mask) == low;'),
            ('range_coverage','a - f.ranges[i].address <= f.ranges[i].bytes - PAGE','a - f.ranges[i].address <= f.ranges[i].bytes'),
            ('state_FW_write_offset','*state = 0x20; *write = 0x10;','*state = 0x20; *write = 0x0c;'),
            ('state_read_bound','v.read_index >= AGXC_SLOTS','v.read_index > AGXC_SLOTS'),
            ('state_write_bound','v.write_index >= AGXC_SLOTS','v.write_index > AGXC_SLOTS'),
            ('candidate_read_bound','s.read_index >= AGXC_SLOTS','s.read_index > AGXC_SLOTS'),
            ('candidate_write_bound','s.write_index >= AGXC_SLOTS','s.write_index > AGXC_SLOTS'),
            ('ring_full','tx && occupied == AGXC_SLOTS - 1','tx && occupied == AGXC_SLOTS'),
            ('ring_empty','!tx && !occupied','!tx && occupied == AGXC_SLOTS'),
            ('ring_wrap','(index + 1) % AGXC_SLOTS','index + 1'),
            ('ring_slot_offset','index * stride','index + stride'),
            ('ring_available','AGXC_SLOTS - 1 - occupied','AGXC_SLOTS - occupied'),
        ]
        for name,before,after,*counts in mutations:
            if source.count(before)!=(counts[0] if counts else 1): raise RuntimeError('Mutation anchor: '+name)
            mutated=temp/(name+'.c'); mutated.write_text(source.replace(before,after)); exe=temp/name
            run(['clang','-std=c11',*FLAGS,'-I',HERE,mutated,HERE/'test_channel.c','-o',exe])
            p=run([exe],okay=False)
            if p.returncode!=1 or not re.search(r'test_channel\.c:\d+: ',p.stderr):
                raise RuntimeError('Mutant not rejected by check: '+name+': '+p.stderr)
            result['mutations'].append({'name':name,'compiled':True,'exit':1,'failed_check':p.stderr.strip()})
        p=run(['clang','--analyze','-std=c11',*FLAGS,HERE/'channel.c','-o',temp/'analysis.plist'])
        if p.stderr: raise RuntimeError(p.stderr)
        result['static_analyzer_diagnostics']=0
        obj=temp/'channel.o'; dep=temp/'channel.d'
        flags=['-std=c11',*FLAGS,'-ffreestanding','-fno-stack-protector','-fno-common','-Wframe-larger-than=512']
        run([TOOL+'clang',*flags,'-MD','-MF',dep,'-c',HERE/'channel.c','-o',obj],env=dict(os.environ,DARWIN_SDK_ROOT=SDK))
        header=run([TOOL+'otool','-hv',obj]).stdout
        if 'ARM64' not in header or 'OBJECT' not in header: raise RuntimeError(header)
        imports=run([TOOL+'nm','-u',obj]).stdout.split()
        if imports: raise RuntimeError('Unexpected external imports: '+repr(imports))
        deps={str(Path(p).resolve()):sha(p) for p in dep.read_text().replace('\\\n',' ').split(':',1)[1].split()}
        result['SDK_object']={'sha256':sha(obj),'flags':flags,'undefined':imports,'SDK':SDK,'compiler':TOOL+'clang','dependencies_sha256':dict(sorted(deps.items()))}
    print(json.dumps(result,indent=2))
if __name__=='__main__': main()
