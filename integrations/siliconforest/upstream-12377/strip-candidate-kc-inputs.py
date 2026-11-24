# SPDX-License-Identifier: MIT
# Derived from m1-firefox-kernel/strip-kc-inputs.py, same non-debug-byte proof.
# The optional full variant adds em + igc; membership is checked by verify-candidate.py.
import pathlib as P,struct as S,subprocess as SP,re,shutil,json,sys,hashlib,os,shlex
phase,out,strip,dest=map(str,sys.argv[1:]);dest=P.Path(dest);dest.mkdir(exist_ok=True)
def sections(path):
 b=path.read_bytes();assert S.unpack_from('<I',b)[0]==0xfeedfacf
 off=32;data={}
 for _ in range(S.unpack_from('<I',b,16)[0]):
  cmd,n=S.unpack_from('<II',b,off)
  if cmd==0x19:
   for j in range(S.unpack_from('<I',b,off+64)[0]):
    q=off+72+80*j;seg=b[q+16:q+32].rstrip(b'\0').decode();name=b[q:q+16].rstrip(b'\0').decode()
    if seg=='__DWARF':continue
    addr,size,pos=S.unpack_from('<QQI',b,q+32);flags=S.unpack_from('<I',b,q+64)[0]
    data[seg+','+name]=(addr,size,None if flags&0xff in (1,12,18) else hashlib.sha256(b[pos:pos+size]).hexdigest())
  off+=n
 return data
s=P.Path(phase).read_text();variables={}
for line in s.splitlines():
 if re.match(r'^[A-Z_][A-Z0-9_]*=["\']?/nix/store/',line):
  assignment=shlex.split(line);assert len(assignment)==1
  key,value=assignment[0].split('=',1);variables[key]=value
proof=[]
def replace(m):
 value=re.sub(r'\$(?:\{([A-Z_][A-Z0-9_]*)\}|([A-Z_][A-Z0-9_]*))',lambda x:variables[x[1] or x[2]],m[1])
 assert '$' not in value and value.startswith('/nix/store/'),value
 source=P.Path(value);target=dest/str(len(proof))/source.name
 target.parent.mkdir();shutil.copytree(source,target,symlinks=False)
 for p in target.rglob('*'):
  if not p.is_symlink():p.chmod(p.stat().st_mode|0o200)
 name=source.stem;before=source/'Contents/MacOS'/name;after=target/'Contents/MacOS'/name
 x=sections(before);SP.run([strip,'-S','-x',str(after)],check=True);y=sections(after);assert x==y,source
 proof.append({'bundle':name,'source':str(source),'before_bytes':before.stat().st_size,'after_bytes':after.stat().st_size,'all_non_debug_sections_identical':True,'sections':x})
 return '-kext "'+str(target)+'"'
s=re.sub(r'-kext "([^"]+\.kext)"',replace,s)
expected=58 if os.environ['SF_SELECTED_NETWORK_MODULES']=='1' else 56
assert len(proof)==expected,len(proof)
P.Path(out).write_text(s);P.Path('kc-strip-proof.json').write_text(json.dumps(proof,indent=2)+'\n')
print('Local-symbol stripping:',len(proof),'bundles;',sum(p['before_bytes']-p['after_bytes'] for p in proof),'bytes removed; all non-debug section addresses/sizes/bytes identical',flush=True)
