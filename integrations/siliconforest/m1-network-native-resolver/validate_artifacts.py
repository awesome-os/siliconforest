#!/usr/bin/env python3
"""Read-only ARM64/source/import validation. Never executes a Darwin program."""
from pathlib import Path
import argparse
import hashlib
import json
import posixpath
import re
import shlex
import struct
import subprocess

PIN='e7aa00a60f0b56ef35aa24252e81467787bf71ea'
SYSTEM_SHA='9fc03c87e929310d881bd5b440bf0f3d7f0b230d79521b07cea539cf5c7e481e'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def command(args):return subprocess.check_output(args,text=True)
def exports(path):
 text=command(['llvm-objdump','--macho','--exports-trie',str(path)])
 return {line.split()[1] for line in text.splitlines() if re.match(r'^0x[0-9A-Fa-f]+\s+\S+',line)}
def binary(path,available):
 raw=path.read_bytes();magic,cpu,subtype,kind,count,commandbytes,flags,_=struct.unpack_from('<8I',raw)
 assert magic==0xfeedfacf and cpu==0x100000c and subtype==0 and kind==2
 at=32;dependencies=[];interpreter=None;versions=[];codesign=False
 for _ in range(count):
  cmd,size=struct.unpack_from('<II',raw,at);assert size>=8 and at+size<=32+commandbytes
  if cmd==0xc:
   off=struct.unpack_from('<I',raw,at+8)[0];dependencies.append(raw[at+off:at+size].split(b'\0')[0].decode())
  if cmd==0xe:
   off=struct.unpack_from('<I',raw,at+8)[0];interpreter=raw[at+off:at+size].split(b'\0')[0].decode()
  if cmd==0x32:versions.append(struct.unpack_from('<III',raw,at+8))
  if cmd==0x1d:codesign=True
  at+=size
 assert at==32+commandbytes and dependencies==['/usr/lib/libSystem.B.dylib'] and interpreter=='/usr/lib/dyld'
 assert versions==[(1,0x1a0500,0x1a0500)] and codesign
 imports=set(command(['llvm-nm','-u',str(path)]).splitlines())
 assert imports and not imports-available,sorted(imports-available)
 return dict(path=str(path),sha256=sha(path),bytes=len(raw),cpu='ARM64_ALL',type='MH_EXECUTE',
  platform='macOS',minimum='26.5',SDK='26.5',dependencies=dependencies,interpreter=interpreter,
  code_signature_command=True,imports=sorted(imports),import_count=len(imports),unresolved=[])
def validate(mdnsd,probe,system,repo):
 lib=system/'usr/lib/libSystem.B.dylib';dyld=system/'usr/lib/system/libdyld.dylib'
 assert sha(lib)==SYSTEM_SHA
 available=exports(lib)|exports(dyld)
 daemon=binary(mdnsd/'usr/sbin/mdnsd',available);client=binary(probe/'usr/bin/darwin-resolver-probe',available)
 assert '_getaddrinfo' in client['imports'] and '_freeaddrinfo' in client['imports']
 build=mdnsd/'share/native-resolver-build';source=build/'source'
 commands=(build/'mdnsd-commands.txt').read_text();units=[]
 for line in commands.splitlines():
  args=shlex.split(line)
  if '-c' in args:units.append(args[args.index('-c')+1].removeprefix('/build/source/'))
 assert len(units)==17 and len(set(units))==17
 assert str(system/'usr/lib') in commands and '-DPOSIX_BUILD' in commands
 inputs=set(units)|{'src/Userspace/mdnsd/CMakeLists.txt','cmake/crosscompile.cmake','cmake/prebuilt_libsystem.cmake','CMakeLists.txt'}
 external=set()
 for line in (build/'compiler-dependencies.txt').read_text().splitlines():
  if line.startswith('    /build/source/'):
   inputs.add(posixpath.normpath(line.strip().removeprefix('/build/source/')))
  elif line.startswith('    /nix/store/'):
   external.add(line.strip())
 checked={}
 for name in sorted(inputs):
  path=source/name
  assert path.is_file(),name
  expected=subprocess.check_output(['git','-C',str(repo),'show',PIN+':'+name])
  assert path.read_bytes()==expected,name
  checked[name]=sha(path)
 current=Path(__file__).resolve().parent
 for name in ['resolver_probe.c','resolver_result.c','resolver_result.h']:
  assert (probe/'share/native-resolver-probe/source'/name).read_bytes()==(current/name).read_bytes()
 return dict(schema_version=1,status='SOURCE_AND_ARM64_IMPORT_CLOSURE_VERIFIED_NOT_RUNTIME_EXECUTED',
  pin=PIN,mdnsd=daemon,probe=client,libSystem=dict(path=str(lib),sha256=sha(lib)),
  libdyld=dict(path=str(dyld),sha256=sha(dyld)),compiled_daemon_units=units,
  source_inputs_matching_exact_pin=checked,external_header_count=len(external),
  external_headers_set_sha256=hashlib.sha256(json.dumps(sorted(external),separators=(',',':')).encode()).hexdigest(),
  build_evidence={str(p):sha(p) for p in [build/'CMakeCache.txt',build/'build.ninja',build/'mdnsd-commands.txt',build/'compiler-dependencies.txt']},
  probe_source_copy_matches=True,SDK_ABI_assertions=['sockaddr_in16,len0,family1,port2,address4,zero8','addrinfo48,addrlen16,canonname24,addr32,next40','AF_INET2,SOCK_STREAM1,IPPROTO_TCP6'],
  actual_compiler=(probe/'share/native-resolver-probe/compiler.txt').read_text(),
  not_claimed=['Darwin execution','system resolver operation','daemon lifetime/UDS cleanup','network or hardware execution'])
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--mdnsd-output',type=Path,required=True);p.add_argument('--probe-output',type=Path,required=True);p.add_argument('--system',type=Path,required=True);p.add_argument('--repo',type=Path,required=True)
 a=p.parse_args();print(json.dumps(validate(a.mdnsd_output,a.probe_output,a.system,a.repo),indent=2))
