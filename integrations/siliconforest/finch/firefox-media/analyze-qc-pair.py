#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Private matched QC copy admission. Actual four renderer images need review."""
import argparse
import importlib.util
import json
from pathlib import Path
import qc_copy_control as qc

spec=importlib.util.spec_from_file_location('installed_video',Path(__file__).with_name('analyze-installed-video.py'))
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)

def arm(stage,mode,partial1080=False):
    stage=Path(stage);run=stage/'target-browser/browser-run'
    launch=json.loads((run/'cadence-launch.json').read_text())
    raw=json.loads((run/'browser/result.json').read_text())
    identity=json.loads((stage/'qc-nv12/identity.json').read_text())
    original=base.analyze(run)
    # The three installed/graphics gates MUST be replaced by explicit private
    # admission, never promoted to a canonical or installed success.
    checks={k:v for k,v in original['checks'].items() if k not in ['installed_outer','canonical_graphics','actual_graphics','native_output_collected']}
    checks['own_native_stdout']=bool((run/'browser/firefox.log').read_text(errors='replace').strip())
    if partial1080:
        checks.pop('two_frozen_clips',None)
        checks['explicit_partial_1080']=launch.get('qc_1080_only') is True and len(original['clips'])==1 and original['clips'][0]['expected_frames']==600
    else:checks['complete_clip_selection']=launch.get('qc_1080_only') is not True
    if (run/'qc-settle.json').is_file():
        settle=json.loads((run/'qc-settle.json').read_text())
        checks['fixed_settle_control']=settle.get('requested_seconds')==launch.get('qc_startup_settle_seconds',0) and settle.get('elapsed_seconds',-1)>=settle['requested_seconds'] and settle.get('normal_services') is True and settle.get('quiet_preferences') is False and settle.get('owner_idle_wait') is False and settle.get('outside_measurements') is True
    else:checks['fixed_settle_control']='qc_startup_settle_seconds' not in launch # Legacy default0, no invented timing receipt.
    checks['private_only']=launch.get('private_quartzcore_copy')==mode and not any(launch.get(k) for k in ['staged_media','graphics_override','private_frame_identity_graphics'])
    target=Path('/private/var/db/sffx')/stage.name
    try:qc.verify(raw['providers'],identity,target);checks['actual_private_cohort']=True
    except (ValueError,RuntimeError,KeyError):checks['actual_private_cohort']=False
    wrapper=json.loads((run/'browser/private-media-launch.json').read_text())
    checks['actual_switch']=wrapper.get('private_quartzcore_copy')==str(target/'qc-nv12') and wrapper.get('retained_installed_media') is True and wrapper.get('environment',{}).get('SF_CA_NV12_GPU_COPY')==('1' if mode=='on' else '0')
    clips=[]
    for index,(entry,clip) in enumerate(zip(raw.get('video_measurements',[]),original['clips'])):
        observed=entry['video'].get('quartzcore_uploads',{})
        delta=qc.compare(observed['before'],observed['after']);total=delta['total']
        checks[f'{index}_counter_receipt']=delta==observed['comparison'] and observed.get('gpu_copy')==mode
        checks[f'{index}_both_roles']=set(p['role'] for p in delta['processes'])=={'parent','gpu'}
        checks[f'{index}_nonvacuous']=total['yuv_frames']>=clip['expected_frames']*.8 and total['wide_cpu_frames']==0
        if mode=='on':
            checks[f'{index}_qc_gpu']=total['gpu_attempts']==total['gpu_frames']==total['yuv_frames'] and total['gpu_failures']==0
            checks[f'{index}_qc_cpu_zero']=all(total[k]==0 for k in ['cpu_frames','cpu_plane_calls','cpu_visible_bytes','cpu_source_span_bytes'])
        else:
            checks[f'{index}_qc_cpu_baseline']=total['cpu_frames']==total['yuv_frames'] and total['cpu_plane_calls']>=2*total['cpu_frames'] and total['cpu_visible_bytes']>0 and total['cpu_source_span_bytes']>=total['cpu_visible_bytes'] and total['gpu_attempts']==total['gpu_frames']==0
        checks[f'{index}_cpu_available']=clip.get('cpu_family_complete') is True and isinstance(clip.get('matched_family_cpu'),(float,int))
        clips.append(dict(clip,qc_uploads=delta))
    return dict(passed=all(checks.values()),checks=checks,clips=clips,launch=launch,
        identity=identity,providers=raw['providers'],installed_admission=original['passed'],
        installed_admission_must_remain_false=True,visual_review='REQUIRED actual two renderer images',
        partial_1080_only=partial1080,
        scope='Owned private QC/CGL caller, not an installed provider, panel FPS or all browser memory copies.'+(' H264 ONLY: 4K admission remains open.' if partial1080 else ''))

def compare(off,on,cgl_variant_proof=None):
    variant=cgl_variant_proof is not None
    source_checks=['same_exports','same_drv_inputs','env_only_postpatch_out','null_control_exact',
        'GLAPI_unchanged','GLU_unchanged','not_installed','paired_function_only_source_change']
    approved_variant=variant and all(cgl_variant_proof.get('checks',{}).get(k) is True for k in source_checks) and all(cgl_variant_proof.get('checks',{}).values()) and cgl_variant_proof.get('baseline')==off['identity'].get('cgl') and cgl_variant_proof.get('candidate')==on['identity'].get('cgl') and off['identity'].get('cgl')!=on['identity'].get('cgl')
    def other_originals(value):return {k:v for k,v in value['identity'].items() if k!='cgl'}
    checks=dict(both_arms=off['passed'] and on['passed'],
        same_release=off['launch']['release']==on['launch']['release'],
        same_kernel=off['launch']['kernel_uuid']==on['launch']['kernel_uuid'],
        same_originals=other_originals(off)==other_originals(on) if variant else off['identity']==on['identity'],
        installed_claim_refused=off['installed_admission'] is False and on['installed_admission'] is False)
    if variant:checks['exact_private_cgl_change']=approved_variant
    checks['same_settle_selection']=off['launch'].get('qc_startup_settle_seconds',0)==on['launch'].get('qc_startup_settle_seconds',0) and off['launch'].get('qc_1080_only',False)==on['launch'].get('qc_1080_only',False)
    def providers(value):
        return sorted((p['role'],x['name'],'approved-private-CGL' if variant and x['name']=='OpenGL' else x['uuid'],x['path'].split('/qc-nv12/',1)[-1]
                       if '/qc-nv12/' in x['path'] else x['path'])
            for p in value['providers']['processes'] for x in qc.image_rows(p['images']))
    checks['same_actual_providers']=providers(off)==providers(on)
    rows=[]
    for a,b in zip(off['clips'],on['clips']):
        checks['same_clip_'+str(a['expected_frames'])]=a['source'].rsplit('/',1)[-1]==b['source'].rsplit('/',1)[-1] and a['expected_frames']==b['expected_frames']
        rows.append(dict(source=a['source'],frames=a['expected_frames'],
            cpu_off=a['cpu_percent_one_core'],cpu_on=b['cpu_percent_one_core'],
            family_cpu_off=a['matched_family_cpu'],family_cpu_on=b['matched_family_cpu'],
            qc_off=a['qc_uploads']['total'],qc_on=b['qc_uploads']['total'],
            off_callback_gap_ms=a.get('rvfc_ms'),on_callback_gap_ms=b.get('rvfc_ms')))
    return dict(passed=all(checks.values()),checks=checks,clips=rows,
        controlled_change='Private CGL paired function' if variant else 'Owned SF_CA_NV12_GPU_COPY switch',
        arm_labels=dict(off='current helper',on='batched helper') if variant else dict(off='CPU upload',on='GPU copy'),
        partial_1080_only=off.get('partial_1080_only',False),
        visual_review='REQUIRED: independently inspect all '+('two' if off.get('partial_1080_only') else 'four')+' actual renderer images',
        limits='One fresh-profile OFF then ON pair; startup/background work and ordering can confound CPU. Counter-zero applies to observed decoder/CGL/QC transport, not every driver/browser copy. Not panel FPS, completed flips or asynchronous-fence qualification.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--off',type=Path,required=True);p.add_argument('--on',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--partial-1080',action='store_true');p.add_argument('--cgl-variant-proof',type=Path);a=p.parse_args()
    proof=json.loads(a.cgl_variant_proof.read_text()) if a.cgl_variant_proof else None
    off=arm(a.off,'on' if proof is not None else 'off',a.partial_1080);on=arm(a.on,'on',a.partial_1080);result=dict(off=off,on=on,pair=compare(off,on,proof))
    a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result['pair'],indent=2));raise SystemExit(0 if result['pair']['passed'] else 1)
