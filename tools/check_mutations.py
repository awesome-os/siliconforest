#!/usr/bin/env python3
"""Test sensitivity to deliberate memory-coordinator or mapping regressions in disposable copies.

A mutation counts as detected ONLY if it compiles and the tests fail at runtime.
Compile errors or timeouts are failures of this check, not successful detections.
Never modifies working-tree sources. No network or hardware access.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
try:
    from .lab import ROOT
except ImportError:
    from lab import ROOT

MUTATIONS=[
    ('ignore_device_lease','if (v.owner_alive || v.host || v.device)', 'if (v.owner_alive || v.host)'),
    ('ignore_host_lease','if (v.owner_alive || v.host || v.device)', 'if (v.owner_alive || v.device)'),
    ('forget_byte_accounting','s->resident_bytes-=v.bytes;', '/* deliberately forget accounting */'),
    ('swallow_release_failure','return out->backend_failures ? DL_STORE_BACKEND_ERROR : DL_OK;', 'return DL_OK;'),
    ('drop_failed_release','if (!freed) { out->backend_failures++; out->retained++; continue; }',
     'if (!freed) { out->backend_failures++; /* deliberately drop backing ownership */ }'),
    ('ignore_owner','if (v.owner_alive || v.host || v.device)', 'if (v.host || v.device)'),
]

MAPPING_MUTATIONS=[
    ('ignore_notification','if (!notification_load_acquire(&ticket->release_notified))',
     'if (false && !notification_load_acquire(&ticket->release_notified))'),
    ('wrong_lease_kind','dl_store_hold(store, buffer, DL_CPU_MAP, &hold)',
     'dl_store_hold(store, buffer, DL_IN_FLIGHT, &hold)'),
    ('skip_range_end','request.bytes > view.bytes - request.offset','false'),
    ('skip_permissions','(request.requested_access & ~request.allowed_access)','false'),
    ('truncate_page_number','if (number > UINT32_MAX) return DL_OVERFLOW;','/* deliberate truncation */'),
    ('drop_notification','notification_store_release(&ticket->release_notified);',
     'notification_init(&ticket->release_notified); (void)notification_store_release;'),
]


PROBE_MUTATIONS=[
    ('ignore_version','if(version!=DLMP_VERSION || flags) return DL_INVALID;',
     'if(flags) return DL_INVALID; (void)version;'),
    ('forget_binding','s->bound=true;', 's->bound=false;'),
    ('forget_export_quarantine','if(v->export_only) v->p->ops.exported(v->p->context,cookie);',
     'if(v->export_only) { /* deliberately omit provider notification */ }'),
    ('hide_release_failure','v.backend_failures=c.backend_failures;', 'v.backend_failures=0;'),
    ('leak_failed_mapping_hold','if(status!=DL_OK) { (void)dl_map_abort(t); return status; }\n    v.export_only=true;',
     'if(status!=DL_OK) { return status; }\n    v.export_only=true;'),
    ('forget_owner_close','status = dl_store_close_owner(&p->store, s->buffer);', 'status = DL_OK;'),
    ('forget_engine_export','p->exported_serial[s->buffer.index]=s->buffer.serial;',
     '/* deliberately omit engine export tracking */'),
    ('ignore_engine_quarantine','if (slot->cookie == cookie && quarantined(p, slot))',
     'if (false && slot->cookie == cookie && quarantined(p, slot))'),
    ('hide_engine_quarantine','v.quarantined=p->collection_quarantined;',
     'v.quarantined=0;'),
]

RESULT_MUTATIONS=[
    ('ignore_copyout_sessions',
     'return dlmp_result_stats_valid(s) && s->open_sessions==2 &&',
     'return dlmp_result_stats_valid(s) && s->open_sessions<=2 &&'),
    ('ignore_pending_count','s->pending_maps==published-drained;', 'true;'),
    ('invent_reclaim_capability',
     'const bool can_reclaim=(capabilities & DLMP_CAP_MAPPED_RECLAIM)!=0;',
     'const bool can_reclaim=true;'),
    ('ignore_live_checkpoints','if(checkpoints!=DLMP_CHECK_COMPLETE || !bytes ||',
     'if((checkpoints & 0u) || !bytes ||'),
    ('confuse_provider_error_with_quarantine',
     'c->quarantined!=1 || c->backend_failures!=1 ||',
     'false || false ||'),
    ('ignore_quarantined_byte_charge',
     'c->quarantined!=1 || c->backend_failures!=1 ||\n           s->resident_bytes!=bytes || s->live_buffers!=1)',
     'c->quarantined!=1 || c->backend_failures!=1 ||\n           false || s->live_buffers!=1)'),
]


def run(cc: str, directory: Path, suite: str = 'backing-store') -> dict:
    directory.mkdir(parents=True,exist_ok=True)
    configs={
        'backing-store':('core/backing_store.c','tests/c/test_backing_store.c',MUTATIONS,[]),
        'mapping-ticket':('core/mapping_ticket.c','tests/c/test_mapping_ticket.c',MAPPING_MUTATIONS,['core/backing_store.c']),
        'memory-probe':('probe/memory_probe.c','tests/c/test_memory_probe.c',PROBE_MUTATIONS,['core/backing_store.c','core/mapping_ticket.c']),
        'memory-probe-result':('userspace/memory_probe_result.h','tests/c/test_memory_probe_result.c',RESULT_MUTATIONS,[]),
    }
    if suite not in configs: raise ValueError('Unknown mutation suite')
    source_path,test_path,mutations,dependencies=configs[suite]
    source=(ROOT/source_path).read_text()
    results=[]
    flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Icontracts','-Icore','-Iprobe']
    # The unmodified baseline MUST pass using the same compiler/flags/tests.
    for name,before,after in [('unmodified',None,None),*mutations]:
        header_only=Path(source_path).suffix=='.h'
        if header_only:
            variant=directory/name; variant.mkdir(exist_ok=True)
            path=variant/Path(source_path).name; binary=variant/'test'
        else:
            path=directory/(name+'.c'); binary=directory/name
        if before is not None and source.count(before)!=1:
            raise ValueError(f'{name}: mutation anchor is no longer unique')
        content=source if before is None else source.replace(before,after,1)
        path.write_text(content)
        sources=[test_path] if header_only else ['contracts/buffer_lifetime.c',*dependencies,str(path),test_path]
        command=[cc,*(['-I'+str(path.parent),'-Iuserspace'] if header_only else []),
                 *flags,*(['-pthread'] if suite=='mapping-ticket' else []),*sources,'-o',str(binary)]
        build=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=30)
        item={'name':name,'compile_command':command,'compile_returncode':build.returncode,'compile_stderr':build.stderr}
        if build.returncode==0:
            proc=subprocess.run([str(binary)],cwd=directory,capture_output=True,text=True,timeout=30)
            item.update(run_returncode=proc.returncode,stdout=proc.stdout,stderr=proc.stderr)
            item['status']=('BASELINE_PASSED' if proc.returncode==0 else 'BASELINE_FAILED') if before is None else (
                'DETECTED_AT_RUNTIME' if proc.returncode!=0 else 'MISSED_REGRESSION')
        else: item['status']='COMPILE_FAILED_NOT_A_DETECTION'
        results.append(item)
    passed=results[0]['status']=='BASELINE_PASSED' and all(r['status']=='DETECTED_AT_RUNTIME' for r in results[1:])
    return {'schema_version':1,'status':'PASSED_MUTATION_CHECKS' if passed else 'FAILED',
            'suite':suite,'source_path':source_path,'source_sha256':hashlib.sha256(source.encode()).hexdigest(),
            'test_sha256':hashlib.sha256((ROOT/test_path).read_bytes()).hexdigest(),
            'detected':sum(r['status']=='DETECTED_AT_RUNTIME' for r in results),'mutations':len(mutations),'results':results,
            'limits':['Deliberate source mutations are examples, not exhaustive correctness proof.',
                      'All execution is native host code with simulated device events, not an XNU driver.',
                      'A failing test process is expected for each mutated program.']}


def main(argv=None)->int:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--suite',choices=['backing-store','mapping-ticket','memory-probe','memory-probe-result'],default='backing-store')
    p.add_argument('--cc');p.add_argument('--report',type=Path,default=ROOT/'.build/mutations.json')
    p.add_argument('--build-dir',type=Path,default=ROOT/'.build/mutations')
    a=p.parse_args(argv)
    try:
        cc=a.cc or shutil.which('clang') or shutil.which('cc')
        if not cc: raise ValueError('C compiler required')
        report=run(cc,a.build_dir.resolve(),a.suite)
        a.report.parent.mkdir(parents=True,exist_ok=True)
        a.report.write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps({'status':report['status'],'detected':report['detected'],'mutations':report['mutations']}))
        return 0 if report['status']=='PASSED_MUTATION_CHECKS' else 1
    except (ValueError,OSError,subprocess.SubprocessError) as exc:
        a.report.parent.mkdir(parents=True,exist_ok=True)
        a.report.write_text(json.dumps({'status':'FAILED_OR_INCOMPLETE','error':str(exc)})+'\n')
        print(str(exc),file=sys.stderr);return 2
if __name__=='__main__':raise SystemExit(main())
