#!/usr/bin/env python3
"""Host filesystem/parser fixtures, independent JSON oracle, mutants, pinned SDK."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import random
import re
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
SDK='/nix/store/6zpwyx6p9swad42f0vx6rl4z0dp2kj1g-puredarwin-oss-sdk-26.5/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk'
TOOL='/nix/store/v0x4y2cjbasgmrqsbka2bp8allw8j9ph-darwin-cross-toolchain-nix/bin/arm64-apple-darwin20.4-'
SYSTEM=Path('/nix/store/9i1vaph8rfh102h1x4v7gj7fl7q4x7rm-puredarwin-libsystem-spawnattr-0.1/usr/lib/libSystem.B.dylib')
FLAGS=['-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O1']
ENV=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(cmd,okay=True,env=None):
    p=subprocess.run([str(x) for x in cmd],capture_output=True,text=True,timeout=60,env=env)
    if okay and p.returncode:raise RuntimeError(f'{cmd}: {p.returncode}\n{p.stdout}\n{p.stderr}')
    return p
def oracle(raw):
    def reject(*_):raise ValueError('unsupported')
    def obj(pairs):
        d={}
        for k,v in pairs:
            if k in d:reject()
            d[k]=v
        return d
    def admitted(v):
        if isinstance(v,str):return all(ord(c)<=127 for c in v)
        if isinstance(v,bool) or v is None:return True
        if isinstance(v,int):return -(1<<63)<=v<(1<<64)
        if isinstance(v,list):return all(admitted(x) for x in v)
        if isinstance(v,dict):return all(admitted(k) and admitted(x) for k,x in v.items())
        return False
    try:return admitted(json.loads(raw.decode('ascii'),object_pairs_hook=obj,parse_float=reject,parse_constant=reject))
    except (ValueError,UnicodeError):return False
def differential(temp):
    so=temp/'json.so';run(['clang',*FLAGS,'-shared','-fPIC',HERE/'json.c','-o',so])
    lib=ctypes.CDLL(str(so))
    class Doc(ctypes.Structure):_fields_=[('bytes',ctypes.c_void_p),('length',ctypes.c_size_t),('tokens',ctypes.c_void_p),('count',ctypes.c_size_t)]
    lib.rg_json_parse.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.POINTER(Doc)]
    workspace=(ctypes.c_uint64*32768)();rng=random.Random(1741);cases=[]
    def value(depth):
        choices=[None,True,False,rng.randrange(-(1<<63),(1<<64)),''.join(rng.choice('ab\\\"\n\t\0\x7f') for _ in range(rng.randrange(20)))]
        if depth:choices.extend([[value(depth-1) for _ in range(rng.randrange(4))],{f'key{j}':value(depth-1) for j in range(rng.randrange(4))}])
        return rng.choice(choices)
    for _ in range(150):
        raw=json.dumps(value(3),ensure_ascii=True,separators=(',',':')).encode();cases.append(raw)
        for _ in range(3):
            b=bytearray(raw);pos=rng.randrange(len(b));b[pos]=rng.randrange(128);cases.append(bytes(b))
    cases.extend([b'"\x7f"',b'{"a":1,"\\u0061":2}',b'{"x":{"b":0,"b":1}}',b'{"unknown":1.0}',b'"\\u0080"',b'"\\u0000"'])
    for raw in cases:
        b=ctypes.create_string_buffer(raw);doc=Doc();actual=lib.rg_json_parse(b,len(raw),workspace,ctypes.byref(doc))==0
        if actual!=oracle(raw):raise RuntimeError('Oracle disagreement: '+repr(raw))
    return {'cases':len(cases),'seed':1741,'oracle':'Python standard JSON plus duplicate/ASCII/integer64 admission; independent of C implementation'}
def main():
    result={'scope':'Host fake identity root + real filesystem and exact SDK compile only','host':[],'mutations':[],'SDK_objects':[],'target_executed':False}
    parent=json.loads((HERE.parent/'source-validation-001.json').read_text())
    for f,item in parent['files'].items():
        if sha(HERE.parent/f)!=item['sha256']:raise RuntimeError('Frozen parent changed: '+f)
    result['frozen_parent_source_set_sha256']=parent['source_set_sha256']
    report=ROOT/'reports/sprint-010/network-desktop-env-source-001.json'
    if sha(report)!='0275c13169d8d8bb00e248601a251d27dbee44fe9948b6e12b0bfd3390fd7a98':raise RuntimeError('Frozen ENV report changed')
    result['frozen_ENV_report_sha256']=sha(report)
    with tempfile.TemporaryDirectory(prefix='record-guard-check-') as d:
        temp=Path(d);testenv=dict(ENV,RG_TEST_PARENT=str(temp))
        for name,extra in [('asan-ubsan',['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']),('ndebug',['-DNDEBUG'])]:
            exe=temp/name;run(['clang',*FLAGS,*extra,'-DRG_TEST_ONLY',HERE/'json.c',HERE/'read.c',HERE/'test_guard.c','-o',exe])
            p=run([exe],env=testenv);result['host'].append({'build':name,'stdout':p.stdout.strip(),'stderr':p.stderr})
        result['independent_JSON_oracle']=differential(temp)
        mutations=[
            ('json','comparison_budget','++p->steps>RG_MAX_STEPS','++p->steps>UINT_MAX'),
            ('json','duplicate_keys','if(equal_keys(p,&p->w->tokens[j],&p->w->tokens[key]))','if(0&&equal_keys(p,&p->w->tokens[j],&p->w->tokens[key]))'),
            ('json','escaped_duplicate','if(a->magnitude!=b->magnitude)return 0;','if(a->magnitude!=b->magnitude||a->length!=b->length)return 0;'),
            ('json','integer_overflow','t->magnitude>(UINT64_MAX-d)/10','t->magnitude==UINT64_MAX'),
            ('json','signed_underflow','t->magnitude>(UINT64_C(1)<<63)','t->magnitude>UINT64_MAX'),
            ('json','leading_zero',"p->pos-first>1&&p->b[first]=='0'",'p->pos-first>99'),
            ('json','trailing_bytes','if(p.pos!=n)return RG_JSON;','if(p.pos>n)return RG_JSON;'),
            ('json','depth_boundary','depth>RG_MAX_DEPTH','depth>RG_MAX_DEPTH+1'),
            ('json','token_boundary','p->used==RG_MAX_TOKENS','p->used==RG_MAX_TOKENS-1'),
            ('json','escaped_nonASCII','if(v>127)return 0;','if(v>255)return 0;'),
            ('json','unknown_escape','default:return 0;','default:*out=c;return 1;'),
            ('json','raw_controls',"if(c<32||c>127||c=='\"')return 0;", "if(c>127||c=='\"')return 0;"),
            ('json','u64_negative','if(t->negative)return RG_RANGE;','if(t->negative>1)return RG_RANGE;'),
            ('json','i64_overflow','if(!t->negative&&t->magnitude>INT64_MAX)return RG_RANGE;','if(!t->negative&&t->magnitude>UINT64_MAX)return RG_RANGE;'),
            ('json','bool_type','if(t->type!=RG_BOOL)return RG_TYPE;','if(t->type==RG_NULL)return RG_TYPE;'),
            ('json','string_capacity','if(t->magnitude>=cap)return RG_LIMIT;','if(t->magnitude>cap)return RG_LIMIT;'),
            ('json','null_type','return t->type==RG_NULL?RG_OK:RG_TYPE;','return t->type!=RG_INTEGER?RG_OK:RG_TYPE;'),
            ('read','owner_uid','s->st_uid==OWNER_UID','s->st_uid==(uid_t)-1',2),
            ('read','file_mode','(s->st_mode&07777)==0600','(s->st_mode&07777)==0644'),
            ('read','hardlink','s->st_nlink==1','s->st_nlink>=1'),
            ('read','app_dir_mode','(s->st_mode&07777)==0700','(s->st_mode&0700)==0700'),
            ('read','ancestor_writable','!(s->st_mode&0022)','!(s->st_mode&0000)'),
            ('read','size_boundary','(uint64_t)s->st_size<=RG_MAX_BYTES','(uint64_t)s->st_size<RG_MAX_BYTES'),
            ('read','named_file_limit','before.st_size>file_limit','before.st_size>file_limit+1'),
            ('read','mtime_restored_rewrite','CTIME(*a).tv_sec==CTIME(*b).tv_sec&&CTIME(*a).tv_nsec==CTIME(*b).tv_nsec','1'),
            ('read','directory_rebinding','!same_id(&binding,&identities[i+1])','0'),
            ('read','file_NOFOLLOW','O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC','O_RDONLY|O_NONBLOCK|O_CLOEXEC'),
            ('read','parent_NOFOLLOW','O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC','O_RDONLY|O_DIRECTORY|O_NONBLOCK|O_CLOEXEC',2),
        ]
        for kind,name,before,after,*counts in mutations:
            source=(HERE/(kind+'.c')).read_text()
            if source.count(before)!=(counts[0] if counts else 1):raise RuntimeError('Mutation anchor '+name)
            mutated=temp/(name+'.c');mutated.write_text(source.replace(before,after));exe=temp/name
            paths=[mutated if k==kind else HERE/(k+'.c') for k in ['json','read']]
            run(['clang',*FLAGS,'-DRG_TEST_ONLY','-I',HERE,*paths,HERE/'test_guard.c','-o',exe])
            p=run([exe],okay=False,env=testenv)
            if p.returncode!=1 or not re.search(r'test_guard\.c:\d+: ',p.stderr):raise RuntimeError('Mutant not assertion-detected '+name+': '+p.stderr)
            result['mutations'].append({'name':name,'compiled':True,'exit':1,'failed_check':p.stderr.strip()})
        for kind in ['json','read']:
            p=run(['clang','--analyze',*FLAGS,HERE/(kind+'.c'),'-o',temp/(kind+'.plist')])
            if p.stderr:raise RuntimeError(p.stderr)
        result['static_analyzer_diagnostics']=0
        imports=set();deps={}
        for kind in ['json','read']:
            obj=temp/(kind+'.o');dep=temp/(kind+'.d');flags=[*FLAGS,'-fno-stack-protector','-Wframe-larger-than=2048']
            run([TOOL+'clang',*flags,'-MD','-MF',dep,'-c',HERE/(kind+'.c'),'-o',obj],env=dict(os.environ,DARWIN_SDK_ROOT=SDK))
            header=run([TOOL+'otool','-hv',obj]).stdout
            if 'ARM64' not in header or 'OBJECT' not in header:raise RuntimeError(header)
            undefined=run([TOOL+'nm','-u',obj]).stdout.split();imports.update(undefined)
            if any('rg_test' in s for s in undefined):raise RuntimeError('Test hook in production')
            for p in dep.read_text().replace('\\\n',' ').split(':',1)[1].split():deps[str(Path(p).resolve())]=sha(p)
            result['SDK_objects'].append({'source':kind+'.c','sha256':sha(obj),'undefined':undefined,'flags':flags})
        if sha(SYSTEM)!='9fc03c87e929310d881bd5b440bf0f3d7f0b230d79521b07cea539cf5c7e481e':raise RuntimeError('libSystem changed')
        exports=set()
        for path in [SYSTEM,SYSTEM.parent/'system/libdyld.dylib']:
            text=run(['llvm-objdump','--macho','--exports-trie',path]).stdout
            exports.update(re.findall(r'\b(_[^\s]+)',text))
        if imports-exports:raise RuntimeError('Unresolved imports '+repr(imports-exports))
        result['imports_resolved_in_retained_libSystem_or_libdyld']=sorted(imports)
        result['SDK_dependencies_sha256']=dict(sorted(deps.items()));result['SDK']=SDK;result['compiler']=TOOL+'clang';result['libSystem_sha256']=sha(SYSTEM)
        result['libSystem_path']=str(SYSTEM);result['libdyld_sha256']=sha(SYSTEM.parent/'system/libdyld.dylib')
    print(json.dumps(result,indent=2))
if __name__=='__main__':main()
