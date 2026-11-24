# SPDX-License-Identifier: MIT
"""Paired-composition workload measurements on an owned installed Firefox.

Ten-second phases retain per-window WMPACING, family CPU and owner interference.
Captures and warm-up lie outside each interval. No compositor policy is changed.
"""
import json
from pathlib import Path
from urllib.parse import urlsplit
import re
import time


def cpu_times(text):
    rows = {}
    for line in text.splitlines()[1:]:
        fields = line.split(None, 5)
        if len(fields) >= 6:
            rows[int(fields[0])] = sum(float(n)*60**i for i,n in enumerate(fields[2].split(':')[::-1]))
    return rows


def measure(client, snapshot, directory, deadline):
    from presentation_seat import admit, _request
    from renderer_capture import capture
    from compositor_stats import flush_command
    inject = Path('/private/var/run/gpu-desktop/inject')
    log = Path('/var/log/desktop-session.log')
    output = directory / 'composition-workloads.json'
    result = dict(complete=False, phases=[], workload_seconds=10,
                  performance_comparison_only=True, owner_policy_changed=False)
    policy = Path('/private/var/db/desktop-state/chonkstep/policy')
    result['compositor_policy_before'] = policy.read_text() if policy.exists() else None
    def script(source):
        return client.command('WebDriver:ExecuteScript', dict(script=source, args=[], newSandbox=False))
    client.command('Marionette:SetContext', {'value': 'chrome'})
    result['native_compositor'] = script('return Services.prefs.getBoolPref("gfx.webrender.compositor");')
    pages = [('scroll', Path(__file__).with_name('composition-scroll.html').as_uri()),
             ('css', Path(__file__).with_name('composition-css.html').as_uri()),
             ('video', Path(__file__).with_name('composition-video.html').as_uri()),
             ('cnn-idle', 'https://www.cnn.com/')]
    for name, url in pages:
        if time.monotonic() > deadline-23:
            raise RuntimeError('Insufficient owned-run time for another workload')
        client.command('Marionette:SetContext', {'value': 'content'})
        client.command('WebDriver:Navigate', {'url': url})
        time.sleep(8 if name=='cnn-idle' else 2)
        row = dict(name=name, url=url)
        result['phases'].append(row)
        try:
            # Inspect a stable picture outside the timed interval. Restore the
            # exact running media/animations before warm-up and measurement.
            row['capture_pause'] = script('''window.__sffxCapturePause={
              animations:document.getAnimations().filter(a=>a.playState==='running'),
              videos:[...document.querySelectorAll('video')].filter(v=>!v.paused)};
              for(const a of window.__sffxCapturePause.animations)a.pause();
              for(const v of window.__sffxCapturePause.videos)v.pause();
              return {animations:window.__sffxCapturePause.animations.length,
                videos:window.__sffxCapturePause.videos.length};''')
            admission = admit(client, directory/(name+'-admission.json'), fixture=False,
                              verify_renderer=False)
            # Restoring the temporary admission title also changes the scene.
            # Keep a bounded record of capture failures rather than mistaking
            # the compositor's two-second current-scene timeout for no window.
            row['capture_attempts'] = []
            for attempt in range(3):
                time.sleep(.3)
                try:
                    renderer = capture(directory/(name+'-admission-%d.ppm' % attempt))
                    row['capture_attempts'].append(dict(attempt=attempt,complete=True))
                    row['renderer'] = renderer
                    admission['renderer'] = renderer
                    break
                except RuntimeError as error:
                    row['capture_attempts'].append(dict(attempt=attempt,error=repr(error)))
                    if attempt==2:
                        raise
            row['window_id'] = admission['window_id']
            client.command('Marionette:SetContext', {'value': 'chrome'})
            script('Services.wm.getMostRecentWindow("navigator:browser").gBrowser.selectedBrowser.focus();return true;')
            client.command('Marionette:SetContext', {'value': 'content'})
            if name=='video':
                state = script('''const v=document.querySelector('video');v.muted=true;v.play();return {
                  width:v.videoWidth,height:v.videoHeight,ready:v.readyState,paused:v.paused,error:v.error?.code||null};''')
                row['video_admission'] = state
                if state['width']!=1920 or state['height']!=1080 or state['error']:
                    raise RuntimeError('Actual 1080p video not admitted')
            row['capture_restored'] = script('''const state=window.__sffxCapturePause;
              if(!state)return false;for(const a of state.animations)a.play();
              for(const v of state.videos)v.play().catch(()=>{});
              delete window.__sffxCapturePause;return true;''')
            x,y,width,height=admission['frame']
            px=max(1,min(2558,x+width//2));py=max(1,min(1598,y+min(height-100,400)))
            row['pointer_admission'] = _request(inject,'abs %d %d\nwait 250' %
                (round(px*65535/2559),round(py*65535/1599)))
            time.sleep(2)
            media = '''const v=document.querySelector('video'),q=v?.getVideoPlaybackQuality();
              return {sampleWall:performance.now(),url:location.href,title:document.title,visibility:document.visibilityState,scrollY,
                video:v?{time:v.currentTime,width:v.videoWidth,height:v.videoHeight,paused:v.paused,
                  decoded:q?.totalVideoFrames,dropped:q?.droppedVideoFrames,error:v.error?.code||null}:null};'''
            row['page_before'] = script(media)
            flush_command('stats')
            info=log.stat();offset=info.st_size
            row['cpu_before'] = snapshot()
            wall=time.monotonic()
            # Every pointer/key stream has the explicit virtual-seat window
            # target admitted above. Waiting phases inject no pointer motion.
            commands='\n'.join(['wheel -1','wait 33']*300) if name=='scroll' else 'wait 10000'
            row['input_status'] = _request(inject, commands, timeout=22)
            row['wall_seconds'] = time.monotonic()-wall
            row['cpu_after'] = snapshot()
            flush_command('stats')
            if log.stat().st_ino != info.st_ino or log.stat().st_size < offset:
                raise RuntimeError('Compositor log rotated during phase')
            with log.open('rb')as stream:
                stream.seek(offset);raw=stream.read().decode(errors='replace')
            (directory/(name+'-desktop.log')).write_text(raw)
            lines=[line for line in raw.splitlines() if line.startswith('WMPACING ') and
                   re.search(r'\bwindow='+str(row['window_id'])+r'\b',line)]
            row['wmpacing'] = lines
            counters={key:0 for key in ['over_ms','commits','shown','replaced_before_shown']}
            for line in lines:
                for key in counters:
                    match=re.search(r'\b'+key+r'=(\d+)',line)
                    if not match:raise RuntimeError('Incomplete WMPACING row')
                    counters[key]+=int(match[1])
            row['presentation'] = counters
            if counters['over_ms']>0:
                row['presentation'].update(presented_fps=counters['shown']*1000/counters['over_ms'],
                    commits_per_second=counters['commits']*1000/counters['over_ms'])
            before,after=cpu_times(row['cpu_before']),cpu_times(row['cpu_after'])
            row['family_cpu_seconds'] = sum(max(0,n-before.get(pid,0))for pid,n in after.items())
            row['family_cpu_percent_one_core'] = row['family_cpu_seconds']/row['wall_seconds']*100
            row['exited_processes'] = sorted(set(before)-set(after))
            row['page_after'] = script(media)
            row['owner_input_overlap'] = bool(re.search(r'owner_seen_active=1\b',row['input_status']))
            row['timing_qualified'] = not row['owner_input_overlap'] and not row['exited_processes'] and admission['focused']
            same_page = row['page_after']['url']==url or (name=='cnn-idle' and (urlsplit(row['page_after']['url']).hostname or '').endswith('.cnn.com'))
            row['complete'] = bool(lines) and 9000<=counters['over_ms']<=13000 and same_page
            if name=='scroll':row['complete'] &= row['page_after']['scrollY']>row['page_before']['scrollY']
            if name=='video':
                a,b=row['page_before']['video'],row['page_after']['video']
                row['video_frames'] = b['decoded']-a['decoded']
                row['video_dropped'] = b['dropped']-a['dropped']
                # DOM observations bracket the stats/CPU probes as well as the
                # ten-second input wait. Report their actual clock interval.
                row['video_observed_seconds'] = (row['page_after']['sampleWall']-
                                                  row['page_before']['sampleWall'])/1000
                row['complete'] &= row['video_frames']>500 and not b['paused'] and not b['error']
        except Exception as error:
            row['error'] = repr(error)
            row['complete'] = False
        finally:
            try:
                client.command('Marionette:SetContext', {'value': 'content'})
                script('''const state=window.__sffxCapturePause;if(state){
                  for(const a of state.animations)a.play();for(const v of state.videos)v.play().catch(()=>{});
                  delete window.__sffxCapturePause;}return true;''')
            except Exception:
                pass
            output.write_text(json.dumps(result,indent=2)+'\n')
    result['complete'] = len(result['phases'])==4 and all(p.get('complete')for p in result['phases'])
    result['compositor_policy_after'] = policy.read_text() if policy.exists() else None
    result['policy_unchanged'] = result['compositor_policy_before']==result['compositor_policy_after']
    result['timing_qualified'] = (result['complete'] and result['policy_unchanged'] and
                                  all(p.get('timing_qualified')for p in result['phases']))
    output.write_text(json.dumps(result,indent=2)+'\n')
    return result
