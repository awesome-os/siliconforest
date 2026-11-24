#!/usr/bin/env python3
"""Run offline tooling tests and the native C policy tests. Never boot a target."""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import io
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))

def command(args: list[str], timeout: int=60) -> dict:
    try:
        result=subprocess.run(args,cwd=ROOT,capture_output=True,text=True,timeout=timeout,check=False)
        return {"command":args,"returncode":result.returncode,"stdout":result.stdout,"stderr":result.stderr}
    except (OSError,subprocess.TimeoutExpired) as exc:
        return {"command":args,"returncode":None,"stdout":"","stderr":str(exc)}

def main(argv: list[str]|None=None)->int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize',action='store_true')
    parser.add_argument('--cross',action='store_true',help='Also compile our core for Darwin ARM64 and Linux AArch64; no target execution')
    parser.add_argument('--cc',default=None)
    parser.add_argument('--report',type=Path,default=ROOT/'.build/checks.json')
    a=parser.parse_args(argv)
    started=datetime.now(timezone.utc).isoformat()
    build=ROOT/'.build'; build.mkdir(exist_ok=True)
    log=io.StringIO(); suite=unittest.defaultTestLoader.discover(str(ROOT/'tests'))
    result=unittest.TextTestRunner(stream=log,verbosity=2).run(suite)
    unit={"run":result.testsRun,"failures":len(result.failures),"errors":len(result.errors),"skipped":len(result.skipped),"passed":result.wasSuccessful()}
    print(log.getvalue(),end='')
    cc=a.cc or shutil.which('clang') or shutil.which('cc')
    c_results=[]; compiler=None; c_count=0; c_suites=[]
    if cc:
        compiler=command([cc,'--version'])
        flags=['-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O1','-g']
        if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        suites=[
            ('buffer-lifetime',['contracts/buffer_lifetime.c','tests/c/test_buffer_lifetime.c']),
            ('backing-store',['contracts/buffer_lifetime.c','core/backing_store.c','tests/c/test_backing_store.c']),
            ('mapping-ticket',['contracts/buffer_lifetime.c','core/backing_store.c','core/mapping_ticket.c','tests/c/test_mapping_ticket.c']),
            ('memory-probe',['contracts/buffer_lifetime.c','core/backing_store.c','core/mapping_ticket.c','probe/memory_probe.c','tests/c/test_memory_probe.c']),
            ('memory-probe-result',['tests/c/test_memory_probe_result.c']),
        ]
        for name, sources in suites:
            binary=build/(name+('-tests.exe' if os.name=='nt' else '-tests'))
            compile_result=command([cc,*flags,*(['-pthread'] if name=='mapping-ticket' else []),'-Icontracts','-Icore','-Iprobe','-Iuserspace',*sources,'-o',str(binary)])
            c_results.append(compile_result)
            groups=0
            if compile_result['returncode']==0:
                run=command([str(binary)])
                c_results.append(run)
                match=re.search(r'^1\.\.(\d+)$',run['stdout'],re.M)
                groups=int(match.group(1)) if match and run['returncode']==0 else 0
            c_count+=groups
            c_suites.append({'name':name,'passed_groups':groups})
        # Freestanding compile only. Compiler runtime helpers may still be emitted.
        for src in ('contracts/buffer_lifetime.c','core/backing_store.c','core/mapping_ticket.c','probe/memory_probe.c'):
            c_results.append(command([cc,'-std=c11','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror',
                '-Icontracts','-Icore','-Iprobe','-c',src,'-o',str(build/(Path(src).stem+'-freestanding.o'))]))
            # Check unoptimized component frames separately from host tests,
            # whose fixtures deliberately contain large automatic objects.
            c_results.append(command([cc,'-std=c11','-O0','-ffreestanding','-fno-builtin',
                '-Wall','-Wextra','-Werror','-Wframe-larger-than=1024',
                '-Icontracts','-Icore','-Iprobe','-c',src,
                '-o',str(build/(Path(src).stem+'-frame-check.o'))]))
        smoke=build/'memory-probe-smoke'
        smoke_build=command([cc,*flags,'-Iprobe','userspace/memory_probe_smoke.c','-o',str(smoke)])
        c_results.append(smoke_build)
        if smoke_build['returncode']==0:
            c_results.append(command([str(smoke),'--posix-control']))
    cross=None
    if a.cross:
        cross_command=command([sys.executable,str(ROOT/'tools/cross_abi.py'),'--report',str(build/'cross-abi.json')])
        c_results.append(cross_command)
        if (build/'cross-abi.json').is_file():
            cross=json.loads((build/'cross-abi.json').read_text())
    for item in c_results:
        print('$',' '.join(item['command'])); print(item['stdout'],end=''); print(item['stderr'],end='',file=sys.stderr)
    passed=bool(result.wasSuccessful() and not result.skipped and cc and len(c_suites)==5 and all(x['passed_groups']>0 for x in c_suites) and all(x['returncode']==0 for x in c_results))
    source_files=sorted({p for folder in ('tools','tests','contracts','core','probe','integrations','userspace') for p in (ROOT/folder).rglob('*') if p.is_file() and p.suffix in {'.py','.c','.h','.cpp'}})
    source_hashes={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files}
    report={"schema_version":1,"tested_source_sha256":source_hashes,"started_at":started,"host":{"system":platform.system(),"machine":platform.machine(),"python":platform.python_version()},
            "status":"PASSED_LOCAL_TOOLING" if passed else "FAILED_OR_INCOMPLETE", "unit_tests":unit,
            "native_c_test_groups":c_count,"native_c_suites":c_suites,"cross_requested":a.cross,"cross_compilation":cross,"sanitizers_requested":a.sanitize,"compiler":compiler,"c_commands":c_results,
            "unoptimized_native_frame_limit_bytes":1024,
            "xnu_cpp_adapter_compiled":False,"posix_control_is_not_xnu":True,"os_built":False,"xnu_booted":False,"gpu_tested":False,"hardware_accessed":False,
            "limits":["Native tests exercise original policy/coordinator code and a host allocator, not Omacom or PureDarwin drivers.",
                      "Synthetic C layout tests do not establish Darwin or Asahi ABI compatibility.",
                      "The experimental IOKit C++ adapter is source-only, not compiled or run by these checks.",
                      "The POSIX control uses real host mappings with a temporary file, not XNU.",
                      "No live network acquisition or GitHub publication is tested by this command."]}
    a.report.parent.mkdir(parents=True,exist_ok=True)
    a.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    a.report.with_suffix('.unittest.txt').write_text(log.getvalue(),encoding='utf-8')
    print(json.dumps({"status":report['status'],"python_tests":unit['run'],"c_test_groups":c_count,"report":str(a.report)},indent=2))
    return 0 if passed else 1
if __name__=='__main__':raise SystemExit(main())
