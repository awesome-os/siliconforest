#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Compact only structural XML whitespace in our generated collection metadata.
# Preserve plist values/tags, every module and every other section/offset.
import pathlib,struct,plistlib,re,json,hashlib,sys
p=pathlib.Path(sys.argv[1]);raw=p.read_bytes();data=bytearray(raw);off=32;target=None;segments=[];payloads=[]
assert struct.unpack_from('<I',raw)[0]==0xfeedfacf
for _ in range(struct.unpack_from('<I',raw,16)[0]):
 cmd,size=struct.unpack_from('<II',raw,off)
 if cmd==2:
  sym,count,strings,length=struct.unpack_from('<IIII',raw,off+8)
  payloads.extend([(sym,count*16,'symbol-table'),(strings,length,'string-table')])
 elif cmd in (0x1d,0x1e,0x26,0x29,0x2b,0x2e,0x80000033,0x80000034):
  start,length=struct.unpack_from('<II',raw,off+8);payloads.append((start,length,hex(cmd)))
 elif cmd in (0x22,0x80000022):
  for k in range(5):
   start,length=struct.unpack_from('<II',raw,off+8+8*k);payloads.append((start,length,'dyld-info'))
 if cmd==0x19:
  name=raw[off+8:off+24].rstrip(b'\0');vm,vs,fo,fs=struct.unpack_from('<QQQQ',raw,off+24)
  segments.append((name,vm,vs,fo,fs))
  if name==b'__PRELINK_INFO':
   assert target is None and struct.unpack_from('<I',raw,off+64)[0]==1
   q=off+72;assert raw[q:q+16].rstrip(b'\0')==b'__info'
   addr,n,pos=struct.unpack_from('<QQI',raw,q+32);assert addr==vm and pos==fo and n<=fs
   target=(off,q,vm,vs,fo,fs,n)
 off+=size
assert target
seg,section,vm,old_vs,fo,old_fs,n=target
for name,v,sz,f,z in segments:
 if name==b'__PRELINK_INFO' or not z:continue
 # The existing fileset converter extends __LINKEDIT's FILE extent over
 # appended kexts/metadata to reach chained fixups. Validate real payloads
 # separately; retain that containing load command byte for byte.
 if name==b'__LINKEDIT' and f<=fo and fo+old_fs<=f+z:continue
 assert fo+old_fs<=f or f+z<=fo,(name,fo,old_fs,f,z)
for f,z,kind in payloads:
 if z:assert fo+old_fs<=f or f+z<=fo,(kind,fo,old_fs,f,z)
xml=raw[fo:fo+n].rstrip(b'\0');before=plistlib.loads(xml);root=xml.index(b'<plist');parts=re.split(rb'(<[^>]+>)',xml[root:]);stack=[];out=[]
for part in parts:
 if part.startswith(b'<'):
  if part.startswith(b'</'):
   name=part[2:-1].strip();assert stack and stack.pop()==name
  elif not part.endswith(b'/>'):
   name=part[1:-1].split(None,1)[0];assert name in (b'plist',b'dict',b'array',b'key',b'string',b'data',b'integer',b'real',b'date')
   stack.append(name)
  out.append(part)
 elif part.strip() or (stack and stack[-1] not in (b'plist',b'dict',b'array')):out.append(part)
assert not stack
compact=xml[:root]+b''.join(out);assert plistlib.loads(compact)==before
compact+=b'\0';assert len(compact)<n and len(compact)<=old_fs
# Shrink both load-command extents and the actual section. No gate is bypassed.
data[fo:fo+old_fs]=compact+b'\0'*(old_fs-len(compact))
struct.pack_into('<Q',data,seg+32,len(compact));struct.pack_into('<Q',data,seg+48,len(compact));struct.pack_into('<Q',data,section+40,len(compact))
# Apart from this segment's size fields and metadata bytes, preserve every byte.
check=bytearray(data);check[fo:fo+old_fs]=raw[fo:fo+old_fs]
for where in (seg+32,seg+48,section+40):check[where:where+8]=raw[where:where+8]
assert bytes(check)==raw
p.write_bytes(data)
report={'semantic_plist_identical':True,'old_metadata_bytes':n,'new_metadata_bytes':len(compact),'removed_structural_whitespace_bytes':n-len(compact),'other_bytes_identical':True,'modules':len(before['_PrelinkInfoDictionary'])}
pathlib.Path('kc-prelink-compact-proof.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report),flush=True)
