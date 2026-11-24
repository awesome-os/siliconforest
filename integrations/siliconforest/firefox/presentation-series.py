#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""One round of actual-installed scroll/CSS/1080p60 presentation.

The host releases GUI/core/bench between rounds. One explicitly owned profile
may be reused only after the previous natural exit and complete family cleanup.
No quiet-profile service prefs, private graphics or compositor policy changes.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import secrets
import signal
import subprocess
import time
from marionette import Marionette
from owner_guard import OwnerInputGuard
from presentation_seat import _request, functional_qa, target_window
from compositor_stats import flush_command
from provider_images import inspect


def pacing(text, window):
    lines=[line for line in text.splitlines() if line.startswith('WMPACING ') and
           re.search(r'\bwindow='+str(window)+r'\b',line)]
    counts={key:0 for key in ['over_ms','commits','shown','replaced_before_shown']}
    for line in lines:
        for key in counts:
            match=re.search(r'\b'+key+r'=(\d+)\b',line)
            if not match:raise ValueError('Incomplete per-window pacing row')
            counts[key]+=int(match[1])
    return dict(counts,lines=lines,presented_fps=(counts['shown']*1000/counts['over_ms']
                if counts['over_ms'] else None),quantile_scope='Raw per-flush histograms; percentiles are not averaged.')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root',type=Path,required=True)
    ap.add_argument('--round',type=int,required=True)
    ap.add_argument('--automatic-intervals',action='store_true',
                    help='Observe 22s without stats requests; use the last full automatic interval after discarding the first')
    ap.add_argument('--settle-seconds',type=int,default=0,
                    help='Normal services remain enabled during a bounded pre-workload settle')
    args=ap.parse_args();root=args.root
    if (os.uname().sysname!='Darwin' or root.parent!=Path('/private/var/db/sffx') or
            '..' in root.parts or not 1<=args.round<=12 or not 0<=args.settle_seconds<=120):raise ValueError('Owned target round and bounded settle required')
    functional_qa();guard=OwnerInputGuard(require_idle=False)
    def check_owner():
        after=guard.check()
        if any(after[key]!=guard.before[key] for key in
               ['last_real_input_unix_ms','real_pointer_records','real_key_records']):
            raise RuntimeError('Real input overlapped presentation; discard and stop')
        return after
    run=root/('round-%02d'%args.round);run.mkdir(exist_ok=False)
    spec=importlib.util.spec_from_file_location('series_remote',root/'remote-run.py')
    helpers=importlib.util.module_from_spec(spec);spec.loader.exec_module(helpers)
    profile=root/'profile';profile.mkdir(exist_ok=True);home=root/'home';home.mkdir(exist_ok=True)
    checkpoint=root/'profile-checkpoint.json'
    if args.round>1:
        prior=json.loads(checkpoint.read_text())
        if prior.get('round')!=args.round-1 or prior.get('natural_exit')!=0 or prior.get('surviving_family'):
            raise ValueError('Prior own profile did not end naturally')
    prefs={'marionette.port':28482,'termsofuse.bypassNotification':True,
           'browser.aboutwelcome.enabled':False,'browser.preonboarding.enabled':False,
           'app.update.auto':False,'media.autoplay.default':0}
    (profile/'user.js').write_text(''.join('user_pref(%s,%s);\n'%(json.dumps(k),json.dumps(v))for k,v in prefs.items()))
    release=Path('/usr/local/lib/siliconforest/firefox/160-system1/release.json').read_text()
    manifest=json.loads(release)
    if (manifest.get('video_override')!='/usr/local/lib/siliconforest/firefox/160-system1/overrides/video-clock-0172-v1/Frameworks'
            or manifest.get('gpu_damage') or manifest.get('gpu_image_cache')):raise ValueError('Expected 0172 selected; damage/cache OFF')
    result=dict(round=args.round,complete=False,installed_outer=True,input_injected=False,
                timed_input_injected=False,pointer_input_injected=False,
                programmatic_scroll=True,manifest=manifest,phases=[],owned_profile=str(profile),test_preferences=prefs,
                owner_input_before=guard.before,automatic_intervals=args.automatic_intervals,
                normal_services_settle_seconds=args.settle_seconds)
    owned={};process=None;client=None
    def save():(run/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    def identities():
        current=helpers.process_identities()
        owned.update({pid:current[pid][1]for pid in helpers.descendants(process.pid,current)if pid in current})
        return current
    def wait(seconds):
        end=time.monotonic()+seconds
        while time.monotonic()<end:check_owner();time.sleep(min(.05,max(0,end-time.monotonic())))
    def script(value,args=None):
        check_owner();return client.command('WebDriver:ExecuteScript',dict(script=value,args=args or [],newSandbox=False))
    def cpu():
        identities();return subprocess.check_output(['ps','-p',','.join(map(str,sorted(owned))),'-o','pid,ppid,time,rss,stat,command'],text=True)
    try:
        env=os.environ.copy();env.update(HOME=str(home),CFFIXED_USER_HOME=str(home),MOZ_NO_REMOTE='1',
                MOZ_CRASHREPORTER_DISABLE='1',MOZ_REMOTE_ALLOW_SYSTEM_ACCESS='1')
        for key in ['SFFX_GPU_DAMAGE','SFFX_GPU_IMAGE_CACHE','SF_CV_TIMER',
                    'SFFX_FRAME_ID_GEOMETRY','SFFX_FRAME_ID_DIMENSIONS',
                    'SFFX_GPU_RENDER_THREAD','SF_CA_FRAME_TRACE']:
            env.pop(key,None)
        with(run/'launcher.log').open('w')as log:
            process=subprocess.Popen(['/Applications/Firefox.app/Contents/MacOS/firefox-sf','--no-remote',
                '--profile',str(profile),'--marionette','about:blank'],env=env,stdout=log,stderr=subprocess.STDOUT)
        result['wrapper_pid']=process.pid;identities();result['owned_start_identities']=owned;save()
        end=time.monotonic()+30
        while time.monotonic()<end:
            identities();check_owner()
            if process.poll() is not None:raise RuntimeError('Wrapper exited during startup')
            try:client=Marionette(28482,timeout=1);break
            except (OSError,EOFError):wait(.2)
        if client is None:raise RuntimeError('Marionette startup deadline')
        client.socket.settimeout(20);session=client.start('none');parent=session['capabilities']['moz:processID']
        identities()
        if parent not in owned:raise RuntimeError('Parent outside owned family')
        result['parent_pid']=parent
        save();wait(args.settle_seconds)
        for case in ['scroll','css','video']:
            client.command('Marionette:SetContext',{'value':'content'})
            url=(root/'presentation-series.html').as_uri()+'?case='+case
            if args.automatic_intervals:url+='&automatic=1'
            client.command('WebDriver:Navigate',{'url':url})
            end=time.monotonic()+12
            while not script('return location.href===arguments[0] && typeof fxBenchStart==="function";',[url]):
                if time.monotonic()>end:raise RuntimeError('Fixture readiness deadline')
                wait(.1)
            marker='SF-FX 0172 '+case+' '+secrets.token_hex(6)
            script('document.title=arguments[0];return true;',[marker]);wait(.4)
            inject=Path('/private/var/run/gpu-desktop/inject');name='sffx-series-'+secrets.token_hex(6)
            _request(inject,'dump '+name);dump=(inject/(name+'.txt')).read_text()
            matches=[line for line in dump.splitlines()if marker in line and 'mapped=true' in line]
            if len(matches)!=1:raise RuntimeError('Unique owned window absent')
            window=int(re.match(r'window (\d+)',matches[0])[1])
            target_window(window)
            if case=='scroll':
                # The desktop does not wake its display on virtual pointer
                # motion. One targeted modifier tap precedes all timing.
                result['display_wake']=_request(inject,'target %d\nkey e1 down\nkey e1 up'%window)
                result['input_injected']=True
            started=client.command('WebDriver:ExecuteAsyncScript',dict(script='''const done=arguments[arguments.length-1];
                fxBenchStart().then(done,e=>done({error:String(e)}));''',args=[],newSandbox=False,scriptTimeout=12000))
            if started.get('error'):raise RuntimeError(started['error'])
            wait(2 if case=='video' else (3 if args.round>1 else 5))
            row=dict(case=case,window_id=window,start=started,programmatic_scroll=case=='scroll');result['phases'].append(row)
            row['before']=script('return fxBenchState();')
            row['cpu_before']=cpu()
            if not args.automatic_intervals:flush_command('stats')
            log=Path('/var/log/desktop-session.log');info=log.stat()
            began=time.monotonic();row['begin_monotonic']=began
            wait(22 if args.automatic_intervals else 10)
            if not args.automatic_intervals:flush_command('stats')
            row['end_monotonic']=time.monotonic();row['cpu_after']=cpu()
            if log.stat().st_ino!=info.st_ino:raise RuntimeError('Desktop log rotated')
            with log.open('rb')as stream:stream.seek(info.st_size);raw=stream.read().decode(errors='replace')
            (run/(case+'-desktop.log')).write_text(raw);row['presentation']=pacing(raw,window)
            if args.automatic_intervals:
                row['automatic_all']=row['presentation']
                lines=row['automatic_all']['lines']
                if len(lines)<2:raise RuntimeError('Need two automatic intervals; first overlaps warmup')
                row['automatic_selected_rule']='Last automatic interval after discarding the first, chosen in advance; no stats/dump/input during 22s observation.'
                row['presentation']=pacing(lines[-1],window)
            row['after']=script('return fxBenchState();');script('return fxBenchStop();')
            row['active_interval']=row['before']['visibility']=='visible' and row['after']['visibility']=='visible'
            if case=='scroll':row['active_interval'] &= row['after']['active'] and row['after']['distance']-row['before']['distance']>10000
            if case=='video':
                a,b=row['before']['video'],row['after']['video'];row['video_frames']=b['decoded']-a['decoded'];row['video_drops']=b['dropped']-a['dropped']
                row['active_interval'] &= not b['paused'] and not b['ended'] and not b['error'] and row['video_frames']>500
            count=row['presentation']['over_ms'];row['measurement_complete']=row['active_interval'] and 9000<=count<=12000
            row['fps_59_5']=row['measurement_complete'] and row['presentation']['presented_fps']>=59.5
            if args.round==1:
                from renderer_capture import capture
                row['capture_attempts']=[]
                for attempt in range(3):
                    script('''for(const a of document.getAnimations())a.pause();
                      let b=document.querySelector('#capture-badge');if(!b){b=document.createElement('div');b.id='capture-badge';document.body.append(b);}
                      b.style.cssText='position:fixed;top:0;left:0;width:12px;height:12px;z-index:99999;background:'+arguments[0];return true;''',
                      ['#e43743' if attempt%2 else '#29a876'])
                    wait(.2)
                    try:
                        row['renderer']=capture(run/(case+'-renderer.ppm'))
                        row['capture_attempts'].append(dict(attempt=attempt,complete=True));break
                    except RuntimeError as error:
                        row['capture_attempts'].append(dict(attempt=attempt,error=repr(error)))
            save()
        result['support']=client.support()
        graphics=result['support'].get('graphics',{})
        if graphics.get('windowLayerManagerType')!='WebRender' or graphics.get('numAcceleratedWindows',0)<1:
            raise RuntimeError('Hardware WebRender not active')
        identities();result['providers']=inspect('/private/var/db/sffx/probes-v3/sf-sample',run/'providers.json',owned,parent,
                '/System/Library/Frameworks/AppKit.framework',system_gpu=True)
        if not result['providers']['complete']:raise RuntimeError('Actual graphics providers differ')
        result['actual_video_clock']=[]
        for role in result['providers']['processes']:
            text=(run/('provider-images-%d.txt'%role['pid'])).read_text()
            lines=[l.strip() for l in text.splitlines() if re.search(r'\sCoreVideo\s+<',l)]
            result['actual_video_clock'].append(dict(pid=role['pid'],role=role['role'],images=lines))
            if len(lines)!=1 or '<9FB0E7EB-4CFA-35AB-B18F-2C9F21B5AFDE>' not in lines[0] or not lines[0].endswith(
                    manifest['video_override']+'/CoreVideo.framework/Versions/A/CoreVideo'):
                raise RuntimeError('Selected 0172 CoreVideo was not used in '+role['role'])
        from lifecycle_observer import native_menu
        result['quit_reply']=native_menu(parent,'quit');end=time.monotonic()+12
        while time.monotonic()<end and process.poll() is None:identities();wait(.1)
        current=identities();result['natural_exit']=process.poll();result['surviving_family']={str(p):s for p,s in owned.items()if p in current and current[p][1]==s}
        result['complete']=result['natural_exit']==0 and not result['surviving_family'] and all(r['measurement_complete']for r in result['phases'])
        checkpoint.write_text(json.dumps(dict(round=args.round,natural_exit=result['natural_exit'],surviving_family=result['surviving_family']))+'\n')
    except Exception as error:result['error']=repr(error)
    finally:
        result['cleanup_signals']=[]
        if client is not None:client.close()
        if process is not None:
            current=identities()
            for sig in [signal.SIGTERM,signal.SIGKILL]:
                for pid,start in sorted(owned.items(),reverse=True):
                    if pid in current and current[pid][1]==start:
                        try:os.kill(pid,sig);result['cleanup_signals'].append([pid,int(sig)])
                        except ProcessLookupError:pass
                time.sleep(.3);current=helpers.process_identities()
            process.wait(timeout=5)
        try:result['owner_input_after']=check_owner()
        except (OSError,RuntimeError)as error:result['qualification_invalidated']=repr(error);result['complete']=False
        save()
    return 0 if result['complete'] else 1


if __name__=='__main__':raise SystemExit(main())
