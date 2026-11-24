#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Target-only, finite installed-Firefox trace in a fresh isolated profile.

Caller holds gui.lock and bench and checks coord-gate immediately beforehand.
Uses the shared Firefox runner without changing its sources or installed files.
No pointer/key injection, default profile, preferences outside this run or signals
to processes outside the runner's identity-checked child family.
"""
import argparse
import json
import os
from pathlib import Path
import runpy
import socket
import subprocess
import sys
import types


def renderer_readback(destination):
    """Existing capture-only protocol; unlike --screencapture, includes GPU buffers."""
    from renderer_capture import capture
    import presentation_seat
    previous=presentation_seat.functional_qa_enabled()
    # Durable owner-idle lift. This scope sends ONLY a capture command; no
    # pointer, key, focus, seat or renderer settings are changed.
    presentation_seat.functional_qa(True)
    try:result=capture(destination)
    finally:presentation_seat.functional_qa(previous)
    if result.get('source')!='renderer':raise RuntimeError('Actual renderer readback was not established')
    result.update(input_injected=False,visible_playback_qualified=False)
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--run',type=Path,required=True)
    p.add_argument('--profile-video',action='store_true')
    p.add_argument('--rate-control',action='store_true')
    p.add_argument('--staged-media',action='store_true')
    p.add_argument('--installed-media',action='store_true',help='Exact installed video-shared-v1 selection; no private media wrapper')
    p.add_argument('--frame-mode',choices=['copy','require'],default='copy')
    p.add_argument('--without-trace',action='store_true')
    p.add_argument('--graphics-override',type=Path,help='FX-qualified complete cohort under the runtime overrides directory')
    p.add_argument('--no-native-compositor',action='store_true',help='Own-profile diagnostic: one full-window layer instead of native tile layers')
    p.add_argument('--timer-control',action='store_true',help='Own-process comparison using the existing SF_CV_TIMER escape hatch')
    p.add_argument('--codec-matrix',action='store_true',help='Owned AV1/VP9/PQ fixtures; declarations remain separate from decode')
    p.add_argument('--youtube',action='store_true',help='Own public non-DRM YouTube tab, no account/consent/ad interaction')
    p.add_argument('--youtube-profile',action='store_true',help='Separate diagnostic Gecko media profile; excludes performance qualification')
    p.add_argument('--youtube-1080',action='store_true',help='Request1080 on the admitted own video; require actual dimensions before measurement')
    p.add_argument('--disable-av1',action='store_true',help='Own1080 streaming profile only; never changes the installed preference')
    p.add_argument('--reuse-media-profile',type=Path,help='Previously completed own1080 profile; requires the reviewed copy-only runner patch')
    p.add_argument('--start-media',type=float,help='Own public1080 video seek before timing, seconds0..60')
    p.add_argument('--compare-pixels',action='store_true',help='After measurement, seek own video to10s and read128x72 diagnostic pixels')
    p.add_argument('--installed-codec-policy',action='store_true',help='Fresh actual installed codec default; no test codec preference assignment')
    p.add_argument('--av1-switchback',action='store_true',help='Fresh generated AV1-only control via installed SFFX_AV1=1 opt-in')
    p.add_argument('--lifetime-control',action='store_true',help='Observe owned ended-video idle, then unload its source; not a visible-memory qualification')
    p.add_argument('--retire-pool',action='store_true',help='Private invalidated-session pool retirement; requires strict lifetime control')
    p.add_argument('--mse',action='store_true',help='Own local muxed H264/AAC init-segment/representation sequence, not a network-adaptation benchmark')
    p.add_argument('--mse-scroll',action='store_true',help='Own fixture programmatic viewport scrolling; no pointer/key input')
    p.add_argument('--view-state',action='store_true',help='Main-queue read-only window/view getter diagnostic, no focus/input/drawing')
    p.add_argument('--frame-id',action='store_true',help='Own900-frame Gray-code fixture, preview calibration then15s playback; not a panel-rate claim')
    p.add_argument('--frame-id-geometry',help='Explicit private-CGL seven-field marker geometry; bounds are not pixel admission')
    p.add_argument('--frame-id-cgl',action='store_true',help='Packaged private CGL only; keep installed media/GLAPI/IOSurface/broker/UI plan')
    p.add_argument('--qc-copy',choices=['off','on'],help='Private QC/CGL paired-copy comparison; retains installed media and all other providers')
    p.add_argument('--qc-1080-only',action='store_true',help='Separate partial caller diagnostic while 4K resource admission is unavailable')
    p.add_argument('--qc-settle',type=int,choices=[0,60],default=0,help='Fixed normal-services startup control outside all measurements')
    p.add_argument('--frame-id-dimensions-only',action='store_true',help='Paused preview/private CGL export dimensions only; no geometry/readback/15s run')
    p.add_argument('--frame-id-quiet-rollouts',action='store_true',help='Disable Nimbus rollout/study work only in this fresh owned test profile')
    a=p.parse_args()
    if a.qc_copy and (not a.installed_media or not a.without_trace or a.frame_id or a.graphics_override or a.staged_media or a.mse or a.youtube or a.codec_matrix or a.profile_video):
        raise ValueError('Separate installed-media/no-trace private QC comparison required')
    if (a.qc_1080_only or a.qc_settle) and not a.qc_copy:raise ValueError('QC startup/clip controls require the private caller case')
    if a.frame_id and (not a.installed_media or not a.without_trace or a.youtube or a.codec_matrix or a.mse or a.rate_control or a.lifetime_control):
        raise ValueError('Frame identity requires its separate installed-media no-trace case')
    if a.frame_id_geometry:
        if not a.frame_id or not a.frame_id_cgl:raise ValueError('Marker geometry requires its packaged private-CGL case')
        from frame_geometry import parse
        parse(a.frame_id_geometry)
    elif a.frame_id_cgl and not a.frame_id_dimensions_only:raise ValueError('Private marker hook requires explicit geometry or paused dimensions preflight')
    if a.frame_id_dimensions_only and (not a.frame_id or not a.frame_id_cgl or a.frame_id_geometry):
        raise ValueError('Paused dimensions preflight requires private CGL and no geometry')
    if a.frame_id_quiet_rollouts and (not a.frame_id or a.frame_id_dimensions_only):
        raise ValueError('Rollout control requires the owned timed frame-ID case')
    if a.frame_id_cgl and (not a.frame_id or a.graphics_override or a.staged_media):
        raise ValueError('Private marker CGL must retain installed media and graphics plan')
    if a.view_state and (not a.mse or not a.without_trace):
        raise ValueError('View-state requires separate MSE without cadence trace (three observer limit)')
    if a.mse and (a.youtube or a.codec_matrix or a.rate_control or a.lifetime_control):
        raise ValueError('MSE representation control is a separate owned measurement')
    if a.mse_scroll and not a.mse:raise ValueError('Programmatic MSE scrolling requires its own fixture')
    # Do not leave crashreporter helpers from owned tests of the open platform
    # shutdown defect (FX-003). No owner process environment is changed.
    os.environ['MOZ_CRASHREPORTER_DISABLE']='1'
    if a.youtube_profile and not a.youtube:raise ValueError('YouTube profile requires its owned playback case')
    if a.youtube_1080 and not a.youtube:raise ValueError('Quality admission requires its own YouTube case')
    if (a.disable_av1 or a.reuse_media_profile or a.start_media is not None or a.compare_pixels) and not a.youtube_1080:
        raise ValueError('Codec/profile/seek controls require our admitted1080 public video')
    if a.start_media is not None and not 0<=a.start_media<=60:raise ValueError('Own content-window seek must be0..60s')
    if a.installed_codec_policy and (a.disable_av1 or a.reuse_media_profile or a.staged_media or a.graphics_override):
        raise ValueError('Installed policy gate requires a fresh profile and no private preference/provider override')
    if a.av1_switchback and (not a.installed_codec_policy or not a.installed_media or not a.codec_matrix or a.youtube):
        raise ValueError('Switchback requires its installed policy/generated AV1 case')
    if a.installed_codec_policy and not (a.youtube_1080 or a.av1_switchback):
        raise ValueError('Installed policy gate requires its fresh YouTube or generated AV1 case')
    if a.installed_codec_policy:
        if 'SFFX_AV1' in os.environ:raise ValueError('Policy gate refuses inherited codec override')
        if a.av1_switchback:os.environ['SFFX_AV1']='1'
    if a.youtube and (a.codec_matrix or a.rate_control):raise ValueError('Separate finite playback cases required')
    if a.lifetime_control and (not a.staged_media or a.frame_mode!='require' or a.youtube or a.codec_matrix or a.rate_control):
        raise ValueError('Lifetime control requires separate strict owned fixtures')
    if a.retire_pool and not a.lifetime_control:raise ValueError('Pool retirement requires its lifetime control')
    if a.installed_media and (a.staged_media or a.graphics_override):
        raise ValueError('Installed video gate cannot use private media or graphics')
    if a.frame_mode=='require' and not (a.staged_media or a.installed_media):
        raise ValueError('Strict sharing requires the reviewed staged VT producer')
    if a.graphics_override and (not a.staged_media or '..' in a.graphics_override.parts or
            not a.graphics_override.is_relative_to(Path('/usr/local/lib/siliconforest/firefox/160-system1/overrides'))):
        raise ValueError('Explicit reviewed runtime graphics cohort required')
    root=Path(__file__).resolve().parent
    private=Path('/private/var/db/sffx')
    if os.uname().sysname!='Darwin':raise ValueError('SiliconForest target only')
    if (not a.run.is_absolute() or '..' in a.run.parts or
            not a.run.is_relative_to(private) or a.run.exists()):
        raise ValueError('Fresh explicit private run required')
    library=root/'lib/SFMediaCadenceTrace.dylib'
    if not root.is_relative_to(private) or not library.is_file():
        raise ValueError('Cold-verified private trace packet required')
    # Current durable policy records input and missing markers, never waits for
    # owner idle. Keep this local adapter out of the shared Firefox runner.
    sys.path.insert(0,str(root))
    from owner_input import OwnerActivityRecorder
    class RecordOnlyGuard(OwnerActivityRecorder):
        def __init__(self,*args,require_idle=False,**kwargs):
            del require_idle
            super().__init__(*args,**kwargs)
    guard=types.ModuleType('owner_guard');guard.OwnerInputGuard=RecordOnlyGuard
    sys.modules['owner_guard']=guard
    tools=root/'tools';sys.path.insert(0,str(tools))
    qc_admitted={}
    if a.qc_copy:
        import provider_images
        import qc_copy_control
        qc_identity=json.loads((root/'qc-nv12/identity.json').read_text())
        inspect_original=provider_images.inspect
        def inspect_private_qc(sample,output,identities,parent_pid,*args,**kwargs):
            observed=inspect_original(sample,output,identities,parent_pid,*args,**kwargs)
            try:
                qc_admitted['images']=qc_copy_control.verify(observed,qc_identity,root)
                qc_admitted['starts']=dict(identities)
            finally:output.write_text(json.dumps(observed,indent=2)+'\n')
            return observed
        provider_images.inspect=inspect_private_qc
        def qc_snapshot():
            if not qc_admitted:raise RuntimeError('Private QC not admitted before media measurement')
            return qc_copy_control.snapshot(qc_admitted['images'],qc_identity,
                root/'bin/thread-policy-probe',qc_admitted['starts'])
    # The shared runner defaults to one fixed port. Other diagnostic Firefox
    # families can exist: select a fresh loopback port for this profile instead.
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1',0))
        port=reservation.getsockname()[1]
    import marionette
    original_client=marionette.Marionette
    chosen_port=port
    class IsolatedClient(original_client):
        def __init__(self,port=None,timeout=10):
            if port is not None and port!=chosen_port:
                raise ValueError('A shared Marionette port is not allowed')
            super().__init__(port=chosen_port,timeout=timeout)

        def command(self,name,parameters=None):
            # This is our frozen streaming.py's single bounded playback RPC,
            # after video identity/ad/consent admission. CPU excludes readiness
            # and network loading. Never instrument arbitrary page scripts.
            source=(parameters or {}).get('script','')
            if not (a.youtube and name=='WebDriver:ExecuteAsyncScript' and
                    'startMedia=v.currentTime' in source and 'v.requestVideoFrameCallback' in source):
                return super().command(name,parameters)
            import process_cpu
            quality=None
            if a.youtube_1080:
                import youtube_quality
                quality=youtube_quality.prepare(self,'aqz-KE-bpKQ',a.run/'youtube-quality-admission.json',start_media=a.start_media)
            before=self.cpu_snapshot();admission=None
            if a.youtube_profile:
                import pipeline_profile
                admission=pipeline_profile.start(self,media=True)
                # Profiler control runs in chrome; playback belongs to the
                # admitted owned page. Leaving chrome selected times out its
                # asynchronous document query instead of measuring the video.
                self.command('Marionette:SetContext',{'value':'content'})
            try:playback=super().command(name,parameters)
            finally:
                if admission:
                    directory=a.run/'youtube-gecko-profile';directory.mkdir()
                    try:profile=pipeline_profile.stop(self,directory,admission)
                    finally:self.command('Marionette:SetContext',{'value':'content'})
            after=self.cpu_snapshot()
            playback['owned_process_cpu']=dict(before=before,after=after,comparison=process_cpu.compare(before,after))
            if a.youtube_1080:
                import media_images
                directory=a.run/'youtube-media-providers';directory.mkdir()
                playback['media_providers']=media_images.inspect(after['processes'],directory,installed_frameworks)
            playback['profile_diagnostic_only']=a.youtube_profile
            if quality:
                playback['quality_admission']=quality
                playback['selected_format_after']=youtube_quality.describe(self)
                frames=playback.get('after',{}).get('total',0)-playback.get('before',{}).get('total',0)
                elapsed=playback.get('endMedia',0)-playback.get('startMedia',0)
                playback['observed_decoded_frames_per_media_second']=frames/elapsed if elapsed>0 else None
                playback['actual_1080p60_observed']=playback.get('width',0)>=1920 and playback.get('height',0)>=1080 and elapsed>=12 and frames/elapsed>=55
                self.youtube_completed=playback.get('actual_1080p60_observed') is True and not playback.get('error') and playback.get('ad') is False
                if a.compare_pixels:playback['diagnosticPixels']=youtube_quality.pixels(self,'aqz-KE-bpKQ')
            if admission:playback['gecko_profile']=profile
            try:
                capture=subprocess.run(['/usr/bin/chonkstep-darwin-desktop','--screencapture',str(a.run/'youtube-cpu-scene.ppm')],
                    capture_output=True,text=True,timeout=5,
                    env=dict(os.environ,XDG_RUNTIME_DIR='/private/var/run/gpu-desktop',WAYLAND_DISPLAY='wayland-0'))
                playback['cpu_scene_capture']=dict(returncode=capture.returncode,stdout=capture.stdout,stderr=capture.stderr,
                    file=str(a.run/'youtube-cpu-scene.ppm'),source='cpu-scene',input_injected=False,visible_playback_qualified=False)
            except Exception as error:playback['cpu_scene_capture_error']=str(error)
            try:playback['renderer_capture']=renderer_readback(a.run/'youtube-renderer.ppm')
            except Exception as error:playback['renderer_capture_error']=str(error)
            return playback

        def start(self,*args,**kwargs):
            result=super().start(*args,**kwargs)
            pid=int(result['capabilities']['moz:processID'])
            rows=subprocess.check_output(['ps','-axo','pid=,ppid=,lstart='],text=True)
            identities={int(p):(int(pp),start) for p,pp,start in
                        (row.split(None,2) for row in rows.splitlines())}
            current=pid;seen=set()
            while current in identities and current not in seen and current!=os.getpid():
                seen.add(current);current=identities[current][0]
            if current!=os.getpid():
                # No support/runtime/DOM command or Quit against another app.
                self.socket.close()
                raise RuntimeError('Marionette parent is outside this owned launch')
            self.owned_parent=pid
            self.owned_parent_start=identities[pid][1]
            # Read actual values, not just requested prefs or user.js. This
            # occurs only after the parent is admitted as our descendant.
            self.command('Marionette:SetContext',{'value':'chrome'})
            try:
                observed=self.command('WebDriver:ExecuteScript',{
                    'script':'''const p=Components.classes["@mozilla.org/preferences-service;1"]
                      .getService(Components.interfaces.nsIPrefBranch),result={};
                      for(const [name,kind] of [["browser.tabs.inTitlebar","int"],
                        ["webgl.disable-angle","bool"],["gfx.webrender.compositor","bool"],["media.av1.enabled","bool"],
                        ["nimbus.rollouts.enabled","bool"],["app.shield.optoutstudies.enabled","bool"]]){
                        try{result[name]={value:kind==="int"?p.getIntPref(name):p.getBoolPref(name),
                          userValue:p.prefHasUserValue(name)}}catch(e){result[name]={error:String(e)}}
                      }return result;''','args':[],'newSandbox':True})
                (a.run/'owned-startup-preferences.json').write_text(json.dumps(observed,indent=2)+'\n')
                if a.frame_id_quiet_rollouts:
                    checks={name:observed.get(name,{}).get('value') is False and observed.get(name,{}).get('userValue') is True
                            for name in ['nimbus.rollouts.enabled','app.shield.optoutstudies.enabled']}
                    (a.run/'rollout-control-admission.json').write_text(json.dumps(dict(checks=checks,
                        passed=all(checks.values()),test_profile_only=True,installed_preferences_changed=False),indent=2)+'\n')
                    if not all(checks.values()):raise RuntimeError('Own rollout control not applied')
                if a.installed_codec_policy:
                    settings=(a.run/'browser/profile/user.js').read_text()
                    value='true' if a.av1_switchback else 'false'
                    marker='// BEGIN SiliconForest hardware video preference\n'
                    end='// END SiliconForest hardware video preference\n'
                    block=settings.partition(marker)[2].partition(end)[0] if marker in settings and end in settings else ''
                    (a.run/'postlaunch-codec-profile.json').write_text(json.dumps(dict(
                        codec_marker_applied=('user_pref("media.av1.enabled",'+value+');') in block,
                        actual_av1_value=observed.get('media.av1.enabled',{}).get('value'),
                        test_pref_assigned=False),indent=2)+'\n')
            finally:self.command('Marionette:SetContext',{'value':'content'})
            return result

        def cpu_snapshot(self):
            import process_cpu
            rows=subprocess.check_output(['ps','-axo','pid=,ppid=,lstart='],text=True)
            identities={int(p):(int(pp),start) for p,pp,start in
                        (row.split(None,2) for row in rows.splitlines())}
            parent=self.owned_parent
            if parent not in identities or identities[parent][1]!=self.owned_parent_start:
                raise RuntimeError('Owned Firefox parent identity changed')
            owned={}
            for pid,(_,start) in identities.items():
                current=pid;seen=set()
                while current in identities and current not in seen and current!=parent:
                    seen.add(current);current=identities[current][0]
                if current==parent:owned[pid]=start
            return process_cpu.capture(self,parent,owned)

        def close(self):
            if (a.qc_copy and hasattr(self,'owned_parent')) or (a.frame_id and getattr(self,'frame_id_preview_completed',False)) or (a.frame_id_dimensions_only and getattr(self,'frame_id_preflight_completed',False)) or ((a.installed_media or a.staged_media) and getattr(self,'completed_installed_videos',0)==(1 if a.mse or a.av1_switchback or a.frame_id else 2)) or (a.youtube_1080 and getattr(self,'youtube_completed',False)):
                import time
                # After both measurements/pages/captures, request ordinary Quit
                # from this admitted client, before the shared runner's cleanup.
                # Keep its original survival verdict: the installed admission
                # separately requires this intentional natural status0 exit.
                result=dict(requested=True,cleanup_signals=[],family_before=[],family_remaining=[])
                def identities():
                    text=subprocess.check_output(['ps','-axo','pid=,ppid=,lstart='],text=True)
                    return {int(p):(int(pp),start) for p,pp,start in (line.split(None,2) for line in text.splitlines())}
                before=identities();owned={}
                if before.get(self.owned_parent,(None,None))[1]!=self.owned_parent_start:
                    raise RuntimeError('Quit parent identity changed')
                for pid,(_,start) in before.items():
                    current=pid;seen=set()
                    while current in before and current not in seen and current!=os.getpid():
                        seen.add(current);current=before[current][0]
                    if pid!=os.getpid() and current==os.getpid():owned[pid]=start
                result['family_before']=sorted(owned)
                result['parent']=self.owned_parent
                result['requested_monotonic']=time.monotonic()
                try:result['reply']=self.command('Marionette:Quit',{'flags':[]})
                except (OSError,EOFError) as error:result['transport_end']=type(error).__name__
                deadline=time.monotonic()+8
                while True:
                    current=identities()
                    remaining=[pid for pid,start in owned.items() if current.get(pid,(None,None))[1]==start]
                    # The launcher may remain a waitable zombie until Popen.poll.
                    if remaining:
                        states=subprocess.check_output(['ps','-axo','pid=,stat='],text=True)
                        live={int(p):state for p,state in (row.split(None,1) for row in states.splitlines())}
                        remaining=[pid for pid in remaining if not live.get(pid,'Z').startswith('Z')]
                    result['family_remaining']=remaining
                    if not remaining or time.monotonic()>=deadline:break
                    time.sleep(.05)
                result['elapsed_seconds']=time.monotonic()-result['requested_monotonic']
                (a.run/'natural-quit.json').write_text(json.dumps(result,indent=2)+'\n')
            return super().close()

        def lifetime_control(self,video):
            import process_cpu
            import time
            if not video.get('ended') or video.get('error'):raise RuntimeError('Completed own video required before lifetime control')
            source=video.get('source','')
            if not source.startswith((root/'media').as_uri()+'/'):raise RuntimeError('Only our frozen local media may be unloaded')
            def snapshot():
                return dict(cpu=self.cpu_snapshot(),global_decoder_stats=subprocess.check_output(
                    ['sysctl','kern.sf_avd.stats'],text=True,timeout=4))
            result=dict(retire_pool=a.retire_pool,synthetic_input=False,
                memory_source='Gecko ProcInfo memory bytes; not unique aggregate physical pages',
                limits='Global decoder counters can include other clients; no panel or owner-visible memory proof')
            result['ended_start']=snapshot();time.sleep(1)
            result['ended_after_idle']=snapshot()
            result['ended_idle_cpu']=process_cpu.compare(result['ended_start']['cpu'],result['ended_after_idle']['cpu'])
            self.command('Marionette:SetContext',{'value':'content'})
            result['unload']=self.command('WebDriver:ExecuteScript',dict(script='''
              const v=document.querySelector('video');
              if(!v||v.currentSrc!==arguments[0])throw new Error('Owned media identity changed');
              v.pause();v.removeAttribute('src');v.load();
              return {paused:v.paused,readyState:v.readyState,currentSrc:v.currentSrc};''',
              args=[source],newSandbox=False))
            result['unloaded_start']=snapshot();time.sleep(1)
            result['unloaded_after_idle']=snapshot()
            result['unloaded_idle_cpu']=process_cpu.compare(result['unloaded_start']['cpu'],result['unloaded_after_idle']['cpu'])
            return result

        def measure_video(self,handle=None):
            try:return self.measure_video_with_admission(handle)
            except Exception as error:
                if a.frame_id:
                    import traceback
                    (a.run/'frame-id-failure.json').write_text(json.dumps(dict(
                        error=repr(error),traceback=traceback.format_exc(),timed_playback_admitted=
                        (a.run/'frame-id-export-geometry-admission.json').exists()),indent=2)+'\n')
                raise

        def frame_id_capture(self,path,kind='FrameID'):
            if kind not in ['FrameID','Media']:raise ValueError('Only our fixture capture functions')
            self.command('Marionette:SetContext',{'value':'content'})
            armed=self.command('WebDriver:ExecuteScript',dict(
                script='return window.fxArm'+kind+'Capture();',args=[],newSandbox=False))
            attempt=dict(kind=kind,armed=armed,input_injected=False)
            try:
                receipt=renderer_readback(path)
                attempt['capture']=receipt
            except Exception as error:
                attempt['error']=str(error)
                raise
            finally:
                finished=self.command('WebDriver:ExecuteAsyncScript',dict(
                    script='const done=arguments[arguments.length-1]; window.fxFinish'+kind+'Capture().then(done,e=>done({error:String(e)}));',
                    args=[],scriptTimeout=4000,newSandbox=False))
                attempt['finished']=finished
                path.with_suffix('.capture-attempt.json').write_text(json.dumps(attempt,indent=2)+'\n')
                if finished.get('error'):raise RuntimeError('Capture repaint cleanup failed: '+str(finished))
            receipt['own_fixture_calibration_repaint' if kind=='FrameID' else 'own_fixture_postplay_repaint']=dict(armed=armed,finished=finished,
                outside_timed_playback=True,input_injected=False)
            return receipt

        def frame_id_flush(self):
            path=str(root/'frame-id-cgl/Frameworks/OpenGL.framework/Versions/A/OpenGL')
            self.command('Marionette:SetContext',{'value':'chrome'})
            try:
                result=self.command('WebDriver:ExecuteAsyncScript',dict(script='''
                  const path=arguments[0],done=arguments[arguments.length-1];
                  const start=Date.now();let library;
                  try {
                    const {ctypes}=ChromeUtils.importESModule("resource://gre/modules/ctypes.sys.mjs");
                    library=ctypes.open(path);
                    const flush=library.declare("SFFXFrameIDFlushLog",ctypes.default_abi,ctypes.int);
                    const attempt=()=>{
                      try {const status=flush();
                        if(status===1&&Date.now()-start<3000){setTimeout(attempt,20);return;}
                        library.close();done({status,path,start,end:Date.now(),explicit:true});
                      }catch(e){library.close();done({error:String(e),path});}
                    };attempt();
                  }catch(e){if(library)library.close();done({error:String(e),path});}
                ''',args=[path],scriptTimeout=5000,newSandbox=True))
            finally:self.command('Marionette:SetContext',{'value':'content'})
            (a.run/'frame-id-log-flush.json').write_text(json.dumps(result,indent=2)+'\n')
            if result.get('error') or result.get('status')!=0:raise RuntimeError('Explicit frame trace flush failed: '+str(result))
            return result

        def measure_video_with_admission(self,handle=None):
            # Snapshot outside the fixture interval. No profiler, per-frame
            # sampling or dependency on zero counters being valid CPU time.
            import process_cpu
            marker_preparation=None
            if a.frame_id:
                if handle is None:raise ValueError('Exact owned marker tab handle required')
                self.command('WebDriver:SwitchToWindow',{'handle':handle})
                self.command('Marionette:SetContext',{'value':'content'})
                marker_preparation=self.command('WebDriver:ExecuteAsyncScript',dict(
                    script='const done=arguments[arguments.length-1]; window.fxPrepareFrameID().then(done,e=>done({error:String(e)}));',
                    args=[],scriptTimeout=16000,newSandbox=False))
                if marker_preparation.get('error'):raise RuntimeError('Own marker calibration failed: '+str(marker_preparation))
                self.frame_id_preview_completed=True
                (a.run/'frame-id-preview.json').write_text(json.dumps(dict(
                    preview=marker_preparation,owned_parent=self.owned_parent,
                    owned_parent_start=self.owned_parent_start,pixel_admission_complete=False),indent=2)+'\n')
                marker_preparation['renderer_capture']=self.frame_id_capture(a.run/'frame-id-calibration.ppm')
                marker_preparation['owned_parent']=self.owned_parent
                marker_preparation['owned_parent_start']=self.owned_parent_start
                self.command('Marionette:SetContext',{'value':'chrome'})
                try:
                    marker_preparation['chrome']=self.command('WebDriver:ExecuteScript',dict(
                        script='''const pack=r=>({x:r.x,y:r.y,width:r.width,height:r.height});
                          return {root:pack(document.documentElement.getBoundingClientRect()),
                            browser:pack(gBrowser.selectedBrowser.getBoundingClientRect()),
                            innerWidth,innerHeight,outerWidth,outerHeight,devicePixelRatio};''',
                        args=[],newSandbox=True))
                except Exception as error:marker_preparation['chrome_error']=str(error)
                finally:self.command('Marionette:SetContext',{'value':'content'})
                (a.run/'frame-id-calibration.json').write_text(json.dumps(marker_preparation,indent=2)+'\n')
                if a.frame_id_cgl and not a.frame_id_dimensions_only:
                    import time
                    from frame_geometry import parse
                    from frame_probe_admission import native_zero
                    deadline=time.monotonic()+2;admission=None
                    while not admission and time.monotonic()<deadline:
                        with (a.run/'browser/firefox.log').open('rb') as log:data=log.read(8_000_001)
                        if len(data)>8_000_000:raise ValueError('Own native marker log exceeds bound')
                        admission=native_zero(data.decode(errors='replace'),self.owned_parent,parse(a.frame_id_geometry))
                        if not admission:time.sleep(.05)
                    if not admission:raise RuntimeError('No independently decoded owned native frame0; timed playback REFUSED')
                    (a.run/'frame-id-export-geometry-admission.json').write_text(json.dumps(admission,indent=2)+'\n')
            cpu={}
            if a.qc_copy and not getattr(self,'qc_settled',False):
                import time
                begin=time.monotonic()
                for _ in range(a.qc_settle//5):time.sleep(5)
                self.qc_settled=True
                (a.run/'qc-settle.json').write_text(json.dumps(dict(requested_seconds=a.qc_settle,
                    elapsed_seconds=time.monotonic()-begin,normal_services=True,quiet_preferences=False,
                    owner_idle_wait=False,input_injected=False,outside_measurements=True),indent=2)+'\n')
            qc_before=qc_snapshot() if a.qc_copy else None
            try:cpu['before']=self.cpu_snapshot()
            except Exception as error:cpu['before_error']=str(error)
            if a.frame_id_dimensions_only:
                video=dict(calibration_only=True,ended=False,paused=True,error=None,
                           source=marker_preparation['source'],sourceFrameCount=900,playback_measured=False)
            else:video=super().measure_video(handle)
            if a.frame_id:
                video['frame_identity_calibration']=marker_preparation
                video['frame_identity_limits']='Known encoded IDs and calibrated own video rectangle. JS decode/rVFC and one GPU capture do not qualify per-export or completed shown-frame cadence. Initial preview outside interval; native readback is diagnostic.'
            try:
                cpu['after']=self.cpu_snapshot()
                if 'before' in cpu:cpu['comparison']=process_cpu.compare(cpu['before'],cpu['after'])
            except Exception as error:cpu['after_error']=str(error)
            video['owned_process_cpu']=cpu
            if a.qc_copy:
                qc_after=qc_snapshot()
                video['quartzcore_uploads']=dict(before=qc_before,after=qc_after,
                    comparison=qc_copy_control.compare(qc_before,qc_after),gpu_copy=a.qc_copy)
            if a.codec_matrix and not getattr(self,'codec_queries_recorded',False):
                self.codec_queries_recorded=True
                try:
                    self.command('Marionette:SetContext',{'value':'content'})
                    script=(root/'codec-capabilities.js').read_text()
                    video['codec_declarations']=self.command('WebDriver:ExecuteAsyncScript',{
                        'script':script+'\nconst done=arguments[arguments.length-1]; window.fxCodecCapabilities().then(done,e=>done({error:String(e)}));',
                        'args':[],'scriptTimeout':70000,'newSandbox':False})
                except Exception as error:video['codec_declarations_error']=str(error)
            if 'after' in cpu:
                import media_images
                directory=a.run/('media-provider-'+str(len(list(a.run.glob('media-provider-*')))))
                directory.mkdir()
                try:video['media_providers']=media_images.inspect(cpu['after']['processes'],directory,
                                    root/'Frameworks' if a.staged_media else installed_frameworks)
                except Exception as error:video['media_provider_error']=str(error)
                if a.staged_media or a.installed_media:
                    try:
                        capture=subprocess.run(['/usr/bin/chonkstep-darwin-desktop','--screencapture',str(directory/'cpu-scene.ppm')],
                                               capture_output=True,text=True,timeout=5,
                                               env=dict(os.environ,XDG_RUNTIME_DIR='/private/var/run/gpu-desktop',WAYLAND_DISPLAY='wayland-0'))
                        video['cpu_scene_capture']=dict(returncode=capture.returncode,stdout=capture.stdout,stderr=capture.stderr,
                                                     file=str(directory/'cpu-scene.ppm'),source='cpu-scene',input_injected=False,
                                                     timing='CPU reconstruction omits external GPU windows; not a visibility gate')
                    except Exception as error:video['cpu_scene_capture_error']=str(error)
                    try:video['renderer_capture']=(self.frame_id_capture(directory/'renderer.ppm') if a.frame_id
                                                   else self.frame_id_capture(directory/'renderer.ppm','Media') if not a.mse
                                                   else renderer_readback(directory/'renderer.ppm'))
                    except Exception as error:video['renderer_capture_error']=str(error)
            if a.frame_id_cgl and json.loads((root/'frame-id-cgl/identity.json').read_text()).get('explicit_log_flush'):
                video['frame_identity_log_flush']=self.frame_id_flush()
            if a.lifetime_control:
                try:video['lifetime_control']=self.lifetime_control(video)
                except Exception as error:video['lifetime_control_error']=str(error)
            if (a.installed_media or a.staged_media) and video.get('ended') is True and not video.get('error') and not video.get('playError'):
                self.completed_installed_videos=getattr(self,'completed_installed_videos',0)+1
            if a.frame_id_dimensions_only:self.frame_id_preflight_completed=True
            return video
    marionette.Marionette=IsolatedClient
    if not a.without_trace:
        os.environ.update(SFFX_DIAGNOSTIC_OBSERVER=str(library),
                          SF_MEDIA_CADENCE_TRACE='1',SF_MEDIA_CADENCE_SECONDS='120')
    else:
        os.environ.pop('SFFX_DIAGNOSTIC_OBSERVER',None)
        os.environ.pop('SF_MEDIA_CADENCE_TRACE',None)
    if a.mse:
        observers=[root/'lib/SFHALPCMTrace.dylib',root/'lib/SFFirefoxMediaObserver.dylib']
        if a.view_state:observers.append(root/'lib/SFFirefoxViewState.dylib')
        if not all(p.is_file() for p in observers):raise ValueError('MSE audio boundary observers missing')
        existing=os.environ.get('SFFX_DIAGNOSTIC_OBSERVER')
        os.environ['SFFX_DIAGNOSTIC_OBSERVER']=':'.join(([existing] if existing else [])+list(map(str,observers)))
        os.environ.update(SF_HAL_PCM_AUDIT='1',SF_HAL_PCM_TRACE='1',
                          SF_FIREFOX_MEDIA_TRACE='1',SF_FIREFOX_MEDIA_TRACE_SECONDS='50')
    if a.view_state:os.environ['SF_FIREFOX_VIEW_STATE']='1'
    else:os.environ.pop('SF_FIREFOX_VIEW_STATE',None)
    runtime=Path('/usr/local/lib/siliconforest/firefox/160-system1')
    # Confirm the installed launcher supports the isolated-profile hook. A
    # missing hook is a refusal, never a claim that a trace was installed.
    launcher=Path('/Applications/Firefox.app/Contents/MacOS/firefox-sf')
    if not launcher.is_file() or 'SFFX_DIAGNOSTIC_OBSERVER' not in launcher.read_text():
        raise ValueError('Installed launcher lacks isolated diagnostic hook')
    manifest=json.loads((runtime/'release.json').read_text())
    installed_frameworks=None
    if a.installed_media or a.youtube_1080:
        from video_selection import installed_family
        installed_frameworks=installed_family(manifest)
        if not installed_frameworks.is_dir():raise ValueError('Selected video family is absent')
    if a.installed_codec_policy and manifest.get('av1_enabled') is not False:
        raise ValueError('Installed hardware-codec policy is not selected; no browser launched')
    if a.staged_media or a.installed_media or a.youtube_1080:os.environ['SF_VT_COPY_COUNTS']='1'
    if a.timer_control:os.environ['SF_CV_TIMER']='1'
    if a.retire_pool:os.environ['SF_VT_RETIRE_POOL_AT_INVALIDATE']='1'
    else:os.environ.pop('SF_VT_RETIRE_POOL_AT_INVALIDATE',None)
    if a.graphics_override:os.environ['SF_MEDIA_GRAPHICS_OVERRIDE']=str(a.graphics_override)
    if a.frame_id_cgl:os.environ['SF_MEDIA_FRAME_CGL']=str(root/'frame-id-cgl/Frameworks')
    else:os.environ.pop('SF_MEDIA_FRAME_CGL',None)
    if a.qc_copy:
        os.environ['SF_MEDIA_QC_NV12']=str(root/'qc-nv12')
        os.environ['SF_CA_NV12_GPU_COPY']='1' if a.qc_copy=='on' else '0'
    else:os.environ.pop('SF_MEDIA_QC_NV12',None)
    if a.frame_id_geometry:os.environ['SFFX_FRAME_ID_GEOMETRY']=a.frame_id_geometry
    else:os.environ.pop('SFFX_FRAME_ID_GEOMETRY',None)
    if a.frame_id_dimensions_only:os.environ['SFFX_FRAME_ID_DIMENSIONS']='1'
    else:os.environ.pop('SFFX_FRAME_ID_DIMENSIONS',None)
    a.run.mkdir(parents=True)
    (a.run/'cadence-launch.json').write_text(json.dumps(dict(
        release=manifest,diagnostic_only=True,timing_qualified=False,
        kernel_uuid=subprocess.check_output(['sysctl','-n','kern.kernelcacheuuid'],text=True,timeout=4).strip(),
        synthetic_input=False,owner_idle_wait=False,rate_control=a.rate_control,
        staged_media=a.staged_media,frame_mode=a.frame_mode,boundary_trace=not a.without_trace,
        installed_media=a.installed_media,
        graphics_override=str(a.graphics_override) if a.graphics_override else None,
        native_compositor_disabled=a.no_native_compositor,
        cv_timer_control=a.timer_control,
        codec_matrix=a.codec_matrix,
        mse_representation_control=a.mse,
        mse_programmatic_scroll=a.mse_scroll,
        view_state_diagnostic=a.view_state,
        known_frame_id_fixture=a.frame_id,gecko_media_profile=a.profile_video,
        owned_rollout_control=a.frame_id_quiet_rollouts,
        private_frame_identity_graphics=a.frame_id_cgl,
        private_quartzcore_copy=a.qc_copy,
        qc_1080_only=a.qc_1080_only,qc_startup_settle_seconds=a.qc_settle,
        frame_identity_geometry=a.frame_id_geometry,
        frame_identity_dimensions_only=a.frame_id_dimensions_only,
        youtube=a.youtube,youtube_profile=a.youtube_profile,
        youtube_1080=a.youtube_1080,
        youtube_disable_av1=a.disable_av1,reused_media_profile=str(a.reuse_media_profile) if a.reuse_media_profile else None,
        requested_start_media=a.start_media,diagnostic_pixels=a.compare_pixels,
        installed_codec_policy=a.installed_codec_policy,av1_switchback=a.av1_switchback,
        lifetime_control=a.lifetime_control,retire_pool=a.retire_pool,
        marionette_port=port,trace_library=str(library)),indent=2)+'\n')
    # The shared runner requires a nonexistent destination.
    destination=a.run/'browser'
    argv=[str(tools/'remote-run.py'),'--bundle','/Applications/Firefox.app',
          '--executable','firefox-sf','--run',str(destination),'--seconds','120',
          '--quiet-trace','--measure-all-videos','--page-load-strategy','none',
          '--sample-path','/private/var/db/sffx/probes-v3/sf-sample',
          '--expect-appkit-prefix','/System/Library/Frameworks/AppKit.framework',
          '--pref','media.autoplay.default='+('1' if a.youtube else '0'),'--pref','marionette.port='+str(port),
          '--pref','termsofuse.bypassNotification=true']
    argv+=['--vt-frame-mode',a.frame_mode]
    if a.frame_id_quiet_rollouts:
        argv+=['--pref','nimbus.rollouts.enabled=false','--pref','app.shield.optoutstudies.enabled=false']
    if a.youtube_1080 and not a.installed_codec_policy:argv+=['--pref','media.av1.enabled='+('false' if a.disable_av1 else 'true')]
    if a.reuse_media_profile:argv+=['--reuse-media-profile',str(a.reuse_media_profile)]
    if (a.installed_media or a.youtube_1080 or a.staged_media) and not (a.frame_id_cgl or a.graphics_override or a.qc_copy):argv+=['--expect-system-gpu']
    if a.no_native_compositor:argv+=['--pref','gfx.webrender.compositor=false']
    if (tools/'gpu-counters.json').is_file() and not a.youtube and not a.frame_id_cgl:
        argv+=['--gpu-transfer-spec',str(tools/'gpu-counters.json'),
               '--policy-probe-path',str(root/'bin/thread-policy-probe')]
    if a.staged_media or a.frame_id_cgl or a.qc_copy:
        bundle=a.run/'Diagnostic.app'
        wrapper=bundle/'Contents/MacOS/firefox-sf'
        wrapper.parent.mkdir(parents=True)
        wrapper.write_text('#!/usr/bin/env python3\nimport os,sys\nos.execv(sys.executable,[sys.executable,'+
                           repr(str(root/'media-launch.py'))+']+sys.argv[1:])\n')
        wrapper.chmod(0o755)
        argv[argv.index('--bundle')+1]=str(bundle)
    if a.profile_video:argv+=['--profile-video']
    clips=([( 'pattern-1080p60-h264.mp4',rate) for rate in [0.5,1,2]] if a.rate_control else
           [('pattern-1080p60-h264.mp4',1),('pattern-4k30-hevc.mp4',1)])
    if a.qc_1080_only:clips=clips[:1]
    if a.codec_matrix:
        if a.rate_control:raise ValueError('Codec and rate comparisons are separate measurements')
        names=['av1-1080p30.webm','vp9-1080p60.webm','hevc-main10-pq.mp4','vp9-profile2-pq.webm']
        selection=root/'media/codecs/selection.json'
        if selection.exists():
            names=json.loads(selection.read_text())
            allowed={'av1-1080p30.webm','vp9-1080p60.webm','hevc-main10-pq.mp4','vp9-profile2-pq.webm','av1-1080p60.webm','vp9-4k30.webm'}
            if not names or len(names)>4 or len(names)!=len(set(names)) or any(name not in allowed for name in names):
                raise ValueError('Invalid frozen codec selection')
        if a.av1_switchback:names=['av1-1080p60.webm']
        clips=[(name,1) for name in names]
    if a.mse:clips=[]
    if a.frame_id:clips=[('frame-id-1080p60.mp4',1)]
    for clip,rate in clips:
        source=root/'media/codecs'/clip if a.codec_matrix else Path('/private/var/db/sffx/media-highrate-v1')/clip
        if a.lifetime_control:
            source=root/'media'/clip
            if not source.is_file():raise ValueError('Missing frozen lifetime fixture: '+clip)
        if a.codec_matrix and not source.is_file():raise ValueError('Missing generated codec fixture: '+clip)
        if a.frame_id:
            import hashlib
            folder=root/'media/frame-id';source=folder/clip
            frozen=json.loads((folder/'manifest.json').read_text())
            if frozen.get('file')!=clip or hashlib.sha256(source.read_bytes()).hexdigest()!=frozen.get('sha256'):
                raise ValueError('Frozen frame-identity fixture mismatch')
            argv+=['--url',(folder/'index.html').as_uri()]
            continue
        argv+=['--url','file://'+str(tools/'video-page.html')+
               '?src='+source.as_uri()+
               '&deferred=1&deadline=25000&rate='+str(rate)]
    if a.youtube:
        # Keep only the owned public tab. No user-account or consent state is
        # imported; streaming.py refuses if the requested non-ad video is absent.
        first=argv.index('--url');argv=argv[:first]
        argv.remove('--measure-all-videos')
        argv+=['--streaming','--url','https://www.youtube.com/watch?v=aqz-KE-bpKQ']
    server=None;server_thread=None
    if a.mse:
        import functools
        from http.server import SimpleHTTPRequestHandler,ThreadingHTTPServer
        import threading
        folder=root/'media/mse'
        if not (folder/'manifest.json').is_file() or not (folder/'index.html').is_file():
            raise ValueError('Frozen local MSE inputs missing')
        handler=functools.partial(SimpleHTTPRequestHandler,directory=str(folder))
        server=ThreadingHTTPServer(('127.0.0.1',0),handler)
        server_thread=threading.Thread(target=server.serve_forever,daemon=True);server_thread.start()
        argv+=['--url','http://127.0.0.1:'+str(server.server_port)+'/index.html'+('?activity=scroll' if a.mse_scroll else '')]
    sys.argv=argv
    from contextlib import nullcontext
    if a.installed_codec_policy:
        import codec_policy_guard
        policy_scope=codec_policy_guard.observe(a.run,override=a.av1_switchback)
    else:policy_scope=nullcontext()
    try:
        try:
            with policy_scope:runpy.run_path(str(tools/'remote-run.py'),run_name='__main__')
        except SystemExit as error:
            if not (a.installed_media or a.staged_media or a.youtube_1080):raise
            # Intentional Quit makes the shared survival-oriented runner return1.
            # Preserve its raw status/verdict and admit only via the exact gate.
            (a.run/'shared-runner-status.json').write_text(json.dumps(dict(returncode=error.code,
                reason='Survival-oriented runner; intentional Quit qualified separately'))+'\n')
            if error.code not in [0,1]:raise
    finally:
        if (a.installed_media or a.youtube_1080) and destination.is_dir():
            import audio_logs
            rows=audio_logs.collect(destination,destination/'home')
            (destination/'native-logs.json').write_text(json.dumps(rows,indent=2)+'\n')
        if server:
            server.shutdown();server.server_close();server_thread.join(timeout=2)


if __name__=='__main__':main()
